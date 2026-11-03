// SPDX-License-Identifier: LGPL-2.1-or-later
#include "../../libvlc/test.h"
#include "../../../lib/libvlc_internal.h"
#include "../../../src/player/player.h"
#include <vlc_addon_player.h>
#include <vlc_demux.h>
#include <vlc_block.h>
#include <stdatomic.h>

// This builtin demux runs inside the actual native player/input threads.
// Gates stop real demux calls. Production code has no fixture callback.
struct fixture {
    libvlc_instance_t *instance;
    vlc_player_t *player;
    vlc_player_listener_id *listener;
    vlc_addon_player_listener *session_listener;
    atomic_uint_fast64_t completed;
    atomic_bool buffered;
    input_item_t *item;
    vlc_mutex_t lock;
    vlc_cond_t wait;
    vlc_sem_t events;
    bool block_demux, demux_entered, block_rate, rate_entered;
    bool adjust_rate, fail_rate;
    vlc_tick_t time, last_seek;
    vlc_tick_t pts_delay;
    unsigned seeks, rates, pauses, resumes, turns;
    unsigned block_rate_call;
    float effective_rate;
};

struct demux_state { struct fixture *fixture; es_out_id_t *audio; };

static const vlc_addon_session_id lease = { 19, 23 };

static void gate(struct fixture *fixture, bool *blocked, bool *entered)
{
    const vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(5);
    if (*blocked) {
        *entered = true;
        vlc_cond_broadcast(&fixture->wait);
        while (*blocked)
            assert(vlc_cond_timedwait(&fixture->wait, &fixture->lock, deadline) == 0);
    }
}

static int fixture_demux(demux_t *demux)
{
    struct demux_state *state = demux->p_sys;
    struct fixture *fixture = state->fixture;
    vlc_mutex_lock(&fixture->lock);
    gate(fixture, &fixture->block_demux, &fixture->demux_entered);
    fixture->time += VLC_TICK_FROM_MS(10);
    if (fixture->time >= VLC_TICK_FROM_SEC(59))
        fixture->time = 0;
    const vlc_tick_t time = fixture->time;
    ++fixture->turns;
    vlc_cond_broadcast(&fixture->wait);
    vlc_mutex_unlock(&fixture->lock);
    es_out_SetPCR(demux->out, VLC_TICK_0 + time);
    block_t *block = block_Alloc(480 * 2 * sizeof(int16_t)); assert(block);
    memset(block->p_buffer, 0, block->i_buffer);
    block->i_nb_samples = 480;
    block->i_pts = block->i_dts = VLC_TICK_0 + time;
    block->i_length = VLC_TICK_FROM_MS(10);
    es_out_Send(demux->out, state->audio, block);
    // This fixture deliberately paces synthetic samples. This is not a poll.
    (vlc_tick_sleep)(VLC_TICK_FROM_MS(1));
    return 1;
}

