// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_artwork_policy.h>
#include <vlc_input_item.h>
#include <vlc_input_policy.h>
#include <vlc_metadata_backend.h>
#include <vlc_metadata_owner.h>
#include <vlc_threads.h>
#include <stdatomic.h>
#include <assert.h>
#include "../../../lib/libvlc_internal.h"
#include "../../../src/preparser/metadata_owner.c"
static _Thread_local bool pause_child_start;
static _Thread_local bool pause_anchor_complete;
static vlc_sem_t child_listed, resume_child;
static vlc_sem_t anchor_completed, resume_commit;
static int child_start(vlc_metadata_job_t *job)
{
    if (pause_child_start) { pause_child_start = false; vlc_sem_post(&child_listed); vlc_sem_wait(&resume_child); }
    return vlc_metadata_job_Start(job);
}
static bool complete_then_pause(vlc_metadata_job_t *job, int status, input_item_t *item,
                               const char *provenance, size_t size)
{
    const bool completed = vlc_metadata_job_Complete(job, status, item, provenance, size);
    if (pause_anchor_complete && !strcmp(vlc_metadata_job_GetRequest(job)->operation, "anchor")) {
        pause_anchor_complete = false; vlc_sem_post(&anchor_completed); vlc_sem_wait(&resume_commit);
    }
    return completed;
}
#define vlc_metadata_job_Start child_start
#define vlc_metadata_job_Complete complete_then_pause
#include "../../../src/preparser/metadata_pipeline.c"
#undef vlc_metadata_job_Start
#undef vlc_metadata_job_Complete

