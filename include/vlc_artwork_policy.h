// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_ARTWORK_POLICY_H
#define VLC_ARTWORK_POLICY_H 1
#include <vlc_common.h>
#ifdef __cplusplus
extern "C" {
#endif

/** A private item policy or a vlc-addon-source: URI restricts artwork.
 * A revoked policy does not remove this restriction.
 */
VLC_API bool vlc_artwork_item_IsRestricted(input_item_t *);

/** Return an allocated artwork URI. Ordinary native items retain their raw
 * artwork URI. A restricted item returns only a current native approval.
 * The caller frees the result. No image or network operation occurs here.
 */
VLC_API char *vlc_artwork_item_GetURI(input_item_t *) VLC_MALLOC;

/** Write and approve one local-file URI. The trusted native caller must first
 * validate the file and its image. This function does not decode an image.
 * It refuses a remote authority, a revoked policy, or an exhausted token.
 * Approval binds the exact artwork URI, source URI, new field token, and
 * held item policy.
 * Package data and metadata text cannot call this native API.
 */
VLC_API int vlc_artwork_item_SetApprovedURI(input_item_t *, const char *uri);

#ifdef __cplusplus
}
#endif
#endif
