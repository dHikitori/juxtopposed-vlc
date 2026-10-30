// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Provider-independent media evidence. QtCore only: no player, UI or network state.
#include <QDate>
#include <QDateTime>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <algorithm>
#include <numeric>

namespace MediaIdentity {
// RSS permits named zones that Qt's RFC 2822 parser does not accept.
inline QDateTime publicationDate(QString value)
{
    static const QMap<QString, QString> zones{{"UT", "+0000"}, {"UTC", "+0000"}, {"GMT", "+0000"}, {"Z", "+0000"},
        {"EST", "-0500"}, {"EDT", "-0400"}, {"CST", "-0600"}, {"CDT", "-0500"},
        {"MST", "-0700"}, {"MDT", "-0600"}, {"PST", "-0800"}, {"PDT", "-0700"}};
    value = value.simplified();
    const auto space = value.lastIndexOf(QLatin1Char(' '));
    const auto zone = zones.constFind(value.mid(space + 1).toUpper());
    if (space >= 0 && zone != zones.cend()) value = value.left(space + 1) + zone.value();
    auto date = QDateTime::fromString(value, Qt::RFC2822Date);
    return date.isValid() ? date : QDateTime::fromString(value, Qt::ISODate);
}

enum class Kind { Unknown, Movie, Episode, MusicTrack, Album, Artist, Podcast, PodcastEpisode, Audiobook, Series };

inline QString kindName(Kind kind)
{
    switch (kind) {
    case Kind::Movie: return QStringLiteral("movie");
    case Kind::Episode: return QStringLiteral("episode");
    case Kind::Series: return QStringLiteral("series");
    case Kind::MusicTrack: return QStringLiteral("musicTrack");
    case Kind::Album: return QStringLiteral("album");
    case Kind::Artist: return QStringLiteral("artist");
    case Kind::Podcast: return QStringLiteral("podcast");
    case Kind::PodcastEpisode: return QStringLiteral("podcastEpisode");
    case Kind::Audiobook: return QStringLiteral("audiobook");
    default: return QStringLiteral("unknown");
    }
}
inline Kind kindFromName(const QString& name)
{
    for (auto kind : {Kind::Movie, Kind::Episode, Kind::MusicTrack, Kind::Album, Kind::Artist,
                      Kind::Podcast, Kind::PodcastEpisode, Kind::Audiobook, Kind::Series})
        if (kindName(kind).compare(name, Qt::CaseInsensitive) == 0) return kind;
    return Kind::Unknown;
}
inline bool isAudio(Kind kind)
{
    return kind == Kind::MusicTrack || kind == Kind::Album || kind == Kind::Artist
        || kind == Kind::Podcast || kind == Kind::PodcastEpisode || kind == Kind::Audiobook;
}

struct Identity {
    Kind kind = Kind::Unknown;
    QString title;
    QStringList aliases;
    int year = -1; // Movie release year or series start year, never episode air year.
    int season = -1;
    QList<int> episodes;
    QString absoluteEpisode; // Kept separate; may include fractional anime episodes.
    QDate airDate;
    QMap<QString, QString> ids; // imdb, seriesImdb, tmdbMovie, tmdbEpisode, seriesTmdb, tvdbEpisode
    QMap<QString, QString> release;
    QMap<QString, QString> provenance;
    QStringList conflicts;
    QString artist;
    QString albumArtist;
    QString album;
    QString series; // Podcast/feed title; separate from the episode title.
    QString guid;
    QString feedUrl;
    int track = -1;
    int disc = -1;
};

inline QString normalize(QString value)
{
    static const QRegularExpression separators(QStringLiteral("[^\\p{L}\\p{N}]+"));
    return value.normalized(QString::NormalizationForm_KC).toCaseFolded()
        .replace(separators, QStringLiteral(" ")).simplified();
}

inline QString compact(QString value) { return normalize(value).remove(QLatin1Char(' ')); }

inline QString displayTitle(QString value)
{
    // Compiled once: these run for every file name in a series folder.
    static const QRegularExpression separators(QStringLiteral("[._]+"));
    static const QRegularExpression edges(QStringLiteral("^[\\s\\-]+|[\\s\\-\\[(]+$"));
    value.replace(separators, QStringLiteral(" "));
    value.remove(edges);
    return value.simplified();
}

inline bool genericTitle(const QString& title)
{
    static const QRegularExpression generic(QStringLiteral(
        "^(?:video|movie|film|episode|feature|sample|untitled|[0-9]+|[0-9a-f]{24,})$"),
        QRegularExpression::CaseInsensitiveOption);
    // A plausible year can itself be a film title (1917, 1984).
    const int number = title.toInt();
    if (number >= 1880 && number <= 2099) return false;
    return title.isEmpty() || generic.match(title).hasMatch();
}

// A bare range must be adjacent to the episode number. In "E10 - 18
// Days Out", the spaced dash introduces a title. Explicit E markers can be spaced.
inline const QRegularExpression& episodePattern()
{
    static const QRegularExpression pattern(QStringLiteral(
        "(?:^|[ ._\\[(-])(?:s(?:eason)?[ ._-]*(\\d{1,3})[ ._-]*e(?:pisode)?[ ._-]*(\\d{1,4})"
        "|(\\d{1,3})x(\\d{1,4}))((?:(?:(?:[ ._]*e|x|[ ._]*-[ ._]*e)[ ._]*|-)\\d{1,4})*)(?![\\da-z])"),
        QRegularExpression::CaseInsensitiveOption);
    return pattern;
}

inline Identity parse(QString name, Kind hint = Kind::Unknown)
{
    Identity result;
    name = name.left(1024).normalized(QString::NormalizationForm_C).trimmed();
    const auto extension = QFileInfo(name).suffix().toLower();
    const bool audioFile = QStringList{"mp3", "flac", "m4a", "m4b", "aac", "ogg", "oga", "opus", "wav", "wma", "aiff", "ape", "alac"}.contains(extension);
    if (hint == Kind::Unknown && audioFile) hint = extension == "m4b" ? Kind::Audiobook : Kind::MusicTrack;
    if (isAudio(hint) && hint != Kind::PodcastEpisode) {
        result.kind = hint;
        if (audioFile) name = QFileInfo(name).completeBaseName();
        // Only an explicit track prefix is removed; numeric titles remain titles.
        static const QRegularExpression numberedName(QStringLiteral("^(\\d{1,3})(?:[ ._-]+)(.+)$"));
        const auto numbered = numberedName.match(name);
        if (hint == Kind::MusicTrack && numbered.hasMatch()) {
            result.track = numbered.captured(1).toInt(); name = numbered.captured(2);
        }
        const auto parts = name.split(QStringLiteral(" - "));
        if (hint == Kind::MusicTrack && parts.size() == 2) {
            result.artist = displayTitle(parts[0]); name = parts[1];
            result.provenance.insert("artist", "filename");
        }
        static const QRegularExpression trackNumber(QStringLiteral("^\\d{1,3}$"));
        if (hint == Kind::MusicTrack && trackNumber.match(name).hasMatch()) result.track = name.toInt();
        result.title = displayTitle(name);
        if (genericTitle(result.title)) result.title.clear();
        if (!result.title.isEmpty()) result.provenance.insert("title", "filename");
        return result;
    }
    // Release names need not have extensions. Do not remove arbitrary final dotted words.
    const auto originalName = name;
    static const QRegularExpression knownExtension(QStringLiteral(
        "\\.(?:mkv|mp4|m4v|avi|mov|wmv|webm|mpg|mpeg|ts|m2ts|srt|ass|ssa|vtt|sub|idx|smi|mp3|m4a|ogg|opus|flac)$"),
        QRegularExpression::CaseInsensitiveOption);
    name.remove(knownExtension);
    const bool hasExtension = name != originalName;
    const auto insensitive = QRegularExpression::CaseInsensitiveOption;
    qsizetype end = name.size();
    const auto cut = [&end](qsizetype position) { if (position >= 0) end = std::min(end, position); };

    const auto ep = episodePattern().match(name);
    if (ep.hasMatch()) {
        result.kind = Kind::Episode;
        result.season = ep.captured(ep.captured(1).isEmpty() ? 3 : 1).toInt();
        const int first = ep.captured(ep.captured(1).isEmpty() ? 4 : 2).toInt();
        result.episodes.append(first);
        static const QRegularExpression extra(QStringLiteral("([ex-])[ ._]*e?[ ._]*(\\d{1,4})"), insensitive);
        auto parts = extra.globalMatch(ep.captured(5));
        while (parts.hasNext()) {
            const auto part = parts.next();
            const int last = part.captured(2).toInt();
            if (part.captured(1) == "-" && last > result.episodes.last() && last - result.episodes.last() <= 100) {
                for (int value = result.episodes.last() + 1; value <= last; ++value) result.episodes.append(value);
            } else if (!result.episodes.contains(last)) result.episodes.append(last);
        }
        std::sort(result.episodes.begin(), result.episodes.end());
        cut(ep.capturedStart());
    } else {
        static const QRegularExpression date(QStringLiteral(
            "(?:^|[ ._-])((?:19|20)\\d{2})[ ._-](0[1-9]|1[0-2])[ ._-]([0-2]\\d|3[01])(?:$|[ ._-])"));
        const auto dated = date.match(name);
        if (dated.hasMatch()) {
            result.airDate = QDate(dated.captured(1).toInt(), dated.captured(2).toInt(), dated.captured(3).toInt());
            if (result.airDate.isValid()) { result.kind = Kind::Episode; cut(dated.capturedStart()); }
        }
        if (!result.airDate.isValid()) {
            static const QRegularExpression absolute(QStringLiteral(
                "(?:[ ._-]+episode[ ._-]+|[ ]+-[ ]+(?:ep[ ._-]*)?)(\\d{1,4}(?:\\.\\d{1,2})?)(?:v\\d+)?(?=$|[ ._\\[(-])"), insensitive);
            const auto numbered = absolute.match(name);
            if (numbered.hasMatch()) {
                result.kind = Kind::Episode;
                result.absoluteEpisode = numbered.captured(1);
                cut(numbered.capturedStart());
                // A leading fansub group is evidence only in an anime-style release.
                static const QRegularExpression group(QStringLiteral("^\\[([^\\]]+)\\][ _.-]*"));
                const auto leading = group.match(name);
                if (leading.hasMatch() && end > leading.capturedLength() && !displayTitle(name.mid(leading.capturedLength(), end - leading.capturedLength())).isEmpty()) {
                    result.release.insert("group", leading.captured(1));
                    name.remove(0, leading.capturedLength());
                    end -= leading.capturedLength();
                }
            } else {
                static const QRegularExpression season(QStringLiteral(
                    "(?:^|[ ._(-])s(?:eason)?[ ._-]*(\\d{1,3})(?=$|[ ._\\[)-])"), insensitive);
                const auto pack = season.match(name);
                if (pack.hasMatch()) { result.kind = Kind::Episode; result.season = pack.captured(1).toInt(); cut(pack.capturedStart()); }
            }
        }
    }

    if (result.kind == Kind::Episode) {
        static const QRegularExpression group(QStringLiteral("^\\[([^\\]]+)\\][ _.-]*"));
        const auto leading = group.match(name);
        if (leading.hasMatch() && end > leading.capturedLength() && !displayTitle(name.mid(leading.capturedLength(), end - leading.capturedLength())).isEmpty()) {
            result.release.insert("group", leading.captured(1));
            name.remove(0, leading.capturedLength()); end -= leading.capturedLength();
        }
    }

    static const auto releasePatterns = [insensitive] {
        QList<QPair<QString, QRegularExpression>> patterns;
        for (const auto& rule : QList<QPair<QString, QString>>{
                 {"resolution", "(?:480|576|720|1080|1440|2160|4320)[pi]|[48]k"},
                 {"source", "web[ .-]?(?:dl|rip)|blu[ .-]?ray|b[dr]rip|hdtv|dvdrip|dvd"},
                 {"videoCodec", "[xh][ .-]?26[45]|hevc|avc|av1|xvid|divx"},
                 {"audioCodec", "e?[ .-]?ac[ .-]?3|dts(?:[ .-]?hd)?|aac|flac|truehd|opus"},
                 {"fps", "(?:23[.]976|24|25|29[.]97|30|50|59[.]94|60)[ .-]?fps"}})
            patterns.append({rule.first, QRegularExpression("(?:^|[ ._\\[(-])(" + rule.second + ")(?=$|[ ._\\[)\\]-])", insensitive)});
        return patterns;
    }();
    static const QRegularExpression fpsSuffix("[ .-]*fps$", insensitive);
    bool technical = false;
    for (const auto& rule : releasePatterns) {
        const auto match = rule.second.match(name);
        if (match.hasMatch()) {
            result.release.insert(rule.first, rule.first == "fps"
                ? match.captured(1).remove(fpsSuffix) : compact(match.captured(1)));
            cut(match.capturedStart()); technical = true;
        }
    }
    static const QRegularExpression year(QStringLiteral("(?:^|[ ._\\[(-])((?:18[89]|19|20)\\d{2})(?=$|[ ._\\[)\\]-])"));
    auto years = year.globalMatch(name.left(end));
    qsizetype yearStart = -1;
    while (years.hasNext()) {
        const auto match = years.next();
        // The number in '1917' or '2001 A Space Odyssey' is part of the title.
        if (displayTitle(name.left(match.capturedStart())).isEmpty()) continue;
        result.year = match.captured(1).toInt(); yearStart = match.capturedStart();
    }
    if (yearStart >= 0) {
        cut(yearStart);
        if (result.kind == Kind::Unknown) result.kind = Kind::Movie;
    }
    if (technical || yearStart >= 0) {
        static const QRegularExpression edition(QStringLiteral(
            "(?:^|[ ._(-])((?:directors?|extended|theatrical|unrated|remastered)(?:[ ._-]+cut)?)(?=$|[ ._)\\]-])"), insensitive);
        const auto match = edition.match(name);
        if (match.hasMatch()) { result.release.insert("edition", normalize(match.captured(1))); cut(match.capturedStart()); }
    }
    if (technical && !result.release.contains("group")) {
        static const QRegularExpression group(QStringLiteral("-([\\p{L}\\p{N}]+)(?:\\[[^\\]]+\\])?$"));
        const auto match = group.match(name);
        if (match.hasMatch()) result.release.insert("group", match.captured(1).toCaseFolded());
    }
    static const QRegularExpression identifier(QStringLiteral(
        "[\\[{](?:imdb(?:id)?[ ._:=/-]*)?(tt\\d{5,12})[\\]}]"), insensitive);
    const auto id = identifier.match(name);
    if (id.hasMatch()) { result.ids.insert(result.kind == Kind::Episode ? "seriesImdb" : "imdb", id.captured(1).toLower()); cut(id.capturedStart()); }
    static const QRegularExpression tmdb(QStringLiteral("[\\[{]tmdb(?:id)?[ ._:=/-]*(\\d+)[\\]}]"), insensitive);
    const auto tmdbId = tmdb.match(name);
    if (tmdbId.hasMatch()) {
        if (result.kind != Kind::Unknown) result.ids.insert(result.kind == Kind::Episode ? "seriesTmdb" : "tmdbMovie", tmdbId.captured(1));
        cut(tmdbId.capturedStart());
    }
    result.title = displayTitle(name.left(std::max(qsizetype(0), end)));
    if (hasExtension && genericTitle(result.title)) result.title.clear();
    if (!result.title.isEmpty()) result.provenance.insert("title", "filename");
    if (result.year >= 0) result.provenance.insert("year", "filename");
    if (result.kind == Kind::Episode) result.provenance.insert("episode", "filename");
    if (hint == Kind::PodcastEpisode) {
        result.kind = hint;
        result.series = result.title;
        result.year = -1; // An episode's publication year does not identify its feed.
    }
    return result;
}

inline void applyMetadata(Identity& identity, const QVariantMap& metadata, const QString& source = QStringLiteral("metadata"))
{
    const auto text = [&](const char* key) { return metadata.value(QLatin1String(key)).toString().trimmed(); };
    const auto requestedKind = kindFromName(text("type"));
    if (isAudio(requestedKind)) identity.kind = requestedKind;
    const bool audio = isAudio(identity.kind);
    auto title = text("showName");
    if (audio) title = text("title");
    // In episode metadata, Title commonly names the episode rather than the series.
    if (title.isEmpty() && (source == "user" || (identity.kind != Kind::Episode
        && text("type") != "episode" && !metadata.contains("episode")))) title = text("title");
    if (!title.isEmpty() && (source == "user" || !genericTitle(title))) {
        if (!identity.title.isEmpty() && compact(identity.title) != compact(title)) {
            identity.conflicts.append(QStringLiteral("title"));
        }
        identity.title = title.left(512); identity.provenance.insert("title", source);
    }
    const auto type = text("type");
    if (type == "movie") identity.kind = Kind::Movie;
    if (type == "series") identity.kind = Kind::Series;
    if (!audio && (type == "episode" || !text("showName").isEmpty())) identity.kind = Kind::Episode;
    for (const auto& field : {qMakePair("artist", &identity.artist), qMakePair("albumArtist", &identity.albumArtist),
             qMakePair("album", &identity.album), qMakePair("series", &identity.series),
             qMakePair("guid", &identity.guid), qMakePair("feedUrl", &identity.feedUrl)}) {
        const auto value = text(field.first);
        if (value.isEmpty()) continue;
        if (!field.second->isEmpty() && normalize(*field.second) != normalize(value)) identity.conflicts.append(QLatin1String(field.first));
        *field.second = value.left(1024); identity.provenance.insert(QLatin1String(field.first), source);
    }
    if (audio && identity.series.isEmpty() && !text("showName").isEmpty()) {
        identity.series = text("showName"); identity.provenance.insert("series", source);
    }
    for (const auto& field : {qMakePair("track", &identity.track), qMakePair("disc", &identity.disc)}) {
        bool ok = false;
        const int number = text(field.first).section('/', 0, 0).toInt(&ok);
        if (ok && number > 0 && number <= 9999) {
            if (*field.second >= 0 && *field.second != number) identity.conflicts.append(QLatin1String(field.first));
            *field.second = number; identity.provenance.insert(QLatin1String(field.first), source);
        }
    }
    for (const auto& key : {QStringLiteral("year"), QStringLiteral("season")}) {
        if (!metadata.contains(key)) continue;
        bool ok = false; const int number = metadata.value(key).toInt(&ok);
        if (!ok || number < (key == "year" ? 1880 : 0) || number > (key == "year" ? 2099 : 999)) continue;
        int& field = key == "year" ? identity.year : identity.season;
        if (field >= 0 && field != number) identity.conflicts.append(key);
        field = number; identity.provenance.insert(key, source);
    }
    if (metadata.contains("episodes") || metadata.contains("episode")) {
        QList<int> episodes;
        const auto values = metadata.contains("episodes") ? metadata.value("episodes").toList() : QVariantList{metadata.value("episode")};
        for (const auto& value : values) {
            bool ok = false; const int episode = value.toInt(&ok);
            if (ok && episode >= 0 && episode <= 9999 && !episodes.contains(episode)) episodes.append(episode);
        }
        std::sort(episodes.begin(), episodes.end());
        if (!episodes.isEmpty()) {
            if (!identity.episodes.isEmpty() && identity.episodes != episodes) identity.conflicts.append("episode");
            identity.episodes = episodes; if (!audio) identity.kind = Kind::Episode; identity.provenance.insert("episode", source);
        }
    }
    const QMap<QString, QString> idKeys{{"imdbId", "imdb"}, {"seriesImdbId", "seriesImdb"},
        {"tmdbId", identity.kind == Kind::Episode ? "tmdbEpisode" : "tmdbMovie"},
        {"seriesTmdbId", "seriesTmdb"}, {"tvdbId", "tvdbEpisode"}};
    for (auto it = idKeys.cbegin(); it != idKeys.cend(); ++it) {
        auto value = metadata.value(it.key()).toString();
        if (value.isEmpty()) continue;
        if (it.value().contains("imdb", Qt::CaseInsensitive)) value.remove(QRegularExpression("^tt"));
        static const QRegularExpression digits(QStringLiteral("^[0-9]{1,12}$"));
        if (!digits.match(value).hasMatch() || value.toLongLong() <= 0) continue;
        if (it.value().contains("imdb", Qt::CaseInsensitive)) value.prepend("tt");
        identity.ids.insert(it.value(), value); identity.provenance.insert(it.value(), source);
    }
    for (const auto& key : {QStringLiteral("musicBrainzTrackId"), QStringLiteral("musicBrainzAlbumId"), QStringLiteral("musicBrainzArtistId")}) {
        const auto value = metadata.value(key).toString();
        static const QRegularExpression uuid(QStringLiteral("^[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}$"));
        if (uuid.match(value).hasMatch()) { identity.ids.insert(key, value.toLower()); identity.provenance.insert(key, source); }
    }
    if (metadata.contains("absoluteEpisode")) {
        const auto absolute = metadata.value("absoluteEpisode").toString();
        static const QRegularExpression pattern(QStringLiteral("^\\d{1,4}(?:\\.\\d{1,2})?$"));
        if (absolute.isEmpty() || pattern.match(absolute).hasMatch()) {
            identity.absoluteEpisode = absolute;
            if (!absolute.isEmpty()) { if (!audio) identity.kind = Kind::Episode; identity.provenance.insert("absoluteEpisode", source); }
        }
    }
    if (metadata.contains("airDate")) {
        const auto date = QDate::fromString(metadata.value("airDate").toString(), Qt::ISODate);
        if (date.isValid() || metadata.value("airDate").toString().isEmpty()) {
            identity.airDate = date;
            if (date.isValid()) { if (!audio) identity.kind = Kind::Episode; identity.provenance.insert("airDate", source); }
        }
    }
    const auto release = metadata.value("release").toMap();
    for (auto it = release.cbegin(); it != release.cend(); ++it)
        if (!it.value().toString().isEmpty()) identity.release.insert(it.key(), it.value().toString());
    identity.conflicts.removeDuplicates();
}

inline bool isSourceLocator(const QUrl& url)
{
    return url.scheme().compare(QStringLiteral("hakone-addon-source"), Qt::CaseInsensitive) == 0
        || url.scheme().compare(QStringLiteral("vlc-addon-source"), Qt::CaseInsensitive) == 0;
}

inline Identity identify(const QUrl& url, const QVariantMap& metadata = {})
{
    if (isSourceLocator(url)) {
        // A source locator is private. Only explicit tags may enter identity.
        auto tags = metadata;
        for (const auto& key : {QStringLiteral("title"), QStringLiteral("showName")}) {
            const auto value = tags.value(key).toString().trimmed();
            const auto payload = compact(url.path());
            if (isSourceLocator(QUrl(value)) || (!payload.isEmpty() && compact(value) == payload)) tags.remove(key);
        }
        Identity result; applyMetadata(result, tags); return result;
    }
    auto result = parse(url.fileName(), kindFromName(metadata.value("type").toString()));
    auto parent = url.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash);
    auto folder = parse(parent.fileName());
    if (result.kind == Kind::MusicTrack) {
        auto albumFolder = parent;
        static const QRegularExpression discFolder(QStringLiteral("^(?:disc|disk|cd)[ ._-]*(\\d+)$"), QRegularExpression::CaseInsensitiveOption);
        const auto disc = discFolder.match(parent.fileName());
        if (disc.hasMatch()) {
            result.disc = disc.captured(1).toInt();
            albumFolder = parent.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash);
        }
        result.album = displayTitle(albumFolder.fileName());
        if (!result.album.isEmpty()) result.provenance.insert("album", "folder");
        // Artist folders are not proof of artist identity; tags or explicit names supply that.
    }
    // Only consult nearby folders for incomplete identities. Never scan the filesystem.
    if (result.title.isEmpty() && !isAudio(result.kind)) {
        if (folder.kind == Kind::Episode && folder.title.isEmpty()) {
            if (result.season < 0) result.season = folder.season;
            folder = parse(parent.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash).fileName());
        }
        if (!folder.title.isEmpty()) {
            result.title = folder.title; result.year = folder.year; result.ids = folder.ids;
            if (result.kind == Kind::Unknown) result.kind = folder.kind;
            result.provenance.insert("title", "folder");
            if (result.year >= 0) result.provenance.insert("year", "folder");
        }
    }
    if (result.season >= 0 && result.episodes.isEmpty()) {
        const auto stem = QFileInfo(url.fileName()).completeBaseName();
        static const QRegularExpression number(QStringLiteral("^\\d{1,3}$"));
        if (number.match(stem).hasMatch()) { result.episodes.append(stem.toInt()); result.provenance.insert("episode", "folder"); }
    }
    if (result.kind == Kind::Unknown && result.season >= 0) result.kind = Kind::Episode;
    applyMetadata(result, metadata);
    return result;
}

