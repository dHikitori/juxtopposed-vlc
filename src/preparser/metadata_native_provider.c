// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <vlc_common.h>
#include <vlc_atomic.h>
#include <vlc_artwork_policy.h>
#include <vlc_input_item.h>
#include <vlc_input_policy.h>
#include <vlc_metadata_revision.h>
#include <vlc_threads.h>
#include "metadata_native_provider.h"
#include "fetcher.h"
#include "../input/item.h"

#define MAX_PROBES 32u
#define MAX_SOURCE_URI 32768u
#define MAX_ARTWORK_URI 8192u

struct native_snapshot {
    vlc_metadata_job_t *job;
    input_item_t *item;
    vlc_input_policy_t *policy;
    struct vlc_metadata_revisions metadata;
    uint64_t uri_revision, name_revision, options_revision;
    uint64_t generation, target_revision;
    char *uri;
};
struct vlc_metadata_native_claim {
    vlc_atomic_rc_t refs;
    vlc_metadata_native_provider_t *provider;
    struct native_snapshot snapshot;
    meta_fetcher_provider_t family;
    char *artwork;
    meta_fetcher_outcome_t outcome;
    struct native_reservation *reservation;
};
struct vlc_metadata_native_provider {
    vlc_atomic_rc_t refs;
    vlc_mutex_t lock;
    vlc_cond_t idle;
    vlc_object_t *object;
    input_fetcher_t *fetcher;
    struct native_probe *head;
    unsigned calls, retained, reserved;
    bool closed, deleting;
};
struct native_probe {
    vlc_atomic_rc_t refs;
    vlc_metadata_native_provider_t *provider;
    struct native_snapshot snapshot;
    meta_fetcher_provider_t family;
    vlc_metadata_native_probe_cb callback;
    void *opaque;
    struct native_probe *next;
    bool listed;
    struct native_reservation *reservation;
};
struct native_reservation {
    vlc_atomic_rc_t refs;
    vlc_metadata_native_provider_t *provider;
    bool counted;
};
static struct native_reservation *reservation_hold(struct native_reservation *reservation)
{ vlc_atomic_rc_inc(&reservation->refs); return reservation; }
static void reservation_release(struct native_reservation *reservation)
{
    if (reservation == NULL || !vlc_atomic_rc_dec(&reservation->refs)) return;
    vlc_metadata_native_provider_t *provider = reservation->provider;
    vlc_mutex_lock(&provider->lock);
    if (reservation->counted) { assert(provider->reserved); --provider->reserved; }
    vlc_mutex_unlock(&provider->lock);
    free(reservation); vlc_metadata_native_provider_Release(provider);
}

