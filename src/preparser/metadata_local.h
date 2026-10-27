// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_METADATA_LOCAL_INTERNAL_H
#define VLC_METADATA_LOCAL_INTERNAL_H 1
#include <vlc_common.h>
#include <vlc_metadata_backend.h>

/* This contract is native-only. The worker ABI cannot call it. */
struct vlc_metadata_local_events {
    input_item_node_t **subtrees;
    size_t subtree_count;
    input_attachment_t **attachments;
    size_t attachment_count;
};

/* Return one full private native snapshot for a pending local anchor. The
 * caller owns a reference. The job retains that exact item until release.
 * Refuse a changed original snapshot. Do not use ordinary input_item_Copy:
 * it omits info groups, EPG, and statistics. */
VLC_API input_item_t *vlc_metadata_job_CopyLocalItem(vlc_metadata_job_t *);

/* Seal the local facts before any remote stage. parsed must be the exact
 * CopyLocalItem result. Call once, while the anchor is pending. On success,
 * copy subtree nodes and hold attachment references. The caller retains all
 * supplied objects and must keep them immutable during this call. Limits:
 * 16 MiB of copied native data, 4096 entries per collection, 32 tree levels,
 * and 16 MiB of retained attachment data. A failure leaves no attached delta.
 * URI, options, policy, and opaque owner data never enter the commit delta.
 * An exact sealed artwork claim can keep the ordinary native artwork route.
 * This exception grants no approval record and never applies to scoped media.
 * A provider claim still requires its own current native approval.
 */
VLC_API int vlc_metadata_job_PrepareLocal(vlc_metadata_job_t *, input_item_t *parsed,
                                const struct vlc_metadata_local_events *);

/* Transfer sideband output once, after successful Apply. A failed or cancelled
 * commit has no output. The caller can now deliver callbacks outside native
 * locks. Clear releases every tree and attachment after those callbacks.
 * Cancel after admission does not undo that admitted output. */
VLC_API bool vlc_metadata_job_TakeLocalEvents(vlc_metadata_job_t *, struct vlc_metadata_local_events *);
VLC_API void vlc_metadata_local_events_Clear(struct vlc_metadata_local_events *);
#endif
