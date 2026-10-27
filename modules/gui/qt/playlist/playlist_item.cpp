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
#include "playlist_item.hpp"
#include "util/vlctick.hpp"
#include <vlc_input_item.h>
#include <vlc_artwork_policy.h>
#include <algorithm>
#include <QDir>

//namespace vlc {
//namespace playlist {

PlaylistItem::PlaylistItem(vlc_playlist_item_t* item)
{
    d = new Data();
    if (item)
    {
        d->item.reset(item);
        sync();
    }
}

bool PlaylistItem::isSelected() const
{
    return d->selected;
}

void PlaylistItem::setSelected(bool selected)
{
    d->selected = selected;
}

QString PlaylistItem::getTitle() const
{
    return d->title;
}

QString PlaylistItem::getArtist() const
{
    return d->artist;
}

QString PlaylistItem::getAlbum() const
{
    return d->album;
}

QString PlaylistItem::getPlaylistName() const
{
    return d->playlistName;
}

QString PlaylistItem::getId() const
{
    return raw() ? QString::number(vlc_playlist_item_GetId(raw())) : QString();
}

QVariantList PlaylistItem::getAncestors() const
{
    return d->ancestors;
}

QUrl PlaylistItem::getArtwork() const
{
    if (auto* item = inputItem(); item && vlc_artwork_item_IsRestricted(item)) {
        const auto uri = vlc::wrap_cptr(vlc_artwork_item_GetURI(item));
        return QUrl::fromEncoded(QByteArray(uri ? uri.get() : ""));
    }
    return d->artwork;
}

VLCDuration PlaylistItem::getDuration() const
{
    return d->duration;
}

QUrl PlaylistItem::getUrl() const
{
    return d->url;
}

void PlaylistItem::sync() {
    input_item_t *media = inputItem();
    assert(media);
    const auto artwork = vlc::wrap_cptr(vlc_artwork_item_GetURI(media));
    d->artwork = QUrl::fromEncoded(QByteArray(artwork ? artwork.get() : ""));
    QUrl childUrl;
    { vlc_mutex_locker childLocker(&media->lock); childUrl = QUrl(QString::fromUtf8(media->psz_uri)); }
    d->ancestors.clear();
    for (auto* parent = vlc_playlist_item_GetParent(raw()); parent;
         parent = vlc_playlist_item_GetParent(parent)) {
        auto* source = vlc_playlist_item_GetMedia(parent);
        vlc_mutex_locker parentLocker(&source->lock);
        const char* title = source->p_meta ? vlc_meta_Get(source->p_meta, vlc_meta_Title) : nullptr;
        const QUrl sourceUrl(QString::fromUtf8(source->psz_uri));
        // The root can retain its original file type after directory parsing.
        // Its known child URL identifies a folder without a filesystem probe.
        QString folderPath = sourceUrl.path();
        if (!folderPath.endsWith(QLatin1Char('/'))) folderPath.append(QLatin1Char('/'));
        const bool folder = source->i_type == ITEM_TYPE_DIRECTORY
            || (source->i_type == ITEM_TYPE_FILE && sourceUrl.scheme() == childUrl.scheme()
                && sourceUrl.authority() == childUrl.authority() && childUrl.path().startsWith(folderPath));
        d->ancestors.append(QVariantMap{
            {QStringLiteral("key"), QString::number(vlc_playlist_item_GetId(parent))},
            {QStringLiteral("title"), QString::fromUtf8(title ? title : source->psz_name)},
            {QStringLiteral("url"), sourceUrl},
            {QStringLiteral("isPlaylist"), !folder && source->i_type != ITEM_TYPE_NODE}});
        childUrl = sourceUrl;
    }
    std::reverse(d->ancestors.begin(), d->ancestors.end());
    vlc_mutex_locker locker(&media->lock);
    d->duration =
        (media->i_duration == INPUT_DURATION_INDEFINITE
            || media->i_duration == INPUT_DURATION_UNSET)
        ? VLCDuration{}
        : VLCDuration{media->i_duration};
    d->url      = media->psz_uri;

    if (media->p_meta) {
        d->playlistName = vlc_meta_GetExtra(media->p_meta, "Source playlist");
        d->title   = vlc_meta_Get(media->p_meta, vlc_meta_Title);
        d->artist  = vlc_meta_Get(media->p_meta, vlc_meta_Artist);
        d->album   = vlc_meta_Get(media->p_meta, vlc_meta_Album);
    }

    if (d->title.isNull()) {
        // Playlist entries without an explicit title can use a full path as
        // their name. Show its file name, without probing the source path.
        d->title = QString::fromUtf8(media->psz_name);
    }
    if (!d->url.fileName().isEmpty()
        && (d->title == d->url.toString()
            || (!d->url.toLocalFile().isEmpty()
                && QDir::fromNativeSeparators(d->title) == QDir::fromNativeSeparators(d->url.toLocalFile()))))
        d->title = d->url.fileName(QUrl::FullyDecoded);
}

PlaylistItem::operator bool() const
{
    return d && d->item.get();
}

bool PlaylistItem::preparsed() const {
    if (const auto item = inputItem())
        return input_item_IsPreparsed(item);
    return false;
}


//}
//}
