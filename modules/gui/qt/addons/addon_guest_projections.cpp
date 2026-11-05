// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_guest_projections.hpp"
#include "util/media_fingerprint.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QMessageAuthenticationCode>
#include <QPointer>
#include <QRandomGenerator>
#include <QThreadPool>
#include <QSet>
#include <QTimer>
#include <atomic>
#include <cmath>
#include <mutex>
#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <sys/stat.h>
#endif

namespace vlc::addons {
namespace {
// An integer request field within limits; a missing field takes the default.
bool integer(const QJsonObject& request, const QString& name, int minimum, int maximum, int fallback, int* output)
{
    const auto value = request.value(name);
    if (value.isUndefined()) { *output = fallback; return true; }
    const double number = value.toDouble(-1);
    if (!value.isDouble() || number < minimum || number > maximum || std::floor(number) != number) return false;
    *output = int(number);
    return true;
}
bool only(const QJsonObject& request, const QStringList& keys)
{
    for (auto it = request.begin(); it != request.end(); ++it) if (!keys.contains(it.key())) return false;
    return true;
}
QStringList strings(const QJsonValue& value)
{
    QStringList result;
    for (const auto& item : value.toArray()) result.append(item.toString());
    return result;
}
QString text(const QString& value) { return value.left(512); }

// Whether a local path exists. "Missing" only when the system says that the
// name is not there; a permission, I/O, network or other error is
// "unknown", so that a file on a share that does not answer, or in a folder
// the app cannot read, never counts as gone. Attributes only: the file is
// not opened, so a cloud placeholder (OneDrive, iCloud) is not downloaded.
enum class Presence { Present, Missing, Unknown };
Presence presence(const QString& path)
{
#if defined(Q_OS_WIN)
    auto native = QDir::toNativeSeparators(path);
    // Long paths need the extended prefix; UNC paths take its UNC form.
    if (native.size() >= MAX_PATH - 12 && !native.startsWith(QLatin1String("\\\\?\\")))
        native = native.startsWith(QLatin1String("\\\\")) ? QStringLiteral("\\\\?\\UNC\\") + native.mid(2) : QStringLiteral("\\\\?\\") + native;
    // No "insert the disk" dialog for an empty removable drive.
    // This thread only: SetErrorMode would change the whole process.
    DWORD previous = 0;
    const bool quiet = SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &previous);
    const DWORD attributes = GetFileAttributesW(reinterpret_cast<const wchar_t*>(native.utf16()));
    const DWORD error = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
    if (quiet) SetThreadErrorMode(previous, nullptr);
    if (attributes != INVALID_FILE_ATTRIBUTES) return Presence::Present;
    switch (error) {
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: case ERROR_INVALID_DRIVE:
    case ERROR_NOT_READY: // a removable drive without its medium
        return Presence::Missing;
    default: // for example ERROR_ACCESS_DENIED, ERROR_BAD_NETPATH, ERROR_BAD_NET_NAME
        return Presence::Unknown;
    }
#else
    struct stat info;
    const auto native = QFile::encodeName(path);
    if (::stat(native.constData(), &info) == 0) return Presence::Present;
    // ENOENT: no such name (also a broken link); ENOTDIR: a path part is a file.
    return errno == ENOENT || errno == ENOTDIR ? Presence::Missing : Presence::Unknown;
#endif
}

// The parent folder of a local path, by text only: no file system access.
QString folderOf(const QString& path)
{
    const auto cut = path.lastIndexOf(QLatin1Char('/'));
    if (cut < 0) return {};
    // Keep the root: "/" or "C:/".
    if (cut == 0 || (cut == 2 && path.at(1) == QLatin1Char(':'))) return path.left(cut + 1);
    return path.left(cut);
}

// File checks run here, not on the global pool: a stale network mount can
// block a thread in the system call for minutes, and the app's own
// background work must not wait behind it. The pool is never destroyed,
// so the app does not wait for a blocked check when it closes.
QThreadPool* checkPool()
{
    static QThreadPool* pool = [] {
        auto* value = new QThreadPool;
        value->setMaxThreadCount(2);
        value->setExpiryTimeout(30000);
        return value;
    }();
    return pool;
}
}

