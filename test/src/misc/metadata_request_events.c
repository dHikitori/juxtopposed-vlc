// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_input.h>
#include <assert.h>
#include "../../../src/preparser/metadata_request_events.c"

static input_item_node_t *tree(input_item_t *item, unsigned depth)
{
    input_item_node_t *root = input_item_node_Create(item); assert(root != NULL);
    input_item_node_t *at = root;
    for (unsigned i = 1; i < depth; ++i) { at = input_item_node_AppendItem(at, item); assert(at != NULL); }
    return root;
}
static void native_tree_and_attachment_ownership(void)
{
    struct vlc_metadata_request_events events = {0};
    input_item_t *item = input_item_New("file:///private/track.flac", "Track"); assert(item != NULL);
    assert(vlc_metadata_request_events_AddTree(&events, tree(item, 3)) == VLC_SUCCESS);
    input_attachment_t *attachment = vlc_input_attachment_New("cover.png", "image/png", "Cover", "data", 4);
    assert(attachment != NULL);
    assert(vlc_metadata_request_events_AddAttachments(&events, &attachment, 1) == VLC_SUCCESS);
    vlc_input_attachment_Release(attachment);
    input_item_Release(item);
    assert(events.output.subtree_count == 1 && events.nodes == 3 && events.output.attachment_count == 1);
    assert(events.output.attachments[0]->i_data == 4 && !memcmp(events.output.attachments[0]->p_data, "data", 4));
    vlc_metadata_request_events_Clear(&events);
    assert(events.output.subtrees == NULL && events.output.attachments == NULL && events.status == VLC_SUCCESS);
}
static void tree_limits_apply_across_callbacks(void)
{
    input_item_t *item = input_item_New("file:///private/track.flac", "Track"); assert(item != NULL);
    struct vlc_metadata_request_events events = {0};
    assert(vlc_metadata_request_events_AddTree(&events, tree(item, MAX_DEPTH)) == VLC_SUCCESS);
    assert(vlc_metadata_request_events_AddTree(&events, tree(item, MAX_DEPTH + 1)) == VLC_ENOMEM);
    assert(events.output.subtree_count == 1 && events.nodes == MAX_DEPTH);
    assert(vlc_metadata_request_events_AddTree(&events, tree(item, 1)) == VLC_ENOMEM);
    vlc_metadata_request_events_Clear(&events);
    input_item_node_t *root = tree(item, 1);
    for (unsigned i = 1; i < MAX_ENTRIES; ++i) assert(input_item_node_AppendItem(root, item) != NULL);
    assert(vlc_metadata_request_events_AddTree(&events, root) == VLC_SUCCESS);
    assert(vlc_metadata_request_events_AddTree(&events, tree(item, 1)) == VLC_ENOMEM);
    assert(events.output.subtree_count == 1 && events.nodes == MAX_ENTRIES);
    vlc_metadata_request_events_Clear(&events); input_item_Release(item);
}
static void attachment_count_and_text_limits_are_atomic(void)
{
    struct vlc_metadata_request_events events = {0};
    input_attachment_t *attachment = vlc_input_attachment_New("", "", "", NULL, 0); assert(attachment != NULL);
    input_attachment_t **array = malloc(MAX_ENTRIES * sizeof(*array)); assert(array != NULL);
    for (unsigned i = 0; i < MAX_ENTRIES; ++i) array[i] = attachment;
    assert(vlc_metadata_request_events_AddAttachments(&events, array, MAX_ENTRIES) == VLC_SUCCESS);
    assert(vlc_metadata_request_events_AddAttachments(&events, array, 1) == VLC_ENOMEM);
    assert(events.output.attachment_count == MAX_ENTRIES);
    vlc_metadata_request_events_Clear(&events); free(array);
    events.attachment_bytes = MAX_BYTES - 3;
    assert(vlc_metadata_request_events_AddAttachments(&events, &attachment, 1) == VLC_SUCCESS);
    assert(events.attachment_bytes == MAX_BYTES);
    assert(vlc_metadata_request_events_AddAttachments(&events, &attachment, 1) == VLC_ENOMEM);
    assert(events.output.attachment_count == 1);
    vlc_metadata_request_events_Clear(&events); vlc_input_attachment_Release(attachment);
}
int main(void)
{
    native_tree_and_attachment_ownership();
    tree_limits_apply_across_callbacks();
    attachment_count_and_text_limits_are_atomic();
    return 0;
}
