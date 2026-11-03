/*****************************************************************************
 * player_metadata.c: Player metadata listener implementation
 *****************************************************************************
 * Copyright © 2020 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <limits.h>

#include <vlc_common.h>
#include <vlc_modules.h>
#include "player.h"
#include "../audio_output/aout_internal.h"

static void
vlc_player_OnLoudnessEvent(vlc_tick_t date,
                           const struct vlc_audio_loudness *loudness,
                           void *data)
{
    vlc_player_t *player = data;

    vlc_mutex_lock(&player->metadata_listeners_lock);

    vlc_player_metadata_listener_id *other_id;
    vlc_list_foreach(other_id, &player->metadata_listeners, node)
    {
        switch (other_id->option)
        {
            case VLC_PLAYER_METADATA_LOUDNESS_MOMENTARY:
                other_id->cbs->on_momentary_loudness_changed(date,
                    loudness->loudness_momentary, other_id->cbs_data);
                break;
            case VLC_PLAYER_METADATA_LOUDNESS_FULL:
                other_id->cbs->on_loudness_changed(date,
                    loudness, other_id->cbs_data);
                break;
            default: break;
        }
    }

    vlc_mutex_unlock(&player->metadata_listeners_lock);
}

static int
vlc_player_AddMetadataLoudnessListener(vlc_player_t *player,
                                       vlc_player_metadata_listener_id *listener_id)
{
    static const struct vlc_audio_meter_cbs meter_cbs = {
        .on_loudness = vlc_player_OnLoudnessEvent,
    };

    listener_id->audio_meter = NULL;

    vlc_player_metadata_listener_id *audio_meter_listener_id = NULL;
    bool has_same_meter_module = false;

    vlc_player_metadata_listener_id *other_id;
    vlc_list_foreach(other_id, &player->metadata_listeners, node)
    {
        if (other_id->option == listener_id->option)
            has_same_meter_module = true;

        if (other_id->audio_meter != NULL && other_id->option != VLC_PLAYER_METADATA_AUDIO_SAMPLES)
        {
            assert(audio_meter_listener_id == NULL);
            audio_meter_listener_id = other_id;
        }
    }

    if (audio_meter_listener_id == NULL
     || (!has_same_meter_module && listener_id->option == VLC_PLAYER_METADATA_LOUDNESS_FULL))
    {
        /* There are no audio meter plugins, or the audio meter plugin mode
         * need to be increased */
        audio_output_t *aout = vlc_player_aout_Hold(player);
        if (aout == NULL)
            return VLC_EGENERIC;

        unsigned mode = listener_id->option == VLC_PLAYER_METADATA_LOUDNESS_FULL ? 4 : 0;
        char chain[sizeof("ebur128{mode=X}")];
        snprintf(chain, ARRAY_SIZE(chain), "ebur128{mode=%1u}", mode);

        const struct vlc_audio_meter_plugin_owner meter_plugin_owner =
        {
            .cbs = &meter_cbs,
            .sys = player,
        };

        listener_id->audio_meter = aout_AddMeterPlugin(aout, chain, &meter_plugin_owner);
        if (listener_id->audio_meter == NULL)
        {
            aout_Release(aout);
            return VLC_EGENERIC;
        }

        if (audio_meter_listener_id != NULL)
        {
            aout_RemoveMeterPlugin(aout, audio_meter_listener_id->audio_meter);
            audio_meter_listener_id->audio_meter = NULL;
        }
        aout_Release(aout);
    }

    return VLC_SUCCESS;

}

static void
vlc_player_RemoveMetadataLoudnessListener(vlc_player_t *player,
                                          vlc_player_metadata_listener_id *listener_id)
{
    if (listener_id->audio_meter == NULL)
        return; /* This listener is not the owner of the audio meter plugin */

    /* Attach the audio meter plugin to an other listener */
    vlc_player_metadata_listener_id *other_id;
    vlc_list_foreach(other_id, &player->metadata_listeners, node)
    {
        if (other_id == listener_id)
            continue;

        if (other_id->option == VLC_PLAYER_METADATA_LOUDNESS_MOMENTARY
         || other_id->option == VLC_PLAYER_METADATA_LOUDNESS_FULL)
        {
            other_id->audio_meter = listener_id->audio_meter;
            listener_id->audio_meter = NULL;
            return;
        }
    }

    /* There are no other listeners, remove the audio meter */
    audio_output_t *aout = vlc_player_aout_Hold(player);
    if (aout != NULL)
    {
        aout_RemoveMeterPlugin(aout, listener_id->audio_meter);
        aout_Release(aout);
    }
}

