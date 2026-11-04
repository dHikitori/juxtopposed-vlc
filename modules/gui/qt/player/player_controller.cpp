/*****************************************************************************
 * Copyright (C) 2019 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "dialogs/extensions/extensions_manager.hpp"
#include "player_controller.hpp"
#include "player_controller_p.hpp"
#include "subtitle_timeline_scanner.hpp"
#include "audio_waveform_scanner.hpp"
#include "util/shared_input_item.hpp"
#include "util/media_identity_vlc.hpp"
#include "util/artwork_approval.hpp"
#include "util/artwork_selection.hpp"
#include "maininterface/mainctx.hpp"
#include "addons/addon_metadata_service.hpp"
#include "playlist/playlist_controller.hpp"
#include "playlist/media.hpp"
#include "util/seek_preview_image_provider.hpp"

#include <vlc_actions.h>           /* ACTION_ID */
#include <vlc_artwork_policy.h>
#include <vlc_url.h>            /* vlc_uri_decode */
#include <vlc_strings.h>        /* vlc_strfplayer */
#include <vlc_aout.h>           /* audio_output_t */
#include <vlc_es.h>
#include <vlc_cxx_helpers.hpp>
#include <vlc_vout.h>
#include <vlc_preparser.h>
#include <vlc_picture.h>
#include <vlc_block.h>

#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QDir>
#include <QSignalMapper>
#include <QThreadPool>
#include <QImage>
#include <QBuffer>
#include <QStandardPaths>
#include <QPointer>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextDocument>
#include <QCryptographicHash>
#include <algorithm>
#include <cmath>
#include <limits>
#ifndef QT_HAS_LIBATOMIC
#include <QReadLocker>
#include <QWriteLocker>
#endif

#include <cassert>

#define POSITION_MIN_UPDATE_INTERVAL VLC_TICK_FROM_MS(15)

//PlayerController private implementation

using SharedEsId = ::vlc::vlc_shared_data_ptr<vlc_es_id_t,
                                              &vlc_es_id_Hold,
                                              &vlc_es_id_Release>;

using SharedTitleList = ::vlc::vlc_shared_data_ptr<vlc_player_title_list,
                                                   &vlc_player_title_list_Hold,
                                                   &vlc_player_title_list_Release>;

PlayerControllerPrivate::~PlayerControllerPrivate()
{
    m_artworkSelection.reset();
    assert(m_smpteTimerRequestCount == 0);
    m_seekSampleTimer.stop();
    m_seekPreviewEnabled = false;
    m_subtitleTimelineEnabled = false;
    resetSubtitleTimeline();
    // Finish cancelled reads before the interface and queued callbacks die.
    m_subtitleTimelineWorkers.waitForDone();
    m_audioWaveformEnabled = false;
    m_audioWaveformAutomatic = false;
    resetAudioWaveform();
    m_audioWaveformWorkers.waitForDone();

    {
        vlc_player_locker locker{m_player};
        vlc_player_vout_RemoveListener( m_player, m_player_vout_listener );
        vlc_player_aout_RemoveListener( m_player, m_player_aout_listener );
        vlc_player_RemoveListener( m_player, m_player_listener );
        vlc_player_RemoveTimer( m_player, m_player_timer );
    }
    // Artwork completion takes the player lock. Never hold it while waiting
    // for preparser workers, including callbacks delivered during cancellation.
    if (m_preparser != nullptr)
        vlc_preparser_Delete(m_preparser);
    if (m_seekPreparser != nullptr)
        vlc_preparser_Delete(m_seekPreparser);
    if (m_seekRequest != nullptr)
        vlc_preparser_req_Release(m_seekRequest);
}

bool PlayerControllerPrivate::isCurrentItemSynced()
{
    /* The media can change before the UI gets updated and thus a player
     * request can be submitted on the wrong media or worse, no media at
     * all. Here d->m_currentItem is read and modified under the player
     * lock. */
    return m_currentItem.get() == vlc_player_GetCurrentMedia( m_player );
}


void PlayerControllerPrivate::UpdateName(input_item_t* media)
{
    Q_Q(PlayerController);
    /* Update text, name and nowplaying */
    QString name;
    if (! media)
        return;

    /* Try to get the nowplaying */
    char *format = var_InheritString( p_intf, "input-title-format" );
    char *formatted = NULL;
    if (format != NULL)
    {
        vlc_player_Lock( m_player );
        formatted = vlc_strfplayer( m_player, media, format );
        vlc_player_Unlock( m_player );
        free( format );
        if( formatted != NULL )
        {
            name = qfu(formatted);
            free( formatted );
        }
    }

    /* If we have Nothing */
    if( name.simplified().isEmpty() )
    {
        char *uri = input_item_GetURI( media );
        char *file = uri ? strrchr( uri, '/' ) : NULL;
        if( file != NULL )
        {
            vlc_uri_decode( ++file );
            name = qfu(file);
        }
        else
            name = qfu(uri);
        free( uri );
    }

    name = name.trimmed();

    if( m_name != name )
    {
        emit q->nameChanged( name );
        m_name = std::move(name);
    }
}


void PlayerControllerPrivate::UpdateArt(input_item_t *p_item)
{
    Q_Q(PlayerController);
    if (! p_item)
        return;

    QString url = PlayerController::decodeArtURL( p_item );

    /* the art hasn't changed, no need to update */
    if(m_artUrl == url)
        return;

    /* Update Art meta */
    m_artUrl = std::move(url);
    emit q->artChanged( m_artUrl );
}

void PlayerControllerPrivate::UpdateStats( const input_stats_t& stats )
{
    Q_Q(PlayerController);
    m_stats = stats;
    emit q->statisticsUpdated( stats );
    emit q->playbackStatisticsChanged();
}

void PlayerControllerPrivate::UpdateProgram(enum vlc_player_list_action action, const struct vlc_player_program* prgm)
{
    Q_Q(PlayerController);
    m_programList.updatePrograms( action, prgm );

    bool hasPrograms = (m_programList.getCount() > 1);

    if (m_hasPrograms != hasPrograms)
    {
        m_hasPrograms = hasPrograms;

        emit q->hasProgramsChanged(hasPrograms);
    }

    emit q->isEncryptedChanged( prgm->scrambled );
}

void PlayerControllerPrivate::UpdateTrackSelection(vlc_es_id_t *trackid, bool selected)
{
    if (trackid == NULL)
        return;
    es_format_category_e cat = vlc_es_id_GetCat(trackid);
    TrackListModel* tracklist;
    switch (cat) {
    case VIDEO_ES: tracklist = &m_videoTracks; break;
    case AUDIO_ES: tracklist = &m_audioTracks; break;
    case SPU_ES: tracklist = &m_subtitleTracks; break;
    default: return;
    }
    tracklist->updateTrackSelection(trackid, selected);
}

void PlayerControllerPrivate::UpdateMeta( input_item_t *p_item )
{
    Q_Q(PlayerController);
    const auto artwork = vlc::wrap_cptr(vlc_artwork_item_GetURI(p_item));
    m_artwork = QUrl::fromEncoded(QByteArray(artwork ? artwork.get() : ""));

    {
        vlc_mutex_locker locker(&p_item->lock);

        if (p_item->p_meta)
        {
            m_title   = vlc_meta_Get(p_item->p_meta, vlc_meta_Title);
            m_artist  = vlc_meta_Get(p_item->p_meta, vlc_meta_Artist);
            m_album   = vlc_meta_Get(p_item->p_meta, vlc_meta_Album);

            /* Capture the raw sylt-data buffer; the entry list is built on
             * first read so we don't pay for it when lyrics are never shown.
             * Later meta refreshes may not carry sylt-data again, so keep the
             * cached buffer until the current media changes. */
            const char *psz_sylt_data = vlc_meta_GetExtra( p_item->p_meta, "sylt-data" );
            if (!psz_sylt_data)
                psz_sylt_data = vlc_meta_GetExtra( p_item->p_meta, "uslt-data" );

            if (psz_sylt_data != NULL)
            {
                QByteArray newRaw( psz_sylt_data );
                const auto hash = qHash(newRaw);
                if (!m_syltRaw || hash != m_syltRaw->second)
                {
                    m_syltRaw = qMakePair(std::move( newRaw ), hash);
                    m_syltLyrics.reset();
                }
            }
        }
    }

    if (m_syltRaw) {
        emit q->hasLyricsChanged(true);
        emit q->syltLyricsChanged();
    }

    emit q->currentMetaChanged( p_item  );
}

QList<TimedText>
PlayerControllerPrivate::syltLyrics() const
{
    if (!m_syltLyrics.has_value())
    {
        QList<TimedText> lyrics;

        if (m_syltRaw)
        {
            assert(!m_syltRaw->first.isEmpty());
            const QString sylt = QString::fromUtf8(m_syltRaw->first);
            m_syltRaw->first.clear();

            const auto entries = QStringView( sylt ).split( QChar( 0x1E ), Qt::SkipEmptyParts );

            for (const QStringView &entry : entries)
            {
                const qsizetype sep = entry.indexOf( QChar( 0x1F ) );
                if (sep <= 0)
                    continue;

                const vlc_tick_t time =
                    VLC_TICK_FROM_MS( entry.first( sep ).toLongLong() );
                lyrics.push_back( TimedText( entry.sliced( sep + 1 ).toString(), VLCTime( time ) ) );
            }
            m_syltLyrics = std::move( lyrics );
        }
    }
    return *m_syltLyrics;
}

void PlayerControllerPrivate::UpdateInfo( input_item_t *p_item )
{
    Q_Q(PlayerController);
    emit q->infoChanged( p_item );
}

void PlayerControllerPrivate::UpdateVouts(vout_thread_t **vouts, size_t i_vouts, enum vlc_player_vout_action action)
{
    Q_Q(PlayerController);
    bool hadVideo = m_hasVideo;

    if (action == VLC_PLAYER_VOUT_STARTED)
        ++m_vout_ref;
    else if (action == VLC_PLAYER_VOUT_STOPPED)
        --m_vout_ref;
    assert(m_vout_ref >= 0);
    m_hasVideo = (m_vout_ref > 0);

    vout_thread_t* main_vout = nullptr;
    if (m_hasVideo)
        main_vout = vouts[0];

    m_videoStereoMode.resetObject( main_vout );
    m_zoom.resetObject( main_vout );
    m_aspectRatio.resetObject( main_vout );
    m_crop.resetObject(  main_vout );
    m_fit.resetObject(  main_vout );
    m_deinterlace.resetObject( main_vout );
    m_deinterlaceMode.resetObject( main_vout );
    m_autoscale.resetObject( main_vout );

    emit q->voutListChanged(vouts, i_vouts);
    if( hadVideo != m_hasVideo )
        emit q->hasVideoOutputChanged(m_hasVideo);
}

void PlayerControllerPrivate::UpdateSpuOrder(vlc_es_id_t *es_id, enum vlc_vout_order order)
{
    switch (order)
    {
        case VLC_VOUT_ORDER_NONE:
            if (es_id == m_secondarySpuEsId.get())
                m_secondarySpuEsId.reset(NULL, false);
            break;
        case VLC_VOUT_ORDER_SECONDARY:
            m_secondarySpuEsId.reset(es_id, true);
            if (m_secondarySubtitleDelay.valid())
            {
                vlc_player_locker lock{ m_player };
                vlc_player_SetEsIdDelay(m_player, es_id,
                                        m_secondarySubtitleDelay.toVLCTick(),
                                        VLC_PLAYER_WHENCE_ABSOLUTE);
            }
            break;
        default:
            break;
    }
    resetSubtitleTimeline();
}

int PlayerControllerPrivate::interpolateTime(vlc_tick_t system_now)
{
    vlc_tick_t new_time;
    if (vlc_player_timer_point_Interpolate(&m_player_time, system_now,
                                           &new_time, &m_position) == VLC_SUCCESS)
    {
        m_time = new_time;
        return VLC_SUCCESS;
    }
    return VLC_EGENERIC;
}

void PlayerControllerPrivate::startPositionTimer()
{
    // Use the player's clock interpolation, rather than easing toward stale
    // UI positions. Pauses and seek discontinuities still stop this timer.
    if (m_playing_status == PlayerController::PLAYING_STATE_PAUSED
        || m_playing_status == PlayerController::PLAYING_STATE_STOPPED
        || m_playing_status == PlayerController::PLAYING_STATE_STOPPING)
    {
        m_position_timer.stop();
        return;
    }
    const double minimum = MS_FROM_VLC_TICK(POSITION_MIN_UPDATE_INTERVAL);
    const double nominal = m_player_time.rate > 0
        ? m_length.toVLCTick() / double(CLOCK_FREQ) / m_player_time.rate : minimum;
    const int interval = m_precisePositionRequests ? 16
        : int(std::clamp(std::isfinite(nominal) ? nominal : minimum,
                         minimum, double(std::numeric_limits<int>::max())));
    m_position_timer.setTimerType(m_precisePositionRequests ? Qt::PreciseTimer : Qt::CoarseTimer);
    m_position_timer.start(interval);
}

void PlayerController::requestAddPrecisePositionTimer()
{
    Q_D(PlayerController);
    if (++d->m_precisePositionRequests == 1 && d->m_position_timer.isActive())
        d->startPositionTimer();
}

void PlayerController::requestRemovePrecisePositionTimer()
{
    Q_D(PlayerController);
    assert(d->m_precisePositionRequests > 0);
    if (d->m_precisePositionRequests && --d->m_precisePositionRequests == 0 && d->m_position_timer.isActive())
        d->startPositionTimer();
}

