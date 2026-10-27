// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <vlc_common.h>
#include <vlc_atomic.h>
#include <vlc_metadata_owner.h>
#include <vlc_threads.h>
#include <stdatomic.h>
#include "metadata_requests.h"

#define MAX_REQUESTS 256u
struct metadata_entry {
    vlc_atomic_rc_t refs;
    struct vlc_metadata_requests *owner;
    struct metadata_entry *next;
    vlc_metadata_job_t *job;
    const void *key;
    vlc_metadata_requests_cb callback;
    void *userdata;
    atomic_bool cancel_requested;
    bool listed;
};
struct vlc_metadata_requests {
    vlc_atomic_rc_t refs;
    vlc_object_t *instance;
    vlc_mutex_t lock;
    vlc_cond_t idle;
    struct metadata_entry *entries;
    unsigned count, callbacks;
    bool closed, deleting;
};
struct metadata_callback_frame {
    vlc_metadata_requests_t *owner;
    struct metadata_callback_frame *previous;
};
static _Thread_local struct metadata_callback_frame *callback_frame;

static void owner_release(vlc_metadata_requests_t *owner)
{
    if (!vlc_atomic_rc_dec(&owner->refs)) return;
    assert(owner->entries == NULL && owner->count == 0 && owner->callbacks == 0);
    free(owner);
}
static struct metadata_entry *entry_hold(struct metadata_entry *entry)
{ vlc_atomic_rc_inc(&entry->refs); return entry; }
static void entry_release(struct metadata_entry *entry)
{
    if (!vlc_atomic_rc_dec(&entry->refs)) return;
    if (entry->job != NULL) vlc_metadata_job_Release(entry->job);
    owner_release(entry->owner); free(entry);
}
static bool remove_locked(vlc_metadata_requests_t *owner, struct metadata_entry *entry)
{
    if (!entry->listed) return false;
    struct metadata_entry **link = &owner->entries;
    while (*link != entry) link = &(*link)->next;
    *link = entry->next; entry->listed = false; --owner->count;
    vlc_cond_broadcast(&owner->idle); return true;
}
static void ended(vlc_metadata_job_t *job, int status, void *opaque)
{
    struct metadata_entry *entry = opaque;
    vlc_metadata_requests_t *owner = entry->owner;
    vlc_mutex_lock(&owner->lock);
    const bool listed = remove_locked(owner, entry);
    ++owner->callbacks;
    vlc_mutex_unlock(&owner->lock);
    if (listed) entry_release(entry);
    struct metadata_callback_frame frame = {owner, callback_frame}; callback_frame = &frame;
    entry->callback(job, status, entry->userdata);
    callback_frame = frame.previous;
    vlc_mutex_lock(&owner->lock);
    --owner->callbacks; vlc_cond_broadcast(&owner->idle);
    vlc_mutex_unlock(&owner->lock);
    entry_release(entry);
}
vlc_metadata_requests_t *vlc_metadata_requests_New(vlc_object_t *object)
{
    if (object == NULL) return NULL;
    vlc_metadata_requests_t *owner = calloc(1, sizeof(*owner));
    if (owner == NULL) return NULL;
    vlc_atomic_rc_init(&owner->refs);
    owner->instance = VLC_OBJECT(vlc_object_instance(object));
    vlc_mutex_init(&owner->lock); vlc_cond_init(&owner->idle); return owner;
}
int vlc_metadata_requests_Push(vlc_metadata_requests_t *owner, const struct vlc_metadata_request *request,
                              const void *key, vlc_metadata_requests_cb callback, void *userdata)
{
    if (owner == NULL || request == NULL || callback == NULL) return VLC_EINVAL;
    bool owned = false; vlc_metadata_backend_t *backend = vlc_metadata_owner_Hold(owner->instance, &owned);
    vlc_mutex_lock(&owner->lock); const bool closed = owner->closed; vlc_mutex_unlock(&owner->lock);
    if (closed) { if (backend != NULL) vlc_metadata_backend_Release(backend); return -EINTR; }
    if (!owned) return VLC_ENOENT;
    if (backend == NULL || vlc_metadata_backend_IsClosed(backend)) {
        if (backend != NULL) vlc_metadata_backend_Release(backend);
        return -EINTR;
    }
    struct metadata_entry *entry = calloc(1, sizeof(*entry));
    if (entry == NULL) { vlc_metadata_backend_Release(backend); return VLC_ENOMEM; }
    vlc_atomic_rc_init(&entry->refs); vlc_atomic_rc_inc(&owner->refs);
    entry->owner = owner; entry->key = key; entry->callback = callback; entry->userdata = userdata;
    atomic_init(&entry->cancel_requested, false);
    entry->job = vlc_metadata_job_New(backend, request, ended, entry); vlc_metadata_backend_Release(backend);
    if (entry->job == NULL) { entry_release(entry); return VLC_ENOMEM; }
    vlc_mutex_lock(&owner->lock);
    if (owner->closed || owner->count >= MAX_REQUESTS) {
        const int status = owner->closed ? -EINTR : VLC_ENOMEM;
        vlc_mutex_unlock(&owner->lock); entry_release(entry); return status;
    }
    entry->next = owner->entries; owner->entries = entry_hold(entry); entry->listed = true; ++owner->count;
    vlc_mutex_unlock(&owner->lock);
    entry_hold(entry);
    const int status = vlc_metadata_job_Start(entry->job);
    if (status != VLC_SUCCESS) {
        vlc_mutex_lock(&owner->lock); const bool listed = remove_locked(owner, entry); vlc_mutex_unlock(&owner->lock);
        if (listed) entry_release(entry);
        entry_release(entry);
    } else if (atomic_load(&entry->cancel_requested)) vlc_metadata_job_Cancel(entry->job);
    entry_release(entry); return status;
}
size_t vlc_metadata_requests_Cancel(vlc_metadata_requests_t *owner, const void *key)
{
    struct metadata_entry *entries[MAX_REQUESTS]; size_t count = 0;
    vlc_mutex_lock(&owner->lock);
    for (struct metadata_entry *entry = owner->entries; entry != NULL; entry = entry->next) {
        if (key != NULL && entry->key != key) continue;
        atomic_store(&entry->cancel_requested, true); entries[count++] = entry_hold(entry);
    }
    vlc_mutex_unlock(&owner->lock);
    for (size_t i = 0; i < count; ++i) { vlc_metadata_job_Cancel(entries[i]->job); entry_release(entries[i]); }
    return count;
}
void vlc_metadata_requests_Close(vlc_metadata_requests_t *owner)
{
    vlc_mutex_lock(&owner->lock); owner->closed = true; vlc_mutex_unlock(&owner->lock);
    vlc_metadata_requests_Cancel(owner, NULL);
}
void vlc_metadata_requests_Delete(vlc_metadata_requests_t *owner)
{
    if (owner == NULL) return;
    vlc_mutex_lock(&owner->lock);
    if (owner->deleting) { vlc_mutex_unlock(&owner->lock); return; }
    owner->deleting = true; vlc_mutex_unlock(&owner->lock);
    vlc_metadata_requests_Close(owner);
    unsigned active_here = 0;
    for (const struct metadata_callback_frame *frame = callback_frame; frame != NULL; frame = frame->previous)
        if (frame->owner == owner) ++active_here;
    vlc_mutex_lock(&owner->lock);
    while (owner->count != 0 || owner->callbacks > active_here) vlc_cond_wait(&owner->idle, &owner->lock);
    vlc_mutex_unlock(&owner->lock); owner_release(owner);
}
