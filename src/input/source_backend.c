// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <vlc_common.h>
#include <vlc_source_backend.h>
#include <vlc_atomic.h>
#include <vlc_threads.h>
#include <stdatomic.h>

#define MAX_ACTIVE 128u
#define MAX_SOURCE_URI 12000u
#define MAX_PROXY_URI 1024u

struct vlc_source_lease {
    vlc_atomic_rc_t refs;
    atomic_bool cancelled;
    atomic_bool attached;
    vlc_mutex_t cancel_lock;
    vlc_cond_t cancel_ended;
    unsigned long cancel_owner;
    bool cancel_finished;
    struct vlc_source_lease_operations ops;
    void *opaque;
};
struct transport {
    struct transport *next;
    vlc_input_policy_t *policy;
    vlc_source_lease_t *lease;
};
struct vlc_source_backend {
    vlc_atomic_rc_t refs;
    vlc_mutex_t lock;
    bool closed;
    size_t active;
    vlc_source_job_t *head;
    struct transport *issued;
    size_t issued_count;
    size_t transport_count;
    /* Transports that finish() is releasing outside the lock. Close() waits
     * for them, so that every transport is revoked once it returns. */
    size_t releasing;
    vlc_cond_t released;
    struct vlc_source_backend_operations ops;
    void *opaque;
};
struct vlc_source_job {
    vlc_atomic_rc_t refs;
    vlc_source_backend_t *backend;
    vlc_source_job_t *next, *previous;
    bool started, terminal, cancelled;
    struct vlc_source_request request;
    struct vlc_source_result result;
    struct transport *transport;
    vlc_source_ended_cb ended;
    void *userdata;
};