const QHash<QString, QStringList>& AddonGuestProjections::libraryFields()
{
    static const QHash<QString, QStringList> fields{
        {QStringLiteral("tracks"), {QStringLiteral("title"), QStringLiteral("durationMs"), QStringLiteral("year"), QStringLiteral("trackNumber")}},
        {QStringLiteral("albums"), {QStringLiteral("title"), QStringLiteral("artist"), QStringLiteral("year"), QStringLiteral("trackCount"), QStringLiteral("durationMs")}},
        {QStringLiteral("artists"), {QStringLiteral("title"), QStringLiteral("albumCount"), QStringLiteral("trackCount")}},
        {QStringLiteral("videos"), {QStringLiteral("title"), QStringLiteral("durationMs"), QStringLiteral("year")}},
        {QStringLiteral("playlists"), {QStringLiteral("title"), QStringLiteral("trackCount"), QStringLiteral("durationMs")}}};
    return fields;
}

const QStringList& AddonGuestProjections::fingerprintAlgorithms()
{
    static const QStringList algorithms{QStringLiteral("opensubtitles-hash")};
    return algorithms;
}

AddonGuestProjections::AddonGuestProjections(QObject* parent)
    : QObject(parent), m_closed(std::make_shared<std::atomic_bool>(false))
{
    m_salt.resize(32);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(m_salt.data()), m_salt.size() / 4);
}

AddonGuestProjections::~AddonGuestProjections() { m_closed->store(true); }

bool AddonGuestProjections::mount(AddonHostServices* services, QueueReader queueReader, LibraryReader libraryReader,
                                  CurrentMedia media, QueueRemover queueRemover)
{
    if (!services) return false;
    m_queue = std::move(queueReader); m_library = std::move(libraryReader); m_media = std::move(media);
    m_remove = std::move(queueRemover);
    const QPointer<AddonGuestProjections> owner(this);
    const auto handler = [owner](auto method) {
        return [owner, method](const AddonHostServices::Context& context, const QJsonObject& request, AddonHostServices::Completion done) {
            if (!owner) { done({{}, QStringLiteral("This host operation is not available.")}); return AddonHostServices::Cancel{}; }
            return (owner.data()->*method)(context, request, std::move(done));
        };
    };
    bool mounted = true;
    if (m_queue) mounted &= services->registerOperation(QStringLiteral("queue.snapshot"), this, handler(&AddonGuestProjections::queue));
    if (m_remove) mounted &= services->registerOperation(QStringLiteral("queue.apply"), this, handler(&AddonGuestProjections::apply));
    if (m_library) mounted &= services->registerOperation(QStringLiteral("library.query"), this, handler(&AddonGuestProjections::library));
    if (m_media) mounted &= services->registerOperation(QStringLiteral("media.fingerprint"), this, handler(&AddonGuestProjections::fingerprint));
    return mounted;
}

QString AddonGuestProjections::opaque(const QString& package, const QString& kind, const QByteArray& key) const
{
    const QByteArray message = package.toUtf8() + '\n' + kind.toUtf8() + '\n' + key;
    return kind + QLatin1Char(':') + QString::fromLatin1(QMessageAuthenticationCode::hash(message, m_salt, QCryptographicHash::Sha256).toHex().left(32));
}