static int fixture_control(demux_t *demux, int query, va_list args)
{
    struct fixture *fixture = ((struct demux_state *)demux->p_sys)->fixture;
    switch (query) {
        case DEMUX_CAN_SEEK:
        case DEMUX_CAN_PAUSE:
        case DEMUX_CAN_CONTROL_RATE:
            *va_arg(args, bool *) = true; return VLC_SUCCESS;
        case DEMUX_CAN_CONTROL_PACE:
            *va_arg(args, bool *) = false; return VLC_SUCCESS;
        case DEMUX_GET_PTS_DELAY:
            *va_arg(args, vlc_tick_t *) = fixture->pts_delay; return VLC_SUCCESS;
        case DEMUX_GET_LENGTH:
            *va_arg(args, vlc_tick_t *) = VLC_TICK_FROM_SEC(60); return VLC_SUCCESS;
        case DEMUX_GET_NORMAL_TIME:
            *va_arg(args, vlc_tick_t *) = VLC_TICK_0; return VLC_SUCCESS;
        case DEMUX_GET_TIME: {
            vlc_mutex_lock(&fixture->lock);
            *va_arg(args, vlc_tick_t *) = fixture->time;
            vlc_mutex_unlock(&fixture->lock); return VLC_SUCCESS;
        }
        case DEMUX_GET_POSITION: {
            vlc_mutex_lock(&fixture->lock);
            *va_arg(args, double *) = (double)fixture->time / VLC_TICK_FROM_SEC(60);
            vlc_mutex_unlock(&fixture->lock); return VLC_SUCCESS;
        }
        case DEMUX_SET_TIME: {
            const vlc_tick_t time = va_arg(args, vlc_tick_t);
            vlc_mutex_lock(&fixture->lock);
            fixture->time = fixture->last_seek = time;
            ++fixture->seeks;
            vlc_cond_broadcast(&fixture->wait);
            vlc_mutex_unlock(&fixture->lock); return VLC_SUCCESS;
        }
        case DEMUX_SET_POSITION: {
            const double position = va_arg(args, double);
            vlc_mutex_lock(&fixture->lock);
            fixture->time = fixture->last_seek = position * VLC_TICK_FROM_SEC(60);
            ++fixture->seeks;
            vlc_cond_broadcast(&fixture->wait);
            vlc_mutex_unlock(&fixture->lock); return VLC_SUCCESS;
        }
        case DEMUX_SET_PAUSE_STATE: {
            const bool pause = va_arg(args, int);
            vlc_mutex_lock(&fixture->lock);
            if (pause) ++fixture->pauses; else ++fixture->resumes;
            vlc_cond_broadcast(&fixture->wait);
            vlc_mutex_unlock(&fixture->lock); return VLC_SUCCESS;
        }
        case DEMUX_SET_RATE: {
            float *rate = va_arg(args, float *);
            vlc_mutex_lock(&fixture->lock);
            if (fixture->block_rate_call == fixture->rates + 1)
                fixture->block_rate = true;
            gate(fixture, &fixture->block_rate, &fixture->rate_entered);
            ++fixture->rates;
            const bool fail = fixture->fail_rate;
            fixture->fail_rate = false;
            if (fixture->adjust_rate) { *rate = 1.01f; fixture->adjust_rate = false; }
            if (!fail) fixture->effective_rate = *rate;
            vlc_cond_broadcast(&fixture->wait);
            vlc_mutex_unlock(&fixture->lock);
            return fail ? VLC_EGENERIC : VLC_SUCCESS;
        }
        default: return VLC_EGENERIC;
    }
}

static int fixture_open(vlc_object_t *object)
{
    demux_t *demux = (demux_t *)object;
    struct fixture *fixture = var_InheritAddress(demux, "addon-session-fixture");
    if (fixture == NULL) return VLC_EGENERIC;
    struct demux_state *state = malloc(sizeof(*state));
    if (state == NULL) return VLC_ENOMEM;
    state->fixture = fixture;
    es_format_t format;
    es_format_Init(&format, AUDIO_ES, VLC_CODEC_S16N);
    format.audio.i_rate = 48000; format.audio.i_channels = 2;
    format.audio.i_physical_channels = AOUT_CHANS_STEREO;
    format.audio.i_bitspersample = 16;
    format.audio.i_blockalign = 4;
    format.b_packetized = true;
    state->audio = es_out_Add(demux->out, &format);
    es_format_Clean(&format);
    if (state->audio == NULL) { free(state); return VLC_EGENERIC; }
    demux->p_sys = state;
    demux->pf_demux = fixture_demux;
    demux->pf_control = fixture_control;
    return VLC_SUCCESS;
}

static void fixture_close(vlc_object_t *object)
{
    demux_t *demux = (demux_t *)object;
    struct demux_state *state = demux->p_sys;
    es_out_Del(demux->out, state->audio);
    free(state);
}

#define MODULE_NAME test_src_player_addon_session
#undef VLC_DYNAMIC_PLUGIN
#include <vlc_plugin.h>
const char vlc_module_name[] = MODULE_STRING;
vlc_module_begin()
    set_capability("access", 0)
    add_shortcut("sessionfixture")
    set_callbacks(fixture_open, fixture_close)
