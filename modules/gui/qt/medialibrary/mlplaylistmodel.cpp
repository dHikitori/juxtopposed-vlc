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

#include "mlplaylistmodel.hpp"

// Util includes
#include "util/shared_input_item.hpp"
#include "util/vlctick.hpp"

// MediaLibrary includes
#include "mlhelper.hpp"
#include "mlplaylistmedia.hpp"

#include "playlist/playlist_controller.hpp"
#include "playlist/media.hpp"

//=================================================================================================
// MLPlaylistModel
//=================================================================================================

/* explicit */ MLPlaylistModel::MLPlaylistModel(QObject * parent)
    : MLBaseModel(parent)
{
    connect(this, &MLBaseModel::parentIdChanged, this, [this] { m_requestedThumbnails.clear(); });
}

//-------------------------------------------------------------------------------------------------
// Interface
//-------------------------------------------------------------------------------------------------

void MLPlaylistModel::appendFiles(const QStringList& urls)
{
    const auto playlistId = parentId();
    if (m_transactionPending || !playlistId.id || urls.isEmpty()) return;
    setTransactionPending(true);
    struct Result { bool success = false; QString error; };
    m_mediaLib->runOnMLThread<Result>(this,
        [playlistId, urls](vlc_medialibrary_t* ml, Result& result) {
            ml_unique_ptr<vlc_ml_playlist_t> playlist(vlc_ml_get_playlist(ml, playlistId.id));
            if (!playlist) return;
            if (playlist->b_is_read_only) { result.error = qtr("You cannot change this playlist."); return; }
            std::vector<int64_t> ids;
            for (const auto& text : urls) {
                const QUrl url(text, QUrl::TolerantMode);
                if (!url.isValid() || url.scheme().isEmpty()) return;
                const auto uri = url.toEncoded();
                ml_unique_ptr<vlc_ml_media_t> media(vlc_ml_get_media_by_mrl(ml, uri.constData()));
                if (!media) media.reset(vlc_ml_new_external_media(ml, uri.constData()));
                if (!media) return;
                ids.push_back(media->i_id);
            }
            result.success = vlc_ml_playlist_append(ml, playlistId.id, ids.data(), ids.size()) == VLC_SUCCESS;
        },
        [this, playlistId](quint64, Result& result) {
            endTransaction();
            if (parentId() == playlistId) emit resetRequested();
            if (!result.success && result.error.isEmpty()) result.error = qtr("Hakone cannot add these files. Try again.");
            emit itemsEdited(playlistId, result.success, result.error);
        });
}

void MLPlaylistModel::editItem(int row, int destination, bool remove, const MLItemId& expected, const MLItemId& playlistId)
{
    if (m_transactionPending) return;
    const auto reject = [this, playlistId] {
        emit itemsEdited(playlistId, false, qtr("The playlist changed. Use playlist order and try again."));
    };
    if (!playlistId.id || playlistId != parentId() || !expected.id || row < 0
            || row >= rowCount() || (!remove && (destination < 0 || destination >= rowCount()))
            || (getSortCriteria() != QString {} && getSortCriteria() != QStringLiteral("id"))
            || getMLSortingCriteria() != VLC_ML_SORTING_DEFAULT || getSortOrder() != Qt::AscendingOrder
            || !searchPattern().isEmpty() || getOffset() != 0 || getFavoriteOnly()) { reject(); return; }
    const auto entry = item(row);
    if (!entry || entry->getId() != expected) { reject(); return; }
    if (!remove && row == destination) return;
    setTransactionPending(true);
    struct Result { bool success = false; QString error; };
    m_mediaLib->runOnMLThread<Result>(this,
        [playlistId, expected, row, destination, remove](vlc_medialibrary_t* ml, Result& result) {
            ml_unique_ptr<vlc_ml_playlist_t> playlist(vlc_ml_get_playlist(ml, playlistId.id));
            if (!playlist) return;
            if (playlist->b_is_read_only) { result.error = qtr("You cannot change this playlist."); return; }
            if (static_cast<unsigned>(row) >= playlist->i_nb_media
                || (!remove && static_cast<unsigned>(destination) >= playlist->i_nb_media)) return;
            auto query = vlc_ml_query_params_create();
            query.i_offset = row; query.i_nbResults = 1;
            ml_unique_ptr<vlc_ml_media_list_t> media(vlc_ml_list_playlist_media(ml, &query, playlistId.id));
            if (!media || media->i_nb_items != 1 || media->p_items[0].i_id != expected.id) return;
            result.success = (remove ? vlc_ml_playlist_remove(ml, playlistId.id, row, 1)
                : vlc_ml_playlist_move(ml, playlistId.id, row, destination, 1)) == VLC_SUCCESS;
        },
        [this, playlistId](quint64, Result& result) {
            endTransaction();
            if (parentId() == playlistId) emit resetRequested();
            if (!result.success && result.error.isEmpty()) result.error = qtr("Hakone cannot change this playlist. Try again.");
            emit itemsEdited(playlistId, result.success, result.error);
        });
}