AddonHostServices::Cancel AddonGuestProjections::queue(const AddonHostServices::Context& context, const QJsonObject& request,
                                                       AddonHostServices::Completion done)
{
    int offset = 0, limit = 0;
    const auto check = request.value(QStringLiteral("check"));
    if (!only(request, {QStringLiteral("offset"), QStringLiteral("limit"), QStringLiteral("check")})
        || !integer(request, QStringLiteral("offset"), 0, 100000, 0, &offset)
        || !integer(request, QStringLiteral("limit"), 1, MaxQueueItems, 50, &limit)
        || (!check.isUndefined() && !check.isBool())) {
        done({{}, QStringLiteral("The queue request is not correct.")});
        return {};
    }
    const auto snapshot = m_queue(offset, limit);
    if (!snapshot) { done({{}, QStringLiteral("The queue is not available.")}); return {}; }
    QJsonArray items;
    QStringList files;
    for (const auto& item : snapshot->items.mid(0, limit)) {
        QJsonObject row{{"id", opaque(context.package, QStringLiteral("queue"), item.key)}, {"title", text(item.title)}};
        if (!item.artist.isEmpty()) row.insert("artist", text(item.artist));
        if (!item.album.isEmpty()) row.insert("album", text(item.album));
        if (!item.kind.isEmpty()) row.insert("kind", item.kind);
        if (item.durationMs >= 0) row.insert("durationMs", double(item.durationMs));
        if (!item.location.isEmpty()) row.insert("location", opaque(context.package, QStringLiteral("location"), item.location));
        if (!item.file.isEmpty()) row.insert("file", true);
        items.append(row);
        files.append(item.file);
    }
    QJsonObject response{{"offset", offset}, {"total", double(snapshot->total)}, {"current", double(snapshot->current)}, {"items", items}};
    if (!check.toBool()) { done({response, {}}); return {}; }
    // A file on a network share or a sleeping disk can be slow to find:
    // check on background work, never on the UI thread or under the lock.
    // The reply comes by the deadline in all cases. Items that the check
    // did not reach are "unknown", and checkIncomplete tells the add-on to
    // stop asking for checks in this scan.
    struct Check {
        std::mutex mutex;
        QStringList availability; // one entry per item; empty: not a file
        bool answered = false;
    };
    const auto state = std::make_shared<Check>();
    for (const auto& file : files) state->availability.append(file.isEmpty() ? QString() : QStringLiteral("unknown"));
    const auto cancelled = std::make_shared<std::atomic_bool>(false);
    const auto closed = m_closed;
    const QPointer<AddonGuestProjections> owner(this);
    const auto finish = [owner, done, response, state, cancelled](bool complete) {
        QStringList availability;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->answered) return;
            state->answered = true;
            availability = state->availability;
        }
        if (cancelled->load() || !owner) return;
        auto reply = response;
        auto rows = reply.value(QStringLiteral("items")).toArray();
        for (qsizetype i = 0; i < rows.size() && i < availability.size(); ++i) {
            if (availability[i].isEmpty()) continue;
            auto row = rows[i].toObject();
            row.insert(QStringLiteral("availability"), availability[i]);
            rows[i] = row;
        }
        reply.insert(QStringLiteral("items"), rows);
        if (!complete) reply.insert(QStringLiteral("checkIncomplete"), true);
        done({reply, {}});
    };
    QTimer::singleShot(CheckMilliseconds, this, [finish] { finish(false); });
    checkPool()->start([files, state, cancelled, closed, finish] {
        // One check per folder at most: a folder that is gone (an unplugged
        // disk, an unmounted share) answers for each of its files.
        QHash<QString, Presence> folders;
        for (qsizetype i = 0; i < files.size(); ++i) {
            if (cancelled->load() || closed->load()) return;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->answered) return;
            }
            if (files[i].isEmpty()) continue;
            const auto folder = folderOf(files[i]);
            const auto known = folders.constFind(folder);
            Presence result;
            if (known != folders.cend() && *known == Presence::Missing) result = Presence::Missing;
            else result = presence(files[i]);
            QString value = result == Presence::Present ? QStringLiteral("present")
                : result == Presence::Unknown ? QStringLiteral("unknown") : QStringLiteral("missing");
            if (result == Presence::Missing && !folder.isEmpty()) {
                auto parent = folders.constFind(folder);
                if (parent == folders.cend()) parent = folders.insert(folder, presence(folder));
                if (*parent == Presence::Missing) value = QStringLiteral("folderMissing");
                // A folder that the app cannot read cannot prove a file gone.
                else if (*parent == Presence::Unknown) value = QStringLiteral("unknown");
            }
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->answered) return;
            state->availability[i] = value;
        }
        QMetaObject::invokeMethod(QCoreApplication::instance(), [finish] { finish(true); }, Qt::QueuedConnection);
    });
    return [cancelled] { cancelled->store(true); };
}

AddonHostServices::Cancel AddonGuestProjections::apply(const AddonHostServices::Context& context, const QJsonObject& request,
                                                       AddonHostServices::Completion done)
{
    const auto operation = request.value(QStringLiteral("operation")).toString();
    const auto ids = request.value(QStringLiteral("items"));
    if (!only(request, {QStringLiteral("operation"), QStringLiteral("items")}) || !ids.isArray()
        || ids.toArray().isEmpty() || ids.toArray().size() > MaxQueueChanges) {
        done({{}, QStringLiteral("The queue change is not correct.")});
        return {};
    }
    // The host's permission check has already matched the operation to the
    // granted scope. Only removal has a host path today.
    if (operation != QLatin1String("remove")) { done({{}, QStringLiteral("This queue change is not available.")}); return {}; }
    QSet<QString> selected;
    for (const auto& id : ids.toArray()) {
        const auto value = id.toString();
        if (!id.isString() || value.size() != 38 || !value.startsWith(QLatin1String("queue:"))) {
            done({{}, QStringLiteral("The queue change is not correct.")});
            return {};
        }
        selected.insert(value);
    }
    // An ID that is not in the queue now (removed or never seen) is ignored:
    // the reply counts the items that the app removed.
    const auto package = context.package;
    const auto removal = m_remove([this, &selected, &package](const QByteArray& key) {
        return selected.contains(opaque(package, QStringLiteral("queue"), key));
    });
    if (!removal) { done({{}, QStringLiteral("The queue is not available.")}); return {}; }
    done({{{"operation", operation}, {"removed", removal->removed}, {"total", double(removal->total)}}, {}});
    return {};
}

