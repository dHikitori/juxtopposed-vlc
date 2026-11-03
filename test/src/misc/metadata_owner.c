// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_metadata_owner.h>
#include <vlc_threads.h>
#include <stdatomic.h>
#include <assert.h>
#include "../../../lib/libvlc_internal.h"

static atomic_bool fail_allocation;
static atomic_uint allocation_calls;
static void *owner_calloc(size_t count, size_t bytes)
{
    atomic_fetch_add(&allocation_calls, 1);
    return atomic_load(&fail_allocation) ? NULL : calloc(count, bytes);
}
static int unit_Set(vlc_object_t *, vlc_metadata_backend_t *);
static vlc_metadata_backend_t *unit_Hold(vlc_object_t *, bool *);
static void unit_Close(vlc_object_t *);
static void unit_Forget(vlc_object_t *);
#define vlc_metadata_owner_Set unit_Set
#define vlc_metadata_owner_Hold unit_Hold
#define vlc_metadata_owner_Close unit_Close
#define vlc_metadata_owner_Forget unit_Forget
#define calloc owner_calloc
#include "../../../src/preparser/metadata_owner.c"
#undef calloc
#undef vlc_metadata_owner_Set
#undef vlc_metadata_owner_Hold
#undef vlc_metadata_owner_Close
#undef vlc_metadata_owner_Forget