extern "C" {

//player callbacks

static  void on_player_current_media_changed(vlc_player_t *, input_item_t *new_media, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_player_current_media_changed");

    if (!new_media)
    {
        that->callAsync([that] () {
            that->m_seekDragItem.reset();
            that->m_autoMetadataItem.reset();
            {
                vlc_player_locker lock{ that->m_player };
                that->m_currentItem.reset(nullptr);
            }

            that->m_syltRaw.reset();
            that->m_syltLyrics.reset();
            that->m_currentLyricIndex = -1;
            emit that->q_func()->hasLyricsChanged(false);
            emit that->q_func()->syltLyricsChanged();
            emit that->q_func()->currentLyricIndexChanged(-1);
            emit that->q_func()->currentLyricChanged();
            that->UpdateStats({});
            emit that->q_func()->inputChanged(false);
        });
        return;
    }

    SharedInputItem newMediaPtr = SharedInputItem( new_media );
    that->callAsync([that,newMediaPtr] () {
        PlayerController* q = that->q_func();
        that->m_seekDragItem.reset();
        that->m_autoMetadataItem.reset();

        that->m_syltRaw.reset();
        that->m_syltLyrics.reset();
        that->m_currentLyricIndex = -1;
        emit q->hasLyricsChanged(false);
        emit q->syltLyricsChanged();
        emit q->currentLyricIndexChanged(-1);
        emit q->currentLyricChanged();
        that->UpdateStats({});

        that->UpdateArt( newMediaPtr.get() );
        that->UpdateMeta( newMediaPtr.get() );
        that->UpdateInfo( newMediaPtr.get() );
        that->UpdateName( newMediaPtr.get() );
        that->m_url = vlc::wrap_cptr( input_item_GetURI( newMediaPtr.get() ) ).get();

        {
            vlc_player_locker lock{ that->m_player };
            that->m_currentItem = std::move(newMediaPtr);
        }

        that->m_canRestorePlayback = false;
        that->m_playbackRestorePosition = -1.;
        emit q->playbackRestoreQueried();

        emit q->inputChanged(true);
    });
}

static void on_player_length_changed(vlc_player_t *player, vlc_tick_t length, void *data)
{
    auto* that = static_cast<PlayerControllerPrivate*>(data);
    SharedInputItem media(vlc_player_GetCurrentMedia(player));
    that->callAsync([that, media, length] {
        if (that->m_currentItem != media)
            return;
        const auto duration = length > 0 ? VLCDuration(length) : VLCDuration();
        if (that->m_length.toVLCTick() != duration.toVLCTick()) {
            that->m_length = duration;
            emit that->q_func()->lengthChanged(duration);
        }
    });
}

static void on_player_state_changed(vlc_player_t *, enum vlc_player_state state, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_player_state_changed");

    that->callAsync([that,state] () {
        PlayerController* q = that->q_func();
        that->m_playing_status = static_cast<PlayerController::PlayingState>(state);
        switch ( state ) {
        case VLC_PLAYER_STATE_STARTED:
            msg_Dbg( that->p_intf, "on_player_state_changed VLC_PLAYER_STATE_STARTED");
            break;
        case VLC_PLAYER_STATE_PLAYING:
        {
            msg_Dbg( that->p_intf, "on_player_state_changed VLC_PLAYER_STATE_PLAYING");
            SharedAOut aout = q->getAout();
            that->m_audioStereoMode.resetObject( aout.get() );
            that->m_audioMixMode.resetObject( aout.get() );
            that->m_audioVisualization.resetObject( aout.get() );
            break;
        }
        case VLC_PLAYER_STATE_PAUSED:
            that->m_position_timer.stop();
            that->m_time_timer.stop();
            msg_Dbg( that->p_intf, "on_player_state_changed VLC_PLAYER_STATE_PAUSED");
            break;
        case VLC_PLAYER_STATE_STOPPING:
            that->m_seekDragItem.reset();
            msg_Dbg( that->p_intf, "on_player_state_changed VLC_PLAYER_STATE_STOPPING");
            break;
        case VLC_PLAYER_STATE_STOPPED:
        {
            that->m_seekDragItem.reset();
            msg_Dbg( that->p_intf, "on_player_state_changed VLC_PLAYER_STATE_STOPPED");

            that->m_audioStereoMode.resetObject((audio_output_t*)nullptr);
            that->m_audioMixMode.resetObject((audio_output_t*)nullptr);
            that->m_audioVisualization.resetObject((audio_output_t*)nullptr);

            /* reset the state on stop */
            that->m_position = 0;
            that->m_time = 0;
            that->m_length = 0;
            emit q->positionUpdated( -1.0, VLCTime() , VLCDuration() );
            that->m_rate = 1.0f;
            emit q->rateChanged( 1.0f );
            that->m_name = "";
            emit q->nameChanged( "" );
            that->m_hasChapters = false;
            emit q->hasChaptersChanged( false );
            that->m_hasTitles = false;
            emit q->hasTitlesChanged( false );
            that->m_hasMenu= false;
            emit q->hasMenuChanged( false );
            that->m_isMenu= false;
            emit q->isMenuChanged( false );
            that->m_isInteractive= false;
            emit q->isInteractiveChanged( false );
            that->m_canRestorePlayback = false;
            that->m_playbackRestorePosition = -1.;
            emit q->playbackRestoreQueried();

            that->m_teletextAvailable = false;
            emit q->teletextAvailableChanged( false );
            that->m_ABLoopState = PlayerController::ABLOOP_STATE_NONE;
            that->m_ABLoopA = VLCTime();
            that->m_ABLoopB = VLCTime();
            emit q->ABLoopStateChanged(PlayerController::ABLOOP_STATE_NONE);
            emit q->ABLoopAChanged(that->m_ABLoopA);
            emit q->ABLoopBChanged(that->m_ABLoopB);
            that->m_hasVideo = false;
            emit q->hasVideoOutputChanged( false );

            emit q->voutListChanged( NULL, 0 );

            /* Reset all InfoPanels but stats */
            that->m_artUrl = "";
            emit q->artChanged( NULL );
            emit q->artChanged( "" );
            emit q->infoChanged( NULL );
            emit q->currentMetaChanged( (input_item_t *)NULL );

            if (that->m_syltRaw || that->m_currentLyricIndex != -1)
            {
                that->m_syltRaw.reset();
                that->m_syltLyrics.reset();
                that->m_currentLyricIndex = -1;
                emit q->hasLyricsChanged( false );
                emit q->syltLyricsChanged();
                emit q->currentLyricIndexChanged( -1 );
                emit q->currentLyricChanged();
            }

            that->m_hasPrograms =false;
            emit q->hasProgramsChanged( false );

            that->m_encrypted =false;
            emit q->isEncryptedChanged( false );
            that->m_recording =false;
            emit q->recordingChanged( false );

            that->m_buffering = 0.f;
            emit q->bufferingChanged( 0.0 );

            break;
        }
        }
        if (state == VLC_PLAYER_STATE_PLAYING || state == VLC_PLAYER_STATE_PAUSED)
            that->requestAutoMetadata();
        emit q->playingStateChanged( that->m_playing_status );
    });
}

void on_player_error_changed(vlc_player_t *, enum vlc_player_error , void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_player_error_changed");
}

static void on_player_buffering(vlc_player_t *, float new_buffering, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_player_buffering");
    that->callAsync([that,new_buffering](){
        that->m_buffering = new_buffering;
        emit that->q_func()->bufferingChanged( new_buffering );
    });
}

static void on_player_capabilities_changed(vlc_player_t *, int old_caps, int new_caps, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_player_capabilities_changed");
    that->callAsync([that, old_caps, new_caps]() {
        PlayerController* q = that->q_func();
        that->m_capabilities = new_caps;

        bool oldSeekable = old_caps & VLC_PLAYER_CAP_SEEK;
        bool newSeekable = new_caps & VLC_PLAYER_CAP_SEEK;
        if (newSeekable != oldSeekable)
            emit q->seekableChanged( newSeekable );

        bool oldRewindable = old_caps & VLC_PLAYER_CAP_REWIND;
        bool newRewindable = new_caps & VLC_PLAYER_CAP_REWIND;
        if (newRewindable != oldRewindable)
            emit q->rewindableChanged( newRewindable );

        bool oldPauseable = old_caps & VLC_PLAYER_CAP_PAUSE;
        bool newPauseable = new_caps & VLC_PLAYER_CAP_PAUSE;
        if (newPauseable != oldPauseable)
            emit q->pausableChanged( newPauseable );

        bool oldChangeRate = old_caps & VLC_PLAYER_CAP_CHANGE_RATE;
        bool newChangeRate = new_caps & VLC_PLAYER_CAP_CHANGE_RATE;
        if (newChangeRate != oldChangeRate)
            emit q->rateChangableChanged( newChangeRate );
    });

    //FIXME other events?
}

static void on_player_track_list_changed(vlc_player_t *, enum vlc_player_list_action action, const struct vlc_player_track *track, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    struct vlc_player_track* dup = vlc_player_track_Dup(track);
    if (!dup)
        return;
    std::shared_ptr<struct vlc_player_track> trackPtr(dup, vlc_player_track_Delete);

    msg_Dbg( that->p_intf, "on_player_track_list_changed");
    that->callAsync([that,action,trackPtr] () {
        switch (trackPtr.get()->fmt.i_cat) {
        case VIDEO_ES:
            msg_Dbg( that->p_intf, "on_player_track_list_changed (video)");
            that->m_videoTracks.updateTracks( action, trackPtr.get() );
            break;
        case AUDIO_ES:
            msg_Dbg( that->p_intf, "on_player_track_list_changed (audio)");
            that->m_audioTracks.updateTracks( action, trackPtr.get() );
            break;
        case SPU_ES:
            msg_Dbg( that->p_intf, "on_player_track_list_changed (spu)");
            that->m_subtitleTracks.updateTracks( action, trackPtr.get() );
            break;
        default:
            //we don't handle other kind of tracks
            msg_Dbg( that->p_intf, "on_player_track_list_changed (other)");
            break;
        }
    });
}

static void on_player_track_selection_changed(vlc_player_t *, vlc_es_id_t * unselected, vlc_es_id_t *selected, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_player_track_selection_changed");

    SharedEsId unselectedPtr = SharedEsId(unselected);
    SharedEsId selectedPtr = SharedEsId(selected);

    that->callAsync([that,unselectedPtr,selectedPtr] () {
        if (unselectedPtr)
            that->UpdateTrackSelection( unselectedPtr.get(), false );
        if (selectedPtr)
            that->UpdateTrackSelection( selectedPtr.get(), true );
    });
}


static void on_player_program_list_changed(vlc_player_t *, enum vlc_player_list_action action, const struct vlc_player_program *prgm, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_player_program_list_changed");
    struct vlc_player_program* dup = vlc_player_program_Dup(prgm);
    if (!dup)
        return;
    std::shared_ptr<struct vlc_player_program> prgmPtr(dup, vlc_player_program_Delete);

    that->callAsync([that,action,prgmPtr] (){
        that->UpdateProgram(action, prgmPtr.get());
    });
}

static void on_player_program_selection_changed(vlc_player_t *, int unselected, int selected, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_player_program_selection_changed");

    that->callAsync([that,unselected,selected] (){
        that->m_programList.updateProgramSelection(unselected, false);
        that->m_programList.updateProgramSelection(selected, true);
    });
}

static void on_player_titles_changed(vlc_player_t *, struct vlc_player_title_list *titles, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_player_title_array_changed");

    SharedTitleList sharedTitleList = SharedTitleList(titles);

    that->callAsync([that,sharedTitleList] (){
        struct vlc_player_title_list *titles = sharedTitleList.get();
        that->m_chapterList.resetTitle(nullptr);
        that->m_titleList.resetTitles(titles);

        bool hasMenu = false;
        bool hasTitles = false;
        if (titles)
        {
            size_t nbTitles = vlc_player_title_list_GetCount(titles);
            for( size_t i = 0; i < nbTitles; i++)
            {
                const vlc_player_title* title = vlc_player_title_list_GetAt(titles, i);
                if( (title ->flags & INPUT_TITLE_MENU) != 0 )
                {
                    hasMenu = true;
                    break;
                }
            }
            hasTitles = nbTitles != 0;
        }
        if (hasTitles != that->m_hasTitles)
        {
            that->m_hasTitles = hasTitles;
            emit that->q_func()->hasTitlesChanged(hasTitles);
        }
        if (!hasTitles && that->m_hasChapters)
        {
            that->m_hasChapters = false;
            emit that->q_func()->hasChaptersChanged(false);
        }
        if (hasMenu != that->m_hasMenu)
        {
            that->m_hasMenu = hasMenu;
            emit that->q_func()->hasMenuChanged(hasMenu);
        }
    });
}

static void on_player_title_selection_changed(vlc_player_t *,
                                              const struct vlc_player_title *new_title, size_t new_idx, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_player_title_selection_changed");

    bool hasChapter = (new_title != nullptr && new_title->chapter_count != 0);
    that->callAsync([that,new_title,new_idx,hasChapter] (){
        PlayerController* q = that->q_func();
        that->m_chapterList.resetTitle(new_title);
        that->m_titleList.setCurrent(new_idx);
        that->m_hasChapters  = hasChapter;
        that->m_isInteractive = new_title && (new_title->flags & VLC_PLAYER_TITLE_INTERACTIVE);
        that->m_isMenu = new_title && (new_title->flags & VLC_PLAYER_TITLE_MENU);
        emit q->isMenuChanged( that->m_isMenu );
        emit q->isInteractiveChanged( that->m_isInteractive );
        emit q->hasChaptersChanged( hasChapter );
    });
}

static void on_player_chapter_selection_changed(vlc_player_t *,
                                                const struct vlc_player_title *, size_t,
                                                const struct vlc_player_chapter *, size_t chapter_idx,
                                                void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_player_chapter_selection_changed");
    that->callAsync([that,chapter_idx] (){
        that->m_chapterList.setCurrent(chapter_idx);
    });
}


static void on_player_teletext_menu_changed(vlc_player_t *, bool has_teletext_menu, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_player_teletext_menu_changed, %s", has_teletext_menu ? "available" : "unavailable" );
    that->callAsync([that,has_teletext_menu] () {
        that->m_teletextAvailable = has_teletext_menu;
        emit that->q_func()->teletextAvailableChanged(has_teletext_menu);
    });
}

static void on_player_teletext_enabled_changed(vlc_player_t *, bool enabled, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_player_teletext_enabled_changed %s", enabled ? "enabled" : "disabled");
    that->callAsync([that,enabled] () {
        that->m_teletextEnabled = enabled;
        emit that->q_func()->teletextEnabledChanged(enabled);
    });
}

static void on_player_teletext_page_changed(vlc_player_t *, unsigned new_page, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_player_teletext_page_changed %u", new_page);
    that->callAsync([that,new_page] () {
        that->m_teletextPage = new_page;
        emit that->q_func()->teletextPageChanged(new_page);
    });
}

static void on_player_teletext_transparency_changed(vlc_player_t *, bool enabled, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_player_teletext_transparency_changed %s", enabled ? "enabled" : "disabled");
    that->callAsync([that,enabled] () {
        that->m_teletextTransparent = enabled;
        emit that->q_func()->teletextTransparencyChanged(enabled);
    });
}

static void on_player_category_delay_changed(vlc_player_t *,
                               enum es_format_category_e cat, vlc_tick_t new_delay,
                               void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_player_category_delay_changed: %d", cat );
    that->callAsync([that,cat,new_delay] (){
        switch (cat)
        {
            case AUDIO_ES:
                that->m_audioDelay = new_delay;
                emit that->q_func()->audioDelayChanged( new_delay );
                break;
            case SPU_ES:
                that->m_subtitleDelay = new_delay;
                emit that->q_func()->subtitleDelayChanged( new_delay );
                break;
            default:
                break;
        }
    });
}

static void on_player_track_delay_changed(vlc_player_t *,
                                vlc_es_id_t *es_id, vlc_tick_t new_delay,
                                void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    SharedEsId sharedEsId = SharedEsId(es_id);

    that->callAsync([that,sharedEsId,new_delay] (){
        if (that->m_secondarySpuEsId == sharedEsId)
        {
            that->m_secondarySubtitleDelay = new_delay;
            emit that->q_func()->secondarySubtitleDelayChanged( new_delay );
        }
    });
}

static void on_player_associated_subs_fps_changed(vlc_player_t *, float subs_fps, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_associated_subs_fps_changed");
    that->callAsync([that,subs_fps] (){
        that->m_subtitleFPS = subs_fps;
        emit that->q_func()->subtitleFPSChanged( subs_fps );
    });
}

static void on_player_subtitle_text_size_changed(vlc_player_t *, unsigned scale,
    enum vlc_player_subtitle_size_mode mode, unsigned pixels, void *data)
{
    auto *that = static_cast<PlayerControllerPrivate *>(data);
    that->callAsync([that, scale, mode, pixels] {
        that->m_subtitleTextScale = scale;
        that->m_subtitleTextSizeMode = mode;
        that->m_subtitleTextPixelSize = pixels;
        emit that->q_func()->subtitleTextScaleChanged(scale);
        emit that->q_func()->subtitleTextSizeModeChanged(mode);
        emit that->q_func()->subtitleTextPixelSizeChanged(pixels);
    });
}

void on_player_renderer_changed(vlc_player_t *, vlc_renderer_item_t *new_item, void *data)
{
    VLC_UNUSED(new_item);
    VLC_UNUSED(data);
}


static void on_player_record_changed(vlc_player_t *, bool recording, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_player_record_changed");
    that->callAsync([that,recording] (){
        that->m_recording = recording;
        emit that->q_func()->recordingChanged( recording );
    });
}

static void on_player_signal_changed(vlc_player_t *, float quality, float strength, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    VLC_UNUSED(quality);
    VLC_UNUSED(strength);
    msg_Dbg( that->p_intf, "on_player_signal_changed");
}

static void on_player_stats_changed(vlc_player_t *, const struct input_stats_t *stats, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    struct input_stats_t stats_tmp = *stats;
    that->callAsync([that,stats_tmp] () {
        that->UpdateStats(stats_tmp);
    });
}

static void on_player_atobloop_changed(vlc_player_t *, enum vlc_player_abloop state, vlc_tick_t time, double, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_player_atobloop_changed");
    that->callAsync([that,state,time] (){
        PlayerController* q = that->q_func();
        switch (state) {
        case VLC_PLAYER_ABLOOP_NONE:
            that->m_ABLoopA = VLCTime();
            that->m_ABLoopB = VLCTime();
            emit q->ABLoopAChanged(that->m_ABLoopA);
            emit q->ABLoopBChanged(that->m_ABLoopB);
            break;
        case VLC_PLAYER_ABLOOP_A:
            that->m_ABLoopA = time;
            emit q->ABLoopAChanged(that->m_ABLoopA);
            break;
        case VLC_PLAYER_ABLOOP_B:
            that->m_ABLoopB = time;
            emit q->ABLoopBChanged(that->m_ABLoopB);
            break;
        }
        that->m_ABLoopState = static_cast<PlayerController::ABLoopState>(state);
        emit q->ABLoopStateChanged(that->m_ABLoopState);
    });
}

static void on_player_media_meta_changed(vlc_player_t *, input_item_t *media, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_player_item_meta_changed");

    SharedInputItem mediaPtr(media);
    //call on object thread
    that->callAsync([that,mediaPtr] () {
        that->UpdateName(mediaPtr.get());
        that->UpdateArt(mediaPtr.get());
        that->UpdateMeta(mediaPtr.get());
        that->UpdateInfo(mediaPtr.get());
        // Consecutive inputs can keep the player in PLAYING without another
        // state-change event. Their demux metadata still needs a lookup.
        that->requestAutoMetadata();
    });
}

static void on_player_media_epg_changed(vlc_player_t *, input_item_t *, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_player_item_epg_changed");
    that->callAsync([that] () {
        emit that->q_func()->epgChanged();
    });
}

static void on_player_subitems_changed(vlc_player_t *, input_item_t *, const input_item_node_t *, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_player_subitems_changed");
}


static void on_player_vout_changed(vlc_player_t *player, enum vlc_player_vout_action action,
    vout_thread_t *, enum vlc_vout_order order, vlc_es_id_t *es_id, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_player_vout_list_changed");

    switch (vlc_es_id_GetCat(es_id))
    {
        case VIDEO_ES:
        {
            //player is locked within callbacks*
            size_t i_vout = 0;
            vout_thread_t **vouts = vlc_player_vout_HoldAll(player, &i_vout);

            std::shared_ptr<vout_thread_t*> voutsPtr( vouts, [i_vout]( vout_thread_t**vouts ) {
                for (size_t i = 0; i < i_vout; i++)
                    vout_Release( vouts[i] );
                free(vouts);
            });

            //call on object thread
            that->callAsync([that,voutsPtr,i_vout,action] () {
                that->UpdateVouts(voutsPtr.get(), i_vout, action);
            });
            break;
        }
        case SPU_ES:
        {
            SharedEsId sharedEsId = SharedEsId(es_id);
            that->callAsync([that,sharedEsId,order] () {
                that->UpdateSpuOrder(sharedEsId.get(), order);
            });
            break;
        }
        default:
            break;
    }
}

//player vout callbacks

static void on_player_vout_fullscreen_changed(vout_thread_t* vout, bool is_fullscreen, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_player_vout_fullscreen_changed %s", is_fullscreen ? "fullscreen" : "windowed");

    SharedVOutThread vOutThread(vout);
    that->callAsync([that,vOutThread,is_fullscreen] () {
        PlayerController* q = that->q_func();
        const PlayerController::VOutThreadList voutList = q->getVouts();
        if (vOutThread == nullptr //property sets for all vout
            || (voutList.size() == 1 && vOutThread.get() == voutList[0].get()) ) //on the only vout
        {
            that->m_fullscreen = is_fullscreen;
            emit q->fullscreenChanged(is_fullscreen);
        }
    });
}

static void on_player_vout_wallpaper_mode_changed(vout_thread_t* vout, bool enabled, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_player_vout_wallpaper_mode_changed %s", enabled ? "enabled" : "disabled");

    SharedVOutThread vOutThread(vout);
    that->callAsync([that,vOutThread, enabled] () {
        PlayerController* q = that->q_func();
        const PlayerController::VOutThreadList voutList = q->getVouts();
        if (vOutThread == nullptr  //property sets for all vout
            || (voutList.size() == 1 && vOutThread.get() == voutList[0].get()) ) //on the only vout
        {
            that->m_wallpaperMode = enabled;
            emit q->wallpaperModeChanged(enabled);
        }
    });
}

//player aout callbacks

static void on_player_aout_volume_changed(audio_output_t *, float volume, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_player_aout_volume_changed");
    that->callAsync([that,volume](){
        that->m_volume = volume;
        emit that->q_func()->volumeChanged( volume );
    });
}

static void on_player_aout_mute_changed(audio_output_t *, bool muted, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_player_aout_mute_changed");
    that->callAsync([that,muted](){
        that->m_muted = muted;
        emit that->q_func()->soundMuteChanged(muted);
    });
}

static void on_player_rate_changed(vlc_player_t *, float rate, void *data)
{
    auto *that = static_cast<PlayerControllerPrivate *>(data);
    that->callAsync([that, rate] {
        // Rate controls also work while paused, when the playback clock does
        // not produce new points. Update the value without moving that clock.
        if (that->m_rate != rate)
        {
            that->m_rate = rate;
            emit that->q_func()->rateChanged(rate);
        }
    });
}

static void on_player_corks_changed(vlc_player_t *, unsigned, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_player_corks_changed");
}

static void on_player_playback_restore_queried(vlc_player_t *player, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    msg_Dbg( that->p_intf, "on_playback_restore_queried");
    SharedInputItem media(vlc_player_GetCurrentMedia(player));
    const auto position = vlc_player_GetPlaybackRestorePosition(player);
    that->callAsync([that, media, position](){
        if (that->m_currentItem != media)
            return;
        that->m_playbackRestorePosition = position;
        that->m_canRestorePlayback = true;
        emit that->q_func()->playbackRestoreQueried();
    });
}

static void on_player_timer_update(const struct vlc_player_timer_point *point,
                                   void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    if (that->seeking)
        return;

    that->callAsync([that,point_copy = *point](){
        PlayerController* q = that->q_func();

        bool lengthOrRateChanged = that->m_position_timer.isActive()
            && that->m_player_time.rate != point_copy.rate;

        if (that->m_length.toVLCTick() != point_copy.length)
        {
            that->m_length = point_copy.length > 0 ? VLCDuration(point_copy.length) : VLCDuration();
            emit q->lengthChanged(that->m_length);

            lengthOrRateChanged = true;
        }
        if (that->m_rate != point_copy.rate)
        {
            that->m_rate = point_copy.rate;
            emit q->rateChanged(that->m_rate);

            lengthOrRateChanged = true;
        }

        // Keep rate/length changes from paused controls, but do not let a
        // late advancing point move the paused UI or restart its clock.
        // Paused seeks have their own explicit on_seek callback.
        if (that->m_playing_status == PlayerController::PLAYING_STATE_PAUSED
            && point_copy.system_date != VLC_TICK_MAX)
            return;
        that->m_player_time = point_copy;

        vlc_tick_t system_now = vlc_tick_now();
        if (that->interpolateTime(system_now) == VLC_SUCCESS)
        {
            if (lengthOrRateChanged || !that->m_position_timer.isActive())
            {
                q->updatePosition();

                if (that->m_player_time.system_date != VLC_TICK_MAX)
                {
                    that->startPositionTimer();
                }
            }
            q->updateTime(system_now, lengthOrRateChanged);
        }

    });
}

static void on_player_timer_paused(vlc_tick_t system_date, void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    that->callAsync([that,system_date](){
        PlayerController* q = that->q_func();

        if (system_date != VLC_TICK_INVALID
         && that->interpolateTime(system_date) == VLC_SUCCESS)
        {
            // The discontinuity event got a valid system date, update the time
            // properties.
            q->updatePosition();
            q->updateTime(system_date, false);
        }

        // And stop the timers.
        that->m_position_timer.stop();
        that->m_time_timer.stop();
    });
}

static void on_player_timer_seek(const struct vlc_player_timer_point *point,
                                 void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate*>(data);
    if (point != NULL)
    {
        that->seeking = true;
        that->callAsync([that,point_copy = *point](){
            PlayerController* q = that->q_func();

            that->m_player_time = point_copy;
            if (that->m_player_time.position >= 0)
                that->m_position = that->m_player_time.position;
            if (that->m_player_time.ts != VLC_TICK_INVALID)
                that->m_time = that->m_player_time.ts - VLC_TICK_0;

            q->updatePosition();
            q->updateTime(VLC_TICK_INVALID, false);

            that->m_position_timer.stop();
            that->m_time_timer.stop();
        });
    }
    else
        that->seeking = false;
}
static void on_player_timer_smpte_update(const struct vlc_player_timer_smpte_timecode *tc,
                                         void *data)
{
    PlayerControllerPrivate* that = static_cast<PlayerControllerPrivate *>(data);
#ifndef QT_HAS_LIBATOMIC
    QWriteLocker lock(&that->m_highResolutionTimeLock);
#endif
    that->m_highResolutionTime = *tc;
}


struct ArtFetchContext
{
    PlayerControllerPrivate *controller;
    bool automatic;
    quint64 generation;
    SharedInputItem original;
    QStringList before;
};

static void on_preparse_ended_callback(vlc_preparser_req *req,
                                       int status, void *userdata)
{
    std::unique_ptr<ArtFetchContext> context(static_cast<ArtFetchContext *>(userdata));
    // Fetch-only requests can keep the parser's default error status even
    // after fetching metadata. Only cancellation/timeout discards the result.
    if (status == -EINTR || status == VLC_ETIMEOUT)
        return;
    input_item_t *p_item = vlc_preparser_req_GetItem(req);
    context->controller->onArtFetchEnded(p_item, context->original.get(),
                                        context->automatic, context->generation, context->before);
}


} //extern "C"

