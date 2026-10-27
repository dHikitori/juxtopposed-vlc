// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_input_item.h>
#include <vlc_input.h>
#include <vlc_epg.h>
#include <vlc_meta.h>
#include <vlc_input_policy.h>
#include <vlc_metadata_backend.h>
#include <vlc_metadata_revision.h>
#include <vlc_threads.h>
#include "../../../src/input/item.h"
#include "../../../src/preparser/metadata_local.h"
#include <assert.h>

struct fixture {
    input_item_t *item, *parsed;
    vlc_metadata_backend_t *backend;
    vlc_metadata_job_t *job;
    unsigned ended;
};
static int dispatch(void *opaque, vlc_metadata_job_t *job)
{ (void)opaque; (void)job; abort(); }
static void ended(vlc_metadata_job_t *job, int status, void *opaque)
{ (void)job; (void)status; ++((struct fixture *)opaque)->ended; }
static void init(struct fixture *f)
{
    memset(f, 0, sizeof(*f));
    static const struct vlc_metadata_backend_operations ops = {.dispatch=dispatch};
    f->backend = vlc_metadata_backend_New(&ops, f); assert(f->backend != NULL);
    f->item = input_item_New("file:///local-parse.flac", "User name"); assert(f->item != NULL);
    input_item_SetTitle(f->item, "Original title");
    assert(input_item_AddInfo(f->item, "Existing", "Owner", "%s", "User value") == VLC_SUCCESS);
    assert(input_item_AddOption(f->item, "no-audio", VLC_INPUT_OPTION_TRUSTED) == VLC_SUCCESS);
}
static void start_anchor(struct fixture *f)
{
    const struct vlc_metadata_request request = {.feature="music.details", .operation="enrich",
        .item=f->item, .flags=VLC_METADATA_REQUEST_AUTOMATIC};
    f->job = vlc_metadata_job_New(f->backend, &request, ended, f); assert(f->job != NULL);
    assert(vlc_metadata_job_StartLocal(f->job) == VLC_SUCCESS);
}
static void start(struct fixture *f)
{
    start_anchor(f);
    f->parsed = vlc_metadata_job_CopyLocalItem(f->job); assert(f->parsed != NULL);
    assert(vlc_metadata_job_CopyLocalItem(f->job) == NULL);
}
static void clean(struct fixture *f)
{
    vlc_metadata_backend_Close(f->backend);
    if (f->parsed != NULL) input_item_Release(f->parsed);
    vlc_metadata_job_Release(f->job); vlc_metadata_backend_Release(f->backend); input_item_Release(f->item);
}
static void complete(struct fixture *f)
{
    input_item_t *remote = input_item_Copy(f->parsed); assert(remote != NULL);
    input_item_SetDescription(remote, "Remote description");
    /* Remote nonstandard facts cannot replace the sealed native delta. */
    input_item_SetDuration(remote, VLC_TICK_FROM_SEC(999));
    input_item_SetMetaExtra(remote, "Native extra", "Remote forgery");
    assert(input_item_AddInfo(remote, "Parsed", "Format", "%s", "Remote forgery") == VLC_SUCCESS);
    assert(vlc_metadata_job_Complete(f->job, VLC_SUCCESS, remote, NULL, 0));
    input_item_Release(remote); assert(f->ended == 1);
}
static void add_track(input_item_t *item, const char *id)
{
    input_item_t *source = input_item_Copy(item); assert(source != NULL);
    struct input_item_es track = {.id=strdup(id), .id_stable=true}; assert(track.id != NULL);
    es_format_Init(&track.es, AUDIO_ES, VLC_CODEC_FLAC);
    track.es.audio.i_rate = 48000; track.es.psz_language = strdup("en");
    assert(track.es.psz_language != NULL); assert(vlc_vector_push(&source->es_vec, track));
    assert(input_item_Update(item, source) == VLC_SUCCESS); input_item_Release(source);
}
static void set_epg(input_item_t *item, const char *name)
{
    vlc_epg_t *epg = vlc_epg_New(1, 2); assert(epg != NULL); epg->b_present = true;
    epg->psz_name = strdup("Channel"); assert(epg->psz_name != NULL);
    vlc_epg_event_t *event = vlc_epg_event_New(3, 1000, 60); assert(event != NULL);
    event->psz_name = strdup(name); event->psz_description = strdup("Native description");
    assert(event->psz_name != NULL && event->psz_description != NULL);
    assert(vlc_epg_AddEvent(epg, event)); vlc_epg_SetCurrent(epg, 1000);
    input_item_SetEpg(item, epg, true); vlc_epg_Delete(epg);
}
static void local_facts(input_item_t *item)
{
    input_item_SetDuration(item, VLC_TICK_FROM_SEC(123));
    assert(input_item_AddInfo(item, "Parsed", "Format", "%s", "Native format") == VLC_SUCCESS);
    vlc_mutex_lock(&item->lock);
    vlc_meta_SetExtraWithPriority(item->p_meta, "Native extra", "Native value", VLC_META_PRIORITY_INBAND);
    item->p_stats = calloc(1, sizeof(*item->p_stats)); assert(item->p_stats != NULL); item->p_stats->i_read_bytes = 1234;
    item->i_type = ITEM_TYPE_STREAM; vlc_item_parse_TouchLocked(item, VLC_ITEM_PARSE_TYPE);
    vlc_mutex_unlock(&item->lock);
    add_track(item, "audio/1"); set_epg(item, "Native event"); input_item_SetEpgTime(item, 2000);
    input_item_SetPreparsed(item);
}
static void field(input_item_t *item, vlc_meta_type_t type, const char *expected)
{
    char *value = input_item_GetMeta(item, type);
    assert(expected == NULL ? value == NULL : value != NULL && !strcmp(value, expected)); free(value);
}
static void info(input_item_t *item, const char *category, const char *name, const char *expected)
{
    char *value = input_item_GetInfo(item, category, name); assert(value != NULL && !strcmp(value, expected)); free(value);
}
static void full_native_parity_and_seal(void)
{
    struct fixture f; init(&f);
    input_item_SetMetaExtra(f.item, "Existing extra", "Original value");
    input_item_slave_t *existing = input_item_slave_New("file:///selected.srt", SLAVE_TYPE_SPU, SLAVE_PRIORITY_USER);
    assert(existing != NULL); existing->b_forced = true; assert(input_item_AddSlave(f.item, existing) == VLC_SUCCESS);
    start(&f); info(f.parsed, "Existing", "Owner", "User value");
    assert(f.parsed->i_options == 1 && f.parsed->optflagv[0] == VLC_INPUT_OPTION_TRUSTED);
    local_facts(f.parsed); input_item_SetName(f.parsed, "Parser name");
    input_item_slave_t *duplicate = input_item_slave_New("file:///selected.srt", SLAVE_TYPE_SPU, SLAVE_PRIORITY_MATCH_ALL);
    input_item_slave_t *new_link = input_item_slave_New("file:///native.srt", SLAVE_TYPE_SPU, SLAVE_PRIORITY_MATCH_ALL);
    assert(duplicate != NULL && new_link != NULL);
    assert(input_item_AddSlave(f.parsed, duplicate) == VLC_SUCCESS && input_item_AddSlave(f.parsed, new_link) == VLC_SUCCESS);
    assert(vlc_metadata_job_PrepareLocal(f.job, f.parsed, NULL) == VLC_SUCCESS);
    assert(vlc_metadata_job_PrepareLocal(f.job, f.parsed, NULL) != VLC_SUCCESS);
    complete(&f); assert(vlc_metadata_job_Apply(f.job, f.item, 0, NULL) == VLC_SUCCESS);
    assert(f.item->i_duration == VLC_TICK_FROM_SEC(123) && f.item->es_vec.size == 1);
    assert(f.item->es_vec.data[0].es.audio.i_rate == 48000 && !strcmp(f.item->es_vec.data[0].id, "audio/1"));
    info(f.item, "Parsed", "Format", "Native format"); info(f.item, "Existing", "Owner", "User value");
    assert(!strcmp(f.item->psz_name, "User name") && !strcmp(f.item->psz_uri, "file:///local-parse.flac"));
    assert(f.item->i_options == 1 && !strcmp(f.item->ppsz_options[0], "no-audio"));
    assert(f.item->pp_slaves[0] == existing && existing->b_forced && f.item->i_slaves == 2);
    assert(!strcmp(f.item->pp_slaves[1]->psz_uri, "file:///native.srt"));
    vlc_meta_priority_t priority;
    assert(!strcmp(vlc_meta_GetExtraWithPriority(f.item->p_meta, "Native extra", &priority), "Native value")
        && priority == VLC_META_PRIORITY_INBAND);
    assert(!strcmp(vlc_meta_GetExtra(f.item->p_meta, "Existing extra"), "Original value"));
    assert(f.item->i_epg == 1 && f.item->p_epg_table == f.item->pp_epg[0]);
    assert(!strcmp(f.item->p_epg_table->p_current->psz_name, "Native event") && f.item->i_epg_time == 2000);
    assert(f.item->p_stats != NULL && f.item->p_stats->i_read_bytes == 1234 && f.item->i_type == ITEM_TYPE_STREAM);
    assert(input_item_IsPreparsed(f.item)); field(f.item, vlc_meta_Description, "Remote description");
    clean(&f);
}
static void sideband_waits_for_admission(void)
{
    for (unsigned cancel = 0; cancel < 2; ++cancel) {
        struct fixture f; init(&f); start(&f);
        input_item_node_t *tree = input_item_node_Create(f.parsed); assert(tree != NULL);
        input_item_t *child = input_item_New("file:///child.flac", "Native child"); assert(child != NULL);
        assert(input_item_node_AppendItem(tree, child) != NULL);
        input_attachment_t *attachment = vlc_input_attachment_New("cover", "image/png", "Native image", "data", 4);
        assert(attachment != NULL);
        struct vlc_metadata_local_events events = {.subtrees=&tree,.subtree_count=1,.attachments=&attachment,.attachment_count=1};
        assert(vlc_metadata_job_PrepareLocal(f.job, f.parsed, &events) == VLC_SUCCESS);
        input_item_SetName(child, "Later mutation"); input_item_Release(child);
        input_item_node_Delete(tree); vlc_input_attachment_Release(attachment);
        struct vlc_metadata_local_events output = {0};
        assert(!vlc_metadata_job_TakeLocalEvents(f.job, &output)); complete(&f);
        assert(!vlc_metadata_job_TakeLocalEvents(f.job, &output));
        if (cancel) { assert(!vlc_metadata_job_Cancel(f.job)); assert(vlc_metadata_job_Apply(f.job, f.item, 0, NULL) == -EINTR); }
        else assert(vlc_metadata_job_Apply(f.job, f.item, 0, NULL) == VLC_SUCCESS);
        assert(vlc_metadata_job_TakeLocalEvents(f.job, &output) == !cancel);
        if (!cancel) {
            assert(output.subtree_count == 1 && output.attachment_count == 1);
            assert(!strcmp(output.subtrees[0]->pp_children[0]->p_item->psz_name, "Native child"));
            assert(output.attachments[0]->i_data == 4 && !memcmp(output.attachments[0]->p_data, "data", 4));
            assert(!vlc_metadata_job_TakeLocalEvents(f.job, &events));
        }
        vlc_metadata_local_events_Clear(&output); clean(&f);
    }
}
static void stale_native_same_value_and_aba(void)
{
    for (unsigned mode = 0; mode < 9; ++mode) {
        struct fixture f; init(&f); start(&f); local_facts(f.parsed);
        assert(vlc_metadata_job_PrepareLocal(f.job, f.parsed, NULL) == VLC_SUCCESS); complete(&f);
        if (mode == 0) input_item_SetDuration(f.item, f.item->i_duration);
        if (mode == 1) { const vlc_tick_t old = f.item->i_duration; input_item_SetDuration(f.item, 1); input_item_SetDuration(f.item, old); }
        if (mode == 2) input_item_SetName(f.item, "User name");
        if (mode == 3) { input_item_SetName(f.item, "Other"); input_item_SetName(f.item, "User name"); }
        if (mode == 4) assert(input_item_AddInfo(f.item, "Existing", "Owner", "%s", "User value") == VLC_SUCCESS);
        if (mode == 5) { assert(input_item_DelInfo(f.item, "Existing", "Owner") == VLC_SUCCESS); assert(input_item_AddInfo(f.item, "Existing", "Owner", "%s", "User value") == VLC_SUCCESS); }
        if (mode == 6) input_item_SetURI(f.item, "file:///local-parse.flac");
        if (mode == 7) assert(input_item_AddOption(f.item, "no-audio", 0) == VLC_SUCCESS);
        if (mode == 8) input_item_SetMetaExtra(f.item, "User extra", "Later user value");
        assert(vlc_metadata_job_Apply(f.job, f.item, 0, NULL) == -EAGAIN);
        field(f.item, vlc_meta_Description, NULL); info(f.item, "Parsed", "Format", ""); assert(f.item->es_vec.size == 0);
        clean(&f);
    }
}
static void live_progress_has_precedence(void)
{
    struct fixture f; init(&f); start(&f); local_facts(f.parsed);
    assert(vlc_metadata_job_PrepareLocal(f.job, f.parsed, NULL) == VLC_SUCCESS); complete(&f);
    vlc_mutex_lock(&f.item->lock); f.item->p_stats = calloc(1, sizeof(*f.item->p_stats)); assert(f.item->p_stats != NULL);
    f.item->p_stats->i_read_bytes = 9999; vlc_mutex_unlock(&f.item->lock);
    set_epg(f.item, "Live event"); input_item_SetEpgTime(f.item, 3000);
    assert(vlc_metadata_job_Apply(f.job, f.item, 0, NULL) == VLC_SUCCESS);
    assert(f.item->p_stats->i_read_bytes == 9999 && f.item->i_epg_time == 3000);
    assert(!strcmp(f.item->p_epg_table->p_current->psz_name, "Live event"));
    assert(f.item->i_duration == VLC_TICK_FROM_SEC(123)); field(f.item, vlc_meta_Description, "Remote description"); clean(&f);
}
static void closed_owner_and_cancelled_local_result(void)
{
    for (unsigned mode = 0; mode < 3; ++mode) {
        struct fixture f; init(&f); start(&f); local_facts(f.parsed);
        assert(vlc_metadata_job_PrepareLocal(f.job, f.parsed, NULL) == VLC_SUCCESS); complete(&f);
        if (mode == 0) vlc_metadata_backend_Close(f.backend);
        if (mode == 1) assert(vlc_metadata_backend_CancelAutomatic(f.backend) == 1);
        if (mode == 2) assert(!vlc_metadata_job_Cancel(f.job));
        assert(vlc_metadata_job_Apply(f.job, f.item, 0, NULL) == -EINTR);
        assert(f.item->i_duration == INPUT_DURATION_UNSET && f.item->es_vec.size == 0);
        field(f.item, vlc_meta_Description, NULL); clean(&f);
    }
}
static void exact_draft_and_execution_identity(void)
{
    for (unsigned mode = 0; mode < 3; ++mode) {
        struct fixture f; init(&f); start(&f);
        input_item_t *copy = input_item_Copy(f.parsed); assert(copy != NULL);
        assert(vlc_metadata_job_PrepareLocal(f.job, copy, NULL) == VLC_EINVAL); input_item_Release(copy);
        if (mode == 0) input_item_SetURI(f.parsed, "file:///different.flac");
        if (mode == 1) input_item_SetURI(f.parsed, "file:///local-parse.flac");
        if (mode == 2) assert(input_item_AddOption(f.parsed, "no-video", 0) == VLC_SUCCESS);
        assert(vlc_metadata_job_PrepareLocal(f.job, f.parsed, NULL) == -EAGAIN); clean(&f);
    }
}
static void missing_name_and_native_edit_guards(void)
{
    struct fixture f; init(&f);
    vlc_mutex_lock(&f.item->lock); free(f.item->psz_name); f.item->psz_name = NULL;
    vlc_item_parse_TouchLocked(f.item, VLC_ITEM_PARSE_NAME); vlc_mutex_unlock(&f.item->lock);
    start(&f); input_item_SetName(f.parsed, "Native name");
    assert(vlc_metadata_job_PrepareLocal(f.job, f.parsed, NULL) == VLC_SUCCESS); complete(&f);
    assert(vlc_metadata_job_Apply(f.job, f.item, 0, NULL) == VLC_SUCCESS && !strcmp(f.item->psz_name, "Native name")); clean(&f);
    for (unsigned mode = 0; mode < 3; ++mode) {
        init(&f); start(&f); local_facts(f.parsed);
        input_item_slave_t *new_link = input_item_slave_New("file:///local.srt", SLAVE_TYPE_SPU, SLAVE_PRIORITY_MATCH_ALL);
        assert(new_link != NULL && input_item_AddSlave(f.parsed, new_link) == VLC_SUCCESS);
        assert(vlc_metadata_job_PrepareLocal(f.job, f.parsed, NULL) == VLC_SUCCESS); complete(&f);
        if (mode == 0) add_track(f.item, "user/audio");
        if (mode == 1) {
            input_item_slave_t *user = input_item_slave_New("file:///user.srt", SLAVE_TYPE_SPU, SLAVE_PRIORITY_USER);
            assert(user != NULL); user->b_forced = true; assert(input_item_AddSlave(f.item, user) == VLC_SUCCESS);
        }
        if (mode == 2) {
            vlc_mutex_lock(&f.item->lock); vlc_item_parse_TouchLocked(f.item, VLC_ITEM_PARSE_TYPE); vlc_mutex_unlock(&f.item->lock);
        }
        assert(vlc_metadata_job_Apply(f.job, f.item, 0, NULL) == -EAGAIN);
        field(f.item, vlc_meta_Description, NULL); assert(f.item->i_duration == INPUT_DURATION_UNSET);
        if (mode == 1) assert(f.item->i_slaves == 1 && f.item->pp_slaves[0]->b_forced);
        clean(&f);
    }
}
static bool allow_url(void *opaque, const char *url) { (void)opaque; (void)url; return true; }
static bool allow_connection(void *opaque, const char *host, unsigned port, bool datagram)
{ (void)opaque; (void)host; (void)port; (void)datagram; return true; }
static void policy_identity_and_revocation(void)
{
    static const struct vlc_input_policy_operations ops = {allow_url, allow_connection, NULL};
    struct fixture f; init(&f); vlc_input_policy_t *policy = vlc_input_policy_New(&ops, NULL); assert(policy != NULL);
    assert(vlc_input_policy_AttachItem(f.item, policy) == VLC_SUCCESS); start(&f); local_facts(f.parsed);
    assert(vlc_input_policy_HoldItem(f.parsed) == policy); vlc_input_policy_Release(policy);
    assert(vlc_metadata_job_PrepareLocal(f.job, f.parsed, NULL) == VLC_SUCCESS); complete(&f);
    vlc_input_policy_Revoke(policy); assert(vlc_metadata_job_Apply(f.job, f.item, 0, NULL) == -EACCES);
    assert(f.item->i_duration == INPUT_DURATION_UNSET); field(f.item, vlc_meta_Description, NULL);
    clean(&f); vlc_input_policy_Release(policy);
}
static void unrelated_native_edits_keep_standard_only_path(void)
{
    struct fixture f; init(&f); start(&f); complete(&f);
    input_item_SetDuration(f.item, VLC_TICK_FROM_SEC(321));
    assert(input_item_AddInfo(f.item, "User", "Native", "%s", "Later value") == VLC_SUCCESS);
    assert(vlc_metadata_job_Apply(f.job, f.item, 0, NULL) == VLC_SUCCESS);
    assert(f.item->i_duration == VLC_TICK_FROM_SEC(321)); info(f.item, "User", "Native", "Later value");
    assert(vlc_meta_GetExtra(f.item->p_meta, "Native extra") == NULL);
    field(f.item, vlc_meta_Description, "Remote description"); clean(&f);
}
int main(void)
{
    full_native_parity_and_seal(); sideband_waits_for_admission(); stale_native_same_value_and_aba();
    live_progress_has_precedence(); closed_owner_and_cancelled_local_result(); exact_draft_and_execution_identity();
    missing_name_and_native_edit_guards(); policy_identity_and_revocation(); unrelated_native_edits_keep_standard_only_path();
    return 0;
}
