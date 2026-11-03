// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_visualizer_surface.hpp"
#include <QPainter>
#include <QQuickWindow>
#include <QMutexLocker>
#include <atomic>

namespace vlc::addons {
struct AddonVisualizerSurface::WindowState {
    QMutex mutex;
    QPointer<AddonVisualizerSurface> owner;
    std::atomic_bool closed{false}, available{false}, failed{false}, lost{false};
    std::atomic<quint64> epoch{1};
};
AddonVisualizerSurface::AddonVisualizerSurface(QQuickItem* parent) : QQuickPaintedItem(parent)
{ connect(this, &AddonVisualizerSurface::colorsChanged, this, &AddonVisualizerSurface::synchronize); attachWindow(); }
AddonVisualizerSurface::~AddonVisualizerSurface()
{
    const auto state = std::atomic_load(&m_windowState);
    if (state) { state->closed.store(true); QMutexLocker lock(&state->mutex); state->owner = nullptr; }
    disconnect(m_frameConnection); for (const auto& connection : m_windowConnections) disconnect(connection);
    if (m_controller) m_controller->setSurface(this, false, {});
}
void AddonVisualizerSurface::setController(AddonVisualizers* controller)
{
    if (m_controller == controller) return;
    QPointer<AddonVisualizerSurface> guard(this); const QPointer<AddonVisualizers> next(controller);
    if (m_controller) m_controller->setSurface(this, false, {}); if (!guard) return;
    disconnect(m_frameConnection); m_controller = next;
    { QMutexLocker lock(&m_imageMutex); m_image = {}; }
    update();
    if (next) m_frameConnection = connect(next, &AddonVisualizers::imageChanged, this, [this] {
        { QMutexLocker lock(&m_imageMutex); m_image = m_controller ? m_controller->image() : QImage{}; }
        update();
    });
    synchronize(); if (guard) emit controllerChanged();
}
void AddonVisualizerSurface::synchronize()
{
    if (!m_controller) return;
    const auto state = std::atomic_load(&m_windowState);
    const bool exposed = state && state->available.load(std::memory_order_acquire) && window() && isVisible()
        && window()->isVisible() && window()->visibility() != QWindow::Minimized && width() > 0 && height() > 0;
    QPointer<AddonVisualizerSurface> guard(this); const auto controller = m_controller;
    controller->setColors({m_background, m_foreground, m_accent}); if (!guard || !controller) return;
    controller->setReducedMotion(m_reducedMotion); if (!guard || !controller) return;
    controller->setSurface(this, exposed, {qCeil(width()), qCeil(height())}); if (!guard) return;
    const auto texture = QSize(qCeil(width()), qCeil(height()));
    setTextureSize(texture.width() > 1024 || texture.height() > 1024 ? texture.scaled(1024, 1024, Qt::KeepAspectRatio) : texture);
}
void AddonVisualizerSurface::paint(QPainter* painter)
{
    const auto state = std::atomic_load(&m_windowState);
    if (!state || state->closed.load(std::memory_order_acquire) || !state->available.load(std::memory_order_acquire)) return;
    QImage image; { QMutexLocker lock(&m_imageMutex); image = m_image; }
    if (!image.isNull()) painter->drawImage(boundingRect(), image);
}
void AddonVisualizerSurface::geometryChange(const QRectF& next, const QRectF& previous)
{ QQuickPaintedItem::geometryChange(next, previous); synchronize(); }
void AddonVisualizerSurface::graphEvent(const std::shared_ptr<WindowState>& state, int event)
{
    if (!state || state->closed.load(std::memory_order_acquire)) return;
    if (event == 2) {
        if (state->failed.load() || state->lost.load() || state->available.exchange(true)) return;
    } else if (event == 1) { state->failed.store(false); state->lost.store(false); state->available.store(true); }
    else { state->lost.store(true); state->available.store(false); if (event == -2) state->failed.store(true); }
    const auto epoch = state->epoch.fetch_add(1, std::memory_order_acq_rel) + 1;
    QMutexLocker lock(&state->mutex); const auto owner = state->owner;
    if (owner) QMetaObject::invokeMethod(owner, [state, owner, epoch] {
        if (!owner || state->closed.load() || state->epoch.load() != epoch || std::atomic_load(&owner->m_windowState) != state) return;
        if (!state->available.load()) { QMutexLocker lock(&owner->m_imageMutex); owner->m_image = {}; }
        owner->update(); owner->synchronize();
    }, Qt::QueuedConnection);
}
void AddonVisualizerSurface::attachWindow()
{
    const auto previous = std::atomic_load(&m_windowState);
    if (previous) { previous->closed.store(true); QMutexLocker lock(&previous->mutex); previous->owner = nullptr; }
    for (const auto& connection : m_windowConnections) disconnect(connection); m_windowConnections.clear();
    const auto state = std::make_shared<WindowState>(); state->owner = this; std::atomic_store(&m_windowState, state);
    if (!window()) return;
    m_windowConnections.append(connect(window(), &QWindow::visibilityChanged, this, &AddonVisualizerSurface::synchronize));
    m_windowConnections.append(connect(window(), &QQuickWindow::sceneGraphInitialized, this, [state] { graphEvent(state, 1); }, Qt::DirectConnection));
    m_windowConnections.append(connect(window(), &QQuickWindow::sceneGraphInvalidated, this, [state] { graphEvent(state, -1); }, Qt::DirectConnection));
    m_windowConnections.append(connect(window(), &QQuickWindow::sceneGraphError, this, [state] { graphEvent(state, -2); }, Qt::DirectConnection));
    m_windowConnections.append(connect(window(), &QQuickWindow::afterRendering, this, [state] { graphEvent(state, 2); }, Qt::DirectConnection));
    window()->update();
}
void AddonVisualizerSurface::itemChange(ItemChange change, const ItemChangeData& data)
{
    QQuickPaintedItem::itemChange(change, data);
    if (change == ItemSceneChange) attachWindow();
    if (change == ItemSceneChange || change == ItemVisibleHasChanged) synchronize();
}
void AddonVisualizerSurface::releaseResources()
{
    graphEvent(std::atomic_load(&m_windowState), -1);
    QQuickPaintedItem::releaseResources();
}
}