vlc_module_end()
VLC_EXPORT const vlc_plugin_cb vlc_static_modules[] = { VLC_SYMBOL(vlc_entry), NULL };

static struct vlc_addon_session_snapshot snapshot(struct fixture *fixture)
{
    struct vlc_addon_session_snapshot result;
    vlc_player_Lock(fixture->player);
    vlc_addon_player_Snapshot(fixture->player, &result);
    vlc_player_Unlock(fixture->player);
    return result;
}

static void wait_state(struct fixture *fixture, enum vlc_player_state expected)
{
    const vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(5);
    for (;;) {
        vlc_player_Lock(fixture->player);
        const bool done = vlc_player_GetState(fixture->player) == expected;
        vlc_player_Unlock(fixture->player);
        if (done) return;
        if (vlc_sem_timedwait(&fixture->events, deadline) != 0) {
            vlc_player_Lock(fixture->player);
            fprintf(stderr, "State wait failed: wanted=%d current=%d error=%d\n", expected,
                vlc_player_GetState(fixture->player), vlc_player_GetError(fixture->player));
            vlc_player_Unlock(fixture->player);
            assert(false);
        }
    }
}

static void wait_completion(struct fixture *fixture, uint64_t sequence)
{
    const vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(5);
    while (!(atomic_load(&fixture->completed) & (UINT64_C(1) << sequence))) {
        assert(vlc_sem_timedwait(&fixture->events, deadline) == 0);
    }
}

static void completed(const struct vlc_addon_session_snapshot *value, void *opaque)
{
    struct fixture *fixture = opaque;
    if (value->completed_origin == VLC_ADDON_SESSION_REMOTE && value->completed_sequence < 64)
        atomic_fetch_or(&fixture->completed, UINT64_C(1) << value->completed_sequence);
    vlc_sem_post(&fixture->events);
}

static void buffering_changed(vlc_player_t *player, float value, void *opaque)
{
    (void)player;
    struct fixture *fixture = opaque;
    atomic_store(&fixture->buffered, value >= 1);
    vlc_sem_post(&fixture->events);
}

static void state_changed(vlc_player_t *player, enum vlc_player_state state, void *opaque)
{
    (void)player; (void)state;
    vlc_sem_post(&((struct fixture *)opaque)->events);
}

static void rate_changed(vlc_player_t *player, float rate, void *opaque)
{
    (void)player; (void)rate;
    vlc_sem_post(&((struct fixture *)opaque)->events);
}

static void wait_rate(struct fixture *fixture, float expected)
{
    const vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(5);
    for (;;) {
        vlc_player_Lock(fixture->player);
        const float rate = vlc_player_GetRate(fixture->player);
        vlc_player_Unlock(fixture->player);
        if (rate == expected) return;
        assert(vlc_sem_timedwait(&fixture->events, deadline) == 0);
    }
}

static void wait_rate_released(struct fixture *fixture)
{
    const vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(5);
    while (snapshot(fixture).rate_owned) {
        assert(vlc_sem_timedwait(&fixture->events, deadline) == 0);
    }
}

static void wait_turn(struct fixture *fixture, unsigned turns)
{
    vlc_mutex_lock(&fixture->lock);
    const vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(5);
    while (fixture->turns < turns)
        assert(vlc_cond_timedwait(&fixture->wait, &fixture->lock, deadline) == 0);
    vlc_mutex_unlock(&fixture->lock);
}

static void block_demux(struct fixture *fixture)
{
    vlc_mutex_lock(&fixture->lock);
    fixture->block_demux = true; fixture->demux_entered = false;
    const vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(5);
    while (!fixture->demux_entered)
        assert(vlc_cond_timedwait(&fixture->wait, &fixture->lock, deadline) == 0);
    vlc_mutex_unlock(&fixture->lock);
}

static unsigned release_demux(struct fixture *fixture)
{
    vlc_mutex_lock(&fixture->lock);
    fixture->block_demux = false;
    const unsigned turns = fixture->turns;
    vlc_cond_broadcast(&fixture->wait);
    vlc_mutex_unlock(&fixture->lock);
    return turns;
}

