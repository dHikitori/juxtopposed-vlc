// SPDX-License-Identifier: GPL-2.0-or-later
#include "redesign_folder_model.hpp"
#include <QTimer>
#include <QDir>
#include <QDateTime>
#include <QThreadPool>
#include <algorithm>
#include <cmath>
#include <utility>

namespace {
QUrl normalizeFolderUrl(const QUrl& folder)
{
    if (folder.scheme().isEmpty()) {
        QString path = folder.path();
        if (!folder.host().isEmpty()) path.prepend("//" + folder.host());
        if (QDir::isAbsolutePath(path)) return QUrl::fromLocalFile(path);
    }
#ifdef Q_OS_WIN
    else if (folder.scheme().size() == 1) {
        const QString path = folder.scheme() + ":" + folder.path();
        if (QDir::isAbsolutePath(path)) return QUrl::fromLocalFile(path);
    }
#endif
    return folder;
}

void sortEntries(QFileInfoList& entries, const QString& sort, bool reversed,
                 const QHash<QUrl, bool>& played, const QHash<QUrl, RedesignFolderMetadata>& metadata)
{
    std::sort(entries.begin(), entries.end(), [&sort, reversed, &played, &metadata](const QFileInfo& a, const QFileInfo& b) {
        if (a.isDir() != b.isDir()) return a.isDir();
        const auto title = [&metadata](const QFileInfo& file) {
            const auto value = metadata.value(QUrl::fromLocalFile(file.absoluteFilePath())).title;
            return value.isEmpty() ? file.fileName() : value;
        };
        int primary;
        if (sort == "played") {
            const bool av = !a.isDir() && played.value(QUrl::fromLocalFile(a.absoluteFilePath()), false);
            const bool bv = !b.isDir() && played.value(QUrl::fromLocalFile(b.absoluteFilePath()), false);
            if (av != bv) return reversed ? av : !av;
            // Watch groups keep a predictable title order in both directions.
            primary = QString::compare(title(a), title(b), Qt::CaseInsensitive);
            if (primary) return primary < 0;
        } else {
            if (sort == "main_artist") {
                primary = QString::compare(metadata.value(QUrl::fromLocalFile(a.absoluteFilePath())).artist,
                    metadata.value(QUrl::fromLocalFile(b.absoluteFilePath())).artist, Qt::CaseInsensitive);
                if (!primary) primary = QString::compare(title(a), title(b), Qt::CaseInsensitive);
            } else if (sort == "insertion") {
                const auto av = a.lastModified();
                const auto bv = b.lastModified();
                primary = av < bv ? -1 : av > bv ? 1 : 0;
            } else if (sort == "uri") {
                primary = QUrl::fromLocalFile(a.absoluteFilePath()).toEncoded().compare(
                    QUrl::fromLocalFile(b.absoluteFilePath()).toEncoded());
            } else primary = QString::compare(title(a), title(b), Qt::CaseInsensitive);
            if (primary) return reversed ? primary > 0 : primary < 0;
        }
        return a.fileName() < b.fileName();
    });
}
}

namespace {
// Same paths, types, sizes and modification times: nothing to show again.
bool sameFiles(const QFileInfoList& a, const QFileInfoList& b)
{
    if (a.size() != b.size()) return false;
    const auto key = [](const QFileInfo& file) {
        return file.absoluteFilePath() + QLatin1Char('\0') + (file.isDir() ? QLatin1Char('d') : QLatin1Char('f'))
            + QString::number(file.size()) + QLatin1Char(':') + QString::number(file.lastModified().toMSecsSinceEpoch());
    };
    QStringList left, right;
    for (const auto& file : a) left.append(key(file));
    for (const auto& file : b) right.append(key(file));
    left.sort(); right.sort();
    return left == right;
}
}

