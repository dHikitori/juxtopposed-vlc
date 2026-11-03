// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <vlc_common.h>
#include <vlc_atomic.h>
#include <vlc_threads.h>
#include <vlc_medialibrary_work.h>
#include "medialibrary_work.h"

struct vlc_ml_work_owner {
    vlc_atomic_rc_t refs;
    vlc_mutex_t lock;
    vlc_cond_t idle;
    vlc_medialibrary_t *library;
    size_t running;
    bool retiring;
};
struct vlc_ml_work {
    struct vlc_ml_work_owner *owner;
    enum { PENDING, RUNNING, ENDED } state; /* Protected by owner.lock. */
};
struct vlc_ml_work_owner *vlc_ml_work_owner_New(vlc_medialibrary_t *library)
{
    struct vlc_ml_work_owner *owner=calloc(1,sizeof(*owner));
    if (owner == NULL) return NULL;
    vlc_atomic_rc_init(&owner->refs); vlc_mutex_init(&owner->lock); vlc_cond_init(&owner->idle);
    owner->library=library; return owner;
}
void vlc_ml_work_owner_Release(struct vlc_ml_work_owner *owner)
{
    if (owner != NULL && vlc_atomic_rc_dec(&owner->refs)) {
        assert(owner->running == 0); free(owner);
    }
}
vlc_ml_work_t *vlc_ml_work_owner_Acquire(struct vlc_ml_work_owner *owner)
{
    if (owner == NULL) return NULL;
    vlc_ml_work_t *work=malloc(sizeof(*work)); if (work == NULL) return NULL;
    vlc_mutex_lock(&owner->lock);
    if (owner->retiring || owner->library == NULL) {
        vlc_mutex_unlock(&owner->lock); free(work); return NULL;
    }
    vlc_atomic_rc_inc(&owner->refs); work->owner=owner; work->state=PENDING;
    vlc_mutex_unlock(&owner->lock); return work;
}
vlc_medialibrary_t *vlc_ml_work_Begin(vlc_ml_work_t *work)
{
    if (work == NULL) return NULL;
    struct vlc_ml_work_owner *owner=work->owner; vlc_medialibrary_t *library=NULL;
    vlc_mutex_lock(&owner->lock);
    if (work->state == PENDING) {
        if (!owner->retiring && owner->library != NULL && owner->running != SIZE_MAX) {
            work->state=RUNNING; ++owner->running; library=owner->library;
        } else work->state=ENDED;
    }
    vlc_mutex_unlock(&owner->lock); return library;
}
void vlc_ml_work_End(vlc_ml_work_t *work)
{
    if (work == NULL) return;
    struct vlc_ml_work_owner *owner=work->owner;
    vlc_mutex_lock(&owner->lock);
    if (work->state == RUNNING) {
        assert(owner->running > 0); --owner->running;
        if (owner->running == 0) vlc_cond_broadcast(&owner->idle);
    }
    work->state=ENDED; vlc_mutex_unlock(&owner->lock);
}
void vlc_ml_work_Release(vlc_ml_work_t *work)
{
    if (work == NULL) return;
    struct vlc_ml_work_owner *owner=work->owner;
    vlc_mutex_lock(&owner->lock); assert(work->state != RUNNING); vlc_mutex_unlock(&owner->lock);
    free(work); vlc_ml_work_owner_Release(owner);
}
void vlc_ml_work_owner_Retire(struct vlc_ml_work_owner *owner)
{
    if (owner == NULL) return;
    vlc_mutex_lock(&owner->lock); owner->retiring=true;
    while (owner->running != 0) vlc_cond_wait(&owner->idle,&owner->lock);
    owner->library=NULL; vlc_mutex_unlock(&owner->lock);
}
