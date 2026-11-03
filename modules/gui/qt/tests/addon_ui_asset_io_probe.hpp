// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QQmlEngine>
#include <QQmlNetworkAccessManagerFactory>
#include <QQuickImageProvider>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QBuffer>
#include <QTimer>
#include <QMutex>
#include <QMutexLocker>
#include <atomic>
#include <cstring>
#include <memory>
#ifdef Q_OS_LINUX
#include <sys/inotify.h>
#include <unistd.h>
#endif

struct AddonAssetIoState {
    std::atomic_int images{0}, network{0}; QByteArray png;
    QMutex mutex; QStringList networkUrls;
    int files = 0;
#ifdef Q_OS_LINUX
    int watch = -1;
    ~AddonAssetIoState() { if (watch >= 0) close(watch); }
    void collect() {
        if (watch < 0) return;
        alignas(inotify_event) char events[4096]; ssize_t size;
        while ((size = read(watch, events, sizeof(events))) > 0)
            for (ssize_t offset = 0; offset < size;) {
                const auto* event = reinterpret_cast<const inotify_event*>(events + offset);
                if (event->mask & IN_OPEN) ++files;
                offset += sizeof(inotify_event) + event->len;
            }
    }
#else
    void collect() {}
#endif
};
class AddonAssetIoReply final : public QNetworkReply {
public:
    AddonAssetIoReply(const QNetworkRequest& request, QNetworkAccessManager::Operation operation, QByteArray bytes, QObject* parent)
        : QNetworkReply(parent), m_bytes(std::move(bytes)) {
        setRequest(request); setUrl(request.url()); setOperation(operation);
        setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("image/png")); open(QIODevice::ReadOnly); setFinished(true);
        QTimer::singleShot(0, this, [this] { emit readyRead(); emit finished(); });
    }
    void abort() override { close(); }
    qint64 bytesAvailable() const override { return m_bytes.size() - m_offset + QNetworkReply::bytesAvailable(); }
    qint64 readData(char* target, qint64 maximum) override {
        const auto count = qMin(maximum, qint64(m_bytes.size() - m_offset)); if (count <= 0) return -1;
        std::memcpy(target, m_bytes.constData() + m_offset, count); m_offset += count; return count;
    }
private:
    QByteArray m_bytes; qint64 m_offset = 0;
};
class AddonAssetIoNetwork final : public QNetworkAccessManager {
public:
    AddonAssetIoNetwork(std::shared_ptr<AddonAssetIoState> state, QObject* parent) : QNetworkAccessManager(parent), m_state(std::move(state)) {}
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request, QIODevice*) override {
        const auto scheme = request.url().scheme();
        if (scheme == QLatin1String("data") || scheme == QLatin1String("qrc") || scheme == QLatin1String("file"))
            return QNetworkAccessManager::createRequest(operation, request, nullptr);
        ++m_state->network;
        { QMutexLocker lock(&m_state->mutex); m_state->networkUrls.append(request.url().toString()); }
        return new AddonAssetIoReply(request, operation, m_state->png, this);
    }
private:
    std::shared_ptr<AddonAssetIoState> m_state;
};
class AddonAssetIoFactory final : public QQmlNetworkAccessManagerFactory {
public:
    explicit AddonAssetIoFactory(std::shared_ptr<AddonAssetIoState> state) : m_state(std::move(state)) {}
    QNetworkAccessManager* create(QObject* parent) override { return new AddonAssetIoNetwork(m_state, parent); }
private:
    std::shared_ptr<AddonAssetIoState> m_state;
};
class AddonAssetIoImage final : public QQuickImageProvider {
public:
    explicit AddonAssetIoImage(std::shared_ptr<AddonAssetIoState> state)
        : QQuickImageProvider(Image, ForceAsynchronousImageLoading), m_state(std::move(state)) {}
    QImage requestImage(const QString&, QSize* size, const QSize&) override {
        ++m_state->images; auto image = QImage::fromData(m_state->png); if (size) *size = image.size(); return image;
    }
private:
    std::shared_ptr<AddonAssetIoState> m_state;
};
class AddonAssetIoProbe final : public QObject {
public:
    explicit AddonAssetIoProbe(QQmlEngine* engine) : QObject(engine), state(std::make_shared<AddonAssetIoState>()), factory(state) {
        QImage image(2, 2, QImage::Format_ARGB32); image.fill(Qt::green);
        QBuffer output(&state->png); output.open(QIODevice::WriteOnly); image.save(&output, "PNG");
        if (directory.isValid() && image.save(directory.filePath(QStringLiteral("outside.png")))) {
#ifdef Q_OS_LINUX
            state->watch = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
            if (state->watch >= 0 && inotify_add_watch(state->watch, directory.filePath(QStringLiteral("outside.png")).toLocal8Bit().constData(), IN_OPEN) < 0) {
                close(state->watch); state->watch = -1;
            }
#endif
        }
        engine->addImageProvider(QStringLiteral("addon-image-outside"), new AddonAssetIoImage(state));
        engine->setNetworkAccessManagerFactory(&factory);
    }
    QString fileUrl() const { return QUrl::fromLocalFile(directory.filePath(QStringLiteral("outside.png"))).toString(); }
    int fileReads() { state->collect(); return state->files; }
    void reset() {
        state->collect(); state->files = 0; state->images = 0; state->network = 0;
        QMutexLocker lock(&state->mutex); state->networkUrls.clear();
    }
    QString networkUrls() { QMutexLocker lock(&state->mutex); return state->networkUrls.join(QLatin1Char('\n')); }
    std::shared_ptr<AddonAssetIoState> state;
private:
    QTemporaryDir directory;
    AddonAssetIoFactory factory;
};
