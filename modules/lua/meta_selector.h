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
    static const char *const names[][2] = {
        {NULL, NULL}, {NULL, NULL},
        {"lua" DIR_SEP "meta" DIR_SEP "art" DIR_SEP "00_musicbrainz.lua", "lua" DIR_SEP "meta" DIR_SEP "art" DIR_SEP "00_musicbrainz.luac"},
        {"lua" DIR_SEP "meta" DIR_SEP "art" DIR_SEP "01_googleimage.lua", "lua" DIR_SEP "meta" DIR_SEP "art" DIR_SEP "01_googleimage.luac"},
        {"lua" DIR_SEP "meta" DIR_SEP "art" DIR_SEP "02_frenchtv.lua", "lua" DIR_SEP "meta" DIR_SEP "art" DIR_SEP "02_frenchtv.luac"},
        {"lua" DIR_SEP "meta" DIR_SEP "art" DIR_SEP "03_lastfm.lua", "lua" DIR_SEP "meta" DIR_SEP "art" DIR_SEP "03_lastfm.luac"},
    };
    if (provider != META_FETCHER_PROVIDER_ANY
        && (provider <= META_FETCHER_PROVIDER_NONE || (unsigned)provider >= ARRAY_SIZE(names))) return false;
    const unsigned first = provider == META_FETCHER_PROVIDER_ANY ? META_FETCHER_PROVIDER_MUSICBRAINZ : (unsigned)provider;
    const unsigned end = provider == META_FETCHER_PROVIDER_ANY ? ARRAY_SIZE(names) : (unsigned)provider + 1;
    for (unsigned i = first; i < end; ++i)
        if (vlclua_metadata_system_file(filename, libdir, names[i][0])
            || vlclua_metadata_system_file(filename, libdir, names[i][1])
            || vlclua_metadata_system_file(filename, datadir, names[i][0])
            || vlclua_metadata_system_file(filename, datadir, names[i][1])) return true;
    return false;
}
static inline bool vlclua_metadata_reader_selected(const char *filename, const char *libdir, const char *datadir)
{
    static const char *const names[] = {
        "lua" DIR_SEP "meta" DIR_SEP "reader" DIR_SEP "filename.lua",
        "lua" DIR_SEP "meta" DIR_SEP "reader" DIR_SEP "filename.luac",
    };
    for (unsigned i = 0; i < ARRAY_SIZE(names); ++i)
        if (vlclua_metadata_system_file(filename, libdir, names[i])
            || vlclua_metadata_system_file(filename, datadir, names[i])) return true;
    return false;
}
#endif
