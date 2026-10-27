// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_LUA_META_SELECTOR_H
#define VLC_LUA_META_SELECTOR_H 1
#include <vlc_common.h>
#include <vlc_meta_fetcher.h>

static inline bool vlclua_metadata_system_file(const char *filename, const char *directory, const char *relative)
{
    if (filename == NULL || directory == NULL || directory[0] == '\0') return false;
    const size_t length = strlen(directory);
    return !strncmp(filename, directory, length) && filename[length] == DIR_SEP_CHAR
        && !strcmp(filename + length + 1, relative);
}
static inline bool vlclua_metadata_script_selected(meta_fetcher_provider_t provider, const char *filename,
    const char *libdir, const char *datadir)
{
    if (provider == META_FETCHER_PROVIDER_ANY) return true;
    static const char *const names[][2] = {
        {NULL, NULL}, {NULL, NULL},
        {"lua" DIR_SEP "meta" DIR_SEP "art" DIR_SEP "00_musicbrainz.lua", "lua" DIR_SEP "meta" DIR_SEP "art" DIR_SEP "00_musicbrainz.luac"},
        {"lua" DIR_SEP "meta" DIR_SEP "art" DIR_SEP "01_googleimage.lua", "lua" DIR_SEP "meta" DIR_SEP "art" DIR_SEP "01_googleimage.luac"},
        {"lua" DIR_SEP "meta" DIR_SEP "art" DIR_SEP "02_frenchtv.lua", "lua" DIR_SEP "meta" DIR_SEP "art" DIR_SEP "02_frenchtv.luac"},
        {"lua" DIR_SEP "meta" DIR_SEP "art" DIR_SEP "03_lastfm.lua", "lua" DIR_SEP "meta" DIR_SEP "art" DIR_SEP "03_lastfm.luac"},
    };
    if (provider <= META_FETCHER_PROVIDER_NONE || (unsigned)provider >= ARRAY_SIZE(names)) return false;
    return vlclua_metadata_system_file(filename, libdir, names[provider][0])
        || vlclua_metadata_system_file(filename, libdir, names[provider][1])
        || vlclua_metadata_system_file(filename, datadir, names[provider][0])
        || vlclua_metadata_system_file(filename, datadir, names[provider][1]);
}
#endif