RedesignFolderModel::RedesignFolderModel(QObject* parent)
    : QAbstractListModel(parent)
{
    qRegisterMetaType<QFileInfoList>();
    m_results = std::shared_ptr<RedesignFolderScanResult>(new RedesignFolderScanResult,
        [](RedesignFolderScanResult* result) { result->deleteLater(); });
    connect(m_results.get(), &RedesignFolderScanResult::ready, this,
        [this](quint64 serial, const QFileInfoList& entries) {
            if (serial != m_serial) return;
            // A directory change can report the files already listed, for
            // example a late event for files created before the folder opened,
            // or another application's metadata file. Keep the view as it is.
            if (!std::exchange(m_reloadPending, false) && sameFiles(entries, m_allEntries)) return;
            m_allEntries = entries;
            m_forceReset = true;
            if (m_audioMetadataEnabled) requestAudioMetadata();
            if (m_sortCriteria == "played") requestWatchStates();
            scheduleSort();
        });
    connect(m_results.get(), &RedesignFolderScanResult::infoReady, this, &RedesignFolderModel::infoReady);
    connect(m_results.get(), &RedesignFolderScanResult::sorted, this,
        [this](quint64 serial, const QFileInfoList& entries) {
            m_sortRunning = false;
            if (serial == m_sortSerial)
                applyOrder(entries);
            if (m_sortNeeded) QTimer::singleShot(0, this, &RedesignFolderModel::startSort);
        });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
        &RedesignFolderModel::scheduleRescan);
}

void RedesignFolderModel::setFolder(const QUrl& folder)
{
    const auto normalized = normalizeFolderUrl(folder);
    if (m_folder == normalized)
        return;
    m_folder = normalized;
    ++m_folderSerial;
    m_artwork.clear();
    m_metadata.clear();
    m_metadataRequested.clear();
    m_progress.clear();
    m_played.clear();
    m_watchRequested.clear();
    m_artworkRequested.clear();
    const auto watched = m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);
    if (m_folder.isLocalFile() && QFileInfo(m_folder.toLocalFile()).isDir())
        m_watcher.addPath(m_folder.toLocalFile());
    m_allEntries.clear();
    replaceEntries({});
    scheduleRefresh();
    emit folderChanged();
}

void RedesignFolderModel::refresh()
{
    ++m_folderSerial;
    m_artwork.clear();
    m_metadata.clear();
    m_metadataRequested.clear();
    m_progress.clear();
    m_played.clear();
    m_watchRequested.clear();
    m_artworkRequested.clear();
    scheduleRefresh();
}

quint64 RedesignFolderModel::requestInfo(const QUrl& folder)
{
    const auto serial = ++m_infoSerial;
    const auto results = m_results;
    const auto url = normalizeFolderUrl(folder);
    QThreadPool::globalInstance()->start([results, url, serial] {
        QVariantMap info {{"success", false}, {"url", url.toString(QUrl::FullyEncoded)},
            {"location", url.isLocalFile() ? QDir::toNativeSeparators(url.toLocalFile()) : url.toDisplayString()}};
        if (url.isLocalFile()) {
            const QFileInfo directory(url.toLocalFile());
            if (directory.isDir() && directory.isReadable()) {
                quint64 size = 0;
                int files = 0, folders = 0;
                const auto entries = QDir(directory.absoluteFilePath()).entryInfoList(
                    QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::NoSort);
                for (const auto& entry : entries) {
                    if (entry.isDir()) ++folders;
                    else {
                        ++files;
                        size += quint64(std::max(qint64(0), entry.size()));
                    }
                }
                info = {{"success", true}, {"url", url.toString(QUrl::FullyEncoded)},
                    {"name", directory.fileName().isEmpty() ? directory.absoluteFilePath() : directory.fileName()},
                    {"location", QDir::toNativeSeparators(directory.absoluteFilePath())},
                    {"files", files}, {"folders", folders}, {"size", QVariant::fromValue(size)},
                    {"created", directory.birthTime().isValid() ? QVariant(directory.birthTime().toMSecsSinceEpoch()) : QVariant()},
                    {"modified", directory.lastModified().isValid() ? QVariant(directory.lastModified().toMSecsSinceEpoch()) : QVariant()},
                    {"readable", directory.isReadable()}, {"writable", directory.isWritable()},
                    {"symbolicLink", directory.isSymLink()}};
            }
        }
        emit results->infoReady(serial, info);
    });
    return serial;
}

void RedesignFolderModel::setNameFilters(const QStringList& filters)
{
    if (filters == nameFilters())
        return;
    m_nameFilters = filters;
    scheduleRefresh();
    emit nameFiltersChanged();
}

void RedesignFolderModel::setSortReversed(bool reversed)
{
    if (m_sortReversed == reversed)
        return;
    m_sortReversed = reversed;
    scheduleSort();
    emit sortReversedChanged();
}

