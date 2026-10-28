/*****************************************************************************
 * meta.c: Get meta/artwork using lua scripts
 *****************************************************************************
 * Copyright (C) 2007-2008 the VideoLAN team
 *
 * Authors: Antoine Cellerier <dionoea at videolan tod org>
 *          Pierre d'Herbemont <pdherbemont # videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/
#ifndef  _GNU_SOURCE
#   define  _GNU_SOURCE
#endif

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "vlc.h"
#include "libs.h"
#include "meta_selector.h"
#include "meta_outcome.h"
#include "../../src/input/stream_observer.h"
#include <vlc_configuration.h>

/*****************************************************************************
 * Init lua
 *****************************************************************************/
static void open_library(lua_State *L, const char *name, lua_CFunction function)
{
#if LUA_VERSION_NUM >= 502
    luaL_requiref(L, name, function, 1); lua_pop(L, 1);
#else
    lua_pushcfunction(L, function); lua_pushstring(L, name); lua_call(L, 1, 0);
#endif
}

static lua_State *init(vlc_object_t *p_this, input_item_t *p_item, bool artwork)
{
    lua_State * L = luaL_newstate();
    if( !L )
    {
        msg_Err( p_this, "Could not create new Lua State" );
        return NULL;
    }

    vlclua_set_this( L, p_this );

    /* The shipped scripts need no file loader or external Lua module. */
    open_library(L, "_G", luaopen_base);
    open_library(L, LUA_STRLIBNAME, luaopen_string);
    if (artwork) {
        open_library(L, LUA_TABLIBNAME, luaopen_table);
        open_library(L, LUA_MATHLIBNAME, luaopen_math);
    }
    static const char *const loaders[] = {"dofile", "loadfile", "load", "loadstring", "require", "module"};
    for (unsigned i = 0; i < ARRAY_SIZE(loaders); ++i) { lua_pushnil(L); lua_setglobal(L, loaders[i]); }

    static const luaL_Reg p_reg[] = { { NULL, NULL } };
    luaL_register_namespace( L, "vlc", p_reg );

    if (artwork) vlclua_metadata_io_watch(L);

    luaopen_msg( L );
    if (artwork) { luaopen_stream(L); luaopen_strings(L); }
    luaopen_input_item( L, p_item );

    return L;
}

static bool selected_script(const char *filename, meta_fetcher_provider_t provider, bool reader)
{
    char *libdir = config_GetSysPath(VLC_PKG_LIBEXEC_DIR, NULL);
    char *datadir = config_GetSysPath(VLC_PKG_DATA_DIR, NULL);
    const bool selected = reader ? vlclua_metadata_reader_selected(filename, libdir, datadir)
        : vlclua_metadata_script_selected(provider, filename, libdir, datadir);
    free(libdir); free(datadir); return selected;
}

/*****************************************************************************
 * Run a lua entry point function
 *****************************************************************************/
static int run( vlc_object_t *p_this, const char * psz_filename,
                lua_State * L, const char *luafunction,
                const luabatch_context_t *p_context, int results )
{
    /* Ugly hack to delete previous versions of the fetchart()
     * functions. */
    lua_pushnil( L );
    lua_setglobal( L, luafunction );

    /* Load and run the script(s) */
    if( vlclua_dofile( p_this, L, psz_filename ) )
    {
        msg_Warn( p_this, "Error loading script %s: %s", psz_filename,
                 lua_tostring( L, lua_gettop( L ) ) );
        goto error;
    }


    meta_fetcher_scope_t e_scope = FETCHER_SCOPE_NETWORK; /* default to restricted one */
    lua_getglobal( L, "descriptor" );
    if( lua_isfunction( L, lua_gettop( L ) ) && !lua_pcall( L, 0, 1, 0 ) )
    {
        lua_getfield( L, -1, "scope" );
        char *psz_scope = luaL_strdupornull( L, -1 );
        if ( psz_scope && !strcmp( psz_scope, "local" ) )
            e_scope = FETCHER_SCOPE_LOCAL;
        free( psz_scope );
        lua_pop( L, 1 );
    }
    lua_pop( L, 1 );

    if ( p_context && p_context->pf_validator && !p_context->pf_validator( p_context, e_scope ) )
    {
        msg_Dbg( p_this, "skipping script (unmatched scope) %s", psz_filename );
        if (p_context->p_outcome != NULL) *p_context->p_outcome = META_FETCHER_OUTCOME_DENIED;
        goto error;
    }

    lua_getglobal( L, luafunction );

    if( !lua_isfunction( L, lua_gettop( L ) ) )
    {
        msg_Warn( p_this, "Error while running script %s, "
                 "function %s() not found", psz_filename, luafunction );
        goto error;
    }

    if( lua_pcall( L, 0, results, 0 ) )
    {
        msg_Warn( p_this, "Error while running script %s, "
                 "function %s(): %s", psz_filename, luafunction,
                 lua_tostring( L, lua_gettop( L ) ) );
        goto error;
    }
    return VLC_SUCCESS;

error:
    if (p_context != NULL && p_context->p_outcome != NULL
        && *p_context->p_outcome != META_FETCHER_OUTCOME_DENIED)
        *p_context->p_outcome = META_FETCHER_OUTCOME_ERROR;
    lua_pop( L, 1 );
    return VLC_EGENERIC;
}

/*****************************************************************************
 * Called through lua_scripts_batch_execute to call 'fetch_art' on the script
 * pointed by psz_filename.
 *****************************************************************************/
static bool validate_scope( const luabatch_context_t *p_context, meta_fetcher_scope_t e_scope )
{
    return p_context->e_scope == FETCHER_SCOPE_ANY
        || p_context->e_scope == e_scope;
}