static void snapshot_clear(struct native_snapshot *snapshot)
{
    if (snapshot->job != NULL) vlc_metadata_job_Release(snapshot->job);
    if (snapshot->item != NULL) input_item_Release(snapshot->item);
    vlc_input_policy_Release(snapshot->policy); free(snapshot->uri);
    memset(snapshot, 0, sizeof(*snapshot));
}
static bool metadata_revisions_locked(input_item_t *item, struct vlc_metadata_revisions *revisions)
{
    if (item->p_meta == NULL) return false;
    for (int i = 0; i < VLC_META_TYPE_COUNT; ++i) {
        revisions->fields[i] = vlc_meta_FieldRevision(item->p_meta, i);
        if (revisions->fields[i] == UINT64_MAX) return false;
    }
    revisions->extra = vlc_meta_ExtraRevision(item->p_meta);
    return revisions->extra != UINT64_MAX;
}
static bool snapshot_capture(struct native_snapshot *snapshot, vlc_metadata_job_t *job)
{
    const struct vlc_metadata_request *request = vlc_metadata_job_GetRequest(job);
    bool artwork = false;
    for (size_t i = 0; i < request->field_count; ++i)
        artwork |= !strcmp(request->fields[i], "artwork") || !strcmp(request->fields[i], "artworkUrl");
    if (!(request->flags & VLC_METADATA_REQUEST_NETWORK) || request->item == NULL
        || !artwork || !vlc_metadata_job_IsPending(job) || vlc_artwork_item_IsRestricted(request->item)) return false;
    input_item_t *item = request->item;
    vlc_mutex_lock(&item->lock);
    const struct vlc_item_parse_revisions *native = &item_owner(item)->parse_revisions;
    bool valid = item_owner(item)->input_policy == NULL && item->psz_uri != NULL
        && !vlc_input_policy_IsAddonSourceURI(item->psz_uri)
        && strnlen(item->psz_uri, MAX_SOURCE_URI + 1) <= MAX_SOURCE_URI
        && metadata_revisions_locked(item, &snapshot->metadata);
    snapshot->uri_revision = native->fields[VLC_ITEM_PARSE_URI];
    snapshot->name_revision = native->fields[VLC_ITEM_PARSE_NAME];
    snapshot->options_revision = native->fields[VLC_ITEM_PARSE_OPTIONS];
    valid = valid && snapshot->uri_revision != UINT64_MAX && snapshot->name_revision != UINT64_MAX
        && snapshot->options_revision != UINT64_MAX;
    if (valid) snapshot->uri = strdup(item->psz_uri);
    if (valid && snapshot->uri == NULL) valid = false;
    if (valid) snapshot->policy = vlc_input_policy_Hold(item_owner(item)->input_policy);
    vlc_mutex_unlock(&item->lock);
    if (!valid) return false;
    snapshot->job = vlc_metadata_job_Hold(job); snapshot->item = input_item_Hold(item);
    snapshot->generation = request->generation; snapshot->target_revision = request->target_revision;
    return vlc_metadata_job_IsPending(job);
}
static bool snapshot_current(const struct native_snapshot *snapshot, vlc_metadata_job_t *job)
{
    if (job == NULL || job != snapshot->job || !vlc_metadata_job_IsPending(job)) return false;
    const struct vlc_metadata_request *request = vlc_metadata_job_GetRequest(job);
    if (request->item != snapshot->item || !(request->flags & VLC_METADATA_REQUEST_NETWORK)
        || request->generation != snapshot->generation || request->target_revision != snapshot->target_revision) return false;
    input_item_t *item = snapshot->item;
    struct vlc_metadata_revisions metadata;
    vlc_mutex_lock(&item->lock);
    const struct vlc_item_parse_revisions *native = &item_owner(item)->parse_revisions;
    const bool current = item_owner(item)->input_policy == snapshot->policy
        && !vlc_input_policy_IsRevoked(snapshot->policy) && item->psz_uri != NULL
        && strcmp(snapshot->uri, item->psz_uri) == 0
        && native->fields[VLC_ITEM_PARSE_URI] == snapshot->uri_revision
        && native->fields[VLC_ITEM_PARSE_NAME] == snapshot->name_revision
        && native->fields[VLC_ITEM_PARSE_OPTIONS] == snapshot->options_revision
        && metadata_revisions_locked(item, &metadata)
        && !memcmp(&metadata, &snapshot->metadata, sizeof(metadata));
    vlc_mutex_unlock(&item->lock);
    return current && vlc_metadata_job_IsPending(job);
}
static bool snapshot_copy(struct native_snapshot *target, const struct native_snapshot *source)
{
    *target = *source; target->uri = strdup(source->uri);
    if (target->uri == NULL) { memset(target, 0, sizeof(*target)); return false; }
    target->job = vlc_metadata_job_Hold(source->job); target->item = input_item_Hold(source->item);
    target->policy = vlc_input_policy_Hold(source->policy); return true;
}

vlc_metadata_native_claim_t *vlc_metadata_native_claim_Hold(vlc_metadata_native_claim_t *claim)
{ vlc_atomic_rc_inc(&claim->refs); return claim; }
void vlc_metadata_native_claim_Release(vlc_metadata_native_claim_t *claim)
{
    if (claim == NULL || !vlc_atomic_rc_dec(&claim->refs)) return;
    snapshot_clear(&claim->snapshot); free(claim->artwork);
    reservation_release(claim->reservation);
    vlc_metadata_native_provider_Release(claim->provider); free(claim);
}
meta_fetcher_provider_t vlc_metadata_native_claim_GetFamily(const vlc_metadata_native_claim_t *claim)
{ return claim->family; }
const char *vlc_metadata_native_claim_GetArtworkURI(const vlc_metadata_native_claim_t *claim)
{ return claim->artwork; }
meta_fetcher_outcome_t vlc_metadata_native_claim_GetOutcome(const vlc_metadata_native_claim_t *claim)
{ return claim != NULL ? claim->outcome : META_FETCHER_OUTCOME_UNKNOWN; }
bool vlc_metadata_native_claim_MatchesJob(const vlc_metadata_native_claim_t *claim, vlc_metadata_job_t *job)
{
    if (claim == NULL) return false;
    vlc_mutex_lock(&claim->provider->lock); const bool open = !claim->provider->closed;
    vlc_mutex_unlock(&claim->provider->lock);
    return open && snapshot_current(&claim->snapshot, job);
}

