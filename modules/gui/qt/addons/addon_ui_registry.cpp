// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_ui_registry.hpp"
#include "addon_ui_assets.hpp"
#include "addon_catalogue_view.hpp"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace vlc::addons {
namespace {
const QMap<QString, QStringList>& hooks()
{
    static const QMap<QString, QStringList> value{
        {QStringLiteral("home.sections@1"), {QStringLiteral("ui.section")}},
        {QStringLiteral("home.content@1"), {QStringLiteral("ui.replacement")}},
        {QStringLiteral("library.navigation@1"), {QStringLiteral("ui.route")}},
        {QStringLiteral("media.details.sections@1"), {QStringLiteral("ui.section")}},
        {QStringLiteral("subtitles.results.sections@1"), {QStringLiteral("ui.section")}},
        {QStringLiteral("subtitles.search.content@1"), {QStringLiteral("ui.replacement")}},
        {QStringLiteral("queue.item.actions@1"), {QStringLiteral("ui.command")}},
        {QStringLiteral("player.actions@1"), {QStringLiteral("ui.command")}},
        {QStringLiteral("dialog.add-to-playlist.content@1"), {QStringLiteral("ui.replacement")}},
        {QStringLiteral("settings.addons.sections@1"), {QStringLiteral("ui.section")}},
        {QStringLiteral("actions.subtitles.open@1"), {QStringLiteral("ui.replacement")}},
        {QStringLiteral("player.panels@1"), {QStringLiteral("ui.panel")}}
    };
    return value;
}
const QStringList& uiTypes()
{
    static const QStringList value{QStringLiteral("ui.command"), QStringLiteral("ui.route"), QStringLiteral("ui.modal"),
        QStringLiteral("ui.section"), QStringLiteral("ui.replacement"), QStringLiteral("ui.panel")};
    return value;
}
QString type(const QVariantMap& entry) { return entry.value(QStringLiteral("document")).toMap().value(QStringLiteral("type")).toString(); }
QString routeName(const QVariantMap& entry)
{ return QStringLiteral("addon/") + entry.value("packageId").toString() + '/' + entry.value("id").toString().section('/', 1); }
bool boundedData(const QJsonValue& value, int depth, qsizetype& nodes)
{
    if (depth > 16 || ++nodes > 4096) return false;
    if (value.isString()) return value.toString().size() <= 16384;
    if (value.isArray()) {
        if (value.toArray().size() > 512) return false;
        for (const auto& child : value.toArray()) if (!boundedData(child, depth + 1, nodes)) return false;
    }
    if (value.isObject()) {
        const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (it.key() == QLatin1String("__proto__") || it.key() == QLatin1String("constructor") || it.key() == QLatin1String("prototype")) return false;
            if (!boundedData(it.value(), depth + 1, nodes)) return false;
        }
    }
    return true;
}
}

AddonUiRegistry::AddonUiRegistry(AddonManager* manager, QObject* parent)
    : AddonUiRegistry(manager, Backend{}, parent) {}