// Return an actual episode name, with embedded metadata ahead of the filename.
// Filename readers often manufacture "Show 3" or "Show S01E03"; neither is a name.
inline QString episodeTitle(const QUrl& url, const QVariantMap& metadata, bool useFilename = true)
{
    const auto identity = identify(url, metadata);
    const auto usable = [&](const QString& title) {
        if (title.isEmpty()) return false;
        const auto normalized = normalize(title);
        if (normalized == normalize(url.fileName())
            || normalized == normalize(QFileInfo(url.fileName()).completeBaseName())
            || normalized == normalize(identity.title)
            || normalized == normalize(metadata.value("showName").toString())) return false;
        if (parse(title).kind == Kind::Episode) return false;
        static const QRegularExpression generic(QStringLiteral("^(?:episode|ep)[ ._-]*[0-9]+$"),
            QRegularExpression::CaseInsensitiveOption);
        if (generic.match(title).hasMatch()) return false;
        bool numbered = false;
        const int numericTitle = title.toInt(&numbered);
        if (numbered && identity.episodes.contains(numericTitle)) return false;
        const auto series = normalize(identity.title);
        if (!series.isEmpty() && normalized.startsWith(series + QLatin1Char(' '))) {
            auto tail = normalized.mid(series.size() + 1);
            static const QRegularExpression episodeWord(QStringLiteral("^(?:episode|ep) "));
            tail.remove(episodeWord);
            bool ok = false;
            const int number = tail.toInt(&ok);
            if (ok && identity.episodes.contains(number)) return false;
        }
        return true;
    };
    for (const auto* key : {"episodeTitle", "title"}) {
        const auto title = metadata.value(QLatin1String(key)).toString().trimmed();
        if (usable(title)) return title.left(1024);
    }
    if (!useFilename) return {};
    // A title after an explicit season/episode marker is useful even when the
    // file has no tags. Do not use a release suffix (1080p, WEB-DL, codecs, etc.).
    auto name = url.fileName();
    static const QRegularExpression videoExtension(QStringLiteral("\\.(?:mkv|mp4|m4v|avi|mov|wmv|webm|mpg|mpeg|ts|m2ts|mts|ogv|divx)$"),
        QRegularExpression::CaseInsensitiveOption);
    name.remove(videoExtension);
    const auto marker = episodePattern().match(name);
    if (!marker.hasMatch()) return {};
    auto title = name.mid(marker.capturedEnd());
    static const QRegularExpression release(QStringLiteral(
        "(?:^|[ ._\\[(-])(?:(?:480|576|720|1080|1440|2160|4320)[pi]|[48]k|web[ .-]?(?:dl|rip)|"
        "blu[ .-]?ray|b[dr]rip|hdtv|dvdrip|dvd|[xh][ .-]?26[45]|hevc|avc|av1|xvid|divx|"
        "e?[ .-]?ac[ .-]?3|dts(?:[ .-]?hd)?|aac|flac|truehd|opus)(?=$|[ ._\\[)\\]-])"),
        QRegularExpression::CaseInsensitiveOption);
    const auto technical = release.match(title);
    if (technical.hasMatch()) title.truncate(technical.capturedStart());
    title = displayTitle(title);
    return usable(title) ? title.left(1024) : QString();
}

