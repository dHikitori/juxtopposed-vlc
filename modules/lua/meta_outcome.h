// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_LUA_META_OUTCOME_H
#define VLC_LUA_META_OUTCOME_H 1
#include <vlc_meta_fetcher.h>
#include <lua.h>
#include <stdbool.h>
#include <string.h>

#define VLC_LUA_METADATA_IO_KEY "hakone.native-artwork-io"
#define VLC_LUA_METADATA_IO_LIMIT 64u
struct vlclua_metadata_io {
    unsigned attempts, incomplete;
    bool failed;
};
static inline struct vlclua_metadata_io *vlclua_metadata_io_state(lua_State *state)
{
    lua_pushliteral(state, VLC_LUA_METADATA_IO_KEY); lua_rawget(state, LUA_REGISTRYINDEX);
#if LUA_VERSION_NUM >= 502
    const size_t bytes = lua_rawlen(state, -1);
#else
    const size_t bytes = lua_objlen(state, -1);
#endif
    struct vlclua_metadata_io *io = lua_type(state, -1) == LUA_TUSERDATA
        && bytes == sizeof(*io) ? lua_touserdata(state, -1) : NULL;
    lua_pop(state, 1); return io;
}
static inline void vlclua_metadata_io_watch(lua_State *state)
{
    if (vlclua_metadata_io_state(state) != NULL) return;
    lua_pushliteral(state, VLC_LUA_METADATA_IO_KEY);
    struct vlclua_metadata_io *io = lua_newuserdata(state, sizeof(*io));
    memset(io, 0, sizeof(*io));
    lua_rawset(state, LUA_REGISTRYINDEX);
}
static inline void vlclua_metadata_io_attempt(lua_State *state)
{
    struct vlclua_metadata_io *io = vlclua_metadata_io_state(state);
    if (io == NULL) return;
    if (io->attempts < VLC_LUA_METADATA_IO_LIMIT) ++io->attempts;
    else io->failed = true;
}
static inline void vlclua_metadata_io_failure(lua_State *state)
{
    struct vlclua_metadata_io *io = vlclua_metadata_io_state(state);
    if (io != NULL) io->failed = true;
}
static inline bool vlclua_metadata_io_opened(lua_State *state)
{
    struct vlclua_metadata_io *io = vlclua_metadata_io_state(state);
    if (io == NULL) return false;
    if (io->incomplete >= VLC_LUA_METADATA_IO_LIMIT) { io->failed = true; return false; }
    ++io->incomplete; return true;
}
static inline void vlclua_metadata_io_complete(lua_State *state)
{
    struct vlclua_metadata_io *io = vlclua_metadata_io_state(state);
    if (io == NULL) return;
    if (io->incomplete != 0) --io->incomplete;
    else io->failed = true;
}

/* The first return keeps the Lua URL/nil contract. Only this closed second
 * return can supply negative native evidence. It never enters item metadata.
 */
static inline meta_fetcher_outcome_t vlclua_metadata_art_outcome(lua_State *state,
                                                              int value, int outcome)
{
    size_t value_size = 0;
    const bool artwork = lua_type(state, value) == LUA_TSTRING
        && lua_tolstring(state, value, &value_size) != NULL && value_size != 0;
    if (lua_isnoneornil(state, outcome))
        return artwork ? META_FETCHER_OUTCOME_MATCH : META_FETCHER_OUTCOME_UNKNOWN;
    if (lua_type(state, outcome) != LUA_TSTRING) return META_FETCHER_OUTCOME_ERROR;
    size_t size = 0;
    const char *text = lua_tolstring(state, outcome, &size);
    static const struct {
        const char *text;
        size_t size;
        meta_fetcher_outcome_t value;
    } names[] = {
        {"match", 5, META_FETCHER_OUTCOME_MATCH},
        {"no_match", 8, META_FETCHER_OUTCOME_NO_MATCH},
        {"not_applicable", 14, META_FETCHER_OUTCOME_NOT_APPLICABLE},
        {"unavailable", 11, META_FETCHER_OUTCOME_UNAVAILABLE},
        {"denied", 6, META_FETCHER_OUTCOME_DENIED},
        {"error", 5, META_FETCHER_OUTCOME_ERROR},
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (size != names[i].size || memcmp(text, names[i].text, size)) continue;
        if (names[i].value == META_FETCHER_OUTCOME_MATCH)
            return artwork ? META_FETCHER_OUTCOME_MATCH : META_FETCHER_OUTCOME_ERROR;
        if (!lua_isnoneornil(state, value)) return META_FETCHER_OUTCOME_ERROR;
        if (names[i].value == META_FETCHER_OUTCOME_NO_MATCH
            || names[i].value == META_FETCHER_OUTCOME_NOT_APPLICABLE) {
            const struct vlclua_metadata_io *io = vlclua_metadata_io_state(state);
            if (io == NULL || io->failed || io->incomplete != 0
                || (names[i].value == META_FETCHER_OUTCOME_NOT_APPLICABLE && io->attempts != 0))
                return META_FETCHER_OUTCOME_UNAVAILABLE;
        }
        return names[i].value;
    }
    return META_FETCHER_OUTCOME_ERROR;
}
#endif
