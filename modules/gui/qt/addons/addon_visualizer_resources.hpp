// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_ui_assets.hpp"
#include "addon_visualizer_activation.hpp"
#include <QObject>
#include <QPointer>

namespace vlc::addons {
class AddonManager;
class AddonVisualizerResources : public QObject
{
    Q_OBJECT
public:
    explicit AddonVisualizerResources(AddonManager*, QObject* parent = nullptr);
    ~AddonVisualizerResources() override;
    bool load(const QString& contribution, QObject* surface = nullptr);
    bool setSurface(QObject* surface);
    void close();
    bool valid() const { return m_prepared && m_prepared->live && m_prepared->live(); }
    bool ready() const { return valid() && !m_claim.isEmpty(); }
    bool loading() const;
    QString errorMessage() const { return m_error; }
    std::shared_ptr<const AddonVisualizerActivation> activation() const { return valid() ? m_prepared : nullptr; }
    QMap<QString, QImage> images() const { return ready() ? m_images : QMap<QString, QImage>{}; }
signals:
    void changed();
private:
    struct Shared;
    static AddonUiAssets::Backend backend(AddonManager*, const std::shared_ptr<Shared>&);
    void refresh();
    void clear(bool notify);
    QPointer<AddonManager> m_manager;
    QPointer<QObject> m_surface;
    QMetaObject::Connection m_surfaceDestroyed;
    std::shared_ptr<Shared> m_shared;
    AddonUiAssets* m_assets;
    std::shared_ptr<const AddonVisualizerActivation> m_prepared;
    QMap<QString, QImage> m_images;
    QString m_session, m_claim, m_error;
};
}
