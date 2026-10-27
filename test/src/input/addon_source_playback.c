// SPDX-License-Identifier: LGPL-2.1-or-later
#include "../../libvlc/test.h"
#include "../../../lib/libvlc_internal.h"
#include <vlc_input_item.h>
#include <vlc_input_policy.h>
#include <vlc_source_owner.h>
#include <vlc_player.h>
#include <vlc_preparser.h>
#include <vlc_picture.h>
#include <vlc_fs.h>
#include <vlc_url.h>
#include <stdatomic.h>

static const char source_uri[] = "vlc-addon-source:v1:native_playback_fixture";
static const char proxy_uri[] = "http://127.0.0.1:9/fixture_token/resource";
static const char mock_uri[] = "mock://video_track_count=0;audio_track_count=1;length=60000000";
static const char thumbnail_uri[] = "mock://video_track_count=1;audio_track_count=0;length=5000000;video_chroma=ARGB";

struct fixture {
    libvlc_instance_t *instance;
    vlc_player_t *player;
    vlc_player_listener_id *listener;
    vlc_source_backend_t *backend;
    vlc_source_job_t *job;
    vlc_input_policy_t *policy;
    vlc_source_lease_t *lease;
    vlc_sem_t dispatched, cancelled, events;
    atomic_uint dispatch_count, cancel_count, destroy_count, states, subtitle_tracks;
    bool pending;
};

static void lease_cancel(void *opaque)
{
    struct fixture *fixture = opaque;
    atomic_fetch_add(&fixture->cancel_count, 1);
    vlc_sem_post(&fixture->cancelled);
}

static void lease_destroy(void *opaque)
{ atomic_fetch_add(&((struct fixture *)opaque)->destroy_count, 1); }

static bool complete(struct fixture *fixture)
{
    const struct vlc_source_result result = {
        .proxy_uri = proxy_uri, .policy = fixture->policy, .lease = fixture->lease,
    };
    return vlc_source_job_Complete(fixture->job, VLC_SUCCESS, &result);
}

static int dispatch(void *opaque, vlc_source_job_t *job)
{
    struct fixture *fixture = opaque;
    assert(atomic_fetch_add(&fixture->dispatch_count, 1) == 0);
    const struct vlc_source_request *request = vlc_source_job_GetRequest(job);
    assert(request->input_generation != 0 && !strcmp(request->source_uri, source_uri));
    fixture->job = vlc_source_job_Hold(job);
    assert(vlc_source_job_AttachTransport(job, fixture->policy, fixture->lease));
    vlc_sem_post(&fixture->dispatched);
    if (!fixture->pending) assert(complete(fixture));
    return VLC_SUCCESS;
}

static void state_changed(vlc_player_t *player, enum vlc_player_state state, void *opaque)
{
    (void)player;
    struct fixture *fixture = opaque;
    atomic_fetch_or(&fixture->states, 1u << state);
    vlc_sem_post(&fixture->events);
}

static void tracks_changed(vlc_player_t *player, enum vlc_player_list_action action,
                           const struct vlc_player_track *track, void *opaque)
{
    (void)player;
    struct fixture *fixture = opaque;
    if (action == VLC_PLAYER_LIST_ADDED && track->fmt.i_cat == SPU_ES) {
        atomic_fetch_add(&fixture->subtitle_tracks, 1);
        vlc_sem_post(&fixture->events);
    }
}

static void wait_state(struct fixture *fixture, enum vlc_player_state state)
{
    const vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(5);
    while (!(atomic_load(&fixture->states) & (1u << state)))
        assert(vlc_sem_timedwait(&fixture->events, deadline) == 0);
}

