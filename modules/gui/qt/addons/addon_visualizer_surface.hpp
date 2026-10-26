// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_visualizers.hpp"
#include <QQuickPaintedItem>
#include <QMutex>

namespace vlc::addons {
class AddonVisualizerSurface : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(AddonVisualizers* controller READ controller WRITE setController NOTIFY controllerChanged FINAL)
    Q_PROPERTY(QColor backgroundColor MEMBER m_background NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor foregroundColor MEMBER m_foreground NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor accentColor MEMBER m_accent NOTIFY colorsChanged FINAL)
    Q_PROPERTY(bool reducedMotion MEMBER m_reducedMotion NOTIFY colorsChanged FINAL)
public:
    explicit AddonVisualizerSurface(QQuickItem* parent = nullptr);
    ~AddonVisualizerSurface() override;
    AddonVisualizers* controller() const { return m_controller; }
    void setController(AddonVisualizers*);
    void paint(QPainter*) override;
signals:
    void controllerChanged();
    void colorsChanged();
protected:
    void geometryChange(const QRectF&, const QRectF&) override;
    void itemChange(ItemChange, const ItemChangeData&) override;
    void releaseResources() override;
private:
    void synchronize();
    QPointer<AddonVisualizers> m_controller;
    QMetaObject::Connection m_frameConnection, m_windowConnection;
    QMutex m_imageMutex;
    QImage m_image;
    QColor m_background = Qt::black, m_foreground = Qt::white, m_accent = QColor(211, 121, 24);
    bool m_reducedMotion = false;
};
}
