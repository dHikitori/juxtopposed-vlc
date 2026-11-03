/*****************************************************************************
 * Copyright (C) 2021 VLC authors and VideoLAN
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

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include "mlplaylistlistmodel.hpp"

// VLC includes
#include <vlc_media_library.h>
#include "qt.hpp"
#include "util/vlctick.hpp"
#include "dialogs/dialogs_provider.hpp"
#include "playlist/playlist_controller.hpp"

// MediaLibrary includes
#include "mlhelper.hpp"
#include "mlcustomcover.hpp"
#include "mlplaylist.hpp"
#include "util/playlist_cover_store.hpp"
#include <vlc_configuration.h>
#include <vlc_cxx_helpers.hpp>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QSettings>
#include <QUrlQuery>

//-------------------------------------------------------------------------------------------------
// Static variables


namespace  {

// NOTE: We multiply by 3 to cover most dpi settings.
const int MLPLAYLISTMODEL_COVER_WIDTH  = 260 * 3; // 16 / 10 ratio
const int MLPLAYLISTMODEL_COVER_HEIGHT = 162 * 3;

const int PLAYLIST_COVERX = 2;
const int PLAYLIST_COVERY = 2;

QString playlistCoverDirectory()
{
    return PlaylistCoverStore::directory();
}

void removePlaylistCover(int64_t id)
{
    const auto directory = playlistCoverDirectory();
    if (directory.isEmpty()) return;
    const auto writerLock = PlaylistCoverStore::lock();
    if (!writerLock) return;
    QSettings covers(QDir(directory).filePath(QStringLiteral("covers.ini")), QSettings::IniFormat);
    const auto prefix = QString::number(id) + QLatin1Char('-');
    for (const auto& key : covers.allKeys()) {
        if (!key.startsWith(prefix)) continue;
        const auto filename = covers.value(key).toString();
        if (filename == QFileInfo(filename).fileName()) QFile::remove(QDir(directory).filePath(filename));
        covers.remove(key);
    }
}

void appendMediaIntoPlaylist(vlc_medialibrary_t* ml, int64_t playlistId, const std::vector<MLItemId>& itemList)
{
    vlc_ml_query_params_t query = vlc_ml_query_params_create();

    for (auto itemId : itemList)
    {
        // NOTE: When we have a parent it's a collection of media(s).
        if (itemId.type != VLC_ML_PARENT_UNKNOWN)
        {
            ml_unique_ptr<vlc_ml_media_list_t> list;

            list.reset(vlc_ml_list_media_of(ml, &query, itemId.type, itemId.id));

            if (!list)
                continue;

            for (const vlc_ml_media_t & media : ml_range_iterate<vlc_ml_media_t>(list))
                vlc_ml_playlist_append(ml, playlistId, &media.i_id, 1);
        }
        // NOTE: Otherwise we add the media directly.
        else
            vlc_ml_playlist_append(ml, playlistId, &itemId.id, 1);
    }
}

} //anonymous namespace

//=================================================================================================
// MLPlaylistListModel
//=================================================================================================

/* explicit */ MLPlaylistListModel::MLPlaylistListModel(QObject * parent)
    : MLBaseModel(parent)
    , m_coverSize(MLPLAYLISTMODEL_COVER_WIDTH, MLPLAYLISTMODEL_COVER_HEIGHT)
{
    m_coverRefreshTimer.setSingleShot(true);
    m_coverRefreshTimer.setInterval(100);
    connect(&m_coverRefreshTimer, &QTimer::timeout, this, &MLPlaylistListModel::refreshCovers);
    connect(this, &MLPlaylistListModel::coverSizeChanged, this, &MLPlaylistListModel::refreshCovers);
    connect(this, &MLPlaylistListModel::coverDefaultChanged, this, &MLPlaylistListModel::refreshCovers);
    connect(this, &MLBaseModel::mlChanged, this, [this] {
        disconnect(m_detailsConnection);
        m_detailsConnection = connect(m_mediaLib, &MediaLib::playlistDetailsChanged, this,
            [this](const MLItemId& id) {
                if (m_transactionPending) m_resetAfterTransaction = true;
                else updateItemInCache(id);
            });
    });
}

//-------------------------------------------------------------------------------------------------

