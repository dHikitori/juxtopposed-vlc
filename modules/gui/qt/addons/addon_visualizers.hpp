// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_manager.hpp"
#include "addon_visual_scene.hpp"
#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>
#include <functional>

namespace vlc::addons {
class AddonVisualizers : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList availableVisualizers READ availableVisualizers NOTIFY changed FINAL)
    Q_PROPERTY(QString selectedVisualizer READ selectedVisualizer NOTIFY changed FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed FINAL)
    Q_PROPERTY(bool active READ active NOTIFY changed FINAL)
    Q_PROPERTY(int quality READ quality NOTIFY changed FINAL)
public:
    struct Snapshot { std::uint64_t epoch = 0; qint64 timeUs = -1; double rate = 1; bool playing = false; };
    struct Backend {
        std::function<Snapshot()> snapshot;
        std::function<bool(AddonAudioAnalysis*, std::uint64_t)> startAnalysis;
        // Stop waits for the last audio callback. The callback does not use Qt.
        std::function<void()> stopAnalysis;
    };
    AddonVisualizers(AddonManager*, Backend, QObject* parent = nullptr);
    ~AddonVisualizers() override;
    QVariantList availableVisualizers() const;
    QString selectedVisualizer() const { return m_selected; }
    QString errorMessage() const { return m_error; }
    bool active() const { return m_subscribed && !m_scene.isEmpty() && !m_image.isNull(); }
    int quality() const { return m_quality; }
    QImage image() const { return m_image; }
    Q_INVOKABLE bool selectVisualizer(const QString&);
    void setSurface(QObject* owner, bool visible, const QSize&);
    void setColors(const AddonVisualScene::Colors&);
    void setReducedMotion(bool);
    void invalidateTimeline();
    void refresh();
    void close();
signals:
    void changed();
    void imageChanged();
private:
    void reload();
    void stop();
    bool available(const QVariantMap&) const;
    QPointer<AddonManager> m_manager;
    Backend m_backend;
    AddonAudioAnalysis m_analysis;
    AddonVisualScene m_renderer;
    QJsonObject m_scene;
    QMap<QString, QImage> m_assets;
    QImage m_image;
    QPointer<QObject> m_surface;
    QMetaObject::Connection m_surfaceDestroyed;
    QTimer m_timer;
    QElapsedTimer m_retry, m_renderClock;
    AddonVisualScene::Colors m_colors{Qt::black, Qt::white, QColor(211, 121, 24)};
    QString m_selected, m_error;
    QSize m_size;
    std::uint64_t m_generation = 1, m_mediaEpoch = 0, m_frameSequence = 0;
    qint64 m_mediaTime = -1;
    int m_quality = 1, m_slowFrames = 0;
    bool m_subscribed = false, m_visible = false, m_reducedMotion = false, m_dirty = true, m_closing = false;
};
}
