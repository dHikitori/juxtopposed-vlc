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

int native_import_unused_local_baseline_main(void);
#define main native_import_unused_local_baseline_main
#include "../misc/metadata_local.c"
#undef main
#include "../../../src/preparser/external.c"

/* ASAN retains the external operations table. These unused routing paths
 * cannot substitute for a real request-engine or process test. */
const char vlc_module_name[] = "native-preparser-import-test";
bool vlc_preparser_internal_HasRequest(void *owner, const struct vlc_preparser_req *request)
{ (void)owner; (void)request; abort(); }

static void import_preserves_all_facts_on_allocation_failure(void)
{
    unsigned refused = 0;
    for (size_t index = 0; index < 256; ++index) {
        struct fixture f; init(&f);
        input_item_slave_t *selected = input_item_slave_New("file:///selected.srt", SLAVE_TYPE_SPU, SLAVE_PRIORITY_USER);
        assert(selected != NULL); selected->b_forced = true;
        assert(input_item_AddSlave(f.item, selected) == VLC_SUCCESS);
        start(&f); local_facts(f.parsed);
        input_item_SetMetaExtra(f.parsed, "Child extra", "Complete value");
        input_item_SetTitle(f.parsed, "Child title");
        input_item_SetName(f.parsed, "Child name");
        f.parsed->pp_slaves[0]->b_forced = false;
        input_item_slave_t *added = input_item_slave_New("file:///new.srt", SLAVE_TYPE_SPU, SLAVE_PRIORITY_MATCH_ALL);
        assert(added != NULL && input_item_AddSlave(f.parsed, added) == VLC_SUCCESS);
        allocation_at = index; allocation_count = 0; allocation_fault = true;
        const int status = import_native_result(f.item, f.parsed);
        allocation_fault = false;
        if (status == VLC_SUCCESS) {
            assert(refused >= 20 && refused < 256);
            assert(f.item->i_duration == VLC_TICK_FROM_SEC(123) && f.item->es_vec.size == 1);
            assert(f.item->p_stats != NULL && f.item->p_stats->i_read_bytes == 1234 && f.item->i_epg_time == 2000);
            assert(f.item->i_epg == 1 && f.item->pp_epg[0]->i_event == 1 && f.item->p_epg_table != NULL);
            assert(f.item->i_slaves == 2 && f.item->pp_slaves[0] == selected && selected->b_forced);
            assert(f.item->i_options == 1 && !strcmp(f.item->ppsz_options[0], "no-audio"));
            assert(!strcmp(f.item->psz_name, "User name") && !strcmp(f.item->psz_uri, "file:///local-parse.flac"));
            field(f.item, vlc_meta_Title, "Child title");
            info(f.item, "Parsed", "Format", "Native format");
            char *extra = input_item_GetMetaExtra(f.item, "Child extra"); assert(extra != NULL && !strcmp(extra, "Complete value")); free(extra);
            assert(input_item_IsPreparsed(f.item)); clean(&f); break;
        }
        assert(status == VLC_ENOMEM); ++refused;
        assert(f.item->i_duration == INPUT_DURATION_UNSET && f.item->es_vec.size == 0 && f.item->p_stats == NULL && f.item->i_epg == 0);
        assert(f.item->i_slaves == 1 && f.item->pp_slaves[0] == selected && selected->b_forced);
        assert(f.item->i_options == 1 && !strcmp(f.item->psz_name, "User name"));
        field(f.item, vlc_meta_Title, "Original title"); info(f.item, "Parsed", "Format", "");
        assert(input_item_GetMetaExtra(f.item, "Child extra") == NULL && !input_item_IsPreparsed(f.item)); clean(&f);
    }
    assert(refused < 256);
}
static void import_refuses_uri_mismatch_before_writes(void)
{
    struct fixture f; init(&f); start(&f); local_facts(f.parsed);
    input_item_SetURI(f.parsed, "file:///other.flac");
    assert(import_native_result(f.item, f.parsed) == VLC_EINVAL);
    assert(f.item->i_duration == INPUT_DURATION_UNSET && f.item->es_vec.size == 0);
    field(f.item, vlc_meta_Title, "Original title"); clean(&f);
}
int main(void)
{
    import_preserves_all_facts_on_allocation_failure();
    import_refuses_uri_mismatch_before_writes(); return 0;
}