static void init(struct fixture *fixture, bool pending)
{
    memset(fixture, 0, sizeof(*fixture)); fixture->pending = pending;
    atomic_init(&fixture->dispatch_count, 0); atomic_init(&fixture->cancel_count, 0);
    atomic_init(&fixture->destroy_count, 0); atomic_init(&fixture->states, 0);
    atomic_init(&fixture->subtitle_tracks, 0);
    vlc_sem_init(&fixture->dispatched, 0); vlc_sem_init(&fixture->cancelled, 0);
    vlc_sem_init(&fixture->events, 0);
    static const char *const args[] = {"--ignore-config", "--quiet", "--no-plugins-cache",
        "--no-media-library", "--no-metadata-network-access", "--aout=adummy", "--vout=vdummy",
        "--text-renderer=tdummy"};
    fixture->instance = libvlc_new(ARRAY_SIZE(args), args); assert(fixture->instance);
    assert(var_SetString(fixture->instance->p_libvlc_int, "window", "wdummy") == VLC_SUCCESS);
    static const struct vlc_source_backend_operations backend_ops = {.dispatch = dispatch};
    static const struct vlc_source_lease_operations lease_ops = {lease_cancel, lease_destroy};
    fixture->backend = vlc_source_backend_New(&backend_ops, fixture);
    fixture->lease = vlc_source_lease_New(&lease_ops, fixture);
    fixture->policy = vlc_input_policy_NewProxy(9, "/fixture_token/", NULL, false);
    assert(fixture->backend && fixture->lease && fixture->policy);
    assert(vlc_source_owner_Set(VLC_OBJECT(fixture->instance->p_libvlc_int), fixture->backend) == VLC_SUCCESS);
}

static void new_player(struct fixture *fixture)
{
    fixture->player = vlc_player_New(VLC_OBJECT(fixture->instance->p_libvlc_int), VLC_PLAYER_LOCK_NORMAL);
    assert(fixture->player);
    static const struct vlc_player_cbs cbs = {
        .on_state_changed = state_changed, .on_track_list_changed = tracks_changed,
    };
    vlc_player_Lock(fixture->player);
    fixture->listener = vlc_player_AddListener(fixture->player, &cbs, fixture);
    assert(fixture->listener);
    vlc_player_Unlock(fixture->player);
}

static void delete_player(struct fixture *fixture)
{
    if (!fixture->player) return;
    vlc_player_Lock(fixture->player);
    vlc_player_Stop(fixture->player);
    vlc_player_RemoveListener(fixture->player, fixture->listener);
    vlc_player_Unlock(fixture->player);
    vlc_player_Delete(fixture->player); fixture->player = NULL; fixture->listener = NULL;
}

static void clean(struct fixture *fixture)
{
    delete_player(fixture);
    vlc_source_backend_Close(fixture->backend);
    if (fixture->job) vlc_source_job_Release(fixture->job);
    vlc_source_backend_Release(fixture->backend);
    libvlc_release(fixture->instance);
    vlc_input_policy_Release(fixture->policy);
    vlc_source_lease_Release(fixture->lease);
    assert(atomic_load(&fixture->cancel_count) == 1);
    assert(atomic_load(&fixture->destroy_count) == 1);
}

static void primary_dispatch_and_end(void)
{
    struct fixture fixture; init(&fixture, false); new_player(&fixture);
    input_item_t *item = input_item_New(source_uri, "Primary source"); assert(item);
    vlc_player_Lock(fixture.player);
    assert(vlc_player_SetCurrentMedia(fixture.player, item) == VLC_SUCCESS);
    assert(vlc_player_Start(fixture.player) == VLC_SUCCESS);
    vlc_player_Unlock(fixture.player);
    assert(vlc_sem_timedwait(&fixture.dispatched, vlc_tick_now() + VLC_TICK_FROM_SEC(5)) == 0);
    wait_state(&fixture, VLC_PLAYER_STATE_STOPPED);
    delete_player(&fixture);
    assert(atomic_load(&fixture.dispatch_count) == 1 && atomic_load(&fixture.cancel_count) == 1);
    assert(vlc_input_policy_IsRevoked(fixture.policy));
    char *uri = input_item_GetURI(item); assert(uri && !strcmp(uri, source_uri)); free(uri);
    vlc_input_policy_t *source = vlc_input_policy_HoldItem(item);
    assert(vlc_input_policy_IsSource(source, source_uri)); vlc_input_policy_Release(source);
    input_item_Release(item); clean(&fixture);
}

