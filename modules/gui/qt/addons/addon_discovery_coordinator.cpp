// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_discovery_coordinator.hpp"
#include "util/media_identity.hpp"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace {
constexpr int MaxRequests = 64, MaxViews = 64, MaxHandles = 4096, MaxCursors = 512, MaxPage = 200;
QString opaque() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
QString account(const QVariantMap& registration) { return registration.value(QStringLiteral("accountReference"), QStringLiteral("default")).toString(); }
QString partition(const QVariantMap& registration) { return registration.value(QStringLiteral("cachePartition")).toString(); }
QStringList declaredOperations(const QVariantMap& registration) {
    if (registration.contains(QStringLiteral("operations"))) return registration.value(QStringLiteral("operations")).toStringList();
    return registration.value(QStringLiteral("document")).toMap().value(QStringLiteral("operations")).toStringList();
}
bool identifier(const QString& value) {
    if (value.isEmpty() || value.size() > 512) return false;
    for (qsizetype i = 0; i < value.size(); ++i) {
        const auto ch = value[i];
        if (ch.isNull() || ch.category() == QChar::Other_Control || ch.isLowSurrogate()) return false;
        if (ch.isHighSurrogate() && (++i >= value.size() || !value[i].isLowSurrogate())) return false;
    }
    return true;
}
QString fingerprint(const QVariantMap& context) {
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(QJsonObject::fromVariantMap(context)).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}
QVariantMap safeIdentity(QVariantMap value) {
    if (!value.contains(QStringLiteral("type"))) value.insert(QStringLiteral("type"), value.value(QStringLiteral("kind")));
    // The fixed host identity vocabulary excludes provider URLs, playback
    // options and arbitrary executable/custom fields from display projections.
    return MediaIdentity::toMap(MediaIdentity::fromMap(value));
}
}

AddonDiscoveryCoordinator::AddonDiscoveryCoordinator(Backend backend, QObject* parent)
    : QObject(parent), m_backend(std::move(backend)) {}
AddonDiscoveryCoordinator::~AddonDiscoveryCoordinator()
{
    const auto requests = m_requests;
    const auto cancel = m_backend.cancel, cancelPlayable = m_backend.cancelPlayable;
    for (const auto& request : requests) {
        if (!request.invocation.isEmpty() && cancel) cancel(request.invocation);
        if (!request.gateJob.isEmpty() && cancelPlayable) cancelPlayable(request.gateJob);
    }
}

