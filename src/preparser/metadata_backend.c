// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <vlc_common.h>
#include <vlc_metadata_backend.h>
#include <vlc_atomic.h>
#include <vlc_input_item.h>
#include <vlc_input_policy.h>
#include <vlc_artwork_policy.h>
#include <vlc_metadata_revision.h>
#include <vlc_threads.h>
#include <vlc_input.h>
#include "../input/item.h"
#include "../input/item_parse.h"
#include "../input/artwork_policy.h"
#include "../input/metadata_revision.h"
#include "metadata_local.h"

#define MAX_JSON (1024u * 1024u)
#define MAX_FIELDS 64u
#define MAX_ACTIVE 1024u
_Static_assert(VLC_META_TYPE_COUNT < 32, "The native replacement mask must include every metadata field.");

struct vlc_metadata_backend {
    vlc_atomic_rc_t refs;
    vlc_mutex_t lock;
    bool closed;
    size_t active, suspensions;
    vlc_metadata_job_t *head;
    struct vlc_metadata_backend_operations ops;
    void *opaque;
};
struct vlc_metadata_job {
    vlc_atomic_rc_t refs;
    vlc_metadata_backend_t *backend;
    vlc_metadata_job_t *next, *previous;
    bool started, terminal, dispatched, listed, local, applying, sealed;
    bool commit_cancelled;
    int status;
    struct vlc_metadata_request request;
    input_item_t *target;
    char *uri;
    vlc_input_policy_t *policy;
    struct vlc_metadata_revisions baseline;
    struct vlc_item_parse_revisions native_baseline;
    uint64_t status_baseline;
    input_item_t *local_item;
    struct vlc_item_parse_delta local_delta;
    struct vlc_metadata_local_events local_events;
    bool local_prepared, local_admitted, local_events_taken, local_name_missing;
    char *fetched_artwork;
    uint64_t artwork_applied_revision;
    struct vlc_metadata_result result;
    struct vlc_metadata_field_patch patches[VLC_META_TYPE_COUNT];
    vlc_metadata_ended_cb ended;
    void *userdata;
};

vlc_metadata_backend_t *vlc_metadata_backend_New(const struct vlc_metadata_backend_operations *ops, void *opaque)
{
    if (ops == NULL || ops->dispatch == NULL) return NULL;
    vlc_metadata_backend_t *backend = calloc(1, sizeof(*backend));
    if (backend == NULL) return NULL;
    vlc_atomic_rc_init(&backend->refs);
    vlc_mutex_init(&backend->lock);
    backend->ops = *ops;
    backend->opaque = opaque;
    return backend;
}
vlc_metadata_backend_t *vlc_metadata_backend_Hold(vlc_metadata_backend_t *backend)
{ vlc_atomic_rc_inc(&backend->refs); return backend; }
void vlc_metadata_backend_Release(vlc_metadata_backend_t *backend)
{
    if (!vlc_atomic_rc_dec(&backend->refs)) return;
    vlc_assert(backend->head == NULL);
    if (backend->ops.destroy != NULL) backend->ops.destroy(backend->opaque);
    free(backend);
}
bool vlc_metadata_backend_IsClosed(vlc_metadata_backend_t *backend)
{
    vlc_mutex_lock(&backend->lock);
    const bool closed = backend->closed;
    vlc_mutex_unlock(&backend->lock);
    return closed;
}
bool vlc_metadata_backend_Suspend(vlc_metadata_backend_t *backend)
{
    vlc_mutex_lock(&backend->lock);
    const bool accepted = !backend->closed && backend->suspensions != SIZE_MAX
        && (backend->suspensions != 0 || backend->active == 0);
    if (accepted) ++backend->suspensions;
    vlc_mutex_unlock(&backend->lock); return accepted;
}
bool vlc_metadata_backend_IsReady(vlc_metadata_backend_t *backend)
{
    vlc_mutex_lock(&backend->lock);
    const bool ready = !backend->closed && backend->suspensions == 0;
    vlc_mutex_unlock(&backend->lock); return ready;
}
vlc_metadata_job_t *vlc_metadata_job_Hold(vlc_metadata_job_t *job)
{ vlc_atomic_rc_inc(&job->refs); return job; }
void vlc_metadata_job_Release(vlc_metadata_job_t *job)
{
    if (!vlc_atomic_rc_dec(&job->refs)) return;
    vlc_metadata_backend_t *backend = job->backend;
    free((char *)job->request.feature);
    free((char *)job->request.operation);
    for (size_t i = 0; i < job->request.field_count; ++i) free((char *)job->request.fields[i]);
    free((char **)job->request.fields);
    free((char *)job->request.context_json);
    if (job->request.item != NULL) input_item_Release(job->request.item);
    if (job->target != NULL) input_item_Release(job->target);
    if (job->local_item != NULL) input_item_Release(job->local_item);
    vlc_item_parse_Clear(&job->local_delta);
    vlc_metadata_local_events_Clear(&job->local_events);
    free(job->fetched_artwork);
    free(job->uri);
    vlc_input_policy_Release(job->policy);
    if (job->result.item != NULL) input_item_Release(job->result.item);
    for (size_t i = 0; i < job->result.field_count; ++i) {
        free((char *)job->patches[i].expected_value);
        free((char *)job->patches[i].value);
    }
    free((char *)job->result.provenance_json);
    free(job);
    vlc_metadata_backend_Release(backend);
}

