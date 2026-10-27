// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_artwork_policy.h>
#include <vlc_input_item.h>
#include <vlc_input_policy.h>
#include <vlc_metadata_revision.h>
#include <vlc_stream.h>
#include <vlc_threads.h>
#include <assert.h>
#include <stdatomic.h>
#include "../../../src/libvlc.h"
#include "../../../lib/libvlc_internal.h"
#include "../../../src/preparser/art.h"
#include "../../../src/misc/interrupt.h"

static atomic_uint stream_calls;
static char *last_url;
static stream_t *test_stream(vlc_object_t *owner, const char *url)
{
    (void)owner; atomic_fetch_add(&stream_calls, 1); free(last_url); last_url = strdup(url); assert(last_url != NULL); return NULL;
}
static void *test_object(vlc_object_t *owner, size_t size, const char *name)
{ (void)owner; (void)size; (void)name; abort(); }
static int test_find_cache(input_item_t *item)
{ (void)item; abort(); }
static int test_save(vlc_object_t *owner, input_item_t *item, const void *data, size_t size, const char *type)
{ (void)owner; (void)item; (void)data; (void)size; (void)type; abort(); }
static void test_interrupt_init(vlc_interrupt_t *context)
{
    vlc_mutex_init(&context->lock); context->interrupted = false; atomic_init(&context->killed, false);
    context->callback = NULL; context->data = NULL;
}
static void test_interrupt_deinit(vlc_interrupt_t *context)
{ assert(context->callback == NULL); }
/* The status setters are private. This fixture tests artwork access. */
static void test_art_status(input_item_t *item, bool value)
{ (void)item; (void)value; }
#undef vlc_stream_NewURL
#undef vlc_custom_create
#define vlc_stream_NewURL test_stream
#define vlc_custom_create test_object
#define input_FindArtInCache test_find_cache
#define input_FindArtInCacheUsingItemUID test_find_cache
#define input_SaveArt test_save
#define vlc_interrupt_init test_interrupt_init
#define vlc_interrupt_deinit test_interrupt_deinit
#define input_item_SetArtFetched test_art_status
#define input_item_SetArtNotFound test_art_status
#include "../../../src/preparser/fetcher.c"
#undef vlc_stream_NewURL
#undef vlc_custom_create
#undef input_FindArtInCache
#undef input_FindArtInCacheUsingItemUID
#undef input_SaveArt
#undef vlc_interrupt_init
#undef vlc_interrupt_deinit
#undef input_item_SetArtFetched
#undef input_item_SetArtNotFound