static void stop_before_complete(void)
{
    struct fixture fixture; init(&fixture, true); new_player(&fixture);
    input_item_t *item = input_item_New(source_uri, "Pending source"); assert(item);
    vlc_player_Lock(fixture.player);
    assert(vlc_player_SetCurrentMedia(fixture.player, item) == VLC_SUCCESS);
    assert(vlc_player_Start(fixture.player) == VLC_SUCCESS);
    vlc_player_Unlock(fixture.player);
    assert(vlc_sem_timedwait(&fixture.dispatched, vlc_tick_now() + VLC_TICK_FROM_SEC(5)) == 0);
    vlc_player_Lock(fixture.player); assert(vlc_player_Stop(fixture.player) == VLC_SUCCESS); vlc_player_Unlock(fixture.player);
    assert(vlc_sem_timedwait(&fixture.cancelled, vlc_tick_now() + VLC_TICK_FROM_SEC(5)) == 0);
    delete_player(&fixture);
    assert(atomic_load(&fixture.dispatch_count) == 1 && atomic_load(&fixture.cancel_count) == 1);
    assert(!complete(&fixture)); assert(vlc_input_policy_IsRevoked(fixture.policy));
    input_item_Release(item); clean(&fixture);
}

static void later_source_uri_has_no_dispatch(void)
{
    struct fixture fixture; init(&fixture, false); new_player(&fixture);
    input_item_t *item = input_item_New(mock_uri, "Changed source"); assert(item);
    vlc_player_Lock(fixture.player);
    assert(vlc_player_SetCurrentMedia(fixture.player, item) == VLC_SUCCESS);
    // SetCurrentMedia creates the input and applies its options before Start.
    input_item_SetURI(item, source_uri);
    assert(vlc_player_Start(fixture.player) == VLC_SUCCESS);
    vlc_player_Unlock(fixture.player);
    wait_state(&fixture, VLC_PLAYER_STATE_STOPPED); delete_player(&fixture);
    assert(atomic_load(&fixture.dispatch_count) == 0 && atomic_load(&fixture.cancel_count) == 0);
    input_item_Release(item); clean(&fixture);
}

struct async_result { vlc_sem_t ended; int status; bool picture; };
static void parsed(input_item_t *item, int status, void *opaque)
{
    (void)item;
    struct async_result *result = opaque; result->status = status; vlc_sem_post(&result->ended);
}
static void thumbnail(vlc_preparser_req *req, int status, picture_t *picture, void *opaque)
{
    (void)req;
    struct async_result *result = opaque; result->status = status; result->picture = picture != NULL;
    vlc_sem_post(&result->ended);
}

static void preparse_has_no_dispatch(void)
{
    struct fixture fixture; init(&fixture, false);
    for (unsigned source = 0; source < 2; ++source) {
        input_item_t *item = input_item_New(source ? source_uri : thumbnail_uri, "Parse source"); assert(item);
        struct async_result result = {.status = VLC_EGENERIC}; vlc_sem_init(&result.ended, 0);
        static const input_item_parser_cbs_t cbs = {.on_ended = parsed};
        const struct input_item_parser_cfg cfg = {.cbs = &cbs, .cbs_data = &result};
        input_item_parser_id_t *parser = input_item_Parse(VLC_OBJECT(fixture.instance->p_libvlc_int), item, &cfg);
        assert(parser);
        assert(vlc_sem_timedwait(&result.ended, vlc_tick_now() + VLC_TICK_FROM_SEC(5)) == 0);
        input_item_parser_id_Release(parser);
        assert(source ? result.status != VLC_SUCCESS : result.status == VLC_SUCCESS);
        assert(atomic_load(&fixture.dispatch_count) == 0); input_item_Release(item);
    }
    clean(&fixture);
}

