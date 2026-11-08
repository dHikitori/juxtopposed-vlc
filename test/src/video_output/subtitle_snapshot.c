/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#define MODULE_NAME test_subtitle_snapshot
#undef VLC_DYNAMIC_PLUGIN
#include <vlc/vlc.h>
#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_player.h>
#include <vlc_vout.h>
#include <vlc_vout_display.h>
#include <vlc_window.h>
#include <vlc_subpicture.h>
#include <stdatomic.h>
#include <limits.h>
#include <stdio.h>
#undef NDEBUG
#include <assert.h>
#include "../../../lib/libvlc_internal.h"

static vlc_sem_t event;
static atomic_uint frames;
static atomic_bool ready, check_live;
static bool window_blending;
static unsigned window_scale;

static int EnableWindow(vlc_window_t *window, const vlc_window_cfg_t *cfg)
{
    (void)cfg;
    vlc_window_ReportSize(window, 64 * window_scale, 48 * window_scale);
    vlc_window_ReportSubtitleArea(window, 0, 0, 1, .75f);
    return VLC_SUCCESS;
}

static int OpenWindow(vlc_window_t *window)
{
    static const struct vlc_window_operations ops = { .enable = EnableWindow };
    window->ops = &ops;
    return VLC_SUCCESS;
}

static bool IsColor(const uint8_t *pixel, bool caption)
{
    return pixel[0] == 255 && pixel[1] == (caption ? 255 : 0) && pixel[2] == 255;
}

static void Prepare(vout_display_t *display, picture_t *picture,
                    const vlc_render_subpicture *subpicture, vlc_tick_t date)
{
    (void)display; (void)date;
    bool caption = false, osd = false;
    if (window_blending && subpicture)
    {
        struct subpicture_region_rendered *region;
        vlc_vector_foreach(region, &subpicture->regions)
        {
            const uint8_t *pixel = region->p_picture->p[0].p_pixels;
            if (IsColor(pixel, true))
            {
                if (region->place.x != (int)(30 * window_scale) || region->place.y != (int)(32 * window_scale))
                    fprintf(stderr, "unexpected live caption: %d,%d %ux%u; picture %ux%u\n",
                        region->place.x, region->place.y, region->place.width, region->place.height,
                        picture->format.i_visible_width, picture->format.i_visible_height);
                assert(region->place.x == (int)(30 * window_scale) && region->place.y == (int)(32 * window_scale));
                caption = true;
            }
            else if (IsColor(pixel, false))
            {
                assert(region->place.x == 0 && region->place.y == 0);
                osd = true;
            }
        }
    }
    else if (!window_blending)
    {
        const plane_t *plane = &picture->p[0];
        for (int y = 0; y < 48; ++y)
            for (int x = 0; x < 64; ++x)
            {
                const uint8_t *pixel = plane->p_pixels + y * plane->i_pitch + 4 * x;
                if (IsColor(pixel, true))
                {
                    assert(x >= 30 && x < 34 && y >= 32 && y < 36);
                    caption = true;
                }
                else if (IsColor(pixel, false))
                {
                    assert(x < 4 && y < 4);
                    osd = true;
                }
            }
    }
    /* Every live frame, including the snapshot frame, keeps its OSD and inset. */
    if (atomic_load(&check_live))
        assert(caption && osd);
    if (caption && osd)
        atomic_store(&ready, true);
    atomic_fetch_add(&frames, 1);
    vlc_sem_post(&event);
}

static void Display(vout_display_t *display, picture_t *picture)
{
    (void)display; (void)picture;
}

static int OpenDisplay(vout_display_t *display, video_format_t *format,
                       vlc_video_context *context)
{
    (void)context;
    assert(format->i_chroma == VLC_CODEC_RGBA);
    static const struct vlc_display_operations ops = { .prepare = Prepare, .display = Display };
    static const vlc_fourcc_t chromas[] = { VLC_CODEC_RGBA, 0 };
    display->ops = &ops;
    display->info.subpicture_chromas = window_blending ? chromas : NULL;
    return VLC_SUCCESS;
}

vlc_module_begin()
    set_capability("vout window", INT_MAX)
    set_callback(OpenWindow)
    add_shortcut("subtitle-snapshot")
    add_submodule()
        set_callback_display(OpenDisplay, INT_MAX)
        add_shortcut("subtitle-snapshot")
vlc_module_end()
VLC_EXPORT const vlc_plugin_cb vlc_static_modules[] = { VLC_SYMBOL(vlc_entry), NULL };

static void WaitFrame(unsigned previous)
{
    const vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(5);
    while (atomic_load(&frames) <= previous)
        assert(vlc_sem_timedwait(&event, deadline) == 0);
}

