// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_METADATA_ARTWORK_OUTCOME_INTERNAL_H
#define VLC_METADATA_ARTWORK_OUTCOME_INTERNAL_H 1
#include <vlc_common.h>
#include <vlc_meta_fetcher.h>
#include <vlc_metadata_backend.h>
#include <vlc_tick.h>
#ifdef __cplusplus
extern "C" {
#endif

#define VLC_METADATA_ARTWORK_PLAN_LIMIT 16u
typedef struct vlc_metadata_artwork_admission vlc_metadata_artwork_admission_t;

/* Native state only. The owner must hold a reference through each call.
 * Revoke closes new admission, then joins an admitted commit. Repeated Revoke
 * also joins. The deadline is monotonic and at most 30 seconds from creation.
 * No callback, file access, Qt work or backend/item lock occurs here. */
VLC_API vlc_metadata_artwork_admission_t *vlc_metadata_artwork_admission_New(vlc_tick_t deadline);
VLC_API vlc_metadata_artwork_admission_t *vlc_metadata_artwork_admission_Hold(vlc_metadata_artwork_admission_t *);
VLC_API void vlc_metadata_artwork_admission_Release(vlc_metadata_artwork_admission_t *);
VLC_API void vlc_metadata_artwork_admission_Revoke(vlc_metadata_artwork_admission_t *);
/* Atomic open + immutable native deadline query only. This takes no admission
 * mutex and may run under a backend/item lock; it grants no commit lease. */
VLC_API bool vlc_metadata_artwork_admission_IsCurrent(const vlc_metadata_artwork_admission_t *);

/* Core only. Enter holds admission on success. Final lock order is parent
 * pipeline guard -> admission -> backend -> item. No callback runs inside.
 * Prepare values first; release captures/events only after Leave. */
bool vlc_metadata_artwork_admission_Enter(vlc_metadata_artwork_admission_t *);
void vlc_metadata_artwork_admission_Leave(vlc_metadata_artwork_admission_t *);

struct vlc_metadata_artwork_exhaustion_summary {
    unsigned char plan_digest[32];
    unsigned count;
    meta_fetcher_outcome_t outcomes[VLC_METADATA_ARTWORK_PLAN_LIMIT];
};

/* Native-only: sole production caller is the typed host attempt aggregate.
 * It has already admitted every exact invocation/claim/partition/rights fact.
 * A generic Metadata@1 provider may supply a semantic no_match claim after
 * host contract/identity/field/host-call validation. Raw JSON/provenance alone
 * cannot enter here. Only typed NO_MATCH/NTA slots, with at least one NO_MATCH,
 * are accepted. Bind once to the exact pending artwork child and its snapshot.
 * The attached sideband does not publish a field/status or complete the job.
 * keeper owns only native/std watches and native outcome authorities, never
 * a job/claim, QObject or Qt capture. Ownership transfers only on success.
 * keeper_release runs outside every native lock, once at final destruction. */
VLC_API int vlc_metadata_job_SealArtworkNotFound(vlc_metadata_job_t *,
    vlc_metadata_artwork_admission_t *,
    const struct vlc_metadata_artwork_exhaustion_summary *,
    void *keeper, void (*keeper_release)(void *));

/* Bind current provider/account authority to an already approved artwork
 * result. Only the native host may call this function. approved must carry a
 * native approval for this exact child's URI and policy. This function grants
 * no image approval and completes no job. The pending network child must ask
 * for artwork/enrich with exactly one artwork field. Bind once, before Complete.
 * A successful child Apply and PrepareFetchedArtwork carry the same authority
 * into final native Apply. Cancellation/revocation refuses new admission and
 * joins an admitted commit. The keeper contains native/std values only and
 * owns no job/claim or Qt value. Ownership transfers only on success. The
 * release callback runs once, outside all native locks, at final destruction. */
VLC_API int vlc_metadata_job_SealArtworkSource(vlc_metadata_job_t *child,
    input_item_t *approved, const char *image_uri, vlc_metadata_artwork_admission_t *,
    void *keeper, void (*keeper_release)(void *));

#ifdef __cplusplus
}
#endif
#endif