/* Q_INVOKABLE */ void MLPlaylistListModel::create(const QString & name, const QVariantList& initialItems)
{
    assert(m_mediaLib);

    struct Ctx {
        MLItemId createdPlaylistId;
    };

    setTransactionPending(true);

    m_mediaLib->runOnMLThread<Ctx>(this,
    //ML thread
    [name](vlc_medialibrary_t* ml, Ctx& ctx)
    {
        vlc_ml_playlist_t * playlist = vlc_ml_playlist_create(ml, qtu(name));
        if (playlist)
        {
            ctx.createdPlaylistId = MLItemId(playlist->i_id, VLC_ML_PARENT_UNKNOWN);
            vlc_ml_playlist_release(playlist);
        }
    },
    [this, initialItems](quint64, const Ctx& ctx) {
        endTransaction(); // this is intentionally called here and not after `append()`

        if (ctx.createdPlaylistId.id && !initialItems.isEmpty())
        {
            append(ctx.createdPlaylistId, initialItems);
        }
    });
}

void MLPlaylistListModel::createWithFiles(const QString& name, const QStringList& urls, const QUrl& cover)
{
    if (name.trimmed().isEmpty() || !setTransactionPending(true)) {
        emit playlistCreated(false, {}, qtr("Enter a playlist name, then try again."));
        return;
    }
    struct Result { MLItemId id; QString error; };
    m_mediaLib->runOnMLThread<Result>(this,
        [name = name.trimmed(), urls, cover](vlc_medialibrary_t* ml, Result& result) {
            result.error = qtr("Hakone cannot save this playlist. Try again.");
            QString coverPath;
            bool saved = false;
            const auto cleanup = qScopeGuard([&] { if (!saved && !coverPath.isEmpty()) QFile::remove(coverPath); });
            if (!cover.isEmpty()) {
                coverPath = PlaylistCoverStore::prepareImage(cover);
                if (coverPath.isEmpty()) {
                    result.error = qtr("Hakone cannot read or save this image. Select another image.");
                    return;
                }
            }
            std::vector<int64_t> ids;
            for (const auto& text : urls) {
                // Qt drops expose readable URLs; file pickers return encoded URLs.
                const QUrl url(text, QUrl::TolerantMode);
                if (!url.isValid() || url.scheme().isEmpty()) return;
                const auto uri = url.toEncoded();
                ml_unique_ptr<vlc_ml_media_t> media(vlc_ml_get_media_by_mrl(ml, uri.constData()));
                if (!media) media.reset(vlc_ml_new_external_media(ml, uri.constData()));
                if (!media) return;
                ids.push_back(media->i_id);
            }
            ml_unique_ptr<vlc_ml_playlist_t> playlist(vlc_ml_playlist_create(ml, qtu(name)));
            if (!playlist) return;
            const auto rollback = qScopeGuard([&] { if (!saved) vlc_ml_playlist_delete(ml, playlist->i_id); });
            if (!ids.empty() && vlc_ml_playlist_append(ml, playlist->i_id, ids.data(), ids.size()) != VLC_SUCCESS) return;
            if (!coverPath.isEmpty()) {
                const auto writerLock = PlaylistCoverStore::lock();
                if (!writerLock) return;
                auto metadata = PlaylistCoverStore::stage(playlist->i_id, playlist->i_creation_date, coverPath);
                if (!metadata || !metadata->commit()) return;
            }
            saved = true;
            result.id = MLItemId(playlist->i_id, VLC_ML_PARENT_PLAYLIST);
            result.error.clear();
        },
        [this](quint64, Result& result) {
            endTransaction();
            emit resetRequested();
            emit playlistCreated(result.error.isEmpty(), result.id, result.error);
        });
}

