// SPDX-License-Identifier: LGPL-2.1-or-later
#include "../../libvlc/test.h"
#include "../../../lib/libvlc_internal.h"
#include <vlc_player.h>
#include <vlc_aout.h>
#include <vlc_block.h>
#include <stdatomic.h>

static void samples(vlc_tick_t time, double rate, const audio_sample_format_t *format,
                    const block_t *block, void *opaque)
{
    (void)time; (void)rate; (void)format; (void)block;
    atomic_fetch_add((atomic_uint *)opaque, 1);
}

static vlc_player_metadata_listener_id *add(vlc_player_t *player, atomic_uint *calls)
{
    static const union vlc_player_metadata_cbs cbs = {.on_audio_samples = samples};
    vlc_player_Lock(player);
    vlc_player_metadata_listener_id *listener = vlc_player_AddMetadataListener(player,
        VLC_PLAYER_METADATA_AUDIO_SAMPLES, &cbs, calls);
    vlc_player_Unlock(player); assert(listener); return listener;
}

static void remove_listener(vlc_player_t *player, vlc_player_metadata_listener_id *listener)
{
    vlc_player_Lock(player); vlc_player_RemoveMetadataListener(player, listener); vlc_player_Unlock(player);
}

static uintptr_t current_output(vlc_player_t *player)
{
    audio_output_t *output = vlc_player_aout_Hold(player); assert(output);
    const uintptr_t address = (uintptr_t)output;
    // The test keeps no reference that could hide a missing listener hold.
    aout_Release(output); return address;
}

int main(void)
{
    test_init();
    const char *args[] = {"--ignore-config", "--quiet", "--no-plugins-cache", "--no-media-library",
        "--no-metadata-network-access", "--aout=adummy"};
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(args), args); assert(instance);
    vlc_player_t *player = vlc_player_New(VLC_OBJECT(instance->p_libvlc_int), VLC_PLAYER_LOCK_NORMAL); assert(player);
    atomic_uint calls; atomic_init(&calls, 0);
    for (unsigned reverse = 0; reverse < 2; ++reverse) {
        vlc_player_metadata_listener_id *listeners[3]; uintptr_t outputs[3];
        for (unsigned index = 0; index < ARRAY_SIZE(listeners); ++index) {
            listeners[index] = add(player, &calls); outputs[index] = current_output(player);
            // Reset is valid here because the player has no active decoder.
            vlc_player_aout_Reset(player);
            const uintptr_t replacement = current_output(player);
            for (unsigned previous = 0; previous <= index; ++previous) assert(replacement != outputs[previous]);
        }
        for (unsigned index = 0; index < ARRAY_SIZE(listeners); ++index)
            remove_listener(player, listeners[reverse ? ARRAY_SIZE(listeners) - 1 - index : index]);
    }
    assert(atomic_load(&calls) == 0);
    vlc_player_Delete(player); libvlc_release(instance); return 0;
}