static const struct vlc_player_cbs player_cbs = [] {
    struct vlc_player_cbs cbs {};
    cbs.on_current_media_changed = on_player_current_media_changed;
    cbs.on_state_changed = on_player_state_changed;
    cbs.on_length_changed = on_player_length_changed;
    cbs.on_error_changed = on_player_error_changed;
    cbs.on_buffering_changed = on_player_buffering;
    cbs.on_capabilities_changed = on_player_capabilities_changed;
    cbs.on_track_list_changed = on_player_track_list_changed;
    cbs.on_track_selection_changed = on_player_track_selection_changed;
    cbs.on_track_delay_changed = on_player_track_delay_changed;
    cbs.on_program_list_changed = on_player_program_list_changed;
    cbs.on_program_selection_changed = on_player_program_selection_changed;
    cbs.on_titles_changed = on_player_titles_changed;
    cbs.on_title_selection_changed = on_player_title_selection_changed;
    cbs.on_chapter_selection_changed = on_player_chapter_selection_changed;
    cbs.on_teletext_menu_changed = on_player_teletext_menu_changed;
    cbs.on_teletext_enabled_changed = on_player_teletext_enabled_changed;
    cbs.on_teletext_page_changed = on_player_teletext_page_changed;
    cbs.on_teletext_transparency_changed = on_player_teletext_transparency_changed;
    cbs.on_category_delay_changed = on_player_category_delay_changed;
    cbs.on_associated_subs_fps_changed = on_player_associated_subs_fps_changed;
    cbs.on_subtitle_text_size_changed = on_player_subtitle_text_size_changed;
    cbs.on_renderer_changed = on_player_renderer_changed;
    cbs.on_recording_changed = on_player_record_changed;
    cbs.on_signal_changed = on_player_signal_changed;
    cbs.on_statistics_changed = on_player_stats_changed;
    cbs.on_atobloop_changed = on_player_atobloop_changed;
    cbs.on_media_meta_changed = on_player_media_meta_changed;
    cbs.on_media_epg_changed = on_player_media_epg_changed;
    cbs.on_media_subitems_changed = on_player_subitems_changed;
    cbs.on_vout_changed = on_player_vout_changed;
    cbs.on_cork_changed = on_player_corks_changed;
    cbs.on_rate_changed = on_player_rate_changed;
    cbs.on_playback_restore_queried = on_player_playback_restore_queried;
    return cbs;
}();

static const vlc_player_vout_cbs player_vout_cbs = []{
    struct vlc_player_vout_cbs cbs{};
    cbs.on_fullscreen_changed = on_player_vout_fullscreen_changed;
    cbs.on_wallpaper_mode_changed = on_player_vout_wallpaper_mode_changed;
    return cbs;
}();

static const vlc_player_aout_cbs player_aout_cbs = []{
    struct vlc_player_aout_cbs cbs{};
    cbs.on_volume_changed = on_player_aout_volume_changed;
    cbs.on_mute_changed = on_player_aout_mute_changed;
    return cbs;
}();

static const vlc_player_timer_cbs player_timer_cbs = []{
    struct vlc_player_timer_cbs cbs {};
    cbs.on_update = on_player_timer_update;
    cbs.on_paused = on_player_timer_paused;
    cbs.on_seek = on_player_timer_seek;
    return cbs;
}();

static const struct vlc_player_timer_smpte_cbs player_timer_smpte_cbs = []{
    struct vlc_player_timer_smpte_cbs cbs {};
    cbs.on_update = on_player_timer_smpte_update;
    return cbs;
}();

// art fetcher callbacks

static const struct vlc_preparser_cbs art_fetcher_cbs = []{
    struct vlc_preparser_cbs cbs{};
    cbs.on_ended = on_preparse_ended_callback;
    return cbs;
}();


PlayerControllerPrivate::PlayerControllerPrivate(PlayerController *playercontroller, qt_intf_t *p_intf)
    : q_ptr(playercontroller)
    , p_intf(p_intf)
    , m_player(p_intf->p_player)
    , m_videoTracks(m_player)
    , m_audioTracks(m_player)
    , m_subtitleTracks(m_player)
    , m_titleList(m_player)
    , m_chapterList(m_player)
    , m_programList(m_player)
    , m_videoStereoMode((vout_thread_t*)nullptr, "video-stereo-mode")
    , m_zoom((vout_thread_t*)nullptr, "zoom")
    , m_aspectRatio((vout_thread_t*)nullptr, "aspect-ratio")
    , m_crop((vout_thread_t*)nullptr, "crop")
    , m_fit((vout_thread_t*)nullptr, "fit")
    , m_deinterlace((vout_thread_t*)nullptr, "deinterlace")
    , m_deinterlaceMode((vout_thread_t*)nullptr, "deinterlace-mode")
    , m_autoscale((vout_thread_t*)nullptr, "autoscale")
    , m_audioStereoMode((audio_output_t*)nullptr, "stereo-mode")
    , m_audioMixMode((audio_output_t*)nullptr, "mix-mode")
    , m_audioDeviceList(m_player)
    , m_audioVisualization((audio_output_t*)nullptr, "visual")
    , m_rendererManager(p_intf, m_player)
{
    {
        vlc_player_locker locker{m_player};
        m_player_listener = vlc_player_AddListener( m_player, &player_cbs, this );
        m_player_aout_listener = vlc_player_aout_AddListener( m_player, &player_aout_cbs, this );
        m_player_vout_listener = vlc_player_vout_AddListener( m_player, &player_vout_cbs, this );
        m_player_timer = vlc_player_AddTimer( m_player, VLC_TICK_FROM_MS(500), &player_timer_cbs, this );
        m_subtitleFPS = vlc_player_GetAssociatedSubsFPS(m_player);
        m_subtitleTextScale = vlc_player_GetSubtitleTextScale(m_player);
        m_subtitleTextSizeMode = vlc_player_GetSubtitleTextSizeMode(m_player);
        m_subtitleTextPixelSize = vlc_player_GetSubtitleTextPixelSize(m_player);
    }

    QObject::connect( &m_autoscale, &QVLCBool::valueChanged, q_ptr, &PlayerController::autoscaleChanged );
    QObject::connect( &m_audioVisualization, &VLCVarChoiceModel::hasCurrentChanged, q_ptr, &PlayerController::hasAudioVisualizationChanged );

    m_time_timer.setSingleShot( true );
    m_time_timer.setTimerType( Qt::PreciseTimer );

    // Initialise fullscreen to match the player state
    m_fullscreen = vlc_player_vout_IsFullscreen( m_player );

    m_volume = vlc_player_aout_GetVolume( m_player );
}

void PlayerControllerPrivate::addSMPTETimer()
{
    assert( !m_player_timer_smpte );

    vlc_player_locker lock{ m_player };
    m_player_timer_smpte = vlc_player_AddSmpteTimer( m_player, &player_timer_smpte_cbs, this );
    assert( m_player_timer_smpte );
}

void PlayerControllerPrivate::removeSMPTETimer()
{
    assert( m_player_timer_smpte );

    vlc_player_locker lock{ m_player };
    vlc_player_RemoveTimer( m_player, m_player_timer_smpte );
    m_player_timer_smpte = nullptr;
}

