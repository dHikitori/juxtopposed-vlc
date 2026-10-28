// SPDX-License-Identifier: GPL-2.0-or-later
// Shared strict SDK identity normalization. Host C++ only.
#pragma once
#include "util/media_identity.hpp"
#include <QJsonDocument>
#include <QDate>
#include <QRegularExpression>
#include <QSet>
#include <QVariantMap>
#include <optional>
#include <cmath>
namespace vlc::addons {
inline std::optional<QVariantMap> normalizeMetadataIdentity(const QVariantMap& input, bool requireEntity = false)
{
    if (QJsonDocument::fromVariant(input).toJson(QJsonDocument::Compact).size() > 8192) return {};
    // Use one SDK-to-host boundary for identify and ordinary enrichment. Private
    // caller properties and local paths do not enter the provider context.
    const QSet<QString> allowed{QStringLiteral("kind"), QStringLiteral("type"), QStringLiteral("title"),
        QStringLiteral("titles"), QStringLiteral("aliases"), QStringLiteral("year"), QStringLiteral("season"),
        QStringLiteral("episodes"), QStringLiteral("episode"), QStringLiteral("episodeNumbering"),
        QStringLiteral("absoluteEpisode"), QStringLiteral("airDate"), QStringLiteral("ids"), QStringLiteral("externalIds"),
        QStringLiteral("release"), QStringLiteral("artist"), QStringLiteral("albumArtist"), QStringLiteral("album"),
        QStringLiteral("series"), QStringLiteral("guid"), QStringLiteral("feedUrl"), QStringLiteral("track"), QStringLiteral("disc")};
    QVariantMap value;
    for (auto it = input.cbegin(); it != input.cend(); ++it) if (allowed.contains(it.key())) value.insert(it.key(), it.value());
    const auto kindText = input.value(QStringLiteral("kind"), input.value(QStringLiteral("type"))).toString();
    const auto kind = MediaIdentity::kindFromName(kindText);
    if ((kind == MediaIdentity::Kind::Unknown && !kindText.isEmpty() && kindText.compare(QStringLiteral("unknown"), Qt::CaseInsensitive))
        || (requireEntity && kind == MediaIdentity::Kind::Unknown)) return {};
    if (input.contains(QStringLiteral("type"))) {
        const auto type = input.value(QStringLiteral("type")).toString();
        const auto parsedType = MediaIdentity::kindFromName(type);
        if (parsedType != kind || (parsedType == MediaIdentity::Kind::Unknown && !type.isEmpty()
            && type.compare(QStringLiteral("unknown"), Qt::CaseInsensitive))) return {};
    }
    value.insert(QStringLiteral("type"), MediaIdentity::kindName(kind));
    value.insert(QStringLiteral("kind"), MediaIdentity::kindName(kind));
    for (const auto& key : {QStringLiteral("title"), QStringLiteral("artist"), QStringLiteral("albumArtist"), QStringLiteral("album"),
             QStringLiteral("series"), QStringLiteral("guid"), QStringLiteral("feedUrl")})
        if (input.value(key).toString().size() > 1024) return {};
    for (const auto& key : {QStringLiteral("titles"), QStringLiteral("aliases")}) {
        if (input.contains(key) && !input.value(key).canConvert<QVariantList>() && !input.value(key).canConvert<QStringList>()) return {};
        const auto entries = input.value(key).toList();
        if (entries.size() > 32) return {};
        for (const auto& entry : entries) if (entry.toString().isEmpty() || entry.toString().size() > 1024) return {};
    }
    if (value.value(QStringLiteral("title")).toString().isEmpty()) {
        const auto titles = value.value(QStringLiteral("titles")).toList();
        if (!titles.isEmpty()) value.insert(QStringLiteral("title"), titles.first());
    }
    if ((input.contains(QStringLiteral("ids")) && !input.value(QStringLiteral("ids")).canConvert<QVariantMap>())
        || (input.contains(QStringLiteral("externalIds")) && !input.value(QStringLiteral("externalIds")).canConvert<QVariantList>())) return {};
    auto ids = input.value(QStringLiteral("ids")).toMap();
    const auto external = input.value(QStringLiteral("externalIds")).toList();
    if (ids.size() > 32 || external.size() > 32) return {};
    static const QRegularExpression atom(QStringLiteral("^[a-z][a-z0-9._-]{0,31}$"));
    for (const auto& entry : external) {
        const auto id = entry.toMap();
        const auto space = id.value(QStringLiteral("namespace")).toString();
        const auto scope = id.value(QStringLiteral("scope")).toString();
        const auto text = id.value(QStringLiteral("value")).toString();
        if (!atom.match(space).hasMatch() || !atom.match(scope).hasMatch() || text.isEmpty() || text.size() > 256) return {};
        QString key = QStringLiteral("external:%1:%2").arg(space, scope);
        if (space == QStringLiteral("imdb")) {
            if (scope == QStringLiteral("movie")) key = QStringLiteral("imdb");
            else if (scope == QStringLiteral("series")) key = QStringLiteral("seriesImdb");
            else if (scope == QStringLiteral("episode")) key = QStringLiteral("episodeImdb");
        } else if (space == QStringLiteral("tmdb")) {
            if (scope == QStringLiteral("movie")) key = QStringLiteral("tmdbMovie");
            else if (scope == QStringLiteral("series")) key = QStringLiteral("seriesTmdb");
            else if (scope == QStringLiteral("episode")) key = QStringLiteral("tmdbEpisode");
        } else if (space == QStringLiteral("tvdb")) {
            if (scope == QStringLiteral("series")) key = QStringLiteral("seriesTvdb");
            else if (scope == QStringLiteral("episode")) key = QStringLiteral("tvdbEpisode");
        } else if (space == QStringLiteral("tvmaze")) {
            if (scope == QStringLiteral("series")) key = QStringLiteral("tvmazeShow");
            else if (scope == QStringLiteral("episode")) key = QStringLiteral("tvmazeEpisode");
        } else if (space == QStringLiteral("musicbrainz")) {
            if (scope == QStringLiteral("track")) key = QStringLiteral("musicBrainzTrackId");
            else if (scope == QStringLiteral("album")) key = QStringLiteral("musicBrainzAlbumId");
            else if (scope == QStringLiteral("artist")) key = QStringLiteral("musicBrainzArtistId");
        }
        if (ids.contains(key) && ids.value(key).toString() != text) return {};
        ids.insert(key, text);
    }
    if (ids.size() > 32) return {};
    for (auto it = ids.cbegin(); it != ids.cend(); ++it)
        if (it.key().isEmpty() || it.key().size() > 128 || it.value().toString().isEmpty() || it.value().toString().size() > 256) return {};
    value.insert(QStringLiteral("ids"), ids);
    const auto integer = [](const QVariant& data, int minimum, int maximum, int& number) {
        bool valid = false;
        const double real = data.toDouble(&valid);
        if (!valid || data.metaType().id() == QMetaType::Bool || !std::isfinite(real)
            || real < minimum || real > maximum || std::trunc(real) != real) return false;
        number = int(real); return true;
    };
    for (const auto& key : {QStringLiteral("year"), QStringLiteral("season"), QStringLiteral("track"), QStringLiteral("disc")}) {
        if (!input.contains(key)) continue;
        int number;
        const int maximum = key == QStringLiteral("year") ? 2099 : key == QStringLiteral("season") ? 999 : 9999;
        if (!integer(input.value(key), -1, maximum, number) || (key == QStringLiteral("year") && number != -1 && number < 1880)) return {};
        value.insert(key, number);
    }
    if (input.contains(QStringLiteral("episodes"))) {
        if (!input.value(QStringLiteral("episodes")).canConvert<QVariantList>()) return {};
        const auto episodes = input.value(QStringLiteral("episodes")).toList();
        if (episodes.size() > 128) return {};
        QVariantList normalized;
        for (const auto& entry : episodes) {
            int number;
            if (!integer(entry, 0, 9999, number)) return {};
            normalized.append(number);
        }
        value.insert(QStringLiteral("episodes"), normalized);
    }
    static const QRegularExpression absolute(QStringLiteral("^[0-9]{1,6}(?:\\.[0-9]{1,3})?$"));
    const auto absoluteText = input.value(QStringLiteral("absoluteEpisode")).toString();
    if (!absoluteText.isEmpty() && !absolute.match(absoluteText).hasMatch()) return {};
    const auto dateText = input.value(QStringLiteral("airDate")).toString();
    if (!dateText.isEmpty() && QDate::fromString(dateText, Qt::ISODate).toString(Qt::ISODate) != dateText) return {};
    const auto put = [&](const QString& key, const QVariant& data) {
        const auto prior = value.value(key);
        // Host-normalized empty fields are absence, not contrary evidence.
        const bool absent = !value.contains(key)
            || (key == QStringLiteral("season") && prior.toInt() == -1)
            || (key == QStringLiteral("episodes") && prior.toList().isEmpty())
            || ((key == QStringLiteral("absoluteEpisode") || key == QStringLiteral("airDate")) && prior.toString().isEmpty());
        if (!absent && prior != data) return false;
        value.insert(key, data); return true;
    };
    QVariantMap numbering;
    if (input.contains(QStringLiteral("episodeNumbering"))) {
        if (!input.value(QStringLiteral("episodeNumbering")).canConvert<QVariantMap>()) return {};
        numbering = input.value(QStringLiteral("episodeNumbering")).toMap();
        if (numbering.isEmpty()) return {};
    }
    if (input.contains(QStringLiteral("episode"))) {
        const auto legacy = input.value(QStringLiteral("episode"));
        if (legacy.canConvert<QVariantMap>()) {
            const auto alias = legacy.toMap();
            if (alias.isEmpty() || (!numbering.isEmpty() && alias != numbering)) return {};
            numbering = alias;
        } else {
            int number;
            if (!numbering.isEmpty() || !integer(legacy, 0, 9999, number)
                || !put(QStringLiteral("episodes"), QVariantList{number})) return {};
        }
    }
    if (!numbering.isEmpty()) {
        if (kind != MediaIdentity::Kind::Episode && kind != MediaIdentity::Kind::PodcastEpisode) return {};
        const auto scheme = numbering.value(QStringLiteral("scheme")).toString();
        const auto keys = QSet<QString>(numbering.keyBegin(), numbering.keyEnd());
        if (scheme == QStringLiteral("seasonal")) {
            if (keys != QSet<QString>{QStringLiteral("scheme"), QStringLiteral("season"), QStringLiteral("episodes")}) return {};
            int season;
            const auto episodes = numbering.value(QStringLiteral("episodes")).toList();
            if (!integer(numbering.value(QStringLiteral("season")), 0, 999, season)
                || !numbering.value(QStringLiteral("episodes")).canConvert<QVariantList>()
                || episodes.isEmpty() || episodes.size() > 128) return {};
            QVariantList normalized;
            for (const auto& entry : episodes) {
                int number;
                if (!integer(entry, 0, 9999, number)) return {};
                normalized.append(number);
            }
            if (!put(QStringLiteral("season"), season) || !put(QStringLiteral("episodes"), normalized)) return {};
        } else if (scheme == QStringLiteral("absolute")) {
            const auto number = numbering.value(QStringLiteral("number")).toString();
            if (keys != QSet<QString>{QStringLiteral("scheme"), QStringLiteral("number")}
                || !absolute.match(number).hasMatch() || !put(QStringLiteral("absoluteEpisode"), number)) return {};
        } else if (scheme == QStringLiteral("air-date")) {
            const auto text = numbering.value(QStringLiteral("date")).toString();
            const auto date = QDate::fromString(text, Qt::ISODate);
            if (keys != QSet<QString>{QStringLiteral("scheme"), QStringLiteral("date")}
                || !date.isValid() || date.toString(Qt::ISODate) != text || !put(QStringLiteral("airDate"), text)) return {};
        } else return {};
    }
    const auto parsed = MediaIdentity::fromMap(value);
    if (parsed.kind != kind || (requireEntity && parsed.title.isEmpty()) || parsed.aliases.size() > 32 || parsed.episodes.size() > 128) return {};
    auto result = MediaIdentity::toMap(parsed);
    result.insert(QStringLiteral("kind"), result.value(QStringLiteral("type")));
    result.remove(QStringLiteral("provenance")); result.remove(QStringLiteral("conflicts"));
    return result;
}

}