static void thumbnail_has_no_dispatch(void)
{
    struct fixture fixture; init(&fixture, false);
    const struct vlc_preparser_cfg cfg = {.types = VLC_PREPARSER_TYPE_THUMBNAIL,
        .max_thumbnailer_threads = 1, .timeout = VLC_TICK_FROM_SEC(5), .external_process = false};
    vlc_preparser_t *parser = vlc_preparser_New(VLC_OBJECT(fixture.instance->p_libvlc_int), &cfg); assert(parser);
    for (unsigned source = 0; source < 2; ++source) {
        input_item_t *item = input_item_New(source ? source_uri : thumbnail_uri, "Thumbnail source"); assert(item);
        struct async_result result = {.status = VLC_EGENERIC}; vlc_sem_init(&result.ended, 0);
        static const struct vlc_thumbnailer_cbs cbs = {.on_ended = thumbnail};
        vlc_preparser_req *req = vlc_preparser_req_NewThumbnail(parser, item, NULL, &cbs, &result); assert(req);
        assert(vlc_preparser_Submit(parser, req) == VLC_SUCCESS);
        assert(vlc_sem_timedwait(&result.ended, vlc_tick_now() + VLC_TICK_FROM_SEC(6)) == 0);
        vlc_preparser_req_Release(req);
        assert(source ? result.status != VLC_SUCCESS && !result.picture : result.status == VLC_SUCCESS && result.picture);
        assert(atomic_load(&fixture.dispatch_count) == 0); input_item_Release(item);
    }
    vlc_preparser_Delete(parser); clean(&fixture);
}

static void secondary_has_no_dispatch(void)
{
    struct fixture fixture; init(&fixture, false); new_player(&fixture);
    char path[] = "addon-source-slave-XXXXXX"; int fd = vlc_mkstemp(path); assert(fd >= 0);
    FILE *file = fdopen(fd, "w"); assert(file);
    assert(fputs("1\n00:00:00,000 --> 00:01:00,000\nNative source fixture\n", file) >= 0); assert(fclose(file) == 0);
    char *subtitle = vlc_path2uri(path, NULL); assert(subtitle);
    input_item_t *item = input_item_New(mock_uri, "Ordinary master"); assert(item);
    vlc_player_Lock(fixture.player);
    assert(vlc_player_SetCurrentMedia(fixture.player, item) == VLC_SUCCESS);
    assert(vlc_player_Start(fixture.player) == VLC_SUCCESS);
    vlc_player_Unlock(fixture.player);
    wait_state(&fixture, VLC_PLAYER_STATE_PLAYING);
    vlc_player_Lock(fixture.player);
    assert(vlc_player_AddAssociatedMedia(fixture.player, SPU_ES, source_uri, false, false, false) == VLC_SUCCESS);
    assert(vlc_player_AddAssociatedMedia(fixture.player, SPU_ES, subtitle, false, false, false) == VLC_SUCCESS);
    vlc_player_Unlock(fixture.player);
    // The successful subtitle follows the refused source in the native queue.
    const vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(5);
    while (atomic_load(&fixture.subtitle_tracks) == 0)
        assert(vlc_sem_timedwait(&fixture.events, deadline) == 0);
    assert(atomic_load(&fixture.dispatch_count) == 0);
    delete_player(&fixture); input_item_Release(item); clean(&fixture);
    free(subtitle); assert(vlc_unlink(path) == 0);
}

int main(void)
{
    test_init();
    primary_dispatch_and_end(); stop_before_complete(); later_source_uri_has_no_dispatch();
    preparse_has_no_dispatch(); thumbnail_has_no_dispatch(); secondary_has_no_dispatch();
    return 0;
}
