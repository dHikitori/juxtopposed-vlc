// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <vlc_common.h>
#include <vlc_source_owner.h>
#include <vlc_threads.h>
#include "../libvlc.h"
#include "source_owner.h"

#define MAX_SOURCE_OWNERS 32u
struct source_owner {
    libvlc_int_t *instance;
    vlc_source_backend_t *backend;
    struct source_owner *next;
};
static vlc_mutex_t owner_lock = VLC_STATIC_MUTEX;
static struct source_owner *owners;
static unsigned owner_count;

int vlc_source_owner_Set(vlc_object_t *object, vlc_source_backend_t *backend)
{
    if (object == NULL || backend == NULL || vlc_source_backend_IsClosed(backend)) return VLC_EINVAL;
    libvlc_int_t *instance = vlc_object_instance(object);
    vlc_mutex_lock(&owner_lock);
    if (libvlc_priv(instance)->source_owner_retiring) {
        vlc_mutex_unlock(&owner_lock);
        return -EINTR;
    }
    struct source_owner *entry = owners;
    while (entry != NULL && entry->instance != instance) entry = entry->next;
    int status = VLC_SUCCESS; vlc_source_backend_t *previous = NULL;
    if (entry != NULL && entry->backend == backend) {}
    else if (entry != NULL && !vlc_source_backend_IsClosed(entry->backend)) status = -EBUSY;
    else {
        if (entry == NULL) {
            if (owner_count >= MAX_SOURCE_OWNERS) status = VLC_ENOMEM;
            else {
                entry = calloc(1, sizeof(*entry));
                if (entry == NULL) status = VLC_ENOMEM;
                else { entry->instance = instance; entry->next = owners; owners = entry; ++owner_count; }
            }
        }
        if (status == VLC_SUCCESS) { previous = entry->backend; entry->backend = vlc_source_backend_Hold(backend); }
    }
    vlc_mutex_unlock(&owner_lock);
    if (previous != NULL) vlc_source_backend_Release(previous);
    return status;
}
vlc_source_backend_t *vlc_source_owner_Hold(vlc_object_t *object)
{
    if (object == NULL) return NULL;
    libvlc_int_t *instance = vlc_object_instance(object);
    vlc_mutex_lock(&owner_lock);
    const struct source_owner *entry = owners;
    while (entry != NULL && entry->instance != instance) entry = entry->next;
    vlc_source_backend_t *backend = entry != NULL ? vlc_source_backend_Hold(entry->backend) : NULL;
    vlc_mutex_unlock(&owner_lock); return backend;
}
void vlc_source_owner_Close(vlc_object_t *object)
{
    if (object == NULL) return;
    libvlc_int_t *instance = vlc_object_instance(object);
    vlc_mutex_lock(&owner_lock);
    libvlc_priv_t *priv = libvlc_priv(instance);
    if (priv->source_owner_retiring) { vlc_mutex_unlock(&owner_lock); return; }
    priv->source_owner_retiring = true;
    struct source_owner *entry = owners;
    while (entry != NULL && entry->instance != instance) entry = entry->next;
    if (entry == NULL) { vlc_mutex_unlock(&owner_lock); return; }
    vlc_source_backend_t *backend = vlc_source_backend_Hold(entry->backend);
    vlc_mutex_unlock(&owner_lock);
    vlc_source_backend_Close(backend); vlc_source_backend_Release(backend);
}
void vlc_source_owner_Forget(vlc_object_t *object)
{
    if (object == NULL) return;
    vlc_source_owner_Close(object);
    libvlc_int_t *instance = vlc_object_instance(object);
    vlc_mutex_lock(&owner_lock);
    struct source_owner **link = &owners;
    while (*link != NULL && (*link)->instance != instance) link = &(*link)->next;
    struct source_owner *entry = *link;
    if (entry != NULL) { *link = entry->next; --owner_count; }
    vlc_mutex_unlock(&owner_lock);
    if (entry != NULL) { vlc_source_backend_Release(entry->backend); free(entry); }
}
