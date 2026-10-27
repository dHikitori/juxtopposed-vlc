// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_METADATA_BACKEND_H
#define VLC_METADATA_BACKEND_H 1
#include <vlc_common.h>
#include <vlc_meta.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vlc_metadata_backend vlc_metadata_backend_t;
typedef struct vlc_metadata_job vlc_metadata_job_t;

#define VLC_METADATA_REQUEST_NETWORK 0x01u
#define VLC_METADATA_REQUEST_AUTOMATIC 0x02u

/** The host owns this request. New() copies all strings, context data, and the
 * input item. It can create an empty metadata object for token capture.
 * Do not send a native pointer, URI, or storage to an add-on.
 */
struct vlc_metadata_request {
    const char *feature;
    const char *operation;
    const char *const *fields;
    size_t field_count;
    const char *context_json;
    size_t context_size;
    input_item_t *item;
    uint64_t generation;
    uint64_t target_revision;
    unsigned flags;
};

/** A native field patch. The job owns the values. Before a consumer writes a
 * target, it must compare expected_value and its user and field revisions.
 * Completion does not write a live target. A native consumer can call Apply.
 */
struct vlc_metadata_field_patch {
    vlc_meta_type_t type;
    const char *expected_value;
    const char *value;
    uint64_t expected_revision;
};
struct vlc_metadata_result {
    input_item_t *item; /**< A separate native result copy. Do not change it. */
    const struct vlc_metadata_field_patch *fields;
    size_t field_count;
    const char *provenance_json;
    size_t provenance_size;
};

struct vlc_metadata_backend_operations {
    /** The bridge calls this function without its lock. The job is valid for
     * this call. Use Hold before asynchronous work. Release the reference
     * after a result, cancellation, or queue error. Return without a wait for
     * a UI event or network operation. Refuse a cancelled or closed job before
     * I/O and after each queued handoff.
     */
    int (*dispatch)(void *opaque, vlc_metadata_job_t *job);
    /** Use this hook for host cleanup. The native request is complete before
     * this hook starts. Do not wait for a UI event loop.
     */
    void (*cancel)(void *opaque, vlc_metadata_job_t *job);
    void (*destroy)(void *opaque); /**< The last backend reference has ended. */
};
typedef void (*vlc_metadata_ended_cb)(vlc_metadata_job_t *, int status, void *userdata);

VLC_API vlc_metadata_backend_t *vlc_metadata_backend_New(const struct vlc_metadata_backend_operations *, void *opaque);
VLC_API vlc_metadata_backend_t *vlc_metadata_backend_Hold(vlc_metadata_backend_t *);
VLC_API void vlc_metadata_backend_Release(vlc_metadata_backend_t *);
/** Reject new Start calls. Finish each pending request with -EINTR before
 * return. Call its callback outside the backend lock. No Qt event is
 * necessary. A concurrent result callback can still be active. Its caller
 * must keep callback state until that callback returns. A provider reference
 * can exist after Close. Ignore late Complete calls.
 */
VLC_API void vlc_metadata_backend_Close(vlc_metadata_backend_t *);
VLC_API bool vlc_metadata_backend_IsClosed(vlc_metadata_backend_t *);
/** Cancel the automatic jobs present at admission. Manual jobs stay active.
 * Hold the bounded job set under the backend lock, then cancel outside that
 * lock. Return the size of that set. The native host must first withdraw its
 * automatic-network consent. Every new host commit anchor must read that
 * consent after StartLocal, so it cannot miss a concurrent withdrawal.
 * This function does not grant or restore network consent.
 */
VLC_API size_t vlc_metadata_backend_CancelAutomatic(vlc_metadata_backend_t *);

/** Suspend dispatch before an ownership transition. Refuse a closed backend
 * or a backend with active dispatched work. An already suspended backend can
 * retain another suspension. Pair each successful call with Resume.
 * Start can accept bounded pending jobs while dispatch is suspended.
 * A suspended backend does not admit an Apply operation.
 */
