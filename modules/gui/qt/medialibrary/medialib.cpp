/*****************************************************************************
 * Copyright (C) 2019 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#include "medialib.hpp"
#include <vlc_medialibrary_work.h>
#include <QScopeGuard>
#include "mlhelper.hpp"
#include "playlist/playlist_controller.hpp"
#include "util/shared_input_item.hpp"
#include "util/playlist_cover_store.hpp"
#include "util/track_matching.hpp"
#include "util/media_identity_ml.hpp"
#include "util/subtitle_matching.hpp"
#include "util/local_file_url.hpp"
#include "../../../misc/medialibrary/track_rules.hpp"
#include <QScopeGuard>

#include <utility>

#include <QQmlEngine>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QRandomGenerator>
#include <QTemporaryFile>
#include <QSaveFile>
#include <QDir>
#include <vlc_playlist.h>
#include <vlc_interface.h>
#include <QCollator>
#include <algorithm>

MediaLib::MediaLib(qt_intf_t *_intf, vlc::playlist::PlaylistController* playlistController, QObject *_parent)
    : QObject( _parent )
    , m_intf( _intf )
    , m_playlistController(playlistController)
    , m_ml( vlc_ml_instance_get( _intf ) )
    , m_event_cb( nullptr, [this](vlc_ml_event_callback_t* cb ) {
        vlc_ml_event_unregister_callback( m_ml, cb );
      })
{
    m_event_cb.reset( vlc_ml_event_register_callback( m_ml, MediaLib::onMediaLibraryEvent,
                                                      this ) );
    m_runner = new ThreadRunner();
    m_addonRunner = new MLJoinedThreadRunner(vlc::addons::AddonStreamLifetime::applicationHandle(),this);
}

MediaLib::~MediaLib()
{
    cancelTrackMatching();
    m_addonRunner->close();
    m_runner->destroy();
}

struct MediaLib::NativeTask {
    vlc_ml_work_t* lease;
    explicit NativeTask(vlc_ml_work_t* value) : lease(value) {}
    ~NativeTask() { vlc_ml_work_Release(lease); }
};
std::shared_ptr<MediaLib::NativeTask> MediaLib::nativeTask() const
{
    auto* lease=vlc_ml_work_Acquire(m_ml); if (!lease) return {};
    try { return std::make_shared<NativeTask>(lease); }
    catch (...) { vlc_ml_work_Release(lease); throw; }
}
void MediaLib::invokeNativeTask(const std::shared_ptr<NativeTask>& task,
    const std::function<void(vlc_medialibrary_t*)>& work)
{
    auto* ml=vlc_ml_work_Begin(task->lease); if (!ml) return;
    const auto finish=qScopeGuard([&] { vlc_ml_work_End(task->lease); });
    work(ml);
}

void MediaLib::setFolderRoots(QStringList roots)
{
    roots.removeAll(QString());
    roots.removeDuplicates();
    if (roots == m_folderRoots) return;
    m_folderRoots = std::move(roots);
    emit folderRootsChanged();
}

static void convertMLItemToPlaylistMedias(vlc_medialibrary_t* ml, const MLItemId & itemId, const QStringList &options, QVector<vlc::playlist::Media>& medias)
{
    //invalid item
    if (itemId.id == 0)
        return;

    if (itemId.type == VLC_ML_PARENT_UNKNOWN)
    {
        SharedInputItem item( vlc_ml_get_input_item( ml, itemId.id ), false );
        if (item)
            medias.push_back(vlc::playlist::Media(item.get(), options));
    }
    else
    {
        vlc_ml_query_params_t query = vlc_ml_query_params_create();
        ml_unique_ptr<vlc_ml_media_list_t> media_list(vlc_ml_list_media_of( ml, &query, itemId.type, itemId.id));
        if (media_list == nullptr || media_list->i_nb_items == 0)
            return;

        QByteArray playlistName;
        if (itemId.type == VLC_ML_PARENT_PLAYLIST) {
            ml_unique_ptr<vlc_ml_playlist_t> playlist(vlc_ml_get_playlist(ml, itemId.id));
            if (playlist && playlist->psz_name) playlistName = playlist->psz_name;
        }
        auto mediaRange = ml_range_iterate<vlc_ml_media_t>( media_list );
        std::transform(mediaRange.begin(), mediaRange.end(), std::back_inserter(medias), [&](vlc_ml_media_t& m) {
            SharedInputItem item(vlc_ml_get_input_item( ml, m.i_id ), false);
            vlc::playlist::Media media(item.get(), options);
            // Media owns a copy: provenance belongs to this queued instance,
            // so the same file can also be played from another playlist.
            if (media && !playlistName.isEmpty())
                input_item_SetMetaExtra(media.raw(), "Source playlist", playlistName.constData());
            return media;
        });
    }
}

static void convertQUrlToPlaylistMedias(QUrl mrl, const QStringList& options, QVector<vlc::playlist::Media>& medias)
{
    vlc::playlist::Media media{ mrl.toString(QUrl::FullyEncoded), mrl.fileName(), options };
    medias.push_back(media);
}

static void convertQStringToPlaylistMedias(const QString &mrl, const QStringList& options, QVector<vlc::playlist::Media>& medias)
{
    vlc::playlist::Media media{ mrl, mrl, options };
    medias.push_back(media);
}


static void convertQVariantListToPlaylistMedias(vlc_medialibrary_t* ml, QVariantList itemIdList, const QStringList& options, QVector<vlc::playlist::Media>& medias)
{
    for (const QVariant& varValue: itemIdList)
    {
        if (varValue.canConvert<QUrl>())
        {
            auto mrl = varValue.value<QUrl>();
            convertQUrlToPlaylistMedias(mrl, options, medias);
        }
        else if (varValue.canConvert<QString>())
        {
            auto mrl = varValue.value<QString>();
            convertQStringToPlaylistMedias(mrl, options, medias);
        }
        else if (varValue.canConvert<MLItemId>())
        {
            MLItemId itemId = varValue.value<MLItemId>();
            convertMLItemToPlaylistMedias(ml, itemId, options, medias);
        }
        // else ignore
    }
}

MLItemId MediaLib::deserializeMlItemIdFromString(const QString& serialized_id) {
    return MLItemId::fromString(serialized_id);
}

void MediaLib::addToPlaylist(const QString& mrl, const QStringList &options)
{
    QVector<vlc::playlist::Media> medias;
    convertQStringToPlaylistMedias(mrl, options, medias);
    m_playlistController->append(medias, false);
}

void MediaLib::addToPlaylist(const QUrl& mrl, const QStringList &options)
{
    QVector<vlc::playlist::Media> medias;
    convertQUrlToPlaylistMedias(mrl, options, medias);
    m_playlistController->append(medias, false);
}

// A specific item has been asked to be added to the playlist
void MediaLib::addToPlaylist(const MLItemId & itemId, const QStringList &options)
{
    struct Context {
        QVector<vlc::playlist::Media> medias;
    };
    runOnMLThread<Context>(this,
    //ML thread
    [itemId, options]
    (vlc_medialibrary_t* ml, Context& ctx){
        convertMLItemToPlaylistMedias(ml, itemId, options, ctx.medias);
    },
    //UI thread
    [this](quint64, Context& ctx){
        if (!ctx.medias.empty())
            m_playlistController->append(ctx.medias, false);
    });
}

void MediaLib::addToPlaylist(const QVariantList& itemIdList, const QStringList &options)
{
    struct Context {
        QVector<vlc::playlist::Media> medias;
    };
    runOnMLThread<Context>(this,
    //ML thread
    [itemIdList, options]
    (vlc_medialibrary_t* ml, Context& ctx)
    {
        convertQVariantListToPlaylistMedias(ml, itemIdList, options, ctx.medias);
    },
    //UI thread
    [this](quint64, Context& ctx){
        if (!ctx.medias.empty())
            m_playlistController->append(ctx.medias, false);
    });
}

// A specific item has been asked to be played,
// so it's added to the playlist and played
void MediaLib::addAndPlay(const MLItemId & itemId, const QStringList &options )
{
    struct Context {
        QVector<vlc::playlist::Media> medias;
    };
    runOnMLThread<Context>(this,
    //ML thread
    [itemId, options]
    (vlc_medialibrary_t* ml, Context& ctx)
    {
        convertMLItemToPlaylistMedias(ml, itemId, options, ctx.medias);
    },
    //UI thread
    [this](quint64, Context& ctx){
        if (!ctx.medias.empty())
            m_playlistController->append(ctx.medias, true);
    });


}

void MediaLib::addAndPlay(const QString& mrl, const QStringList &options)
{
    vlc::playlist::Media media{ mrl, mrl, options };
    m_playlistController->append( QVector<vlc::playlist::Media>{media}, true );
}

void MediaLib::addAndPlay(const QUrl& mrl, const QStringList &options)
{
    vlc::playlist::Media media{ mrl.toString(QUrl::FullyEncoded), mrl.fileName(), options };
    m_playlistController->append( QVector<vlc::playlist::Media>{media}, true );
}


void MediaLib::addAndPlay(const QVariantList& itemIdList, const QStringList &options)
{
    struct Context {
        QVector<vlc::playlist::Media> medias;
    };
    runOnMLThread<Context>(this,
    //ML thread
    [itemIdList, options]
    (vlc_medialibrary_t* ml, Context& ctx)
    {
        convertQVariantListToPlaylistMedias(ml, itemIdList, options, ctx.medias);
    },
    //UI thread
    [this](quint64, Context& ctx){
        if (!ctx.medias.empty())
            m_playlistController->append(ctx.medias, true);
    });
}

void MediaLib::insertIntoPlaylist(const size_t index, const QVariantList &itemIdList, const QStringList &options)
{
    struct Context {
        QVector<vlc::playlist::Media> medias;
    };
    runOnMLThread<Context>(this,
    //ML thread
    [itemIdList, options]
    (vlc_medialibrary_t* ml, Context& ctx )
    {
        convertQVariantListToPlaylistMedias(ml, itemIdList, options, ctx.medias);
    },
    //UI thread
    [this, index]
    (quint64, Context& ctx) {
        if (!ctx.medias.isEmpty())
            m_playlistController->insert( index, ctx.medias );
    });
}

void MediaLib::reload()
{
    runOnMLThread(this,
    //ML thread
    [](vlc_medialibrary_t* ml){
        vlc_ml_reload_folder(ml, nullptr);
    });
}

void MediaLib::mediaState(const QUrl& url, bool create, QJSValue callback)
{
    requestMediaState(url, create ? MediaStateAction::Ensure : MediaStateAction::Read, false, callback);
}

void MediaLib::resyncFolder(const QUrl& url, QJSValue callback)
{
    if (!callback.isCallable()) return;
    runOnMLThread<QVariantMap>(this,
        [url](vlc_medialibrary_t* ml, QVariantMap& result) {
            result = {{"success", false}, {"error", qtr("VLC cannot rescan this folder. Make sure that you can read the folder. Try again.")}};
            if (!url.isValid() || url.scheme().isEmpty()) return;
            const auto mrl = url.toEncoded();
            if (vlc_ml_reload_folder(ml, mrl.constData()) == VLC_SUCCESS)
                result = {{"success", true}, {"error", QString()}};
        },
        [this, callback](quint64, const QVariantMap& result) mutable {
            if (auto engine = qjsEngine(this)) callback.call({engine->toScriptValue(result)});
        }, "redesign-folder-resync");
}

void MediaLib::playlistState(const MLItemId& id, QJSValue callback)
{
    requestPlaylistState(id, false, false, callback);
}

void MediaLib::setPlaylistFavorite(const MLItemId& id, bool favorite, QJSValue callback)
{
    requestPlaylistState(id, true, favorite, callback);
}

void MediaLib::playlistTrackTargets(const MLItemId& id, QJSValue callback)
{
    if (!callback.isCallable()) return;
    runOnMLThread<QVariantMap>(this,
        [id](vlc_medialibrary_t* ml, QVariantMap& result) {
            result = {{"success", false}, {"error", qtr("VLC cannot read this playlist. Select it again.")}};
            if (id.type != VLC_ML_PARENT_PLAYLIST || id.id <= 0) return;
            ml_unique_ptr<vlc_ml_playlist_t> playlist(vlc_ml_get_playlist(ml, id.id));
            if (!playlist) return;
            auto query = vlc_ml_query_params_create();
            ml_unique_ptr<vlc_ml_media_list_t> media(vlc_ml_list_playlist_media(ml, &query, id.id));
            if (!media) return;
            QVariantList items;
            QSet<int64_t> seen;
            for (const auto& item : ml_range_iterate<vlc_ml_media_t>(media)) {
                if (seen.contains(item.i_id) || !item.p_files) continue;
                seen.insert(item.i_id);
                for (const auto& file : ml_range_iterate<vlc_ml_file_t>(item.p_files)) {
                    if (file.i_type != VLC_ML_FILE_TYPE_MAIN) continue;
                    const QUrl url(qfu(file.psz_mrl));
                    if (!url.isValid() || url.scheme().isEmpty()) continue;
                    QString name = qfu(item.psz_filename);
                    if (name.isEmpty()) name = url.fileName();
                    if (name.isEmpty()) name = qfu(item.psz_title);
                    if (name.isEmpty()) name = url.toDisplayString();
                    items.push_back(QVariantMap{{"name", name}, {"url", url.toString(QUrl::FullyEncoded)},
                        {"location", url.toDisplayString(QUrl::PreferLocalFile)}, {"enabled", file.b_present}});
                    break;
                }
            }
            result = {{"success", true}, {"error", QString()}, {"name", qfu(playlist->psz_name)}, {"items", items}};
        },
        [this, callback](quint64, const QVariantMap& result) mutable {
            if (auto engine = qjsEngine(this)) callback.call({engine->toScriptValue(result)});
        }, "redesign-playlist-tracks");
}

namespace {
QString trackString(const QUrl& url)
{
    return vlc::qt::normalizeLocalFileUrl(url).toString(QUrl::FullyEncoded);
}

QStringList selectedTracks(const vlc_ml_media_t* media, vlc_ml_file_type_t type)
{
    QStringList tracks;
    if (media && media->p_files) {
        for (const auto& file : ml_range_iterate<vlc_ml_file_t>(media->p_files))
            if (file.i_type == type && !file.b_unselected)
                tracks.append(trackString(QUrl(qfu(file.psz_mrl))));
    }
    tracks.removeDuplicates();
    return tracks;
}

bool sameTracks(QStringList left, QStringList right)
{
    left.removeDuplicates(); right.removeDuplicates();
    left.sort(); right.sort();
    return left == right;
}

QStringList trackStrings(const QVariant& value)
{
    QStringList result;
    for (const auto& item : value.toList()) result.append(trackString(item.toUrl()));
    return result;
}

QStringList trackFilters(bool subtitles)
{
    return (subtitles ? QStringLiteral(EXTENSIONS_SUBTITLE) : QStringLiteral(EXTENSIONS_AUDIO)).split(QLatin1Char(';'));
}

QFileInfoList trackFiles(const QString& folder, bool subtitles)
{
    const auto filters = trackFilters(subtitles);
    const QDir root(folder);
    auto files = root.entryInfoList(filters, QDir::Files, QDir::NoSort);
    if (subtitles) {
        const auto subtitleFolder = [](const QString& name) {
            return name.compare("subs", Qt::CaseInsensitive) == 0 ||
                   name.compare("sub", Qt::CaseInsensitive) == 0 ||
                   name.compare("subtitles", Qt::CaseInsensitive) == 0;
        };
        const auto children = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
        for (const auto& child : children) {
            if (!subtitleFolder(root.dirName()) && !subtitleFolder(child.fileName())) continue;
            const QDir directory(child.absoluteFilePath());
            files.append(directory.entryInfoList(filters, QDir::Files, QDir::NoSort));
            // A nearby subtitle folder can contain one folder per episode.
            // Keep this search shallow so opening a media folder does not scan a drive.
            if (!subtitleFolder(child.fileName())) continue;
            for (const auto& episode : directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks))
                files.append(QDir(episode.absoluteFilePath()).entryInfoList(filters, QDir::Files, QDir::NoSort));
        }
    }
    return files;
}

QVariantList trackSources(QFileInfoList files, bool subtitles, const QString& folder = {})
{
    QCollator collator; collator.setNumericMode(true); collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(files.begin(), files.end(), [&collator](const QFileInfo& a, const QFileInfo& b) {
        return collator.compare(a.absoluteFilePath(), b.absoluteFilePath()) < 0;
    });
    QVariantList sources;
    QSet<QUrl> seen;
    QHash<QString, QHash<QString, QFileInfo>> indexes;
    for (auto file : files) {
        // A VobSub pair is opened through its index, even when only its data file was dropped.
        if (subtitles && file.suffix().compare("sub", Qt::CaseInsensitive) == 0) {
            const auto directory = file.absolutePath();
            if (!indexes.contains(directory)) {
                auto& found = indexes[directory];
                for (const auto& index : QDir(directory).entryInfoList({"*.idx"}, QDir::Files))
                    found.insert(index.completeBaseName().toCaseFolded(), index);
            }
            const auto& found = indexes[directory];
            const auto index = found.constFind(file.completeBaseName().toCaseFolded());
            if (index != found.cend()) file = *index;
        }
        const auto url = QUrl::fromLocalFile(file.absoluteFilePath());
        if (seen.contains(url)) continue;
        seen.insert(url);
        const auto name = folder.isEmpty() ? file.fileName() : QDir(folder).relativeFilePath(file.absoluteFilePath());
        sources.append(QVariantMap{{"url", url.toString(QUrl::FullyEncoded)}, {"name", name}});
    }
    return sources;
}

}

void MediaLib::trackAssignmentRows(const QVariantMap& target, bool subtitles, QJSValue callback)
{
    if (!callback.isCallable()) return;
    const bool automatic = !subtitles || var_InheritBool(m_intf, "sub-autodetect-file");
    runOnMLThread<QVariantMap>(this,
        [target, subtitles, automatic](vlc_medialibrary_t* ml, QVariantMap& result) {
            result = {{"success", false}, {"error", qtr("VLC cannot read these files. Select them again.")}};
            QList<QUrl> urls;
            const auto scope = target.value("scope").toString();
            if (scope == "file") urls.append(target.value("url").toUrl());
            else if (scope == "folder") {
                const auto folder = target.value("folder", target.value("url")).toUrl();
                if (!folder.isLocalFile() || !QFileInfo(folder.toLocalFile()).isDir()) return;
                auto filters = QStringLiteral(EXTENSIONS_VIDEO ";" EXTENSIONS_AUDIO).split(QLatin1Char(';'));
                const auto files = QDir(folder.toLocalFile()).entryInfoList(filters, QDir::Files, QDir::NoSort);
                QCollator collator; collator.setNumericMode(true); collator.setCaseSensitivity(Qt::CaseInsensitive);
                auto sorted = files;
                std::sort(sorted.begin(), sorted.end(), [&collator](const QFileInfo& a, const QFileInfo& b) {
                    return collator.compare(a.fileName(), b.fileName()) < 0;
                });
                for (const auto& file : sorted) urls.append(QUrl::fromLocalFile(file.absoluteFilePath()));
            } else if (scope == "playlist") {
                const auto id = deserializeMlItemIdFromString(target.value("key").toString());
                if (id.type != VLC_ML_PARENT_PLAYLIST || id.id <= 0) return;
                ml_unique_ptr<vlc_ml_playlist_t> playlist(vlc_ml_get_playlist(ml, id.id));
                if (!playlist) return;
                auto query = vlc_ml_query_params_create();
                ml_unique_ptr<vlc_ml_media_list_t> items(vlc_ml_list_playlist_media(ml, &query, id.id));
                if (!items) return;
                for (const auto& item : ml_range_iterate<vlc_ml_media_t>(items)) {
                    if (!item.p_files) continue;
                    for (const auto& file : ml_range_iterate<vlc_ml_file_t>(item.p_files)) {
                        if (file.i_type != VLC_ML_FILE_TYPE_MAIN) continue;
                        urls.append(QUrl(qfu(file.psz_mrl))); break;
                    }
                }
                result.insert("name", qfu(playlist->psz_name));
            } else if (scope == "queue") {
                for (const auto& value : target.value("urls").toList()) urls.append(value.toUrl());
            } else return;
            const auto type = subtitles ? VLC_ML_FILE_TYPE_SUBTITLE : VLC_ML_FILE_TYPE_SOUNDTRACK;
            bool hasFolderRule = false;
            if (scope == "folder") {
                const auto folder = target.value("folder", target.value("url")).toUrl();
                char* value = nullptr;
                const int status = vlc_ml_folder_get_track_rule(ml, folder.toEncoded().constData(), type, &value);
                const auto definition = qfu(value); free(value);
                result.insert("folderRule", definition); result.insert("automaticMatching", automatic);
                if (status != VLC_SUCCESS) result.insert("ruleError", qtr("VLC cannot read the saved folder matches. Save again to replace them."));
                std::vector<vlc::medialibrary::trackrules::Pair> pairs;
                if (!definition.isEmpty() && vlc::medialibrary::trackrules::decode(definition.toStdString(), pairs) && !pairs.empty()) {
                    hasFolderRule = true;
                    result.insert("sourceFolder", QUrl::fromLocalFile(QString::fromStdString(pairs.front().source)).toString(QUrl::FullyEncoded));
                }
            }
            QVariantList rows;
            QSet<QUrl> seen;
            int commonCategory = -1;
            for (const auto& url : urls) {
                if (!url.isValid() || url.scheme().isEmpty() || seen.contains(url)) continue;
                seen.insert(url);
                const auto uri = url.toEncoded();
                ml_unique_ptr<vlc_ml_media_t> media(vlc_ml_get_media_by_mrl(ml, uri.constData()));
                int category = media && media->i_type == VLC_ML_MEDIA_TYPE_VIDEO ? 1 : media && media->i_type == VLC_ML_MEDIA_TYPE_AUDIO ? 2 : 0;
                const auto runtimeCategory = target.value("runtimeCategory").toInt();
                if (url == target.value("runtimeUrl").toUrl() && (runtimeCategory == 1 || runtimeCategory == 2)) category = runtimeCategory;
                commonCategory = commonCategory < 0 ? category : commonCategory == category ? category : 0;
                auto name = url.fileName();
                if (name.isEmpty() && media) name = qfu(media->psz_title);
                if (name.isEmpty()) name = url.toDisplayString();
                const bool explicitSelection = media && (subtitles ? media->b_subtitle_track_selection : media->b_soundtrack_track_selection);
                const auto baseline = selectedTracks(media.get(), type);
                auto initial = baseline;
                bool inherited = false;
                if (!explicitSelection && (scope == "folder" ? hasFolderRule : automatic)) {
                    vlc_ml_file_list_t* files = nullptr;
                    const int status = vlc_ml_media_get_folder_tracks(ml, uri.constData(), type, &inherited, &files);
                    if (status != VLC_SUCCESS) inherited = false;
                    if (inherited) initial.clear();
                    if (status == VLC_SUCCESS && files) for (const auto& file : ml_range_iterate<vlc_ml_file_t>(files)) initial.append(trackString(QUrl(qfu(file.psz_mrl))));
                    if (files) vlc_ml_file_list_release(files);
                    initial.removeDuplicates();
                }
                if (!explicitSelection && url == target.value("runtimeUrl").toUrl()) {
                    initial.append(trackStrings(target.value("runtimeTracks")));
                    initial.removeDuplicates();
                }
                QVariantMap metadata = MediaIdentity::libraryMetadata(media.get(), url);
                if (media) {
                    QVariantMap release;
                    QStringList languages;
                    if (media->p_tracks) for (const auto& track : ml_range_iterate<vlc_ml_media_track_t>(media->p_tracks)) {
                        if (track.i_type == VLC_ML_TRACK_TYPE_VIDEO) {
                            metadata.insert("width", track.v.i_width); metadata.insert("height", track.v.i_height);
                            if (track.v.i_fpsDen) release.insert("fps", QString::number(double(track.v.i_fpsNum) / track.v.i_fpsDen));
                        } else if (track.i_type == VLC_ML_TRACK_TYPE_AUDIO && track.psz_language) languages.append(qfu(track.psz_language));
                    }
                    languages.removeDuplicates(); metadata.insert("audioLanguages", languages); metadata.insert("release", release);
                }
                if (url == target.value("runtimeUrl").toUrl()) {
                    const auto runtime = target.value("runtimeMetadata").toMap();
                    for (auto it = runtime.cbegin(); it != runtime.cend(); ++it) metadata.insert(it.key(), it.value());
                }
                const auto identity = MediaIdentity::toMap(MediaIdentity::identify(url, metadata));
                rows.append(QVariantMap{{"url", url.toString(QUrl::FullyEncoded)}, {"name", name},
                    {"location", url.toDisplayString(QUrl::PreferLocalFile)},
                    {"explicitSelection", explicitSelection}, {"inheritedRule", inherited}, {"initialTracks", initial},
                    {"baseline", baseline}, {"metadata", metadata}, {"identity", identity}});
            }
            const auto category = target.value("libraryCategory").toInt();
            result.insert("folderCategory", category == 1 || category == 2 ? category : std::max(0, commonCategory));
            result.insert("success", true); result.insert("error", QString()); result.insert("rows", rows);
        },
        [this, callback](quint64, const QVariantMap& result) mutable {
            if (auto engine = qjsEngine(this)) callback.call({engine->toScriptValue(result)});
        }, "redesign-track-targets");
}

void MediaLib::trackAssignmentSources(const QUrl& folder, bool subtitles, QJSValue callback)
{
    if (!callback.isCallable()) return;
    runOnMLThread<QVariantMap>(this,
        [folder, subtitles](vlc_medialibrary_t*, QVariantMap& result) {
            result = {{"success", false}, {"error", qtr("VLC cannot read this folder. Select it again.")}};
            if (!folder.isLocalFile() || !QFileInfo(folder.toLocalFile()).isDir()) return;
            const auto sources = trackSources(trackFiles(folder.toLocalFile(), subtitles), subtitles, folder.toLocalFile());
            result = {{"success", true}, {"error", QString()}, {"sources", sources}};
        },
        [this, callback](quint64, const QVariantMap& result) mutable {
            if (auto engine = qjsEngine(this)) callback.call({engine->toScriptValue(result)});
        }, "redesign-track-sources");
}

void MediaLib::prepareTrackDrop(const QList<QUrl>& urls, bool subtitles, QJSValue callback)
{
    if (!callback.isCallable()) return;
    runOnMLThread<QVariantMap>(this,
        [urls, subtitles](vlc_medialibrary_t*, QVariantMap& result) {
            QFileInfoList files;
            QStringList folders;
            int skipped = 0;
            for (const auto& url : urls) {
                if (!url.isLocalFile()) { ++skipped; continue; }
                const QFileInfo file(url.toLocalFile());
                if (file.isDir() && file.isReadable()) {
                    if (!folders.contains(file.absoluteFilePath())) {
                        folders.append(file.absoluteFilePath()); files.append(trackFiles(file.absoluteFilePath(), subtitles));
                    }
                } else if (file.isFile() && file.isReadable() && QDir::match(trackFilters(subtitles), file.fileName()))
                    files.append(file);
                else ++skipped;
            }
            const auto sources = trackSources(files, subtitles, folders.size() == 1 ? folders.first() : QString());
            result = {{"success", !sources.isEmpty()}, {"sources", sources}, {"skipped", skipped},
                {"folder", folders.size() == 1 ? QUrl::fromLocalFile(folders.first()).toString(QUrl::FullyEncoded) : QString()},
                {"error", sources.isEmpty() ? (subtitles ? qtr("Select subtitle files or folders.") : qtr("Select audio files or folders."))
                    : skipped ? qtr("Some files cannot be used. Select track files or folders.") : QString()}};
        },
        [this, callback](quint64, const QVariantMap& result) mutable {
            if (auto engine = qjsEngine(this)) callback.call({engine->toScriptValue(result)});
        }, "redesign-track-drop");
}

QVariantMap MediaLib::matchTrackAssignments(const QList<QUrl>& media, const QList<QUrl>& tracks) const
{
    QVariantMap result;
    const auto matches = TrackMatching::match(media, tracks);
    for (auto it = matches.cbegin(); it != matches.cend(); ++it) {
        QStringList values;
        for (const auto& track : it.value()) values.append(track.toString(QUrl::FullyEncoded));
        result.insert(it.key().toString(QUrl::FullyEncoded), values);
    }
    return result;
}

void MediaLib::cancelTrackMatching()
{
    if (m_trackMatchCancelled) m_trackMatchCancelled->store(true, std::memory_order_relaxed);
    if (m_trackMatchTask) cancelMLTask(this, m_trackMatchTask);
    m_trackMatchTask = 0;
    m_trackMatchCancelled.reset();
}

void MediaLib::requestTrackMatches(const QList<QUrl>& media, const QList<QUrl>& tracks, QJSValue callback)
{
    cancelTrackMatching();
    if (!callback.isCallable()) return;
    const auto cancelled = std::make_shared<std::atomic_bool>(false);
    m_trackMatchCancelled = cancelled;
    m_trackMatchTask = runOnMLThread<QVariantMap>(this,
        [media, tracks, cancelled](vlc_medialibrary_t*, QVariantMap& result) {
            const auto matches = TrackMatching::match(media, tracks, cancelled.get());
            for (auto it = matches.cbegin(); it != matches.cend(); ++it) {
                QStringList values;
                for (const auto& track : it.value()) values.append(track.toString(QUrl::FullyEncoded));
                result.insert(it.key().toString(QUrl::FullyEncoded), values);
            }
        },
        [this, callback, cancelled](quint64, const QVariantMap& result) mutable {
            if (cancelled->load(std::memory_order_relaxed)) return;
            m_trackMatchTask = 0;
            m_trackMatchCancelled.reset();
            if (auto engine = qjsEngine(this)) callback.call({engine->toScriptValue(result)});
        }, "redesign-track-matching");
}

void MediaLib::requestSubtitleMatches(const QVariantList& media, const QVariantList& sources, const QString& language, QJSValue callback)
{
    cancelTrackMatching();
    if (!callback.isCallable()) return;
    const auto cancelled = std::make_shared<std::atomic_bool>(false); m_trackMatchCancelled = cancelled;
    m_trackMatchTask = runOnMLThread<QVariantMap>(this,
        [media, sources, language, cancelled](vlc_medialibrary_t*, QVariantMap& result) {
            result = SubtitleMatching::match(media, sources, language, cancelled.get());
        },
        [this, callback, cancelled](quint64, const QVariantMap& result) mutable {
            if (cancelled->load(std::memory_order_relaxed)) return;
            m_trackMatchTask = 0; m_trackMatchCancelled.reset();
            if (auto engine = qjsEngine(this)) callback.call({engine->toScriptValue(result)});
        }, "redesign-subtitle-matching");
}

void MediaLib::saveTrackAssignments(const QVariantList& rows, bool subtitles, QJSValue callback)
{
    saveTrackAssignmentsImpl(rows, subtitles, {}, std::move(callback));
}

QString MediaLib::trackFolderRuleDefinition(const QVariantList& rows, const QUrl& folder,
                                           const QUrl& source, const QString& inherited) const
{
    namespace Rules = vlc::medialibrary::trackrules;
    if (!folder.isLocalFile() || !QFileInfo(folder.toLocalFile()).isAbsolute()) return {};
    const auto target = QDir::cleanPath(folder.toLocalFile());
    std::vector<Rules::Pair> pairs;
    std::vector<Rules::Pair> previous;
    Rules::decode(inherited.toStdString(), previous);
    QSet<QString> families, untouched;
    QVariantList validRows;
    for (const auto& value : rows) {
        const auto row = value.toMap(); const auto media = row.value("url").toUrl();
        if (!media.isLocalFile()) continue;
        const auto parent = QDir::cleanPath(QFileInfo(media.toLocalFile()).absolutePath());
#ifdef Q_OS_WIN
        if (parent.compare(target, Qt::CaseInsensitive) != 0) continue;
#else
        if (parent != target) continue;
#endif
        validRows.append(row);
        const auto family = QString::fromStdString(Rules::family(media.fileName().toStdString()));
        families.insert(family);
        if (row.value("inheritedRule").toBool() && !row.value("manual").toBool()
            && sameTracks(trackStrings(row.value("initialTracks")), trackStrings(row.value("tracks")))) untouched.insert(family);
    }
    for (const auto& pair : previous) {
        const auto family = QString::fromStdString(Rules::family(pair.media));
        if (family == "*" ? validRows.isEmpty() || !untouched.isEmpty() : !families.contains(family) || untouched.contains(family)) pairs.push_back(pair);
    }
    for (const auto& value : validRows) {
        const auto row = value.toMap(); const auto media = row.value("url").toUrl();
        for (const auto& entry : row.value("tracks").toList()) {
            const auto track = entry.toUrl();
            if (!track.isLocalFile() || !QFileInfo(track.toLocalFile()).isAbsolute()) continue;
            auto root = source.isLocalFile() ? QDir::cleanPath(source.toLocalFile()) : QString();
            const auto relative = root.isEmpty() ? QStringLiteral("../") : QDir(root).relativeFilePath(track.toLocalFile());
            if (relative == ".." || relative.startsWith("../")) {
                root = QFileInfo(track.toLocalFile()).absolutePath();
                if (!Rules::episodes(QFileInfo(root).fileName().toStdString()).empty()) root = QFileInfo(root).absolutePath();
            }
            pairs.push_back({media.fileName().toStdString(), QDir::cleanPath(track.toLocalFile()).toStdString(), root.toStdString()});
        }
    }
    pairs = Rules::distinct(pairs);
    return pairs.empty() ? QString() : QString::fromStdString(Rules::encode(pairs));
}

void MediaLib::saveTrackAssignmentsWithRule(const QVariantList& rows, bool subtitles,
                                           const QVariantMap& rule, QJSValue callback)
{
    saveTrackAssignmentsImpl(rows, subtitles, rule, std::move(callback));
}

void MediaLib::saveTrackAssignmentsImpl(const QVariantList& rows, bool subtitles,
                                       const QVariantMap& rule, QJSValue callback)
{
    if (!callback.isCallable()) return;
    runOnMLThread<QVariantMap>(this,
        [rows, subtitles, rule](vlc_medialibrary_t* ml, QVariantMap& result) {
            QVariantList saved;
            result = {{"success", false}, {"error", QString()}, {"saved", saved}};
            const auto type = subtitles ? VLC_ML_FILE_TYPE_SUBTITLE : VLC_ML_FILE_TYPE_SOUNDTRACK;
            if (!rule.isEmpty()) {
                std::vector<vlc::medialibrary::trackrules::Pair> pairs;
                const auto definition = rule.value("definition").toString();
                if (!rule.value("folder").toUrl().isLocalFile()
                    || (!definition.isEmpty() && !vlc::medialibrary::trackrules::decode(definition.toStdString(), pairs))) {
                    result["error"] = qtr("VLC cannot save these folder matches. Select the folder again."); return;
                }
            }
            struct Edit { QUrl url; QStringList before; QStringList after; QStringList initial; };
            QList<Edit> edits;
            QSet<QUrl> seen;
            // Validate the whole draft before writing any media. Each later
            // write is atomic; report completed rows if another write fails.
            for (const auto& value : rows) {
                const auto row = value.toMap();
                const auto url = row.value("url").toUrl();
                auto before = trackStrings(row.value("baseline"));
                auto after = trackStrings(row.value("tracks"));
                const auto initial = trackStrings(row.value("initialTracks", row.value("baseline")));
                before.removeDuplicates(); after.removeDuplicates();
                if (!url.isValid() || url.scheme().isEmpty() || seen.contains(url)) {
                    result["error"] = qtr("VLC cannot save this selection. Select the files again."); return;
                }
                seen.insert(url);
                const auto uri = url.toEncoded();
                ml_unique_ptr<vlc_ml_media_t> media(vlc_ml_get_media_by_mrl(ml, uri.constData()));
                if (!sameTracks(before, selectedTracks(media.get(), type))) {
                    result["error"] = qtr("The saved tracks changed. Close this window and select the files again."); return;
                }
                if (!media && (!url.isLocalFile() || !QFileInfo(url.toLocalFile()).isFile())) {
                    result["error"] = qtr("VLC cannot read this media: %1").arg(url.toDisplayString()); return;
                }
                for (const auto& text : after) {
                    const QUrl track(text);
                    if (!track.isValid() || track.scheme().isEmpty() || text == trackString(url)) {
                        result["error"] = qtr("VLC cannot use this track: %1").arg(text); return;
                    }
                    if (media && media->p_files) {
                        for (const auto& file : ml_range_iterate<vlc_ml_file_t>(media->p_files)) {
                            if (trackString(QUrl(qfu(file.psz_mrl))) == text && file.i_type != type) {
                                result["error"] = qtr("VLC already uses this file for another track: %1").arg(track.fileName()); return;
                            }
                        }
                    }
                    // Keep an unchanged saved or already loaded source, including an offline file.
                    if (before.contains(text) || initial.contains(text)) continue;
                    QFile file(track.toLocalFile());
                    if (!track.isLocalFile() || !QFileInfo(file).isFile() || !file.open(QIODevice::ReadOnly)) {
                        result["error"] = qtr("VLC cannot read this track: %1").arg(track.toDisplayString()); return;
                    }
                }
                if (!sameTracks(before, after) || !sameTracks(initial, after)
                    || (row.value("inheritedRule").toBool() && row.value("manual").toBool())) edits.append({url, before, after, initial});
            }
            for (const auto& edit : edits) {
                const auto uri = edit.url.toEncoded();
                ml_unique_ptr<vlc_ml_media_t> media(vlc_ml_get_media_by_mrl(ml, uri.constData()));
                if (!sameTracks(edit.before, selectedTracks(media.get(), type))) {
                    result["error"] = qtr("The saved tracks changed. Close this window and select the files again."); return;
                }
                if (!media) media.reset(vlc_ml_new_external_media(ml, uri.constData()));
                QList<QByteArray> encoded;
                std::vector<const char*> pointers;
                for (const auto& track : edit.after) encoded.append(QUrl(track).toEncoded());
                for (const auto& track : encoded) pointers.push_back(track.constData());
                const int status = media ? vlc_ml_media_set_external_mrls(ml, media->i_id, type,
                    pointers.data(), pointers.size()) : VLC_EGENERIC;
                if (status != VLC_SUCCESS) {
                    result["error"] = status == VLC_ENOTSUP
                        ? qtr("This VLC build cannot replace saved tracks.")
                        : qtr("VLC cannot save the tracks for %1. Try again.").arg(edit.url.fileName());
                    return;
                }
                saved.append(QVariantMap{{"url", edit.url.toString(QUrl::FullyEncoded)},
                    {"baseline", edit.before}, {"initialTracks", edit.initial}, {"tracks", edit.after}});
                result["saved"] = saved;
            }
            if (!rule.isEmpty()) {
                const auto folder = rule.value("folder").toUrl().toEncoded();
                const auto definition = rule.value("definition").toString().toUtf8();
                if (vlc_ml_folder_set_track_rule(ml, folder.constData(), type, definition.isEmpty() ? nullptr : definition.constData()) != VLC_SUCCESS) {
                    result["error"] = qtr("VLC cannot save the folder matches. Try Save again."); return;
                }
                result["folderRuleSaved"] = true; result["folderRule"] = QString::fromUtf8(definition);
            }
            result["success"] = true;
        },
        [this, callback](quint64, const QVariantMap& result) mutable {
            if (auto engine = qjsEngine(this)) callback.call({engine->toScriptValue(result)});
        }, "redesign-track-save");
}

void MediaLib::requestPlaylistState(const MLItemId& id, bool update, bool favorite, QJSValue callback)
{
    if (!callback.isCallable()) return;
    runOnMLThread<QVariantMap>(this,
        [id, update, favorite](vlc_medialibrary_t* ml, QVariantMap& state) {
            state = {{"success", false}, {"error", qtr("VLC cannot read this playlist. Select it again.")}};
            if (id.type != VLC_ML_PARENT_PLAYLIST || id.id <= 0) return;
            ml_unique_ptr<vlc_ml_playlist_t> playlist(vlc_ml_get_playlist(ml, id.id));
            if (!playlist) return;
            if (update && vlc_ml_playlist_set_favorite(ml, id.id, favorite) != VLC_SUCCESS) {
                state["error"] = qtr("VLC cannot save this change. Try again.");
                return;
            }
            state = {{"success", true}, {"error", QString()}, {"id", id.toString()},
                {"name", qfu(playlist->psz_name)}, {"favorite", update ? favorite : playlist->b_is_favorite},
                {"count", playlist->i_nb_media}, {"duration", QVariant::fromValue(playlist->i_duration)},
                {"readOnly", playlist->b_is_read_only}, {"created", playlist->i_creation_date},
                {"videoCount", playlist->i_nb_video}, {"audioCount", playlist->i_nb_audio},
                {"unknownCount", playlist->i_nb_unknown}, {"missingCount", playlist->i_nb_media - std::min(playlist->i_nb_media, playlist->i_nb_present_media)},
                {"unknownDurationCount", playlist->i_nb_duration_unknown}, {"source", qfu(playlist->psz_mrl)},
                {"cover", PlaylistCoverStore::cover(id.id, playlist->i_creation_date).toString(QUrl::FullyEncoded)}};
        },
        [this, callback](quint64, const QVariantMap& state) mutable {
            if (auto engine = qjsEngine(this)) callback.call({engine->toScriptValue(state)});
        }, "redesign-playlist-state");
}

void MediaLib::savePlaylistDetails(const MLItemId& id, const QString& name, const QUrl& cover, QJSValue callback)
{
    if (!callback.isCallable()) return;
    runOnMLThread<QVariantMap>(this,
        [id, name = name.trimmed(), cover](vlc_medialibrary_t* ml, QVariantMap& result) {
            result = {{"success", false}, {"error", qtr("VLC cannot save this playlist. Try again.")}};
            if (id.type != VLC_ML_PARENT_PLAYLIST || id.id <= 0 || name.isEmpty()) return;
            ml_unique_ptr<vlc_ml_playlist_t> playlist(vlc_ml_get_playlist(ml, id.id));
            if (!playlist) return;
            if (playlist->b_is_read_only) {
                result["error"] = qtr("You cannot change this playlist.");
                return;
            }
            const auto oldName = qfu(playlist->psz_name);
            const auto writerLock = PlaylistCoverStore::lock();
            if (!writerLock) return;
            const auto oldCover = PlaylistCoverStore::cover(id.id, playlist->i_creation_date);
            const bool changeCover = cover != oldCover;
            QString image;
            bool saved = false;
            const auto cleanup = qScopeGuard([&] { if (!saved && !image.isEmpty()) QFile::remove(image); });
            std::unique_ptr<QSaveFile> metadata;
            if (changeCover) {
                if (!cover.isEmpty()) {
                    image = PlaylistCoverStore::prepareImage(cover);
                    if (image.isEmpty()) {
                        result["error"] = qtr("VLC cannot read or save this image. Select another image.");
                        return;
                    }
                }
                metadata = PlaylistCoverStore::stage(id.id, playlist->i_creation_date, image);
                if (!metadata) return;
            }
            const bool changeName = oldName != name;
            if (changeName && vlc_ml_playlist_rename(ml, id.id, qtu(name)) != VLC_SUCCESS) return;
            if (metadata && !metadata->commit()) {
                if (changeName && vlc_ml_playlist_rename(ml, id.id, qtu(oldName)) != VLC_SUCCESS)
                    result["error"] = qtr("VLC saved the name but cannot save the thumbnail. Open the properties again to see the saved values.");
                return;
            }
            saved = true;
            if (changeCover && oldCover.isLocalFile()) QFile::remove(oldCover.toLocalFile());
            result = {{"success", true}, {"error", QString()}, {"name", name},
                      {"cover", PlaylistCoverStore::cover(id.id, playlist->i_creation_date)}};
        },
        [this, id, callback](quint64, const QVariantMap& result) mutable {
            if (result.value("success").toBool())
                emit playlistDetailsChanged(id, result.value("name").toString(), result.value("cover").toUrl());
            if (auto engine = qjsEngine(this)) callback.call({engine->toScriptValue(result)});
        }, "redesign-playlist-details");
}

void MediaLib::recordNetworkPlaylistAccess(const QUrl& url)
{
    if (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https")) return;
    if (!var_InheritBool(m_intf, "save-recentplay")) return;
    const auto expression = vlc::wrap_cptr(var_InheritString(m_intf, "recentplay-filter"));
    const auto pattern = QString::fromUtf8(expression.get() ? expression.get() : "");
    runOnMLThread(this, [uri = url.toEncoded(), intf = m_intf, pattern](vlc_medialibrary_t* ml) {
        if (!var_InheritBool(intf, "save-recentplay")) return;
        const auto current = vlc::wrap_cptr(var_InheritString(intf, "recentplay-filter"));
        if (QString::fromUtf8(current.get() ? current.get() : "") != pattern) return;
        ml_unique_ptr<vlc_ml_media_t> media(vlc_ml_get_media_by_mrl(ml, uri.constData()));
        // Containers are expanded before playback can record their access.
        // Bump the source's history date, without marking a channel as played
        // or resetting a playable media item's saved position. The ML control
        // also applies recentplay-filter, including invalid-pattern rejection.
        if (media && media->i_type == VLC_ML_MEDIA_TYPE_UNKNOWN)
            vlc_ml_media_update_progress(ml, media->i_id, 0.);
    });
}

void MediaLib::shuffleAndPlay(const MLItemId& id)
{
    if (id.type != VLC_ML_PARENT_PLAYLIST || id.id <= 0) return;
    struct Result { QVector<vlc::playlist::Media> media; };
    runOnMLThread<Result>(this,
        [id](vlc_medialibrary_t* ml, Result& result) {
            convertMLItemToPlaylistMedias(ml, id, {}, result.media);
            std::shuffle(result.media.begin(), result.media.end(), *QRandomGenerator::global());
        },
        [this](quint64, const Result& result) {
            if (!result.media.isEmpty()) m_playlistController->append(result.media, true);
        });
}

void MediaLib::exportPlaylist(const MLItemId& id, const QUrl& file, const QByteArray& format,
                             QObject* receiver, std::function<void(const PlaylistExportResult&)> callback)
{
    runOnMLThread<PlaylistExportResult>(receiver,
        [id, file, format, intf = m_intf](vlc_medialibrary_t* ml, PlaylistExportResult& result) {
            auto& error = result.error;
            error = qtr("VLC cannot export this playlist. Select it again.");
            if (id.type != VLC_ML_PARENT_PLAYLIST || id.id <= 0 || !file.isLocalFile()) return;
            if (format != "export-xspf" && format != "export-m3u" && format != "export-m3u8" && format != "export-html") return;
            ml_unique_ptr<vlc_ml_playlist_t> source(vlc_ml_get_playlist(ml, id.id));
            if (!source) return;
            auto query = vlc_ml_query_params_create();
            ml_unique_ptr<vlc_ml_media_list_t> media(vlc_ml_list_playlist_media(ml, &query, id.id));
            if (!media) return;
            std::vector<SharedInputItem> items;
            std::vector<input_item_t*> pointers;
            for (const auto& entry : ml_range_iterate<vlc_ml_media_t>(media)) {
                SharedInputItem item(vlc_ml_get_input_item(ml, entry.i_id), false);
                if (!item) return;
                pointers.push_back(item.get());
                items.push_back(std::move(item));
            }
            /* An independent, non-playing queue keeps export separate from the
             * user's playback queue, selected item and shuffle setting. */
            auto queue = vlc::wrap_cptr(vlc_playlist_New(VLC_OBJECT(intf),
                VLC_PLAYLIST_PREPARSING_DISABLED, 1, 0), vlc_playlist_Delete);
            if (!queue) return;
            result.failure = PlaylistExportResult::Failure::Destination;
            error = qtr("VLC cannot save this file. Select another location.");
            const QFileInfo destination(file.toLocalFile());
            QTemporaryFile temporary(destination.dir().filePath(QStringLiteral(".vlc-export-XXXXXX")));
            if (!temporary.open()) return;
            temporary.close();
            vlc_playlist_Lock(queue.get());
            int status = pointers.empty() ? VLC_SUCCESS :
                vlc_playlist_Insert(queue.get(), 0, pointers.data(), pointers.size());
            if (status == VLC_SUCCESS)
                status = vlc_playlist_Export(queue.get(), qtu(temporary.fileName()), format.constData());
            vlc_playlist_Unlock(queue.get());
            if (status != VLC_SUCCESS) return;
            QFile input(temporary.fileName());
            QSaveFile output(destination.filePath());
            if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) return;
            while (!input.atEnd()) {
                const auto bytes = input.read(65536);
                if (bytes.isEmpty() || output.write(bytes) != bytes.size()) return;
            }
            if (input.error() != QFile::NoError || !output.commit()) return;
            error.clear();
            result.failure = PlaylistExportResult::Failure::None;
        },
        [callback = std::move(callback)](quint64, const PlaylistExportResult& result) { callback(result); },
        "redesign-playlist-export");
}