vlc_metadata_native_provider_t *vlc_metadata_native_provider_Hold(vlc_metadata_native_provider_t *provider)
{ vlc_atomic_rc_inc(&provider->refs); return provider; }
void vlc_metadata_native_provider_Release(vlc_metadata_native_provider_t *provider)
{
    if (!vlc_atomic_rc_dec(&provider->refs)) return;
    assert(provider->fetcher == NULL && provider->object == NULL && !provider->calls
        && !provider->retained && !provider->reserved);
    free(provider);
}
vlc_metadata_native_provider_t *vlc_metadata_native_provider_New(vlc_object_t *parent)
{
    if (parent == NULL) return NULL;
    vlc_metadata_native_provider_t *provider = calloc(1, sizeof(*provider));
    if (provider == NULL) return NULL;
    vlc_atomic_rc_init(&provider->refs); vlc_mutex_init(&provider->lock); vlc_cond_init(&provider->idle);
    provider->object = vlc_object_create(parent, sizeof(vlc_object_t));
    if (provider->object != NULL)
        provider->fetcher = input_fetcher_New(provider->object, VLC_PREPARSER_TYPE_FETCHMETA_NET);
    if (provider->fetcher == NULL) {
        if (provider->object != NULL) vlc_object_delete(provider->object);
        provider->object = NULL; vlc_metadata_native_provider_Release(provider); return NULL;
    }
    return provider;
}
static input_fetcher_t *begin_call(vlc_metadata_native_provider_t *provider, bool require_open)
{
    vlc_mutex_lock(&provider->lock);
    input_fetcher_t *fetcher = !provider->deleting && (!require_open || !provider->closed)
        ? provider->fetcher : NULL;
    if (fetcher != NULL) { input_fetcher_Hold(fetcher); ++provider->calls; }
    vlc_mutex_unlock(&provider->lock); return fetcher;
}
static void end_call(vlc_metadata_native_provider_t *provider, input_fetcher_t *fetcher)
{
    input_fetcher_Release(fetcher);
    vlc_mutex_lock(&provider->lock); assert(provider->calls); --provider->calls;
    vlc_cond_broadcast(&provider->idle); vlc_mutex_unlock(&provider->lock);
}
void vlc_metadata_native_provider_Close(vlc_metadata_native_provider_t *provider)
{
    vlc_metadata_native_provider_Hold(provider);
    vlc_metadata_job_t *jobs[MAX_PROBES]; unsigned count = 0;
    vlc_mutex_lock(&provider->lock); const bool first = !provider->closed; provider->closed = true;
    input_fetcher_t *fetcher = first ? provider->fetcher : NULL;
    if (fetcher != NULL) { input_fetcher_Hold(fetcher); ++provider->calls; }
    if (first) for (struct native_probe *probe = provider->head; probe != NULL; probe = probe->next) {
        assert(count < MAX_PROBES); jobs[count++] = vlc_metadata_job_Hold(probe->snapshot.job);
    }
    vlc_mutex_unlock(&provider->lock);
    // A listed request can still be before fetcher submission. Native job
    // cancellation seals that gap before any delayed submission can start.
    for (unsigned i = 0; i < count; ++i) { vlc_metadata_job_Cancel(jobs[i]); vlc_metadata_job_Release(jobs[i]); }
    if (fetcher != NULL) { input_fetcher_Cancel(fetcher, NULL); end_call(provider, fetcher); }
    vlc_metadata_native_provider_Release(provider);
}
void vlc_metadata_native_provider_Delete(vlc_metadata_native_provider_t *provider)
{
    vlc_metadata_native_provider_Close(provider);
    vlc_mutex_lock(&provider->lock);
    assert(!provider->deleting); provider->deleting = true;
    while (provider->calls) vlc_cond_wait(&provider->idle, &provider->lock);
    input_fetcher_t *fetcher = provider->fetcher; provider->fetcher = NULL;
    vlc_mutex_unlock(&provider->lock);
    input_fetcher_Delete(fetcher);
    vlc_mutex_lock(&provider->lock);
    while (provider->retained) vlc_cond_wait(&provider->idle, &provider->lock);
    vlc_object_t *object = provider->object; provider->object = NULL;
    vlc_mutex_unlock(&provider->lock);
    vlc_object_delete(object); vlc_metadata_native_provider_Release(provider);
}
void vlc_metadata_native_provider_Cancel(vlc_metadata_native_provider_t *provider, vlc_metadata_job_t *job)
{
    if (job == NULL) return;
    vlc_metadata_native_provider_Hold(provider);
    vlc_metadata_job_t *active = NULL;
    input_fetcher_t *fetcher = NULL;
    vlc_mutex_lock(&provider->lock);
    for (struct native_probe *probe = !provider->deleting ? provider->head : NULL; probe != NULL; probe = probe->next)
        if (probe->snapshot.job == job) { active = vlc_metadata_job_Hold(job); break; }
    if (active != NULL && provider->fetcher != NULL) {
        fetcher = input_fetcher_Hold(provider->fetcher); ++provider->calls;
    }
    vlc_mutex_unlock(&provider->lock);
    if (active != NULL) vlc_metadata_job_Cancel(active);
    if (fetcher != NULL) input_fetcher_Cancel(fetcher, job);
    if (active != NULL) vlc_metadata_job_Release(active);
    if (fetcher != NULL) end_call(provider, fetcher);
    vlc_metadata_native_provider_Release(provider);
}
static void probe_release(struct native_probe *probe)
{
    if (!vlc_atomic_rc_dec(&probe->refs)) return;
    vlc_metadata_native_provider_t *provider = probe->provider;
    struct native_reservation *reservation = probe->reservation;
    const bool counted = probe->listed;
    vlc_mutex_lock(&provider->lock);
    if (counted) {
        struct native_probe **link = &provider->head;
        while (*link != probe) link = &(*link)->next;
        *link = probe->next;
    }
    vlc_mutex_unlock(&provider->lock);
    snapshot_clear(&probe->snapshot); free(probe);
    vlc_mutex_lock(&provider->lock);
    if (counted) { assert(provider->retained); --provider->retained; }
    vlc_cond_broadcast(&provider->idle); vlc_mutex_unlock(&provider->lock);
    reservation_release(reservation);
    vlc_metadata_native_provider_Release(provider);
}
static vlc_metadata_native_claim_t *claim_new(struct native_probe *probe,
                                             meta_fetcher_outcome_t outcome, char *artwork)
{
    vlc_metadata_native_claim_t *claim = calloc(1, sizeof(*claim));
    if (claim == NULL || !snapshot_copy(&claim->snapshot, &probe->snapshot)) {
        free(claim); return NULL;
    }
    vlc_atomic_rc_init(&claim->refs); claim->family = probe->family;
    claim->artwork = artwork; claim->outcome = outcome;
    claim->provider = vlc_metadata_native_provider_Hold(probe->provider);
    claim->reservation = reservation_hold(probe->reservation);
    return claim;
}
static void probe_ended(input_item_t *item, int status, void *opaque)
{
    struct native_probe *probe = opaque;
    vlc_mutex_lock(&probe->provider->lock); const bool closed = probe->provider->closed;
    vlc_mutex_unlock(&probe->provider->lock);
    vlc_metadata_native_claim_t *claim = NULL;
    if (closed || !snapshot_current(&probe->snapshot, probe->snapshot.job)) status = -EINTR;
    if (status == VLC_SUCCESS) {
        char *artwork = NULL;
        vlc_mutex_lock(&item->lock);
        const char *raw = item->p_meta != NULL ? vlc_meta_Get(item->p_meta, vlc_meta_ArtworkURL) : NULL;
        if (raw == NULL || !raw[0]) status = VLC_ENOENT;
        else if (strnlen(raw, MAX_ARTWORK_URI + 1) > MAX_ARTWORK_URI
            || (strncasecmp(raw, "http://", 7) && strncasecmp(raw, "https://", 8))) status = VLC_EACCES;
        else if ((artwork = strdup(raw)) == NULL) status = VLC_ENOMEM;
        vlc_mutex_unlock(&item->lock);
        if (status == VLC_SUCCESS) {
            claim = claim_new(probe, META_FETCHER_OUTCOME_MATCH, artwork);
            if (claim == NULL) { free(artwork); status = VLC_ENOMEM; }
        }
    }
    probe->callback(claim, status, probe->opaque);
    vlc_metadata_native_claim_Release(claim); probe_release(probe);
}
static void probe_outcome_ended(input_item_t *item, int status,
                               meta_fetcher_outcome_t outcome, void *opaque)
{
    struct native_probe *probe = opaque;
    vlc_mutex_lock(&probe->provider->lock); const bool closed = probe->provider->closed;
    vlc_mutex_unlock(&probe->provider->lock);
    vlc_metadata_native_claim_t *claim = NULL;
    char *artwork = NULL;
    if (closed || !snapshot_current(&probe->snapshot, probe->snapshot.job)) status = -EINTR;
    if (status != -EINTR && status != VLC_ENOMEM) {
        if (outcome < META_FETCHER_OUTCOME_UNKNOWN || outcome > META_FETCHER_OUTCOME_ERROR)
            outcome = META_FETCHER_OUTCOME_ERROR;
        vlc_mutex_lock(&item->lock);
        const char *raw = item->p_meta != NULL ? vlc_meta_Get(item->p_meta, vlc_meta_ArtworkURL) : NULL;
        if (outcome == META_FETCHER_OUTCOME_MATCH) {
            if (status != VLC_SUCCESS || raw == NULL || !raw[0]
                || strnlen(raw, MAX_ARTWORK_URI + 1) > MAX_ARTWORK_URI
                || (strncasecmp(raw, "http://", 7) && strncasecmp(raw, "https://", 8)))
                outcome = META_FETCHER_OUTCOME_ERROR;
            else if ((artwork = strdup(raw)) == NULL) status = VLC_ENOMEM;
        } else if (raw != NULL && raw[0]) outcome = META_FETCHER_OUTCOME_ERROR;
        vlc_mutex_unlock(&item->lock);
        if (status != VLC_ENOMEM) {
            claim = claim_new(probe, outcome, artwork);
            if (claim != NULL) { artwork = NULL; status = VLC_SUCCESS; }
            else status = VLC_ENOMEM;
        }
    }
    free(artwork);
    probe->callback(claim, status, probe->opaque);
    vlc_metadata_native_claim_Release(claim); probe_release(probe);
}
static int probe_start(vlc_metadata_native_provider_t *provider, vlc_metadata_job_t *job,
    meta_fetcher_provider_t family, vlc_metadata_native_probe_cb callback, void *opaque, bool outcome)
{
    if (provider == NULL || job == NULL || callback == NULL || family < META_FETCHER_PROVIDER_MUSICBRAINZ
        || family > META_FETCHER_PROVIDER_LASTFM) return VLC_EINVAL;
    input_fetcher_t *fetcher = begin_call(provider, true);
    if (fetcher == NULL) return -EINTR;
    struct native_probe *probe = calloc(1, sizeof(*probe));
    if (probe == NULL) { end_call(provider, fetcher); return VLC_ENOMEM; }
    if (!snapshot_capture(&probe->snapshot, job)) {
        snapshot_clear(&probe->snapshot); free(probe); end_call(provider, fetcher); return -EACCES;
    }
    vlc_atomic_rc_init(&probe->refs); vlc_atomic_rc_inc(&probe->refs);
    probe->provider = vlc_metadata_native_provider_Hold(provider); probe->family = family;
    probe->callback = callback; probe->opaque = opaque;
    probe->reservation = calloc(1, sizeof(*probe->reservation));
    if (probe->reservation == NULL) {
        probe_release(probe); probe_release(probe); end_call(provider, fetcher); return VLC_ENOMEM;
    }
    vlc_atomic_rc_init(&probe->reservation->refs);
    probe->reservation->provider = vlc_metadata_native_provider_Hold(provider);
    vlc_mutex_lock(&provider->lock);
    const bool admitted = !provider->closed && !provider->deleting && provider->reserved < MAX_PROBES;
    const int refusal = provider->reserved >= MAX_PROBES ? VLC_ENOMEM : -EINTR;
    if (admitted) {
        ++provider->reserved; probe->reservation->counted = true;
        ++provider->retained; probe->next = provider->head; provider->head = probe; probe->listed = true;
    }
    vlc_mutex_unlock(&provider->lock);
    if (!admitted) { probe_release(probe); probe_release(probe); end_call(provider, fetcher); return refusal; }
    const int status = outcome ? input_fetcher_ProbeOutcome(fetcher, job, family, probe_outcome_ended, probe)
        : input_fetcher_Probe(fetcher, job, family, probe_ended, probe);
    if (status != VLC_SUCCESS) probe_release(probe);
    probe_release(probe); end_call(provider, fetcher); return status;
}
int vlc_metadata_native_provider_Probe(vlc_metadata_native_provider_t *provider,
    vlc_metadata_job_t *job, meta_fetcher_provider_t family, vlc_metadata_native_probe_cb callback, void *opaque)
{ return probe_start(provider, job, family, callback, opaque, false); }
int vlc_metadata_native_provider_ProbeOutcome(vlc_metadata_native_provider_t *provider,
    vlc_metadata_job_t *job, meta_fetcher_provider_t family, vlc_metadata_native_probe_cb callback, void *opaque)
{ return probe_start(provider, job, family, callback, opaque, true); }