static void init_buffering(struct fixture *fixture, bool buffering)
{
    *fixture = (struct fixture) { .effective_rate = 1, .block_demux = buffering,
        .pts_delay = buffering ? VLC_TICK_FROM_SEC(1) : 0 };
    atomic_init(&fixture->completed, 0);
    atomic_init(&fixture->buffered, false);
    vlc_mutex_init(&fixture->lock); vlc_cond_init(&fixture->wait);
    vlc_sem_init(&fixture->events, 0);
    const char *args[] = { "--ignore-config", "--verbose=2", "--no-media-library",
        "--no-metadata-network-access", "--no-plugins-cache", "--aout=adummy", "--vout=none" };
    fixture->instance = libvlc_new(ARRAY_SIZE(args), args); assert(fixture->instance);
    assert(var_Create(fixture->instance->p_libvlc_int, "addon-session-fixture", VLC_VAR_ADDRESS) == VLC_SUCCESS);
    var_SetAddress(fixture->instance->p_libvlc_int, "addon-session-fixture", fixture);
    fixture->player = vlc_player_New(VLC_OBJECT(fixture->instance->p_libvlc_int), VLC_PLAYER_LOCK_NORMAL);
    assert(fixture->player);
    fixture->item = input_item_New("sessionfixture://test", "Session fixture"); assert(fixture->item);
    vlc_player_Lock(fixture->player);
    fixture->session_listener = vlc_addon_player_AddListener(fixture->player, completed, fixture);
    assert(fixture->session_listener);
    static const struct vlc_player_cbs callbacks = {
        .on_buffering_changed = buffering_changed, .on_state_changed = state_changed,
        .on_rate_changed = rate_changed,
    };
    fixture->listener = vlc_player_AddListener(fixture->player, &callbacks, fixture);
    assert(fixture->listener);
    assert(vlc_player_SetCurrentMedia(fixture->player, fixture->item) == VLC_SUCCESS);
    assert(vlc_player_Start(fixture->player) == VLC_SUCCESS);
    vlc_player_Unlock(fixture->player);
    wait_state(fixture, VLC_PLAYER_STATE_PLAYING);
    if (!buffering) {
        const vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(5);
        while (!atomic_load(&fixture->buffered)) {
            assert(vlc_sem_timedwait(&fixture->events, deadline) == 0);
        }
        wait_turn(fixture, 2);
    }
}

static void init(struct fixture *fixture) { init_buffering(fixture, false); }

static void clean(struct fixture *fixture)
{
    vlc_mutex_lock(&fixture->lock);
    fixture->block_demux = fixture->block_rate = false;
    vlc_cond_broadcast(&fixture->wait);
    vlc_mutex_unlock(&fixture->lock);
    vlc_player_Lock(fixture->player); vlc_player_Stop(fixture->player);
    vlc_addon_player_RemoveListener(fixture->player, fixture->session_listener);
    vlc_player_RemoveListener(fixture->player, fixture->listener);
    vlc_player_Unlock(fixture->player);
    vlc_player_Delete(fixture->player);
    input_item_Release(fixture->item);
    libvlc_release(fixture->instance);
}

static void grant(struct fixture *fixture)
{
    vlc_player_Lock(fixture->player);
    struct vlc_addon_session_snapshot current;
    vlc_addon_player_Snapshot(fixture->player, &current);
    assert(vlc_addon_player_Grant(fixture->player, &current.stamp, lease,
        VLC_ADDON_SESSION_REMOTE_OPERATIONS, VLC_TICK_FROM_SEC(30)) == VLC_ADDON_SESSION_OK);
    vlc_player_Unlock(fixture->player);
}

static struct vlc_addon_session_command command(struct fixture *fixture,
    uint64_t sequence, enum vlc_addon_session_operation operation)
{
    return (struct vlc_addon_session_command) { .expected = snapshot(fixture).stamp,
        .lease = lease, .sequence = sequence, .action.operation = operation };
}

