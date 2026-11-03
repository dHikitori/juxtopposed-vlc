// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#define MODULE_NAME test_artwork_fallback
#undef VLC_DYNAMIC_PLUGIN
#include "../../libvlc/test.h"
#include "../../../lib/libvlc_internal.h"
#include <vlc_input_item.h>
#include <vlc_meta.h>
#include <vlc_meta_fetcher.h>
#include <vlc_preparser.h>
#include <vlc_plugin.h>
#include <vlc_fs.h>
#include <vlc_url.h>
#include <vlc_atomic.h>
#include <limits.h>

const char vlc_module_name[] = MODULE_STRING;
static char *art_uri;
static bool network_art, metadata_available;
static atomic_uint metadata_calls, local_art_calls, network_art_calls;

static int FetchMetadata(vlc_object_t *object)
{
    meta_fetcher_t *fetcher = (meta_fetcher_t *)object;
    atomic_fetch_add(&metadata_calls, 1);
    if (!metadata_available)
        return VLC_EGENERIC;
    input_item_SetTitle(fetcher->p_item, "Provider title");
    input_item_SetArtist(fetcher->p_item, "Provider artist");
    input_item_SetAlbum(fetcher->p_item, "Provider album");
    return VLC_SUCCESS;
}

static int FindArtwork(vlc_object_t *object)
{
    meta_fetcher_t *fetcher = (meta_fetcher_t *)object;
    if (fetcher->e_scope == FETCHER_SCOPE_LOCAL) {
        atomic_fetch_add(&local_art_calls, 1);
        if (network_art)
            return VLC_EGENERIC;
    } else {
        assert(fetcher->e_scope == FETCHER_SCOPE_NETWORK);
        atomic_fetch_add(&network_art_calls, 1);
    }
    input_item_SetArtURL(fetcher->p_item, art_uri);
    return VLC_SUCCESS;
}

vlc_module_begin()
    set_capability("meta fetcher", INT_MAX)
    set_callback(FetchMetadata)
    add_submodule()
        set_capability("art finder", INT_MAX)
        set_callback(FindArtwork)
vlc_module_end()
VLC_EXPORT const vlc_plugin_cb vlc_static_modules[] = {VLC_SYMBOL(vlc_entry), NULL};

struct completion {
    vlc_sem_t done;
    atomic_uint calls;
};

static void Ended(vlc_preparser_req *req, int status, void *opaque)
{
    (void)req; (void)status;
    struct completion *completion = opaque;
    atomic_fetch_add(&completion->calls, 1);
    vlc_sem_post(&completion->done);
}

static void Check(vlc_object_t *owner, unsigned tags, bool allow_network,
                  bool provider_network, bool known_art, bool fill_metadata)
{
    assert(var_SetBool(owner, "metadata-network-access", allow_network) == VLC_SUCCESS);
    network_art = provider_network;
    metadata_available = fill_metadata;
    atomic_store(&metadata_calls, 0);
    atomic_store(&local_art_calls, 0);
    atomic_store(&network_art_calls, 0);
    char *uri;
    assert(asprintf(&uri, "file:///owned-artwork-fallback-%u-%u-%u-%u-%u",
                    tags, allow_network, provider_network, known_art, fill_metadata) >= 0);
    input_item_t *item = input_item_New(uri, "Artwork fallback fixture");
    free(uri);
    assert(item);
    if (tags & 1) input_item_SetTitle(item, "Existing title");
    if (tags & 2) input_item_SetArtist(item, "Existing artist");
    if (tags & 4) input_item_SetAlbum(item, "Existing album");
    if (known_art) input_item_SetArtURL(item, art_uri);

    const int options = allow_network ? VLC_PREPARSER_TYPE_FETCHMETA_ALL
                                     : VLC_PREPARSER_TYPE_FETCHMETA_LOCAL;
    struct vlc_preparser_cfg cfg = {.types = options};
    vlc_preparser_t *parser = vlc_preparser_New(owner, &cfg);
    assert(parser);
    struct completion completion;
    vlc_sem_init(&completion.done, 0);
    atomic_init(&completion.calls, 0);
    static const struct vlc_preparser_cbs callbacks = {.on_ended = Ended};
    vlc_preparser_req *req = vlc_preparser_req_NewParse(parser, item, options,
                                                     &callbacks, &completion);
    assert(req);
    assert(vlc_preparser_Submit(parser, req) == VLC_SUCCESS);
    vlc_preparser_req_Release(req);
    vlc_sem_wait(&completion.done);
    vlc_preparser_Delete(parser);
    assert(atomic_load(&completion.calls) == 1);

    const bool expected = known_art || !provider_network || allow_network;
    char *art = input_item_GetArtURL(item);
    fprintf(stderr, "tags=%u network=%u provider_network=%u known_art=%u metadata=%u: art=%s\n",
            tags, allow_network, provider_network, known_art, fill_metadata, art ? art : "none");
    assert(input_item_IsArtFetched(item) == expected);
    assert(expected ? art && !strcmp(art, art_uri) : art == NULL);
    if (!allow_network) assert(atomic_load(&network_art_calls) == 0);
    if (known_art) assert(atomic_load(&local_art_calls) == 0 && atomic_load(&network_art_calls) == 0);
    if (tags == 7) assert(atomic_load(&metadata_calls) == 0);
    if (!fill_metadata && (tags & 1)) {
        char *title = input_item_GetTitle(item);
        assert(title && !strcmp(title, "Existing title"));
        free(title);
    }
    free(art);
    input_item_Release(item);
}

int main(void)
{
#if defined(HAVE_DYNAMIC_PLUGINS) && !defined(__ELF__) \
    && !(defined(__MACH__) && defined(HAVE_DYLIB_DYNAMIC_LOOKUP))
    // Match module_InitStaticModules(): these shared-library builds cannot
    // discover the providers exported by this standalone test executable.
    fputs("Test requires executable-provided built-in modules.\n", stderr);
    return 77;
#endif
    test_init();
    char path[] = "artwork-fallback-XXXXXX";
    int fd = vlc_mkstemp(path);
    assert(fd >= 0);
    vlc_close(fd);
    art_uri = vlc_path2uri(path, NULL);
    assert(art_uri);
    const char *args[] = {"--ignore-config", "--no-plugins-cache", "--no-plugins-scan", "--no-media-library"};
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(args), args);
    assert(instance);
    vlc_object_t *owner = VLC_OBJECT(instance->p_libvlc_int);
    assert(var_Create(owner, "metadata-network-access", VLC_VAR_BOOL) == VLC_SUCCESS);
    // Positive control first: the provider works when all tags are present.
    Check(owner, 7, false, false, false, false);
    for (unsigned tags = 0; tags < 8; ++tags) {
        Check(owner, tags, false, false, false, false);
        Check(owner, tags, true, true, false, false);
        Check(owner, tags, false, true, false, false);
        Check(owner, tags, false, true, true, false);
    }
    Check(owner, 0, true, true, false, true);
    libvlc_release(instance);
    free(art_uri);
    assert(vlc_unlink(path) == 0);
    return 0;
}
