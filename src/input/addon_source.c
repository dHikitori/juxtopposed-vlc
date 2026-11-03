// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <vlc_common.h>
#include <vlc_atomic.h>
#include <vlc_input_item.h>
#include <vlc_input_policy.h>
#include <vlc_interrupt.h>
#include <vlc_source_owner.h>
#include <stdatomic.h>
#include <assert.h>
#include "addon_source.h"
#include "item.h"

struct source_wait {
    vlc_atomic_rc_t refs;
    vlc_mutex_t lock;
    vlc_sem_t ready;
    bool done, abandoned;
    atomic_bool interrupted;
    int status;
    struct vlc_source_result result;
};
static void discard(struct vlc_source_result *result)
{
    vlc_input_policy_Revoke(result->policy);
    if (result->lease != NULL) {
        vlc_source_lease_Cancel(result->lease);
        vlc_source_lease_Release(result->lease);
    }
    vlc_input_policy_Release(result->policy);
    free((char *)result->proxy_uri);
    memset(result, 0, sizeof(*result));
}
static void wait_release(struct source_wait *wait)
{
    if (!vlc_atomic_rc_dec(&wait->refs)) return;
    discard(&wait->result); free(wait);
}
static void resolved(vlc_source_job_t *job, int status, void *opaque)
{
    struct source_wait *wait = opaque;
    struct vlc_source_result result = {0};
    if (status == VLC_SUCCESS) {
        const struct vlc_source_result *value = vlc_source_job_GetResult(job);
        if (value == NULL || !vlc_input_policy_IsProxy(value->policy, value->proxy_uri)) status = -EACCES;
        else {
            result.proxy_uri = strdup(value->proxy_uri);
            if (result.proxy_uri == NULL) status = VLC_ENOMEM;
            else {
                result.policy = vlc_input_policy_Hold(value->policy);
                result.lease = vlc_source_lease_Hold(value->lease);
            }
        }
    }
    vlc_mutex_lock(&wait->lock);
    if (!wait->abandoned) {
        wait->status = status; wait->done = true;
        wait->result = result; memset(&result, 0, sizeof(result));
        vlc_sem_post(&wait->ready);
    }
    vlc_mutex_unlock(&wait->lock);
    if (status != VLC_SUCCESS || result.policy != NULL) vlc_source_job_Cancel(job);
    discard(&result);
    wait_release(wait);
}
static void interrupted(void *opaque)
{
    struct source_wait *wait = opaque;
    atomic_store_explicit(&wait->interrupted, true, memory_order_release);
    vlc_sem_post(&wait->ready);
}
static uint64_t generation(void)
{
    static atomic_uint_fast64_t sequence;
    uint_fast64_t value = atomic_load_explicit(&sequence, memory_order_relaxed);
    while (value < UINT64_MAX)
        if (atomic_compare_exchange_weak_explicit(&sequence, &value, value + 1,
                memory_order_relaxed, memory_order_relaxed)) return value + 1;
    return 0;
}
bool input_addon_source_IsURI(const char *uri)
{ return vlc_input_policy_IsAddonSourceURI(uri); }
void input_addon_source_Init(input_addon_source_t *state)
{ memset(state, 0, sizeof(*state)); vlc_mutex_init(&state->lock); }
void input_addon_source_Close(input_addon_source_t *state)
{
    vlc_mutex_lock(&state->lock); state->closed = true;
    vlc_source_job_t *job = state->pending != NULL ? vlc_source_job_Hold(state->pending) : NULL;
    vlc_source_lease_t *lease = state->lease != NULL ? vlc_source_lease_Hold(state->lease) : NULL;
    vlc_input_policy_t *policy = vlc_input_policy_Hold(state->policy);
    vlc_mutex_unlock(&state->lock);
    if (job != NULL) { vlc_source_job_Cancel(job); vlc_source_job_Release(job); }
    vlc_input_policy_Revoke(policy); vlc_input_policy_Release(policy);
    if (lease != NULL) { vlc_source_lease_Cancel(lease); vlc_source_lease_Release(lease); }
}
void input_addon_source_Clean(input_addon_source_t *state)
{
    input_addon_source_Close(state);
    assert(state->pending == NULL);
    vlc_input_policy_Release(state->policy);
    if (state->lease != NULL) vlc_source_lease_Release(state->lease);
    state->policy = NULL; state->lease = NULL;
}
int input_addon_source_Resolve(input_addon_source_t *state, vlc_object_t *object, input_item_t *item,
    const char *uri, bool primary_playback, vlc_tick_t timeout, char **output)
{
    if (output != NULL) *output = NULL;
    if (!state || !object || !item || !output || !primary_playback || uri == NULL
        || strnlen(uri, 12001) > 12000 || strncmp(uri, "hakone-addon-source:v1:", sizeof("hakone-addon-source:v1:") - 1)
        || timeout <= 0 || timeout > VLC_TICK_FROM_SEC(10)) return VLC_EINVAL;
    vlc_mutex_lock(&state->lock);
    const bool refused = state->closed || state->used;
    if (!refused) state->used = true;
    vlc_mutex_unlock(&state->lock);
    if (refused) return -EINTR;

    int status = VLC_EGENERIC;
    vlc_input_policy_t *source = vlc_input_policy_HoldSourceItem(item, uri);
    if (source == NULL) return -EACCES;
    vlc_input_policy_t *current = vlc_input_policy_HoldObject(object);
    const bool compatible = current == source;
    vlc_input_policy_Release(current);
    if (!compatible) goto out_source;
    vlc_source_backend_t *backend = vlc_source_owner_Hold(object);
    if (backend == NULL) goto out_source;
    struct source_wait *wait = calloc(1, sizeof(*wait));
    if (wait == NULL) { status = VLC_ENOMEM; vlc_source_backend_Release(backend); goto out_source; }
    vlc_atomic_rc_init(&wait->refs); vlc_mutex_init(&wait->lock); vlc_sem_init(&wait->ready, 0);
    atomic_init(&wait->interrupted, false);
    const struct vlc_source_request request = {uri, generation()};
    vlc_source_job_t *job = request.input_generation != 0
        ? vlc_source_job_New(backend, &request, resolved, wait) : NULL;
    vlc_source_backend_Release(backend);
    if (job == NULL) { wait_release(wait); goto out_source; }
    vlc_mutex_lock(&state->lock);
    if (state->closed) { vlc_mutex_unlock(&state->lock); vlc_source_job_Release(job); wait_release(wait); status = -EINTR; goto out_source; }
    state->pending = vlc_source_job_Hold(job);
    vlc_mutex_unlock(&state->lock);

    vlc_atomic_rc_inc(&wait->refs); /* One callback, or a failed Start. */
    vlc_interrupt_register(interrupted, wait);
    if (atomic_load_explicit(&wait->interrupted, memory_order_acquire)) vlc_source_job_Cancel(job);
    status = vlc_source_job_Start(job);
    if (status != VLC_SUCCESS) wait_release(wait);
    else if (vlc_sem_timedwait(&wait->ready, vlc_tick_now() + timeout) != 0) status = -ETIMEDOUT;
    const int interruption = vlc_interrupt_unregister();
    vlc_mutex_lock(&wait->lock);
    if (status == VLC_SUCCESS) status = wait->done ? wait->status : VLC_EGENERIC;
    if (interruption != 0) status = -EINTR;
    wait->abandoned = true;
    vlc_mutex_unlock(&wait->lock);

    vlc_mutex_lock(&state->lock);
    if (state->closed) status = -EINTR;
    if (status == VLC_SUCCESS) {
        vlc_mutex_lock(&item->lock);
        const bool same = item->psz_uri != NULL && !strcmp(item->psz_uri, uri)
            && item_owner(item)->input_policy == source;
        if (!same || !vlc_input_policy_IsSource(source, uri)
            || !vlc_input_policy_IsProxy(wait->result.policy, wait->result.proxy_uri)
            || vlc_input_policy_ReplaceObject(object, source, wait->result.policy) != VLC_SUCCESS) status = -EAGAIN;
        vlc_mutex_unlock(&item->lock);
    }
    if (status == VLC_SUCCESS) {
        *output = (char *)wait->result.proxy_uri; wait->result.proxy_uri = NULL;
        state->policy = wait->result.policy; wait->result.policy = NULL;
        state->lease = wait->result.lease; wait->result.lease = NULL;
    }
    vlc_source_job_t *pending = state->pending; state->pending = NULL;
    vlc_mutex_unlock(&state->lock);
    if (status != VLC_SUCCESS) vlc_source_job_Cancel(job);
    vlc_source_job_Release(pending); vlc_source_job_Release(job); wait_release(wait);
out_source:
    vlc_input_policy_Release(source); return status;
}
