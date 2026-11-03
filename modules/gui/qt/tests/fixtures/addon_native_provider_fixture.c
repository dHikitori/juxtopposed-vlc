// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <assert.h>
#include <stdatomic.h>
#include <vlc_common.h>
#include <vlc_atomic.h>
#include <vlc_input_item.h>
#include <vlc_metadata_backend.h>
#include <vlc_threads.h>
#include "preparser/fetcher.h"
#include "addon_native_provider_fixture.h"

// This fixture replaces the native family's execution only. The target also
// compiles the real metadata_native_provider.c and uses real shared Core jobs,
// snapshots, revision tokens, and artwork records.
struct pending {
    input_item_t *item;
    vlc_metadata_job_t *job;
    input_fetcher_probe_cb callback;
    input_fetcher_probe_outcome_cb outcome_callback;
    meta_fetcher_outcome_t outcome;
    void *opaque;
};
struct input_fetcher_t {
    vlc_atomic_rc_t refs;
    vlc_mutex_t lock;
    struct pending pending[32];
    unsigned count;
};
static vlc_mutex_t fixture_lock = VLC_STATIC_MUTEX;
static vlc_cond_t fixture_changed = VLC_STATIC_COND;
static input_fetcher_t *current;
static atomic_bool queued, block_delete;
static atomic_uint deleted;
static atomic_bool outcomes_enabled;
static atomic_int outcome_value;
void addon_test_native_provider_queued(bool value) { atomic_store(&queued, value); }
void addon_test_native_provider_outcome(bool enabled, meta_fetcher_outcome_t value)
{ atomic_store(&outcome_value, value); atomic_store(&outcomes_enabled, enabled); }
void addon_test_native_provider_block_delete(bool value)
{
    vlc_mutex_lock(&fixture_lock); atomic_store(&block_delete, value);
    vlc_cond_broadcast(&fixture_changed); vlc_mutex_unlock(&fixture_lock);
}
unsigned addon_test_native_provider_deleted(void) { return atomic_load(&deleted); }
input_fetcher_t *input_fetcher_New(vlc_object_t *object, int options)
{
    assert(object && options == VLC_PREPARSER_TYPE_FETCHMETA_NET);
    input_fetcher_t *fetcher = calloc(1, sizeof(*fetcher)); assert(fetcher);
    vlc_atomic_rc_init(&fetcher->refs); vlc_mutex_init(&fetcher->lock);
    vlc_mutex_lock(&fixture_lock); assert(!current); current = fetcher; vlc_mutex_unlock(&fixture_lock);
    return fetcher;
}
input_fetcher_t *input_fetcher_Hold(input_fetcher_t *fetcher)
{ vlc_atomic_rc_inc(&fetcher->refs); return fetcher; }
void input_fetcher_Release(input_fetcher_t *fetcher)
{ if (vlc_atomic_rc_dec(&fetcher->refs)) { assert(!fetcher->count); free(fetcher); } }
static void ended(struct pending value, int status)
{
    if (value.outcome_callback) value.outcome_callback(value.item, status, value.outcome, value.opaque);
    else value.callback(value.item, status, value.opaque);
    input_item_Release(value.item); vlc_metadata_job_Release(value.job);
}
static int probe(input_fetcher_t *fetcher, vlc_metadata_job_t *job,
    meta_fetcher_provider_t family, input_fetcher_probe_cb callback,
    input_fetcher_probe_outcome_cb outcome_callback, void *opaque)
{
    assert(family >= META_FETCHER_PROVIDER_MUSICBRAINZ && family <= META_FETCHER_PROVIDER_LASTFM);
    if (!vlc_metadata_job_IsPending(job)) return -EINTR;
    struct pending value = {.item=input_item_Copy(vlc_metadata_job_GetRequest(job)->item),
        .job=vlc_metadata_job_Hold(job), .callback=callback, .outcome_callback=outcome_callback,
        .outcome=outcome_callback ? atomic_load(&outcome_value) : META_FETCHER_OUTCOME_MATCH, .opaque=opaque};
    assert(value.item);
    if (value.outcome == META_FETCHER_OUTCOME_MATCH) input_item_SetArtURL(value.item, "http://127.0.0.1/native-family.png");
    if (!atomic_load(&queued)) ended(value, VLC_SUCCESS);
    else {
        vlc_mutex_lock(&fetcher->lock); assert(fetcher->count < ARRAY_SIZE(fetcher->pending));
        fetcher->pending[fetcher->count++] = value; vlc_mutex_unlock(&fetcher->lock);
    }
    return VLC_SUCCESS;
}
int input_fetcher_Probe(input_fetcher_t *fetcher, vlc_metadata_job_t *job,
    meta_fetcher_provider_t family, input_fetcher_probe_cb callback, void *opaque)
{ return probe(fetcher, job, family, callback, NULL, opaque); }
/* Outcome tests opt in to a controlled closed result. These tests exercise the
 * native bridge, not the shipped family parser or its stream completion. */
int input_fetcher_ProbeOutcome(input_fetcher_t *fetcher, vlc_metadata_job_t *job,
    meta_fetcher_provider_t family, input_fetcher_probe_outcome_cb callback, void *opaque)
{
    if (!atomic_load(&outcomes_enabled)) return VLC_ENOTSUP;
    return probe(fetcher, job, family, NULL, callback, opaque);
}
static size_t finish(input_fetcher_t *fetcher, const void *key, int status)
{
    struct pending values[32]; unsigned count = 0;
    vlc_mutex_lock(&fetcher->lock);
    for (unsigned i = 0; i < fetcher->count;)
        if (!key || key == fetcher->pending[i].job) {
            values[count++] = fetcher->pending[i]; fetcher->pending[i] = fetcher->pending[--fetcher->count];
        } else ++i;
    vlc_mutex_unlock(&fetcher->lock);
    for (unsigned i = 0; i < count; ++i) ended(values[i], status);
    return count;
}
size_t input_fetcher_Cancel(input_fetcher_t *fetcher, const void *key) { return finish(fetcher, key, -EINTR); }
void addon_test_native_provider_complete(void)
{
    vlc_mutex_lock(&fixture_lock); input_fetcher_t *fetcher = current ? input_fetcher_Hold(current) : NULL; vlc_mutex_unlock(&fixture_lock);
    if (fetcher) { finish(fetcher, NULL, VLC_SUCCESS); input_fetcher_Release(fetcher); }
}
unsigned addon_test_native_provider_pending(void)
{
    vlc_mutex_lock(&fixture_lock);
    input_fetcher_t *fetcher = current ? input_fetcher_Hold(current) : NULL;
    vlc_mutex_unlock(&fixture_lock);
    if (!fetcher) return 0;
    vlc_mutex_lock(&fetcher->lock); const unsigned count = fetcher->count; vlc_mutex_unlock(&fetcher->lock);
    input_fetcher_Release(fetcher); return count;
}
void input_fetcher_Delete(input_fetcher_t *fetcher)
{
    input_fetcher_Cancel(fetcher, NULL);
    vlc_mutex_lock(&fixture_lock);
    while (atomic_load(&block_delete)) vlc_cond_wait(&fixture_changed, &fixture_lock);
    assert(current == fetcher); current = NULL; vlc_mutex_unlock(&fixture_lock);
    ++deleted; input_fetcher_Release(fetcher);
}
