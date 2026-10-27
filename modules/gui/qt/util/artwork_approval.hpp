// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addons/addon_artwork_assets.hpp"
#include <vlc_artwork_policy.h>
#include <vlc_input_item.h>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

namespace ArtworkApproval {
inline bool validUserFile(const QUrl& url)
{
    const QByteArray encoded = url.toEncoded();
    if (!url.isLocalFile() || !url.host().isEmpty() || !url.query().isEmpty() || !url.fragment().isEmpty()
        || !vlc_artwork_uri_IsLocalFile(encoded.constData())) return false;
    const QFileInfo info(url.toLocalFile());
    if (!info.isFile() || info.size() > 1024 * 1024) return false;
    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return false;
    const auto bytes = file.read(1024 * 1024 + 1);
    return AddonArtworkAssets::imageFormat(bytes, AddonArtworkAssets::Options{}, nullptr);
}
inline bool userFile(input_item_t *item, const QUrl& url)
{
    if (!item) return false;
    const QByteArray encoded = url.toEncoded();
    if (!vlc_artwork_item_IsRestricted(item)) {
        input_item_SetArtURL(item, encoded.constData()); return true;
    }
    if (!validUserFile(url)) return false;
    return vlc_artwork_item_SetApprovedURI(item, encoded.constData()) == VLC_SUCCESS;
}
}
