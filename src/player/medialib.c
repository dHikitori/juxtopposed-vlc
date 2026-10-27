/*****************************************************************************
 * medialib.c: Player/Media Library interractions
 *****************************************************************************
 * Copyright © 2018-2019 VLC authors and VideoLAN
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

#include <vlc_common.h>
#include <vlc_arrays.h>
#include "player.h"
#include "misc/variables.h"

void
vlc_player_input_RestoreMlStates(struct vlc_player_input* input, bool force_pos)
{
    vlc_player_t* player = input->player;
    vlc_player_assert_locked(player);

    int restore_pos;
    if (force_pos)
        restore_pos = VLC_PLAYER_RESTORE_PLAYBACK_POS_ALWAYS;
    else
        restore_pos = var_InheritInteger(input->thread, "restore-playback-pos");

    const bool restore_states = var_InheritBool(input->thread, "restore-playback-states");

    vlc_medialibrary_t* ml = vlc_ml_instance_get(input->player);
    if (!ml)
        return;
    input_item_t* item = input_GetItem(input->thread);
    vlc_ml_media_t* media = NULL;
    char *feed = input_item_GetMetaExtra(item, "podcast:feed");
    input->ml.podcast = feed != NULL && *feed;
    free(feed);
    if (input->ml.podcast)
        if (vlc_ml_control(ml, VLC_ML_REGISTER_PODCAST, item, &media) != VLC_SUCCESS) media = NULL;
    if (!media) media = vlc_ml_get_media_by_mrl( ml, item->psz_uri);
    if (!media)
        return;
    if (media->p_podcast && !input->ml.podcast) {
        input->ml.podcast = true;
        vlc_mutex_lock(&item->lock);
        vlc_meta_Merge(item->p_meta, media->p_podcast->p_meta);
        vlc_mutex_unlock(&item->lock);
    }
    if (media->b_subtitle_track_selection)
    {
        /* An explicit empty choice must also suppress the filesystem's
         * automatic subtitle search. This input has not started yet. */
        var_Create(input->thread, "sub-autodetect-file", VLC_VAR_BOOL);
        var_SetBool(input->thread, "sub-autodetect-file", false);
    }
    /* Associated tracks are independent of saved playback preferences. */
    vlc_mutex_lock(&item->lock);
    if (media->b_subtitle_track_selection)
    {
        for (int j = 0; j < item->i_slaves;)
        {
            input_item_slave_t *slave = item->pp_slaves[j];
            bool selected = slave->i_type != SLAVE_TYPE_SPU;
            if (media->p_files)
                for (size_t i = 0; i < media->p_files->i_nb_items; ++i)
                {
                    const vlc_ml_file_t *file = &media->p_files->p_items[i];
                    if (file->i_type == VLC_ML_FILE_TYPE_SUBTITLE &&
                        !file->b_unselected && file->psz_mrl &&
                        !strcmp(file->psz_mrl, slave->psz_uri))
                        selected = true;
                }
            if (!selected)
            {
                input_item_slave_Delete(slave);
                TAB_ERASE(item->i_slaves, item->pp_slaves, j);
            }
            else ++j;
        }
    }
    if (media->p_files)
    {
        for (size_t i = 0; i < media->p_files->i_nb_items; ++i)
        {
            const vlc_ml_file_t *file = &media->p_files->p_items[i];
            if (file->b_unselected && file->psz_mrl)
            {
                for (int j = 0; j < item->i_slaves;)
                {
                    if (!strcmp(item->pp_slaves[j]->psz_uri, file->psz_mrl))
                    {
                        input_item_slave_Delete(item->pp_slaves[j]);
                        TAB_ERASE(item->i_slaves, item->pp_slaves, j);
                    }
                    else ++j;
                }
            }
            if (!file->psz_mrl || !strcmp(file->psz_mrl, item->psz_uri) ||
                file->b_unselected ||
                (file->i_type != VLC_ML_FILE_TYPE_SUBTITLE &&
                                  file->i_type != VLC_ML_FILE_TYPE_SOUNDTRACK))
                continue;
            bool exists = false;
            const bool saved_choice = file->i_type == VLC_ML_FILE_TYPE_SUBTITLE
                ? media->b_subtitle_track_selection : media->b_soundtrack_track_selection;
            for (int j = 0; j < item->i_slaves; ++j)
                if (!strcmp(item->pp_slaves[j]->psz_uri, file->psz_mrl))
                {
                    exists = true;
                    input_item_slave_t *slave = item->pp_slaves[j];
                    if (saved_choice || file->b_external)
                        slave->i_priority = SLAVE_PRIORITY_USER;
                    else if (slave->i_priority == SLAVE_PRIORITY_FOLDER_RULE ||
                             slave->i_priority == SLAVE_PRIORITY_ML_AUTO)
                        slave->i_priority = SLAVE_PRIORITY_ML_AUTO;
                }
            if (exists) continue;
            input_item_slave_t *slave = input_item_slave_New(file->psz_mrl,
                file->i_type == VLC_ML_FILE_TYPE_SUBTITLE ? SLAVE_TYPE_SPU : SLAVE_TYPE_GENERIC,
                saved_choice || file->b_external ? SLAVE_PRIORITY_USER : SLAVE_PRIORITY_ML_AUTO);
            if (slave) TAB_APPEND(item->i_slaves, item->pp_slaves, slave);
        }
    }
    vlc_mutex_unlock(&item->lock);
    if (vlc_ml_media_get_all_playback_pref(ml, media->i_id,
                                           &input->ml.states) != VLC_SUCCESS)
    {
        vlc_ml_release(media);
        return;
    }

    input->ml.restore = (restore_pos == VLC_PLAYER_RESTORE_PLAYBACK_POS_ALWAYS) ?
                            VLC_RESTOREPOINT_TITLE : VLC_RESTOREPOINT_NONE;
    input->ml.restore_states = restore_states;
    /* If we are aiming at a specific title, wait for it to be added, and
     * only then select it & set the position.
     * If we're not aiming at a specific title, just set the position now.
     */
    if (restore_pos == VLC_PLAYER_RESTORE_PLAYBACK_POS_ALWAYS &&
            input->ml.states.current_title == -1 &&
            media->f_progress > .0f)
    {
        vlc_player_session_Record(player, VLC_ADDON_SESSION_CORE, VLC_ADDON_SESSION_SEEK);
        input_SetPosition(input->thread, media->f_progress, false);
    }
    else if (restore_pos == VLC_PLAYER_RESTORE_PLAYBACK_POS_ASK &&
             media->f_progress > .0f)
    {
        input->ml.delay_restore = true;
        input->ml.pos = media->f_progress;
    }

    vlc_ml_release(media);

    if (!restore_states)
        return;
    if (input->ml.states.rate != .0f)
        vlc_player_ChangeRateInternal(player, input->ml.states.rate, VLC_ADDON_SESSION_CORE);


    const char *video_track_ids = input->ml.states.current_video_track;
    const char *audio_track_ids = input->ml.states.current_audio_track;
    const char *subtitle_track_ids = input->ml.states.current_subtitle_track;

    if (video_track_ids)
        vlc_player_input_SelectTracksByStringIds(input, VIDEO_ES,
                                                 video_track_ids);
    if (audio_track_ids)
        vlc_player_input_SelectTracksByStringIds(input, AUDIO_ES,
                                                 audio_track_ids);
    if (subtitle_track_ids)
        vlc_player_input_SelectTracksByStringIds(input, SPU_ES,
                                                 subtitle_track_ids);

    vout_thread_t* vout = vlc_player_vout_Hold(player);
    if (vout != NULL)
    {
        if (input->ml.states.zoom >= .0f)
            var_SetFloat(vout, "zoom", input->ml.states.zoom);
        else
            var_SetFloat(vout, "zoom", 1.f);
        if (input->ml.states.aspect_ratio)
            var_SetString(vout, "aspect-ratio", input->ml.states.aspect_ratio);
        else
            var_SetString(vout, "aspect-ratio", NULL);
        if (input->ml.states.deinterlace)
        {
            var_SetString(vout, "deinterlace-mode", input->ml.states.deinterlace);
            var_SetInteger(vout, "deinterlace", 1);
        }
        else
        {
            var_SetString(vout, "deinterlace-mode", NULL);
            var_SetInteger(vout, "deinterlace", 0);
        }
        if (input->ml.states.video_filter)
            var_SetString(vout, "video-filter", input->ml.states.video_filter);
        else
            var_SetString(vout, "video-filter", NULL);
        vout_Release(vout);
    }
}

