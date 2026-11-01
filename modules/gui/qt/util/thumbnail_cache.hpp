// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_THUMBNAIL_CACHE_HPP
#define VLC_QT_THUMBNAIL_CACHE_HPP

#include <QObject>
#include <QSize>
#include <QUrl>
#include <memory>

class QQuickImageProvider;

// Library lists build and drop tiles as they scroll, and Qt keeps only about
// 2 MB of images that are not on screen, so scrolling back would read and
// decode each file again. This keeps decoded local thumbnails in memory at
// the size a tile asked for. Files are only read on Qt's image threads; the
// GUI thread is answered from memory or not at all.
class ThumbnailCache final : public QObject
{
    Q_OBJECT
public:
    static constexpr const char* providerId = "librarythumb";
    static constexpr qint64 defaultLimit = 96 * 1024 * 1024;
    explicit ThumbnailCache(qint64 limitBytes = defaultLimit, QObject* parent = nullptr);
    ~ThumbnailCache() override;
    // The image provider that reads through this cache; the engine owns it.
    QQuickImageProvider* createProvider() const;
    // The cached form of a local file's URL; other URLs are returned as is.
    Q_INVOKABLE QUrl source(const QUrl& url) const;
    // Whether source can be shown at once at this size, without reading a file.
    Q_INVOKABLE bool ready(const QUrl& source, const QSize& size) const;
    // Files on disk may have changed: check each one again before reuse.
    Q_INVOKABLE void revalidate();
    struct Store;
private:
    std::shared_ptr<Store> m_store;
};

#endif
