// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <vlc_common.h>
#include <vlc_metadata_backend.h>
#include <vlc_atomic.h>
#include <vlc_input_item.h>
#include <vlc_input_policy.h>
#include <vlc_metadata_revision.h>
#include <vlc_threads.h>
#include "../input/item.h"
#include "../input/artwork_policy.h"
#include "../input/metadata_revision.h"

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
            job->uri = uri; job->policy = policy; job->baseline = before; return true;
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
            status = vlc_artwork_item_PrepareApproval(job->result.item, job->uri, job->policy, value, &artwork);
            if (status != VLC_SUCCESS) goto out;
        }
    }

    static const vlc_meta_type_t identity_fields[] = {
        vlc_meta_Title, vlc_meta_Artist, vlc_meta_Album, vlc_meta_AlbumArtist,
        vlc_meta_Date, vlc_meta_TrackID, vlc_meta_TrackNumber, vlc_meta_DiscNumber,
        vlc_meta_ShowName, vlc_meta_Season, vlc_meta_Episode,
    };
    vlc_mutex_lock(&job->backend->lock);
    vlc_mutex_lock(&target->lock);
    if (job->backend->closed || job->commit_cancelled) status = -EINTR;
    else if (job->backend->suspensions != 0) status = -EAGAIN;
    else if (target->p_meta == NULL || !same_string(job->uri, target->psz_uri)
        || job->policy != item_owner(target)->input_policy
        || vlc_meta_ExtraRevision(target->p_meta) != job->baseline.extra) status = -EAGAIN;
    for (size_t i = 0; status == VLC_SUCCESS && i < ARRAY_SIZE(identity_fields); ++i) {
        const vlc_meta_type_t type = identity_fields[i];
        if (vlc_meta_FieldRevision(target->p_meta, type) != job->baseline.fields[type]) status = -EAGAIN;
    }
    for (size_t i = 0; status == VLC_SUCCESS && i < job->result.field_count; ++i) {
        const struct vlc_metadata_field_patch *field = &job->patches[i];
        if (field->expected_revision == UINT64_MAX
            || vlc_meta_FieldRevision(target->p_meta, field->type) != field->expected_revision
            || !same_string(field->expected_value, vlc_meta_Get(target->p_meta, field->type))) status = -EAGAIN;
    }
    uint64_t first = 0;
    if (status == VLC_SUCCESS && !vlc_meta_ReserveRevisions(count, &first)) status = -EAGAIN;
    if (status == VLC_SUCCESS && vlc_input_policy_IsRevoked(job->policy)) status = -EACCES;
    if (status == VLC_SUCCESS) {
        for (size_t i = 0; i < job->result.field_count; ++i) {
            const struct vlc_metadata_field_patch *field = &job->patches[i];
            if (!changes[i]) continue;
            const uint64_t revision = first++;
            vlc_meta_SetOwnedRevision(target->p_meta, field->type, values[i], revision); values[i] = NULL;
            if (field->type == vlc_meta_ArtworkURL) vlc_artwork_item_SwapApprovalLocked(target, &artwork, revision);
            if (applied != NULL) ++*applied;
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