struct fixture {
    unsigned ended, destroyed, dispatched;
    vlc_object_t *owner;
    vlc_metadata_backend_t *replacement;
    bool native, destroy_reentry, ended_forget, close_in_dispatch;
};
static vlc_metadata_backend_t *hold_owner(struct fixture *state, bool *owned)
{ return state->native ? vlc_metadata_owner_Hold(state->owner, owned) : unit_Hold(state->owner, owned); }
static int set_owner(struct fixture *state)
{ return state->native ? vlc_metadata_owner_Set(state->owner, state->replacement) : unit_Set(state->owner, state->replacement); }
static int dispatch(void *opaque, vlc_metadata_job_t *job)
{
    (void)job; struct fixture *state = opaque; ++state->dispatched;
    if (state->close_in_dispatch) unit_Close(state->owner);
    return VLC_SUCCESS;
}
static void destroy(void *opaque)
{
    struct fixture *state = opaque; ++state->destroyed;
    if (!state->destroy_reentry) return;
    assert(set_owner(state) == -EINTR);
    bool owned = false; assert(hold_owner(state, &owned) == NULL && owned);
    if (!state->native) { unit_Forget(state->owner); unit_Close(state->owner); unit_Forget(state->owner); }
}
static void ended(vlc_metadata_job_t *job, int status, void *opaque)
{
    (void)job; assert(status == -EINTR); struct fixture *state = opaque; ++state->ended;
    bool owned = false; vlc_metadata_backend_t *backend = hold_owner(state, &owned);
    assert(owned && backend != NULL && vlc_metadata_backend_IsClosed(backend)); vlc_metadata_backend_Release(backend);
    assert(set_owner(state) == -EINTR);
    if (!state->native) { unit_Close(state->owner); if (state->ended_forget) unit_Forget(state->owner); }
}
static vlc_metadata_backend_t *new_backend(struct fixture *state)
{
    const struct vlc_metadata_backend_operations operations = {.dispatch=dispatch, .destroy=destroy};
    vlc_metadata_backend_t *backend = vlc_metadata_backend_New(&operations, state); assert(backend != NULL); return backend;
}
static libvlc_int_t *instance_new(void)
{
    libvlc_int_t *instance = libvlc_InternalCreate(); assert(instance != NULL);
    assert(!libvlc_priv(instance)->metadata_owner_retiring); return instance;
}
static vlc_metadata_job_t *start_job(vlc_metadata_backend_t *backend, struct fixture *state)
{
    const struct vlc_metadata_request request = {.feature="artwork", .operation="enrich", .flags=VLC_METADATA_REQUEST_NETWORK};
    vlc_metadata_job_t *job = vlc_metadata_job_New(backend, &request, ended, state); assert(job != NULL);
    assert(vlc_metadata_job_Start(job) == VLC_SUCCESS); return job;
}
static void binding_and_closed_owner(void)
{
    libvlc_int_t *instance = instance_new(); vlc_object_t *root = VLC_OBJECT(instance);
    vlc_object_t *child = vlc_object_create(root, sizeof(*child)); assert(child != NULL);
    struct fixture first = {.owner=root}, second = {.owner=root}, third = {.owner=root};
    vlc_metadata_backend_t *a = new_backend(&first), *b = new_backend(&second), *c = new_backend(&third);
    bool owned = true; assert(unit_Hold(child, &owned) == NULL && !owned);
    assert(unit_Set(root, a) == VLC_SUCCESS); vlc_metadata_backend_t *held = unit_Hold(child, &owned);
    assert(owned && held == a); vlc_metadata_backend_Release(held);
    assert(unit_Set(child, a) == VLC_SUCCESS && unit_Set(root, NULL) == VLC_EINVAL && unit_Set(root, b) == -EBUSY);
    vlc_metadata_backend_Close(a); held = unit_Hold(child, &owned);
    assert(owned && held == a && vlc_metadata_backend_IsClosed(held)); vlc_metadata_backend_Release(held);
    assert(unit_Set(child, b) == VLC_SUCCESS); vlc_metadata_backend_Release(a); assert(first.destroyed == 1);
    second.replacement = c; vlc_metadata_job_t *job = start_job(b, &second);
    unit_Close(root); assert(second.ended == 1 && !vlc_metadata_job_IsPending(job)); assert(unit_Set(child, c) == -EINTR);
    assert(!vlc_metadata_job_Complete(job, VLC_SUCCESS, NULL, NULL, 0));
    unit_Forget(root); owned = false; assert(unit_Hold(child, &owned) == NULL && owned);
    unit_Forget(root); unit_Close(root); assert(unit_Set(child, c) == -EINTR);
    vlc_metadata_job_Release(job); vlc_metadata_backend_Release(b); vlc_metadata_backend_Release(c);
    assert(second.destroyed == 1 && third.destroyed == 1); vlc_object_delete(child); libvlc_InternalDestroy(instance);
    assert(owner_count == 0);
}
static void unbound_close_and_allocation_failure(void)
{
    struct fixture state = {0}; vlc_metadata_backend_t *backend = new_backend(&state);
    libvlc_int_t *instance = instance_new(); vlc_object_t *root = VLC_OBJECT(instance);
    atomic_store(&fail_allocation, true); const unsigned calls = atomic_load(&allocation_calls);
    unit_Close(root); assert(atomic_load(&allocation_calls) == calls); assert(unit_Set(root, backend) == -EINTR);
    bool owned = false; assert(unit_Hold(root, &owned) == NULL && owned); unit_Forget(root); libvlc_InternalDestroy(instance);
    instance = instance_new(); root = VLC_OBJECT(instance);
    assert(unit_Set(root, backend) == VLC_ENOMEM); owned = true; assert(unit_Hold(root, &owned) == NULL && !owned);
    unit_Close(root); atomic_store(&fail_allocation, false); assert(unit_Set(root, backend) == -EINTR);
    unit_Forget(root); libvlc_InternalDestroy(instance); vlc_metadata_backend_Release(backend); assert(state.destroyed == 1);
}
static void final_release_and_callback_reentry(void)
{
    for (unsigned during_ended = 0; during_ended < 2; ++during_ended) {
        libvlc_int_t *instance = instance_new(); vlc_object_t *root = VLC_OBJECT(instance);
        struct fixture state = {.owner=root, .destroy_reentry=true, .ended_forget=during_ended != 0}, replacement = {0};
        vlc_metadata_backend_t *backend = new_backend(&state), *next = new_backend(&replacement); state.replacement = next;
        assert(unit_Set(root, backend) == VLC_SUCCESS);
        vlc_metadata_job_t *job = during_ended ? start_job(backend, &state) : NULL;
        vlc_metadata_backend_Release(backend); unit_Forget(root);
        if (job != NULL) vlc_metadata_job_Release(job);
        assert(state.destroyed == 1 && state.ended == during_ended);
        vlc_metadata_backend_Release(next); unit_Forget(root); libvlc_InternalDestroy(instance); assert(owner_count == 0);
    }
}
static void bounded_instance_registry(void)
{
    struct fixture state = {0}; vlc_metadata_backend_t *backend = new_backend(&state);
    libvlc_int_t *instances[MAX_OWNERS + 1];
    for (unsigned i = 0; i < ARRAY_SIZE(instances); ++i) {
        instances[i] = instance_new();
        assert(unit_Set(VLC_OBJECT(instances[i]), backend) == (i < MAX_OWNERS ? VLC_SUCCESS : VLC_ENOMEM));
    }
    vlc_object_t *last = VLC_OBJECT(instances[MAX_OWNERS]); unit_Close(last); assert(unit_Set(last, backend) == -EINTR);
    for (unsigned i = 0; i < ARRAY_SIZE(instances); ++i) { unit_Forget(VLC_OBJECT(instances[i])); libvlc_InternalDestroy(instances[i]); }
    vlc_metadata_backend_Release(backend); assert(state.destroyed == 1 && owner_count == 0);
}
struct race { vlc_object_t *root; vlc_metadata_backend_t *backend; vlc_sem_t *start; bool close; int status; };
static void *set_or_close(void *opaque)
{
    struct race *state = opaque; vlc_sem_wait(state->start);
    if (state->close) unit_Close(state->root); else state->status = unit_Set(state->root, state->backend); return NULL;
}
static void concurrent_set_and_close(void)
{
    for (unsigned round = 0; round < 64; ++round) {
        libvlc_int_t *instance = instance_new(); vlc_object_t *root = VLC_OBJECT(instance);
        struct fixture state = {0}; vlc_metadata_backend_t *backend = new_backend(&state);
        vlc_sem_t start; vlc_sem_init(&start, 0);
        struct race a = {root, backend, &start, false, 0}, b = {root, backend, &start, true, 0}; vlc_thread_t first, second;
        assert(vlc_clone(&first, set_or_close, &a) == 0 && vlc_clone(&second, set_or_close, &b) == 0);
        vlc_sem_post(&start); vlc_sem_post(&start); vlc_join(first, NULL); vlc_join(second, NULL);
        assert(a.status == VLC_SUCCESS || a.status == -EINTR); assert(unit_Set(root, backend) != VLC_SUCCESS);
        bool owned = false; vlc_metadata_backend_t *held = unit_Hold(root, &owned); assert(owned);
        if (held != NULL) { assert(vlc_metadata_backend_IsClosed(held)); vlc_metadata_backend_Release(held); }
        unit_Forget(root); vlc_metadata_backend_Release(backend); libvlc_InternalDestroy(instance); assert(state.destroyed == 1);
    }
}
static void native_lifecycle_and_instance_isolation(void)
{
    libvlc_int_t *first = instance_new(), *second = instance_new();
    struct fixture state = {.owner=VLC_OBJECT(first), .native=true, .destroy_reentry=true}, other = {.owner=VLC_OBJECT(second), .native=true};
    vlc_metadata_backend_t *a = new_backend(&state), *b = new_backend(&other); state.replacement = b;
    assert(vlc_metadata_owner_Set(VLC_OBJECT(first), a) == VLC_SUCCESS && vlc_metadata_owner_Set(VLC_OBJECT(second), b) == VLC_SUCCESS);
    vlc_metadata_backend_Release(a); libvlc_InternalDestroy(first); assert(state.destroyed == 1);
    bool owned = false; vlc_metadata_backend_t *held = vlc_metadata_owner_Hold(VLC_OBJECT(second), &owned);
    assert(owned && held == b && !vlc_metadata_backend_IsClosed(held)); vlc_metadata_backend_Release(held);
    libvlc_InternalDestroy(second); vlc_metadata_backend_Release(b); assert(other.destroyed == 1);
}
struct gate_result { unsigned count; int status; };
static void gate_ended(vlc_metadata_job_t *job, int status, void *opaque)
{ (void)job; struct gate_result *result = opaque; ++result->count; result->status = status; }
static vlc_metadata_job_t *gate_job(vlc_metadata_backend_t *backend, struct gate_result *result)
{
    const struct vlc_metadata_request request = {.feature="artwork", .operation="enrich", .flags=VLC_METADATA_REQUEST_NETWORK};
    vlc_metadata_job_t *job = vlc_metadata_job_New(backend, &request, gate_ended, result); assert(job != NULL);
    assert(vlc_metadata_job_Start(job) == VLC_SUCCESS); return job;
}
static bool scope_killed(vlc_metadata_legacy_t *scope)
{
    vlc_interrupt_t *old = vlc_interrupt_set(vlc_metadata_legacy_Interrupt(scope));
    const bool killed = vlc_killed(); vlc_interrupt_set(old); return killed;
}
static void pending_legacy_cannot_begin_after_owner_install(void)
{
    libvlc_int_t *instance = instance_new(); vlc_object_t *root = VLC_OBJECT(instance);
    vlc_metadata_legacy_t *scope = vlc_metadata_legacy_Acquire(root); assert(scope != NULL);
    struct fixture state = {.owner=root}; vlc_metadata_backend_t *backend = new_backend(&state);
    assert(unit_Set(root, backend) == VLC_SUCCESS && vlc_metadata_backend_IsReady(backend));
    assert(scope_killed(scope) && !vlc_metadata_legacy_Begin(scope)); assert(vlc_metadata_legacy_Acquire(root) == NULL);
    struct gate_result result = {0}; vlc_metadata_job_t *job = gate_job(backend, &result); assert(state.dispatched == 1);
    unit_Close(root); assert(result.count == 1 && result.status == -EINTR);
    vlc_metadata_legacy_Release(scope); unit_Forget(root); vlc_metadata_job_Release(job);
    vlc_metadata_backend_Release(backend); libvlc_InternalDestroy(instance); assert(legacy_count == 0 && owner_count == 0);
}
static void running_legacy_blocks_dispatch_until_native_end(void)
{
    for (unsigned reentry = 0; reentry < 2; ++reentry) {
        libvlc_int_t *instance = instance_new(); vlc_object_t *root = VLC_OBJECT(instance);
        vlc_metadata_legacy_t *scope = vlc_metadata_legacy_Acquire(root); assert(scope != NULL && vlc_metadata_legacy_Begin(scope));
        struct fixture state = {.owner=root, .close_in_dispatch=reentry != 0}; vlc_metadata_backend_t *backend = new_backend(&state);
        assert(unit_Set(root, backend) == VLC_SUCCESS && !vlc_metadata_backend_IsReady(backend) && scope_killed(scope));
        struct gate_result a = {0}, b = {0}; vlc_metadata_job_t *first = gate_job(backend, &a), *second = gate_job(backend, &b);
        assert(state.dispatched == 0); assert(vlc_metadata_job_Cancel(first)); assert(a.count == 1 && a.status == -EINTR);
        vlc_metadata_legacy_End(scope); assert(state.dispatched == 1); vlc_metadata_legacy_End(scope); assert(state.dispatched == 1);
        if (reentry) assert(b.count == 1 && b.status == -EINTR && vlc_metadata_backend_IsClosed(backend));
        else assert(vlc_metadata_job_Complete(second, VLC_SUCCESS, NULL, NULL, 0) && b.count == 1 && b.status == VLC_SUCCESS);
        vlc_metadata_legacy_Release(scope); unit_Forget(root); vlc_metadata_job_Release(first); vlc_metadata_job_Release(second);
        vlc_metadata_backend_Release(backend); libvlc_InternalDestroy(instance); assert(legacy_count == 0 && owner_count == 0);
    }
}
static void shared_backend_keeps_every_instance_suspension(void)
{
    libvlc_int_t *a = instance_new(), *b = instance_new();
    vlc_metadata_legacy_t *first = vlc_metadata_legacy_Acquire(VLC_OBJECT(a)), *second = vlc_metadata_legacy_Acquire(VLC_OBJECT(b));
    assert(first != NULL && second != NULL && vlc_metadata_legacy_Begin(first) && vlc_metadata_legacy_Begin(second));
    struct fixture state = {0}; vlc_metadata_backend_t *backend = new_backend(&state);
    assert(unit_Set(VLC_OBJECT(a), backend) == VLC_SUCCESS);
    struct gate_result result = {0}; vlc_metadata_job_t *job = gate_job(backend, &result); assert(state.dispatched == 0);
    assert(unit_Set(VLC_OBJECT(b), backend) == VLC_SUCCESS);
    vlc_metadata_legacy_End(first); assert(state.dispatched == 0 && !vlc_metadata_backend_IsReady(backend));
    vlc_metadata_legacy_End(second); assert(state.dispatched == 1 && vlc_metadata_backend_IsReady(backend));
    assert(vlc_metadata_job_Complete(job, VLC_SUCCESS, NULL, NULL, 0));
    vlc_metadata_legacy_Release(first); vlc_metadata_legacy_Release(second); unit_Forget(VLC_OBJECT(a)); unit_Forget(VLC_OBJECT(b));
    vlc_metadata_job_Release(job); vlc_metadata_backend_Release(backend); libvlc_InternalDestroy(a); libvlc_InternalDestroy(b);
    assert(legacy_count == 0 && owner_count == 0);
}
static void retired_leases_do_not_access_a_dead_instance(void)
{
    libvlc_int_t *instance = instance_new(); vlc_object_t *root = VLC_OBJECT(instance);
    vlc_metadata_legacy_t *pending = vlc_metadata_legacy_Acquire(root), *running = vlc_metadata_legacy_Acquire(root);
    assert(pending != NULL && running != NULL && vlc_metadata_legacy_Begin(running));
    unit_Close(root); assert(scope_killed(pending) && scope_killed(running)); assert(vlc_metadata_legacy_Acquire(root) == NULL);
    unit_Forget(root); libvlc_InternalDestroy(instance);
    assert(!vlc_metadata_legacy_Begin(pending) && !vlc_metadata_legacy_Begin(running));
    vlc_metadata_legacy_Release(pending); vlc_metadata_legacy_End(running); vlc_metadata_legacy_Release(running);
    assert(legacy_count == 0);
}
static void bounded_leases_and_active_backend_refusal(void)
{
    libvlc_int_t *instance = instance_new(); vlc_object_t *root = VLC_OBJECT(instance);
    vlc_metadata_legacy_t *scopes[MAX_LEGACY];
    for (unsigned i = 0; i < MAX_LEGACY; ++i) { scopes[i] = vlc_metadata_legacy_Acquire(root); assert(scopes[i] != NULL); }
    assert(vlc_metadata_legacy_Acquire(root) == NULL);
    for (unsigned i = 0; i < MAX_LEGACY; ++i) vlc_metadata_legacy_Release(scopes[i]);
    vlc_metadata_legacy_t *cancelled = vlc_metadata_legacy_Acquire(root); assert(cancelled != NULL);
    vlc_metadata_legacy_Cancel(cancelled); assert(scope_killed(cancelled) && !vlc_metadata_legacy_Begin(cancelled));
    vlc_metadata_legacy_Release(cancelled);
    vlc_metadata_legacy_t *scope = vlc_metadata_legacy_Acquire(root); assert(scope != NULL && vlc_metadata_legacy_Begin(scope));
    struct fixture state = {0}; vlc_metadata_backend_t *backend = new_backend(&state);
    struct gate_result result = {0}; vlc_metadata_job_t *job = gate_job(backend, &result);
    assert(unit_Set(root, backend) == -EBUSY && owner_count == 0 && !scope_killed(scope));
    assert(vlc_metadata_job_Complete(job, VLC_SUCCESS, NULL, NULL, 0));
    assert(unit_Set(root, backend) == VLC_SUCCESS && scope_killed(scope)); vlc_metadata_legacy_End(scope);
    vlc_metadata_legacy_Release(scope); vlc_metadata_job_Release(job); unit_Forget(root); vlc_metadata_backend_Release(backend);
    libvlc_InternalDestroy(instance); assert(legacy_count == 0 && owner_count == 0);
}
struct native_gate_wait {
    vlc_metadata_legacy_t *scope;
    vlc_sem_t entered, interrupted, finish;
};
static void native_gate_wake(void *opaque) { vlc_sem_post(opaque); }
static void *native_gate_worker(void *opaque)
{
    struct native_gate_wait *state = opaque; assert(vlc_metadata_legacy_Begin(state->scope));
    vlc_interrupt_t *old = vlc_interrupt_set(vlc_metadata_legacy_Interrupt(state->scope));
    vlc_interrupt_register(native_gate_wake, &state->interrupted); vlc_sem_post(&state->entered);
    vlc_sem_wait(&state->interrupted); assert(vlc_interrupt_unregister() == EINTR); vlc_interrupt_set(old);
    vlc_sem_wait(&state->finish); vlc_metadata_legacy_End(state->scope); vlc_metadata_legacy_Release(state->scope); return NULL;
}
static void native_interrupt_acknowledgement_releases_the_gate(void)
{
    libvlc_int_t *instance = instance_new(); vlc_object_t *root = VLC_OBJECT(instance);
    struct native_gate_wait wait = {.scope=vlc_metadata_legacy_Acquire(root)}; assert(wait.scope != NULL);
    vlc_sem_init(&wait.entered, 0); vlc_sem_init(&wait.interrupted, 0); vlc_sem_init(&wait.finish, 0);
    vlc_thread_t worker; assert(vlc_clone(&worker, native_gate_worker, &wait) == 0); vlc_sem_wait(&wait.entered);
    struct fixture state = {.owner=root}; vlc_metadata_backend_t *backend = new_backend(&state);
    assert(unit_Set(root, backend) == VLC_SUCCESS && !vlc_metadata_backend_IsReady(backend));
    struct gate_result result = {0}; vlc_metadata_job_t *job = gate_job(backend, &result); assert(state.dispatched == 0);
    vlc_sem_post(&wait.finish); vlc_join(worker, NULL); assert(state.dispatched == 1 && vlc_metadata_backend_IsReady(backend));
    assert(vlc_metadata_job_Complete(job, VLC_SUCCESS, NULL, NULL, 0));
    vlc_metadata_job_Release(job); unit_Forget(root); vlc_metadata_backend_Release(backend); libvlc_InternalDestroy(instance);
    assert(legacy_count == 0 && owner_count == 0);
}
int main(void)
{
    binding_and_closed_owner(); unbound_close_and_allocation_failure(); final_release_and_callback_reentry();
    bounded_instance_registry(); concurrent_set_and_close(); native_lifecycle_and_instance_isolation();
    pending_legacy_cannot_begin_after_owner_install(); running_legacy_blocks_dispatch_until_native_end();
    shared_backend_keeps_every_instance_suspension(); retired_leases_do_not_access_a_dead_instance();
    bounded_leases_and_active_backend_refusal(); native_interrupt_acknowledgement_releases_the_gate(); return 0;
}
