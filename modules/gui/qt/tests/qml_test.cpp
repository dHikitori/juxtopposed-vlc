/*****************************************************************************
 * Copyright (C) 2023 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
*****************************************************************************/

#include <QtQuickTest>
#include <QQmlContext>
#include <QQmlNetworkAccessManagerFactory>
#include <QQuickImageProvider>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QBuffer>
#include <QTimer>
#include <QtMath>
#include <atomic>
#include <cstring>
#ifdef Q_OS_LINUX
#include <sys/inotify.h>
#include <unistd.h>
#endif
#include "qml_test.hpp"

struct TestTextResourceState
{
    std::atomic_int images{0}, network{0};
    QByteArray png;
    mutable int files = 0;
#ifdef Q_OS_LINUX
    int descriptor = -1;
    ~TestTextResourceState() { if (descriptor >= 0) close(descriptor); }
    void collectFiles() const
    {
        if (descriptor < 0) return;
        alignas(inotify_event) char events[4096];
        ssize_t size;
        while ((size = read(descriptor, events, sizeof(events))) > 0)
            for (ssize_t offset = 0; offset < size;) {
                const auto *event = reinterpret_cast<const inotify_event *>(events + offset);
                if (event->mask & IN_OPEN) ++files;
                offset += sizeof(inotify_event) + event->len;
            }
    }
#else
    void collectFiles() const {}
#endif
};

namespace {
class TextProbeImageProvider : public QQuickImageProvider
{
public:
    explicit TextProbeImageProvider(std::shared_ptr<TestTextResourceState> state)
        : QQuickImageProvider(QQuickImageProvider::Image, QQuickImageProvider::ForceAsynchronousImageLoading), m_state(std::move(state)) {}
    QImage requestImage(const QString &, QSize *size, const QSize &) override
    {
        ++m_state->images;
        const auto image = QImage::fromData(m_state->png);
        if (size) *size = image.size();
        return image;
    }
private:
    std::shared_ptr<TestTextResourceState> m_state;
};

class TextProbeReply : public QNetworkReply
{
public:
    TextProbeReply(const QNetworkRequest &request, QNetworkAccessManager::Operation operation,
                   QByteArray bytes, QObject *parent)
        : QNetworkReply(parent), m_bytes(std::move(bytes))
    {
        setRequest(request); setUrl(request.url()); setOperation(operation);
        setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("image/png"));
        setHeader(QNetworkRequest::ContentLengthHeader, m_bytes.size());
        open(QIODevice::ReadOnly); setFinished(true);
        QTimer::singleShot(0, this, [this] { emit readyRead(); emit finished(); });
    }
    void abort() override { close(); }
    qint64 bytesAvailable() const override { return m_bytes.size() - m_offset + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 maximum) override
    {
        const auto size = qMin(maximum, qint64(m_bytes.size() - m_offset));
        if (size <= 0) return -1;
        std::memcpy(data, m_bytes.constData() + m_offset, size); m_offset += size;
        return size;
    }
private:
    QByteArray m_bytes;
    qint64 m_offset = 0;
};

class TextProbeNetworkManager : public QNetworkAccessManager
{
public:
    TextProbeNetworkManager(std::shared_ptr<TestTextResourceState> state, QObject *parent)
        : QNetworkAccessManager(parent), m_state(std::move(state)) {}
protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request, QIODevice *outgoing) override
    {
        if (request.url().host() == QLatin1String("addon-text-probe.invalid")) {
            ++m_state->network;
            return new TextProbeReply(request, operation, m_state->png, this);
        }
        return QNetworkAccessManager::createRequest(operation, request, outgoing);
    }
private:
    std::shared_ptr<TestTextResourceState> m_state;
};

class TextProbeNetworkFactory : public QQmlNetworkAccessManagerFactory
{
public:
    explicit TextProbeNetworkFactory(std::shared_ptr<TestTextResourceState> state) : m_state(std::move(state)) {}
    QNetworkAccessManager *create(QObject *parent) override { return new TextProbeNetworkManager(m_state, parent); }
private:
    std::shared_ptr<TestTextResourceState> m_state;
};
}

