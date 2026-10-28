// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_discovery_service.hpp"
#include "addon_manager.hpp"
#include "addon_catalogue_view.hpp"
#include <QTimer>
#include <QUuid>

namespace vlc::addons {
namespace {
QStringList playlistChoices(const QVariantList& values)
{ QStringList result; for (const auto& value:values) result.append(value.toMap().value("ref").toString()); return result; }
}
AddonDiscoveryCoordinator::Backend AddonDiscoveryService::managerBackend(AddonManager* manager, AddonDiscoveryCoordinator::Backend backend)
{
    const QPointer<AddonManager> owner(manager);
    const QPointer<AddonCatalogueView> display(manager ? manager->displayCatalogue() : nullptr);
    const bool attached = bool(display);
    const auto current = backend.current;
    backend.current = [owner, display, attached, current] {
        return owner && (!attached || (display && display->retainedCatalogueCurrent())) && (!current || current());
    };
    backend.sources = [owner, display, attached] {
        QVariantList result; if (!owner) return result;
        if (attached) {
            if (!display) return result;
            for (const auto& value : display->contributions(QStringLiteral("discovery.source"), true)) {
                auto row = value.toMap();
                const bool accountKnown = row.value("accountReference").metaType().id() == QMetaType::QString && !row.value("accountReference").toString().isEmpty();
                row.insert("configured", row.value("configurationKnown").toBool() && row.value("configured").toBool() && accountKnown);
                row.insert("cachePartition", row.value("displayPartition")); result.append(row);
            }
            return result;
        }
        // Explicit standalone tools may use the strict synchronous adapter.
        // An attached service never enters this path after View destruction.
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
    m_display = manager ? manager->displayCatalogue() : nullptr;
    m_displayAttached = bool(m_display);
    connect(manager, &AddonManager::operationFinished, this, &AddonDiscoveryService::operationFinished);
    connect(manager, &AddonManager::retiring, this, &AddonDiscoveryService::close, Qt::DirectConnection);
    connect(manager, &QObject::destroyed, this, &AddonDiscoveryService::close);
    if (m_displayAttached) {
        connect(m_display, &AddonCatalogueView::catalogueChanged, this, &AddonDiscoveryService::displayChanged);
        connect(m_display, &AddonCatalogueView::freshnessChanged, this, &AddonDiscoveryService::displayChanged);
        connect(m_display, &QObject::destroyed, this, &AddonDiscoveryService::close);
    } else connect(manager, &AddonManager::revisionChanged, this, &AddonDiscoveryService::invalidate);
}
AddonDiscoveryService::AddonDiscoveryService(AddonDiscoveryCoordinator::Backend backend,
                                           AddonQueueTransactions::Backend queues, QObject* parent)
    : QObject(parent), m_coordinator(std::move(backend)), m_transactions(std::move(queues))
{
    connect(&m_coordinator, &AddonDiscoveryCoordinator::finished, this, &AddonDiscoveryService::completed);
    connect(&m_transactions, &AddonQueueTransactions::finished, this, [this](const QString& request, bool success, const QString& error) {
        m_playActions.remove(request);
        const auto view = m_actions.take(request); const auto target=m_libraryActions.take(request);
        const auto release=m_playlists.release; const QPointer<AddonDiscoveryService> owner(this);
        if (!target.isEmpty() && release) release(target);
        if (owner && !view.isEmpty() && m_views.contains(view)) emit actionFinished(view, success, error);
    });
}
AddonDiscoveryService::~AddonDiscoveryService()
{
    m_closing = true; m_displayRequests.clear(); ++m_demandGeneration;
    const auto ticket = m_featureTicket; m_featureTicket.clear();
    if (m_display && !ticket.isEmpty()) m_display->release(ticket);
    const auto views=m_views; m_views.clear(); const auto actions=m_libraryActions; m_libraryActions.clear(); const auto release=m_playlists.release; const auto releaseChoices=m_playlists.releaseChoices;
    for (const auto& view:views) {
        if (view.playlistCancel) view.playlistCancel();
        if (release && !view.playlistTarget.isEmpty()) release(view.playlistTarget);
        if (releaseChoices) releaseChoices(playlistChoices(view.playlists));
    }
    if (release) for (const auto& target:actions) release(target);
}
bool AddonDiscoveryService::available() const
{ return !m_closing && !m_demandRefused && m_coordinator.isCurrent() && (!m_displayAttached || (m_display && m_display->isCurrent())); }
bool AddonDiscoveryService::loading() const
{ return !m_closing && m_displayAttached && m_display && m_display->loading(); }
bool AddonDiscoveryService::viewCurrent(const View& view) const
{
    if (!available()) return false;
    const auto now = m_coordinator.sourceIdentity(view.source);
    return now == view.sourceIdentity && now.value("enabled").toBool()
        && now.value("configurationKnown").toBool() && now.value("configured").toBool();
}
QString AddonDiscoveryService::acquireDisplay()
{
    if (m_closing || m_displayRequests.size() >= 16) return {};
    const auto request = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_displayRequests.insert(request); ++m_demandGeneration; m_demandRefused = false; m_preparationError.clear();
    const QPointer<AddonDiscoveryService> owner(this); syncDisplayDemand();
    if (owner) emit availabilityChanged();
    return owner && m_displayRequests.contains(request) ? request : QString{};
}
void AddonDiscoveryService::releaseDisplay(const QString& request)
{
    if (!m_displayRequests.remove(request)) return;
    ++m_demandGeneration; const QPointer<AddonDiscoveryService> owner(this); syncDisplayDemand();
    if (owner) emit availabilityChanged();
}
bool AddonDiscoveryService::needsDisplayDemand() const
{
    if (m_closing || !m_displayAttached || !m_display || m_displayRequests.isEmpty()) return false;
    for (const auto& value : sources()) if (value.toMap().value("enabled").toBool()) return true;
    return false;
}
void AddonDiscoveryService::syncDisplayDemand()
{
    if (m_acquiringDemand) return;
    const QPointer<AddonCatalogueView> display(m_display);
    if (!needsDisplayDemand()) {
        const auto ticket = m_featureTicket; m_featureTicket.clear();
        m_demandRefused = false; m_preparationError.clear();
        if (display && !ticket.isEmpty()) display->release(ticket);
        return;
    }
    if (!m_featureTicket.isEmpty() || m_demandRefused) return;
    const auto generation = m_demandGeneration;
    const QPointer<AddonDiscoveryService> owner(this);
    m_acquiringDemand = true;
    const auto ticket = display->acquireFeature(QStringLiteral("discovery.source"));
    if (!owner) { if (display && !ticket.isEmpty()) display->release(ticket); return; }
    m_acquiringDemand = false;
    if (display == m_display && generation == m_demandGeneration && needsDisplayDemand()) {
        m_featureTicket = ticket;
        if (ticket.isEmpty()) {
            m_demandRefused = true;
            m_preparationError = tr("Close another add-on view, then close and open this page to check source settings.");
        }
    }
    else {
        if (display && !ticket.isEmpty()) display->release(ticket);
        if (owner) QTimer::singleShot(0, this, &AddonDiscoveryService::syncDisplayDemand);
    }
}
void AddonDiscoveryService::displayChanged()
{
    if (m_closing) return;
    const QPointer<AddonDiscoveryService> owner(this); syncDisplayDemand(); if (!owner) return;
    if (m_display && m_display->isCurrent()) {
        const auto views = m_views.keys();
        for (const auto& id : views) {
            const auto it = m_views.constFind(id); if (it == m_views.cend()) continue;
            const auto now = m_coordinator.sourceIdentity(it->source);
            // A demand-only publication may still be checking a stored secret.
            // Unknown presence is not proof that the selected source changed.
            if (!now.isEmpty() && !now.value("configurationKnown").toBool()) continue;
            if (now != it->sourceIdentity) { closeView(id); if (!owner) return; }
        }
    }
    ++m_revision; emit changed(); if (!owner) return;
    emit availabilityChanged(); if (!owner) return;
    const auto views = m_views.keys();
    for (const auto& id : views) {
        startView(id); if (!owner) return;
        emit viewChanged(id); if (!owner) return;
    }
}
void AddonDiscoveryService::close()
{
    if (m_closing) return;
    m_closing = true; m_displayRequests.clear(); ++m_demandGeneration;
    const QPointer<AddonDiscoveryService> owner(this); syncDisplayDemand(); if (!owner) return;
    const auto views = m_views.keys();
    for (const auto& id : views) { closeView(id); if (!owner) return; }
    ++m_revision; emit changed(); if (!owner) return;
    emit availabilityChanged();
}
QString AddonDiscoveryService::openSource(const QString& source)
{
    if (!available() || m_views.size() >= 16) return {};
    const auto identity = m_coordinator.sourceIdentity(source);
    if (identity.isEmpty() || !identity.value("enabled").toBool() || !identity.value("configured").toBool()
        || !identity.value("configurationKnown").toBool()) return {};
    const QPointer<AddonDiscoveryService> owner(this);
    const auto refresh = m_sourcePlayback.refresh;
    if (refresh) refresh(source);
    if (!owner || !available() || identity != m_coordinator.sourceIdentity(source)) return {};
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    View view; view.source = source; view.sourceIdentity = identity; m_views.insert(id, view);
    QTimer::singleShot(0, this, [this, id] { startView(id); });
    return id;
}
void AddonDiscoveryService::startView(const QString& id)
{
    auto it = m_views.find(id);
    if (it == m_views.end() || it->started || !viewCurrent(*it)) return;
    it->started = true;
    const auto source = it->source; const QPointer<AddonDiscoveryService> owner(this);
    if (m_coordinator.supports(source, QStringLiteral("roots"))) fetch(id);
    else emit viewChanged(id);
    if (owner && m_views.contains(id) && viewCurrent(m_views.value(id)) && m_playlists.list) loadPlaylists(id);
}
QVariantMap AddonDiscoveryService::view(const QString& id) const
{
    const auto it = m_views.constFind(id); if (it == m_views.cend()) return {};
    return {{"id", id}, {"source", it->source}, {"items", it->items}, {"busy", it->busy}, {"error", it->error},
        {"available", viewCurrent(*it)}, {"loading", loading()},
        {"canGoBack", !it->path.isEmpty() || !it->query.isEmpty()}, {"hasMore", !it->cursor.isEmpty()},
        {"query", it->query}, {"canPlay", canPlay(id)}, {"canEnqueue", canEnqueue(id)},
        {"playlistAvailable",it->playlistAvailable},{"playlistBusy",it->playlistBusy},{"playlists",it->playlists},
        {"playlistTarget",it->playlistTarget},{"playlistError",it->playlistError},{"morePlaylists",it->morePlaylists},
        {"canAddToPlaylist",viewCurrent(*it) && !it->playlistTarget.isEmpty() && m_transactions.appendAvailable() && m_transactions.snapshot(it->playlistTarget).available}};
}
bool AddonDiscoveryService::fetch(const QString& id, bool append)
{
    auto it = m_views.find(id); if (it == m_views.end() || !viewCurrent(*it)) return false;
    const auto pending = it->pending; const auto generation = it->generation;
    it->pending.clear(); const QPointer<AddonDiscoveryService> owner(this);
    if (!pending.isEmpty()) m_coordinator.cancel(pending);
    if (!owner) return false;
    it = m_views.find(id); if (it == m_views.end() || it->generation != generation || !viewCurrent(*it)) return false;
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
    auto it = m_views.find(id); if (it == m_views.end() || !viewCurrent(*it)) return false;
    bool found = false; for (const auto& value : it->items) { const auto row = value.toMap(); if (row.value("ref").toString() == node && row.value("kind").toString() == QLatin1String("node")) found = true; }
    if (!found || !m_coordinator.supports(it->source, QStringLiteral("browse"))) return false;
    it->query.clear(); it->path.append(node); return fetch(id);
}
bool AddonDiscoveryService::search(const QString& id, const QString& query)
{
    auto it = m_views.find(id); if (it == m_views.end() || !viewCurrent(*it) || query.size() > 512 || !m_coordinator.supports(it->source, QStringLiteral("search"))) return false;
    it->path.clear(); it->query = query.trimmed(); return fetch(id);
}
bool AddonDiscoveryService::back(const QString& id)
{
    auto it = m_views.find(id); if (it == m_views.end() || !viewCurrent(*it) || (it->path.isEmpty() && it->query.isEmpty())) return false;
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
{
    const auto it=m_views.constFind(id); if(it==m_views.cend() || !viewCurrent(*it)) return false;
    return m_sourcePlayback.ready ? m_transactions.referencePlayAvailable() && m_sourcePlayback.ready(it->source)
        : m_transactions.playAvailable() && m_coordinator.supports(it->source,QStringLiteral("resolve"));
}
bool AddonDiscoveryService::canEnqueue(const QString& id) const
{ const auto it=m_views.constFind(id); return it!=m_views.cend() && viewCurrent(*it) && m_transactions.appendAvailable() && m_transactions.snapshot().available
    && (!m_sourcePlayback.ready || m_sourcePlayback.ready(it->source)); }
void AddonDiscoveryService::setSourcePlayback(SourcePlayback backend)
{ m_sourcePlayback=std::move(backend); refreshSourcePlayback(); }
void AddonDiscoveryService::refreshSourcePlayback()
{
    const auto views=m_views.keys(); const QPointer<AddonDiscoveryService> owner(this);
    for(const auto& id:views) { emit viewChanged(id); if(!owner) return; }
}
void AddonDiscoveryService::setPlaylistBackend(PlaylistBackend backend)
{ if (m_closing) return; const QPointer<AddonDiscoveryService> owner(this); invalidate(); if (owner) m_playlists=std::move(backend); }
void AddonDiscoveryService::clearPlaylist(const QString& id)
{
    auto it=m_views.find(id); if (m_closing || it==m_views.end()) return;
    const auto cancel=std::move(it->playlistCancel); const auto target=it->playlistTarget; const auto release=m_playlists.release;
    it->playlistCancel={}; it->playlistTarget.clear(); it->playlistBusy=false; ++it->playlistGeneration;
    QStringList requests, targets;
    for (auto action=m_libraryActions.begin();action!=m_libraryActions.end();) {
        if (m_actions.value(action.key())==id) { requests.append(action.key()); targets.append(action.value()); action=m_libraryActions.erase(action); }
        else ++action;
    }
    const QPointer<AddonDiscoveryService> owner(this);
    if (cancel) cancel();
    if (!owner) return;
    if (release && !target.isEmpty()) release(target);
    if (!owner) return;
    for (const auto& request:requests) { m_transactions.cancel(request); if (!owner) return; }
    if (release) for (const auto& value:targets) { release(value); if (!owner) return; }
    if (m_views.contains(id)) emit viewChanged(id);
}
bool AddonDiscoveryService::loadPlaylists(const QString& id,const QString& query)
{
    if (!m_views.contains(id) || !viewCurrent(m_views.value(id)) || !m_playlists.list || query.size()>256) return false;
    const QPointer<AddonDiscoveryService> owner(this); clearPlaylist(id); if (!owner || !m_views.contains(id) || !viewCurrent(m_views.value(id))) return false;
    auto& view=m_views[id]; view.playlists.clear(); view.playlistError.clear(); view.playlistBusy=true;
    const auto generation=view.playlistGeneration; const auto list=m_playlists.list;
    auto cancel=list(query,[owner,id,generation](bool available,const QVariantList& choices,bool more,const QString& error) {
        if (!owner) return;
        auto it=owner->m_views.find(id);
        if (it==owner->m_views.end() || it->playlistGeneration!=generation) return;
        it->playlistCancel={}; it->playlistBusy=false; it->playlistAvailable=available;
        it->playlists=choices; it->morePlaylists=more; it->playlistError=error.left(512); emit owner->viewChanged(id);
    });
    if (owner && m_views.contains(id) && m_views[id].playlistGeneration==generation && m_views[id].playlistBusy) m_views[id].playlistCancel=std::move(cancel);
    else if (cancel) cancel();
    if (owner && m_views.contains(id)) emit viewChanged(id);
    return bool(owner);
}
bool AddonDiscoveryService::choosePlaylist(const QString& id,const QString& choice)
{
    const auto it=m_views.constFind(id); if (it==m_views.cend() || !viewCurrent(*it) || !m_playlists.prepare || !it->playlistAvailable) return false;
    bool found=false; for (const auto& value:it->playlists) if (value.toMap().value("ref").toString()==choice) found=true;
    if (!found) return false;
    const QPointer<AddonDiscoveryService> owner(this); clearPlaylist(id); if (!owner || !m_views.contains(id) || !viewCurrent(m_views.value(id))) return false;
    auto& view=m_views[id]; view.playlistBusy=true; view.playlistError.clear(); const auto generation=view.playlistGeneration;
    const auto release=m_playlists.release; const auto prepare=m_playlists.prepare;
    auto cancel=prepare(choice,[owner,id,generation,release](const QString& target,const QString& error) {
        if (!owner || !owner->m_views.contains(id) || owner->m_views[id].playlistGeneration!=generation) {
            if (release && !target.isEmpty()) release(target);
            return;
        }
        auto& view=owner->m_views[id]; view.playlistCancel={}; view.playlistBusy=false;
        view.playlistTarget=target; view.playlistError=error.left(512); emit owner->viewChanged(id);
    });
    if (owner && m_views.contains(id) && m_views[id].playlistGeneration==generation && m_views[id].playlistBusy) m_views[id].playlistCancel=std::move(cancel);
    else if (cancel) cancel();
    if (owner && m_views.contains(id)) emit viewChanged(id);
    return bool(owner);
}
QString AddonDiscoveryService::enqueue(const QString& id, const QStringList& items, const QString& target)
{
    const auto it = m_views.constFind(id); if (it == m_views.cend() || !viewCurrent(*it) || items.isEmpty() || items.size() > 200) return {};
    if (target==QLatin1String("queue") ? !canEnqueue(id)
        : target.isEmpty() || it->playlistTarget!=target || !m_transactions.snapshot(target).available) return {};
    QVariantList references; QSet<QString> seen;
    for (const auto& item : items) {
        if (seen.contains(item) || !visibleMedia(*it, item)) return {};
        seen.insert(item); const auto reference = m_coordinator.queueReference(item); if (reference.isEmpty()) return {};
        if(target==QLatin1String("queue") && m_sourcePlayback.referenceReady && !m_sourcePlayback.referenceReady(reference)) return {};
        references.append(reference);
    }
    const QPointer<AddonDiscoveryService> owner(this);
    const auto request = m_transactions.append(id, references, target); if (!owner) return request; m_actions.insert(request, id);
    if (target!=QLatin1String("queue")) { m_libraryActions.insert(request,target); m_views[id].playlistTarget.clear(); emit viewChanged(id); }
    return request;
}
QString AddonDiscoveryService::play(const QString& id, const QString& item)
{
    const auto it = m_views.constFind(id); if (it == m_views.cend() || !canPlay(id) || !visibleMedia(*it, item)) return {};
    const auto snapshot = m_transactions.snapshot(); if (!snapshot.available) return {};
    const auto generation=it->generation; const QPointer<AddonDiscoveryService> alive(this);
    const auto pending = m_plays.keys(); for (const auto& request : pending) if (m_plays.value(request).view == id) { m_plays.remove(request); m_coordinator.cancel(request); if(!alive) return {}; }
    const auto actions = m_playActions.keys(id); for (const auto& request : actions) { m_actions.remove(request); m_playActions.remove(request); m_transactions.cancel(request); if(!alive) return {}; }
    const auto currentView=m_views.constFind(id);
    if(currentView==m_views.cend() || currentView->generation!=generation || !visibleMedia(*currentView,item)) return {};
    if(m_sourcePlayback.referenceReady) {
        const auto reference=m_coordinator.queueReference(item);
        if(reference.isEmpty() || !m_sourcePlayback.referenceReady(reference)) return {};
        const QPointer<AddonDiscoveryService> owner(this);
        const auto request=m_transactions.playReference(id,reference,snapshot);
        if(owner) { m_actions.insert(request,id); m_playActions.insert(request,id); }
        return request;
    }
    const auto request = m_coordinator.resolve(item, QStringLiteral("play"), id, generation);
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
    if (!viewCurrent(*it)) { emit viewChanged(id); return; }
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
    if (!m_views.contains(id)) return;
    const auto view=m_views.take(id); const auto release=m_playlists.release; const auto releaseChoices=m_playlists.releaseChoices; const QPointer<AddonDiscoveryService> owner(this);
    if (view.playlistCancel) view.playlistCancel();
    if (!owner) return;
    if (!view.playlistTarget.isEmpty() && release) release(view.playlistTarget);
    if (!owner) return;
    if (releaseChoices) releaseChoices(playlistChoices(view.playlists));
    if (!owner) return;
    m_coordinator.cancelView(id);
    if (!owner) return;
    m_transactions.cancelView(id);
    if (!owner) return;
    const auto plays = m_plays.keys(); for (const auto& request : plays) if (m_plays.value(request).view == id) m_plays.remove(request);
    const auto actions = m_actions.keys(id); for (const auto& request : actions) m_actions.remove(request);
    emit viewClosed(id);
}
void AddonDiscoveryService::invalidate()
{
    if (m_closing) return;
    const auto views = m_views.keys(); const QPointer<AddonDiscoveryService> owner(this);
    for (const auto& id : views) { closeView(id); if (!owner) return; }
    ++m_revision; emit changed();
}
void AddonDiscoveryService::operationFinished(const QString& invocation, const QString& contribution, const QString& status,
                                            const QVariantMap& data, const QString& error)
{ m_coordinator.operationFinished(invocation, contribution, status, data, error); }
}
