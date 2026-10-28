// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <vlc_common.h>
#include <vlc_atomic.h>
#include <vlc_artwork_policy.h>
#include <vlc_metadata_backend.h>
#include <vlc_input_policy.h>
#include <vlc_metadata_owner.h>
#include <vlc_threads.h>
#include <stdatomic.h>
#include "metadata_pipeline.h"

#define MAX_ENTRIES 256u
#define MAX_PROVENANCE (1024u * 1024u)
#define STAGE_COUNT 5u
struct pipeline_entry;
struct vlc_metadata_pipeline_stage {
    vlc_atomic_rc_t refs;
    struct pipeline_entry *entry;
    atomic_bool cancelled, done;
};
struct vlc_metadata_pipeline {
    vlc_atomic_rc_t refs;
    vlc_object_t *instance;
    struct vlc_metadata_pipeline_operations ops;
    void *stage_context;
    vlc_metadata_backend_t *local_backend;
    vlc_mutex_t lock;
    vlc_cond_t idle;
    struct pipeline_entry *entries;
    unsigned retained, pending_callbacks, user_callbacks;
    uint64_t generation;
    bool closed, deleting;
};
struct pipeline_entry {
    vlc_atomic_rc_t refs;
    vlc_metadata_pipeline_t *owner;
    struct pipeline_entry *next;
    input_item_t *target, *working;
    vlc_input_policy_t *policy;
    vlc_metadata_pipeline_guard_t *parent_guard;
    char *source_uri;
    vlc_metadata_backend_t *backend;
    vlc_metadata_job_t *anchor, *child;
    vlc_metadata_pipeline_stage_t *local;
    const void *key;
    vlc_metadata_pipeline_ended_cb ended;
    vlc_metadata_pipeline_request_cb request_ended;
    struct vlc_metadata_local_events events;
    void *userdata;
    uint64_t generation;
    unsigned stage;
    int options, status;
    bool listed, retained, terminal, notified, native_artwork_replace, key_held;
    bool request_mode, remote_owned, legacy_allowed, local_prepared;
    char *provenance[STAGE_COUNT];
    size_t provenance_sizes[STAGE_COUNT], provenance_bytes;
};
struct remote_call { struct pipeline_entry *entry; unsigned stage; };
struct pipeline_frame {
    vlc_metadata_pipeline_t *owner;
    struct pipeline_frame *previous;
    bool provider;
};
static _Thread_local struct pipeline_frame *callback_frame;
static void entry_finish(struct pipeline_entry *, int);
static void entry_next(struct pipeline_entry *);