void MediaLib::addMediaTracks(const QUrl& url, const QList<QUrl>& tracks, bool subtitles,
                             QObject* receiver, std::function<void(const TrackAddition&)> callback)
{
    runOnMLThread<TrackAddition>(receiver,
        [url, tracks, subtitles](vlc_medialibrary_t* ml, TrackAddition& result) {
            if (!url.isValid() || url.scheme().isEmpty() ||
                (url.isLocalFile() && !QFileInfo(url.toLocalFile()).isFile())) {
                result.error = qtr("VLC cannot read this media. Select the file again.");
                return;
            }
            QList<QUrl> unique;
            QSet<QUrl> seen;
            for (const auto& track : tracks) {
                QFile file(track.toLocalFile());
                if (!track.isLocalFile() || track == url ||
                    !QFileInfo(file).isFile() || !file.open(QIODevice::ReadOnly)) {
                    result.error = qtr("VLC cannot read this track: %1").arg(track.toDisplayString());
                    return;
                }
                if (!seen.contains(track)) { seen.insert(track); unique.push_back(track); }
            }
            if (unique.isEmpty()) return;
            const auto mrl = url.toEncoded();
            ml_unique_ptr<vlc_ml_media_t> media(vlc_ml_get_media_by_mrl(ml, mrl.constData()));
            if (!media) media.reset(vlc_ml_new_external_media(ml, mrl.constData()));
            if (!media) {
                result.error = qtr("VLC cannot save this change. Try again.");
                return;
            }
            const auto type = subtitles ? VLC_ML_FILE_TYPE_SUBTITLE : VLC_ML_FILE_TYPE_SOUNDTRACK;
            for (const auto& track : unique) {
                bool found = false;
                if (media->p_files) {
                    for (const auto& file : ml_range_iterate<vlc_ml_file_t>(media->p_files)) {
                        if (QUrl(qfu(file.psz_mrl)) != track) continue;
                        if (file.i_type != type) {
                            result.error = qtr("VLC already uses this file for another track: %1").arg(track.fileName());
                            return;
                        }
                        found = true;
                        break;
                    }
                }
                if (found) continue;
                const auto uri = track.toEncoded();
                if (vlc_ml_media_add_external_mrl(ml, media->i_id, uri.constData(), type) != VLC_SUCCESS) {
                    result.error = qtr("VLC cannot save this track: %1").arg(track.fileName());
                    return;
                }
                result.added.push_back(track);
            }
        },
        [callback = std::move(callback)](quint64, const TrackAddition& result) { callback(result); },
        "redesign-media-tracks");
}

