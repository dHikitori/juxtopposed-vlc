// Actual current Core gate source, plus a private fixture lock observation.
// This does not add a Core export or substitute a native field/status writer.
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#undef NDEBUG
#include "addon_artwork_attempt_keeper.h"
#include <vlc_input_item.h>
#include <vlc_input.h>
#include <vlc_meta.h>
#include <vlc_threads.h>
#include "../../../../../src/preparser/metadata_local.h"
#include "../../../../../src/preparser/metadata_artwork_outcome.c"
#include <assert.h>
struct keeper_fixture {
    input_item_t *target, *working;
    vlc_metadata_backend_t *local_backend, *remote_backend;
    vlc_metadata_job_t *anchor, *child;
};
static int dispatched(void *opaque, vlc_metadata_job_t *job)
{ (void)opaque; (void)job; return VLC_SUCCESS; }
static void ended(vlc_metadata_job_t *job, int status, void *opaque)
{ (void)job; (void)status; (void)opaque; }
struct keeper_fixture *keeper_fixture_new(void)
{
    struct keeper_fixture *f = calloc(1, sizeof(*f)); assert(f);
    const struct vlc_metadata_backend_operations operations = {.dispatch=dispatched};
    f->target = input_item_New("file:///keeper-fixture.flac", "Keeper fixture"); assert(f->target);
    input_item_SetTitle(f->target, "Exact subject");
    vlc_mutex_lock(&f->target->lock);
    vlc_meta_SetStatus(f->target->p_meta, vlc_meta_GetStatus(f->target->p_meta) | ITEM_PREPARSED);
    vlc_mutex_unlock(&f->target->lock);
    f->local_backend = vlc_metadata_backend_New(&operations, NULL); assert(f->local_backend);
    f->remote_backend = vlc_metadata_backend_New(&operations, NULL); assert(f->remote_backend);
    const struct vlc_metadata_request local = {.feature="music.details", .operation="enrich", .item=f->target,
        .flags=VLC_METADATA_REQUEST_AUTOMATIC};
    f->anchor = vlc_metadata_job_New(f->local_backend, &local, ended, NULL); assert(f->anchor);
    assert(vlc_metadata_job_StartLocal(f->anchor) == VLC_SUCCESS);
    f->working = vlc_metadata_job_CopyLocalItem(f->anchor); assert(f->working);
    assert(vlc_metadata_job_PrepareLocal(f->anchor, f->working, NULL) == VLC_SUCCESS);
    const char *fields[] = {"artwork"};
    const struct vlc_metadata_request remote = {.feature="artwork", .operation="enrich", .item=f->working,
        .fields=fields, .field_count=1, .flags=VLC_METADATA_REQUEST_NETWORK};
    f->child = vlc_metadata_job_New(f->remote_backend, &remote, ended, NULL); assert(f->child);
    assert(vlc_metadata_job_Start(f->child) == VLC_SUCCESS); return f;
}
vlc_metadata_job_t *keeper_fixture_child(struct keeper_fixture *f) { return f->child; }
int keeper_fixture_prepare_anchor(struct keeper_fixture *f)
{
    input_item_t *result = input_item_Copy(vlc_metadata_job_GetRequest(f->child)->item);
    if (!result) return VLC_ENOMEM;
    const bool completed = vlc_metadata_job_Complete(f->child, VLC_SUCCESS, result, NULL, 0);
    input_item_Release(result); if (!completed) return VLC_EGENERIC;
    int status = vlc_metadata_job_Apply(f->child, f->working, 0, NULL);
    if (status == VLC_SUCCESS) status = vlc_metadata_job_PrepareNotFoundArtwork(f->anchor, f->child, f->working);
    if (status != VLC_SUCCESS) return status;
    return vlc_metadata_job_Complete(f->anchor, VLC_SUCCESS, f->working, NULL, 0) ? VLC_SUCCESS : VLC_EGENERIC;
}
int keeper_fixture_apply(struct keeper_fixture *f) { return vlc_metadata_job_Apply(f->anchor, f->target, 0, NULL); }
void keeper_fixture_lock_target(struct keeper_fixture *f) { vlc_mutex_lock(&f->target->lock); }
void keeper_fixture_unlock_target(struct keeper_fixture *f) { vlc_mutex_unlock(&f->target->lock); }
int keeper_fixture_status(struct keeper_fixture *f)
{ vlc_mutex_lock(&f->target->lock); int status=vlc_meta_GetStatus(f->target->p_meta); vlc_mutex_unlock(&f->target->lock); return status; }
void keeper_fixture_delete(struct keeper_fixture *f)
{
    if (!f) return;
    vlc_metadata_backend_Close(f->remote_backend); vlc_metadata_backend_Close(f->local_backend);
    vlc_metadata_job_Release(f->child); vlc_metadata_job_Release(f->anchor);
    input_item_Release(f->working); input_item_Release(f->target);
    vlc_metadata_backend_Release(f->remote_backend); vlc_metadata_backend_Release(f->local_backend); free(f);
}
bool keeper_fixture_gate_locked(vlc_metadata_artwork_admission_t *gate)
{
    if (vlc_mutex_trylock(&gate->lock)) return true;
    vlc_mutex_unlock(&gate->lock); return false;
}
bool keeper_fixture_enter(vlc_metadata_artwork_admission_t *gate) { return vlc_metadata_artwork_admission_Enter(gate); }
void keeper_fixture_leave(vlc_metadata_artwork_admission_t *gate) { vlc_metadata_artwork_admission_Leave(gate); }
