// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_block.h>
#include <vlc_configuration.h>
#include <vlc_fs.h>
#include <vlc_input_item.h>
#include <vlc_meta_fetcher.h>
#include <vlc_metadata_backend.h>
#include <vlc_metadata_owner.h>
#include <vlc_demux.h>
#include <vlc_stream.h>
#include <vlc_input_policy.h>
#include <vlc_modules.h>
#include <vlc/vlc.h>
#include "../../../lib/libvlc_internal.h"
#include "../../../src/input/stream_observer.h"
#include <assert.h>
extern unsigned metadata_consume_decline_calls;
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
static void copy_file(const char *source, const char *target)
{
    FILE *input = vlc_fopen(source, "rb"), *output = vlc_fopen(target, "wb");
    assert(input != NULL && output != NULL);
    char buffer[4096]; size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), input)) != 0) assert(fwrite(buffer, 1, count, output) == count);
    assert(!ferror(input) && fclose(input) == 0 && fclose(output) == 0);
}
static void script(const char *path, const char *marker, const char *artwork, bool reader)
{
    FILE *file = vlc_fopen(path, "wb"); assert(file != NULL);
    /* Item metadata exposes top-level execution even when file I/O is absent. */
    fputs("vlc.item:set_meta(", file); quoted(file, marker); fputs(", 'executed')\n", file);
    fputs("assert(io==nil and os==nil and package==nil and debug==nil and require==nil)\n"
          "assert(dofile==nil and loadfile==nil and load==nil and loadstring==nil and module==nil)\n"
          "assert(vlc.object==nil and vlc.var==nil and vlc.xml==nil)\n", file);
    if (reader) fputs("assert(vlc.stream==nil and vlc.strings==nil)\n", file);
    else fputs("assert(type(vlc.stream)=='function' and type(vlc.strings.encode_uri_component)=='function')\n", file);
    fputs("vlc.item:set_meta('restricted_vm', 'yes')\nfunction descriptor() return {scope=", file);
    quoted(file, reader ? "local" : "network"); fputs("} end\n", file);
    if (reader) fputs("function read_meta() vlc.item:set_meta('reader_called', 'yes') end\n", file);
    else { fputs("function fetch_art() return ", file); quoted(file, artwork); fputs(" end\n", file); }
    assert(fclose(file) == 0);
}
static void extra(input_item_t *item, const char *name, const char *expected)
{
    char *value = input_item_GetMetaExtra(item, name);
    assert(expected != NULL ? value != NULL && !strcmp(value, expected) : value == NULL); free(value);
}
static void field(input_item_t *item, vlc_meta_type_t type, const char *expected)
{
    char *value = input_item_GetMeta(item, type);
    const bool matches = expected != NULL ? value != NULL && !strcmp(value, expected) : value == NULL;
    if (!matches) fprintf(stderr, "Field %d has %s. Expected %s.\n", type, value != NULL ? value : "NULL", expected != NULL ? expected : "NULL");
    assert(matches); free(value);
}
static void no_unselected(input_item_t *item)
{
    extra(item, "user_art", NULL); extra(item, "unknown_art", NULL);
    extra(item, "user_reader", NULL); extra(item, "unknown_reader", NULL); extra(item, "unknown_fetcher", NULL);
}
static bool art_probe(libvlc_instance_t *instance, meta_fetcher_provider_t provider,
                      meta_fetcher_scope_t scope, const char *expected, const char *marker)
{
    meta_fetcher_t *finder = vlc_object_create(VLC_OBJECT(instance->p_libvlc_int), sizeof(*finder)); assert(finder != NULL);
    finder->p_item = input_item_New("file:///fixture/song.flac", "Song"); assert(finder->p_item != NULL);
    finder->e_provider = provider; finder->e_scope = scope;
    module_t *module = module_need(finder, "art finder", "lua-metadata-provider", true);
    if (module != NULL) module_unneed(finder, module);
    char *actual = input_item_GetArtURL(finder->p_item);
    if (expected != NULL && (actual == NULL || strcmp(actual, expected)))
        fprintf(stderr, "Provider %u with scope %u has an unexpected result.\n", (unsigned)provider, (unsigned)scope);
    free(actual);
    field(finder->p_item, vlc_meta_ArtworkURL, expected); no_unselected(finder->p_item);
    extra(finder->p_item, "system_musicbrainz", marker != NULL && !strcmp(marker, "system_musicbrainz") ? "executed" : NULL);
    extra(finder->p_item, "system_google", marker != NULL && !strcmp(marker, "system_google") ? "executed" : NULL);
    extra(finder->p_item, "restricted_vm", marker != NULL ? "yes" : NULL);
    input_item_Release(finder->p_item); vlc_object_delete(finder); return module != NULL;
}
static void fetch_probe(libvlc_instance_t *instance, meta_fetcher_provider_t provider)
{
    meta_fetcher_t *finder = vlc_object_create(VLC_OBJECT(instance->p_libvlc_int), sizeof(*finder)); assert(finder != NULL);
    finder->p_item = input_item_New("file:///fixture/song.flac", "Song"); assert(finder->p_item != NULL);
    finder->e_provider = provider; finder->e_scope = FETCHER_SCOPE_ANY;
    module_t *module = module_need(finder, "meta fetcher", "lua-metadata-provider", true);
    assert(module == NULL); no_unselected(finder->p_item);
    input_item_Release(finder->p_item); vlc_object_delete(finder);
}
static void native_outcome_case(libvlc_instance_t *instance, const char *path,
    const char *value, const char *outcome, bool failure, meta_fetcher_scope_t scope,
    meta_fetcher_outcome_t expected, bool legacy_success)
{
    FILE *file = vlc_fopen(path, "wb"); assert(file != NULL);
    fputs("function descriptor() return {scope='network'} end\nfunction fetch_art()\n", file);
    if (failure) fputs("error('Fixture execution failure')\n", file);
    else {
        fputs("return ", file);
        if (value != NULL) quoted(file, value); else fputs("nil", file);
        fputs(", ", file);
        if (outcome != NULL) quoted(file, outcome); else fputs("nil", file);
        fputc('\n', file);
    }
    fputs("end\n", file); assert(fclose(file) == 0);
    meta_fetcher_t *finder = vlc_object_create(VLC_OBJECT(instance->p_libvlc_int), sizeof(*finder)); assert(finder);
    finder->p_item = input_item_New("file:///fixture/song.flac", "Song"); assert(finder->p_item);
    finder->e_provider = META_FETCHER_PROVIDER_MUSICBRAINZ; finder->e_scope = scope;
    finder->e_outcome = META_FETCHER_OUTCOME_UNKNOWN;
    module_t *module = module_need(finder, "art finder", "lua-metadata-provider", true);
    assert((module != NULL) == legacy_success);
    if (module != NULL) module_unneed(finder, module);
    assert(finder->e_outcome == expected);
    field(finder->p_item, vlc_meta_ArtworkURL, legacy_success ? value : NULL);
    input_item_Release(finder->p_item); vlc_object_delete(finder);
}
static void native_outcome_results(libvlc_instance_t *instance, const char *path)
{
    native_outcome_case(instance, path, "https://art.example/image.png", "match", false,
        FETCHER_SCOPE_NETWORK, META_FETCHER_OUTCOME_MATCH, true);
    native_outcome_case(instance, path, "https://art.example/image.png", NULL, false,
        FETCHER_SCOPE_NETWORK, META_FETCHER_OUTCOME_MATCH, true);
    native_outcome_case(instance, path, NULL, "no_match", false,
        FETCHER_SCOPE_NETWORK, META_FETCHER_OUTCOME_NO_MATCH, false);
    native_outcome_case(instance, path, NULL, "not_applicable", false,
        FETCHER_SCOPE_NETWORK, META_FETCHER_OUTCOME_NOT_APPLICABLE, false);
    native_outcome_case(instance, path, NULL, "unavailable", false,
        FETCHER_SCOPE_NETWORK, META_FETCHER_OUTCOME_UNAVAILABLE, false);
    native_outcome_case(instance, path, NULL, "denied", false,
        FETCHER_SCOPE_NETWORK, META_FETCHER_OUTCOME_DENIED, false);
    native_outcome_case(instance, path, NULL, NULL, false,
        FETCHER_SCOPE_NETWORK, META_FETCHER_OUTCOME_UNKNOWN, false);
    native_outcome_case(instance, path, NULL, "NO_MATCH", false,
        FETCHER_SCOPE_NETWORK, META_FETCHER_OUTCOME_ERROR, false);
    native_outcome_case(instance, path, "https://art.example/image.png", "no_match", false,
        FETCHER_SCOPE_NETWORK, META_FETCHER_OUTCOME_ERROR, true);
    native_outcome_case(instance, path, NULL, NULL, true,
        FETCHER_SCOPE_NETWORK, META_FETCHER_OUTCOME_ERROR, false);
    native_outcome_case(instance, path, "https://art.example/image.png", "match", false,
        FETCHER_SCOPE_LOCAL, META_FETCHER_OUTCOME_DENIED, false);
    script(path, "system_musicbrainz", "file:///system-selected.png", false);
}
static void native_io_case(libvlc_instance_t *instance, const char *path,
                           const char *body, meta_fetcher_outcome_t expected, bool match)
{
    FILE *file = vlc_fopen(path, "wb"); assert(file != NULL);
    fputs("function descriptor() return {scope='network'} end\nfunction fetch_art()\n", file);
    fputs(body, file); fputs("\nend\n", file); assert(fclose(file) == 0);
    meta_fetcher_t *finder = vlc_object_create(VLC_OBJECT(instance->p_libvlc_int), sizeof(*finder)); assert(finder);
    finder->p_item = input_item_New("file:///fixture/song.flac", "Song"); assert(finder->p_item);
    finder->e_provider = META_FETCHER_PROVIDER_MUSICBRAINZ; finder->e_scope = FETCHER_SCOPE_NETWORK;
    finder->e_outcome = META_FETCHER_OUTCOME_UNKNOWN;
    module_t *module = module_need(finder, "art finder", "lua-metadata-provider", true);
    assert((module != NULL) == match);
    if (module != NULL) module_unneed(finder, module);
    assert(finder->e_outcome == expected);
    field(finder->p_item, vlc_meta_ArtworkURL, match ? "https://art.example/image.png" : NULL);
    input_item_Release(finder->p_item); vlc_object_delete(finder);
}
static void native_io_outcomes(libvlc_instance_t *instance, const char *path)
{
    native_io_case(instance, path,
        "local s=vlc.stream('hakone-native-io-fixture-missing://resource')\n"
        "assert(s==nil)\nreturn nil,'no_match'", META_FETCHER_OUTCOME_UNAVAILABLE, false);
    native_io_case(instance, path,
        "local s=vlc.memory_stream('empty')\nassert(s:read(64)=='empty')\nreturn nil,'no_match'",
        META_FETCHER_OUTCOME_NO_MATCH, false);
    native_io_case(instance, path,
        "local s=vlc.memory_stream('empty-tail')\nassert(s:read(5)=='empty')\nreturn nil,'no_match'",
        META_FETCHER_OUTCOME_UNAVAILABLE, false);
    native_io_case(instance, path,
        "local s=vlc.memory_stream('empty')\nassert(s:read(64)=='empty')\nreturn nil,'not_applicable'",
        META_FETCHER_OUTCOME_UNAVAILABLE, false);
    native_io_case(instance, path,
        "local s=vlc.stream('hakone-native-io-fixture-missing://resource')\n"
        "assert(s==nil)\nreturn 'https://art.example/image.png','match'",
        META_FETCHER_OUTCOME_MATCH, true);
    script(path, "system_musicbrainz", "file:///system-selected.png", false);
}
static input_item_t *reader_probe(libvlc_instance_t *instance, const char *uri, const char *title, bool restricted)
{
    demux_meta_t *reader = vlc_object_create(VLC_OBJECT(instance->p_libvlc_int), sizeof(*reader)); assert(reader != NULL);
    reader->p_item = input_item_New(uri, "User item name"); assert(reader->p_item != NULL);
    reader->p_meta = NULL; reader->i_attachments = 0; reader->attachments = NULL;
    if (title != NULL) input_item_SetTitle(reader->p_item, title);
    module_t *module = module_need(reader, "meta reader", NULL, false);
    if (module != NULL) module_unneed(reader, module);
    no_unselected(reader->p_item);
    extra(reader->p_item, "restricted_vm", restricted ? "yes" : NULL);
    extra(reader->p_item, "reader_called", restricted ? "yes" : NULL);
    input_item_t *item = reader->p_item; vlc_object_delete(reader); return item;
}
static int no_dispatch(void *opaque, vlc_metadata_job_t *job)
{ (void)opaque; (void)job; abort(); }
static void filename_probe(libvlc_instance_t *instance, const char *uri, const char *title,
                           const char *expected_title, const char *show, const char *season, const char *episode)
{
    input_item_t *item = reader_probe(instance, uri, title, false);
    field(item, vlc_meta_Title, expected_title); extra(item, "showName", show);
    extra(item, "seasonNumber", season); extra(item, "episodeNumber", episode);
    /* Retain the shipped script's exact output names and case. */
    field(item, vlc_meta_ShowName, NULL); field(item, vlc_meta_Season, NULL); field(item, vlc_meta_Episode, NULL);
    input_item_Release(item);
}
static bool allow_url(void *opaque, const char *url)
{ (void)opaque; (void)url; return true; }
static bool allow_connection(void *opaque, const char *host, unsigned port, bool datagram)
{ (void)opaque; (void)host; (void)port; (void)datagram; return true; }
static void playlist_probe(libvlc_instance_t *instance, const char *script_path, const char *marker, unsigned scope)
{
    FILE *script_file = vlc_fopen(script_path, "wb"); assert(script_file != NULL);
    fputs("local f=assert(io.open(", script_file); quoted(script_file, marker);
    fputs(", 'wb'));f:write('executed');f:close()\n"
          "function probe() return vlc.peek(6)=='POLICY' end\n"
          "function parse() return {{path='file:///fixture/local-track.flac',name='Ordinary playlist entry'}} end\n", script_file);
    assert(fclose(script_file) == 0);
    struct stat state; assert(vlc_stat(marker, &state) != 0);
    vlc_object_t *ancestor = vlc_object_create(VLC_OBJECT(instance->p_libvlc_int), sizeof(*ancestor)); assert(ancestor != NULL);
    vlc_object_t *parent = NULL;
    vlc_input_policy_t *policy = NULL;
    if (scope != 0) {
        static const struct vlc_input_policy_operations operations = {allow_url, allow_connection, NULL};
        policy = vlc_input_policy_New(&operations, NULL); assert(policy != NULL);
        assert(vlc_input_policy_AttachObject(ancestor, policy) == VLC_SUCCESS);
        if (scope == 3) vlc_input_policy_Revoke(policy);
    }
    if (scope == 2) { parent = vlc_object_create(ancestor, sizeof(*parent)); assert(parent != NULL); }
    else parent = ancestor;
    assert(vlc_input_policy_Has(parent) == (scope != 0));
    static const uint8_t payload[] = "POLICY\n";
    stream_t *source = vlc_stream_MemoryNew(parent, (uint8_t *)payload, sizeof(payload) - 1, true); assert(source != NULL);
    source->psz_url = strdup("mock://policy-fixture"); assert(source->psz_url != NULL);
    demux_t *demux = demux_New(parent, "luaplaylist", source->psz_url, source, NULL);
    assert((demux != NULL) == (scope == 0));
    assert((vlc_stat(marker, &state) == 0) == (scope == 0));
    if (demux != NULL) {
        input_item_t *item = input_item_New(INPUT_ITEM_URI_NOP, NULL); assert(item != NULL);
        input_item_node_t *node = input_item_node_Create(item); assert(node != NULL);
        assert(vlc_stream_ReadDir(demux, node) == VLC_SUCCESS && node->i_children == 1);
        assert(!strcmp(node->pp_children[0]->p_item->psz_name, "Ordinary playlist entry"));
        assert(!strcmp(node->pp_children[0]->p_item->psz_uri, "file:///fixture/local-track.flac"));
        input_item_node_Delete(node); input_item_Release(item); demux_Delete(demux);
        assert(vlc_unlink(marker) == 0);
    } else vlc_stream_Delete(source);
    if (parent != ancestor) vlc_object_delete(parent);
    vlc_object_delete(ancestor); vlc_input_policy_Release(policy);
}
static void filter_profile_probe(libvlc_instance_t *instance, const char *name,
                                  bool selected, bool accepted, bool consumed_decline)
{
    vlc_object_t *parent = vlc_object_create(VLC_OBJECT(instance->p_libvlc_int), sizeof(*parent));
    assert(parent != NULL);
    vlc_stream_observer_t *observer = vlc_stream_observer_New();
    assert(observer != NULL);
    assert(vlc_stream_observer_AttachObject(parent, observer) == VLC_SUCCESS);
    uint8_t data[] = "xempty";
    stream_t *source = vlc_stream_MemoryNew(parent, consumed_decline ? data : data + 1,
                                           consumed_decline ? 6 : 5, true);
    assert(source != NULL);
    const unsigned probe_calls = metadata_consume_decline_calls;
    stream_t *filter = vlc_stream_FilterNew(source, name);
    assert((filter != NULL) == selected);
    if (consumed_decline) assert(metadata_consume_decline_calls == probe_calls + 1);
    stream_t *output = filter != NULL ? filter : source;
    char bytes[64];
    assert(vlc_stream_Read(output, bytes, sizeof(bytes)) == 5);
    assert(!memcmp(bytes, "empty", 5));
    vlc_stream_Delete(output);
    assert(vlc_stream_observer_Seal(observer, false) == accepted);
    vlc_stream_observer_Release(observer);
    vlc_object_delete(parent);
}

