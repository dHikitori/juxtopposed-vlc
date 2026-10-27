// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_addon_medialibrary.h>
#include <assert.h>
#include <stdarg.h>

static unsigned calls;
static bool supported;
static int64_t expected_playlist;
static vlc_addon_playlist_snapshot_t *expected_snapshot;
static const int64_t *expected_ids;
static size_t expected_count;

int vlc_ml_control(vlc_medialibrary_t *ml, int query, ...)
{
    assert(ml != NULL); ++calls;
    va_list args; va_start(args,query);
    int result = VLC_EGENERIC;
    if (supported && query == VLC_ML_ADDON_PLAYLIST_CAS_AVAILABLE) {
        *va_arg(args,bool*) = true; result = VLC_SUCCESS;
    } else if (supported && query == VLC_ML_ADDON_PLAYLIST_SNAPSHOT) {
        assert(va_arg(args,int64_t) == expected_playlist);
        vlc_addon_playlist_snapshot_t **output = va_arg(args,vlc_addon_playlist_snapshot_t**);
        *output = malloc(64); assert(*output != NULL); result = VLC_SUCCESS;
    } else if (supported && query == VLC_ML_ADDON_PLAYLIST_APPEND) {
        assert(va_arg(args,const vlc_addon_playlist_snapshot_t*) == expected_snapshot);
        assert(va_arg(args,const int64_t*) == expected_ids);
        assert(va_arg(args,size_t) == expected_count); result = VLC_SUCCESS;
    } else if (supported && query == VLC_ML_ADDON_SOURCE_MEDIA_PREPARE) {
        assert(!strcmp(va_arg(args,const char*), "hakone-addon-source:v1:admitted"));
        assert(!strcmp(va_arg(args,const char*), "Station"));
        *va_arg(args,int64_t*) = 42; result = VLC_SUCCESS;
    }
    va_end(args); return result;
}
int main(void)
{
    unsigned char opaque;
    vlc_medialibrary_t *ml = (vlc_medialibrary_t*)&opaque;
    assert(!vlc_addon_playlist_cas_available(NULL) && calls==0);
    assert(!vlc_addon_playlist_cas_available(ml) && calls==1);
    assert(vlc_addon_playlist_snapshot_create(ml,1)==NULL);
    supported = true; assert(vlc_addon_playlist_cas_available(ml));
    const unsigned before = calls;
    assert(vlc_addon_playlist_snapshot_create(NULL,1)==NULL);
    assert(vlc_addon_playlist_snapshot_create(ml,0)==NULL && calls==before);
    expected_playlist = 23;
    vlc_addon_playlist_snapshot_t *snapshot = vlc_addon_playlist_snapshot_create(ml,expected_playlist);
    assert(snapshot != NULL); expected_snapshot = snapshot;
    const int64_t ids[] = {17,31}; expected_ids=ids; expected_count=2;
    assert(vlc_addon_playlist_append_if_unchanged(ml,snapshot,ids,2)==VLC_SUCCESS);
    const unsigned admitted = calls;
    const int64_t invalid[] = {17,0};
    assert(vlc_addon_playlist_append_if_unchanged(NULL,snapshot,ids,2)==VLC_EINVAL);
    assert(vlc_addon_playlist_append_if_unchanged(ml,NULL,ids,2)==VLC_EINVAL);
    assert(vlc_addon_playlist_append_if_unchanged(ml,snapshot,NULL,2)==VLC_EINVAL);
    assert(vlc_addon_playlist_append_if_unchanged(ml,snapshot,ids,0)==VLC_EINVAL);
    assert(vlc_addon_playlist_append_if_unchanged(ml,snapshot,ids,1001)==VLC_EINVAL);
    assert(vlc_addon_playlist_append_if_unchanged(ml,snapshot,invalid,2)==VLC_EINVAL && calls==admitted);
    int64_t id = -1;
    assert(vlc_addon_source_media_prepare(ml,"hakone-addon-source:v1:admitted","Station",&id)==VLC_SUCCESS && id==42);
    const unsigned prepared = calls;
    assert(vlc_addon_source_media_prepare(ml,"file:///private.mkv","Station",&id)==VLC_EINVAL && id==0);
    assert(vlc_addon_source_media_prepare(ml,"hakone-addon-source:v1:","Station",&id)==VLC_EINVAL);
    assert(vlc_addon_source_media_prepare(ml,"hakone-addon-source:v1:admitted","Station\nInjected",&id)==VLC_EINVAL);
    assert(vlc_addon_source_media_prepare(ml,"hakone-addon-source:v1:admitted","",&id)==VLC_EINVAL && calls==prepared);
    supported = false;
    assert(vlc_addon_playlist_append_if_unchanged(ml,snapshot,ids,2)==VLC_EGENERIC);
    vlc_addon_playlist_snapshot_destroy(snapshot); vlc_addon_playlist_snapshot_destroy(NULL);
    return 0;
}