static bool same_string(const char *a, const char *b)
{ return a == NULL ? b == NULL : b != NULL && strcmp(a, b) == 0; }
static bool revisions_locked(input_item_t *item, struct vlc_metadata_revisions *value)
{
    if (item->p_meta == NULL) return false;
    for (int i = 0; i < VLC_META_TYPE_COUNT; ++i) {
        value->fields[i] = vlc_meta_FieldRevision(item->p_meta, i);
        if (value->fields[i] == UINT64_MAX) return false;
    }
    value->extra = vlc_meta_ExtraRevision(item->p_meta);
    return value->extra != UINT64_MAX;
}
static bool capture_item(vlc_metadata_job_t *job, input_item_t *item)
{
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        struct vlc_metadata_revisions before, after;
        vlc_mutex_lock(&item->lock);
        if (item->p_meta == NULL) item->p_meta = vlc_meta_New();
        const bool available = revisions_locked(item, &before);
        const struct vlc_item_parse_revisions native_before = item_owner(item)->parse_revisions;
        const uint64_t status_before = vlc_meta_StatusRevision(item->p_meta);
        const bool has_uri = item->psz_uri != NULL;
        char *uri = has_uri ? strdup(item->psz_uri) : NULL;
        vlc_input_policy_t *policy = vlc_input_policy_Hold(item_owner(item)->input_policy);
        vlc_mutex_unlock(&item->lock);
        if (!available || (has_uri && uri == NULL) || vlc_input_policy_IsRevoked(policy)) {
            free(uri); vlc_input_policy_Release(policy); return false;
        }
        input_item_t *copy = input_item_Copy(item);
        if (copy == NULL) { free(uri); vlc_input_policy_Release(policy); return false; }
        vlc_mutex_lock(&item->lock);
        bool current = revisions_locked(item, &after)
            && memcmp(&before, &after, sizeof(before)) == 0
            && same_string(uri, item->psz_uri) && policy == item_owner(item)->input_policy
            && !vlc_input_policy_IsRevoked(policy);
        vlc_mutex_unlock(&item->lock);
        vlc_mutex_lock(&copy->lock);
        current = current && same_string(uri, copy->psz_uri) && policy == item_owner(copy)->input_policy;
        vlc_mutex_unlock(&copy->lock);
        if (current) {
            job->request.item = copy; job->target = input_item_Hold(item);
            job->uri = uri; job->policy = policy; job->baseline = before;
            job->native_baseline = native_before; job->status_baseline = status_before; return true;
        }
        input_item_Release(copy); free(uri); vlc_input_policy_Release(policy);
    }
    return false;
}

vlc_metadata_job_t *vlc_metadata_job_New(vlc_metadata_backend_t *backend,
    const struct vlc_metadata_request *request, vlc_metadata_ended_cb ended, void *userdata)
{
    if (backend == NULL || request == NULL || ended == NULL || request->feature == NULL
        || request->operation == NULL || strlen(request->feature) > 128 || strlen(request->operation) > 128
        || request->field_count > MAX_FIELDS || (request->field_count > 0 && request->fields == NULL)
        || request->context_size > MAX_JSON || (request->context_size > 0 && request->context_json == NULL)) return NULL;
    vlc_metadata_job_t *job = calloc(1, sizeof(*job));
    if (job == NULL) return NULL;
    vlc_atomic_rc_init(&job->refs);
    job->backend = vlc_metadata_backend_Hold(backend);
    job->ended = ended; job->userdata = userdata;
    job->request.generation = request->generation;
    job->request.target_revision = request->target_revision;
    job->request.flags = request->flags;
    job->request.feature = strdup(request->feature);
    job->request.operation = strdup(request->operation);
    if (job->request.feature == NULL || job->request.operation == NULL) goto error;
    if (request->context_size > 0) {
        char *context = malloc(request->context_size + 1);
        if (context == NULL) goto error;
        memcpy(context, request->context_json, request->context_size); context[request->context_size] = '\0';
        job->request.context_json = context;
        job->request.context_size = request->context_size;
    }
    if (request->field_count > 0) {
        char **fields = calloc(request->field_count, sizeof(*fields));
        if (fields == NULL) goto error;
        job->request.fields = (const char *const *)fields;
        for (size_t i = 0; i < request->field_count; ++i) {
            if (request->fields[i] == NULL || strlen(request->fields[i]) > 128) goto error;
            fields[i] = strdup(request->fields[i]);
            if (fields[i] == NULL) goto error;
            ++job->request.field_count;
        }
    }
    if (request->item != NULL) {
        if (!capture_item(job, request->item)) goto error;
    }
    return job;
error:
    vlc_metadata_job_Release(job);
    return NULL;
}

const struct vlc_metadata_request *vlc_metadata_job_GetRequest(const vlc_metadata_job_t *job)
{ return &job->request; }

