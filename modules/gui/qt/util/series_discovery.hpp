// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "media_identity.hpp"
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <atomic>
#include <functional>

// Local series discovery is independent of playback and never parses media or
// contacts a metadata provider. Call scan() on a worker, not the interface thread.
namespace SeriesDiscovery {
using Identity = MediaIdentity::Identity;
struct Episode {
    Identity identity;
    QList<QUrl> urls;
};
struct Result {
    QUrl root;
    QList<Episode> episodes;
    QStringList directories;
    bool limited = false;
    QString error;
};
struct Limits {
    int depth = 4;
    int directories = 128;
    int entries = 16384;
    int entriesPerDirectory = 2048;
    int milliseconds = 2000;
};

inline int seasonFolder(const QString& name)
{
    if (name.compare("specials", Qt::CaseInsensitive) == 0) return 0;
    static const QRegularExpression pattern(QStringLiteral("^(?:season|series|s)[ ._-]*(\\d{1,3})$"), QRegularExpression::CaseInsensitiveOption);
    const auto match = pattern.match(name);
    return match.hasMatch() ? match.captured(1).toInt() : -1;
}

inline bool sameSeries(const Identity& a, const Identity& b)
{
    if (a.year >= 0 && b.year >= 0 && a.year != b.year) return false;
    bool idMatch = false;
    for (const auto& key : {QStringLiteral("seriesImdb"), QStringLiteral("seriesTmdb")}) {
        if (!a.ids.contains(key) || !b.ids.contains(key)) continue;
        if (a.ids.value(key) != b.ids.value(key)) return false;
        idMatch = true;
    }
    return idMatch || (!a.title.isEmpty() && MediaIdentity::normalize(a.title) == MediaIdentity::normalize(b.title));
}

inline Identity identify(const QUrl& url, const QVariantMap& metadata = {})
{
    auto result = MediaIdentity::parse(url.fileName());
    auto parent = url.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash);
    // The episode wrapper and season folder can both supply missing fields.
    // Do not replace a named work with an unrelated ancestor's name.
    for (int level = 0; level < 4 && !parent.fileName().isEmpty(); ++level) {
        const auto name = parent.fileName();
        const auto season = seasonFolder(name);
        if (season >= 0) {
            if (result.season < 0) result.season = season;
            static const QRegularExpression numberedFile(QStringLiteral("^\\d{1,4}(?:[ ._-]+.*)?$"));
            if (numberedFile.match(QFileInfo(url.fileName()).completeBaseName()).hasMatch()) result.title.clear();
        } else {
            const auto folder = MediaIdentity::parse(name);
            if (result.title.isEmpty()) {
                result.title = folder.title;
                result.year = folder.year;
                result.ids = folder.ids;
            }
            if (sameSeries(result, folder)) {
                if (result.year < 0) result.year = folder.year;
                for (auto it = folder.ids.cbegin(); it != folder.ids.cend(); ++it)
                    if (!result.ids.contains(it.key())) result.ids.insert(it.key(), it.value());
                if (result.season < 0) result.season = folder.season;
                if (result.episodes.isEmpty()) result.episodes = folder.episodes;
                if (result.absoluteEpisode.isEmpty()) result.absoluteEpisode = folder.absoluteEpisode;
                if (!result.airDate.isValid()) result.airDate = folder.airDate;
            } else break;
            // A series directory is the final source of inherited identity.
            if (folder.kind != MediaIdentity::Kind::Episode) break;
        }
        parent = parent.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash);
    }
    if (result.season >= 0 && result.episodes.isEmpty()) {
        const auto stem = QFileInfo(url.fileName()).completeBaseName();
        static const QRegularExpression leadingNumber(QStringLiteral("^(\\d{1,4})(?:[ ._-]+.*)?$"));
        const auto number = leadingNumber.match(stem);
        if (number.hasMatch()) result.episodes.append(number.captured(1).toInt());
    }
    if (result.season >= 0 || !result.episodes.isEmpty() || !result.absoluteEpisode.isEmpty() || result.airDate.isValid())
        result.kind = MediaIdentity::Kind::Episode;
    MediaIdentity::applyMetadata(result, metadata);
    return result;
}

inline QUrl seriesRoot(const QUrl& url, const Identity& identity)
{
    auto folder = url.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash);
    for (int level = 0; level < 4 && !folder.fileName().isEmpty(); ++level) {
        const auto name = folder.fileName();
        const auto parsed = MediaIdentity::parse(name);
        if (seasonFolder(name) >= 0 || (parsed.kind == MediaIdentity::Kind::Episode && sameSeries(identity, parsed))) {
            const auto parent = folder.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash);
            // Never turn an episode at the drive/filesystem root into a drive scan.
            if (parent.fileName().isEmpty() || QDir(parent.toLocalFile()).isRoot()) break;
            folder = parent;
        } else break;
    }
    return folder;
}

inline bool videoFile(const QFileInfo& file)
{
    return QStringList{"mkv", "mp4", "m4v", "avi", "mov", "wmv", "webm", "mpg", "mpeg", "ts", "m2ts", "mts", "ogv", "divx"}.contains(file.suffix().toLower());
}

