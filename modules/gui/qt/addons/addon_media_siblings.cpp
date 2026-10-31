// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_media_siblings.hpp"
#include <QJsonArray>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QTimer>
#include <cmath>
#include <map>

namespace vlc::addons {
namespace {
bool integer(const QJsonObject& request, const QString& name, int minimum, int maximum, int fallback, int* output)
{
    const auto value = request.value(name);
    if (value.isUndefined()) { *output = fallback; return true; }
    const double number = value.toDouble(-1e9);
    if (!value.isDouble() || number < minimum || number > maximum || std::floor(number) != number) return false;
    *output = int(number);
    return true;
}
QString text(const QString& value, int limit) { return value.left(limit); }
}

AddonMediaSiblings::AddonMediaSiblings(QObject* parent) : QObject(parent), m_alive(std::make_shared<bool>(true))
{
    m_salt.resize(32);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(m_salt.data()), m_salt.size() / 4);
}

AddonMediaSiblings::~AddonMediaSiblings()
{
    *m_alive = false;
    const auto waiting = std::move(m_waiting);
    for (const auto& entry : waiting) entry.done({{}, QStringLiteral("The episode list is not available.")});
}

bool AddonMediaSiblings::mount(AddonHostServices* services, Source source)
{
    if (!services || !source.snapshot) return false;
    m_source = std::move(source);
    const QPointer<AddonMediaSiblings> owner(this);
    return services->registerOperation(QStringLiteral("media.siblings"), this,
        [owner](const AddonHostServices::Context& context, const QJsonObject& request, AddonHostServices::Completion done) {
            if (!owner) { done({{}, QStringLiteral("This host operation is not available.")}); return AddonHostServices::Cancel{}; }
            return owner->read(context, request, std::move(done));
        });
}

QString AddonMediaSiblings::opaque(const QString& package, const QUrl& url) const
{
    const QByteArray message = package.toUtf8() + "\nsibling\n" + url.toString(QUrl::FullyEncoded).toUtf8();
    return QStringLiteral("media:") + QString::fromLatin1(QMessageAuthenticationCode::hash(message, m_salt, QCryptographicHash::Sha256).toHex().left(32));
}

AddonHostServices::Cancel AddonMediaSiblings::read(const AddonHostServices::Context& context, const QJsonObject& request,
                                                   AddonHostServices::Completion done)
{
    int offset = 0, limit = 0, season = 0;
    for (auto it = request.begin(); it != request.end(); ++it)
        if (!QStringList{"offset", "limit", "season", "artwork"}.contains(it.key())) {
            done({{}, QStringLiteral("The episode request is not correct.")}); return {};
        }
    const auto artwork = request.value(QStringLiteral("artwork"));
    if (!integer(request, QStringLiteral("offset"), 0, 10000, 0, &offset) || !integer(request, QStringLiteral("limit"), 1, MaxItems, 50, &limit)
        || !integer(request, QStringLiteral("season"), -1, 9999, -2, &season)
        || (!artwork.isUndefined() && (!artwork.isArray() || artwork.toArray().size() > MaxArtworkRequests))) {
        done({{}, QStringLiteral("The episode request is not correct.")}); return {};
    }
    if (!m_source.snapshot().loading) { answer(context, request, done, false); return {}; }
    // A folder scan or a feed is still loading: answer when it ends.
    const auto id = ++m_nextWait;
    m_waiting.append({id, context, request, std::move(done)});
    const std::weak_ptr<bool> alive = m_alive;
    QTimer::singleShot(WaitMilliseconds, this, [this, id] {
        for (int i = 0; i < m_waiting.size(); ++i)
            if (m_waiting[i].id == id) { const auto entry = m_waiting.takeAt(i); answer(entry.context, entry.request, entry.done, true); return; }
    });
    const QPointer<AddonMediaSiblings> owner(this);
    return [owner, id] {
        if (!owner) return;
        for (int i = 0; i < owner->m_waiting.size(); ++i) if (owner->m_waiting[i].id == id) { owner->m_waiting.removeAt(i); return; }
    };
}