struct block_profile_source { bool sent, error_sent, fail; };
static block_t *block_profile_read(stream_t *stream, bool *eof)
{
    struct block_profile_source *source = stream->p_sys;
    if (!source->sent) {
        source->sent = true;
        block_t *block = block_Alloc(5);
        assert(block != NULL);
        memcpy(block->p_buffer, "empty", 5);
        return block;
    }
    if (source->fail && !source->error_sent) {
        source->error_sent = true;
        return NULL; /* Preserve block error: no EOF flag. */
    }
    *eof = true;
    return NULL;
}
static int block_profile_control(stream_t *stream, int query, va_list args)
{
    (void)stream;
    switch (query) {
        case STREAM_CAN_SEEK:
        case STREAM_CAN_FASTSEEK:
        case STREAM_CAN_PAUSE:
        case STREAM_CAN_CONTROL_PACE:
            *va_arg(args, bool *) = false;
            return VLC_SUCCESS;
        default:
            return VLC_EGENERIC;
    }
}
static void block_profile_destroy(stream_t *stream) { (void)stream; }
static void block_cache_profile(libvlc_instance_t *instance, bool error)
{
    vlc_object_t *parent = vlc_object_create(VLC_OBJECT(instance->p_libvlc_int), sizeof(*parent));
    assert(parent != NULL);
    vlc_stream_observer_t *observer = vlc_stream_observer_New();
    assert(observer != NULL);
    assert(vlc_stream_observer_AttachObject(parent, observer) == VLC_SUCCESS);
    struct block_profile_source state = {.fail=error};
    stream_t *source = vlc_stream_CommonNew(parent, block_profile_destroy);
    assert(source != NULL);
    source->p_sys = &state;
    source->pf_block = block_profile_read;
    source->pf_control = block_profile_control;
    stream_t *cache = vlc_stream_FilterNew(source, "cache");
    assert(cache != NULL);
    char bytes[64];
    assert(vlc_stream_Read(cache, bytes, sizeof(bytes)) == 5);
    assert(!memcmp(bytes, "empty", 5));
    vlc_stream_Delete(cache);
    assert(vlc_stream_observer_Seal(observer, false) == !error);
    vlc_stream_observer_Release(observer);
    vlc_object_delete(parent);
}