void MediaLib::playPodcast(const QUrl& url, bool video, bool fromStart)
{
    runOnMLThread<SharedInputItem>(this,
        [url, video, fromStart](vlc_medialibrary_t* ml, SharedInputItem& item) {
            item.reset(vlc_ml_get_input_item_by_mrl(ml, url.toEncoded().constData()), false);
            if (!item) return;
            if (video) {
                const auto source = vlc::wrap_cptr(input_item_GetMetaExtra(item.get(), "podcast:video"));
                if (!source || !*source) { item.reset(); return; }
                input_item_SetURI(item.get(), source.get());
            }
            input_item_AddOption(item.get(), fromStart ? "restore-playback-pos=0" : "restore-playback-pos=2", VLC_INPUT_OPTION_TRUSTED);
        },
        [this](quint64, const SharedInputItem& item) {
            if (item) m_playlistController->append(QVector<vlc::playlist::Media>{vlc::playlist::Media{item.get()}}, true);
        }, "podcast-play");
}

void MediaLib::setMediaState(const QUrl& url, const QString& field, bool value, QJSValue callback)
{
    if (field != QLatin1String("played") && field != QLatin1String("favorite")) return;
    requestMediaState(url, field == QLatin1String("played") ? MediaStateAction::Played : MediaStateAction::Favorite, value, callback);
}

