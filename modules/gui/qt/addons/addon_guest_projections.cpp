// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_guest_projections.hpp"
#include "util/media_fingerprint.hpp"
#include <QCoreApplication>
#include <QJsonArray>
#include <QMessageAuthenticationCode>
#include <QPointer>
#include <QRandomGenerator>
#include <QThreadPool>
#include <atomic>
#include <cmath>

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
                                  CurrentMedia media)
{
    if (!services) return false;
    m_queue = std::move(queueReader); m_library = std::move(libraryReader); m_media = std::move(media);
    const QPointer<AddonGuestProjections> owner(this);
    const auto handler = [owner](auto method) {
        return [owner, method](const AddonHostServices::Context& context, const QJsonObject& request, AddonHostServices::Completion done) {
            if (!owner) { done({{}, QStringLiteral("This host operation is not available.")}); return AddonHostServices::Cancel{}; }
            return (owner.data()->*method)(context, request, std::move(done));
        };
    };
    bool mounted = true;
    if (m_queue) mounted &= services->registerOperation(QStringLiteral("queue.snapshot"), this, handler(&AddonGuestProjections::queue));
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
    if (!only(request, {QStringLiteral("offset"), QStringLiteral("limit")})
        || !integer(request, QStringLiteral("offset"), 0, 100000, 0, &offset)
        || !integer(request, QStringLiteral("limit"), 1, MaxQueueItems, 50, &limit)) {
        done({{}, QStringLiteral("The queue request is not correct.")});
        return {};
    }
    const auto snapshot = m_queue(offset, limit);
    if (!snapshot) { done({{}, QStringLiteral("The queue is not available.")}); return {}; }
    QJsonArray items;
    for (const auto& item : snapshot->items.mid(0, limit)) {
        QJsonObject row{{"id", opaque(context.package, QStringLiteral("queue"), item.key)}, {"title", text(item.title)}};
        if (!item.artist.isEmpty()) row.insert("artist", text(item.artist));
        if (!item.album.isEmpty()) row.insert("album", text(item.album));
        if (!item.kind.isEmpty()) row.insert("kind", item.kind);
        if (item.durationMs >= 0) row.insert("durationMs", double(item.durationMs));
        items.append(row);
    }
    done({{{"offset", offset}, {"total", double(snapshot->total)}, {"current", double(snapshot->current)}, {"items", items}}, {}});
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
