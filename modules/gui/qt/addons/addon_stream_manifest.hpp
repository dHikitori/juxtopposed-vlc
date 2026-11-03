// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QByteArray>
#include <QList>
#include <QStringList>
#include <QUrl>
#include <functional>
#include <optional>

namespace vlc::addons {
class AddonStreamManifest
{
public:
    enum class Kind { Playlist, Media, Initialization, Key, SessionData };
    struct Range { quint64 offset, length; };
    struct Resource { QUrl url; Kind kind; std::optional<Range> range; };
    struct Result { QByteArray bytes; QString error; bool valid() const { return error.isEmpty() && !bytes.isEmpty(); } };
    // This callback registers a complete batch or no resources. It must check
    // the source authority and return one opaque URL per entry. Parsing does
    // not grant network access or authorize DNS, redirects, or byte transfers.
    using Register = std::function<QStringList(const QList<Resource>&)>;
    static Result hls(const QByteArray&, const QUrl& upstream, const QUrl& proxyPrefix, const Register&);
    // Static MPDs with explicit representation SegmentList or SegmentBase.
    // Templates, remote clocks, protection, and external XML links are refused.
    static Result dash(const QByteArray&, const QUrl& upstream, const QUrl& proxyPrefix, const Register&);
};
}
