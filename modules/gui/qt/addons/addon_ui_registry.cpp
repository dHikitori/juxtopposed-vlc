// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_ui_registry.hpp"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>
#include <algorithm>

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
        {QStringLiteral("actions.subtitles.open@1"), {QStringLiteral("ui.replacement")}}
    };
    return value;
}
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
    : QObject(parent), m_manager(manager), m_backend(std::move(backend))
{
    const QPointer<AddonManager> guardedManager(manager);
    if (!m_backend.invoke) m_backend.invoke = [guardedManager](const QString& id, const QString& operation, const QVariantMap& context) { return guardedManager ? guardedManager->invoke(id, operation, context) : QString(); };
    if (!m_backend.cancel) m_backend.cancel = [guardedManager](const QString& request) { if (guardedManager) guardedManager->cancel(request); };
    QFile file(QDir(manager->storageDirectory()).filePath(QStringLiteral("ui-replacements.json")));
    if (file.open(QIODevice::ReadOnly) && file.size() <= 64 * 1024) m_selected = QJsonDocument::fromJson(file.readAll()).object();
    connect(manager, &AddonManager::revisionChanged, this, &AddonUiRegistry::reload);
    connect(manager, &AddonManager::operationFinished, this, &AddonUiRegistry::operationFinished);
    connect(manager, &QObject::destroyed, this, &AddonUiRegistry::reload);
    reload();
}
AddonUiRegistry::~AddonUiRegistry()
{ for (const auto& request : m_runningCommands.keys()) m_backend.cancel(request); }
void AddonUiRegistry::reload()
{
    const auto sessions = m_sessions.keys();
    for (const auto& id : sessions) { closeSession(id); emit sessionFailed(id); }
    const auto pending = m_runningCommands;
    m_runningCommands.clear();
    for (auto it = pending.begin(); it != pending.end(); ++it) {
        m_backend.cancel(it.key());
        if (!it->replacement.isEmpty()) emit replacementFinished(it->replacement, false);
    }
    const auto modals = m_modals.keys(); for (const auto& ticket : modals) cancelModal(ticket);
    const auto replacements = m_replacementViews.keys(); m_replacementViews.clear();
    for (const auto& ticket : replacements) emit replacementFinished(ticket, false);
    m_entries.clear(); m_failed.clear();
    if (m_manager) for (const auto& type : {QStringLiteral("ui.command"), QStringLiteral("ui.route"), QStringLiteral("ui.modal"), QStringLiteral("ui.section"), QStringLiteral("ui.replacement")}) {
        for (const auto& value : m_manager->contributionsOfType(type)) {
            const auto entry = value.toMap(); const auto document = entry.value("document").toMap();
            const auto hook = document.value("hook").toString();
            if (!hook.isEmpty() && (!hooks().contains(hook) || !hooks().value(hook).contains(type))) continue;
            if (!permitted(entry, hook)) continue;
            m_entries.insert(entry.value("id").toString(), entry);
        }
    }
    ++m_revision; emit changed();
}
bool AddonUiRegistry::permitted(const QVariantMap& entry, const QString& hook) const
{
    const auto scope = m_manager->grantedScope(entry.value("packageId").toString(), QStringLiteral("ui.contribute"));
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
            const auto descriptor = view(entry.value("id").toString()); if (!descriptor.isEmpty()) result.append(descriptor);
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
    if (!replaceable(hook)) return {};
    const auto id = m_selected.value(hook).toString();
    if (!m_entries.contains(id) || m_entries.value(id).value("document").toMap().value("hook").toString() != hook) return {};
    if (hook.startsWith(QLatin1String("actions."))) return m_entries.value(id);
    return view(id);
}
bool AddonUiRegistry::selectReplacement(const QString& hook, const QString& id)
{
    if (!replaceable(hook)) return false;
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
    m_selected = selected; ++m_revision; emit changed(); return true;
}
bool AddonUiRegistry::validViewDocument(const QJsonObject& document)
{
    qsizetype nodes = 0;
    if (!boundedData(document, 0, nodes) || document.value("contract").toString() != QLatin1String("ui@1") || !document.value("root").isObject()) return false;
    static const QSet<QString> types{QStringLiteral("Page"), QStringLiteral("Section"), QStringLiteral("Column"), QStringLiteral("Row"),
        QStringLiteral("Heading"), QStringLiteral("Text"), QStringLiteral("Notice"), QStringLiteral("EmptyState"), QStringLiteral("Button"),
        QStringLiteral("IconButton"), QStringLiteral("Image"), QStringLiteral("MediaCard"), QStringLiteral("List"), QStringLiteral("Progress")};
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
{
    if (!m_entries.contains(id) || m_failed.contains(id)) return {};
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
    for (const auto& entry : routes()) {
        const auto item = entry.toMap(); if (item.value("route").toString() != route) continue;
        if (view(item.value("id").toString()).isEmpty() || !parametersValid(parameters, QJsonObject::fromVariantMap(item.value("document").toMap().value("parameters").toMap()))) return false;
        emit routeRequested(route, parameters); return true;
    }
    return false;
}
bool AddonUiRegistry::openModal(const QString& id, const QVariantMap& parameters)
{
    if (m_modals.size() >= 8 || !m_entries.contains(id) || m_entries.value(id).value("document").toMap().value("type").toString() != QLatin1String("ui.modal")
        || view(id).isEmpty() || !parametersValid(parameters, QJsonObject::fromVariantMap(m_entries.value(id).value("document").toMap().value("parameters").toMap()))) return false;
    const auto ticket = QUuid::createUuid().toString(QUuid::WithoutBraces); m_modals.insert(ticket, id);
    emit modalRequested(id, parameters); emit modalSessionRequested(ticket, id, parameters); return true;
}
bool AddonUiRegistry::triggerCommand(const QString& id, const QVariantMap& arguments)
{
    return dispatch(id, arguments, {}, {});
}
bool AddonUiRegistry::dispatch(const QString& id, const QVariantMap& arguments, const QString& sessionId, const QString& replacement)
{
    if (m_runningCommands.size() >= 64) return false;
    const auto entry = m_entries.value(id); const auto document = entry.value("document").toMap();
    if (entry.isEmpty() || document.value("type").toString() != QLatin1String("ui.command")
        || !parametersValid(arguments, QJsonObject::fromVariantMap(document.value("parameters").toMap()))) return false;
    auto route = document.value("route").toString();
    if (!route.isEmpty()) {
        if (!route.startsWith(QLatin1String("addon/"))) route = QStringLiteral("addon/") + entry.value("packageId").toString() + '/' + route;
        if (!route.startsWith(QStringLiteral("addon/") + entry.value("packageId").toString() + '/')) return false;
        if (!replacement.isEmpty()) for (const auto& candidate : routes()) if (candidate.toMap().value("route").toString() == route) m_replacementViews.insert(replacement, candidate.toMap().value("id").toString());
        const bool success = openRoute(route, arguments);
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
    quint64 generation = 0;
    if (!sessionId.isEmpty()) {
        const auto session = m_sessions.constFind(sessionId);
        if (session == m_sessions.cend() || session->suspended || session->descriptor.value("packageId") != entry.value("packageId")) return false;
        context.insert("parameters", session->parameters);
        context.insert("ui", session->context);
        context.insert("state", session->descriptor.value("state"));
        context.insert("identity", session->context.value("identity"));
        generation = session->generation;
    }
    const auto request = m_backend.invoke(id, operation, context);
    if (request.isEmpty()) return false;
    m_runningCommands.insert(request, {id, sessionId, replacement, generation});
    QTimer::singleShot(15000, this, [this, request] {
        if (!m_runningCommands.contains(request)) return;
        const auto pending = m_runningCommands.take(request);
        m_backend.cancel(request);
        emit commandCompleted(pending.command, false, {});
        if (!pending.replacement.isEmpty()) emit replacementFinished(pending.replacement, false);
        if (!pending.session.isEmpty() && m_sessions.contains(pending.session)) emit sessionFailed(pending.session);
    });
    return true;
}
bool AddonUiRegistry::triggerReplacement(const QString& hook, const QVariantMap& arguments)
{
    if (hook != QLatin1String("actions.subtitles.open@1")) return false;
    const auto entry = replacement(hook); if (entry.isEmpty()) return false;
    const auto command = entry.value("document").toMap().value("command").toString();
    return triggerCommand(command, arguments);
}
void AddonUiRegistry::reportViewFailure(const QString& id)
{
    const auto tickets = m_replacementViews.keys(id);
    for (const auto& ticket : tickets) { m_replacementViews.remove(ticket); emit replacementFinished(ticket, false); }
    if (m_entries.contains(id)) { m_failed.insert(id); m_error = tr("An add-on view is not available. Use the built-in view."); ++m_revision; emit changed(); }
}

QVariantMap AddonUiRegistry::projectContext(const QVariantMap& entry, const QVariantMap& input) const
{
    QVariantMap output;
    for (const auto& key : {QStringLiteral("surface"), QStringLiteral("search"), QStringLiteral("layout")}) {
        if (input.value(key).metaType().id() == QMetaType::QString && input.value(key).toString().size() <= 256) output.insert(key, input.value(key));
    }
    const auto identity = input.value(QStringLiteral("identity")).toMap();
    const auto fields = m_manager->grantedScope(entry.value("packageId").toString(), QStringLiteral("metadata.context")).value("fields").toArray();
    QVariantMap permitted;
    for (const auto& field : fields) {
        const auto name = field.toString(); const auto value = QJsonValue::fromVariant(identity.value(name));
        if ((value.isString() && value.toString().size() <= 4096) || value.isBool() || value.isDouble()) permitted.insert(name, value.toVariant());
    }
    if (!permitted.isEmpty()) output.insert(QStringLiteral("identity"), permitted);
    return output;
}
QString AddonUiRegistry::createSession(const QString& id, const QVariantMap& parameters, const QVariantMap& context)
{
    if (m_sessions.size() >= 32) return {};
    const auto descriptor = view(id);
    if (descriptor.isEmpty() || !parametersValid(parameters, QJsonObject::fromVariantMap(m_entries.value(id).value("document").toMap().value("parameters").toMap()))) return {};
    const auto key = QUuid::createUuid().toString(QUuid::WithoutBraces);
    Session session{id, descriptor, parameters, projectContext(m_entries.value(id), context), ++m_generation, false};
    auto state = descriptor.value("state").toMap();
    state.insert(QStringLiteral("parameters"), parameters); state.insert(QStringLiteral("context"), session.context);
    session.descriptor.insert(QStringLiteral("state"), state);
    m_sessions.insert(key, session);
    return key;
}
QVariantMap AddonUiRegistry::session(const QString& id) const
{ return m_sessions.contains(id) ? m_sessions.value(id).descriptor : QVariantMap{}; }
bool AddonUiRegistry::dispatchSession(const QString& id, const QString& command, const QVariantMap& arguments)
{
    const auto session = m_sessions.find(id);
    if (session == m_sessions.end() || session->suspended || !session->descriptor.value("registeredCommands").toStringList().contains(command)) return false;
    session->generation = ++m_generation;
    cancelSessionJobs(id);
    return dispatch(command, arguments, id, {});
}
void AddonUiRegistry::cancelSessionJobs(const QString& id)
{
    const auto requests = m_runningCommands.keys();
    for (const auto& request : requests) if (m_runningCommands.value(request).session == id) {
        m_runningCommands.remove(request); m_backend.cancel(request);
    }
}
void AddonUiRegistry::suspendSession(const QString& id, bool suspended)
{
    auto it = m_sessions.find(id); if (it == m_sessions.end() || it->suspended == suspended) return;
    it->suspended = suspended; it->generation = ++m_generation;
    if (suspended) cancelSessionJobs(id);
}
void AddonUiRegistry::closeSession(const QString& id)
{ m_sessions.remove(id); cancelSessionJobs(id); }
QString AddonUiRegistry::executeReplacement(const QString& hook, const QVariantMap& arguments)
{
    if (hook != QLatin1String("actions.subtitles.open@1")) return {};
    const auto entry = replacement(hook); if (entry.isEmpty()) return {};
    const auto ticket = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return dispatch(entry.value("document").toMap().value("command").toString(), arguments, {}, ticket) ? ticket : QString();
}
void AddonUiRegistry::operationFinished(const QString& request, const QString& contribution, const QString& status, const QVariantMap& data, const QString&)
{
    if (!m_runningCommands.contains(request)) return;
    const auto pending = m_runningCommands.take(request);
    bool success = contribution == pending.command && (status == QLatin1String("match") || status == QLatin1String("partial"));
    auto session = m_sessions.find(pending.session);
    if (!pending.session.isEmpty()) {
        if (session == m_sessions.end() || session->suspended || session->generation != pending.generation) return;
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
            if (success) { session->descriptor.insert(QStringLiteral("state"), state); emit sessionChanged(pending.session); }
        }
        if (!success) emit sessionFailed(pending.session);
    }
    emit commandCompleted(pending.command, success, success ? data : QVariantMap{});
    if (!pending.replacement.isEmpty()) emit replacementFinished(pending.replacement, success);
}
void AddonUiRegistry::acknowledgeSession(const QString& id)
{
    const auto session = m_sessions.constFind(id); if (session == m_sessions.cend() || session->suspended) return;
    const auto tickets = m_replacementViews.keys(session->contribution);
    for (const auto& ticket : tickets) { m_replacementViews.remove(ticket); emit replacementFinished(ticket, true); }
}
bool AddonUiRegistry::completeModal(const QString& ticket, const QVariantMap& result)
{
    if (!m_modals.contains(ticket)) return false;
    const auto contribution = m_entries.value(m_modals.value(ticket)).value("document").toMap();
    if (!parametersValid(result, QJsonObject::fromVariantMap(contribution.value("resultSchema").toMap()))) return false;
    m_modals.remove(ticket); emit modalFinished(ticket, true, result); return true;
}
void AddonUiRegistry::cancelModal(const QString& ticket)
{ if (m_modals.remove(ticket)) emit modalFinished(ticket, false, {}); }
}
