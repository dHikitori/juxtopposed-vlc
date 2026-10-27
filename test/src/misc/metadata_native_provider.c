// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <vlc/vlc.h>
#include <vlc_common.h>
#include <vlc_atomic.h>
#include <vlc_input_item.h>
#include <vlc_input_policy.h>
#include <vlc_metadata_backend.h>
#include <vlc_threads.h>
#include "lib/libvlc_internal.h"
#include "preparser/fetcher.h"
#include "preparser/metadata_native_provider.h"

/* The wrapper uses real Core objects, jobs, snapshots, and revision tokens.
 * This controlled fetcher replaces only the native provider execution boundary.
 * The real fetcher and Lua suites prove that boundary separately.
 */
struct pending { input_item_t *item; vlc_metadata_job_t *job; input_fetcher_probe_cb callback; void *opaque; };
struct input_fetcher_t {
    vlc_atomic_rc_t refs;
    vlc_mutex_t lock;
    struct pending pending[32];
    unsigned count;
};
enum mode { INLINE, QUEUED, BEFORE_SUBMIT };
static enum mode mode;
static const char *artwork = "http://127.0.0.1/native-family.png";
static int provider_status = VLC_SUCCESS;
static atomic_uint probes, fetchers_deleted;
static vlc_sem_t before_submit, allow_submit;
input_fetcher_t *input_fetcher_New(vlc_object_t *object, int options)
{
    assert(object != NULL && options == VLC_PREPARSER_TYPE_FETCHMETA_NET);
    input_fetcher_t *fetcher = calloc(1, sizeof(*fetcher)); assert(fetcher);
    vlc_atomic_rc_init(&fetcher->refs); vlc_mutex_init(&fetcher->lock); return fetcher;
}
input_fetcher_t *input_fetcher_Hold(input_fetcher_t *fetcher)
{ vlc_atomic_rc_inc(&fetcher->refs); return fetcher; }
void input_fetcher_Release(input_fetcher_t *fetcher)
{ if (vlc_atomic_rc_dec(&fetcher->refs)) { assert(fetcher->count == 0); free(fetcher); } }
static void pending_end(struct pending pending, int status)
{
    pending.callback(pending.item, status, pending.opaque);
    input_item_Release(pending.item); vlc_metadata_job_Release(pending.job);
}
int input_fetcher_Probe(input_fetcher_t *fetcher, vlc_metadata_job_t *job,
    meta_fetcher_provider_t family, input_fetcher_probe_cb callback, void *opaque)
{
    assert(family >= META_FETCHER_PROVIDER_MUSICBRAINZ && family <= META_FETCHER_PROVIDER_LASTFM);
    ++probes;
    if (mode == BEFORE_SUBMIT) { vlc_sem_post(&before_submit); vlc_sem_wait(&allow_submit); }
    if (!vlc_metadata_job_IsPending(job)) return -EINTR;
    struct pending pending = {.item=input_item_Copy(vlc_metadata_job_GetRequest(job)->item),
        .job=vlc_metadata_job_Hold(job), .callback=callback, .opaque=opaque};
    assert(pending.item); input_item_SetArtURL(pending.item, artwork);
    if (mode == INLINE) pending_end(pending, provider_status);
    else {
        vlc_mutex_lock(&fetcher->lock); assert(fetcher->count < ARRAY_SIZE(fetcher->pending));
        fetcher->pending[fetcher->count++] = pending; vlc_mutex_unlock(&fetcher->lock);
    }
    return VLC_SUCCESS;
}
size_t input_fetcher_Cancel(input_fetcher_t *fetcher, const void *key)
{
    struct pending canceled[32]; unsigned count = 0;
    vlc_mutex_lock(&fetcher->lock);
    for (unsigned i = 0; i < fetcher->count;)
        if (key == NULL || key == fetcher->pending[i].job) {
            canceled[count++] = fetcher->pending[i];
            fetcher->pending[i] = fetcher->pending[--fetcher->count];
        } else ++i;
    vlc_mutex_unlock(&fetcher->lock);
    for (unsigned i = 0; i < count; ++i) pending_end(canceled[i], -EINTR);
    return count;
}
void input_fetcher_Delete(input_fetcher_t *fetcher)
{ input_fetcher_Cancel(fetcher, NULL); ++fetchers_deleted; input_fetcher_Release(fetcher); }