inline QString episodeKey(const Identity& identity)
{
    QStringList numbers;
    for (int episode : identity.episodes) numbers.append(QString::number(episode));
    if (!numbers.isEmpty()) return QString::number(identity.season) + "/" + numbers.join(',');
    if (!identity.absoluteEpisode.isEmpty()) return "absolute/" + identity.absoluteEpisode;
    if (identity.airDate.isValid()) return "date/" + identity.airDate.toString(Qt::ISODate);
    return {};
}

inline bool less(const Episode& a, const Episode& b)
{
    if (a.identity.season != b.identity.season) return a.identity.season < b.identity.season;
    if (a.identity.episodes != b.identity.episodes)
        return std::lexicographical_compare(a.identity.episodes.cbegin(), a.identity.episodes.cend(), b.identity.episodes.cbegin(), b.identity.episodes.cend());
    if (a.identity.airDate != b.identity.airDate) return a.identity.airDate < b.identity.airDate;
    if (a.identity.absoluteEpisode != b.identity.absoluteEpisode) return a.identity.absoluteEpisode.toDouble() < b.identity.absoluteEpisode.toDouble();
    return a.urls.first().toString() < b.urls.first().toString();
}

inline Result scan(const QUrl& current, const Identity& target, const QUrl& chosenRoot = {},
                   Limits limits = {}, const std::atomic_bool* cancelled = nullptr,
                   const std::function<void(const Result&)>& progress = {})
{
    Result result;
    if (!current.isLocalFile() || target.kind != MediaIdentity::Kind::Episode || target.title.isEmpty() || episodeKey(target).isEmpty()) return result;
    result.root = chosenRoot.isEmpty() ? seriesRoot(current, target) : chosenRoot;
    if (!result.root.isLocalFile() || !QFileInfo(result.root.toLocalFile()).isDir()) {
        result.error = QStringLiteral("unreadable"); return result;
    }
    const QDir root(result.root.toLocalFile());
    if (root.isRoot()) {
        result.episodes.append({target, {current}});
        result.limited = true; result.error = QStringLiteral("root");
        return result;
    }
    QHash<QString, int> episodes;
    const auto add = [&](const QUrl& url, const Identity& identity) {
        const auto key = episodeKey(identity);
        if (key.isEmpty()) return;
        if (!episodes.contains(key)) {
            episodes.insert(key, result.episodes.size());
            result.episodes.append({identity, {url}});
        } else if (!result.episodes[episodes.value(key)].urls.contains(url)) result.episodes[episodes.value(key)].urls.append(url);
    };
    // Keep the playing file even if its directory is unreadable or truncated.
    add(current, target);
    QStringList pending;
    auto parent = QFileInfo(current.toLocalFile()).absolutePath();
    // Start at the current episode/season, then visit the remaining series.
    while (parent == root.absolutePath() || parent.startsWith(root.absolutePath() + '/')) {
        pending.append(parent);
        if (parent == root.absolutePath()) break;
        parent = QFileInfo(parent).absolutePath();
    }
    if (!pending.contains(root.absolutePath())) pending.append(root.absolutePath());
    QSet<QString> visited;
    QElapsedTimer timer; timer.start();
    int examined = 0;
    const auto stopped = [&] { return cancelled && cancelled->load(std::memory_order_relaxed); };
    while (!pending.isEmpty() && !stopped()) {
        if (visited.size() >= limits.directories || examined >= limits.entries || timer.elapsed() >= limits.milliseconds) {
            result.limited = true; break;
        }
        const auto path = pending.takeFirst();
        const QFileInfo directory(path);
        const auto canonical = directory.canonicalFilePath();
        if (canonical.isEmpty() || visited.contains(canonical)) continue;
        visited.insert(canonical);
        result.directories.append(path);
        const auto relative = root.relativeFilePath(path);
        const int depth = relative == "." ? 0 : relative.count('/') + 1;
        QDirIterator iterator(path, QDir::Files | QDir::Dirs | QDir::Readable | QDir::NoDotAndDotDot | QDir::NoSymLinks);
        QFileInfoList entries;
        while (iterator.hasNext() && !stopped()) {
            if (entries.size() >= limits.entriesPerDirectory || examined >= limits.entries || timer.elapsed() >= limits.milliseconds) {
                result.limited = true; break;
            }
            iterator.next(); entries.append(iterator.fileInfo()); ++examined;
        }
        std::sort(entries.begin(), entries.end(), [](const QFileInfo& a, const QFileInfo& b) { return a.fileName() < b.fileName(); });
        for (const auto& file : entries) {
            if (stopped()) return {};
            if (file.isDir()) {
                // Explicitly unrelated series folders need no traversal.
                const auto folder = MediaIdentity::parse(file.fileName());
                if (folder.kind == MediaIdentity::Kind::Episode && !folder.title.isEmpty() && !sameSeries(target, folder)) continue;
                if (depth < limits.depth) pending.append(file.absoluteFilePath());
                else result.limited = true;
            } else if (videoFile(file)) {
                const auto url = QUrl::fromLocalFile(file.absoluteFilePath());
                const auto identity = url == current ? target : identify(url);
                if (identity.kind == MediaIdentity::Kind::Episode && sameSeries(target, identity)) add(url, identity);
            }
        }
        if (progress && !stopped()) progress(result);
    }
    if (stopped()) return {};
    std::sort(result.episodes.begin(), result.episodes.end(), less);
    return result;
}
}