void RedesignFolderModel::setSortCriteria(const QString& criteria)
{
    if (criteria != "title" && criteria != "insertion" && criteria != "uri" && criteria != "played" && criteria != "main_artist")
        return;
    if (m_sortCriteria == criteria) return;
    m_sortCriteria = criteria;
    if (criteria == "played") requestWatchStates();
    else {
        ++m_watchRequestSerial;
        setWatchStatesPending(false);
        if (!m_audioMetadataEnabled) emit watchStatesCancelled();
        scheduleSort();
    }
    emit sortCriteriaChanged();
}

int RedesignFolderModel::directoryCount() const
{
    return std::count_if(m_entries.cbegin(), m_entries.cend(),
                         [](const QFileInfo& file) { return file.isDir(); });
}

void RedesignFolderModel::setSearchPattern(const QString& pattern)
{
    if (m_searchPattern == pattern)
        return;
    m_searchPattern = pattern;
    if (m_sortCriteria == "played") {
        emit watchStatesCancelled();
        requestWatchStates();
    }
    scheduleSort();
    emit searchPatternChanged();
}

void RedesignFolderModel::setAudioMetadataEnabled(bool enabled)
{
    if (m_audioMetadataEnabled == enabled) return;
    m_audioMetadataEnabled = enabled;
    m_metadata.clear();
    m_metadataRequested.clear();
    ++m_audioMetadataSerial;
    setAudioMetadataPending(false);
    if (enabled) requestAudioMetadata();
    else emit watchStatesCancelled();
    scheduleSort();
    emit audioMetadataEnabledChanged();
}

void RedesignFolderModel::setAudioMetadataPending(bool pending)
{
    if (m_audioMetadataPending == pending) return;
    m_audioMetadataPending = pending;
    emit audioMetadataPendingChanged();
}

void RedesignFolderModel::requestAudioMetadata()
{
    // A directory change rescans the folder without clearing what was already
    // requested (refresh() and setFolder() do). Ask only for new files: an
    // unrelated change must not re-read the metadata of the whole folder.
    QList<QUrl> urls;
    for (const auto& file : std::as_const(m_allEntries)) {
        if (file.isDir()) continue;
        const auto url = QUrl::fromLocalFile(file.absoluteFilePath());
        if (m_metadataRequested.contains(url)) continue;
        urls.append(url);
        m_metadataRequested.insert(url);
    }
    if (urls.isEmpty()) return;
    const auto serial = ++m_audioMetadataSerial;
    setAudioMetadataPending(true);
    emit audioMetadataRequested(urls, serial);
}

void RedesignFolderModel::finishAudioMetadata(quint64 serial)
{
    if (serial != m_audioMetadataSerial || !m_audioMetadataEnabled || !m_audioMetadataPending) return;
    setAudioMetadataPending(false);
    scheduleSort();
}

void RedesignFolderModel::setMetadata(const QUrl& url, const QString& title, const QString& artist)
{
    if (!m_audioMetadataEnabled || (!m_metadataRequested.contains(url) && !m_artworkRequested.contains(url))) return;
    const auto old = m_metadata.value(url);
    if (old.title == title && old.artist == artist) return;
    m_metadata.insert(url, {title, artist});
    const auto row = m_rowsByUrl.value(url, -1);
    if (row >= 0) emit dataChanged(index(row), index(row), {Title, Artist, Qt::DisplayRole});
    if (!m_audioMetadataPending && (m_sortCriteria == "title" || m_sortCriteria == "main_artist" || !m_searchPattern.isEmpty()))
        scheduleSort();
}

int RedesignFolderModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : count();
}

QHash<int, QByteArray> RedesignFolderModel::roleNames() const
{
    return {{FileName, "fileName"}, {FileUrl, "fileUrl"}, {FileIsDir, "fileIsDir"},
            {Thumbnail, "thumbnail"}, {Progress, "progress"}, {Title, "title"}, {Artist, "main_artist"}, {Played, "played"}};
}

