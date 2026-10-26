/*****************************************************************************
 * Copyright (C) 2024 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
#ifndef QML_TEST_HPP
#define QML_TEST_HPP

#include <QObject>
#include <QQmlEngine>
#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <memory>

class QQmlNetworkAccessManagerFactory;
struct TestTextResourceState;

class TestTextResources : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int imageRequests READ imageRequests)
    Q_PROPERTY(int networkRequests READ networkRequests)
    Q_PROPERTY(int fileRequests READ fileRequests)
    Q_PROPERTY(bool fileObservationAvailable READ fileObservationAvailable CONSTANT)
    Q_PROPERTY(QString fileUrl READ fileUrl CONSTANT)

public:
    explicit TestTextResources(QQmlEngine *engine);
    ~TestTextResources() override;
    int imageRequests() const;
    int networkRequests() const;
    int fileRequests() const;
    bool fileObservationAvailable() const;
    QString fileUrl() const;
    Q_INVOKABLE void reset();

private:
    std::shared_ptr<TestTextResourceState> m_state;
    QTemporaryDir m_directory;
    std::unique_ptr<QQmlNetworkAccessManagerFactory> m_factory;
};

// Keep the presented window's physical pixels. Qt Quick Test's grabImage()
// crops with local item coordinates and can lose the DPR on the software path.
class TestWindowImage : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int width READ width CONSTANT)
    Q_PROPERTY(int height READ height CONSTANT)

public:
    explicit TestWindowImage(QQuickWindow *window);
    int width() const { return m_image.width(); }
    int height() const { return m_image.height(); }
    Q_INVOKABLE QPoint point(QQuickItem *item, qreal x, qreal y) const;
    Q_INVOKABLE int red(int x, int y) const;
    Q_INVOKABLE int green(int x, int y) const;
    Q_INVOKABLE int blue(int x, int y) const;

private:
    QImage m_image;
    QPointer<QQuickWindow> m_window;
    QSize m_logicalSize;
};

class TestImages : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE QObject *grabWindow(QQuickItem *item) const;
};

class Setup : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

public slots:
    void qmlEngineAvailable(QQmlEngine *engine);
};

#endif /* QML_TEST_HPP */
