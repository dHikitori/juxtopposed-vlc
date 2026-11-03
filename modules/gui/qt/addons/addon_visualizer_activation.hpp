// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_package_store.hpp"
#include <functional>
namespace vlc::addons {
// Native capture data. No QObject or credential bytes enter this value.
struct AddonVisualizerActivation {
    InstalledPackage package;
    QJsonObject scene, settings, secretStatus;
    QByteArray runtime;
    QString executable, account, key;
    quint64 settingsRevision = 0;
    // current can read files on a worker. live reads memory only.
    std::function<bool()> current, live;
};
}
