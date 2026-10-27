// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_SOURCE_BACKEND_H
#define VLC_SOURCE_BACKEND_H 1
#include <vlc_common.h>
#include <vlc_input_policy.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct vlc_source_backend vlc_source_backend_t;
typedef struct vlc_source_job vlc_source_job_t;
typedef struct vlc_source_lease vlc_source_lease_t;

/** These functions can run on any native thread. Keep their state independent
 * of the interface. cancel must close the transport and wake blocked reads.
 * Neither function can wait for a UI event.
 */
struct vlc_source_lease_operations {
    void (*cancel)(void *opaque);
    void (*destroy)(void *opaque);
};
VLC_API vlc_source_lease_t *vlc_source_lease_New(const struct vlc_source_lease_operations *, void *opaque);
VLC_API vlc_source_lease_t *vlc_source_lease_Hold(vlc_source_lease_t *);
VLC_API void vlc_source_lease_Cancel(vlc_source_lease_t *);
/** The last release also cancels the transport. */
VLC_API void vlc_source_lease_Release(vlc_source_lease_t *);

struct vlc_source_request {
    const char *source_uri; /**< Copied host admission URI. */
    uint64_t input_generation; /**< A nonzero native input generation. */
};
struct vlc_source_result {
    const char *proxy_uri; /**< A host-owned loopback resource. */
    vlc_input_policy_t *policy;
    vlc_source_lease_t *lease;
};
struct vlc_source_backend_operations {
    /** Hold the job before asynchronous work. Return without a wait for Qt.
     * Refuse a terminal or closed job before work and after a queued hop.
     * Every later network request needs the same current-job guard.
     */
    int (*dispatch)(void *opaque, vlc_source_job_t *);
    /** The native request is complete before this cleanup hook starts. */
    void (*cancel)(void *opaque, vlc_source_job_t *);
    void (*destroy)(void *opaque);
};
typedef void (*vlc_source_ended_cb)(vlc_source_job_t *, int status, void *userdata);

VLC_API vlc_source_backend_t *vlc_source_backend_New(const struct vlc_source_backend_operations *, void *opaque);
VLC_API vlc_source_backend_t *vlc_source_backend_Hold(vlc_source_backend_t *);
VLC_API void vlc_source_backend_Release(vlc_source_backend_t *);
/** Complete pending requests with -EINTR before return. A concurrent callback
 * can still be active. Callback state must remain valid until it returns.
 * Late Complete calls have no effect. No interface event is necessary.
 */
VLC_API void vlc_source_backend_Close(vlc_source_backend_t *);
VLC_API bool vlc_source_backend_IsClosed(vlc_source_backend_t *);
VLC_API vlc_source_job_t *vlc_source_job_New(vlc_source_backend_t *, const struct vlc_source_request *, vlc_source_ended_cb, void *userdata);
VLC_API vlc_source_job_t *vlc_source_job_Hold(vlc_source_job_t *);
VLC_API void vlc_source_job_Release(vlc_source_job_t *);
/** New jobs are idle. A successful Start adds exactly one terminal callback.
 * That callback can run before Start returns. A failed Start adds none.
 */
VLC_API int vlc_source_job_Start(vlc_source_job_t *);
/** Idle cancellation prevents Start and adds no terminal callback. A completed
 * result keeps its status, but Cancel still revokes its policy and transport.
 * Return true only when this call won a pending or idle cancellation.
 */
VLC_API bool vlc_source_job_Cancel(vlc_source_job_t *);
VLC_API bool vlc_source_job_IsPending(vlc_source_job_t *);
/** Register transport ownership before its first I/O operation. A lease can
 * belong to one job only. Cancellation closes it without a UI event. A
 * successful result transfers this registration to the backend until Stop
 * cancels the lease, or Close revokes all issued policies and transports.
 * Input Stop and the end of playback must cancel the lease before final release.
 * An individual manifest or segment EOF must keep the input's lease active.
 */
VLC_API bool vlc_source_job_AttachTransport(vlc_source_job_t *, vlc_input_policy_t *, vlc_source_lease_t *);
VLC_API const struct vlc_source_request *vlc_source_job_GetRequest(const vlc_source_job_t *);
VLC_API const struct vlc_source_result *vlc_source_job_GetResult(vlc_source_job_t *);
/** Copy the URI and hold the policy and lease before the callback. The caller
 * retains its references. Release them after this call, including on false.
 * Success requires the same policy and lease as AttachTransport. Native
 * consumers must install the policy with an expected-policy CAS. A result
 * can be revoked during its callback. The native access policy still applies.
 * This function does not change the input, queue item, or admission policy.
 */
VLC_API bool vlc_source_job_Complete(vlc_source_job_t *, int status, const struct vlc_source_result *);

#ifdef __cplusplus
}
#endif
#endif
