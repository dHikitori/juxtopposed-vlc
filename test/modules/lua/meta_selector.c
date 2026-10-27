// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <assert.h>
#include "../../../modules/lua/meta_selector.h"
int main(void)
{
    const char *library = "/app" DIR_SEP "lib", *data = "/app" DIR_SEP "share";
    assert(!vlclua_metadata_script_selected(META_FETCHER_PROVIDER_ANY, "/user/old.lua", library, data));
    assert(!vlclua_metadata_script_selected(META_FETCHER_PROVIDER_NONE, "/app" DIR_SEP "share" DIR_SEP "lua" DIR_SEP "meta" DIR_SEP "art" DIR_SEP "00_musicbrainz.lua", library, data));
    assert(vlclua_metadata_script_selected(META_FETCHER_PROVIDER_MUSICBRAINZ, "/app" DIR_SEP "share" DIR_SEP "lua" DIR_SEP "meta" DIR_SEP "art" DIR_SEP "00_musicbrainz.lua", library, data));
    assert(vlclua_metadata_script_selected(META_FETCHER_PROVIDER_MUSICBRAINZ, "/app" DIR_SEP "lib" DIR_SEP "lua" DIR_SEP "meta" DIR_SEP "art" DIR_SEP "00_musicbrainz.luac", library, data));
    assert(!vlclua_metadata_script_selected(META_FETCHER_PROVIDER_MUSICBRAINZ, "/user/lua/meta/art/00_musicbrainz.lua", library, data));
    assert(!vlclua_metadata_script_selected(META_FETCHER_PROVIDER_MUSICBRAINZ, "/app" DIR_SEP "share" DIR_SEP "lua" DIR_SEP "meta" DIR_SEP "art" DIR_SEP "01_googleimage.lua", library, data));
    assert(!vlclua_metadata_script_selected(META_FETCHER_PROVIDER_MUSICBRAINZ, "/app" DIR_SEP "share-old" DIR_SEP "lua" DIR_SEP "meta" DIR_SEP "art" DIR_SEP "00_musicbrainz.lua", library, data));
    assert(!vlclua_metadata_script_selected(META_FETCHER_PROVIDER_MUSICBRAINZ, "/app" DIR_SEP "share" DIR_SEP "lua" DIR_SEP "meta" DIR_SEP "art" DIR_SEP "extra" DIR_SEP "00_musicbrainz.lua", library, data));
    assert(!vlclua_metadata_script_selected(META_FETCHER_PROVIDER_MUSICBRAINZ, "/app" DIR_SEP "share" DIR_SEP "lua" DIR_SEP "meta" DIR_SEP "art" DIR_SEP "00_musicbrainz.lua.vle", library, data));
    assert(!vlclua_metadata_script_selected(META_FETCHER_PROVIDER_MUSICBRAINZ, "/app" DIR_SEP "share" DIR_SEP "lua" DIR_SEP "meta" DIR_SEP "fetcher" DIR_SEP "00_musicbrainz.lua", library, data));
    assert(!vlclua_metadata_script_selected((meta_fetcher_provider_t)999, "/app" DIR_SEP "share" DIR_SEP "lua" DIR_SEP "meta" DIR_SEP "art" DIR_SEP "00_musicbrainz.lua", library, data));
    assert(!vlclua_metadata_script_selected(META_FETCHER_PROVIDER_MUSICBRAINZ, NULL, library, data));
    assert(!vlclua_metadata_script_selected(META_FETCHER_PROVIDER_MUSICBRAINZ, "/app" DIR_SEP "share" DIR_SEP "lua" DIR_SEP "meta" DIR_SEP "art" DIR_SEP "00_musicbrainz.lua", NULL, NULL));
    static const char *const art[] = {"00_musicbrainz", "01_googleimage", "02_frenchtv", "03_lastfm"};
    const char *roots[] = {library, data};
    const char *extensions[] = {"lua", "luac"};
    for (unsigned i = 0; i < ARRAY_SIZE(art); ++i)
        for (unsigned r = 0; r < ARRAY_SIZE(roots); ++r)
            for (unsigned e = 0; e < ARRAY_SIZE(extensions); ++e) {
                char filename[256];
                snprintf(filename, sizeof(filename), "%s" DIR_SEP "lua" DIR_SEP "meta" DIR_SEP "art" DIR_SEP "%s.%s", roots[r], art[i], extensions[e]);
                assert(vlclua_metadata_script_selected(META_FETCHER_PROVIDER_ANY, filename, library, data));
                assert(vlclua_metadata_script_selected((meta_fetcher_provider_t)(META_FETCHER_PROVIDER_MUSICBRAINZ + i), filename, library, data));
            }
    assert(!vlclua_metadata_script_selected(META_FETCHER_PROVIDER_ANY, "/app" DIR_SEP "share" DIR_SEP "lua" DIR_SEP "meta" DIR_SEP "art" DIR_SEP "04_custom.lua", library, data));
    assert(!vlclua_metadata_script_selected(META_FETCHER_PROVIDER_ANY, "/user/lua/meta/art/00_musicbrainz.lua", library, data));
    for (unsigned r = 0; r < ARRAY_SIZE(roots); ++r)
        for (unsigned e = 0; e < ARRAY_SIZE(extensions); ++e) {
            char filename[256];
            snprintf(filename, sizeof(filename), "%s" DIR_SEP "lua" DIR_SEP "meta" DIR_SEP "reader" DIR_SEP "filename.%s", roots[r], extensions[e]);
            assert(vlclua_metadata_reader_selected(filename, library, data));
        }
    assert(!vlclua_metadata_reader_selected("/user/lua/meta/reader/filename.lua", library, data));
    assert(!vlclua_metadata_reader_selected("/app" DIR_SEP "share-old" DIR_SEP "lua" DIR_SEP "meta" DIR_SEP "reader" DIR_SEP "filename.lua", library, data));
    assert(!vlclua_metadata_reader_selected("/app" DIR_SEP "share" DIR_SEP "lua" DIR_SEP "meta" DIR_SEP "reader" DIR_SEP "other.lua", library, data));
    assert(!vlclua_metadata_reader_selected("/app" DIR_SEP "share" DIR_SEP "lua" DIR_SEP "meta" DIR_SEP "reader" DIR_SEP "filename.lua.vle", library, data));
    assert(!vlclua_metadata_reader_selected("/app" DIR_SEP "share" DIR_SEP "lua" DIR_SEP "meta" DIR_SEP "art" DIR_SEP "filename.lua", library, data));
    assert(!vlclua_metadata_reader_selected(NULL, library, data));
    assert(!vlclua_metadata_reader_selected("/app" DIR_SEP "share" DIR_SEP "lua" DIR_SEP "meta" DIR_SEP "reader" DIR_SEP "filename.lua", NULL, NULL));
    return 0;
}
