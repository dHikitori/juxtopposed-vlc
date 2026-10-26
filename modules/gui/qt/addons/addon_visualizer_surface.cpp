// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_visualizer_surface.hpp"
#include <QPainter>
#include <QQuickWindow>
#include <QMutexLocker>

namespace vlc::addons {
AddonVisualizerSurface::AddonVisualizerSurface(QQuickItem* parent) : QQuickPaintedItem(parent)
{ connect(this, &AddonVisualizerSurface::colorsChanged, this, &AddonVisualizerSurface::synchronize); }
AddonVisualizerSurface::~AddonVisualizerSurface()
{ if (m_controller) m_controller->setSurface(this, false, {}); }
void AddonVisualizerSurface::setController(AddonVisualizers* controller)
{
    if (m_controller == controller) return;
    if (m_controller) m_controller->setSurface(this, false, {});
    disconnect(m_frameConnection); m_controller = controller;
    { QMutexLocker lock(&m_imageMutex); m_image = {}; }
    update();
    if (controller) m_frameConnection = connect(controller, &AddonVisualizers::imageChanged, this, [this] {
        { QMutexLocker lock(&m_imageMutex); m_image = m_controller ? m_controller->image() : QImage{}; }
        update();
    });
    synchronize(); emit controllerChanged();
}
void AddonVisualizerSurface::synchronize()
{
    if (!m_controller) return;
    const bool exposed = window() && isVisible() && window()->isVisible() && window()->visibility() != QWindow::Minimized && width() > 0 && height() > 0;
    m_controller->setColors({m_background, m_foreground, m_accent}); m_controller->setReducedMotion(m_reducedMotion);
    m_controller->setSurface(this, exposed, {qCeil(width()), qCeil(height())});
    const auto texture = QSize(qCeil(width()), qCeil(height()));
    setTextureSize(texture.width() > 1024 || texture.height() > 1024 ? texture.scaled(1024, 1024, Qt::KeepAspectRatio) : texture);
}
void AddonVisualizerSurface::paint(QPainter* painter)
{
    QImage image; { QMutexLocker lock(&m_imageMutex); image = m_image; }
    if (!image.isNull()) painter->drawImage(boundingRect(), image);
}
void AddonVisualizerSurface::geometryChange(const QRectF& next, const QRectF& previous)
{ QQuickPaintedItem::geometryChange(next, previous); synchronize(); }
void AddonVisualizerSurface::itemChange(ItemChange change, const ItemChangeData& data)
{
    QQuickPaintedItem::itemChange(change, data);
    if (change == ItemSceneChange) {
        disconnect(m_windowConnection);
        if (window()) m_windowConnection = connect(window(), &QWindow::visibilityChanged, this, &AddonVisualizerSurface::synchronize);
    }
    if (change == ItemSceneChange || change == ItemVisibleHasChanged) synchronize();
}
void AddonVisualizerSurface::releaseResources()
{
    QQuickPaintedItem::releaseResources();
    QMetaObject::invokeMethod(this, [this] { if (m_controller) m_controller->invalidateTimeline(); }, Qt::QueuedConnection);
}
}
