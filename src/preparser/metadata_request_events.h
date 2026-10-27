// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_METADATA_REQUEST_EVENTS_INTERNAL_H
#define VLC_METADATA_REQUEST_EVENTS_INTERNAL_H 1
#include "metadata_local.h"

/* The native parser owns one collector until its worker has stopped. No public
 * callback can observe these events before the final native admission. */
struct vlc_metadata_request_events {
    struct vlc_metadata_local_events output;
    size_t nodes, attachment_bytes;
    int status;
};
/* Take ownership of the subtree, including on failure. The parser must supply
 * a valid acyclic native tree. Retain at most 4096 nodes and 32 tree levels. */
int vlc_metadata_request_events_AddTree(struct vlc_metadata_request_events *, input_item_node_t *);
/* Hold each immutable attachment. The caller retains its original references.
 * Retain at most 4096 attachments and 16 MiB of attachment data and text. */
int vlc_metadata_request_events_AddAttachments(struct vlc_metadata_request_events *,
    input_attachment_t *const *, size_t);
void vlc_metadata_request_events_Clear(struct vlc_metadata_request_events *);
#endif
