// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_artwork_policy.h>
#include <vlc_input_item.h>
#include <vlc_input_policy.h>
#include <vlc_metadata_revision.h>
#include <assert.h>

static const char artwork[] = "file:///host-cache/approved.png";
static input_item_t *source(void)
{
    input_item_t *item = input_item_New("vlc-addon-source:v1:opaque", "Source"); assert(item != NULL); return item;
}
static void uri(input_item_t *item, const char *expected)
{
    char *value = vlc_artwork_item_GetURI(item);
    assert(expected == NULL ? value == NULL : value != NULL && !strcmp(value, expected)); free(value);
}
static void ordinary_native_behavior(void)
{
    input_item_t *item = input_item_New("file:///native-media.flac", "Native"); assert(item != NULL);
    assert(!vlc_artwork_item_IsRestricted(item)); uri(item, NULL);
    const char *values[] = {"http://192.168.1.20/cover.jpg", "https://example.test/cover.jpg", "file:///native-cover.png", "attachment://cover.png"};
    for (size_t i = 0; i < ARRAY_SIZE(values); ++i) { input_item_SetArtURL(item, values[i]); uri(item, values[i]); }
    assert(!vlc_artwork_item_IsRestricted(NULL)); assert(vlc_artwork_item_GetURI(NULL) == NULL);
    assert(vlc_artwork_item_SetApprovedURI(NULL, artwork) == VLC_EINVAL); input_item_Release(item);
}
static void raw_source_uris_and_imports(void)
{
    const char *sources[] = {"vlc-addon-source:v1:opaque", "VLC-ADDON-SOURCE:v1:opaque", "VlC-AdDoN-SoUrCe:future:opaque"};
    const char *values[] = {"http://127.0.0.1/private.png", "https://public.example.test/cover.png", "file:///home/private.png", "attachment://cover.png"};
    for (size_t i = 0; i < ARRAY_SIZE(sources); ++i) {
        input_item_t *item = input_item_New(sources[i], "Imported source"); assert(item != NULL);
        assert(vlc_input_policy_HoldItem(item) == NULL); assert(vlc_artwork_item_IsRestricted(item));
        for (size_t j = 0; j < ARRAY_SIZE(values); ++j) { input_item_SetArtURL(item, values[j]); uri(item, NULL); }
        input_item_t *copy = input_item_Copy(item); assert(copy != NULL);
        assert(vlc_artwork_item_IsRestricted(copy)); uri(copy, NULL); input_item_Release(copy); input_item_Release(item);
    }
}
static bool allow_url(void *opaque, const char *url)
{ (void)opaque; (void)url; return true; }
static bool allow_connection(void *opaque, const char *host, unsigned port, bool datagram)
{ (void)opaque; (void)host; (void)port; (void)datagram; return true; }
static void destroyed(void *opaque) { if (opaque != NULL) ++*(unsigned *)opaque; }
static vlc_input_policy_t *policy(unsigned *count)
{
    static const struct vlc_input_policy_operations operations = {allow_url, allow_connection, destroyed};
    vlc_input_policy_t *result = vlc_input_policy_New(&operations, count); assert(result != NULL); return result;
}
static void approvals_and_native_copies(void)
{
    input_item_t *item = source(); assert(vlc_artwork_item_SetApprovedURI(item, artwork) == VLC_SUCCESS); uri(item, artwork);
    input_item_t *copy = input_item_Copy(item); assert(copy != NULL); uri(copy, artwork);
    input_item_Release(item); uri(copy, artwork);
    struct vlc_metadata_revisions revisions; input_item_t *snapshot = vlc_metadata_item_Snapshot(copy, &revisions); assert(snapshot != NULL);
    uri(snapshot, artwork); input_item_Release(snapshot); input_item_Release(copy);

    unsigned count = 0; vlc_input_policy_t *scope = policy(&count); item = source();
    assert(vlc_input_policy_AttachItem(item, scope) == VLC_SUCCESS);
    assert(vlc_artwork_item_SetApprovedURI(item, artwork) == VLC_SUCCESS);
    copy = input_item_Copy(item); assert(copy != NULL); input_item_Release(item);
    uri(copy, artwork); vlc_input_policy_Release(scope); assert(count == 0);
    input_item_Release(copy); assert(count == 1);
}
static void same_value_aba_and_source_changes(void)
{
    input_item_t *item = source(); assert(vlc_artwork_item_SetApprovedURI(item, artwork) == VLC_SUCCESS);
    input_item_SetArtURL(item, artwork); uri(item, NULL);
    input_item_t *copy = input_item_Copy(item); assert(copy != NULL); uri(copy, NULL); input_item_Release(copy);
    assert(vlc_artwork_item_SetApprovedURI(item, artwork) == VLC_SUCCESS);
    input_item_SetArtURL(item, "file:///other.png"); input_item_SetArtURL(item, artwork); uri(item, NULL);
    assert(vlc_artwork_item_SetApprovedURI(item, artwork) == VLC_SUCCESS);
    input_item_SetURI(item, "vlc-addon-source:v1:different"); uri(item, NULL);
    copy = input_item_Copy(item); assert(copy != NULL); uri(copy, NULL); input_item_Release(copy); input_item_Release(item);
}
static void policy_change_and_revocation(void)
{
    input_item_t *item = source(); assert(vlc_artwork_item_SetApprovedURI(item, artwork) == VLC_SUCCESS);
    vlc_input_policy_t *scope = policy(NULL); assert(vlc_input_policy_AttachItem(item, scope) == VLC_SUCCESS); uri(item, NULL);
    assert(vlc_artwork_item_SetApprovedURI(item, artwork) == VLC_SUCCESS); uri(item, artwork);
    vlc_input_policy_Revoke(scope); assert(vlc_artwork_item_IsRestricted(item)); uri(item, NULL);
    assert(vlc_artwork_item_SetApprovedURI(item, artwork) == -EACCES);
    input_item_t *copy = input_item_Copy(item); assert(copy != NULL);
    assert(vlc_artwork_item_IsRestricted(copy)); uri(copy, NULL); input_item_Release(copy);
    vlc_input_policy_Release(scope); input_item_Release(item);

    item = input_item_New("file:///scoped-native.flac", "Policy"); assert(item != NULL);
    scope = policy(NULL); assert(vlc_input_policy_AttachItem(item, scope) == VLC_SUCCESS);
    input_item_SetArtURL(item, "file:///private.png"); assert(vlc_artwork_item_IsRestricted(item)); uri(item, NULL);
    assert(vlc_artwork_item_SetApprovedURI(item, artwork) == VLC_SUCCESS);
    vlc_input_policy_t *other = policy(NULL); assert(vlc_input_policy_AttachItem(item, other) != VLC_SUCCESS); uri(item, NULL);
    vlc_input_policy_Release(scope); vlc_input_policy_Release(other); input_item_Release(item);
}
static void metadata_and_options_cannot_transfer_approval(void)
{
    input_item_t *approved = source(); assert(vlc_artwork_item_SetApprovedURI(approved, artwork) == VLC_SUCCESS);
    input_item_t *imported = source(); input_item_SetArtURL(imported, artwork);
    input_item_SetMetaExtra(imported, "artwork-approved", "true"); input_item_SetMetaExtra(imported, "artwork-policy", "approved");
    assert(input_item_AddOption(imported, "artwork-approved=true", VLC_INPUT_OPTION_TRUSTED) == VLC_SUCCESS);
    uri(imported, NULL); input_item_CopyOptions(imported, approved); uri(imported, NULL);
    vlc_mutex_lock(&imported->lock); vlc_mutex_lock(&approved->lock);
    vlc_meta_Merge(imported->p_meta, approved->p_meta);
    vlc_mutex_unlock(&approved->lock); vlc_mutex_unlock(&imported->lock); uri(imported, NULL);
    vlc_mutex_lock(&approved->lock); vlc_mutex_lock(&imported->lock);
    vlc_meta_Merge(approved->p_meta, imported->p_meta);
    vlc_mutex_unlock(&imported->lock); vlc_mutex_unlock(&approved->lock); uri(approved, NULL);
    input_item_Release(imported); input_item_Release(approved);
}
static void local_file_uri_limits(void)
{
    input_item_t *item = source(); assert(vlc_artwork_item_SetApprovedURI(item, artwork) == VLC_SUCCESS);
    const char *incorrect[] = {"https://example.test/a.png", "http://127.0.0.1/a.png", "file://host/share/a.png", "file://localhost/a.png",
        "file:////host/share/a.png", "file:///%2fhost/share/a.png", "file:///%5chost/share/a.png", "file:///%00hidden.png", "file:///a%0a.png",
        "file:///a.png?request", "file:///a.png#fragment", "file:///a%g0.png", "file:///raw-\xff.png", "file:///raw-\xc0\xaf.png",
        "file:///", "/tmp/a.png", "", NULL};
    for (size_t i = 0; i < ARRAY_SIZE(incorrect); ++i) {
        assert(!vlc_artwork_uri_IsLocalFile(incorrect[i]));
        assert(vlc_artwork_item_SetApprovedURI(item, incorrect[i]) == VLC_EINVAL); uri(item, artwork);
    }
    char *large = malloc(32770); assert(large != NULL); memset(large, 'a', 32769); memcpy(large, "file:///", 8); large[32769] = '\0';
    assert(vlc_artwork_item_SetApprovedURI(item, large) == VLC_EINVAL); free(large); uri(item, artwork);
    assert(vlc_artwork_uri_IsLocalFile("FILE:///C:/cache/a%20b.png"));
    assert(vlc_artwork_item_SetApprovedURI(item, "FILE:///C:/cache/a%20b.png") == VLC_SUCCESS); uri(item, "FILE:///C:/cache/a%20b.png");
    assert(vlc_artwork_item_SetApprovedURI(item, "file:///host-cache/caf\xc3\xa9.png") == VLC_SUCCESS); uri(item, "file:///host-cache/caf\xc3\xa9.png");
    input_item_Release(item);
}
int main(void)
{
    ordinary_native_behavior(); raw_source_uris_and_imports(); approvals_and_native_copies();
    same_value_aba_and_source_changes(); policy_change_and_revocation(); metadata_and_options_cannot_transfer_approval();
    local_file_uri_limits(); return 0;
}