/* Q_INVOKABLE */ void MLPlaylistModel::insert(const QVariantList & items, int at)
{
    assert(m_mediaLib);

    int64_t id = parentId().id;

    assert(id);

    if (unlikely(m_transactionPending))
        return;

    QVector<vlc::playlist::Media> medias = vlc::playlist::toMediaList(items);

    setTransactionPending(true);

    struct Ctx {
        std::vector<std::unique_ptr<MLItem>> medias;
    };
    m_mediaLib->runOnMLThread<Ctx>(this,
    //ML thread
    [medias, id, at](vlc_medialibrary_t* ml, Ctx& ctx) {
        std::vector<int64_t> mediaIdList;
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
            mediaIdList.push_back(ml_media->i_id);
            ctx.medias.emplace_back(std::make_unique<MLPlaylistMedia>(ml_media));
            vlc_ml_media_release(ml_media);
        }
        if (mediaIdList.size() == 0)
            return;

        vlc_ml_playlist_insert(ml, id, mediaIdList.data(), mediaIdList.size(), at);
    },
    //UI thread
    [this, at](quint64, Ctx& ctx) {
        insertItemListInCache(std::move(ctx.medias), at);
        m_need_reset = true;
        endTransaction();
    });
}


void MLPlaylistModel::moveImpl(int64_t playlistId, HighLowRanges&& ranges)
{
    struct Ctx {
        int newTo;
    };
    int low;
    int high;
    if (ranges.lowRangeIt > 0)
    {
        std::tie(low, high) = ranges.lowRanges[ranges.lowRangeIt - 1];

        assert(low <= high);
        m_mediaLib->runOnMLThread<Ctx>(this,
        //ML thread
        [playlistId, high, low, to = ranges.lowTo]
        (vlc_medialibrary_t* ml, Ctx& ctx) {
            int nbElement = high - low + 1;
            vlc_ml_playlist_move(ml, playlistId, low, to - 1, nbElement);
            ctx.newTo = to - nbElement;
        },
        //UI thread
        [this, playlistId, high, low, r = std::move(ranges)](quint64, Ctx& ctx) mutable {
            moveRangeInCache(low, high, r.lowTo);
            r.lowTo = ctx.newTo;
            --r.lowRangeIt;
            moveImpl(playlistId, std::move(r));
        });
    }
    else if (ranges.highRangeIt < ranges.highRanges.size())
    {
        std::tie(low, high) = ranges.highRanges[ranges.highRangeIt];
        assert(low <= high);

        m_mediaLib->runOnMLThread<Ctx>(this,
        //ML thread
        [playlistId, high, low, to = ranges.highTo](vlc_medialibrary_t* ml, Ctx& ctx) {
            int nbElement = high - low + 1;
            vlc_ml_playlist_move(ml, playlistId, low, to, nbElement);
            ctx.newTo = to + nbElement;
        },
        //UI thread
        [this, playlistId, low, high, r = std::move(ranges)](quint64, Ctx& ctx) mutable {
            moveRangeInCache(low, high, r.highTo);
            r.highTo = ctx.newTo;
            ++r.highRangeIt;
            moveImpl(playlistId, std::move(r));
        });
    }
    else
    {
        //we're done
        endTransaction();
    }
}


