// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_settings_migrations.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <cmath>

namespace vlc::addons {
namespace {
bool fail(QString* error, const QString& message)
{ if (error) *error = message; return false; }
bool onlyKeys(const QJsonObject& object, const QSet<QString>& keys)
{
    for (auto it = object.begin(); it != object.end(); ++it)
        if (!keys.contains(it.key())) return false;
    return true;
}
bool fieldName(const QJsonValue& value)
{
    static const QRegularExpression field(QStringLiteral("^[A-Za-z][A-Za-z0-9_.-]{0,127}$"));
    return value.isString() && field.match(value.toString()).hasMatch();
}
bool version(const QJsonValue& value, int* output)
{
    const double number = value.toDouble(-1);
    if (!value.isDouble() || number < 1 || number > 1000000 || std::floor(number) != number) return false;
    *output = int(number); return true;
}
bool program(const QByteArray& bytes, QJsonArray* operations, QString* error)
{
    if (bytes.size() > 65536) return fail(error, QStringLiteral("The settings migration file is too large."));
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(bytes, &parse);
    const auto object = document.object();
    if (parse.error != QJsonParseError::NoError || !document.isObject()
        || !onlyKeys(object, {QStringLiteral("formatVersion"), QStringLiteral("operations")})
        || object.value("formatVersion") != 1 || !object.value("operations").isArray()
        || object.value("operations").toArray().size() > 128)
        return fail(error, QStringLiteral("The settings migration document is not correct."));
    *operations = object.value("operations").toArray();
    for (const auto& value : *operations) {
        if (!value.isObject()) return fail(error, QStringLiteral("The settings migration operation is not correct."));
        const auto operation = value.toObject();
        const auto kind = operation.value("op").toString();
        bool valid = false;
        if (kind == QStringLiteral("rename"))
            valid = onlyKeys(operation, {QStringLiteral("op"), QStringLiteral("from"), QStringLiteral("to")})
                && fieldName(operation.value("from")) && fieldName(operation.value("to"))
                && operation.value("from") != operation.value("to");
        else if (kind == QStringLiteral("remove"))
            valid = onlyKeys(operation, {QStringLiteral("op"), QStringLiteral("field")}) && fieldName(operation.value("field"));
        else if (kind == QStringLiteral("default"))
            valid = onlyKeys(operation, {QStringLiteral("op"), QStringLiteral("field"), QStringLiteral("value")})
                && fieldName(operation.value("field")) && operation.contains("value");
        if (!valid) return fail(error, QStringLiteral("The settings migration operation is not permitted."));
    }
    return true;
}
QJsonObject credentialBindings(const Manifest& manifest)
{
    QJsonObject result;
    for (const auto& value : manifest.permissionScope(QStringLiteral("credentials.use")).value("bindings").toArray()) {
        const auto binding = value.toObject();
        result.insert(binding.value("field").toString(), binding);
    }
    return result;
}
bool valueValid(const QJsonValue& value, const QJsonObject& field, int depth = 0)
{
    if (depth > 16 || (field.contains("enum") && !field.value("enum").toArray().contains(value))) return false;
    const auto type = field.value("type").toString();
    if (type == QStringLiteral("boolean")) return value.isBool();
    if (type == QStringLiteral("number") || type == QStringLiteral("integer")) {
        const auto number = value.toDouble();
        return value.isDouble() && std::isfinite(number)
            && (type != QStringLiteral("integer") || std::floor(number) == number)
            && number >= field.value("minimum").toDouble(-1e12) && number <= field.value("maximum").toDouble(1e12);
    }
    if (type == QStringLiteral("array")) {
        if (!value.isArray()) return false;
        const auto array = value.toArray();
        if (array.size() > qMin(64, field.value("maxItems").toInt(64)) || array.size() < field.value("minItems").toInt()) return false;
        QSet<QByteArray> seen;
        for (const auto& entry : array) {
            if (!valueValid(entry, field.value("items").toObject(), depth + 1)) return false;
            const auto key = QJsonDocument(QJsonArray{entry}).toJson(QJsonDocument::Compact);
            if (field.value("uniqueItems").toBool() && seen.contains(key)) return false;
            seen.insert(key);
        }
        return true;
    }
    if (type != QStringLiteral("string") || !value.isString()) return false;
    const auto text = value.toString();
    if (text.size() < field.value("minLength").toInt() || text.size() > qMin(4096, field.value("maxLength").toInt(4096))) return false;
    const auto pattern = field.value("pattern").toString();
    if (pattern.isEmpty()) return true;
    if (pattern != QStringLiteral("^[A-Za-z]{2,3}(-[A-Za-z0-9]{2,8})*$")) return false;
    static const QRegularExpression language(QStringLiteral("^[A-Za-z]{2,3}(-[A-Za-z0-9]{2,8})*$"));
    return text.size() <= 64 && language.match(text).hasMatch();
}
}

bool validateSettingsMigrations(const Manifest& manifest, const QMap<QString, QByteArray>& contents, QString* error)
{
    const auto settings = manifest.document.value("settings").toObject();
    if (!settings.contains("migrations")) return true;
    const auto value = settings.value("migrations");
    if (!value.isArray() || value.toArray().size() > 32)
        return fail(error, QStringLiteral("The settings migration list is not correct."));
    QSet<int> versions;
    for (const auto& entry : value.toArray()) {
        if (!entry.isObject()) return fail(error, QStringLiteral("The settings migration step is not correct."));
        const auto step = entry.toObject(); int from, to;
        const auto resource = step.value("resource").toString();
        if (!onlyKeys(step, {QStringLiteral("from"), QStringLiteral("to"), QStringLiteral("resource")})
            || !version(step.value("from"), &from) || !version(step.value("to"), &to)
            || to != from + 1 || to > manifest.settingsVersion || versions.contains(from)
            || !step.value("resource").isString() || !manifest.files.contains(resource)
            || (!contents.isEmpty() && !contents.contains(resource)))
            return fail(error, QStringLiteral("The settings migration chain is not correct."));
        versions.insert(from);
        QJsonArray operations;
        if (!contents.isEmpty() && !program(contents.value(resource), &operations, error)) return false;
    }
    return true;
}

bool migrateSettings(const Manifest& next, const QMap<QString, QByteArray>& contents,
                     const Manifest* previous, const QJsonObject& settings,
                     const QJsonObject& grants, QJsonObject* output, QString* error)
{
    if (!validateSettingsMigrations(next, contents, error)) return false;
    auto ordinary = settings; ordinary.remove("_credentials");
    const bool hadOrdinarySettings = !ordinary.isEmpty();
    const auto references = settings.value("_credentials").toObject();
    const auto nextBindings = credentialBindings(next);
    const auto oldBindings = previous ? credentialBindings(*previous) : QJsonObject{};
    const auto approvedBindings = grants.value("credentials.use").toObject().value("bindings").toArray();
    if (settings.contains("_credentials") && !settings.value("_credentials").isObject())
        return fail(error, QStringLiteral("The host credential reference envelope is not correct."));
    static const QRegularExpression uuid(QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
    for (auto it = references.begin(); it != references.end(); ++it)
        if (!it.value().isString() || !uuid.match(it.value().toString()).hasMatch()
            || !nextBindings.contains(it.key()) || nextBindings.value(it.key()) != oldBindings.value(it.key())
            || !approvedBindings.contains(nextBindings.value(it.key())))
            return fail(error, QStringLiteral("A saved credential binding changed. Keep the installed release and reconnect the account first."));
    const int oldVersion = previous ? previous->settingsVersion : next.settingsVersion;
    if (!ordinary.isEmpty() && oldVersion != next.settingsVersion) {
        if (oldVersion > next.settingsVersion)
            return fail(error, QStringLiteral("Use rollback to restore the older settings snapshot."));
        QMap<int, QJsonObject> steps;
        for (const auto& entry : next.document.value("settings").toObject().value("migrations").toArray()) {
            const auto step = entry.toObject(); steps.insert(step.value("from").toInt(), step);
        }
        for (int current = oldVersion; current < next.settingsVersion; ++current) {
            if (!steps.contains(current)) return fail(error, QStringLiteral("A settings migration step is missing. The installed release stays active."));
            QJsonArray operations;
            if (!program(contents.value(steps.value(current).value("resource").toString()), &operations, error)) return false;
            for (const auto& entry : operations) {
                const auto operation = entry.toObject(); const auto kind = operation.value("op").toString();
                if (kind == QStringLiteral("rename")) {
                    const auto from = operation.value("from").toString(), to = operation.value("to").toString();
                    if (oldBindings.contains(from) || nextBindings.contains(to))
                        return fail(error, QStringLiteral("A settings migration cannot change credential fields."));
                    if (ordinary.contains(from)) {
                        if (ordinary.contains(to)) return fail(error, QStringLiteral("The settings migration would replace an existing value."));
                        ordinary.insert(to, ordinary.take(from));
                    }
                } else {
                    const auto field = operation.value("field").toString();
                    if (oldBindings.contains(field) || nextBindings.contains(field))
                        return fail(error, QStringLiteral("A settings migration cannot change credential fields."));
                    if (kind == QStringLiteral("remove")) ordinary.remove(field);
                    else if (!ordinary.contains(field)) ordinary.insert(field, operation.value("value"));
                }
                if (QJsonDocument(ordinary).toJson(QJsonDocument::Compact).size() > 256 * 1024 || ordinary.size() > 64)
                    return fail(error, QStringLiteral("The settings migration result is too large."));
            }
        }
    }
    if (!next.settingsSchema.isEmpty()) {
        const auto schema = QJsonDocument::fromJson(contents.value(next.settingsSchema)).object();
        const auto properties = schema.value("properties").toObject();
        if (properties.size() > 64) return fail(error, QStringLiteral("The settings schema has too many fields."));
        for (auto it = ordinary.begin(); it != ordinary.end(); ++it)
            if (!properties.contains(it.key()) || properties.value(it.key()).toObject().value("x-vlc-secret").toBool()
                || !valueValid(it.value(), properties.value(it.key()).toObject()))
                return fail(error, QStringLiteral("The settings migration result does not agree with the new schema."));
        if (hadOrdinarySettings)
            for (const auto& required : schema.value("required").toArray())
                if (!properties.value(required.toString()).toObject().value("x-vlc-secret").toBool()
                    && !ordinary.contains(required.toString()))
                    return fail(error, QStringLiteral("A required settings field is missing after migration."));
    }
    if (!references.isEmpty()) ordinary.insert("_credentials", references);
    *output = ordinary;
    return true;
}
}