struct job { vlc_metadata_backend_t *backend; vlc_metadata_job_t *job; input_item_t *item; unsigned ended; };
static int dispatch(void *opaque, vlc_metadata_job_t *job) { (void)opaque; (void)job; return VLC_SUCCESS; }
static void ended(vlc_metadata_job_t *job, int status, void *opaque)
{ (void)job; (void)status; ++((struct job *)opaque)->ended; }
static void job_init(struct job *job, const char *uri, unsigned flags)
{
    memset(job, 0, sizeof(*job));
    const struct vlc_metadata_backend_operations operations = {.dispatch=dispatch};
    job->backend = vlc_metadata_backend_New(&operations, NULL); assert(job->backend);
    job->item = input_item_New(uri, "Native item"); assert(job->item);
    input_item_SetArtist(job->item, "Native artist"); input_item_SetAlbum(job->item, "Native album");
    const char *fields[] = {"artwork"};
    const struct vlc_metadata_request request = {.feature="artwork", .operation="enrich",
        .item=job->item, .fields=fields, .field_count=1, .generation=17, .target_revision=23, .flags=flags};
    job->job = vlc_metadata_job_New(job->backend, &request, ended, job); assert(job->job);
    assert(vlc_metadata_job_Start(job->job) == VLC_SUCCESS);
}
static void job_clear(struct job *job)
{
    vlc_metadata_backend_Close(job->backend); vlc_metadata_job_Release(job->job);
    vlc_metadata_backend_Release(job->backend); input_item_Release(job->item);
}
struct completion { unsigned count; int status; vlc_metadata_native_claim_t *claim; bool close; vlc_metadata_native_provider_t *provider; };
static void complete(vlc_metadata_native_claim_t *claim, int status, void *opaque)
{
    struct completion *completion = opaque; ++completion->count; completion->status = status;
    if (claim != NULL) completion->claim = vlc_metadata_native_claim_Hold(claim);
    if (completion->close) vlc_metadata_native_provider_Close(completion->provider);
}
static void ordinary_claims(vlc_object_t *object)
{
    mode = INLINE;
    for (unsigned mutation = 0; mutation < 4; ++mutation) {
        struct job job, other; job_init(&job, "file:///native/item.flac", VLC_METADATA_REQUEST_NETWORK);
        job_init(&other, "file:///native/item.flac", VLC_METADATA_REQUEST_NETWORK);
        vlc_metadata_native_provider_t *provider = vlc_metadata_native_provider_New(object); assert(provider);
        struct completion completion = {0};
        assert(vlc_metadata_native_provider_Probe(provider, job.job, META_FETCHER_PROVIDER_MUSICBRAINZ, complete, &completion) == VLC_SUCCESS);
        assert(completion.count == 1 && completion.status == VLC_SUCCESS && completion.claim != NULL);
        assert(vlc_metadata_native_claim_GetFamily(completion.claim) == META_FETCHER_PROVIDER_MUSICBRAINZ);
        assert(!strcmp(vlc_metadata_native_claim_GetArtworkURI(completion.claim), "http://127.0.0.1/native-family.png"));
        assert(vlc_metadata_native_claim_MatchesJob(completion.claim, job.job));
        assert(!vlc_metadata_native_claim_MatchesJob(completion.claim, other.job));
        vlc_metadata_native_provider_Cancel(provider, other.job);
        assert(vlc_metadata_job_IsPending(other.job));
        input_item_t *snapshot = vlc_metadata_job_GetRequest(job.job)->item;
        if (mutation == 0) input_item_SetArtist(snapshot, "Native artist");
        else if (mutation == 1) {
            input_item_SetURI(snapshot, "file:///native/other.flac"); input_item_SetURI(snapshot, "file:///native/item.flac");
        } else if (mutation == 2) input_item_SetName(snapshot, "Native item");
        else {
            vlc_metadata_native_provider_Close(provider);
        }
        assert(!vlc_metadata_native_claim_MatchesJob(completion.claim, job.job));
        vlc_metadata_native_provider_Delete(provider);
        assert(!vlc_metadata_native_claim_MatchesJob(completion.claim, job.job));
        vlc_metadata_native_claim_Release(completion.claim); job_clear(&other); job_clear(&job);
    }
    puts("PASS: exact native claims and snapshot revisions");
}
static void restricted_admission(vlc_object_t *object)
{
    mode = INLINE; const unsigned before = probes;
    for (unsigned kind = 0; kind < 4; ++kind) {
        struct job job; job_init(&job, kind == 1 ? "HaKoNe-AdDoN-SoUrCe:import" :
            kind == 2 ? "VLC-ADDON-SOURCE:legacy" : "file:///native/item.flac",
            kind == 0 ? 0 : VLC_METADATA_REQUEST_NETWORK);
        if (kind == 3) {
            vlc_input_policy_t *policy = vlc_input_policy_NewProxy(0, NULL, "file:///native/item.flac", false); assert(policy);
            assert(vlc_input_policy_AttachItem(vlc_metadata_job_GetRequest(job.job)->item, policy) == VLC_SUCCESS);
            vlc_input_policy_Release(policy);
        }
        vlc_metadata_native_provider_t *provider = vlc_metadata_native_provider_New(object); assert(provider);
        struct completion completion = {0};
        assert(vlc_metadata_native_provider_Probe(provider, job.job, META_FETCHER_PROVIDER_LASTFM, complete, &completion) != VLC_SUCCESS);
        assert(completion.count == 0 && probes == before);
        assert(vlc_metadata_native_provider_Probe(provider, job.job, META_FETCHER_PROVIDER_ANY, complete, &completion) == VLC_EINVAL);
        vlc_metadata_native_provider_Delete(provider); job_clear(&job);
    }
    puts("PASS: scoped and offline admission");
}
static void result_admission(vlc_object_t *object)
{
    mode = INLINE; char boundary[8194]; memset(boundary, 'a', sizeof(boundary));
    memcpy(boundary, "https://example.invalid/", sizeof("https://example.invalid/") - 1);
    for (unsigned row = 0; row < 6; ++row) {
        struct job job; job_init(&job, "file:///native/item.flac", VLC_METADATA_REQUEST_NETWORK);
        vlc_metadata_native_provider_t *provider = vlc_metadata_native_provider_New(object); assert(provider);
        provider_status = row == 0 ? VLC_ENOENT : row == 1 ? VLC_EGENERIC : VLC_SUCCESS;
        artwork = row == 2 ? "" : row == 3 ? "file:///private/not-a-family-asset.png" : "https://example.invalid/art.png";
        if (row >= 4) { boundary[row == 4 ? 8193 : 8192] = '\0'; artwork = boundary; }
        struct completion completion = {0};
        assert(vlc_metadata_native_provider_Probe(provider, job.job, META_FETCHER_PROVIDER_LASTFM, complete, &completion) == VLC_SUCCESS);
        const int expected = row == 0 || row == 2 ? VLC_ENOENT : row == 1 ? VLC_EGENERIC : row < 5 ? VLC_EACCES : VLC_SUCCESS;
        assert(completion.count == 1 && completion.status == expected);
        assert((completion.claim != NULL) == (row == 5));
        if (completion.claim != NULL) {
            assert(strlen(vlc_metadata_native_claim_GetArtworkURI(completion.claim)) == 8192);
            vlc_metadata_native_claim_Release(completion.claim);
        }
        vlc_metadata_native_provider_Delete(provider); job_clear(&job);
    }
    artwork = "http://127.0.0.1/native-family.png"; provider_status = VLC_SUCCESS;
    puts("PASS: provider errors and bounded native result claims");
}
static void close_and_quota(vlc_object_t *object)
{
    mode = QUEUED; struct job jobs[33]; struct completion results[33] = {{0}};
    vlc_metadata_native_provider_t *provider = vlc_metadata_native_provider_New(object); assert(provider);
    for (unsigned i = 0; i < ARRAY_SIZE(jobs); ++i) {
        job_init(&jobs[i], "file:///native/item.flac", VLC_METADATA_REQUEST_NETWORK);
        const int status = vlc_metadata_native_provider_Probe(provider, jobs[i].job,
            META_FETCHER_PROVIDER_FRENCH_TV, complete, &results[i]);
        assert(status == (i < 32 ? VLC_SUCCESS : VLC_ENOMEM));
    }
    vlc_metadata_native_provider_Close(provider);
    for (unsigned i = 0; i < 32; ++i) {
        assert(results[i].count == 1 && results[i].status == -EINTR && results[i].claim == NULL);
        assert(jobs[i].ended == 1);
    }
    assert(results[32].count == 0); vlc_metadata_native_provider_Delete(provider);
    for (unsigned i = 0; i < ARRAY_SIZE(jobs); ++i) job_clear(&jobs[i]);
    mode = INLINE; struct job job; job_init(&job, "file:///native/item.flac", VLC_METADATA_REQUEST_NETWORK);
    provider = vlc_metadata_native_provider_New(object); assert(provider);
    struct completion result = {.close=true, .provider=provider};
    assert(vlc_metadata_native_provider_Probe(provider, job.job, META_FETCHER_PROVIDER_GOOGLE_IMAGES, complete, &result) == VLC_SUCCESS);
    assert(result.count == 1 && result.claim != NULL && !vlc_metadata_native_claim_MatchesJob(result.claim, job.job));
    vlc_metadata_native_provider_Delete(provider); vlc_metadata_native_claim_Release(result.claim); job_clear(&job);
    puts("PASS: bounded quota and close reentry");
}
struct submission { vlc_metadata_native_provider_t *provider; struct job *job; struct completion completion; int status; };
static void *submit_thread(void *opaque)
{
    struct submission *submission = opaque;
    submission->status = vlc_metadata_native_provider_Probe(submission->provider, submission->job->job,
        META_FETCHER_PROVIDER_MUSICBRAINZ, complete, &submission->completion); return NULL;
}
struct deletion { vlc_metadata_native_provider_t *provider; atomic_bool ended; vlc_sem_t entered, completed; };
static void *delete_thread(void *opaque)
{
    struct deletion *deletion = opaque; vlc_sem_post(&deletion->entered);
    vlc_metadata_native_provider_Delete(deletion->provider); atomic_store(&deletion->ended, true);
    vlc_sem_post(&deletion->completed); return NULL;
}
static void deletion_init(struct deletion *deletion, vlc_metadata_native_provider_t *provider)
{
    deletion->provider = provider; atomic_init(&deletion->ended, false);
    vlc_sem_init(&deletion->entered, 0); vlc_sem_init(&deletion->completed, 0);
}
static void listed_before_submit(vlc_object_t *object)
{
    mode = BEFORE_SUBMIT; vlc_sem_init(&before_submit, 0); vlc_sem_init(&allow_submit, 0);
    struct job job; job_init(&job, "file:///native/item.flac", VLC_METADATA_REQUEST_NETWORK);
    struct submission submission = {.provider=vlc_metadata_native_provider_New(object), .job=&job}; assert(submission.provider);
    vlc_thread_t worker, reaper; assert(vlc_clone(&worker, submit_thread, &submission) == 0);
    vlc_sem_wait(&before_submit); vlc_metadata_native_provider_Close(submission.provider);
    assert(job.ended == 1);
    struct deletion deletion; deletion_init(&deletion, submission.provider);
    assert(vlc_clone(&reaper, delete_thread, &deletion) == 0);
    vlc_sem_wait(&deletion.entered);
    assert(vlc_sem_timedwait(&deletion.completed, vlc_tick_now() + VLC_TICK_FROM_MS(50)) == ETIMEDOUT);
    vlc_sem_post(&allow_submit); vlc_join(worker, NULL); vlc_join(reaper, NULL);
    assert(submission.status == -EINTR && submission.completion.count == 0 && atomic_load(&deletion.ended));
    job_clear(&job); puts("PASS: listed before submission and joined deletion");
}
struct blocked_completion { vlc_sem_t entered, release; unsigned count; };
static void blocked_complete(vlc_metadata_native_claim_t *claim, int status, void *opaque)
{
    struct blocked_completion *completion = opaque;
    assert(claim == NULL && status == -EINTR); ++completion->count;
    vlc_sem_post(&completion->entered); vlc_sem_wait(&completion->release);
}
struct cancellation { vlc_metadata_native_provider_t *provider; vlc_metadata_job_t *job; };
static void *cancel_thread(void *opaque)
{
    struct cancellation *cancellation = opaque;
    vlc_metadata_native_provider_Cancel(cancellation->provider, cancellation->job); return NULL;
}
static void callback_drain(vlc_object_t *object)
{
    mode = QUEUED; struct job job; job_init(&job, "file:///native/item.flac", VLC_METADATA_REQUEST_NETWORK);
    vlc_metadata_native_provider_t *provider = vlc_metadata_native_provider_New(object); assert(provider);
    struct blocked_completion completion = {0};
    vlc_sem_init(&completion.entered, 0); vlc_sem_init(&completion.release, 0);
    assert(vlc_metadata_native_provider_Probe(provider, job.job, META_FETCHER_PROVIDER_LASTFM,
        blocked_complete, &completion) == VLC_SUCCESS);
    struct cancellation cancellation = {.provider=provider, .job=job.job}; vlc_thread_t canceler, reaper;
    assert(vlc_clone(&canceler, cancel_thread, &cancellation) == 0); vlc_sem_wait(&completion.entered);
    vlc_metadata_native_provider_Close(provider);
    struct deletion deletion; deletion_init(&deletion, provider);
    assert(vlc_clone(&reaper, delete_thread, &deletion) == 0);
    vlc_sem_wait(&deletion.entered);
    assert(vlc_sem_timedwait(&deletion.completed, vlc_tick_now() + VLC_TICK_FROM_MS(50)) == ETIMEDOUT);
    vlc_sem_post(&completion.release); vlc_join(canceler, NULL); vlc_join(reaper, NULL);
    assert(atomic_load(&deletion.ended) && completion.count == 1 && job.ended == 1);
    job_clear(&job); puts("PASS: cancel callback and native reference drain");
}
int main(void)
{
    const char *arguments[] = {"--ignore-config", "--quiet", "--no-media-library",
        "--no-plugins-scan", "--no-plugins-cache"};
    libvlc_instance_t *instance = libvlc_new(ARRAY_SIZE(arguments), arguments); assert(instance);
    vlc_object_t *object = VLC_OBJECT(instance->p_libvlc_int);
    ordinary_claims(object); restricted_admission(object); result_admission(object);
    close_and_quota(object); listed_before_submit(object); callback_drain(object);
    assert(fetchers_deleted == 18); libvlc_release(instance);
    return 0;
}