void AddonMediaSiblings::answer(const AddonHostServices::Context& context, const QJsonObject& request,
                                const AddonHostServices::Completion& done, bool timedOut)
{
    const auto snapshot = m_source.snapshot();
    const auto sources = context.grants.value(QStringLiteral("media.siblings")).toObject().value(QStringLiteral("sources")).toArray();
    const int offset = request.value(QStringLiteral("offset")).toInt(0), limit = request.value(QStringLiteral("limit")).toInt(50);
    const int season = request.contains(QStringLiteral("season")) ? request.value(QStringLiteral("season")).toInt() : -2;
    auto& ids = m_ids[context.package];
    if (ids.size() > 4096) ids.clear();
    QJsonObject response{{"kind", "none"}, {"loading", false}, {"limited", false}, {"offset", offset}, {"total", 0},
                         {"seasons", QJsonArray{}}, {"items", QJsonArray{}}};
    if (snapshot.kind.isEmpty() || !sources.contains(snapshot.kind)) { done({response, {}}); return; }
    response.insert(QStringLiteral("kind"), snapshot.kind);
    response.insert(QStringLiteral("show"), text(snapshot.show, 512));
    if (!snapshot.message.isEmpty()) response.insert(QStringLiteral("message"), text(snapshot.message, 512));
    response.insert(QStringLiteral("loading"), snapshot.loading || timedOut);
    response.insert(QStringLiteral("limited"), snapshot.limited);
    // Seasons with their number of episodes (first versions), in order.
    std::map<int, int> seasons;
    for (const auto& item : snapshot.items) if (item.version == 0) ++seasons[item.season];
    QJsonArray seasonList;
    for (const auto& [number, count] : seasons) seasonList.append(QJsonObject{{"season", number}, {"count", count}});
    response.insert(QStringLiteral("seasons"), seasonList);
    QList<const Item*> selected;
    for (const auto& item : snapshot.items) if (season == -2 || item.season == season) selected.append(&item);
    response.insert(QStringLiteral("total"), selected.size());
    QJsonArray items;
    auto& artwork = m_artwork[context.package];
    if (artwork.size() > 4096) artwork.clear();
    for (const auto* item : selected.mid(offset, limit)) {
        const auto id = opaque(context.package, item->url);
        ids.insert(id, item->url);
        QJsonArray episodes;
        for (int episode : item->episodes) episodes.append(episode);
        QJsonObject row{{"id", id}, {"kind", item->kind}, {"title", text(item->title, 1024)}, {"season", item->season},
                        {"episodes", episodes}, {"version", item->version}, {"progress", std::clamp(item->progress, 0.0, 1.0)},
                        {"played", item->played}, {"current", item->current}, {"video", item->video}};
        if (item->kind == QLatin1String("folder")) {
            row.insert(QStringLiteral("name"), text(item->name, 512));
            if (!item->folder.isEmpty()) row.insert(QStringLiteral("folder"), text(item->folder, 1024));
        }
        if (!item->absolute.isEmpty()) row.insert(QStringLiteral("absolute"), text(item->absolute, 32));
        if (item->airDate.isValid()) row.insert(QStringLiteral("airDate"), item->airDate.toString(Qt::ISODate));
        if (item->published.isValid()) row.insert(QStringLiteral("published"), item->published.toUTC().toString(Qt::ISODate));
        if (!item->description.isEmpty()) row.insert(QStringLiteral("description"), text(item->description, 4096));
        if (item->titleSource.scheme() == QLatin1String("https")) row.insert(QStringLiteral("hasSource"), true);
        if (!item->artwork.isEmpty()) { row.insert(QStringLiteral("artwork"), id); artwork.insert(id, item->artwork); }
        items.append(row);
    }
    response.insert(QStringLiteral("items"), items);
    // Artwork is made only for the few items a panel shows in full.
    if (m_source.artwork && snapshot.kind == QLatin1String("folder"))
        for (const auto& value : request.value(QStringLiteral("artwork")).toArray()) {
            const auto url = ids.value(value.toString());
            if (!url.isEmpty()) m_source.artwork(url);
        }
    done({response, {}});
}

const AddonMediaSiblings::Item* AddonMediaSiblings::find(const Snapshot& snapshot, const QString& package, const QString& id) const
{
    const auto url = m_ids.value(package).value(id);
    if (url.isEmpty()) return nullptr;
    for (const auto& item : snapshot.items) if (item.url == url) return &item;
    return nullptr;
}

bool AddonMediaSiblings::runEffect(const QString& package, const QString& effect, const QVariantMap& value)
{
    // Only a package that read this media's siblings can act on them.
    if (!m_ids.contains(package) || !m_source.snapshot) return false;
    const auto snapshot = m_source.snapshot();
    if (snapshot.kind.isEmpty()) return false;
    if (effect == QLatin1String("play") || effect == QLatin1String("source")) {
        const auto id = value.value(QStringLiteral("id"));
        const auto start = value.value(QStringLiteral("fromStart"), false);
        if (id.metaType().id() != QMetaType::QString || start.metaType().id() != QMetaType::Bool) return false;
        const auto* item = find(snapshot, package, id.toString());
        if (!item) return false;
        if (effect == QLatin1String("play")) { emit playRequested(item->url, item->feedIndex, item->version, start.toBool()); return true; }
        if (item->titleSource.scheme() != QLatin1String("https")) return false;
        emit sourceRequested(item->titleSource); return true;
    }
    if (effect == QLatin1String("rescan")) {
        const auto choose = value.value(QStringLiteral("chooseFolder"), false);
        if (choose.metaType().id() != QMetaType::Bool || (choose.toBool() && snapshot.kind != QLatin1String("folder"))) return false;
        emit rescanRequested(choose.toBool()); return true;
    }
    if (effect == QLatin1String("identify") && snapshot.kind == QLatin1String("folder")) { emit identifyRequested(); return true; }
    return false;
}

QVariantMap AddonMediaSiblings::artwork(const QString& package) const { return m_artwork.value(package); }

void AddonMediaSiblings::sourceChanged()
{
    ++m_revision;
    const std::weak_ptr<bool> alive = m_alive;
    emit changed();
    if (alive.expired() || m_waiting.isEmpty() || !m_source.snapshot || m_source.snapshot().loading) return;
    const auto waiting = std::move(m_waiting); m_waiting.clear();
    for (const auto& entry : waiting) { if (alive.expired()) return; answer(entry.context, entry.request, entry.done, false); }
}
}
