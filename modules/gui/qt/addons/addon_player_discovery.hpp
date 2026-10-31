// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_queue_transactions.hpp"
struct input_item_t;
namespace vlc::addons {
class AddonManager;
class AddonPlayableGate;
class AddonDiscoverySourceOwner;
class AddonDiscoveryService;
struct AddonDiscoveryNativeBackend {
    bool stableReferencePlaybackAvailable = false;
    bool libraryAppendAvailable = false;
    std::function<AddonQueueTransactions::Snapshot(const QString& target)> snapshot;
    std::function<AddonQueueTransactions::Cancel(const AddonQueueTransactions::Transaction&, AddonQueueTransactions::Completion)> append;
    // The consumer does the final target identity/revision check under its write
    // lock. It retains the input item if the queue or player will use it later.
    std::function<bool(const AddonQueueTransactions::Transaction&, input_item_t*, QString* error)> play;
    std::function<AddonQueueTransactions::Cancel(const AddonQueueTransactions::Transaction&, AddonQueueTransactions::Completion)> playReference;
    std::function<int(const QString& fillTag)> pendingAhead;
};
AddonDiscoveryNativeBackend combineDiscoveryBackends(AddonDiscoveryNativeBackend queue, AddonDiscoveryNativeBackend library);
AddonDiscoveryService* createPlayerDiscovery(AddonManager*, AddonPlayableGate*, AddonDiscoveryNativeBackend, QObject* parent);
AddonDiscoveryService* createPlayerDiscoveryWithSources(AddonManager*, AddonDiscoverySourceOwner*, AddonDiscoveryNativeBackend, QObject* parent);
// Call on the manager thread. Admission does not change source activation.
bool discoveryReferenceAvailable(AddonManager*, const QVariantMap& reference);
}