static bool
vlc_player_UpdateMediaType(const struct vlc_player_input* input,
                           vlc_medialibrary_t* ml, vlc_ml_media_t* media)
{
    assert(media->i_type == VLC_ML_MEDIA_TYPE_UNKNOWN);
    vlc_ml_media_type_t media_type;
    if (input->ml.has_video_tracks)
        media_type = VLC_ML_MEDIA_TYPE_VIDEO;
    else if (input->ml.has_audio_tracks)
        media_type = VLC_ML_MEDIA_TYPE_AUDIO;
    else
        return false;
    if (vlc_ml_media_set_type(ml, media->i_id, media_type) != VLC_SUCCESS)
        return false;
    media->i_type = media_type;
    return true;
}

static void
vlc_player_CompareAssignState(char **target_ptr, char **input_ptr)
{
    /* We only want to save these states if they are different, and not the
     * default values (NULL), so this means that either one is NULL and the
     * other isn't, or they are both non null and differ lexicographically */

    char *target = *target_ptr;
    char *input = *input_ptr;
    if ( ( input != NULL && target != NULL &&
           strcmp(input, target ) ) ||
         ( input == NULL && target != NULL ) ||
         ( input != NULL && target == NULL ) )
    {
        free(target);
        *target_ptr = input;
        *input_ptr = NULL;
    }
    else
    {
        free(target);
        *target_ptr = NULL;
    }
}