QVariant RedesignFolderModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.column() != 0 || index.row() < 0 || index.row() >= count())
        return {};
    const auto& file = m_entries.at(index.row());
    switch (role) {
    case FileName: return file.fileName();
    case FileUrl: return QUrl::fromLocalFile(file.absoluteFilePath());
    case FileIsDir: return file.isDir();
    case Played: return !file.isDir() && m_played.value(QUrl::fromLocalFile(file.absoluteFilePath()), false);
    case Qt::DisplayRole:
    case Title:
    case Artist:
    case Thumbnail:
    case Progress: {
        if (file.isDir()) {
            if (role == Title || role == Qt::DisplayRole) return file.fileName();
            if (role != Thumbnail) return role == Progress ? QVariant(0.0) : QVariant(QString {});
        }
        const auto url = QUrl::fromLocalFile(file.absoluteFilePath());
        if (!m_artworkRequested.contains(url)) {
            m_artworkRequested.insert(url);
            auto self = const_cast<RedesignFolderModel*>(this);
            QTimer::singleShot(0, self, [self, url, serial = m_folderSerial] {
                if (serial == self->m_folderSerial)
                    emit self->artworkRequested(url);
            });
        }
        if (role == Thumbnail) return m_artwork.value(url);
        if (role == Progress) return m_progress.value(url, 0.0);
        const auto info = m_audioMetadataEnabled ? m_metadata.value(url) : RedesignFolderMetadata {};
        if (role == Artist) return info.artist;
        return info.title.isEmpty() ? file.fileName() : info.title;
    }
    default: return {};
    }
}

void RedesignFolderModel::setArtwork(const QUrl& url, const QString& artwork)
{
    // Results are tied to the requested file, never to a row that may have moved.
    if (!m_artworkRequested.contains(url) || m_artwork.value(url) == artwork)
        return;
    m_artwork.insert(url, artwork);
    const auto row = m_rowsByUrl.value(url, -1);
    if (row >= 0)
        emit dataChanged(index(row), index(row), {Thumbnail});
}

void RedesignFolderModel::setProgress(const QUrl& url, qreal progress)
{
    if (!m_artworkRequested.contains(url))
        return;
    const qreal bounded = std::isfinite(progress) ? std::clamp(progress, qreal(0), qreal(1)) : 0;
    if (m_progress.value(url, 0.0) == bounded)
        return;
    m_progress.insert(url, bounded);
    const auto row = m_rowsByUrl.value(url, -1);
    if (row >= 0)
        emit dataChanged(index(row), index(row), {Progress});
}

void RedesignFolderModel::setPlayed(const QUrl& url, bool played)
{
    if (!m_watchRequested.contains(url) && !m_artworkRequested.contains(url)) return;
    const bool previous = m_played.value(url, false);
    m_played.insert(url, played);
    const auto row = m_rowsByUrl.value(url, -1);
    if (previous != played && row >= 0) {
        emit dataChanged(index(row), index(row), {Played});
        if (m_sortCriteria == "played" && !m_watchStatesPending)
            scheduleSort();
    }
}

void RedesignFolderModel::setWatchStatesPending(bool pending)
{
    if (m_watchStatesPending == pending) return;
    m_watchStatesPending = pending;
    emit watchStatesPendingChanged();
}

void RedesignFolderModel::requestWatchStates()
{
    ++m_sortSerial;
    const auto serial = ++m_watchRequestSerial;
    QList<QUrl> urls;
    for (const auto& file : std::as_const(m_allEntries)) {
        if (file.isDir() || !file.fileName().contains(m_searchPattern, Qt::CaseInsensitive)) continue;
        const auto url = QUrl::fromLocalFile(file.absoluteFilePath());
        urls.append(url);
        m_watchRequested.insert(url);
    }
    setWatchStatesPending(!urls.isEmpty());
    if (urls.isEmpty()) scheduleSort();
    else emit watchStatesRequested(urls, serial);
}

void RedesignFolderModel::finishWatchStates(quint64 serial)
{
    if (serial != m_watchRequestSerial || m_sortCriteria != "played" || !m_watchStatesPending) return;
    setWatchStatesPending(false);
    scheduleSort();
}

void RedesignFolderModel::scheduleSort()
{
    ++m_sortSerial;
    if (m_sortNeeded) return;
    m_sortNeeded = true;
    QTimer::singleShot(0, this, &RedesignFolderModel::startSort);
}

