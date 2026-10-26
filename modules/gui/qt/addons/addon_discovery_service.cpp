// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_discovery_service.hpp"
#include "addon_manager.hpp"
#include <QTimer>
#include <QUuid>

namespace vlc::addons {
AddonDiscoveryCoordinator::Backend AddonDiscoveryService::managerBackend(AddonManager* manager, AddonDiscoveryCoordinator::Backend backend)
{
    const QPointer<AddonManager> owner(manager);
    backend.sources = [owner] {
        QVariantList result; if (!owner) return result;
        QHash<QString, bool> configured;
        for (const auto& value : owner->packages()) { const auto row = value.toMap(); configured.insert(row.value("id").toString(), row.value("configured").toBool()); }
        for (const auto& value : owner->contributionsOfType(QStringLiteral("discovery.source"), true)) {
            auto row = value.toMap();
            const bool accountKnown = row.value("accountReference").metaType().id() == QMetaType::QString && !row.value("accountReference").toString().isEmpty();
            row.insert("configured", configured.value(row.value("packageId").toString()) && accountKnown); result.append(row);
        }
        return result;
    };
    backend.invoke = [owner](const QString& source, const QString& operation, const QVariantMap& context) { return owner ? owner->invoke(source, operation, context) : QString(); };
    backend.cancel = [owner](const QString& request) { if (owner) owner->cancel(request); };
    return backend;
}
AddonDiscoveryService::AddonDiscoveryService(AddonManager* manager, AddonDiscoveryCoordinator::Backend backend,
                                           AddonQueueTransactions::Backend queues, QObject* parent)
    : AddonDiscoveryService(managerBackend(manager, std::move(backend)), std::move(queues), parent)
{
    connect(manager, &AddonManager::operationFinished, this, &AddonDiscoveryService::operationFinished);
    connect(manager, &AddonManager::revisionChanged, this, &AddonDiscoveryService::invalidate);
    connect(manager, &QObject::destroyed, this, &AddonDiscoveryService::invalidate);
}
AddonDiscoveryService::AddonDiscoveryService(AddonDiscoveryCoordinator::Backend backend,
                                           AddonQueueTransactions::Backend queues, QObject* parent)
    : QObject(parent), m_coordinator(std::move(backend)), m_transactions(std::move(queues))
{
    connect(&m_coordinator, &AddonDiscoveryCoordinator::finished, this, &AddonDiscoveryService::completed);
    connect(&m_transactions, &AddonQueueTransactions::finished, this, [this](const QString& request, bool success, const QString& error) {
        m_playActions.remove(request);
        const auto view = m_actions.take(request); if (!view.isEmpty() && m_views.contains(view)) emit actionFinished(view, success, error);
    });
}
QString AddonDiscoveryService::openSource(const QString& source)
{
    if (m_views.size() >= 16) return {};
    bool available = false;
    for (const auto& value : sources()) { const auto row = value.toMap(); if (row.value("id").toString() == source && row.value("enabled").toBool() && row.value("configured").toBool()) available = true; }
    if (!available) return {};
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    View view; view.source = source; m_views.insert(id, view);
    QTimer::singleShot(0, this, [this, id] {
        if (!m_views.contains(id)) return;
        if (m_coordinator.supports(m_views.value(id).source, QStringLiteral("roots"))) fetch(id);
        else emit viewChanged(id);
    });
    return id;
}
QVariantMap AddonDiscoveryService::view(const QString& id) const
{
    const auto it = m_views.constFind(id); if (it == m_views.cend()) return {};
    return {{"id", id}, {"source", it->source}, {"items", it->items}, {"busy", it->busy}, {"error", it->error},
        {"canGoBack", !it->path.isEmpty() || !it->query.isEmpty()}, {"hasMore", !it->cursor.isEmpty()},
        {"query", it->query}, {"canPlay", canPlay(id)}, {"canEnqueue", canEnqueue(id)}};
}
bool AddonDiscoveryService::fetch(const QString& id, bool append)
{
    auto it = m_views.find(id); if (it == m_views.end()) return false;
    if (!it->pending.isEmpty()) m_coordinator.cancel(it->pending);
    it->error.clear(); it->busy = true; it->append = append;
    const auto cursor = append ? it->cursor : QString();
    if (!append) { it->items.clear(); it->cursor.clear(); }
    if (!it->query.isEmpty()) it->pending = m_coordinator.search(it->source, it->query, {}, id, it->generation, cursor);
    else if (!it->path.isEmpty()) it->pending = m_coordinator.browse(it->path.last(), id, it->generation, cursor);
    else it->pending = m_coordinator.roots(it->source, id, it->generation, cursor);
    emit viewChanged(id); return true;
}
bool AddonDiscoveryService::browse(const QString& id, const QString& node)
{
    auto it = m_views.find(id); if (it == m_views.end()) return false;
    bool found = false; for (const auto& value : it->items) { const auto row = value.toMap(); if (row.value("ref").toString() == node && row.value("kind").toString() == QLatin1String("node")) found = true; }
    if (!found || !m_coordinator.supports(it->source, QStringLiteral("browse"))) return false;
    it->query.clear(); it->path.append(node); return fetch(id);
}
bool AddonDiscoveryService::search(const QString& id, const QString& query)
{
    auto it = m_views.find(id); if (it == m_views.end() || query.size() > 512 || !m_coordinator.supports(it->source, QStringLiteral("search"))) return false;
    it->path.clear(); it->query = query.trimmed(); return fetch(id);
}
bool AddonDiscoveryService::back(const QString& id)
{
    auto it = m_views.find(id); if (it == m_views.end() || (it->path.isEmpty() && it->query.isEmpty())) return false;
    if (!it->query.isEmpty()) it->query.clear(); else it->path.removeLast();
    return fetch(id);
}
bool AddonDiscoveryService::loadMore(const QString& id)
{ const auto it = m_views.constFind(id); return it != m_views.cend() && !it->busy && !it->cursor.isEmpty() && it->items.size() < 1000 && fetch(id, true); }
bool AddonDiscoveryService::visibleMedia(const View& view, const QString& item) const
{
    for (const auto& value : view.items) { const auto row = value.toMap();
        if (row.value("ref").toString() == item && row.value("kind").toString() == QLatin1String("media") && row.value("available", true).toBool()) return true; }
    return false;
}
bool AddonDiscoveryService::canPlay(const QString& id) const
{ const auto it = m_views.constFind(id); return it != m_views.cend() && m_transactions.playAvailable() && m_coordinator.supports(it->source, QStringLiteral("resolve")); }
bool AddonDiscoveryService::canEnqueue(const QString& id) const
{ return m_views.contains(id) && m_transactions.appendAvailable(); }
QString AddonDiscoveryService::enqueue(const QString& id, const QStringList& items, const QString& target)
{
    const auto it = m_views.constFind(id); if (it == m_views.cend() || !canEnqueue(id) || items.isEmpty() || items.size() > 200) return {};
    QVariantList references; QSet<QString> seen;
    for (const auto& item : items) {
        if (seen.contains(item) || !visibleMedia(*it, item)) return {};
        seen.insert(item); const auto reference = m_coordinator.queueReference(item); if (reference.isEmpty()) return {};
        references.append(reference);
    }
    const auto request = m_transactions.append(id, references, target); m_actions.insert(request, id); return request;
}
QString AddonDiscoveryService::play(const QString& id, const QString& item)
{
    const auto it = m_views.constFind(id); if (it == m_views.cend() || !canPlay(id) || !visibleMedia(*it, item)) return {};
    const auto snapshot = m_transactions.snapshot(); if (!snapshot.available) return {};
    const auto pending = m_plays.keys(); for (const auto& request : pending) if (m_plays.value(request).view == id) { m_plays.remove(request); m_coordinator.cancel(request); }
    const auto actions = m_playActions.keys(id); for (const auto& request : actions) { m_actions.remove(request); m_playActions.remove(request); m_transactions.cancel(request); }
    const auto request = m_coordinator.resolve(item, QStringLiteral("play"), id, it->generation);
    m_plays.insert(request, {id, item, snapshot}); return request;
}
void AddonDiscoveryService::completed(const QString& request, const QString& status, const QVariantMap& data, const QString& error)
{
    if (m_plays.contains(request)) {
        const auto action = m_plays.take(request); const auto it = m_views.constFind(action.view);
        const auto handle = data.value("playbackHandle").toString();
        if (it == m_views.cend() || !visibleMedia(*it, action.item) || m_coordinator.queueReference(action.item).isEmpty()
            || status != QLatin1String("match") || handle.isEmpty()) {
            m_transactions.releasePlayable(handle);
            if (it != m_views.cend()) emit actionFinished(action.view, false, error.isEmpty() ? tr("The discovery item is no longer available.") : error);
            return;
        }
        const auto transaction = m_transactions.play(action.view, m_coordinator.queueReference(action.item), handle, action.snapshot);
        m_actions.insert(transaction, action.view); m_playActions.insert(transaction, action.view); return;
    }
    const auto id = data.value("view").toString(); auto it = m_views.find(id);
    if (it == m_views.end() || it->pending != request || data.value("generation").toULongLong() != it->generation) return;
    it->pending.clear(); it->busy = false;
    if (status != QLatin1String("match") && status != QLatin1String("partial") && status != QLatin1String("no_match")) it->error = error.isEmpty() ? tr("The discovery source did not return a result.") : error.left(512);
    else {
        if (!it->append) it->items.clear();
        for (const auto& value : data.value("updates").toList()) {
            const auto row = value.toMap(); for (auto& existing : it->items) if (existing.toMap().value("ref") == row.value("ref")) existing = row;
        }
        const auto items = data.value("items").toList();
        if (it->items.size() + items.size() > 1000) it->error = tr("The view has 1000 items. Use search to reduce the result list.");
        else { it->items.append(items); it->cursor = data.value("cursor").toString(); }
    }
    emit viewChanged(id);
}
void AddonDiscoveryService::closeView(const QString& id)
{
    if (!m_views.remove(id)) return;
    m_coordinator.cancelView(id);
    m_transactions.cancelView(id);
    const auto plays = m_plays.keys(); for (const auto& request : plays) if (m_plays.value(request).view == id) m_plays.remove(request);
    const auto actions = m_actions.keys(id); for (const auto& request : actions) m_actions.remove(request);
    emit viewClosed(id);
}
void AddonDiscoveryService::invalidate()
{ const auto views = m_views.keys(); for (const auto& id : views) closeView(id); ++m_revision; emit changed(); }
void AddonDiscoveryService::operationFinished(const QString& invocation, const QString& contribution, const QString& status,
                                            const QVariantMap& data, const QString& error)
{ m_coordinator.operationFinished(invocation, contribution, status, data, error); }
}
