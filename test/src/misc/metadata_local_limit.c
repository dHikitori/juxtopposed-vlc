// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_input_item.h>
#include <vlc_input.h>
#include <vlc_meta.h>
#include <vlc_threads.h>
#include <vlc_artwork_policy.h>
#include <vlc_modules.h>
#include <vlc_url.h>
#include <vlc_charset.h>
#include "../../../src/input/input_internal.h"
#include "../../../src/preparser/metadata_local.h"
#include <assert.h>

/* Keep item.c and its private policy helper in this real-source fixture.
 * The directory option parser is outside this test. Refuse its use rather
 * than replace it with a successful no-op. */
#include "../../../src/input/policy.c"
void var_OptionParse(vlc_object_t *object, const char *option, bool trusted)
{
    (void)object; (void)option; (void)trusted; abort();
}

/* This executable owns its allocation faults and token sequence. No fault
 * setter enters Core. Fail once, then permit complete native cleanup. */
static size_t allocation_at, allocation_count;
static bool allocation_fault;
static vlc_metadata_job_t *cancel_at_preparation;
static bool fail_allocation(void)
{
    if (!allocation_fault) return false;
    if (allocation_count++ != allocation_at) return false;
    allocation_fault = false; return true;
}
static void *fault_malloc(size_t size) { return fail_allocation() ? NULL : malloc(size); }
static void *fault_calloc(size_t count, size_t size) { return fail_allocation() ? NULL : calloc(count, size); }
static char *fault_strdup(const char *value)
{
    if (cancel_at_preparation != NULL) {
        vlc_metadata_job_t *job = cancel_at_preparation; cancel_at_preparation = NULL;
        assert(!vlc_metadata_job_Cancel(job));
    }
    return fail_allocation() ? NULL : strdup(value);
}

#define malloc fault_malloc
#define calloc fault_calloc
#define strdup fault_strdup
int metadata_local_limit_unused_writer(vlc_object_t *, input_item_t *);
#undef input_item_WriteMeta
#define input_item_WriteMeta metadata_local_limit_unused_writer
#include "../../../src/input/meta.c"
#undef input_item_WriteMeta
#define same_string metadata_local_limit_artwork_same_string
#include "../../../src/input/artwork_policy.c"
#undef same_string
#include "../../../src/input/item_parse.c"
static vlc_sem_t *reserve_entered, *reserve_continue;
static unsigned reservation_calls;
static bool observed_reservation(unsigned count, uint64_t *first)
{
    ++reservation_calls;
    if (reserve_entered != NULL) {
        vlc_sem_post(reserve_entered); vlc_sem_wait(reserve_continue);
    }
    return vlc_meta_ReserveRevisions(count, first);
}
#define vlc_meta_ReserveRevisions observed_reservation
#include "../../../src/preparser/metadata_backend.c"
#undef vlc_meta_ReserveRevisions
#undef malloc
#undef calloc
#undef strdup

int metadata_local_baseline_main(void);
#define main metadata_local_baseline_main
#include "metadata_local.c"
#undef main