void MediaLib::requestMediaState(const QUrl& url, MediaStateAction action, bool value, QJSValue callback)
{
    if (!callback.isCallable()) return;
    struct Result { bool success = false; bool played = false; bool favorite = false; QString id; QString error; };
    const auto uri = url.toEncoded();
    const bool valid = url.isValid() && !url.scheme().isEmpty();
    runOnMLThread<Result>(this,
        [uri, valid, action, value](vlc_medialibrary_t* ml, Result& result) {
            result.error = qtr("VLC cannot read this media. Select the file again.");
            if (!valid) return;
            ml_unique_ptr<vlc_ml_media_t> media(vlc_ml_get_media_by_mrl(ml, uri.constData()));
            if (!media && action != MediaStateAction::Read) media.reset(vlc_ml_new_external_media(ml, uri.constData()));
            if (!media && action != MediaStateAction::Read) return;
            if (media) {
                result.error = qtr("VLC cannot save this change. Try again.");
                if (action == MediaStateAction::Played && vlc_ml_media_set_played(ml, media->i_id, value) != VLC_SUCCESS) return;
                if (action == MediaStateAction::Favorite && vlc_ml_media_set_favorite(ml, media->i_id, value) != VLC_SUCCESS) return;
                result.played = action == MediaStateAction::Played ? value : media->i_playcount > 0;
                result.favorite = action == MediaStateAction::Favorite ? value : media->b_is_favorite;
                result.id = MLItemId(media->i_id, VLC_ML_PARENT_UNKNOWN).toString();
            }
            result.success = true;
            result.error.clear();
        },
        [this, callback](quint64, const Result& result) mutable {
            if (auto engine = qjsEngine(this)) {
                const QVariantMap state {{"success", result.success}, {"played", result.played}, {"favorite", result.favorite}, {"id", result.id}, {"error", result.error}};
                callback.call({engine->toScriptValue(state)});
            }
        }, "redesign-media-state");
}