inline QVariantList externalIds(const Identity& identity)
{
    static const QMap<QString, QStringList> known{
        {"imdb", {"imdb", "movie"}}, {"seriesImdb", {"imdb", "series"}}, {"episodeImdb", {"imdb", "episode"}},
        {"tmdbMovie", {"tmdb", "movie"}}, {"seriesTmdb", {"tmdb", "series"}}, {"tmdbEpisode", {"tmdb", "episode"}},
        {"seriesTvdb", {"tvdb", "series"}}, {"tvdbEpisode", {"tvdb", "episode"}},
        {"tvmazeShow", {"tvmaze", "series"}}, {"tvmazeEpisode", {"tvmaze", "episode"}},
        {"musicBrainzTrackId", {"musicbrainz", "track"}}, {"musicBrainzAlbumId", {"musicbrainz", "album"}},
        {"musicBrainzArtistId", {"musicbrainz", "artist"}}};
    static const QRegularExpression atom(QStringLiteral("^[a-z][a-z0-9._-]{0,31}$"));
    QVariantList result;
    for (auto it = identity.ids.cbegin(); it != identity.ids.cend(); ++it) {
        auto scope = known.value(it.key());
        if (scope.isEmpty()) {
            const auto parts = it.key().split(QLatin1Char(':'));
            if (parts.size() != 3 || parts.first() != QStringLiteral("external")
                || !atom.match(parts[1]).hasMatch() || !atom.match(parts[2]).hasMatch()) continue;
            scope = {parts[1], parts[2]};
        }
        if (it.value().isEmpty() || it.value().size() > 256) continue;
        result.append(QVariantMap{{"namespace", scope[0]}, {"scope", scope[1]}, {"value", it.value()}});
    }
    return result;
}

