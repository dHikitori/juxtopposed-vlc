// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <vlc_common.h>
#include <vlc_atomic.h>
#include <vlc_threads.h>
#include <stdatomic.h>
#include "metadata_artwork_outcome.h"

struct vlc_metadata_artwork_admission {
    vlc_atomic_rc_t refs;
    vlc_mutex_t lock;
    atomic_bool open;
    vlc_tick_t deadline;
};
vlc_metadata_artwork_admission_t *vlc_metadata_artwork_admission_New(vlc_tick_t deadline)
{
    const vlc_tick_t now = vlc_tick_now();
    if (deadline <= now || deadline - now > VLC_TICK_FROM_SEC(30)) return NULL;
    vlc_metadata_artwork_admission_t *admission = malloc(sizeof(*admission));
    if (admission == NULL) return NULL;
    /* The private deadline is immutable after publication. */
    admission->deadline = deadline;
    vlc_atomic_rc_init(&admission->refs);
    vlc_mutex_init(&admission->lock); atomic_init(&admission->open, true);
    return admission;
}
vlc_metadata_artwork_admission_t *vlc_metadata_artwork_admission_Hold(vlc_metadata_artwork_admission_t *admission)
{ if (admission != NULL) vlc_atomic_rc_inc(&admission->refs); return admission; }
void vlc_metadata_artwork_admission_Release(vlc_metadata_artwork_admission_t *admission)
{ if (admission != NULL && vlc_atomic_rc_dec(&admission->refs)) free(admission); }
bool vlc_metadata_artwork_admission_IsCurrent(const vlc_metadata_artwork_admission_t *admission)
{
    return admission != NULL && atomic_load_explicit(&admission->open, memory_order_acquire)
        && vlc_tick_now() < admission->deadline;
}
bool vlc_metadata_artwork_admission_Enter(vlc_metadata_artwork_admission_t *admission)
{
    if (admission == NULL) return false;
    vlc_mutex_lock(&admission->lock);
    if (vlc_metadata_artwork_admission_IsCurrent(admission)) return true;
    vlc_mutex_unlock(&admission->lock); return false;
}
void vlc_metadata_artwork_admission_Leave(vlc_metadata_artwork_admission_t *admission)
{ vlc_mutex_unlock(&admission->lock); }
void vlc_metadata_artwork_admission_Revoke(vlc_metadata_artwork_admission_t *admission)
{
    if (admission == NULL) return;
    atomic_store_explicit(&admission->open, false, memory_order_release);
    /* Never return early on an already closed flag: another revoker can still
     * be waiting for an admitted Apply behind this same native mutex. */
    vlc_mutex_lock(&admission->lock); vlc_mutex_unlock(&admission->lock);
}