PlayerController::PlayerController( qt_intf_t *_p_intf )
    : QObject(NULL)
    , d_ptr( new PlayerControllerPrivate(this, _p_intf) )
{
    connect( &d_ptr->m_position_timer, &QTimer::timeout, this, &PlayerController::updatePositionFromTimer );
    connect( &d_ptr->m_time_timer, &QTimer::timeout, this, &PlayerController::updateTimeFromTimer );
    connect(this, &PlayerController::inputChanged, this, [this] {
        d_ptr->resetSeekPreview();
        d_ptr->updateSeekSampling();
    });
    auto* d = d_ptr.data();
    d->m_seekSampleTimer.setSingleShot(true);
    connect(&d->m_seekSampleTimer, &QTimer::timeout, this, [d] { d->sampleSeekPreview(); });
    const auto updateSamples = [d] { d->updateSeekSampling(); };
    connect(this, &PlayerController::lengthChanged, this, updateSamples);
    connect(this, &PlayerController::seekableChanged, this, updateSamples);
    connect(this, &PlayerController::hasVideoOutputChanged, this, updateSamples);
    connect(this, &PlayerController::playingStateChanged, this, updateSamples);
    connect(this, &PlayerController::bufferingChanged, this, updateSamples);
    d->m_progressiveWaveformTimer.setInterval(500);
    connect(&d->m_progressiveWaveformTimer, &QTimer::timeout, this, [d] { d->readProgressiveWaveform(); });
    d->m_audioWaveformWorkers.setMaxThreadCount(1);
    d->m_audioWaveformRefresh.setSingleShot(true);
    d->m_audioWaveformRefresh.setInterval(100);
    d->m_audioWaveformTimeout.setSingleShot(true);
    d->m_audioWaveformTimeout.setInterval(15000);
    connect(&d->m_audioWaveformRefresh, &QTimer::timeout, this, [d] { d->readAudioWaveform(); });
    connect(&d->m_audioWaveformTimeout, &QTimer::timeout, this, [d] {
        if (d->m_audioWaveformRequest) vlc_interrupt_kill(d->m_audioWaveformRequest->interrupt);
    });
    const auto refreshWaveform = [d] { d->refreshAudioWaveform(); };
    connect(this, &PlayerController::inputChanged, this, refreshWaveform);
    connect(this, &PlayerController::lengthChanged, this, refreshWaveform);
    connect(&d->m_audioTracks, &QAbstractItemModel::dataChanged, this, refreshWaveform);
    connect(&d->m_audioTracks, &QAbstractItemModel::rowsInserted, this, refreshWaveform);
    connect(&d->m_audioTracks, &QAbstractItemModel::rowsRemoved, this, refreshWaveform);
    connect(&d->m_audioTracks, &QAbstractItemModel::modelReset, this, refreshWaveform);
    connect(&d->m_videoTracks, &QAbstractItemModel::rowsInserted, this, refreshWaveform);
    connect(&d->m_videoTracks, &QAbstractItemModel::rowsRemoved, this, refreshWaveform);
    connect(&d->m_videoTracks, &QAbstractItemModel::modelReset, this, refreshWaveform);
    d->m_subtitleTimelineWorkers.setMaxThreadCount(1);
    d->m_subtitleTimelineRefresh.setSingleShot(true);
    d->m_subtitleTimelineRefresh.setInterval(100);
    d->m_subtitleTimelineTimeout.setSingleShot(true);
    d->m_subtitleTimelineTimeout.setInterval(15000);
    connect(&d->m_subtitleTimelineRefresh, &QTimer::timeout, this, [d] { d->readSubtitleTimeline(); });
    connect(&d->m_subtitleTimelineTimeout, &QTimer::timeout, this, [d] {
        if (d->m_subtitleTimelineRequest) vlc_interrupt_kill(d->m_subtitleTimelineRequest->interrupt);
    });
    const auto refreshTimeline = [d] { d->resetSubtitleTimeline(); };
    connect(this, &PlayerController::inputChanged, this, refreshTimeline);
    connect(this, &PlayerController::subtitleFPSChanged, this, refreshTimeline);
    connect(&d->m_subtitleTracks, &QAbstractItemModel::dataChanged, this, refreshTimeline);
    connect(&d->m_subtitleTracks, &QAbstractItemModel::rowsInserted, this, refreshTimeline);
    connect(&d->m_subtitleTracks, &QAbstractItemModel::rowsRemoved, this, refreshTimeline);
    connect(&d->m_subtitleTracks, &QAbstractItemModel::modelReset, this, refreshTimeline);
    connect(&d->m_videoTracks, &QAbstractItemModel::dataChanged, this, refreshTimeline);
    connect(&d->m_videoTracks, &QAbstractItemModel::rowsInserted, this, refreshTimeline);
}

PlayerController::~PlayerController()
{
}

bool PlayerController::getSubtitleTimelineEnabled() const { return d_ptr->m_subtitleTimelineEnabled; }
QVariantList PlayerController::getSubtitleTimelineCues() const { return d_ptr->m_subtitleTimelineCues; }
QString PlayerController::getSubtitleTimelineState() const { return d_ptr->m_subtitleTimelineState; }
double PlayerController::getLengthSeconds() const { return d_ptr->m_length.valid() ? d_ptr->m_length.toSecf() : 0; }

void PlayerController::setSubtitleTimelineEnabled(bool enabled)
{
    Q_D(PlayerController);
    if (d->m_subtitleTimelineEnabled == enabled) return;
    d->m_subtitleTimelineEnabled = enabled;
    d->resetSubtitleTimeline();
    emit subtitleTimelineEnabledChanged();
}

void PlayerControllerPrivate::resetSubtitleTimeline()
{
    ++m_subtitleTimelineGeneration;
    m_subtitleTimelineRefresh.stop();
    m_subtitleTimelineTimeout.stop();
    if (m_subtitleTimelineRequest) vlc_interrupt_kill(m_subtitleTimelineRequest->interrupt);
    m_subtitleTimelineRequest.reset();
    m_subtitleTimelineWorkers.clear();
    m_subtitleTimelineCues.clear();
    m_subtitleTimelineState = m_subtitleTimelineEnabled ? QStringLiteral("loading") : QStringLiteral("off");
    emit q_ptr->subtitleTimelineChanged();
    if (m_subtitleTimelineEnabled) m_subtitleTimelineRefresh.start();
}

void PlayerControllerPrivate::readSubtitleTimeline()
{
    SubtitleTimelineSource source;
    bool external = false;
    SharedInputItem media;
    SharedEsId selectedId;
    {
        vlc_player_locker lock{m_player};
        if (!isCurrentItemSynced() || !m_currentItem) {
            m_subtitleTimelineState = QStringLiteral("no-track");
            emit q_ptr->subtitleTimelineChanged();
            return;
        }
        const vlc_player_track* selected = nullptr;
        for (size_t i = 0; i < vlc_player_GetTrackCount(m_player, SPU_ES); ++i) {
            const auto* track = vlc_player_GetTrackAt(m_player, SPU_ES, i);
            if (track->selected && track->es_id != m_secondarySpuEsId.get()) { selected = track; break; }
        }
        if (!selected) {
            m_subtitleTimelineState = QStringLiteral("no-track");
            emit q_ptr->subtitleTimelineChanged();
            return;
        }
        media = m_currentItem;
        selectedId.reset(selected->es_id, true);
        if (const char* uri = vlc_es_id_GetSourceUri(selected->es_id)) {
            source.uri = uri;
            external = true;
        }
        const QByteArray id = vlc_es_id_GetStrId(selected->es_id);
        const qsizetype split = id.indexOf("/spu/");
        const QByteArray prefix = split < 0 ? QByteArray() : id.first(split + 1);
        if ((!prefix.isEmpty() && prefix.size() != 33) || (prefix.isEmpty() && !id.startsWith("spu/"))) {
            m_subtitleTimelineState = QStringLiteral("unavailable");
            emit q_ptr->subtitleTimelineChanged();
            return; // Derived closed-caption IDs cannot be reopened as SPU tracks.
        }
        source.subtitleIndex = 0;
        for (size_t i = 0; i < vlc_player_GetTrackCount(m_player, SPU_ES); ++i) {
            const auto* track = vlc_player_GetTrackAt(m_player, SPU_ES, i);
            if (track == selected) break;
            const QByteArray other = vlc_es_id_GetStrId(track->es_id);
            if (prefix.isEmpty() ? other.startsWith("spu/") : other.startsWith(QByteArray(prefix + "spu/")))
                ++source.subtitleIndex;
        }
        source.duration = vlc_player_GetLength(m_player);
        if (const auto* video = vlc_player_GetSelectedTrack(m_player, VIDEO_ES)) {
            const auto& format = video->fmt.video;
            source.videoWidth = format.i_visible_width;
            source.videoHeight = format.i_visible_height;
            if (format.i_frame_rate_base) source.originalFPS = double(format.i_frame_rate) / format.i_frame_rate_base;
        }
        {
            vlc_mutex_locker itemLock{&m_currentItem->lock};
            if (prefix.isEmpty()) source.uri = m_currentItem->psz_uri;
            // Preserve trusted per-item settings. The two HTTP header options
            // below are also explicitly safe in the native HTTP access module.
            for (int i = 0; i < m_currentItem->i_options; ++i) {
                QByteArray option = m_currentItem->ppsz_options[i];
                if (option.startsWith(':')) option.remove(0, 1);
                const auto equal = option.indexOf('=');
                QByteArray name = equal < 0 ? option : option.first(equal);
                const QByteArray value = equal < 0 ? QByteArray() : option.sliced(equal + 1);
                bool negative = false;
                int type = config_GetType(name.constData());
                if (!type && equal < 0 && name.startsWith("no")) {
                    name.remove(0, name.startsWith("no-") ? 3 : 2);
                    type = config_GetType(name.constData()); negative = true;
                }
                const bool trusted = unsigned(i) < m_currentItem->optflagc
                    && (m_currentItem->optflagv[i] & VLC_INPUT_OPTION_TRUSTED);
                if (!type || (!trusted && name != "http-referrer" && name != "http-user-agent")) continue;
                if (type != VLC_VAR_BOOL && value.isEmpty()) continue;
                QVariant parsed;
                switch (type) {
                case VLC_VAR_BOOL: {
                    bool number = false; const auto integer = value.toLongLong(&number, 0);
                    parsed = equal < 0 ? !negative : number ? integer != 0 : value.toLower() == "true" || value.toLower() == "yes";
                    break;
                }
                case VLC_VAR_INTEGER: parsed = value.toLongLong(nullptr, 0); break;
                case VLC_VAR_FLOAT: parsed = value.toFloat(); break;
                case VLC_VAR_STRING: parsed = QString::fromUtf8(value); break;
                default: continue;
                }
                source.options.insert(QString::fromUtf8(name), parsed);
            }
        }
        if (external) {
            source.demux = "subtitle";
            const float fps = vlc_player_GetAssociatedSubsFPS(m_player);
            if (source.originalFPS > 1 && fps > 1) source.subtitleRate = source.originalFPS / fps;
        } else {
            const auto demux = source.options.value(QStringLiteral("demux")).toString().toUtf8();
            if (!demux.isEmpty()) source.demux = demux;
        }
    }
    if (source.uri.isEmpty()) {
        m_subtitleTimelineState = QStringLiteral("unavailable");
        emit q_ptr->subtitleTimelineChanged();
        return;
    }
    auto request = std::make_shared<TimelineReadRequest>();
    if (!request->interrupt) {
        m_subtitleTimelineState = QStringLiteral("unavailable");
        emit q_ptr->subtitleTimelineChanged();
        return;
    }
    m_subtitleTimelineRequest = request;
    m_subtitleTimelineTimeout.start();
    source.deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(15);
    const auto generation = m_subtitleTimelineGeneration;
    m_subtitleTimelineWorkers.start([this, request, source, external, generation, media, selectedId]() mutable {
        auto result = scanSubtitleTimeline(VLC_OBJECT(p_intf), source, request->interrupt);
        if (external && result.status == SubtitleTimelineScan::Failed) {
            source.demux = "any";
            result = scanSubtitleTimeline(VLC_OBJECT(p_intf), source, request->interrupt);
        }
        QVariantList cues;
        if (result.status == SubtitleTimelineScan::Complete) {
            cues.reserve(result.cues.size());
            for (const auto& cue : result.cues)
                cues.append(QVariantMap{{QStringLiteral("start"), double(cue.start) / CLOCK_FREQ},
                                        {QStringLiteral("stop"), double(cue.stop) / CLOCK_FREQ},
                                        {QStringLiteral("text"), cue.text}});
        }
        callAsync([this, generation, media, selectedId, status = result.status, cues = std::move(cues)] {
            if (generation != m_subtitleTimelineGeneration) return;
            {
                vlc_player_locker lock{m_player};
                const auto* selected = vlc_player_GetTrack(m_player, selectedId.get());
                if (vlc_player_GetCurrentMedia(m_player) != media.get()
                    || !selected || !selected->selected || selected->es_id == m_secondarySpuEsId.get()) {
                    resetSubtitleTimeline();
                    return;
                }
            }
            m_subtitleTimelineTimeout.stop();
            m_subtitleTimelineRequest.reset();
            m_subtitleTimelineCues = std::move(cues);
            m_subtitleTimelineState = status == SubtitleTimelineScan::Complete
                ? m_subtitleTimelineCues.isEmpty() ? QStringLiteral("empty") : QStringLiteral("ready")
                : QStringLiteral("unavailable");
            msg_Dbg(p_intf, "Subtitle timeline: %lld cues, status %d", static_cast<long long>(m_subtitleTimelineCues.size()), int(status));
            emit q_ptr->subtitleTimelineChanged();
        });
    });
}

