// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_artwork_policy.h>
#include <vlc_input_item.h>
#include <vlc_input_policy.h>
#include <vlc_metadata_backend.h>
#include <assert.h>

struct fixture {
    input_item_t *target, *result;
    vlc_metadata_backend_t *backend;
    vlc_metadata_job_t *job;
    vlc_input_policy_t *policy;
    unsigned ended;
};
static bool allow_url(void *opaque, const char *url)
{ (void)opaque; (void)url; return true; }
static bool allow_connection(void *opaque, const char *host, unsigned port, bool datagram)
{ (void)opaque; (void)host; (void)port; (void)datagram; return true; }
static vlc_input_policy_t *new_policy(void)
{
    static const struct vlc_input_policy_operations operations = {allow_url, allow_connection, NULL};
    vlc_input_policy_t *policy = vlc_input_policy_New(&operations, NULL); assert(policy != NULL); return policy;
}
static int dispatch(void *opaque, vlc_metadata_job_t *job)
{ (void)opaque; (void)job; return VLC_SUCCESS; }
static void ended(vlc_metadata_job_t *job, int status, void *opaque)
{ (void)job; assert(status == VLC_SUCCESS); ++((struct fixture *)opaque)->ended; }
static void init_base(struct fixture *fixture, const char *uri, const char *existing, bool approve)
{
    memset(fixture, 0, sizeof(*fixture));
    const struct vlc_metadata_backend_operations operations = {.dispatch=dispatch};
    fixture->backend = vlc_metadata_backend_New(&operations, NULL); assert(fixture->backend != NULL);
    fixture->target = input_item_New(uri, "Source"); assert(fixture->target != NULL);
    input_item_SetTitle(fixture->target, "Original");
    if (!strncmp(uri, "hakone-addon-source:", sizeof("hakone-addon-source:") - 1)) {
        fixture->policy = new_policy();
        assert(vlc_input_policy_AttachItem(fixture->target, fixture->policy) == VLC_SUCCESS);
    }
    if (existing != NULL) {
        if (approve) assert(vlc_artwork_item_SetApprovedURI(fixture->target, existing) == VLC_SUCCESS);
        else input_item_SetArtURL(fixture->target, existing);
    }
    const struct vlc_metadata_request request = {.feature="artwork", .operation="enrich", .item=fixture->target,
        .flags=VLC_METADATA_REQUEST_NETWORK, .generation=1, .target_revision=1};
    fixture->job = vlc_metadata_job_New(fixture->backend, &request, ended, fixture); assert(fixture->job != NULL);
    fixture->result = input_item_Copy(vlc_metadata_job_GetRequest(fixture->job)->item); assert(fixture->result != NULL);
    assert(vlc_metadata_job_Start(fixture->job) == VLC_SUCCESS);
    input_item_SetDescription(fixture->result, "Provider description");
}
static void init(struct fixture *fixture, const char *uri, const char *existing)
{ init_base(fixture, uri, existing, true); }
static void complete(struct fixture *fixture)
{
    assert(vlc_metadata_job_Complete(fixture->job, VLC_SUCCESS, fixture->result, "{}", 2));
    assert(fixture->ended == 1);
}
static void art(input_item_t *item, const char *expected)
{
    char *actual = vlc_artwork_item_GetURI(item);
    assert(expected == NULL ? actual == NULL : actual != NULL && !strcmp(actual, expected)); free(actual);
}
static void no_description(struct fixture *fixture)
{ char *value = input_item_GetDescription(fixture->target); assert(value == NULL); free(value); }
static void clean(struct fixture *fixture)
{
    input_item_Release(fixture->result); vlc_metadata_job_Release(fixture->job);
    vlc_metadata_backend_Close(fixture->backend); vlc_metadata_backend_Release(fixture->backend);
    input_item_Release(fixture->target); vlc_input_policy_Release(fixture->policy);
}
static void approved_proof_reaches_the_original_target(void)
{
    struct fixture fixture; init(&fixture, "hakone-addon-source:v1:item", NULL);
    assert(vlc_artwork_item_SetApprovedURI(fixture.result, "file:///broker/image.png") == VLC_SUCCESS); complete(&fixture);
    size_t applied = 0; assert(vlc_metadata_job_Apply(fixture.job, fixture.target, 0, &applied) == VLC_SUCCESS && applied == 2);
    art(fixture.target, "file:///broker/image.png");
    input_item_t *copy = input_item_Copy(fixture.target); assert(copy != NULL); art(copy, "file:///broker/image.png"); input_item_Release(copy);
    input_item_SetArtURL(fixture.target, "file:///broker/image.png"); art(fixture.target, NULL); clean(&fixture);

}
static void unapproved_source_artwork_is_a_missing_display_asset(void)
{
    for (unsigned mode = 0; mode < 3; ++mode) {
        struct fixture fixture;
        init_base(&fixture, mode == 2 ? "file:///native.flac" : "hakone-addon-source:v1:item",
            mode == 1 ? "file:///unapproved.png" : "http://127.0.0.1/unapproved.png", false);
        assert(vlc_artwork_item_SetApprovedURI(fixture.result, "file:///broker/image.png") == VLC_SUCCESS); complete(&fixture);
        size_t applied = 0;
        assert(vlc_metadata_job_Apply(fixture.job, fixture.target, 0, &applied) == VLC_SUCCESS && applied == (mode == 2 ? 1 : 2));
        art(fixture.target, mode == 2 ? "http://127.0.0.1/unapproved.png" : "file:///broker/image.png"); clean(&fixture);
    }
}
static void text_and_stale_result_records_cannot_grant_proof(void)
{
    for (unsigned native = 0; native < 2; ++native) for (unsigned stale = 0; stale < 2; ++stale) {
        struct fixture fixture; init(&fixture, native ? "file:///native.flac" : "hakone-addon-source:v1:item", NULL);
        if (stale) assert(vlc_artwork_item_SetApprovedURI(fixture.result, "file:///guest/image.png") == VLC_SUCCESS);
        input_item_SetArtURL(fixture.result, "file:///guest/image.png"); complete(&fixture);
        size_t applied = 99; assert(vlc_metadata_job_Apply(fixture.job, fixture.target, 0, &applied) == -EACCES && applied == 0);
        art(fixture.target, NULL); no_description(&fixture); clean(&fixture);
    }
}
static void foreign_source_and_policy_cannot_supply_proof(void)
{
    struct fixture fixture; init(&fixture, "hakone-addon-source:v1:item", NULL);
    input_item_SetURI(fixture.result, "hakone-addon-source:v1:other");
    assert(vlc_artwork_item_SetApprovedURI(fixture.result, "file:///broker/image.png") == VLC_SUCCESS); complete(&fixture);
    size_t applied = 99; assert(vlc_metadata_job_Apply(fixture.job, fixture.target, 0, &applied) == -EACCES && applied == 0);
    art(fixture.target, NULL); no_description(&fixture); clean(&fixture);

    init(&fixture, "hakone-addon-source:v1:item", NULL); input_item_Release(fixture.result);
    fixture.result = input_item_New("hakone-addon-source:v1:item", "Other result"); assert(fixture.result != NULL);
    vlc_input_policy_t *other = new_policy(); assert(vlc_input_policy_AttachItem(fixture.result, other) == VLC_SUCCESS);
    assert(vlc_artwork_item_SetApprovedURI(fixture.result, "file:///broker/image.png") == VLC_SUCCESS);
    input_item_SetDescription(fixture.result, "Provider description"); complete(&fixture);
    assert(vlc_metadata_job_Apply(fixture.job, fixture.target, 0, &applied) == -EACCES && applied == 0);
    art(fixture.target, NULL); no_description(&fixture); clean(&fixture); vlc_input_policy_Release(other);
}
static void existing_artwork_stays_until_native_replacement(void)
{
    struct fixture fixture; init(&fixture, "hakone-addon-source:v1:item", "file:///user/image.png");
    assert(vlc_artwork_item_SetApprovedURI(fixture.result, "file:///broker/image.png") == VLC_SUCCESS); complete(&fixture);
    size_t applied = 0; assert(vlc_metadata_job_Apply(fixture.job, fixture.target, 0, &applied) == VLC_SUCCESS && applied == 1);
    art(fixture.target, "file:///user/image.png"); clean(&fixture);

    init(&fixture, "hakone-addon-source:v1:item", "file:///user/image.png");
    assert(vlc_artwork_item_SetApprovedURI(fixture.result, "file:///broker/image.png") == VLC_SUCCESS); complete(&fixture);
    assert(vlc_metadata_job_Apply(fixture.job, fixture.target, UINT32_C(1) << vlc_meta_ArtworkURL, &applied) == VLC_SUCCESS && applied == 2);
    art(fixture.target, "file:///broker/image.png"); clean(&fixture);
}
static void stale_target_and_revoked_jobs_leave_every_field_unchanged(void)
{
    for (unsigned mode = 0; mode < 4; ++mode) {
        struct fixture fixture; init(&fixture, "hakone-addon-source:v1:item", NULL);
        assert(vlc_artwork_item_SetApprovedURI(fixture.result, "file:///broker/image.png") == VLC_SUCCESS); complete(&fixture);
        if (mode == 0) { input_item_SetDescription(fixture.target, "User edit"); input_item_SetDescription(fixture.target, NULL); }
        else if (mode == 1) vlc_input_policy_Revoke(fixture.policy);
        else if (mode == 2) assert(!vlc_metadata_job_Cancel(fixture.job));
        else vlc_metadata_backend_Close(fixture.backend);
        size_t applied = 99; const int status = vlc_metadata_job_Apply(fixture.job, fixture.target, 0, &applied);
        assert(status != VLC_SUCCESS && applied == 0); art(fixture.target, NULL); no_description(&fixture); clean(&fixture);
    }
}
static void native_clear_removes_the_old_approval(void)
{
    struct fixture fixture; init(&fixture, "hakone-addon-source:v1:item", "file:///user/image.png");
    input_item_SetArtURL(fixture.result, NULL); complete(&fixture);
    size_t applied = 0;
    assert(vlc_metadata_job_Apply(fixture.job, fixture.target, UINT32_C(1) << vlc_meta_ArtworkURL, &applied) == VLC_SUCCESS && applied == 2);
    art(fixture.target, NULL); clean(&fixture);
}
int main(void)
{
    approved_proof_reaches_the_original_target(); text_and_stale_result_records_cannot_grant_proof();
    unapproved_source_artwork_is_a_missing_display_asset();
    foreign_source_and_policy_cannot_supply_proof(); existing_artwork_stays_until_native_replacement();
    stale_target_and_revoked_jobs_leave_every_field_unchanged(); native_clear_removes_the_old_approval(); return 0;
}
