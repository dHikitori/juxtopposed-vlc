// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "media_identity.hpp"
#include <QDirIterator>
#include <QFileInfo>
#include <functional>

// Filesystem conventions and ownership, independent of playback, UI and providers.
namespace ArtworkDiscovery {
inline const QRegularExpression& driveRoot()
{
    static const QRegularExpression pattern(QStringLiteral("^/[A-Za-z]:/$"));
    return pattern;
}
inline QUrl normalizeUrl(const QUrl& url)
{
    auto result = url.adjusted(QUrl::NormalizePathSegments);
    if (result.isLocalFile() && result.path() != "/"
        && !driveRoot().match(result.path()).hasMatch())
        result = result.adjusted(QUrl::StripTrailingSlash);
    return result;
}
enum class Role { Primary, Thumb, Backdrop, Banner, Logo, Disc, Art };
struct Candidate {
    QUrl url;
    Role role = Role::Primary;
    int score = 0;
    QString reason;
};
inline bool imageFile(const QFileInfo& file)
{
    return QStringList{"jpg", "jpeg", "png", "webp", "tbn", "bmp", "gif", "avif"}.contains(file.suffix().toLower());
}
inline bool mediaFile(const QFileInfo& file)
{
    return QStringList{"mkv", "mp4", "m4v", "avi", "mov", "wmv", "webm", "mpg", "mpeg", "ts", "m2ts",
        "mp3", "flac", "m4a", "m4b", "aac", "ogg", "oga", "opus", "wav", "wma", "aiff", "ape", "alac"}.contains(file.suffix().toLower());
}
struct ImageName { QString owner; Role role = Role::Primary; bool generic = false; int preference = 0; };
inline ImageName imageName(const QFileInfo& file)
{
    ImageName result;
    auto stem = file.completeBaseName();
    const QList<QPair<QString, Role>> names{{"poster",Role::Primary}, {"folder",Role::Primary},
        {"cover",Role::Primary}, {"default",Role::Primary}, {"movie",Role::Primary}, {"show",Role::Primary},
        {"album",Role::Primary}, {"jacket",Role::Primary}, {"artist",Role::Primary},
        {"thumb",Role::Thumb}, {"landscape",Role::Thumb}, {"backdrop",Role::Backdrop},
        {"fanart",Role::Backdrop}, {"background",Role::Backdrop}, {"banner",Role::Banner},
        {"clearlogo",Role::Logo}, {"logo",Role::Logo}, {"disc",Role::Disc}, {"cdart",Role::Disc}, {"clearart",Role::Art}};
    for (int i = 0; i < names.size(); ++i) {
        if (stem.compare(names[i].first, Qt::CaseInsensitive) == 0) {
            result.generic = true; result.role = names[i].second; result.preference = names.size() - i;
            return result;
        }
        const QRegularExpression suffix(QStringLiteral("[ ._-]+%1$").arg(names[i].first), QRegularExpression::CaseInsensitiveOption);
        const auto match = suffix.match(stem);
        if (match.hasMatch()) {
            result.owner = stem.left(match.capturedStart()); result.role = names[i].second;
            result.preference = names.size() - i; return result;
        }
    }
    result.owner = stem;
    return result;
}
// Bound enumeration even for huge or unreadable directories. No symlink traversal.
inline QFileInfoList entries(const QString& path, int limit = 512)
{
    QFileInfoList result;
    QDirIterator iterator(path, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable | QDir::NoSymLinks);
    while (iterator.hasNext() && result.size() < limit) { iterator.next(); result.append(iterator.fileInfo()); }
    std::sort(result.begin(), result.end(), [](const QFileInfo& a, const QFileInfo& b) {
        const int compared = QString::compare(a.fileName(), b.fileName(), Qt::CaseInsensitive);
        return compared ? compared < 0 : a.fileName() < b.fileName();
    });
    return result;
}
inline QList<Candidate> local(const QUrl& url, bool directory, const MediaIdentity::Identity& identity = {},
                              Role wanted = Role::Primary, const std::function<bool()>& cancelled = {})
{
    QList<Candidate> result;
    if (!url.isLocalFile()) return result;
    const QFileInfo target(url.toLocalFile());
    const auto files = entries(directory ? target.absoluteFilePath() : target.absolutePath());
    const bool noIdentity = identity.kind == MediaIdentity::Kind::Unknown && identity.title.isEmpty()
        && identity.ids.isEmpty() && identity.album.isEmpty() && identity.artist.isEmpty() && identity.series.isEmpty();
    const auto targetIdentity = noIdentity ? MediaIdentity::identify(url) : identity;
    const bool audio = MediaIdentity::isAudio(targetIdentity.kind);
    bool sharedCover = directory;
    if (!directory && audio) {
        sharedCover = targetIdentity.album.isEmpty()
            || MediaIdentity::normalize(targetIdentity.album) == MediaIdentity::normalize(QFileInfo(target.absolutePath()).fileName());
        for (const auto& file : files)
            if (file.isFile() && mediaFile(file) && !MediaIdentity::isAudio(MediaIdentity::parse(file.fileName()).kind)) sharedCover = false;
    }
    if (!directory && !audio) {
        sharedCover = true;
        for (const auto& file : files) {
            if (!mediaFile(file)) continue;
            if (MediaIdentity::compact(file.completeBaseName()) == MediaIdentity::compact(target.completeBaseName())) continue;
            if (!MediaIdentity::compare(targetIdentity, MediaIdentity::identify(QUrl::fromLocalFile(file.absoluteFilePath()))).strong) {
                sharedCover = false; break;
            }
        }
    }
    if (files.size() == 512 && !directory) sharedCover = false;
    for (const auto& file : files) {
        if (cancelled && cancelled()) return {};
        if (!file.isFile() || !imageFile(file)) continue;
        auto name = imageName(file);
        // An exact media basename establishes ownership even if it happens to
        // be a reserved convention such as "movie" or "cover".
        if (!directory && MediaIdentity::compact(file.completeBaseName()) == MediaIdentity::compact(target.completeBaseName())) {
            name.owner = file.completeBaseName(); name.generic = false; name.role = Role::Primary;
        }
        // Landscape thumbs may be used as primary art; backgrounds/logos never are.
        if (name.role != wanted && !(wanted == Role::Primary && name.role == Role::Thumb)) continue;
        int score = 0;
        QString reason;
        if (name.generic && sharedCover) { score = 100; reason = QStringLiteral("Folder artwork"); }
        else if (!name.owner.isEmpty()) {
            const auto stem = directory ? target.fileName() : target.completeBaseName();
            if (MediaIdentity::compact(name.owner) == MediaIdentity::compact(stem)) {
                score = 300; reason = QStringLiteral("Matching filename");
            } else {
                auto candidate = MediaIdentity::parse(name.owner, targetIdentity.kind);
                const auto comparison = MediaIdentity::compare(targetIdentity, candidate);
                if (!comparison.conflict && comparison.strong) { score = 200; reason = QStringLiteral("Matching media identity"); }
            }
        }
        if (score) result.append({QUrl::fromLocalFile(file.absoluteFilePath()), name.role,
            score * 100 + (name.role == wanted ? 50 : 0) + name.preference, reason});
    }
    if (!directory && audio && wanted == Role::Primary) {
        const QFileInfo parent(target.absolutePath());
        static const QRegularExpression disc(QStringLiteral("^(?:disc|disk|cd)[ ._-]*\\d+$"), QRegularExpression::CaseInsensitiveOption);
        if (disc.match(parent.fileName()).hasMatch()) {
            const auto albumDirectory = QFileInfo(parent.absolutePath());
            if (targetIdentity.album.isEmpty() || MediaIdentity::normalize(targetIdentity.album) == MediaIdentity::normalize(albumDirectory.fileName())) {
                auto inherited = local(QUrl::fromLocalFile(parent.absolutePath()), true, {}, wanted, cancelled);
                for (auto& candidate : inherited) { candidate.score = 9000; candidate.reason = QStringLiteral("Album artwork above disc folder"); result.append(candidate); }
            }
        }
    }
    std::stable_sort(result.begin(), result.end(), [](const Candidate& a, const Candidate& b) {
        return a.score > b.score;
    });
    // Different equally plausible owners require a choice. Extension alternatives of
    // the same owner remain deterministic and can recover from an unreadable image.
    if (!result.isEmpty() && result.first().score / 100 == 200) {
        const auto best = MediaIdentity::compact(imageName(QFileInfo(result.first().url.toLocalFile())).owner);
        for (const auto& candidate : result) {
            if (candidate.score / 100 != 200) break;
            if (MediaIdentity::compact(imageName(QFileInfo(candidate.url.toLocalFile())).owner) != best) return {};
        }
    }
    return result;
}
// Only confirmed work identities collapse different encodes in a folder preview.
inline QString workKey(const QUrl& url, const QVariantMap& metadata = {})
{
    const auto identity = MediaIdentity::identify(url, metadata);
    if (identity.kind == MediaIdentity::Kind::Movie) {
        if (identity.ids.contains("imdb")) return "imdb:" + identity.ids.value("imdb");
        if (identity.ids.contains("tmdbMovie")) return "tmdb:" + identity.ids.value("tmdbMovie");
        if (!identity.title.isEmpty() && identity.year > 0)
            return "movie:" + MediaIdentity::compact(identity.title) + ':' + QString::number(identity.year);
    }
    if (identity.kind == MediaIdentity::Kind::Episode && !identity.title.isEmpty()
        && identity.season >= 0 && !identity.episodes.isEmpty()) {
        QStringList episodes;
        for (int episode : identity.episodes) episodes.append(QString::number(episode));
        return "episode:" + MediaIdentity::compact(identity.title) + ':' + QString::number(identity.year)
            + ':' + QString::number(identity.season) + ':' + episodes.join(',');
    }
    return {};
}
struct Children { QList<QUrl> urls; bool limited = false; };
inline Children children(const QUrl& folder, const std::function<bool()>& cancelled = {},
                         const std::function<bool(const QUrl&)>& hasArtwork = {})
{
    Children result;
    if (!folder.isLocalFile()) return result;
    struct Directory { QString path; int depth; };
    QList<Directory> pending{{folder.toLocalFile(), 0}};
    QSet<QString> visited;
    int examined = 0;
    while (!pending.isEmpty() && result.urls.size() < 32 && examined < 2048 && visited.size() < 32) {
        if (cancelled && cancelled()) return {};
        const auto next = pending.takeFirst();
        const auto canonical = QFileInfo(next.path).canonicalFilePath();
        if (canonical.isEmpty() || visited.contains(canonical)) continue;
        visited.insert(canonical);
        if (next.depth > 0) {
            const auto child = QUrl::fromLocalFile(next.path);
            if (hasArtwork ? hasArtwork(child) : !local(child, true).isEmpty()) {
                result.urls.append(child); continue;
            }
        }
        const auto files = entries(next.path);
        examined += files.size();
        if (files.size() == 512) result.limited = true;
        for (const auto& file : files) {
            if (result.urls.size() == 32) { result.limited = true; break; }
            if (file.isDir()) {
                if (next.depth < 2) pending.append({file.absoluteFilePath(), next.depth + 1});
                else result.limited = true;
            } else if (mediaFile(file)) result.urls.append(QUrl::fromLocalFile(file.absoluteFilePath()));
        }
    }
    result.limited |= !pending.isEmpty();
    return result;
}
} // namespace ArtworkDiscovery
