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
 * input item. Do not send a native pointer, URI, or storage to an add-on.
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
 * Use the normal commit lock. This bridge does not write a live target.
 */
struct vlc_metadata_field_patch {
    vlc_meta_type_t type;
    const char *expected_value;
    const char *value;
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
     * a UI event or network operation.
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

VLC_API vlc_metadata_job_t *vlc_metadata_job_New(vlc_metadata_backend_t *, const struct vlc_metadata_request *, vlc_metadata_ended_cb, void *userdata);
VLC_API vlc_metadata_job_t *vlc_metadata_job_Hold(vlc_metadata_job_t *);
VLC_API void vlc_metadata_job_Release(vlc_metadata_job_t *);
/** New jobs are idle. If Start succeeds, on_ended runs once. It can run before
 * Start returns. If Start fails, it does not add a callback. A host dispatch
 * error after acceptance causes an on_ended error.
 */
VLC_API int vlc_metadata_job_Start(vlc_metadata_job_t *);
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

#ifdef __cplusplus
}
#endif
#endif
