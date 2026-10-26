// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_source_owner.h>
#include <vlc_threads.h>
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include "../../../lib/libvlc_internal.h"

/* The private copy permits allocation failure and direct cleanup tests.
 * The lifecycle cases use the registry in the native core.
 */
static atomic_bool fail_allocation;
static atomic_uint allocation_calls;
static void *owner_calloc(size_t count, size_t bytes)
{
    atomic_fetch_add(&allocation_calls, 1);
    return atomic_load(&fail_allocation) ? NULL : calloc(count, bytes);
}
static int unit_Set(vlc_object_t *, vlc_source_backend_t *);
static vlc_source_backend_t *unit_Hold(vlc_object_t *);
static void unit_Close(vlc_object_t *);
static void unit_Forget(vlc_object_t *);
#define vlc_source_owner_Set unit_Set
#define vlc_source_owner_Hold unit_Hold
#define vlc_source_owner_Close unit_Close
#define vlc_source_owner_Forget unit_Forget
#define calloc owner_calloc
#include "../../../src/input/source_owner.c"
#undef calloc
#undef vlc_source_owner_Set
#undef vlc_source_owner_Hold
#undef vlc_source_owner_Close
#undef vlc_source_owner_Forget

struct state {
    unsigned ended, destroyed;
    vlc_object_t *root;
    vlc_source_backend_t *replacement;
    bool native, destroy_reentry, ended_forget;
};
static vlc_source_backend_t *hold_owner(struct state *state)
{
    return state->native ? vlc_source_owner_Hold(state->root) : unit_Hold(state->root);
}
static int set_owner(struct state *state)
{
    return state->native ? vlc_source_owner_Set(state->root, state->replacement)
                         : unit_Set(state->root, state->replacement);
}
static int dispatch(void *opaque, vlc_source_job_t *job)
{ (void)opaque; (void)job; return VLC_SUCCESS; }
static void destroy(void *opaque)
{
    struct state *state = opaque;
    ++state->destroyed;
    if (!state->destroy_reentry) return;
    assert(set_owner(state) == -EINTR);
    assert(hold_owner(state) == NULL);
    if (!state->native) {
        unit_Forget(state->root);
        unit_Close(state->root);
        unit_Forget(state->root);
        assert(set_owner(state) == -EINTR);
    }
}
static void ended(vlc_source_job_t *job, int status, void *opaque)
{
    (void)job;
    struct state *state = opaque;
    assert(status == -EINTR);
    ++state->ended;
    vlc_source_backend_t *held = hold_owner(state);
    assert(held != NULL && vlc_source_backend_IsClosed(held));
    vlc_source_backend_Release(held);
    assert(set_owner(state) == -EINTR);
    if (!state->native) {
        unit_Close(state->root);
        if (state->ended_forget) unit_Forget(state->root);
    }
}
static vlc_source_backend_t *backend_new(struct state *state)
{
    static const struct vlc_source_backend_operations ops = {
        .dispatch = dispatch, .destroy = destroy
    };
    vlc_source_backend_t *backend = vlc_source_backend_New(&ops, state);
    assert(backend != NULL);
    return backend;
}
static libvlc_int_t *instance_new(void)
{
    libvlc_int_t *instance = libvlc_InternalCreate();
    assert(instance != NULL);
    assert(!libvlc_priv(instance)->source_owner_retiring);
    return instance;
}
static vlc_source_job_t *job_new(vlc_source_backend_t *backend, struct state *state)
{
    const struct vlc_source_request request = {"vlc-addon-source:v1:admitted", 1};
    vlc_source_job_t *job = vlc_source_job_New(backend, &request, ended, state);
    assert(job != NULL);
    assert(vlc_source_job_Start(job) == VLC_SUCCESS);
    return job;
}
static void binding_and_cleanup(void)
{
    libvlc_int_t *instance = instance_new();
    vlc_object_t *root = VLC_OBJECT(instance);
    vlc_object_t *child = vlc_object_create(root, sizeof(*child));
    assert(child != NULL);
    struct state first = {.root=root}, second = {.root=root}, third = {.root=root};
    vlc_source_backend_t *a = backend_new(&first), *b = backend_new(&second), *c = backend_new(&third);
    assert(unit_Hold(child) == NULL);
    assert(unit_Set(root, a) == VLC_SUCCESS);
    vlc_source_backend_t *held = unit_Hold(child);
    assert(held == a); vlc_source_backend_Release(held);
    assert(unit_Set(child, a) == VLC_SUCCESS);
    assert(unit_Set(root, b) == -EBUSY);
    assert(unit_Set(root, NULL) == VLC_EINVAL);
    vlc_source_backend_Close(a);
    held = unit_Hold(child);
    assert(held == a && vlc_source_backend_IsClosed(held));
    vlc_source_backend_Release(held);
    assert(unit_Set(root, b) == VLC_SUCCESS);
    vlc_source_backend_Release(a); assert(first.destroyed == 1);
    second.replacement = c;
    vlc_source_job_t *job = job_new(b, &second);
    unit_Close(root);
    assert(second.ended == 1 && !vlc_source_job_IsPending(job));
    assert(unit_Set(child, c) == -EINTR);
    assert(!vlc_source_job_Complete(job, VLC_EGENERIC, NULL));
    held = unit_Hold(child); assert(held == b); vlc_source_backend_Release(held);
    unit_Forget(root); assert(unit_Hold(child) == NULL);
    unit_Forget(root); unit_Close(root);
    assert(unit_Set(child, c) == -EINTR);
    vlc_source_job_Release(job);
    vlc_source_backend_Release(b); vlc_source_backend_Release(c);
    assert(second.destroyed == 1 && third.destroyed == 1);
    vlc_object_delete(child);
    libvlc_InternalDestroy(instance);
    assert(owner_count == 0);
}
static void unbound_and_allocation_failure(void)
{
    struct state state = {0};
    vlc_source_backend_t *backend = backend_new(&state);
    libvlc_int_t *instance = instance_new();
    vlc_object_t *root = VLC_OBJECT(instance);
    atomic_store(&fail_allocation, true);
    assert(unit_Set(root, backend) == VLC_ENOMEM);
    unsigned calls = atomic_load(&allocation_calls);
    unit_Close(root);
    assert(libvlc_priv(instance)->source_owner_retiring);
    assert(unit_Set(root, backend) == -EINTR);
    unit_Forget(root); unit_Close(root); unit_Forget(root);
    assert(unit_Set(root, backend) == -EINTR);
    assert(unit_Hold(root) == NULL);
    assert(atomic_load(&allocation_calls) == calls);
    atomic_store(&fail_allocation, false);
    libvlc_InternalDestroy(instance);
    instance = instance_new(); root = VLC_OBJECT(instance);
    assert(unit_Set(root, backend) == VLC_SUCCESS);
    unit_Forget(root); libvlc_InternalDestroy(instance);
    vlc_source_backend_Release(backend);
    assert(state.destroyed == 1 && owner_count == 0);
}
static void full_registry_does_not_reopen(void)
{
    struct state state = {0}, replacement_state = {0};
    vlc_source_backend_t *backend = backend_new(&state), *replacement = backend_new(&replacement_state);
    libvlc_int_t *instances[MAX_SOURCE_OWNERS + 1];
    for (unsigned i = 0; i < ARRAY_SIZE(instances); ++i) {
        instances[i] = instance_new();
        assert(unit_Set(VLC_OBJECT(instances[i]), backend) == (i < MAX_SOURCE_OWNERS ? VLC_SUCCESS : VLC_ENOMEM));
    }
    vlc_object_t *unbound = VLC_OBJECT(instances[MAX_SOURCE_OWNERS]);
    unsigned calls = atomic_load(&allocation_calls);
    unit_Close(unbound);
    assert(unit_Set(unbound, replacement) == -EINTR);
    assert(atomic_load(&allocation_calls) == calls);
    for (unsigned i = 0; i < MAX_SOURCE_OWNERS; ++i) {
        unit_Forget(VLC_OBJECT(instances[i]));
        libvlc_InternalDestroy(instances[i]);
    }
    assert(owner_count == 0);
    assert(unit_Set(unbound, replacement) == -EINTR);
    unit_Forget(unbound); libvlc_InternalDestroy(instances[MAX_SOURCE_OWNERS]);
    vlc_source_backend_Release(backend); vlc_source_backend_Release(replacement);
    assert(state.destroyed == 1 && replacement_state.destroyed == 1);
}
static void destroy_and_completion_can_reenter(void)
{
    for (unsigned pending = 0; pending < 2; ++pending) {
        libvlc_int_t *instance = instance_new();
        vlc_object_t *root = VLC_OBJECT(instance);
        struct state replacement_state = {0}, state = {
            .root=root, .destroy_reentry=true, .ended_forget=true
        };
        vlc_source_backend_t *replacement = backend_new(&replacement_state);
        state.replacement = replacement;
        vlc_source_backend_t *backend = backend_new(&state);
        assert(unit_Set(root, backend) == VLC_SUCCESS);
        vlc_source_job_t *job = pending ? job_new(backend, &state) : NULL;
        vlc_source_backend_Release(backend);
        unit_Forget(root);
        assert(state.ended == pending);
        if (job != NULL) vlc_source_job_Release(job);
        assert(state.destroyed == 1 && unit_Hold(root) == NULL && owner_count == 0);
        unit_Forget(root);
        libvlc_InternalDestroy(instance);
        vlc_source_backend_Release(replacement);
        assert(replacement_state.destroyed == 1);
    }
}
struct race {
    vlc_object_t *root;
    vlc_source_backend_t *backend;
    vlc_sem_t start;
    int status;
};
static void *set_thread(void *opaque)
{
    struct race *race = opaque;
    vlc_sem_wait(&race->start);
    race->status = unit_Set(race->root, race->backend);
    return NULL;
}
static void *close_thread(void *opaque)
{
    struct race *race = opaque;
    vlc_sem_wait(&race->start);
    unit_Close(race->root);
    return NULL;
}
static void concurrent_set_and_close(void)
{
    for (unsigned i = 0; i < 100; ++i) {
        libvlc_int_t *instance = instance_new();
        struct state state = {0};
        vlc_source_backend_t *backend = backend_new(&state);
        struct race race = {.root=VLC_OBJECT(instance), .backend=backend};
        vlc_sem_init(&race.start, 0);
        vlc_thread_t setter, closer;
        assert(vlc_clone(&setter, set_thread, &race) == 0);
        assert(vlc_clone(&closer, close_thread, &race) == 0);
        vlc_sem_post(&race.start); vlc_sem_post(&race.start);
        vlc_join(setter, NULL); vlc_join(closer, NULL);
        assert(race.status == VLC_SUCCESS || race.status == -EINTR);
        if (race.status == VLC_SUCCESS) assert(vlc_source_backend_IsClosed(backend));
        struct state next_state = {0};
        vlc_source_backend_t *next = backend_new(&next_state);
        assert(unit_Set(race.root, next) == -EINTR);
        unit_Forget(race.root);
        assert(unit_Hold(race.root) == NULL && owner_count == 0);
        libvlc_InternalDestroy(instance);
        vlc_source_backend_Release(backend); vlc_source_backend_Release(next);
        assert(state.destroyed == 1 && next_state.destroyed == 1);
    }
}
static void native_destroy_mount(void)
{
    libvlc_int_t *instance = instance_new();
    struct state replacement_state = {0}, state = {
        .root=VLC_OBJECT(instance), .native=true, .destroy_reentry=true
    };
    state.replacement = backend_new(&replacement_state);
    vlc_source_backend_t *backend = backend_new(&state);
    assert(vlc_source_owner_Set(state.root, backend) == VLC_SUCCESS);
    vlc_source_backend_Release(backend);
    libvlc_InternalDestroy(instance);
    assert(state.destroyed == 1);
    vlc_source_backend_Release(state.replacement);
    assert(replacement_state.destroyed == 1);
}
static void native_cleanup_mount(void)
{
    libvlc_int_t *instance = instance_new();
    const char *options[] = {"source-owner-test", "--ignore-config", "--no-plugins-cache", "--no-media-library", "--quiet"};
    assert(libvlc_InternalInit(instance, ARRAY_SIZE(options), options) == VLC_SUCCESS);
    struct state replacement_state = {0}, state = {
        .root=VLC_OBJECT(instance), .native=true, .destroy_reentry=true
    };
    state.replacement = backend_new(&replacement_state);
    vlc_source_backend_t *backend = backend_new(&state);
    assert(vlc_source_owner_Set(state.root, backend) == VLC_SUCCESS);
    vlc_source_job_t *job = job_new(backend, &state);
    vlc_source_backend_Release(backend);
    libvlc_InternalCleanup(instance);
    assert(state.ended == 1 && vlc_source_owner_Hold(state.root) == NULL);
    assert(vlc_source_owner_Set(state.root, state.replacement) == -EINTR);
    vlc_source_job_Release(job);
    assert(state.destroyed == 1);
    libvlc_InternalDestroy(instance);
    vlc_source_backend_Release(state.replacement);
    assert(replacement_state.destroyed == 1);
}
int main(void)
{
#ifdef _WIN32
    assert(_putenv_s("VLC_PLUGIN_PATH", TOP_BUILDDIR "/modules") == 0);
#else
    assert(setenv("VLC_PLUGIN_PATH", TOP_BUILDDIR "/modules", 1) == 0);
#endif
    binding_and_cleanup();
    unbound_and_allocation_failure();
    full_registry_does_not_reopen();
    destroy_and_completion_can_reenter();
    concurrent_set_and_close();
    native_destroy_mount();
    native_cleanup_mount();
    puts("Source owner: 7 case groups and 100 races passed.");
    return 0;
}