void
vlc_player_input_SavePodcastProgress(struct vlc_player_input* input)
{
    if (!input->ml.podcast || !var_GetBool(input->thread, "save-recentplay")) return;
    const vlc_tick_t now = vlc_tick_now();
    if (now - input->ml.last_progress_save < VLC_TICK_FROM_SEC(15) && input->state != VLC_PLAYER_STATE_PAUSED) return;
    input->ml.last_progress_save = now;
    vlc_medialibrary_t* ml = vlc_ml_instance_get(input->player);
    if (!ml) return;
    input_item_t* item = input_GetItem(input->thread);
    vlc_ml_media_t* media = vlc_ml_get_media_by_mrl(ml, item->psz_uri);
    if (!media) return;
    if (vlc_ml_control(ml, VLC_ML_MEDIA_SAVE_PODCAST_PROGRESS, media->i_id, (double) input->position, 0) != VLC_SUCCESS)
        msg_Warn(input->player, "could not save podcast progress");
    vlc_ml_release(media);
}

/* Same start/end margins as the media library's
 * Media::computePositionType(), which only applies them when it knows the
 * media duration. */
static float
vlc_player_GetMlProgressMargin(vlc_tick_t length)
{
    const vlc_tick_t hour = VLC_TICK_FROM_SEC(3600);
    if (length < hour)
        return .05f;
    if (length < 2 * hour)
        return .04f;
    if (length < 3 * hour)
        return .03f;
    if (length < 4 * hour)
        return .02f;
    return .01f;
}

