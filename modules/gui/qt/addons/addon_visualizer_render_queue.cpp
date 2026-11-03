// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_visualizer_render_queue.hpp"
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <atomic>
namespace vlc::addons {
struct AddonVisualizerRenderQueue::Shared {
    QMutex mutex;
    QPointer<AddonVisualizerRenderQueue> owner;
    std::atomic<quint64> epoch{1};
    std::atomic_bool closed{false};
    AddonVisualScene renderer;
    quint64 rendererEpoch = 0;
};
namespace {
std::atomic_int jobs{0};
struct JobLease { ~JobLease() { jobs.fetch_sub(1, std::memory_order_release); } };
}
AddonVisualizerRenderQueue::AddonVisualizerRenderQueue(Render render, QObject* parent)
    : QObject(parent), m_shared(std::make_shared<Shared>()),
      m_lifetime(AddonStreamLifetime::applicationHandle()), m_render(std::move(render))
{
    m_shared->owner = this;
    if (!m_render) m_render = [](AddonVisualScene& renderer, const Request& frame) {
        return renderer.render(frame.scene, frame.analysis, frame.mediaTimeUs, frame.size, frame.colors,
            frame.quality, frame.reducedMotion, frame.assets);
    };
    m_timer.setInterval(10); connect(&m_timer, &QTimer::timeout, this, &AddonVisualizerRenderQueue::pump);
}
AddonVisualizerRenderQueue::~AddonVisualizerRenderQueue() { close(); }
int AddonVisualizerRenderQueue::liveJobs() { return jobs.load(std::memory_order_acquire); }
void AddonVisualizerRenderQueue::close()
{
    if (m_closed) return;
    m_closed = true; m_timer.stop(); m_latest.reset(); m_shared->closed.store(true, std::memory_order_release);
    m_shared->epoch.fetch_add(1, std::memory_order_acq_rel);
    QMutexLocker lock(&m_shared->mutex); m_shared->owner = nullptr;
}
void AddonVisualizerRenderQueue::invalidate()
{
    if (m_closed) return;
    m_shared->epoch.fetch_add(1, std::memory_order_acq_rel); m_latest.reset(); m_timer.stop();
}
bool AddonVisualizerRenderQueue::submit(Request request)
{
    if (m_closed || !m_lifetime.current() || request.mediaTimeUs < 0 || request.size.isEmpty() || request.size.width() > 4096 || request.size.height() > 4096
        || request.quality < 0 || request.quality > 2 || !request.current || !request.current()
        || request.assets.size() > 16 || QJsonDocument(request.scene).toJson(QJsonDocument::Compact).size() > 256 * 1024) return false;
    qint64 pixels = 0;
    for (auto it = request.assets.cbegin(); it != request.assets.cend(); ++it) {
        if (it->width() > 1024 || it->height() > 1024 || (pixels += it->sizeInBytes()) > 16 * 1024 * 1024) return false;
    }
    if (m_latest) ++m_replaced;
    m_latest = Work{std::move(request), m_shared->epoch.load(std::memory_order_acquire)}; pump(); return true;
}
void AddonVisualizerRenderQueue::pump()
{
    if (m_closed || m_running || !m_latest) return;
    if (!m_lifetime.current()) { close(); return; }
    int expected = 0;
    if (!jobs.compare_exchange_strong(expected, 1)) { m_timer.start(); return; }
    const auto lease = std::make_shared<JobLease>();
    m_timer.stop(); m_running = true; auto work = std::move(*m_latest); m_latest.reset();
    const auto shared = m_shared; const auto render = m_render;
    const bool started = m_lifetime.startQtWork([shared, render, lease, work]() mutable {
        const auto current = [&] { return !shared->closed.load(std::memory_order_acquire)
            && work.epoch == shared->epoch.load(std::memory_order_acquire) && work.request.current(); };
        QImage image; qint64 cost = 0;
        if (current()) {
            if (shared->rendererEpoch != work.epoch) { shared->renderer.clear(); shared->rendererEpoch = work.epoch; }
            QElapsedTimer elapsed; elapsed.start(); image = render(shared->renderer, work.request); cost = elapsed.nsecsElapsed();
        }
        if (!current()) image = {};
        QMutexLocker lock(&shared->mutex); const auto owner = shared->owner;
        if (owner) QMetaObject::invokeMethod(owner, [owner, work = std::move(work), image = std::move(image), cost]() mutable {
            if (owner) owner->finished(std::move(work), std::move(image), cost);
        }, Qt::QueuedConnection);
    }, [shared] { shared->closed.store(true, std::memory_order_release); },
        [lease](const auto& ticket) { ticket.retain(lease); });
    if (!started) { m_running = false; m_latest = std::move(work); m_timer.start(); }
}
void AddonVisualizerRenderQueue::finished(Work work, QImage image, qint64 cost)
{
    m_running = false;
    if (m_closed || !m_lifetime.current()) return;
    const bool current = work.epoch == m_shared->epoch.load(std::memory_order_acquire) && work.request.current();
    if (current) {
        ++m_rendered; m_maximumCostNs = qMax(m_maximumCostNs, cost); QPointer<AddonVisualizerRenderQueue> guard(this);
        emit frameReady(image, cost, work.request.analysis.epoch, work.request.mediaTimeUs, work.request.analysis.sequence,
            work.request.quality, work.request.analysis.timeUs);
        if (!guard) return;
    }
    pump();
}
QVariantMap AddonVisualizerRenderQueue::statistics() const
{
    return {{"rendered", QVariant::fromValue<qulonglong>(m_rendered)}, {"replaced", QVariant::fromValue<qulonglong>(m_replaced)},
        {"maximumCostNs", m_maximumCostNs}, {"globalJobs", liveJobs()}, {"pending", m_latest.has_value()}, {"running", m_running}};
}
}
