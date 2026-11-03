// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include "player.h"

struct vlc_addon_player_listener {
    struct vlc_list node;
    vlc_addon_player_completion callback;
    void *opaque;
};

vlc_addon_player_listener *vlc_addon_player_AddListener(vlc_player_t *player,
    vlc_addon_player_completion callback, void *opaque)
{
    vlc_player_assert_locked(player);
    assert(!player->addon_session_notifying);
    if (callback == NULL || player->addon_session_listener_count >= 8)
        return NULL;
    vlc_addon_player_listener *listener = malloc(sizeof(*listener));
    if (listener == NULL)
        return NULL;
    *listener = (vlc_addon_player_listener) { .callback = callback, .opaque = opaque };
    vlc_list_append(&listener->node, &player->addon_session_listeners);
    ++player->addon_session_listener_count;
    return listener;
}

void vlc_addon_player_RemoveListener(vlc_player_t *player, vlc_addon_player_listener *listener)
{
    vlc_player_assert_locked(player);
    assert(!player->addon_session_notifying);
    vlc_list_remove(&listener->node);
    --player->addon_session_listener_count;
    free(listener);
}

void vlc_player_session_Completed(vlc_player_t *player,
    const struct vlc_addon_session_snapshot *snapshot)
{
    vlc_player_assert_locked(player);
    player->addon_session_notifying = true;
    vlc_addon_player_listener *listener;
    vlc_list_foreach(listener, &player->addon_session_listeners, node)
        listener->callback(snapshot, listener->opaque);
    player->addon_session_notifying = false;
}

void vlc_player_session_Record(vlc_player_t *player,
    enum vlc_addon_session_origin origin, enum vlc_addon_session_operation operation)
{
    vlc_player_assert_locked(player);
    vlc_addon_session_state_Record(player->addon_session, origin, operation);
    // State methods have released their mutex before the FIFO wake.
    if (player->input != NULL)
        input_SessionWake(player->input->thread);
}

void vlc_addon_player_Snapshot(vlc_player_t *player, struct vlc_addon_session_snapshot *result)
{
    vlc_player_assert_locked(player);
    vlc_addon_session_state_Snapshot(player->addon_session, result);
}

static bool input_ready(vlc_player_t *player)
{
    return player->input != NULL && player->input->started && player->started
        && (player->input->state == VLC_PLAYER_STATE_PLAYING
         || player->input->state == VLC_PLAYER_STATE_PAUSED);
}

enum vlc_addon_session_status vlc_addon_player_Grant(vlc_player_t *player,
    const struct vlc_addon_session_stamp *expected, vlc_addon_session_id lease,
    uint32_t operations, vlc_tick_t lifetime)
{
    vlc_player_assert_locked(player);
    if (!input_ready(player))
        return VLC_ADDON_SESSION_NO_INPUT;
    const enum vlc_addon_session_status result = vlc_addon_session_state_Grant(
        player->addon_session, expected, lease, operations, vlc_tick_now(), lifetime);
    input_SessionWake(player->input->thread);
    return result;
}

enum vlc_addon_session_status vlc_addon_player_Renew(vlc_player_t *player,
    const struct vlc_addon_session_stamp *expected, vlc_addon_session_id lease,
    vlc_tick_t lifetime)
{
    vlc_player_assert_locked(player);
    return vlc_addon_session_state_Renew(player->addon_session, expected, lease,
                                        vlc_tick_now(), lifetime);
}

enum vlc_addon_session_status vlc_addon_player_Resume(vlc_player_t *player,
    const struct vlc_addon_session_stamp *expected, vlc_addon_session_id lease,
    vlc_tick_t lifetime)
{
    vlc_player_assert_locked(player);
    if (!input_ready(player))
        return VLC_ADDON_SESSION_NO_INPUT;
    const enum vlc_addon_session_status result = vlc_addon_session_state_Resume(
        player->addon_session, expected, lease, vlc_tick_now(), lifetime);
    input_SessionWake(player->input->thread);
    return result;
}

bool vlc_addon_player_Revoke(vlc_player_t *player, vlc_addon_session_id lease)
{
    vlc_player_assert_locked(player);
    const bool changed = vlc_addon_session_state_Revoke(player->addon_session, lease);
    if (changed && player->input != NULL)
        input_SessionWake(player->input->thread);
    return changed;
}

enum vlc_addon_session_status vlc_addon_player_Apply(vlc_player_t *player,
    const struct vlc_addon_session_command *command)
{
    vlc_player_assert_locked(player);
    if (!input_ready(player))
        return VLC_ADDON_SESSION_NO_INPUT;
    struct vlc_player_input *input = player->input;
    const struct vlc_addon_session_capabilities caps = {
        .pausable = input->capabilities & VLC_PLAYER_CAP_PAUSE,
        .seekable = input->capabilities & VLC_PLAYER_CAP_SEEK,
        .rate_change = input->capabilities & VLC_PLAYER_CAP_CHANGE_RATE,
        .repeat_conflict = input->abloop_state[0].set || input->abloop_state[1].set
                       || input->repeat != 0,
        // A live source without an explicit current seek window denies seek.
        .seek_first = 0, .seek_last = input->live ? VLC_TICK_INVALID : input->length,
        .saved_rate = var_GetFloat(player, "rate"), .effective_rate = input->rate,
        .minimum_rate = INPUT_RATE_MIN, .maximum_rate = INPUT_RATE_MAX,
    };
    vlc_addon_session_token *token = NULL;
    const enum vlc_addon_session_status result = vlc_addon_session_state_Admit(
        player->addon_session, command, &caps, vlc_tick_now(), &token);
    if (result != VLC_ADDON_SESSION_OK)
        return result;
    // The input owns this token even when it rejects a full or stopped FIFO.
    return input_ControlPushSession(input->thread, &command->action, token) == VLC_SUCCESS
        ? VLC_ADDON_SESSION_OK : VLC_ADDON_SESSION_BUSY;
}