QVariantMap AddonDiscoveryCoordinator::registration(const QString& source) const
{
    if (m_backend.sources) for (const auto& value : m_backend.sources())
        if (value.toMap().value(QStringLiteral("id")).toString() == source) return value.toMap();
    return {};
}
bool AddonDiscoveryCoordinator::available(const QVariantMap& source, const QString& operation) const
{
    return isCurrent() && source.value(QStringLiteral("enabled")).toBool() && source.value(QStringLiteral("configured")).toBool()
        && !partition(source).isEmpty() && declaredOperations(source).contains(operation);
}
bool AddonDiscoveryCoordinator::isCurrent() const
{
    const auto current = m_backend.current;
    return !current || current();
}
QVariantMap AddonDiscoveryCoordinator::sourceIdentity(const QString& source) const
{
    const auto entry = registration(source);
    if (entry.isEmpty()) return {};
    return {{QStringLiteral("partition"), partition(entry)}, {QStringLiteral("account"), account(entry)},
        {QStringLiteral("enabled"), entry.value(QStringLiteral("enabled"))},
        {QStringLiteral("configured"), entry.value(QStringLiteral("configured"))},
        {QStringLiteral("configurationKnown"), entry.value(QStringLiteral("configurationKnown"), true)}};
}
QVariantList AddonDiscoveryCoordinator::sources() const
{
    QVariantList output;
    if (m_backend.sources) for (const auto& value : m_backend.sources()) {
        const auto entry = value.toMap();
        auto operations = declaredOperations(entry);
        if (!m_backend.authorizePlayable) operations.removeAll(QStringLiteral("resolve"));
        output.append(QVariantMap{{QStringLiteral("id"), entry.value(QStringLiteral("id"))},
            {QStringLiteral("name"), entry.value(QStringLiteral("name"))}, {QStringLiteral("enabled"), entry.value(QStringLiteral("enabled"))},
            {QStringLiteral("configured"), entry.value(QStringLiteral("configured"))},
            {QStringLiteral("configurationKnown"), entry.value(QStringLiteral("configurationKnown"), true)},
            {QStringLiteral("operations"), operations}});
    }
    return output;
}
bool AddonDiscoveryCoordinator::supports(const QString& source, const QString& operation) const
{
    if (operation == QLatin1String("resolve") && !m_backend.authorizePlayable) return false;
    return available(registration(source), operation);
}
bool AddonDiscoveryCoordinator::declares(const QString& source, const QString& operation) const
{
    return declaredOperations(registration(source)).contains(operation);
}
bool AddonDiscoveryCoordinator::current(const Handle& handle) const
{
    const auto source = registration(handle.source);
    return isCurrent() && source.value(QStringLiteral("enabled")).toBool() && source.value(QStringLiteral("configured")).toBool()
        && partition(source) == handle.partition && account(source) == handle.account
        && m_views.contains(handle.view) && m_views.value(handle.view) == handle.generation;
}
void AddonDiscoveryCoordinator::revokeHandles(const QString& view)
{
    for (auto it = m_handles.begin(); it != m_handles.end();) {
        if (it->view != view) { ++it; continue; }
        m_stable.remove(it->stable);
        it = m_handles.erase(it);
    }
    for (auto it = m_cursors.begin(); it != m_cursors.end();)
        if (it->view == view) it = m_cursors.erase(it); else ++it;
}
bool AddonDiscoveryCoordinator::enterView(const QString& view, quint64 generation)
{
    if (!identifier(view) || (!m_views.contains(view) && m_views.size() >= MaxViews)) return false;
    if (m_closedViews.contains(view) && generation <= m_closedViews.value(view)) return false;
    if (m_views.contains(view) && m_views.value(view) > generation) return false;
    if (m_views.contains(view) && m_views.value(view) != generation) cancelView(view);
    m_views.insert(view, generation);
    m_closedViews.remove(view);
    return true;
}
QString AddonDiscoveryCoordinator::rejected(const QString& view, quint64 generation, const QString& status, const QString& error)
{
    const auto request = opaque();
    QTimer::singleShot(0, this, [this, request, view, generation, status, error] {
        emit finished(request, status, {{QStringLiteral("view"), view}, {QStringLiteral("generation"), QVariant::fromValue(generation)}}, error);
    });
    return request;
}
QString AddonDiscoveryCoordinator::roots(const QString& source, const QString& view, quint64 generation, const QString& cursor)
{ return start(source, QStringLiteral("roots"), {}, view, generation, cursor); }
QString AddonDiscoveryCoordinator::browse(const QString& node, const QString& view, quint64 generation, const QString& cursor)
{
    const auto found = m_handles.constFind(node);
    if (found == m_handles.cend() || found->kind != QLatin1String("node") || !current(*found)
        || found->view != view || found->generation != generation)
        return rejected(view, generation, QStringLiteral("error"), tr("This discovery folder is no longer valid. Open its source again."));
    return start(found->source, QStringLiteral("browse"), {{QStringLiteral("nodeRef"), found->item}}, view, generation, cursor);
}
QString AddonDiscoveryCoordinator::search(const QString& source, const QString& query, const QVariantMap& filters,
                                         const QString& view, quint64 generation, const QString& cursor)
{
    if (query.size() > 512 || filters.size() > 32)
        return rejected(view, generation, QStringLiteral("error"), tr("The discovery search exceeds its limits."));
    QVariantMap safe;
    for (auto it = filters.cbegin(); it != filters.cend(); ++it)
        if (identifier(it.key()) && it.value().metaType().id() != QMetaType::QVariantMap
            && it.value().metaType().id() != QMetaType::QVariantList && it.value().toString().size() <= 256) safe.insert(it.key(), it.value());
    return start(source, QStringLiteral("search"), {{QStringLiteral("query"), query}, {QStringLiteral("filters"), safe}}, view, generation, cursor);
}
QString AddonDiscoveryCoordinator::suggest(const QString& source, const QVariantMap& context, int limit,
                                          const QString& view, quint64 generation)
{
    if (limit < 1 || limit > 50) return rejected(view, generation, QStringLiteral("error"), tr("Use a limit of 1 to 50 discovery suggestions."));
    QVariantMap safe{{QStringLiteral("limit"), limit}};
    if (context.contains(QStringLiteral("identity"))) safe.insert(QStringLiteral("identity"), safeIdentity(context.value(QStringLiteral("identity")).toMap()));
    if (context.contains(QStringLiteral("language"))) safe.insert(QStringLiteral("language"), context.value(QStringLiteral("language")).toString().left(32));
    return start(source, QStringLiteral("suggest"), safe, view, generation);
}
QString AddonDiscoveryCoordinator::subscribe(const QString& source, const QString& node, const QString& revision,
                                            const QString& view, quint64 generation)
{
    static const QRegularExpression token(QStringLiteral("^[\\x21-\\x7e]{0,256}\\z"));
    if (!token.match(revision).hasMatch())
        return rejected(view, generation, QStringLiteral("error"), tr("The discovery revision is not correct."));
    QString collection;
    if (!node.isEmpty()) {
        const auto found = m_handles.constFind(node);
        if (found == m_handles.cend() || found->kind != QLatin1String("node") || found->source != source || !current(*found)
            || found->view != view || found->generation != generation)
            return rejected(view, generation, QStringLiteral("error"), tr("This discovery folder is no longer valid. Open its source again."));
        collection = found->item;
    }
    return start(source, QStringLiteral("subscribe"), {{QStringLiteral("collectionRef"), collection}, {QStringLiteral("revision"), revision}},
                 view, generation);
}