void RedesignFolderModel::startSort()
{
    if (m_sortRunning || !m_sortNeeded) return;
    m_sortNeeded = false;
    m_sortRunning = true;
    const auto results = m_results;
    const auto serial = m_sortSerial;
    const auto played = m_played;
    const auto reversed = m_sortReversed;
    const auto sort = m_sortCriteria;
    const auto search = m_searchPattern;
    const auto metadata = m_audioMetadataEnabled ? m_metadata : QHash<QUrl, RedesignFolderMetadata> {};
    auto entries = m_allEntries;
    QThreadPool::globalInstance()->start([results, serial, played, reversed, sort, search, metadata, entries = std::move(entries)]() mutable {
        if (!search.isEmpty())
            entries.erase(std::remove_if(entries.begin(), entries.end(), [&search, &metadata](const QFileInfo& file) {
                const auto info = metadata.value(QUrl::fromLocalFile(file.absoluteFilePath()));
                return !file.fileName().contains(search, Qt::CaseInsensitive)
                    && !info.title.contains(search, Qt::CaseInsensitive) && !info.artist.contains(search, Qt::CaseInsensitive);
            }), entries.end());
        sortEntries(entries, sort, reversed, played, metadata);
        emit results->sorted(serial, entries);
    });
}

void RedesignFolderModel::applyOrder(const QFileInfoList& entries)
{
    if (m_forceReset) {
        m_forceReset = false;
        replaceEntries(entries);
        return;
    }
    bool sameFiles = entries.size() == m_entries.size();
    if (sameFiles) for (const auto& file : entries)
        sameFiles &= m_rowsByUrl.contains(QUrl::fromLocalFile(file.absoluteFilePath()));
    if (!sameFiles) { replaceEntries(entries); return; }
    bool changed = false;
    for (qsizetype row = 0; row < entries.size(); ++row)
        changed |= entries[row].absoluteFilePath() != m_entries[row].absoluteFilePath();
    if (!changed) return;
    emit layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);
    const auto persistent = persistentIndexList();
    QList<QUrl> urls;
    for (const auto& index : persistent)
        urls.append(QUrl::fromLocalFile(m_entries[index.row()].absoluteFilePath()));
    m_entries = entries;
    rebuildRows();
    QModelIndexList updated;
    for (const auto& url : urls) updated.append(index(m_rowsByUrl.value(url, -1)));
    changePersistentIndexList(persistent, updated);
    emit layoutChanged({}, QAbstractItemModel::VerticalSortHint);
}

QVariant RedesignFolderModel::get(int row, const QString& role) const
{
    const auto roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        if (QString::fromLatin1(it.value()) == role)
            return data(index(row, 0), it.key());
    return {};
}

void RedesignFolderModel::scheduleRescan()
{
    // Unlike a refresh, a rescan keeps the current listing and its pending
    // metadata and watch-state requests until the files actually change.
    if (m_refreshPending || m_rescanPending)
        return;
    m_rescanPending = true;
    QTimer::singleShot(0, this, [this] {
        m_rescanPending = false;
        scan();
    });
}

void RedesignFolderModel::scheduleRefresh()
{
    m_reloadPending = true;
    ++m_serial;
    ++m_watchRequestSerial;
    ++m_audioMetadataSerial;
    setAudioMetadataPending(false);
    ++m_sortSerial;
    m_sortNeeded = false;
    setWatchStatesPending(false);
    emit watchStatesCancelled();
    if (m_refreshPending)
        return;
    m_refreshPending = true;
    QTimer::singleShot(0, this, [this] {
        m_refreshPending = false;
        scan();
    });
}

void RedesignFolderModel::scan()
{
    const auto results = m_results;
    const auto folder = m_folder;
    const auto filters = m_nameFilters;
    const auto serial = m_serial;
    QThreadPool::globalInstance()->start([results, folder, filters, serial] {
        QFileInfoList entries;
        if (folder.isLocalFile()) {
            QDir directory(folder.toLocalFile());
            entries = directory.entryInfoList(filters,
                QDir::AllEntries | QDir::AllDirs | QDir::Readable | QDir::NoDotAndDotDot,
                QDir::NoSort);
        }
        emit results->ready(serial, entries);
    });
}

void RedesignFolderModel::replaceEntries(const QFileInfoList& entries)
{
    const int oldCount = count();
    const int oldDirectoryCount = directoryCount();
    beginResetModel();
    m_entries = entries;
    rebuildRows();
    endResetModel();
    if (oldCount != count() || oldDirectoryCount != directoryCount())
        emit countChanged();
}

void RedesignFolderModel::rebuildRows()
{
    m_rowsByUrl.clear();
    for (int row = 0; row < m_entries.size(); ++row)
        m_rowsByUrl.insert(QUrl::fromLocalFile(m_entries[row].absoluteFilePath()), row);
}
