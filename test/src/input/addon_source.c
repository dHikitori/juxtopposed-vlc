// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_input_item.h>
#include <vlc_input_policy.h>
#include <vlc_interrupt.h>
#include <vlc_source_owner.h>
#include <assert.h>
#include <stdatomic.h>
#include "../../../lib/libvlc_internal.h"

/* The private copy supplies controlled allocation and callback delays. */
static atomic_bool fail_allocate, fail_copy, pause_copy;
static vlc_sem_t copy_entered, copy_release;
static void *scope_calloc(size_t count, size_t bytes)
{ return atomic_load(&fail_allocate) ? NULL : calloc(count, bytes); }
static char *scope_strdup(const char *value)
{
    if (atomic_load(&pause_copy)) {
        vlc_sem_post(&copy_entered);
        assert(vlc_sem_timedwait(&copy_release, vlc_tick_now() + VLC_TICK_FROM_SEC(5)) == 0);
    }
    return atomic_load(&fail_copy) ? NULL : strdup(value);
}
#define calloc scope_calloc
#define strdup scope_strdup
#include "../../../src/input/addon_source.c"
#undef calloc
#undef strdup

static const char source_uri[] = "vlc-addon-source:v1:admitted";
static const char proxy_uri[] = "http://127.0.0.1:9000/token/resource";
enum mode { INLINE, PENDING, ERROR, CHANGE_URI, CHANGE_ITEM_POLICY, CHANGE_OBJECT_POLICY, INTERRUPT, ASYNC };
struct fixture {
    libvlc_int_t *instance;
    vlc_object_t *object;
    input_item_t *item;
    input_addon_source_t scope;
    vlc_source_backend_t *backend;
    vlc_source_job_t *job;
    vlc_input_policy_t *policy;
    vlc_source_lease_t *lease;
    vlc_interrupt_t *interrupt;
    vlc_thread_t completion;
    vlc_sem_t dispatched;
    enum mode mode;
    atomic_uint calls, cancelled, destroyed;
    uint64_t generation;
    bool raise_in_cancel;
};
static void lease_cancel(void *opaque)
{
    struct fixture *state = opaque;
    ++state->cancelled;
    if (state->raise_in_cancel) vlc_interrupt_raise(state->interrupt);
}
static void lease_destroy(void *opaque) { ++((struct fixture *)opaque)->destroyed; }
static bool complete(struct fixture *state)
{
    const struct vlc_source_result result = {proxy_uri, state->policy, state->lease};
    return vlc_source_job_Complete(state->job, VLC_SUCCESS, &result);
}
static void *complete_thread(void *opaque)
{ assert(complete(opaque)); return NULL; }
static int dispatch(void *opaque, vlc_source_job_t *job)
{
    struct fixture *state = opaque;
    ++state->calls;
    state->generation = vlc_source_job_GetRequest(job)->input_generation;
    assert(state->generation != 0);
    assert(!strcmp(vlc_source_job_GetRequest(job)->source_uri, source_uri));
    state->job = vlc_source_job_Hold(job);
    assert(vlc_source_job_AttachTransport(job, state->policy, state->lease));
    vlc_sem_post(&state->dispatched);
    switch (state->mode) {
        case ERROR: return VLC_EGENERIC;
        case PENDING: return VLC_SUCCESS;
        case CHANGE_URI: input_item_SetURI(state->item, "vlc-addon-source:v1:changed"); break;
        case CHANGE_ITEM_POLICY:
            assert(vlc_input_policy_AttachItem(state->item, state->policy) != VLC_SUCCESS); break;
        case CHANGE_OBJECT_POLICY:
            assert(vlc_input_policy_AttachObject(state->object, state->policy) != VLC_SUCCESS); break;
        case INTERRUPT: vlc_interrupt_raise(state->interrupt); return VLC_SUCCESS;
        case ASYNC:
            assert(vlc_clone(&state->completion, complete_thread, state) == 0);
            assert(vlc_sem_timedwait(&copy_entered, vlc_tick_now() + VLC_TICK_FROM_SEC(5)) == 0);
            vlc_sem_post(&copy_entered);
            return VLC_SUCCESS;
        default: break;
    }
    assert(complete(state)); return VLC_SUCCESS;
}
static void init(struct fixture *state, enum mode mode, bool bind)
{
    memset(state, 0, sizeof(*state)); state->mode = mode;
    state->instance = libvlc_InternalCreate(); assert(state->instance != NULL);
    state->object = vlc_object_create(VLC_OBJECT(state->instance), sizeof(*state->object));
    state->item = input_item_New(source_uri, "Source");
    assert(state->object != NULL && state->item != NULL);
    vlc_input_policy_t *source = vlc_input_policy_HoldSourceItem(state->item, source_uri);
    assert(source != NULL && vlc_input_policy_AttachObject(state->object, source) == VLC_SUCCESS);
    vlc_input_policy_Release(source);
    input_addon_source_Init(&state->scope); vlc_sem_init(&state->dispatched, 0);
    static const struct vlc_source_backend_operations backend_ops = {.dispatch = dispatch};
    static const struct vlc_source_lease_operations lease_ops = {lease_cancel, lease_destroy};
    state->backend = vlc_source_backend_New(&backend_ops, state);
    state->lease = vlc_source_lease_New(&lease_ops, state);
    state->policy = vlc_input_policy_NewProxy(9000, "/token/", NULL, false);
    state->interrupt = vlc_interrupt_create();
    assert(state->backend && state->lease && state->policy && state->interrupt);
    if (bind) assert(vlc_source_owner_Set(VLC_OBJECT(state->instance), state->backend) == VLC_SUCCESS);
}
static void clean(struct fixture *state)
{
    input_addon_source_Clean(&state->scope);
    if (state->object) vlc_object_delete(state->object);
    input_item_Release(state->item);
    if (state->job) vlc_source_job_Release(state->job);
    vlc_source_backend_Close(state->backend);
    vlc_source_backend_Release(state->backend);
    libvlc_InternalDestroy(state->instance);
    vlc_source_lease_Release(state->lease);
    vlc_input_policy_Release(state->policy);
    vlc_interrupt_destroy(state->interrupt);
    assert(state->cancelled == 1 && state->destroyed == 1);
}
static int resolve(struct fixture *state, vlc_tick_t timeout, char **output)
{
    vlc_interrupt_t *previous = vlc_interrupt_set(state->interrupt);
    int status = input_addon_source_Resolve(&state->scope, state->object, state->item,
        source_uri, true, timeout, output);
    vlc_interrupt_set(previous); return status;
}
static void success_and_input_lifetime(void)
{
    struct fixture state; init(&state, INLINE, true); char *output = NULL;
    assert(resolve(&state, VLC_TICK_FROM_SEC(1), &output) == VLC_SUCCESS);
    assert(output != NULL && !strcmp(output, proxy_uri)); free(output);
    assert(state.calls == 1 && state.cancelled == 0);
    vlc_input_policy_t *source = vlc_input_policy_HoldItem(state.item);
    assert(vlc_input_policy_IsSource(source, source_uri));
    assert(vlc_input_policy_CheckURL(state.object, proxy_uri));
    assert(!vlc_input_policy_CheckURL(state.object, "https://example.invalid/stream"));
    /* A resource EOF does not close the scope. Stop does. */
    input_addon_source_Close(&state.scope);
    assert(state.cancelled == 1 && vlc_input_policy_IsRevoked(state.policy));
    assert(vlc_input_policy_IsSource(source, source_uri));
    assert(!vlc_input_policy_CheckURL(state.object, proxy_uri));
    assert(resolve(&state, VLC_TICK_FROM_SEC(1), &output) == -EINTR && output == NULL);
    vlc_input_policy_Release(source); clean(&state);
}
static void admission_and_failure_cases(void)
{
    const enum mode modes[] = {ERROR, CHANGE_URI, CHANGE_ITEM_POLICY, CHANGE_OBJECT_POLICY};
    for (size_t i = 0; i < ARRAY_SIZE(modes); ++i) {
        struct fixture state; init(&state, modes[i], true); char *output = NULL;
        assert(resolve(&state, VLC_TICK_FROM_SEC(1), &output) != VLC_SUCCESS);
        assert(output == NULL && state.cancelled == 1); clean(&state);
    }
    for (unsigned absent = 0; absent < 3; ++absent) {
        struct fixture state; init(&state, INLINE, absent != 0); char *output = NULL;
        if (absent == 1) vlc_source_backend_Close(state.backend);
        if (absent == 2) input_addon_source_Close(&state.scope);
        assert(resolve(&state, VLC_TICK_FROM_SEC(1), &output) != VLC_SUCCESS);
        assert(state.calls == 0 && output == NULL); clean(&state);
    }
}
static void native_gate_and_bounds(void)
{
    struct fixture state; init(&state, INLINE, true); char *output = (char *)1;
    assert(input_addon_source_IsURI("VLC-ADDON-SOURCE:v1:x"));
    assert(!input_addon_source_IsURI("https://example.invalid"));
    assert(input_addon_source_Resolve(&state.scope, state.object, state.item,
        source_uri, false, VLC_TICK_FROM_SEC(1), &output) == VLC_EINVAL && output == NULL);
    assert(input_addon_source_Resolve(&state.scope, state.object, state.item,
        "VLC-ADDON-SOURCE:v1:admitted", true, VLC_TICK_FROM_SEC(1), &output) == VLC_EINVAL);
    assert(resolve(&state, 0, &output) == VLC_EINVAL);
    assert(resolve(&state, VLC_TICK_FROM_SEC(10) + 1, &output) == VLC_EINVAL);
    vlc_object_delete(state.object);
    state.object = vlc_object_create(VLC_OBJECT(state.instance), sizeof(*state.object));
    assert(state.object != NULL);
    assert(resolve(&state, VLC_TICK_FROM_SEC(1), &output) != VLC_SUCCESS && output == NULL);
    assert(state.calls == 0); clean(&state);
}
static void interrupt_and_timeout(void)
{
    for (unsigned prior = 0; prior < 2; ++prior) {
        struct fixture state; init(&state, INTERRUPT, true); state.raise_in_cancel = true;
        if (prior) vlc_interrupt_raise(state.interrupt);
        char *output = NULL;
        assert(resolve(&state, VLC_TICK_FROM_SEC(1), &output) == -EINTR);
        assert(output == NULL && state.calls == (prior ? 0 : 1)); clean(&state);
    }
    struct fixture state; init(&state, PENDING, true); char *output = NULL;
    assert(resolve(&state, VLC_TICK_FROM_MS(5), &output) == -ETIMEDOUT);
    assert(output == NULL && state.cancelled == 1); clean(&state);
}
struct resolver { struct fixture *state; int status; char *output; };
static void *resolve_thread(void *opaque)
{
    struct resolver *call = opaque;
    call->status = resolve(call->state, VLC_TICK_FROM_SEC(2), &call->output); return NULL;
}
static void close_pending(void)
{
    struct fixture state; init(&state, PENDING, true);
    struct resolver call = {.state = &state}; vlc_thread_t thread;
    assert(vlc_clone(&thread, resolve_thread, &call) == 0);
    assert(vlc_sem_timedwait(&state.dispatched, vlc_tick_now() + VLC_TICK_FROM_SEC(5)) == 0);
    input_addon_source_Close(&state.scope); vlc_join(thread, NULL);
    assert(call.status == -EINTR && call.output == NULL && state.cancelled == 1);
    clean(&state);
}
static void allocation_failure(void)
{
    for (unsigned copy = 0; copy < 2; ++copy) {
        struct fixture state; init(&state, INLINE, true); char *output = NULL;
        atomic_store(copy ? &fail_copy : &fail_allocate, true);
        assert(resolve(&state, VLC_TICK_FROM_SEC(1), &output) == VLC_ENOMEM);
        atomic_store(copy ? &fail_copy : &fail_allocate, false);
        assert(output == NULL && state.calls == copy); clean(&state);
    }
}
static void callback_can_outlive_input(void)
{
    struct fixture state; init(&state, ASYNC, true); char *output = NULL;
    vlc_sem_init(&copy_entered, 0); vlc_sem_init(&copy_release, 0);
    atomic_store(&pause_copy, true);
    assert(resolve(&state, VLC_TICK_FROM_MS(30), &output) == -ETIMEDOUT);
    assert(vlc_sem_timedwait(&copy_entered, vlc_tick_now() + VLC_TICK_FROM_SEC(5)) == 0);
    /* Complete has won, but its consumer callback is still blocked. */
    assert(output == NULL && state.cancelled == 1 && vlc_input_policy_IsRevoked(state.policy));
    input_addon_source_Clean(&state.scope);
    vlc_object_delete(state.object); state.object = NULL;
    atomic_store(&pause_copy, false); vlc_sem_post(&copy_release);
    vlc_join(state.completion, NULL);
    clean(&state);
}
int main(void)
{
    success_and_input_lifetime(); admission_and_failure_cases(); native_gate_and_bounds();
    interrupt_and_timeout(); close_pending(); allocation_failure(); callback_can_outlive_input();
    return 0;
}
