// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "media_identity.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace EpisodeTitleCache {
inline const QByteArray prefix("vlc-episode-v1\n");
struct Entry { QString title; QUrl source; };

inline QByteArray encode(const MediaIdentity::Identity& identity, const QString& title, const QUrl& source)
{
    const QJsonObject record{{"series", MediaIdentity::normalize(identity.title)}, {"year", identity.year}, {"season", identity.season},
        {"episodes", QJsonArray::fromVariantList(MediaIdentity::toMap(identity).value("episodes").toList())},
        {"title", title}, {"source", source.toString()}};
    const QByteArray bytes = prefix + QJsonDocument(record).toJson(QJsonDocument::Compact);
    return bytes.size() <= 16384 ? bytes : QByteArray{};
}
inline Entry decode(const QByteArray& bytes, const QUrl& url, const MediaIdentity::Identity& identity)
{
    Entry entry;
    if (!bytes.startsWith(prefix) || bytes.size() > 16384) return entry;
    const auto record = QJsonDocument::fromJson(bytes.mid(prefix.size())).object();
    if (record.value("series").toString() != MediaIdentity::normalize(identity.title)
        || record.value("year").toInt(-1) != identity.year || record.value("season").toInt(-1) != identity.season
        || record.value("episodes").toArray() != QJsonArray::fromVariantList(MediaIdentity::toMap(identity).value("episodes").toList())) return entry;
    entry.title = MediaIdentity::episodeTitle(url, {{"episodeTitle", record.value("title").toString()}}, false);
    const QUrl source(record.value("source").toString());
    if (source.scheme() == QStringLiteral("https") && !source.host().isEmpty() && source.userInfo().isEmpty()
        && source.toEncoded().size() <= 2048) entry.source = source;
    return entry;
}
}

namespace HostMetadataCache {
inline const QByteArray prefix("vlc-host-metadata-v1\n");
struct Entry { QString title; QUrl source; QVariantMap provenance; };
inline QJsonObject identityRecord(const MediaIdentity::Identity& identity)
{
    return {{"kind", MediaIdentity::kindName(identity.kind)}, {"series", MediaIdentity::normalize(identity.title)},
        {"year", identity.year}, {"season", identity.season},
        {"episodes", QJsonArray::fromVariantList(MediaIdentity::toMap(identity).value("episodes").toList())},
        {"ids", QJsonObject::fromVariantMap(MediaIdentity::toMap(identity).value("ids").toMap())}};
}
inline QUrl source(const QVariantMap& provenance)
{
    const QUrl url(provenance.value("attribution").toMap().value("url").toString(), QUrl::StrictMode);
    return url.scheme() == QStringLiteral("https") && !url.host().isEmpty() && url.userInfo().isEmpty()
        && url.toEncoded().size() <= 2048 ? url : QUrl{};
}
inline QVariantMap boundedProvenance(const QVariantMap& value)
{
    QVariantMap result;
    for (const auto& name : {QStringLiteral("provider"), QStringLiteral("feature"), QStringLiteral("cachePartition"), QStringLiteral("matchEvidence")}) {
        const auto text = value.value(name).toString();
        if (!text.isEmpty() && text.size() <= 512 && !text.contains(QChar::Null)) result.insert(name, text);
    }
    const auto credit = value.value("attribution").toMap(); QVariantMap attribution;
    for (const auto& name : {QStringLiteral("name"), QStringLiteral("license")}) {
        const auto text = credit.value(name).toString(); if (!text.isEmpty() && text.size() <= 128 && !text.contains(QChar::Null)) attribution.insert(name, text);
    }
    const auto link = source(value); if (!link.isEmpty()) attribution.insert("url", link.toString());
    if (!attribution.isEmpty()) result.insert("attribution", attribution);
    return result;
}
inline QByteArray encode(const MediaIdentity::Identity& identity, const QString& title, const QVariantMap& provenance)
{
    const auto source = boundedProvenance(provenance);
    if (source.value("provider").toString().isEmpty() || title.isEmpty()) return {};
    const QJsonObject record{{"identity", identityRecord(identity)}, {"fields", QJsonObject{{"episodeTitle", title}}},
        {"provenance", QJsonObject{{"episodeTitle", QJsonObject::fromVariantMap(source)}}}};
    const QByteArray bytes = prefix + QJsonDocument(record).toJson(QJsonDocument::Compact);
    return bytes.size() <= 16384 ? bytes : QByteArray{};
}
inline Entry decode(const QByteArray& bytes, const QUrl& url, const MediaIdentity::Identity& identity)
{
    Entry result;
    if (!bytes.startsWith(prefix) || bytes.size() > 16384) return result;
    const auto record = QJsonDocument::fromJson(bytes.mid(prefix.size())).object();
    const auto expected = identityRecord(identity), stored = record.value("identity").toObject();
    for (const auto& field : {QStringLiteral("kind"), QStringLiteral("series"), QStringLiteral("year"), QStringLiteral("season"), QStringLiteral("episodes")})
        if (stored.value(field) != expected.value(field)) return result;
    const auto ids = stored.value("ids").toObject(), requiredIds = expected.value("ids").toObject();
    for (auto it = requiredIds.begin(); it != requiredIds.end(); ++it)
        if (ids.contains(it.key()) && ids.value(it.key()) != it.value()) return result;
    result.provenance = boundedProvenance(record.value("provenance").toObject().value("episodeTitle").toObject().toVariantMap());
    if (result.provenance.value("provider").toString().isEmpty()) return {};
    result.title = MediaIdentity::episodeTitle(url, {{"episodeTitle", record.value("fields").toObject().value("episodeTitle").toString()}}, false);
    result.source = source(result.provenance); return result;
}
}
