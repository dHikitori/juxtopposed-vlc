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
    assert(vlclua_metadata_script_selected(META_FETCHER_PROVIDER_ANY, "/user/old.lua", library, data));
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
    return 0;
}