void MediaLib::addLibraryFiles(const QList<QUrl>& urls, QJSValue callback)
{
    if (!callback.isCallable() || urls.isEmpty()) return;
    runOnMLThread<QVariantMap>(this,
        [urls](vlc_medialibrary_t* ml, QVariantMap& result) {
            result = {{"success", false}, {"folders", QStringList{}}, {"error", QString()}};
            QList<QUrl> folders;
            QSet<QString> seen;
            // Validate the entire selection before scheduling discovery. Files
            // stay in place; the library watches their source locations.
            for (const auto& url : urls) {
                QFile file(url.toLocalFile());
                const QFileInfo info(file);
                if (!url.isLocalFile() || !info.isFile() || !file.open(QIODevice::ReadOnly)) {
                    result["error"] = qtr("VLC cannot read this file: %1").arg(url.toDisplayString()); return;
                }
                const auto folder = QUrl::fromLocalFile(info.absolutePath());
                auto key = folder.toString(QUrl::FullyEncoded);
#ifdef Q_OS_WIN
                key = key.toCaseFolded();
#endif
                if (!seen.contains(key)) { seen.insert(key); folders.append(folder); }
            }
            QStringList completed;
            for (const auto& folder : folders) {
                const auto uri = folder.toEncoded();
                if (vlc_ml_add_folder(ml, uri.constData()) != VLC_SUCCESS) {
                    result["folders"] = completed;
                    result["error"] = qtr("VLC cannot add this folder: %1").arg(folder.toDisplayString(QUrl::PreferLocalFile)); return;
                }
                completed.append(folder.toString(QUrl::FullyEncoded));
            }
            result["success"] = true; result["folders"] = completed;
        },
        [this, callback](quint64, const QVariantMap& result) mutable {
            if (auto engine = qjsEngine(this)) callback.call({engine->toScriptValue(result)});
        }, "redesign-library-import");
}