static void allocation_failures_leave_every_fact(void)
{
    unsigned refused = 0;
    for (size_t index = 0; index < 256; ++index) {
        struct fixture f; init(&f); start(&f); local_facts(f.parsed);
        allocation_at = index; allocation_count = 0; allocation_fault = true;
        const int status = vlc_metadata_job_PrepareLocal(f.job, f.parsed, NULL);
        allocation_fault = false;
        if (status == VLC_SUCCESS) { clean(&f); break; }
        ++refused;
        assert(status == VLC_ENOMEM && !f.job->local_prepared);
        assert(f.item->i_duration == INPUT_DURATION_UNSET && f.item->es_vec.size == 0);
        info(f.item, "Existing", "Owner", "User value"); info(f.item, "Parsed", "Format", "");
        assert(vlc_metadata_job_PrepareLocal(f.job, f.parsed, NULL) == VLC_SUCCESS);
        complete(&f); assert(vlc_metadata_job_Apply(f.job, f.item, 0, NULL) == VLC_SUCCESS); clean(&f);
    }
    assert(refused >= 20 && refused < 256);
}
static void failed_local_snapshot_can_retry(void)
{
    unsigned refused = 0;
    for (size_t index = 0; index < 128; ++index) {
        struct fixture f; init(&f); start_anchor(&f);
        allocation_at = index; allocation_count = 0; allocation_fault = true;
        f.parsed = vlc_metadata_job_CopyLocalItem(f.job); allocation_fault = false;
        if (f.parsed != NULL) { clean(&f); break; }
        ++refused; assert(f.job->local_item == NULL && vlc_metadata_job_IsPending(f.job));
        f.parsed = vlc_metadata_job_CopyLocalItem(f.job); assert(f.parsed != NULL);
        info(f.parsed, "Existing", "Owner", "User value");
        assert(f.item->i_duration == INPUT_DURATION_UNSET); clean(&f);
    }
    assert(refused >= 10 && refused < 128);
}
static void failed_extra_and_status_reservation_has_no_native_writes(void)
{
    struct fixture f; init(&f); start(&f); local_facts(f.parsed);
    assert(vlc_metadata_job_PrepareLocal(f.job, f.parsed, NULL) == VLC_SUCCESS); complete(&f);
    const uint_fast64_t saved = atomic_load(&metadata_revision_sequence);
    atomic_store(&metadata_revision_sequence, UINT64_MAX - 3);
    /* The description, extra tags, and status need three reserved tokens. */
    assert(vlc_metadata_job_Apply(f.job, f.item, 0, NULL) == -EAGAIN);
    assert(f.item->i_duration == INPUT_DURATION_UNSET && f.item->es_vec.size == 0 && f.item->i_epg == 0);
    assert(vlc_meta_GetExtra(f.item->p_meta, "Native extra") == NULL);
    field(f.item, vlc_meta_Description, NULL); assert(!input_item_IsPreparsed(f.item));
    assert(atomic_load(&metadata_revision_sequence) == UINT64_MAX - 3);
    atomic_store(&metadata_revision_sequence, saved); clean(&f);
}
static void cancellation_during_value_preparation_has_no_native_writes(void)
{
    struct fixture f; init(&f); start(&f); local_facts(f.parsed);
    assert(vlc_metadata_job_PrepareLocal(f.job, f.parsed, NULL) == VLC_SUCCESS); complete(&f);
    cancel_at_preparation = f.job;
    assert(vlc_metadata_job_Apply(f.job, f.item, 0, NULL) == -EINTR && cancel_at_preparation == NULL);
    assert(f.item->i_duration == INPUT_DURATION_UNSET && f.item->es_vec.size == 0);
    info(f.item, "Parsed", "Format", ""); field(f.item, vlc_meta_Description, NULL); clean(&f);
}
static void fetched_artwork_reserves_one_combined_status_token(void)
{
    struct fixture f; prepare_fetched_artwork(&f);
    const uint_fast64_t saved = atomic_load(&metadata_revision_sequence);
    atomic_store(&metadata_revision_sequence, UINT64_MAX - 4);
    /* Artwork, description, extras, and the combined status need four tokens. */
    assert(vlc_metadata_job_Apply(f.job, f.item, 0, NULL) == -EAGAIN);
    assert(f.item->i_duration == INPUT_DURATION_UNSET && !input_item_IsArtFetched(f.item));
    assert(!input_item_IsPreparsed(f.item) && item_owner(f.item)->artwork_uri == NULL);
    assert(vlc_meta_GetExtra(f.item->p_meta, "Native extra") == NULL);
    field(f.item, vlc_meta_ArtworkURL, NULL); field(f.item, vlc_meta_Description, NULL);
    assert(atomic_load(&metadata_revision_sequence) == UINT64_MAX - 4);
    atomic_store(&metadata_revision_sequence, saved); clean(&f);
}
static void fetched_artwork_preparation_failures_leave_no_receipt(void)
{
    unsigned refused = 0;
    for (size_t index = 0; index < 16; ++index) {
        struct fixture f; init(&f); start(&f);
        assert(vlc_metadata_job_PrepareLocal(f.job, f.parsed, NULL) == VLC_SUCCESS);
        struct artwork_child child; artwork_child_start(&child, &f, "file:///cache/approved.png", true);
        assert(vlc_metadata_job_Apply(child.job, f.parsed, 0, NULL) == VLC_SUCCESS);
        allocation_at = index; allocation_count = 0; allocation_fault = true;
        const int status = vlc_metadata_job_PrepareFetchedArtwork(f.job, child.job, f.parsed, false);
        allocation_fault = false;
        if (status != VLC_SUCCESS) {
            ++refused; assert(status == VLC_ENOMEM && f.job->fetched_artwork == NULL);
            assert(!input_item_IsArtFetched(f.item)); field(f.item, vlc_meta_ArtworkURL, NULL);
            assert(vlc_metadata_job_PrepareFetchedArtwork(f.job, child.job, f.parsed, false) == VLC_SUCCESS);
        }
        artwork_child_clear(&child); complete(&f);
        assert(vlc_metadata_job_Apply(f.job, f.item, 0, NULL) == VLC_SUCCESS && input_item_IsArtFetched(f.item));
        clean(&f); if (status == VLC_SUCCESS) break;
    }
    assert(refused >= 4 && refused < 16);
}
static void saturated_native_token_has_no_partial_commit(void)
{
    struct fixture f; init(&f);
    item_owner(f.item)->parse_revisions.fields[VLC_ITEM_PARSE_DURATION] = UINT64_MAX - 1;
    start(&f); input_item_SetDuration(f.parsed, 1);
    assert(vlc_metadata_job_PrepareLocal(f.job, f.parsed, NULL) == VLC_SUCCESS); complete(&f);
    /* No admitted write can produce an exhausted native token. */
    assert(vlc_metadata_job_Apply(f.job, f.item, 0, NULL) == -EAGAIN);
    assert(item_owner(f.item)->parse_revisions.fields[VLC_ITEM_PARSE_DURATION] == UINT64_MAX - 1);
    assert(f.item->i_duration == INPUT_DURATION_UNSET); field(f.item, vlc_meta_Description, NULL);
    clean(&f);

    init(&f); start(&f); local_facts(f.parsed);
    assert(vlc_metadata_job_PrepareLocal(f.job, f.parsed, NULL) == VLC_SUCCESS); complete(&f);
    item_owner(f.item)->parse_revisions.fields[VLC_ITEM_PARSE_DURATION] = UINT64_MAX;
    assert(vlc_metadata_job_Apply(f.job, f.item, 0, NULL) == -EAGAIN);
    field(f.item, vlc_meta_Description, NULL); info(f.item, "Parsed", "Format", ""); clean(&f);
}
static void limits_leave_no_attached_delta(void)
{
    struct fixture f; init(&f); start(&f);
    struct vlc_metadata_local_events events = {.attachment_count=VLC_ITEM_PARSE_MAX_ENTRIES + 1};
    assert(vlc_metadata_job_PrepareLocal(f.job, f.parsed, &events) == VLC_EINVAL && !f.job->local_prepared);
    input_attachment_t attachment = {.i_data=VLC_ITEM_PARSE_MAX_BYTES + 1}; input_attachment_t *array[] = {&attachment};
    events = (struct vlc_metadata_local_events){.attachments=array,.attachment_count=1};
    assert(vlc_metadata_job_PrepareLocal(f.job, f.parsed, &events) == VLC_EINVAL && !f.job->local_prepared);
    input_item_node_t *root = input_item_node_Create(f.parsed), *node = root; assert(root != NULL);
    for (unsigned i = 0; i < 32; ++i) { node = input_item_node_AppendItem(node, f.parsed); assert(node != NULL); }
    events = (struct vlc_metadata_local_events){.subtrees=&root,.subtree_count=1};
    assert(vlc_metadata_job_PrepareLocal(f.job, f.parsed, &events) != VLC_SUCCESS && !f.job->local_prepared);
    input_item_node_Delete(root);
    char *large = malloc(VLC_ITEM_PARSE_MAX_BYTES + 1); assert(large != NULL);
    memset(large, 'A', VLC_ITEM_PARSE_MAX_BYTES); large[VLC_ITEM_PARSE_MAX_BYTES] = 0;
    input_item_SetMetaExtra(f.parsed, "Large tag", large); free(large);
    assert(vlc_metadata_job_PrepareLocal(f.job, f.parsed, NULL) == VLC_ENOMEM && !f.job->local_prepared);
    assert(f.item->i_duration == INPUT_DURATION_UNSET); clean(&f);
}
static void notfound_allocation_failures_leave_no_sideband(void)
{
    unsigned refused = 0;
    for (size_t index = 0; index < 256; ++index) {
        struct fixture f; init(&f); start(&f);
        for (unsigned tag = 0; tag < 4; ++tag) {
            char key[32]; snprintf(key, sizeof(key), "Scoped work ID %u", tag);
            input_item_SetMetaExtra(f.parsed, key, "Bounded native value");
        }
        assert(vlc_metadata_job_PrepareLocal(f.job, f.parsed, NULL) == VLC_SUCCESS);
        struct artwork_child child; notfound_child_start(&child, &f);
        struct exhaustion_keeper keeper = {.gate=vlc_metadata_artwork_admission_New(vlc_tick_now()+VLC_TICK_FROM_SEC(5))}; assert(keeper.gate);
        const struct vlc_metadata_artwork_exhaustion_summary summary = exhaustion_summary();
        allocation_at=index; allocation_count=0; allocation_fault=true;
        const int status=vlc_metadata_job_SealArtworkNotFound(child.job, keeper.gate, &summary, &keeper, exhaustion_keeper_release);
        allocation_fault=false;
        if (status != VLC_SUCCESS) {
            ++refused; assert(status == VLC_ENOMEM && child.job->artwork_exhaustion == NULL && keeper.releases == 0);
            assert(vlc_metadata_job_SealArtworkNotFound(child.job, keeper.gate, &summary, &keeper, exhaustion_keeper_release) == VLC_SUCCESS);
        }
        notfound_child_complete(&child, &f);
        assert(vlc_metadata_job_PrepareNotFoundArtwork(f.job, child.job, f.parsed) == VLC_SUCCESS);
        artwork_child_clear(&child); complete(&f); assert(vlc_metadata_job_Apply(f.job, f.item, 0, NULL) == VLC_SUCCESS);
        assert(vlc_meta_GetStatus(f.item->p_meta)&ITEM_ART_NOTFOUND); clean(&f); assert(keeper.releases==1);
        vlc_metadata_artwork_admission_Release(keeper.gate);
        if (status == VLC_SUCCESS) break;
    }
    assert(refused >= 3 && refused < 256);
}
static void notfound_and_local_status_reserve_only_one_joint_token(void)
{
    for (unsigned remaining=3; remaining<=4; ++remaining) {
        struct exhaustion_keeper keeper = {.gate=vlc_metadata_artwork_admission_New(vlc_tick_now()+VLC_TICK_FROM_SEC(5))}; assert(keeper.gate);
        struct fixture f; prepare_notfound(&f, &keeper);
        const uint_fast64_t saved=atomic_load(&metadata_revision_sequence);
        const uint64_t status_token=vlc_meta_StatusRevision(f.item->p_meta);
        atomic_store(&metadata_revision_sequence, UINT64_MAX-remaining);
        const int status=vlc_metadata_job_Apply(f.job, f.item, 0, NULL);
        if (remaining==3) {
            assert(status==-EAGAIN && vlc_meta_StatusRevision(f.item->p_meta)==status_token);
            assert(!(vlc_meta_GetStatus(f.item->p_meta)&ITEM_ART_NOTFOUND) && !input_item_IsPreparsed(f.item));
            assert(f.item->i_duration==INPUT_DURATION_UNSET); field(f.item,vlc_meta_Description,NULL);
            assert(vlc_meta_GetExtra(f.item->p_meta,"Native extra")==NULL);
            assert(atomic_load(&metadata_revision_sequence)==UINT64_MAX-3);
        } else {
            assert(status==VLC_SUCCESS && atomic_load(&metadata_revision_sequence)==UINT64_MAX-1);
            assert((vlc_meta_GetStatus(f.item->p_meta)&(ITEM_PREPARSED|ITEM_ART_NOTFOUND))==(ITEM_PREPARSED|ITEM_ART_NOTFOUND));
        }
        atomic_store(&metadata_revision_sequence,saved); clean(&f); assert(keeper.releases==1);
        vlc_metadata_artwork_admission_Release(keeper.gate);
    }
}
struct notfound_apply_call { struct fixture *fixture; int status; };
static void *notfound_apply(void *opaque)
{
    struct notfound_apply_call *call=opaque;
    call->status=vlc_metadata_job_Apply(call->fixture->job,call->fixture->item,0,NULL); return NULL;
}
struct notfound_revoke_call { vlc_metadata_artwork_admission_t *gate; input_item_t *item; atomic_bool done; };
static void *notfound_revoke(void *opaque)
{
    struct notfound_revoke_call *call=opaque; vlc_metadata_artwork_admission_Revoke(call->gate);
    vlc_mutex_lock(&call->item->lock); assert(vlc_meta_GetStatus(call->item->p_meta)&ITEM_ART_NOTFOUND); vlc_mutex_unlock(&call->item->lock);
    atomic_store(&call->done,true); return NULL;
}
static void actual_apply_finishes_before_concurrent_revocation_returns(void)
{
    struct exhaustion_keeper keeper = {.gate=vlc_metadata_artwork_admission_New(vlc_tick_now()+VLC_TICK_FROM_SEC(5))}; assert(keeper.gate);
    struct fixture f; prepare_notfound(&f,&keeper);
    vlc_sem_t entered,proceed; vlc_sem_init(&entered,0);vlc_sem_init(&proceed,0);
    reserve_entered=&entered;reserve_continue=&proceed;
    struct notfound_apply_call apply={.fixture=&f};vlc_thread_t writer;assert(vlc_clone(&writer,notfound_apply,&apply)==0);
    vlc_sem_wait(&entered); /* Actual backend is inside its all-token reservation. */
    struct notfound_revoke_call revoke={.gate=keeper.gate,.item=f.item};atomic_init(&revoke.done,false);
    vlc_thread_t invalidator;assert(vlc_clone(&invalidator,notfound_revoke,&revoke)==0);
    const vlc_tick_t deadline=vlc_tick_now()+VLC_TICK_FROM_SEC(2);
    while(vlc_metadata_artwork_admission_IsCurrent(keeper.gate)&&vlc_tick_now()<deadline)vlc_tick_sleep(VLC_TICK_FROM_MS(100));
    assert(!vlc_metadata_artwork_admission_IsCurrent(keeper.gate)&&!atomic_load(&revoke.done));
    vlc_sem_post(&proceed);vlc_join(writer,NULL);vlc_join(invalidator,NULL);reserve_entered=reserve_continue=NULL;
    assert(apply.status==VLC_SUCCESS&&atomic_load(&revoke.done));clean(&f);assert(keeper.releases==1);
    vlc_metadata_artwork_admission_Release(keeper.gate);
}

