// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_ui_assets.hpp"
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <atomic>

namespace vlc::addons {
class AddonManager;
class AddonVisualizerCatalog : public QObject
{
    Q_OBJECT
public:
    explicit AddonVisualizerCatalog(AddonManager*, QObject* parent = nullptr);
    ~AddonVisualizerCatalog() override;
    QVariantList choices() const { return m_choices; }
    bool loading() const;
    void reload();
signals:
    void changed();
private:
    static AddonUiAssets::Backend backend(AddonManager*, const std::shared_ptr<std::atomic<quint64>>&);
    void completed();
    QPointer<AddonManager> m_manager;
    std::shared_ptr<std::atomic<quint64>> m_epoch;
    AddonUiAssets* m_assets;
    QString m_session;
    QVariantList m_choices;
};
}
