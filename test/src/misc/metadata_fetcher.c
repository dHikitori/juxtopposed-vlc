// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_artwork_policy.h>
#include <vlc_executor.h>
#include <vlc_input_item.h>
#include <vlc_meta_fetcher.h>
#include <vlc_metadata_backend.h>
#include <vlc_modules.h>
#include <vlc_threads.h>
#include <assert.h>
#include <stdatomic.h>
#include "../../../lib/libvlc_internal.h"
#include "../../../src/preparser/art.h"
#include "../../../src/input/input_interface.h"
#include "../../../src/libvlc.h"
#include "../../../src/misc/interrupt.c"
#include "../../../src/preparser/metadata_owner.c"
#include "../../../src/preparser/metadata_pipeline.c"

// Use a controlled executor and I/O fixture. The owner, pipeline, item, and
// backend code are real. A separate Lua fixture proves script selection.
struct vlc_executor {
    vlc_mutex_t lock;
    struct vlc_runnable *queue[2048];
    unsigned count;
};
static bool native_executors;
static vlc_executor_t *fixture_executor_new(unsigned threads)
{
    if (native_executors) return vlc_executor_New(threads);
    assert(threads > 0);
    vlc_executor_t *executor = calloc(1, sizeof(*executor)); assert(executor != NULL);
    vlc_mutex_init(&executor->lock); return executor;
}
static void fixture_executor_submit(vlc_executor_t *executor, struct vlc_runnable *runnable)
{
    if (native_executors) { vlc_executor_Submit(executor, runnable); return; }
    vlc_mutex_lock(&executor->lock); assert(executor->count < ARRAY_SIZE(executor->queue));
    executor->queue[executor->count++] = runnable; vlc_mutex_unlock(&executor->lock);
}
static bool fixture_executor_cancel(vlc_executor_t *executor, struct vlc_runnable *runnable)
{
    if (native_executors) return vlc_executor_Cancel(executor, runnable);
    vlc_mutex_lock(&executor->lock);
    for (unsigned i = 0; i < executor->count; ++i) {
        if (executor->queue[i] != runnable) continue;
        memmove(&executor->queue[i], &executor->queue[i+1], (executor->count-i-1)*sizeof(*executor->queue));
        --executor->count; vlc_mutex_unlock(&executor->lock); return true;
    }
    vlc_mutex_unlock(&executor->lock); return false;
}
static void fixture_executor_delete(vlc_executor_t *executor)
{
    if (native_executors) { vlc_executor_Delete(executor); return; }
    assert(executor->count == 0); free(executor);
}
static void run_next(vlc_executor_t *executor)
{
    vlc_mutex_lock(&executor->lock); assert(executor->count > 0);
    struct vlc_runnable *runnable = executor->queue[0];
    memmove(&executor->queue[0], &executor->queue[1], (--executor->count)*sizeof(*executor->queue));
    vlc_mutex_unlock(&executor->lock); runnable->run(runnable->userdata);
}

