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
    unsigned ended, destroyed;
    vlc_object_t *owner;
    vlc_metadata_backend_t *replacement;
    bool native, destroy_reentry, ended_forget;
};
static vlc_metadata_backend_t *hold_owner(struct fixture *state, bool *owned)
{ return state->native ? vlc_metadata_owner_Hold(state->owner, owned) : unit_Hold(state->owner, owned); }
static int set_owner(struct fixture *state)
{ return state->native ? vlc_metadata_owner_Set(state->owner, state->replacement) : unit_Set(state->owner, state->replacement); }
static int dispatch(void *opaque, vlc_metadata_job_t *job)
{ (void)opaque; (void)job; return VLC_SUCCESS; }
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
int main(void)
{
    binding_and_closed_owner(); unbound_close_and_allocation_failure(); final_release_and_callback_reentry();
    bounded_instance_registry(); concurrent_set_and_close(); native_lifecycle_and_instance_isolation(); return 0;
}