AddonUiRegistry::AddonUiRegistry(AddonManager* manager, Backend backend, QObject* parent)
    : QObject(parent), m_attached(AddonCatalogueView::attached(manager)!=nullptr),
      m_catalogue(AddonCatalogueView::attached(manager)),m_manager(manager),m_backend(std::move(backend))
{
    const QPointer<AddonManager> guardedManager(manager);
    if (!m_backend.invoke) m_backend.invoke = [guardedManager](const QString& id, const QString& operation, const QVariantMap& context) { return guardedManager ? guardedManager->invoke(id, operation, context) : QString(); };
    if (!m_backend.cancel) m_backend.cancel = [guardedManager](const QString& request) { if (guardedManager) guardedManager->cancel(request); };
    if(m_attached) {
        connect(m_catalogue,&AddonCatalogueView::catalogueChanged,this,&AddonUiRegistry::reloadAttached);
        connect(m_catalogue,&AddonCatalogueView::documentsChanged,this,&AddonUiRegistry::viewDocumentsChanged);
        connect(m_catalogue,&AddonCatalogueView::freshnessChanged,this,&AddonUiRegistry::viewFreshnessChanged);
        connect(m_catalogue,&QObject::destroyed,this,[this]{const QPointer<AddonUiRegistry> alive(this);retireSessions();if(alive)emit availabilityChanged();});
    } else if(manager) {
        QFile file(QDir(manager->storageDirectory()).filePath(QStringLiteral("ui-replacements.json")));
        if(file.open(QIODevice::ReadOnly)&&file.size()<=64*1024)m_selected=QJsonDocument::fromJson(file.readAll()).object();
        connect(manager,&AddonManager::revisionChanged,this,&AddonUiRegistry::reload);
    }
    if(manager) {
        connect(manager,&AddonManager::operationFinished,this,&AddonUiRegistry::operationFinished);
        connect(manager,&AddonManager::retiring,this,[this]{m_ownerRetired=true;const QPointer<AddonUiRegistry> alive(this);retireSessions();if(alive)emit availabilityChanged();});
        connect(manager,&QObject::destroyed,this,[this]{m_ownerRetired=true;const QPointer<AddonUiRegistry> alive(this);retireSessions();if(alive)emit availabilityChanged();});
    }
    m_panelRefresh=new QTimer(this);m_panelRefresh->setSingleShot(true);m_panelRefresh->setInterval(1000);
    connect(m_panelRefresh,&QTimer::timeout,this,[this]{for(const auto& panel:m_panelSessions.keys())dispatchPanelContext(panel);});
    connect(this,&AddonUiRegistry::changed,this,&AddonUiRegistry::panelsChanged);
    reload();
}
AddonUiRegistry::~AddonUiRegistry()
{
    m_destroying=true;retireSessions(false);
}
bool AddonUiRegistry::available() const noexcept
{ return !m_destroying && !m_ownerRetired && !m_reloading && m_manager && (!m_attached || (m_catalogue && m_catalogue->isCurrent())); }
bool AddonUiRegistry::loading() const noexcept
{ return m_attached && m_catalogue && m_catalogue->loading(); }
QJsonObject AddonUiRegistry::scope(const QString& package,const QString& capability) const
{
    if(m_attached)return m_catalogue?m_catalogue->grantedScope(package,capability):QJsonObject{};
    return m_manager?m_manager->grantedScope(package,capability):QJsonObject{};
}
QString AddonUiRegistry::documentField(const QVariantMap& entry)
{ return entry.value(QStringLiteral("document")).toMap().contains(QStringLiteral("view"))?QStringLiteral("view"):QStringLiteral("resource"); }
QString AddonUiRegistry::holdDocument(const QString& id)
{ return m_attached && m_catalogue?m_catalogue->acquireDocument(id,documentField(m_entries.value(id))):QString{}; }
void AddonUiRegistry::dropDocument(const QString& ticket)
{ if(!ticket.isEmpty() && m_catalogue)m_catalogue->release(ticket); }
void AddonUiRegistry::retireSessions(bool notify)
{
    const auto cancel=m_backend.cancel;
    const QPointer<AddonUiRegistry> alive(this);const auto sessions=m_sessions.keys();
    for(const auto& id:sessions){closeSession(id);if(!alive)return;if(notify)emit sessionFailed(id);if(!alive)return;}
    const auto pending=m_runningCommands;m_runningCommands.clear();
    for(auto it=pending.begin();it!=pending.end();++it){cancel(it.key());if(!alive)return;if(notify&&!it->replacement.isEmpty())emit replacementFinished(it->replacement,false);if(!alive)return;}
    const auto modals=m_modals.keys();m_modals.clear();if(notify)for(const auto& ticket:modals){emit modalFinished(ticket,false,{});if(!alive)return;}
    const auto replacements=m_replacementViews.keys();m_replacementViews.clear();if(notify)for(const auto& ticket:replacements){emit replacementFinished(ticket,false);if(!alive)return;}
}
void AddonUiRegistry::viewFreshnessChanged()
{
    if(m_destroying)return;
    // Lazy document demand does not invalidate the catalogue. A real Store
    // withdrawal revokes sessions; an unchanged-token refresh keeps static UI.
    const QPointer<AddonUiRegistry> alive(this);
    if(!m_catalogue || !m_catalogue->retainedCatalogueCurrent())retireSessions();
    if(!alive || m_destroying)return;
    if(m_catalogue && !m_catalogue->loading() && !m_catalogue->error().isEmpty()) {
        const auto sessions=m_sessions.keys();
        for(const auto& id:sessions)if(m_sessions.contains(id)&&!m_sessions[id].ready) {
            m_sessions[id].descriptor.insert(QStringLiteral("pending"),false);m_sessions[id].descriptor.insert(QStringLiteral("unavailable"),true);
            emit sessionChanged(id);if(!alive)return;emit sessionFailed(id);if(!alive)return;
        }
    }
    emit availabilityChanged();if(!alive)return;
    if(available())viewDocumentsChanged();
}
void AddonUiRegistry::reloadAttached()
{
    if(m_destroying || !m_catalogue || !m_catalogue->isCurrent())return;
    QMap<QString,QVariantMap> entries;
    for(const auto& type:uiTypes())
        for(const auto& value:m_catalogue->contributions(type)) {
            const auto entry=value.toMap(),document=entry.value(QStringLiteral("document")).toMap();const auto hook=document.value(QStringLiteral("hook")).toString();
            if(!hook.isEmpty()&&(!hooks().contains(hook)||!hooks().value(hook).contains(type)))continue;
            if(permitted(entry,hook))entries.insert(entry.value(QStringLiteral("id")).toString(),entry);
        }
    const auto selected=QJsonObject::fromVariantMap(m_catalogue->preferences(QStringLiteral("uiReplacements")));
    const bool changed=entries!=m_entries || selected!=m_selected;
    m_reloading=true;
    const auto sessions=m_sessions.keys();const QPointer<AddonUiRegistry> alive(this);
    for(const auto& key:sessions) {
        const auto old=m_sessions.value(key);const auto declaration=entries.value(old.contribution).value(QStringLiteral("document")).toMap();
        const auto hook=declaration.value(QStringLiteral("hook")).toString();
        if(!entries.contains(old.contribution)||entries.value(old.contribution)!=m_entries.value(old.contribution)
            ||(declaration.value(QStringLiteral("type")).toString()==QLatin1String("ui.replacement")&&selected.value(hook).toString()!=old.contribution)) {
            closeSession(key);if(!alive)return;emit sessionFailed(key);if(!alive)return;
        }
    }
    for(auto it=m_failed.begin();it!=m_failed.end();)if(entries.value(*it)!=m_entries.value(*it))it=m_failed.erase(it);else ++it;
    m_entries=std::move(entries);m_selected=selected;m_reloading=false;
    if(changed){++m_revision;emit this->changed();if(!alive)return;}
    emit availabilityChanged();if(!alive)return;viewDocumentsChanged();if(!alive)return;
    askNewPanels();
}
void AddonUiRegistry::viewDocumentsChanged()
{
    if(m_destroying || !m_catalogue)return;
    m_documentRevision=m_catalogue->documentRevision();m_documentValidity.clear();m_validatedDocumentRevision=m_documentRevision;
    const auto sessions=m_sessions.keys();const QPointer<AddonUiRegistry> alive(this);
    for(const auto& id:sessions){materializeSession(id);if(!alive)return;}
    emit documentsChanged();
}
void AddonUiRegistry::setAssetService(AddonUiAssets* assets)
{
    QPointer<AddonUiRegistry> guard(this);if(m_assets==assets)return;
    if(m_assets)disconnect(m_assets,nullptr,this,nullptr);
    const auto sessions=m_sessions.keys();
    for(const auto& id:sessions){if(m_assets)m_assets->closeSession(id,false);if(!guard)return;if(m_sessions.contains(id))m_sessions[id].assetsOpened=false;}
    m_assets=assets;
    if(assets) {
        connect(assets,&AddonUiAssets::imageBudgetChanged,this,&AddonUiRegistry::imageBudgetChanged);
        connect(assets,&AddonUiAssets::sessionAssetsChanged,this,[this](const QString& id) {
            if(!available())return;
            auto session=m_sessions.find(id);if(session==m_sessions.end()||!session->ready)return;
            session->descriptor.insert(QStringLiteral("assetUrls"),m_assets?m_assets->sessionAssets(id):QVariantMap{});emit sessionChanged(id);
        });
        connect(assets,&QObject::destroyed,this,[this] {
            const auto sessions=m_sessions.keys();const QPointer<AddonUiRegistry> alive(this);
            for(const auto& id:sessions){if(!m_sessions.contains(id))continue;m_sessions[id].assetsOpened=false;m_sessions[id].descriptor.insert(QStringLiteral("assetUrls"),QVariantMap{});emit sessionChanged(id);if(!alive)return;}
        });
    }
    if(!available())return;
    for(const auto& id:sessions) {
        if(m_attached){materializeSession(id);if(!guard)return;continue;}
        if(!m_sessions.contains(id)||m_sessions[id].suspended)continue;
        const bool opened=m_assets&&m_assets->openSession(id,m_sessions[id].contribution);if(!guard)return;
        if(m_sessions.contains(id)){m_sessions[id].assetsOpened=opened;m_sessions[id].descriptor.insert(QStringLiteral("assetUrls"),m_assets?m_assets->sessionAssets(id):QVariantMap{});}
    }
}
void AddonUiRegistry::reload()
{
    if(m_attached){reloadAttached();return;}
    const QPointer<AddonUiRegistry> alive(this);retireSessions();if(!alive)return;
    m_entries.clear(); m_failed.clear();
    if (m_manager) for (const auto& type : uiTypes()) {
        for (const auto& value : m_manager->contributionsOfType(type)) {
            const auto entry = value.toMap(); const auto document = entry.value("document").toMap();
            const auto hook = document.value("hook").toString();
            if (!hook.isEmpty() && (!hooks().contains(hook) || !hooks().value(hook).contains(type))) continue;
            if (!permitted(entry, hook)) continue;
            m_entries.insert(entry.value("id").toString(), entry);
        }
    }
    ++m_revision; const QPointer<AddonUiRegistry> guard(this); emit changed(); if (!guard) return;
    askNewPanels();
}
void AddonUiRegistry::askNewPanels()
{
    // A panel that appears while media plays gets its context call now.
    const QPointer<AddonUiRegistry> alive(this);
    for (const auto& entry : m_entries) {
        const auto id = entry.value("id").toString();
        if (type(entry) != QLatin1String("ui.panel") || m_panelAvailable.contains(id)) continue;
        if (std::any_of(m_runningCommands.cbegin(), m_runningCommands.cend(), [&](const Pending& pending) { return pending.panel == id; })) continue;
        dispatchPanelContext(id); if (!alive) return;
    }
}
bool AddonUiRegistry::permitted(const QVariantMap& entry, const QString& hook) const
{
    const auto scope = this->scope(entry.value("packageId").toString(),QStringLiteral("ui.contribute"));
    if (scope.isEmpty()) return false;
    return hook.isEmpty() || scope.value("hooks").toArray().contains(hook);
}
bool AddonUiRegistry::replaceable(const QString& hook)
{ return hooks().value(hook).contains(QStringLiteral("ui.replacement")); }
QVariantList AddonUiRegistry::routes() const
{
    QVariantList result;
    for (const auto& entry : m_entries) {
        const auto document = entry.value("document").toMap();
        if (document.value("type").toString() != QLatin1String("ui.route")) continue;
        auto row = entry; row.insert("route", routeName(entry)); result.append(row);
    }
    return result;
}
QVariantList AddonUiRegistry::commands() const
{
    QVariantList result;
    for (const auto& entry : m_entries) if (entry.value("document").toMap().value("type").toString() == QLatin1String("ui.command")) result.append(entry);
    return result;
}
QVariantList AddonUiRegistry::sections(const QString& hook) const
{
    QVariantList result;
    if (!hooks().value(hook).contains(QStringLiteral("ui.section"))) return result;
    for (const auto& entry : m_entries) {
        const auto document = entry.value("document").toMap();
        if (document.value("type").toString() == QLatin1String("ui.section") && document.value("hook").toString() == hook) {
            const auto descriptor = this->descriptor(entry.value("id").toString(),false); if (!descriptor.isEmpty()) result.append(descriptor);
        }
    }
    return result;
}
QVariantList AddonUiRegistry::replacementChoices(const QString& hook) const
{
    QVariantList result; if (!replaceable(hook)) return result;
    for (const auto& entry : m_entries)
        if (entry.value("document").toMap().value("hook").toString() == hook) result.append(entry);
    return result;
}
QVariantMap AddonUiRegistry::replacement(const QString& hook) const
{
    if (!m_manager || !replaceable(hook)) return {};
    const auto id = m_selected.value(hook).toString();
    if (!m_entries.contains(id) || m_entries.value(id).value("document").toMap().value("hook").toString() != hook) return {};
    if (hook.startsWith(QLatin1String("actions."))) return m_entries.value(id);
    auto descriptor = this->descriptor(id,false);
    if (!descriptor.isEmpty() && hook == QLatin1String("subtitles.search.content@1")) {
        const auto fields = scope(m_entries.value(id).value("packageId").toString(),QStringLiteral("metadata.context")).value("fields").toArray();
        descriptor.insert(QStringLiteral("selectionTitlesAllowed"), fields.contains(QStringLiteral("title")));
    }
    return descriptor;
}
bool AddonUiRegistry::selectReplacement(const QString& hook, const QString& id)
{
    if (!available() || !replaceable(hook)) return false;
    if (!id.isEmpty()) {
        const auto choices = replacementChoices(hook);
        if (std::none_of(choices.begin(), choices.end(), [&](const QVariant& value) { return value.toMap().value("id").toString() == id; })) return false;
    }
    auto selected = m_selected;
    if (id.isEmpty()) selected.remove(hook); else selected.insert(hook, id);
    QSaveFile file(QDir(m_manager->storageDirectory()).filePath(QStringLiteral("ui-replacements.json")));
    if (!QDir().mkpath(m_manager->storageDirectory()) || !file.open(QIODevice::WriteOnly)) return false;
    const auto bytes = QJsonDocument(selected).toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size() || !file.commit()) return false;
    if(m_attached){m_catalogue->refresh();return true;}
    m_selected = selected; ++m_revision; emit changed(); return true;
}
bool AddonUiRegistry::validViewDocument(const QJsonObject& document)
{
    qsizetype nodes = 0;
    if (!boundedData(document, 0, nodes) || document.value("contract").toString() != QLatin1String("ui@1") || !document.value("root").isObject()) return false;
    static const QSet<QString> types{QStringLiteral("Page"), QStringLiteral("Section"), QStringLiteral("Column"), QStringLiteral("Row"),
        QStringLiteral("Heading"), QStringLiteral("Text"), QStringLiteral("Notice"), QStringLiteral("EmptyState"), QStringLiteral("Button"),
        QStringLiteral("IconButton"), QStringLiteral("Image"), QStringLiteral("MediaCard"), QStringLiteral("List"), QStringLiteral("Progress"),
        QStringLiteral("Tabs"), QStringLiteral("Menu"), QStringLiteral("MediaRow")};
    std::function<bool(const QJsonObject&, int)> validate = [&](const QJsonObject& node, int depth) {
        if (depth > 16 || !types.contains(node.value("type").toString())) return false;
        if (node.contains("source") || node.contains("qml") || node.contains("script") || node.contains("url")) return false;
        if (node.contains("children") && !node.value("children").isArray()) return false;
        for (const auto& child : node.value("children").toArray()) if (!child.isObject() || !validate(child.toObject(), depth + 1)) return false;
        if (node.contains("itemTemplate") && (!node.value("itemTemplate").isObject() || !validate(node.value("itemTemplate").toObject(), depth + 1))) return false;
        return true;
    };
    return validate(document.value("root").toObject(), 0);
}
QVariantMap AddonUiRegistry::view(const QString& id) const
{ return descriptor(id,true); }
QVariantMap AddonUiRegistry::descriptor(const QString& id,bool request) const
{
    if(m_attached) {
        if(!m_entries.contains(id)||m_failed.contains(id))return {};
        const auto entry=m_entries.value(id),contribution=entry.value(QStringLiteral("document")).toMap();
        const auto field=documentField(entry);
        QString state=m_catalogue?m_catalogue->documentState(id,field):QStringLiteral("unavailable");
        if(request && state==QLatin1String("pending") && m_catalogue)m_catalogue->ensureDocument(id,field);
        const auto document=m_catalogue?m_catalogue->resource(id,field):QJsonObject{};
        bool valid=false;
        if(state==QLatin1String("ready")) {
            const auto revision=m_catalogue->documentRevision();if(revision!=m_validatedDocumentRevision){m_documentValidity.clear();m_validatedDocumentRevision=revision;}
            if(!m_documentValidity.contains(id))m_documentValidity.insert(id,validViewDocument(document));valid=m_documentValidity.value(id);
            if(!valid)state=QStringLiteral("unavailable");
        }
        QStringList registered;for(const auto& command:commands())if(command.toMap().value(QStringLiteral("packageId"))==entry.value(QStringLiteral("packageId")))registered.append(command.toMap().value(QStringLiteral("id")).toString());
        QVariantMap result{{"id",id},{"packageId",entry.value(QStringLiteral("packageId"))},{"state",contribution.value(QStringLiteral("state")).toMap()},
            {"registeredCommands",registered},{"assetUrls",QVariantMap{}},{"pending",state==QLatin1String("pending")},
            {"unavailable",state==QLatin1String("unavailable")},{"available",available()&&valid}};
        if(valid){result.insert(QStringLiteral("document"),document.toVariantMap());result.insert(QStringLiteral("messages"),m_catalogue->settingsMessages(entry.value(QStringLiteral("packageId")).toString()));}
        return result;
    }
    if (!m_manager || m_ownerRetired || !m_entries.contains(id) || m_failed.contains(id)) return {};
    const auto entry = m_entries.value(id), contribution = entry.value("document").toMap();
    const auto document = m_manager->contributionResource(id, contribution.contains("view") ? QStringLiteral("view") : QStringLiteral("resource"));
    if (!validViewDocument(document)) return {};
    QStringList registered;
    for (const auto& command : commands()) if (command.toMap().value("packageId") == entry.value("packageId")) registered.append(command.toMap().value("id").toString());
    return {{"id", id}, {"packageId", entry.value("packageId")}, {"document", document.toVariantMap()},
        {"state", contribution.value("state").toMap()}, {"messages", m_manager->settingsMessages(entry.value("packageId").toString())},
        {"registeredCommands", registered}, {"assetUrls", QVariantMap{}}};
}
bool AddonUiRegistry::parametersValid(const QVariantMap& parameters, const QJsonObject& schema)
{
    qsizetype nodes = 0; if (!boundedData(QJsonObject::fromVariantMap(parameters), 0, nodes)) return false;
    if (schema.isEmpty()) return parameters.isEmpty();
    return validateUiValue(schema, QJsonObject::fromVariantMap(parameters));
}
bool AddonUiRegistry::openRoute(const QString& route, const QVariantMap& parameters)
{
    if(!available())return false;
    for (const auto& entry : routes()) {
        const auto item = entry.toMap(); if (item.value("route").toString() != route) continue;
        if (view(item.value("id").toString()).isEmpty() || !parametersValid(parameters, QJsonObject::fromVariantMap(item.value("document").toMap().value("parameters").toMap()))) return false;
        emit routeRequested(route, parameters); return true;
    }
    return false;
}
bool AddonUiRegistry::openModal(const QString& id, const QVariantMap& parameters)
{
    if(!available())return false;
    if (m_modals.size() >= 8 || !m_entries.contains(id) || m_entries.value(id).value("document").toMap().value("type").toString() != QLatin1String("ui.modal")
        || view(id).isEmpty() || !parametersValid(parameters, QJsonObject::fromVariantMap(m_entries.value(id).value("document").toMap().value("parameters").toMap()))) return false;
    const auto ticket = QUuid::createUuid().toString(QUuid::WithoutBraces); m_modals.insert(ticket, id);
    const QPointer<AddonUiRegistry> alive(this);emit modalRequested(id, parameters);if(!alive)return false;
    emit modalSessionRequested(ticket, id, parameters); return true;
}
bool AddonUiRegistry::triggerCommand(const QString& id, const QVariantMap& arguments)
{
    return dispatch(id, arguments, {}, {});
}
bool AddonUiRegistry::dispatch(const QString& id, const QVariantMap& arguments, const QString& sessionId, const QString& replacement,
                               const QVariantMap& extra, const QString& panel, bool gesture)
{
    if(!available())return false;
    if (m_runningCommands.size() >= 64) return false;
    const auto entry = m_entries.value(id); const auto document = entry.value("document").toMap();
    if (entry.isEmpty() || document.value("type").toString() != QLatin1String("ui.command")
        || !parametersValid(arguments, QJsonObject::fromVariantMap(document.value("parameters").toMap()))) return false;
    auto route = document.value("route").toString();
    if (!route.isEmpty()) {
        if (!route.startsWith(QLatin1String("addon/"))) route = QStringLiteral("addon/") + entry.value("packageId").toString() + '/' + route;
        if (!route.startsWith(QStringLiteral("addon/") + entry.value("packageId").toString() + '/')) return false;
        if (!replacement.isEmpty()) for (const auto& candidate : routes()) if (candidate.toMap().value("route").toString() == route) m_replacementViews.insert(replacement, candidate.toMap().value("id").toString());
        const QPointer<AddonUiRegistry> alive(this);const bool success = openRoute(route, arguments);
        if(!alive)return false;
        if (!success) m_replacementViews.remove(replacement);
        if (success && !replacement.isEmpty()) QTimer::singleShot(15000, this, [this, replacement] {
            if (m_replacementViews.remove(replacement)) emit replacementFinished(replacement, false);
        });
        return success;
    }
    const auto operation = document.value("command").toString();
    if (operation.isEmpty()) return false;
    const auto modalId = operation.contains('/') ? operation : entry.value("packageId").toString() + '/' + operation;
    if (m_entries.value(modalId).value("document").toMap().value("type").toString() == QLatin1String("ui.modal")) {
        if (!modalId.startsWith(entry.value("packageId").toString() + '/')) return false;
        return openModal(modalId, arguments);
    }
    QVariantMap context{{"arguments", arguments}};
    for (auto it = extra.begin(); it != extra.end(); ++it) context.insert(it.key(), it.value());
    quint64 generation = 0;
    if (!sessionId.isEmpty()) {
        const auto session = m_sessions.constFind(sessionId);
        if (session == m_sessions.cend() || session->suspended || !session->ready || session->descriptor.value("packageId") != entry.value("packageId")) return false;
        context.insert("parameters", session->parameters);
        context.insert("ui", session->context);
        context.insert("state", session->descriptor.value("state"));
        context.insert("identity", session->context.value("identity"));
        generation = session->generation;
    }
    const auto invoke=m_backend.invoke;const auto cancel=m_backend.cancel;
    const QPointer<AddonUiRegistry> alive(this);
    const auto request = invoke(id, operation, context);
    if(!alive){if(!request.isEmpty())cancel(request);return false;}
    if (request.isEmpty()) return false;
    bool current=available() && m_entries.value(id)==entry;
    if(current && !sessionId.isEmpty()) {
        const auto session=m_sessions.constFind(sessionId);
        current=session!=m_sessions.cend() && session->ready && !session->suspended
            && session->generation==generation && session->descriptor.value("packageId")==entry.value("packageId");
    }
    if(!current){cancel(request);return false;}
    m_runningCommands.insert(request, {id, sessionId, replacement, panel, generation, m_panelRevision, gesture});
    QTimer::singleShot(15000, this, [this, request] {
        if (!m_runningCommands.contains(request)) return;
        const auto pending = m_runningCommands.take(request);
        const auto cancel=m_backend.cancel;const QPointer<AddonUiRegistry> alive(this);cancel(request);if(!alive)return;
        emit commandCompleted(pending.command, false, {});if(!alive)return;
        if (!pending.replacement.isEmpty()) emit replacementFinished(pending.replacement, false);if(!alive)return;
        if (!pending.session.isEmpty() && m_sessions.contains(pending.session) && available()) emit sessionFailed(pending.session);
    });
    return true;
}
bool AddonUiRegistry::triggerReplacement(const QString& hook, const QVariantMap& arguments)
{
    if(!available())return false;
    if (hook != QLatin1String("actions.subtitles.open@1")) return false;
    const auto entry = replacement(hook); if (entry.isEmpty()) return false;
    const auto command = entry.value("document").toMap().value("command").toString();
    return triggerCommand(command, arguments);
}
void AddonUiRegistry::reportViewFailure(const QString& id)
{
    if(m_attached && !available())return;
    const auto tickets = m_replacementViews.keys(id);const QPointer<AddonUiRegistry> alive(this);
    for (const auto& ticket : tickets) { m_replacementViews.remove(ticket); emit replacementFinished(ticket, false);if(!alive)return; }
    // A panel keeps its button: its frame offers Reload and Turn off.
    if (m_entries.contains(id) && type(m_entries.value(id)) == QLatin1String("ui.panel")) return;
    if (m_entries.contains(id)) { m_failed.insert(id); m_error = tr("An add-on view is not available. Use the built-in view."); ++m_revision; emit changed(); }
}

