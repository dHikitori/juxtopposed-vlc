// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
/* Reuse the controlled native executor and I/O fixture. The request, fetcher,
 * pipeline, backend, snapshot, and final native Apply use production code.
 * The separate module fixture proves the shipped Lua execution boundary. */
int metadata_fetcher_fixture_main(void);
#define main metadata_fetcher_fixture_main
#include "metadata_fetcher.c"
#undef main
#include "../../../src/preparser/metadata_request_guard.c"
#undef MAX_ENTRIES
#include "../../../src/preparser/metadata_request_events.c"
#undef MAX_ENTRIES
#undef MAX_DEPTH
#undef MAX_BYTES
#undef input_item_SetArtFetched
#undef input_item_SetArtNotFound

static input_item_t *original;
static unsigned parse_calls;
static bool parse_block;
static int parse_status;
static vlc_sem_t parse_entered;
struct input_item_parser_id_t { unsigned marker; };
static input_item_parser_id_t *native_parse(vlc_object_t *object, input_item_t *item,
    const struct input_item_parser_cfg *cfg)
{
    (void)object; ++parse_calls; assert(item != original);
    char *baseline = input_item_GetInfo(item, "Existing", "Baseline");
    assert(baseline != NULL && !strcmp(baseline, "Preserved")); free(baseline);
    input_item_SetDescription(item, "Native parse result"); input_item_SetDuration(item, VLC_TICK_FROM_SEC(42));
    assert(input_item_AddInfo(item, "Parsed", "Format", "%s", "Native test") == VLC_SUCCESS);
    input_item_node_t *tree = input_item_node_Create(item); assert(tree != NULL);
    cfg->cbs->on_subtree_added(item, tree, cfg->cbs_data);
    input_attachment_t *attachment = vlc_input_attachment_New("cover.bin", "application/octet-stream", "Cover", "data", 4);
    assert(attachment != NULL); cfg->cbs->on_attachments_added(item, &attachment, 1, cfg->cbs_data);
    vlc_input_attachment_Release(attachment);
    if (!parse_block) cfg->cbs->on_ended(item, parse_status, cfg->cbs_data);
    else vlc_sem_post(&parse_entered);
    input_item_parser_id_t *id = malloc(sizeof(*id)); assert(id != NULL); id->marker = 123; return id;
}
static void native_parser_release(input_item_parser_id_t *id)
{ assert(id->marker == 123); free(id); }
/* These thumbnail entry points are outside this parse fixture. Fail if the
 * parse route calls one. Do not replace an exercised native operation. */
#include "../../../src/input/input_internal.h"
static input_thread_t *unused_input_create(vlc_object_t *object, input_item_t *item, const struct vlc_input_thread_cfg *cfg)
{ (void)object; (void)item; (void)cfg; abort(); }
static int unused_input_start(input_thread_t *input) { (void)input; abort(); }
static void unused_input_end(input_thread_t *input) { (void)input; abort(); }
static void unused_input_time(input_thread_t *input, vlc_tick_t time, bool fast)
{ (void)input; (void)time; (void)fast; abort(); }
static void unused_input_position(input_thread_t *input, double position, bool fast)
{ (void)input; (void)position; (void)fast; abort(); }
const char vlc_module_name[] = "metadata-preparser-fixture";
#undef input_Create
#define input_Create unused_input_create
#define input_Start unused_input_start
#define input_Close unused_input_end
#define input_Stop unused_input_end
#define input_SetTime unused_input_time
#define input_SetPosition unused_input_position
#define input_item_Parse native_parse
#define input_item_parser_id_Release native_parser_release
#include "../../../src/preparser/internal.c"
#undef input_item_Parse
#undef input_item_parser_id_Release