static enum vlc_addon_session_status apply(struct fixture *fixture,
    const struct vlc_addon_session_command *request)
{
    vlc_player_Lock(fixture->player);
    const enum vlc_addon_session_status result = vlc_addon_player_Apply(fixture->player, request);
    vlc_player_Unlock(fixture->player);
    return result;
}

static void local_priority_at_admission_and_repeated_pause(void)
{
    struct fixture fixture; init(&fixture); grant(&fixture);
    struct vlc_addon_session_command request = command(&fixture, 1, VLC_ADDON_SESSION_SEEK);
    request.action.parameters.seek_time = VLC_TICK_FROM_SEC(20);
    vlc_player_Lock(fixture.player); vlc_player_Pause(fixture.player); vlc_player_Unlock(fixture.player);
    wait_state(&fixture, VLC_PLAYER_STATE_PAUSED);
    assert(apply(&fixture, &request) == VLC_ADDON_SESSION_STALE);
    grant(&fixture);
    const struct vlc_addon_session_snapshot before = snapshot(&fixture);
    vlc_player_Lock(fixture.player); vlc_player_Pause(fixture.player); vlc_player_Unlock(fixture.player);
    const struct vlc_addon_session_snapshot after = snapshot(&fixture);
    assert(after.suspended && after.stamp.local_revision > before.stamp.local_revision);
    assert(after.origin == VLC_ADDON_SESSION_LOCAL);
    clean(&fixture);
}

static void queued_remote_seek_cannot_follow_local_seek(void)
{
    struct fixture fixture; init(&fixture); grant(&fixture); block_demux(&fixture);
    struct vlc_addon_session_command request = command(&fixture, 1, VLC_ADDON_SESSION_SEEK);
    request.action.parameters.seek_time = VLC_TICK_FROM_SEC(20);
    assert(apply(&fixture, &request) == VLC_ADDON_SESSION_OK);
    vlc_player_Lock(fixture.player); vlc_player_SetTime(fixture.player, VLC_TICK_FROM_SEC(5));
    vlc_player_Unlock(fixture.player);
    const unsigned turns = release_demux(&fixture);
    wait_turn(&fixture, turns + 3);
    vlc_mutex_lock(&fixture.lock);
    assert(fixture.seeks == 1 && fixture.last_seek == VLC_TICK_FROM_SEC(5));
    vlc_mutex_unlock(&fixture.lock);
    assert(snapshot(&fixture).retained_tokens == 0);
    clean(&fixture);
}

static void fifo_reduction_preserves_latest_remote_provenance(void)
{
    struct fixture fixture; init(&fixture); grant(&fixture); block_demux(&fixture);
    struct vlc_addon_session_command first = command(&fixture, 1, VLC_ADDON_SESSION_SEEK);
    first.action.parameters.seek_time = VLC_TICK_FROM_SEC(20);
    assert(apply(&fixture, &first) == VLC_ADDON_SESSION_OK);
    struct vlc_addon_session_command latest = command(&fixture, 2, VLC_ADDON_SESSION_SEEK);
    latest.action.parameters.seek_time = VLC_TICK_FROM_SEC(25);
    assert(apply(&fixture, &latest) == VLC_ADDON_SESSION_OK);
    assert(snapshot(&fixture).retained_tokens == 1);
    const unsigned turns = release_demux(&fixture);
    wait_completion(&fixture, 2); wait_turn(&fixture, turns + 2);
    vlc_mutex_lock(&fixture.lock);
    assert(fixture.seeks == 1 && fixture.last_seek == VLC_TICK_FROM_SEC(25));
    vlc_mutex_unlock(&fixture.lock);
    const struct vlc_addon_session_snapshot done = snapshot(&fixture);
    assert(done.origin == VLC_ADDON_SESSION_REMOTE && done.completed_origin == VLC_ADDON_SESSION_REMOTE);
    assert(done.completed_success && done.retained_tokens == 0);
    clean(&fixture);
}

