// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_input_item.h>
#include <vlc_metadata_revision.h>
#include <assert.h>

static void same_value_and_aba(void)
{
    vlc_meta_t *meta = vlc_meta_New(); assert(meta != NULL);
    const uint64_t empty = vlc_meta_FieldRevision(meta, vlc_meta_Title);
    vlc_meta_Set(meta, vlc_meta_Title, "A"); const uint64_t first = vlc_meta_FieldRevision(meta, vlc_meta_Title);
    vlc_meta_Set(meta, vlc_meta_Title, "A"); const uint64_t same = vlc_meta_FieldRevision(meta, vlc_meta_Title);
    assert(empty != first && first != same);
    vlc_meta_Set(meta, vlc_meta_Title, "B"); const uint64_t changed = vlc_meta_FieldRevision(meta, vlc_meta_Title);
    vlc_meta_Set(meta, vlc_meta_Title, "A"); const uint64_t restored = vlc_meta_FieldRevision(meta, vlc_meta_Title);
    assert(changed != same && restored != changed && restored != first);
    assert(!strcmp(vlc_meta_Get(meta, vlc_meta_Title), "A"));
    vlc_meta_Set(meta, vlc_meta_Title, NULL); assert(vlc_meta_FieldRevision(meta, vlc_meta_Title) != restored);
    vlc_meta_Delete(meta);
}
static void independent_fields_and_merge(void)
{
    vlc_meta_t *target = vlc_meta_New(), *source = vlc_meta_New(); assert(target != NULL && source != NULL);
    const uint64_t title = vlc_meta_FieldRevision(target, vlc_meta_Title);
    const uint64_t artist = vlc_meta_FieldRevision(target, vlc_meta_Artist);
    assert(title != vlc_meta_FieldRevision(source, vlc_meta_Title));
    vlc_meta_Set(source, vlc_meta_Title, "Title"); vlc_meta_Merge(target, source);
    assert(vlc_meta_FieldRevision(target, vlc_meta_Title) != title);
    assert(vlc_meta_FieldRevision(target, vlc_meta_Artist) == artist);
    const uint64_t merged = vlc_meta_FieldRevision(target, vlc_meta_Title);
    vlc_meta_Merge(target, source); assert(vlc_meta_FieldRevision(target, vlc_meta_Title) != merged);
    vlc_meta_SetWithPriority(target, vlc_meta_Title, "User title", VLC_META_PRIORITY_INBAND);
    const uint64_t protected = vlc_meta_FieldRevision(target, vlc_meta_Title);
    vlc_meta_Merge(target, source); assert(vlc_meta_FieldRevision(target, vlc_meta_Title) == protected);
    assert(!strcmp(vlc_meta_Get(target, vlc_meta_Title), "User title"));
    vlc_meta_Delete(source); vlc_meta_Delete(target);
}
static void native_snapshot_keeps_source_tokens(void)
{
    input_item_t *item = input_item_New("file:///metadata-revision-test.flac", "Test"); assert(item != NULL);
    input_item_SetTitle(item, "A"); input_item_SetArtist(item, "Artist");
    input_item_SetMetaExtra(item, "MB_ALBUMID", "76df3287-6cda-33eb-8e9a-044b5e15ffdd");
    struct vlc_metadata_revisions baseline; input_item_t *copy = vlc_metadata_item_Snapshot(item, &baseline); assert(copy != NULL);
    vlc_mutex_lock(&item->lock);
    assert(baseline.fields[vlc_meta_Title] == vlc_meta_FieldRevision(item->p_meta, vlc_meta_Title));
    assert(baseline.fields[vlc_meta_Artist] == vlc_meta_FieldRevision(item->p_meta, vlc_meta_Artist));
    assert(baseline.extra == vlc_meta_ExtraRevision(item->p_meta));
    vlc_mutex_unlock(&item->lock);
    char *title = input_item_GetTitle(copy); assert(title != NULL && !strcmp(title, "A")); free(title);
    input_item_SetTitle(item, "B"); input_item_SetTitle(item, "A");
    vlc_mutex_lock(&item->lock); assert(baseline.fields[vlc_meta_Title] != vlc_meta_FieldRevision(item->p_meta, vlc_meta_Title)); vlc_mutex_unlock(&item->lock);
    input_item_SetMetaExtra(item, "MB_ALBUMID", "76df3287-6cda-33eb-8e9a-044b5e15ffdd");
    vlc_mutex_lock(&item->lock); assert(baseline.extra != vlc_meta_ExtraRevision(item->p_meta)); vlc_mutex_unlock(&item->lock);
    input_item_Release(copy); input_item_Release(item);
}
int main(void)
{ same_value_and_aba(); independent_fields_and_merge(); native_snapshot_keeps_source_tokens(); return 0; }