void MediaLib::mlInputItem(const QVector<MLItemId>& itemIdVector, QJSValue callback)
{
    if (!callback.isCallable()) // invalid argument
    {
        msg_Warn(m_intf, "callback is not callbable");
        return;
    }

    struct Ctx {
        std::vector<SharedInputItem> items;
    };

    if (itemIdVector.empty())
    {
        //call the callback with and empty list
        auto jsEngine = qjsEngine(this);
        if (!jsEngine)
            return;
        auto jsArray = jsEngine->newArray(0);
        callback.call({jsArray});
        return;
    }

    auto it = m_inputItemQuery.find(itemIdVector);

    if (it == m_inputItemQuery.end())
    {
        it = m_inputItemQuery.insert(itemIdVector, {callback});
    }
    else
    {
        // be patient

        for (const auto& it2 : it.value())
        {
            if (callback.strictlyEquals(it2))
                return;
        }

        it.value().push_back(callback); // FIXME: Use an ordered set
        return;
    }

    runOnMLThread<Ctx>(this,
    //ML thread
    [itemIdVector](vlc_medialibrary_t* ml, Ctx& ctx){
        for (auto mlId : itemIdVector)
        {
            // NOTE: When we have a parent it's a collection of media(s).
            if (mlId.type == VLC_ML_PARENT_UNKNOWN)
            {
                ctx.items.emplace_back(vlc_ml_get_input_item(ml, mlId.id), false);
            }
            else
            {
                ml_unique_ptr<vlc_ml_media_list_t> list;

                vlc_ml_query_params_t query = vlc_ml_query_params_create();

                list.reset(vlc_ml_list_media_of(ml, &query, mlId.type, mlId.id));

                if (list == nullptr)
                    continue;

                for (const vlc_ml_media_t & media : ml_range_iterate<vlc_ml_media_t>(list))
                    ctx.items.emplace_back(vlc_ml_get_input_item(ml, media.i_id), false);
            }
        }
    },
    //UI thread
    [this, it](quint64, Ctx& ctx) mutable
    {
        auto jsEngine = qjsEngine(this);
        if (!jsEngine)
            return;

        auto jsArray = jsEngine->newArray(ctx.items.size());

        int i  = 0;
        for (const auto& inputItem : ctx.items)
        {
            jsArray.setProperty(i, jsEngine->toScriptValue(inputItem));
            i++;
        }

        for (const auto& cb : std::as_const(it.value()))
        {
            cb.call({jsArray});
        }

        m_inputItemQuery.erase(it);
    });
}