static void stop_and_same_media_restart_change_input_identity(void)
{
    struct fixture fixture; init(&fixture); grant(&fixture); block_demux(&fixture);
    struct vlc_addon_session_command request = command(&fixture, 1, VLC_ADDON_SESSION_SEEK);
    request.action.parameters.seek_time = VLC_TICK_FROM_SEC(20);
    assert(apply(&fixture, &request) == VLC_ADDON_SESSION_OK);
    const uint64_t old_input = request.expected.input_generation;
    vlc_player_Lock(fixture.player); assert(vlc_player_Stop(fixture.player) == VLC_SUCCESS);
    assert(vlc_addon_player_Apply(fixture.player, &request) == VLC_ADDON_SESSION_NO_INPUT);
    vlc_player_Unlock(fixture.player);
    release_demux(&fixture); wait_state(&fixture, VLC_PLAYER_STATE_STOPPED);
    vlc_player_Lock(fixture.player); assert(vlc_player_Start(fixture.player) == VLC_SUCCESS);
    vlc_player_Unlock(fixture.player); wait_state(&fixture, VLC_PLAYER_STATE_PLAYING);
    assert(snapshot(&fixture).stamp.input_generation > old_input);
    assert(apply(&fixture, &request) == VLC_ADDON_SESSION_STALE);
    vlc_mutex_lock(&fixture.lock); assert(fixture.seeks == 0); vlc_mutex_unlock(&fixture.lock);
    clean(&fixture);
}

static void deferred_media_selection_revokes_before_shutdown(void)
{
    struct fixture fixture; init(&fixture); grant(&fixture); block_demux(&fixture);
    struct vlc_addon_session_command request = command(&fixture, 1, VLC_ADDON_SESSION_SEEK);
    request.action.parameters.seek_time = VLC_TICK_FROM_SEC(20);
    assert(apply(&fixture, &request) == VLC_ADDON_SESSION_OK);
    input_item_t *next = input_item_New("sessionfixture://next", "Next fixture"); assert(next);
    vlc_player_Lock(fixture.player); assert(vlc_player_SetCurrentMedia(fixture.player, next) == VLC_SUCCESS);
    assert(vlc_addon_player_Apply(fixture.player, &request) == VLC_ADDON_SESSION_NO_INPUT);
    vlc_player_Unlock(fixture.player);
    assert(!snapshot(&fixture).active);
    release_demux(&fixture);
    input_item_Release(next);
    clean(&fixture);
}

static void rate_end_seek_and_deadline_restore(void)
{
    for (unsigned end = 0; end < 3; ++end) {
        struct fixture fixture; init(&fixture); grant(&fixture);
        struct vlc_addon_session_command request = command(&fixture, 1, VLC_ADDON_SESSION_TEMPORARY_RATE);
        request.action.parameters.rate.value = 1.02f;
        request.action.parameters.rate.duration = VLC_TICK_FROM_MS(200);
        assert(apply(&fixture, &request) == VLC_ADDON_SESSION_OK);
        wait_completion(&fixture, 1); wait_rate(&fixture, 1.02f);
        vlc_player_Lock(fixture.player);
        assert(var_GetFloat(fixture.player, "rate") == 1);
        if (end == 0) assert(vlc_addon_player_Revoke(fixture.player, lease));
        else if (end == 1) vlc_player_SetTime(fixture.player, VLC_TICK_FROM_SEC(5));
        vlc_player_Unlock(fixture.player);
        wait_rate(&fixture, 1);
        wait_rate_released(&fixture);
        clean(&fixture);
    }
}