void MLPlaylistListModel::saveSelection(const MLItemId& playlistId, const QString& newName, const QVariantList& items)
{
    if (transactionPending()) return;
    const auto name = newName.trimmed();
    if (items.isEmpty() || (!playlistId.id && name.isEmpty())) {
        emit selectionSaved(false, {}, qtr("Select a playlist or enter a new name."));
        return;
    }
    // Resolve input items while their caller is alive. The worker receives only
    // value types, and keeps the selection's order even when sources are mixed.
    QVariantList sources;
    for (const auto& item : items) {
        if (item.canConvert<MLItemId>()) sources.push_back(item);
        else {
            const auto media = vlc::playlist::toMediaList({item});
            if (media.size() != 1 || !media.front().raw()->psz_uri) {
                emit selectionSaved(false, {}, qtr("Hakone cannot read this selection. Select the files again."));
                return;
            }
            sources.push_back(qfu(media.front().raw()->psz_uri));
        }
    }
    setTransactionPending(true);
    struct Result { MLItemId id; QString error = qtr("Hakone cannot save this selection. Try again."); };
    m_mediaLib->runOnMLThread<Result>(this,
        [playlistId, name, sources](vlc_medialibrary_t* ml, Result& result) {
            ml_unique_ptr<vlc_ml_playlist_t> playlist;
            const bool create = !name.isEmpty();
            if (!create) {
                playlist.reset(vlc_ml_get_playlist(ml, playlistId.id));
                if (!playlist) return;
                if (playlist->b_is_read_only) { result.error = qtr("You cannot change this playlist."); return; }
            }
            std::vector<int64_t> ids;
            auto query = vlc_ml_query_params_create();
            for (const auto& source : sources) {
                if (source.canConvert<MLItemId>()) {
                    const auto id = source.value<MLItemId>();
                    if (!id.id) return;
                    if (id.hasParent()) {
                        ml_unique_ptr<vlc_ml_media_list_t> media(id.type == VLC_ML_PARENT_PLAYLIST
                            ? vlc_ml_list_playlist_media(ml, &query, id.id)
                            : vlc_ml_list_media_of(ml, &query, id.type, id.id));
                        if (!media) return;
                        for (const auto& entry : ml_range_iterate<vlc_ml_media_t>(media)) ids.push_back(entry.i_id);
                    } else {
                        ml_unique_ptr<vlc_ml_media_t> media(vlc_ml_get_media(ml, id.id));
                        if (!media) return;
                        ids.push_back(media->i_id);
                    }
                } else {
                    const auto uri = QUrl(source.toString(), QUrl::TolerantMode).toEncoded();
                    ml_unique_ptr<vlc_ml_media_t> media(vlc_ml_get_media_by_mrl(ml, uri.constData()));
                    if (!media) media.reset(vlc_ml_new_external_media(ml, uri.constData()));
                    if (!media) return;
                    ids.push_back(media->i_id);
                }
            }
            if (ids.empty()) { result.error = qtr("This selection has no media to add."); return; }
            if (create) playlist.reset(vlc_ml_playlist_create(ml, qtu(name)));
            if (!playlist) return;
            bool saved = false;
            const auto rollback = qScopeGuard([&] { if (create && !saved) vlc_ml_playlist_delete(ml, playlist->i_id); });
            if (vlc_ml_playlist_append(ml, playlist->i_id, ids.data(), ids.size()) != VLC_SUCCESS) return;
            saved = true;
            result.id = MLItemId(playlist->i_id, VLC_ML_PARENT_PLAYLIST);
            result.error.clear();
        },
        [this](quint64, const Result& result) {
            endTransaction();
            emit resetRequested();
            emit selectionSaved(result.error.isEmpty(), result.id, result.error);
        });
}

/* Q_INVOKABLE */ void MLPlaylistListModel::append(const MLItemId     & playlistId,
                                                   const QVariantList & ids)
{
    assert(m_mediaLib);
    if (ids.isEmpty()) return;

    if (unlikely(!setTransactionPending(true)))
        return;

    m_mediaLib->runOnMLThread(this,
    //ML thread
    [playlistId, ids](vlc_medialibrary_t* ml) {
        std::vector<MLItemId> mlItemIdVector;
        mlItemIdVector.reserve(ids.size());

        if (ids.at(0).canConvert<MLItemId>())
        {
            for (const QVariant& id : ids)
            {
                // Do not mix types. This is currently not possible
                // in the application anyway.
                assert(id.canConvert<MLItemId>());

                mlItemIdVector.push_back(id.value<MLItemId>());
            }

            appendMediaIntoPlaylist(ml, playlistId.id, mlItemIdVector);
        }
        else
        {
            QVector<vlc::playlist::Media> medias = vlc::playlist::toMediaList(ids);

            for (const auto& media : medias)
            {
                assert(media.raw());

                const char * const uri = media.raw()->psz_uri;

                vlc_ml_media_t * ml_media = vlc_ml_get_media_by_mrl(ml, uri);

                if (ml_media == nullptr)
                {
                    ml_media = vlc_ml_new_external_media(ml, uri);
                    if (ml_media == nullptr)
                        continue;
                }

                vlc_ml_playlist_append(ml, playlistId.id, &ml_media->i_id, 1);
                vlc_ml_media_release(ml_media);
            }
        }
    },
    //UI thread
    [this]() {
        endTransaction();
    });
}