bool PlayerController::getAudioWaveformEnabled() const { return d_ptr->m_audioWaveformEnabled; }
bool PlayerController::getAudioWaveformAutomatic() const { return d_ptr->m_audioWaveformAutomatic; }
QVariantList PlayerController::getAudioWaveformPeaks() const { return d_ptr->m_audioWaveformPeaks; }
QString PlayerController::getAudioWaveformState() const { return d_ptr->m_audioWaveformState; }
void PlayerController::setAudioWaveformEnabled(bool enabled)
{
    Q_D(PlayerController);
    if (d->m_audioWaveformEnabled == enabled) return;
    d->m_audioWaveformEnabled = enabled;
    d->resetAudioWaveform();
    emit audioWaveformEnabledChanged();
}
void PlayerControllerPrivate::refreshAudioWaveform()
{
    // Audio files get a wavebar independently of the selected live visualizer.
    // Use media tracks, since a visualizer can itself create a video output.
    QUrl url;
    vlc_tick_t duration = 0;
    SharedInputItem media;
    QByteArray trackId;
    bool audioOnly = false;
    {
        vlc_player_locker lock{m_player};
        const auto* selected = vlc_player_GetSelectedTrack(m_player, AUDIO_ES);
        if (isCurrentItemSynced() && m_currentItem
            && selected
            && vlc_player_GetLength(m_player) > 0) {
            media = m_currentItem;
            trackId = vlc_es_id_GetStrId(selected->es_id);
            audioOnly = vlc_player_GetTrackCount(m_player, VIDEO_ES) == 0;
            vlc_mutex_locker itemLock{&m_currentItem->lock};
            url = QUrl::fromEncoded(m_currentItem->psz_uri);
            duration = vlc_player_GetLength(m_player);
        }
    }
    const bool automatic = audioOnly && (canScanAudioWaveform(url, duration)
        || (duration > 0 && (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https"))));
    // MP3 duration estimates and track metadata can change during playback.
    // They do not change the audio source or invalidate its waveform.
    if (media == m_audioWaveformMedia && trackId == m_audioWaveformTrack
        && automatic == m_audioWaveformAutomatic) return;
    m_audioWaveformMedia = media;
    m_audioWaveformTrack = trackId;
    m_audioWaveformAutomatic = automatic;
    resetAudioWaveform();
}

void PlayerControllerPrivate::resetAudioWaveform()
{
    stopProgressiveWaveform();
    ++m_audioWaveformGeneration;
    m_audioWaveformRefresh.stop();m_audioWaveformTimeout.stop();
    if (m_audioWaveformRequest) vlc_interrupt_kill(m_audioWaveformRequest->interrupt);
    m_audioWaveformRequest.reset();m_audioWaveformWorkers.clear();
    m_audioWaveformPeaks.clear();
    const bool enabled = m_audioWaveformEnabled || m_audioWaveformAutomatic;
    m_audioWaveformState = enabled ? QStringLiteral("loading") : QStringLiteral("off");
    emit q_ptr->audioWaveformChanged();
    if (enabled) m_audioWaveformRefresh.start();
}
void PlayerControllerPrivate::readAudioWaveform()
{
    AudioWaveformSource source;
    SharedInputItem media;SharedEsId selectedId;
    {
        vlc_player_locker lock{m_player};
        const auto* selected = vlc_player_GetSelectedTrack(m_player, AUDIO_ES);
        if (!isCurrentItemSynced() || !m_currentItem || !selected) {
            m_audioWaveformState = QStringLiteral("unavailable");emit q_ptr->audioWaveformChanged();return;
        }
        media = m_currentItem;selectedId.reset(selected->es_id, true);
        const QByteArray id = vlc_es_id_GetStrId(selected->es_id);
        const qsizetype split = id.indexOf("/audio/");
        const QByteArray prefix = split < 0 ? QByteArray() : id.first(split + 1);
        if ((!prefix.isEmpty() && prefix.size() != 33) || (prefix.isEmpty() && !id.startsWith("audio/"))) {
            m_audioWaveformState = QStringLiteral("unavailable");emit q_ptr->audioWaveformChanged();return;
        }
        if (const char* uri = vlc_es_id_GetSourceUri(selected->es_id)) source.uri = uri;
        else { vlc_mutex_locker itemLock{&m_currentItem->lock};source.uri = m_currentItem->psz_uri; }
        for (size_t i = 0; i < vlc_player_GetTrackCount(m_player, AUDIO_ES); ++i) {
            const auto* track = vlc_player_GetTrackAt(m_player, AUDIO_ES, i);
            if (track == selected) break;
            const QByteArray other = vlc_es_id_GetStrId(track->es_id);
            if (prefix.isEmpty() ? other.startsWith("audio/") : other.startsWith(QByteArray(prefix + "audio/"))) ++source.audioIndex;
        }
        source.duration = vlc_player_GetLength(m_player);
    }
    const auto url = QUrl::fromEncoded(source.uri);
    // Observe an on-demand HTTP stream without opening another connection.
    if (source.duration > 0 && (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https"))) {
        startProgressiveWaveform(source, media.get(), selectedId.get());
        return;
    }
    // Include files on shares, but do not replay streams or capture devices.
    if (!canScanAudioWaveform(url, source.duration)) {
        m_audioWaveformState = QStringLiteral("unavailable");emit q_ptr->audioWaveformChanged();return;
    }
    auto request = std::make_shared<TimelineReadRequest>();
    if (!request->interrupt) { m_audioWaveformState = QStringLiteral("unavailable");emit q_ptr->audioWaveformChanged();return; }
    m_audioWaveformRequest = request;m_audioWaveformTimeout.start();
    source.deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(15);
    const auto generation = m_audioWaveformGeneration;
    const auto cachedKey = m_audioWaveformCacheKey;
    const auto cachedPeaks = m_audioWaveformCachePeaks;
    m_audioWaveformWorkers.start([this, request, source, generation, media, selectedId, cachedKey, cachedPeaks] {
        QVariantList peaks;
        QString key;
        auto* previous = vlc_interrupt_set(request->interrupt);
        // Use the same path as VLC for local files and UNC shares.
        const auto path = vlc::wrap_cptr(vlc_uri2path(source.uri.constData()));
        // Even a file metadata check can wait on a network share.
        const QFileInfo info(path ? QString::fromUtf8(path.get()) : QString());
        if (!vlc_killed() && info.isFile()) {
            key = QString::fromUtf8(source.uri) + QLatin1Char('|') + QString::number(source.audioIndex)
                + QLatin1Char('|') + QString::number(source.duration) + QLatin1Char('|') + QString::number(info.size())
                + QLatin1Char('|') + QString::number(info.lastModified().toMSecsSinceEpoch());
            if (!vlc_killed() && vlc_tick_now() < source.deadline) {
                if (key == cachedKey && !cachedPeaks.isEmpty()) peaks = cachedPeaks;
                else {
                    const auto result = scanAudioWaveform(VLC_OBJECT(p_intf), source, request->interrupt);
                    if (result.status == AudioWaveformScan::Complete) {
                        peaks.reserve(result.peaks.size());
                        for (float peak : result.peaks) peaks.append(peak);
                    }
                }
            }
        }
        vlc_interrupt_set(previous);
        callAsync([this, generation, media, selectedId, key, peaks = std::move(peaks)] {
            if (generation != m_audioWaveformGeneration) return;
            {
                vlc_player_locker lock{m_player};
                const auto* selected = vlc_player_GetTrack(m_player, selectedId.get());
                if (vlc_player_GetCurrentMedia(m_player) != media.get() || !selected || !selected->selected) { resetAudioWaveform();return; }
            }
            m_audioWaveformTimeout.stop();m_audioWaveformRequest.reset();
            m_audioWaveformPeaks = peaks;
            m_audioWaveformState = peaks.isEmpty() ? QStringLiteral("unavailable") : QStringLiteral("ready");
            if (!peaks.isEmpty()) { m_audioWaveformCacheKey = key;m_audioWaveformCachePeaks = peaks; }
            emit q_ptr->audioWaveformChanged();
        });
    });
}

void PlayerControllerPrivate::startProgressiveWaveform(const AudioWaveformSource& source, input_item_t* media, vlc_es_id_t* track)
{
    m_progressiveWaveform = std::make_unique<ProgressiveAudioWaveform>(source.duration, source.bins);
    const QByteArray key = source.uri + '|' + QByteArray::number(source.audioIndex) + '|' + QByteArray::number(source.duration);
    const QDir directory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/waveforms"));
    m_progressiveWaveformFile = directory.filePath(QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex()) + QStringLiteral(".json"));
    QFile cached(m_progressiveWaveformFile);
    const QFileInfo info(cached);
    if (info.size() <= 128 * 1024 && info.lastModified().secsTo(QDateTime::currentDateTime()) < 7 * 86400 && cached.open(QIODevice::ReadOnly)) {
        const auto object = QJsonDocument::fromJson(cached.read(128 * 1024)).object();
        if (object.value("version").toInt() == 1) {
            QVector<float> peaks;
            for (const auto value : object.value("peaks").toArray()) peaks.push_back(value.isDouble() ? value.toDouble() : std::numeric_limits<float>::quiet_NaN());
            m_progressiveWaveform->restore(peaks);
        }
    }
    static const union vlc_player_metadata_cbs callbacks = [] {
        union vlc_player_metadata_cbs callbacks{};
        callbacks.on_audio_samples = [](vlc_tick_t time, double rate, const audio_sample_format_t* format, const block_t* block, void* data) {
            static_cast<ProgressiveAudioWaveform*>(data)->append(time, rate, *format, *block);
        };
        return callbacks;
    }();
    {
        vlc_player_locker lock{m_player};
        const auto* selected = vlc_player_GetTrack(m_player, track);
        if (vlc_player_GetCurrentMedia(m_player) == media && selected && selected->selected)
            m_waveformListener = vlc_player_AddMetadataListener(m_player, VLC_PLAYER_METADATA_AUDIO_SAMPLES, &callbacks, m_progressiveWaveform.get());
    }
    m_audioWaveformState = m_waveformListener ? QStringLiteral("partial") : QStringLiteral("unavailable");
    readProgressiveWaveform();
    if (m_waveformListener) m_progressiveWaveformTimer.start();
}

void PlayerControllerPrivate::readProgressiveWaveform()
{
    if (!m_progressiveWaveform) return;
    QVariantList peaks;
    for (float value : m_progressiveWaveform->snapshot()) peaks.push_back(value);
    if (peaks != m_audioWaveformPeaks) {
        m_audioWaveformPeaks = std::move(peaks);
        emit q_ptr->audioWaveformChanged();
    }
    if (vlc_tick_now() - m_waveformLastSaved >= VLC_TICK_FROM_SEC(10)) saveProgressiveWaveform();
}

void PlayerControllerPrivate::saveProgressiveWaveform()
{
    if (!m_progressiveWaveform || m_progressiveWaveformFile.isEmpty()) return;
    m_waveformLastSaved = vlc_tick_now();
    const auto peaks = m_progressiveWaveform->snapshot();
    if (std::none_of(peaks.cbegin(), peaks.cend(), [](float peak) { return peak >= 0; })) return;
    QDir directory = QFileInfo(m_progressiveWaveformFile).dir();
    if (!directory.mkpath(QStringLiteral("."))) return;
    QJsonArray values;
    for (float peak : peaks) values.append(peak);
    QSaveFile output(m_progressiveWaveformFile);
    if (!output.open(QIODevice::WriteOnly)) return;
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"peaks", values}}).toJson(QJsonDocument::Compact);
    if (output.write(bytes) != bytes.size() || !output.commit()) return;
    // Keep at most 256 files and 16 MiB, removing the least recent files first.
    const auto files = directory.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Time);
    qint64 size = 0;
    for (qsizetype i = 0; i < files.size(); ++i) {
        size += files[i].size();
        if (i >= 256 || size > 16 * 1024 * 1024) QFile::remove(files[i].absoluteFilePath());
    }
}

void PlayerControllerPrivate::stopProgressiveWaveform()
{
    m_progressiveWaveformTimer.stop();
    if (m_waveformListener) {
        vlc_player_locker lock{m_player};
        vlc_player_RemoveMetadataListener(m_player, m_waveformListener);
        m_waveformListener = nullptr;
    }
    saveProgressiveWaveform();
    m_progressiveWaveform.reset();
    m_progressiveWaveformFile.clear();
}

// PLAYBACK

QUrl PlayerController::getSeekPreview() const
{
    Q_D(const PlayerController);
    return d->m_seekPreview;
}

double PlayerController::getSeekPreviewPosition() const
{
    Q_D(const PlayerController);
    return d->m_seekPreviewPosition;
}

bool PlayerController::getSeekPreviewEnabled() const
{
    Q_D(const PlayerController);
    return d->m_seekPreviewEnabled;
}

void PlayerController::setSeekPreviewEnabled(bool enabled)
{
    Q_D(PlayerController);
    if (d->m_seekPreviewEnabled == enabled) return;
    d->m_seekPreviewEnabled = enabled;
    if (enabled) d->updateSeekSampling();
    else d->resetSeekPreview();
    emit seekPreviewEnabledChanged();
}

void PlayerControllerPrivate::cancelSeekRequest()
{
    ++m_seekGeneration;
    if (m_seekRequest) {
        vlc_preparser_Cancel(m_seekPreparser, m_seekRequest);
        vlc_preparser_req_Release(m_seekRequest);
        m_seekRequest = nullptr;
    }
    m_seekRequestBackground = false;
}

void PlayerControllerPrivate::resetSeekPreview()
{
    Q_Q(PlayerController);
    m_seekSampleTimer.stop();
    m_seekPreviewActive = false;
    m_pendingSeekPosition = -1;
    cancelSeekRequest();
    m_seekPreviewCache.clear();
    m_seekSamplePoints.clear();
    m_seekSampleLength = 0;
    m_seekSampleFailures = 0;
    m_seekPreviewPosition = -1;
    if (!m_seekPreview.isEmpty()) {
        m_seekPreview = QUrl();
        emit q->seekPreviewChanged();
    }
}

void PlayerControllerPrivate::updateSeekSampling()
{
    Q_Q(PlayerController);
    const bool started = m_playing_status == PlayerController::PLAYING_STATE_STARTED
        || m_playing_status == PlayerController::PLAYING_STATE_PLAYING
        || m_playing_status == PlayerController::PLAYING_STATE_PAUSED;
    if (!m_seekPreviewEnabled || !started) {
        resetSeekPreview();
        return;
    }
    if (!m_hasVideo || !q->isSeekable() || !m_length.valid() || m_length.toVLCTick() <= 0
        || m_buffering < 1) {
        m_seekSampleTimer.stop();
        if (m_seekRequestBackground) cancelSeekRequest();
        return;
    }
    const vlc_tick_t length = m_length.toVLCTick();
    const bool first = m_seekSampleLength != length;
    if (first) {
        m_seekSamplePoints = SeekPreviewCache::samplesFor(length);
        m_seekSampleLength = length;
        m_seekSampleFailures = 0;
    }
    if (!m_seekPreviewActive && !m_seekRequest && !m_seekSamplePoints.empty()
        && m_seekSampleFailures < 3 && !m_seekSampleTimer.isActive())
        m_seekSampleTimer.start(first ? 1500 : 500);
}

void PlayerControllerPrivate::sampleSeekPreview()
{
    if (m_seekPreviewActive || m_seekRequest || m_seekSamplePoints.empty()) return;
    if (!startSeekPreview(m_seekSamplePoints.front(), true)) {
        m_seekSamplePoints.removeFirst();
        ++m_seekSampleFailures;
        updateSeekSampling();
    }
}

void PlayerController::cancelSeekPreview()
{
    Q_D(PlayerController);
    d->m_seekPreviewActive = false;
    d->m_pendingSeekPosition = -1;
    if (!d->m_seekRequestBackground) d->cancelSeekRequest();
    d->m_seekPreviewPosition = -1;
    if (!d->m_seekPreview.isEmpty()) {
        d->m_seekPreview = QUrl();
        emit seekPreviewChanged();
    }
    d->updateSeekSampling();
}

void PlayerController::requestSeekPreview(double position)
{
    Q_D(PlayerController);
    if (!d->m_seekPreviewEnabled || !std::isfinite(position) || position < 0 || position > 1
        || !d->m_length.valid() || d->m_length.toVLCTick() <= 0 || !isSeekable()) {
        cancelSeekPreview();
        return;
    }
    d->m_seekPreviewActive = true;
    d->m_seekSampleTimer.stop();
    const vlc_tick_t length = d->m_length.toVLCTick();
    const vlc_tick_t time = std::min(length - 1, vlc_tick_t(position * length));
    const vlc_tick_t bucket = time / SeekPreviewCache::Bucket * SeekPreviewCache::Bucket;
    const auto exact = d->m_seekPreviewCache.exact(bucket);
    const auto cached = exact ? exact : d->m_seekPreviewCache.nearest(bucket);
    if (cached) {
        d->m_seekPreview = cached->image;
        d->m_seekPreviewPosition = double(cached->time) / length;
        emit seekPreviewChanged();
    }
    if (exact) {
        // An older in-flight frame must not replace this cached pointer target.
        d->cancelSeekRequest();
        d->m_pendingSeekPosition = -1;
        return;
    }
    // Coarse work yields to the pointer. Finish an active precise decode so
    // continuous movement cannot cancel every frame before it is available.
    if (d->m_seekRequestBackground) d->cancelSeekRequest();
    if (d->m_seekRequest) {
        d->m_pendingSeekPosition = position;
        return;
    }
    d->startSeekPreview(bucket, false);
}

bool PlayerControllerPrivate::startSeekPreview(vlc_tick_t bucket, bool background)
{
    auto* d = this;
    if (!m_seekPreviewEnabled || m_seekRequest || !m_length.valid()) return false;
    const vlc_tick_t length = m_length.toVLCTick();
    if (length <= 0) return false;
    SharedInputItem media;
    {
        vlc_player_locker lock{m_player};
        if (!isCurrentItemSynced()) return false;
        media = m_currentItem;
    }
    if (!media) return false;
    ++m_seekGeneration;
    if (!d->m_seekPreparser) {
        vlc_preparser_cfg cfg{};
        cfg.types = VLC_PREPARSER_TYPE_THUMBNAIL;
        cfg.max_thumbnailer_threads = 1;
        cfg.timeout = VLC_TICK_FROM_SEC(15);
        cfg.external_process = false;
        d->m_seekPreparser = vlc_preparser_New(VLC_OBJECT(d->p_intf), &cfg);
        if (!d->m_seekPreparser) return false;
    }

    struct Request {
        PlayerControllerPrivate* owner;
        quint64 generation;
        vlc_tick_t time;
        vlc_tick_t length;
        bool background;
    };
    static const vlc_thumbnailer_cbs callbacks = {
        [](vlc_preparser_req*, int status, picture_t* picture, void* data) {
            std::unique_ptr<Request> request(static_cast<Request*>(data));
            auto* owner = request->owner;
            QUrl imageUrl;
            vlc_tick_t decodedTime = request->time;
            if (status == VLC_SUCCESS && picture) {
                msg_Dbg(owner->p_intf, "%s: requested %" PRId64 ", decoded %" PRId64,
                        request->background ? "Seek preview sample" : "Seek preview",
                        request->time, picture->date);
                // Some demuxers return a nearby frame even for precise seeks.
                // Label the decoded frame, rather than the requested position.
                if (picture->date >= VLC_TICK_0)
                    decodedTime = std::clamp(picture->date - VLC_TICK_0, vlc_tick_t(0), request->length);
                block_t* pixels = nullptr;
                video_format_t format{};
                const auto& source = picture->format;
                const double aspect = double(source.i_visible_width) / std::max(1u, source.i_visible_height)
                    * (source.i_sar_num && source.i_sar_den ? double(source.i_sar_num) / source.i_sar_den : 1.0);
                const bool portrait = aspect < 1;
                if (picture_Export(VLC_OBJECT(owner->p_intf), &pixels, &format, picture,
                                   VLC_CODEC_RGBA, portrait ? 0 : 252, portrait ? 252 : 0, false) == VLC_SUCCESS) {
                    if (format.i_width > 0 && format.i_width <= 252
                        && format.i_height > 0 && format.i_height <= 252
                        && pixels->i_buffer >= size_t(format.i_width) * format.i_height * 4) {
                        QImage image(pixels->p_buffer, format.i_width, format.i_height,
                                     format.i_width * 4, QImage::Format_RGBA8888);
                        QByteArray png;
                        QBuffer buffer(&png);
                        buffer.open(QIODevice::WriteOnly);
                        if (image.save(&buffer, "PNG"))
                            imageUrl = SeekPreviewImageProvider::url(png);
                    }
                    block_Release(pixels);
                }
            }
            if (imageUrl.isEmpty())
                msg_Dbg(owner->p_intf, "Seek preview unavailable: status %d at %" PRId64,
                        status, request->time);
            owner->callAsync([owner, generation = request->generation, time = request->time, decodedTime,
                              length = request->length, background = request->background, imageUrl] {
                if (owner->m_seekGeneration != generation) return;
                vlc_preparser_req_Release(owner->m_seekRequest);
                owner->m_seekRequest = nullptr;
                owner->m_seekRequestBackground = false;
                const bool saved = owner->m_seekPreviewCache.insert(time, imageUrl, decodedTime, background);
                if (background) {
                    owner->m_seekSamplePoints.removeOne(time);
                    owner->m_seekSampleFailures = saved ? 0 : owner->m_seekSampleFailures + 1;
                } else {
                    if (saved) owner->m_seekSampleFailures = 0;
                    // The pointer moved on while this frame was decoded: show
                    // the cached frame nearest to where it is now, which may be
                    // this one, and decode the frame for that time.
                    const double pending = std::exchange(owner->m_pendingSeekPosition, -1);
                    if (pending >= 0) owner->q_func()->requestSeekPreview(pending);
                    else if (saved) {
                        owner->m_seekPreview = imageUrl;
                        owner->m_seekPreviewPosition = double(decodedTime) / length;
                        emit owner->q_func()->seekPreviewChanged();
                    }
                }
                owner->updateSeekSampling();
            });
        }
    };
    vlc_thumbnailer_arg args{};
    args.seek.type = decltype(args.seek)::VLC_THUMBNAILER_SEEK_TIME;
    args.seek.time = bucket;
    args.seek.speed = background ? decltype(args.seek)::VLC_THUMBNAILER_SEEK_FAST
                                 : decltype(args.seek)::VLC_THUMBNAILER_SEEK_PRECISE;
    auto request = std::make_unique<Request>(Request{d, d->m_seekGeneration, bucket, length, background});
    d->m_seekRequest = vlc_preparser_req_NewThumbnail(d->m_seekPreparser, media.get(), &args, &callbacks, request.get());
    if (!d->m_seekRequest) return false;
    d->m_seekRequestBackground = background;
    msg_Dbg(p_intf, "Seek preview request: %s at %" PRId64, background ? "sample" : "precise", bucket);
    // Callbacks may run before Submit returns, so transfer their context first.
    Request* context = request.release();
    if (vlc_preparser_Submit(d->m_seekPreparser, d->m_seekRequest) != VLC_SUCCESS) {
        delete context;
        vlc_preparser_req_Release(d->m_seekRequest);
        d->m_seekRequest = nullptr;
        d->m_seekRequestBackground = false;
        return false;
    }
    return true;
}