inline QVariantMap toMap(const Identity& identity)
{
    QVariantList episodes;
    for (int value : identity.episodes) episodes.append(value);
    QVariantMap ids, release, provenance;
    for (auto it = identity.ids.cbegin(); it != identity.ids.cend(); ++it) ids.insert(it.key(), it.value());
    for (auto it = identity.release.cbegin(); it != identity.release.cend(); ++it) release.insert(it.key(), it.value());
    for (auto it = identity.provenance.cbegin(); it != identity.provenance.cend(); ++it) provenance.insert(it.key(), it.value());
    return {{"title", identity.title}, {"aliases", identity.aliases}, {"year", identity.year},
        {"type", kindName(identity.kind)},
        {"artist", identity.artist}, {"albumArtist", identity.albumArtist}, {"album", identity.album},
        {"series", identity.series}, {"guid", identity.guid}, {"feedUrl", identity.feedUrl},
        {"track", identity.track}, {"disc", identity.disc},
        {"season", identity.season}, {"episodes", episodes}, {"absoluteEpisode", identity.absoluteEpisode},
        {"airDate", identity.airDate.toString(Qt::ISODate)}, {"ids", ids}, {"externalIds", externalIds(identity)}, {"release", release},
        {"provenance", provenance}, {"conflicts", identity.conflicts}};
}