static vlc_metadata_pipeline_t *owner_hold(vlc_metadata_pipeline_t *owner)
{ vlc_atomic_rc_inc(&owner->refs); return owner; }
static void owner_release(vlc_metadata_pipeline_t *owner)
{
    if (!vlc_atomic_rc_dec(&owner->refs)) return;
    assert(owner->entries == NULL && owner->retained == 0 && owner->pending_callbacks == 0 && owner->user_callbacks == 0);
    vlc_metadata_backend_Release(owner->local_backend); free(owner);
}
static struct pipeline_entry *entry_hold(struct pipeline_entry *entry)
{ vlc_atomic_rc_inc(&entry->refs); return entry; }
static void entry_release(struct pipeline_entry *entry)
{
    if (!vlc_atomic_rc_dec(&entry->refs)) return;
    vlc_metadata_pipeline_t *owner = entry->owner;
    assert(!entry->listed && entry->local == NULL && entry->child == NULL);
    if (entry->anchor != NULL) vlc_metadata_job_Release(entry->anchor);
    if (entry->backend != NULL) vlc_metadata_backend_Release(entry->backend);
    if (entry->working != NULL) input_item_Release(entry->working);
    if (entry->target != NULL) input_item_Release(entry->target);
    vlc_input_policy_Release(entry->policy); free(entry->source_uri);
    vlc_metadata_pipeline_guard_Release(entry->parent_guard);
    vlc_metadata_local_events_Clear(&entry->events);
    for (unsigned i = 0; i < STAGE_COUNT; ++i) free(entry->provenance[i]);
    vlc_mutex_lock(&owner->lock);
    if (entry->retained) --owner->retained;
    vlc_cond_broadcast(&owner->idle); vlc_mutex_unlock(&owner->lock);
    const void *key = entry->key; const bool key_held = entry->key_held;
    free(entry);
    if (key_held) owner->ops.release_key(owner->stage_context, key);
    owner_release(owner);
}
static bool entry_current(struct pipeline_entry *entry)
{
    vlc_mutex_lock(&entry->owner->lock);
    const bool current = !entry->owner->closed && !entry->terminal;
    vlc_mutex_unlock(&entry->owner->lock);
    if (!current || !vlc_metadata_pipeline_guard_IsCurrent(entry->parent_guard) || vlc_input_policy_IsRevoked(entry->policy) || !vlc_metadata_job_IsPending(entry->anchor)) return false;
    char *uri = input_item_GetURI(entry->working);
    vlc_input_policy_t *policy = vlc_input_policy_HoldItem(entry->working);
    const bool same = policy == entry->policy && (uri == NULL ? entry->source_uri == NULL
        : entry->source_uri != NULL && strcmp(uri, entry->source_uri) == 0);
    free(uri); vlc_input_policy_Release(policy); return same;
}
static void pending_add(vlc_metadata_pipeline_t *owner)
{ vlc_mutex_lock(&owner->lock); ++owner->pending_callbacks; vlc_mutex_unlock(&owner->lock); }
static void pending_end(vlc_metadata_pipeline_t *owner)
{
    vlc_mutex_lock(&owner->lock); assert(owner->pending_callbacks > 0); --owner->pending_callbacks;
    vlc_cond_broadcast(&owner->idle); vlc_mutex_unlock(&owner->lock);
}
vlc_metadata_pipeline_stage_t *vlc_metadata_pipeline_stage_Hold(vlc_metadata_pipeline_stage_t *stage)
{ vlc_atomic_rc_inc(&stage->refs); return stage; }
void vlc_metadata_pipeline_stage_Release(vlc_metadata_pipeline_stage_t *stage)
{
    if (!vlc_atomic_rc_dec(&stage->refs)) return;
    entry_release(stage->entry); free(stage);
}
input_item_t *vlc_metadata_pipeline_stage_GetItem(vlc_metadata_pipeline_stage_t *stage)
{ return stage->entry->working; }
const void *vlc_metadata_pipeline_stage_GetKey(vlc_metadata_pipeline_stage_t *stage)
{ return stage->entry->key; }
int vlc_metadata_pipeline_stage_GetOptions(vlc_metadata_pipeline_stage_t *stage)
{ return stage->entry->options; }
bool vlc_metadata_pipeline_stage_LegacyAllowed(vlc_metadata_pipeline_stage_t *stage)
{ return stage->entry->legacy_allowed && (stage->entry->options & VLC_PREPARSER_TYPE_FETCHMETA_NET); }
int vlc_metadata_pipeline_stage_PrepareLocal(vlc_metadata_pipeline_stage_t *stage,
    const struct vlc_metadata_local_events *events)
{
    if (!vlc_metadata_pipeline_stage_IsCurrent(stage)) return -EINTR;
    struct pipeline_entry *entry = stage->entry;
    const int status = vlc_metadata_job_PrepareLocal(entry->anchor, entry->working, events);
    if (status == VLC_SUCCESS) entry->local_prepared = true;
    return status;
}
bool vlc_metadata_pipeline_stage_IsCurrent(vlc_metadata_pipeline_stage_t *stage)
{ return !atomic_load(&stage->cancelled) && !atomic_load(&stage->done) && entry_current(stage->entry); }
static bool remove_entry_locked(struct pipeline_entry *entry)
{
    vlc_metadata_pipeline_t *owner = entry->owner;
    if (!entry->listed) return false;
    struct pipeline_entry **link = &owner->entries;
    while (*link != entry) link = &(*link)->next;
    *link = entry->next; entry->listed = false; return true;
}
static void entry_finish(struct pipeline_entry *entry, int status)
{
    entry_hold(entry); vlc_metadata_pipeline_t *owner = entry->owner;
    vlc_mutex_lock(&owner->lock);
    status = vlc_metadata_pipeline_guard_Finish(entry->parent_guard, status);
    if (!entry->terminal) { entry->terminal = true; entry->status = status; }
    vlc_metadata_pipeline_stage_t *local = entry->local != NULL ? vlc_metadata_pipeline_stage_Hold(entry->local) : NULL;
    vlc_metadata_job_t *child = entry->child != NULL ? vlc_metadata_job_Hold(entry->child) : NULL;
    vlc_metadata_job_t *anchor = entry->anchor != NULL ? vlc_metadata_job_Hold(entry->anchor) : NULL;
    vlc_mutex_unlock(&owner->lock);
    // Keep terminal entries listed through native cancellation and notification.
    // A reentrant Close can finish a callback that has not started yet.
    if (local != NULL) {
        atomic_store(&local->cancelled, true);
        if (!atomic_load(&local->done)) owner->ops.cancel_local(owner->stage_context, local);
        vlc_metadata_pipeline_stage_Release(local);
    }
    if (child != NULL) { vlc_metadata_job_Cancel(child); vlc_metadata_job_Release(child); }
    if (anchor != NULL) { vlc_metadata_job_Cancel(anchor); vlc_metadata_job_Release(anchor); }
    vlc_mutex_lock(&owner->lock);
    const bool notify = !entry->notified;
    if (notify) { entry->notified = true; ++owner->user_callbacks; }
    vlc_mutex_unlock(&owner->lock);
    if (notify) {
        struct pipeline_frame frame = {owner, callback_frame, false}; callback_frame = &frame;
        if (entry->request_ended != NULL) entry->request_ended(entry->target, entry->status, &entry->events, entry->userdata);
        else entry->ended(entry->target, entry->status, entry->userdata);
        callback_frame = frame.previous;
        vlc_mutex_lock(&owner->lock); --owner->user_callbacks;
        vlc_cond_broadcast(&owner->idle); vlc_mutex_unlock(&owner->lock);
    }
    vlc_mutex_lock(&owner->lock); const bool removed = remove_entry_locked(entry); vlc_mutex_unlock(&owner->lock);
    if (removed) entry_release(entry);
    entry_release(entry);
}
static void anchor_ended(vlc_metadata_job_t *job, int status, void *opaque)
{
    (void)job; struct pipeline_entry *entry = opaque; vlc_metadata_pipeline_t *owner = entry->owner;
    struct pipeline_frame frame = {owner, callback_frame, true}; callback_frame = &frame;
    if (status != VLC_SUCCESS) entry_finish(entry, status);
    callback_frame = frame.previous; pending_end(owner); entry_release(entry);
}
static bool retain_provenance(struct pipeline_entry *entry, unsigned stage, const struct vlc_metadata_result *result)
{
    if (result == NULL || result->provenance_size == 0) return true;
    const size_t size = result->provenance_size;
    if (size > MAX_PROVENANCE || entry->provenance_bytes > MAX_PROVENANCE - size) return false;
    char *copy = malloc(size + 1); if (copy == NULL) return false;
    memcpy(copy, result->provenance_json, size); copy[size] = '\0';
    entry->provenance[stage] = copy; entry->provenance_sizes[stage] = size; entry->provenance_bytes += size; return true;
}
static char *provenance(struct pipeline_entry *entry, size_t *size)
{
    size_t needed = sizeof("{\"stages\":[]}") - 1;
    for (unsigned i = 0; i < STAGE_COUNT; ++i) {
        if (entry->provenance[i] == NULL) continue;
        needed += 3;
        for (size_t j = 0; j < entry->provenance_sizes[i]; ++j) {
            const unsigned char value = entry->provenance[i][j];
            const size_t bytes = value < 32 ? 6 : value == '"' || value == '\\' ? 2 : 1;
            if (needed > MAX_PROVENANCE - bytes) return NULL;
            needed += bytes;
        }
    }
    if (needed > MAX_PROVENANCE) return NULL;
    char *output = malloc(needed + 1); if (output == NULL) return NULL;
    char *at = output; memcpy(at, "{\"stages\":[", 11); at += 11; bool first = true;
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < STAGE_COUNT; ++i) {
        if (entry->provenance[i] == NULL) continue;
        if (!first) *at++ = ',';
        first = false; *at++ = '"';
        for (size_t j = 0; j < entry->provenance_sizes[i]; ++j) {
            const unsigned char value = entry->provenance[i][j];
            if (value < 32) { memcpy(at, "\\u00", 4); at += 4; *at++ = hex[value >> 4]; *at++ = hex[value & 15]; }
            else { if (value == '"' || value == '\\') *at++ = '\\'; *at++ = value; }
        }
        *at++ = '"';
    }
    *at++ = ']'; *at++ = '}'; *at = '\0'; *size = at - output; return output;
}
static void remote_ended(vlc_metadata_job_t *job, int status, void *opaque)
{
    struct remote_call *call = opaque; struct pipeline_entry *entry = call->entry;
    vlc_metadata_pipeline_t *owner = entry->owner;
    struct pipeline_frame frame = {owner, callback_frame, true}; callback_frame = &frame;
    vlc_mutex_lock(&owner->lock);
    vlc_metadata_job_t *held = entry->child == job ? entry->child : NULL;
    if (held != NULL) entry->child = NULL;
    vlc_mutex_unlock(&owner->lock);
    if (held != NULL) vlc_metadata_job_Release(held);
    if (entry_current(entry)) {
        const struct vlc_metadata_result *result = vlc_metadata_job_GetResult(job);
        if (status == -EINTR) entry_finish(entry, status);
        else if (!retain_provenance(entry, call->stage, result)) entry_finish(entry, VLC_ENOMEM);
        else {
            int applied = VLC_SUCCESS;
            if (status == VLC_SUCCESS) applied = vlc_metadata_job_Apply(job, entry->working,
                call->stage == 0 ? UINT32_C(1) << vlc_meta_ArtworkURL : 0, NULL);
            if (status == VLC_SUCCESS && applied == VLC_SUCCESS && call->stage == 0 && result != NULL) {
                char *original = input_item_GetArtURL(vlc_metadata_job_GetRequest(entry->anchor)->item);
                char *before = input_item_GetArtURL(vlc_metadata_job_GetRequest(job)->item);
                bool artwork_changed = false;
                for (size_t i = 0; i < result->field_count; ++i) artwork_changed |= result->fields[i].type == vlc_meta_ArtworkURL;
                entry->native_artwork_replace = artwork_changed && original != NULL && before != NULL && strcmp(original, before) == 0
                    && (!strncasecmp(original, "http://", 7) || !strncasecmp(original, "https://", 8));
                free(original); free(before);
            }
            if (status == VLC_SUCCESS && applied == VLC_SUCCESS)
                applied = vlc_metadata_job_PrepareFetchedArtwork(entry->anchor, job,
                    entry->working, entry->native_artwork_replace);
            if (status == VLC_SUCCESS && applied == VLC_SUCCESS && call->stage == STAGE_COUNT - 1)
                applied = vlc_metadata_job_PrepareNotFoundArtwork(entry->anchor, job, entry->working);
            if (applied != VLC_SUCCESS) entry_finish(entry, applied);
            else entry_next(entry);
        }
    } else entry_finish(entry, vlc_input_policy_IsRevoked(entry->policy) ? -EACCES : -EINTR);
    callback_frame = frame.previous; pending_end(owner); entry_release(entry); free(call);
}
static void entry_next(struct pipeline_entry *entry)
{
    if (!entry_current(entry)) { entry_finish(entry, vlc_input_policy_IsRevoked(entry->policy) ? -EACCES : -EINTR); return; }
    vlc_metadata_pipeline_t *owner = entry->owner;
    vlc_mutex_lock(&owner->lock); const unsigned stage = entry->stage++; vlc_mutex_unlock(&owner->lock);
    if (!entry->remote_owned || stage >= STAGE_COUNT || (stage != 0 && !(entry->options & VLC_PREPARSER_TYPE_FETCHMETA_NET))) {
        size_t size; char *json = provenance(entry, &size);
        if (json == NULL) { entry_finish(entry, VLC_ENOMEM); return; }
        const bool completed = vlc_metadata_job_Complete(entry->anchor, VLC_SUCCESS, entry->working, json, size); free(json);
        const int status = completed ? vlc_metadata_pipeline_guard_Commit(entry->parent_guard,
            entry->anchor, entry->target, entry->native_artwork_replace ? UINT32_C(1) << vlc_meta_ArtworkURL : 0,
            NULL, &entry->events) : -EINTR;
        entry_finish(entry, status); return;
    }
    static const char *const features[STAGE_COUNT] = {"artwork", "video.details", "music.details", "podcast.details", "artwork"};
    static const char *const fields[] = {"title", "episodeTitle", "artist", "albumArtist", "album", "showName", "description",
        "genre", "date", "season", "episode", "track", "disc", "artwork"};
    static const char *const artwork_fields[] = {"artwork"};
    const bool artwork = stage == 0 || stage == STAGE_COUNT - 1;
    const struct vlc_metadata_request request = {.feature=features[stage], .operation=stage == 0 ? "materializeNativeArtwork" : "enrich",
        .fields=artwork ? artwork_fields : fields, .field_count=artwork ? ARRAY_SIZE(artwork_fields) : ARRAY_SIZE(fields),
        .item=entry->working, .generation=entry->generation, .target_revision=entry->generation,
        .flags=entry->options & VLC_PREPARSER_TYPE_FETCHMETA_NET ? VLC_METADATA_REQUEST_NETWORK | VLC_METADATA_REQUEST_AUTOMATIC : 0};
    struct remote_call *call = malloc(sizeof(*call));
    if (call == NULL) { entry_finish(entry, VLC_ENOMEM); return; }
    *call = (struct remote_call){entry_hold(entry), stage}; pending_add(owner);
    vlc_metadata_job_t *child = vlc_metadata_job_New(entry->backend, &request, remote_ended, call);
    if (child == NULL) { pending_end(owner); entry_release(call->entry); free(call); entry_finish(entry, VLC_ENOMEM); return; }
    vlc_mutex_lock(&owner->lock); const bool accepted = !owner->closed && !entry->terminal && vlc_metadata_pipeline_guard_IsCurrent(entry->parent_guard);
    if (accepted) entry->child = vlc_metadata_job_Hold(child);
    vlc_mutex_unlock(&owner->lock);
    const int started = accepted ? vlc_metadata_job_Start(child) : -EINTR;
    if (started != VLC_SUCCESS) {
        vlc_mutex_lock(&owner->lock); vlc_metadata_job_t *held = entry->child == child ? entry->child : NULL;
        if (held != NULL) entry->child = NULL;
        vlc_mutex_unlock(&owner->lock);
        if (held != NULL) vlc_metadata_job_Release(held);
        pending_end(owner); entry_release(call->entry); free(call); entry_finish(entry, started);
    }
    vlc_metadata_job_Release(child);
}
void vlc_metadata_pipeline_stage_Done(vlc_metadata_pipeline_stage_t *stage, int status)
{
    if (atomic_exchange(&stage->done, true)) return;
    struct pipeline_entry *entry = stage->entry; vlc_metadata_pipeline_t *owner = entry->owner;
    struct pipeline_frame frame = {owner, callback_frame, true}; callback_frame = &frame;
    vlc_mutex_lock(&owner->lock); const bool held = entry->local == stage;
    if (held) entry->local = NULL;
    vlc_mutex_unlock(&owner->lock);
    if (held) vlc_metadata_pipeline_stage_Release(stage);
    if (entry_current(entry)) {
        if (status == -EINTR || status == VLC_ENOMEM || status == VLC_ETIMEOUT) entry_finish(entry, status);
        else {
            const int prepared = entry->local_prepared ? VLC_SUCCESS
                : vlc_metadata_job_PrepareLocal(entry->anchor, entry->working, NULL);
            if (prepared != VLC_SUCCESS) entry_finish(entry, prepared);
            else { entry->local_prepared = true; entry_next(entry); }
        }
    } else entry_finish(entry, vlc_input_policy_IsRevoked(entry->policy) ? -EACCES : -EINTR);
    callback_frame = frame.previous; pending_end(owner); vlc_metadata_pipeline_stage_Release(stage);
}
static int local_only_dispatch(void *opaque, vlc_metadata_job_t *job)
{ (void)opaque; (void)job; return VLC_ENOENT; }
vlc_metadata_pipeline_t *vlc_metadata_pipeline_New(vlc_object_t *object,
    const struct vlc_metadata_pipeline_operations *ops, void *context)
{
    if (object == NULL || ops == NULL || ops->local == NULL || ops->cancel_local == NULL
        || (ops->retain_key == NULL) != (ops->release_key == NULL)) return NULL;
    vlc_metadata_pipeline_t *owner = calloc(1, sizeof(*owner)); if (owner == NULL) return NULL;
    static const struct vlc_metadata_backend_operations local_ops = {.dispatch=local_only_dispatch};
    owner->local_backend = vlc_metadata_backend_New(&local_ops, NULL);
    if (owner->local_backend == NULL) { free(owner); return NULL; }
    vlc_atomic_rc_init(&owner->refs); owner->instance = VLC_OBJECT(vlc_object_instance(object));
    owner->ops = *ops; owner->stage_context = context; vlc_mutex_init(&owner->lock); vlc_cond_init(&owner->idle); return owner;
}
static int push_request(vlc_metadata_pipeline_t *owner, input_item_t *item, int options,
    vlc_metadata_pipeline_guard_t *parent_guard, const void *key, vlc_metadata_pipeline_ended_cb ended,
    vlc_metadata_pipeline_request_cb request_ended, void *userdata, bool request_mode)
{
    const int required = request_mode ? VLC_PREPARSER_TYPE_PARSE | VLC_PREPARSER_TYPE_FETCHMETA_ALL : VLC_PREPARSER_TYPE_FETCHMETA_ALL;
    if (owner == NULL || item == NULL || (ended == NULL && request_ended == NULL) || !(options & required)) return VLC_EINVAL;
    if (!vlc_metadata_pipeline_guard_IsCurrent(parent_guard)) return -EINTR;
    owner_hold(owner);
    bool owned = false; vlc_metadata_backend_t *backend = vlc_metadata_owner_Hold(owner->instance, &owned);
    vlc_mutex_lock(&owner->lock); const bool closed = owner->closed; vlc_mutex_unlock(&owner->lock);
    const bool ready = backend != NULL && vlc_metadata_backend_IsReady(backend);
    const int selection = closed || (owned && (backend == NULL || vlc_metadata_backend_IsClosed(backend))) ? -EINTR
        : !owned ? VLC_ENOENT : !ready ? -EAGAIN : VLC_SUCCESS;
    if (closed || (!request_mode && selection != VLC_SUCCESS)) {
        if (backend != NULL) vlc_metadata_backend_Release(backend);
        owner_release(owner); return selection;
    }
    if (!ready) {
        if (backend != NULL) vlc_metadata_backend_Release(backend);
        backend = vlc_metadata_backend_Hold(owner->local_backend);
    }
    struct pipeline_entry *entry = calloc(1, sizeof(*entry));
    if (entry == NULL) { vlc_metadata_backend_Release(backend); owner_release(owner); return VLC_ENOMEM; }
    vlc_atomic_rc_init(&entry->refs); entry->owner = owner_hold(owner); entry->backend = backend;
    entry->parent_guard = parent_guard != NULL ? vlc_metadata_pipeline_guard_Hold(parent_guard) : vlc_metadata_pipeline_guard_New();
    entry->target = input_item_Hold(item); entry->key = key; entry->ended = ended;
    if (key != NULL && owner->ops.retain_key != NULL) {
        owner->ops.retain_key(owner->stage_context, key); entry->key_held = true;
    }
    entry->request_ended = request_ended; entry->userdata = userdata; entry->options = options; entry->request_mode = request_mode;
    entry->remote_owned = ready && (options & VLC_PREPARSER_TYPE_FETCHMETA_ALL);
    entry->legacy_allowed = request_mode && selection == VLC_ENOENT;
    if (request_mode && owned && !ready) entry->options &= ~VLC_PREPARSER_TYPE_FETCHMETA_NET;
    const struct vlc_metadata_request request = {.feature="native.transaction", .operation="anchor", .item=item,
        .flags=options & VLC_PREPARSER_TYPE_FETCHMETA_NET ? VLC_METADATA_REQUEST_NETWORK | VLC_METADATA_REQUEST_AUTOMATIC : 0};
    if (entry->parent_guard != NULL) entry->anchor = vlc_metadata_job_New(backend, &request, anchor_ended, entry);
    if (entry->anchor != NULL) {
        input_item_t *snapshot = vlc_metadata_job_GetRequest(entry->anchor)->item;
        entry->source_uri = input_item_GetURI(snapshot); entry->policy = vlc_input_policy_HoldItem(snapshot);
    }
    if (entry->anchor == NULL) { entry_release(entry); owner_release(owner); return VLC_ENOMEM; }
    vlc_mutex_lock(&owner->lock);
    const bool refused_closed = owner->closed;
    const bool accepted = !refused_closed && owner->retained < MAX_ENTRIES && owner->generation != UINT64_MAX;
    if (accepted) {
        entry->generation = ++owner->generation; entry->retained = true; ++owner->retained;
        entry->next = owner->entries; owner->entries = entry_hold(entry); entry->listed = true;
        entry_hold(entry); ++owner->pending_callbacks;
    }
    vlc_mutex_unlock(&owner->lock);
    if (!accepted) { entry_release(entry); owner_release(owner); return refused_closed ? -EINTR : VLC_ENOMEM; }
    const int started = vlc_metadata_job_StartLocal(entry->anchor);
    if (started != VLC_SUCCESS) { pending_end(owner); entry_release(entry); entry_finish(entry, started); }
    else {
        /* Read consent after the retained anchor starts. Do not revive remote
         * work when consent becomes available after this decision. */
        if ((options & VLC_PREPARSER_TYPE_FETCHMETA_NET) && !var_InheritBool(owner->instance, "metadata-network-access"))
            entry->options = (entry->options & ~VLC_PREPARSER_TYPE_FETCHMETA_NET) | VLC_PREPARSER_TYPE_FETCHMETA_LOCAL;
        entry->working = vlc_metadata_job_CopyLocalItem(entry->anchor);
        if (entry->working == NULL) entry_finish(entry, vlc_metadata_pipeline_guard_IsCurrent(entry->parent_guard) ? VLC_ENOMEM : -EINTR);
        else if (!entry_current(entry)) entry_finish(entry, vlc_input_policy_IsRevoked(entry->policy) ? -EACCES : -EINTR);
        else {
            vlc_metadata_pipeline_stage_t *stage = calloc(1, sizeof(*stage));
            if (stage == NULL) entry_finish(entry, VLC_ENOMEM);
            else {
                vlc_atomic_rc_init(&stage->refs); stage->entry = entry_hold(entry); atomic_init(&stage->cancelled, false); atomic_init(&stage->done, false);
                pending_add(owner); vlc_metadata_pipeline_stage_Hold(stage);
                vlc_mutex_lock(&owner->lock); const bool live = !owner->closed && !entry->terminal;
                if (live) entry->local = vlc_metadata_pipeline_stage_Hold(stage);
                vlc_mutex_unlock(&owner->lock);
                const int status = live ? owner->ops.local(owner->stage_context, stage) : -EINTR;
                if (status != VLC_SUCCESS) vlc_metadata_pipeline_stage_Done(stage, status);
                vlc_metadata_pipeline_stage_Release(stage);
            }
        }
    }
    entry_release(entry); owner_release(owner); return VLC_SUCCESS;
}
int vlc_metadata_pipeline_Push(vlc_metadata_pipeline_t *owner, input_item_t *item, int options,
    vlc_metadata_pipeline_guard_t *guard, const void *key, vlc_metadata_pipeline_ended_cb callback, void *userdata)
{ return push_request(owner, item, options, guard, key, callback, NULL, userdata, false); }
int vlc_metadata_pipeline_PushRequest(vlc_metadata_pipeline_t *owner, input_item_t *item, int options,
    vlc_metadata_pipeline_guard_t *guard, const void *key, vlc_metadata_pipeline_request_cb callback, void *userdata)
{ return push_request(owner, item, options, guard, key, NULL, callback, userdata, true); }
size_t vlc_metadata_pipeline_Cancel(vlc_metadata_pipeline_t *owner, const void *key)
{
    owner_hold(owner); struct pipeline_entry *entries[MAX_ENTRIES]; size_t count = 0;
    vlc_mutex_lock(&owner->lock);
    for (struct pipeline_entry *entry = owner->entries; entry != NULL; entry = entry->next)
        if (key == NULL || key == entry->key) entries[count++] = entry_hold(entry);
    vlc_mutex_unlock(&owner->lock);
    for (size_t i = 0; i < count; ++i) {
        vlc_metadata_pipeline_guard_TryCancel(entries[i]->parent_guard);
        entry_finish(entries[i], -EINTR); entry_release(entries[i]);
    }
    owner_release(owner); return count;
}
void vlc_metadata_pipeline_Close(vlc_metadata_pipeline_t *owner)
{
    owner_hold(owner); vlc_mutex_lock(&owner->lock); owner->closed = true; vlc_mutex_unlock(&owner->lock);
    vlc_metadata_pipeline_Cancel(owner, NULL); vlc_metadata_backend_Close(owner->local_backend); owner_release(owner);
}
void vlc_metadata_pipeline_Delete(vlc_metadata_pipeline_t *owner)
{
    if (owner == NULL) return;
    vlc_mutex_lock(&owner->lock);
    if (owner->deleting) { vlc_mutex_unlock(&owner->lock); return; }
    owner->deleting = true; vlc_mutex_unlock(&owner->lock); vlc_metadata_pipeline_Close(owner);
    unsigned providers_here = 0, users_here = 0;
    for (const struct pipeline_frame *frame = callback_frame; frame != NULL; frame = frame->previous)
        if (frame->owner == owner) { if (frame->provider) ++providers_here; else ++users_here; }
    vlc_mutex_lock(&owner->lock);
    while (owner->pending_callbacks > providers_here || owner->user_callbacks > users_here) vlc_cond_wait(&owner->idle, &owner->lock);
    vlc_mutex_unlock(&owner->lock); owner_release(owner);
}
