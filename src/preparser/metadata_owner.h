// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_METADATA_OWNER_INTERNAL_H
#define VLC_METADATA_OWNER_INTERNAL_H 1
#include <vlc_metadata_owner.h>
#include <vlc_interrupt.h>

typedef struct vlc_metadata_legacy vlc_metadata_legacy_t;
/* A pending lease can outlive instance cleanup. It gives no network authority.
 * Acquire before task submission. Begin on the native worker before legacy
 * network work. NULL or false means refusal. No interface event is necessary.
 */
vlc_metadata_legacy_t *vlc_metadata_legacy_Acquire(vlc_object_t *);
bool vlc_metadata_legacy_Begin(vlc_metadata_legacy_t *);
vlc_interrupt_t *vlc_metadata_legacy_Interrupt(vlc_metadata_legacy_t *);
void vlc_metadata_legacy_Cancel(vlc_metadata_legacy_t *);
/* Restore the thread interrupt and stop forwarding before End. End completes
 * the native work before it can resume an owned backend. Call Release after
 * End for running work. Release also removes a pending lease.
 */
void vlc_metadata_legacy_End(vlc_metadata_legacy_t *);
void vlc_metadata_legacy_Release(vlc_metadata_legacy_t *);

/* Call Close before interface shutdown can wait for a native input or parser.
 * Forget after all native metadata users end. These cleanup calls run on the
 * instance cleanup thread. They do not drain concurrent metadata result callbacks.
 */
void vlc_metadata_owner_Close(vlc_object_t *);
void vlc_metadata_owner_Forget(vlc_object_t *);
#endif
