// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_meta.h>
#include <assert.h>
#include <string.h>
#include "../../../modules/meta_engine/ID3Tag.h"
#include "../../../modules/meta_engine/ID3Meta.h"

static const uint8_t artwork[] = "\x03" "artworkURL_1\0https://radio.example/art.jpg";
static const char expected[] = "https://radio.example/art.jpg";

static void assert_artwork(const vlc_meta_t *meta)
{
    const char *value = vlc_meta_Get(meta, vlc_meta_ArtworkURL);
    assert(value != NULL && strcmp(value, expected) == 0);
}

static int parse_frame(uint32_t tag, const uint8_t *bytes, size_t size, void *opaque)
{
    assert(tag == VLC_FOURCC('W', 'X', 'X', 'X'));
    assert(ID3HandleTag(bytes, size, tag, opaque, NULL));
    return VLC_SUCCESS;
}

static void a_ts_frame_accepts_no_update_pointer(void)
{
    uint8_t tag[20 + sizeof(artwork) - 1] = {'I', 'D', '3', 4};
    tag[9] = sizeof(tag) - 10;
    memcpy(tag + 10, "WXXX", 4);
    tag[17] = sizeof(artwork) - 1;
    memcpy(tag + 20, artwork, sizeof(artwork) - 1);
    vlc_meta_t *meta = vlc_meta_New(); assert(meta != NULL);
    assert(ID3TAG_Parse(tag, sizeof(tag), parse_frame, meta) == sizeof(tag));
    assert_artwork(meta);
    vlc_meta_Delete(meta);
}

static void an_update_pointer_reports_the_artwork(void)
{
    vlc_meta_t *meta = vlc_meta_New(); assert(meta != NULL);
    vlc_meta_Set(meta, vlc_meta_ArtworkURL, "https://radio.example/old.jpg");
    bool updated = false;
    assert(ID3HandleTag(artwork, sizeof(artwork) - 1,
        VLC_FOURCC('W', 'X', 'X', 'X'), meta, &updated));
    assert(updated); assert_artwork(meta);
    vlc_meta_Delete(meta);
}

static void a_short_frame_preserves_the_artwork(void)
{
    vlc_meta_t *meta = vlc_meta_New(); assert(meta != NULL);
    vlc_meta_Set(meta, vlc_meta_ArtworkURL, expected);
    bool updated = false;
    assert(!ID3HandleTag(artwork, 13,
        VLC_FOURCC('W', 'X', 'X', 'X'), meta, &updated));
    assert(!updated); assert_artwork(meta);
    vlc_meta_Delete(meta);
}

int main(void)
{
    a_ts_frame_accepts_no_update_pointer();
    an_update_pointer_reports_the_artwork();
    a_short_frame_preserves_the_artwork();
    return 0;
}