static atomic_uint module_calls, saved_assets;
static atomic_int last_provider;
static atomic_bool block_module, hold_after_interrupt;
static bool local_art;
static vlc_sem_t module_entered, module_interrupted, module_resume, handoff_reached, handoff_resume;
static _Thread_local bool pause_handoff;
static void interrupt_module(void *opaque) { vlc_sem_post(opaque); }
static module_t *fixture_module_need(vlc_object_t *object, const char *type, const char *name, bool strict)
{
    meta_fetcher_t *fetcher = (meta_fetcher_t *)object;
    atomic_fetch_add(&module_calls, 1); atomic_store(&last_provider, fetcher->e_provider);
    if (fetcher->e_provider >= META_FETCHER_PROVIDER_MUSICBRAINZ) {
        assert(!strcmp(type, "art finder") && strict && name != NULL && !strcmp(name, "lua-metadata-provider"));
        assert(fetcher->e_scope == FETCHER_SCOPE_NETWORK);
    } else assert(!strict && name == NULL);
    if (atomic_load(&block_module)) {
        vlc_interrupt_register(interrupt_module, &module_interrupted); vlc_sem_post(&module_entered);
        vlc_sem_wait(&module_interrupted); vlc_interrupt_unregister();
        if (atomic_load(&hold_after_interrupt)) vlc_sem_wait(&module_resume);
    }
    if (!strcmp(type, "meta fetcher")) input_item_SetDescription(fetcher->p_item, "Native local observation");
    else if (fetcher->e_provider >= META_FETCHER_PROVIDER_MUSICBRAINZ)
        input_item_SetArtURL(fetcher->p_item, "https://provider.example/claim.jpg");
    else if (local_art) input_item_SetArtURL(fetcher->p_item, "file:///native/cache/cover.png");
    return (module_t *)(uintptr_t)1;
}
static void fixture_module_unneed(vlc_object_t *object, module_t *module)
{ (void)object; assert(module == (module_t *)(uintptr_t)1); }
static int fixture_cache(input_item_t *item) { (void)item; return VLC_EGENERIC; }
static int fixture_save(vlc_object_t *object, input_item_t *item, const void *data, size_t size, const char *type)
{ (void)object; (void)item; (void)data; (void)size; (void)type; atomic_fetch_add(&saved_assets, 1); return VLC_SUCCESS; }
static void *fixture_custom_create(vlc_object_t *object, size_t size, const char *name)
{ (void)name; return vlc_object_create(object, size); }
static void fixture_art_status(input_item_t *item, bool enabled, int flag)
{
    vlc_mutex_lock(&item->lock);
    int status = vlc_meta_GetStatus(item->p_meta);
    vlc_meta_SetStatus(item->p_meta, enabled ? status | flag : status & ~flag);
    vlc_mutex_unlock(&item->lock);
}
static void end_before_handoff(vlc_metadata_legacy_t *lease)
{
    vlc_metadata_legacy_End(lease);
    if (pause_handoff) { pause_handoff = false; vlc_sem_post(&handoff_reached); vlc_sem_wait(&handoff_resume); }
}
#undef module_need
#undef module_unneed
#define module_need(a,b,c,d) fixture_module_need(VLC_OBJECT(a),b,c,d)
#define module_unneed(a,b) fixture_module_unneed(VLC_OBJECT(a),b)
#define vlc_executor_New fixture_executor_new
#define vlc_executor_Submit fixture_executor_submit
#define vlc_executor_Cancel fixture_executor_cancel
#define vlc_executor_Delete fixture_executor_delete
#define input_FindArtInCache fixture_cache
#define input_FindArtInCacheUsingItemUID fixture_cache
#define input_SaveArt fixture_save
#undef vlc_custom_create
#define vlc_custom_create(a,b,c) fixture_custom_create(VLC_OBJECT(a),b,c)
#define input_item_SetArtFetched(a,b) fixture_art_status(a,b,ITEM_ART_FETCHED)
#define input_item_SetArtNotFound(a,b) fixture_art_status(a,b,ITEM_ART_NOTFOUND)
#define vlc_metadata_legacy_End end_before_handoff
#include "../../../src/preparser/fetcher.c"
#undef vlc_metadata_legacy_End