/**
 * the move operation is done in separate phases
 * * we sort the items, and organise them as "ranges" of consecutive items to
 *   be move (think beginMoveRows/endMoveRows)
 *
 * * and split ranges between thoose who are before and
 *   those who are after the destination point
 *
 * * for each range to move, we will
 *    * beginMoveRows(range)
 *    * move the items in the range on ML thread
 *    * endMoveRows on the UI thread
 *    * move the next range
 *
 *  * the ranges before the destination needs to be moved from the closest to the
 *    destination point to the farthest, so this doesn't mess the indexe, the
 *    same policy applies when moving the items from a given range for the same
 *    reason. Having the data split in before/after the destination point, allows to
 *    do it sequentially without having to guess what is the next segment to move
 */
/* Q_INVOKABLE */ void MLPlaylistModel::move(const QModelIndexList & indexes, int to)
{
    assert(m_mediaLib);

    if (indexes.size() == 0)
        return;

    if (unlikely(m_transactionPending))
        return;

    int64_t id = parentId().id;
    assert(id);
    //get Ranges in asc order
    auto rangeList = getSortedRowsRanges(indexes, true);
    assert(rangeList.size() > 0);

    //split ranges between those who are above and those who below the destination
    HighLowRanges highLowRanges;
    highLowRanges.lowTo = to;
    highLowRanges.highTo = to;

    auto it = rangeList.cbegin();
    while (it != rangeList.end())
    {
        if (it->second < to - 1)
            highLowRanges.lowRanges.push_back(*it);
        else if (it->first > to)
            highLowRanges.highRanges.push_back(*it);
        else
        {
            //range is overlapping destination, it doesn't need to be moved
            highLowRanges.lowTo = it->first;
            highLowRanges.highTo = it->second;
        }
        ++it;
    }
    highLowRanges.lowRangeIt = highLowRanges.lowRanges.size();
    highLowRanges.highRangeIt = 0;

    setTransactionPending(true);

    moveImpl(id, std::move(highLowRanges));
}


void MLPlaylistModel::removeImpl(int64_t playlistId, const std::vector<std::pair<int, int> >&& rangeList, size_t index)
{
    if (index >= rangeList.size())
    {
        //we're done
        endTransaction();
        return;
    }

    std::pair<int, int> range = rangeList[index];

    m_mediaLib->runOnMLThread(this,
    //ML thread
    [playlistId, range](vlc_medialibrary_t* ml) {
        vlc_ml_playlist_remove(ml, playlistId, range.first, range.second - range.first + 1);
    },
    //UI thread
    [this, playlistId, range, rows = std::move(rangeList), index]() {
        deleteRangeInCache(range.first, range.second);
        removeImpl(playlistId, std::move(rows), index+1);
    });
}

/* Q_INVOKABLE */ void MLPlaylistModel::remove(const QModelIndexList & indexes)
{
    assert(m_mediaLib);
    if (indexes.size() == 0)
        return;

    if (unlikely(m_transactionPending))
        return;

    int64_t id = parentId().id;
    assert(id);

    //get range in decreasing order to avoid having to update the index
    auto rangeList = getSortedRowsRanges(indexes, false);
    assert(rangeList.size() > 0);

    setTransactionPending(true);
    removeImpl(id, std::move(rangeList), 0);
}

void MLPlaylistModel::endTransaction()
{
    setTransactionPending(false);
}

void MLPlaylistModel::setTransactionPending(bool value)
{
    if (m_transactionPending == value)
        return;

    m_transactionPending = value;

    if (!value)
    {
        if (m_resetAfterTransaction)
        {
            m_resetAfterTransaction = false;
            emit resetRequested();
        }
    }

    emit transactionPendingChanged();
}