struct request_fixture {
    libvlc_int_t *instance;
    vlc_metadata_backend_t *backend;
    struct preparser_sys *sys;
    struct vlc_preparser_t handle;
    input_item_t *item;
    struct vlc_preparser_req *req;
    unsigned ended, trees, attachments, remote;
    int status;
    bool release_in_callback, cancel_in_tree;
};
static int remote_dispatch(void *opaque, vlc_metadata_job_t *job)
{
    struct request_fixture *fixture = opaque; ++fixture->remote;
    input_item_t *copy = input_item_Copy(vlc_metadata_job_GetRequest(job)->item); assert(copy != NULL);
    assert(vlc_metadata_job_Complete(job, VLC_SUCCESS, copy, NULL, 0)); input_item_Release(copy); return VLC_SUCCESS;
}
static void request_ended(struct vlc_preparser_req *req, int status, void *opaque)
{
    struct request_fixture *fixture = opaque; ++fixture->ended; fixture->status = status;
    assert(vlc_preparser_req_GetItem(req) == fixture->item);
    struct vlc_preparser_req_owner *request = preparser_req_get_owner(req);
    assert(request->listed);
    if (fixture->release_in_callback) { fixture->req = NULL; vlc_preparser_req_Release(req); }
}
static void request_tree(struct vlc_preparser_req *req, input_item_node_t *tree, void *opaque)
{
    struct request_fixture *fixture = opaque; ++fixture->trees; assert(tree->p_item == fixture->item);
    assert(input_item_GetDuration(fixture->item) == VLC_TICK_FROM_SEC(42));
    if (fixture->cancel_in_tree) assert(preparser_Cancel(fixture->sys, req) == 1);
    input_item_node_Delete(tree);
}
static void request_attachments(struct vlc_preparser_req *req, input_attachment_t *const *array, size_t count, void *opaque)
{
    (void)req; struct request_fixture *fixture = opaque; ++fixture->attachments;
    assert(count == 1 && array[0]->i_data == 4 && !memcmp(array[0]->p_data, "data", 4));
}
static void request_init(struct request_fixture *fixture, bool owned)
{
    parse_calls = 0; parse_block = false; parse_status = VLC_SUCCESS; local_art = false;
    atomic_store(&module_calls, 0); atomic_store(&saved_assets, 0); atomic_store(&last_provider, -1);
    atomic_store(&block_module, false); atomic_store(&hold_after_interrupt, false);
    fixture->instance = libvlc_InternalCreate(); assert(fixture->instance != NULL);
    assert(var_Create(fixture->instance, "metadata-network-access", VLC_VAR_BOOL) == VLC_SUCCESS);
    assert(var_SetBool(fixture->instance, "metadata-network-access", true) == VLC_SUCCESS);
    assert(var_Create(fixture->instance, "fetch-art-threads", VLC_VAR_INTEGER) == VLC_SUCCESS);
    assert(var_SetInteger(fixture->instance, "fetch-art-threads", 1) == VLC_SUCCESS);
    const struct vlc_metadata_backend_operations ops = {.dispatch=remote_dispatch};
    fixture->backend = vlc_metadata_backend_New(&ops, fixture); assert(fixture->backend != NULL);
    if (owned) assert(vlc_metadata_owner_Set(VLC_OBJECT(fixture->instance), fixture->backend) == VLC_SUCCESS);
    const struct vlc_preparser_cfg cfg = {.types=VLC_PREPARSER_TYPE_PARSE | VLC_PREPARSER_TYPE_FETCHMETA_ALL};
    fixture->sys = vlc_preparser_internal_New(&fixture->handle, VLC_OBJECT(fixture->instance), &cfg); assert(fixture->sys != NULL);
    fixture->item = input_item_New("file:///private/song.flac", "Song"); assert(fixture->item != NULL); original = fixture->item;
    input_item_SetTitle(fixture->item, "Song");
    assert(input_item_AddInfo(fixture->item, "Existing", "Baseline", "%s", "Preserved") == VLC_SUCCESS);
}
static void request_submit(struct request_fixture *fixture, int options)
{
    static const struct vlc_preparser_cbs cbs = {.on_ended=request_ended,.on_subtree_added=request_tree,.on_attachments_added=request_attachments};
    fixture->req = preparser_req_NewParse(fixture->sys, fixture->item, options, &cbs, fixture); assert(fixture->req != NULL);
    assert(preparser_Submit(fixture->sys, fixture->req) == VLC_SUCCESS);
}
static void request_destroy(struct request_fixture *fixture)
{
    if (fixture->sys != NULL) preparser_Delete(fixture->sys);
    if (fixture->req != NULL) vlc_preparser_req_Release(fixture->req);
    input_item_Release(fixture->item);
    vlc_metadata_owner_Close(VLC_OBJECT(fixture->instance)); vlc_metadata_owner_Forget(VLC_OBJECT(fixture->instance));
    vlc_metadata_backend_Close(fixture->backend); vlc_metadata_backend_Release(fixture->backend); libvlc_InternalDestroy(fixture->instance);
}
static void local_parse_publishes_complete_output_after_admission(void)
{
    for (unsigned owned = 0; owned < 2; ++owned) {
        struct request_fixture fixture = {0}; request_init(&fixture, owned);
        request_submit(&fixture, VLC_PREPARSER_TYPE_PARSE);
        assert(fixture.ended == 0 && fixture.trees == 0 && input_item_GetDuration(fixture.item) == VLC_TICK_INVALID);
        run_next(fixture.sys->parser);
        assert(fixture.ended == 1 && fixture.status == VLC_SUCCESS && fixture.trees == 1 && fixture.attachments == 1);
        assert(fixture.remote == 0 && input_item_IsPreparsed(fixture.item));
        char *info = input_item_GetInfo(fixture.item, "Parsed", "Format"); assert(info != NULL && !strcmp(info, "Native test")); free(info);
        request_destroy(&fixture);
    }
}
static void queued_cancel_and_same_value_edits_publish_no_output(void)
{
    for (unsigned stale = 0; stale < 3; ++stale) {
        struct request_fixture fixture = {0}; request_init(&fixture, true); request_submit(&fixture, VLC_PREPARSER_TYPE_PARSE);
        if (stale == 0) assert(preparser_Cancel(fixture.sys, fixture.req) == 1);
        else {
            if (stale == 2) input_item_SetTitle(fixture.item, "Other title");
            input_item_SetTitle(fixture.item, "Song"); run_next(fixture.sys->parser);
        }
        assert(fixture.ended == 1 && fixture.status == (stale ? -EAGAIN : -EINTR));
        assert(fixture.trees == 0 && fixture.attachments == 0 && input_item_GetDuration(fixture.item) == VLC_TICK_INVALID);
        request_destroy(&fixture);
    }
}
static void absent_closed_and_offline_requests_keep_native_local_output(void)
{
    for (unsigned state = 0; state < 3; ++state) {
        struct request_fixture fixture = {0}; request_init(&fixture, state != 0);
        if (state == 1) vlc_metadata_owner_Close(VLC_OBJECT(fixture.instance));
        if (state == 2) assert(var_SetBool(fixture.instance, "metadata-network-access", false) == VLC_SUCCESS);
        request_submit(&fixture, VLC_PREPARSER_TYPE_PARSE | VLC_PREPARSER_TYPE_FETCHMETA_ALL);
        run_next(fixture.sys->parser); assert(fixture.ended == 0);
        run_next(fixture.sys->fetcher->executor_local);
        if (state == 0) {
            assert(fixture.sys->fetcher->executor_network->count == 1);
            run_next(fixture.sys->fetcher->executor_network);
        }
        assert(fixture.ended == 1 && fixture.status == VLC_SUCCESS && fixture.trees == 1 && fixture.attachments == 1);
        assert(input_item_GetDuration(fixture.item) == VLC_TICK_FROM_SEC(42));
        assert(fixture.remote == (state == 2 ? 1u : 0u)); request_destroy(&fixture);
    }
}
static void fetch_only_and_callback_reentry_keep_the_request_alive(void)
{
    struct request_fixture fixture = {.release_in_callback=true}; request_init(&fixture, true);
    request_submit(&fixture, VLC_PREPARSER_TYPE_FETCHMETA_LOCAL);
    run_next(fixture.sys->parser); run_next(fixture.sys->fetcher->executor_local);
    assert(fixture.ended == 1 && fixture.status == VLC_SUCCESS && fixture.remote == 1 && parse_calls == 0 && fixture.req == NULL);
    request_destroy(&fixture);
    struct request_fixture reentry = {.cancel_in_tree=true}; request_init(&reentry, true);
    request_submit(&reentry, VLC_PREPARSER_TYPE_PARSE); run_next(reentry.sys->parser);
    assert(reentry.ended == 1 && reentry.status == VLC_SUCCESS && reentry.trees == 1 && reentry.attachments == 1);
    request_destroy(&reentry);
}
static void *run_parser(void *opaque) { run_next(opaque); return NULL; }
static void running_parse_cancel_keeps_native_state_until_io_ends(void)
{
    struct request_fixture fixture = {.release_in_callback=true}; request_init(&fixture, true);
    vlc_sem_init(&parse_entered, 0); parse_block = true;
    request_submit(&fixture, VLC_PREPARSER_TYPE_PARSE);
    vlc_thread_t worker; assert(vlc_clone(&worker, run_parser, fixture.sys->parser) == 0);
    vlc_sem_wait(&parse_entered);
    assert(preparser_Cancel(fixture.sys, fixture.req) == 1);
    vlc_join(worker, NULL);
    assert(fixture.ended == 1 && fixture.status == -EINTR && fixture.req == NULL);
    assert(fixture.trees == 0 && fixture.attachments == 0 && input_item_GetDuration(fixture.item) == VLC_TICK_INVALID);
    request_destroy(&fixture);
}
static void cancellation_finds_the_native_fetch_stage(void)
{
    struct request_fixture fixture = {.release_in_callback=true}; request_init(&fixture, true);
    request_submit(&fixture, VLC_PREPARSER_TYPE_PARSE | VLC_PREPARSER_TYPE_FETCHMETA_ALL);
    run_next(fixture.sys->parser); assert(fixture.sys->fetcher->executor_local->count == 1);
    assert(preparser_Cancel(fixture.sys, fixture.req) == 1);
    assert(fixture.ended == 1 && fixture.status == -EINTR && fixture.req == NULL && fixture.remote == 0);
    assert(fixture.sys->fetcher->executor_local->count == 0 && fixture.trees == 0);
    request_destroy(&fixture);
}
static void owner_or_consent_transition_discards_only_the_legacy_copy(void)
{
    for (unsigned state = 0; state < 2; ++state) {
        struct request_fixture fixture = {0}; request_init(&fixture, false);
        request_submit(&fixture, VLC_PREPARSER_TYPE_PARSE | VLC_PREPARSER_TYPE_FETCHMETA_ALL);
        run_next(fixture.sys->parser); run_next(fixture.sys->fetcher->executor_local);
        assert(fixture.sys->fetcher->executor_network->count == 1 && fixture.ended == 0);
        if (state == 0) assert(vlc_metadata_owner_Set(VLC_OBJECT(fixture.instance), fixture.backend) == VLC_SUCCESS);
        else assert(var_SetBool(fixture.instance, "metadata-network-access", false) == VLC_SUCCESS);
        const unsigned calls = atomic_load(&module_calls);
        run_next(fixture.sys->fetcher->executor_network);
        assert(atomic_load(&module_calls) == calls && fixture.remote == 0);
        assert(fixture.ended == 1 && fixture.status == VLC_SUCCESS && fixture.trees == 1 && fixture.attachments == 1);
        assert(input_item_GetDuration(fixture.item) == VLC_TICK_FROM_SEC(42)); request_destroy(&fixture);
    }
}
static void local_and_attachment_results_keep_native_fetched_status(void)
{
    const char *claims[] = {"file:///native/cache/cover.png", "attachment://native-cover"};
    for (unsigned i = 0; i < ARRAY_SIZE(claims); ++i) {
        struct request_fixture fixture = {0}; request_init(&fixture, false);
        input_item_SetArtURL(fixture.item, claims[i]);
        request_submit(&fixture, VLC_PREPARSER_TYPE_FETCHMETA_LOCAL);
        run_next(fixture.sys->parser); run_next(fixture.sys->fetcher->executor_local);
        assert(fixture.ended == 1 && fixture.status == VLC_SUCCESS && input_item_IsArtFetched(fixture.item));
        assert(fixture.remote == 0 && atomic_load(&saved_assets) == 0); request_destroy(&fixture);
    }
}
static void native_parse_errors_keep_status_and_timeout_discards_output(void)
{
    for (unsigned timeout = 0; timeout < 2; ++timeout) {
        struct request_fixture fixture = {0}; request_init(&fixture, true);
        parse_status = VLC_EGENERIC;
        if (timeout) { parse_block = true; vlc_sem_init(&parse_entered, 0); fixture.sys->timeout = VLC_TICK_FROM_MS(1); }
        request_submit(&fixture, VLC_PREPARSER_TYPE_PARSE); run_next(fixture.sys->parser);
        assert(fixture.ended == 1 && fixture.status == (timeout ? VLC_ETIMEOUT : VLC_EGENERIC));
        assert(fixture.trees == (timeout ? 0u : 1u) && fixture.attachments == (timeout ? 0u : 1u));
        assert(input_item_GetDuration(fixture.item) == (timeout ? VLC_TICK_INVALID : VLC_TICK_FROM_SEC(42)));
        assert(!input_item_IsPreparsed(fixture.item)); request_destroy(&fixture);
    }
}
static void delete_cancels_queued_work_before_native_waits(void)
{
    struct request_fixture fixture = {0}; request_init(&fixture, true);
    request_submit(&fixture, VLC_PREPARSER_TYPE_PARSE);
    preparser_Delete(fixture.sys); fixture.sys = NULL;
    assert(fixture.ended == 1 && fixture.status == -EINTR && parse_calls == 0);
    assert(vlc_preparser_req_GetItem(fixture.req) == fixture.item);
    request_destroy(&fixture);
}
int main(void)
{
    local_and_attachment_results_keep_native_fetched_status();
    native_parse_errors_keep_status_and_timeout_discards_output();
    delete_cancels_queued_work_before_native_waits();
    running_parse_cancel_keeps_native_state_until_io_ends();
    cancellation_finds_the_native_fetch_stage();
    owner_or_consent_transition_discards_only_the_legacy_copy();
    local_parse_publishes_complete_output_after_admission();
    queued_cancel_and_same_value_edits_publish_no_output();
    absent_closed_and_offline_requests_keep_native_local_output();
    fetch_only_and_callback_reentry_keep_the_request_alive();
    return 0;
}