static void user_rate_after_begin_has_priority(void)
{
    struct fixture fixture; init(&fixture); grant(&fixture);
    vlc_mutex_lock(&fixture.lock); fixture.block_rate = true; vlc_mutex_unlock(&fixture.lock);
    struct vlc_addon_session_command request = command(&fixture, 1, VLC_ADDON_SESSION_TEMPORARY_RATE);
    request.action.parameters.rate.value = 1.02f;
    request.action.parameters.rate.duration = VLC_TICK_FROM_MS(100);
    assert(apply(&fixture, &request) == VLC_ADDON_SESSION_OK);
    vlc_mutex_lock(&fixture.lock);
    const vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(5);
    while (!fixture.rate_entered)
        assert(vlc_cond_timedwait(&fixture.wait, &fixture.lock, deadline) == 0);
    vlc_mutex_unlock(&fixture.lock);
    vlc_player_Lock(fixture.player); vlc_player_ChangeRate(fixture.player, 1.25f);
    vlc_player_Unlock(fixture.player);
    vlc_mutex_lock(&fixture.lock); fixture.block_rate = false; vlc_cond_broadcast(&fixture.wait);
    vlc_mutex_unlock(&fixture.lock);
    wait_rate(&fixture, 1.25f);
    // No later restore may replace the user rate, even after the old deadline.
    // Observe beyond the old correction deadline. This is a time-window test.
    (vlc_tick_sleep)(VLC_TICK_FROM_MS(300)); wait_rate(&fixture, 1.25f);
    assert(!snapshot(&fixture).rate_owned);
    clean(&fixture);
}

static void restore_needs_no_free_fifo_entry(void)
{
    struct fixture fixture; init(&fixture); grant(&fixture);
    struct vlc_addon_session_command request = command(&fixture, 1, VLC_ADDON_SESSION_TEMPORARY_RATE);
    request.action.parameters.rate.value = 1.02f;
    request.action.parameters.rate.duration = VLC_TICK_FROM_SEC(5);
    assert(apply(&fixture, &request) == VLC_ADDON_SESSION_OK);
    wait_completion(&fixture, 1); wait_rate(&fixture, 1.02f); block_demux(&fixture);
    vlc_player_Lock(fixture.player);
    for (unsigned index = 0; index < INPUT_CONTROL_FIFO_SIZE; ++index)
        assert(vlc_player_SetCategoryDelay(fixture.player, AUDIO_ES, index + 1,
            VLC_PLAYER_WHENCE_ABSOLUTE) == VLC_SUCCESS);
    input_thread_private_t *priv = input_priv(fixture.player->input->thread);
    vlc_mutex_lock(&priv->lock_control); assert(priv->i_control == INPUT_CONTROL_FIFO_SIZE);
    vlc_mutex_unlock(&priv->lock_control);
    assert(vlc_addon_player_Revoke(fixture.player, lease));
    vlc_player_Unlock(fixture.player);
    release_demux(&fixture); wait_rate(&fixture, 1);
    wait_rate_released(&fixture);
    clean(&fixture);
}

static void adjusted_and_failed_demux_rates_keep_native_fallback(void)
{
    for (unsigned fail = 0; fail < 2; ++fail) {
        struct fixture fixture; init(&fixture); grant(&fixture);
        vlc_mutex_lock(&fixture.lock);
        fixture.adjust_rate = !fail; fixture.fail_rate = fail;
        fixture.block_rate_call = fail ? 0 : 2;
        vlc_mutex_unlock(&fixture.lock);
        struct vlc_addon_session_command request = command(&fixture, 1, VLC_ADDON_SESSION_TEMPORARY_RATE);
        request.action.parameters.rate.value = 1.02f;
        request.action.parameters.rate.duration = VLC_TICK_FROM_SEC(1);
        assert(apply(&fixture, &request) == VLC_ADDON_SESSION_OK);
        wait_completion(&fixture, 1);
        if (!fail) {
            vlc_mutex_lock(&fixture.lock);
            const vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(5);
            while (!fixture.rate_entered)
                assert(vlc_cond_timedwait(&fixture.wait, &fixture.lock, deadline) == 0);
            vlc_mutex_unlock(&fixture.lock);
            assert(snapshot(&fixture).rate_owned);
            vlc_mutex_lock(&fixture.lock);
            fixture.block_rate = false;
            vlc_cond_broadcast(&fixture.wait);
            vlc_mutex_unlock(&fixture.lock);
        }
        wait_rate(&fixture, 1);
        wait_rate_released(&fixture);
        clean(&fixture);
    }
}