//-------------------------------------------------------------------------------------------------
// QAbstractItemModel implementation
//-------------------------------------------------------------------------------------------------

QHash<int, QByteArray> MLPlaylistModel::roleNames() const /* override */
{
    return
    {
        { MEDIA_ID,                 QByteArrayLiteral("id")                 },
        { MEDIA_IS_NEW,             QByteArrayLiteral("isNew")              },
        { MEDIA_TITLE,              QByteArrayLiteral("title")              },
        { MEDIA_THUMBNAIL,          QByteArrayLiteral("thumbnail")          },
        { MEDIA_DURATION,           QByteArrayLiteral("duration")           },
        { MEDIA_PROGRESS,           QByteArrayLiteral("progress")           },
        { MEDIA_PLAYCOUNT,          QByteArrayLiteral("playcount")          },
        { MEDIA_RESOLUTION,         QByteArrayLiteral("resolution_name")    },
        { MEDIA_CHANNEL,            QByteArrayLiteral("channel")            },
        { MEDIA_MRL,                QByteArrayLiteral("mrl")                },
        { MEDIA_DISPLAY_MRL,        QByteArrayLiteral("display_mrl")        },
        { MEDIA_AUDIO_TRACK,        QByteArrayLiteral("audioDesc")          },
        { MEDIA_VIDEO_TRACK,        QByteArrayLiteral("videoDesc")          },
        { MEDIA_TITLE_FIRST_SYMBOL, QByteArrayLiteral("title_first_symbol") },
        { MEDIA_IS_VIDEO,           QByteArrayLiteral("isVideo")            },
        { MEDIA_ICON_NAME,          QByteArrayLiteral("iconName")           }
    };
}

QVariant MLPlaylistModel::itemRoleData(const MLItem *item, int role) const /* override */
{
    auto media = static_cast<const MLPlaylistMedia *>(item);
    if (media == nullptr)
        return QVariant();

    switch (role)
    {
        case MEDIA_ID:
            return QVariant::fromValue(media->getId());
        case MEDIA_IS_NEW:
            return QVariant::fromValue(media->isNew());
        case MEDIA_IS_VIDEO:
            if (media->type() == VLC_ML_MEDIA_TYPE_UNKNOWN) return {};
            return media->type() == VLC_ML_MEDIA_TYPE_VIDEO;
        case MEDIA_ICON_NAME:
            return media->type() == VLC_ML_MEDIA_TYPE_AUDIO ? QStringLiteral("music") : QStringLiteral("video");
        case MEDIA_TITLE:
            return QVariant::fromValue(media->title());
        case MEDIA_THUMBNAIL:
        {
            vlc_ml_thumbnail_status_t status;
            QString thumbnail = media->smallCover(&status);
            if ((media->type() == VLC_ML_MEDIA_TYPE_VIDEO)
                && (status == VLC_ML_THUMBNAIL_STATUS_MISSING
                    || status == VLC_ML_THUMBNAIL_STATUS_FAILURE))
            {
                generateThumbnail(item->getId());
            }

            return QVariant::fromValue(thumbnail);
        }
        case MEDIA_DURATION:
            return QVariant::fromValue(media->duration());
        case MEDIA_PROGRESS:
            return QVariant::fromValue(media->progress());
        case MEDIA_PLAYCOUNT:
            return QVariant::fromValue(media->playCount());
        case MEDIA_RESOLUTION:
            return QVariant::fromValue(media->getResolutionName());
        case MEDIA_CHANNEL:
            return QVariant::fromValue(media->getChannel());
        case MEDIA_MRL:
            return QVariant::fromValue(media->mrl());
        case MEDIA_DISPLAY_MRL:
            return QVariant::fromValue(media->getMRLDisplay());
        case MEDIA_VIDEO_TRACK:
            return QVariant::fromValue(media->getVideo());
        case MEDIA_AUDIO_TRACK:
            return QVariant::fromValue(media->getAudio());
        case MEDIA_TITLE_FIRST_SYMBOL:
            return QVariant::fromValue(getFirstSymbol(media->title()));
        default:
            return QVariant();
    }
}