void
vlc_player_UpdateMLStates(vlc_player_t *player, struct vlc_player_input* input)
{
    /* Do not save states for any secondary player. If this player's parent is
       the main vlc object, then it's the main player */
    if (player->obj.priv->parent != (vlc_object_t*)vlc_object_instance(player))
        return;

    vlc_medialibrary_t* ml = vlc_ml_instance_get(player);
    if (!ml)
        return;
    input_item_t* item = input_GetItem(input->thread);
    if (!item)
        return;
    vlc_ml_media_t* media = vlc_ml_get_media_by_mrl(ml, item->psz_uri);
    if (!media)
    {
        /* We don't know this media yet, let's add it as an external media so
         * we can still store its playback preferences
         */
        media = vlc_ml_new_external_media(ml, item->psz_uri);
        if (media == NULL)
            return;
    }

    if (media->i_type == VLC_ML_MEDIA_TYPE_UNKNOWN)
    {
        if (!vlc_player_UpdateMediaType(input, ml, media))
        {
            vlc_ml_release( media );
            return;
        }
    }
    assert(media->i_type != VLC_ML_MEDIA_TYPE_UNKNOWN);

    if ( var_GetBool( input->thread, "save-recentplay" ) )
    {
        /* Match the float precision used by Media::computePositionType(),
         * including positions exactly at a margin. */
        float position = input->ml.stop_position >= 0.f
                       ? input->ml.stop_position : input->position;
        /* A short clip can finish before its last position update reaches
         * the end margin. Use the stop reason instead: let the library handle
         * completion when it knows the duration, or clear progress directly. */
        if (input->ml.completed)
            position = media->i_duration > 0 ? 1.f : -1.f;
        else if (media->i_duration <= 0 && input->length > 0)
        {
            /* External media have no library duration. Apply the library's
             * start/end margins using the length the player knows. There is
             * still no play count bump without a library duration. */
            const float margin = vlc_player_GetMlProgressMargin(input->length);
            if (position < margin || position > 1.f - margin)
                position = -1.f;
        }
        if (input->ml.podcast) {
            const double saved = input->ml.stop_position >= 0.f ? input->ml.stop_position : input->position;
            if (vlc_ml_control(ml, VLC_ML_MEDIA_SAVE_PODCAST_PROGRESS, media->i_id,
                               input->ml.completed ? 0. : saved, (int) input->ml.completed) != VLC_SUCCESS)
                msg_Warn(player, "could not save podcast progress");
        }
        else vlc_ml_media_update_progress( ml, media->i_id, position );
    }

    /* If the value changed during the playback, update it in the medialibrary.
     * If not, set each state to their "unset" values, so that they aren't saved
     * in database */
    if ((input->ml.states.current_title == -1 && input->title_selected != 0) ||
         (input->ml.states.current_title != -1 &&
          input->ml.states.current_title != (int)input->title_selected))
    {
        input->ml.states.current_title = input->title_selected;
    }
    else
        input->ml.states.current_title = -1;

    /* We use .0f to signal an unsaved rate. We want to save it to the ml if it
     * changed, and if it's not the player's default value when the value was
     * never saved in the ML */
    if (input->rate != input->ml.states.rate &&
            (input->rate != 1.f || input->ml.states.rate != .0f))
        input->ml.states.rate = input->rate;
    else
        input->ml.states.rate = -1.f;

    struct vlc_player_track_priv* t;
    vout_thread_t* vout = NULL;

    vlc_vector_foreach(t, &input->video_track_vector)
    {
        if (!t->t.selected)
            continue;
        enum vlc_vout_order order;
        vout = vlc_player_GetEsIdVout(player, t->t.es_id, &order);
        if (vout != NULL && order == VLC_VOUT_ORDER_PRIMARY)
            break;
        vout = NULL;
    }
    if (vout != NULL)
    {
        char* aspect_ratio = var_GetNonEmptyString(vout, "aspect-ratio");
        char* crop = var_GetNonEmptyString(vout, "crop");
        char* deinterlace = var_GetNonEmptyString(vout, "deinterlace-mode");
        char* video_filter = var_GetNonEmptyString(vout, "video-filter");

        vlc_player_CompareAssignState(&input->ml.states.aspect_ratio, &aspect_ratio);
        vlc_player_CompareAssignState(&input->ml.states.crop, &crop);
        vlc_player_CompareAssignState(&input->ml.states.deinterlace, &deinterlace);
        vlc_player_CompareAssignState(&input->ml.states.video_filter, &video_filter);

        free(video_filter);
        free(deinterlace);
        free(crop);
        free(aspect_ratio);

        if (input->ml.states.deinterlace != NULL &&
            !strcmp(input->ml.states.deinterlace, "auto"))
        {
            free(input->ml.states.deinterlace);
            input->ml.states.deinterlace = NULL;
        }

        float zoom = var_GetFloat(vout, "zoom");
        if (zoom != input->ml.states.zoom &&
            (zoom != 1.f && input->ml.states.zoom >= .0f))
            input->ml.states.zoom = zoom;
        else
            input->ml.states.zoom = -1.f;
    }

    char *video_track_ids =
        vlc_player_input_GetSelectedTrackStringIds(input, VIDEO_ES);
    char *audio_track_ids =
        vlc_player_input_GetSelectedTrackStringIds(input, AUDIO_ES);
    char *subtitle_track_ids =
        vlc_player_input_GetSelectedTrackStringIds(input, SPU_ES);

    vlc_player_CompareAssignState(&input->ml.states.current_video_track, &video_track_ids);
    vlc_player_CompareAssignState(&input->ml.states.current_audio_track, &audio_track_ids);
    vlc_player_CompareAssignState(&input->ml.states.current_subtitle_track, &subtitle_track_ids);

    free(video_track_ids);
    free(audio_track_ids);
    free(subtitle_track_ids);

    vlc_ml_media_set_all_playback_states(ml, media->i_id, &input->ml.states);

    vlc_ml_release(&input->ml.states);
    vlc_ml_release(media);
}

void
vlc_player_RestorePlaybackPos(vlc_player_t *player)
{
    vlc_player_input_RestoreMlStates(player->input, true);
}

double
vlc_player_GetPlaybackRestorePosition(vlc_player_t *player)
{
    struct vlc_player_input *input = vlc_player_get_input_locked(player);
    return input ? input->ml.pos : -1.;
}