static void notfound_effective_scope_refusal_precedes_token_reservation(void)
{
    for (unsigned mode = 0; mode < 2; ++mode) {
        struct exhaustion_keeper keeper = {.gate=vlc_metadata_artwork_admission_New(vlc_tick_now()+VLC_TICK_FROM_SEC(5))}; assert(keeper.gate);
        struct fixture f; init(&f); start(&f); local_facts(f.parsed);
        if (mode == 0) input_item_SetTitle(f.parsed, "Lookup title");
        assert(vlc_metadata_job_PrepareLocal(f.job, f.parsed, NULL) == VLC_SUCCESS);
        if (mode == 1) input_item_SetMetaExtra(f.parsed, "IMDBID", "tt2222222");
        notfound_seal_and_complete_child(&f, &keeper); complete(&f);
        const struct notfound_target_tokens before = notfound_tokens(f.item);
        const unsigned calls = reservation_calls;
        const uint_fast64_t sequence = atomic_load(&metadata_revision_sequence);
        assert(vlc_metadata_job_Apply(f.job, f.item, 0, NULL) == -EAGAIN);
        assert(reservation_calls == calls && atomic_load(&metadata_revision_sequence) == sequence);
        notfound_no_target_writes(&f, &before); clean(&f); assert(keeper.releases == 1);
        vlc_metadata_artwork_admission_Release(keeper.gate);
    }
}
static void notfound_oversized_extra_snapshot_has_no_sideband(void)
{
    struct fixture f; init(&f); start(&f);
    char *large = malloc(MAX_JSON + 2); assert(large);
    memset(large, 'A', MAX_JSON + 1); large[MAX_JSON + 1] = '\0';
    input_item_SetMetaExtra(f.parsed, "Bounded lookup work ID", large); free(large);
    assert(vlc_metadata_job_PrepareLocal(f.job, f.parsed, NULL) == VLC_SUCCESS);
    struct artwork_child child; notfound_child_start(&child, &f);
    struct exhaustion_keeper keeper = {.gate=vlc_metadata_artwork_admission_New(vlc_tick_now()+VLC_TICK_FROM_SEC(5))}; assert(keeper.gate);
    const struct vlc_metadata_artwork_exhaustion_summary summary = exhaustion_summary();
    const struct notfound_target_tokens before = notfound_tokens(f.item);
    assert(vlc_metadata_job_SealArtworkNotFound(child.job, keeper.gate, &summary, &keeper, exhaustion_keeper_release) == VLC_ENOMEM);
    assert(child.job->artwork_exhaustion == NULL && keeper.releases == 0);
    notfound_no_target_writes(&f, &before); artwork_child_clear(&child); clean(&f);
    assert(keeper.releases == 0); vlc_metadata_artwork_admission_Release(keeper.gate);
}
struct notfound_working_mutation {
    input_item_t *working;
    vlc_sem_t attempted;
    atomic_bool writer_holds_working, done;
};
static void *notfound_change_working_at_reservation(void *opaque)
{
    struct notfound_working_mutation *call = opaque;
    const int locked = vlc_mutex_trylock(&call->working->lock);
    atomic_store(&call->writer_holds_working, locked != 0);
    if (locked == 0) vlc_mutex_unlock(&call->working->lock);
    vlc_sem_post(&call->attempted);
    /* The actual same-value native write must wait for final publication. */
    input_item_SetURI(call->working, "file:///local-parse.flac");
    atomic_store(&call->done, true); return NULL;
}
static void notfound_final_publication_serializes_a_working_mutation(void)
{
    struct exhaustion_keeper keeper = {.gate=vlc_metadata_artwork_admission_New(vlc_tick_now()+VLC_TICK_FROM_SEC(5))}; assert(keeper.gate);
    struct fixture f; prepare_notfound(&f, &keeper);
    vlc_sem_t entered, proceed; vlc_sem_init(&entered, 0); vlc_sem_init(&proceed, 0);
    reserve_entered = &entered; reserve_continue = &proceed;
    struct notfound_apply_call apply = {.fixture=&f}; vlc_thread_t writer;
    assert(vlc_clone(&writer, notfound_apply, &apply) == 0); vlc_sem_wait(&entered);
    struct notfound_working_mutation mutation = {.working=f.parsed};
    vlc_sem_init(&mutation.attempted, 0); atomic_init(&mutation.writer_holds_working, false); atomic_init(&mutation.done, false);
    vlc_thread_t mutator; assert(vlc_clone(&mutator, notfound_change_working_at_reservation, &mutation) == 0);
    vlc_sem_wait(&mutation.attempted);
    /* The writer is stopped inside the actual final token reservation. This
     * try-lock control proves a real held mutex, not scheduler delay alone. */
    assert(atomic_load(&mutation.writer_holds_working) && !atomic_load(&mutation.done));
    vlc_sem_post(&proceed); vlc_join(writer, NULL); vlc_join(mutator, NULL);
    reserve_entered = reserve_continue = NULL;
    assert(apply.status == VLC_SUCCESS && atomic_load(&mutation.done));
    assert(vlc_meta_GetStatus(f.item->p_meta) & ITEM_ART_NOTFOUND);
    field(f.item, vlc_meta_Description, "Remote description");
    assert(input_item_IsPreparsed(f.item) && f.item->i_duration == VLC_TICK_FROM_SEC(123));
    clean(&f); assert(keeper.releases == 1); vlc_metadata_artwork_admission_Release(keeper.gate);
}
static void positive_source_allocation_failures_keep_caller_ownership(void)
{
    unsigned refused = 0;
    for (size_t index = 0; index < 256; ++index) {
        struct fixture f; struct artwork_child child; struct exhaustion_keeper keeper;
        input_item_t *result = source_fixture(&f, &child, &keeper);
        allocation_at = index; allocation_count = 0; allocation_fault = true;
        const int status = source_seal(&child, result, &keeper); allocation_fault = false;
        if (status != VLC_SUCCESS) {
            ++refused; assert(status == VLC_ENOMEM); assert(child.job->artwork_source == NULL && keeper.releases == 0);
            assert(source_seal(&child, result, &keeper) == VLC_SUCCESS);
        }
        source_complete(&f, &child, result);
        assert(vlc_metadata_job_PrepareFetchedArtwork(f.job, child.job, f.parsed, false) == VLC_SUCCESS);
        input_item_Release(result); artwork_child_clear(&child); complete(&f);
        assert(vlc_metadata_job_Apply(f.job, f.item, 0, NULL) == VLC_SUCCESS);
        clean(&f); assert(keeper.releases == 1); vlc_metadata_artwork_admission_Release(keeper.gate);
        if (status == VLC_SUCCESS) break;
    }
    assert(refused >= 10 && refused < 256);
}