QVariantMap AddonUiRegistry::projectContext(const QVariantMap& entry, const QVariantMap& input) const
{
    QVariantMap output;
    for (const auto& key : {QStringLiteral("surface"), QStringLiteral("search"), QStringLiteral("layout")}) {
        if (input.value(key).metaType().id() == QMetaType::QString && input.value(key).toString().size() <= 256) output.insert(key, input.value(key));
    }
    const auto identity = input.value(QStringLiteral("identity")).toMap();
    const auto fields = scope(entry.value("packageId").toString(),QStringLiteral("metadata.context")).value("fields").toArray();
    QVariantMap permitted;
    for (const auto& field : fields) {
        const auto name = field.toString(); const auto value = QJsonValue::fromVariant(identity.value(name));
        if ((value.isString() && value.toString().size() <= 4096) || value.isBool() || value.isDouble()) permitted.insert(name, value.toVariant());
    }
    if (!permitted.isEmpty()) output.insert(QStringLiteral("identity"), permitted);
    return output;
}
QString AddonUiRegistry::createSession(const QString& id,const QVariantMap& parameters,const QVariantMap& context)
{
    if(!available() || m_sessions.size()>=32)return {};
    auto value=descriptor(id,false);
    if(value.isEmpty() || value.value(QStringLiteral("unavailable")).toBool()
        || !parametersValid(parameters,QJsonObject::fromVariantMap(m_entries.value(id).value(QStringLiteral("document")).toMap().value(QStringLiteral("parameters")).toMap())))return {};
    const auto ticket=holdDocument(id);if(m_attached && ticket.isEmpty())return {};
    const auto key=QUuid::createUuid().toString(QUuid::WithoutBraces);
    Session session{id,value,parameters,projectContext(m_entries.value(id),context),++m_generation,false};
    session.documentTicket=ticket;session.ready=!value.value(QStringLiteral("pending")).toBool();
    auto state=value.value(QStringLiteral("state")).toMap();state.insert(QStringLiteral("parameters"),parameters);state.insert(QStringLiteral("context"),session.context);
    session.descriptor.insert(QStringLiteral("state"),state);m_sessions.insert(key,session);
    const QPointer<AddonUiRegistry> alive(this);
    if(session.ready && m_assets) {
        const bool opened=m_assets->openSession(key,id);if(!alive)return {};
        if(m_sessions.contains(key)){m_sessions[key].assetsOpened=opened;m_sessions[key].descriptor.insert(QStringLiteral("assetUrls"),m_assets?m_assets->sessionAssets(key):QVariantMap{});}
    }
    return key;
}
bool AddonUiRegistry::materializeSession(const QString& id)
{
    if(!m_attached || !available() || !m_sessions.contains(id))return false;
    const auto old=m_sessions.value(id);auto value=descriptor(old.contribution,false);
    if(value.isEmpty() || value.value(QStringLiteral("unavailable")).toBool()) {
        m_sessions[id].ready=false;m_sessions[id].descriptor.insert(QStringLiteral("unavailable"),true);m_sessions[id].descriptor.insert(QStringLiteral("pending"),false);
        const QPointer<AddonUiRegistry> alive(this);emit sessionChanged(id);if(alive)emit sessionFailed(id);return false;
    }
    if(value.value(QStringLiteral("pending")).toBool())return false;
    value.insert(QStringLiteral("state"),old.descriptor.value(QStringLiteral("state")));
    value.insert(QStringLiteral("assetUrls"),m_assets&&old.assetsOpened?m_assets->sessionAssets(id):old.descriptor.value(QStringLiteral("assetUrls")));
    const bool changed=!old.ready || value!=old.descriptor;
    m_sessions[id].descriptor=std::move(value);m_sessions[id].ready=true;
    const QPointer<AddonUiRegistry> alive(this);
    if(m_assets && !old.suspended && !old.assetsOpened) {
        const bool opened=m_assets->openSession(id,old.contribution);if(!alive)return false;
        if(!m_sessions.contains(id))return false;
        m_sessions[id].assetsOpened=opened;m_sessions[id].descriptor.insert(QStringLiteral("assetUrls"),m_assets?m_assets->sessionAssets(id):QVariantMap{});
    }
    if(changed){emit sessionChanged(id);if(!alive)return false;}
    if(m_panelContextWanted.contains(id)){resumePanelContext(id);if(!alive)return false;}
    return true;
}
bool AddonUiRegistry::bindSessionOwner(const QString& id, QObject* owner)
{
    if(!available())return false;
    auto session = m_sessions.find(id);
    if (session == m_sessions.end() || !owner || (session->owner && session->owner != owner)) return false;
    if (!session->owner) {
        session->owner = owner;
        session->ownerDestroyed = connect(owner, &QObject::destroyed, this, [this, id] { closeSession(id); });
    }
    return true;
}
QString AddonUiRegistry::claimImage(const QString& id, QObject* owner)
{
    const auto session = m_sessions.constFind(id);
    if (!available() || !m_assets || session == m_sessions.cend() || !session->ready || session->suspended || !session->owner || !owner || owner == session->owner) return {};
    QObject* parent = owner; QSet<QObject*> visited;
    for (int depth = 0; parent && depth < 128 && !visited.contains(parent); ++depth) {
        if (parent == session->owner) return m_assets->claimImage(id, owner);
        visited.insert(parent);
        const auto visual = parent->inherits("QQuickItem") ? parent->property("parent").value<QObject*>() : nullptr;
        parent = visual ? visual : parent->parent();
    }
    return {};
}
void AddonUiRegistry::releaseImage(const QString& claim)
{ if (m_assets) m_assets->releaseImage(claim); }
QString AddonUiRegistry::assetUrlForClaim(const QString& id, const QString& alias, const QString& claim) const
{
    const auto session = m_sessions.constFind(id);
    return available() && m_assets && session != m_sessions.cend() && session->ready && !session->suspended && session->owner ? m_assets->assetUrlForClaim(id, alias, claim) : QString{};
}
QVariantMap AddonUiRegistry::session(const QString& id) const
{
    if(!m_sessions.contains(id))return {};
    auto value=m_sessions.value(id).descriptor;
    if(m_attached)value.insert(QStringLiteral("available"),available()&&m_sessions.value(id).ready);
    return value;
}
bool AddonUiRegistry::dispatchSession(const QString& id, const QString& command, const QVariantMap& arguments)
{
    if(!available())return false;
    const auto session = m_sessions.find(id);
    if (session == m_sessions.end() || !session->ready || session->suspended || !session->descriptor.value("registeredCommands").toStringList().contains(command)) return false;
    const auto generation=session->generation=++m_generation;
    const QPointer<AddonUiRegistry> alive(this);cancelSessionJobs(id);
    if(!alive || !available())return false;
    const auto current=m_sessions.constFind(id);
    // Session commands come from a click in the view, so a reply can ask for
    // a host effect. Context calls from the app are not gestures.
    return current!=m_sessions.cend() && current->ready && !current->suspended && current->generation==generation
        && dispatch(command, arguments, id, {}, {}, {}, true);
}
void AddonUiRegistry::cancelSessionJobs(const QString& id)
{
    const auto requests = m_runningCommands.keys();
    const auto cancel = m_backend.cancel; QPointer<AddonUiRegistry> guard(this);
    for (const auto& request : requests) if (m_runningCommands.value(request).session == id) {
        m_runningCommands.remove(request); cancel(request); if (!guard) return;
    }
}
void AddonUiRegistry::suspendSession(const QString& id, bool suspended)
{
    if(!suspended && !available())return;
    auto it = m_sessions.find(id); if (it == m_sessions.end() || it->suspended == suspended) return;
    it->suspended = suspended; it->generation = ++m_generation;
    QPointer<AddonUiRegistry> guard(this);
    if (m_assets && it->assetsOpened) m_assets->suspendSession(id, suspended);
    // A suspended panel loses its running context call; it asks again on resume.
    if (guard && suspended && std::find(m_panelSessions.cbegin(), m_panelSessions.cend(), id) != m_panelSessions.cend()) m_panelContextWanted.insert(id);
    if (guard && suspended) cancelSessionJobs(id);
    else if(guard && m_attached)materializeSession(id);
    else if(guard && m_panelContextWanted.contains(id))resumePanelContext(id);
}
void AddonUiRegistry::closeSession(const QString& id)
{
    const auto session=m_sessions.constFind(id);QString ticket;
    if(session!=m_sessions.cend()){disconnect(session->ownerDestroyed);ticket=session->documentTicket;}
    m_sessions.remove(id);
    for(auto it=m_panelSessions.begin();it!=m_panelSessions.end();)if(it.value()==id)it=m_panelSessions.erase(it);else ++it;
    m_panelContextWanted.remove(id);
    QPointer<AddonUiRegistry> guard(this);dropDocument(ticket);if(!guard)return;
    if(m_assets)m_assets->closeSession(id);if(guard)cancelSessionJobs(id);
}
QString AddonUiRegistry::executeReplacement(const QString& hook, const QVariantMap& arguments)
{
    if(!available())return {};
    if (hook != QLatin1String("actions.subtitles.open@1")) return {};
    const auto entry = replacement(hook); if (entry.isEmpty()) return {};
    const auto ticket = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return dispatch(entry.value("document").toMap().value("command").toString(), arguments, {}, ticket) ? ticket : QString();
}
void AddonUiRegistry::operationFinished(const QString& request, const QString& contribution, const QString& status, const QVariantMap& data, const QString&)
{
    if (!m_runningCommands.contains(request)) return;
    const auto pending = m_runningCommands.take(request);
    if(!available()) {
        const QPointer<AddonUiRegistry> alive(this);emit commandCompleted(pending.command,false,{});
        if(alive&&!pending.replacement.isEmpty())emit replacementFinished(pending.replacement,false);
        return;
    }
    const QPointer<AddonUiRegistry> alive(this);
    bool success = contribution == pending.command && (status == QLatin1String("match") || status == QLatin1String("partial"));
    if (!pending.panel.isEmpty()) { panelReply(pending, success, data); if (!alive) return; }
    auto session = m_sessions.find(pending.session);
    if (!pending.session.isEmpty()) {
        if (session == m_sessions.end() || !session->ready || session->suspended || session->generation != pending.generation) return;
        if (success && (data.contains("state") || data.contains("patch"))) {
            const auto stateValue = data.value(data.contains("state") ? QStringLiteral("state") : QStringLiteral("patch"));
            success = stateValue.metaType().id() == QMetaType::QVariantMap;
            auto state = data.contains("state") ? stateValue.toMap() : session->descriptor.value("state").toMap();
            if (data.contains("patch")) {
                const auto patch = stateValue.toMap(); for (auto it = patch.begin(); it != patch.end(); ++it) state.insert(it.key(), it.value());
            }
            state.insert(QStringLiteral("parameters"), session->parameters); state.insert(QStringLiteral("context"), session->context);
            const auto json = QJsonObject::fromVariantMap(state); qsizetype nodes = 0;
            success = success && QJsonDocument(json).toJson(QJsonDocument::Compact).size() <= 128 * 1024 && boundedData(json, 0, nodes);
            auto packageState = json; packageState.remove(QStringLiteral("parameters")); packageState.remove(QStringLiteral("context"));
            const auto schema = QJsonObject::fromVariantMap(m_entries.value(session->contribution).value("document").toMap().value("stateSchema").toMap());
            success = success && !schema.isEmpty() && validateUiValue(schema, packageState);
            if (success) { session->descriptor.insert(QStringLiteral("state"), state); emit sessionChanged(pending.session);if(!alive)return; }
        }
        if (!success) emit sessionFailed(pending.session);if(!alive)return;
    }
    if (success && pending.gesture && !pending.session.isEmpty() && data.contains(QStringLiteral("effect"))) {
        const auto panel = m_sessions.value(pending.session).contribution;
        const auto effect = data.value(QStringLiteral("effect")).toMap();
        static const QStringList effects{QStringLiteral("play"), QStringLiteral("rescan"), QStringLiteral("identify"), QStringLiteral("source")};
        if (type(m_entries.value(panel)) == QLatin1String("ui.panel") && effect.size() == 1 && effects.contains(effect.firstKey())
            && effect.first().metaType().id() == QMetaType::QVariantMap) {
            emit panelEffect(pending.session, m_entries.value(panel).value(QStringLiteral("packageId")).toString(), effect.firstKey(), effect.first().toMap());
            if (!alive) return;
        }
    }
    emit commandCompleted(pending.command, success, success ? data : QVariantMap{});if(!alive)return;
    if (!pending.replacement.isEmpty()) emit replacementFinished(pending.replacement, success);
}
QVariantList AddonUiRegistry::panels() const
{
    QVariantList result;
    if (!available()) return result;
    for (const auto& entry : m_entries) {
        if (type(entry) != QLatin1String("ui.panel")) continue;
        const auto document = entry.value("document").toMap(); const auto id = entry.value("id").toString();
        result.append(QVariantMap{{"id", id}, {"packageId", entry.value("packageId")}, {"name", document.value("name")},
            {"label", document.value("label")}, {"placement", document.value("placement")},
            {"iconMarkup", entry.value("iconMarkup").toString()}, {"available", m_panelAvailable.value(id, false)},
            {"open", m_panelSessions.contains(id)}});
    }
    return result;
}
bool AddonUiRegistry::siblingsWanted() const
{
    if (!available()) return false;
    for (const auto& entry : m_entries)
        if (type(entry) == QLatin1String("ui.panel") && !scope(entry.value("packageId").toString(), QStringLiteral("media.siblings")).value("sources").toArray().isEmpty())
            return true;
    return false;
}
void AddonUiRegistry::setPanelMedia(const QVariantMap& media)
{
    // Copy only what a panel may know about the media: its kind and length.
    QVariantMap projected{{"revision", double(++m_panelRevision)}};
    const auto kind = media.value(QStringLiteral("kind")).toString();
    if (QStringList{"video", "audio", "none"}.contains(kind)) projected.insert(QStringLiteral("kind"), kind);
    const auto duration = media.value(QStringLiteral("durationMs")).toDouble();
    if (std::isfinite(duration) && duration > 0 && duration < 1e12) projected.insert(QStringLiteral("durationMs"), std::floor(duration));
    m_panelMedia = projected;
    m_panelRefresh->stop();
    // Old context calls cannot answer for the new media.
    const auto cancel = m_backend.cancel; const QPointer<AddonUiRegistry> alive(this);
    for (const auto& request : m_runningCommands.keys())
        if (!m_runningCommands.value(request).panel.isEmpty()) { m_runningCommands.remove(request); cancel(request); if (!alive) return; }
    if (!available()) return;
    for (const auto& entry : m_entries)
        if (type(entry) == QLatin1String("ui.panel")) { dispatchPanelContext(entry.value("id").toString()); if (!alive) return; }
}
void AddonUiRegistry::refreshPanels()
{
    if (!m_panelSessions.isEmpty() && !m_panelRefresh->isActive()) m_panelRefresh->start();
}
bool AddonUiRegistry::attachPanelSession(const QString& id)
{
    const auto session = m_sessions.constFind(id);
    if (!available() || session == m_sessions.cend() || type(m_entries.value(session->contribution)) != QLatin1String("ui.panel")) return false;
    m_panelSessions.insert(session->contribution, id);
    dispatchPanelContext(session->contribution);
    return true;
}
void AddonUiRegistry::dispatchPanelContext(const QString& panel)
{
    const auto entry = m_entries.value(panel);
    if (!available() || type(entry) != QLatin1String("ui.panel") || m_panelMedia.isEmpty()) return;
    const auto command = entry.value("packageId").toString() + QLatin1Char('/') + entry.value("document").toMap().value("command").toString();
    const QVariantMap extra{{"media", m_panelMedia}, {"panel", panel.section(QLatin1Char('/'), 1)}};
    const auto sessionId = m_panelSessions.value(panel);
    if (sessionId.isEmpty()) { dispatch(command, {}, {}, {}, extra, panel, false); return; }
    const auto session = m_sessions.find(sessionId);
    if (session == m_sessions.end()) return;
    // An attached view loads its document after the session opens: call the
    // panel when the session is ready and visible (resumePanelContext).
    if (!session->ready || session->suspended) { m_panelContextWanted.insert(sessionId); return; }
    m_panelContextWanted.remove(sessionId);
    // Like a session command: the newest call wins, older calls are canceled.
    const auto generation = session->generation = ++m_generation;
    const QPointer<AddonUiRegistry> alive(this); cancelSessionJobs(sessionId);
    if (!alive || !m_sessions.contains(sessionId) || m_sessions.value(sessionId).generation != generation) return;
    dispatch(command, {}, sessionId, {}, extra, panel, false);
}
void AddonUiRegistry::resumePanelContext(const QString& sessionId)
{
    const auto session = m_sessions.constFind(sessionId);
    if (session == m_sessions.cend() || !session->ready || session->suspended || m_panelSessions.value(session->contribution) != sessionId) return;
    dispatchPanelContext(session->contribution);
}
void AddonUiRegistry::panelReply(const Pending& pending, bool success, const QVariantMap& data)
{
    if (pending.panelRevision != m_panelRevision || !m_entries.contains(pending.panel)) return;
    // Without a clear answer the button stays hidden.
    const bool value = success && data.value(QStringLiteral("available")).metaType().id() == QMetaType::Bool && data.value(QStringLiteral("available")).toBool();
    if (m_panelAvailable.value(pending.panel, false) == value && m_panelAvailable.contains(pending.panel)) return;
    m_panelAvailable.insert(pending.panel, value);
    emit panelsChanged();
}
void AddonUiRegistry::acknowledgeSession(const QString& id)
{
    const auto session = m_sessions.constFind(id); if (!available() || session == m_sessions.cend() || !session->ready || session->suspended) return;
    const auto tickets = m_replacementViews.keys(session->contribution);const QPointer<AddonUiRegistry> alive(this);
    for (const auto& ticket : tickets) { m_replacementViews.remove(ticket); emit replacementFinished(ticket, true);if(!alive)return; }
}
bool AddonUiRegistry::completeModal(const QString& ticket, const QVariantMap& result)
{
    if (!available() || !m_modals.contains(ticket)) return false;
    const auto candidate=descriptor(m_modals.value(ticket),false);
    if(candidate.isEmpty() || candidate.value(QStringLiteral("pending")).toBool() || candidate.value(QStringLiteral("unavailable")).toBool())return false;
    const auto contribution = m_entries.value(m_modals.value(ticket)).value("document").toMap();
    if (!parametersValid(result, QJsonObject::fromVariantMap(contribution.value("resultSchema").toMap()))) return false;
    m_modals.remove(ticket); emit modalFinished(ticket, true, result); return true;
}
void AddonUiRegistry::cancelModal(const QString& ticket)
{ if (m_modals.remove(ticket)) emit modalFinished(ticket, false, {}); }

