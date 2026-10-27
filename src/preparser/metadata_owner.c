// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <vlc_common.h>
#include <vlc_metadata_owner.h>
#include <vlc_threads.h>
#include <vlc_atomic.h>
#include "../libvlc.h"
#include "../misc/interrupt.h"
#include "metadata_owner.h"

#define MAX_OWNERS 32u
#define MAX_LEGACY 1024u
struct metadata_owner {
    libvlc_int_t *instance;
    vlc_metadata_backend_t *backend;
    struct metadata_owner *next;
    bool suspended;
};
struct vlc_metadata_legacy {
    vlc_atomic_rc_t refs;
    libvlc_int_t *instance;
    struct vlc_metadata_legacy *next;
    vlc_interrupt_t *interrupt;
    enum { LEGACY_PENDING, LEGACY_RUNNING, LEGACY_ENDED } state;
    bool cancelled, listed;
};
static vlc_mutex_t owner_lock = VLC_STATIC_MUTEX;
static struct metadata_owner *owners;
static unsigned owner_count;
static vlc_metadata_legacy_t *legacy;
static unsigned legacy_count;

static struct metadata_owner *find_owner(libvlc_int_t *instance)
{
    struct metadata_owner *entry = owners;
    while (entry != NULL && entry->instance != instance) entry = entry->next;
    return entry;
}
static bool legacy_running(libvlc_int_t *instance)
{
    for (vlc_metadata_legacy_t *entry = legacy; entry != NULL; entry = entry->next)
        if (entry->instance == instance && entry->state == LEGACY_RUNNING) return true;
    return false;
}
static vlc_metadata_legacy_t *legacy_hold(vlc_metadata_legacy_t *entry)
{ vlc_atomic_rc_inc(&entry->refs); return entry; }
static void legacy_release(vlc_metadata_legacy_t *entry)
{
    if (!vlc_atomic_rc_dec(&entry->refs)) return;
    assert(!entry->listed); vlc_interrupt_destroy(entry->interrupt); free(entry);
}
static size_t cancel_legacy_locked(libvlc_int_t *instance, vlc_metadata_legacy_t **pending)
{
    size_t count = 0;
    for (vlc_metadata_legacy_t *entry = legacy; entry != NULL; entry = entry->next)
        if (entry->instance == instance) { entry->cancelled = true; pending[count++] = legacy_hold(entry); }
    return count;
}
static void interrupt_legacy(vlc_metadata_legacy_t **pending, size_t count)
{
    for (size_t i = 0; i < count; ++i) { vlc_interrupt_kill(pending[i]->interrupt); legacy_release(pending[i]); }
}
static void remove_legacy_locked(vlc_metadata_legacy_t *entry)
{
    if (!entry->listed) return;
    vlc_metadata_legacy_t **link = &legacy;
    while (*link != entry) link = &(*link)->next;
    *link = entry->next; entry->listed = false; --legacy_count;
}

vlc_metadata_legacy_t *vlc_metadata_legacy_Acquire(vlc_object_t *object)
{
    if (object == NULL) return NULL;
    vlc_metadata_legacy_t *entry = calloc(1, sizeof(*entry));
    if (entry == NULL) return NULL;
    entry->interrupt = vlc_interrupt_create();
    if (entry->interrupt == NULL) { free(entry); return NULL; }
    vlc_atomic_rc_init(&entry->refs);
    libvlc_int_t *instance = vlc_object_instance(object);
    vlc_mutex_lock(&owner_lock);
    const bool accepted = legacy_count < MAX_LEGACY && !libvlc_priv(instance)->metadata_owner_retiring
        && find_owner(instance) == NULL;
    if (accepted) {
        entry->instance = instance; entry->next = legacy; legacy = entry; entry->listed = true; ++legacy_count;
    }
    vlc_mutex_unlock(&owner_lock);
    if (!accepted) { legacy_release(entry); return NULL; }
    return entry;
}
bool vlc_metadata_legacy_Begin(vlc_metadata_legacy_t *entry)
{
    if (entry == NULL) return false;
    vlc_mutex_lock(&owner_lock);
    const bool accepted = entry->listed && entry->state == LEGACY_PENDING && !entry->cancelled && entry->instance != NULL
        && !atomic_load_explicit(&entry->interrupt->killed, memory_order_acquire)
        && !libvlc_priv(entry->instance)->metadata_owner_retiring && find_owner(entry->instance) == NULL;
    if (accepted) entry->state = LEGACY_RUNNING;
    vlc_mutex_unlock(&owner_lock); return accepted;
}
vlc_interrupt_t *vlc_metadata_legacy_Interrupt(vlc_metadata_legacy_t *entry)
{ return entry != NULL ? entry->interrupt : NULL; }
void vlc_metadata_legacy_Cancel(vlc_metadata_legacy_t *entry)
{
    if (entry == NULL) return;
    legacy_hold(entry); vlc_mutex_lock(&owner_lock); entry->cancelled = true; vlc_mutex_unlock(&owner_lock);
    vlc_interrupt_kill(entry->interrupt); legacy_release(entry);
}
void vlc_metadata_legacy_End(vlc_metadata_legacy_t *entry)
{
    if (entry == NULL) return;
    legacy_hold(entry); vlc_mutex_lock(&owner_lock);
    libvlc_int_t *instance = entry->instance;
    remove_legacy_locked(entry); entry->state = LEGACY_ENDED; entry->instance = NULL;
    struct metadata_owner *owner = instance != NULL ? find_owner(instance) : NULL;
    vlc_metadata_backend_t *resume = NULL;
    if (owner != NULL && owner->suspended && !legacy_running(instance)) {
        owner->suspended = false; resume = vlc_metadata_backend_Hold(owner->backend);
    }
    vlc_mutex_unlock(&owner_lock);
    if (resume != NULL) { vlc_metadata_backend_Resume(resume); vlc_metadata_backend_Release(resume); }
    legacy_release(entry);
}
void vlc_metadata_legacy_Release(vlc_metadata_legacy_t *entry)
{
    if (entry == NULL) return;
    vlc_mutex_lock(&owner_lock); assert(entry->state != LEGACY_RUNNING); vlc_mutex_unlock(&owner_lock);
    vlc_metadata_legacy_End(entry); legacy_release(entry);
}