QString AddonDiscoveryCoordinator::start(const QString& source, const QString& operation, QVariantMap context,
                                        const QString& view, quint64 generation, const QString& cursor, const QString& intent)
{
    if (!isCurrent()) return rejected(view, generation, QStringLiteral("unavailable"), tr("The app updates the source list. Try again when it is ready."));
    if (!enterView(view, generation)) return rejected(view, generation, QStringLiteral("error"), tr("The discovery view is no longer valid."));
    const auto entry = registration(source);
    if (entry.isEmpty() || !entry.value(QStringLiteral("enabled")).toBool())
        return rejected(view, generation, QStringLiteral("unsupported"), tr("Enable this discovery source first."));
    if (!entry.value(QStringLiteral("configured")).toBool())
        return rejected(view, generation, QStringLiteral("auth_required"), tr("Set up this discovery source first."));
    if (!available(entry, operation))
        return rejected(view, generation, QStringLiteral("unsupported"), tr("The source does not support this discovery operation."));
    if (m_requests.size() >= MaxRequests)
        return rejected(view, generation, QStringLiteral("error"), tr("Too many discovery requests are active."));
    if (!cursor.isEmpty()) {
        const auto found = m_cursors.constFind(cursor);
        if (found == m_cursors.cend() || found->source != source || found->operation != operation
            || found->view != view || found->generation != generation || found->partition != partition(entry)
            || found->fingerprint != fingerprint(context))
            return rejected(view, generation, QStringLiteral("error"), tr("The discovery page is no longer valid. Open its source again."));
        context.insert(QStringLiteral("cursor"), found->value);
    }
    const auto request = opaque();
    Request job;
    job.source = source; job.operation = operation; job.context = context; job.view = view;
    job.generation = generation; job.registration = entry; job.intent = intent;
    m_requests.insert(request, job);
    QTimer::singleShot(15000, this, [this, request] {
        if (m_requests.contains(request)) finish(request, QStringLiteral("error"), {}, tr("The discovery request time limit has ended."));
    });
    QTimer::singleShot(0, this, [this, request] {
        const auto found = m_requests.constFind(request);
        if (found == m_requests.cend()) return;
        const auto job = *found;
        const auto now = registration(job.source);
        if (!available(now, job.operation) || partition(now) != partition(job.registration)
            || account(now) != account(job.registration)) {
            finish(request, QStringLiteral("unavailable"), {}, tr("The discovery source changed before the request started.")); return;
        }
        const auto invoke = m_backend.invoke;
        const auto cancel = m_backend.cancel;
        const QPointer<AddonDiscoveryCoordinator> owner(this);
        const auto invocation = invoke ? invoke(job.source, job.operation, job.context) : QString{};
        if (!owner) { if (!invocation.isEmpty() && cancel) cancel(invocation); return; }
        auto current = m_requests.find(request);
        const auto latest = registration(job.source);
        if (current == m_requests.end() || !available(latest, job.operation)
            || partition(latest) != partition(job.registration) || account(latest) != account(job.registration)
            || m_views.value(job.view) != job.generation) {
            if (current != m_requests.end()) finish(request, QStringLiteral("unavailable"), {}, tr("The discovery source changed before the request started."));
            if (!invocation.isEmpty() && cancel) cancel(invocation); return;
        }
        if (invocation.isEmpty() || m_invocations.contains(invocation)) { finish(request, QStringLiteral("error"), {}, tr("The discovery source did not start.")); return; }
        current->invocation = invocation;
        m_invocations.insert(invocation, request);
    });
    return request;
}
void AddonDiscoveryCoordinator::operationFinished(const QString& invocation, const QString& source,
                                                 const QString& status, const QVariantMap& data, const QString& error)
{
    QTimer::singleShot(0, this, [this, invocation, source, status, data, error] { complete(invocation, source, status, data, error); });
}
void AddonDiscoveryCoordinator::complete(const QString& invocation, const QString& source, const QString& status,
                                        const QVariantMap& data, const QString& error)
{
    const auto request = m_invocations.value(invocation);
    auto found = m_requests.find(request);
    if (found == m_requests.end() || found->source != source || found->invocation != invocation) return;
    const auto now = registration(source);
    if (!available(now, found->operation) || partition(now) != partition(found->registration)
        || account(now) != account(found->registration) || m_views.value(found->view) != found->generation) {
        finish(request, QStringLiteral("error"), {}, tr("The discovery source or account changed. Open it again.")); return;
    }
    m_invocations.remove(invocation);
    found->invocation.clear();
    if (status != QLatin1String("match") && status != QLatin1String("partial") && status != QLatin1String("no_match")) {
        finish(request, status, {}, error.left(512)); return;
    }
    // A change feed reports no change with its revision and next check.
    if (status == QLatin1String("no_match") && found->operation != QLatin1String("subscribe")) {
        finish(request, status, {{QStringLiteral("items"), QVariantList{}}}); return;
    }
    if (found->operation == QLatin1String("resolve")) authorize(request, data);
    else page(request, data);
}

