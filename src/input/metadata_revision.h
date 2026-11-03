// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_METADATA_REVISION_INTERNAL_H
#define VLC_METADATA_REVISION_INTERNAL_H 1
#include <stdint.h>

/* Reserve a complete token range. No token can equal UINT64_MAX. */
bool vlc_meta_ReserveRevisions(unsigned count, uint64_t *first);
/* Take a preallocated value and a reserved token. The item lock is held. */
void vlc_meta_SetOwnedRevision(vlc_meta_t *, vlc_meta_type_t, char *value, uint64_t revision);
/* On failure, retain the caller's value and leave the metadata unchanged. */
bool vlc_meta_TrySetOwned(vlc_meta_t *, vlc_meta_type_t, char *value);
/* Copy all values, priorities, status, and tokens. Refuse incomplete copies. */
vlc_meta_t *vlc_meta_CopyNative(const vlc_meta_t *, size_t *remaining_bytes);
uint64_t vlc_meta_StatusRevision(const vlc_meta_t *);
/* Swap prepared extra tags without allocation. The caller owns the old tags. */
void vlc_meta_SwapExtraOwned(vlc_meta_t *, vlc_meta_t *, uint64_t revision);
void vlc_meta_SetStatusOwned(vlc_meta_t *, int status, uint64_t revision);
#endif
