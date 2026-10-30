// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_guest_sources.hpp"
#include "addons/addon_guest_projections.hpp"
#include "maininterface/mainctx.hpp"
#include "medialibrary/medialib.hpp"
#include "player/player_controller.hpp"
#include "playlist/playlist_controller.hpp"
#include <vlc_input_item.h>
#include <vlc_media_library.h>
#include <vlc_player.h>
#include <vlc_playlist.h>
#include <QPointer>
#include <memory>

namespace {
using Projections = vlc::addons::AddonGuestProjections;

QString meta(input_item_t* item, vlc_meta_type_t type)
{
    char* value = input_item_GetMeta(item, type);
    const auto result = QString::fromUtf8(value ? value : "");
    free(value);
    return result;
}
QString utf8(const char* value) { return QString::fromUtf8(value ? value : ""); }

struct LibraryQuery {
    QString collection, pattern;
    int offset = 0, limit = 0;
    std::optional<Projections::LibraryPage> page;
};

// Runs on the library thread. Only fields named by the projection are read;
// no MRL, file or folder leaves this function.
void listLibrary(vlc_medialibrary_t* ml, LibraryQuery& query)
{
    const auto pattern = query.pattern.toUtf8();
    auto params = vlc_ml_query_params_create();
    params.psz_pattern = pattern.isEmpty() ? nullptr : pattern.constData();
    params.i_nbResults = uint32_t(query.limit + 1);
    params.i_offset = uint32_t(query.offset);
    Projections::LibraryPage page;
    const auto add = [&page](int64_t id, QVariantMap fields) {
        page.items.append({QByteArray::number(qlonglong(id)), std::move(fields)});
    };
    if (query.collection == QLatin1String("tracks") || query.collection == QLatin1String("videos")) {
        auto* list = query.collection == QLatin1String("tracks") ? vlc_ml_list_audio_media(ml, &params) : vlc_ml_list_video_media(ml, &params);
        if (!list) return;
        for (size_t i = 0; i < list->i_nb_items; ++i) {
            const auto& media = list->p_items[i];
            QVariantMap fields{{"title", utf8(media.psz_title)}, {"durationMs", qlonglong(media.i_duration)}};
            if (media.i_year > 0) fields.insert("year", media.i_year);
            if (query.collection == QLatin1String("tracks") && media.i_subtype == VLC_ML_MEDIA_SUBTYPE_ALBUMTRACK && media.album_track.i_track_nb > 0)
                fields.insert("trackNumber", media.album_track.i_track_nb);
            add(media.i_id, fields);
        }
        vlc_ml_release(list);
    } else if (query.collection == QLatin1String("albums")) {
        auto* list = vlc_ml_list_albums(ml, &params);
        if (!list) return;
        for (size_t i = 0; i < list->i_nb_items; ++i) {
            const auto& album = list->p_items[i];
            QVariantMap fields{{"title", utf8(album.psz_title)}, {"artist", utf8(album.psz_artist)},
                {"trackCount", qlonglong(album.i_nb_tracks)}, {"durationMs", qlonglong(album.i_duration)}};
            if (album.i_year > 0) fields.insert("year", album.i_year);
            add(album.i_id, fields);
        }
        vlc_ml_release(list);
    } else if (query.collection == QLatin1String("artists")) {
        auto* list = vlc_ml_list_artists(ml, &params, false);
        if (!list) return;
        for (size_t i = 0; i < list->i_nb_items; ++i) {
            const auto& artist = list->p_items[i];
            add(artist.i_id, {{"title", utf8(artist.psz_name)}, {"albumCount", artist.i_nb_album}, {"trackCount", artist.i_nb_tracks}});
        }
        vlc_ml_release(list);
    } else if (query.collection == QLatin1String("playlists")) {
        auto* list = vlc_ml_list_playlists(ml, &params, VLC_ML_PLAYLIST_TYPE_ALL);
        if (!list) return;
        for (size_t i = 0; i < list->i_nb_items; ++i) {
            const auto& playlist = list->p_items[i];
            add(playlist.i_id, {{"title", utf8(playlist.psz_name)}, {"trackCount", playlist.i_nb_media},
                {"durationMs", qlonglong(playlist.i_duration)}});
        }
        vlc_ml_release(list);
    } else return;
    page.more = page.items.size() > query.limit;
    if (page.more) page.items.resize(query.limit);
    query.page = std::move(page);
}
}

