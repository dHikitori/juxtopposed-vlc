// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_MEDIALIBRARY_WORK_H
#define VLC_MEDIALIBRARY_WORK_H 1
#include <vlc_common.h>
typedef struct vlc_medialibrary_t vlc_medialibrary_t;
typedef struct vlc_ml_work vlc_ml_work_t;
#ifdef __cplusplus
extern "C" {
#endif
/* Acquire before task submission while the library is valid. NULL means that
 * the library has retired or cannot retain another task. A pending lease can
 * outlive library destruction. It gives no authority to call the library.
 */
VLC_API vlc_ml_work_t *vlc_ml_work_Acquire(vlc_medialibrary_t *);
/* Begin once on the native worker. NULL means that the call is not admitted.
 * A non-NULL result remains valid until End. The worker must not wait for Qt.
 */
VLC_API vlc_medialibrary_t *vlc_ml_work_Begin(vlc_ml_work_t *);
/* End the native call before any Qt completion. Repeated End calls are safe. */
VLC_API void vlc_ml_work_End(vlc_ml_work_t *);
/* Release pending or ended work on any thread. End running work first. */
VLC_API void vlc_ml_work_Release(vlc_ml_work_t *);
#ifdef __cplusplus
}
#endif
#endif
