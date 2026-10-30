// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "addon_accepted_identities.hpp"
#include "util/media_identity.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <limits>

namespace vlc::addons {
namespace {
constexpr qint64 MaximumBytes = 4 * 1024 * 1024;
// Only the facts that name the accepted work are kept. Episode numbering
// stays local, so one choice serves every episode of the series.
QVariantMap kept(const QVariantMap& identity)
{
    QVariantMap result;
    for (const auto* field : {"kind", "title", "year", "ids", "externalIds"})
        if (identity.contains(QLatin1String(field))) result.insert(QLatin1String(field), identity.value(QLatin1String(field)));
    return result;
}
}

AddonAcceptedIdentities::AddonAcceptedIdentities(QString profileDirectory)
    : m_path(QDir(profileDirectory).filePath(QStringLiteral("accepted-identities.json")))
{
}

QString AddonAcceptedIdentities::key(const QVariantMap& local)
{
    const auto kind = local.value(QStringLiteral("type"), local.value(QStringLiteral("kind"))).toString();
    const auto title = MediaIdentity::normalize(local.value(QStringLiteral("title")).toString());
    if (kind != QLatin1String("episode") || title.isEmpty() || title.size() > 512) return {};
    const int year = local.value(QStringLiteral("year")).toInt();
    return QStringLiteral("series:") + title + (year > 0 ? QStringLiteral(":") + QString::number(year) : QString());
}

bool AddonAcceptedIdentities::load(Snapshot& snapshot, QString* error) const
{
    snapshot = {};
    const QFileInfo info(m_path);
    if (!info.exists()) return true;
    QFile file(m_path);
    if (info.size() > MaximumBytes || !file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("The accepted identities cannot be read.");
        return false;
    }
    const auto document = QJsonDocument::fromJson(file.read(MaximumBytes + 1));
    if (!document.isObject()) {
        if (error) *error = QStringLiteral("The accepted identities file is not valid.");
        return false;
    }
    const auto root = document.object().toVariantMap();
    snapshot.records = root.value(QStringLiteral("records")).toMap();
    snapshot.revision = root.value(QStringLiteral("revision")).toULongLong();
    return true;
}

bool AddonAcceptedIdentities::store(const Snapshot& snapshot, QString* error) const
{
    QSaveFile file(m_path);
    const auto bytes = QJsonDocument(QJsonObject::fromVariantMap({{QStringLiteral("revision"), snapshot.revision},
        {QStringLiteral("records"), snapshot.records}})).toJson(QJsonDocument::Compact);
    if (bytes.size() > MaximumBytes || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error) *error = QStringLiteral("The accepted identity cannot be saved.");
        return false;
    }
    return true;
}

std::optional<AddonAcceptedIdentities::Record> AddonAcceptedIdentities::find(const QString& key) const
{
    Snapshot snapshot;
    if (key.isEmpty() || !load(snapshot, nullptr)) return std::nullopt;
    const auto value = snapshot.records.value(key).toMap();
    if (value.isEmpty()) return std::nullopt;
    return Record{value.value(QStringLiteral("identity")).toMap(), value.value(QStringLiteral("provider")).toString(),
        value.value(QStringLiteral("revision")).toULongLong()};
}

quint64 AddonAcceptedIdentities::accept(const QString& key, const QVariantMap& identity, const QString& provider,
                                        QString* error)
{
    if (key.isEmpty() || key.size() > 1024 || provider.isEmpty() || identity.isEmpty()) {
        if (error) *error = QStringLiteral("This identity cannot be accepted.");
        return 0;
    }
    if (!QDir().mkpath(QFileInfo(m_path).path())) {
        if (error) *error = QStringLiteral("The accepted identity cannot be saved.");
        return 0;
    }
    QLockFile lock(m_path + QStringLiteral(".lock"));
    if (!lock.tryLock(5000)) {
        if (error) *error = QStringLiteral("Another window is saving an identity. Try again.");
        return 0;
    }
    Snapshot snapshot;
    if (!load(snapshot, error) || snapshot.revision == std::numeric_limits<quint64>::max()) return 0;
    const auto revision = snapshot.revision + 1;
    snapshot.records.insert(key, QVariantMap{{QStringLiteral("identity"), kept(identity)},
        {QStringLiteral("provider"), provider}, {QStringLiteral("revision"), revision}});
    // Keep the newest records within the limit.
    while (snapshot.records.size() > MaximumRecords) {
        auto oldest = snapshot.records.begin();
        for (auto it = snapshot.records.begin(); it != snapshot.records.end(); ++it)
            if (it.value().toMap().value(QStringLiteral("revision")).toULongLong()
                < oldest.value().toMap().value(QStringLiteral("revision")).toULongLong()) oldest = it;
        snapshot.records.erase(oldest);
    }
    snapshot.revision = revision;
    if (!store(snapshot, error)) return 0;
    return revision;
}

bool AddonAcceptedIdentities::forget(const QString& key, QString* error)
{
    QLockFile lock(m_path + QStringLiteral(".lock"));
    if (!lock.tryLock(5000)) {
        if (error) *error = QStringLiteral("Another window is saving an identity. Try again.");
        return false;
    }
    Snapshot snapshot;
    if (!load(snapshot, error)) return false;
    if (!snapshot.records.remove(key)) return true;
    // Forgetting is a change too: later requests must not reuse a cache entry
    // made under the removed identity.
    ++snapshot.revision;
    return store(snapshot, error);
}

}