void vlc_metadata_local_events_Clear(struct vlc_metadata_local_events *events)
{
    for (size_t i = 0; i < events->subtree_count; ++i) input_item_node_Delete(events->subtrees[i]);
    for (size_t i = 0; i < events->attachment_count; ++i) vlc_input_attachment_Release(events->attachments[i]);
    free(events->subtrees); free(events->attachments); memset(events, 0, sizeof(*events));
}
static input_item_node_t *copy_local_tree(const input_item_node_t *source, unsigned depth, size_t *nodes, size_t *bytes)
{
    if (source == NULL || source->p_item == NULL || depth > 32 || ++*nodes > VLC_ITEM_PARSE_MAX_ENTRIES
        || source->i_children < 0 || (unsigned)source->i_children > VLC_ITEM_PARSE_MAX_ENTRIES) return NULL;
    const size_t node_bytes = sizeof(input_item_node_t) + source->i_children * sizeof(input_item_node_t *);
    if (node_bytes > *bytes) return NULL;
    *bytes -= node_bytes;
    vlc_mutex_lock(&source->p_item->lock);
    input_item_t *item = vlc_item_parse_CopyBudgetLocked(source->p_item, bytes);
    vlc_mutex_unlock(&source->p_item->lock);
    if (item == NULL) return NULL;
    input_item_node_t *copy = input_item_node_Create(item); input_item_Release(item);
    if (copy == NULL) return NULL;
    if (source->i_children != 0) {
        copy->pp_children = calloc(source->i_children, sizeof(*copy->pp_children));
        if (copy->pp_children == NULL) goto error;
    }
    for (int i = 0; i < source->i_children; ++i) {
        input_item_node_t *child = copy_local_tree(source->pp_children[i], depth + 1, nodes, bytes);
        if (child == NULL) goto error;
        copy->pp_children[copy->i_children++] = child;
    }
    return copy;
error:
    input_item_node_Delete(copy); return NULL;
}
static int copy_local_events(struct vlc_metadata_local_events *target, const struct vlc_metadata_local_events *source)
{
    if (source == NULL) return VLC_SUCCESS;
    if (source->subtree_count > VLC_ITEM_PARSE_MAX_ENTRIES || source->attachment_count > VLC_ITEM_PARSE_MAX_ENTRIES
        || (source->subtree_count != 0 && source->subtrees == NULL)
        || (source->attachment_count != 0 && source->attachments == NULL)) return VLC_EINVAL;
    if (source->subtree_count != 0) {
        target->subtrees = calloc(source->subtree_count, sizeof(*target->subtrees));
        if (target->subtrees == NULL) return VLC_ENOMEM;
    }
    if (source->attachment_count != 0) {
        target->attachments = calloc(source->attachment_count, sizeof(*target->attachments));
        if (target->attachments == NULL) return VLC_ENOMEM;
    }
    size_t nodes = 0, bytes = 0;
    size_t tree_bytes = VLC_ITEM_PARSE_MAX_BYTES - source->subtree_count * sizeof(*target->subtrees);
    for (size_t i = 0; i < source->subtree_count; ++i) {
        input_item_node_t *tree = copy_local_tree(source->subtrees[i], 1, &nodes, &tree_bytes);
        if (tree == NULL) return VLC_ENOMEM;
        target->subtrees[target->subtree_count++] = tree;
    }
    for (size_t i = 0; i < source->attachment_count; ++i) {
        input_attachment_t *attachment = source->attachments[i];
        if (attachment == NULL || attachment->i_data > VLC_ITEM_PARSE_MAX_BYTES - bytes) return VLC_EINVAL;
        bytes += attachment->i_data;
        target->attachments[target->attachment_count++] = vlc_input_attachment_Hold(attachment);
    }
    return VLC_SUCCESS;
}
static bool original_snapshot_locked(vlc_metadata_job_t *job)
{
    input_item_t *target = job->target;
    struct vlc_metadata_revisions current;
    bool native_current = true;
    for (unsigned i = 0; i < VLC_ITEM_PARSE_FIELD_COUNT; ++i)
        if (i != VLC_ITEM_PARSE_EPG && (job->native_baseline.fields[i] == UINT64_MAX
            || item_owner(target)->parse_revisions.fields[i] != job->native_baseline.fields[i])) native_current = false;
    return revisions_locked(target, &current) && !memcmp(&current, &job->baseline, sizeof(current)) && native_current
        && same_string(job->uri, target->psz_uri) && job->policy == item_owner(target)->input_policy
        && !vlc_input_policy_IsRevoked(job->policy);
}
input_item_t *vlc_metadata_job_CopyLocalItem(vlc_metadata_job_t *job)
{
    if (job == NULL || job->target == NULL) return NULL;
    vlc_metadata_job_Hold(job);
    vlc_mutex_lock(&job->backend->lock);
    bool ready = job->local && job->started && !job->terminal && !job->commit_cancelled
        && !job->backend->closed && job->local_item == NULL;
    vlc_mutex_unlock(&job->backend->lock);
    input_item_t *copy = NULL;
    if (ready) {
        vlc_mutex_lock(&job->target->lock);
        if (original_snapshot_locked(job)) copy = vlc_item_parse_CopyLocked(job->target);
        vlc_mutex_unlock(&job->target->lock);
    }
    if (copy != NULL) {
        vlc_mutex_lock(&job->backend->lock);
        ready = job->local && !job->terminal && !job->commit_cancelled && !job->backend->closed && job->local_item == NULL;
        if (ready) { job->local_item = input_item_Hold(copy); job->local_name_missing = copy->psz_name == NULL; }
        vlc_mutex_unlock(&job->backend->lock);
        if (!ready) { input_item_Release(copy); copy = NULL; }
    }
    vlc_metadata_job_Release(job); return copy;
}
int vlc_metadata_job_PrepareLocal(vlc_metadata_job_t *job, input_item_t *parsed,
                                const struct vlc_metadata_local_events *events)
{
    if (job == NULL || parsed == NULL) return VLC_EINVAL;
    vlc_metadata_job_Hold(job);
    vlc_mutex_lock(&job->backend->lock);
    bool ready = job->local && job->started && !job->terminal && !job->commit_cancelled
        && !job->backend->closed && !job->local_prepared && parsed == job->local_item;
    vlc_mutex_unlock(&job->backend->lock);
    int status = ready ? VLC_SUCCESS : VLC_EINVAL;
    struct vlc_item_parse_delta delta = {0}; struct vlc_metadata_local_events staged = {0};
    if (!ready) goto out;
    vlc_mutex_lock(&parsed->lock);
    if (!same_string(job->uri, parsed->psz_uri) || job->policy != item_owner(parsed)->input_policy)
        status = -EAGAIN;
    else if ((delta.item = vlc_item_parse_CopyLocked(parsed)) == NULL) status = VLC_ENOMEM;
    vlc_mutex_unlock(&parsed->lock);
    if (status != VLC_SUCCESS) goto out;
    for (unsigned i = 0; i < VLC_ITEM_PARSE_FIELD_COUNT; ++i)
        if (item_owner(delta.item)->parse_revisions.fields[i] != job->native_baseline.fields[i]) delta.fields |= VLC_ITEM_PARSE_BIT(i);
    if (delta.fields & (VLC_ITEM_PARSE_BIT(VLC_ITEM_PARSE_URI) | VLC_ITEM_PARSE_BIT(VLC_ITEM_PARSE_OPTIONS))) { status = -EAGAIN; goto out; }
    /* Names supplied by the current target have precedence. */
    if (!job->local_name_missing) delta.fields &= ~VLC_ITEM_PARSE_BIT(VLC_ITEM_PARSE_NAME);
    delta.extras = vlc_meta_ExtraRevision(delta.item->p_meta) != job->baseline.extra;
    delta.status = vlc_meta_StatusRevision(delta.item->p_meta) != job->status_baseline;
    vlc_mutex_lock(&job->target->lock);
    if (item_owner(job->target)->parse_revisions.fields[VLC_ITEM_PARSE_SLAVES] != job->native_baseline.fields[VLC_ITEM_PARSE_SLAVES]) status = -EAGAIN;
    else status = vlc_item_parse_PrepareLinksLocked(&delta, job->target);
    vlc_mutex_unlock(&job->target->lock);
    if (status == VLC_SUCCESS) status = copy_local_events(&staged, events);
    if (status != VLC_SUCCESS) goto out;
    vlc_mutex_lock(&job->backend->lock);
    ready = !job->terminal && !job->commit_cancelled && !job->backend->closed && !job->local_prepared;
    if (ready) {
        job->local_delta = delta; memset(&delta, 0, sizeof(delta));
        job->local_events = staged; memset(&staged, 0, sizeof(staged)); job->local_prepared = true;
    }
    vlc_mutex_unlock(&job->backend->lock);
    if (!ready) status = -EINTR;
out:
    vlc_item_parse_Clear(&delta); vlc_metadata_local_events_Clear(&staged);
    vlc_metadata_job_Release(job); return status;
}
bool vlc_metadata_job_TakeLocalEvents(vlc_metadata_job_t *job, struct vlc_metadata_local_events *events)
{
    if (job == NULL || events == NULL) return false;
    vlc_mutex_lock(&job->backend->lock);
    const bool ready = job->local_admitted && !job->local_events_taken;
    if (ready) { *events = job->local_events; memset(&job->local_events, 0, sizeof(job->local_events)); job->local_events_taken = true; }
    vlc_mutex_unlock(&job->backend->lock); return ready;
}
const struct vlc_metadata_result *vlc_metadata_job_GetResult(vlc_metadata_job_t *job)
{
    vlc_mutex_lock(&job->backend->lock);
    const struct vlc_metadata_result *result = job->terminal ? &job->result : NULL;
    vlc_mutex_unlock(&job->backend->lock);
    return result;
}
bool vlc_metadata_job_IsPending(vlc_metadata_job_t *job)
{
    vlc_mutex_lock(&job->backend->lock);
    const bool pending = job->started && !job->terminal;
    vlc_mutex_unlock(&job->backend->lock);
    return pending;
}

