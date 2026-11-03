// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <vlc_common.h>
#include <vlc_input_item.h>
#include <vlc_metadata_revision.h>

static bool field_revisions(input_item_t *item, struct vlc_metadata_revisions *revisions)
{
    bool available = true;
    vlc_mutex_lock(&item->lock);
    for (int i = 0; i < VLC_META_TYPE_COUNT; ++i) {
        revisions->fields[i] = item->p_meta != NULL ? vlc_meta_FieldRevision(item->p_meta, i) : 0;
        available &= revisions->fields[i] != UINT64_MAX;
    }
    revisions->extra = vlc_meta_ExtraRevision(item->p_meta);
    available &= revisions->extra != UINT64_MAX;
    vlc_mutex_unlock(&item->lock);
    return available;
}
input_item_t *vlc_metadata_item_Snapshot(input_item_t *item, struct vlc_metadata_revisions *revisions)
{
    if (item == NULL || revisions == NULL) return NULL;
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        struct vlc_metadata_revisions before;
        if (!field_revisions(item, &before)) return NULL;
        input_item_t *snapshot = input_item_Copy(item);
        if (snapshot == NULL) return NULL;
        if (field_revisions(item, revisions) && before.extra == revisions->extra
            && memcmp(before.fields, revisions->fields, sizeof(before.fields)) == 0) return snapshot;
        input_item_Release(snapshot);
    }
    return NULL;
}