static void
vlc_player_OnAudioSamples(const audio_sample_format_t *format,
                           const block_t *block, void *data)
{
    vlc_player_metadata_listener_id *listener = data;
    vlc_player_t *player = listener->player;
    if (block->i_pts == VLC_TICK_INVALID) return;
    vlc_mutex_lock(&player->timer.lock);
    const vlc_tick_t time = block->i_pts - player->timer.input_normal_time - player->timer.start_offset;
    const double rate = player->timer.input_rate;
    vlc_mutex_unlock(&player->timer.lock);
    if (time >= 0 && rate > 0) listener->cbs->on_audio_samples(time, rate, format, block, listener->cbs_data);
}

vlc_player_metadata_listener_id *
vlc_player_AddMetadataListener(vlc_player_t *player,
                               enum vlc_player_metadata_option option,
                               const union vlc_player_metadata_cbs *cbs,
                               void *cbs_data)
{
    vlc_player_assert_locked(player);
    assert(cbs);

    vlc_player_metadata_listener_id *listener_id = malloc(sizeof(*listener_id));
    if (listener_id == NULL)
        return NULL;

    listener_id->player = player;
    listener_id->cbs = cbs;
    listener_id->cbs_data = cbs_data;
    listener_id->option = option;
    listener_id->samples_aout = NULL;

    if (option == VLC_PLAYER_METADATA_AUDIO_SAMPLES) {
        static const struct vlc_audio_meter_cbs samples_cbs = {.on_samples = vlc_player_OnAudioSamples};
        const struct vlc_audio_meter_plugin_owner owner = {.cbs = &samples_cbs, .sys = listener_id};
        audio_output_t *aout = vlc_player_aout_Hold(player);
        // Never hold the listener mutex while waiting for an audio callback.
        // Other meter callbacks can take that mutex from the audio thread.
        listener_id->audio_meter = aout ? aout_AddMeterPlugin(aout, NULL, &owner) : NULL;
        if (!listener_id->audio_meter) {
            if (aout) aout_Release(aout);
            free(listener_id); return NULL;
        }
        listener_id->samples_aout = aout;
        vlc_mutex_lock(&player->metadata_listeners_lock);
        vlc_list_append(&listener_id->node, &player->metadata_listeners);
        vlc_mutex_unlock(&player->metadata_listeners_lock);
        return listener_id;
    }

    vlc_mutex_lock(&player->metadata_listeners_lock);

    int ret;
    switch (option)
    {
        case VLC_PLAYER_METADATA_LOUDNESS_MOMENTARY:
        case VLC_PLAYER_METADATA_LOUDNESS_FULL:
            ret = vlc_player_AddMetadataLoudnessListener(player, listener_id);
            break;
        default: vlc_assert_unreachable();
    }

    if (ret == VLC_EGENERIC)
    {
        free(listener_id);
        vlc_mutex_unlock(&player->metadata_listeners_lock);
        return NULL;
    }

    vlc_list_append(&listener_id->node, &player->metadata_listeners);

    vlc_mutex_unlock(&player->metadata_listeners_lock);

    return listener_id;
}

void
vlc_player_RemoveMetadataListener(vlc_player_t *player,
                                    vlc_player_metadata_listener_id *listener_id)
{
    vlc_player_assert_locked(player);
    assert(listener_id);

    if (listener_id->option == VLC_PLAYER_METADATA_AUDIO_SAMPLES) {
        vlc_mutex_lock(&player->metadata_listeners_lock);
        vlc_list_remove(&listener_id->node);
        vlc_mutex_unlock(&player->metadata_listeners_lock);
        audio_output_t *aout = listener_id->samples_aout;
        if (aout) {
            // Removing the observer waits for its last audio callback.
            aout_RemoveMeterPlugin(aout, listener_id->audio_meter);
            aout_Release(aout);
        }
        free(listener_id);
        return;
    }

    vlc_mutex_lock(&player->metadata_listeners_lock);

    switch (listener_id->option)
    {
        case VLC_PLAYER_METADATA_LOUDNESS_MOMENTARY:
        case VLC_PLAYER_METADATA_LOUDNESS_FULL:
            vlc_player_RemoveMetadataLoudnessListener(player, listener_id);
            break;
        default: vlc_assert_unreachable();
    }

    vlc_list_remove(&listener_id->node);
    free(listener_id);

    vlc_mutex_unlock(&player->metadata_listeners_lock);
}
