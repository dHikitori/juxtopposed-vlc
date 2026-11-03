// SPDX-License-Identifier: LGPL-2.1-or-later
/* Reuse the full native snapshot fixture and real private item setters. */
int metadata_local_fixture_main(void);
#define main metadata_local_fixture_main
#include "metadata_local.c"
#undef main

static void prepare_current(struct fixture *f, bool changes)
{
    init(f); start(f);
    if (changes) {
        input_item_SetDescription(f->parsed, "Native description");
        input_item_SetDuration(f->parsed, VLC_TICK_FROM_SEC(42));
    }
    assert(vlc_metadata_job_IsCurrentFor(f->job, f->item));
    assert(vlc_metadata_job_PrepareLocal(f->job, f->parsed, NULL) == VLC_SUCCESS);
    assert(vlc_metadata_job_IsCurrentFor(f->job, f->item));
    assert(vlc_metadata_job_Complete(f->job, VLC_SUCCESS, f->parsed, NULL, 0));
    assert(vlc_metadata_job_IsCurrentFor(f->job, f->item));
}
static void pending_complete_and_no_change_query_do_not_consume_the_anchor(void)
{
    for (unsigned changes = 0; changes < 2; ++changes) {
        struct fixture f; prepare_current(&f, changes);
        input_item_t *other = input_item_Copy(f.item); assert(other != NULL);
        assert(!vlc_metadata_job_IsCurrentFor(f.job, other)); input_item_Release(other);
        for (unsigned repeat = 0; repeat < 3; ++repeat) assert(vlc_metadata_job_IsCurrentFor(f.job, f.item));
        size_t applied = 99; assert(vlc_metadata_job_Apply(f.job, f.item, 0, &applied) == VLC_SUCCESS);
        assert(applied == changes && !vlc_metadata_job_IsCurrentFor(f.job, f.item));
        clean(&f);
    }
}
static void uri_name_options_and_metadata_aba_refuse_before_write(void)
{
    for (unsigned change = 0; change < 8; ++change) {
        struct fixture f; prepare_current(&f, true);
        switch (change) {
            case 0: input_item_SetURI(f.item, "file:///local-parse.flac"); break;
            case 1:
                input_item_SetURI(f.item, "file:///different.flac"); input_item_SetURI(f.item, "file:///local-parse.flac"); break;
            case 2: input_item_SetName(f.item, "User name"); break;
            case 3: assert(input_item_AddOption(f.item, "no-audio", VLC_INPUT_OPTION_TRUSTED) == VLC_SUCCESS); break;
            case 4: input_item_SetTitle(f.item, "Original title"); break;
            case 5: input_item_SetDescription(f.item, NULL); break;
            case 6: input_item_SetMetaExtra(f.item, "User", "Value"); break;
            case 7: input_item_SetDuration(f.item, VLC_TICK_INVALID); break;
        }
        assert(!vlc_metadata_job_IsCurrentFor(f.job, f.item));
        assert(vlc_metadata_job_Apply(f.job, f.item, 0, NULL) == -EAGAIN);
        field(f.item, vlc_meta_Description, NULL); assert(!vlc_metadata_job_IsCurrentFor(f.job, f.item)); clean(&f);
    }
}
static void cancellation_close_and_automatic_withdrawal_refuse_completed_anchor(void)
{
    for (unsigned state = 0; state < 3; ++state) {
        struct fixture f; prepare_current(&f, true);
        if (state == 0) assert(!vlc_metadata_job_Cancel(f.job));
        if (state == 1) vlc_metadata_backend_Close(f.backend);
        if (state == 2) assert(vlc_metadata_backend_CancelAutomatic(f.backend) == 1);
        assert(!vlc_metadata_job_IsCurrentFor(f.job, f.item));
        assert(vlc_metadata_job_Apply(f.job, f.item, 0, NULL) == -EINTR);
        field(f.item, vlc_meta_Description, NULL); clean(&f);
    }
}
static void pending_suspend_and_live_progress_have_explicit_rules(void)
{
    struct fixture f; init(&f);
    const struct vlc_metadata_request request = {.feature="artwork", .operation="selection", .item=f.item};
    f.job = vlc_metadata_job_New(f.backend, &request, ended, &f); assert(f.job != NULL);
    assert(!vlc_metadata_job_IsCurrentFor(f.job, f.item));
    assert(vlc_metadata_backend_Suspend(f.backend)); assert(vlc_metadata_job_StartLocal(f.job) == -EAGAIN);
    assert(!vlc_metadata_job_IsCurrentFor(f.job, f.item)); vlc_metadata_backend_Resume(f.backend);
    assert(vlc_metadata_job_StartLocal(f.job) == VLC_SUCCESS && vlc_metadata_job_IsCurrentFor(f.job, f.item)); clean(&f);
    prepare_current(&f, true);
    vlc_mutex_lock(&f.item->lock);
    f.item->p_stats = calloc(1, sizeof(*f.item->p_stats)); assert(f.item->p_stats != NULL); f.item->p_stats->i_read_bytes = 1234;
    vlc_mutex_unlock(&f.item->lock); input_item_SetEpgTime(f.item, 1000);
    input_item_SetGenre(f.item, "Unrelated live metadata");
    assert(vlc_metadata_job_IsCurrentFor(f.job, f.item)); assert(vlc_metadata_job_Apply(f.job, f.item, 0, NULL) == VLC_SUCCESS); clean(&f);
}
static void policy_revocation_and_replacement_refuse_before_write(void)
{
    static const struct vlc_input_policy_operations ops = {allow_url, allow_connection, NULL};
    for (unsigned revoke = 0; revoke < 2; ++revoke) {
        struct fixture f; init(&f);
        vlc_input_policy_t *policy = vlc_input_policy_New(&ops, NULL); assert(policy != NULL);
        assert(vlc_input_policy_AttachItem(f.item, policy) == VLC_SUCCESS); start(&f);
        assert(vlc_metadata_job_PrepareLocal(f.job, f.parsed, NULL) == VLC_SUCCESS);
        assert(vlc_metadata_job_Complete(f.job, VLC_SUCCESS, f.parsed, NULL, 0));
        assert(vlc_metadata_job_IsCurrentFor(f.job, f.item));
        if (revoke) vlc_input_policy_Revoke(policy);
        else {
            vlc_input_policy_t *replacement = vlc_input_policy_New(&ops, NULL); assert(replacement != NULL);
            vlc_mutex_lock(&f.item->lock); vlc_input_policy_Release(item_owner(f.item)->input_policy);
            item_owner(f.item)->input_policy = replacement; vlc_mutex_unlock(&f.item->lock);
        }
        assert(!vlc_metadata_job_IsCurrentFor(f.job, f.item)); clean(&f); vlc_input_policy_Release(policy);
    }
}
int main(void)
{
    pending_complete_and_no_change_query_do_not_consume_the_anchor();
    uri_name_options_and_metadata_aba_refuse_before_write();
    cancellation_close_and_automatic_withdrawal_refuse_completed_anchor();
    pending_suspend_and_live_progress_have_explicit_rules();
    policy_revocation_and_replacement_refuse_before_write();
    return 0;
}
