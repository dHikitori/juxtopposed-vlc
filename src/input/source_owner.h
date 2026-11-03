// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_INPUT_SOURCE_OWNER_H
#define VLC_INPUT_SOURCE_OWNER_H 1
#include <vlc_common.h>
/* Call from the instance cleanup thread. Reject replacement owners, then close
 * native work before the interface or player can wait for an input thread.
 * Close permanently retires the instance, even without a bound owner. It
 * does not allocate memory. Forget after those users have ended. Both
 * functions are idempotent. The instance must remain valid during callbacks.
 * Backend callbacks run outside the registry lock.
 */
void vlc_source_owner_Close(vlc_object_t *);
void vlc_source_owner_Forget(vlc_object_t *);
#endif