vlc_source_lease_t *vlc_source_lease_New(const struct vlc_source_lease_operations *ops, void *opaque)
{
    if (ops == NULL || ops->cancel == NULL) return NULL;
    vlc_source_lease_t *lease = malloc(sizeof(*lease));
    if (lease == NULL) return NULL;
    vlc_atomic_rc_init(&lease->refs);
    atomic_init(&lease->cancelled, false); atomic_init(&lease->attached, false);
    vlc_mutex_init(&lease->cancel_lock); vlc_cond_init(&lease->cancel_ended);
    lease->cancel_owner = 0; lease->cancel_finished = false;
    lease->ops = *ops; lease->opaque = opaque;
    return lease;
}
vlc_source_lease_t *vlc_source_lease_Hold(vlc_source_lease_t *lease)
{ vlc_atomic_rc_inc(&lease->refs); return lease; }
static void cancel_lease(vlc_source_lease_t *lease)
{
    const unsigned long self = vlc_thread_id();
    vlc_mutex_lock(&lease->cancel_lock);
    if (atomic_load_explicit(&lease->cancelled, memory_order_acquire)) {
        while (!lease->cancel_finished && lease->cancel_owner != self)
            vlc_cond_wait(&lease->cancel_ended, &lease->cancel_lock);
        vlc_mutex_unlock(&lease->cancel_lock);
        return;
    }
    atomic_store_explicit(&lease->cancelled, true, memory_order_release);
    lease->cancel_owner = self;
    vlc_mutex_unlock(&lease->cancel_lock);
    lease->ops.cancel(lease->opaque);
    vlc_mutex_lock(&lease->cancel_lock);
    lease->cancel_finished = true; lease->cancel_owner = 0;
    vlc_cond_broadcast(&lease->cancel_ended);
    vlc_mutex_unlock(&lease->cancel_lock);
}
void vlc_source_lease_Cancel(vlc_source_lease_t *lease)
{
    const int cancel_state = vlc_savecancel();
    vlc_source_lease_Hold(lease);
    cancel_lease(lease);
    vlc_source_lease_Release(lease);
    vlc_restorecancel(cancel_state);
}
void vlc_source_lease_Release(vlc_source_lease_t *lease)
{
    uintptr_t count = atomic_load_explicit(&lease->refs.refs, memory_order_acquire);
    for (;;) {
        vlc_assert(count != 0);
        if (count == 1) break;
        if (atomic_compare_exchange_weak_explicit(&lease->refs.refs, &count, count - 1,
                                                memory_order_acq_rel, memory_order_acquire)) return;
    }
    // Keep the last reference through callback reentry. Never revive zero refs.
    const int cancel_state = vlc_savecancel();
    cancel_lease(lease);
    if (vlc_atomic_rc_dec(&lease->refs)) {
        if (lease->ops.destroy != NULL) lease->ops.destroy(lease->opaque);
        free(lease);
    }
    vlc_restorecancel(cancel_state);
}
vlc_source_backend_t *vlc_source_backend_New(const struct vlc_source_backend_operations *ops, void *opaque)
{
    if (ops == NULL || ops->dispatch == NULL) return NULL;
    vlc_source_backend_t *backend = calloc(1, sizeof(*backend));
    if (backend == NULL) return NULL;
    vlc_atomic_rc_init(&backend->refs); vlc_mutex_init(&backend->lock);
    vlc_cond_init(&backend->released);
    backend->ops = *ops; backend->opaque = opaque;
    return backend;
}
vlc_source_backend_t *vlc_source_backend_Hold(vlc_source_backend_t *backend)
{ vlc_atomic_rc_inc(&backend->refs); return backend; }
static void release_transports(struct transport *transport)
{
    while (transport != NULL) {
        struct transport *next = transport->next;
        vlc_input_policy_Revoke(transport->policy); vlc_source_lease_Cancel(transport->lease);
        vlc_input_policy_Release(transport->policy); vlc_source_lease_Release(transport->lease);
        free(transport); transport = next;
    }
}
void vlc_source_backend_Release(vlc_source_backend_t *backend)
{
    if (!vlc_atomic_rc_dec(&backend->refs)) return;
    vlc_assert(backend->head == NULL);
    // Keep a cleanup reference while native lease callbacks can re-enter Close.
    vlc_atomic_rc_init(&backend->refs);
    vlc_mutex_lock(&backend->lock);
    backend->closed = true;
    struct transport *issued = backend->issued; backend->issued = NULL;
    backend->issued_count = 0; backend->transport_count = 0;
    vlc_mutex_unlock(&backend->lock);
    release_transports(issued);
    if (!vlc_atomic_rc_dec(&backend->refs)) return;
    if (backend->ops.destroy != NULL) backend->ops.destroy(backend->opaque);
    free(backend);
}
bool vlc_source_backend_IsClosed(vlc_source_backend_t *backend)
{
    vlc_mutex_lock(&backend->lock);
    const bool closed = backend->closed;
    vlc_mutex_unlock(&backend->lock);
    return closed;
}
vlc_source_job_t *vlc_source_job_Hold(vlc_source_job_t *job)
{ vlc_atomic_rc_inc(&job->refs); return job; }
static void release_result(struct vlc_source_result *result)
{
    free((char *)result->proxy_uri);
    if (result->policy != NULL) vlc_input_policy_Release(result->policy);
    if (result->lease != NULL) vlc_source_lease_Release(result->lease);
    memset(result, 0, sizeof(*result));
}
void vlc_source_job_Release(vlc_source_job_t *job)
{
    if (!vlc_atomic_rc_dec(&job->refs)) return;
    vlc_source_backend_t *backend = job->backend;
    free((char *)job->request.source_uri); release_result(&job->result); free(job);
    vlc_source_backend_Release(backend);
}
vlc_source_job_t *vlc_source_job_New(vlc_source_backend_t *backend, const struct vlc_source_request *request,
                                   vlc_source_ended_cb ended, void *userdata)
{
    if (backend == NULL || request == NULL || ended == NULL || request->source_uri == NULL
        || request->input_generation == 0 || strncmp(request->source_uri, "hakone-addon-source:v1:", sizeof("hakone-addon-source:v1:") - 1) != 0
        || strnlen(request->source_uri, MAX_SOURCE_URI + 1) > MAX_SOURCE_URI) return NULL;
    vlc_source_job_t *job = calloc(1, sizeof(*job));
    if (job == NULL) return NULL;
    vlc_atomic_rc_init(&job->refs); job->backend = vlc_source_backend_Hold(backend);
    job->request.source_uri = strdup(request->source_uri);
    job->request.input_generation = request->input_generation;
    job->ended = ended; job->userdata = userdata;
    if (job->request.source_uri == NULL) { vlc_source_job_Release(job); return NULL; }
    return job;
}
const struct vlc_source_request *vlc_source_job_GetRequest(const vlc_source_job_t *job)
{ return &job->request; }
const struct vlc_source_result *vlc_source_job_GetResult(vlc_source_job_t *job)
{
    vlc_mutex_lock(&job->backend->lock);
    const struct vlc_source_result *result = job->terminal && !job->backend->closed ? &job->result : NULL;
    vlc_mutex_unlock(&job->backend->lock);
    return result;
}
bool vlc_source_job_IsPending(vlc_source_job_t *job)
{
    vlc_mutex_lock(&job->backend->lock);
    const bool pending = job->started && !job->terminal && !job->backend->closed;
    vlc_mutex_unlock(&job->backend->lock);
    return pending;
}
bool vlc_source_job_AttachTransport(vlc_source_job_t *job, vlc_input_policy_t *policy, vlc_source_lease_t *lease)
{
    if (policy == NULL || lease == NULL) return false;
    struct transport *record = malloc(sizeof(*record));
    if (record == NULL) return false;
    vlc_source_backend_t *backend = job->backend;
    vlc_mutex_lock(&backend->lock);
    struct transport *discarded = NULL, **previous = &backend->issued;
    while (*previous != NULL) {
        struct transport *current = *previous;
        if (atomic_load_explicit(&current->lease->cancelled, memory_order_acquire)) {
            *previous = current->next; current->next = discarded; discarded = current; --backend->issued_count; --backend->transport_count;
        } else previous = &current->next;
    }
    bool expected = false;
    const bool accepted = job->started && !job->terminal && !backend->closed && job->transport == NULL
        && backend->transport_count < MAX_ACTIVE && !atomic_load_explicit(&lease->cancelled, memory_order_acquire)
        && atomic_compare_exchange_strong_explicit(&lease->attached, &expected, true, memory_order_acq_rel, memory_order_relaxed);
    if (accepted) {
        record->next = NULL; record->policy = vlc_input_policy_Hold(policy); record->lease = vlc_source_lease_Hold(lease);
        job->transport = record; ++backend->transport_count;
    }
    vlc_mutex_unlock(&backend->lock);
    release_transports(discarded); if (!accepted) free(record);
    return accepted;
}
static bool finish(vlc_source_job_t *job, int status, struct vlc_source_result *result, bool cancel)
{
    vlc_source_backend_t *backend = job->backend;
    vlc_mutex_lock(&backend->lock);
    if (!job->started || job->terminal) {
        vlc_mutex_unlock(&backend->lock); release_result(result); return false;
    }
    if (backend->closed) { status = -EINTR; cancel = true; }
    struct transport *transport = job->transport; job->transport = NULL;
    if (status == VLC_SUCCESS && (transport == NULL || transport->policy != result->policy || transport->lease != result->lease))
        status = VLC_EINVAL;
    if (status == VLC_SUCCESS) {
        transport->next = backend->issued; backend->issued = transport; ++backend->issued_count; transport = NULL;
    }
    else if (transport != NULL) --backend->transport_count;
    job->terminal = true;
    if (job->previous != NULL) job->previous->next = job->next; else backend->head = job->next;
    if (job->next != NULL) job->next->previous = job->previous;
    --backend->active;
    if (status == VLC_SUCCESS) job->result = *result;
    if (transport != NULL) ++backend->releasing;
    vlc_mutex_unlock(&backend->lock);
    if (status != VLC_SUCCESS) release_result(result);
    if (transport != NULL) {
        release_transports(transport);
        vlc_mutex_lock(&backend->lock);
        if (--backend->releasing == 0) vlc_cond_broadcast(&backend->released);
        vlc_mutex_unlock(&backend->lock);
    }
    job->ended(job, status, job->userdata);
    if (cancel && backend->ops.cancel != NULL) backend->ops.cancel(backend->opaque, job);
    vlc_source_job_Release(job); // Active-list reference.
    return true;
}
static void revoke_completed(vlc_source_job_t *job)
{
    vlc_mutex_lock(&job->backend->lock);
    vlc_input_policy_t *policy = vlc_input_policy_Hold(job->result.policy);
    vlc_source_lease_t *lease = job->result.lease;
    if (lease != NULL) vlc_source_lease_Hold(lease);
    vlc_mutex_unlock(&job->backend->lock);
    vlc_input_policy_Revoke(policy);
    if (lease != NULL) { vlc_source_lease_Cancel(lease); vlc_source_lease_Release(lease); }
    vlc_input_policy_Release(policy);
}
bool vlc_source_job_Cancel(vlc_source_job_t *job)
{
    vlc_source_job_Hold(job);
    vlc_mutex_lock(&job->backend->lock);
    if (job->terminal) {
        vlc_mutex_unlock(&job->backend->lock);
        revoke_completed(job);
        vlc_source_job_Release(job);
        return false;
    }
    if (!job->started) {
        const bool won = !job->cancelled; job->cancelled = true;
        vlc_mutex_unlock(&job->backend->lock); vlc_source_job_Release(job); return won;
    }
    vlc_mutex_unlock(&job->backend->lock);
    struct vlc_source_result result = {0};
    const bool won = finish(job, -EINTR, &result, true);
    if (!won) revoke_completed(job);
    vlc_source_job_Release(job); return won;
}
static bool path_character(unsigned char c)
{ return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; }
static bool proxy_uri(const char *uri)
{
    if (uri == NULL || strnlen(uri, MAX_PROXY_URI + 1) > MAX_PROXY_URI || strncmp(uri, "http://127.0.0.1:", 17)) return false;
    const char *p = uri + 17; unsigned port = 0, digits = 0;
    if (*p < '1' || *p > '9') return false;
    while (*p >= '0' && *p <= '9') {
        if (++digits > 5) return false;
        port = port * 10 + (*p++ - '0');
    }
    if (port > 65535 || *p++ != '/') return false;
    unsigned token = 0;
    while (path_character((unsigned char)*p)) { ++p; if (++token > 128) return false; }
    if (token == 0 || *p++ != '/' || *p == '\0' || *p == '/') return false;
    unsigned length = 0, segments = 1;
    for (; *p != '\0'; ++p) {
        if (++length > 512) return false;
        if (*p == '/') { if (++segments > 4 || p[1] == '/' || p[1] == '\0') return false; }
        else if (!path_character((unsigned char)*p)) return false;
    }
    return true;
}
bool vlc_source_job_Complete(vlc_source_job_t *job, int status, const struct vlc_source_result *result)
{
    vlc_source_job_Hold(job);
    struct vlc_source_result copy = {0};
    if (status == VLC_SUCCESS) {
        if (result == NULL || result->policy == NULL || result->lease == NULL || !proxy_uri(result->proxy_uri))
            status = VLC_EINVAL;
        else {
            copy.proxy_uri = strdup(result->proxy_uri);
            if (copy.proxy_uri == NULL) status = VLC_ENOMEM;
            else { copy.policy = vlc_input_policy_Hold(result->policy); copy.lease = vlc_source_lease_Hold(result->lease); }
        }
    }
    const bool won = finish(job, status, &copy, false);
    vlc_source_job_Release(job); return won;
}
int vlc_source_job_Start(vlc_source_job_t *job)
{
    vlc_source_job_Hold(job);
    vlc_source_backend_t *backend = job->backend;
    vlc_mutex_lock(&backend->lock);
    if (backend->closed || job->cancelled || job->started || backend->active >= MAX_ACTIVE) {
        const int error = backend->closed || job->cancelled ? -EINTR : VLC_EGENERIC;
        vlc_mutex_unlock(&backend->lock); vlc_source_job_Release(job); return error;
    }
    job->started = true; job->next = backend->head;
    if (backend->head != NULL) backend->head->previous = job;
    backend->head = job; ++backend->active; vlc_source_job_Hold(job);
    vlc_mutex_unlock(&backend->lock);
    const int error = backend->ops.dispatch(backend->opaque, job);
    if (error != VLC_SUCCESS) vlc_source_job_Complete(job, error, NULL);
    vlc_source_job_Release(job); return VLC_SUCCESS;
}
void vlc_source_backend_Close(vlc_source_backend_t *backend)
{
    vlc_source_backend_Hold(backend);
    vlc_mutex_lock(&backend->lock); backend->closed = true;
    struct transport *issued = backend->issued; backend->issued = NULL;
    backend->transport_count -= backend->issued_count; backend->issued_count = 0;
    vlc_mutex_unlock(&backend->lock); release_transports(issued); vlc_mutex_lock(&backend->lock);
    while (backend->head != NULL) {
        vlc_source_job_t *job = vlc_source_job_Hold(backend->head);
        vlc_mutex_unlock(&backend->lock); vlc_source_job_Cancel(job); vlc_source_job_Release(job);
        vlc_mutex_lock(&backend->lock);
    }
    /* A job that finished concurrently left the list before releasing its
     * transport. */
    while (backend->releasing > 0) vlc_cond_wait(&backend->released, &backend->lock);
    vlc_mutex_unlock(&backend->lock); vlc_source_backend_Release(backend);
}