/* Q_INVOKABLE */ bool MLPlaylistListModel::deletePlaylists(const QVariantList & ids)
{
    assert(m_mediaLib);

    if (unlikely(!setTransactionPending(true)))
        return false;

    std::vector<MLItemId> itemList;
    for (const QVariant & id : ids)
    {
        if (id.canConvert<MLItemId>() == false)
            continue;

        const MLItemId & itemId = id.value<MLItemId>();

        if (itemId.id == 0)
            continue;

        itemList.push_back(itemId);
    }

    m_mediaLib->runOnMLThread(this,
    //ML thread
    [itemList](vlc_medialibrary_t* ml)
    {
        for (auto itemId : itemList)
            if (vlc_ml_playlist_delete(ml, itemId.id) == VLC_SUCCESS) removePlaylistCover(itemId.id);
    },
    //UI thread
    [this](){
        endTransaction();
    });

    return true;
}

//-------------------------------------------------------------------------------------------------

/* Q_INVOKABLE */ bool MLPlaylistListModel::showDialogRename(const QModelIndex & index)
{
    int row = index.row();

    if (row < 0 || row >= rowCount())
        return false;

    MLPlaylist * playlist = static_cast<MLPlaylist *> (item(row));

    QString name = playlist->getName();

    auto *provider = DialogsProvider::getInstance();

    bool ok = false;

    QString result = provider->getTextDialog(NULL, qtr("Rename playlist"),
                                             qtr("Please enter the new playlist name:"),
                                             name, &ok).toString();

    if (ok == false || result.isEmpty() || result == name)
        return false;

    int64_t id = playlist->getId().id;

    m_mediaLib->runOnMLThread(this,
    // ML thread
    [id, result](vlc_medialibrary_t * ml)
    {
        vlc_ml_playlist_rename(ml, id, qtu(result));
    });

    // NOTE: We want the change to be visible right away.
    playlist->setName(result);

    emit dataChanged(index, index, { PLAYLIST_NAME });

    return true;
}

//-------------------------------------------------------------------------------------------------

/* Q_INVOKABLE */ MLItemId MLPlaylistListModel::getItemId(int index) const
{
    if (index < 0 || index >= rowCount())
        return MLItemId();

    return item(index)->getId();
}

//-------------------------------------------------------------------------------------------------
// QAbstractItemModel implementation
//-------------------------------------------------------------------------------------------------

QHash<int, QByteArray> MLPlaylistListModel::roleNames() const /* override */
{
    return
    {
        { PLAYLIST_ID,        QByteArrayLiteral("id")         },
        { PLAYLIST_NAME,      QByteArrayLiteral("name")       },
        { PLAYLIST_THUMBNAIL, QByteArrayLiteral("thumbnail")  },
        { PLAYLIST_DURATION,  QByteArrayLiteral("duration")   },
        { PLAYLIST_COUNT,     QByteArrayLiteral("count")      },
        { PLAYLIST_NB_AUDIO,  QByteArrayLiteral("nb_audio")   },
        { PLAYLIST_NB_VIDEO,  QByteArrayLiteral("nb_video")   },
        { PLAYLIST_NB_UNKNOWN,QByteArrayLiteral("nb_unknown") },
        { PLAYLIST_READ_ONLY, QByteArrayLiteral("readOnly")   },
        { PLAYLIST_FAVORITE,  QByteArrayLiteral("favorite")   }
    };
}

QVariant MLPlaylistListModel::itemRoleData(const MLItem *item, int role) const /* override */
{
    auto playlist = static_cast<const MLPlaylist *>(item);
    if (playlist == nullptr)
        return QVariant();

    switch (role)
    {
        // NOTE: This is the condition for QWidget view(s).
        case Qt::DisplayRole:
            return playlist->getName();
        // NOTE: These are the conditions for QML view(s).
        case PLAYLIST_ID:
            return QVariant::fromValue(playlist->getId());
        case PLAYLIST_NAME:
            return playlist->getName();
        case PLAYLIST_READ_ONLY:
            return playlist->readOnly();
        case PLAYLIST_FAVORITE:
            return playlist->favorite();
        case PLAYLIST_THUMBNAIL:
            return getCover(playlist);
        case PLAYLIST_DURATION:
            return QVariant::fromValue(playlist->getDuration());
        case PLAYLIST_COUNT:
            return QVariant::fromValue(playlist->getCount());
        case PLAYLIST_NB_AUDIO:
            return QVariant::fromValue(playlist->getNbAudio());
        case PLAYLIST_NB_VIDEO:
            return QVariant::fromValue(playlist->getNbVideo());
        case PLAYLIST_NB_UNKNOWN:
            return QVariant::fromValue(playlist->getNbUnknown());
        default:
            return QVariant();
    }
}

