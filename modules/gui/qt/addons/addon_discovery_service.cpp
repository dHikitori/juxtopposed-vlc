// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_discovery_service.hpp"
#include "addon_manager.hpp"
#include "addon_catalogue_view.hpp"
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <utility>

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
        if (m_fill.active && request == m_fill.transaction) {
            m_fill.transaction.clear(); m_fill.busy = false;
            const auto adding = std::exchange(m_fill.adding, {});
            if (success) { m_fill.added += int(adding.size()); m_fill.failures = 0; m_fill.error.clear(); }
            // A queue that changed during the append gets the items next time.
            else for (const auto& item : adding) m_fill.seen.remove(item);
            if (!success && ++m_fill.failures >= 3) { endFill(error.isEmpty() ? tr("The app cannot add suggestions to the queue.") : error.left(512)); return; }
            emit queueFillChanged();
            return;
        }
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
    // A queue-fill session keeps the source list current after its page closes.
    if (m_closing || !m_displayAttached || !m_display || (m_displayRequests.isEmpty() && !m_fill.active)) return false;
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
    endFill({});
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
    View view; view.source = source; view.sourceIdentity = identity;
    view.declaresResolve = m_coordinator.declares(source, QStringLiteral("resolve"));
    // The first page is requested next. Show loading from the first frame,
    // not "Use search" while the worker starts.
    view.busy = m_coordinator.supports(source, QStringLiteral("roots"));
    m_views.insert(id, view);
    QTimer::singleShot(0, this, [this, id] { startView(id); });
    return id;
}
void AddonDiscoveryService::startView(const QString& id)
{
    auto it = m_views.find(id);
    if (it == m_views.end() || it->started || !viewCurrent(*it)) return;
    it->started = true;
    const auto source = it->source; const QPointer<AddonDiscoveryService> owner(this);
    if (!m_coordinator.supports(source, QStringLiteral("roots")) || !fetch(id)) {
        if (!owner) return;
        if (const auto view = m_views.find(id); view != m_views.end()) view->busy = false;
        emit viewChanged(id);
    }
    if (owner && m_views.contains(id) && viewCurrent(m_views.value(id)) && m_playlists.list) loadPlaylists(id);
}
QVariantMap AddonDiscoveryService::view(const QString& id) const
{
    const auto it = m_views.constFind(id); if (it == m_views.cend()) return {};
    return {{"id", id}, {"source", it->source}, {"items", it->items}, {"busy", it->busy}, {"error", it->error},
        {"available", viewCurrent(*it)}, {"loading", loading()},
        {"canGoBack", !it->path.isEmpty() || !it->query.isEmpty()}, {"hasMore", !it->cursor.isEmpty()},
        {"query", it->query}, {"canPlay", canPlay(id)}, {"canEnqueue", canEnqueue(id)}, {"canFillQueue", canFillQueue(id)},
        {"needsApproval", needsApproval(id)},
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
    // A new page replaces the feed of the previous folder.
    if (!append) {
        ++it->feed; it->revision.clear(); it->nextCheckSeconds = 0;
        const auto feedRequest = std::exchange(it->feedRequest, QString{});
        if (!feedRequest.isEmpty()) {
            m_coordinator.cancel(feedRequest);
            if (!owner) return false;
            it = m_views.find(id); if (it == m_views.end() || it->generation != generation) return false;
        }
    }
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
QVariantList AddonDiscoveryService::sources() const
{
    if (m_closing) return {};
    auto output = m_coordinator.sources();
    if (!m_sourcePlayback.ready) return output;
    // The app's source owner resolves for playback, not the coordinator, so
    // a declared resolve stays listed: the panel offers its playback review.
    for (auto& value : output) {
        auto entry = value.toMap(); auto operations = entry.value(QStringLiteral("operations")).toStringList();
        if (operations.contains(QStringLiteral("resolve"))
            || !m_coordinator.declares(entry.value(QStringLiteral("id")).toString(), QStringLiteral("resolve"))) continue;
        operations.append(QStringLiteral("resolve")); entry.insert(QStringLiteral("operations"), operations); value = entry;
    }
    return output;
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
bool AddonDiscoveryService::needsApproval(const QString& id) const
{
    const auto it=m_views.constFind(id); if(it==m_views.cend() || !viewCurrent(*it) || !m_sourcePlayback.ready) return false;
    return it->declaresResolve && !m_sourcePlayback.ready(it->source);
}
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
    if (m_fill.active && request == m_fill.request) { fillFinished(status, data, error); return; }
    if (const auto feedView = data.value("view").toString(); m_views.contains(feedView) && m_views.value(feedView).feedRequest == request) {
        feedFinished(feedView, status, data); return;
    }
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
        QVariantList items;
        if (it->append) {
            // A later page can repeat a row that an earlier page showed.
            for (const auto& value : data.value("updates").toList()) {
                const auto row = value.toMap(); for (auto& existing : it->items) if (existing.toMap().value("ref") == row.value("ref")) existing = row;
            }
            items = data.value("items").toList();
        } else {
            // A new page replaces the view, including rows that the view showed
            // before, for example after Back or a repeated search.
            it->items.clear();
            items = data.value("page").toList();
        }
        if (it->items.size() + items.size() > 1000) it->error = tr("The view has 1000 items. Use search to reduce the result list.");
        else { it->items.append(items); it->cursor = data.value("cursor").toString(); }
        if (!it->append) {
            it->revision = data.value("revision").toString();
            it->nextCheckSeconds = data.value("nextCheckSeconds").toInt();
            const QPointer<AddonDiscoveryService> owner(this);
            emit viewChanged(id);
            if (owner) scheduleFeed(id);
            return;
        }
    }
    emit viewChanged(id);
}
void AddonDiscoveryService::scheduleFeed(const QString& id)
{
    auto it = m_views.find(id);
    // Only the open root or folder, not a search; only while the view is open.
    if (it == m_views.end() || !it->query.isEmpty() || !viewCurrent(*it) || !m_coordinator.supports(it->source, QStringLiteral("subscribe"))) return;
    const auto feed = ++it->feed;
    const int delay = it->nextCheckSeconds > 0 ? std::max(m_feedMinimumMs, it->nextCheckSeconds * 1000) : m_feedDefaultMs;
    QTimer::singleShot(delay, this, [this, id, feed] {
        auto it = m_views.find(id);
        if (it == m_views.end() || it->feed != feed || !viewCurrent(*it)) return;
        if (it->busy || !it->feedRequest.isEmpty()) { scheduleFeed(id); return; }
        const auto request = m_coordinator.subscribe(it->source, it->path.isEmpty() ? QString{} : it->path.last(), it->revision, id, it->generation);
        if (const auto view = m_views.find(id); view != m_views.end() && view->feed == feed) view->feedRequest = request;
    });
}
void AddonDiscoveryService::feedFinished(const QString& id, const QString& status, const QVariantMap& data)
{
    auto it = m_views.find(id);
    if (it == m_views.end()) return;
    it->feedRequest.clear();
    // A refused or failed check ends the feed; the shown items stay.
    if (status != QLatin1String("match") && status != QLatin1String("no_match")) return;
    if (data.value("generation").toULongLong() != it->generation || !viewCurrent(*it)) return;
    QSet<QString> removed;
    for (const auto& ref : data.value("removed").toList()) removed.insert(ref.toString());
    QHash<QString, QVariantMap> updates;
    for (const auto& value : data.value("updates").toList()) updates.insert(value.toMap().value("ref").toString(), value.toMap());
    QVariantList items;
    // New rows first, in source order: a feed reports what was added.
    for (const auto& value : data.value("items").toList()) if (!removed.contains(value.toMap().value("ref").toString())) items.append(value);
    for (const auto& value : std::as_const(it->items)) {
        const auto ref = value.toMap().value("ref").toString();
        if (removed.contains(ref)) continue;
        items.append(updates.contains(ref) ? QVariant(updates.value(ref)) : value);
    }
    if (items.size() > 1000) items.resize(1000);
    it->items = items;
    if (data.contains("revision")) it->revision = data.value("revision").toString();
    if (data.contains("nextCheckSeconds")) it->nextCheckSeconds = data.value("nextCheckSeconds").toInt();
    const QPointer<AddonDiscoveryService> owner(this);
    emit viewChanged(id);
    if (owner) scheduleFeed(id);
}
bool AddonDiscoveryService::canFillQueue(const QString& id) const
{
    const auto it = m_views.constFind(id);
    return it != m_views.cend() && canEnqueue(id) && m_transactions.fillAvailable()
        && m_coordinator.supports(it->source, QStringLiteral("suggest"));
}
bool AddonDiscoveryService::startQueueFill(const QString& id, int maxPending)
{
    if (!canFillQueue(id) || maxPending < 1 || maxPending > 10) return false;
    const QPointer<AddonDiscoveryService> owner(this);
    endFill({});
    if (!owner || !canFillQueue(id)) return false;
    const auto& view = m_views[id];
    QString name;
    for (const auto& value : sources()) if (value.toMap().value("id").toString() == view.source) name = value.toMap().value("name").toString();
    const auto generation = m_fill.generation + 1;
    m_fill = {};
    m_fill.generation = generation;
    m_fill.active = true; m_fill.source = view.source; m_fill.sourceName = name.left(256);
    m_fill.sourceIdentity = view.sourceIdentity; m_fill.maxPending = maxPending;
    m_fill.view = QStringLiteral("queue-fill-") + QString::number(generation);
    m_fill.tag = QUuid::createUuid().toString(QUuid::WithoutBraces);
    syncDisplayDemand(); if (!owner) return false;
    emit queueFillChanged();
    if (owner) evaluateFill();
    return bool(owner);
}
void AddonDiscoveryService::pauseQueueFill(bool paused)
{
    if (!m_fill.active || m_fill.paused == paused) return;
    m_fill.paused = paused;
    const QPointer<AddonDiscoveryService> owner(this);
    emit queueFillChanged();
    if (owner && !paused) evaluateFill();
}
void AddonDiscoveryService::stopQueueFill() { endFill({}); }
void AddonDiscoveryService::endFill(const QString& error)
{
    if (!m_fill.active) return;
    const auto request = m_fill.request, transaction = m_fill.transaction, view = m_fill.view;
    m_fill.active = false; m_fill.busy = false; m_fill.paused = false; m_fill.request.clear(); m_fill.transaction.clear();
    m_fill.error = error; ++m_fill.generation;
    const QPointer<AddonDiscoveryService> owner(this);
    if (!request.isEmpty()) { m_coordinator.cancel(request); if (!owner) return; }
    if (!transaction.isEmpty()) { m_transactions.cancel(transaction); if (!owner) return; }
    m_coordinator.cancelView(view); if (!owner) return;
    syncDisplayDemand(); if (!owner) return;
    emit queueFillChanged();
}
QVariantMap AddonDiscoveryService::queueFill() const
{
    const int pending = m_fill.active ? m_transactions.pendingAhead(m_fill.tag) : 0;
    return {{"active", m_fill.active}, {"paused", m_fill.paused}, {"busy", m_fill.busy}, {"source", m_fill.source},
        {"sourceName", m_fill.sourceName}, {"maxPending", m_fill.maxPending}, {"pending", std::max(0, pending)},
        {"added", m_fill.added}, {"error", m_fill.error}};
}
void AddonDiscoveryService::noteQueueChanged()
{
    if (!m_fill.active || m_fill.wake) return;
    // Coalesce a burst of queue changes into one check.
    m_fill.wake = true;
    const auto generation = m_fill.generation;
    QTimer::singleShot(0, this, [this, generation] {
        if (m_fill.generation != generation) return;
        m_fill.wake = false;
        const QPointer<AddonDiscoveryService> owner(this);
        emit queueFillChanged();
        if (owner) evaluateFill();
    });
}
void AddonDiscoveryService::evaluateFill()
{
    if (!m_fill.active || m_fill.paused || m_fill.busy) return;
    if (!available() || m_coordinator.sourceIdentity(m_fill.source) != m_fill.sourceIdentity
        || !m_coordinator.supports(m_fill.source, QStringLiteral("suggest")) || !m_transactions.fillAvailable()
        || (m_sourcePlayback.ready && !m_sourcePlayback.ready(m_fill.source))) {
        endFill(tr("The source or its account changed. The queue is no longer filled."));
        return;
    }
    const int pending = m_transactions.pendingAhead(m_fill.tag);
    if (pending < 0) { endFill(tr("The queue is not available.")); return; }
    // Refill only when at most one suggestion is left ahead.
    if (pending > std::min(1, m_fill.maxPending - 1)) return;
    // At most one suggestion request every 5 s, also when the queue moves fast.
    if (m_fill.last.isValid() && m_fill.last.elapsed() < m_fillSpacingMs) {
        const auto generation = m_fill.generation;
        QTimer::singleShot(int(m_fillSpacingMs - m_fill.last.elapsed()), this, [this, generation] { if (m_fill.generation == generation) evaluateFill(); });
        return;
    }
    m_fill.last.start(); m_fill.busy = true;
    const auto request = m_coordinator.suggest(m_fill.source, {}, m_fill.maxPending - pending, m_fill.view, 1);
    if (m_fill.active && m_fill.busy) m_fill.request = request;
    emit queueFillChanged();
}
void AddonDiscoveryService::fillFinished(const QString& status, const QVariantMap& data, const QString& error)
{
    m_fill.request.clear(); m_fill.busy = false;
    if (status != QLatin1String("match") && status != QLatin1String("no_match")) {
        endFill(error.isEmpty() ? tr("The source did not return suggestions.") : error.left(512)); return;
    }
    QVariantList references;
    for (const auto& value : data.value("page").toList()) {
        const auto row = value.toMap();
        if (row.value("kind").toString() != QLatin1String("media") || !row.value("available", true).toBool()) continue;
        const auto reference = m_coordinator.queueReference(row.value("ref").toString());
        const auto item = reference.value("itemId").toString();
        // A suggestion that this session already added is not added again.
        if (reference.isEmpty() || m_fill.seen.contains(item)) continue;
        if (m_sourcePlayback.referenceReady && !m_sourcePlayback.referenceReady(reference)) continue;
        references.append(reference);
    }
    if (references.isEmpty()) {
        if (++m_fill.empty >= 2) endFill(tr("The source has no more suggestions."));
        else emit queueFillChanged();
        return;
    }
    m_fill.empty = 0;
    const QPointer<AddonDiscoveryService> owner(this);
    const auto transaction = m_transactions.append(m_fill.view, references, QStringLiteral("queue"), m_fill.tag, m_fill.sourceName);
    if (!owner || !m_fill.active) return;
    if (transaction.isEmpty()) { endFill(tr("The app cannot add suggestions to the queue.")); return; }
    for (const auto& reference : references) {
        if (m_fill.seen.size() >= 1000) m_fill.seen.clear();
        m_fill.seen.insert(reference.toMap().value("itemId").toString());
        m_fill.adding.append(reference.toMap().value("itemId").toString());
    }
    m_fill.busy = true; m_fill.transaction = transaction;
    emit queueFillChanged();
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
    endFill(tr("The add-ons changed. The queue is no longer filled."));
    const auto views = m_views.keys(); const QPointer<AddonDiscoveryService> owner(this);
    for (const auto& id : views) { closeView(id); if (!owner) return; }
    ++m_revision; emit changed();
}
void AddonDiscoveryService::operationFinished(const QString& invocation, const QString& contribution, const QString& status,
                                            const QVariantMap& data, const QString& error)
{ m_coordinator.operationFinished(invocation, contribution, status, data, error); }
}
