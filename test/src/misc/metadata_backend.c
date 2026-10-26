// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <vlc_common.h>
#include <vlc_metadata_backend.h>
#include <vlc_input_item.h>
#include <vlc_threads.h>
#include <stdatomic.h>
#include <assert.h>

struct fixture {
    atomic_uint callbacks, cancels, destroys;
    atomic_int status;
    vlc_metadata_job_t *held[16];
    unsigned held_count;
    bool reject, synchronous, close_in_callback;
    vlc_metadata_backend_t *backend;
};
static void ended(vlc_metadata_job_t *job, int status, void *opaque)
{
    struct fixture *fixture = opaque;
    atomic_fetch_add(&fixture->callbacks, 1);
    atomic_store(&fixture->status, status);
    assert(vlc_metadata_job_GetResult(job) != NULL);
    assert(!vlc_metadata_job_IsPending(job));
    if (fixture->close_in_callback) vlc_metadata_backend_Close(fixture->backend);
}
static int dispatch(void *opaque, vlc_metadata_job_t *job)
{
    struct fixture *fixture = opaque;
    if (fixture->reject) return VLC_EGENERIC;
    if (fixture->synchronous) {
        assert(vlc_metadata_job_Complete(job, VLC_SUCCESS, NULL, "{}", 2));
        return VLC_SUCCESS;
    }
    assert(fixture->held_count < ARRAY_SIZE(fixture->held));
    fixture->held[fixture->held_count++] = vlc_metadata_job_Hold(job);
    return VLC_SUCCESS;
}
static void cancel(void *opaque, vlc_metadata_job_t *job)
{
    struct fixture *fixture = opaque;
    // The waiter must be complete before any potentially queued host cleanup.
    assert(!vlc_metadata_job_IsPending(job));
    assert(atomic_load(&fixture->callbacks) > 0);
    atomic_fetch_add(&fixture->cancels, 1);
}
static void destroy(void *opaque)
{ atomic_fetch_add(&((struct fixture *)opaque)->destroys, 1); }
static vlc_metadata_backend_t *backend(struct fixture *fixture)
{
    const struct vlc_metadata_backend_operations operations = {dispatch, cancel, destroy};
    fixture->backend = vlc_metadata_backend_New(&operations, fixture);
    assert(fixture->backend != NULL);
    return fixture->backend;
}
static vlc_metadata_job_t *job(vlc_metadata_backend_t *backend, struct fixture *fixture)
{
    const char *fields[] = {"description", "title"};
    const struct vlc_metadata_request request = {.feature="video.details", .operation="enrich", .fields=fields,
        .field_count=ARRAY_SIZE(fields), .context_json="{\"locale\":\"en\"}", .context_size=15,
        .generation=17, .target_revision=21, .flags=VLC_METADATA_REQUEST_NETWORK};
    vlc_metadata_job_t *value = vlc_metadata_job_New(backend, &request, ended, fixture);
    assert(value != NULL);
    return value;
}
static void release_held(struct fixture *fixture)
{
    for (unsigned i = 0; i < fixture->held_count; ++i) vlc_metadata_job_Release(fixture->held[i]);
    fixture->held_count = 0;
}
static void idle_synchronous_and_rejected_dispatch(void)
{
    struct fixture fixture = {0};
    vlc_metadata_backend_t *owner = backend(&fixture);
    vlc_metadata_job_t *value = job(owner, &fixture);
    assert(!vlc_metadata_job_Cancel(value));
    assert(atomic_load(&fixture.callbacks) == 0);
    assert(vlc_metadata_job_GetResult(value) == NULL);
    fixture.synchronous = true;
    assert(vlc_metadata_job_Start(value) == VLC_SUCCESS);
    assert(atomic_load(&fixture.callbacks) == 1);
    assert(vlc_metadata_job_Start(value) != VLC_SUCCESS);
    assert(!vlc_metadata_job_Complete(value, VLC_SUCCESS, NULL, NULL, 0));
    vlc_metadata_job_Release(value);
    fixture.synchronous = false; fixture.reject = true;
    value = job(owner, &fixture);
    assert(vlc_metadata_job_Start(value) == VLC_SUCCESS);
    assert(atomic_load(&fixture.callbacks) == 2);
    assert(atomic_load(&fixture.status) == VLC_EGENERIC);
    vlc_metadata_job_Release(value);
    vlc_metadata_backend_Close(owner); vlc_metadata_backend_Release(owner);
    assert(atomic_load(&fixture.destroys) == 1);
}
static void close_drains_without_an_event_loop_and_ignores_late_results(void)
{
    struct fixture fixture = {0}; vlc_metadata_backend_t *owner = backend(&fixture);
    vlc_metadata_job_t *first = job(owner, &fixture), *second = job(owner, &fixture);
    assert(vlc_metadata_job_Start(first) == VLC_SUCCESS && vlc_metadata_job_Start(second) == VLC_SUCCESS);
    vlc_metadata_backend_Close(owner);
    assert(vlc_metadata_backend_IsClosed(owner));
    assert(atomic_load(&fixture.callbacks) == 2 && atomic_load(&fixture.cancels) == 2);
    assert(atomic_load(&fixture.status) == -EINTR);
    assert(!vlc_metadata_job_Complete(first, VLC_SUCCESS, NULL, NULL, 0));
    assert(!vlc_metadata_job_Cancel(second));
    vlc_metadata_job_t *idle = job(owner, &fixture);
    assert(vlc_metadata_job_Start(idle) == -EINTR);
    assert(atomic_load(&fixture.callbacks) == 2);
    vlc_metadata_job_Release(idle); vlc_metadata_job_Release(first); vlc_metadata_job_Release(second);
    vlc_metadata_backend_Release(owner);
    assert(atomic_load(&fixture.destroys) == 0); // provider-held refs remain safe
    release_held(&fixture);
    assert(atomic_load(&fixture.destroys) == 1);
}
static void native_snapshot_and_patch_do_not_touch_the_live_item(void)
{
    struct fixture fixture = {0}; vlc_metadata_backend_t *owner = backend(&fixture);
    input_item_t *live = input_item_New("file:///private/movie.mkv", "Example");
    assert(live != NULL); input_item_SetMeta(live, vlc_meta_Title, "Original");
    const struct vlc_metadata_request request = {.feature="video.details", .operation="enrich", .item=live,
        .generation=5, .target_revision=9, .flags=VLC_METADATA_REQUEST_NETWORK};
    vlc_metadata_job_t *value = vlc_metadata_job_New(owner, &request, ended, &fixture); assert(value != NULL);
    assert(vlc_metadata_job_GetRequest(value)->item != live);
    input_item_SetMeta(live, vlc_meta_Title, "User edit");
    input_item_t *result = input_item_Copy(vlc_metadata_job_GetRequest(value)->item); assert(result != NULL);
    input_item_SetMeta(result, vlc_meta_Title, "Provider title");
    assert(vlc_metadata_job_Start(value) == VLC_SUCCESS);
    assert(vlc_metadata_job_Complete(value, VLC_SUCCESS, result, "{\"provider\":\"test\"}", 19));
    input_item_SetMeta(result, vlc_meta_Title, "Tampered later"); input_item_Release(result);
    const struct vlc_metadata_result *completed = vlc_metadata_job_GetResult(value);
    assert(completed->field_count == 1 && completed->fields[0].type == vlc_meta_Title);
    assert(strcmp(completed->fields[0].expected_value, "Original") == 0);
    assert(strcmp(completed->fields[0].value, "Provider title") == 0);
    char *title = input_item_GetMeta(live, vlc_meta_Title); assert(strcmp(title, "User edit") == 0); free(title);
    title = input_item_GetMeta(completed->item, vlc_meta_Title); assert(strcmp(title, "Provider title") == 0); free(title);
    vlc_metadata_job_Release(value); release_held(&fixture);
    vlc_metadata_backend_Close(owner); vlc_metadata_backend_Release(owner); input_item_Release(live);
}
struct race { vlc_metadata_job_t *job; vlc_sem_t *start; bool cancel; };
static void *race(void *opaque)
{
    struct race *value = opaque;
    vlc_sem_wait(value->start);
    if (value->cancel) vlc_metadata_job_Cancel(value->job);
    else vlc_metadata_job_Complete(value->job, VLC_SUCCESS, NULL, NULL, 0);
    vlc_metadata_job_Release(value->job);
    return NULL;
}
static void cancellation_completion_race_and_reentrant_close(void)
{
    for (unsigned round = 0; round < 100; ++round) {
        struct fixture fixture = {0}; vlc_metadata_backend_t *owner = backend(&fixture);
        vlc_metadata_job_t *value = job(owner, &fixture); assert(vlc_metadata_job_Start(value) == VLC_SUCCESS);
        vlc_sem_t start; vlc_sem_init(&start, 0);
        struct race first = {vlc_metadata_job_Hold(value), &start, true};
        struct race second = {vlc_metadata_job_Hold(value), &start, false};
        vlc_thread_t a, b;
        assert(vlc_clone(&a, race, &first) == 0 && vlc_clone(&b, race, &second) == 0);
        vlc_sem_post(&start); vlc_sem_post(&start); vlc_join(a, NULL); vlc_join(b, NULL);
        assert(atomic_load(&fixture.callbacks) == 1);
        vlc_metadata_job_Release(value); release_held(&fixture);
        vlc_metadata_backend_Close(owner); vlc_metadata_backend_Release(owner);
        assert(atomic_load(&fixture.destroys) == 1);
    }
    struct fixture fixture = {.synchronous=true, .close_in_callback=true};
    vlc_metadata_backend_t *owner = backend(&fixture); vlc_metadata_job_t *value = job(owner, &fixture);
    assert(vlc_metadata_job_Start(value) == VLC_SUCCESS);
    assert(vlc_metadata_backend_IsClosed(owner)); assert(atomic_load(&fixture.callbacks) == 1);
    vlc_metadata_job_Release(value); vlc_metadata_backend_Release(owner);
}
int main(void)
{
    idle_synchronous_and_rejected_dispatch();
    close_drains_without_an_event_loop_and_ignores_late_results();
    native_snapshot_and_patch_do_not_touch_the_live_item();
    cancellation_completion_race_and_reentrant_close();
    return 0;
}
