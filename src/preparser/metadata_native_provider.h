// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_METADATA_NATIVE_PROVIDER_INTERNAL_H
#define VLC_METADATA_NATIVE_PROVIDER_INTERNAL_H 1
#include <vlc_common.h>
#include <vlc_meta_fetcher.h>
#include <vlc_metadata_backend.h>
#ifdef __cplusplus
extern "C" {
#endif

/* This API is private to the native host. It is not a worker capability.
 * Admit the app's counted join before New creates native executors. The native
 * parent must remain valid until Delete returns. Close never waits for Qt.
 * Delete closes and drains on the counted cleanup thread. Do not call Delete
 * from a probe callback. A retained closed context has no parent after Delete.
 */
typedef struct vlc_metadata_native_provider vlc_metadata_native_provider_t;
typedef struct vlc_metadata_native_claim vlc_metadata_native_claim_t;
VLC_API vlc_metadata_native_provider_t *vlc_metadata_native_provider_New(vlc_object_t *);
VLC_API vlc_metadata_native_provider_t *vlc_metadata_native_provider_Hold(vlc_metadata_native_provider_t *);
VLC_API void vlc_metadata_native_provider_Release(vlc_metadata_native_provider_t *);
VLC_API void vlc_metadata_native_provider_Close(vlc_metadata_native_provider_t *);
VLC_API void vlc_metadata_native_provider_Delete(vlc_metadata_native_provider_t *);

/* A successful Probe calls its callback once, after native I/O ends. The claim
 * is borrowed through that call; Hold retains it. An admission error has no
 * callback. Only one named app-shipped family can run. The exact pending job
 * and its ordinary native snapshot supply authority. No URL can create a job.
 * Keep callback data valid through the terminal callback. Cancel uses the exact
 * job, is safe for an unknown job, and can deliver a callback before return.
 */
typedef void (*vlc_metadata_native_probe_cb)(vlc_metadata_native_claim_t *, int, void *);
VLC_API int vlc_metadata_native_provider_Probe(vlc_metadata_native_provider_t *,
    vlc_metadata_job_t *, meta_fetcher_provider_t, vlc_metadata_native_probe_cb, void *);
/* Return a sealed native outcome through the same borrowed callback contract.
 * A completed lookup has status VLC_SUCCESS and a receipt, including a negative
 * lookup or a lookup failure. Read GetOutcome and check MatchesJob before use.
 * Allocation, cancellation, Close, or a stale snapshot has no usable receipt.
 * Only MATCH has an artwork address. The old Probe remains positive-only.
 * One of 32 slots remains held through the final receipt reference. Delete
 * drains active probes and callbacks; it does not wait for closed receipts.
 */
VLC_API int vlc_metadata_native_provider_ProbeOutcome(vlc_metadata_native_provider_t *,
    vlc_metadata_job_t *, meta_fetcher_provider_t, vlc_metadata_native_probe_cb, void *);
VLC_API void vlc_metadata_native_provider_Cancel(vlc_metadata_native_provider_t *, vlc_metadata_job_t *);

/* Only the trusted native image adapter can consume this sealed claim. These
 * accessors grant no guest URL, write, or original embedded-artwork permit.
 * MatchesJob compares the exact job, snapshot, tokens, policy, and request
 * generations. Final native Apply must still validate the live target.
 * A claim retains its job and backend. Counted app cleanup must release every
 * claim before Core and Qt teardown. A cancel control must not retain a claim
 * after that join.
 */
VLC_API vlc_metadata_native_claim_t *vlc_metadata_native_claim_Hold(vlc_metadata_native_claim_t *);
VLC_API void vlc_metadata_native_claim_Release(vlc_metadata_native_claim_t *);
VLC_API meta_fetcher_provider_t vlc_metadata_native_claim_GetFamily(const vlc_metadata_native_claim_t *);
VLC_API const char *vlc_metadata_native_claim_GetArtworkURI(const vlc_metadata_native_claim_t *);
VLC_API meta_fetcher_outcome_t vlc_metadata_native_claim_GetOutcome(const vlc_metadata_native_claim_t *);
VLC_API bool vlc_metadata_native_claim_MatchesJob(const vlc_metadata_native_claim_t *, vlc_metadata_job_t *);

/* Admit one negative outcome authority while the exact child remains pending.
 * Require the expected family and typed NO_MATCH/NTA receipt. The returned
 * reference holds provider+reservation+admission, never a child/claim. Provider
 * Close revokes it synchronously, including concurrent repeated Close calls.
 * A refused registration revokes admission before returning NULL. Keep the
 * authority in the native-only sideband keeper through final Apply.
 * At most one admission per claim; it shares the original 32-slot reservation.
 * The provider list is borrowed. Final Release decrements/unlinks under the
 * provider lock; Close takes temporary refs under that lock, then revokes and
 * releases outside it. No provider/authority or job/claim cycle is created. */
typedef struct vlc_metadata_artwork_admission vlc_metadata_artwork_admission_t;
typedef struct vlc_metadata_native_outcome_authority vlc_metadata_native_outcome_authority_t;
VLC_API vlc_metadata_native_outcome_authority_t *vlc_metadata_native_claim_AdmitOutcome(
    vlc_metadata_native_claim_t *, vlc_metadata_job_t *, meta_fetcher_provider_t expected_family,
    vlc_metadata_artwork_admission_t *);
VLC_API void vlc_metadata_native_outcome_authority_Release(vlc_metadata_native_outcome_authority_t *);

#ifdef __cplusplus
}
#endif
#endif
