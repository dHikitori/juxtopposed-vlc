// SPDX-License-Identifier: GPL-2.0-or-later
#include "thumbnail_cache.hpp"

#include <QCache>
#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QImage>
#include <QImageIOHandler>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickImageProvider>
#include <QThread>
#include <algorithm>
#include <cmath>

namespace {
QString prefix() { return QStringLiteral("image://") + QLatin1String(ThumbnailCache::providerId) + QLatin1Char('/'); }
QString key(const QString& id, const QSize& size)
{
    return id + QLatin1Char('@') + QString::number(size.width()) + QLatin1Char('x') + QString::number(size.height());
}
bool onGuiThread()
{
    const auto* application = QCoreApplication::instance();
    return application && QThread::currentThread() == application->thread();
}
}

struct ThumbnailCache::Store
{
    struct Entry
    {
        QImage image;
        QDateTime modified;
        qint64 bytes = -1;
        quint64 generation = 0;
    };
    explicit Store(qint64 limitBytes) : cache(std::max<qint64>(1, limitBytes / 1024)) {}
    mutable QMutex mutex;
    QCache<QString, Entry> cache;
    quint64 generation = 0;
};

namespace {
class Provider final : public QQuickImageProvider
{
public:
    explicit Provider(std::shared_ptr<ThumbnailCache::Store> store) : QQuickImageProvider(Image), m_store(std::move(store)) {}

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override
    {
        const auto entryKey = key(id, requestedSize);
        const bool gui = onGuiThread();
        QDateTime cachedModified;
        qint64 cachedBytes = -1;
        {
            QMutexLocker lock(&m_store->mutex);
            if (const auto* entry = m_store->cache.object(entryKey)) {
                // The GUI thread never waits on a file, even to check one.
                if (gui || entry->generation == m_store->generation) {
                    if (size) *size = entry->image.size();
                    return entry->image;
                }
                cachedModified = entry->modified; cachedBytes = entry->bytes;
            }
        }
        if (gui) return {};
        const QUrl url = QUrl::fromEncoded(QByteArray::fromBase64(id.toLatin1(), QByteArray::Base64UrlEncoding));
        if (!url.isLocalFile()) return {};
        const QFileInfo info(url.toLocalFile());
        const auto modified = info.lastModified();
        const auto bytes = info.size();
        if (cachedModified.isValid() && cachedModified == modified && cachedBytes == bytes) {
            QMutexLocker lock(&m_store->mutex);
            if (auto* entry = m_store->cache.object(entryKey)) {
                entry->generation = m_store->generation;
                if (size) *size = entry->image.size();
                return entry->image;
            }
        }
        QImageReader reader(info.filePath());
        reader.setAutoTransform(true);
        // Decode only enough to cover the tile, as Image does for a cropped
        // sourceSize; the reader scales before it rotates.
        auto target = requestedSize;
        if (reader.transformation() & QImageIOHandler::TransformationRotate90) target.transpose();
        const auto original = reader.size();
        if (original.isValid() && (target.width() > 0 || target.height() > 0)) {
            const double scale = std::max(target.width() > 0 ? double(target.width()) / original.width() : 0.,
                                          target.height() > 0 ? double(target.height()) / original.height() : 0.);
            if (scale < 1)
                reader.setScaledSize(QSize(std::max(1, int(std::ceil(original.width() * scale))),
                                           std::max(1, int(std::ceil(original.height() * scale)))));
        }
        const auto image = reader.read();
        if (image.isNull()) return {};
        if (size) *size = image.size();
        QMutexLocker lock(&m_store->mutex);
        auto* entry = new ThumbnailCache::Store::Entry {image, modified, bytes, m_store->generation};
        m_store->cache.insert(entryKey, entry, std::max<qsizetype>(1, image.sizeInBytes() / 1024));
        return image;
    }

private:
    std::shared_ptr<ThumbnailCache::Store> m_store;
};
}

ThumbnailCache::ThumbnailCache(qint64 limitBytes, QObject* parent)
    : QObject(parent), m_store(std::make_shared<Store>(limitBytes))
{
}

ThumbnailCache::~ThumbnailCache() = default;

QQuickImageProvider* ThumbnailCache::createProvider() const
{
    return new Provider(m_store);
}

QUrl ThumbnailCache::source(const QUrl& url) const
{
    if (!url.isLocalFile()) return url;
    return QUrl(prefix() + QString::fromLatin1(url.toEncoded(QUrl::FullyEncoded)
        .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)));
}

bool ThumbnailCache::ready(const QUrl& source, const QSize& size) const
{
    const auto text = source.toString(QUrl::FullyEncoded);
    if (!text.startsWith(prefix())) return false;
    QMutexLocker lock(&m_store->mutex);
    // A lookup also keeps the entry from being the next one dropped.
    const auto* entry = m_store->cache.object(key(text.mid(prefix().size()), size));
    return entry && entry->generation == m_store->generation;
}

void ThumbnailCache::revalidate()
{
    QMutexLocker lock(&m_store->mutex);
    ++m_store->generation;
}
