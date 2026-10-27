// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_INPUT_ARTWORK_POLICY_H
#define VLC_INPUT_ARTWORK_POLICY_H 1
#include <vlc_common.h>
#include <vlc_input_policy.h>

struct vlc_artwork_approval {
    char *uri, *source_uri;
    vlc_input_policy_t *policy;
};
/* Only a current native record is a display asset on a restricted item. */
bool vlc_artwork_item_HasMissingDisplay(input_item_t *);
/* Allocate proof from a current native result. Text metadata is not proof. */
int vlc_artwork_item_PrepareApproval(input_item_t *result, const char *source_uri,
    vlc_input_policy_t *, const char *artwork_uri, struct vlc_artwork_approval *);
/* The item lock is held. No allocation or callback occurs. Swap takes the
 * prepared proof and returns the old proof for cleanup after unlock.
 */
void vlc_artwork_item_SwapApprovalLocked(input_item_t *, struct vlc_artwork_approval *, uint64_t revision);
void vlc_artwork_approval_Clear(struct vlc_artwork_approval *);

/* The source lock is held. The destination is a new, unpublished item. */
void vlc_artwork_item_CopyApprovalLocked(input_item_t *destination, input_item_t *source);
/* Call only after the final item reference ends. */
void vlc_artwork_item_ClearApproval(input_item_t *);
#endif
