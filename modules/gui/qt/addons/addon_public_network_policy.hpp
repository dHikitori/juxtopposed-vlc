// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QHostAddress>
namespace vlc::addons {
class AddonPublicNetworkPolicy final
{
public:
    static bool address(const QHostAddress&);
};
}
