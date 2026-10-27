// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <vlc_common.h>
#include <vlc_addon_medialibrary.h>
#include <vlc_charset.h>
#include <medialibrary/IMediaLibrary.h>
#ifdef VLC_ML_HAS_ADDON_PLAYLIST_CAS
# include <medialibrary/AddonPlaylist.h>
#endif
#include <cstdarg>
#include <new>
#include <type_traits>
#include <vector>

#ifdef VLC_ML_HAS_ADDON_PLAYLIST_CAS
struct vlc_addon_playlist_snapshot {
    medialibrary::AddonPlaylistSnapshot value;
};
static_assert(std::is_trivially_destructible<vlc_addon_playlist_snapshot>::value,
              "The snapshot must need no module destructor.");
#endif

namespace vlc::medialibrary::addonplaylist {
inline int control(::medialibrary::IMediaLibrary& library, int query, va_list args)
{
    (void)library;
    if (query == VLC_ML_ADDON_PLAYLIST_CAS_AVAILABLE) {
        auto* available = va_arg(args, bool*);
        if (!available) return VLC_EINVAL;
#ifdef VLC_ML_HAS_ADDON_PLAYLIST_CAS
        *available = true;
#else
        *available = false;
#endif
        return VLC_SUCCESS;
    }
    if (query == VLC_ML_ADDON_PLAYLIST_SNAPSHOT) {
        const auto id = va_arg(args, int64_t);
        auto** output = va_arg(args, vlc_addon_playlist_snapshot_t**);
        if (!output) return VLC_EINVAL;
        *output = nullptr;
#ifdef VLC_ML_HAS_ADDON_PLAYLIST_CAS
        ::medialibrary::AddonPlaylistSnapshot value;
        if (!::medialibrary::createAddonPlaylistSnapshot(library, id, value)) return VLC_EGENERIC;
        auto* memory = malloc(sizeof(vlc_addon_playlist_snapshot));
        if (!memory) return VLC_ENOMEM;
        *output = new (memory) vlc_addon_playlist_snapshot{value};
        return VLC_SUCCESS;
#else
        (void)id;
        return VLC_EGENERIC;
#endif
    }
    if (query == VLC_ML_ADDON_PLAYLIST_APPEND) {
        const auto* snapshot = va_arg(args, const vlc_addon_playlist_snapshot_t*);
        const auto* ids = va_arg(args, const int64_t*);
        const auto count = va_arg(args, size_t);
        if (!snapshot || !ids || !count || count > 1000) return VLC_EINVAL;
        for (size_t i = 0; i < count; ++i) if (ids[i] <= 0) return VLC_EINVAL;
#ifdef VLC_ML_HAS_ADDON_PLAYLIST_CAS
        try {
            return ::medialibrary::appendAddonPlaylistIfUnchanged(library, snapshot->value,
                std::vector<int64_t>(ids, ids + count)) ? VLC_SUCCESS : VLC_EGENERIC;
        } catch (const std::bad_alloc&) { return VLC_ENOMEM; }
#else
        return VLC_EGENERIC;
#endif
    }
    if (query == VLC_ML_ADDON_SOURCE_MEDIA_PREPARE) {
        const auto* uri = va_arg(args, const char*);
        const auto* title = va_arg(args, const char*);
        auto* output = va_arg(args, int64_t*);
        if (!output) return VLC_EINVAL;
        *output = 0;
        if (!uri || !title || strnlen(uri, 12001) > 12000 || strnlen(title, 2049) > 2048
            || !IsUTF8(uri) || !IsUTF8(title)) return VLC_EINVAL;
#ifdef VLC_ML_HAS_ADDON_PLAYLIST_CAS
        return ::medialibrary::prepareAddonSourceMedia(library, uri, title, *output) ? VLC_SUCCESS : VLC_EGENERIC;
#else
        return VLC_EGENERIC;
#endif
    }
    return VLC_EGENERIC;
}
}
