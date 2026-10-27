// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_METADATA_OWNER_H
#define VLC_METADATA_OWNER_H 1
#include <vlc_common.h>
#include <vlc_metadata_backend.h>
#ifdef __cplusplus
extern "C" {
#endif

/** Bind one metadata owner to the object's VLC instance. The binding holds
 * the backend. It rejects a second active owner. A closed owner can be
 * replaced. A NULL backend cannot restore the legacy network path.
 */
VLC_API int vlc_metadata_owner_Set(vlc_object_t *, vlc_metadata_backend_t *);

/** Return a held backend, or NULL if no registry entry exists.
 * Set owned to true for an active owner, a closed owner, or a retired instance.
 * A retired instance remains owned even after the entry is detached.
 * When owned is true,
 * a caller must not use a legacy remote provider as a fallback.
 */
VLC_API vlc_metadata_backend_t *vlc_metadata_owner_Hold(vlc_object_t *, bool *owned);

#ifdef __cplusplus
}
#endif
#endif
