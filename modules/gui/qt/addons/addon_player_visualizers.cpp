// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <vlc_common.h>
#include "addon_player_visualizers.hpp"
#include "addon_native_player_analysis.hpp"
#include "player/player_controller.hpp"

namespace vlc::addons {
AddonVisualizers* createPlayerVisualizers(AddonManager* manager, PlayerController* player, QObject* parent)
{
    if (!player) return new AddonVisualizers(manager, {}, parent);
    const auto analysis = AddonNativePlayerAnalysis::create(player->getPlayer());
    auto* result = new AddonVisualizers(manager, analysis->backend(), parent);
    QObject::connect(player, &QObject::destroyed, result, [analysis, result] { result->close(); analysis->close(); });
    return result;
}
}
