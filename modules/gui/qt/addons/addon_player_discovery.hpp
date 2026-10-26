// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_queue_transactions.hpp"
struct input_item_t;
namespace vlc::addons {
class AddonManager;
class AddonPlayableGate;
class AddonDiscoveryService;
struct AddonDiscoveryNativeBackend {
    bool stableReferencePlaybackAvailable = false;
    std::function<AddonQueueTransactions::Snapshot(const QString& target)> snapshot;
    std::function<AddonQueueTransactions::Cancel(const AddonQueueTransactions::Transaction&, AddonQueueTransactions::Completion)> append;
    // The consumer does the final target identity/revision check under its write
    // lock. It retains the input item if the queue or player will use it later.
    std::function<bool(const AddonQueueTransactions::Transaction&, input_item_t*, QString* error)> play;
};
AddonDiscoveryService* createPlayerDiscovery(AddonManager*, AddonPlayableGate*, AddonDiscoveryNativeBackend, QObject* parent);
// Call on the manager thread. Admission does not change source activation.
bool discoveryReferenceAvailable(AddonManager*, const QVariantMap& reference);
}