static int fetch_art( vlc_object_t *p_this, const char * psz_filename,
                      const luabatch_context_t *p_context )
{
    if (p_context->e_scope != FETCHER_SCOPE_NETWORK && p_context->e_scope != FETCHER_SCOPE_ANY) {
        if (p_context->p_outcome != NULL) *p_context->p_outcome = META_FETCHER_OUTCOME_DENIED;
        return VLC_EGENERIC;
    }
    if (!selected_script(psz_filename, p_context->e_provider, false)) return VLC_EGENERIC;
    lua_State *L = init(p_this, p_context->p_item, true);
    if( !L )
    {
        if (p_context->p_outcome != NULL) *p_context->p_outcome = META_FETCHER_OUTCOME_ERROR;
        return VLC_EGENERIC;
    }

    int i_ret = run(p_this, psz_filename, L, "fetch_art", p_context, 2);
    if(i_ret != VLC_SUCCESS)
    {
        lua_close( L );
        return i_ret;
    }

    if (p_context->p_outcome != NULL)
        *p_context->p_outcome = vlclua_metadata_art_outcome(L, -2, -1);
    lua_pop(L, 1);

    if(lua_gettop(L) == 0)
    {
        msg_Err(p_this, "Script went completely foobar");
        goto error;
    }

    if (!lua_isstring(L, -1))
    {
        if (!lua_isnoneornil(L, -1))
            msg_Err(p_this, "Lua art fetcher script %s: "
                 "didn't return a string", psz_filename);
        goto error;
    }

    const char *psz_value = lua_tostring(L, -1);
    if (psz_value && *psz_value != 0)
    {
        lua_Dbg(p_this, "setting arturl: %s", psz_value);
        input_item_SetArtURL(p_context->p_item, psz_value);
    }

    lua_close(L);
    return VLC_SUCCESS;

error:
    lua_close( L );
    return VLC_EGENERIC;
}

/*****************************************************************************
 * Called through lua_scripts_batch_execute to call 'read_meta' on the script
 * pointed by psz_filename.
 *****************************************************************************/
static int read_meta( vlc_object_t *p_this, const char * psz_filename,
                      const luabatch_context_t *p_context )
{
    /* FIXME: merge with finder */
    demux_meta_t *p_demux_meta = (demux_meta_t *)p_this;
    VLC_UNUSED( p_context );
    if (!selected_script(psz_filename, META_FETCHER_PROVIDER_NONE, true)) return VLC_EGENERIC;

    lua_State *L = init(p_this, p_demux_meta->p_item, false);
    if( !L )
        return VLC_EGENERIC;

    int i_ret = run(p_this, psz_filename, L, "read_meta", NULL, 1);
    lua_close( L );

    // Continue even if an error occurred: all "meta reader" are always run.
    return i_ret == VLC_SUCCESS ? VLC_EGENERIC : i_ret;
}


/*****************************************************************************
 * Read meta.
 *****************************************************************************/

int ReadMeta( demux_meta_t *p_this )
{
    if( lua_Disabled( p_this ) )
        return VLC_EGENERIC;

    return vlclua_scripts_batch_execute( VLC_OBJECT(p_this), "meta"DIR_SEP"reader",
                                         (void*) &read_meta, NULL );
}


/*****************************************************************************
 * Read meta.
 *****************************************************************************/

int FetchMeta( meta_fetcher_t *p_finder )
{
    /* Core ships no Lua metadata fetcher. Do not scan an add-on directory. */
    VLC_UNUSED(p_finder); return VLC_EGENERIC;
}


/*****************************************************************************
 * Module entry point for art.
 *****************************************************************************/
int FindArt( meta_fetcher_t *p_finder )
{
    p_finder->e_outcome = META_FETCHER_OUTCOME_UNKNOWN;
    if( lua_Disabled( p_finder ) ) {
        p_finder->e_outcome = META_FETCHER_OUTCOME_UNAVAILABLE;
        return VLC_EGENERIC;
    }
    if (p_finder->e_provider == META_FETCHER_PROVIDER_NONE) {
        p_finder->e_outcome = META_FETCHER_OUTCOME_DENIED;
        return VLC_EGENERIC;
    }

    luabatch_context_t context = {.p_item=p_finder->p_item, .e_scope=p_finder->e_scope,
        .pf_validator=validate_scope, .e_provider=p_finder->e_provider,
        .p_outcome=&p_finder->e_outcome};

    /* The scope is installed before a constructor can probe or start prefetch.
     * Allocation failure only removes negative evidence; it does not change
     * the first URL/nil result or prevent the legacy lookup. */
    vlc_stream_observer_t *observer = vlc_stream_observer_New();
    const bool watched = observer != NULL
        && vlc_stream_observer_AttachObject(VLC_OBJECT(p_finder), observer) == VLC_SUCCESS;
    const int result = vlclua_scripts_batch_execute(VLC_OBJECT(p_finder),
        "meta"DIR_SEP"art", &fetch_art, (void*)&context);
    /* Each VM has closed here. Stream destruction joins the prefetch worker.
     * No observer lock was held across a module or Lua callback. */
    if (p_finder->e_outcome == META_FETCHER_OUTCOME_NO_MATCH
        || p_finder->e_outcome == META_FETCHER_OUTCOME_NOT_APPLICABLE) {
        const bool no_io = p_finder->e_outcome == META_FETCHER_OUTCOME_NOT_APPLICABLE;
        if (!watched || !vlc_stream_observer_Seal(observer, no_io))
            p_finder->e_outcome = META_FETCHER_OUTCOME_UNAVAILABLE;
    } else vlc_stream_observer_Close(observer);
    vlc_stream_observer_Release(observer);
    return result;
}