static void PutRegion(vout_thread_t *vout, ssize_t channel, bool caption)
{
    subpicture_t *subpicture = subpicture_New(NULL);
    assert(subpicture);
    subpicture->i_channel = channel;
    subpicture->b_subtitle = caption;
    subpicture->i_original_picture_width = 64;
    subpicture->i_original_picture_height = 48;
    subpicture->i_start = vlc_tick_now() - VLC_TICK_FROM_SEC(1);
    subpicture->i_stop = vlc_tick_now() + VLC_TICK_FROM_SEC(60);
    video_format_t format;
    video_format_Init(&format, 0);
    video_format_Setup(&format, VLC_CODEC_RGBA, 4, 4, 4, 4, 1, 1);
    subpicture_region_t *region = subpicture_region_New(&format);
    assert(region);
    region->i_x = region->i_y = 0;
    region->i_align = caption ? SUBPICTURE_ALIGN_BOTTOM : SUBPICTURE_ALIGN_TOP | SUBPICTURE_ALIGN_LEFT;
    region->b_absolute = !caption;
    region->text_flags = caption ? VLC_SUBPIC_TEXT_FLAG_DEFAULT_POSITION : 0;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
        {
            uint8_t *pixel = region->p_picture->p[0].p_pixels + y * region->p_picture->p[0].i_pitch + 4 * x;
            pixel[0] = pixel[2] = pixel[3] = 255;
            pixel[1] = caption ? 255 : 0;
        }
    vlc_spu_regions_push(&subpicture->regions, region);
    vout_PutSubpicture(vout, subpicture);
}

static void Run(bool hardware, bool paused, unsigned scale)
{
    fprintf(stderr, "subtitle snapshot: window_blending=%d paused=%d scale=%u\n", hardware, paused, scale);
    window_blending = hardware;
    window_scale = scale;
    atomic_store(&ready, false);
    atomic_store(&check_live, false);
    const char *args[] = {
        "--ignore-config", "--no-plugins-cache", "--no-media-library", "--no-auto-preparse",
        "--no-video-title-show", "--no-osd", "--aout=dummy", "--clock-master=monotonic", "--sub-margin=0",
        "--vout=subtitle-snapshot,none",
    };
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(args), args);
    assert(instance);
    var_Create(instance->p_libvlc_int, "window", VLC_VAR_STRING);
    var_SetString(instance->p_libvlc_int, "window", "subtitle-snapshot,none");
    vlc_player_t *player = vlc_player_New(VLC_OBJECT(instance->p_libvlc_int), VLC_PLAYER_LOCK_NORMAL);
    assert(player);
    input_item_t *media = input_item_New("mock://video_track_count=1;video_chroma=RGBA;video_colorbar=true;video_width=64;video_height=48;length=60000000", "subtitle snapshot");
    assert(media);
    unsigned previous = atomic_load(&frames);
    vlc_player_Lock(player);
    assert(vlc_player_SetCurrentMedia(player, media) == VLC_SUCCESS);
    assert(vlc_player_Start(player) == VLC_SUCCESS);
    vlc_player_Unlock(player);
    input_item_Release(media);
    WaitFrame(previous);
    vout_thread_t *vout = vlc_player_vout_Hold(player);
    assert(vout);
    if (paused)
    {
        vlc_player_Lock(player);
        vlc_player_Pause(player);
        vlc_player_Unlock(player);
        const vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(5);
        for (;;)
        {
            vlc_player_Lock(player);
            const bool is_paused = vlc_player_IsPaused(player);
            vlc_player_Unlock(player);
            if (is_paused)
                break;
            assert(vlc_tick_now() < deadline);
            WaitFrame(atomic_load(&frames));
        }
    }
    const ssize_t channel = vout_RegisterSubpictureChannel(vout);
    assert(channel >= 0);
    PutRegion(vout, channel, true);
    PutRegion(vout, VOUT_SPU_CHANNEL_OSD, false);
    const vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(5);
    while (!atomic_load(&ready))
        assert(vlc_sem_timedwait(&event, deadline) == 0);
    atomic_store(&check_live, true);
    for (unsigned i = 0; i < 3; ++i)
    {
        picture_t *snapshot;
        assert(vout_GetSnapshot(vout, NULL, &snapshot, NULL, NULL, VLC_TICK_FROM_SEC(5)) == VLC_SUCCESS);
        assert(snapshot->format.i_chroma == VLC_CODEC_RGBA);
        assert(snapshot->format.i_visible_width == 64 && snapshot->format.i_visible_height == 48);
        unsigned caption_pixels = 0;
        for (int y = 0; y < 48; ++y)
            for (int x = 0; x < 64; ++x)
            {
                const uint8_t *pixel = snapshot->p[0].p_pixels + y * snapshot->p[0].i_pitch + 4 * x;
                assert(!IsColor(pixel, false)); /* The saved image contains no OSD. */
                if (IsColor(pixel, true))
                {
                    assert(x >= 30 && x < 34 && y >= 44 && y < 48);
                    caption_pixels++;
                }
            }
        assert(caption_pixels == 16);
        picture_Release(snapshot);
        WaitFrame(atomic_load(&frames) + 1);
    }
    atomic_store(&check_live, false);
    vout_UnregisterSubpictureChannel(vout, channel);
    vout_Release(vout);
    vlc_player_Delete(player);
    libvlc_release(instance);
}

int main(void)
{
    vlc_sem_init(&event, 0);
    /* Each case starts its own libvlc. Keep one instance alive so the plugin
     * bank loads once: on macOS each plugin load costs seconds. */
    const char *keep_args[] = {"--ignore-config", "--no-media-library"};
    libvlc_instance_t *keep = libvlc_new(ARRAY_SIZE(keep_args), keep_args);
    assert(keep != NULL);
    for (unsigned scale = 1; scale <= 2; ++scale)
    {
        Run(true, false, scale);
        Run(true, true, scale);
        Run(false, false, scale);
        Run(false, true, scale);
    }
    libvlc_release(keep);
    return 0;
}