static void discard_result(struct vlc_metadata_result *result)
{
    if (result->item != NULL) input_item_Release(result->item);
    for (size_t i = 0; i < result->field_count; ++i) {
        free((char *)result->fields[i].expected_value);
        free((char *)result->fields[i].value);
    }
    free((char *)result->provenance_json);
    memset(result, 0, sizeof(*result));
}
static bool copy_field(input_item_t *item, vlc_meta_type_t type, char **value)
{
    *value = NULL;
    if (item == NULL) return true;
    vlc_mutex_lock(&item->lock);
    const char *raw = item->p_meta != NULL ? vlc_meta_Get(item->p_meta, type) : NULL;
    if (raw != NULL) *value = strdup(raw);
    const bool success = raw == NULL || *value != NULL;
    vlc_mutex_unlock(&item->lock);
    return success;
}
static bool remove_job_locked(vlc_metadata_job_t *job)
{
    vlc_metadata_backend_t *backend = job->backend;
    vlc_mutex_assert(&backend->lock);
    if (!job->listed) return false;
    if (job->previous != NULL) job->previous->next = job->next; else backend->head = job->next;
    if (job->next != NULL) job->next->previous = job->previous;
    job->next = job->previous = NULL; job->listed = false; --backend->active; return true;
}
static bool finish(vlc_metadata_job_t *job, int status, struct vlc_metadata_result *result, bool cancel)
{
    vlc_metadata_backend_t *backend = job->backend;
    vlc_mutex_lock(&backend->lock);
    if (!job->started || job->terminal) {
        vlc_mutex_unlock(&backend->lock);
        discard_result(result);
        return false;
    }
    job->terminal = true;
    job->status = status;
    const bool retain = job->local && status == VLC_SUCCESS && !cancel && !job->commit_cancelled;
    const bool removed = !retain && remove_job_locked(job);
    job->result = *result;
    if (result->field_count > 0) memcpy(job->patches, result->fields, result->field_count * sizeof(*job->patches));
    job->result.fields = job->patches;
    // A callback can seal or cancel a retained anchor and release its caller
    // reference. Keep a separate callback reference through that reentry.
    vlc_metadata_job_Hold(job);
    vlc_mutex_unlock(&backend->lock);
    job->ended(job, status, job->userdata);
    if (cancel && backend->ops.cancel != NULL) backend->ops.cancel(backend->opaque, job);
    if (removed) vlc_metadata_job_Release(job); // active-list reference
    vlc_metadata_job_Release(job); // callback reference
    return true;
}
bool vlc_metadata_job_Cancel(vlc_metadata_job_t *job)
{
    vlc_metadata_job_Hold(job);
    vlc_mutex_lock(&job->backend->lock);
    if (!job->started) {
        const bool won = !job->commit_cancelled;
        job->commit_cancelled = true;
        vlc_mutex_unlock(&job->backend->lock);
        vlc_metadata_job_Release(job); return won;
    }
    job->commit_cancelled = true;
    if (job->terminal) {
        const bool removed = job->local && remove_job_locked(job);
        vlc_mutex_unlock(&job->backend->lock);
        if (removed) vlc_metadata_job_Release(job);
        vlc_metadata_job_Release(job); return false;
    }
    vlc_mutex_unlock(&job->backend->lock);
    struct vlc_metadata_result result = {0};
    const bool won = finish(job, -EINTR, &result, true);
    vlc_metadata_job_Release(job);
    return won;
}
bool vlc_metadata_job_Complete(vlc_metadata_job_t *job, int status, input_item_t *result,
                              const char *provenance_json, size_t provenance_size)
{
    vlc_metadata_job_Hold(job);
    if (!vlc_metadata_job_IsPending(job)) { vlc_metadata_job_Release(job); return false; }
    struct vlc_metadata_field_patch fields[VLC_META_TYPE_COUNT];
    struct vlc_metadata_result value = {.fields = fields};
    if (provenance_size > MAX_JSON || (provenance_size > 0 && provenance_json == NULL)) status = VLC_EINVAL;
    if (status == VLC_SUCCESS && result != NULL && (value.item = input_item_Copy(result)) == NULL) status = VLC_ENOMEM;
    if (status == VLC_SUCCESS && provenance_size > 0) {
        char *provenance = malloc(provenance_size + 1);
        if (provenance == NULL) status = VLC_ENOMEM;
        else {
            memcpy(provenance, provenance_json, provenance_size); provenance[provenance_size] = '\0';
            value.provenance_json = provenance; value.provenance_size = provenance_size;
        }
    }
    if (status == VLC_SUCCESS && value.item != NULL) {
        for (int i = 0; i < VLC_META_TYPE_COUNT; ++i) {
            char *before = NULL, *next = NULL;
            if (!copy_field(job->request.item, i, &before) || !copy_field(value.item, i, &next)) {
                free(before); free(next); status = VLC_ENOMEM; break;
            }
            if ((before == NULL && next == NULL) || (before != NULL && next != NULL && strcmp(before, next) == 0)) {
                free(before); free(next); continue;
            }
            fields[value.field_count++] = (struct vlc_metadata_field_patch){i, before, next, job->baseline.fields[i]};
        }
    }
    if (status != VLC_SUCCESS) discard_result(&value);
    const bool won = finish(job, status, &value, false);
    vlc_metadata_job_Release(job);
    return won;
}