struct fixture {
    libvlc_int_t *instance;
    input_fetcher_t *fetcher;
    vlc_metadata_backend_t *backend;
    vlc_metadata_pipeline_t *pipeline;
    input_item_t *item;
    unsigned dispatches, ended, local_ended;
    bool complete_backend;
    int local_status;
    vlc_metadata_pipeline_stage_t *pending_stage;
};
struct result {
    struct fixture *fixture;
    unsigned calls;
    bool fetched, delete_fetcher;
    int status;
    vlc_sem_t *terminal;
};
static int dispatch(void *opaque, vlc_metadata_job_t *job)
{
    struct fixture *fixture = opaque; ++fixture->dispatches;
    if (fixture->complete_backend) {
        input_item_t *copy = input_item_Copy(vlc_metadata_job_GetRequest(job)->item); assert(copy != NULL);
        assert(vlc_metadata_job_Complete(job, VLC_SUCCESS, copy, NULL, 0)); input_item_Release(copy);
    }
    return VLC_SUCCESS;
}
static void ended(vlc_metadata_job_t *job, int status, void *opaque)
{ (void)job; (void)status; ++((struct fixture *)opaque)->ended; }
static int local_start(void *opaque, vlc_metadata_pipeline_stage_t *stage)
{ return input_fetcher_PushLocal(((struct fixture *)opaque)->fetcher, stage); }
static void local_cancel(void *opaque, vlc_metadata_pipeline_stage_t *stage)
{ input_fetcher_Cancel(((struct fixture *)opaque)->fetcher, stage); }
static int local_hold(void *opaque, vlc_metadata_pipeline_stage_t *stage)
{ ((struct fixture *)opaque)->pending_stage = stage; return VLC_SUCCESS; }
static void local_ended(input_item_t *item, int status, void *opaque)
{
    struct fixture *fixture = opaque; assert(item == fixture->item);
    ++fixture->local_ended; fixture->local_status = status;
}
static void fetched(input_item_t *item, bool success, void *opaque)
{
    struct result *result = opaque; assert(item == result->fixture->item);
    ++result->calls; result->fetched = success;
    if (result->delete_fetcher) {
        input_fetcher_t *fetcher = result->fixture->fetcher;
        result->fixture->fetcher = NULL; input_fetcher_Delete(fetcher);
    }
    if (result->terminal != NULL) vlc_sem_post(result->terminal);
}
static const input_fetcher_callbacks_t callbacks = {.on_art_fetch_ended=fetched};
static void probed(input_item_t *item, int status, void *opaque)
{
    struct result *result = opaque; assert(item != result->fixture->item); ++result->calls; result->status = status;
    if (status == VLC_SUCCESS) {
        char *art = input_item_GetArtURL(item);
        assert(art != NULL && !strcmp(art, "https://provider.example/claim.jpg")); free(art);
    }
}
static void create(struct fixture *fixture)
{
    atomic_store(&module_calls, 0); atomic_store(&saved_assets, 0); atomic_store(&last_provider, -1);
    atomic_store(&block_module, false); atomic_store(&hold_after_interrupt, false); local_art = false;
    fixture->instance = libvlc_InternalCreate(); assert(fixture->instance != NULL);
    assert(var_Create(fixture->instance, "metadata-network-access", VLC_VAR_BOOL) == VLC_SUCCESS);
    assert(var_SetBool(fixture->instance, "metadata-network-access", true) == VLC_SUCCESS);
    assert(var_Create(fixture->instance, "fetch-art-threads", VLC_VAR_INTEGER) == VLC_SUCCESS);
    assert(var_SetInteger(fixture->instance, "fetch-art-threads", 1) == VLC_SUCCESS);
    fixture->fetcher = input_fetcher_New(VLC_OBJECT(fixture->instance), VLC_PREPARSER_TYPE_FETCHMETA_ALL); assert(fixture->fetcher != NULL);
    const struct vlc_metadata_backend_operations operations = {.dispatch=dispatch};
    fixture->backend = vlc_metadata_backend_New(&operations, fixture); assert(fixture->backend != NULL);
    fixture->item = input_item_New("file:///private/song.flac", "Song"); assert(fixture->item != NULL);
    input_item_SetTitle(fixture->item, "Song");
    const struct vlc_metadata_pipeline_operations stages = {.local=local_start, .cancel_local=local_cancel};
    fixture->pipeline = vlc_metadata_pipeline_New(VLC_OBJECT(fixture->instance), &stages, fixture); assert(fixture->pipeline != NULL);
}
static void destroy(struct fixture *fixture)
{
    vlc_metadata_pipeline_Close(fixture->pipeline);
    if (fixture->fetcher != NULL) input_fetcher_Delete(fixture->fetcher);
    vlc_metadata_pipeline_Delete(fixture->pipeline);
    vlc_metadata_owner_Close(VLC_OBJECT(fixture->instance)); vlc_metadata_owner_Forget(VLC_OBJECT(fixture->instance));
    vlc_metadata_backend_Close(fixture->backend); vlc_metadata_backend_Release(fixture->backend);
    input_item_Release(fixture->item); libvlc_InternalDestroy(fixture->instance);
    assert(legacy_count == 0 && owner_count == 0);
}
static vlc_metadata_job_t *job_new(struct fixture *fixture, unsigned flags)
{
    const struct vlc_metadata_request request = {.feature="artwork", .operation="enrich", .item=fixture->item, .flags=flags};
    vlc_metadata_job_t *job = vlc_metadata_job_New(fixture->backend, &request, ended, fixture); assert(job != NULL);
    assert(vlc_metadata_job_Start(job) == VLC_SUCCESS); return job;
}
static void legacy_local_art_keeps_its_native_path(void)
{
    struct fixture fixture = {0}; create(&fixture); local_art = true; struct result result = {.fixture=&fixture};
    assert(input_fetcher_Push(fixture.fetcher, fixture.item, VLC_PREPARSER_TYPE_FETCHMETA_LOCAL, &callbacks, &result) == VLC_SUCCESS);
    assert(legacy_count == 1); run_next(fixture.fetcher->executor_local);
    assert(result.calls == 0 && fixture.fetcher->executor_downloader->count == 1 && legacy_count == 1);
    run_next(fixture.fetcher->executor_downloader);
    assert(result.calls == 1 && result.fetched && legacy_count == 0 && atomic_load(&last_provider) == META_FETCHER_PROVIDER_ANY);
    assert(atomic_load(&saved_assets) == 0); destroy(&fixture);
}
static void owner_install_refuses_a_queued_legacy_stage(void)
{
    struct fixture fixture = {0}; create(&fixture); local_art = true; struct result result = {.fixture=&fixture};
    assert(input_fetcher_Push(fixture.fetcher, fixture.item, VLC_PREPARSER_TYPE_FETCHMETA_ALL, &callbacks, &result) == VLC_SUCCESS);
    assert(vlc_metadata_owner_Set(VLC_OBJECT(fixture.instance), fixture.backend) == VLC_SUCCESS);
    run_next(fixture.fetcher->executor_local);
    assert(result.calls == 1 && !result.fetched && atomic_load(&module_calls) == 0 && fixture.fetcher->executor_network->count == 0);
    assert(input_fetcher_Push(fixture.fetcher, fixture.item, VLC_PREPARSER_TYPE_FETCHMETA_ALL, &callbacks, &result) == -EINTR);
    destroy(&fixture);
}
struct run_call { vlc_executor_t *executor; bool handoff; };
static void *run_thread(void *opaque)
{ struct run_call *call = opaque; pause_handoff = call->handoff; run_next(call->executor); return NULL; }
static void owner_install_interrupts_running_legacy_before_owned_dispatch(void)
{
    struct fixture fixture = {0}; create(&fixture); local_art = true; struct result result = {.fixture=&fixture};
    atomic_store(&block_module, true); vlc_sem_init(&module_entered, 0); vlc_sem_init(&module_interrupted, 0);
    assert(input_fetcher_Push(fixture.fetcher, fixture.item, VLC_PREPARSER_TYPE_FETCHMETA_ALL, &callbacks, &result) == VLC_SUCCESS);
    struct run_call call = {fixture.fetcher->executor_local, false}; vlc_thread_t thread; assert(vlc_clone(&thread, run_thread, &call) == 0);
    vlc_sem_wait(&module_entered);
    assert(vlc_metadata_owner_Set(VLC_OBJECT(fixture.instance), fixture.backend) == VLC_SUCCESS);
    vlc_join(thread, NULL);
    assert(result.calls == 1 && !result.fetched && vlc_metadata_backend_IsReady(fixture.backend));
    assert(fixture.fetcher->executor_network->count == 0 && fixture.fetcher->executor_downloader->count == 0);
    destroy(&fixture);
}
static void cancel_latches_between_legacy_end_and_successor_submission(void)
{
    struct fixture fixture = {0}; create(&fixture); local_art = true; struct result result = {.fixture=&fixture};
    vlc_sem_init(&handoff_reached, 0); vlc_sem_init(&handoff_resume, 0);
    assert(input_fetcher_Push(fixture.fetcher, fixture.item, VLC_PREPARSER_TYPE_FETCHMETA_ALL, &callbacks, &result) == VLC_SUCCESS);
    struct run_call call = {fixture.fetcher->executor_local, true}; vlc_thread_t thread; assert(vlc_clone(&thread, run_thread, &call) == 0);
    vlc_sem_wait(&handoff_reached); assert(legacy_count == 0);
    assert(input_fetcher_Cancel(fixture.fetcher, &result) == 1);
    vlc_sem_post(&handoff_resume); vlc_join(thread, NULL);
    assert(result.calls == 1 && !result.fetched && fixture.fetcher->executor_downloader->count == 0 && fixture.fetcher->executor_network->count == 0);
    destroy(&fixture);
}
static void queued_cancel_callbacks_can_delete_the_fetcher(void)
{
    struct fixture fixture = {0}; create(&fixture);
    struct result first = {.fixture=&fixture, .delete_fetcher=true}, second = {.fixture=&fixture};
    assert(input_fetcher_Push(fixture.fetcher, fixture.item, VLC_PREPARSER_TYPE_FETCHMETA_ALL, &callbacks, &first) == VLC_SUCCESS);
    assert(input_fetcher_Push(fixture.fetcher, fixture.item, VLC_PREPARSER_TYPE_FETCHMETA_ALL, &callbacks, &second) == VLC_SUCCESS);
    assert(input_fetcher_Cancel(fixture.fetcher, &first) == 1);
    assert(fixture.fetcher == NULL && first.calls == 1 && second.calls == 1 && !first.fetched && !second.fetched);
    assert(atomic_load(&module_calls) == 0); destroy(&fixture);
}
static void owned_local_stage_uses_no_lua_or_downloader(void)
{
    struct fixture fixture = {.complete_backend=true}; create(&fixture);
    assert(vlc_metadata_owner_Set(VLC_OBJECT(fixture.instance), fixture.backend) == VLC_SUCCESS);
    assert(vlc_metadata_pipeline_Push(fixture.pipeline, fixture.item, VLC_PREPARSER_TYPE_FETCHMETA_LOCAL,
        NULL, &fixture, local_ended, &fixture) == VLC_SUCCESS);
    assert(fixture.local_ended == 0); run_next(fixture.fetcher->executor_local);
    assert(fixture.local_ended == 1 && fixture.local_status == VLC_SUCCESS && fixture.dispatches == 1);
    assert(atomic_load(&last_provider) == META_FETCHER_PROVIDER_NONE);
    assert(fixture.fetcher->executor_network->count == 0 && fixture.fetcher->executor_downloader->count == 0 && legacy_count == 0);
    char *description = input_item_GetDescription(fixture.item);
    assert(description != NULL && !strcmp(description, "Native local observation")); free(description); destroy(&fixture);
}
static void owned_local_cancel_finishes_the_stage_once(void)
{
    struct fixture fixture = {.complete_backend=true}; create(&fixture);
    assert(vlc_metadata_owner_Set(VLC_OBJECT(fixture.instance), fixture.backend) == VLC_SUCCESS);
    assert(vlc_metadata_pipeline_Push(fixture.pipeline, fixture.item, VLC_PREPARSER_TYPE_FETCHMETA_ALL,
        NULL, &fixture, local_ended, &fixture) == VLC_SUCCESS);
    assert(vlc_metadata_pipeline_Cancel(fixture.pipeline, &fixture) == 1);
    assert(fixture.local_ended == 1 && fixture.local_status == -EINTR && fixture.dispatches == 0);
    assert(fixture.fetcher->executor_local->count == 0 && atomic_load(&module_calls) == 0); destroy(&fixture);
}
static void a_named_probe_returns_a_private_claim_without_download(void)
{
    struct fixture fixture = {0}; create(&fixture); input_item_SetArtURL(fixture.item, "file:///user/cover.png");
    vlc_metadata_job_t *job = job_new(&fixture, VLC_METADATA_REQUEST_NETWORK); struct result result = {.fixture=&fixture};
    assert(input_fetcher_Probe(fixture.fetcher, job, META_FETCHER_PROVIDER_FRENCH_TV, probed, &result) == VLC_SUCCESS);
    run_next(fixture.fetcher->executor_network);
    assert(result.calls == 1 && result.status == VLC_SUCCESS && atomic_load(&module_calls) == 1);
    assert(atomic_load(&last_provider) == META_FETCHER_PROVIDER_FRENCH_TV && atomic_load(&saved_assets) == 0);
    assert(fixture.fetcher->executor_downloader->count == 0);
    char *art = input_item_GetArtURL(fixture.item); assert(art != NULL && !strcmp(art, "file:///user/cover.png")); free(art);
    art = input_item_GetArtURL(vlc_metadata_job_GetRequest(job)->item); assert(art != NULL && !strcmp(art, "file:///user/cover.png")); free(art);
    vlc_metadata_job_Cancel(job); vlc_metadata_job_Release(job); destroy(&fixture);
}
static void a_named_probe_requires_native_network_authority_and_an_ordinary_item(void)
{
    struct fixture fixture = {0}; create(&fixture); struct result result = {.fixture=&fixture};
    vlc_metadata_job_t *job = job_new(&fixture, 0);
    assert(input_fetcher_Probe(fixture.fetcher, job, META_FETCHER_PROVIDER_LASTFM, probed, &result) == -EACCES);
    vlc_metadata_job_Cancel(job); vlc_metadata_job_Release(job);
    input_item_SetURI(fixture.item, "HAKONE-ADDON-SOURCE:v1:opaque"); job = job_new(&fixture, VLC_METADATA_REQUEST_NETWORK);
    assert(input_fetcher_Probe(fixture.fetcher, job, META_FETCHER_PROVIDER_LASTFM, probed, &result) == -EACCES);
    vlc_metadata_job_Cancel(job); vlc_metadata_job_Release(job);
    input_item_SetURI(fixture.item, "file:///private/song.flac"); job = job_new(&fixture, VLC_METADATA_REQUEST_NETWORK);
    assert(input_fetcher_Probe(fixture.fetcher, job, META_FETCHER_PROVIDER_ANY, probed, &result) == VLC_EINVAL);
    assert(var_SetBool(fixture.instance, "metadata-network-access", false) == VLC_SUCCESS);
    assert(input_fetcher_Probe(fixture.fetcher, job, META_FETCHER_PROVIDER_LASTFM, probed, &result) == -EACCES);
    assert(result.calls == 0 && atomic_load(&module_calls) == 0);
    vlc_metadata_job_Cancel(job); vlc_metadata_job_Release(job); destroy(&fixture);
}
static void native_job_cancel_refuses_a_queued_probe(void)
{
    struct fixture fixture = {0}; create(&fixture); struct result result = {.fixture=&fixture};
    vlc_metadata_job_t *job = job_new(&fixture, VLC_METADATA_REQUEST_NETWORK);
    assert(input_fetcher_Probe(fixture.fetcher, job, META_FETCHER_PROVIDER_MUSICBRAINZ, probed, &result) == VLC_SUCCESS);
    vlc_metadata_job_Cancel(job); run_next(fixture.fetcher->executor_network);
    assert(result.calls == 1 && result.status == -EINTR && atomic_load(&module_calls) == 0);
    vlc_metadata_job_Release(job); destroy(&fixture);
}
static void task_limit_retains_the_existing_requests(void)
{
    struct fixture fixture = {0}; create(&fixture); struct result result = {.fixture=&fixture};
    vlc_metadata_job_t *job = job_new(&fixture, VLC_METADATA_REQUEST_NETWORK);
    for (unsigned i = 0; i < MAX_TASKS; ++i)
        assert(input_fetcher_Probe(fixture.fetcher, job, META_FETCHER_PROVIDER_LASTFM, probed, &result) == VLC_SUCCESS);
    assert(input_fetcher_Probe(fixture.fetcher, job, META_FETCHER_PROVIDER_LASTFM, probed, &result) == VLC_ENOMEM);
    assert(fixture.fetcher->executor_network->count == MAX_TASKS && result.calls == 0);
    assert(input_fetcher_Cancel(fixture.fetcher, job) == MAX_TASKS);
    assert(result.calls == MAX_TASKS && result.status == -EINTR && fixture.fetcher->retained == 0);
    vlc_metadata_job_Cancel(job); vlc_metadata_job_Release(job); destroy(&fixture);
}
static void the_real_executor_drains_legacy_io_before_owned_dispatch(void)
{
    native_executors = true;
    struct fixture fixture = {0}; create(&fixture); local_art = true;
    vlc_sem_t terminal; vlc_sem_init(&terminal, 0);
    struct result result = {.fixture=&fixture, .terminal=&terminal};
    atomic_store(&block_module, true); atomic_store(&hold_after_interrupt, true);
    vlc_sem_init(&module_entered, 0); vlc_sem_init(&module_interrupted, 0); vlc_sem_init(&module_resume, 0);
    assert(input_fetcher_Push(fixture.fetcher, fixture.item, VLC_PREPARSER_TYPE_FETCHMETA_ALL, &callbacks, &result) == VLC_SUCCESS);
    vlc_sem_wait(&module_entered);
    assert(vlc_metadata_owner_Set(VLC_OBJECT(fixture.instance), fixture.backend) == VLC_SUCCESS);
    assert(!vlc_metadata_backend_IsReady(fixture.backend));
    vlc_metadata_job_t *job = job_new(&fixture, VLC_METADATA_REQUEST_NETWORK); assert(fixture.dispatches == 0);
    vlc_sem_post(&module_resume); vlc_sem_wait(&terminal);
    input_fetcher_Delete(fixture.fetcher); fixture.fetcher = NULL;
    assert(result.calls == 1 && !result.fetched && fixture.dispatches == 1 && vlc_metadata_backend_IsReady(fixture.backend));
    vlc_metadata_job_Cancel(job); vlc_metadata_job_Release(job); destroy(&fixture);
    native_executors = false;
}
static void a_retained_context_refuses_a_late_stage_after_delete(void)
{
    struct fixture fixture = {0}; create(&fixture);
    input_fetcher_t *context = input_fetcher_Hold(fixture.fetcher);
    fixture.pipeline->ops.local = local_hold;
    assert(vlc_metadata_owner_Set(VLC_OBJECT(fixture.instance), fixture.backend) == VLC_SUCCESS);
    assert(vlc_metadata_pipeline_Push(fixture.pipeline, fixture.item, VLC_PREPARSER_TYPE_FETCHMETA_LOCAL,
        NULL, &fixture, local_ended, &fixture) == VLC_SUCCESS);
    assert(fixture.pending_stage != NULL && fixture.local_ended == 0);
    vlc_metadata_pipeline_Close(fixture.pipeline); input_fetcher_Delete(fixture.fetcher);
    assert(fixture.local_ended == 1 && fixture.local_status == -EINTR);
    assert(input_fetcher_PushLocal(context, fixture.pending_stage) == -EINTR);
    vlc_metadata_pipeline_stage_Done(fixture.pending_stage, -EINTR); fixture.pending_stage = NULL;
    assert(input_fetcher_Cancel(context, NULL) == 0 && atomic_load(&module_calls) == 0);
    destroy(&fixture); input_fetcher_Release(context);
}
int main(void)
{
    legacy_local_art_keeps_its_native_path();
    owner_install_refuses_a_queued_legacy_stage();
    owner_install_interrupts_running_legacy_before_owned_dispatch();
    cancel_latches_between_legacy_end_and_successor_submission();
    queued_cancel_callbacks_can_delete_the_fetcher();
    owned_local_stage_uses_no_lua_or_downloader();
    owned_local_cancel_finishes_the_stage_once();
    a_named_probe_returns_a_private_claim_without_download();
    a_named_probe_requires_native_network_authority_and_an_ordinary_item();
    native_job_cancel_refuses_a_queued_probe();
    task_limit_retains_the_existing_requests();
    the_real_executor_drains_legacy_io_before_owned_dispatch();
    a_retained_context_refuses_a_late_stage_after_delete();
    return 0;
}
