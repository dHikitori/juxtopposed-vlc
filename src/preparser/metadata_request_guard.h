// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_METADATA_REQUEST_GUARD_INTERNAL_H
#define VLC_METADATA_REQUEST_GUARD_INTERNAL_H 1
#include <vlc_common.h>
#include <vlc_metadata_backend.h>

typedef struct vlc_metadata_pipeline_guard vlc_metadata_pipeline_guard_t;
struct vlc_metadata_local_events;
vlc_metadata_pipeline_guard_t *vlc_metadata_pipeline_guard_New(void);
vlc_metadata_pipeline_guard_t *vlc_metadata_pipeline_guard_Hold(vlc_metadata_pipeline_guard_t *);
void vlc_metadata_pipeline_guard_Release(vlc_metadata_pipeline_guard_t *);
void vlc_metadata_pipeline_guard_Cancel(vlc_metadata_pipeline_guard_t *);
bool vlc_metadata_pipeline_guard_IsCurrent(vlc_metadata_pipeline_guard_t *);

/* One terminal decision wins. Cancel after an Apply decision cannot change
 * its status or sideband authority. An idle cancellation refuses later work. */
bool vlc_metadata_pipeline_guard_TryCancel(vlc_metadata_pipeline_guard_t *);
/* Record an error or a completion without native publication. This function
 * never grants sideband authority. Return the winning terminal status. */
int vlc_metadata_pipeline_guard_Finish(vlc_metadata_pipeline_guard_t *, int status);
/* Run the callback-free native Apply under the cancellation admission lock.
 * Retain the job through unlock, so its final backend destroy hook cannot run
 * under this lock. Record the exact result before another thread can cancel.
 * Success can admit sidebands even when no standard field needed a write.
 * Take the prepared event arrays into an empty output before unlock. Deliver
 * their callbacks after return. An error never admits sidebands. Call once. */
int vlc_metadata_pipeline_guard_Commit(vlc_metadata_pipeline_guard_t *,
    vlc_metadata_job_t *, input_item_t *, uint32_t replace_fields, size_t *applied,
    struct vlc_metadata_local_events *events);
bool vlc_metadata_pipeline_guard_IsCommitted(vlc_metadata_pipeline_guard_t *);
bool vlc_metadata_pipeline_guard_GetStatus(vlc_metadata_pipeline_guard_t *, int *status);
#endif
