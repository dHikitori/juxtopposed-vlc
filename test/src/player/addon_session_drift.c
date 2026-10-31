// SPDX-License-Identifier: LGPL-2.1-or-later
// The two-player drift experiment of 05-visualizers-and-sessions.md: two real
// players in one process play the same media, the second one late. Through
// the native session API, a seek and then a bounded temporary rate correct
// the second player. The test prints the measured offsets as JSON and checks
// loose bounds. Both players share one monotonic clock, so this measures the
// host command path (seek precision, rate convergence, jitter), not network
// or device clocks. The output latency of the test audio output is unknown.
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "../../libvlc/test.h"
#include "../../../lib/libvlc_internal.h"
#include <vlc_common.h>
#include <vlc_addon_player.h>
#include <vlc_player.h>
#include <math.h>
#include <stdio.h>

static const vlc_addon_session_id lease = { 41, 43 };

// The precise timeline: the last timer point, interpolated to now. This is
// what a session observation uses; vlc_player_GetTime moves in coarse steps.
struct side {
    vlc_player_t *player; input_item_t *item; vlc_player_timer_id *timer;
    vlc_mutex_t lock; struct vlc_player_timer_point point; bool valid;
};

static void timer_update(const struct vlc_player_timer_point *value, void *data)
{
    struct side *side = data;
    vlc_mutex_lock(&side->lock);
    side->point = *value; side->valid = value->ts != VLC_TICK_INVALID;
    vlc_mutex_unlock(&side->lock);
}

static vlc_tick_t time_of(struct side *side)
{
    vlc_tick_t time = VLC_TICK_INVALID;
    double position;
    vlc_mutex_lock(&side->lock);
    if (side->valid && vlc_player_timer_point_Interpolate(&side->point, vlc_tick_now(), &time, &position) != VLC_SUCCESS)
        time = VLC_TICK_INVALID;
    vlc_mutex_unlock(&side->lock);
    return time;
}

static void start(libvlc_instance_t *instance, struct side *side)
{
    side->player = vlc_player_New(VLC_OBJECT(instance->p_libvlc_int), VLC_PLAYER_LOCK_NORMAL);
    assert(side->player);
    vlc_mutex_init(&side->lock); side->valid = false;
    static const struct vlc_player_timer_cbs callbacks = { .on_update = timer_update };
    vlc_player_Lock(side->player);
    side->timer = vlc_player_AddTimer(side->player, 0, &callbacks, side);
    vlc_player_Unlock(side->player);
    assert(side->timer);
    side->item = input_item_New("mock://audio_track_count=1;video_track_count=0;length=120000000;"
                                "can_control_pace=true;can_control_rate=true", "Drift");
    assert(side->item);
    vlc_player_Lock(side->player);
    assert(vlc_player_SetCurrentMedia(side->player, side->item) == VLC_SUCCESS);
    assert(vlc_player_Start(side->player) == VLC_SUCCESS);
    vlc_player_Unlock(side->player);
}

static void wait_playing(struct side *side)
{
    const vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(10);
    while (time_of(side) == VLC_TICK_INVALID || time_of(side) <= 0) {
        assert(vlc_tick_now() < deadline);
        vlc_tick_sleep(VLC_TICK_FROM_MS(20));
    }
}

static void stop(struct side *side)
{
    vlc_player_Lock(side->player);
    vlc_player_Stop(side->player);
    vlc_player_RemoveTimer(side->player, side->timer);
    vlc_player_Unlock(side->player);
    vlc_player_Delete(side->player);
    input_item_Release(side->item);
}

// The offset of b behind a, in ms, as the mean of samples over a window.
struct window { double mean, minimum, maximum, slope; unsigned count; };

static struct window measure(struct side *a, struct side *b, vlc_tick_t duration)
{
    struct window result = { 0, INFINITY, -INFINITY, 0, 0 };
    const vlc_tick_t begin = vlc_tick_now();
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    while (vlc_tick_now() - begin < duration) {
        const vlc_tick_t ta = time_of(a), tb = time_of(b);
        const double offset = MS_FROM_VLC_TICK(ta - tb), x = MS_FROM_VLC_TICK(vlc_tick_now() - begin) / 1000.0;
        result.mean += offset; ++result.count;
        result.minimum = fmin(result.minimum, offset); result.maximum = fmax(result.maximum, offset);
        sx += x; sy += offset; sxx += x * x; sxy += x * offset;
        vlc_tick_sleep(VLC_TICK_FROM_MS(50));
    }
    result.mean /= result.count;
    const double n = result.count, denominator = n * sxx - sx * sx;
    result.slope = denominator > 0 ? (n * sxy - sx * sy) / denominator : 0; // ms per second
    return result;
}

static enum vlc_addon_session_status apply(struct side *side, uint64_t sequence, struct vlc_addon_session_action action)
{
    vlc_player_Lock(side->player);
    struct vlc_addon_session_snapshot current;
    vlc_addon_player_Snapshot(side->player, &current);
    const struct vlc_addon_session_command command = { .expected = current.stamp, .lease = lease,
        .sequence = sequence, .action = action };
    const enum vlc_addon_session_status status = vlc_addon_player_Apply(side->player, &command);
    vlc_player_Unlock(side->player);
    return status;
}