vlc::addons::AddonGuestProjections* mountAddonGuestSources(qt_intf_t* intf, MainCtx* ctx, vlc::addons::AddonHostServices* services,
                                                           QObject* parent)
{
    auto* projections = new Projections(parent);
    vlc_playlist_t* playlist = intf->p_mainPlaylistController ? intf->p_mainPlaylistController->getPlaylist().m_playlist : nullptr;
    Projections::QueueReader queue;
    if (playlist) queue = [playlist](int offset, int limit) -> std::optional<Projections::Queue> {
        // A bounded copy under the playlist lock; nothing waits on I/O here.
        Projections::Queue result;
        vlc_playlist_Lock(playlist);
        result.total = qint64(vlc_playlist_Count(playlist));
        result.current = qint64(vlc_playlist_GetCurrentIndex(playlist));
        for (qint64 i = offset; i < result.total && i < offset + limit; ++i) {
            vlc_playlist_item_t* entry = vlc_playlist_Get(playlist, size_t(i));
            input_item_t* media = vlc_playlist_item_GetMedia(entry);
            Projections::QueueItem item;
            item.key = QByteArray::number(qulonglong(vlc_playlist_item_GetId(entry)));
            item.title = meta(media, vlc_meta_Title);
            if (item.title.isEmpty()) item.title = utf8(media->psz_name);
            item.artist = meta(media, vlc_meta_Artist);
            item.album = meta(media, vlc_meta_Album);
            const auto duration = input_item_GetDuration(media);
            item.durationMs = duration == VLC_TICK_INVALID || duration < 0 ? -1 : MS_FROM_VLC_TICK(duration);
            result.items.append(item);
        }
        vlc_playlist_Unlock(playlist);
        return result;
    };
    Projections::LibraryReader library;
    if (MediaLib* mediaLibrary = ctx ? ctx->getMediaLibrary() : nullptr) {
        const QPointer<MediaLib> ml(mediaLibrary);
        const QPointer<QObject> owner(projections);
        library = [ml, owner](const QString& collection, const QString& pattern, int offset, int limit,
                              Projections::LibraryDone done) -> std::function<void()> {
            auto live = std::make_shared<bool>(true);
            if (!ml) { done(std::nullopt); return [] {}; }
            ml->runOnMLThread<LibraryQuery>(owner,
                [collection, pattern, offset, limit](vlc_medialibrary_t* library, LibraryQuery& query) {
                    query.collection = collection; query.pattern = pattern; query.offset = offset; query.limit = limit;
                    listLibrary(library, query);
                },
                [done, live](quint64, LibraryQuery& query) { if (*live) done(std::move(query.page)); },
                "addon-guest-library");
            return [live] { *live = false; };
        };
    }
    vlc_player_t* player = intf->p_mainPlayerController ? intf->p_mainPlayerController->getPlayer() : nullptr;
    Projections::CurrentMedia media;
    if (player) media = [player]() -> QUrl {
        vlc_player_Lock(player);
        input_item_t* item = vlc_player_GetCurrentMedia(player);
        char* uri = item ? input_item_GetURI(item) : nullptr;
        vlc_player_Unlock(player);
        const QUrl url(QString::fromUtf8(uri ? uri : ""));
        free(uri);
        return url;
    };
    if (!projections->mount(services, std::move(queue), std::move(library), std::move(media)))
        msg_Warn(intf, "Cannot offer every read-only add-on projection");
    return projections;
}
