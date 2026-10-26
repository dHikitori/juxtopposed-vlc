// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "media_identity.hpp"
#include <QDateTime>
#include <vlc_common.h>
#include <vlc_input_item.h>

namespace MediaIdentity {
// Read existing tags only. Identification never starts playback or network preparsing.
inline QVariantMap inputMetadata(input_item_t* item)
{
    QVariantMap metadata;
    if (!item) return metadata;
    const QMap<QString, vlc_meta_type_t> fields{{"title", vlc_meta_Title}, {"showName", vlc_meta_ShowName},
        {"season", vlc_meta_Season}, {"episode", vlc_meta_Episode},
        {"artist", vlc_meta_Artist}, {"albumArtist", vlc_meta_AlbumArtist}, {"album", vlc_meta_Album},
        {"track", vlc_meta_TrackNumber}, {"disc", vlc_meta_DiscNumber}};
    for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        char* value = input_item_GetMeta(item, it.value());
        if (value && *value) metadata.insert(it.key(), QString::fromUtf8(value));
        free(value);
    }
    const auto extra = [item](const char* name) {
        char* value = input_item_GetMetaExtra(item, name);
        if (!value) {
            const auto lower = QByteArray(name).toLower();
            value = input_item_GetMetaExtra(item, lower.constData());
        }
        const auto result = value ? QString::fromUtf8(value).trimmed() : QString();
        free(value); return result;
    };
    const auto podcast = extra("PODCAST");
    auto feed = extra("PODCASTURL");
    if (feed.isEmpty()) feed = extra("ITUNESPODCASTURL");
    if (feed.isEmpty()) feed = extra("PURL");
    if (feed.isEmpty()) feed = extra("podcast:feed");
    if (podcast == "1" || podcast.compare("true", Qt::CaseInsensitive) == 0 || !feed.isEmpty()) {
        metadata.insert("type", "podcastEpisode");
        metadata.insert("feedUrl", feed);
        metadata.insert("series", metadata.value("showName", metadata.value("album")));
        auto guid = extra("GUID");
        if (guid.isEmpty()) guid = extra("EGID");
        if (guid.isEmpty()) guid = extra("podcast:guid");
        metadata.insert("guid", guid);
        char* value = input_item_GetMeta(item, vlc_meta_Date);
        auto date = value ? QDate::fromString(QString::fromUtf8(value).left(10), Qt::ISODate) : QDate();
        if (!date.isValid() && value) date = publicationDate(QString::fromUtf8(value)).date();
        free(value);
        if (date.isValid()) metadata.insert("airDate", date.toString(Qt::ISODate));
    }
    for (const auto& field : {qMakePair("MUSICBRAINZ_TRACKID", "musicBrainzTrackId"),
             qMakePair("MUSICBRAINZ_ALBUMID", "musicBrainzAlbumId"), qMakePair("MUSICBRAINZ_ARTISTID", "musicBrainzArtistId")}) {
        const auto value = extra(field.first);
        if (!value.isEmpty()) metadata.insert(QLatin1String(field.second), value);
    }
    const auto albumId = extra("MB_ALBUMID");
    if (!albumId.isEmpty()) metadata.insert("musicBrainzAlbumId", albumId);
    const auto listing = extra("Listing Type").toCaseFolded();
    if (listing == QStringLiteral("radio") || listing == QStringLiteral("tv"))
        metadata.insert("release", QVariantMap{{"listingType", listing}});
    char* uri = input_item_GetURI(item);
    const auto filename = uri ? QUrl(QString::fromUtf8(uri)).fileName() : QString(); free(uri);
    // An embedded movie date is useful; a TV episode date is not its series start year.
    if (parse(filename).kind != Kind::Episode && !metadata.contains("showName") && !metadata.contains("episode")
        && metadata.value("type").toString() != "podcastEpisode") {
        char* date = input_item_GetMeta(item, vlc_meta_Date);
        const auto match = QRegularExpression(QStringLiteral("^((?:18[89]|19|20)\\d{2})(?:$|-)"))
            .match(date ? QString::fromUtf8(date) : QString());
        if (match.hasMatch()) metadata.insert("year", match.captured(1).toInt());
        free(date);
    }
    const auto title = metadata.value("title").toString();
    if (title == filename || title == QFileInfo(filename).completeBaseName()) metadata.remove("title");
    const auto duration = input_item_GetDuration(item);
    if (duration != VLC_TICK_INVALID) metadata.insert("durationMs", qint64(MS_FROM_VLC_TICK(duration)));
    return metadata;
}
}