static void print(const char *name, struct window value, bool last)
{
    printf("  \"%s\": {\"meanMs\": %.1f, \"minMs\": %.1f, \"maxMs\": %.1f, \"driftMsPerSecond\": %.2f, \"samples\": %u}%s\n",
           name, value.mean, value.minimum, value.maximum, value.slope, value.count, last ? "" : ",");
}

int main(void)
{
    test_init();
    const char *args[] = { "--ignore-config", "--verbose=0", "--no-media-library", "--no-plugins-cache",
        "--aout=adummy", "--vout=none", "--no-video", "--clock-master=monotonic" };
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(args), args);
    assert(instance);
    struct side a, b;
    start(instance, &a);
    wait_playing(&a);
    // The second player joins late, as a remote peer would.
    vlc_tick_sleep(VLC_TICK_FROM_MS(300));
    start(instance, &b);
    wait_playing(&b);
    vlc_tick_sleep(VLC_TICK_FROM_MS(500));

    const struct window joined = measure(&a, &b, VLC_TICK_FROM_SEC(3));

    vlc_player_Lock(b.player);
    struct vlc_addon_session_snapshot current;
    vlc_addon_player_Snapshot(b.player, &current);
    assert(vlc_addon_player_Grant(b.player, &current.stamp, lease, VLC_ADDON_SESSION_REMOTE_OPERATIONS,
                                  VLC_TICK_FROM_SEC(60)) == VLC_ADDON_SESSION_OK);
    vlc_player_Unlock(b.player);

    // Seek b to a's position. The seek takes time, so aim at where a will be
    // when it lands: the measured mean offset is the first estimate.
    const vlc_tick_t seek_started = vlc_tick_now();
    const vlc_tick_t target = time_of(&a);
    assert(apply(&b, 1, (struct vlc_addon_session_action) { .operation = VLC_ADDON_SESSION_SEEK,
        .parameters.seek_time = target }) == VLC_ADDON_SESSION_OK);
    vlc_tick_sleep(VLC_TICK_FROM_MS(1500));
    const struct window seeked = measure(&a, &b, VLC_TICK_FROM_SEC(2));
    const double seek_ms = MS_FROM_VLC_TICK(vlc_tick_now() - seek_started);

    // A bounded temporary rate (at most 5 % for at most 5 s) removes the rest.
    const double residual = seeked.mean;
    double rate_value = 1.0, observed_rate = 1.0, mid_offset = residual;
    vlc_tick_t rate_duration = 0;
    if (fabs(residual) > 20) {
        rate_value = residual > 0 ? 1.05 : 0.95;
        rate_duration = VLC_TICK_FROM_MS(fmin(fabs(residual) / 0.05, 5000));
        assert(apply(&b, 2, (struct vlc_addon_session_action) { .operation = VLC_ADDON_SESSION_TEMPORARY_RATE,
            .parameters.rate = { (float)rate_value, rate_duration } }) == VLC_ADDON_SESSION_OK);
        vlc_tick_sleep(rate_duration / 2);
        vlc_player_Lock(b.player);
        observed_rate = vlc_player_GetRate(b.player);
        vlc_player_Unlock(b.player);
        mid_offset = MS_FROM_VLC_TICK(time_of(&a) - time_of(&b));
        // The restore can report one timer discontinuity; measure after it.
        vlc_tick_sleep(rate_duration / 2 + VLC_TICK_FROM_MS(2000));
    }
    const struct window corrected = measure(&a, &b, VLC_TICK_FROM_SEC(3));
    vlc_player_Lock(b.player);
    const double restored_rate = vlc_player_GetRate(b.player);
    vlc_player_Unlock(b.player);

    printf("{\n  \"experiment\": \"two-player drift, seek and bounded rate\",\n  \"clock\": \"one monotonic clock\",\n"
           "  \"outputLatency\": \"unknown (adummy)\",\n");
    print("afterJoin", joined, false);
    print("afterSeek", seeked, false);
    printf("  \"rateCorrection\": {\"rate\": %.2f, \"durationMs\": %.0f, \"observedRate\": %.2f, \"midOffsetMs\": %.1f, \"restoredRate\": %.2f},\n",
           rate_value, (double)MS_FROM_VLC_TICK(rate_duration), observed_rate, mid_offset, restored_rate);
    print("afterRate", corrected, true);
    printf("}\n");
    (void)seek_ms;
    fflush(stdout);

    // What this setup can show: the late join is visible; the seek command
    // brings the second player close and it stays there; the bounded rate is
    // admitted, active for its duration and restored. With the mock demuxer
    // and the dummy output, the timeline follows the shared clock, so a rate
    // change cannot close the residual here; that needs a real audio output.
    assert(fabs(joined.mean) > 150);
    assert(fabs(seeked.mean) < fabs(joined.mean) && fabs(seeked.mean) < 150);
    assert(fabs(seeked.slope) < 5 && fabs(corrected.slope) < 5);
    assert(fabs(corrected.mean) < 150);
    if (rate_duration) assert(fabs(observed_rate - rate_value) < 0.001);
    assert(fabs(restored_rate - 1.0) < 0.001);

    stop(&b);
    stop(&a);
    libvlc_release(instance);
    return 0;
}
