// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_metadata_backend.h>
#include <vlc_metadata_owner.h>
#include <vlc_threads.h>
#include <stdatomic.h>
#include <assert.h>
#include "../../../lib/libvlc_internal.h"
static _Thread_local bool pause_start;
static vlc_sem_t before_start, resume_start;
static int paused_start(vlc_metadata_job_t *job)
{
    if (pause_start) { vlc_sem_post(&before_start); vlc_sem_wait(&resume_start); }
    return vlc_metadata_job_Start(job);
}
#define vlc_metadata_job_Start paused_start
#include "../../../src/preparser/metadata_requests.c"
#undef vlc_metadata_job_Start

struct fixture { vlc_metadata_job_t *held[512]; unsigned count; bool synchronous, reject; };
static int dispatch(void *opaque, vlc_metadata_job_t *job)
{
    struct fixture *fixture = opaque; if (fixture->reject) return VLC_EGENERIC; assert(fixture->count < ARRAY_SIZE(fixture->held));
    fixture->held[fixture->count++] = vlc_metadata_job_Hold(job);
    if (fixture->synchronous) assert(vlc_metadata_job_Complete(job, VLC_SUCCESS, NULL, NULL, 0));
    return VLC_SUCCESS;
}
static vlc_metadata_backend_t *new_backend(struct fixture *fixture)
{
    const struct vlc_metadata_backend_operations ops = {.dispatch=dispatch};
    vlc_metadata_backend_t *backend = vlc_metadata_backend_New(&ops, fixture); assert(backend != NULL); return backend;
}
static void release_held(struct fixture *fixture)
{ for (unsigned i = 0; i < fixture->count; ++i) vlc_metadata_job_Release(fixture->held[i]); fixture->count = 0; }
struct result {
    atomic_uint count;
    atomic_int status;
    vlc_metadata_requests_t **delete_in_callback;
    bool block;
    vlc_sem_t started, resume;
};
static void on_ended(vlc_metadata_job_t *job, int status, void *opaque)
{
    (void)job; struct result *result = opaque;
    atomic_store(&result->status, status); atomic_fetch_add(&result->count, 1);
    if (result->delete_in_callback != NULL) {
        vlc_metadata_requests_t *owner = *result->delete_in_callback;
        *result->delete_in_callback = NULL; vlc_metadata_requests_Delete(owner);
    }
    if (result->block) { vlc_sem_post(&result->started); vlc_sem_wait(&result->resume); }
}
static const struct vlc_metadata_request request = {.feature="artwork", .operation="enrich", .flags=VLC_METADATA_REQUEST_NETWORK};
static void closed_unowned_list_refuses_fallback(void)
{
    libvlc_int_t *instance = libvlc_InternalCreate(); assert(instance != NULL);
    vlc_metadata_requests_t *owner = vlc_metadata_requests_New(VLC_OBJECT(instance)); assert(owner != NULL);
    struct result result = {0}; vlc_metadata_requests_Close(owner);
    assert(vlc_metadata_requests_Push(owner, &request, &result, on_ended, &result) == -EINTR);
    assert(atomic_load(&result.count) == 0); vlc_metadata_requests_Delete(owner); libvlc_InternalDestroy(instance);
}
static void independent_lists_and_owned_shutdown(void)
{
    libvlc_int_t *instance = libvlc_InternalCreate(); assert(instance != NULL);
    vlc_object_t *root = VLC_OBJECT(instance);
    struct fixture fixture = {0}; vlc_metadata_backend_t *backend = new_backend(&fixture);
    vlc_metadata_requests_t *first = vlc_metadata_requests_New(root), *second = vlc_metadata_requests_New(root);
    assert(first != NULL && second != NULL); struct result a = {0}, b = {0};
    assert(vlc_metadata_requests_Push(first, &request, &a, on_ended, &a) == VLC_ENOENT);
    assert(atomic_load(&a.count) == 0);
    assert(vlc_metadata_owner_Set(root, backend) == VLC_SUCCESS);
    assert(vlc_metadata_requests_Push(first, &request, &a, on_ended, &a) == VLC_SUCCESS);
    assert(vlc_metadata_requests_Push(second, &request, &b, on_ended, &b) == VLC_SUCCESS);
    vlc_metadata_requests_Delete(first); assert(atomic_load(&a.count) == 1 && atomic_load(&a.status) == -EINTR);
    assert(atomic_load(&b.count) == 0 && !vlc_metadata_backend_IsClosed(backend));
    assert(!vlc_metadata_job_Complete(fixture.held[0], VLC_SUCCESS, NULL, NULL, 0));
    assert(vlc_metadata_job_Complete(fixture.held[1], VLC_SUCCESS, NULL, NULL, 0));
    assert(atomic_load(&b.count) == 1 && atomic_load(&b.status) == VLC_SUCCESS);
    vlc_metadata_backend_Close(backend);
    assert(vlc_metadata_requests_Push(second, &request, &b, on_ended, &b) == -EINTR);
    assert(atomic_load(&b.count) == 1);
    vlc_metadata_requests_Delete(second); release_held(&fixture);
    vlc_metadata_backend_Release(backend); libvlc_InternalDestroy(instance);
}
static void key_cancellation_and_capacity(void)
{
    libvlc_int_t *instance = libvlc_InternalCreate(); assert(instance != NULL);
    vlc_object_t *root = VLC_OBJECT(instance);
    struct fixture fixture = {0}; vlc_metadata_backend_t *backend = new_backend(&fixture);
    assert(vlc_metadata_owner_Set(root, backend) == VLC_SUCCESS);
    vlc_metadata_requests_t *owner = vlc_metadata_requests_New(root); assert(owner != NULL);
    struct result results[MAX_REQUESTS + 1] = {0};
    for (unsigned i = 0; i < MAX_REQUESTS; ++i)
        assert(vlc_metadata_requests_Push(owner, &request, &results[i], on_ended, &results[i]) == VLC_SUCCESS);
    assert(vlc_metadata_requests_Push(owner, &request, &results[MAX_REQUESTS], on_ended, &results[MAX_REQUESTS]) == VLC_ENOMEM);
    assert(vlc_metadata_requests_Cancel(owner, &results[0]) == 1);
    assert(atomic_load(&results[0].count) == 1 && atomic_load(&results[1].count) == 0);
    assert(vlc_metadata_requests_Push(owner, &request, &results[MAX_REQUESTS], on_ended, &results[MAX_REQUESTS]) == VLC_SUCCESS);
    vlc_metadata_requests_Delete(owner);
    for (unsigned i = 0; i <= MAX_REQUESTS; ++i) assert(atomic_load(&results[i].count) == 1);
    release_held(&fixture); vlc_metadata_backend_Release(backend); libvlc_InternalDestroy(instance);
}
static void synchronous_callback_can_delete_its_list(void)
{
    libvlc_int_t *instance = libvlc_InternalCreate(); assert(instance != NULL);
    vlc_object_t *root = VLC_OBJECT(instance);
    struct fixture fixture = {.synchronous=true}; vlc_metadata_backend_t *backend = new_backend(&fixture);
    assert(vlc_metadata_owner_Set(root, backend) == VLC_SUCCESS);
    vlc_metadata_requests_t *owner = vlc_metadata_requests_New(root); assert(owner != NULL);
    struct result result = {.delete_in_callback=&owner};
    assert(vlc_metadata_requests_Push(owner, &request, &result, on_ended, &result) == VLC_SUCCESS);
    assert(owner == NULL && atomic_load(&result.count) == 1 && atomic_load(&result.status) == VLC_SUCCESS);
    fixture.reject = true;
    owner = vlc_metadata_requests_New(root); assert(owner != NULL);
    struct result rejected = {.delete_in_callback=&owner};
    assert(vlc_metadata_requests_Push(owner, &request, &rejected, on_ended, &rejected) == VLC_SUCCESS);
    assert(owner == NULL && atomic_load(&rejected.count) == 1 && atomic_load(&rejected.status) == VLC_EGENERIC);
    release_held(&fixture); vlc_metadata_backend_Release(backend); libvlc_InternalDestroy(instance);
}
struct delete_state { vlc_metadata_requests_t *owner; vlc_sem_t started, done; atomic_bool ended; };
static void *delete_list(void *opaque)
{
    struct delete_state *state = opaque; vlc_sem_post(&state->started);
    vlc_metadata_requests_Delete(state->owner); atomic_store(&state->ended, true); vlc_sem_post(&state->done); return NULL;
}
static void *complete_job(void *opaque)
{ assert(vlc_metadata_job_Complete(opaque, VLC_SUCCESS, NULL, NULL, 0)); return NULL; }
static void deletion_drains_a_callback_on_another_thread(void)
{
    libvlc_int_t *instance = libvlc_InternalCreate(); assert(instance != NULL);
    vlc_object_t *root = VLC_OBJECT(instance);
    struct fixture fixture = {0}; vlc_metadata_backend_t *backend = new_backend(&fixture);
    assert(vlc_metadata_owner_Set(root, backend) == VLC_SUCCESS);
    vlc_metadata_requests_t *owner = vlc_metadata_requests_New(root); assert(owner != NULL);
    struct result result = {.block=true}; vlc_sem_init(&result.started, 0); vlc_sem_init(&result.resume, 0);
    assert(vlc_metadata_requests_Push(owner, &request, &result, on_ended, &result) == VLC_SUCCESS);
    vlc_thread_t completing; assert(vlc_clone(&completing, complete_job, fixture.held[0]) == 0); vlc_sem_wait(&result.started);
    struct delete_state state = {.owner=owner}; vlc_sem_init(&state.started, 0); vlc_sem_init(&state.done, 0);
    vlc_thread_t deleting; assert(vlc_clone(&deleting, delete_list, &state) == 0); vlc_sem_wait(&state.started);
    assert(vlc_sem_timedwait(&state.done, vlc_tick_now() + VLC_TICK_FROM_MS(10)) != 0); assert(!atomic_load(&state.ended));
    vlc_sem_post(&result.resume); vlc_join(completing, NULL); vlc_join(deleting, NULL);
    assert(atomic_load(&state.ended) && atomic_load(&result.count) == 1);
    release_held(&fixture); vlc_metadata_backend_Release(backend); libvlc_InternalDestroy(instance);
}
struct push_state { vlc_metadata_requests_t *owner; struct result result; int status; };
static void *push_before_start(void *opaque)
{
    struct push_state *state = opaque; pause_start = true;
    state->status = vlc_metadata_requests_Push(state->owner, &request, &state->result, on_ended, &state->result);
    pause_start = false; return NULL;
}
static void cancellation_after_list_insert_prevents_dispatch(void)
{
    libvlc_int_t *instance = libvlc_InternalCreate(); assert(instance != NULL);
    struct fixture fixture = {0}; vlc_metadata_backend_t *backend = new_backend(&fixture);
    assert(vlc_metadata_owner_Set(VLC_OBJECT(instance), backend) == VLC_SUCCESS);
    vlc_metadata_requests_t *owner = vlc_metadata_requests_New(VLC_OBJECT(instance)); assert(owner != NULL);
    vlc_sem_init(&before_start, 0); vlc_sem_init(&resume_start, 0);
    struct push_state state = {.owner=owner}; vlc_thread_t thread;
    assert(vlc_clone(&thread, push_before_start, &state) == 0); vlc_sem_wait(&before_start);
    assert(owner->count == 1 && fixture.count == 0);
    assert(vlc_metadata_requests_Cancel(owner, &state.result) == 1);
    assert(fixture.count == 0 && atomic_load(&state.result.count) == 0);
    vlc_sem_post(&resume_start); vlc_join(thread, NULL);
    assert(state.status == -EINTR && owner->count == 0);
    assert(fixture.count == 0 && atomic_load(&state.result.count) == 0);
    vlc_metadata_requests_Delete(owner); vlc_metadata_backend_Release(backend); libvlc_InternalDestroy(instance);
}
int main(void)
{
    closed_unowned_list_refuses_fallback();
    independent_lists_and_owned_shutdown(); key_cancellation_and_capacity();
    synchronous_callback_can_delete_its_list(); deletion_drains_a_callback_on_another_thread();
    cancellation_after_list_insert_prevents_dispatch(); return 0;
}