inline Identity fromMap(const QVariantMap& data)
{
    Identity identity;
    applyMetadata(identity, data, "user");
    identity.aliases = data.value("aliases").toStringList();
    identity.absoluteEpisode = data.value("absoluteEpisode").toString();
    identity.airDate = QDate::fromString(data.value("airDate").toString(), Qt::ISODate);
    const auto ids = data.value("ids").toMap();
    for (auto it = ids.cbegin(); it != ids.cend(); ++it) identity.ids.insert(it.key(), it.value().toString());
    const auto provenance = data.value("provenance").toMap();
    for (auto it = provenance.cbegin(); it != provenance.cend(); ++it) identity.provenance.insert(it.key(), it.value().toString());
    identity.conflicts = data.value("conflicts").toStringList();
    return identity;
}

inline double similarity(const QString& first, const QString& second)
{
    const auto a = compact(first).left(512), b = compact(second).left(512);
    if (a.isEmpty() || b.isEmpty()) return 0;
    if (a == b) return 1;
    // Fuzzy matches for very short titles are too ambiguous to be useful.
    if (std::min(a.size(), b.size()) < 5) return 0;
    QList<int> previous(b.size() + 1), current(b.size() + 1);
    std::iota(previous.begin(), previous.end(), 0);
    for (qsizetype i = 1; i <= a.size(); ++i) {
        current[0] = int(i);
        for (qsizetype j = 1; j <= b.size(); ++j)
            current[j] = std::min({current[j - 1] + 1, previous[j] + 1, previous[j - 1] + (a[i - 1] != b[j - 1])});
        previous.swap(current);
    }
    return 1.0 - double(previous.last()) / std::max(a.size(), b.size());
}