void AddonDiscoveryCoordinator::page(const QString& request, const QVariantMap& data)
{
    const auto found = m_requests.constFind(request);
    if (found == m_requests.cend()) return;
    const auto job = *found;
    if ((!data.contains(QStringLiteral("items")) && !data.contains(QStringLiteral("nodes")) && job.operation != QLatin1String("subscribe"))
        || (data.contains(QStringLiteral("items")) && data.value(QStringLiteral("items")).metaType().id() != QMetaType::QVariantList)
        || (data.contains(QStringLiteral("nodes")) && data.value(QStringLiteral("nodes")).metaType().id() != QMetaType::QVariantList)) {
        finish(request, QStringLiteral("error"), {}, tr("The discovery source returned an invalid page.")); return;
    }
    auto entries = data.value(QStringLiteral("items")).toList();
    const auto nodes = data.value(QStringLiteral("nodes")).toList();
    for (auto value = nodes.crbegin(); value != nodes.crend(); ++value) {
        auto node = value->toMap(); node.insert(QStringLiteral("kind"), QStringLiteral("node")); entries.prepend(node);
    }
    if (entries.size() > MaxPage || (job.operation == QLatin1String("suggest") && entries.size() > job.context.value(QStringLiteral("limit")).toInt())) {
        finish(request, QStringLiteral("error"), {}, tr("The discovery source returned too many items.")); return;
    }
    // items: rows new to this view; updates: rows that it showed before.
    // page: every row in source order, for a page that replaces the view.
    QVariantList items, updates, page;
    QHash<QString, Handle> stagedHandles;
    QHash<QString, QString> stagedStable;
    int newHandles = 0;
    QSet<QString> seen;
    for (const auto& value : entries) {
        const auto item = value.toMap();
        // An ID is text: a number or a Boolean is not converted to one.
        const auto itemId = item.value(QStringLiteral("id")).metaType().id() == QMetaType::QString
            ? item.value(QStringLiteral("id")).toString() : QString{};
        const auto kind = item.value(QStringLiteral("kind"), QStringLiteral("media")).toString();
        if (!identifier(itemId) || (kind != QLatin1String("node") && kind != QLatin1String("media")) || seen.contains(itemId)) continue;
        seen.insert(itemId);
        const QString stable = job.source + '\n' + partition(job.registration) + '\n' + account(job.registration)
            + '\n' + job.view + '\n' + QString::number(job.generation) + '\n' + itemId;
        const auto existing = m_stable.value(stable);
        const auto old = m_handles.constFind(existing);
        if (old != m_handles.cend() && old->kind != kind) continue;
        if (old == m_handles.cend() && m_handles.size() + newHandles >= MaxHandles) { finish(request, QStringLiteral("error"), {}, tr("Open this discovery view again before you load more items.")); return; }
        const auto ref = old == m_handles.cend() ? opaque() : existing;
        QVariantMap display{{QStringLiteral("ref"), ref}, {QStringLiteral("source"), job.source}, {QStringLiteral("kind"), kind},
            {QStringLiteral("title"), item.value(QStringLiteral("title")).toString().left(512)},
            {QStringLiteral("mediaKind"), item.value(QStringLiteral("mediaKind")).toString().left(32)}};
        if (item.contains(QStringLiteral("identity"))) display.insert(QStringLiteral("identity"), safeIdentity(item.value(QStringLiteral("identity")).toMap()));
        if (item.contains(QStringLiteral("available"))) display.insert(QStringLiteral("available"), item.value(QStringLiteral("available")).toBool());
        Handle handle;
        handle.source = job.source; handle.item = itemId; handle.kind = kind;
        handle.partition = partition(job.registration); handle.account = account(job.registration);
        handle.view = job.view; handle.generation = job.generation; handle.display = display; handle.stable = stable;
        const bool wasExisting = old != m_handles.cend();
        if (!wasExisting) ++newHandles;
        stagedHandles.insert(ref, handle); stagedStable.insert(stable, ref);
        if (wasExisting) updates.append(display); else items.append(display);
        page.append(display);
    }
    QVariantMap result{{QStringLiteral("items"), items}, {QStringLiteral("updates"), updates}, {QStringLiteral("page"), page}};
    const auto rawCursor = data.value(QStringLiteral("cursor"));
    if (rawCursor.isValid() && !rawCursor.isNull()
        && (rawCursor.metaType().id() != QMetaType::QString || !rawCursor.toString().isEmpty())) {
        if (m_cursors.size() >= MaxCursors) { finish(request, QStringLiteral("error"), {}, tr("The discovery view has too many pages. Open the view again.")); return; }
        if (QJsonDocument(QJsonArray{QJsonValue::fromVariant(rawCursor)}).toJson(QJsonDocument::Compact).size() > 4096) {
            finish(request, QStringLiteral("error"), {}, tr("The discovery page cursor exceeds the size limit.")); return;
        }
        auto context = job.context; context.remove(QStringLiteral("cursor"));
        const auto cursor = opaque();
        m_cursors.insert(cursor, {job.source, job.operation, partition(job.registration), job.view, fingerprint(context), job.generation, data.value(QStringLiteral("cursor"))});
        result.insert(QStringLiteral("cursor"), cursor);
    }
    // A source can name its revision and when to check again; a change feed
    // also names the items it removed. Anything else is ignored.
    static const QRegularExpression token(QStringLiteral("^[\\x21-\\x7e]{1,256}\\z"));
    const auto revision = data.value(QStringLiteral("revision"));
    if (revision.metaType().id() == QMetaType::QString && token.match(revision.toString()).hasMatch())
        result.insert(QStringLiteral("revision"), revision.toString());
    const auto next = data.value(QStringLiteral("nextCheckSeconds"));
    if (next.canConvert<double>() && next.metaType().id() != QMetaType::QString && std::isfinite(next.toDouble())
        && std::floor(next.toDouble()) == next.toDouble())
        result.insert(QStringLiteral("nextCheckSeconds"), int(std::clamp(next.toDouble(), 60.0, 3600.0)));
    QVariantList removed;
    if (job.operation == QLatin1String("subscribe")) {
        const auto ids = data.value(QStringLiteral("removed")).toList();
        if (ids.size() > MaxPage) { finish(request, QStringLiteral("error"), {}, tr("The discovery source removed too many items at once.")); return; }
        for (const auto& id : ids) {
            if (id.metaType().id() != QMetaType::QString) continue;
            const QString stable = job.source + '\n' + partition(job.registration) + '\n' + account(job.registration)
                + '\n' + job.view + '\n' + QString::number(job.generation) + '\n' + id.toString();
            const auto ref = m_stable.value(stable);
            if (!identifier(id.toString()) || ref.isEmpty() || seen.contains(id.toString())) continue;
            removed.append(ref);
        }
        result.insert(QStringLiteral("removed"), removed);
    }
    for (auto it = stagedHandles.cbegin(); it != stagedHandles.cend(); ++it) m_handles.insert(it.key(), it.value());
    for (auto it = stagedStable.cbegin(); it != stagedStable.cend(); ++it) m_stable.insert(it.key(), it.value());
    for (const auto& ref : removed) {
        const auto handle = m_handles.take(ref.toString());
        m_stable.remove(handle.stable);
    }
    finish(request, items.isEmpty() && updates.isEmpty() && removed.isEmpty() ? QStringLiteral("no_match") : QStringLiteral("match"), result);
}