vlc_ml_event_callback_t* MediaLib::registerEventListener( void (*callback)(void*, const vlc_ml_event_t*), void* data)
{
    return vlc_ml_event_register_callback( m_ml , callback , data ) ;
}

void MediaLib::unregisterEventListener(vlc_ml_event_callback_t* cb)
{
    vlc_ml_event_unregister_callback( m_ml , cb );
}

void MediaLib::onMediaLibraryEvent( void* data, const vlc_ml_event_t* event )
{
    MediaLib* self = static_cast<MediaLib*>( data );
    switch ( event->i_type )
    {
        case VLC_ML_EVENT_MEDIA_UPDATED:
        {
            const auto id = MLItemId(event->modification.i_entity_id, VLC_ML_PARENT_UNKNOWN).toString();
            QMetaObject::invokeMethod(self, [self, id]() { emit self->mediaStateChanged(id); });
            break;
        }
        case VLC_ML_EVENT_PARSING_PROGRESS_UPDATED:
        {
            int percent =  event->parsing_progress.i_percent;
            QMetaObject::invokeMethod(self, [self, percent]() {
                self->m_parsingProgress = percent;
                self->emit parsingProgressChanged(percent);
            });
            break;
        }
        case VLC_ML_EVENT_DISCOVERY_STARTED:
        {
            QMetaObject::invokeMethod(self, [self]() {
                self->m_discoveryPending = true;
                self->emit discoveryPendingChanged(self->m_discoveryPending);
                self->emit discoveryStarted();
            });
            break;
        }
        case VLC_ML_EVENT_DISCOVERY_PROGRESS:
        {
            const QUrl entryPoint{ event->discovery_progress.psz_entry_point };
            const QString entryPointStr = entryPoint.isLocalFile()
                                              ? entryPoint.toLocalFile()
                                              : entryPoint.toDisplayString();

            QMetaObject::invokeMethod(self, [self, entryPointStr]() {
                self->m_discoveryEntryPoint = entryPointStr;
                self->emit discoveryEntryPointChanged(entryPointStr);
            });
            break;
        }
        case VLC_ML_EVENT_DISCOVERY_COMPLETED:
        {
            QMetaObject::invokeMethod(self, [self]() {
                self->m_discoveryPending = false;
                self->emit discoveryPendingChanged(self->m_discoveryPending);
                self->emit discoveryCompleted();
            });
            break;
        }
        case VLC_ML_EVENT_BACKGROUND_IDLE_CHANGED:
        {
            bool idle = event->background_idle_changed.b_idle;
            QMetaObject::invokeMethod(self, [self, idle]() {
                self->m_idle = idle;
                self->emit idleChanged();
            });
            break;
        }
        default:
            break;
    }
}

