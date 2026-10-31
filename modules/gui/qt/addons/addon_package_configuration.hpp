// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_package_store.hpp"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <functional>
#include <cmath>

namespace vlc::addons::packageConfiguration {
inline bool secretField(const QJsonObject& field)
{ return field.value("x-hakone-secret").toBool() || field.value("writeOnly").toBool() || field.value("format").toString() == QLatin1String("password"); }
inline bool valueValid(const QJsonValue& value, const QJsonObject& field)
{
    const auto type = field.value("type").toString();
    if (field.contains("enum") && !field.value("enum").toArray().contains(value)) return false;
    if (type == QLatin1String("boolean")) return value.isBool();
    if (type == QLatin1String("number") || type == QLatin1String("integer")) {
        if (!value.isDouble()) return false;
        const auto n = value.toDouble();
        if (type == QLatin1String("integer") && (n < -0x1p63 || n >= 0x1p63 || std::trunc(n) != n)) return false;
        return n >= field.value("minimum").toDouble(-1e12) && n <= field.value("maximum").toDouble(1e12);
    }
    if (type == QLatin1String("array")) {
        if (!value.isArray() || value.toArray().size() > qMin(64, field.value("maxItems").toInt(64))
            || value.toArray().size() < field.value("minItems").toInt(0)) return false;
        QSet<QByteArray> seen;
        for (const auto& entry : value.toArray()) {
            if (!valueValid(entry, field.value("items").toObject())) return false;
            const auto key = QJsonDocument(QJsonArray{entry}).toJson(QJsonDocument::Compact);
            if (field.value("uniqueItems").toBool() && seen.contains(key)) return false;
            seen.insert(key);
        }
        return true;
    }
    if (type != QLatin1String("string") || !value.isString()) return false;
    const auto text = value.toString();
    if (text.size() < field.value("minLength").toInt(0) || text.size() > qMin(4096, field.value("maxLength").toInt(4096))) return false;
    const auto pattern = field.value("pattern").toString();
    if (!pattern.isEmpty()) {
        // The first SDK supports this bounded language-code grammar. Never
        // compile arbitrary package-supplied PCRE into a UI-thread validator.
        if (pattern != QLatin1String("^[A-Za-z]{2,3}(-[A-Za-z0-9]{2,8})*$")) return false;
        static const QRegularExpression language(QStringLiteral("^[A-Za-z]{2,3}(-[A-Za-z0-9]{2,8})*\\z"));
        if (text.size() > 64 || !language.match(text).hasMatch()) return false;
    }
    return true;
}
inline QString packageAccountReference(const InstalledPackage& package, const QJsonObject& saved,
                                const QJsonObject& document,
                                const std::function<bool(const QString&)>& present)
{
    const auto storedReferences = saved.value(QStringLiteral("_credentials")).toObject();
    const auto properties = document.value(QStringLiteral("properties")).toObject();
    QJsonObject references, account;
    bool connected = true, protectedAccount = false;
    const auto approved = package.grants.value(QStringLiteral("credentials.use")).toObject().value(QStringLiteral("bindings")).toArray();
    for (const auto& binding : package.manifest.permissionScope(QStringLiteral("credentials.use")).value(QStringLiteral("bindings")).toArray()) {
        if (!approved.contains(binding)) continue;
        protectedAccount = true;
        const auto name = binding.toObject().value(QStringLiteral("field")).toString();
        const auto reference = storedReferences.value(name).toString();
        references.insert(name, reference);
        connected &= present(reference);
    }
    for (const auto& name : package.manifest.accountFields) {
        const auto field = properties.value(name).toObject();
        if (!field.isEmpty() && !secretField(field)) {
            const auto value = saved.contains(name) ? saved.value(name) : field.value(QStringLiteral("default"));
            account.insert(name, value.isUndefined() ? QJsonValue(QJsonValue::Null) : value);
        }
    }
    const QJsonObject identity{{"package", package.manifest.id}, {"source", package.identity.source},
        {"references", references}, {"account", account}};
    const QByteArray canonical = QByteArray("vlc-addon-account-v1\n") + QJsonDocument(identity).toJson(QJsonDocument::Compact);
    const QString digest = QString::fromLatin1(QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex());
    const QString prefix = !connected && protectedAccount ? QStringLiteral("disconnected:")
        : !protectedAccount && account.isEmpty() ? QStringLiteral("public:") : QStringLiteral("account:");
    return prefix + digest;
}

inline bool configured(const QJsonObject& saved, const QJsonObject& document,
                       const std::function<bool(const QString&)>& present)
{
    if (document.value(QStringLiteral("type")).toString() != QLatin1String("object")) return false;
    const auto properties = document.value(QStringLiteral("properties")).toObject();
    const auto references = saved.value(QStringLiteral("_credentials")).toObject();
    for (const auto& required : document.value(QStringLiteral("required")).toArray()) {
        const auto name = required.toString();
        const auto field = properties.value(name).toObject();
        if (secretField(field)) {
            if (!present(references.value(name).toString())) return false;
        } else if (!valueValid(saved.contains(name) ? saved.value(name)
            : field.value(QStringLiteral("default")), field)) return false;
    }
    return true;
}

inline QJsonObject projectMetadataContext(const Manifest& manifest, const QJsonObject& grants,
                                          QJsonObject context)
{
    const auto identity = context.value(QStringLiteral("identity")).toObject();
    QJsonObject permitted;
    const auto declared = manifest.permissionScope(QStringLiteral("metadata.context")).value(QStringLiteral("fields")).toArray();
    const auto approved = grants.value(QStringLiteral("metadata.context")).toObject().value(QStringLiteral("fields")).toArray();
    for (const auto& field : declared)
        if (approved.contains(field) && identity.contains(field.toString()))
            permitted.insert(field.toString(), identity.value(field.toString()));
    context.insert(QStringLiteral("identity"), permitted);
    for (const auto& key : {QStringLiteral("url"), QStringLiteral("targets"), QStringLiteral("existingFields"),
            QStringLiteral("protectedFields"), QStringLiteral("fieldRevisions")}) context.remove(key);
    return context;
}
}
