// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_METADATA_REQUESTS_INTERNAL_H
#define VLC_METADATA_REQUESTS_INTERNAL_H 1
#include <vlc_common.h>
#include <vlc_metadata_backend.h>

typedef struct vlc_metadata_requests vlc_metadata_requests_t;
typedef void (*vlc_metadata_requests_cb)(vlc_metadata_job_t *, int status, void *userdata);
/* The instance must outlive this list and all its callbacks. */
vlc_metadata_requests_t *vlc_metadata_requests_New(vlc_object_t *);

/** VLC_ENOENT means this instance has no owner. Only that result permits
 * a legacy remote path. A successful push has one callback, which can run
 * before return. The callback must not wait for a UI event.
 */
int vlc_metadata_requests_Push(vlc_metadata_requests_t *, const struct vlc_metadata_request *,
                              const void *key, vlc_metadata_requests_cb, void *userdata);
size_t vlc_metadata_requests_Cancel(vlc_metadata_requests_t *, const void *key);

/** Stop new requests and finish this list's pending jobs. Other preparsers
 * can still use the shared backend. Delete drains active callbacks, except
 * the callback that calls Delete itself. No Qt event is necessary.
 */
void vlc_metadata_requests_Close(vlc_metadata_requests_t *);
void vlc_metadata_requests_Delete(vlc_metadata_requests_t *);
#endif
