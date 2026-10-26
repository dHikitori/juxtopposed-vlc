// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_package_store.hpp"
#include "addon_host_services.hpp"
#include <atomic>
#include <memory>
#include <mutex>

namespace vlc::addons {
struct PrivateStorageContext {
    InstalledPackage package;
    QJsonObject settings;
    quint64 settingsRevision = 0;
    QString accountReference;
};
class AddonStorage final : public QObject
{
public:
    using Capture = std::function<bool(const AddonHostServices::Context&, PrivateStorageContext*, QString*)>;
    AddonStorage(PackageStore, Capture, QObject* parent = nullptr);
    ~AddonStorage() override;
    bool mount(AddonHostServices*);
    void close();
private:
    struct State {
        std::atomic_bool closed{false};
        std::mutex mutex;
        QList<std::weak_ptr<std::atomic_int>> jobs;
    };
    AddonHostServices::Cancel start(const AddonHostServices::Context&, const QString&, const QJsonObject&, AddonHostServices::Completion);
    PackageStore m_store;
    Capture m_capture;
    std::shared_ptr<State> m_state;
};
}