struct observed { char feature[64], operation[64]; unsigned flags; };
struct fixture {
    libvlc_int_t *instance;
    vlc_metadata_backend_t *backend;
    vlc_metadata_pipeline_t *pipeline;
    input_item_t *item;
    vlc_metadata_pipeline_stage_t *locals[512];
    vlc_metadata_job_t *held[512];
    struct observed observed[2048];
    unsigned local_count, local_cancels, held_count, calls, remote_calls;
    bool synchronous_local, synchronous_remote, complete_local_cancel;
    bool materialize, force_native_replacement, mutate_uri, close_on_cancel;
};
struct result { struct fixture *fixture; unsigned calls; int status; bool delete_owner; };
static void on_ended(input_item_t *item, int status, void *opaque)
{
    struct result *result = opaque; assert(item == result->fixture->item); ++result->calls; result->status = status;
    if (result->delete_owner) {
        vlc_metadata_pipeline_t *owner = result->fixture->pipeline; result->fixture->pipeline = NULL; vlc_metadata_pipeline_Delete(owner);
    }
}
static void local_done(struct fixture *fixture, unsigned index, int status)
{
    vlc_metadata_pipeline_stage_t *stage = fixture->locals[index]; assert(stage != NULL);
    fixture->locals[index] = NULL; vlc_metadata_pipeline_stage_Done(stage, status);
}
static int local_start(void *opaque, vlc_metadata_pipeline_stage_t *stage)
{
    struct fixture *fixture = opaque;
    if (!vlc_metadata_pipeline_stage_IsCurrent(stage)) return -EINTR;
    assert(fixture->local_count < ARRAY_SIZE(fixture->locals)); const unsigned index = fixture->local_count++;
    fixture->locals[index] = stage;
    input_item_t *working = vlc_metadata_pipeline_stage_GetItem(stage); assert(working != fixture->item);
    input_item_SetDescription(working, "Local observation");
    if (fixture->mutate_uri) input_item_SetURI(working, "file:///different/source.flac");
    if (fixture->synchronous_local) local_done(fixture, index, VLC_SUCCESS);
    return VLC_SUCCESS;
}
static void local_cancel(void *opaque, vlc_metadata_pipeline_stage_t *stage)
{
    struct fixture *fixture = opaque; ++fixture->local_cancels;
    assert(!vlc_metadata_pipeline_stage_IsCurrent(stage));
    if (fixture->complete_local_cancel)
        for (unsigned i = 0; i < fixture->local_count; ++i)
            if (fixture->locals[i] == stage) { local_done(fixture, i, -EINTR); break; }
}
static void complete_remote(struct fixture *fixture, vlc_metadata_job_t *job, int status)
{
    const struct vlc_metadata_request *request = vlc_metadata_job_GetRequest(job);
    input_item_t *copy = input_item_Copy(request->item); assert(copy != NULL);
    if (!strcmp(request->operation, "materializeNativeArtwork")) {
        if (fixture->materialize && (request->flags & VLC_METADATA_REQUEST_NETWORK)) assert(vlc_artwork_item_SetApprovedURI(copy, "file:///validated/native.png") == VLC_SUCCESS);
    } else if (!strcmp(request->feature, "video.details")) input_item_SetArtist(copy, "Remote artist");
    else if (!strcmp(request->feature, "music.details")) input_item_SetAlbum(copy, "Remote album");
    else if (!strcmp(request->feature, "podcast.details")) input_item_SetGenre(copy, "Remote genre");
    else if (!strcmp(request->feature, "artwork")) assert(vlc_artwork_item_SetApprovedURI(copy, "file:///validated/guest.png") == VLC_SUCCESS);
    assert(vlc_metadata_job_Complete(job, status, copy, "{\"provider\":\"test\"}", 19)); input_item_Release(copy);
}
static int dispatch(void *opaque, vlc_metadata_job_t *job)
{
    struct fixture *fixture = opaque; const struct vlc_metadata_request *request = vlc_metadata_job_GetRequest(job);
    assert(fixture->local_count > 0 && request->item != fixture->item && fixture->calls < ARRAY_SIZE(fixture->observed));
    struct observed *seen = &fixture->observed[fixture->calls++]; seen->flags = request->flags;
    snprintf(seen->feature, sizeof(seen->feature), "%s", request->feature);
    snprintf(seen->operation, sizeof(seen->operation), "%s", request->operation);
    if (strcmp(request->operation, "materializeNativeArtwork")) ++fixture->remote_calls;
    if (fixture->synchronous_remote) complete_remote(fixture, job, VLC_SUCCESS);
    else { assert(fixture->held_count < ARRAY_SIZE(fixture->held)); fixture->held[fixture->held_count++] = vlc_metadata_job_Hold(job); }
    return VLC_SUCCESS;
}
static void backend_cancel(void *opaque, vlc_metadata_job_t *job)
{
    (void)job; struct fixture *fixture = opaque;
    if (fixture->close_on_cancel) vlc_metadata_backend_Close(fixture->backend);
}
static void create(struct fixture *fixture, bool owned)
{
    fixture->instance = libvlc_InternalCreate(); assert(fixture->instance != NULL);
    assert(var_Create(fixture->instance, "metadata-network-access", VLC_VAR_BOOL) == VLC_SUCCESS);
    assert(var_SetBool(fixture->instance, "metadata-network-access", true) == VLC_SUCCESS);
    const struct vlc_metadata_backend_operations backend_ops = {.dispatch=dispatch, .cancel=backend_cancel};
    fixture->backend = vlc_metadata_backend_New(&backend_ops, fixture); assert(fixture->backend != NULL);
    const struct vlc_metadata_pipeline_operations pipeline_ops = {.local=local_start, .cancel_local=local_cancel};
    fixture->pipeline = vlc_metadata_pipeline_New(VLC_OBJECT(fixture->instance), &pipeline_ops, fixture); assert(fixture->pipeline != NULL);
    fixture->item = input_item_New("file:///private/song.flac", "Song"); assert(fixture->item != NULL); input_item_SetTitle(fixture->item, "Song");
    if (owned) assert(vlc_metadata_owner_Set(VLC_OBJECT(fixture->instance), fixture->backend) == VLC_SUCCESS);
}
static void destroy(struct fixture *fixture)
{
    fixture->complete_local_cancel = true;
    if (fixture->pipeline != NULL) { vlc_metadata_pipeline_Delete(fixture->pipeline); fixture->pipeline = NULL; }
    vlc_metadata_owner_Close(VLC_OBJECT(fixture->instance));
    vlc_metadata_owner_Forget(VLC_OBJECT(fixture->instance));
    vlc_metadata_backend_Close(fixture->backend);
    for (unsigned i = 0; i < fixture->held_count; ++i) vlc_metadata_job_Release(fixture->held[i]);
    vlc_metadata_backend_Release(fixture->backend); input_item_Release(fixture->item); libvlc_InternalDestroy(fixture->instance);
}
static int push(struct fixture *fixture, struct result *result, int options)
{ result->fixture = fixture; return vlc_metadata_pipeline_Push(fixture->pipeline, fixture->item, options, NULL, result, on_ended, result); }
static void original_snapshot_and_stage_order(void)
{
    struct fixture fixture = {.synchronous_remote=true}; create(&fixture, true); struct result result = {0};
    input_item_SetArtURL(fixture.item, "http://native.lan/cover.png"); fixture.materialize = true;
    assert(push(&fixture, &result, VLC_PREPARSER_TYPE_FETCHMETA_ALL) == VLC_SUCCESS);
    assert(result.calls == 0 && fixture.calls == 0);
    char *description = input_item_GetDescription(fixture.item); assert(description == NULL); free(description);
    local_done(&fixture, 0, VLC_SUCCESS);
    assert(result.calls == 1 && result.status == VLC_SUCCESS && fixture.calls == STAGE_COUNT);
    const char *features[] = {"artwork", "video.details", "music.details", "podcast.details", "artwork"};
    for (unsigned i = 0; i < STAGE_COUNT; ++i) assert(!strcmp(fixture.observed[i].feature, features[i]));
    assert(!strcmp(fixture.observed[0].operation, "materializeNativeArtwork"));
    description = input_item_GetDescription(fixture.item); assert(description != NULL && !strcmp(description, "Local observation")); free(description);
    char *artist = input_item_GetArtist(fixture.item); assert(artist != NULL && !strcmp(artist, "Remote artist")); free(artist);
    char *artwork = vlc_artwork_item_GetURI(fixture.item); assert(artwork != NULL && !strcmp(artwork, "file:///validated/native.png")); free(artwork);
    destroy(&fixture);
}
static void a_same_value_user_edit_refuses_the_whole_staged_patch(void)
{
    struct fixture fixture = {.synchronous_remote=true}; create(&fixture, true); struct result result = {0};
    assert(push(&fixture, &result, VLC_PREPARSER_TYPE_FETCHMETA_ALL) == VLC_SUCCESS);
    input_item_SetTitle(fixture.item, "Song"); local_done(&fixture, 0, VLC_SUCCESS);
    assert(result.calls == 1 && result.status == -EAGAIN);
    char *description = input_item_GetDescription(fixture.item); assert(description == NULL); free(description);
    char *artist = input_item_GetArtist(fixture.item); assert(artist == NULL); free(artist); destroy(&fixture);
}
static void protected_native_artwork_is_not_an_automatic_replacement(void)
{
    struct fixture fixture = {.synchronous_local=true, .synchronous_remote=true, .materialize=true}; create(&fixture, true);
    assert(vlc_artwork_item_SetApprovedURI(fixture.item, "file:///private/user.png") == VLC_SUCCESS);
    struct result result = {0}; assert(push(&fixture, &result, VLC_PREPARSER_TYPE_FETCHMETA_ALL) == VLC_SUCCESS);
    assert(result.calls == 1 && result.status == VLC_SUCCESS);
    char *artwork = vlc_artwork_item_GetURI(fixture.item); assert(artwork != NULL && !strcmp(artwork, "file:///private/user.png")); free(artwork); destroy(&fixture);
}
static void only_unowned_enoent_permits_the_parent_legacy_path(void)
{
    struct fixture fixture = {0}; create(&fixture, false); struct result result = {0};
    assert(push(&fixture, &result, VLC_PREPARSER_TYPE_FETCHMETA_ALL) == VLC_ENOENT);
    assert(result.calls == 0 && fixture.local_count == 0);
    vlc_metadata_owner_Close(VLC_OBJECT(fixture.instance));
    assert(push(&fixture, &result, VLC_PREPARSER_TYPE_FETCHMETA_ALL) == -EINTR);
    assert(result.calls == 0 && fixture.local_count == 0); destroy(&fixture);
    struct fixture closed = {0}; create(&closed, true); vlc_metadata_backend_Close(closed.backend);
    struct result denied = {0}; assert(push(&closed, &denied, VLC_PREPARSER_TYPE_FETCHMETA_ALL) == -EINTR); destroy(&closed);
}
static void local_only_work_never_calls_a_remote_provider(void)
{
    struct fixture fixture = {.synchronous_local=true, .synchronous_remote=true}; create(&fixture, true); struct result result = {0};
    assert(var_SetBool(fixture.instance, "metadata-network-access", false) == VLC_SUCCESS);
    assert(push(&fixture, &result, VLC_PREPARSER_TYPE_FETCHMETA_LOCAL) == VLC_SUCCESS);
    assert(result.calls == 1 && result.status == VLC_SUCCESS && fixture.calls == 1 && fixture.remote_calls == 0);
    struct result network = {0}; assert(push(&fixture, &network, VLC_PREPARSER_TYPE_FETCHMETA_ALL) == VLC_SUCCESS);
    assert(network.calls == 1 && network.status == VLC_SUCCESS && fixture.local_count == 2 && fixture.calls == 2 && fixture.remote_calls == 0);
    assert(fixture.observed[1].flags == 0); destroy(&fixture);
}
static void disabled_network_stays_local_after_a_later_consent_grant(void)
{
    struct fixture fixture = {.synchronous_remote=true, .materialize=true}; create(&fixture, true); struct result result = {0};
    input_item_SetArtURL(fixture.item, "http://native.lan/original.png");
    assert(var_SetBool(fixture.instance, "metadata-network-access", false) == VLC_SUCCESS);
    assert(push(&fixture, &result, VLC_PREPARSER_TYPE_FETCHMETA_ALL) == VLC_SUCCESS);
    assert(fixture.local_count == 1 && fixture.calls == 0 && result.calls == 0);
    assert(var_SetBool(fixture.instance, "metadata-network-access", true) == VLC_SUCCESS);
    local_done(&fixture, 0, VLC_SUCCESS);
    assert(result.calls == 1 && result.status == VLC_SUCCESS && fixture.calls == 1 && fixture.remote_calls == 0);
    assert(fixture.observed[0].flags == 0);
    char *artwork = input_item_GetArtURL(fixture.item); assert(artwork != NULL && !strcmp(artwork, "http://native.lan/original.png")); free(artwork);
    char *description = input_item_GetDescription(fixture.item); assert(description != NULL && !strcmp(description, "Local observation")); free(description);
    destroy(&fixture);
}
struct push_thread { struct fixture *fixture; struct result *result; int status; };
static void *push_paused(void *opaque)
{
    struct push_thread *call = opaque; pause_child_start = true;
    call->status = push(call->fixture, call->result, VLC_PREPARSER_TYPE_FETCHMETA_ALL); return NULL;
}
static void a_parent_guard_cancels_before_the_pipeline_handoff(void)
{
    struct fixture fixture = {.synchronous_local=true, .synchronous_remote=true}; create(&fixture, true);
    struct result result = {.fixture=&fixture};
    vlc_metadata_pipeline_guard_t *guard = vlc_metadata_pipeline_guard_New(); assert(guard != NULL);
    vlc_metadata_pipeline_guard_Cancel(guard); assert(!vlc_metadata_pipeline_guard_IsCurrent(guard));
    assert(vlc_metadata_pipeline_Push(fixture.pipeline, fixture.item, VLC_PREPARSER_TYPE_FETCHMETA_ALL,
        guard, &result, on_ended, &result) == -EINTR);
    assert(result.calls == 0 && fixture.local_count == 0 && fixture.calls == 0);
    vlc_metadata_pipeline_guard_Release(guard); destroy(&fixture);
}
struct guarded_commit {
    struct fixture *fixture;
    struct result *result;
    vlc_metadata_pipeline_guard_t *guard;
    int status;
};
static void *complete_before_commit(void *opaque)
{
    struct guarded_commit *call = opaque; pause_anchor_complete = true;
    call->status = vlc_metadata_pipeline_Push(call->fixture->pipeline, call->fixture->item,
        VLC_PREPARSER_TYPE_FETCHMETA_LOCAL, call->guard, call->result, on_ended, call->result);
    return NULL;
}
static void a_parent_guard_cancels_after_completion_before_final_admission(void)
{
    struct fixture fixture = {.synchronous_local=true, .synchronous_remote=true}; create(&fixture, true);
    struct result result = {.fixture=&fixture};
    vlc_metadata_pipeline_guard_t *guard = vlc_metadata_pipeline_guard_New(); assert(guard != NULL);
    struct guarded_commit call = {&fixture, &result, guard, VLC_EGENERIC};
    vlc_sem_init(&anchor_completed, 0); vlc_sem_init(&resume_commit, 0);
    vlc_thread_t thread; assert(vlc_clone(&thread, complete_before_commit, &call) == 0);
    vlc_sem_wait(&anchor_completed); assert(result.calls == 0);
    // Parent cancellation wins before final admission, without a list walk.
    vlc_metadata_pipeline_guard_Cancel(guard); vlc_sem_post(&resume_commit); vlc_join(thread, NULL);
    assert(call.status == VLC_SUCCESS && result.calls == 1 && result.status == -EINTR);
    char *description = input_item_GetDescription(fixture.item); assert(description == NULL); free(description);
    vlc_metadata_pipeline_guard_Release(guard); destroy(&fixture);
}
static void parent_cancel_finds_a_child_before_start(void)
{
    struct fixture fixture = {.synchronous_local=true}; create(&fixture, true); struct result result = {0};
    struct push_thread call = {&fixture, &result, VLC_EGENERIC}; vlc_sem_init(&child_listed, 0); vlc_sem_init(&resume_child, 0);
    vlc_thread_t thread; assert(vlc_clone(&thread, push_paused, &call) == 0); vlc_sem_wait(&child_listed);
    assert(vlc_metadata_pipeline_Cancel(fixture.pipeline, &result) == 1 && result.calls == 1 && result.status == -EINTR);
    vlc_sem_post(&resume_child); vlc_join(thread, NULL); assert(call.status == VLC_SUCCESS && fixture.calls == 0); destroy(&fixture);
}
static void native_owner_close_and_late_results_need_no_qt_event(void)
{
    struct fixture fixture = {.synchronous_local=true}; create(&fixture, true); struct result result = {0};
    assert(push(&fixture, &result, VLC_PREPARSER_TYPE_FETCHMETA_ALL) == VLC_SUCCESS && fixture.held_count == 1);
    vlc_metadata_owner_Close(VLC_OBJECT(fixture.instance)); assert(result.calls == 1 && result.status == -EINTR);
    assert(!vlc_metadata_job_Complete(fixture.held[0], VLC_SUCCESS, NULL, NULL, 0)); assert(result.calls == 1); destroy(&fixture);
}
static bool allow_uri(void *opaque, const char *uri) { (void)opaque; (void)uri; return true; }
static bool allow_connection(void *opaque, const char *host, unsigned port, bool tls)
{ (void)opaque; (void)host; (void)port; (void)tls; return true; }
static void a_revoked_policy_or_changed_working_uri_cannot_start_remote_work(void)
{
    struct fixture fixture = {.synchronous_remote=true}; create(&fixture, true);
    const struct vlc_input_policy_operations operations = {.allow_url=allow_uri, .allow_connection=allow_connection};
    vlc_input_policy_t *policy = vlc_input_policy_New(&operations, NULL); assert(policy != NULL);
    assert(vlc_input_policy_AttachItem(fixture.item, policy) == VLC_SUCCESS); struct result result = {0};
    assert(push(&fixture, &result, VLC_PREPARSER_TYPE_FETCHMETA_ALL) == VLC_SUCCESS);
    vlc_input_policy_Revoke(policy); assert(!vlc_metadata_pipeline_stage_IsCurrent(fixture.locals[0])); local_done(&fixture, 0, VLC_SUCCESS);
    assert(result.calls == 1 && result.status == -EACCES && fixture.calls == 0); destroy(&fixture); vlc_input_policy_Release(policy);
    struct fixture changed = {.synchronous_local=true, .synchronous_remote=true, .mutate_uri=true}; create(&changed, true);
    struct result denied = {0}; assert(push(&changed, &denied, VLC_PREPARSER_TYPE_FETCHMETA_ALL) == VLC_SUCCESS);
    assert(denied.calls == 1 && denied.status == -EINTR && changed.calls == 0); destroy(&changed);
}
static void completion_can_delete_the_pipeline_and_cancel_can_reenter_close(void)
{
    struct fixture fixture = {.synchronous_local=true, .synchronous_remote=true}; create(&fixture, true);
    struct result result = {.delete_owner=true}; assert(push(&fixture, &result, VLC_PREPARSER_TYPE_FETCHMETA_ALL) == VLC_SUCCESS);
    assert(result.calls == 1 && result.status == VLC_SUCCESS && fixture.pipeline == NULL); destroy(&fixture);
    struct fixture reentry = {.synchronous_local=true, .close_on_cancel=true}; create(&reentry, true);
    struct result first = {0}, second = {.delete_owner=true};
    assert(push(&reentry, &first, VLC_PREPARSER_TYPE_FETCHMETA_ALL) == VLC_SUCCESS);
    assert(push(&reentry, &second, VLC_PREPARSER_TYPE_FETCHMETA_ALL) == VLC_SUCCESS);
    vlc_metadata_pipeline_t *owner = reentry.pipeline;
    assert(vlc_metadata_pipeline_Cancel(owner, &first) == 1);
    assert(first.calls == 1 && second.calls == 1 && reentry.pipeline == NULL); destroy(&reentry);
}
static void cancelled_local_tokens_keep_the_retained_limit_until_done(void)
{
    struct fixture fixture = {0}; create(&fixture, true); struct result results[MAX_ENTRIES + 2]; memset(results, 0, sizeof(results));
    for (unsigned i = 0; i < MAX_ENTRIES; ++i) assert(push(&fixture, &results[i], VLC_PREPARSER_TYPE_FETCHMETA_ALL) == VLC_SUCCESS);
    assert(push(&fixture, &results[MAX_ENTRIES], VLC_PREPARSER_TYPE_FETCHMETA_ALL) == VLC_ENOMEM);
    assert(vlc_metadata_pipeline_Cancel(fixture.pipeline, &results[0]) == 1 && results[0].calls == 1);
    assert(push(&fixture, &results[MAX_ENTRIES], VLC_PREPARSER_TYPE_FETCHMETA_ALL) == VLC_ENOMEM);
    local_done(&fixture, 0, -EINTR);
    assert(push(&fixture, &results[MAX_ENTRIES], VLC_PREPARSER_TYPE_FETCHMETA_ALL) == VLC_SUCCESS);
    vlc_metadata_pipeline_Close(fixture.pipeline);
    for (unsigned i = 0; i < fixture.local_count; ++i) if (fixture.locals[i] != NULL) local_done(&fixture, i, -EINTR);
    for (unsigned i = 0; i <= MAX_ENTRIES; ++i) assert(results[i].calls == 1);
    destroy(&fixture);
}
int main(void)
{
    original_snapshot_and_stage_order();
    a_same_value_user_edit_refuses_the_whole_staged_patch();
    protected_native_artwork_is_not_an_automatic_replacement();
    only_unowned_enoent_permits_the_parent_legacy_path();
    local_only_work_never_calls_a_remote_provider();
    disabled_network_stays_local_after_a_later_consent_grant();
    a_parent_guard_cancels_before_the_pipeline_handoff();
    a_parent_guard_cancels_after_completion_before_final_admission();
    parent_cancel_finds_a_child_before_start();
    native_owner_close_and_late_results_need_no_qt_event();
    a_revoked_policy_or_changed_working_uri_cannot_start_remote_work();
    completion_can_delete_the_pipeline_and_cancel_can_reenter_close();
    cancelled_local_tokens_keep_the_retained_limit_until_done();
    return 0;
}