struct completion { vlc_sem_t done; bool fetched; };
static void completed(input_item_t *item, bool fetched, void *opaque)
{ (void)item; struct completion *state = opaque; state->fetched = fetched; vlc_sem_post(&state->done); }
static bool allow_url(void *opaque, const char *url)
{ (void)opaque; (void)url; return true; }
static bool allow_connection(void *opaque, const char *host, unsigned port, bool datagram)
{ (void)opaque; (void)host; (void)port; (void)datagram; return true; }
static bool fetch(input_fetcher_t *fetcher, input_item_t *item)
{
    struct completion result = {0}; vlc_sem_init(&result.done, 0);
    static const input_fetcher_callbacks_t callbacks = {.on_art_fetch_ended=completed};
    assert(input_fetcher_Push(fetcher, item, VLC_PREPARSER_TYPE_FETCHMETA_LOCAL, &callbacks, &result) == VLC_SUCCESS);
    assert(vlc_sem_timedwait(&result.done, vlc_tick_now() + VLC_TICK_FROM_SEC(5)) == 0);
    return result.fetched;
}
struct scenario {
    const char *media, *artwork, *cached;
    bool approve, private_policy, revoke, fetched;
    unsigned streams;
};
static void run(const struct scenario *scenario)
{
    libvlc_int_t *instance = libvlc_InternalCreate(); assert(instance != NULL);
    assert(var_Create(instance, "fetch-art-threads", VLC_VAR_INTEGER) == VLC_SUCCESS);
    assert(var_SetInteger(instance, "fetch-art-threads", 1) == VLC_SUCCESS);
    input_fetcher_t *fetcher = input_fetcher_New(VLC_OBJECT(instance), VLC_PREPARSER_TYPE_FETCHMETA_LOCAL); assert(fetcher != NULL);
    input_item_t *item = input_item_New(scenario->media, "Artwork"); assert(item != NULL);
    input_item_SetTitle(item, "Title"); input_item_SetArtist(item, "Artist"); input_item_SetAlbum(item, "Album");
    vlc_input_policy_t *policy = NULL;
    if (scenario->private_policy) {
        static const struct vlc_input_policy_operations operations = {allow_url, allow_connection, NULL};
        policy = vlc_input_policy_New(&operations, NULL); assert(policy != NULL);
        assert(vlc_input_policy_AttachItem(item, policy) == VLC_SUCCESS);
    }
    if (scenario->approve) assert(vlc_artwork_item_SetApprovedURI(item, scenario->artwork) == VLC_SUCCESS);
    else input_item_SetArtURL(item, scenario->artwork);
    if (scenario->revoke) vlc_input_policy_Revoke(policy);
    vlc_mutex_lock(&item->lock); const uint64_t revision = vlc_meta_FieldRevision(item->p_meta, vlc_meta_ArtworkURL); vlc_mutex_unlock(&item->lock);
    if (scenario->cached != NULL) {
        char *key = CreateCacheKey(item), *value = strdup(scenario->cached); assert(key != NULL && value != NULL);
        vlc_dictionary_insert(&fetcher->album_cache, key, value); free(key);
    }
    atomic_store(&stream_calls, 0); free(last_url); last_url = NULL;
    const bool fetched = fetch(fetcher, item);
    input_fetcher_Delete(fetcher);
    assert(fetched == scenario->fetched && atomic_load(&stream_calls) == scenario->streams);
    if (scenario->streams) assert(last_url != NULL && !strcmp(last_url, scenario->artwork));
    if (scenario->approve && !scenario->revoke) {
        char *uri = vlc_artwork_item_GetURI(item); assert(uri != NULL && !strcmp(uri, scenario->artwork)); free(uri);
        vlc_mutex_lock(&item->lock); assert(vlc_meta_FieldRevision(item->p_meta, vlc_meta_ArtworkURL) == revision); vlc_mutex_unlock(&item->lock);
    }
    if (scenario->cached != NULL && !vlc_artwork_item_IsRestricted(item)) {
        char *uri = input_item_GetArtURL(item); assert(uri != NULL && !strcmp(uri, scenario->cached)); free(uri);
    }
    vlc_input_policy_Release(policy); input_item_Release(item); libvlc_InternalDestroy(instance);
}
static void test_shared_cache(void)
{
    libvlc_int_t *instance = libvlc_InternalCreate(); assert(instance != NULL);
    assert(var_Create(instance, "fetch-art-threads", VLC_VAR_INTEGER) == VLC_SUCCESS);
    assert(var_SetInteger(instance, "fetch-art-threads", 1) == VLC_SUCCESS);
    input_fetcher_t *fetcher = input_fetcher_New(VLC_OBJECT(instance), VLC_PREPARSER_TYPE_FETCHMETA_LOCAL); assert(fetcher != NULL);
    input_item_t *source = input_item_New("vlc-addon-source:v1:raw", "Source"),
                 *native = input_item_New("file:///native.flac", "Native");
    assert(source != NULL && native != NULL);
    input_item_t *items[] = {source, native};
    for (size_t i = 0; i < ARRAY_SIZE(items); ++i) {
        input_item_SetTitle(items[i], "Title"); input_item_SetArtist(items[i], "Artist"); input_item_SetAlbum(items[i], "Album");
    }
    input_item_SetArtURL(source, "http://127.0.0.1/private.png");
    input_item_SetArtURL(native, "file:///native.png");
    atomic_store(&stream_calls, 0);
    assert(!fetch(fetcher, source));
    assert(fetch(fetcher, native));
    input_fetcher_Delete(fetcher);
    assert(atomic_load(&stream_calls) == 0);
    char *uri = input_item_GetArtURL(native); assert(uri != NULL && !strcmp(uri, "file:///native.png")); free(uri);
    input_item_Release(source); input_item_Release(native); libvlc_InternalDestroy(instance);
}
int main(void)
{
    const struct scenario cases[] = {
        {"VLC-ADDON-SOURCE:v1:raw", "http://127.0.0.1/private.png", "file:///cached.png", false, false, false, false, 0},
        {"vlc-addon-source:v1:raw", "file:///private.png", NULL, false, false, false, false, 0},
        {"vlc-addon-source:v1:raw", "attachment://private.png", NULL, false, false, false, false, 0},
        {"vlc-addon-source:v1:approved", "file:///approved.png", "file:///other-cache.png", true, false, false, true, 0},
        {"vlc-addon-source:v1:approved", "file:///approved.png", "file:///approved.png", true, false, false, true, 0},
        {"file:///native-scoped.flac", "http://127.0.0.1/private.png", NULL, false, true, false, false, 0},
        {"vlc-addon-source:v1:revoked", "file:///approved.png", NULL, true, true, true, false, 0},
        {"file:///native.flac", "file:///native.png", "file:///cached.png", false, false, false, true, 0},
        {"file:///native.flac", "attachment://native.png", NULL, false, false, false, true, 0},
        {"file:///native.flac", "http://127.0.0.1/native.png", NULL, false, false, false, false, 1},
    };
    for (size_t i = 0; i < ARRAY_SIZE(cases); ++i) run(&cases[i]);
    test_shared_cache();
    free(last_url); return 0;
}
