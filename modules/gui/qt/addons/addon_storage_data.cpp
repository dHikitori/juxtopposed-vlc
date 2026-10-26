// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_storage_data.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace vlc::addons {
namespace {
bool fail(QString* error, const QString& message) { if (error) *error = message; return false; }
bool keys(const QJsonObject& object, const QSet<QString>& permitted)
{ for (auto it = object.begin(); it != object.end(); ++it) if (!permitted.contains(it.key())) return false; return true; }
bool version(const QJsonValue& value)
{ const auto n = value.toDouble(); return value.isDouble() && n >= 1 && n <= 1000000 && std::floor(n) == n; }
bool digest(const QString& value)
{ static const QRegularExpression pattern(QStringLiteral("\\A[0-9a-f]{64}\\z")); return pattern.match(value).hasMatch(); }
bool bounded(const QJsonValue& value, int& nodes, int depth, int limit = PrivateStorageLimits::MaximumValueNodes)
{
    if (++nodes > limit || depth > PrivateStorageLimits::MaximumValueDepth || value.isUndefined()) return false;
    if (value.isDouble()) return std::isfinite(value.toDouble());
    if (value.isString()) return value.toString().size() <= PrivateStorageLimits::MaximumValueBytes;
    if (value.isArray()) {
        const auto array = value.toArray(); if (array.size() > 1024) return false;
        for (const auto& child : array) if (!bounded(child, nodes, depth + 1, limit)) return false;
    } else if (value.isObject()) {
        const auto object = value.toObject(); if (object.size() > 256) return false;
        for (auto it = object.begin(); it != object.end(); ++it)
            if (it.key().size() > 256 || !bounded(it.value(), nodes, depth + 1, limit)) return false;
    }
    return true;
}
bool migrationProgram(const QByteArray& bytes, QJsonArray* output)
{
    if (bytes.size() > 65536) return false;
    QJsonParseError error; const auto document = QJsonDocument::fromJson(bytes, &error); const auto object = document.object();
    if (error.error != QJsonParseError::NoError || !document.isObject() || object.size() != 2
        || object.value("formatVersion") != 1 || !object.value("operations").isArray()
        || object.value("operations").toArray().size() > 128) return false;
    *output = object.value("operations").toArray();
    for (const auto& value : *output) {
        if (!value.isObject()) return false;
        const auto operation = value.toObject(); const auto kind = operation.value("op").toString();
        if (kind == QStringLiteral("rename")) {
            if (operation.size() != 3 || !keys(operation, {"op", "from", "to"})
                || !privateStorageKey(operation.value("from").toString()) || !privateStorageKey(operation.value("to").toString())
                || operation.value("from") == operation.value("to")) return false;
        } else if (kind == QStringLiteral("remove")) {
            if (operation.size() != 2 || !keys(operation, {"op", "key"}) || !privateStorageKey(operation.value("key").toString())) return false;
        } else if (kind == QStringLiteral("default")) {
            if (operation.size() != 3 || !keys(operation, {"op", "key", "value"})
                || !privateStorageKey(operation.value("key").toString()) || !privateStorageValue(operation.value("value"))) return false;
        } else return false;
    }
    return true;
}
}
int privateStorageVersion(const Manifest& manifest)
{ return manifest.document.value("storage").toObject().value("version").toInt(1); }
QJsonObject emptyPrivateStorage(int value)
{ return {{"formatVersion", 1}, {"schemaVersion", value}, {"namespaces", QJsonObject{}}}; }
bool privateStorageKey(const QString& key)
{ static const QRegularExpression pattern(QStringLiteral("\\A[A-Za-z][A-Za-z0-9_.-]{0,127}\\z")); return pattern.match(key).hasMatch(); }
bool privateStorageValue(const QJsonValue& value)
{
    int nodes = 0;
    return bounded(value, nodes, 0)
        && QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact).size() - 2 <= PrivateStorageLimits::MaximumValueBytes;
}
bool validatePrivateStorage(const QJsonObject& document, qint64 quota, qint64* used, QString* error)
{
    if (document.size() != 3 || document.value("formatVersion") != 1 || !version(document.value("schemaVersion"))
        || !document.value("namespaces").isObject()) return fail(error, QStringLiteral("The private storage snapshot is not correct."));
    const auto accounts = document.value("namespaces").toObject(); int count = 0, nodes = 0;
    if (accounts.size() > PrivateStorageLimits::MaximumAccounts) return fail(error, QStringLiteral("The private storage account limit was reached."));
    for (auto account = accounts.begin(); account != accounts.end(); ++account) {
        if (!digest(account.key()) || !account.value().isObject()) return fail(error, QStringLiteral("The private storage namespace is not correct."));
        const auto entries = account.value().toObject();
        for (auto it = entries.begin(); it != entries.end(); ++it)
            if (++count > PrivateStorageLimits::MaximumKeys || !privateStorageKey(it.key())
                || !privateStorageValue(it.value()) || !bounded(it.value(), nodes, 0, 65536))
                return fail(error, QStringLiteral("The private storage key or value limit was reached."));
    }
    const auto bytes = accounts.isEmpty() ? 0 : QJsonDocument(accounts).toJson(QJsonDocument::Compact).size();
    if (used) *used = bytes;
    if (quota < 0 || bytes > std::min(quota, PrivateStorageLimits::MaximumBytes))
        return fail(error, QStringLiteral("The private storage byte limit was reached."));
    return true;
}
bool validatePrivateStorageDeclaration(const Manifest& manifest, const QMap<QString, QByteArray>& contents, bool available, QString* error)
{
    if (!manifest.document.contains("storage")) return true;
    const auto value = manifest.document.value("storage"); const auto declaration = value.toObject();
    if (!value.isObject() || !keys(declaration, {"version", "migrations"}) || !version(declaration.value("version"))
        || !manifest.permissionDeclared(QStringLiteral("storage.private")))
        return fail(error, QStringLiteral("The private storage declaration is not correct."));
    if (!declaration.contains("migrations")) return true;
    if (!declaration.value("migrations").isArray() || declaration.value("migrations").toArray().size() > 32)
        return fail(error, QStringLiteral("The private storage migration list is not correct."));
    QSet<int> starts;
    for (const auto& item : declaration.value("migrations").toArray()) {
        if (!item.isObject()) return fail(error, QStringLiteral("The private storage migration step is not correct."));
        const auto step = item.toObject(); const auto path = step.value("resource").toString();
        const auto from = step.value("from").toInt(); const auto to = step.value("to").toInt();
        if (step.size() != 3 || !keys(step, {"from", "to", "resource"}) || !version(step.value("from")) || !version(step.value("to"))
            || to != from + 1 || to > privateStorageVersion(manifest) || starts.contains(from)
            || !portablePackagePath(path) || !manifest.files.contains(path))
            return fail(error, QStringLiteral("The private storage migration step is not correct."));
        starts.insert(from); QJsonArray operations;
        if (available && (!contents.contains(path) || !migrationProgram(contents.value(path), &operations)))
            return fail(error, QStringLiteral("The private storage migration program is not correct."));
    }
    return true;
}
bool migratePrivateStorage(const Manifest& next, const QMap<QString, QByteArray>& contents, int previousVersion,
                           const QJsonObject& previous, qint64 quota, QJsonObject* output, QString* error)
{
    if (!validatePrivateStorageDeclaration(next, contents, true, error) || !validatePrivateStorage(previous, PrivateStorageLimits::MaximumBytes, nullptr, error)
        || previous.value("schemaVersion").toInt() != previousVersion) return false;
    const auto target = privateStorageVersion(next);
    if (previousVersion > target) return fail(error, QStringLiteral("The private storage schema cannot change to an earlier version."));
    auto accounts = previous.value("namespaces").toObject();
    while (previousVersion < target) {
        QJsonObject step;
        for (const auto& entry : next.document.value("storage").toObject().value("migrations").toArray())
            if (entry.toObject().value("from").toInt() == previousVersion) step = entry.toObject();
        QJsonArray operations;
        if (step.isEmpty() || !migrationProgram(contents.value(step.value("resource").toString()), &operations))
            return fail(error, QStringLiteral("A private storage migration step is missing."));
        for (auto account = accounts.begin(); account != accounts.end(); ++account) {
            auto entries = account.value().toObject();
            for (const auto& item : operations) {
                const auto operation = item.toObject(); const auto kind = operation.value("op").toString();
                const auto key = operation.value("key").toString();
                if (kind == QStringLiteral("remove")) entries.remove(key);
                else if (kind == QStringLiteral("default")) { if (!entries.contains(key)) entries.insert(key, operation.value("value")); }
                else {
                    const auto from = operation.value("from").toString(), to = operation.value("to").toString();
                    if (entries.contains(from)) {
                        if (entries.contains(to)) return fail(error, QStringLiteral("A private storage migration key already exists."));
                        entries.insert(to, entries.take(from));
                    }
                }
            }
            account.value() = entries;
        }
        ++previousVersion;
    }
    auto result = emptyPrivateStorage(target); result.insert(QStringLiteral("namespaces"), accounts);
    if (!validatePrivateStorage(result, quota, nullptr, error)) return false;
    *output = result; return true;
}
bool readPrivateStorage(const QString& directory, const QString& hash, QJsonObject* output, QString* error)
{
    if (!digest(hash) || QFileInfo(directory).isSymLink()) return fail(error, QStringLiteral("The private storage digest or directory is not correct."));
    QFile file(QDir(directory).filePath(hash + QStringLiteral(".json")));
    if (QFileInfo(file).isSymLink() || !file.open(QIODevice::ReadOnly) || file.size() > PrivateStorageLimits::MaximumBytes + 256)
        return fail(error, QStringLiteral("The app cannot read the private storage snapshot."));
    const auto bytes = file.readAll(); QJsonParseError parse; const auto document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()
        || QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex() != hash.toLatin1()
        || !validatePrivateStorage(document.object(), PrivateStorageLimits::MaximumBytes, nullptr, error))
        return fail(error, QStringLiteral("The private storage snapshot failed its integrity check."));
    *output = document.object(); return true;
}
bool writePrivateStorage(const QString& directory, const QJsonObject& document, QString* output, QString* error)
{
    if (!validatePrivateStorage(document, PrivateStorageLimits::MaximumBytes, nullptr, error)) return false;
    const auto bytes = QJsonDocument(document).toJson(QJsonDocument::Compact);
    const auto hash = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    if (QFileInfo(directory).isSymLink() || !QDir().mkpath(directory)) return fail(error, QStringLiteral("The app cannot create private storage."));
    if (!QFile::setPermissions(directory, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner))
        return fail(error, QStringLiteral("The app cannot protect the private storage directory."));
    const QString path = QDir(directory).filePath(hash + QStringLiteral(".json"));
    if (QFileInfo::exists(path)) { QJsonObject checked; if (!readPrivateStorage(directory, hash, &checked, error)) return false; }
    else {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)
            || file.write(bytes) != bytes.size() || !file.commit()) return fail(error, QStringLiteral("The app cannot save the private storage snapshot."));
    }
    *output = hash; return true;
}
}
