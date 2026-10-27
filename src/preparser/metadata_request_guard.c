// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <vlc_common.h>
#include <vlc_atomic.h>
#include <vlc_threads.h>
#include <stdatomic.h>
#include "metadata_request_guard.h"
#include "metadata_local.h"

struct vlc_metadata_pipeline_guard {
    vlc_atomic_rc_t refs;
    vlc_mutex_t lock;
    atomic_bool decided, committed;
    int status;
};
vlc_metadata_pipeline_guard_t *vlc_metadata_pipeline_guard_New(void)
{
    vlc_metadata_pipeline_guard_t *guard = malloc(sizeof(*guard));
    if (guard != NULL) {
        vlc_atomic_rc_init(&guard->refs); vlc_mutex_init(&guard->lock);
        atomic_init(&guard->decided, false); atomic_init(&guard->committed, false);
        guard->status = VLC_EGENERIC;
    }
    return guard;
}
vlc_metadata_pipeline_guard_t *vlc_metadata_pipeline_guard_Hold(vlc_metadata_pipeline_guard_t *guard)
{ if (guard != NULL) vlc_atomic_rc_inc(&guard->refs); return guard; }
void vlc_metadata_pipeline_guard_Release(vlc_metadata_pipeline_guard_t *guard)
{ if (guard != NULL && vlc_atomic_rc_dec(&guard->refs)) free(guard); }
bool vlc_metadata_pipeline_guard_TryCancel(vlc_metadata_pipeline_guard_t *guard)
{
    if (guard == NULL) return false;
    vlc_mutex_lock(&guard->lock);
    const bool won = !atomic_load_explicit(&guard->decided, memory_order_relaxed);
    if (won) { guard->status = -EINTR; atomic_store_explicit(&guard->decided, true, memory_order_release); }
    vlc_mutex_unlock(&guard->lock); return won;
}
void vlc_metadata_pipeline_guard_Cancel(vlc_metadata_pipeline_guard_t *guard)
{ (void)vlc_metadata_pipeline_guard_TryCancel(guard); }
bool vlc_metadata_pipeline_guard_IsCurrent(vlc_metadata_pipeline_guard_t *guard)
{ return guard == NULL || !atomic_load_explicit(&guard->decided, memory_order_acquire); }
int vlc_metadata_pipeline_guard_Finish(vlc_metadata_pipeline_guard_t *guard, int status)
{
    if (guard == NULL) return status;
    vlc_mutex_lock(&guard->lock);
    if (!atomic_load_explicit(&guard->decided, memory_order_relaxed)) {
        guard->status = status; atomic_store_explicit(&guard->decided, true, memory_order_release);
    }
    const int result = guard->status;
    vlc_mutex_unlock(&guard->lock); return result;
}
int vlc_metadata_pipeline_guard_Commit(vlc_metadata_pipeline_guard_t *guard,
    vlc_metadata_job_t *job, input_item_t *target, uint32_t replace_fields, size_t *applied,
    struct vlc_metadata_local_events *events)
{
    if (applied != NULL) *applied = 0;
    if (guard == NULL || job == NULL || target == NULL) return VLC_EINVAL;
    if (events != NULL && (events->subtrees != NULL || events->attachments != NULL
        || events->subtree_count != 0 || events->attachment_count != 0)) return VLC_EINVAL;
    vlc_metadata_pipeline_guard_Hold(guard); vlc_metadata_job_Hold(job);
    vlc_mutex_lock(&guard->lock);
    int status;
    if (atomic_load_explicit(&guard->decided, memory_order_relaxed))
        status = guard->status == VLC_SUCCESS ? VLC_EINVAL : guard->status;
    else {
        status = vlc_metadata_job_Apply(job, target, replace_fields, applied);
        if (status == VLC_SUCCESS && events != NULL) vlc_metadata_job_TakeLocalEvents(job, events);
        guard->status = status;
        atomic_store_explicit(&guard->committed, status == VLC_SUCCESS, memory_order_release);
        atomic_store_explicit(&guard->decided, true, memory_order_release);
    }
    vlc_mutex_unlock(&guard->lock);
    vlc_metadata_job_Release(job); vlc_metadata_pipeline_guard_Release(guard); return status;
}
bool vlc_metadata_pipeline_guard_IsCommitted(vlc_metadata_pipeline_guard_t *guard)
{ return guard != NULL && atomic_load_explicit(&guard->committed, memory_order_acquire); }
bool vlc_metadata_pipeline_guard_GetStatus(vlc_metadata_pipeline_guard_t *guard, int *status)
{
    if (guard == NULL || status == NULL) return false;
    vlc_mutex_lock(&guard->lock);
    const bool decided = atomic_load_explicit(&guard->decided, memory_order_relaxed);
    if (decided) *status = guard->status;
    vlc_mutex_unlock(&guard->lock); return decided;
}
