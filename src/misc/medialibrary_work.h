// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_MISC_MEDIALIBRARY_WORK_H
#define VLC_MISC_MEDIALIBRARY_WORK_H 1
#include <vlc_medialibrary_work.h>
struct vlc_ml_work_owner;
struct vlc_ml_work_owner *vlc_ml_work_owner_New(vlc_medialibrary_t *);
vlc_ml_work_t *vlc_ml_work_owner_Acquire(struct vlc_ml_work_owner *);
/* Retire outside the media-library callback lock. Wait only for running calls.
 * Passive leases keep this owner alive after the library is gone.
 */
void vlc_ml_work_owner_Retire(struct vlc_ml_work_owner *);
void vlc_ml_work_owner_Release(struct vlc_ml_work_owner *);
#endif
