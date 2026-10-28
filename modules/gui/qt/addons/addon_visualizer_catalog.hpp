// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_ui_assets.hpp"
#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <atomic>

namespace vlc::addons {
class AddonManager;
class AddonCatalogueView;
class AddonVisualizerCatalog : public QObject
{
    Q_OBJECT
public:
    explicit AddonVisualizerCatalog(AddonManager*, QObject* parent = nullptr);
    ~AddonVisualizerCatalog() override;
    QVariantList choices() const { return m_choices; }
    bool loading() const;
    bool current() const;
    QString partition(const QString&) const;
    void reload();
signals:
    void changed();
private:
    static AddonUiAssets::Backend backend(AddonManager*, const std::shared_ptr<std::atomic<quint64>>&);
    void completed();
    QPointer<AddonManager> m_manager;
    QPointer<AddonCatalogueView> m_display;
    const bool m_displayMode;
    std::shared_ptr<std::atomic<quint64>> m_epoch;
    AddonUiAssets* m_assets = nullptr;
    QString m_session;
    QVariantList m_choices;
};
}