/* The backend and target locks are held, in that order. This function has
 * no allocation, callback, or revision reservation. The caller performs the
 * final policy read at its admission point. */
static int target_current_locked(vlc_metadata_job_t *job, input_item_t *target,
    bool native_guard, uint32_t *native_fields)
{
    static const vlc_meta_type_t identity_fields[] = {
        vlc_meta_Title, vlc_meta_Artist, vlc_meta_Album, vlc_meta_AlbumArtist,
        vlc_meta_Date, vlc_meta_TrackID, vlc_meta_TrackNumber, vlc_meta_DiscNumber,
        vlc_meta_ShowName, vlc_meta_Season, vlc_meta_Episode,
    };
    if (job->backend->closed || job->commit_cancelled) return -EINTR;
    if (job->backend->suspensions != 0) return -EAGAIN;
    if (target->p_meta == NULL || !same_string(job->uri, target->psz_uri)
        || job->policy != item_owner(target)->input_policy
        || vlc_meta_ExtraRevision(target->p_meta) != job->baseline.extra) return -EAGAIN;
    if (job->fetched_artwork != NULL
        && (job->status_baseline == UINT64_MAX
            || vlc_meta_StatusRevision(target->p_meta) != job->status_baseline
            || vlc_meta_FieldRevision(target->p_meta, vlc_meta_ArtworkURL)
                != job->baseline.fields[vlc_meta_ArtworkURL])) return -EAGAIN;
    for (size_t i = 0; i < ARRAY_SIZE(identity_fields); ++i) {
        const vlc_meta_type_t type = identity_fields[i];
        if (vlc_meta_FieldRevision(target->p_meta, type) != job->baseline.fields[type]) return -EAGAIN;
    }
    for (size_t i = 0; i < job->result.field_count; ++i) {
        const struct vlc_metadata_field_patch *field = &job->patches[i];
        if (field->expected_revision == UINT64_MAX
            || vlc_meta_FieldRevision(target->p_meta, field->type) != field->expected_revision
            || !same_string(field->expected_value, vlc_meta_Get(target->p_meta, field->type))) return -EAGAIN;
    }
    if (native_guard || job->local_prepared) {
        const uint32_t guarded = (*native_fields & ~VLC_ITEM_PARSE_BIT(VLC_ITEM_PARSE_EPG))
            | VLC_ITEM_PARSE_BIT(VLC_ITEM_PARSE_URI) | VLC_ITEM_PARSE_BIT(VLC_ITEM_PARSE_NAME)
            | VLC_ITEM_PARSE_BIT(VLC_ITEM_PARSE_OPTIONS);
        for (unsigned i = 0; i < VLC_ITEM_PARSE_FIELD_COUNT; ++i) {
            const uint64_t current = item_owner(target)->parse_revisions.fields[i];
            if ((guarded & VLC_ITEM_PARSE_BIT(i)) && (current == UINT64_MAX
                || current != job->native_baseline.fields[i])) return -EAGAIN;
            if (i != VLC_ITEM_PARSE_EPG && (*native_fields & VLC_ITEM_PARSE_BIT(i))
                && current >= UINT64_MAX - 1) return -EAGAIN;
        }
        /* A live EPG update has precedence. It does not block other facts. */
        const uint64_t epg = item_owner(target)->parse_revisions.fields[VLC_ITEM_PARSE_EPG];
        if (epg >= UINT64_MAX - 1 || epg != job->native_baseline.fields[VLC_ITEM_PARSE_EPG])
            *native_fields &= ~VLC_ITEM_PARSE_BIT(VLC_ITEM_PARSE_EPG);
    }
    return VLC_SUCCESS;
}
bool vlc_metadata_job_IsCurrentFor(vlc_metadata_job_t *job, input_item_t *target)
{
    if (job == NULL || target == NULL || target != job->target) return false;
    vlc_mutex_lock(&job->backend->lock);
    bool current = job->local && job->started && job->listed && !job->applying && !job->sealed
        && (!job->terminal || job->status == VLC_SUCCESS);
    if (current) {
        uint32_t native_fields = job->local_prepared ? job->local_delta.fields : 0;
        vlc_mutex_lock(&target->lock);
        current = target_current_locked(job, target, true, &native_fields) == VLC_SUCCESS
            && !vlc_input_policy_IsRevoked(job->policy);
        vlc_mutex_unlock(&target->lock);
    }
    vlc_mutex_unlock(&job->backend->lock); return current;
}

