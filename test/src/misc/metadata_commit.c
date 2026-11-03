// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_input_item.h>
#include <vlc_input_policy.h>
#include <vlc_metadata_backend.h>
#include <vlc_metadata_revision.h>
#include <vlc_threads.h>
#include <assert.h>

struct fixture {
    vlc_metadata_backend_t *backend;
    vlc_metadata_job_t *job;
    input_item_t *item;
    unsigned ended;
    bool apply_in_callback;
    int commit_status;
    size_t applied;
};
static int dispatch(void *opaque, vlc_metadata_job_t *job)
{ (void)opaque; (void)job; return VLC_SUCCESS; }
static void ended(vlc_metadata_job_t *job, int status, void *opaque)
{
    struct fixture *fixture = opaque; ++fixture->ended;
    if (fixture->apply_in_callback) {
        assert(status == VLC_SUCCESS);
        fixture->commit_status = vlc_metadata_job_Apply(job, fixture->item, 0, &fixture->applied);
    }
}
static void init_empty(struct fixture *fixture)
{
    memset(fixture, 0, sizeof(*fixture));
    static const struct vlc_metadata_backend_operations operations = {.dispatch = dispatch};
    fixture->backend = vlc_metadata_backend_New(&operations, fixture); assert(fixture->backend != NULL);
    fixture->item = input_item_New("file:///metadata-commit.flac", "Commit test"); assert(fixture->item != NULL);
}
static void init(struct fixture *fixture)
{
    init_empty(fixture);
    input_item_SetTitle(fixture->item, "Title"); input_item_SetArtist(fixture->item, "Artist");
}
static void start(struct fixture *fixture)
{
    const struct vlc_metadata_request request = {.feature="music.details", .operation="enrich",
        .item=fixture->item, .flags=VLC_METADATA_REQUEST_NETWORK};
    fixture->job = vlc_metadata_job_New(fixture->backend, &request, ended, fixture); assert(fixture->job != NULL);
    assert(vlc_metadata_job_Start(fixture->job) == VLC_SUCCESS);
}
static void complete(struct fixture *fixture)
{
    input_item_t *result = input_item_Copy(vlc_metadata_job_GetRequest(fixture->job)->item); assert(result != NULL);
    input_item_SetDescription(result, "Provider description"); input_item_SetGenre(result, "Provider genre");
    assert(vlc_metadata_job_Complete(fixture->job, VLC_SUCCESS, result, NULL, 0)); input_item_Release(result);
    assert(fixture->ended == 1);
}
static void clean(struct fixture *fixture)
{
    vlc_metadata_backend_Close(fixture->backend);
    vlc_metadata_job_Release(fixture->job);
    vlc_metadata_backend_Release(fixture->backend); input_item_Release(fixture->item);
}
static void field(input_item_t *item, vlc_meta_type_t type, const char *expected)
{
    char *value = input_item_GetMeta(item, type);
    assert(expected == NULL ? value == NULL : value != NULL && !strcmp(value, expected)); free(value);
}
static void complete_commit_and_repeat(void)
{
    struct fixture fixture; init(&fixture); start(&fixture); complete(&fixture);
    field(fixture.item, vlc_meta_Description, NULL); field(fixture.item, vlc_meta_Genre, NULL);
    const struct vlc_metadata_result *result = vlc_metadata_job_GetResult(fixture.job);
    assert(result->field_count == 2);
    vlc_mutex_lock(&fixture.item->lock);
    for (size_t i = 0; i < result->field_count; ++i)
        assert(result->fields[i].expected_revision == vlc_meta_FieldRevision(fixture.item->p_meta, result->fields[i].type));
    vlc_mutex_unlock(&fixture.item->lock);
    size_t applied = 99; assert(vlc_metadata_job_Apply(fixture.job, fixture.item, 0, &applied) == VLC_SUCCESS && applied == 2);
    field(fixture.item, vlc_meta_Description, "Provider description"); field(fixture.item, vlc_meta_Genre, "Provider genre");
    assert(vlc_metadata_job_Apply(fixture.job, fixture.item, 0, &applied) == -EAGAIN && applied == 0);
    clean(&fixture);

    init(&fixture); fixture.apply_in_callback = true; start(&fixture); complete(&fixture);
    assert(fixture.commit_status == VLC_SUCCESS && fixture.applied == 2); clean(&fixture);
}
static void fresh_and_absent_metadata(void)
{
    for (unsigned absent = 0; absent < 2; ++absent) {
        struct fixture fixture; init_empty(&fixture);
        if (absent) {
            vlc_mutex_lock(&fixture.item->lock); vlc_meta_Delete(fixture.item->p_meta); fixture.item->p_meta = NULL;
            vlc_mutex_unlock(&fixture.item->lock);
        }
        start(&fixture); assert(fixture.item->p_meta != NULL);
        vlc_mutex_lock(&fixture.item->lock);
        assert(vlc_meta_FieldRevision(fixture.item->p_meta, vlc_meta_Title) != 0);
        vlc_mutex_unlock(&fixture.item->lock);
        complete(&fixture); size_t applied = 0;
        assert(vlc_metadata_job_Apply(fixture.job, fixture.item, 0, &applied) == VLC_SUCCESS && applied == 2);
        field(fixture.item, vlc_meta_Title, NULL); clean(&fixture);
    }
    struct fixture fixture; init_empty(&fixture); start(&fixture); complete(&fixture);
    input_item_SetTitle(fixture.item, NULL);
    assert(vlc_metadata_job_Apply(fixture.job, fixture.item, 0, NULL) == -EAGAIN);
    field(fixture.item, vlc_meta_Description, NULL); clean(&fixture);
}
static void preserve_existing_and_explicit_replace(void)
{
    struct fixture fixture; init(&fixture); input_item_SetDescription(fixture.item, "Local description");
    start(&fixture); complete(&fixture); size_t applied;
    assert(vlc_metadata_job_Apply(fixture.job, fixture.item, 0, &applied) == VLC_SUCCESS && applied == 1);
    field(fixture.item, vlc_meta_Description, "Local description"); clean(&fixture);

    init(&fixture); input_item_SetDescription(fixture.item, "Local description"); start(&fixture); complete(&fixture);
    assert(vlc_metadata_job_Apply(fixture.job, fixture.item, UINT32_C(1) << 31, &applied) == VLC_EINVAL && applied == 0);
    field(fixture.item, vlc_meta_Genre, NULL);
    assert(vlc_metadata_job_Apply(fixture.job, fixture.item, UINT32_C(1) << vlc_meta_Description, &applied) == VLC_SUCCESS && applied == 2);
    field(fixture.item, vlc_meta_Description, "Provider description"); clean(&fixture);
}
static void stale_fields_never_partially_commit(void)
{
    for (unsigned mode = 0; mode < 4; ++mode) {
        struct fixture fixture; init(&fixture); start(&fixture); complete(&fixture);
        if (mode == 0) input_item_SetDescription(fixture.item, NULL);
        if (mode == 1) { input_item_SetDescription(fixture.item, "Temporary"); input_item_SetDescription(fixture.item, NULL); }
        if (mode == 2) input_item_SetTitle(fixture.item, "Title");
        if (mode == 3) { input_item_SetArtist(fixture.item, "Other artist"); input_item_SetArtist(fixture.item, "Artist"); }
        size_t applied = 99;
        assert(vlc_metadata_job_Apply(fixture.job, fixture.item, UINT32_C(1) << vlc_meta_Description, &applied) == -EAGAIN && applied == 0);
        field(fixture.item, vlc_meta_Description, NULL); field(fixture.item, vlc_meta_Genre, NULL); clean(&fixture);
    }
    struct fixture fixture; init(&fixture); input_item_SetMetaExtra(fixture.item, "MB_ALBUMID", "release-one");
    start(&fixture); complete(&fixture); input_item_SetMetaExtra(fixture.item, "MB_ALBUMID", "release-one");
    assert(vlc_metadata_job_Apply(fixture.job, fixture.item, 0, NULL) == -EAGAIN);
    field(fixture.item, vlc_meta_Genre, NULL); clean(&fixture);
}
static bool allow_url(void *opaque, const char *url)
{ (void)opaque; (void)url; return true; }
static bool allow_connection(void *opaque, const char *host, unsigned port, bool datagram)
{ (void)opaque; (void)host; (void)port; (void)datagram; return true; }
static vlc_input_policy_t *new_policy(void)
{
    static const struct vlc_input_policy_operations operations = {allow_url, allow_connection, NULL};
    vlc_input_policy_t *policy = vlc_input_policy_New(&operations, NULL); assert(policy != NULL); return policy;
}
static void uri_scope_and_revocation(void)
{
    struct fixture fixture; init(&fixture); start(&fixture); complete(&fixture);
    input_item_SetURI(fixture.item, "file:///different-media.flac");
    assert(vlc_metadata_job_Apply(fixture.job, fixture.item, 0, NULL) == -EAGAIN);
    field(fixture.item, vlc_meta_Genre, NULL); clean(&fixture);

    init(&fixture); start(&fixture); complete(&fixture); vlc_input_policy_t *policy = new_policy();
    assert(vlc_input_policy_AttachItem(fixture.item, policy) == VLC_SUCCESS); vlc_input_policy_Release(policy);
    assert(vlc_metadata_job_Apply(fixture.job, fixture.item, 0, NULL) == -EAGAIN);
    field(fixture.item, vlc_meta_Genre, NULL); clean(&fixture);

    init(&fixture); policy = new_policy(); assert(!vlc_input_policy_IsRevoked(policy));
    assert(vlc_input_policy_AttachItem(fixture.item, policy) == VLC_SUCCESS); start(&fixture); complete(&fixture);
    vlc_input_policy_Revoke(policy); assert(vlc_input_policy_IsRevoked(policy));
    assert(vlc_metadata_job_Apply(fixture.job, fixture.item, 0, NULL) == -EACCES);
    field(fixture.item, vlc_meta_Description, NULL); field(fixture.item, vlc_meta_Genre, NULL);
    vlc_input_policy_Release(policy); clean(&fixture);

    init(&fixture); policy = new_policy(); assert(vlc_input_policy_AttachItem(fixture.item, policy) == VLC_SUCCESS);
    start(&fixture); complete(&fixture);
    assert(vlc_metadata_job_Apply(fixture.job, fixture.item, 0, NULL) == VLC_SUCCESS);
    field(fixture.item, vlc_meta_Genre, "Provider genre"); vlc_input_policy_Release(policy); clean(&fixture);

    init(&fixture); policy = new_policy(); assert(vlc_input_policy_AttachItem(fixture.item, policy) == VLC_SUCCESS);
    start(&fixture); complete(&fixture); vlc_input_policy_t *other = new_policy();
    assert(vlc_input_policy_AttachItem(fixture.item, other) != VLC_SUCCESS);
    assert(vlc_metadata_job_Apply(fixture.job, fixture.item, 0, NULL) == -EAGAIN);
    field(fixture.item, vlc_meta_Genre, NULL); vlc_input_policy_Release(policy); vlc_input_policy_Release(other); clean(&fixture);

    init(&fixture); start(&fixture); complete(&fixture); input_item_t *copy = input_item_Copy(fixture.item); assert(copy != NULL);
    assert(vlc_metadata_job_Apply(fixture.job, copy, 0, NULL) == VLC_EINVAL);
    field(copy, vlc_meta_Genre, NULL); input_item_Release(copy); clean(&fixture);
    assert(!vlc_input_policy_IsRevoked(NULL));
}
struct waiting_commit { struct fixture *fixture; vlc_sem_t entered; int status; };
static void *apply_after_lock(void *opaque)
{
    struct waiting_commit *waiter = opaque; vlc_sem_post(&waiter->entered);
    waiter->status = vlc_metadata_job_Apply(waiter->fixture->job, waiter->fixture->item, 0, NULL); return NULL;
}
static void revocation_before_admission(void)
{
    struct fixture fixture; init(&fixture); vlc_input_policy_t *policy = new_policy();
    assert(vlc_input_policy_AttachItem(fixture.item, policy) == VLC_SUCCESS); start(&fixture); complete(&fixture);
    struct waiting_commit waiter = {.fixture=&fixture}; vlc_sem_init(&waiter.entered, 0);
    vlc_mutex_lock(&fixture.item->lock); vlc_thread_t thread;
    assert(vlc_clone(&thread, apply_after_lock, &waiter) == 0); vlc_sem_wait(&waiter.entered);
    vlc_input_policy_Revoke(policy); vlc_mutex_unlock(&fixture.item->lock); vlc_join(thread, NULL);
    assert(waiter.status == -EACCES);
    field(fixture.item, vlc_meta_Description, NULL); field(fixture.item, vlc_meta_Genre, NULL);
    vlc_input_policy_Release(policy); clean(&fixture);
}
static void cancellation_and_close_before_admission(void)
{
    struct fixture fixture; init(&fixture); start(&fixture);
    assert(vlc_metadata_job_Cancel(fixture.job)); assert(vlc_metadata_job_Apply(fixture.job, fixture.item, 0, NULL) != VLC_SUCCESS);
    field(fixture.item, vlc_meta_Genre, NULL); clean(&fixture);

    init(&fixture); start(&fixture); complete(&fixture); assert(!vlc_metadata_job_Cancel(fixture.job));
    assert(vlc_metadata_job_Apply(fixture.job, fixture.item, 0, NULL) == -EINTR);
    field(fixture.item, vlc_meta_Genre, NULL); clean(&fixture);

    init(&fixture); start(&fixture); complete(&fixture); vlc_metadata_backend_Close(fixture.backend);
    assert(vlc_metadata_job_Apply(fixture.job, fixture.item, 0, NULL) == -EINTR);
    field(fixture.item, vlc_meta_Genre, NULL); clean(&fixture);
}
static void bounded_replacement_values(void)
{
    struct fixture fixture; init(&fixture); start(&fixture);
    input_item_t *result = input_item_Copy(vlc_metadata_job_GetRequest(fixture.job)->item); assert(result != NULL);
    const size_t length = 600 * 1024; char *large = malloc(length + 1); assert(large != NULL);
    memset(large, 'a', length); large[length] = '\0';
    input_item_SetDescription(result, large); input_item_SetGenre(result, large); free(large);
    assert(vlc_metadata_job_Complete(fixture.job, VLC_SUCCESS, result, NULL, 0)); input_item_Release(result);
    assert(vlc_metadata_job_Apply(fixture.job, fixture.item, 0, NULL) == VLC_EINVAL);
    field(fixture.item, vlc_meta_Description, NULL); field(fixture.item, vlc_meta_Genre, NULL); clean(&fixture);
}
int main(void)
{
    complete_commit_and_repeat(); fresh_and_absent_metadata(); preserve_existing_and_explicit_replace(); stale_fields_never_partially_commit();
    uri_scope_and_revocation(); revocation_before_admission(); cancellation_and_close_before_admission();
    bounded_replacement_values(); return 0;
}