struct Comparison {
    int score = 0; // Evidence weight, not a probability.
    bool conflict = false;
    bool strong = false;
    bool exactTitle = false;
    QStringList agreements;
    QStringList conflicts;
};

inline Comparison compare(const Identity& target, const Identity& candidate)
{
    Comparison result;
    const auto reject = [&](const QString& reason) { result.conflict = true; result.conflicts.append(reason); };
    if (target.kind != Kind::Unknown && candidate.kind != Kind::Unknown && target.kind != candidate.kind) reject("type");
    const auto kind = target.kind != Kind::Unknown ? target.kind : candidate.kind;
    for (auto it = target.ids.cbegin(); it != target.ids.cend(); ++it) {
        if (!candidate.ids.contains(it.key()) || it.value().isEmpty()) continue;
        if (candidate.ids.value(it.key()) != it.value()) reject(it.key());
        else result.agreements.append(it.key());
    }
    const auto agrees = [&](const QStringList& keys) {
        return std::any_of(keys.cbegin(), keys.cend(), [&](const QString& key) { return result.agreements.contains(key); });
    };
    const bool movieId = kind == Kind::Movie && agrees({"imdb", "tmdbMovie"});
    const bool seriesId = (kind == Kind::Series || kind == Kind::Episode)
        && agrees({"seriesImdb", "seriesTmdb", "seriesTvdb", "tvmazeShow"});
    const bool episodeId = kind == Kind::Episode
        && agrees({"episodeImdb", "tmdbEpisode", "tvdbEpisode", "tvmazeEpisode"});
    const bool musicId = (kind == Kind::MusicTrack && agrees({"musicBrainzTrackId"}))
        || (kind == Kind::Album && agrees({"musicBrainzAlbumId"}))
        || (kind == Kind::Artist && agrees({"musicBrainzArtistId"}));
    const bool idMatch = movieId || seriesId || episodeId;
    if (target.year >= 0 && candidate.year >= 0 && target.year != candidate.year) reject("year");
    if (target.season >= 0 && candidate.season >= 0 && target.season != candidate.season) reject("season");
    if (!target.episodes.isEmpty() && !candidate.episodes.isEmpty() && target.episodes != candidate.episodes) reject("episode");
    if (!target.absoluteEpisode.isEmpty() && !candidate.absoluteEpisode.isEmpty() && target.absoluteEpisode != candidate.absoluteEpisode) reject("absoluteEpisode");
    if (target.airDate.isValid() && candidate.airDate.isValid() && target.airDate != candidate.airDate) reject("airDate");
    if (isAudio(target.kind) || isAudio(candidate.kind)) {
        for (const auto& field : {qMakePair("artist", qMakePair(target.artist, candidate.artist)),
                 qMakePair("albumArtist", qMakePair(target.albumArtist, candidate.albumArtist)),
                 qMakePair("album", qMakePair(target.album, candidate.album)),
                 qMakePair("series", qMakePair(target.series, candidate.series))})
            if (!field.second.first.isEmpty() && !field.second.second.isEmpty()
                && normalize(field.second.first) != normalize(field.second.second)) reject(QLatin1String(field.first));
        if (target.track > 0 && candidate.track > 0 && target.track != candidate.track) reject("track");
        if (target.disc > 0 && candidate.disc > 0 && target.disc != candidate.disc) reject("disc");
        if (!target.guid.isEmpty() && !candidate.guid.isEmpty() && target.guid != candidate.guid) reject("guid");
        if (!target.feedUrl.isEmpty() && !candidate.feedUrl.isEmpty() && target.feedUrl != candidate.feedUrl) reject("feedUrl");
        result.exactTitle = !target.title.isEmpty() && normalize(target.title) == normalize(candidate.title);
        const bool artist = (!target.artist.isEmpty() && normalize(target.artist) == normalize(candidate.artist))
            || (!target.albumArtist.isEmpty() && normalize(target.albumArtist) == normalize(candidate.albumArtist));
        const bool series = !target.series.isEmpty() && normalize(target.series) == normalize(candidate.series);
        const bool guid = !target.guid.isEmpty() && target.guid == candidate.guid;
        const bool feed = !target.feedUrl.isEmpty() && target.feedUrl == candidate.feedUrl;
        const bool numbered = !target.episodes.isEmpty() && target.episodes == candidate.episodes
            && ((target.season < 0 && candidate.season < 0)
                || (target.season >= 0 && target.season == candidate.season));
        const bool episode = numbered
            || (!target.absoluteEpisode.isEmpty() && target.absoluteEpisode == candidate.absoluteEpisode)
            || (target.airDate.isValid() && target.airDate == candidate.airDate);
        // A feed or artist describes a parent. An episode also needs its own
        // GUID, number, or publication date.
        const bool podcastEpisode = (guid && (feed || series))
            || ((feed || series) && result.exactTitle && episode);
        result.strong = !result.conflict && (kind == Kind::PodcastEpisode ? podcastEpisode
            : musicId || (result.exactTitle && artist) || (kind == Kind::Podcast && feed && result.exactTitle));
        result.score = result.conflict ? 0 : musicId || (guid && result.strong) ? 120
            : result.strong ? 90 : result.exactTitle ? 70 : 0;
        if (result.exactTitle) result.agreements.append("title");
        if (artist) result.agreements.append("artist");
        if (series) result.agreements.append("series");
        return result;
    }
    double title = similarity(target.title, candidate.title);
    for (const auto& alias : candidate.aliases) title = std::max(title, similarity(target.title, alias));
    for (const auto& alias : target.aliases) title = std::max(title, similarity(alias, candidate.title));
    result.exactTitle = title == 1;
    if (title < .86 && !idMatch) return result;
    if (result.conflict) return result;
    result.score = idMatch ? 120 : result.exactTitle ? 70 : 40;
    result.agreements.append(result.exactTitle ? "title" : "similarTitle");
    const bool year = target.year >= 0 && target.year == candidate.year;
    const bool episode = target.season >= 0 && target.season == candidate.season
        && !target.episodes.isEmpty() && target.episodes == candidate.episodes;
    const bool absolute = !target.absoluteEpisode.isEmpty() && target.absoluteEpisode == candidate.absoluteEpisode;
    const bool date = target.airDate.isValid() && target.airDate == candidate.airDate;
    if (year) { result.score += 20; result.agreements.append("year"); }
    if (episode || absolute || date) { result.score += 30; result.agreements.append("episode"); }
    // A parent series ID needs episode numbering. An exact episode ID does not.
    result.strong = kind == Kind::Episode ? episodeId || ((episode || absolute || date)
                                && (result.exactTitle || seriesId) && (target.year < 0 || year || seriesId))
                    : kind == Kind::Series ? seriesId || (result.exactTitle && year)
                    : movieId || (result.exactTitle && year);
    return result;
}
}