int vlc_metadata_job_PrepareFetchedArtwork(vlc_metadata_job_t *anchor,
                    vlc_metadata_job_t *child, input_item_t *working, bool replace_artwork)
{
    if (anchor == NULL || child == NULL || anchor == child || working == NULL)
        return VLC_EINVAL;
    vlc_metadata_job_Hold(anchor); vlc_metadata_job_Hold(child);
    struct vlc_artwork_approval result_proof = {0}, working_proof = {0};
    int status = VLC_SUCCESS;
    const struct vlc_metadata_field_patch *field = NULL;
    uint64_t applied_revision = 0;
    vlc_mutex_lock(&anchor->backend->lock);
    const bool pending = anchor->local && anchor->started && anchor->local_prepared
        && !anchor->terminal && !anchor->commit_cancelled && !anchor->backend->closed
        && !anchor->applying && !anchor->sealed && anchor->local_item == working;
    vlc_mutex_unlock(&anchor->backend->lock);
    if (!pending) { status = -EINTR; goto out; }
    vlc_mutex_lock(&child->backend->lock);
    const bool completed = child->terminal && child->status == VLC_SUCCESS
        && !child->commit_cancelled && !child->backend->closed && child->target == working;
    if (completed)
        for (size_t i = 0; i < child->result.field_count; ++i)
            if (child->patches[i].type == vlc_meta_ArtworkURL) { field = &child->patches[i]; break; }
    if (completed) applied_revision = child->artwork_applied_revision;
    vlc_mutex_unlock(&child->backend->lock);
    if (!completed) { status = VLC_EINVAL; goto out; }
    if (field == NULL) goto out;

    vlc_mutex_lock(&working->lock);
    const uint64_t revision = vlc_meta_FieldRevision(working->p_meta, vlc_meta_ArtworkURL);
    const bool unchanged = revision == field->expected_revision;
    const bool exact = working->p_meta != NULL && applied_revision != 0
        && revision == applied_revision && revision != UINT64_MAX
        && same_string(field->value, vlc_meta_Get(working->p_meta, vlc_meta_ArtworkURL))
        && same_string(child->uri, working->psz_uri)
        && child->policy == item_owner(working)->input_policy;
    vlc_mutex_unlock(&working->lock);
    /* Apply can preserve a current user artwork field. That is not a fetch. */
    if (unchanged) goto out;
    const bool missing_artwork = vlc_artwork_item_HasMissingDisplay(anchor->request.item);
    vlc_mutex_lock(&anchor->request.item->lock);
    const char *original = vlc_meta_Get(anchor->request.item->p_meta, vlc_meta_ArtworkURL);
    const bool protected_artwork = !replace_artwork && !missing_artwork
        && original != NULL && original[0] != '\0';
    vlc_mutex_unlock(&anchor->request.item->lock);
    if (protected_artwork) goto out;
    if (!exact || !vlc_artwork_uri_IsLocalFile(field->value)) { status = -EAGAIN; goto out; }
    status = vlc_artwork_item_PrepareApproval(child->result.item, child->uri,
        child->policy, field->value, &result_proof);
    if (status != VLC_SUCCESS) goto out;
    status = vlc_artwork_item_PrepareApproval(working, child->uri,
        child->policy, field->value, &working_proof);
    if (status != VLC_SUCCESS) goto out;

    vlc_mutex_lock(&anchor->backend->lock);
    const bool ready = anchor->local && anchor->started && anchor->local_prepared
        && !anchor->terminal && !anchor->commit_cancelled && !anchor->backend->closed
        && !anchor->applying && !anchor->sealed && anchor->local_item == working;
    if (!ready) status = -EINTR;
    if (status == VLC_SUCCESS) {
        uint32_t native_fields = anchor->local_delta.fields;
        vlc_mutex_lock(&anchor->target->lock);
        status = target_current_locked(anchor, anchor->target, true, &native_fields);
        if (status == VLC_SUCCESS && (anchor->status_baseline == UINT64_MAX
            || vlc_meta_StatusRevision(anchor->target->p_meta) != anchor->status_baseline
            || vlc_meta_FieldRevision(anchor->target->p_meta, vlc_meta_ArtworkURL)
                != anchor->baseline.fields[vlc_meta_ArtworkURL])) status = -EAGAIN;
        vlc_mutex_unlock(&anchor->target->lock);
    }
    if (status == VLC_SUCCESS) {
        vlc_mutex_lock(&working->lock);
        const bool current = same_string(anchor->uri, working->psz_uri)
            && anchor->policy == item_owner(working)->input_policy
            && !vlc_input_policy_IsRevoked(anchor->policy)
            && vlc_meta_FieldRevision(working->p_meta, vlc_meta_ArtworkURL) == revision
            && same_string(field->value, vlc_meta_Get(working->p_meta, vlc_meta_ArtworkURL));
        vlc_mutex_unlock(&working->lock);
        if (!current) status = -EAGAIN;
    }
    if (status == VLC_SUCCESS) {
        char *old = anchor->fetched_artwork;
        anchor->fetched_artwork = result_proof.uri; result_proof.uri = old;
    }
    vlc_mutex_unlock(&anchor->backend->lock);
out:
    vlc_artwork_approval_Clear(&result_proof); vlc_artwork_approval_Clear(&working_proof);
    vlc_metadata_job_Release(child); vlc_metadata_job_Release(anchor); return status;
}

