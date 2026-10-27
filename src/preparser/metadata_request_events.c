// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <vlc_common.h>
#include <vlc_input.h>
#include "metadata_request_events.h"

#define MAX_ENTRIES 4096u
#define MAX_DEPTH 32u
#define MAX_BYTES (16u * 1024u * 1024u)
static bool count_tree(const input_item_node_t *tree, unsigned depth, size_t *nodes)
{
    if (tree == NULL || tree->p_item == NULL || depth > MAX_DEPTH || *nodes == MAX_ENTRIES
        || tree->i_children < 0 || (tree->i_children != 0 && tree->pp_children == NULL)) return false;
    ++*nodes;
    if ((size_t)tree->i_children > MAX_ENTRIES - *nodes) return false;
    for (int i = 0; i < tree->i_children; ++i)
        if (!count_tree(tree->pp_children[i], depth + 1, nodes)) return false;
    return true;
}
int vlc_metadata_request_events_AddTree(struct vlc_metadata_request_events *events, input_item_node_t *tree)
{
    size_t nodes = events->nodes;
    if (events->status != VLC_SUCCESS || !count_tree(tree, 1, &nodes)) {
        if (events->status == VLC_SUCCESS) events->status = VLC_ENOMEM;
        if (tree != NULL) input_item_node_Delete(tree);
        return events->status;
    }
    const size_t count = events->output.subtree_count;
    input_item_node_t **array = realloc(events->output.subtrees, (count + 1) * sizeof(*array));
    if (array == NULL) {
        input_item_node_Delete(tree); return events->status = VLC_ENOMEM;
    }
    events->output.subtrees = array; events->output.subtrees[count] = tree;
    events->output.subtree_count = count + 1; events->nodes = nodes; return VLC_SUCCESS;
}
static bool add_text(const char *value, size_t *bytes)
{
    if (value == NULL) return true;
    const size_t length = strnlen(value, MAX_BYTES - *bytes);
    if (length == MAX_BYTES - *bytes) return false;
    *bytes += length + 1; return true;
}
int vlc_metadata_request_events_AddAttachments(struct vlc_metadata_request_events *events,
    input_attachment_t *const *attachments, size_t count)
{
    if (events->status != VLC_SUCCESS) return events->status;
    const size_t previous = events->output.attachment_count;
    if (count > MAX_ENTRIES - previous || (count != 0 && attachments == NULL)) return events->status = VLC_ENOMEM;
    size_t bytes = events->attachment_bytes;
    for (size_t i = 0; i < count; ++i) {
        const input_attachment_t *attachment = attachments[i];
        if (attachment == NULL || attachment->i_data > MAX_BYTES - bytes
            || (attachment->i_data != 0 && attachment->p_data == NULL)) return events->status = VLC_ENOMEM;
        bytes += attachment->i_data;
        if (!add_text(attachment->psz_name, &bytes) || !add_text(attachment->psz_mime, &bytes)
            || !add_text(attachment->psz_description, &bytes)) return events->status = VLC_ENOMEM;
    }
    if (count == 0) return VLC_SUCCESS;
    input_attachment_t **array = realloc(events->output.attachments, (previous + count) * sizeof(*array));
    if (array == NULL) return events->status = VLC_ENOMEM;
    events->output.attachments = array;
    for (size_t i = 0; i < count; ++i) array[previous + i] = vlc_input_attachment_Hold(attachments[i]);
    events->output.attachment_count += count; events->attachment_bytes = bytes; return VLC_SUCCESS;
}
void vlc_metadata_request_events_Clear(struct vlc_metadata_request_events *events)
{
    vlc_metadata_local_events_Clear(&events->output); *events = (struct vlc_metadata_request_events){0};
}
