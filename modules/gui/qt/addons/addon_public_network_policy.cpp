// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_public_network_policy.hpp"
#include <QList>

namespace vlc::addons {
bool AddonPublicNetworkPolicy::address(const QHostAddress& address)
{
    if (address.isNull() || address.isLoopback() || address.isMulticast() || !address.scopeId().isEmpty()) return false;
    if (address.protocol() == QAbstractSocket::IPv4Protocol) {
        const quint32 value = address.toIPv4Address();
        static const QList<QPair<quint32, int>> denied{
            {0x00000000,8}, {0x0a000000,8}, {0x64400000,10}, {0x7f000000,8},
            {0xa9fe0000,16}, {0xac100000,12}, {0xc0000000,24}, {0xc0000200,24},
            {0xc0586300,24}, {0xc0a80000,16}, {0xc6120000,15}, {0xc6336400,24},
            {0xcb007100,24}, {0xe0000000,4}, {0xf0000000,4}
        };
        for (const auto& range : denied) if ((value & (0xffffffffu << (32 - range.second))) == range.first) return false;
        return true;
    }
    return address.protocol() == QAbstractSocket::IPv6Protocol
        && address.isInSubnet(QHostAddress(QStringLiteral("2000::")), 3)
        && !address.isInSubnet(QHostAddress(QStringLiteral("2001::")), 23)
        && !address.isInSubnet(QHostAddress(QStringLiteral("2001:db8::")), 32)
        && !address.isInSubnet(QHostAddress(QStringLiteral("2002::")), 16)
        && !address.isInSubnet(QHostAddress(QStringLiteral("3fff::")), 20);
}
}