vlc_player_t * PlayerController::getPlayer() const
{
    Q_D(const PlayerController);

    return d->m_player;
}

input_item_t *PlayerController::getInput() const
{
    Q_D(const PlayerController);
    vlc_player_locker locker{ d->m_player };
    return vlc_player_GetCurrentMedia( d->m_player );
}

bool PlayerController::isStarted() const
{
    Q_D(const PlayerController);
    vlc_player_locker locker{ d->m_player };
    return vlc_player_IsStarted( d->m_player );
}

bool PlayerController::hasInput() const
{
    return getInput();
}

void PlayerController::reverse()
{
    Q_D(PlayerController);
    msg_Dbg( d->p_intf, "reverse");
    vlc_player_locker lock{ d->m_player };
    if ( vlc_player_CanChangeRate( d->m_player ) )
    {
        float f_rate_ = vlc_player_GetRate( d->m_player );
        vlc_player_ChangeRate( d->m_player, -f_rate_ );
    }
}

void PlayerController::setRate( float new_rate )
{
    Q_D(PlayerController);

    if (qFuzzyCompare(d->m_rate, new_rate))
        return;

    msg_Dbg( d->p_intf, "setRate %f", new_rate);
    vlc_player_locker lock{ d->m_player };
    if ( vlc_player_CanChangeRate( d->m_player ) )
    {
        vlc_player_ChangeRate( d->m_player, new_rate );

        // Until the timer notifies the change, we need to still change
        // the rate, because player timer update may take a long time.
        // For position, it is supposed to be interpolated but for rate
        // the new value can be taken as is:
        d->m_rate = new_rate;
        emit rateChanged(d->m_rate);
    }
}

void PlayerController::slower()
{
    Q_D(PlayerController);
    msg_Dbg( d->p_intf, "slower");
    vlc_player_locker lock{ d->m_player };
    if ( vlc_player_CanChangeRate( d->m_player ) )
        vlc_player_DecrementRate( d->m_player );
}

void PlayerController::faster()
{
    Q_D(PlayerController);
    msg_Dbg( d->p_intf, "faster");
    vlc_player_locker lock{ d->m_player };
    if ( vlc_player_CanChangeRate( d->m_player ) )
        vlc_player_IncrementRate( d->m_player );
}

void PlayerController::littlefaster()
{
    Q_D(PlayerController);
    msg_Dbg( d->p_intf, "littlefaster");
    var_SetInteger( vlc_object_instance(d->p_intf), "key-action", ACTIONID_RATE_FASTER_FINE );
}

void PlayerController::littleslower()
{
    Q_D(PlayerController);
    msg_Dbg( d->p_intf, "littleslower");
    var_SetInteger( vlc_object_instance(d->p_intf), "key-action", ACTIONID_RATE_SLOWER_FINE );
}

void PlayerController::normalRate()
{
    Q_D(PlayerController);
    msg_Dbg( d->p_intf, "normalRate");
    vlc_player_locker lock{ d->m_player };
    if ( vlc_player_CanChangeRate( d->m_player ) )
        vlc_player_ChangeRate( d->m_player, 1.0f );
}


void PlayerController::setTime(VLCTime new_time)
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    if( !d->isCurrentItemSynced() )
        return;
    vlc_player_SetTime( d->m_player, new_time.toVLCTick() );
}

void PlayerController::setPosition(double position)
{
    Q_D(PlayerController);
    if (!std::isfinite(position))
        return;
    position = std::clamp(position, 0.0, 1.0);
    if (qFuzzyCompare(d->m_position, position))
        return;

    vlc_player_locker lock{ d->m_player };
    if( !d->isCurrentItemSynced() )
        return;
    vlc_player_SetPosition( d->m_player, position );
}

void PlayerController::jumpFwd()
{
    Q_D(PlayerController);
    msg_Dbg( d->p_intf, "jumpFwd");
    int i_interval = var_InheritInteger( d->p_intf, "short-jump-size" );
    vlc_player_locker lock{ d->m_player };
    if( !d->isCurrentItemSynced() )
        return;
    vlc_player_JumpTime( d->m_player, vlc_tick_from_sec( i_interval ) );
}

void PlayerController::jumpBwd()
{
    Q_D(PlayerController);
    msg_Dbg( d->p_intf, "jumpBwd");
    int i_interval = var_InheritInteger( d->p_intf, "short-jump-size" );
    vlc_player_locker lock{ d->m_player };
    if( !d->isCurrentItemSynced() )
        return;
    vlc_player_JumpTime( d->m_player, vlc_tick_from_sec( -i_interval ) );
}

void PlayerController::replay()
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    if (!d->isCurrentItemSynced() || !vlc_player_IsStarted(d->m_player)
        || !vlc_player_CanSeek(d->m_player))
        return;
    vlc_player_SetPosition(d->m_player, 0);
    vlc_player_Resume(d->m_player);
}

void PlayerController::jumpSeconds(int seconds)
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    if (!d->isCurrentItemSynced())
        return;
    vlc_player_JumpTime(d->m_player, vlc_tick_from_sec(seconds));
}

void PlayerController::jumpToSeconds(int seconds)
{
    Q_D(PlayerController);
    if (seconds < 0)
        return;
    vlc_player_locker lock{d->m_player};
    if (!d->isCurrentItemSynced() || !vlc_player_IsStarted(d->m_player)
        || !vlc_player_CanSeek(d->m_player))
        return;
    const vlc_tick_t time = vlc_tick_from_sec(seconds);
    const vlc_tick_t length = vlc_player_GetLength(d->m_player);
    if (length > 0 && time > length)
        return;
    vlc_player_SetTime(d->m_player, time);
}

void PlayerController::jumpTime(VLCTime i_time)
{
    Q_D(PlayerController);
    msg_Dbg( d->p_intf, "jumpTime");
    vlc_player_locker lock{ d->m_player };
    if( !d->isCurrentItemSynced() )
        return;
    vlc_player_JumpTime( d->m_player, i_time.toVLCTick() );
}

void PlayerController::jumpToPos( double new_pos )
{
    Q_D(PlayerController);
    if (!std::isfinite(new_pos))
        return;
    new_pos = std::clamp(new_pos, 0.0, 1.0);
    {
        vlc_player_locker lock{ d->m_player };
        if( !d->isCurrentItemSynced() )
            return;
        if( vlc_player_IsStarted( d->m_player ) )
        {
            const auto length = vlc_player_GetLength(d->m_player);
            if (d->m_seekDragItem && length > 0)
            {
                /* Keep a decodable frame before EOF while the pointer is
                 * held. The full position is committed on release. */
                vlc_tick_t frame = VLC_TICK_FROM_MS(40);
                for (size_t i = 0; i < vlc_player_GetTrackCount(d->m_player, VIDEO_ES); ++i)
                {
                    const auto* track = vlc_player_GetTrackAt(d->m_player, VIDEO_ES, i);
                    const auto& format = track->fmt.video;
                    if (track->selected && format.i_frame_rate && format.i_frame_rate_base)
                    {
                        frame = std::max(vlc_tick_t(1),
                            vlc_tick_from_samples(format.i_frame_rate_base, format.i_frame_rate));
                        break;
                    }
                }
                const auto lastFrame = std::max(vlc_tick_t(0), length - frame);
                vlc_player_SetTime(d->m_player,
                    std::min(vlc_tick_t(length * new_pos), lastFrame));
            }
            else
                vlc_player_SetPosition( d->m_player, new_pos );
        }
    }
    emit seekRequested( new_pos );
}

void PlayerController::beginSeekDrag()
{
    Q_D(PlayerController);
    vlc_player_locker lock{d->m_player};
    if (d->m_seekDragItem || !d->isCurrentItemSynced()
        || !vlc_player_IsStarted(d->m_player)
        || !vlc_player_CanSeek(d->m_player) || !vlc_player_CanPause(d->m_player))
        return;
    d->m_seekDragItem = d->m_currentItem;
    d->m_seekDragWasPlaying = !vlc_player_IsPaused(d->m_player);
    if (d->m_seekDragWasPlaying)
        vlc_player_Pause(d->m_player);
}

void PlayerController::finishSeekDrag(double position, bool commit)
{
    Q_D(PlayerController);
    vlc_player_locker lock{d->m_player};
    if (!d->m_seekDragItem)
        return;
    const bool sameItem = d->m_seekDragItem.get() == vlc_player_GetCurrentMedia(d->m_player);
    d->m_seekDragItem.reset();
    if (!sameItem || !d->isCurrentItemSynced() || !vlc_player_IsStarted(d->m_player))
        return;
    if (commit && std::isfinite(position) && vlc_player_CanSeek(d->m_player))
        vlc_player_SetPosition(d->m_player, std::clamp(position, 0.0, 1.0));
    if (d->m_seekDragWasPlaying)
        vlc_player_Resume(d->m_player);
}

void PlayerController::frameNext()
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    if( !d->isCurrentItemSynced() )
        return;
    vlc_player_NextVideoFrame( d->m_player );
}

void PlayerController::framePrev()
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    if( !d->isCurrentItemSynced() )
        return;
    vlc_player_PreviousVideoFrame( d->m_player );
}

//TRACKS

void PlayerController::setAudioDelay(VLCDuration delay)
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    if( !d->isCurrentItemSynced() )
        return;
    vlc_player_SetAudioDelay( d->m_player, delay.toVLCTick(), VLC_PLAYER_WHENCE_ABSOLUTE );
}

/*Q_INVOKABLE*/void PlayerController::addAudioDelay(VLCDuration delay)
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    vlc_player_SetAudioDelay( d->m_player, delay.toVLCTick(), VLC_PLAYER_WHENCE_RELATIVE );
    emit audioDelayChanged(delay);
}

void PlayerController::setSubtitleDelay(VLCDuration delay)
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    if(!d->isCurrentItemSynced() )
        return;
    vlc_player_SetSubtitleDelay( d->m_player, delay.toVLCTick(), VLC_PLAYER_WHENCE_ABSOLUTE );
}

/*Q_INVOKABLE*/void PlayerController::addSubtitleDelay(VLCDuration delay)
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    vlc_player_SetSubtitleDelay( d->m_player, delay.toVLCTick(), VLC_PLAYER_WHENCE_RELATIVE);
    emit subtitleDelayChanged(delay);
}

void PlayerController::setSecondarySubtitleDelay(VLCDuration delay)
{
    Q_D(PlayerController);
    {
        vlc_player_locker lock{d->m_player};
        if (!d->isCurrentItemSynced()) return;
        // A selected subtitle can acquire its secondary renderer after a
        // paused edit. UpdateSpuOrder applies this requested delay when ready.
        d->m_secondarySubtitleDelay = delay;
        if (d->m_secondarySpuEsId)
            vlc_player_SetEsIdDelay(d->m_player, d->m_secondarySpuEsId.get(),
                                   delay.toVLCTick(), VLC_PLAYER_WHENCE_ABSOLUTE);
    }
    emit secondarySubtitleDelayChanged(delay);
}

/*Q_INVOKABLE*/void PlayerController::addSecondarySubtitleDelay(VLCDuration delay)
{
    Q_D(PlayerController);
    VLCDuration requested;
    {
        vlc_player_locker lock{d->m_player};
        if (!d->isCurrentItemSynced()) return;
        const auto actual = d->m_secondarySpuEsId
            ? vlc_player_GetEsIdDelay(d->m_player, d->m_secondarySpuEsId.get()) : VLC_TICK_INVALID;
        const auto current = actual != VLC_TICK_INVALID ? VLCDuration(actual)
            : d->m_secondarySubtitleDelay.valid() ? d->m_secondarySubtitleDelay : VLCDuration::fromMS(0);
        requested = current + delay;
        d->m_secondarySubtitleDelay = requested;
        if (d->m_secondarySpuEsId)
            vlc_player_SetEsIdDelay(d->m_player, d->m_secondarySpuEsId.get(),
                                   delay.toVLCTick(), VLC_PLAYER_WHENCE_RELATIVE);
    }
    emit secondarySubtitleDelayChanged(requested);
}

int PlayerController::getAudioDelayMS() const
{
    return getAudioDelay().toMilliseconds();
}

void PlayerController::setAudioDelayMS(int ms)
{
    setAudioDelay( VLCDuration::fromMS(ms) );
}

int PlayerController::getSubtitleDelayMS() const
{
    return getSubtitleDelay().toMilliseconds();
}

void PlayerController::setSubtitleDelayMS(int ms)
{
    setSubtitleDelay( VLCDuration::fromMS(ms) );
}

int PlayerController::getSecondarySubtitleDelayMS() const
{
    return getSecondarySubtitleDelay().toMilliseconds();
}

void PlayerController::setSecondarySubtitleDelayMS(int ms)
{
    setSecondarySubtitleDelay( VLCDuration::fromMS(ms) );
}

void PlayerController::setSubtitleFPS(float fps)
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    if( !d->isCurrentItemSynced() )
        return;
    vlc_player_SetAssociatedSubsFPS( d->m_player, fps );
}

QVariantMap PlayerController::getPlaybackStatistics() const
{
    Q_D(const PlayerController);
    const auto& stats = d->m_stats;
    const auto counter = [](uint64_t value) { return QVariant::fromValue<qulonglong>(value); };
    return {{QStringLiteral("readBytes"), counter(stats.i_read_bytes)},
        {QStringLiteral("inputBitrate"), stats.f_input_bitrate * 8000.},
        {QStringLiteral("streamBitrate"), stats.f_demux_bitrate * 8000.},
        {QStringLiteral("decodedVideo"), counter(stats.i_decoded_video)},
        {QStringLiteral("displayedPictures"), counter(stats.i_displayed_pictures)},
        {QStringLiteral("latePictures"), counter(stats.i_late_pictures)},
        {QStringLiteral("lostPictures"), counter(stats.i_lost_pictures)},
        {QStringLiteral("decodedAudio"), counter(stats.i_decoded_audio)},
        {QStringLiteral("playedAudioBuffers"), counter(stats.i_played_abuffers)},
        {QStringLiteral("lostAudioBuffers"), counter(stats.i_lost_abuffers)},
        {QStringLiteral("corruptedPackets"), counter(stats.i_demux_corrupted)}};
}

//TITLE/CHAPTER/MENU

