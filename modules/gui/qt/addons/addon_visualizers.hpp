// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_manager.hpp"
#include "addon_visual_scene.hpp"
#include "addon_visualizer_render_queue.hpp"
#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>
#include <functional>

namespace vlc::addons {
class AddonCatalogueView;
class AddonVisualizerCatalog;
class AddonVisualizerResources;
class AddonVisualizerWorker;
struct AddonVisualizerActivation;
struct VisualizerIdleTestAccess;
class AddonVisualizers : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList availableVisualizers READ availableVisualizers NOTIFY changed FINAL)
    Q_PROPERTY(QString selectedVisualizer READ selectedVisualizer NOTIFY changed FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed FINAL)
    Q_PROPERTY(bool active READ active NOTIFY changed FINAL)
    Q_PROPERTY(int quality READ quality NOTIFY changed FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY changed FINAL)
public:
    struct Snapshot { std::uint64_t epoch = 0; qint64 timeUs = -1; double rate = 1; bool playing = false; };
    struct Backend {
        std::function<Snapshot()> snapshot;
        std::function<bool(AddonAudioAnalysis*, std::uint64_t)> startAnalysis;
        // Stop waits for the last audio callback. The callback does not use Qt.
        std::function<void()> stopAnalysis;
        // Optional owner-thread clock/state observation. False is idempotent
        // and drains observation. A refused true call leaves no observation.
        std::function<bool(bool)> setActivity;
    };
    AddonVisualizers(AddonManager*, Backend, QObject* parent = nullptr, AddonVisualizerRenderQueue::Render = {});
    ~AddonVisualizers() override;
    QVariantList availableVisualizers() const;
    QString selectedVisualizer() const { return m_selected; }
    QString errorMessage() const { return m_error; }
    bool active() const;
    bool loading() const;
    int quality() const { return m_quality; }
    QImage image() const { return m_image; }
    Q_INVOKABLE bool selectVisualizer(const QString&);
    void setSurface(QObject* owner, bool visible, const QSize&);
    void setColors(const AddonVisualScene::Colors&);
    void setReducedMotion(bool);
    void invalidateTimeline();
    // Host player event; clears a retained frame even while observation is paused.
    void mediaChanged();
    void refresh();
    void close();
    QVariantMap statistics() const;
signals:
    void changed();
    void imageChanged();
private:
    friend struct VisualizerIdleTestAccess;
    void reload();
    bool stop(bool notify = true, bool keepImage = false);
    void displayChanged();
    void pauseDisplay();
    void loadDisplaySelection();
    void displayWithdrawn(const QString&);
    bool closeResources();
    void clearImage();
    void resourcesChanged();
    void rendered(const QImage&, qint64, quint64, qint64, quint64, int, qint64);
    void startParameterWorker();
    bool persist(const QString&);
    bool observationEligible() const;
    bool syncActivity(bool retry = false);
    QPointer<AddonManager> m_manager;
    QPointer<AddonCatalogueView> m_display;
    const bool m_displayMode;
    Backend m_backend;
    AddonAudioAnalysis m_analysis;
    AddonVisualizerCatalog* m_catalog;
    AddonVisualizerResources* m_resources;
    AddonVisualizerRenderQueue* m_renderQueue;
    std::unique_ptr<AddonVisualizerWorker> m_parameters;
    std::shared_ptr<const AddonVisualizerActivation> m_activation;
    QJsonObject m_scene;
    QMap<QString, QImage> m_assets;
    QImage m_image;
    QPointer<QObject> m_surface;
    QMetaObject::Connection m_surfaceDestroyed;
    QTimer m_timer;
    QElapsedTimer m_retry;
    AddonVisualScene::Colors m_colors{Qt::black, Qt::white, QColor(211, 121, 24)};
    QString m_selected, m_requested, m_error, m_selectedPartition;
    QSize m_size;
    std::uint64_t m_generation = 1, m_mediaEpoch = 0, m_frameSequence = 0;
    qint64 m_mediaTime = -1;
    int m_quality = 1, m_slowFrames = 0;
    std::uint64_t m_activitySerial = 0, m_surfaceSerial = 0;
    QList<int> m_qualityTiers;
    bool m_subscribed = false, m_visible = false, m_reducedMotion = false, m_dirty = true, m_closing = false, m_selectionPending = false;
    bool m_backendActive = false, m_activityBlocked = false;
    bool m_preferencesLoaded = false, m_displayPaused = false, m_retainedFrame = false, m_reloadPending = false, m_resettingResources = false;
};
}
