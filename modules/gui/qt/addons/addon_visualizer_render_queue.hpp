// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_visual_scene.hpp"
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <functional>
#include <memory>
#include <optional>
namespace vlc::addons {
class AddonVisualizerRenderQueue : public QObject
{
    Q_OBJECT
public:
    // Control methods use the owner thread. Render runs on a bounded worker.
    struct Request {
        QJsonObject scene;
        AddonAudioAnalysis::Frame analysis;
        qint64 mediaTimeUs = 0;
        QSize size;
        AddonVisualScene::Colors colors;
        int quality = 1;
        bool reducedMotion = false;
        std::function<bool()> current;
        QMap<QString, QImage> assets;
    };
    using Render = std::function<QImage(AddonVisualScene&, const Request&)>;
    explicit AddonVisualizerRenderQueue(Render = {}, QObject* parent = nullptr);
    ~AddonVisualizerRenderQueue() override;
    bool submit(Request);
    void invalidate();
    void close();
    QVariantMap statistics() const;
    static int liveJobs();
signals:
    void frameReady(const QImage&, qint64 costNs, quint64 generation, qint64 mediaTimeUs, quint64 sequence, int quality, qint64 analysisTimeUs);
private:
    struct Shared;
    struct Work { Request request; quint64 epoch = 0; };
    void pump();
    void finished(Work, QImage, qint64 costNs);
    std::shared_ptr<Shared> m_shared;
    Render m_render;
    std::optional<Work> m_latest;
    QTimer m_timer;
    bool m_running = false, m_closed = false;
    quint64 m_rendered = 0, m_replaced = 0;
    qint64 m_maximumCostNs = 0;
};
}
