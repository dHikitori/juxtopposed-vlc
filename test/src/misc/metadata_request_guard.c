// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_input_item.h>
#include <vlc_metadata_backend.h>
#include <vlc_threads.h>
#include <assert.h>
#include "../../../src/preparser/metadata_request_guard.h"
#include "../../../src/preparser/metadata_local.h"

static unsigned pause_apply;
static vlc_sem_t apply_entered, resume_apply;
static int apply_with_barrier(vlc_metadata_job_t *job, input_item_t *target, uint32_t replace, size_t *applied)
{
    const unsigned pause = pause_apply; pause_apply = 0;
    if (pause == 1) { vlc_sem_post(&apply_entered); vlc_sem_wait(&resume_apply); }
    const int status = vlc_metadata_job_Apply(job, target, replace, applied);
    if (pause == 2) { vlc_sem_post(&apply_entered); vlc_sem_wait(&resume_apply); }
    return status;
}
#define vlc_metadata_job_Apply apply_with_barrier
#include "../../../src/preparser/metadata_request_guard.c"
#undef vlc_metadata_job_Apply

struct fixture {
    input_item_t *item;
    vlc_metadata_backend_t *backend;
    vlc_metadata_job_t *job;
    vlc_metadata_pipeline_guard_t *guard;
    unsigned ended;
};
static int dispatch(void *opaque, vlc_metadata_job_t *job)
{ (void)opaque; (void)job; abort(); }
static void ended(vlc_metadata_job_t *job, int status, void *opaque)
{ (void)job; assert(status == VLC_SUCCESS); ++((struct fixture *)opaque)->ended; }
static void init_with_events(struct fixture *f, bool change, bool sidebands)
{
    memset(f, 0, sizeof(*f));
    static const struct vlc_metadata_backend_operations ops = {.dispatch=dispatch};
    f->backend = vlc_metadata_backend_New(&ops, NULL); f->guard = vlc_metadata_pipeline_guard_New();
    f->item = input_item_New("file:///guard.flac", "Guard");
    assert(f->backend != NULL && f->guard != NULL && f->item != NULL);
    const struct vlc_metadata_request request = {.feature="native.transaction", .operation="anchor", .item=f->item};
    f->job = vlc_metadata_job_New(f->backend, &request, ended, f); assert(f->job != NULL);
    assert(vlc_metadata_job_StartLocal(f->job) == VLC_SUCCESS);
    input_item_t *result = sidebands ? vlc_metadata_job_CopyLocalItem(f->job)
        : input_item_Copy(vlc_metadata_job_GetRequest(f->job)->item); assert(result != NULL);
    if (change) input_item_SetDescription(result, "Native result");
    if (sidebands) {
        input_item_node_t *tree = input_item_node_Create(result); assert(tree != NULL);
        struct vlc_metadata_local_events events = {.subtrees=&tree,.subtree_count=1};
        assert(vlc_metadata_job_PrepareLocal(f->job, result, &events) == VLC_SUCCESS); input_item_node_Delete(tree);
    }
    assert(vlc_metadata_job_Complete(f->job, VLC_SUCCESS, result, NULL, 0)); input_item_Release(result);
    assert(f->ended == 1);
}
static void init(struct fixture *f, bool change) { init_with_events(f, change, false); }
static void clean(struct fixture *f)
{
    vlc_metadata_backend_Close(f->backend); vlc_metadata_job_Release(f->job);
    vlc_metadata_backend_Release(f->backend); input_item_Release(f->item);
    vlc_metadata_pipeline_guard_Release(f->guard);
}
static void description(struct fixture *f, const char *expected)
{
    char *value = input_item_GetDescription(f->item);
    assert(expected != NULL ? value != NULL && !strcmp(value, expected) : value == NULL); free(value);
}
static void idle_cancel_refuses_publication(void)
{
    struct fixture f; init(&f, true);
    assert(vlc_metadata_pipeline_guard_IsCurrent(f.guard)); assert(vlc_metadata_pipeline_guard_TryCancel(f.guard));
    assert(!vlc_metadata_pipeline_guard_TryCancel(f.guard) && !vlc_metadata_pipeline_guard_IsCurrent(f.guard));
    size_t applied = 1;
    assert(vlc_metadata_pipeline_guard_Commit(f.guard, f.job, f.item, 0, &applied, NULL) == -EINTR && applied == 0);
    assert(!vlc_metadata_pipeline_guard_IsCommitted(f.guard)); description(&f, NULL); clean(&f);
}
static void committed_status_and_sideband_authority_stay(void)
{
    for (unsigned change = 0; change < 2; ++change) {
        struct fixture f; init(&f, change); size_t applied = 9;
        assert(vlc_metadata_pipeline_guard_Commit(f.guard, f.job, f.item, 0, &applied, NULL) == VLC_SUCCESS && applied == change);
        assert(vlc_metadata_pipeline_guard_IsCommitted(f.guard) && !vlc_metadata_pipeline_guard_IsCurrent(f.guard));
        assert(!vlc_metadata_pipeline_guard_TryCancel(f.guard));
        assert(vlc_metadata_pipeline_guard_Finish(f.guard, -EINTR) == VLC_SUCCESS);
        int status = -EINTR; assert(vlc_metadata_pipeline_guard_GetStatus(f.guard, &status) && status == VLC_SUCCESS);
        assert(vlc_metadata_pipeline_guard_Commit(f.guard, f.job, f.item, 0, NULL, NULL) == VLC_EINVAL);
        description(&f, change ? "Native result" : NULL); clean(&f);
    }
}
static void stale_refusal_cannot_become_commit_success(void)
{
    struct fixture f; init(&f, true); input_item_SetDescription(f.item, NULL);
    assert(vlc_metadata_pipeline_guard_Commit(f.guard, f.job, f.item, 0, NULL, NULL) == -EAGAIN);
    assert(!vlc_metadata_pipeline_guard_IsCommitted(f.guard) && !vlc_metadata_pipeline_guard_TryCancel(f.guard));
    assert(vlc_metadata_pipeline_guard_Finish(f.guard, VLC_SUCCESS) == -EAGAIN);
    description(&f, NULL); clean(&f);
}
struct contender { struct fixture *fixture; vlc_sem_t *entered; int status; bool cancelled; struct vlc_metadata_local_events events; };
static void *commit_thread(void *opaque)
{
    struct contender *call = opaque;
    call->status = vlc_metadata_pipeline_guard_Commit(call->fixture->guard, call->fixture->job, call->fixture->item, 0, NULL, &call->events);
    return NULL;
}
static void *cancel_thread(void *opaque)
{
    struct contender *call = opaque; vlc_sem_post(call->entered);
    call->cancelled = vlc_metadata_pipeline_guard_TryCancel(call->fixture->guard); return NULL;
}
static void cancellation_after_apply_admission_cannot_relabel_output(void)
{
    for (unsigned pause = 1; pause <= 2; ++pause) {
        struct fixture f; init_with_events(&f, true, true);
        vlc_sem_init(&apply_entered, 0); vlc_sem_init(&resume_apply, 0); vlc_sem_t cancel_entered; vlc_sem_init(&cancel_entered, 0);
        pause_apply = pause; struct contender commit = {.fixture=&f}, cancel = {.fixture=&f,.entered=&cancel_entered};
        vlc_thread_t writer, canceller; assert(vlc_clone(&writer, commit_thread, &commit) == 0); vlc_sem_wait(&apply_entered);
        assert(vlc_clone(&canceller, cancel_thread, &cancel) == 0); vlc_sem_wait(&cancel_entered); vlc_sem_post(&resume_apply);
        vlc_join(writer, NULL); vlc_join(canceller, NULL);
        assert(commit.status == VLC_SUCCESS && !cancel.cancelled && commit.events.subtree_count == 1);
        assert(vlc_metadata_pipeline_guard_Finish(f.guard, -EINTR) == VLC_SUCCESS && vlc_metadata_pipeline_guard_IsCommitted(f.guard));
        description(&f, "Native result"); vlc_metadata_local_events_Clear(&commit.events); clean(&f);
    }
}
static void a_finished_callback_without_apply_has_no_sideband_authority(void)
{
    struct fixture f; init(&f, true);
    assert(vlc_metadata_pipeline_guard_Finish(f.guard, VLC_SUCCESS) == VLC_SUCCESS);
    assert(!vlc_metadata_pipeline_guard_IsCommitted(f.guard));
    assert(vlc_metadata_pipeline_guard_Commit(f.guard, f.job, f.item, 0, NULL, NULL) == VLC_EINVAL);
    description(&f, NULL); clean(&f);
}
static void closed_backend_refusal_stays_terminal(void)
{
    struct fixture f; init(&f, true); vlc_metadata_backend_Close(f.backend);
    assert(vlc_metadata_pipeline_guard_Commit(f.guard, f.job, f.item, 0, NULL, NULL) == -EINTR);
    assert(!vlc_metadata_pipeline_guard_IsCommitted(f.guard));
    assert(vlc_metadata_pipeline_guard_Finish(f.guard, VLC_SUCCESS) == -EINTR);
    description(&f, NULL); clean(&f);
}
int main(void)
{
    idle_cancel_refuses_publication(); committed_status_and_sideband_authority_stay();
    stale_refusal_cannot_become_commit_success(); cancellation_after_apply_admission_cannot_relabel_output();
    a_finished_callback_without_apply_has_no_sideband_authority(); closed_backend_refusal_stays_terminal(); return 0;
}