void PlayerController::sectionPrev()
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    if( !d->isCurrentItemSynced() )
        return;
    if( vlc_player_IsStarted( d->m_player ) )
    {
        if (vlc_player_GetSelectedChapter( d->m_player ) != NULL)
            vlc_player_SelectPrevChapter( d->m_player );
        else
            vlc_player_SelectPrevTitle( d->m_player );
    }
}

void PlayerController::sectionNext()
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    if( !d->isCurrentItemSynced() )
        return;
    if( vlc_player_IsStarted( d->m_player ) )
    {
        if (vlc_player_GetSelectedChapter( d->m_player ) != NULL)
            vlc_player_SelectNextChapter( d->m_player );
        else
            vlc_player_SelectNextTitle( d->m_player );
    }
}

void PlayerController::sectionMenu()
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    if( !d->isCurrentItemSynced() )
        return;
    if( vlc_player_IsStarted( d->m_player ) )
        vlc_player_Navigate( d->m_player, VLC_PLAYER_NAV_MENU );
}

void PlayerController::navigateUp()
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    if( !d->isCurrentItemSynced() )
        return;
    if( vlc_player_IsStarted( d->m_player ) )
        vlc_player_Navigate( d->m_player, VLC_PLAYER_NAV_UP);
}

void PlayerController::navigateDown()
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    if( !d->isCurrentItemSynced() )
        return;
    if( vlc_player_IsStarted( d->m_player ) )
        vlc_player_Navigate( d->m_player, VLC_PLAYER_NAV_DOWN);
}

void PlayerController::navigateLeft()
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    if( !d->isCurrentItemSynced() )
        return;
    if( vlc_player_IsStarted( d->m_player ) )
        vlc_player_Navigate( d->m_player, VLC_PLAYER_NAV_LEFT);
}

void PlayerController::navigateRight()
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    if( !d->isCurrentItemSynced() )
        return;
    if( vlc_player_IsStarted( d->m_player ) )
        vlc_player_Navigate( d->m_player, VLC_PLAYER_NAV_RIGHT);
}

void PlayerController::navigateActivate()
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    if( !d->isCurrentItemSynced() )
        return;
    if( vlc_player_IsStarted( d->m_player ) )
        vlc_player_Navigate( d->m_player, VLC_PLAYER_NAV_ACTIVATE);
}

void PlayerController::chapterNext()
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    if( !d->isCurrentItemSynced() )
        return;
    if (vlc_player_IsStarted(d->m_player ))
        vlc_player_SelectNextChapter( d->m_player );
}

void PlayerController::chapterPrev()
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    if( !d->isCurrentItemSynced() )
        return;
    if (vlc_player_IsStarted(d->m_player ))
        vlc_player_SelectPrevChapter( d->m_player );
}

void PlayerController::titleNext()
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    if( !d->isCurrentItemSynced() )
        return;
    if (vlc_player_IsStarted(d->m_player ))
        vlc_player_SelectNextTitle( d->m_player );
}

void PlayerController::titlePrev()
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    if( !d->isCurrentItemSynced() )
        return;
    if (vlc_player_IsStarted(d->m_player ))
        vlc_player_SelectPrevTitle( d->m_player );
}

//PROGRAMS

void PlayerController::changeProgram( int program )
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    if( !d->isCurrentItemSynced() )
        return;
    if( vlc_player_IsStarted( d->m_player ) )
        vlc_player_SelectProgram( d->m_player, program );
}

//TELETEXT


void PlayerController::enableTeletext( bool enable )
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    if( !d->isCurrentItemSynced() )
        return;
    if (vlc_player_IsStarted(d->m_player ))
        vlc_player_SetTeletextEnabled( d->m_player, enable );
}

void PlayerController::setTeletextPage(int page)
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    if( !d->isCurrentItemSynced() )
        return;
    if (vlc_player_IsTeletextEnabled( d->m_player ))
        vlc_player_SelectTeletextPage( d->m_player, page );
}

void PlayerController::setTeletextTransparency( bool transparent )
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    if( !d->isCurrentItemSynced() )
        return;
    if (vlc_player_IsTeletextEnabled( d->m_player ))
        vlc_player_SetTeletextTransparency( d->m_player, transparent );
}

//VOUT PROPERTIES

PlayerController::VOutThreadList PlayerController::getVouts() const
{
    Q_D(const PlayerController);
    vout_thread_t **pp_vout;
    VOutThreadList VoutList;
    size_t i_vout;
    {
        vlc_player_locker lock{ d->m_player };
        if( !vlc_player_IsStarted( d->m_player ) )
            return VOutThreadList{};
        i_vout = 0;
        pp_vout = vlc_player_vout_HoldAll( d->m_player, &i_vout );
        if ( i_vout <= 0 )
            return VOutThreadList{};
    }
    VoutList.reserve( i_vout );
    for( size_t i = 0; i < i_vout; i++ )
    {
        assert( pp_vout[i] );
        //pass ownership
        VoutList.append(SharedVOutThread(pp_vout[i], false));
    }
    free( pp_vout );

    return VoutList;
}

SharedVOutThread PlayerController::getVout()
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    vout_thread_t* vout = vlc_player_vout_Hold( d->m_player );
    if( vout == NULL )
        return SharedVOutThread{};
    return SharedVOutThread{vout, false};
}

void PlayerController::setFullscreen( bool new_val )
{
    Q_D(PlayerController);
    msg_Dbg(d->p_intf, "setFullscreen %s", new_val? "fullscreen" : "windowed");
    vlc_player_locker lock{ d->m_player };
    vlc_player_vout_SetFullscreen( d->m_player, new_val );
}

void PlayerController::toggleFullscreen()
{
    Q_D(PlayerController);
    setFullscreen( ! d->m_fullscreen );
}

void PlayerController::setWallpaperMode( bool new_val )
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    vlc_player_vout_SetWallpaperModeEnabled( d->m_player, new_val );
}

bool PlayerController::getAutoscale( ) const
{
    Q_D(const PlayerController);
    return d->m_autoscale.getValue();
}

void PlayerController::setAutoscale( bool new_val )
{
    Q_D(PlayerController);
    d->m_autoscale.setValue( new_val );
}

//AOUT PROPERTIES

SharedAOut PlayerController::getAout()
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    return SharedAOut( vlc_player_aout_Hold( d->m_player ), false );
}

void PlayerController::setVolume(float volume)
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    vlc_player_aout_SetVolume( d->m_player, volume );
}

void PlayerController::setVolumeUp(int steps)
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    vlc_player_aout_IncrementVolume( d->m_player, steps, NULL );
}

void PlayerController::setVolumeDown(int steps)
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    vlc_player_aout_DecrementVolume( d->m_player, steps, NULL );
}

void PlayerController::setMuted(bool muted)
{
    Q_D(PlayerController);
    if( d->m_muted == muted )
        return;
    vlc_player_locker lock{ d->m_player };
    vlc_player_aout_Mute( d->m_player, muted );
}

void PlayerController::toggleMuted()
{
    Q_D(PlayerController);
    setMuted( !d->m_muted );
}

bool PlayerController::hasAudioVisualization() const
{
    Q_D(const PlayerController);
    return d->m_audioVisualization.hasCurrent();
}

void PlayerController::updatePosition()
{
    Q_D(PlayerController);

    // Update position properties
    emit positionChanged(d->m_position);
    emit positionUpdated(d->m_position, d->m_time, d->m_length);
}

void PlayerController::updatePositionFromTimer()
{
    Q_D(PlayerController);

    if (d->m_playing_status == PLAYING_STATE_PAUSED
        || d->m_playing_status == PLAYING_STATE_STOPPED
        || d->m_playing_status == PLAYING_STATE_STOPPING)
    {
        d->m_position_timer.stop();
        return;
    }
    vlc_tick_t system_now = vlc_tick_now();
    if (d->interpolateTime(system_now) == VLC_SUCCESS)
        updatePosition();
}

void PlayerController::updateTime(vlc_tick_t system_now, bool forceUpdate)
{
    Q_D(PlayerController);

    // Update time properties
    emit timeChanged(d->m_time);
    if (d->m_time.valid() && d->m_length.valid() && d->m_time <= VLCTime(d->m_length))
        d->m_remainingTime = VLCTime(d->m_length) - d->m_time;
    else
        d->m_remainingTime = VLCDuration();
    emit remainingTimeChanged(d->m_remainingTime);

    if (d->m_syltRaw.has_value() && d->m_time.valid())
    {
        const auto& lyrics = d->syltLyrics();
        int currentIndex = -1;
        const vlc_tick_t currentTime = d->m_time.toVLCTick();

        for (int i = 0; i < lyrics.size(); ++i)
        {
            if ((lyrics[i].time().toVLCTick() - 1) > currentTime)
                break;
            currentIndex = i;
        }

        if (currentIndex != d->m_currentLyricIndex)
        {
            d->m_currentLyricIndex = currentIndex;
            emit currentLyricIndexChanged( currentIndex );
            emit currentLyricChanged();
        }
    }

    if (system_now != VLC_TICK_INVALID
     && d->m_player_time.system_date != VLC_TICK_MAX
     && (forceUpdate || !d->m_time_timer.isActive()))
    {
        // Tell the timer to wait until the next second is reached.
        vlc_tick_t next_update_date =
            vlc_player_timer_point_GetNextIntervalDate(&d->m_player_time, system_now,
                                                       d->m_time.toVLCTick(), VLC_TICK_FROM_SEC(1));

        vlc_tick_t next_update_interval = next_update_date - system_now;

        if (next_update_interval > 0)
        {
            // The timer can be triggered a little before. In that case, it's
            // likely that we didn't reach the next next second. It's better to
            // add a very small delay in order to be triggered after the next
            // seconds.
            static const unsigned imprecision_delay_ms = 30;

            d->m_time_timer.start(MS_FROM_VLC_TICK(next_update_interval)
                                  + imprecision_delay_ms);
        }
    }
}

void PlayerController::openVLsub()
{
    Q_D(PlayerController);

    const auto extensionManager = ExtensionsManager::getInstance( d->p_intf );
    if ( !extensionManager->isLoaded() )
        extensionManager->loadExtensions();

    extensionManager->openVLsub();
}

void PlayerController::updateTimeFromTimer()
{
    Q_D(PlayerController);

    vlc_tick_t system_now = vlc_tick_now();
    if (d->interpolateTime(system_now) == VLC_SUCCESS)
        updateTime(system_now, false);
}

void PlayerController::restorePlaybackPos()
{
    Q_D(PlayerController);
    {
        vlc_player_locker lock{ d->m_player };
        if (!d->m_canRestorePlayback || !d->isCurrentItemSynced())
            return;
        vlc_player_RestorePlaybackPos( d->m_player );
    }
    acknowledgeRestoreCallback();
}

void PlayerController::acknowledgeRestoreCallback()
{
    Q_D(PlayerController);
    if (d->m_canRestorePlayback) {
        d->m_canRestorePlayback = false;
        d->m_playbackRestorePosition = -1.;
        emit playbackRestoreQueried();
    }
}

//MISC

void PlayerController::setABloopState(ABLoopState state)
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    vlc_player_SetAtoBLoop( d->m_player, static_cast<vlc_player_abloop>(state));
}

void PlayerController::toggleABloopState()
{
    Q_D(PlayerController);
    switch (d->m_ABLoopState) {
    case ABLOOP_STATE_NONE:
        setABloopState(ABLOOP_STATE_A);
        break;
    case ABLOOP_STATE_A:
        setABloopState(ABLOOP_STATE_B);
        break;
    case ABLOOP_STATE_B:
        setABloopState(ABLOOP_STATE_NONE);
        break;
    }
}

void PlayerController::toggleRecord()
{
    Q_D(PlayerController);
    setRecording(!d->m_recording);
}

void PlayerController::toggleVisualization()
{
    Q_D(PlayerController);

    if ( d->m_audioVisualization.rowCount() < 1 )
        return;

    if ( !d->m_audioVisualization.hasCurrent()
         || d->m_audioVisualization.currentRow() == 0 /*0th row is "Disable"*/)
    {
        const int r = rand() % d->m_audioVisualization.rowCount();
        d->m_audioVisualization.setData( d->m_audioVisualization.index( r )
                                         , QVariant::fromValue<bool>( true )
                                         , Qt::CheckStateRole );
    }
    else
    {
        d->m_audioVisualization.setData( d->m_audioVisualization.index(0)
                                         , QVariant::fromValue<bool>( true )
                                         , Qt::CheckStateRole);
    }
}

void PlayerController::requestAddSMPTETimer()
{
    Q_D(PlayerController);

    if ( ++d->m_smpteTimerRequestCount == 1 )
    {
        d->addSMPTETimer();
    }
}

void PlayerController::requestRemoveSMPTETimer()
{
    Q_D(PlayerController);

    assert( d->m_smpteTimerRequestCount > 0 );
    if ( --d->m_smpteTimerRequestCount == 0 )
    {
        d->removeSMPTETimer();
    }
}

void PlayerController::setRecording( bool recording )
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    vlc_player_SetRecordingEnabled( d->m_player, recording, NULL );
}

void PlayerController::snapshot()
{
    SharedVOutThread vout = getVout();
    if (vout)
    {
        /* Passing a lambda directly would require Qt 5.15:
         * <https://doc.qt.io/qt-5/qthreadpool.html#start-1>
         */
        struct SnapshotTask : public QRunnable
        {
            SharedVOutThread vout;
            SnapshotTask(SharedVOutThread vout) : vout(std::move(vout)) {}
            void run() override
            {
                var_TriggerCallback(vout.get(), "video-snapshot");
            }
        };

        QThreadPool::globalInstance()->start(new SnapshotTask(std::move(vout)));
    }
}

QString PlayerController::highResolutionTime() const
{
    Q_D(const PlayerController);

    // Sample:
    vlc_player_timer_smpte_timecode sample;
    {
#ifndef QT_HAS_LIBATOMIC
        QReadLocker lock(&d->m_highResolutionTimeLock);
#endif
        sample = d->m_highResolutionTime;
    }

    const vlc_player_timer_smpte_timecode& lastSample = d->m_highResolutionTimeSample.first;

    if (sample.frames == lastSample.frames &&
        sample.frame_resolution == lastSample.frame_resolution &&
        sample.seconds == lastSample.seconds &&
        sample.minutes == lastSample.minutes &&
        sample.hours == lastSample.hours &&
        sample.drop_frame == lastSample.drop_frame)
        return d->m_highResolutionTimeSample.second;

    d->m_highResolutionTimeSample.second = QString::asprintf("%02u:%02u:%02u%c%02u",
                                                             sample.hours,
                                                             sample.minutes,
                                                             sample.seconds,
                                                             sample.drop_frame ? '.' : ':',
                                                             sample.frames);
    d->m_highResolutionTimeSample.first = std::move(sample);

    return d->m_highResolutionTimeSample.second;
}

//OTHER


/* Playlist Control functions */

void PlayerControllerPrivate::requestAutoMetadata()
{
    if (!var_InheritBool(p_intf, "metadata-network-access")
        || (m_playing_status != PlayerController::PLAYING_STATE_PLAYING
            && m_playing_status != PlayerController::PLAYING_STATE_PAUSED))
        return;

    SharedInputItem item;
    {
        vlc_player_locker lock{m_player};
        if (!m_currentItem || !isCurrentItemSynced()
            || m_autoMetadataItem.get() == m_currentItem.get())
            return;
        item = m_currentItem;
        m_autoMetadataItem = item;
    }
    // Start after demux metadata is available; pause/resume must not repeat
    // provider requests for the same current media.
    requestMetadata(item.get(), true);
}

void PlayerController::refreshMetadataPreference()
{
    Q_D(PlayerController);
    emit metadataNetworkAccessChanged(var_InheritBool(d->p_intf, "metadata-network-access"));
    if (!var_InheritBool(d->p_intf, "metadata-network-access"))
    {
        ++d->m_metadataGeneration;
        d->m_autoMetadataItem.reset();
        // Per-request Cancel does not cover metadata-only fetcher tasks.
        // Deleting this dedicated preparser interrupts every fetch stage and
        // prevents a cancelled search from starting an artwork download.
        if (d->m_preparser)
        {
            vlc_preparser_Delete(d->m_preparser);
            d->m_preparser = nullptr;
        }
        return;
    }
    d->requestAutoMetadata();
}

void PlayerController::requestArtUpdate( input_item_t *p_item )
{
    Q_D(PlayerController);
    d->requestMetadata(p_item, false);
}

void PlayerController::setMetadataService(AddonMetadataService* service)
{
    Q_D(PlayerController);
    if (d->m_metadataService == service) return;
    d->m_metadataService = service;
    emit metadataServiceChanged();
}
AddonMetadataService* PlayerController::metadataService() const
{
    Q_D(const PlayerController);
    return d->m_metadataService;
}

