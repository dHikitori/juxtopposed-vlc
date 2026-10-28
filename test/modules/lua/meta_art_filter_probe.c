// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_stream.h>
#include <assert.h>
#undef VLC_DYNAMIC_PLUGIN
#undef MODULE_NAME
#undef MODULE_STRING
#define MODULE_NAME metadata_consume_decline_fixture
#include <vlc_plugin.h>

VLC_EXPORT unsigned metadata_consume_decline_calls;

static int Open(vlc_object_t *object)
{
    stream_t *stream = (stream_t *)object;
    unsigned char prefix;
    ++metadata_consume_decline_calls;
    assert(vlc_stream_Read(stream->s, &prefix, 1) == 1 && prefix == 'x');
    return VLC_EGENERIC;
}

vlc_module_begin()
    set_capability("stream_filter", 0)
    set_description("Native metadata consumed-decline test")
    set_callback(Open)
vlc_module_end()

/* Core's existing weak static-plugin hook loads this exact fixture. The test
 * then uses the ordinary module_need candidate loop and the actual cache. */
VLC_EXPORT const vlc_plugin_cb vlc_static_modules[] = {
    VLC_MODULE_ENTRY(metadata_consume_decline_fixture), NULL,
};