static void native_cork_suspends_lease_with_core_origin(void)
{
    struct fixture fixture; init(&fixture);
    vlc_player_Lock(fixture.player); vlc_player_Pause(fixture.player);
    vlc_player_SetPauseOnCork(fixture.player, true); vlc_player_Unlock(fixture.player);
    wait_state(&fixture, VLC_PLAYER_STATE_PAUSED); grant(&fixture);
    const struct vlc_addon_session_snapshot before = snapshot(&fixture);
    // The native variable callback takes the player lock itself.
    assert(var_SetInteger(fixture.player, "corks", 1) == VLC_SUCCESS);
    const struct vlc_addon_session_snapshot after = snapshot(&fixture);
    assert(after.suspended && after.origin == VLC_ADDON_SESSION_CORE);
    assert(after.stamp.local_revision == before.stamp.local_revision);
    clean(&fixture);
}

static void remote_pause_before_buffer_completion_has_no_deferred_effect(void)
{
    struct fixture fixture; init_buffering(&fixture, true);
    vlc_mutex_lock(&fixture.lock);
    const vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(5);
    while (!fixture.demux_entered)
        assert(vlc_cond_timedwait(&fixture.wait, &fixture.lock, deadline) == 0);
    const unsigned pauses = fixture.pauses;
    vlc_mutex_unlock(&fixture.lock);
    grant(&fixture);
    assert(!atomic_load(&fixture.buffered));
    struct vlc_addon_session_command request = command(&fixture, 1, VLC_ADDON_SESSION_PAUSE);
    assert(apply(&fixture, &request) == VLC_ADDON_SESSION_OK);
    release_demux(&fixture); wait_completion(&fixture, 1);
    const struct vlc_addon_session_snapshot value = snapshot(&fixture);
    assert(!value.completed_success);
    vlc_mutex_lock(&fixture.lock); assert(fixture.pauses == pauses); vlc_mutex_unlock(&fixture.lock);
    vlc_player_Lock(fixture.player);
    assert(vlc_player_GetState(fixture.player) == VLC_PLAYER_STATE_PLAYING);
    vlc_player_Unlock(fixture.player);
    clean(&fixture);
}

static void remote_pause_and_play_preserve_native_origin(void)
{
    struct fixture fixture; init(&fixture); grant(&fixture);
    const uint64_t local = snapshot(&fixture).stamp.local_revision;
    struct vlc_addon_session_command pause = command(&fixture, 1, VLC_ADDON_SESSION_PAUSE);
    assert(apply(&fixture, &pause) == VLC_ADDON_SESSION_OK);
    wait_completion(&fixture, 1); wait_state(&fixture, VLC_PLAYER_STATE_PAUSED);
    struct vlc_addon_session_command play = command(&fixture, 2, VLC_ADDON_SESSION_PLAY);
    assert(apply(&fixture, &play) == VLC_ADDON_SESSION_OK);
    wait_completion(&fixture, 2); wait_state(&fixture, VLC_PLAYER_STATE_PLAYING);
    const struct vlc_addon_session_snapshot value = snapshot(&fixture);
    assert(value.stamp.local_revision == local && value.completed_success);
    assert(value.completed_origin == VLC_ADDON_SESSION_REMOTE && !value.suspended);
    clean(&fixture);
}

int main(void)
{
    test_init();
#define RUN(name) do { puts(#name " started"); name(); puts(#name " passed"); } while (0)
    RUN(local_priority_at_admission_and_repeated_pause);
    RUN(queued_remote_seek_cannot_follow_local_seek);
    RUN(fifo_reduction_preserves_latest_remote_provenance);
    RUN(stop_and_same_media_restart_change_input_identity);
    RUN(deferred_media_selection_revokes_before_shutdown);
    RUN(rate_end_seek_and_deadline_restore);
    RUN(user_rate_after_begin_has_priority);
    RUN(restore_needs_no_free_fifo_entry);
    RUN(adjusted_and_failed_demux_rates_keep_native_fallback);
    RUN(native_cork_suspends_lease_with_core_origin);
    RUN(remote_pause_before_buffer_completion_has_no_deferred_effect);
    RUN(remote_pause_and_play_preserve_native_origin);
    return 0;
}