int main(void)
{
    char root[] = "/tmp/vlc-meta-selector-XXXXXX"; assert(mkdtemp(root) != NULL);
    const char *suffixes[] = {"user", "system", "user/lua", "system/lua", "user/lua/meta", "system/lua/meta",
        "user/lua/meta/art", "system/lua/meta/art", "user/lua/meta/reader", "system/lua/meta/reader",
        "user/lua/meta/fetcher", "system/lua/meta/fetcher", "user/lua/playlist", "system/lua/playlist"};
    char directories[ARRAY_SIZE(suffixes)][512];
    for (unsigned i = 0; i < ARRAY_SIZE(suffixes); ++i) {
        snprintf(directories[i], sizeof(directories[i]), "%s/%s", root, suffixes[i]); assert(vlc_mkdir(directories[i], 0700) == 0);
    }
    const char *file_suffixes[] = {"system/lua/meta/art/00_musicbrainz.lua", "user/lua/meta/art/00_musicbrainz.lua",
        "system/lua/meta/art/01_googleimage.lua", "system/lua/meta/art/00_custom.lua",
        "user/lua/meta/reader/filename.lua", "system/lua/meta/reader/00_custom.lua",
        "system/lua/meta/reader/filename.lua", "system/lua/meta/reader/filename.luac",
        "user/lua/meta/fetcher/custom.lua", "system/lua/meta/fetcher/custom.lua", "user/lua/playlist/00_policy_fixture.lua"};
    char files[ARRAY_SIZE(file_suffixes)][512];
    for (unsigned i = 0; i < ARRAY_SIZE(file_suffixes); ++i) snprintf(files[i], sizeof(files[i]), "%s/%s", root, file_suffixes[i]);
    script(files[0], "system_musicbrainz", "file:///system-selected.png", false);
    script(files[1], "user_art", "file:///user-selected.png", false);
    script(files[2], "system_google", "file:///other-selected.png", false);
    script(files[3], "unknown_art", "file:///unknown-selected.png", false);
    script(files[4], "user_reader", NULL, true); script(files[5], "unknown_reader", NULL, true);
    script(files[6], "system_reader", NULL, true);
    script(files[8], "unknown_fetcher", NULL, true); script(files[9], "unknown_fetcher", NULL, true);
    assert(setenv("VLC_USERDATA_PATH", directories[0], 1) == 0 && setenv("VLC_DATA_PATH", directories[1], 1) == 0
        && setenv("VLC_LIBEXEC_PATH", directories[1], 1) == 0);
    const char *arguments[] = {"--ignore-config", "--no-plugins-cache", "--no-media-library",
        getenv("VLC_TEST_VERBOSE") != NULL ? "--verbose=2" : "--quiet"};
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(arguments), arguments); assert(instance != NULL);
    if (getenv("VLC_TEST_VERBOSE") != NULL) libvlc_log_set_file(instance, stderr);
    assert(!art_probe(instance, META_FETCHER_PROVIDER_NONE, FETCHER_SCOPE_NETWORK, NULL, NULL));
    assert(art_probe(instance, META_FETCHER_PROVIDER_MUSICBRAINZ, FETCHER_SCOPE_NETWORK, "file:///system-selected.png", "system_musicbrainz"));
    assert(!art_probe(instance, META_FETCHER_PROVIDER_MUSICBRAINZ, FETCHER_SCOPE_LOCAL, NULL, NULL));
    assert(art_probe(instance, META_FETCHER_PROVIDER_GOOGLE_IMAGES, FETCHER_SCOPE_NETWORK, "file:///other-selected.png", "system_google"));
    assert(vlc_unlink(files[0]) == 0 && vlc_unlink(files[2]) == 0);
    assert(!art_probe(instance, META_FETCHER_PROVIDER_ANY, FETCHER_SCOPE_NETWORK, NULL, NULL));
    script(files[0], "system_musicbrainz", "file:///system-selected.png", false);
    assert(art_probe(instance, META_FETCHER_PROVIDER_ANY, FETCHER_SCOPE_NETWORK, "file:///system-selected.png", "system_musicbrainz"));
    script(files[2], "system_google", "file:///other-selected.png", false);
    assert(!art_probe(instance, META_FETCHER_PROVIDER_ANY, FETCHER_SCOPE_LOCAL, NULL, NULL));
    assert(!art_probe(instance, (meta_fetcher_provider_t)999, FETCHER_SCOPE_NETWORK, NULL, NULL));
    fetch_probe(instance, META_FETCHER_PROVIDER_ANY); fetch_probe(instance, META_FETCHER_PROVIDER_MUSICBRAINZ);
    native_outcome_results(instance, files[0]);
    native_io_outcomes(instance, files[0]);
    filter_profile_probe(instance, "inflate", false, true, false); /* Real Peek-only decline. */
    filter_profile_probe(instance, "cache", true, true, false); /* Prebuffer consumes source. */
    filter_profile_probe(instance, "record", true, false, false); /* Bytes stay, proof is refused. */
    filter_profile_probe(instance, "metadata_consume_decline_fixture,cache", true, false, true);
    block_cache_profile(instance, false);
    block_cache_profile(instance, true);
    input_item_t *item = reader_probe(instance, "file:///fixture/Show.Name.S01E12.avi", NULL, true); input_item_Release(item);
    copy_file(TOP_SRCDIR "/share/lua/meta/reader/filename.lua", files[6]);
    filename_probe(instance, "file:///fixture/Show.Name.S01E12-blah.avi", NULL, "Show Name S01E12", "Show Name", "01", "12");
    filename_probe(instance, "file:///fixture/Show.Name.S01E12-blah.avi", "User title", "User title", NULL, NULL, NULL);
    filename_probe(instance, "file:///fixture/Show.Name.S01E12-blah.avi", "", "", NULL, NULL, NULL);
    filename_probe(instance, "file:///fixture/Show%20Name.S02E03.avi", NULL, "Show Name S02E03", "Show Name", "02", "03");
    filename_probe(instance, "file:///fixture/Show.Name.s01e12.avi", NULL, NULL, NULL, NULL, NULL);
    filename_probe(instance, "file:///fixture/no-match.flac", NULL, NULL, NULL, NULL, NULL);
    filename_probe(instance, "custom:Show.Name.S01E12.avi", NULL, NULL, NULL, NULL, NULL);
    static const struct vlc_metadata_backend_operations ops = {.dispatch=no_dispatch};
    vlc_metadata_backend_t *backend = vlc_metadata_backend_New(&ops, NULL); assert(backend != NULL);
    assert(vlc_metadata_owner_Set(VLC_OBJECT(instance->p_libvlc_int), backend) == VLC_SUCCESS);
    filename_probe(instance, "file:///fixture/Owned.Show.S03E04.avi", NULL, "Owned Show S03E04", "Owned Show", "03", "04");
    vlc_metadata_backend_Close(backend);
    filename_probe(instance, "file:///fixture/Closed.Show.S04E05.avi", NULL, "Closed Show S04E05", "Closed Show", "04", "05");
    vlc_metadata_backend_Release(backend);
    assert(vlc_unlink(files[6]) == 0);
    const char *compiled = getenv("VLC_TEST_FILENAME_LUAC");
    if (compiled == NULL) compiled = TOP_BUILDDIR "/modules/lua/meta/reader/filename.luac";
    FILE *bytecode = vlc_fopen(compiled, "rb"); unsigned char signature[4];
    assert(bytecode != NULL && fread(signature, 1, sizeof(signature), bytecode) == sizeof(signature));
    assert(!memcmp(signature, "\033Lua", 4) && fclose(bytecode) == 0);
    copy_file(compiled, files[7]);
    filename_probe(instance, "file:///fixture/Compiled.Show.S05E06.avi", NULL, "Compiled Show S05E06", "Compiled Show", "05", "06");
    char playlist_marker[512]; snprintf(playlist_marker, sizeof(playlist_marker), "%s/playlist-marker", root);
    for (unsigned scope = 0; scope < 4; ++scope) playlist_probe(instance, files[10], playlist_marker, scope);
    libvlc_release(instance);
    const char *disabled[] = {"--ignore-config", "--no-plugins-cache", "--no-media-library", "--quiet", "--no-lua"};
    instance = libvlc_new(ARRAY_SIZE(disabled), disabled); assert(instance != NULL);
    filename_probe(instance, "file:///fixture/Disabled.Show.S01E02.avi", NULL, NULL, NULL, NULL, NULL);
    assert(!art_probe(instance, META_FETCHER_PROVIDER_ANY, FETCHER_SCOPE_NETWORK, NULL, NULL));
    libvlc_release(instance);
    for (unsigned i = 0; i < ARRAY_SIZE(files); ++i) if (i != 6) assert(vlc_unlink(files[i]) == 0);
    for (unsigned i = ARRAY_SIZE(directories); i > 0; --i) assert(rmdir(directories[i - 1]) == 0);
    assert(rmdir(root) == 0); return 0;
}
#else
int main(void) { return 77; }
#endif