QVariantMap AddonDiscoveryCoordinator::queueReference(const QString& item) const
{
    const auto found = m_handles.constFind(item);
    if (found == m_handles.cend() || found->kind != QLatin1String("media") || !current(*found)) return {};
    return {{QStringLiteral("format"), QStringLiteral("hakone-addon-source-v1")}, {QStringLiteral("source"), found->source},
        {QStringLiteral("itemId"), found->item}, {QStringLiteral("accountReference"), found->account},
        {QStringLiteral("snapshot"), QVariantMap{{QStringLiteral("title"), found->display.value(QStringLiteral("title"))},
            {QStringLiteral("mediaKind"), found->display.value(QStringLiteral("mediaKind"))}}}};
}
QString AddonDiscoveryCoordinator::resolve(const QString& item, const QString& intent, const QString& view, quint64 generation)
{
    const auto found = m_handles.constFind(item);
    if (found == m_handles.cend() || found->kind != QLatin1String("media") || !current(*found)
        || found->view != view || found->generation != generation)
        return rejected(view, generation, QStringLiteral("error"), tr("This discovery item is no longer valid. Open its source again."));
    return resolveStable(found->source, found->item, found->account, intent, view, generation);
}
QString AddonDiscoveryCoordinator::resolveQueued(const QVariantMap& reference, const QString& intent,
                                                const QString& view, quint64 generation)
{
    if (reference.value(QStringLiteral("format")).toString() != QLatin1String("hakone-addon-source-v1"))
        return rejected(view, generation, QStringLiteral("error"), tr("This saved discovery reference is invalid."));
    return resolveStable(reference.value(QStringLiteral("source")).toString(), reference.value(QStringLiteral("itemId")).toString(),
        reference.value(QStringLiteral("accountReference")).toString(), intent, view, generation);
}
QString AddonDiscoveryCoordinator::resolveStable(const QString& source, const QString& item, const QString& accountRef,
                                                const QString& intent, const QString& view, quint64 generation)
{
    if (!m_backend.authorizePlayable) return rejected(view, generation, QStringLiteral("unsupported"), tr("This host has no discovery playback policy adapter."));
    if (!identifier(item) || account(registration(source)) != accountRef || (intent != QLatin1String("play") && intent != QLatin1String("enqueue")))
        return rejected(view, generation, QStringLiteral("error"), tr("This discovery item or playback intent is invalid."));
    return start(source, QStringLiteral("resolve"), {{QStringLiteral("itemRef"), item}, {QStringLiteral("intent"), intent}}, view, generation, {}, intent);
}
void AddonDiscoveryCoordinator::authorize(const QString& request, const QVariantMap& proposal)
{
    const auto found = m_requests.constFind(request);
    if (found == m_requests.cend()) return;
    const auto job = *found;
    if (!m_backend.authorizePlayable) { finish(request, QStringLiteral("unsupported"), {}, tr("Discovery playback is not available.")); return; }
    for (const auto& key : {QStringLiteral("options"), QStringLiteral("vlcOptions"), QStringLiteral("sout"), QStringLiteral("access"), QStringLiteral("path"), QStringLiteral("file"), QStringLiteral("uri")})
        if (proposal.contains(key)) { finish(request, QStringLiteral("error"), {}, tr("The source returned playback properties that are not permitted.")); return; }
    const QUrl url(proposal.value(QStringLiteral("url")).toString(), QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != QLatin1String("https") || url.host().isEmpty() || (!url.userInfo().isEmpty() || url.authority().contains(QLatin1Char('@')))) {
        finish(request, QStringLiteral("error"), {}, tr("The source returned an unsupported playback protocol.")); return;
    }
    auto guarded = proposal;
    guarded.insert(QStringLiteral("hostContext"), QVariantMap{{QStringLiteral("view"), job.view},
        {QStringLiteral("generation"), QVariant::fromValue(job.generation)}, {QStringLiteral("intent"), job.intent}});
    const QPointer<AddonDiscoveryCoordinator> owner(this);
    const auto authorizePlayable = m_backend.authorizePlayable;
    const auto cancelPlayable = m_backend.cancelPlayable;
    const auto gateJob = authorizePlayable(job.source, guarded, [owner, request](const QString& handle, const QString& error) {
        if (!owner) return;
        QTimer::singleShot(0, owner, [owner, request, handle, error] {
            if (!owner || !owner->m_requests.contains(request)) return;
            const auto job = owner->m_requests.value(request);
            const auto now = owner->registration(job.source);
            if (partition(now) != partition(job.registration) || account(now) != account(job.registration)
                || !owner->available(now, QStringLiteral("resolve"))) {
                owner->finish(request, QStringLiteral("error"), {}, tr("The discovery source changed before playback authorization.")); return;
            }
            static const QRegularExpression opaqueHandle(QStringLiteral("^[A-Za-z0-9_.:-]{1,256}\\z"));
            if (!error.isEmpty() || !opaqueHandle.match(handle).hasMatch()) {
                owner->finish(request, QStringLiteral("error"), {}, error.isEmpty() ? tr("The playback adapter did not issue an opaque handle.") : error.left(512)); return;
            }
            owner->m_requests[request].gateJob.clear();
            owner->finish(request, QStringLiteral("match"), {{QStringLiteral("playbackHandle"), handle}});
        });
    });
    if (!owner) { if (!gateJob.isEmpty() && cancelPlayable) cancelPlayable(gateJob); return; }
    auto current = m_requests.find(request);
    if (current != m_requests.end()) current->gateJob = gateJob;
    else if (!gateJob.isEmpty() && cancelPlayable) cancelPlayable(gateJob);
}

