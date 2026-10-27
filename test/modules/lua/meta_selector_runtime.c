// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_configuration.h>
#include <vlc_fs.h>
#include <vlc_input_item.h>
#include <vlc_meta_fetcher.h>
#include <vlc_modules.h>
#include <vlc/vlc.h>
#include "../../../lib/libvlc_internal.h"
#include <assert.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
static void quoted(FILE *file, const char *value)
{
    fputc('"', file);
    for (const unsigned char *at = (const unsigned char *)value; *at; ++at) {
        if (*at == '\\' || *at == '"') fputc('\\', file);
        fputc(*at, file);
    }
    fputc('"', file);
}
static void script(const char *path, const char *marker, const char *artwork)
{
    FILE *file = vlc_fopen(path, "wb"); assert(file != NULL);
    fputs("local f=assert(io.open(", file); quoted(file, marker); fputs(", 'wb'));f:write('executed');f:close()\n", file);
    fputs("function descriptor() return {scope='network'} end\nfunction fetch_art() return ", file);
    quoted(file, artwork); fputs(" end\n", file); assert(fclose(file) == 0);
}
static bool exists(const char *path)
{ struct stat state; return vlc_stat(path, &state) == 0; }
static bool probe(libvlc_instance_t *instance, meta_fetcher_provider_t provider, meta_fetcher_scope_t scope, const char *expected)
{
    meta_fetcher_t *finder = vlc_object_create(VLC_OBJECT(instance->p_libvlc_int), sizeof(*finder)); assert(finder != NULL);
    finder->p_item = input_item_New("file:///fixture/song.flac", "Song"); assert(finder->p_item != NULL);
    finder->e_provider = provider; finder->e_scope = scope;
    module_t *module = module_need(finder, "art finder", "lua-metadata-provider", true);
    if (module != NULL) module_unneed(finder, module);
    char *artwork = input_item_GetArtURL(finder->p_item);
    assert(expected != NULL ? artwork != NULL && !strcmp(artwork, expected) : artwork == NULL);
    free(artwork); input_item_Release(finder->p_item); vlc_object_delete(finder); return module != NULL;
}
int main(void)
{
    char root[] = "/tmp/vlc-meta-selector-XXXXXX"; assert(mkdtemp(root) != NULL);
    char user[512], data[512], user_lua[512], data_lua[512], user_meta[512], data_meta[512], user_art[512], data_art[512];
    snprintf(user, sizeof(user), "%s/user", root); snprintf(data, sizeof(data), "%s/system", root);
    snprintf(user_lua, sizeof(user_lua), "%s/user/lua", root); snprintf(data_lua, sizeof(data_lua), "%s/system/lua", root);
    snprintf(user_meta, sizeof(user_meta), "%s/user/lua/meta", root); snprintf(data_meta, sizeof(data_meta), "%s/system/lua/meta", root);
    snprintf(user_art, sizeof(user_art), "%s/user/lua/meta/art", root); snprintf(data_art, sizeof(data_art), "%s/system/lua/meta/art", root);
    const char *directories[] = {user, data, user_lua, data_lua, user_meta, data_meta, user_art, data_art};
    for (unsigned i = 0; i < ARRAY_SIZE(directories); ++i) assert(vlc_mkdir(directories[i], 0700) == 0);
    char system_script[512], user_script[512], other_script[512], system_marker[512], user_marker[512], other_marker[512];
    snprintf(system_script, sizeof(system_script), "%s/system/lua/meta/art/00_musicbrainz.lua", root);
    snprintf(user_script, sizeof(user_script), "%s/user/lua/meta/art/00_musicbrainz.lua", root);
    snprintf(other_script, sizeof(other_script), "%s/system/lua/meta/art/01_googleimage.lua", root);
    snprintf(system_marker, sizeof(system_marker), "%s/system-marker", root); snprintf(user_marker, sizeof(user_marker), "%s/user-marker", root);
    snprintf(other_marker, sizeof(other_marker), "%s/other-marker", root);
    script(system_script, system_marker, "file:///system-selected.png"); script(user_script, user_marker, "file:///user-selected.png");
    script(other_script, other_marker, "file:///other-selected.png");
    assert(setenv("VLC_USERDATA_PATH", user, 1) == 0 && setenv("VLC_DATA_PATH", data, 1) == 0 && setenv("VLC_LIBEXEC_PATH", data, 1) == 0);
    const char *arguments[] = {"--ignore-config", "--no-plugins-cache", "--no-media-library", "--quiet"};
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(arguments), arguments); assert(instance != NULL);
    assert(!probe(instance, META_FETCHER_PROVIDER_NONE, FETCHER_SCOPE_NETWORK, NULL));
    assert(!exists(system_marker) && !exists(user_marker) && !exists(other_marker));
    assert(probe(instance, META_FETCHER_PROVIDER_MUSICBRAINZ, FETCHER_SCOPE_NETWORK, "file:///system-selected.png"));
    assert(exists(system_marker) && !exists(user_marker) && !exists(other_marker)); assert(vlc_unlink(system_marker) == 0);
    assert(!probe(instance, META_FETCHER_PROVIDER_MUSICBRAINZ, FETCHER_SCOPE_LOCAL, NULL));
    assert(!exists(system_marker) && !exists(user_marker) && !exists(other_marker));
    assert(probe(instance, META_FETCHER_PROVIDER_GOOGLE_IMAGES, FETCHER_SCOPE_NETWORK, "file:///other-selected.png"));
    assert(exists(other_marker) && !exists(system_marker) && !exists(user_marker)); assert(vlc_unlink(other_marker) == 0);
    assert(probe(instance, META_FETCHER_PROVIDER_ANY, FETCHER_SCOPE_NETWORK, "file:///user-selected.png"));
    assert(exists(user_marker) && !exists(system_marker) && !exists(other_marker)); assert(vlc_unlink(user_marker) == 0);
    assert(!probe(instance, (meta_fetcher_provider_t)999, FETCHER_SCOPE_NETWORK, NULL));
    assert(!exists(system_marker) && !exists(user_marker) && !exists(other_marker));
    libvlc_release(instance);
    assert(vlc_unlink(system_script) == 0 && vlc_unlink(user_script) == 0 && vlc_unlink(other_script) == 0);
    for (unsigned i = ARRAY_SIZE(directories); i > 0; --i) assert(rmdir(directories[i - 1]) == 0);
    assert(rmdir(root) == 0); return 0;
}
#else
int main(void) { return 77; }
#endif