void PlayerControllerPrivate::requestMetadata(input_item_t *p_item, bool automatic)
{
    if (m_preparser == nullptr)
    {
        vlc_tick_t default_timeout =
            VLC_TICK_FROM_MS(var_InheritInteger(p_intf, "preparse-timeout"));
        if (default_timeout < 0)
            default_timeout = 0;

        const struct vlc_preparser_cfg cfg = [default_timeout]{
            struct vlc_preparser_cfg cfg{};
            cfg.types = VLC_PREPARSER_TYPE_FETCHMETA_ALL;
            cfg.max_parser_threads = 1;
            cfg.timeout = default_timeout;
            cfg.external_process = false;
            return cfg;
        }();
        m_preparser = vlc_preparser_New(VLC_OBJECT(p_intf), &cfg);
        if (unlikely(m_preparser == nullptr))
            return;
    }

    int fetch_options = var_InheritBool( p_intf, "metadata-network-access" ) ?
            VLC_PREPARSER_TYPE_FETCHMETA_ALL :
            VLC_PREPARSER_TYPE_FETCHMETA_LOCAL;

    auto context = std::make_unique<ArtFetchContext>(ArtFetchContext{
        this, automatic, m_metadataGeneration, SharedInputItem(p_item), {}});
    // A background provider must not mutate the live item while the user is
    // editing it, or publish results after the network choice was disabled.
    SharedInputItem working = automatic
        ? SharedInputItem(input_item_Copy(p_item), false) : SharedInputItem(p_item);
    if (!working)
        return;
    if (automatic)
        for (int type = 0; type < VLC_META_TYPE_COUNT; ++type)
            context->before.append(qfu(vlc::wrap_cptr(input_item_GetMeta(
                working.get(), static_cast<vlc_meta_type_t>(type))).get()));
    vlc_preparser_req *req =
        vlc_preparser_req_NewParse( m_preparser, working.get(), fetch_options,
                                    &art_fetcher_cbs, context.get() );
    if (unlikely(req == nullptr))
        return;

    // A successful submission transfers the context to the completion callback.
    // This also covers cancellation when the dedicated preparser is deleted.
    if (vlc_preparser_Submit(m_preparser, req) == VLC_SUCCESS)
        context.release();
    vlc_preparser_req_Release( req );
}

void PlayerControllerPrivate::onArtFetchEnded(input_item_t *result, input_item_t *target,
                                             bool automatic, quint64 generation, const QStringList &before)
{
    // The preparser completes on a worker. Keep the item alive until the UI
    // thread can check whether it still belongs to the current media.
    SharedInputItem item(target), fetched(result);
    callAsync([this, item, fetched, automatic, generation, before] {
        Q_Q(PlayerController);
        if (automatic && (generation != m_metadataGeneration
            || !var_InheritBool(p_intf, "metadata-network-access")))
            return;
        bool current;
        {
            vlc_player_locker lock{m_player};
            current = item.get() == vlc_player_GetCurrentMedia(m_player)
                && isCurrentItemSynced();
        }
        if (automatic)
        {
            if (!current)
                return;
            for (int type = 0; type < before.size(); ++type)
            {
                const auto field = static_cast<vlc_meta_type_t>(type);
                const auto value = vlc::wrap_cptr(input_item_GetMeta(fetched.get(), field));
                const auto existing = vlc::wrap_cptr(input_item_GetMeta(item.get(), field));
                // Keep any field changed since the lookup started, including
                // a cover image or title explicitly selected by the user.
                if (qfu(existing.get()) == before[type] && qfu(value.get()) != before[type])
                    input_item_SetMeta(item.get(), field, value.get());
            }
        }
        if (current)
        {
            UpdateName(item.get());
            UpdateMeta(item.get());
            UpdateArt(item.get());
        }
        else
            emit q->artChanged(item.get());
    });
}

const QString PlayerController::decodeArtURL( input_item_t *p_item )
{
    assert( p_item );

    char *psz_art = vlc_artwork_item_GetURI( p_item );
    if( psz_art )
    {
        char *psz = vlc_uri2path( psz_art );
        free( psz_art );
        psz_art = psz;
    }

#if 0
    /* Taglib seems to define a attachment://, It won't work yet */
    url = url.replace( "attachment://", "" );
#endif

    QString path = qfu( psz_art ? psz_art : "" );
    free( psz_art );
    return path;
}

void PlayerController::setArtworkSelectionLifetime(vlc::addons::AddonStreamLifetime::Handle lifetime, QString cacheDirectory)
{
    Q_D(PlayerController);
    d->m_artworkSelection.reset(); d->m_artworkSelectionLifetime=std::move(lifetime);
    d->m_artworkSelectionDirectory=std::move(cacheDirectory);
}

void PlayerController::setArt( input_item_t *p_item, QString fileUrl )
{
    Q_D(PlayerController);
    if(!p_item || !hasInput()) return;
    const SharedInputItem target(p_item);
    {
        vlc_player_locker lock(d->m_player);
        if(vlc_player_GetCurrentMedia(d->m_player)!=target.get()) return;
    }
    if(!d->m_artworkSelection) {
        auto lifetime=d->m_artworkSelectionLifetime;
        auto* ctx=d->p_intf->p_mi;
        if(!lifetime.current() && ctx) lifetime=ctx->addonStreamLifetime();
        if(!lifetime.current()) return;
        const auto directory=d->m_artworkSelectionDirectory.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+QStringLiteral("/selected-artwork") : d->m_artworkSelectionDirectory;
        d->m_artworkSelection=std::make_unique<ArtworkSelection>(lifetime,directory,this);
        auto* selection=d->m_artworkSelection.get();
        connect(this,&PlayerController::inputChanged,selection,[selection] { selection->cancel(); });
        if(ctx) connect(ctx,&MainCtx::addonWorkRetiring,selection,&ArtworkSelection::close,Qt::DirectConnection);
    }
    d->m_artworkSelection->prepare(target.get(),QUrl(fileUrl,QUrl::StrictMode),[this,target](ArtworkSelection::Result result) {
        if(!result.receipt) return;
        Q_D(PlayerController);
        {
            vlc_player_locker lock(d->m_player);
            if(vlc_player_GetCurrentMedia(d->m_player)!=target.get()
                || result.receipt->apply(target.get(),result.generation)!=VLC_SUCCESS) return;
        }
        const QPointer<PlayerController> owner(this);
        d->UpdateMeta(target.get());
        if(!owner) return;
        {
            vlc_player_locker lock(d->m_player);
            if(vlc_player_GetCurrentMedia(d->m_player)!=target.get()) return;
        }
        d->UpdateArt(target.get());
    });
}

void PlayerController::refreshMediaMeta( input_item_t *p_item )
{
    if (p_item == nullptr)
        return;

    Q_D(PlayerController);

    const bool isCurrentItem = [d, p_item]() {
        vlc_player_locker lock{ d->m_player };
        return p_item == vlc_player_GetCurrentMedia( d->m_player );
    }();

    if (!isCurrentItem)
        return;

    d->UpdateName( p_item );
    d->UpdateArt( p_item );
    d->UpdateMeta( p_item );
    d->UpdateInfo( p_item );
}

bool PlayerController::associateSubtitleFile(const QString &uri)
{
    return AddAssociatedMedia(SPU_ES, uri, true, true, true) == VLC_SUCCESS;
}

int PlayerController::AddAssociatedMedia(es_format_category_e cat, const QString &uri, bool select, bool notify, bool check_ext)
{
    Q_D(PlayerController);
    vlc_player_locker lock{ d->m_player };
    return vlc_player_AddAssociatedMedia( d->m_player, cat, qtu(uri), select, notify, check_ext );
}

#define QABSTRACTLIST_GETTER( type, fun, var ) \
    type* PlayerController::fun() \
    { \
        Q_D(PlayerController); \
        return &d->var; \
    }


QABSTRACTLIST_GETTER( TrackListModel, getVideoTracks, m_videoTracks)
QABSTRACTLIST_GETTER( TrackListModel, getAudioTracks, m_audioTracks)
QABSTRACTLIST_GETTER( TrackListModel, getSubtitleTracks, m_subtitleTracks)
QABSTRACTLIST_GETTER( TitleListModel, getTitles, m_titleList)
QABSTRACTLIST_GETTER( ChapterListModel,getChapters, m_chapterList)
QABSTRACTLIST_GETTER( AudioDeviceModel, getAudioDevices, m_audioDeviceList)
QABSTRACTLIST_GETTER( ProgramListModel, getPrograms, m_programList)
QABSTRACTLIST_GETTER( VLCVarChoiceModel, getVideoStereoMode, m_videoStereoMode)
QABSTRACTLIST_GETTER( VLCVarChoiceModel, getZoom, m_zoom)
QABSTRACTLIST_GETTER( VLCVarChoiceModel, getAspectRatio, m_aspectRatio)
QABSTRACTLIST_GETTER( VLCVarChoiceModel, getCrop, m_crop)
QABSTRACTLIST_GETTER( VLCVarChoiceModel, getFit, m_fit)
QABSTRACTLIST_GETTER( VLCVarChoiceModel, getDeinterlace, m_deinterlace)
QABSTRACTLIST_GETTER( VLCVarChoiceModel, getDeinterlaceMode, m_deinterlaceMode)
QABSTRACTLIST_GETTER( VLCVarChoiceModel, getAudioStereoMode, m_audioStereoMode)
QABSTRACTLIST_GETTER( VLCVarChoiceModel, getAudioMixMode, m_audioMixMode)
QABSTRACTLIST_GETTER( VLCVarChoiceModel, getAudioVisualizations, m_audioVisualization)
QABSTRACTLIST_GETTER( RendererManager, getRendererManager, m_rendererManager)

#undef QABSTRACTLIST_GETTER

#define PRIMITIVETYPE_GETTER( type, fun, var ) \
    type PlayerController::fun() const \
    { \
        Q_D(const PlayerController); \
        return d->var; \
    }


PRIMITIVETYPE_GETTER(PlayerController::PlayingState, getPlayingState, m_playing_status)
PRIMITIVETYPE_GETTER(QString, getName, m_name)
PRIMITIVETYPE_GETTER(VLCTime, getTime, m_time)
PRIMITIVETYPE_GETTER(VLCDuration, getRemainingTime, m_remainingTime)
PRIMITIVETYPE_GETTER(double, getPosition, m_position)
PRIMITIVETYPE_GETTER(VLCDuration, getLength, m_length)
PRIMITIVETYPE_GETTER(VLCDuration, getAudioDelay, m_audioDelay)
PRIMITIVETYPE_GETTER(VLCDuration, getSubtitleDelay, m_subtitleDelay)
PRIMITIVETYPE_GETTER(VLCDuration, getSecondarySubtitleDelay, m_secondarySubtitleDelay)
PRIMITIVETYPE_GETTER(bool, isSeekable, m_capabilities & VLC_PLAYER_CAP_SEEK)
PRIMITIVETYPE_GETTER(bool, isRewindable, m_capabilities & VLC_PLAYER_CAP_REWIND)
PRIMITIVETYPE_GETTER(bool, isPausable, m_capabilities & VLC_PLAYER_CAP_PAUSE)
PRIMITIVETYPE_GETTER(bool, isRateChangable, m_capabilities & VLC_PLAYER_CAP_CHANGE_RATE)
PRIMITIVETYPE_GETTER(bool, canRestorePlayback, m_canRestorePlayback);
PRIMITIVETYPE_GETTER(double, getPlaybackRestorePosition, m_playbackRestorePosition);
PRIMITIVETYPE_GETTER(float, getSubtitleFPS, m_subtitleFPS)
PRIMITIVETYPE_GETTER(int, getSubtitleTextScale, m_subtitleTextScale)
PRIMITIVETYPE_GETTER(int, getSubtitleTextSizeMode, m_subtitleTextSizeMode)
PRIMITIVETYPE_GETTER(int, getSubtitleTextPixelSize, m_subtitleTextPixelSize)
PRIMITIVETYPE_GETTER(bool, hasVideoOutput, m_hasVideo)
PRIMITIVETYPE_GETTER(float, getBuffering, m_buffering)
PRIMITIVETYPE_GETTER(float, getVolume, m_volume)
PRIMITIVETYPE_GETTER(bool, isMuted, m_muted)
PRIMITIVETYPE_GETTER(bool, isFullscreen, m_fullscreen)
PRIMITIVETYPE_GETTER(bool, getWallpaperMode, m_wallpaperMode)
PRIMITIVETYPE_GETTER(float, getRate, m_rate)
PRIMITIVETYPE_GETTER(bool, hasTitles, m_hasTitles)
PRIMITIVETYPE_GETTER(bool, hasChapters, m_hasChapters)
PRIMITIVETYPE_GETTER(bool, hasMenu, m_hasMenu)
PRIMITIVETYPE_GETTER(bool, isMenu, m_isMenu)
PRIMITIVETYPE_GETTER(bool, isInteractive, m_isInteractive)
PRIMITIVETYPE_GETTER(bool, hasPrograms, m_hasPrograms)
PRIMITIVETYPE_GETTER(bool, isEncrypted, m_encrypted)
PRIMITIVETYPE_GETTER(bool, isRecording, m_recording)
PRIMITIVETYPE_GETTER(PlayerController::ABLoopState, getABloopState, m_ABLoopState)
PRIMITIVETYPE_GETTER(VLCTime, getABLoopA, m_ABLoopA)
PRIMITIVETYPE_GETTER(VLCTime, getABLoopB, m_ABLoopB)
PRIMITIVETYPE_GETTER(bool, isTeletextEnabled, m_teletextEnabled)
PRIMITIVETYPE_GETTER(bool, isTeletextAvailable, m_teletextAvailable)
PRIMITIVETYPE_GETTER(int, getTeletextPage, m_teletextPage)
PRIMITIVETYPE_GETTER(bool, getTeletextTransparency, m_teletextTransparent)
PRIMITIVETYPE_GETTER(QString, getTitle, m_title)
PRIMITIVETYPE_GETTER(QString, getArtist, m_artist)
PRIMITIVETYPE_GETTER(QString, getAlbum, m_album)
QUrl PlayerController::getArtwork() const
{
    Q_D(const PlayerController);
    if (d->m_currentItem && vlc_artwork_item_IsRestricted(d->m_currentItem.get())) {
        const auto artwork = vlc::wrap_cptr(vlc_artwork_item_GetURI(d->m_currentItem.get()));
        return QUrl::fromEncoded(QByteArray(artwork ? artwork.get() : ""));
    }
    return d->m_artwork;
}
PRIMITIVETYPE_GETTER(QUrl, getUrl, m_url)

QVariantMap PlayerController::getSeriesContext() const
{
    Q_D(const PlayerController);
    SharedInputItem item;
    {
        vlc_player_locker locker{d->m_player};
        item = SharedInputItem(vlc_player_GetCurrentMedia(d->m_player));
    }
    if (!item) return {};
    auto metadata = MediaIdentity::inputMetadata(item.get());
    const auto description = vlc::wrap_cptr(input_item_GetDescription(item.get()));
    metadata["episodeTitle"] = MediaIdentity::episodeTitle(getUrl(), metadata);
    if (description) {
        QTextDocument notes; notes.setHtml(QString::fromUtf8(description.get()));
        metadata["description"] = notes.toPlainText();
    }
    return metadata;
}

void PlayerController::playSeriesEpisode(const QUrl& url, bool fromStart)
{
    Q_D(PlayerController);
    if (!url.isLocalFile() || !QFileInfo(url.toLocalFile()).isFile()) return;
    const auto uri = url.toEncoded();
    const auto item = SharedInputItem(input_item_New(uri.constData(), nullptr), false);
    if (!item) return;
    input_item_AddOption(item.get(), fromStart ? "restore-playback-pos=0" : "restore-playback-pos=2", VLC_INPUT_OPTION_TRUSTED);
    d->p_intf->p_mainPlaylistController->append(QVector<vlc::playlist::Media>{vlc::playlist::Media{item.get()}}, true);
}

TimedText PlayerController::getCurrentLyric() const
{
    Q_D(const PlayerController);
    if (d->m_currentLyricIndex < 0)
        return {};
    const auto& lyrics = d->syltLyrics();
    if (d->m_currentLyricIndex >= lyrics.size())
        return {};
    return lyrics[d->m_currentLyricIndex];
}

bool PlayerController::hasLyrics() const
{
    Q_D(const PlayerController);
    return d->m_syltRaw.has_value();
}

QList<TimedText> PlayerController::getSyltLyrics() const
{
    Q_D(const PlayerController);
    return d->syltLyrics();
}

int PlayerController::getCurrentLyricIndex() const
{
    Q_D(const PlayerController);
    return d->m_currentLyricIndex;
}

#undef PRIMITIVETYPE_GETTER
