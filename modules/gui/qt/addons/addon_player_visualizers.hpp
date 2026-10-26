// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
class QObject;
class PlayerController;
namespace vlc::addons {
class AddonManager;
class AddonVisualizers;
AddonVisualizers* createPlayerVisualizers(AddonManager*, PlayerController*, QObject* parent);
}
