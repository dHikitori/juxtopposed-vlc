// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_ui_registry.hpp"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
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

AddonUiRegistry::AddonUiRegistry(AddonManager* manager, QObject* parent) : QObject(parent), m_manager(manager)
{
    QFile file(QDir(manager->storageDirectory()).filePath(QStringLiteral("ui-replacements.json")));
    if (file.open(QIODevice::ReadOnly) && file.size() <= 64 * 1024) m_selected = QJsonDocument::fromJson(file.readAll()).object();
    connect(manager, &AddonManager::revisionChanged, this, &AddonUiRegistry::reload);
    connect(manager, &AddonManager::operationFinished, this, [this](const QString& request, const QString&, const QString& status, const QVariantMap& data, const QString&) {
        const auto command = m_runningCommands.take(request);
        if (!command.isEmpty()) emit commandCompleted(command, status == QLatin1String("match") || status == QLatin1String("partial"), data);
    });
    reload();
}
void AddonUiRegistry::reload()
{
    m_entries.clear(); m_failed.clear();
    for (const auto& type : {QStringLiteral("ui.command"), QStringLiteral("ui.route"), QStringLiteral("ui.modal"), QStringLiteral("ui.section"), QStringLiteral("ui.replacement")}) {
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
    if (schema.value("type").toString() != QLatin1String("object")) return false;
    const auto properties = schema.value("properties").toObject();
    for (auto it = parameters.begin(); it != parameters.end(); ++it) {
        if (!properties.contains(it.key())) return false;
        const auto field = properties.value(it.key()).toObject(); const auto value = QJsonValue::fromVariant(it.value());
        const auto type = field.value("type").toString();
        if ((type == QLatin1String("string") && !value.isString()) || (type == QLatin1String("boolean") && !value.isBool())
            || (type == QLatin1String("number") && !value.isDouble()) || (type != QLatin1String("string") && type != QLatin1String("boolean") && type != QLatin1String("number"))) return false;
    }
    for (const auto& required : schema.value("required").toArray()) if (!parameters.contains(required.toString())) return false;
    return true;
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
    if (!m_entries.contains(id) || m_entries.value(id).value("document").toMap().value("type").toString() != QLatin1String("ui.modal")
        || view(id).isEmpty() || !parametersValid(parameters, QJsonObject::fromVariantMap(m_entries.value(id).value("document").toMap().value("parameters").toMap()))) return false;
    emit modalRequested(id, parameters); return true;
}
bool AddonUiRegistry::triggerCommand(const QString& id, const QVariantMap& arguments)
{
    const auto entry = m_entries.value(id); const auto document = entry.value("document").toMap();
    if (entry.isEmpty() || document.value("type").toString() != QLatin1String("ui.command")
        || !parametersValid(arguments, QJsonObject::fromVariantMap(document.value("parameters").toMap()))) return false;
    auto route = document.value("route").toString();
    if (!route.isEmpty()) {
        if (!route.startsWith(QLatin1String("addon/"))) route = QStringLiteral("addon/") + entry.value("packageId").toString() + '/' + route;
        if (!route.startsWith(QStringLiteral("addon/") + entry.value("packageId").toString() + '/')) return false;
        return openRoute(route, arguments);
    }
    const auto operation = document.value("command").toString();
    if (operation.isEmpty()) return false;
    const auto request = m_manager->invoke(id, operation, {{"arguments", arguments}});
    m_runningCommands.insert(request, id); return true;
}
bool AddonUiRegistry::triggerReplacement(const QString& hook, const QVariantMap& arguments)
{
    if (hook != QLatin1String("actions.subtitles.open@1")) return false;
    const auto entry = replacement(hook); if (entry.isEmpty()) return false;
    const auto command = entry.value("document").toMap().value("command").toString();
    return triggerCommand(command, arguments);
}
void AddonUiRegistry::reportViewFailure(const QString& id)
{ if (m_entries.contains(id)) { m_failed.insert(id); m_error = tr("An add-on view is unavailable. The built-in view remains available."); ++m_revision; emit changed(); } }
}
