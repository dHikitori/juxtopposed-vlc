// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_METADATA_OWNER_INTERNAL_H
#define VLC_METADATA_OWNER_INTERNAL_H 1
#include <vlc_metadata_owner.h>

/* Call Close before interface shutdown can wait for a native input or parser.
 * Forget after all native metadata users end. These cleanup calls run on the
 * instance cleanup thread. They do not drain a concurrent callback.
 */
void vlc_metadata_owner_Close(vlc_object_t *);
void vlc_metadata_owner_Forget(vlc_object_t *);
#endif
