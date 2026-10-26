// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_player_discovery.hpp"
#include "addon_discovery_service.hpp"
#include "addon_playable_gate.hpp"
#include <QPointer>

namespace vlc::addons {
bool discoveryReferenceAvailable(AddonManager* manager, const QVariantMap& reference)
{
    if (!manager || manager->safeMode() || !AddonQueueTransactions::validReference(reference)) return false;
    const auto source = reference.value("source").toString(); const auto package = source.section('/', 0, 0);
    bool configured = false;
    for (const auto& value : manager->packages()) {
        const auto row = value.toMap(); if (row.value("id").toString() == package)
            configured = row.value("enabled").toBool() && row.value("configured").toBool() && !row.value("recoveryRequired").toBool();
    }
    if (!configured) return false;
    for (const auto& value : manager->contributionsOfType(QStringLiteral("discovery.source"))) {
        const auto row = value.toMap();
        const auto account = row.value("accountReference").toString();
        if (row.value("id").toString() == source && !account.isEmpty() && account == reference.value("accountReference").toString()) return true;
    }
    return false;
}
AddonDiscoveryService* createPlayerDiscovery(AddonManager* manager, AddonPlayableGate* gate,
                                            AddonDiscoveryNativeBackend native, QObject* parent)
{
    const QPointer<AddonPlayableGate> guardedGate(gate);
    AddonDiscoveryCoordinator::Backend discovery;
    if (gate && gate->supported()) {
        discovery.authorizePlayable = [guardedGate](const QString& source, const QVariantMap& proposal, auto completion) {
            if (!guardedGate || !guardedGate->supported()) { completion({}, QObject::tr("Discovery playback is not available.")); return QString(); }
            return guardedGate->authorize(source, proposal, std::move(completion));
        };
        discovery.cancelPlayable = [guardedGate](const QString& request) { if (guardedGate) guardedGate->cancel(request); };
    }
    AddonQueueTransactions::Backend queues;
    const bool playableReferences = gate && gate->supported() && native.stableReferencePlaybackAvailable;
    queues.snapshot = std::move(native.snapshot);
    if (playableReferences) queues.append = std::move(native.append);
    queues.releasePlayable = [guardedGate](const QString& handle) { if (guardedGate) guardedGate->release(handle); };
    if (playableReferences && native.play) queues.play = [guardedGate, consume = std::move(native.play)](const AddonQueueTransactions::Transaction& transaction, auto completion) {
        QString error;
        const bool success = guardedGate && guardedGate->consume(transaction.playbackHandle, QStringLiteral("play"), [&](input_item_t* item) {
            return consume(transaction, item, &error);
        });
        completion(success, success ? QString() : error.isEmpty() ? QObject::tr("The app cannot play this discovery item.") : error);
        return AddonQueueTransactions::Cancel{};
    };
    return new AddonDiscoveryService(manager, std::move(discovery), std::move(queues), parent);
}
}