quint64 MediaLib::runOnMLThread(const QObject* obj,
                std::function< void(vlc_medialibrary_t* ml)> mlCb,
                std::function< void()> uiCb,
                const char* queue)
{
    struct NoCtx{};
    return runOnMLThread<NoCtx>(obj,
    [mlCb](vlc_medialibrary_t* ml,NoCtx&){
        mlCb(ml);
    },
    [uiCb](quint64, NoCtx&){
        uiCb();
    },
    queue);
}

quint64 MediaLib::runOnMLThread(const QObject* obj,
                std::function< void(vlc_medialibrary_t* ml)> mlCb,
                std::function< void(quint64)> uiCb, const char* queue)
{
    struct NoCtx{};
    return runOnMLThread<NoCtx>(obj,
    [mlCb](vlc_medialibrary_t* ml,NoCtx&){
        mlCb(ml);
    },
    [uiCb](quint64 requestId, NoCtx&){
        uiCb(requestId);
    },
    queue);
}

quint64 MediaLib::runOnMLThread(const QObject* obj,
                std::function< void(vlc_medialibrary_t* ml)> mlCb,
                const char* queue)
{
    struct NoCtx{};
    return runOnMLThread<NoCtx>(obj,
    [mlCb](vlc_medialibrary_t* ml,NoCtx&){
        mlCb(ml);
    },
    [](quint64, NoCtx&){
    },
    queue);
}

void MediaLib::cancelMLTask(const QObject* object, quint64 taskId)
{
    if (QThread::currentThread()==thread() && m_addonRunner->cancelTask(object,taskId)) return;
    m_runner->cancelTask(object, taskId);
}