VLC_API bool vlc_metadata_backend_Suspend(vlc_metadata_backend_t *);
/** Release one suspension. Dispatch pending jobs outside the backend lock.
 * A closed backend does not resume work. No interface event is necessary.
 */
VLC_API void vlc_metadata_backend_Resume(vlc_metadata_backend_t *);
VLC_API bool vlc_metadata_backend_IsReady(vlc_metadata_backend_t *);

VLC_API vlc_metadata_job_t *vlc_metadata_job_New(vlc_metadata_backend_t *, const struct vlc_metadata_request *, vlc_metadata_ended_cb, void *userdata);
VLC_API vlc_metadata_job_t *vlc_metadata_job_Hold(vlc_metadata_job_t *);
VLC_API void vlc_metadata_job_Release(vlc_metadata_job_t *);
/** New jobs are idle. If Start succeeds, on_ended runs once. It can run before
 * Start returns. If Start fails, it does not add a callback. A host dispatch
 * error after acceptance causes an on_ended error.
 */
VLC_API int vlc_metadata_job_Start(vlc_metadata_job_t *);
/** Start a host-native transaction anchor without provider dispatch. Capture
 * the target through New before the first local stage. This anchor uses the
 * same retained-job limit and cancellation rules as Start. Refuse a suspended
 * or closed backend. Resume never dispatches the anchor to a provider.
 *
 * Only trusted native code can complete this anchor from its staged result.
 * Apply still validates the original target, URI, policy, and field tokens.
 * A cancelled anchor cannot acquire a new result or commit a field patch.
 * This operation is not part of the worker ABI.
 */
VLC_API int vlc_metadata_job_StartLocal(vlc_metadata_job_t *);

/** Idle cancellation prevents Start and adds no callback. A completed result
 * keeps its status, but Cancel still prevents a later Apply. Return true only
 * when this call wins an idle or pending cancellation.
 */
VLC_API bool vlc_metadata_job_Cancel(vlc_metadata_job_t *);
VLC_API bool vlc_metadata_job_IsPending(vlc_metadata_job_t *);
VLC_API const struct vlc_metadata_request *vlc_metadata_job_GetRequest(const vlc_metadata_job_t *);
VLC_API const struct vlc_metadata_result *vlc_metadata_job_GetResult(vlc_metadata_job_t *);
/** Copy the native result and provenance before the final callback. Native
 * consumers use field patches. They do not need a JSON parser. Return false
 * if a result or cancellation already won. Do not change a live target.
 */
VLC_API bool vlc_metadata_job_Complete(vlc_metadata_job_t *, int status, input_item_t *result,
                                    const char *provenance_json, size_t provenance_size);

/** Commit a successful native result to its original submitted item. Fill
 * missing fields only. A native host can set bits in replace_fields for an
 * explicit correction; an add-on payload must not supply this mask.
 *
 * Before any write, compare every proposed field token, identity field token,
 * extra-tag token, original URI, and exact item policy. Refuse a cancelled job,
 * a closed backend, an exhausted token, or a revoked policy. Return -EAGAIN
 * without writes when the target changed. applied can be NULL.
 *
 * Admission holds the backend lock and then the item lock. Reserve all new
 * field tokens before any write. Preserved fields need no new token. The final
 * policy revocation read is the linearization point. A later cancellation or policy
 * revocation does not undo an admitted commit. No callback runs under these
 * locks. Allocate all replacement values before admission.
 *
 * An artwork change also needs a current native approval on the result copy.
 * Allocate its proof before admission. Publish the proof and reserved artwork
 * token under the same item lock. A file URI in text metadata is not proof.
 * An unapproved raw artwork field on a restricted item is a missing display
 * asset. A current approved record or ordinary native artwork stays protected.
 */
VLC_API int vlc_metadata_job_Apply(vlc_metadata_job_t *, input_item_t *,
                                 uint32_t replace_fields, size_t *applied);

#ifdef __cplusplus
}
#endif
#endif
