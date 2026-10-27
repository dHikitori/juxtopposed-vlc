// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_ADDON_MEDIALIBRARY_H
#define VLC_ADDON_MEDIALIBRARY_H 1
#include <vlc_common.h>
#include <vlc_media_library.h>
#include <stdlib.h>
#include <string.h>

typedef struct vlc_addon_playlist_snapshot vlc_addon_playlist_snapshot_t;

/* Private module queries. An older module returns its unknown-query error. */
enum {
    VLC_ML_ADDON_PLAYLIST_CAS_AVAILABLE = 0x41445000,
    VLC_ML_ADDON_PLAYLIST_SNAPSHOT,
    VLC_ML_ADDON_PLAYLIST_APPEND,
    VLC_ML_ADDON_SOURCE_MEDIA_PREPARE,
};

static inline bool vlc_addon_playlist_cas_available(vlc_medialibrary_t *ml)
{
    bool available = false;
    return ml != NULL && vlc_ml_control(ml, VLC_ML_ADDON_PLAYLIST_CAS_AVAILABLE,
                                       &available) == VLC_SUCCESS && available;
}

/* Call create and append on the media-library thread. The fixed snapshot
 * holds no library, database, module, or Qt reference. An owner mismatch fails.
 */
static inline vlc_addon_playlist_snapshot_t *
vlc_addon_playlist_snapshot_create(vlc_medialibrary_t *ml, int64_t playlist_id)
{
    if (ml == NULL || playlist_id <= 0) return NULL;
    vlc_addon_playlist_snapshot_t *snapshot = NULL;
    if (vlc_ml_control(ml, VLC_ML_ADDON_PLAYLIST_SNAPSHOT, playlist_id, &snapshot) != VLC_SUCCESS) {
        free(snapshot);
        return NULL;
    }
    return snapshot;
}

/* Destroy on any thread, including after library and module shutdown. */
static inline void
vlc_addon_playlist_snapshot_destroy(vlc_addon_playlist_snapshot_t *snapshot)
{ free(snapshot); }

/* Cancellation must stop a task before this call. An admitted append is one
 * atomic operation; a later cancellation does not reverse a completed append.
 */
static inline int
vlc_addon_playlist_append_if_unchanged(vlc_medialibrary_t *ml,
    const vlc_addon_playlist_snapshot_t *snapshot, const int64_t *media_ids, size_t count)
{
    if (ml == NULL || snapshot == NULL || media_ids == NULL || count == 0 || count > 1000)
        return VLC_EINVAL;
    for (size_t i = 0; i < count; ++i) if (media_ids[i] <= 0) return VLC_EINVAL;
    return vlc_ml_control(ml, VLC_ML_ADDON_PLAYLIST_APPEND, snapshot, media_ids, count);
}

/* Native host preparation only. Keep the exact admitted URI. Set a catalog
 * title only for a new record; retain every existing record's title. This
 * operation does not append or resolve a source and starts no stream.
 */
static inline int
vlc_addon_source_media_prepare(vlc_medialibrary_t *ml, const char *source_uri,
                              const char *title, int64_t *media_id)
{
    if (media_id != NULL) *media_id = 0;
    if (ml == NULL || media_id == NULL || source_uri == NULL || title == NULL
        || strnlen(source_uri, 12001) > 12000 || strncmp(source_uri, "hakone-addon-source:v1:", sizeof("hakone-addon-source:v1:") - 1)
        || source_uri[sizeof("hakone-addon-source:v1:") - 1] == '\0' || strnlen(title, 2049) > 2048 || title[0] == '\0') return VLC_EINVAL;
    for (const unsigned char *p = (const unsigned char *)source_uri; *p != '\0'; ++p)
        if (*p <= 32 || *p == 127) return VLC_EINVAL;
    for (const unsigned char *p = (const unsigned char *)title; *p != '\0'; ++p)
        if (*p < 32 || *p == 127) return VLC_EINVAL;
    return vlc_ml_control(ml, VLC_ML_ADDON_SOURCE_MEDIA_PREPARE, source_uri, title, media_id);
}
#endif