//-------------------------------------------------------------------------------------------------
// Protected MLBaseModel implementation
//-------------------------------------------------------------------------------------------------

vlc_ml_sorting_criteria_t MLPlaylistModel::nameToCriteria(QByteArray name) const /* override */
{
    return QHash<QByteArray, vlc_ml_sorting_criteria_t> {
        {QByteArrayLiteral("id"),             VLC_ML_SORTING_DEFAULT},
        {QByteArrayLiteral("title"),          VLC_ML_SORTING_ALPHA},
        {QByteArrayLiteral("duration"),       VLC_ML_SORTING_DURATION},
        {QByteArrayLiteral("playcount"),      VLC_ML_SORTING_PLAYCOUNT},
        {QByteArrayLiteral("played"),         VLC_ML_SORTING_PLAYCOUNT},
        {QByteArrayLiteral("insertion"),      VLC_ML_SORTING_INSERTIONDATE},
        {QByteArrayLiteral("main_artist"),    VLC_ML_SORTING_ARTIST},
        {QByteArrayLiteral("album_title"),    VLC_ML_SORTING_ALBUM},
        {QByteArrayLiteral("track_number"),   VLC_ML_SORTING_TRACKNUMBER},
    }.value(name, VLC_ML_SORTING_DEFAULT);
}

//-------------------------------------------------------------------------------------------------

std::unique_ptr<MLListCacheLoader>
MLPlaylistModel::createMLLoader() const /* override */
{
    return std::make_unique<MLListCacheLoader>(m_mediaLib, std::make_shared<MLPlaylistModel::Loader>(*this));
}

//-------------------------------------------------------------------------------------------------
// Protected MLBaseModel reimplementation
//-------------------------------------------------------------------------------------------------

void MLPlaylistModel::onVlcMlEvent(const MLEvent & event) /* override */
{
    switch (event.i_type)
    {
    case VLC_ML_EVENT_MEDIA_UPDATED:
    {
        MLItemId itemId(event.modification.i_entity_id, VLC_ML_PARENT_UNKNOWN);
        updateItemInCache(itemId);
        return;
    }
    case VLC_ML_EVENT_PLAYLIST_UPDATED:
    {
        MLItemId itemId(event.modification.i_entity_id, VLC_ML_PARENT_PLAYLIST);
        if (m_parent == itemId)
        {
            if (m_transactionPending)
                m_resetAfterTransaction = true;
            else
                emit resetRequested();
        }
        return;
    }
    default:
        break;
    }

    MLBaseModel::onVlcMlEvent(event);
}

void MLPlaylistModel::thumbnailUpdated(const QModelIndex& idx, MLItem* mlitem, const QString& mrl, vlc_ml_thumbnail_status_t status) /* override */
{
    auto playlistItem = static_cast<MLPlaylistMedia*>(mlitem);
    playlistItem->setSmallCover(mrl, status);
    emit dataChanged(idx, idx, { MEDIA_THUMBNAIL });
}

//-------------------------------------------------------------------------------------------------
// Private functions
//-------------------------------------------------------------------------------------------------

std::vector<std::pair<int, int>> MLPlaylistModel::getSortedRowsRanges(const QModelIndexList & indexes, bool asc) const
{
    assert (indexes.size() > 0);

    QList<int> rows;
    for (const QModelIndex & index : indexes)
        rows.append(index.row());

    std::sort(rows.begin(), rows.end());

    //build continuous ranges
    std::vector<std::pair<int, int>> rangeList;
    auto it = rows.cbegin();
    int high = *it;
    int low = *it;
    if (rows.count() == 1)
    {
        rangeList.emplace_back(low, high);
    }
    else
    {
        ++it;
        while (it != rows.cend())
        {
            int value = *it;
            if (value != high + 1)
            {
                rangeList.emplace_back(low, high);
                low = value;
            }
            high = value;
            ++it;
        }
        rangeList.emplace_back(low, high);
    }

    if (!asc)
        std::reverse(rangeList.begin(), rangeList.end());

    return rangeList;
}