static void positive_source_final_allocation_failures_leave_every_fact(void)
{
    unsigned refused = 0;
    for (size_t index = 0; index < 256; ++index) {
        struct fixture f; struct artwork_child child; struct exhaustion_keeper keeper;
        input_item_t *result = source_fixture(&f, &child, &keeper);
        assert(source_seal(&child, result, &keeper) == VLC_SUCCESS); source_complete(&f, &child, result);
        assert(vlc_metadata_job_PrepareFetchedArtwork(f.job, child.job, f.parsed, false) == VLC_SUCCESS);
        complete(&f);
        const struct notfound_target_tokens before = notfound_tokens(f.item);
        const unsigned calls = reservation_calls;
        allocation_at = index; allocation_count = 0; allocation_fault = true;
        const int status = vlc_metadata_job_Apply(f.job, f.item, 0, NULL); allocation_fault = false;
        if (status != VLC_SUCCESS) {
            ++refused; assert(status == VLC_ENOMEM && reservation_calls == calls);
            notfound_no_target_writes(&f, &before);
        } else {
            field(f.item, vlc_meta_ArtworkURL, source_image); assert(input_item_IsArtFetched(f.item));
        }
        input_item_Release(result); artwork_child_clear(&child); clean(&f); assert(keeper.releases == 1);
        vlc_metadata_artwork_admission_Release(keeper.gate);
        if (status == VLC_SUCCESS) break;
    }
    assert(refused >= 4 && refused < 256);
}
static void *positive_source_revoke(void *opaque)
{
    struct notfound_revoke_call *call = opaque;
    vlc_metadata_artwork_admission_Revoke(call->gate);
    assert(input_item_IsArtFetched(call->item)); field(call->item, vlc_meta_ArtworkURL, source_image);
    atomic_store(&call->done, true); return NULL;
}
static void positive_source_final_apply_joins_revocation_and_locks_working(void)
{
    struct fixture f; struct artwork_child child; struct exhaustion_keeper keeper;
    input_item_t *result = source_fixture(&f, &child, &keeper);
    assert(source_seal(&child, result, &keeper) == VLC_SUCCESS); source_complete(&f, &child, result);
    assert(vlc_metadata_job_PrepareFetchedArtwork(f.job, child.job, f.parsed, false) == VLC_SUCCESS); complete(&f);
    vlc_sem_t entered, proceed; vlc_sem_init(&entered, 0); vlc_sem_init(&proceed, 0);
    reserve_entered = &entered; reserve_continue = &proceed;
    struct notfound_apply_call apply = {.fixture=&f}; vlc_thread_t writer;
    assert(vlc_clone(&writer, notfound_apply, &apply) == 0); vlc_sem_wait(&entered);
    struct notfound_working_mutation mutation = {.working=f.parsed};
    vlc_sem_init(&mutation.attempted, 0); atomic_init(&mutation.writer_holds_working, false); atomic_init(&mutation.done, false);
    vlc_thread_t mutator; assert(vlc_clone(&mutator, notfound_change_working_at_reservation, &mutation) == 0);
    vlc_sem_wait(&mutation.attempted); assert(atomic_load(&mutation.writer_holds_working) && !atomic_load(&mutation.done));
    struct notfound_revoke_call revoke = {.gate=keeper.gate, .item=f.item}; atomic_init(&revoke.done, false);
    vlc_thread_t invalidator; assert(vlc_clone(&invalidator, positive_source_revoke, &revoke) == 0);
    const vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(2);
    while (vlc_metadata_artwork_admission_IsCurrent(keeper.gate) && vlc_tick_now() < deadline)
        vlc_tick_wait(vlc_tick_now() + VLC_TICK_FROM_MS(100));
    assert(!vlc_metadata_artwork_admission_IsCurrent(keeper.gate) && !atomic_load(&revoke.done));
    vlc_sem_post(&proceed); vlc_join(writer, NULL); vlc_join(invalidator, NULL); vlc_join(mutator, NULL);
    reserve_entered = reserve_continue = NULL;
    assert(apply.status == VLC_SUCCESS && atomic_load(&revoke.done) && atomic_load(&mutation.done));
    input_item_Release(result); artwork_child_clear(&child); clean(&f); assert(keeper.releases == 1);
    vlc_metadata_artwork_admission_Release(keeper.gate);
}

int main(void)
{
    positive_source_allocation_failures_keep_caller_ownership();
    positive_source_final_allocation_failures_leave_every_fact();
    positive_source_final_apply_joins_revocation_and_locks_working();
    allocation_failures_leave_every_fact(); failed_local_snapshot_can_retry();
    failed_extra_and_status_reservation_has_no_native_writes();
    fetched_artwork_reserves_one_combined_status_token();
    fetched_artwork_preparation_failures_leave_no_receipt();
    cancellation_during_value_preparation_has_no_native_writes();
    saturated_native_token_has_no_partial_commit(); limits_leave_no_attached_delta();
    notfound_allocation_failures_leave_no_sideband(); notfound_and_local_status_reserve_only_one_joint_token();
    actual_apply_finishes_before_concurrent_revocation_returns();
    notfound_effective_scope_refusal_precedes_token_reservation();
    notfound_oversized_extra_snapshot_has_no_sideband();
    notfound_final_publication_serializes_a_working_mutation(); return 0;
}
