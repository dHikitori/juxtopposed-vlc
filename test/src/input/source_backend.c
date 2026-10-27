// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_source_backend.h>
#include <vlc_threads.h>
#include <assert.h>
#include <stdatomic.h>

struct state {
    atomic_uint ended, cancelled, destroyed, lease_cancelled, lease_destroyed, dispatched;
    int status, dispatch_error;
    bool reentrant;
    bool lease_reentrant;
    bool lease_self_reentrant;
    vlc_source_backend_t *backend;
    vlc_source_lease_t *lease;
};
static int dispatch(void *opaque, vlc_source_job_t *job)
{ (void)job; struct state *state = opaque; ++state->dispatched; return state->dispatch_error; }
static void cancel(void *opaque, vlc_source_job_t *job)
{ (void)job; ++((struct state *)opaque)->cancelled; }
static void destroy(void *opaque) { ++((struct state *)opaque)->destroyed; }
static void lease_cancel(void *opaque)
{
    struct state *state = opaque; ++state->lease_cancelled;
    if (state->lease_self_reentrant) vlc_source_lease_Cancel(state->lease);
    if (state->lease_reentrant) vlc_source_backend_Close(state->backend);
}
static void lease_destroy(void *opaque) { ++((struct state *)opaque)->lease_destroyed; }
static void ended(vlc_source_job_t *job, int status, void *opaque)
{
    struct state *state = opaque; ++state->ended; state->status = status;
    if (state->reentrant) {
        assert(!vlc_source_job_Cancel(job));
        vlc_source_backend_Close(state->backend);
        vlc_source_job_Release(job); // Caller reference.
    }
}
static vlc_source_backend_t *backend_new(struct state *state)
{
    static const struct vlc_source_backend_operations ops = { dispatch, cancel, destroy };
    state->backend = vlc_source_backend_New(&ops, state); assert(state->backend != NULL); return state->backend;
}
static vlc_source_job_t *job_new(struct state *state)
{
    char uri[] = "hakone-addon-source:v1:admitted";
    const struct vlc_source_request request = { uri, 1 };
    vlc_source_job_t *job = vlc_source_job_New(state->backend, &request, ended, state); assert(job != NULL);
    uri[0] = 'x';
    assert(!strcmp(vlc_source_job_GetRequest(job)->source_uri, "hakone-addon-source:v1:admitted"));
    assert(vlc_source_job_GetRequest(job)->input_generation == 1);
    return job;
}
static struct vlc_source_result result_new(struct state *state)
{
    static const struct vlc_source_lease_operations ops = { lease_cancel, lease_destroy };
    struct vlc_source_result result = { "http://127.0.0.1:9000/token/resource",
        vlc_input_policy_NewProxy(9000, "/token/", NULL, false), vlc_source_lease_New(&ops, state) };
    assert(result.policy != NULL && result.lease != NULL); return result;
}
static void result_release(struct vlc_source_result *result)
{ vlc_input_policy_Release(result->policy); vlc_source_lease_Release(result->lease); }
static void final_lease_release_can_reenter_cancel(void)
{
    struct state state = { .lease_self_reentrant = true };
    struct vlc_source_result result = result_new(&state); state.lease = result.lease;
    result_release(&result); assert(state.lease_cancelled == 1 && state.lease_destroyed == 1);
}
struct lease_barrier {
    vlc_source_lease_t *lease;
    vlc_sem_t entered, release, second_started, second_done;
    atomic_uint callbacks, destroyed;
    atomic_bool finished, second_returned;
};
static void barrier_cancel(void *opaque)
{
    struct lease_barrier *state = opaque;
    ++state->callbacks;
    vlc_source_lease_Cancel(state->lease); /* Same-thread recursion must return. */
    vlc_sem_post(&state->entered); vlc_sem_wait(&state->release);
    atomic_store_explicit(&state->finished, true, memory_order_release);
}
static void barrier_destroy(void *opaque) { ++((struct lease_barrier *)opaque)->destroyed; }
static void *first_lease_cancel(void *opaque)
{
    struct lease_barrier *state = opaque;
    vlc_source_lease_Cancel(state->lease); vlc_source_lease_Release(state->lease); return NULL;
}
static void *second_lease_cancel(void *opaque)
{
    struct lease_barrier *state = opaque; vlc_sem_post(&state->second_started);
    vlc_source_lease_Cancel(state->lease);
    assert(atomic_load_explicit(&state->finished, memory_order_acquire));
    atomic_store_explicit(&state->second_returned, true, memory_order_release);
    vlc_sem_post(&state->second_done); vlc_source_lease_Release(state->lease); return NULL;
}
static void concurrent_lease_cancellation(bool release_caller)
{
    struct lease_barrier state = {0};
    vlc_sem_init(&state.entered, 0); vlc_sem_init(&state.release, 0);
    vlc_sem_init(&state.second_started, 0); vlc_sem_init(&state.second_done, 0);
    static const struct vlc_source_lease_operations ops = {barrier_cancel, barrier_destroy};
    state.lease = vlc_source_lease_New(&ops, &state); assert(state.lease != NULL);
    vlc_source_lease_Hold(state.lease); vlc_thread_t first, second;
    assert(vlc_clone(&first, first_lease_cancel, &state) == 0); vlc_sem_wait(&state.entered);
    vlc_source_lease_Hold(state.lease);
    assert(vlc_clone(&second, second_lease_cancel, &state) == 0); vlc_sem_wait(&state.second_started);
    if (release_caller) vlc_source_lease_Release(state.lease);
    assert(vlc_sem_timedwait(&state.second_done, vlc_tick_now() + VLC_TICK_FROM_MS(100)) == ETIMEDOUT);
    assert(!atomic_load_explicit(&state.second_returned, memory_order_acquire));
    assert(state.callbacks == 1 && state.destroyed == 0);
    vlc_sem_post(&state.release); vlc_join(first, NULL); vlc_join(second, NULL);
    assert(state.callbacks == 1 && atomic_load_explicit(&state.second_returned, memory_order_acquire));
    if (!release_caller) { assert(state.destroyed == 0); vlc_source_lease_Release(state.lease); }
    assert(state.destroyed == 1);
}
struct retained_lease { vlc_source_lease_t *lease, *retained; unsigned canceled, destroyed; };
static void retained_cancel(void *opaque)
{
    struct retained_lease *state = opaque; ++state->canceled;
    state->retained = vlc_source_lease_Hold(state->lease);
    vlc_source_lease_Cancel(state->lease);
}
static void retained_destroy(void *opaque) { ++((struct retained_lease *)opaque)->destroyed; }
static void final_release_can_retain_a_live_cleanup_reference(void)
{
    struct retained_lease state = {0};
    static const struct vlc_source_lease_operations ops = {retained_cancel, retained_destroy};
    state.lease = vlc_source_lease_New(&ops, &state); assert(state.lease != NULL);
    vlc_source_lease_Release(state.lease);
    assert(state.retained != NULL && state.canceled == 1 && state.destroyed == 0);
    vlc_source_lease_Cancel(state.retained); assert(state.canceled == 1);
    vlc_source_lease_Release(state.retained); assert(state.destroyed == 1);
}
static void success_and_lifetimes(void)
{
    struct state state = { .lease_reentrant = true }; vlc_source_backend_t *backend = backend_new(&state);
    vlc_source_job_t *job = job_new(&state); assert(!vlc_source_job_IsPending(job));
    assert(vlc_source_job_GetResult(job) == NULL); assert(vlc_source_job_Start(job) == VLC_SUCCESS);
    struct vlc_source_result result = result_new(&state);
    assert(vlc_source_job_AttachTransport(job, result.policy, result.lease));
    assert(vlc_source_job_Complete(job, VLC_SUCCESS, &result)); assert(state.ended == 1 && state.status == VLC_SUCCESS);
    assert(!vlc_source_job_Complete(job, VLC_SUCCESS, &result));
    result_release(&result); assert(state.lease_cancelled == 0);
    const struct vlc_source_result *copy = vlc_source_job_GetResult(job);
    assert(copy != NULL && !strcmp(copy->proxy_uri, "http://127.0.0.1:9000/token/resource"));
    vlc_source_backend_Release(backend); assert(state.destroyed == 0);
    vlc_source_job_Release(job); assert(state.destroyed == 1 && state.lease_cancelled == 1 && state.lease_destroyed == 1);
}
static void cancellation_and_close(void)
{
    struct state state = {0}; vlc_source_backend_t *backend = backend_new(&state); vlc_source_job_t *jobs[3];
    for (unsigned i = 0; i < 3; ++i) { jobs[i] = job_new(&state); assert(vlc_source_job_Start(jobs[i]) == VLC_SUCCESS); }
    assert(vlc_source_job_Cancel(jobs[0])); assert(!vlc_source_job_Cancel(jobs[0]));
    vlc_source_backend_Close(backend); assert(vlc_source_backend_IsClosed(backend));
    assert(state.ended == 3 && state.cancelled == 3 && state.status == -EINTR);
    struct vlc_source_result result = result_new(&state);
    assert(!vlc_source_job_Complete(jobs[0], VLC_SUCCESS, &result)); result_release(&result);
    assert(state.lease_cancelled == 1 && state.lease_destroyed == 1);
    vlc_source_job_t *late = job_new(&state); assert(vlc_source_job_Start(late) == -EINTR); assert(state.ended == 3);
    vlc_source_job_Release(late); for (unsigned i = 0; i < 3; ++i) vlc_source_job_Release(jobs[i]);
    vlc_source_backend_Release(backend); assert(state.destroyed == 1);
}
static void errors_and_reentrant_close(void)
{
    struct state state = { .dispatch_error = VLC_EGENERIC, .reentrant = true };
    vlc_source_backend_t *backend = backend_new(&state); vlc_source_job_t *job = job_new(&state);
    assert(vlc_source_job_Start(job) == VLC_SUCCESS); assert(state.ended == 1 && state.status == VLC_EGENERIC);
    assert(vlc_source_backend_IsClosed(backend)); vlc_source_backend_Release(backend); assert(state.destroyed == 1);
}
static void invalid_results_and_requests(void)
{
    struct state state = {0}; vlc_source_backend_t *backend = backend_new(&state);
    const struct vlc_source_request invalid[] = { {"https://remote.invalid", 1}, {"hakone-addon-source:v1:admitted", 0},
        {"vlc-addon-source:v1:admitted", 1}, {"HAKONE-ADDON-SOURCE:v1:admitted", 1} };
    for (unsigned i = 0; i < ARRAY_SIZE(invalid); ++i) assert(vlc_source_job_New(backend, &invalid[i], ended, &state) == NULL);
    char large[12002]; memset(large, 'a', sizeof(large)); memcpy(large, "hakone-addon-source:v1:", sizeof("hakone-addon-source:v1:") - 1); large[12000] = '\0';
    const struct vlc_source_request bounded = {large, 1};
    vlc_source_job_t *boundary = vlc_source_job_New(backend, &bounded, ended, &state); assert(boundary != NULL); vlc_source_job_Release(boundary);
    large[12000] = 'a'; large[12001] = '\0'; assert(vlc_source_job_New(backend, &bounded, ended, &state) == NULL);
    const char *uris[] = { "http://127.0.0.1:9000@remote.invalid/token/resource", "http://127.0.0.1:0/token/resource",
        "http://127.0.0.1:9000/token/../private", "http://127.0.0.1:9000/token/resource?x=1", "http://127.0.0.1:9000/token//x",
        "http://127.0.0.1:9000/token/resource/", "http://localhost:9000/token/resource", "http://127.0.0.1:9000/token/" };
    for (unsigned i = 0; i < ARRAY_SIZE(uris); ++i) {
        vlc_source_job_t *job = job_new(&state); assert(vlc_source_job_Start(job) == VLC_SUCCESS);
        struct vlc_source_result result = result_new(&state);
        assert(vlc_source_job_AttachTransport(job, result.policy, result.lease));
        result.proxy_uri = uris[i]; assert(vlc_source_job_Complete(job, VLC_SUCCESS, &result));
        assert(state.status == VLC_EINVAL && vlc_source_job_GetResult(job)->policy == NULL); vlc_source_job_Release(job);
        result_release(&result);
    }
    vlc_source_backend_Release(backend);
}
struct race { vlc_source_job_t *job; const struct vlc_source_result *result; };
static void *complete_thread(void *opaque)
{ struct race *race = opaque; vlc_source_job_Complete(race->job, VLC_SUCCESS, race->result); return NULL; }
static void result_cancel_races(void)
{
    for (unsigned i = 0; i < 200; ++i) {
        struct state state = {0}; vlc_source_backend_t *backend = backend_new(&state); vlc_source_job_t *job = job_new(&state);
        struct vlc_source_result result = result_new(&state); assert(vlc_source_job_Start(job) == VLC_SUCCESS);
        assert(vlc_source_job_AttachTransport(job, result.policy, result.lease));
        struct race race = {job, &result}; vlc_thread_t thread; assert(vlc_clone(&thread, complete_thread, &race) == 0);
        if (i < 100) vlc_source_job_Cancel(job); else vlc_source_backend_Close(backend);
        assert(state.lease_cancelled == 1 && vlc_input_policy_IsRevoked(result.policy));
        vlc_join(thread, NULL); assert(state.ended == 1 && state.cancelled <= 1);
        result_release(&result); vlc_source_job_Release(job); vlc_source_backend_Release(backend);
        assert(state.destroyed == 1 && state.lease_cancelled == 1 && state.lease_destroyed == 1);
    }
}
static void transport_has_one_job_owner(void)
{
    struct state state = {0}; vlc_source_backend_t *backend = backend_new(&state);
    vlc_source_job_t *a = job_new(&state), *b = job_new(&state);
    assert(vlc_source_job_Start(a) == VLC_SUCCESS && vlc_source_job_Start(b) == VLC_SUCCESS);
    struct vlc_source_result result = result_new(&state);
    assert(vlc_source_job_AttachTransport(a, result.policy, result.lease));
    assert(!vlc_source_job_AttachTransport(b, result.policy, result.lease));
    vlc_source_job_Cancel(a); assert(state.lease_cancelled == 1);
    assert(!vlc_source_job_AttachTransport(b, result.policy, result.lease));
    vlc_source_backend_Close(backend); result_release(&result);
    vlc_source_job_Release(a); vlc_source_job_Release(b); vlc_source_backend_Release(backend);
    assert(state.ended == 2 && state.destroyed == 1 && state.lease_destroyed == 1);
}
static void idle_cancel_prevents_dispatch(void)
{
    struct state state = {0}; vlc_source_backend_t *backend = backend_new(&state); vlc_source_job_t *job = job_new(&state);
    assert(vlc_source_job_Cancel(job)); assert(!vlc_source_job_Cancel(job));
    assert(vlc_source_job_Start(job) == -EINTR); assert(state.dispatched == 0 && state.ended == 0);
    vlc_source_job_Release(job); vlc_source_backend_Release(backend); assert(state.destroyed == 1);
}
static void completed_cancel_revokes_transport(void)
{
    struct state state = {0}; vlc_source_backend_t *backend = backend_new(&state);
    vlc_source_job_t *job = job_new(&state); assert(vlc_source_job_Start(job) == VLC_SUCCESS);
    struct vlc_source_result result = result_new(&state);
    assert(vlc_source_job_AttachTransport(job, result.policy, result.lease));
    assert(vlc_source_job_Complete(job, VLC_SUCCESS, &result)); assert(state.ended == 1);
    assert(!vlc_source_job_Cancel(job)); assert(state.ended == 1 && state.status == VLC_SUCCESS);
    assert(state.lease_cancelled == 1 && vlc_input_policy_IsRevoked(result.policy));
    assert(!vlc_source_job_Cancel(job)); assert(state.lease_cancelled == 1);
    result_release(&result); vlc_source_job_Release(job); vlc_source_backend_Release(backend);
    assert(state.destroyed == 1 && state.lease_destroyed == 1);
}
static void close_revokes_issued_and_pending_transports(void)
{
    for (unsigned complete = 0; complete < 2; ++complete) {
        struct state state = {0}; vlc_source_backend_t *backend = backend_new(&state); vlc_source_job_t *job = job_new(&state);
        struct vlc_source_result result = result_new(&state); assert(vlc_source_job_Start(job) == VLC_SUCCESS);
        assert(vlc_source_job_AttachTransport(job, result.policy, result.lease));
        assert(!vlc_source_job_AttachTransport(job, result.policy, result.lease));
        if (complete) assert(vlc_source_job_Complete(job, VLC_SUCCESS, &result));
        vlc_source_backend_Close(backend); assert(state.lease_cancelled == 1);
        assert(vlc_source_job_GetResult(job) == NULL);
        vlc_source_job_t *late = job_new(&state); assert(!vlc_source_job_AttachTransport(late, result.policy, result.lease));
        vlc_source_job_Release(late); result_release(&result); vlc_source_job_Release(job); vlc_source_backend_Release(backend);
        assert(state.destroyed == 1 && state.lease_destroyed == 1);
    }
}
static void active_limit(void)
{
    struct state state = {0}; vlc_source_backend_t *backend = backend_new(&state); vlc_source_job_t *jobs[129];
    for (unsigned i = 0; i < 129; ++i) { jobs[i] = job_new(&state); assert(vlc_source_job_Start(jobs[i]) == (i < 128 ? VLC_SUCCESS : VLC_EGENERIC)); }
    vlc_source_backend_Close(backend); assert(state.ended == 128);
    for (unsigned i = 0; i < 129; ++i) vlc_source_job_Release(jobs[i]);
    vlc_source_backend_Release(backend);
}
int main(void)
{
    success_and_lifetimes(); cancellation_and_close(); errors_and_reentrant_close();
    invalid_results_and_requests(); result_cancel_races(); active_limit(); close_revokes_issued_and_pending_transports();
    final_lease_release_can_reenter_cancel(); transport_has_one_job_owner(); idle_cancel_prevents_dispatch();
    completed_cancel_revokes_transport();
    concurrent_lease_cancellation(false); concurrent_lease_cancellation(true);
    final_release_can_retain_a_live_cleanup_reference(); return 0;
}
