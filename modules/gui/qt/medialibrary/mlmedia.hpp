/*****************************************************************************
 * Copyright (C) 2024 VLC authors and VideoLAN
 *
 * Authors: Prince Gupta <guptaprince8832@gmail.com>
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

#pragma once

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"
#include "mlqmltypes.hpp"
#include "mlhelper.hpp"
#include "util/media_identity_ml.hpp"
#include "util/vlctick.hpp"
#include "util/podcast_metadata.hpp"
#include "util/shared_input_item.hpp"
#include <vlc_artwork_policy.h>

#include <QObject>
#include <algorithm>
#include <QDateTime>
#include <QTimeZone>

class MLMedia : public MLItem
{
public:
    MLMedia() : MLItem {MLItemId()} {}

    MLMedia(const vlc_ml_media_t *media)
        : MLItem {MLItemId(media->i_id, VLC_ML_PARENT_UNKNOWN)}
    {
        const auto getThumbnail = [](const vlc_ml_thumbnail_t & thumbnail)
        {
            const QString mrl =
                    (thumbnail.i_status == VLC_ML_THUMBNAIL_STATUS_AVAILABLE)
                    ? qfu(thumbnail.psz_mrl) : QString {};

            return MLThumbnail{ mrl, thumbnail.i_status };
        };
        const auto getMRL = [&media]
        {
            for (const vlc_ml_file_t &file: ml_range_iterate<vlc_ml_file_t>(media->p_files))
                // FIXME: should we store every mrl?
                if (file.i_type == VLC_ML_FILE_TYPE_MAIN)
                    return QUrl::fromEncoded(file.psz_mrl);
            return QUrl();
        };

        m_title = qfu(media->psz_title);
        m_fileName = qfu(media->psz_filename);
        m_smallThumbnail = getThumbnail(media->thumbnails[VLC_ML_THUMBNAIL_SMALL]);
        m_bannerThumbnail = getThumbnail(media->thumbnails[VLC_ML_THUMBNAIL_BANNER]);
        m_duration = VLCDuration::fromMS(media->i_duration);
        m_progress = media->f_progress;
        m_playCount = media->i_playcount;
        if (media->i_subtype == VLC_ML_MEDIA_SUBTYPE_ALBUMTRACK) {
            m_trackNumber = std::max(0, media->album_track.i_track_nb);
            m_discNumber = std::max(0, media->album_track.i_disc_nb);
        }
        m_mrl = getMRL();
        if (media->p_podcast) {
            m_artworkInput = SharedInputItem(media->p_podcast);
            m_podcast = podcastMetadata(media->p_podcast);
            m_mrl = QUrl::fromEncoded(media->p_podcast->psz_uri);
            const auto artwork = vlc::wrap_cptr(vlc_artwork_item_GetURI(media->p_podcast));
            if (artwork && m_smallThumbnail.mrl.isEmpty()) m_smallThumbnail = {QString::fromUtf8(artwork.get()), VLC_ML_THUMBNAIL_STATUS_AVAILABLE};
        }
        m_identityMetadata = MediaIdentity::libraryMetadata(media, m_mrl);
        m_type = media->i_type;
        m_isFavorite = media->b_is_favorite;
        m_lastPlayedDate = QDateTime::fromSecsSinceEpoch(media->i_last_played_date, QTimeZone::systemTimeZone());
    }

    QVariantMap podcast() const { return m_podcast; }
    QVariantMap identityMetadata() const { return m_identityMetadata; }
    void setIdentityMetadata(const QVariantMap& metadata) { m_identityMetadata = metadata; }
    QString title() const { return m_title; }
    QString fileName() const { return m_fileName; }
    VLCDuration duration() const { return m_duration; }
    qreal progress() const { return m_progress; }
    int playCount() const { return m_playCount; }
    unsigned trackNumber() const { return m_trackNumber; }
    unsigned discNumber() const { return m_discNumber; }
    QString mrl() const { return m_mrl.toEncoded(); }
    vlc_ml_media_type_t type() const { return m_type; }
    bool isFavorite() const { return m_isFavorite; }
    void setIsFavorite(const bool isFavorite) { m_isFavorite = isFavorite; }
    QDateTime lastPlayedDate() const { return m_lastPlayedDate; }

    QString smallCover(vlc_ml_thumbnail_status_t* status = nullptr) const
    {
        if (requiresNativeArtwork()) return nativeArtwork(status);
        if (status) *status = m_smallThumbnail.status;
        return m_smallThumbnail.mrl;
    }

    void setSmallCover(vlc_ml_thumbnail_status_t status, QString mrl)
    {
        m_smallThumbnail = {std::move(mrl), status};
    }

    QString bannerCover(vlc_ml_thumbnail_status_t* status = nullptr) const
    {
        if (requiresNativeArtwork()) return nativeArtwork(status);
        if (status) *status = m_bannerThumbnail.status;
        return m_bannerThumbnail.mrl;
    }

    VLCTime progressTime() const { return m_duration * m_progress; }

    Q_INVOKABLE bool valid() const { return getId().id != INVALID_MLITEMID_ID; }

protected:
    bool requiresNativeArtwork() const
    {
        return m_mrl.scheme().compare(QLatin1String("hakone-addon-source"), Qt::CaseInsensitive) == 0
            || m_mrl.scheme().compare(QLatin1String("vlc-addon-source"), Qt::CaseInsensitive) == 0
            || (m_artworkInput && vlc_artwork_item_IsRestricted(m_artworkInput.get()));
    }
    QString nativeArtwork(vlc_ml_thumbnail_status_t* status) const
    {
        QString result;
        if (m_artworkInput) {
            const auto source = vlc::wrap_cptr(input_item_GetURI(m_artworkInput.get()));
            if (QUrl::fromEncoded(QByteArray(source ? source.get() : "")) == m_mrl) {
                const auto artwork = vlc::wrap_cptr(vlc_artwork_item_GetURI(m_artworkInput.get()));
                if (artwork) result = QString::fromUtf8(artwork.get());
            }
        }
        if (status) *status = result.isEmpty() ? VLC_ML_THUMBNAIL_STATUS_FAILURE : VLC_ML_THUMBNAIL_STATUS_AVAILABLE;
        return result;
    }
    struct MLThumbnail
    {
        QString mrl;
        vlc_ml_thumbnail_status_t status = VLC_ML_THUMBNAIL_STATUS_MISSING;
    };

    QVariantMap m_podcast;
    SharedInputItem m_artworkInput;
    QVariantMap m_identityMetadata;
    QString m_title;
    QString m_fileName;
    MLThumbnail m_smallThumbnail;
    MLThumbnail m_bannerThumbnail;
    VLCDuration m_duration;
    qreal m_progress = 0;
    int m_playCount = 0;
    unsigned m_trackNumber = 0;
    unsigned m_discNumber = 0;
    QUrl m_mrl;
    vlc_ml_media_type_t m_type = VLC_ML_MEDIA_TYPE_UNKNOWN;
    bool m_isFavorite = false;
    QDateTime m_lastPlayedDate;
};
