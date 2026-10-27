// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_METADATA_PIPELINE_INTERNAL_H
#define VLC_METADATA_PIPELINE_INTERNAL_H 1
#include <vlc_common.h>
#include <vlc_input_item.h>
#include <vlc_preparser.h>

/** This owner exists once per native preparser. The instance and native stage
 * context must outlive Delete and all callbacks that call Delete themselves.
 * No stage may wait for a Qt event. The parent closes this owner before it
 * cancels and deletes its native local fetcher, then calls Delete.
 */
typedef struct vlc_metadata_pipeline vlc_metadata_pipeline_t;
typedef struct vlc_metadata_pipeline_stage vlc_metadata_pipeline_stage_t;
typedef struct vlc_metadata_pipeline_guard vlc_metadata_pipeline_guard_t;

/* The parent holds this native guard before it submits a request. Cancel
 * latches before the pipeline handoff. It cannot restore a cancelled guard.
 * Final native commit and Cancel use the same admission lock. If Cancel wins,
 * no target field changes. If commit wins, Cancel returns after that commit.
 * No callback runs under this lock.
 */
vlc_metadata_pipeline_guard_t *vlc_metadata_pipeline_guard_New(void);
vlc_metadata_pipeline_guard_t *vlc_metadata_pipeline_guard_Hold(vlc_metadata_pipeline_guard_t *);
void vlc_metadata_pipeline_guard_Release(vlc_metadata_pipeline_guard_t *);
void vlc_metadata_pipeline_guard_Cancel(vlc_metadata_pipeline_guard_t *);
bool vlc_metadata_pipeline_guard_IsCurrent(vlc_metadata_pipeline_guard_t *);
struct vlc_metadata_pipeline_operations {
    /** The token supplies a private working item. It never supplies the live
     * target. Call IsCurrent before queue submission and each I/O handoff.
     * On success, consume the provider token with Done exactly once. Done can
     * run inline. On error, do not call Done. The owner consumes that token.
     */
    int (*local)(void *, vlc_metadata_pipeline_stage_t *);
    /** Cancellation can precede local() and can repeat. It must latch before native I/O.
     * The provider still consumes its successful token through Done. A native
     * worker can do that after its interrupt completes. No Qt event is needed.
     */
    void (*cancel_local)(void *, vlc_metadata_pipeline_stage_t *);
};
typedef void (*vlc_metadata_pipeline_ended_cb)(input_item_t *, int status, void *);
vlc_metadata_pipeline_t *vlc_metadata_pipeline_New(vlc_object_t *,
    const struct vlc_metadata_pipeline_operations *, void *stage_context);

/** VLC_ENOENT means this instance has never acquired a metadata owner. Only
 * that result permits the parent to attempt the separate legacy lease path.
 * A closed, retired, suspended, or unavailable owner never permits fallback.
 *
 * Before admission, errors have no callback. After admission, return success
 * and call on_ended once, including cancellation during the initial handoff.
 * Capture the original target and tokens before the first native stage. Only
 * the final native commit can change that target.
 */
int vlc_metadata_pipeline_Push(vlc_metadata_pipeline_t *, input_item_t *, int options,
    vlc_metadata_pipeline_guard_t *parent_guard, const void *request_key, vlc_metadata_pipeline_ended_cb on_ended, void *userdata);
size_t vlc_metadata_pipeline_Cancel(vlc_metadata_pipeline_t *, const void *request_key);
void vlc_metadata_pipeline_Close(vlc_metadata_pipeline_t *);
/** Drain native stage and terminal callbacks. Exclude callbacks on this thread
 * that call Delete. Callback references keep their state alive after return.
 */
void vlc_metadata_pipeline_Delete(vlc_metadata_pipeline_t *);

vlc_metadata_pipeline_stage_t *vlc_metadata_pipeline_stage_Hold(vlc_metadata_pipeline_stage_t *);
void vlc_metadata_pipeline_stage_Release(vlc_metadata_pipeline_stage_t *);
input_item_t *vlc_metadata_pipeline_stage_GetItem(vlc_metadata_pipeline_stage_t *);
bool vlc_metadata_pipeline_stage_IsCurrent(vlc_metadata_pipeline_stage_t *);
void vlc_metadata_pipeline_stage_Done(vlc_metadata_pipeline_stage_t *, int status);
#endif
