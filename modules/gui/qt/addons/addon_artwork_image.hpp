// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QByteArray>
#include <QImage>
#include <QString>

namespace vlc::addons {
/** Decode only after the container pass. Native artwork has a separate profile.
 * A returned image has pixels only. It has no text, profile, or animation data.
 * Codec workspace is not part of the pixel limit.
 */
class AddonArtworkImage
{
public:
    enum class Profile { StaticAddon, NativeArtwork };
    struct Limits {
        qint64 encodedBytes = 1024 * 1024;
        qint64 pixelBytes = 8 * 1024 * 1024;
        int dimension = 1024;
    };
    static QImage decode(const QByteArray&, Profile, const Limits&, QString* error = nullptr);
    static QByteArray format(const QByteArray&);
};
}
