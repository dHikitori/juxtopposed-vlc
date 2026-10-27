// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_INPUT_ARTWORK_POLICY_H
#define VLC_INPUT_ARTWORK_POLICY_H 1
#include <vlc_common.h>

/* The source lock is held. The destination is a new, unpublished item. */
void vlc_artwork_item_CopyApprovalLocked(input_item_t *destination, input_item_t *source);
/* Call only after the final item reference ends. */
void vlc_artwork_item_ClearApproval(input_item_t *);
#endif
