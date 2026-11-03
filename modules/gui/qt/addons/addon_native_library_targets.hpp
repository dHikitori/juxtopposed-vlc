// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_native_library_store.hpp"
#include "addon_discovery_service.hpp"
#include <atomic>

namespace vlc::addons {
// The core picker owns native IDs. The UI receives only choice tokens and names.
class AddonNativeLibraryTargets final : public QObject
{
    Q_OBJECT
public:
    AddonNativeLibraryTargets(AddonNativeLibraryStore::Dispatcher, AddonNativeLibraryStore*, QObject* parent = nullptr);
    ~AddonNativeLibraryTargets() override;
    AddonDiscoveryService::PlaylistBackend backend();
    void close();
private:
    struct Listing;
    AddonQueueTransactions::Cancel list(const QString&, AddonDiscoveryService::PlaylistBackend::Choices);
    AddonQueueTransactions::Cancel prepare(const QString&, AddonDiscoveryService::PlaylistBackend::Prepared);
    void listed(const std::shared_ptr<Listing>&);
    AddonNativeLibraryStore::Dispatcher m_dispatcher;
    QPointer<QObject> m_lifetime;
    QPointer<AddonNativeLibraryStore> m_store;
    std::shared_ptr<std::atomic_bool> m_closed = std::make_shared<std::atomic_bool>(false);
    QHash<quint64, std::shared_ptr<Listing>> m_listings;
    QHash<QString, std::shared_ptr<vlc_addon_playlist_snapshot>> m_choices;
    quint64 m_next = 0, m_version = 0;
};
}
