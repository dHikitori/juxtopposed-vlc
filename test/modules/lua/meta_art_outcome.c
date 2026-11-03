// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <lua.h>
#include <lauxlib.h>
#include <assert.h>
#include "../../../modules/lua/meta_outcome.h"

static void check(lua_State *state, const char *value, const char *outcome,
                  meta_fetcher_outcome_t expected)
{
    lua_settop(state, 0);
    if (value != NULL) lua_pushstring(state, value); else lua_pushnil(state);
    if (outcome != NULL) lua_pushstring(state, outcome); else lua_pushnil(state);
    assert(vlclua_metadata_art_outcome(state, 1, 2) == expected);
}
int main(void)
{
    lua_State *state = luaL_newstate(); assert(state != NULL);
    vlclua_metadata_io_watch(state);
    check(state, "https://image.example/a.png", NULL, META_FETCHER_OUTCOME_MATCH);
    check(state, "https://image.example/a.png", "match", META_FETCHER_OUTCOME_MATCH);
    check(state, NULL, NULL, META_FETCHER_OUTCOME_UNKNOWN);
    check(state, NULL, "no_match", META_FETCHER_OUTCOME_NO_MATCH);
    check(state, NULL, "not_applicable", META_FETCHER_OUTCOME_NOT_APPLICABLE);
    check(state, NULL, "unavailable", META_FETCHER_OUTCOME_UNAVAILABLE);
    check(state, NULL, "denied", META_FETCHER_OUTCOME_DENIED);
    check(state, NULL, "error", META_FETCHER_OUTCOME_ERROR);
    check(state, NULL, "match", META_FETCHER_OUTCOME_ERROR);
    check(state, "", "no_match", META_FETCHER_OUTCOME_ERROR);
    check(state, "https://image.example/a.png", "no_match", META_FETCHER_OUTCOME_ERROR);
    check(state, NULL, "NO_MATCH", META_FETCHER_OUTCOME_ERROR);
    check(state, NULL, "unknown", META_FETCHER_OUTCOME_ERROR);
    lua_settop(state, 0); lua_pushboolean(state, false); lua_pushliteral(state, "no_match");
    assert(vlclua_metadata_art_outcome(state, 1, 2) == META_FETCHER_OUTCOME_ERROR);
    lua_settop(state, 0); lua_pushnil(state); lua_pushinteger(state, 1);
    assert(vlclua_metadata_art_outcome(state, 1, 2) == META_FETCHER_OUTCOME_ERROR);
    lua_settop(state, 0); lua_pushnil(state); lua_pushlstring(state, "no_match\0", 9);
    assert(vlclua_metadata_art_outcome(state, 1, 2) == META_FETCHER_OUTCOME_ERROR);
    lua_close(state);
    for (unsigned row = 0; row < 7; ++row) {
        state = luaL_newstate(); assert(state != NULL);
        if (row != 0) vlclua_metadata_io_watch(state);
        if (row == 2 || row == 3 || row == 4) {
            vlclua_metadata_io_attempt(state);
            assert(vlclua_metadata_io_opened(state));
        }
        if (row == 3 || row == 4) vlclua_metadata_io_complete(state);
        if (row == 4) vlclua_metadata_io_failure(state);
        if (row == 5) {
            vlclua_metadata_io_attempt(state);
            vlclua_metadata_io_failure(state);
        }
        if (row == 6)
            for (unsigned i = 0; i <= VLC_LUA_METADATA_IO_LIMIT; ++i) vlclua_metadata_io_attempt(state);
        check(state, NULL, "no_match", row == 1 || row == 3
            ? META_FETCHER_OUTCOME_NO_MATCH : META_FETCHER_OUTCOME_UNAVAILABLE);
        check(state, NULL, "not_applicable", row == 1
            ? META_FETCHER_OUTCOME_NOT_APPLICABLE : META_FETCHER_OUTCOME_UNAVAILABLE);
        check(state, "https://image.example/a.png", "match", META_FETCHER_OUTCOME_MATCH);
        if (row == 4) {
            vlclua_metadata_io_watch(state);
            check(state, NULL, "no_match", META_FETCHER_OUTCOME_UNAVAILABLE);
        }
        lua_close(state);
    }
    return 0;
}
