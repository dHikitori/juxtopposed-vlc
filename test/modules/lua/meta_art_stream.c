// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_interrupt.h>
#include <vlc/vlc.h>
#include "../../../lib/libvlc_internal.h"
#include <lua.h>
#include <lauxlib.h>
#include <assert.h>
#include <errno.h>
#include <string.h>

const char vlc_module_name[] = "native-artwork-lua-stream-test";

/* Use the fresh Core stream buffer and the real Lua read implementation. The
 * custom source controls only bytes, EOF, and read errors. Section GC removes
 * unused Lua entry points. No successful I/O function replaces production code.
 */
#include "../../../src/input/stream_observer.h"
#include <vlc_stream.h>
#include "../../../modules/lua/libs/stream.c"

enum finish_mode { CLEAN_EOF, ERROR_THEN_EOF, ERROR_UNTIL_INTERRUPT };
struct source {
    const char *bytes;
    size_t size, offset;
    enum finish_mode finish;
    unsigned calls, errors, destroyed;
    vlc_interrupt_t *interrupt;
};
static ssize_t source_read(stream_t *stream, void *buffer, size_t size)
{
    struct source *source = stream->p_sys; ++source->calls;
    if (source->offset < source->size) {
        size_t count = source->size - source->offset;
        if (count > size) count = size;
        memcpy(buffer, source->bytes + source->offset, count); source->offset += count;
        return count;
    }
    if (source->finish == ERROR_THEN_EOF && source->errors++ == 0) { errno = EIO; return -1; }
    if (source->finish == ERROR_UNTIL_INTERRUPT) {
        if (++source->errors == 3) vlc_interrupt_kill(source->interrupt);
        errno = EIO; return -1;
    }
    return 0;
}
static void source_destroy(stream_t *stream)
{ ++((struct source *)stream->p_sys)->destroyed; }
static int source_open(lua_State *state, vlc_object_t *parent, struct source *source)
{
    stream_t *stream = vlc_stream_CommonNew(parent, source_destroy);
    assert(stream != NULL); stream->p_sys = source; stream->pf_read = source_read;
    vlclua_metadata_io_attempt(state);
    if (vlclua_metadata_io_state(state) != NULL) {
        struct vlclua_stream *data = lua_newuserdata(state, sizeof(*data));
        data->stream = stream; data->complete = false; data->counted = vlclua_metadata_io_opened(state);
    } else {
        stream_t **data = lua_newuserdata(state, sizeof(*data)); *data = stream;
    }
    luaL_newmetatable(state, "stream"); lua_setmetatable(state, -2);
    return luaL_ref(state, LUA_REGISTRYINDEX);
}
static void source_read_lua(lua_State *state, int reference, size_t size, const char *expected)
{
    lua_settop(state, 0); lua_rawgeti(state, LUA_REGISTRYINDEX, reference); lua_pushinteger(state, size);
    assert(vlclua_stream_read(state) == 1);
    size_t count = 0; const char *bytes = lua_tolstring(state, -1, &count);
    assert(bytes != NULL && count == strlen(expected) && !memcmp(bytes, expected, count));
}
static meta_fetcher_outcome_t negative(lua_State *state)
{
    lua_settop(state, 0); lua_pushnil(state); lua_pushliteral(state, "no_match");
    return vlclua_metadata_art_outcome(state, 1, 2);
}
static void source_close(lua_State *state, int reference)
{
    lua_settop(state, 0); lua_rawgeti(state, LUA_REGISTRYINDEX, reference);
    assert(vlclua_stream_delete(state) == 0);
    luaL_unref(state, LUA_REGISTRYINDEX, reference); lua_settop(state, 0);
}
static void read_outcome(vlc_object_t *parent, enum finish_mode finish, bool tracked, bool unread)
{
    lua_State *state = luaL_newstate(); assert(state != NULL);
    if (tracked) vlclua_metadata_io_watch(state);
    struct source source = {.bytes=unread ? "empty-tail" : "empty", .size=unread ? 10 : 5, .finish=finish};
    source.interrupt = vlc_interrupt_create(); assert(source.interrupt != NULL);
    vlc_interrupt_t *previous = vlc_interrupt_set(source.interrupt);
    const int reference = source_open(state, parent, &source);
    source_read_lua(state, reference, unread ? 5 : 64, "empty");
    const meta_fetcher_outcome_t expected = tracked && !unread && finish == CLEAN_EOF
        ? META_FETCHER_OUTCOME_NO_MATCH : META_FETCHER_OUTCOME_UNAVAILABLE;
    assert(negative(state) == expected);
    if (!tracked) assert(vlclua_metadata_io_state(state) == NULL);
    if (finish == ERROR_THEN_EOF) assert(source.calls == 3);
    if (finish == ERROR_UNTIL_INTERRUPT) assert(source.calls == 4);
    source_close(state, reference); assert(source.destroyed == 1);
    vlc_interrupt_set(previous); vlc_interrupt_destroy(source.interrupt); lua_close(state);
}
static void every_opened_stream_must_end(vlc_object_t *parent)
{
    lua_State *state = luaL_newstate(); assert(state != NULL); vlclua_metadata_io_watch(state);
    struct source complete = {.bytes="empty", .size=5}, pending = {.bytes="tail", .size=4};
    const int first = source_open(state, parent, &complete);
    const int second = source_open(state, parent, &pending);
    source_read_lua(state, first, 64, "empty");
    assert(negative(state) == META_FETCHER_OUTCOME_UNAVAILABLE);
    source_close(state, second);
    assert(negative(state) == META_FETCHER_OUTCOME_UNAVAILABLE);
    source_close(state, first); assert(complete.destroyed == 1 && pending.destroyed == 1);
    lua_close(state);
}
int main(void)
{
    libvlc_int_t *instance = libvlc_InternalCreate(); assert(instance != NULL);
    vlc_object_t *parent = VLC_OBJECT(instance);
    read_outcome(parent, CLEAN_EOF, true, false);
    read_outcome(parent, ERROR_THEN_EOF, true, false);
    read_outcome(parent, CLEAN_EOF, true, true);
    read_outcome(parent, ERROR_THEN_EOF, false, false);
    read_outcome(parent, ERROR_UNTIL_INTERRUPT, true, false);
    every_opened_stream_must_end(parent);
    libvlc_InternalDestroy(instance); return 0;
}
