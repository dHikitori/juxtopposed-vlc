// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_package_store.hpp"
#include "addon_network.hpp"
#include <functional>
#include <optional>

namespace vlc::addons {
// Native host data only. Do not expose this value or its readers to a guest.
struct AddonNativeAuthority {
    InstalledPackage package;
    Contribution contribution;
    QString globalId, accountReference;
    QJsonObject settings;
    quint64 settingsRevision = 0;
    NetworkBroker::CredentialResolver credentials;
    // The live check uses memory only. The current check also reads saved state.
    NetworkBroker::RequestGuard live, current;
    // Register native-only cancellation before admitting a native lease.
    std::function<StoreAuthorityWatch(std::function<void()>)> onInvalidated;
};
// Invoke this reader on joined background work. It owns no QObject or Core.
using AddonNativeAuthorityReader = std::function<std::optional<AddonNativeAuthority>()>;
}
