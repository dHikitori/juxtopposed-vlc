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
#include "../../../src/preparser/metadata_backend.c"
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
int main(void)
{
    allocation_failures_leave_every_fact(); failed_local_snapshot_can_retry();
    failed_extra_and_status_reservation_has_no_native_writes();
    cancellation_during_value_preparation_has_no_native_writes();
    saturated_native_token_has_no_partial_commit(); limits_leave_no_attached_delta(); return 0;
}