void MLPlaylistModel::generateThumbnail(const MLItemId& itemid) const
{
    // A failed request also emits dataChanged. Retry only on the next visit,
    // rather than starting another request every time a delegate reads its cover.
    if (m_requestedThumbnails.contains(itemid.id)) return;
    m_requestedThumbnails.insert(itemid.id);
    m_mediaLib->runOnMLThread(this,
    //ML thread
    [id = itemid.id](vlc_medialibrary_t* ml)
    {
        ml_unique_ptr<vlc_ml_media_t> media(vlc_ml_get_media(ml, id));
        if (!media || media->i_type != VLC_ML_MEDIA_TYPE_VIDEO) return;
        vlc_ml_media_generate_thumbnail(ml, id, VLC_ML_THUMBNAIL_SMALL,
                                        512, 320, 0.15);
    });
}

//=================================================================================================
// Loader
//=================================================================================================

size_t MLPlaylistModel::Loader::count(vlc_medialibrary_t* ml, const vlc_ml_query_params_t* queryParams) const /* override */
{
    return vlc_ml_count_playlist_media(ml, queryParams, m_parent.id);
}

std::vector<std::unique_ptr<MLItem>>
MLPlaylistModel::Loader::load(vlc_medialibrary_t* ml, const vlc_ml_query_params_t* queryParams) const /* override */
{
    const bool localSort = sortName() == QLatin1String("uri") || sortName() == QLatin1String("disc_number");
    auto query = *queryParams;
    // The media-library API has no full-URI or disc-number sort. Sort the full
    // result before paging so every page belongs to the same requested order.
    if (localSort) { query.i_offset = 0; query.i_nbResults = 0; }
    ml_unique_ptr<vlc_ml_media_list_t> list {
        vlc_ml_list_playlist_media(ml, &query, m_parent.id)
    };

    if (list == nullptr)
        return {};

    std::vector<std::unique_ptr<MLItem>> result;

    for (const vlc_ml_media_t & media : ml_range_iterate<vlc_ml_media_t> (list))
    {
        result.emplace_back(std::make_unique<MLPlaylistMedia>(&media));
    }

    if (localSort) {
        const bool byUri = sortName() == QLatin1String("uri");
        std::stable_sort(result.begin(), result.end(), [byUri, descending = queryParams->b_desc](const auto& a, const auto& b) {
            const auto left = static_cast<const MLPlaylistMedia*>(a.get());
            const auto right = static_cast<const MLPlaylistMedia*>(b.get());
            int comparison;
            if (byUri) comparison = QString::compare(left->mrl(), right->mrl(), Qt::CaseInsensitive);
            else if (left->discNumber() != right->discNumber()) comparison = left->discNumber() < right->discNumber() ? -1 : 1;
            else comparison = left->trackNumber() == right->trackNumber() ? 0 : left->trackNumber() < right->trackNumber() ? -1 : 1;
            return descending ? comparison > 0 : comparison < 0;
        });
        const auto first = std::min<size_t>(queryParams->i_offset, result.size());
        const auto count = queryParams->i_nbResults ? std::min<size_t>(queryParams->i_nbResults, result.size() - first) : result.size() - first;
        std::vector<std::unique_ptr<MLItem>> page;
        page.reserve(count);
        for (size_t i = first; i < first + count; ++i) page.push_back(std::move(result[i]));
        return page;
    }

    return result;
}

std::unique_ptr<MLItem>
MLPlaylistModel::Loader::loadItemById(vlc_medialibrary_t* ml, MLItemId itemId) const
{
    assert(itemId.type == VLC_ML_PARENT_UNKNOWN);
    ml_unique_ptr<vlc_ml_media_t> media(vlc_ml_get_media(ml, itemId.id));
    if (!media)
        return nullptr;
    return std::make_unique<MLPlaylistMedia>(media.get());
}
