// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QHostAddress>
#include <QUrl>
#include <optional>
namespace vlc::addons {
class AddonStreamPolicy final
{
public:
    struct Plan { QHostAddress address; QString peerName; QByteArray host, target; };
    static bool url(const QUrl&);
    static bool publicAddress(const QHostAddress&);
    static std::optional<Plan> plan(const QUrl&, const QList<QHostAddress>&);
    static QByteArray origin(const QUrl&);
};
}
