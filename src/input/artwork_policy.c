// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <vlc_common.h>
#include <vlc_artwork_policy.h>
#include <vlc_charset.h>
#include <vlc_input_item.h>
#include <vlc_input_policy.h>
#include <vlc_metadata_revision.h>
#include <vlc_url.h>
#include "artwork_policy.h"
#include "item.h"
#include "metadata_revision.h"

#define MAX_ARTWORK_URI 32768u
static bool same_string(const char *a, const char *b)
{ return a == NULL ? b == NULL : b != NULL && !strcmp(a, b); }
static bool restricted_locked(input_item_t *item)
{
    return item_owner(item)->input_policy != NULL
        || (item->psz_uri != NULL && !strncasecmp(item->psz_uri, "vlc-addon-source:", 17));
}
static bool current_locked(input_item_t *item)
{
    const input_item_owner_t *owner = item_owner(item);
    if (owner->artwork_uri == NULL || item->p_meta == NULL || owner->artwork_revision == UINT64_MAX
        || owner->artwork_policy != owner->input_policy || vlc_input_policy_IsRevoked(owner->artwork_policy)
        || !same_string(owner->artwork_source_uri, item->psz_uri)
        || owner->artwork_revision != vlc_meta_FieldRevision(item->p_meta, vlc_meta_ArtworkURL)) return false;
    const char *uri = vlc_meta_Get(item->p_meta, vlc_meta_ArtworkURL);
    return uri != NULL && !strcmp(uri, owner->artwork_uri);
}
static bool local_uri(const char *uri)
{
    if (uri == NULL || strnlen(uri, MAX_ARTWORK_URI + 1) > MAX_ARTWORK_URI || !IsUTF8(uri)
        || strncasecmp(uri, "file:///", 8) || uri[8] == '\0' || strpbrk(uri, "?#") != NULL) return false;
    for (const char *part = uri; *part != '\0'; ++part)
        if (*part == '%' && part[1] == '0' && part[2] == '0') return false;
    char *path = vlc_uri_decode_duplicate(uri + 7);
    if (path == NULL) return false;
    bool local = path[0] == '/' && path[1] != '/' && path[1] != '\\';
    for (const unsigned char *part = (const unsigned char *)path; local && *part != '\0'; ++part)
        if (*part < 32 || *part == 127) local = false;
    free(path); return local;
}
bool vlc_artwork_item_IsRestricted(input_item_t *item)
{
    if (item == NULL) return false;
    vlc_mutex_lock(&item->lock); const bool result = restricted_locked(item); vlc_mutex_unlock(&item->lock);
    return result;
}
char *vlc_artwork_item_GetURI(input_item_t *item)
{
    if (item == NULL) return NULL;
    vlc_mutex_lock(&item->lock);
    const char *uri = item->p_meta != NULL ? vlc_meta_Get(item->p_meta, vlc_meta_ArtworkURL) : NULL;
    char *result = uri != NULL && (!restricted_locked(item) || current_locked(item)) ? strdup(uri) : NULL;
    vlc_mutex_unlock(&item->lock); return result;
}
int vlc_artwork_item_SetApprovedURI(input_item_t *item, const char *uri)
{
    if (item == NULL || !local_uri(uri)) return VLC_EINVAL;
    char *record = strdup(uri), *value = strdup(uri);
    if (record == NULL || value == NULL) { free(record); free(value); return VLC_ENOMEM; }
    int status = VLC_SUCCESS; input_item_owner_t *owner = item_owner(item);
    char *old_uri = NULL, *old_source = NULL, *source = NULL; vlc_input_policy_t *old_policy = NULL;
    vlc_mutex_lock(&item->lock);
    if (vlc_input_policy_IsRevoked(owner->input_policy)) status = -EACCES;
    else if (item->p_meta != NULL && vlc_meta_FieldRevision(item->p_meta, vlc_meta_ArtworkURL) == UINT64_MAX) status = -EAGAIN;
    else if (item->psz_uri != NULL && strnlen(item->psz_uri, MAX_ARTWORK_URI + 1) > MAX_ARTWORK_URI) status = VLC_EINVAL;
    else if (item->psz_uri != NULL && (source = strdup(item->psz_uri)) == NULL) status = VLC_ENOMEM;
    else {
        if (item->p_meta == NULL) item->p_meta = vlc_meta_New();
        if (item->p_meta == NULL) status = VLC_ENOMEM;
        else if (!vlc_meta_TrySetOwned(item->p_meta, vlc_meta_ArtworkURL, value)) status = -EAGAIN;
        else {
            value = NULL;
            const uint64_t revision = vlc_meta_FieldRevision(item->p_meta, vlc_meta_ArtworkURL);
            old_uri = owner->artwork_uri; old_source = owner->artwork_source_uri; old_policy = owner->artwork_policy;
            owner->artwork_uri = record; record = NULL; owner->artwork_revision = revision;
            owner->artwork_source_uri = source; source = NULL;
            owner->artwork_policy = vlc_input_policy_Hold(owner->input_policy);
        }
    }
    vlc_mutex_unlock(&item->lock);
    free(old_uri); free(old_source); vlc_input_policy_Release(old_policy); free(record); free(source); free(value); return status;
}
void vlc_artwork_item_CopyApprovalLocked(input_item_t *destination, input_item_t *source)
{
    if (!current_locked(source)) return;
    input_item_owner_t *owner = item_owner(destination);
    const uint64_t revision = vlc_meta_FieldRevision(destination->p_meta, vlc_meta_ArtworkURL);
    if (revision == UINT64_MAX) return;
    char *uri = strdup(item_owner(source)->artwork_uri);
    if (uri == NULL) return;
    const char *original = item_owner(source)->artwork_source_uri;
    char *source_uri = original != NULL ? strdup(original) : NULL;
    if (original != NULL && source_uri == NULL) { free(uri); return; }
    owner->artwork_uri = uri; owner->artwork_revision = revision;
    owner->artwork_source_uri = source_uri;
    owner->artwork_policy = vlc_input_policy_Hold(item_owner(source)->artwork_policy);
}
void vlc_artwork_item_ClearApproval(input_item_t *item)
{
    input_item_owner_t *owner = item_owner(item);
    free(owner->artwork_uri); free(owner->artwork_source_uri); vlc_input_policy_Release(owner->artwork_policy);
    owner->artwork_uri = NULL; owner->artwork_source_uri = NULL; owner->artwork_policy = NULL; owner->artwork_revision = 0;
}
