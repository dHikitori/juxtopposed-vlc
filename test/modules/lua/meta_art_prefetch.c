// SPDX-License-Identifier: LGPL-2.1-or-later
/* Compile the real filter worker in a separate translation unit. Only the
 * module descriptor name is local to this test. Open, Read, and Close keep
 * their production implementation, including the native thread and join.
 */
#undef VLC_DYNAMIC_PLUGIN
#undef MODULE_NAME
#undef MODULE_STRING
#define MODULE_NAME meta_art_prefetch_fixture
#include "../../../modules/stream_filter/prefetch.c"

int meta_art_prefetch_open(stream_t *stream)
{
    return Open(VLC_OBJECT(stream));
}

void meta_art_prefetch_close(stream_t *stream)
{
    Close(VLC_OBJECT(stream));
}