bool AddonUiRegistry::canUseSelection(const QString& id,const QString& hook) const
{
    if(!available() || !canPrepareSelection(id,hook))return false;
    const auto candidate=descriptor(id,false);
    return !candidate.isEmpty() && !candidate.value(QStringLiteral("pending")).toBool()
        && !candidate.value(QStringLiteral("unavailable")).toBool();
}
bool AddonUiRegistry::canPrepareSelection(const QString& id, const QString& hook) const
{
    if (!m_manager || m_ownerRetired || m_destroying || m_reloading || (m_attached && !m_catalogue)
        || (hook != QLatin1String("subtitles.search.content@1") && hook != QLatin1String("dialog.add-to-playlist.content@1"))
        || m_selected.value(hook).toString() != id || !m_entries.contains(id) || m_failed.contains(id)) return false;
    const auto entry = m_entries.value(id), document = entry.value("document").toMap();
    if (document.value("type").toString() != QLatin1String("ui.replacement") || document.value("hook").toString() != hook
        || !permitted(entry, hook)) return false;
    if (hook == QLatin1String("dialog.add-to-playlist.content@1")) {
        const auto scope = this->scope(entry.value("packageId").toString(),QStringLiteral("library.read"));
        return scope.value("collections").toArray().contains(QStringLiteral("playlists"))
            && scope.value("fields").toArray().contains(QStringLiteral("title"));
    }
    return true;
}

bool AddonUiRegistry::validateSessionResult(const QString& id, const QVariantMap& result) const
{
    const auto session = m_sessions.constFind(id);
    if (!available() || session == m_sessions.cend() || !session->ready || session->suspended || !m_entries.contains(session->contribution)) return false;
    const auto choiceSet = session->parameters.value(QStringLiteral("choiceSet"));
    if (choiceSet.metaType().id() != QMetaType::QString || choiceSet.toString().isEmpty()
        || result.value(QStringLiteral("choiceSet")) != choiceSet) return false;
    const auto document = m_entries.value(session->contribution).value("document").toMap();
    return canUseSelection(session->contribution, document.value("hook").toString()) && document.contains("resultSchema")
        && parametersValid(result, QJsonObject::fromVariantMap(document.value("resultSchema").toMap()));
}
}