int vlc_metadata_job_Apply(vlc_metadata_job_t *job, input_item_t *target,
                           uint32_t replace_fields, size_t *applied)
{
    if (applied != NULL) *applied = 0;
    if (job == NULL || target == NULL || target != job->target
        || (replace_fields >> VLC_META_TYPE_COUNT) != 0) return VLC_EINVAL;
    vlc_metadata_job_Hold(job);
    char *values[VLC_META_TYPE_COUNT] = {0};
    struct vlc_artwork_approval artwork = {0};
    bool changes[VLC_META_TYPE_COUNT] = {0};
    unsigned count = 0;
    int status = VLC_SUCCESS;
    bool local_attempt = false;
    bool fetched_artwork = false;
    vlc_mutex_lock(&job->backend->lock);
    const bool ready = job->terminal && job->status == VLC_SUCCESS
        && (!job->local || (!job->applying && !job->sealed));
    if (ready && job->local) { job->applying = true; local_attempt = true; }
    vlc_mutex_unlock(&job->backend->lock);
    if (!ready) { status = VLC_EINVAL; goto out; }
    const bool missing_artwork = vlc_artwork_item_HasMissingDisplay(job->request.item);
    size_t bytes = 0;
    for (size_t i = 0; i < job->result.field_count; ++i) {
        const struct vlc_metadata_field_patch *field = &job->patches[i];
        const char *before = field->expected_value;
        if (before != NULL && before[0] != '\0' && !(replace_fields & (UINT32_C(1) << field->type))
            && !(field->type == vlc_meta_ArtworkURL && missing_artwork)) continue;
        changes[i] = true; ++count;
        const char *value = job->patches[i].value;
        if (value == NULL) continue;
        const size_t length = strnlen(value, MAX_JSON + 1);
        if (length > MAX_JSON || bytes > MAX_JSON - length) { status = VLC_EINVAL; goto out; }
        bytes += length;
        values[i] = strdup(value);
        if (values[i] == NULL) { status = VLC_ENOMEM; goto out; }
        if (field->type == vlc_meta_ArtworkURL) {
            const bool fetched = job->fetched_artwork != NULL
                && same_string(job->fetched_artwork, value);
            /* Only the exact sealed native execution can retain the ordinary
             * native artwork route. A fetched result still needs its proof. */
            const bool native_local = job->local && job->local_prepared && job->policy == NULL
                && !vlc_input_policy_IsAddonSourceURI(job->uri)
                && vlc_meta_FieldRevision(job->local_delta.item->p_meta, vlc_meta_ArtworkURL) != job->baseline.fields[vlc_meta_ArtworkURL]
                && same_string(value, vlc_meta_Get(job->local_delta.item->p_meta, vlc_meta_ArtworkURL));
            status = native_local && !fetched ? VLC_SUCCESS
                : vlc_artwork_item_PrepareApproval(job->result.item, job->uri, job->policy, value, &artwork);
            if (status != VLC_SUCCESS) goto out;
            fetched_artwork = fetched;
        }
    }

    if (job->fetched_artwork != NULL && !fetched_artwork) { status = -EAGAIN; goto out; }

    vlc_mutex_lock(&job->backend->lock);
    vlc_mutex_lock(&target->lock);
    uint32_t native_fields = job->local_prepared ? job->local_delta.fields : 0;
    status = target_current_locked(job, target, false, &native_fields);
    const bool extra = job->local_prepared && job->local_delta.extras;
    const bool local_status = job->local_prepared && job->local_delta.status && target->p_meta != NULL
        && vlc_meta_StatusRevision(target->p_meta) == job->status_baseline;
    const bool write_status = local_status || fetched_artwork;
    uint64_t first = 0;
    if (status == VLC_SUCCESS && !vlc_meta_ReserveRevisions(count + extra + write_status, &first)) status = -EAGAIN;
    if (status == VLC_SUCCESS && vlc_input_policy_IsRevoked(job->policy)) status = -EACCES;
    if (status == VLC_SUCCESS) {
        for (size_t i = 0; i < job->result.field_count; ++i) {
            const struct vlc_metadata_field_patch *field = &job->patches[i];
            if (!changes[i]) continue;
            const uint64_t revision = first++;
            vlc_meta_SetOwnedRevision(target->p_meta, field->type, values[i], revision); values[i] = NULL;
            if (field->type == vlc_meta_ArtworkURL) {
                vlc_artwork_item_SwapApprovalLocked(target, &artwork, revision);
                job->artwork_applied_revision = revision;
            }
            if (applied != NULL) ++*applied;
        }
        if (job->local_prepared) {
            vlc_item_parse_SwapLocked(&job->local_delta, target, native_fields);
            if (extra) vlc_meta_SwapExtraOwned(target->p_meta, job->local_delta.item->p_meta, first++);
            if (write_status) {
                int merged = vlc_meta_GetStatus(target->p_meta);
                if (local_status) merged |= vlc_meta_GetStatus(job->local_delta.item->p_meta);
                if (fetched_artwork) merged |= ITEM_ART_FETCHED;
                vlc_meta_SetStatusOwned(target->p_meta, merged, first++);
            }
            job->local_admitted = true;
        }
    }
    vlc_mutex_unlock(&target->lock);
    vlc_mutex_unlock(&job->backend->lock);
out:
    if (local_attempt) {
        vlc_mutex_lock(&job->backend->lock);
        job->sealed = true; job->applying = false;
        const bool removed = remove_job_locked(job);
        vlc_mutex_unlock(&job->backend->lock);
        if (removed) vlc_metadata_job_Release(job);
    }
    vlc_artwork_approval_Clear(&artwork);
    for (size_t i = 0; i < ARRAY_SIZE(values); ++i) free(values[i]);
    vlc_metadata_job_Release(job); return status;
}
static void dispatch_if_ready(vlc_metadata_job_t *job)
{
    vlc_metadata_backend_t *backend = job->backend;
    vlc_mutex_lock(&backend->lock);
    const bool ready = job->started && !job->terminal && !job->dispatched
        && !backend->closed && backend->suspensions == 0;
    if (ready) job->dispatched = true;
    vlc_mutex_unlock(&backend->lock);
    if (ready && vlc_metadata_job_IsPending(job)) {
        const int status = backend->ops.dispatch(backend->opaque, job);
        if (status != VLC_SUCCESS) vlc_metadata_job_Complete(job, status, NULL, NULL, 0);
    }
}
void vlc_metadata_backend_Resume(vlc_metadata_backend_t *backend)
{
    vlc_metadata_backend_Hold(backend);
    vlc_metadata_job_t *pending[MAX_ACTIVE]; size_t count = 0;
    vlc_mutex_lock(&backend->lock);
    if (backend->suspensions != 0 && --backend->suspensions == 0 && !backend->closed)
        for (vlc_metadata_job_t *job = backend->head; job != NULL; job = job->next)
            if (!job->dispatched) pending[count++] = vlc_metadata_job_Hold(job);
    vlc_mutex_unlock(&backend->lock);
    for (size_t i = 0; i < count; ++i) { dispatch_if_ready(pending[i]); vlc_metadata_job_Release(pending[i]); }
    vlc_metadata_backend_Release(backend);
}
static int start_job(vlc_metadata_job_t *job, bool local)
{
    vlc_metadata_job_Hold(job);
    vlc_metadata_backend_t *backend = job->backend;
    vlc_mutex_lock(&backend->lock);
    if (backend->closed || job->commit_cancelled || job->started || backend->active >= MAX_ACTIVE
        || (local && backend->suspensions != 0)) {
        const int error = backend->closed || job->commit_cancelled ? -EINTR
            : local && backend->suspensions != 0 ? -EAGAIN : VLC_EGENERIC;
        vlc_mutex_unlock(&backend->lock); vlc_metadata_job_Release(job); return error;
    }
    job->started = true;
    job->local = local; job->listed = true;
    job->dispatched = local;
    job->next = backend->head;
    if (backend->head != NULL) backend->head->previous = job;
    backend->head = job;
    ++backend->active;
    vlc_metadata_job_Hold(job); // active-list reference
    vlc_mutex_unlock(&backend->lock);
    if (!local) dispatch_if_ready(job);
    vlc_metadata_job_Release(job);
    return VLC_SUCCESS;
}
int vlc_metadata_job_Start(vlc_metadata_job_t *job)
{ return start_job(job, false); }
int vlc_metadata_job_StartLocal(vlc_metadata_job_t *job)
{ return start_job(job, true); }
size_t vlc_metadata_backend_CancelAutomatic(vlc_metadata_backend_t *backend)
{
    vlc_metadata_backend_Hold(backend);
    vlc_metadata_job_t *pending[MAX_ACTIVE]; size_t count = 0;
    vlc_mutex_lock(&backend->lock);
    for (vlc_metadata_job_t *job = backend->head; job != NULL; job = job->next)
        if (job->request.flags & VLC_METADATA_REQUEST_AUTOMATIC) pending[count++] = vlc_metadata_job_Hold(job);
    vlc_mutex_unlock(&backend->lock);
    for (size_t i = 0; i < count; ++i) { vlc_metadata_job_Cancel(pending[i]); vlc_metadata_job_Release(pending[i]); }
    vlc_metadata_backend_Release(backend); return count;
}
void vlc_metadata_backend_Close(vlc_metadata_backend_t *backend)
{
    vlc_metadata_backend_Hold(backend);
    vlc_mutex_lock(&backend->lock);
    backend->closed = true;
    while (backend->head != NULL) {
        vlc_metadata_job_t *job = vlc_metadata_job_Hold(backend->head);
        vlc_mutex_unlock(&backend->lock);
        vlc_metadata_job_Cancel(job);
        vlc_metadata_job_Release(job);
        vlc_mutex_lock(&backend->lock);
    }
    vlc_mutex_unlock(&backend->lock);
    vlc_metadata_backend_Release(backend);
}
