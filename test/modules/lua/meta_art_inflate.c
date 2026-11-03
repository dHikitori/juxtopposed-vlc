// SPDX-License-Identifier: LGPL-2.1-or-later
/* Open is the real automatic-filter constructor. Its two-byte Peek can retry
 * and hide a source error before Lua receives the returned stream.
 */
#undef VLC_DYNAMIC_PLUGIN
#undef MODULE_NAME
#undef MODULE_STRING
#define MODULE_NAME meta_art_inflate_fixture
#include "../../../modules/stream_filter/inflate.c"

int meta_art_inflate_open(stream_t *stream)
{
    return Open(VLC_OBJECT(stream));
}

void meta_art_inflate_close(stream_t *stream)
{
    Close(VLC_OBJECT(stream));
}
