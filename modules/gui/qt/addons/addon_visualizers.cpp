// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_visualizers.hpp"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QBuffer>
#include <QImageReader>
#include <cmath>

namespace vlc::addons {
AddonVisualizers::AddonVisualizers(AddonManager* manager, Backend backend, QObject* parent)
    : QObject(parent), m_manager(manager), m_backend(std::move(backend))
{
    if (manager) {
        QFile file(QDir(manager->storageDirectory()).filePath(QStringLiteral("visualizers.json")));
        if (file.size() <= 8192 && file.open(QIODevice::ReadOnly)) m_selected = QJsonDocument::fromJson(file.read(8192)).object().value("selected").toString().left(256);
        connect(manager, &AddonManager::revisionChanged, this, &AddonVisualizers::reload);
        connect(manager, &QObject::destroyed, this, &AddonVisualizers::close);
        connect(manager, &AddonManager::packageWithdrawn, this, [this](const QString& package) {
            if (m_selected.section('/', 0, 0) == package) { stop(); m_scene = {}; }
        });
    }
    m_timer.setInterval(34); connect(&m_timer, &QTimer::timeout, this, &AddonVisualizers::refresh);
    m_retry.start(); m_renderClock.start(); reload();
}
AddonVisualizers::~AddonVisualizers() { close(); }
void AddonVisualizers::close()
{ if (!m_closing) { m_closing = true; m_timer.stop(); stop(); } }
QVariantList AddonVisualizers::availableVisualizers() const
{
    QVariantList result{QVariantMap{{"id", ""}, {"name", tr("Artwork")}, {"enabled", true}}};
    if (!m_manager) return result;
    for (const auto& value : m_manager->contributionsOfType(QStringLiteral("visualizer"), true)) {
        auto entry = value.toMap(); entry.insert("enabled", available(entry)); result.append(entry);
    }
    return result;
}
bool AddonVisualizers::available(const QVariantMap& entry) const
{
    if (!m_manager || !entry.value("enabled").toBool()) return false;
    const auto document = entry.value("document").toMap();
    if (document.value("contract").toString() != QLatin1String("visualizer@1")) return false;
    const auto grant = m_manager->grantedScope(entry.value("packageId").toString(), QStringLiteral("audio.analysis"));
    const auto channels = grant.value("channels").toArray();
    for (const auto& value : document.value("analysisChannels").toList()) {
        if (value.toString() != QLatin1String("bands") && value.toString() != QLatin1String("waveform") && value.toString() != QLatin1String("levels")) return false;
        if (!channels.contains(QJsonValue(value.toString()))) return false;
    }
    return !document.value("analysisChannels").toList().isEmpty();
}
bool AddonVisualizers::selectVisualizer(const QString& id)
{
    if (m_closing || !m_manager) return false;
    if (!id.isEmpty()) {
        bool found = false;
        for (const auto& value : availableVisualizers()) {
            const auto entry = value.toMap(); if (entry.value("id").toString() == id && entry.value("enabled").toBool()) { found = true; break; }
        }
        if (!found || !AddonVisualScene::validate(m_manager->contributionResource(id, QStringLiteral("scene")))) return false;
    }
    QSaveFile file(QDir(m_manager->storageDirectory()).filePath(QStringLiteral("visualizers.json")));
    const auto bytes = QJsonDocument(QJsonObject{{"selected", id}}).toJson(QJsonDocument::Compact);
    if (!QDir().mkpath(m_manager->storageDirectory()) || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        m_error = tr("VLC cannot save the visualizer selection."); emit changed(); return false;
    }
    m_selected = id; m_quality = 1; m_slowFrames = 0; reload(); return true;
}
void AddonVisualizers::stop()
{
    if (m_subscribed && m_backend.stopAnalysis) m_backend.stopAnalysis();
    m_subscribed = false; m_analysis.setActive(false); m_analysis.reset(++m_generation);
    m_renderer.clear(); m_image = {}; m_frameSequence = 0; m_mediaTime = -1;
    m_dirty = true; emit imageChanged(); emit changed();
}
void AddonVisualizers::reload()
{
    stop(); m_scene = {}; m_assets.clear(); m_error.clear();
    if (!m_selected.isEmpty() && m_manager) {
        for (const auto& value : availableVisualizers()) {
            const auto entry = value.toMap(); if (entry.value("id").toString() != m_selected || !entry.value("enabled").toBool()) continue;
            const auto scene = m_manager->contributionResource(m_selected, QStringLiteral("scene"));
            if (AddonVisualScene::validate(scene, &m_error)) {
                qint64 decodedBytes = 0; bool correct = true;
                const auto assets = scene.value("assets").toObject();
                for (auto it = assets.begin(); it != assets.end(); ++it) {
                    auto bytes = m_manager->contributionFile(m_selected, it.value().toString(), 1024 * 1024);
                    QBuffer buffer(&bytes); buffer.open(QIODevice::ReadOnly); QImageReader reader(&buffer);
                    const auto format = reader.format().toLower(); const auto size = reader.size();
                    if ((format != "png" && format != "jpeg" && format != "webp") || size.isEmpty() || size.width() > 1024 || size.height() > 1024) { correct = false; break; }
                    auto image = reader.read(); decodedBytes += image.sizeInBytes();
                    if (image.isNull() || decodedBytes > 16 * 1024 * 1024) { correct = false; break; }
                    m_assets.insert(it.key(), image);
                }
                if (correct) m_scene = scene;
                else { m_assets.clear(); m_error = tr("VLC cannot use the visualizer images. VLC will show the artwork."); }
            }
        }
        if (m_scene.isEmpty() && m_error.isEmpty()) m_error = tr("The visualizer is not available. VLC will show the artwork.");
    }
    if (m_visible && !m_scene.isEmpty() && !m_closing) { m_retry.invalidate(); m_timer.start(); refresh(); }
    else m_timer.stop();
    emit changed();
}
void AddonVisualizers::setSurface(QObject* owner, bool visible, const QSize& size)
{
    if (m_closing || !owner || (!m_surface.isNull() && owner != m_surface && visible)) return;
    if (m_surface.isNull() && visible) {
        m_surface = owner;
        m_surfaceDestroyed = connect(owner, &QObject::destroyed, this, [this] { m_surface = nullptr; m_visible = false; m_timer.stop(); stop(); });
    }
    if (owner != m_surface) return;
    m_size = size.boundedTo({4096, 4096}); m_dirty = true;
    if (m_visible == visible) return;
    m_visible = visible;
    if (!visible) { m_surface = nullptr; disconnect(m_surfaceDestroyed); m_timer.stop(); stop(); }
    else if (!m_scene.isEmpty()) { m_retry.invalidate(); m_timer.start(); refresh(); }
}
void AddonVisualizers::setColors(const AddonVisualScene::Colors& colors)
{
    if (colors.background == m_colors.background && colors.foreground == m_colors.foreground && colors.accent == m_colors.accent) return;
    m_colors = colors; m_renderer.clear(); m_dirty = true;
}
void AddonVisualizers::setReducedMotion(bool value)
{ if (value != m_reducedMotion) { m_reducedMotion = value; m_renderer.clear(); m_dirty = true; m_timer.setInterval(value ? 200 : 34); } }
void AddonVisualizers::invalidateTimeline()
{ stop(); m_retry.invalidate(); }
void AddonVisualizers::refresh()
{
    if (m_closing || !m_visible || m_scene.isEmpty() || !m_backend.snapshot || !m_backend.startAnalysis || !m_backend.stopAnalysis) return;
    const auto snapshot = m_backend.snapshot();
    if (snapshot.epoch != m_mediaEpoch) { stop(); m_mediaEpoch = snapshot.epoch; m_retry.invalidate(); }
    if (!snapshot.epoch || snapshot.timeUs < 0 || !std::isfinite(snapshot.rate) || snapshot.rate <= 0) return;
    if (!m_subscribed) {
        if (m_retry.isValid() && m_retry.elapsed() < 1000) return;
        m_retry.start(); m_analysis.setActive(true);
        m_subscribed = m_backend.startAnalysis(&m_analysis, m_generation);
        if (!m_subscribed) { m_analysis.setActive(false); return; }
        emit changed();
    }
    if (!snapshot.playing && !m_dirty) return;
    AddonAudioAnalysis::Frame frame;
    if (!m_analysis.frameAt(m_generation, snapshot.timeUs, &frame)) return;
    if (!m_dirty && frame.sequence == m_frameSequence && snapshot.timeUs == m_mediaTime) return;
    const bool wasActive = active(); m_renderClock.restart();
    m_image = m_renderer.render(m_scene, frame, snapshot.timeUs, m_size, m_colors, m_quality, m_reducedMotion, m_assets);
    const auto cost = m_renderClock.nsecsElapsed(); m_frameSequence = frame.sequence; m_mediaTime = snapshot.timeUs; m_dirty = false;
    if (cost > 8000000) ++m_slowFrames; else m_slowFrames = qMax(0, m_slowFrames - 1);
    if (m_slowFrames >= 3 && m_quality > 0) { --m_quality; m_slowFrames = 0; m_renderer.clear(); m_dirty = true; emit changed(); }
    else if (m_slowFrames >= 8 || m_image.isNull()) {
        stop(); m_scene = {}; m_timer.stop(); m_error = tr("The visualizer exceeded its render limit. VLC will show the artwork."); emit changed(); return;
    }
    if (wasActive != active()) emit changed();
    emit imageChanged();
}
}