TestTextResources::TestTextResources(QQmlEngine *engine)
    : QObject(engine), m_state(std::make_shared<TestTextResourceState>()),
      m_factory(std::make_unique<TextProbeNetworkFactory>(m_state))
{
    QImage image(2, 2, QImage::Format_ARGB32); image.fill(Qt::red);
    QBuffer buffer(&m_state->png); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG");
    if (m_directory.isValid() && image.save(m_directory.filePath(QStringLiteral("probe.png")))) {
#ifdef Q_OS_LINUX
        m_state->descriptor = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (m_state->descriptor >= 0 && inotify_add_watch(m_state->descriptor,
                m_directory.filePath(QStringLiteral("probe.png")).toLocal8Bit().constData(), IN_OPEN) < 0) {
            close(m_state->descriptor); m_state->descriptor = -1;
        }
#endif
    }
    engine->addImageProvider(QStringLiteral("addon-text-probe"), new TextProbeImageProvider(m_state));
    engine->setNetworkAccessManagerFactory(m_factory.get());
}
TestTextResources::~TestTextResources() = default;
int TestTextResources::imageRequests() const { return m_state->images.load(); }
int TestTextResources::networkRequests() const { return m_state->network.load(); }
int TestTextResources::fileRequests() const { m_state->collectFiles(); return m_state->files; }
bool TestTextResources::fileObservationAvailable() const
{
#ifdef Q_OS_LINUX
    return m_state->descriptor >= 0;
#else
    return false;
#endif
}
QString TestTextResources::fileUrl() const
{ return m_directory.isValid() ? QUrl::fromLocalFile(m_directory.filePath(QStringLiteral("probe.png"))).toString() : QString(); }
void TestTextResources::reset()
{
    m_state->collectFiles(); m_state->files = 0; m_state->images = 0; m_state->network = 0;
}

TestWindowImage::TestWindowImage(QQuickWindow *window)
    : m_image(window->grabWindow()), m_window(window), m_logicalSize(window->size())
{
}

QPoint TestWindowImage::point(QQuickItem *item, qreal x, qreal y) const
{
    if (!item || !m_window || item->window() != m_window || m_logicalSize.isEmpty())
        return {-1, -1};
    const QPointF scene = item->mapToScene(QPointF(x, y));
    // Use actual image dimensions; grabWindow's DPR metadata is not reliable
    // on every backend. Select the physical pixel containing the scene point.
    return {qFloor(scene.x() * m_image.width() / m_logicalSize.width()),
            qFloor(scene.y() * m_image.height() / m_logicalSize.height())};
}

int TestWindowImage::red(int x, int y) const
{
    return m_image.valid(x, y) ? m_image.pixelColor(x, y).red() : -1;
}

int TestWindowImage::green(int x, int y) const
{
    return m_image.valid(x, y) ? m_image.pixelColor(x, y).green() : -1;
}

int TestWindowImage::blue(int x, int y) const
{
    return m_image.valid(x, y) ? m_image.pixelColor(x, y).blue() : -1;
}

QObject *TestImages::grabWindow(QQuickItem *item) const
{
    if (!item || !item->window())
        return nullptr;
    auto *image = new TestWindowImage(item->window());
    QQmlEngine::setObjectOwnership(image, QQmlEngine::JavaScriptOwnership);
    return image;
}

void Setup::qmlEngineAvailable(QQmlEngine *engine)
{
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
    engine->addImportPath(":/qt/qml");
#endif
    engine->rootContext()->setContextProperty(QStringLiteral("testImages"), new TestImages(engine));
    engine->rootContext()->setContextProperty(QStringLiteral("testTextResources"), new TestTextResources(engine));
}

int main(int argc, char **argv)
{
    QTEST_SET_MAIN_SOURCE_PATH

    Q_INIT_RESOURCE( util_assets );
#ifdef QT_USE_QMLCACHEGEN
    Q_INIT_RESOURCE( util_cachegen );
#endif

    //run tests offscreen as the CI doesn't have a desktop environment
    qputenv("QT_QPA_PLATFORM", "offscreen");
    Setup setup;
    return quick_test_main_with_setup(argc, argv, "qml_test", QUICK_TEST_SOURCE_DIR, &setup);
}
