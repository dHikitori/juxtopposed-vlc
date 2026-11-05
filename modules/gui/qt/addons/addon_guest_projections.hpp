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

// Guest projections of the queue, the library and the playing file (Step 6.5):
//
//   queue.snapshot    (queue.read)        the current queue, without URIs
//   queue.apply       (queue.modify)      remove queue items by their IDs
//   library.query     (library.read)      granted collections and fields
//   media.fingerprint (media.fingerprint) a named hash of the playing file
//
// The app supplies each source. An operation is registered, and so
// advertised, only when its source exists. Item IDs are opaque: a keyed hash
// of the package and the app's own ID, stable while the app runs, so two
// add-ons cannot compare them and no path or URI is sent. A queue item's
// location is opaque in the same way: two items of one add-on have the same
// location key only when they play the same address. With "check", the host
// also says whether each local file is present, missing, in a missing folder,
// or unknown (no answer in time, no permission, a share that does not
// answer); the check runs on background work, never under the queue lock.
class AddonGuestProjections final : public QObject
{
    Q_OBJECT
public:
    // location (the normalized address) and file (the local path, for a
    // local file) stay in the host: the add-on gets keys and flags only.
    struct QueueItem { QByteArray key; QString title, artist, album, kind; qint64 durationMs = -1; QByteArray location; QString file; };
    struct Queue { QList<QueueItem> items; qint64 current = -1, total = 0; };
    // Owner thread, memory only: a bounded copy of the queue.
    using QueueReader = std::function<std::optional<Queue>(int offset, int limit)>;
    struct QueueRemoval { int removed = 0; qint64 total = 0; };
    // Owner thread: removes, under the queue's own lock, each item whose key
    // the predicate selects.
    using QueueRemover = std::function<std::optional<QueueRemoval>(const std::function<bool(const QByteArray& key)>& selected)>;
    struct LibraryPage { QList<std::pair<QByteArray, QVariantMap>> items; bool more = false; };
    using LibraryDone = std::function<void(std::optional<LibraryPage>)>;
    // Runs the query on the library's own thread; done runs on the owner
    // thread. The returned function cancels it.
    using LibraryReader = std::function<std::function<void()>(const QString& collection, const QString& pattern,
                                                            int offset, int limit, LibraryDone done)>;
    // The playing media, or an empty URL.
    using CurrentMedia = std::function<QUrl()>;

    static constexpr int MaxQueueItems = 100;
    static constexpr int MaxQueueChanges = 1000;
    // A queue.snapshot with "check" answers by this time, with the files it
    // did not reach as "unknown" (a host call has 10 seconds).
    static constexpr int CheckMilliseconds = 3000;
    static constexpr int MaxLibraryItems = 50;
    // The collections and their fields the host can project.
    static const QHash<QString, QStringList>& libraryFields();
    static const QStringList& fingerprintAlgorithms();

    explicit AddonGuestProjections(QObject* parent = nullptr);
    ~AddonGuestProjections() override;
    bool mount(AddonHostServices*, QueueReader, LibraryReader, CurrentMedia, QueueRemover = {});

private:
    QString opaque(const QString& package, const QString& kind, const QByteArray& key) const;
    AddonHostServices::Cancel queue(const AddonHostServices::Context&, const QJsonObject&, AddonHostServices::Completion);
    AddonHostServices::Cancel apply(const AddonHostServices::Context&, const QJsonObject&, AddonHostServices::Completion);
    AddonHostServices::Cancel library(const AddonHostServices::Context&, const QJsonObject&, AddonHostServices::Completion);
    AddonHostServices::Cancel fingerprint(const AddonHostServices::Context&, const QJsonObject&, AddonHostServices::Completion);
    QueueReader m_queue;
    QueueRemover m_remove;
    LibraryReader m_library;
    CurrentMedia m_media;
    QByteArray m_salt;
    std::shared_ptr<std::atomic_bool> m_closed;
};
}
