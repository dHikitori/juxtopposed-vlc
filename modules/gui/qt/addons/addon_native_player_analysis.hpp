// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_visualizers.hpp"
#include <memory>

struct vlc_player_t;
namespace vlc::addons {
class AddonNativePlayerAnalysis : public std::enable_shared_from_this<AddonNativePlayerAnalysis>
{
public:
    // Create, backend control, statistics, and close use one owner thread.
    // The owner closes this bridge before it destroys the native player.
    static std::shared_ptr<AddonNativePlayerAnalysis> create(vlc_player_t*);
    ~AddonNativePlayerAnalysis();
    AddonVisualizers::Backend backend();
    QVariantMap statistics() const;
    void close();
private:
    explicit AddonNativePlayerAnalysis(vlc_player_t*);
    struct State;
    std::unique_ptr<State> m_state;
};
}