//-------------------------------------------------------------------------------------------------
// QAbstractItemModel reimplementation
//-------------------------------------------------------------------------------------------------

QVariant MLPlaylistListModel::headerData(int section, Qt::Orientation orientation,
                                         int role) const /* override */
{
    if (role != Qt::DisplayRole || orientation == Qt::Vertical)
        return QVariant();

    if (section == 0)
        return QVariant::fromValue(qtr("Name"));
    else
        return QVariant();
}

//-------------------------------------------------------------------------------------------------
// Protected MLBaseModel implementation
//-------------------------------------------------------------------------------------------------

std::unique_ptr<MLListCacheLoader> MLPlaylistListModel::createMLLoader() const /* override */
{
    return std::make_unique<MLListCacheLoader>(m_mediaLib, std::make_shared<MLPlaylistListModel::Loader>(*this, m_playlistType));
}

//-------------------------------------------------------------------------------------------------
// Private functions
//-------------------------------------------------------------------------------------------------

void MLPlaylistListModel::endTransaction()
{
    setTransactionPending(false);
}

bool MLPlaylistListModel::setTransactionPending(const bool value)
{
    if (m_transactionPending == value)
        return false;

    m_transactionPending = value;

    if (!value)
    {
        if (m_resetAfterTransaction)
        {
            m_resetAfterTransaction = false;
            emit resetRequested();
        }
    }

    emit transactionPendingChanged(value);
    return true;
}

QString MLPlaylistListModel::getCover(const MLPlaylist * playlist) const
{
    const auto cover = PlaylistCoverStore::cover(playlist->getId().id, playlist->creationDate());
    if (!cover.isEmpty()) return cover.toString(QUrl::FullyEncoded);
    if (!playlist->artwork().isEmpty()) return playlist->artwork();
    if (playlist->getCount() == 0) return {};
    QUrl url(MLCustomCover::url(playlist->getId()
                            , m_coverSize
                            , m_coverDefault
                            , PLAYLIST_COVERX, PLAYLIST_COVERY));
    QUrlQuery query(url);
    query.addQueryItem(QStringLiteral("revision"), QString::number(m_coverRevision));
    url.setQuery(query);
    return url.toString();
}

//-------------------------------------------------------------------------------------------------
// Private MLBaseModel reimplementation
//-------------------------------------------------------------------------------------------------

void MLPlaylistListModel::refreshCovers()
{
    // Update only artwork bindings; keep playlist selection and cached rows.
    ++m_coverRevision;
    const int rows = rowCount();
    if (rows > 0)
        emit dataChanged(index(0), index(rows - 1), {PLAYLIST_THUMBNAIL});
}

void MLPlaylistListModel::onVlcMlEvent(const MLEvent & event) /* override */
{
    if (event.i_type == VLC_ML_EVENT_PLAYLIST_UPDATED)
        ++m_coverRevision;

    if (m_transactionPending
        && (event.i_type == VLC_ML_EVENT_PLAYLIST_UPDATED
            || event.i_type == VLC_ML_EVENT_PLAYLIST_DELETED
            || event.i_type == VLC_ML_EVENT_PLAYLIST_ADDED))
    {
        m_resetAfterTransaction = true;
        return;
    }

    switch (event.i_type)
    {
    case VLC_ML_EVENT_MEDIA_THUMBNAIL_GENERATED:
        if (event.media_thumbnail_generated.b_success
            && event.media_thumbnail_generated.i_size == VLC_ML_THUMBNAIL_SMALL)
            m_coverRefreshTimer.start();
        break;
    case VLC_ML_EVENT_FAVORITES_CHANGED:
        if (event.favorites_changed.i_entity_type == VLC_ML_PARENT_PLAYLIST)
            updateItemInCache(MLItemId(event.favorites_changed.i_entity_id, VLC_ML_PARENT_PLAYLIST));
        break;
    case VLC_ML_EVENT_PLAYLIST_DELETED:
    {
        MLItemId itemId(event.deletion.i_entity_id, VLC_ML_PARENT_PLAYLIST);
        deleteItemInCache(itemId);
        return;
    }
    case VLC_ML_EVENT_PLAYLIST_UPDATED:
    {

        MLItemId itemId(event.modification.i_entity_id, VLC_ML_PARENT_PLAYLIST);
        updateItemInCache(itemId);
        return;
    }
    case VLC_ML_EVENT_PLAYLIST_ADDED:
    {
        emit resetRequested();
        break;
    }
    default:
        break;
    }

    MLBaseModel::onVlcMlEvent(event);
}

