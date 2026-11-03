// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_host_services.hpp"
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <functional>
#include <memory>
#include <optional>

namespace vlc::addons {

// Read-only guest projections (Step 6.5):
//
//   queue.snapshot    (queue.read)        the current queue, without URIs
//   library.query     (library.read)      granted collections and fields
//   media.fingerprint (media.fingerprint) a named hash of the playing file
//
// The app supplies each source. An operation is registered, and so
// advertised, only when its source exists. Item IDs are opaque: a keyed hash
// of the package and the app's own ID, stable while the app runs, so two
// add-ons cannot compare them and no path or URI is sent.
class AddonGuestProjections final : public QObject
{
    Q_OBJECT
public:
    struct QueueItem { QByteArray key; QString title, artist, album, kind; qint64 durationMs = -1; };
    struct Queue { QList<QueueItem> items; qint64 current = -1, total = 0; };
    // Owner thread, memory only: a bounded copy of the queue.
    using QueueReader = std::function<std::optional<Queue>(int offset, int limit)>;
    struct LibraryPage { QList<std::pair<QByteArray, QVariantMap>> items; bool more = false; };
    using LibraryDone = std::function<void(std::optional<LibraryPage>)>;
    // Runs the query on the library's own thread; done runs on the owner
    // thread. The returned function cancels it.
    using LibraryReader = std::function<std::function<void()>(const QString& collection, const QString& pattern,
                                                            int offset, int limit, LibraryDone done)>;
    // The playing media, or an empty URL.
    using CurrentMedia = std::function<QUrl()>;

    static constexpr int MaxQueueItems = 100;
    static constexpr int MaxLibraryItems = 50;
    // The collections and their fields the host can project.
    static const QHash<QString, QStringList>& libraryFields();
    static const QStringList& fingerprintAlgorithms();

    explicit AddonGuestProjections(QObject* parent = nullptr);
    ~AddonGuestProjections() override;
    bool mount(AddonHostServices*, QueueReader, LibraryReader, CurrentMedia);

private:
    QString opaque(const QString& package, const QString& kind, const QByteArray& key) const;
    AddonHostServices::Cancel queue(const AddonHostServices::Context&, const QJsonObject&, AddonHostServices::Completion);
    AddonHostServices::Cancel library(const AddonHostServices::Context&, const QJsonObject&, AddonHostServices::Completion);
    AddonHostServices::Cancel fingerprint(const AddonHostServices::Context&, const QJsonObject&, AddonHostServices::Completion);
    QueueReader m_queue;
    LibraryReader m_library;
    CurrentMedia m_media;
    QByteArray m_salt;
    std::shared_ptr<std::atomic_bool> m_closed;
};
}
