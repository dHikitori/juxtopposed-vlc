// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_visualizers.hpp"
#include "addon_catalogue_view.hpp"
#include "addon_visualizer_catalog.hpp"
#include "addon_visualizer_resources.hpp"
#include "addon_visualizer_parameters.hpp"
#include "addon_visualizer_worker.hpp"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <cmath>

namespace vlc::addons {
AddonVisualizers::AddonVisualizers(AddonManager* manager, Backend backend, QObject* parent, AddonVisualizerRenderQueue::Render render)
    : QObject(parent), m_manager(manager), m_display(AddonCatalogueView::attached(manager)),
      m_displayMode(bool(m_display)), m_backend(std::move(backend)),
      m_catalog(new AddonVisualizerCatalog(manager, this)), m_resources(new AddonVisualizerResources(manager, this)),
      m_renderQueue(new AddonVisualizerRenderQueue(std::move(render), this))
{
    if (m_displayMode) {
        connect(m_display, &AddonCatalogueView::catalogueChanged, this, &AddonVisualizers::displayChanged);
        connect(m_display, &AddonCatalogueView::freshnessChanged, this, &AddonVisualizers::displayChanged);
        connect(m_display, &QObject::destroyed, this, &AddonVisualizers::close);
    } else if (manager) {
        QFile file(QDir(manager->storageDirectory()).filePath(QStringLiteral("visualizers.json")));
        if (file.size() <= 8192 && file.open(QIODevice::ReadOnly)) m_selected = QJsonDocument::fromJson(file.read(8192)).object().value("selected").toString().left(256);
        connect(manager, &AddonManager::revisionChanged, this, &AddonVisualizers::reload);
    }
    if (manager) {
        connect(manager, &QObject::destroyed, this, &AddonVisualizers::close);
        if (m_displayMode) connect(manager, &AddonManager::packageWithdrawn, this, &AddonVisualizers::displayWithdrawn);
    }
    connect(m_catalog, &AddonVisualizerCatalog::changed, this, &AddonVisualizers::changed);
    connect(m_resources, &AddonVisualizerResources::changed, this, &AddonVisualizers::resourcesChanged);
    connect(m_renderQueue, &AddonVisualizerRenderQueue::frameReady, this, &AddonVisualizers::rendered);
    m_timer.setInterval(34); connect(&m_timer, &QTimer::timeout, this, &AddonVisualizers::refresh);
    m_retry.start(); if (m_displayMode) displayChanged(); else reload();
}
AddonVisualizers::~AddonVisualizers() { close(); }
void AddonVisualizers::close()
{
    if (m_closing) return;
    m_closing = true; ++m_surfaceSerial; m_timer.stop();
    disconnect(m_surfaceDestroyed); m_surface = nullptr; m_visible = false;
    // Retire native observation before any other callback can destroy us.
    if (!syncActivity() || !stop(false)) return;
    disconnect(m_resources, nullptr, this, nullptr); disconnect(m_catalog, nullptr, this, nullptr);
    const QPointer<AddonVisualizers> alive(this);
    m_resources->close(); if (!alive) return;
    m_renderQueue->close(); m_scene = {}; m_activation.reset(); m_retainedFrame = false;
}
bool AddonVisualizers::active() const
{ return !m_closing && m_visible && !m_image.isNull()
    && (m_retainedFrame || (m_subscribed && m_resources->ready() && !m_scene.isEmpty())); }
bool AddonVisualizers::loading() const { return m_selectionPending || m_resources->loading(); }
bool AddonVisualizers::observationEligible() const
{
    return !m_closing && m_visible && !m_selected.isEmpty() && !m_requested.isEmpty()
        && !m_displayPaused && m_activation && !m_scene.isEmpty() && m_resources->ready()
        && m_backend.snapshot && m_backend.startAnalysis && m_backend.stopAnalysis;
}
bool AddonVisualizers::syncActivity(bool retry)
{
    const bool wasClosing = m_closing;
    if (retry) m_activityBlocked = false;
    const bool wanted = observationEligible() && !m_activityBlocked;
    if (!wanted) m_timer.stop();
    if (wanted == m_backendActive) {
        if (wanted && !m_timer.isActive()) m_timer.start();
        return true;
    }
    auto activity = m_backend.setActivity;
    const auto serial = ++m_activitySerial;
    // Close can unwind a callback that reenters or destroys this controller.
    m_backendActive = wanted;
    const QPointer<AddonVisualizers> alive(this);
    const bool accepted = !activity || activity(wanted);
    if (!alive) {
        if (wanted && accepted && activity) activity(false);
        return false;
    }
    if (serial != m_activitySerial) {
        if (wanted && accepted && (!m_backendActive || m_closing) && activity) activity(false);
        activity = {};
        return false;
    }
    const bool refused = wanted && !accepted;
    if (refused) {
        m_backendActive = false; m_activityBlocked = true; m_timer.stop();
        m_error = tr("The audio visualizer is not available. The app shows the artwork.");
    } else if (wanted) m_timer.start();
    // End the copied capture before the owner guard and any later field read.
    activity = {};
    if (!alive || serial != m_activitySerial || m_closing != wasClosing) return false;
    if (refused) { emit changed(); return alive && serial == m_activitySerial && m_closing == wasClosing; }
    return true;
}
QVariantList AddonVisualizers::availableVisualizers() const
{
    QVariantList result{QVariantMap{{"id", ""}, {"name", tr("Artwork")}, {"enabled", true}}};
    if (!m_closing) result.append(m_catalog->choices());
    return result;
}
bool AddonVisualizers::persist(const QString& id)
{
    if (!m_manager) return false;
    QSaveFile file(QDir(m_manager->storageDirectory()).filePath(QStringLiteral("visualizers.json")));
    const auto bytes = QJsonDocument(QJsonObject{{"selected", id}}).toJson(QJsonDocument::Compact);
    if (!QDir().mkpath(m_manager->storageDirectory()) || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        m_error = tr("The app cannot save the visualizer selection."); return false;
    }
    return true;
}
bool AddonVisualizers::selectVisualizer(const QString& id)
{
    if (m_closing || !m_manager || (!id.isEmpty() && !m_catalog->current())) return false;
    if (!id.isEmpty()) {
        bool found = false;
        for (const auto& value : m_catalog->choices()) {
            const auto entry = value.toMap(); if (entry.value("id").toString() == id && entry.value("enabled").toBool()) { found = true; break; }
        }
        if (!found) return false;
    }
    // An explicit host choice owns the preference before stop/load callbacks.
    // A delayed initial catalogue must not restore an earlier saved selection.
    m_preferencesLoaded = true;
    QPointer<AddonVisualizers> guard(this); if (!stop()) return false;
    m_scene = {}; m_assets.clear(); m_activation.reset(); m_retainedFrame = false; m_displayPaused = false;
    m_selectedPartition = m_catalog->partition(id); m_reloadPending = false;
    m_error.clear(); m_requested = id; m_selectionPending = !id.isEmpty();
    if (!syncActivity(true)) return false;
    if (id.isEmpty()) {
        m_resources->close(); if (!guard) return false;
        if (!persist(id)) { emit changed(); return false; }
        m_selected.clear(); m_timer.stop(); emit changed(); return true;
    }
    const bool started = m_resources->load(id, m_visible ? m_surface.data() : nullptr);
    if (!guard) return false;
    if (!started) { m_selectionPending = false; m_error = tr("The app cannot load this visualizer. The app shows the artwork."); emit changed(); return false; }
    emit changed(); return true;
}
void AddonVisualizers::clearImage()
{
    m_retainedFrame = false; m_dirty = true;
    if (m_image.isNull()) return;
    m_renderQueue->invalidate();
    m_image = {}; QPointer<AddonVisualizers> guard(this); emit imageChanged(); if (guard) emit changed();
}
bool AddonVisualizers::stop(bool notify, bool keepImage)
{
    const bool subscribed = m_subscribed; m_subscribed = false;
    m_analysis.setActive(false); m_analysis.reset(++m_generation); m_renderQueue->invalidate(); m_parameters.reset();
    if (!keepImage) { m_image = {}; m_retainedFrame = false; }
    m_frameSequence = 0; m_mediaTime = -1; m_dirty = true;
    const auto generation = m_generation; const auto stopAnalysis = m_backend.stopAnalysis; QPointer<AddonVisualizers> guard(this);
    if (subscribed && stopAnalysis) stopAnalysis();
    if (!guard || generation != m_generation) return false;
    if (!notify) return true;
    emit imageChanged(); if (!guard || generation != m_generation) return false;
    emit changed(); return guard && generation == m_generation;
}
bool AddonVisualizers::closeResources()
{
    if (m_resettingResources) return true;
    m_resettingResources = true;
    const QPointer<AddonVisualizers> alive(this);
    m_resources->close();
    if (!alive) return false;
    m_resettingResources = false;
    return true;
}
void AddonVisualizers::pauseDisplay()
{
    if (m_closing || m_displayPaused) return;
    m_displayPaused = true; m_reloadPending = true;
    m_retainedFrame = !m_image.isNull() && !m_scene.isEmpty();
    // Only immutable presentation survives. No activation, decoded-asset claim,
    // parameter worker, sample callback, render job, or native observer does.
    m_activation.reset(); m_assets.clear();
    const QPointer<AddonVisualizers> alive(this);
    if (!stop(false, m_retainedFrame)) return;
    const auto generation = m_generation;
    if (!syncActivity() || !alive || m_closing || !m_displayPaused || generation != m_generation) return;
    if (!closeResources()) return;
    if (alive && !m_closing && m_displayPaused && generation == m_generation) emit changed();
}
void AddonVisualizers::displayChanged()
{
    if (m_closing || !m_displayMode) return;
    if (!m_display) { close(); return; }
    // A document opened elsewhere does not invalidate this activation.
    if (!m_display->retainedCatalogueCurrent()) { pauseDisplay(); return; }
    if (!m_display->isCurrent()) return;
    if (!m_preferencesLoaded) {
        m_preferencesLoaded = true;
        m_selected = m_display->preferences(QStringLiteral("visualizers")).value(QStringLiteral("selected")).toString().left(256);
        m_requested = m_selected; m_reloadPending = !m_selected.isEmpty();
    }
    if (m_selected.isEmpty()) { emit changed(); return; }
    const auto partition = m_catalog->partition(m_selected);
    if (partition.isEmpty() || (!m_selectedPartition.isEmpty() && m_selectedPartition != partition)) {
        const QPointer<AddonVisualizers> alive(this);
        m_displayPaused = false; m_retainedFrame = false; m_reloadPending = !partition.isEmpty();
        if (!stop()) return;
        m_scene = {}; m_assets.clear(); m_activation.reset();
        const auto generation = m_generation;
        if (!closeResources() || !alive || m_closing || generation != m_generation) return;
        m_selectedPartition = partition;
        if (partition.isEmpty()) { m_error = tr("The visualizer is not available. The app shows the artwork."); emit changed(); return; }
    }
    if (m_reloadPending && m_visible) loadDisplaySelection();
}
void AddonVisualizers::loadDisplaySelection()
{
    if (m_closing || !m_display || !m_display->isCurrent() || !m_visible || m_selected.isEmpty()) return;
    const auto partition = m_catalog->partition(m_selected);
    if (partition.isEmpty()) return;
    m_selectedPartition = partition; m_requested = m_selected; m_reloadPending = false;
    // Keep the old picture until this exact partition has a new activation.
    const QPointer<AddonVisualizers> alive(this);
    if (!m_resources->load(m_selected, m_surface.data()) && alive) {
        m_displayPaused = false; m_retainedFrame = false;
        if (!stop()) return;
        m_scene = {}; m_error = tr("The visualizer is not available. The app shows the artwork."); emit changed();
    }
}
void AddonVisualizers::displayWithdrawn(const QString& package)
{
    if (m_closing || (m_selected.section('/', 0, 0) != package && m_requested.section('/', 0, 0) != package)) return;
    m_retainedFrame = false; m_selectedPartition.clear(); m_reloadPending = true;
    if (!stop()) return;
    m_scene = {}; m_assets.clear(); m_activation.reset();
    const auto generation = m_generation;
    if (!syncActivity() || m_closing || generation != m_generation) return;
    closeResources();
}
void AddonVisualizers::reload()
{
    if (m_closing) return;
    QPointer<AddonVisualizers> guard(this); if (!stop()) return;
    m_scene = {}; m_assets.clear(); m_activation.reset(); m_selectionPending = false; m_requested = m_selected; m_error.clear();
    if (!syncActivity(true)) return;
    if (!m_selected.isEmpty()) {
        const bool started = m_resources->load(m_selected, m_visible ? m_surface.data() : nullptr);
        if (!guard) return;
        if (!started) m_error = tr("The visualizer is not available. The app shows the artwork.");
    } else { m_resources->close(); if (!guard) return; }
    if (!syncActivity()) return;
    emit changed();
}
void AddonVisualizers::resourcesChanged()
{
    if (m_closing || m_resettingResources) return;
    if (m_displayMode && (!m_display || !m_display->retainedCatalogueCurrent())) { pauseDisplay(); return; }
    if (m_resources->loading()) {
        if (!stop(false, m_retainedFrame) || !syncActivity(true)) return;
        emit changed(); return;
    }
    const auto prepared = m_resources->activation(); QPointer<AddonVisualizers> guard(this);
    if (!prepared) {
        if (!stop()) return;
        m_scene = {}; m_assets.clear(); m_activation.reset(); m_selectionPending = false;
        if (!syncActivity()) return;
        m_error = m_resources->errorMessage(); emit changed(); return;
    }
    const bool changedActivation = !m_activation || m_activation->key != prepared->key;
    m_displayPaused = false;
    if (changedActivation) {
        m_activation = prepared; m_scene = prepared->scene; m_qualityTiers.clear(); m_slowFrames = 0;
        const auto* entry = prepared->package.manifest.contribution(m_requested.section('/', 1));
        if (!entry) return;
        const auto tiers = entry->document.value("qualityTiers").toArray();
        for (int index = 0; index < 3; ++index) if (tiers.contains(QJsonValue(index == 0 ? "low" : index == 1 ? "normal" : "high"))) m_qualityTiers.append(index);
        m_quality = m_qualityTiers.contains(1) ? 1 : m_qualityTiers.value(0, 0);
        if (m_selectionPending) {
            m_selectionPending = false;
            if (!persist(m_requested)) {
                const auto error = m_error; m_scene = {}; m_resources->close();
                if (guard) { m_error = error; emit changed(); } return;
            }
            m_selected = m_requested;
        }
        m_retry.invalidate(); m_dirty = true;
    }
    m_assets = m_resources->images(); m_error = m_resources->errorMessage();
    if (!syncActivity(changedActivation)) return;
    if (m_backendActive) { refresh(); if (!guard) return; }
    emit changed();
}
void AddonVisualizers::setSurface(QObject* owner, bool visible, const QSize& size)
{
    if (m_closing || !owner || (!m_surface.isNull() && owner != m_surface && visible)) return;
    if (m_surface.isNull() && visible) {
        m_surface = owner;
        m_surfaceDestroyed = connect(owner, &QObject::destroyed, this, [this] {
            if (m_closing) return;
            const auto surfaceSerial = ++m_surfaceSerial;
            const QPointer<AddonVisualizers> alive(this);
            m_surface = nullptr; m_visible = false; m_timer.stop();
            if (!stop() || !alive || m_closing || surfaceSerial != m_surfaceSerial) return;
            if (!syncActivity() || !alive || m_closing || surfaceSerial != m_surfaceSerial) return;
            m_resources->setSurface(nullptr);
        });
    }
    if (owner != m_surface) return;
    const auto bounded = size.boundedTo({4096, 4096});
    if (bounded != m_size) { m_size = bounded; m_dirty = true; m_renderQueue->invalidate(); }
    if (m_visible == visible) return;
    const auto surfaceSerial = ++m_surfaceSerial;
    m_visible = visible; QPointer<AddonVisualizers> guard(this);
    if (!visible) {
        m_surface = nullptr; disconnect(m_surfaceDestroyed); m_timer.stop(); if (!stop()) return;
        if (!guard || m_closing || surfaceSerial != m_surfaceSerial) return;
        if (!syncActivity(true)) return;
        if (!guard || m_closing || surfaceSerial != m_surfaceSerial) return;
        m_resources->setSurface(nullptr); if (!guard || m_closing || surfaceSerial != m_surfaceSerial) return;
        m_assets.clear();
    } else {
        m_activityBlocked = false;
        if (m_displayMode && m_reloadPending) {
            loadDisplaySelection(); if (!guard || m_closing || surfaceSerial != m_surfaceSerial) return;
        }
        m_resources->setSurface(owner); if (!guard || m_closing || surfaceSerial != m_surfaceSerial) return;
        m_assets = m_resources->images(); m_retry.invalidate();
        if (syncActivity() && guard && !m_closing && surfaceSerial == m_surfaceSerial) refresh();
    }
}
void AddonVisualizers::setColors(const AddonVisualScene::Colors& colors)
{
    if (colors.background == m_colors.background && colors.foreground == m_colors.foreground && colors.accent == m_colors.accent) return;
    m_colors = colors; m_renderQueue->invalidate(); m_dirty = true;
}
void AddonVisualizers::setReducedMotion(bool value)
{
    if (value == m_reducedMotion) return;
    m_reducedMotion = value; m_parameters.reset(); m_renderQueue->invalidate(); m_dirty = true; m_timer.setInterval(value ? 200 : 34);
}
void AddonVisualizers::invalidateTimeline()
{ if (stop()) m_retry.invalidate(); }
void AddonVisualizers::mediaChanged()
{
    if (m_closing) return;
    m_retainedFrame = false; m_mediaEpoch = 0;
    if (stop()) m_retry.invalidate();
}
void AddonVisualizers::startParameterWorker()
{
    if (m_parameters || m_reducedMotion || !m_activation || !m_activation->live() || m_scene.value("parameters").toObject().isEmpty()) return;
    const auto* entry = m_activation->package.manifest.contribution(m_requested.section('/', 1));
    if (!entry || !entry->operations.contains(QStringLiteral("updateParameters"))) return;
    m_parameters = std::make_unique<AddonVisualizerWorker>(m_scene, AddonVisualizerWorker::production(m_activation, m_requested));
    connect(m_parameters.get(), &AddonVisualizerWorker::parametersChanged, this, [this] { m_dirty = true; m_renderQueue->invalidate(); refresh(); });
    connect(m_parameters.get(), &AddonVisualizerWorker::statusChanged, this, [this] { m_error = m_parameters->errorMessage(); emit changed(); });
}
void AddonVisualizers::refresh()
{
    if (m_displayPaused) return; // Immutable retained output has no clock/render work.
    if (!observationEligible()) {
        if (m_subscribed && !stop(false)) return;
        if (!syncActivity()) return;
        if (!m_image.isNull()) clearImage(); return;
    }
    if (!syncActivity() || !m_backendActive) return;
    const auto snapshotCall = m_backend.snapshot; const auto stopAnalysis = m_backend.stopAnalysis;
    const auto revision = m_generation;
    QPointer<AddonVisualizers> guard(this); const auto snapshot = snapshotCall(); if (!guard || revision != m_generation) return;
    if (snapshot.epoch != m_mediaEpoch) {
        if (!stop() || m_generation != revision + 1 || m_closing || !m_visible || !m_resources->ready() || m_scene.isEmpty()) return;
        m_mediaEpoch = snapshot.epoch; m_retry.invalidate();
    }
    if (!snapshot.epoch || snapshot.timeUs < 0 || !std::isfinite(snapshot.rate) || snapshot.rate <= 0) { clearImage(); return; }
    if (!m_subscribed) {
        if (m_retry.isValid() && m_retry.elapsed() < 1000) return;
        m_retry.start(); m_analysis.setActive(true); m_subscribed = true;
        const auto startAnalysis = m_backend.startAnalysis; const auto generation = m_generation;
        const bool started = startAnalysis(&m_analysis, generation);
        if (!guard || !m_subscribed || m_closing || !m_visible || m_generation != generation) { if (started) stopAnalysis(); return; }
        if (!started) { m_subscribed = false; m_analysis.setActive(false); return; }
        emit changed(); if (!guard || m_generation != generation || !m_subscribed) return;
    }
    if (!snapshot.playing && !m_dirty) return;
    AddonAudioAnalysis::Frame frame;
    if (!m_analysis.frameAt(m_generation, snapshot.timeUs, &frame)) { if (snapshot.playing && !m_retainedFrame) clearImage(); return; }
    if (!m_dirty && frame.sequence == m_frameSequence && snapshot.timeUs == m_mediaTime) return;
    startParameterWorker(); auto scene = m_scene;
    if (m_parameters) {
        QJsonObject analysis;
        const auto* entry = m_activation->package.manifest.contribution(m_requested.section('/', 1));
        const auto channels = entry->document.value("analysisChannels").toArray();
        if (channels.contains(QJsonValue("bands"))) { QJsonArray values; for (const auto value : frame.bands) values.append(value); analysis.insert("bands", values); }
        if (channels.contains(QJsonValue("waveform"))) { QJsonArray values; for (const auto value : frame.waveform) values.append(value); analysis.insert("waveform", values); }
        if (channels.contains(QJsonValue("levels"))) analysis.insert("levels", QJsonObject{{"rms", frame.rms}, {"peak", frame.peak}});
        const int dimension = m_quality == 0 ? 256 : m_quality == 1 ? 512 : 1024;
        const auto viewport = m_size.width() > dimension || m_size.height() > dimension ? m_size.scaled(dimension, dimension, Qt::KeepAspectRatio) : m_size;
        m_parameters->update({{"clock", QJsonObject{{"mediaTimeUs", double(snapshot.timeUs)}, {"rate", snapshot.rate}, {"playing", snapshot.playing}}},
            {"analysis", analysis}, {"viewport", QJsonObject{{"width", viewport.width()}, {"height", viewport.height()}}},
            {"theme", QJsonObject{{"background", m_colors.background.name(QColor::HexArgb)}, {"foreground", m_colors.foreground.name(QColor::HexArgb)}, {"accent", m_colors.accent.name(QColor::HexArgb)}}}, {"reducedMotion", false}});
        if (!guard) return;
        if (!m_parameters || !m_activation || frame.epoch != m_generation || !m_resources->ready()) return;
        if (!AddonVisualizerParameters::apply(m_scene, m_parameters->values(), &scene)) scene = m_scene;
    }
    AddonVisualizerRenderQueue::Request request{scene, frame, snapshot.timeUs, m_size, m_colors, m_quality, m_reducedMotion, m_activation->live, m_assets};
    if (m_renderQueue->submit(std::move(request))) { m_frameSequence = frame.sequence; m_mediaTime = snapshot.timeUs; m_dirty = false; }
}
void AddonVisualizers::rendered(const QImage& image, qint64 cost, quint64 generation, qint64 time, quint64, int quality, qint64 analysisTime)
{
    if (m_closing || m_displayPaused || !m_visible || generation != m_generation || !m_resources->ready() || quality != m_quality) return;
    const auto snapshotCall = m_backend.snapshot; if (!snapshotCall) return;
    QPointer<AddonVisualizers> guard(this); const auto current = snapshotCall(); if (!guard) return;
    if (generation != m_generation || current.epoch != m_mediaEpoch || !current.epoch) { clearImage(); return; }
    if (cost > 8000000) ++m_slowFrames; else m_slowFrames = qMax(0, m_slowFrames - 1);
    int lower = -1; for (const auto tier : m_qualityTiers) if (tier < m_quality) lower = tier;
    if (m_slowFrames >= 3 && lower >= 0) {
        m_quality = lower; m_slowFrames = 0; m_renderQueue->invalidate(); m_dirty = true; emit changed(); return;
    } else if (m_slowFrames >= 8 || image.isNull()) {
        if (!stop()) return; m_scene = {}; m_timer.stop(); if (!syncActivity()) return;
        m_error = tr("The visualizer reached its render limit. The app shows the artwork."); emit changed(); return;
    }
    if (current.timeUs < analysisTime || current.timeUs - analysisTime > 200000
        || current.timeUs < time || current.timeUs - time > 200000) { clearImage(); return; }
    const bool wasActive = active(); m_retainedFrame = false; m_image = image; emit imageChanged();
    if (guard && wasActive != active()) emit changed();
}
QVariantMap AddonVisualizers::statistics() const
{
    auto result = m_renderQueue->statistics(); result.insert("analysisDropped", QVariant::fromValue<qulonglong>(m_analysis.dropped()));
    result.insert("generation", QVariant::fromValue<qulonglong>(m_generation)); result.insert("quality", m_quality);
    result.insert("refreshTimerActive", m_timer.isActive()); result.insert("nativeActivityRequested", m_backendActive);
    return result;
}
}