void AddonDiscoveryCoordinator::finish(const QString& request, const QString& status, QVariantMap data, const QString& error)
{
    const auto found = m_requests.constFind(request);
    if (found == m_requests.cend()) return;
    const auto job = *found;
    m_requests.remove(request); m_invocations.remove(job.invocation);
    const auto cancel = m_backend.cancel, cancelPlayable = m_backend.cancelPlayable;
    const QPointer<AddonDiscoveryCoordinator> owner(this);
    if (!job.invocation.isEmpty() && cancel) cancel(job.invocation);
    if (!job.gateJob.isEmpty() && cancelPlayable) cancelPlayable(job.gateJob);
    if (!owner) return;
    data.insert(QStringLiteral("view"), job.view); data.insert(QStringLiteral("generation"), QVariant::fromValue(job.generation));
    data.insert(QStringLiteral("source"), job.source);
    emit finished(request, status, data, error);
}
void AddonDiscoveryCoordinator::cancel(const QString& request)
{
    const auto found = m_requests.constFind(request);
    if (found == m_requests.cend()) return;
    const auto job = *found;
    m_requests.remove(request); m_invocations.remove(job.invocation);
    const auto cancel = m_backend.cancel, cancelPlayable = m_backend.cancelPlayable;
    const QPointer<AddonDiscoveryCoordinator> owner(this);
    if (!job.invocation.isEmpty() && cancel) cancel(job.invocation);
    if (!job.gateJob.isEmpty() && cancelPlayable) cancelPlayable(job.gateJob);
    if (!owner) return;
    emit canceled(request);
}
void AddonDiscoveryCoordinator::cancelView(const QString& view)
{
    const auto requests = m_requests.keys();
    const QPointer<AddonDiscoveryCoordinator> owner(this);
    for (const auto& request : requests) if (m_requests.value(request).view == view) {
        cancel(request); if (!owner) return;
    }
    revokeHandles(view);
    if (m_views.contains(view)) {
        if (m_closedViews.size() >= MaxViews * 2) m_closedViews.erase(m_closedViews.begin());
        m_closedViews.insert(view, m_views.value(view));
    }
    m_views.remove(view);
}