int vlc_metadata_owner_Set(vlc_object_t *object, vlc_metadata_backend_t *backend)
{
    if (object == NULL || backend == NULL || vlc_metadata_backend_IsClosed(backend)) return VLC_EINVAL;
    libvlc_int_t *instance = vlc_object_instance(object);
    vlc_metadata_backend_t *previous = NULL; bool resume_previous = false;
    vlc_metadata_legacy_t *pending[MAX_LEGACY]; size_t count = 0;
    vlc_mutex_lock(&owner_lock);
    if (libvlc_priv(instance)->metadata_owner_retiring) { vlc_mutex_unlock(&owner_lock); return -EINTR; }
    struct metadata_owner *entry = find_owner(instance), *allocated = NULL;
    int status = VLC_SUCCESS;
    if (entry != NULL && entry->backend == backend) {}
    else if (entry != NULL && !vlc_metadata_backend_IsClosed(entry->backend)) status = -EBUSY;
    else {
        if (entry == NULL) {
            if (owner_count >= MAX_OWNERS) status = VLC_ENOMEM;
            else {
                allocated = calloc(1, sizeof(*allocated)); entry = allocated;
                if (entry == NULL) status = VLC_ENOMEM;
                else entry->instance = instance;
            }
        }
        if (status == VLC_SUCCESS) {
            const bool suspend = legacy_running(instance);
            if (suspend && !vlc_metadata_backend_Suspend(backend)) status = -EBUSY;
            else {
                if (allocated != NULL) { entry->next = owners; owners = entry; ++owner_count; allocated = NULL; }
                previous = entry->backend; resume_previous = entry->suspended;
                entry->backend = vlc_metadata_backend_Hold(backend); entry->suspended = suspend;
                count = cancel_legacy_locked(instance, pending);
            }
        }
    }
    vlc_mutex_unlock(&owner_lock);
    free(allocated);
    interrupt_legacy(pending, count);
    if (previous != NULL) {
        if (resume_previous) vlc_metadata_backend_Resume(previous);
        vlc_metadata_backend_Release(previous);
    }
    return status;
}

vlc_metadata_backend_t *vlc_metadata_owner_Hold(vlc_object_t *object, bool *owned)
{
    if (owned != NULL) *owned = false;
    if (object == NULL) return NULL;
    libvlc_int_t *instance = vlc_object_instance(object);
    vlc_mutex_lock(&owner_lock);
    const struct metadata_owner *entry = owners;
    while (entry != NULL && entry->instance != instance) entry = entry->next;
    vlc_metadata_backend_t *backend = entry != NULL ? vlc_metadata_backend_Hold(entry->backend) : NULL;
    if (owned != NULL) *owned = entry != NULL || libvlc_priv(instance)->metadata_owner_retiring;
    vlc_mutex_unlock(&owner_lock);
    return backend;
}

void vlc_metadata_owner_Close(vlc_object_t *object)
{
    if (object == NULL) return;
    libvlc_int_t *instance = vlc_object_instance(object);
    vlc_mutex_lock(&owner_lock);
    libvlc_priv_t *priv = libvlc_priv(instance);
    if (priv->metadata_owner_retiring) { vlc_mutex_unlock(&owner_lock); return; }
    priv->metadata_owner_retiring = true;
    struct metadata_owner *entry = find_owner(instance);
    vlc_metadata_backend_t *backend = entry != NULL ? vlc_metadata_backend_Hold(entry->backend) : NULL;
    const bool suspended = entry != NULL && entry->suspended;
    if (entry != NULL) entry->suspended = false;
    vlc_metadata_legacy_t *pending[MAX_LEGACY]; const size_t count = cancel_legacy_locked(instance, pending);
    vlc_mutex_unlock(&owner_lock);
    if (backend != NULL) {
        vlc_metadata_backend_Close(backend);
        if (suspended) vlc_metadata_backend_Resume(backend);
        vlc_metadata_backend_Release(backend);
    }
    interrupt_legacy(pending, count);
}
void vlc_metadata_owner_Forget(vlc_object_t *object)
{
    if (object == NULL) return;
    vlc_metadata_owner_Close(object);
    libvlc_int_t *instance = vlc_object_instance(object);
    vlc_mutex_lock(&owner_lock);
    struct metadata_owner **link = &owners;
    while (*link != NULL && (*link)->instance != instance) link = &(*link)->next;
    struct metadata_owner *entry = *link;
    if (entry != NULL) { *link = entry->next; --owner_count; }
    vlc_metadata_legacy_t *scope = legacy;
    while (scope != NULL) {
        vlc_metadata_legacy_t *next = scope->next;
        if (scope->instance == instance) { remove_legacy_locked(scope); scope->instance = NULL; scope->cancelled = true; }
        scope = next;
    }
    vlc_mutex_unlock(&owner_lock);
    if (entry != NULL) { vlc_metadata_backend_Release(entry->backend); free(entry); }
}
