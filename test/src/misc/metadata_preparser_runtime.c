// SPDX-License-Identifier: LGPL-2.1-or-later
#include "../../libvlc/test.h"
#include "../../../lib/libvlc_internal.h"
#include <vlc_input_item.h>
#include <vlc_metadata_backend.h>
#include <vlc_metadata_owner.h>
#include <vlc_preparser.h>
#include <vlc_url.h>

struct output {
    input_item_t *item;
    vlc_sem_t done;
    unsigned ended, trees, children, remote;
    int status;
};
static void ended(vlc_preparser_req *req, int status, void *opaque)
{
    struct output *output = opaque; assert(vlc_preparser_req_GetItem(req) == output->item);
    ++output->ended; output->status = status; vlc_sem_post(&output->done);
}
static void subtree(vlc_preparser_req *req, input_item_node_t *tree, void *opaque)
{
    struct output *output = opaque; assert(vlc_preparser_req_GetItem(req) == output->item);
    assert(tree->p_item == output->item); ++output->trees; output->children += tree->i_children;
    input_item_node_Delete(tree);
}
static int dispatch(void *opaque, vlc_metadata_job_t *job)
{
    struct output *output = opaque; const struct vlc_metadata_request *request = vlc_metadata_job_GetRequest(job);
    assert(!strcmp(request->operation, "materializeNativeArtwork") && request->flags == 0);
    ++output->remote;
    assert(vlc_metadata_job_Complete(job, VLC_ENOENT, NULL, NULL, 0)); return VLC_SUCCESS;
}
static void put16(unsigned char *at, unsigned value) { at[0] = value; at[1] = value >> 8; }
static void put32(unsigned char *at, unsigned value) { put16(at, value); put16(at + 2, value >> 16); }
static void create_wave(const char *path)
{
    unsigned char data[8044] = {0};
    memcpy(data, "RIFF", 4); put32(data + 4, sizeof(data) - 8); memcpy(data + 8, "WAVEfmt ", 8);
    put32(data + 16, 16); put16(data + 20, 1); put16(data + 22, 1); put32(data + 24, 8000);
    put32(data + 28, 8000); put16(data + 32, 1); put16(data + 34, 8); memcpy(data + 36, "data", 4); put32(data + 40, 8000);
    memset(data + 44, 128, 8000);
    FILE *file = fopen(path, "wb"); assert(file != NULL);
    assert(fwrite(data, 1, sizeof(data), file) == sizeof(data) && fclose(file) == 0);
}
static void parse_file(const char *path, bool playlist, unsigned owner_state, bool external)
{
    const char *arguments[] = {"--ignore-config", "--no-plugins-cache", "--no-media-library", "--quiet", "--no-metadata-network-access"};
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(arguments), arguments); assert(instance != NULL);
    struct output output = {0}; vlc_sem_init(&output.done, 0);
    static const struct vlc_metadata_backend_operations operations = {.dispatch=dispatch};
    vlc_metadata_backend_t *backend = vlc_metadata_backend_New(&operations, &output); assert(backend != NULL);
    if (owner_state != 0) assert(vlc_metadata_owner_Set(VLC_OBJECT(instance->p_libvlc_int), backend) == VLC_SUCCESS);
    if (owner_state == 2) vlc_metadata_backend_Close(backend);
    if (owner_state == 3) assert(vlc_metadata_backend_Suspend(backend));
    char *uri = vlc_path2uri(path, NULL); assert(uri != NULL);
    output.item = input_item_New(uri, "Native file"); free(uri); assert(output.item != NULL);
    input_item_SetTitle(output.item, "User title"); input_item_SetMetaExtra(output.item, "User extra", "Preserved extra");
    assert(input_item_AddInfo(output.item, "Existing", "Baseline", "%s", "Preserved") == VLC_SUCCESS);
    const int options = VLC_PREPARSER_TYPE_PARSE | VLC_PREPARSER_TYPE_FETCHMETA_ALL | VLC_PREPARSER_OPTION_SUBITEMS;
    const struct vlc_preparser_cfg cfg = {.types=VLC_PREPARSER_TYPE_PARSE | VLC_PREPARSER_TYPE_FETCHMETA_ALL, .timeout=VLC_TICK_FROM_SEC(5), .external_process=external};
    vlc_preparser_t *preparser = vlc_preparser_New(VLC_OBJECT(instance->p_libvlc_int), &cfg); assert(preparser != NULL);
    static const struct vlc_preparser_cbs callbacks = {.on_ended=ended,.on_subtree_added=subtree};
    vlc_preparser_req *request = vlc_preparser_req_NewParse(preparser, output.item, options, &callbacks, &output); assert(request != NULL);
    assert(vlc_preparser_Submit(preparser, request) == VLC_SUCCESS);
    assert(vlc_sem_timedwait(&output.done, vlc_tick_now() + VLC_TICK_FROM_SEC(10)) == 0);
    assert(output.ended == 1 && output.status == VLC_SUCCESS && input_item_IsPreparsed(output.item));
    assert(output.remote == (owner_state == 1 ? 1u : 0u));
    char *info = input_item_GetInfo(output.item, "Existing", "Baseline"); assert(info != NULL && !strcmp(info, "Preserved")); free(info);
    char *title = input_item_GetTitle(output.item), *extra = input_item_GetMetaExtra(output.item, "User extra");
    assert(title != NULL && !strcmp(title, "User title") && extra != NULL && !strcmp(extra, "Preserved extra")); free(title); free(extra);
    if (playlist) assert(output.trees != 0 && output.children == 1);
    else {
        assert(input_item_GetDuration(output.item) >= VLC_TICK_FROM_MS(900));
        assert(input_item_GetDuration(output.item) <= VLC_TICK_FROM_MS(1100));
        vlc_mutex_lock(&output.item->lock); assert(output.item->es_vec.size == 1); vlc_mutex_unlock(&output.item->lock);
    }
    vlc_preparser_req_Release(request); vlc_preparser_Delete(preparser); input_item_Release(output.item);
    vlc_metadata_backend_Close(backend); vlc_metadata_backend_Release(backend); libvlc_release(instance);
}
int main(void)
{
    test_init();
    assert(setenv("VLC_DATA_PATH", TOP_SRCDIR "/share", 1) == 0);
    char directory[] = "/tmp/hakone-metadata-parser-XXXXXX"; assert(mkdtemp(directory) != NULL);
    char *wave = NULL, *playlist = NULL;
    assert(asprintf(&wave, "%s/Show.Name.S02E03.wav", directory) >= 0);
    assert(asprintf(&playlist, "%s/playlist.m3u", directory) >= 0);
    create_wave(wave);
    FILE *file = fopen(playlist, "wb"); assert(file != NULL);
    assert(fprintf(file, "#EXTM3U\n#EXTINF:1,Child\n%s\n", wave) > 0 && fclose(file) == 0);
    for (unsigned external = 0; external < 2; ++external) {
#ifndef HAVE_VLC_EXTERNAL_PREPARSER
        if (external) break;
#endif
        for (unsigned owner = 0; owner < 4; ++owner) {
            parse_file(wave, false, owner, external); parse_file(playlist, true, owner, external);
        }
    }
    assert(unlink(wave) == 0 && unlink(playlist) == 0 && rmdir(directory) == 0);
    free(wave); free(playlist); return 0;
}
