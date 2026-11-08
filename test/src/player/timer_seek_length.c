// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "../../libvlc/test.h"
#include "../../../src/player/player.h"
#include <math.h>

static struct vlc_player_timer_point seek_point;
static void Updated(const struct vlc_player_timer_point *point, void *data)
{
    (void)point; (void)data;
}
static void Seeked(const struct vlc_player_timer_point *point, void *data)
{
    (void)data;
    assert(point);
    seek_point = *point;
}

static void Check(vlc_tick_t length, vlc_tick_t time, double position, bool live)
{
    vlc_player_t *player = calloc(1, sizeof(*player));
    assert(player);
    vlc_player_InitTimer(player);
    const struct vlc_player_timer_cbs cbs = {.on_update = Updated, .on_seek = Seeked};
    vlc_player_timer_id *timer = vlc_player_AddTimer(player, VLC_TICK_INVALID, &cbs, NULL);
    assert(timer);
    // The previous input produced clock points for a thirty-second live item.
    struct vlc_player_timer_point point = {.rate = 1., .ts = VLC_TICK_0,
        .length = VLC_TICK_FROM_SEC(30), .live = true, .system_date = VLC_TICK_MAX};
    vlc_player_UpdateTimer(player, NULL, false, &point, VLC_TICK_0, 0, 0, 0);
    vlc_player_ResetTimer(player);
    // The next input reports its duration, but is paused before its first clock point.
    point.ts = VLC_TICK_INVALID;
    point.system_date = VLC_TICK_INVALID;
    point.length = length;
    point.live = live;
    vlc_player_UpdateTimer(player, NULL, false, &point, VLC_TICK_0, 0, 0, 0);
    vlc_player_UpdateTimerSeekState(player, time, position);
    assert(seek_point.length == length);
    assert(seek_point.live == live);
    if (time == VLC_TICK_INVALID) {
        assert(seek_point.position == position);
        assert(seek_point.ts == (length == VLC_TICK_INVALID ? VLC_TICK_INVALID
                                                           : (vlc_tick_t)(position * length)));
    } else {
        assert(seek_point.ts == time);
        if (length > 0 && !live)
            assert(fabs(seek_point.position - time / (double)length) < .000001);
    }
    vlc_player_RemoveTimer(player, timer);
    vlc_player_DestroyTimer(player);
    free(player);
}

static void CheckPausedStop(bool clock_point, bool pending_seek)
{
    vlc_player_t *player = calloc(1, sizeof(*player));
    assert(player);
    vlc_mutex_init(&player->lock);
    vlc_cond_init(&player->mainloop.wait);
    vlc_list_init(&player->mainloop.stop_inputs);
    vlc_list_init(&player->mainloop.stopping_inputs);
    vlc_list_init(&player->mainloop.joinable_inputs);
    vlc_player_InitTimer(player);
    /* Stopping records the operation for add-on sessions. */
    player->addon_session = vlc_addon_session_state_New(
        (vlc_addon_session_id){0, 1}, NULL);
    assert(player->addon_session);
    struct vlc_player_input input = {
        .player = player, .started = true, .state = VLC_PLAYER_STATE_PAUSED,
        .position = .1f, .ml.stop_position = -1.f,
    };
    player->input = &input;
    player->started = true;
    if (clock_point)
    {
        /* The paused picture has moved, but the input's periodic position
         * event still describes the beginning of the file. */
        struct vlc_player_timer_point point = {
            .rate = 1., .ts = VLC_TICK_FROM_SEC(24), .position = .4,
            .length = VLC_TICK_FROM_SEC(60), .system_date = VLC_TICK_MAX,
        };
        vlc_player_UpdateTimer(player, NULL, false, &point, VLC_TICK_0, 0, 0, 0);
    }
    if (pending_seek)
        vlc_player_UpdateTimerSeekState(player, VLC_TICK_INVALID, .8);

    vlc_player_Lock(player);
    assert(vlc_player_Stop(player) == VLC_SUCCESS);
    vlc_player_Unlock(player);
    assert(!player->input);
    /* Save the displayed point, never an unconfirmed seek request. */
    assert(fabs(input.ml.stop_position - (clock_point ? .4 : .1)) < .000001);
    vlc_list_remove(&input.node);
    vlc_player_DestroyTimer(player);
    vlc_addon_session_state_Close(player->addon_session);
    vlc_addon_session_state_Release(player->addon_session);
    free(player);
}
int main(void)
{
    Check(VLC_TICK_FROM_SEC(8), VLC_TICK_INVALID, .65, false);
    Check(VLC_TICK_FROM_SEC(8), VLC_TICK_FROM_SEC(4), -1., false);
    Check(VLC_TICK_FROM_SEC(8), VLC_TICK_INVALID, 0., false);
    Check(VLC_TICK_INVALID, VLC_TICK_INVALID, .5, false);
    Check(VLC_TICK_FROM_SEC(8), VLC_TICK_INVALID, .5, true);
    CheckPausedStop(true, false);
    CheckPausedStop(true, true);
    CheckPausedStop(false, false);
    puts("Five pre-clock seek and three paused-stop cases passed.");
    return 0;
}