//-------------------------------------------------------------------------------------------------
// Properties
//-------------------------------------------------------------------------------------------------

QSize MLPlaylistListModel::coverSize() const
{
    return m_coverSize;
}

void MLPlaylistListModel::setCoverSize(const QSize & size)
{
    if (m_coverSize == size)
        return;

    m_coverSize = size;

    emit coverSizeChanged();
}

QString MLPlaylistListModel::coverDefault() const
{
    return m_coverDefault;
}

void MLPlaylistListModel::setCoverDefault(const QString & fileName)
{
    if (m_coverDefault == fileName)
        return;

    m_coverDefault = fileName;

    emit coverDefaultChanged();
}

QString MLPlaylistListModel::coverPrefix() const
{
    return m_coverPrefix;
}

void MLPlaylistListModel::setCoverPrefix(const QString & prefix)
{
    if (m_coverPrefix == prefix)
        return;

    m_coverPrefix = prefix;

    emit coverPrefixChanged();
}

MLPlaylistListModel::PlaylistType MLPlaylistListModel::playlistType() const
{
    return m_playlistType;
}

void MLPlaylistListModel::setPlaylistType(PlaylistType playlistType)
{
    if (m_playlistType == playlistType)
        return;
    m_playlistType = playlistType;
    resetCache();
    emit playlistTypeChanged();
}

//=================================================================================================
// Loader
//=================================================================================================

static inline vlc_ml_playlist_type_t qmlToMLPlaylistType(MLPlaylistListModel::PlaylistType type)
{
    switch(type) {
    case MLPlaylistListModel::PlaylistType::PLAYLIST_TYPE_ALL:
        return VLC_ML_PLAYLIST_TYPE_ALL;
    case MLPlaylistListModel::PlaylistType::PLAYLIST_TYPE_AUDIO:
        return VLC_ML_PLAYLIST_TYPE_AUDIO;
    case MLPlaylistListModel::PlaylistType::PLAYLIST_TYPE_VIDEO:
        return VLC_ML_PLAYLIST_TYPE_VIDEO;
    case MLPlaylistListModel::PlaylistType::PLAYLIST_TYPE_AUDIO_ONLY:
        return VLC_ML_PLAYLIST_TYPE_AUDIO_ONLY;
    case MLPlaylistListModel::PlaylistType::PLAYLIST_TYPE_VIDEO_ONLY:
        return VLC_ML_PLAYLIST_TYPE_VIDEO_ONLY;
    default:
        vlc_assert_unreachable();
    }
}

MLPlaylistListModel::Loader::Loader(const MLPlaylistListModel & model, PlaylistType playlistType)
    : MLListCacheLoader::MLOp(model)
    , m_playlistType(playlistType)
{}

size_t MLPlaylistListModel::Loader::count(vlc_medialibrary_t* ml, const vlc_ml_query_params_t* queryParams) const /* override */
{
    vlc_ml_playlist_type_t mlPlaylistType = qmlToMLPlaylistType(m_playlistType);

    return vlc_ml_count_playlists(ml, queryParams, mlPlaylistType);
}

std::vector<std::unique_ptr<MLItem>>
MLPlaylistListModel::Loader::load(vlc_medialibrary_t* ml, const vlc_ml_query_params_t* queryParams) const /* override */
{
    vlc_ml_playlist_type_t mlPlaylistType = qmlToMLPlaylistType(m_playlistType);

    ml_unique_ptr<vlc_ml_playlist_list_t> list(vlc_ml_list_playlists(ml, queryParams, mlPlaylistType));

    if (list == nullptr)
        return {};

    std::vector<std::unique_ptr<MLItem>> result;

    for (const vlc_ml_playlist_t & playlist : ml_range_iterate<vlc_ml_playlist_t>(list))
    {
        result.emplace_back(std::make_unique<MLPlaylist>(&playlist));
    }

    return result;
}

std::unique_ptr<MLItem>
MLPlaylistListModel::Loader::loadItemById(vlc_medialibrary_t* ml, MLItemId itemId) const
{
    assert(itemId.type == VLC_ML_PARENT_PLAYLIST);
    ml_unique_ptr<vlc_ml_playlist_t> playlist(vlc_ml_get_playlist(ml, itemId.id));
    if (!playlist)
        return nullptr;
    return std::make_unique<MLPlaylist>(playlist.get());
}
