// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_SOURCE_OWNER_H
#define VLC_SOURCE_OWNER_H 1
#include <vlc_common.h>
#include <vlc_source_backend.h>
#ifdef __cplusplus
extern "C" {
#endif
/** Bind one native source owner to this VLC instance. A second active owner
 * is refused. A closed owner can be replaced before instance cleanup starts.
 * Cleanup permanently refuses new owners. NULL cannot clear the binding.
 */
VLC_API int vlc_source_owner_Set(vlc_object_t *, vlc_source_backend_t *);
/** Return a held backend, including a closed backend, or NULL if none exists.
 * After final cleanup, return NULL. An admitted source URI never permits a
 * legacy or unrestricted fallback.
 */
VLC_API vlc_source_backend_t *vlc_source_owner_Hold(vlc_object_t *);
#ifdef __cplusplus
}
#endif
#endif