AddonHostServices::Cancel AddonGuestProjections::library(const AddonHostServices::Context& context, const QJsonObject& request,
                                                         AddonHostServices::Completion done)
{
    int offset = 0, limit = 0;
    const auto collection = request.value(QStringLiteral("collection")).toString();
    const auto pattern = request.value(QStringLiteral("query"));
    const auto scope = context.grants.value(QStringLiteral("library.read")).toObject();
    if (!only(request, {QStringLiteral("collection"), QStringLiteral("query"), QStringLiteral("offset"), QStringLiteral("limit")})
        || !libraryFields().contains(collection)
        || (!pattern.isUndefined() && (!pattern.isString() || pattern.toString().size() > 128 || pattern.toString().contains(QChar::Null)))
        || !integer(request, QStringLiteral("offset"), 0, 10000, 0, &offset)
        || !integer(request, QStringLiteral("limit"), 1, MaxLibraryItems, 25, &limit)) {
        done({{}, QStringLiteral("The library request is not correct.")});
        return {};
    }
    if (!strings(scope.value(QStringLiteral("collections"))).contains(collection)) {
        done({{}, QStringLiteral("This library collection needs permission.")});
        return {};
    }
    QStringList fields;
    const auto granted = strings(scope.value(QStringLiteral("fields")));
    for (const auto& field : libraryFields().value(collection)) if (granted.contains(field)) fields.append(field);
    const QPointer<AddonGuestProjections> owner(this);
    const auto package = context.package;
    // The query has no URI, path or file field to return: only granted fields leave the host.
    return m_library(collection, pattern.toString().trimmed(), offset, limit,
        [owner, done, fields, package, collection, offset, limit](std::optional<LibraryPage> page) {
            if (!owner) { done({{}, QStringLiteral("The library is not available.")}); return; }
            if (!page) { done({{}, QStringLiteral("The library is not available.")}); return; }
            QJsonArray items;
            for (const auto& [key, values] : page->items.mid(0, limit)) {
                QJsonObject row{{"id", owner->opaque(package, collection, key)}};
                for (const auto& field : fields) {
                    const auto value = values.value(field);
                    if (!value.isValid() || value.isNull()) continue;
                    if (value.metaType().id() == QMetaType::QString) row.insert(field, text(value.toString()));
                    else row.insert(field, value.toDouble());
                }
                items.append(row);
            }
            done({{{"collection", collection}, {"offset", offset}, {"more", page->more}, {"items", items}}, {}});
        });
}

AddonHostServices::Cancel AddonGuestProjections::fingerprint(const AddonHostServices::Context& context, const QJsonObject& request,
                                                             AddonHostServices::Completion done)
{
    const auto algorithm = request.value(QStringLiteral("algorithm")).toString();
    const auto scope = context.grants.value(QStringLiteral("media.fingerprint")).toObject();
    if (!only(request, {QStringLiteral("algorithm")}) || !fingerprintAlgorithms().contains(algorithm)) {
        done({{}, QStringLiteral("The fingerprint request is not correct.")});
        return {};
    }
    if (!strings(scope.value(QStringLiteral("algorithms"))).contains(algorithm)) {
        done({{}, QStringLiteral("This fingerprint needs permission.")});
        return {};
    }
    const auto url = m_media();
    if (!url.isLocalFile()) { done({{}, QStringLiteral("The playing media is not a file that the app can read.")}); return {}; }
    // A network share can be slow: read on background work, never the UI thread.
    const auto cancelled = std::make_shared<std::atomic_bool>(false);
    const auto closed = m_closed;
    const QPointer<AddonGuestProjections> owner(this);
    QThreadPool::globalInstance()->start([url, cancelled, closed, owner, done, algorithm] {
        const auto result = MediaFingerprint::openSubtitles(url, [cancelled, closed] { return cancelled->load() || closed->load(); });
        QMetaObject::invokeMethod(QCoreApplication::instance(), [owner, done, result, cancelled, algorithm] {
            if (cancelled->load() || !owner) return;
            if (result.hash.isEmpty()) { done({{}, QStringLiteral("The app cannot compute a fingerprint for this file.")}); return; }
            done({{{"algorithm", algorithm}, {"value", result.hash}, {"size", double(result.size)}}, {}});
        }, Qt::QueuedConnection);
    });
    return [cancelled] { cancelled->store(true); };
}
}
