// SPDX-License-Identifier: LGPL-2.1-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#undef NDEBUG
#include <vlc_common.h>
#include <vlc_threads.h>
#include <stdatomic.h>
#include "preparser/metadata_artwork_outcome.h"
#include <assert.h>
#include <stdio.h>

struct revoke_call { vlc_metadata_artwork_admission_t *admission; vlc_sem_t entered; atomic_bool done; };
static void *revoke_native_admission(void *opaque)
{
    struct revoke_call *call = opaque;
    vlc_sem_post(&call->entered);
    vlc_metadata_artwork_admission_Revoke(call->admission);
    atomic_store(&call->done, true); return NULL;
}
static void repeated_revokers_join_the_admitted_native_section(void)
{
    vlc_metadata_artwork_admission_t *gate = vlc_metadata_artwork_admission_New(vlc_tick_now() + VLC_TICK_FROM_SEC(5)); assert(gate);
    assert(vlc_metadata_artwork_admission_Enter(gate));
    struct revoke_call calls[2]; vlc_thread_t threads[2];
    for (unsigned i = 0; i < 2; ++i) {
        calls[i].admission = gate; vlc_sem_init(&calls[i].entered, 0); atomic_init(&calls[i].done, false);
        assert(vlc_clone(&threads[i], revoke_native_admission, &calls[i]) == 0); vlc_sem_wait(&calls[i].entered);
    }
    const vlc_tick_t deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(2);
    while (vlc_metadata_artwork_admission_IsCurrent(gate) && vlc_tick_now() < deadline) vlc_tick_sleep(VLC_TICK_FROM_MS(100));
    assert(!vlc_metadata_artwork_admission_IsCurrent(gate));
    assert(!atomic_load(&calls[0].done) && !atomic_load(&calls[1].done));
    vlc_metadata_artwork_admission_Leave(gate);
    for (unsigned i = 0; i < 2; ++i) { vlc_join(threads[i], NULL); assert(atomic_load(&calls[i].done)); }
    assert(!vlc_metadata_artwork_admission_Enter(gate));
    vlc_metadata_artwork_admission_Revoke(gate); vlc_metadata_artwork_admission_Release(gate);
    puts("PASS: repeated revocation joins an admitted native section");
}
static void deadlines_and_retained_native_ownership(void)
{
    assert(!vlc_metadata_artwork_admission_New(vlc_tick_now() - 1));
    assert(!vlc_metadata_artwork_admission_New(vlc_tick_now() + VLC_TICK_FROM_SEC(31)));
    assert(!vlc_metadata_artwork_admission_IsCurrent(NULL));
    assert(!vlc_metadata_artwork_admission_Enter(NULL));
    vlc_metadata_artwork_admission_t *gate = vlc_metadata_artwork_admission_New(vlc_tick_now() + VLC_TICK_FROM_MS(50)); assert(gate);
    vlc_metadata_artwork_admission_Hold(gate); vlc_metadata_artwork_admission_Release(gate);
    assert(vlc_metadata_artwork_admission_IsCurrent(gate));
    vlc_tick_sleep(VLC_TICK_FROM_MS(60));
    assert(!vlc_metadata_artwork_admission_IsCurrent(gate) && !vlc_metadata_artwork_admission_Enter(gate));
    vlc_metadata_artwork_admission_Revoke(gate); vlc_metadata_artwork_admission_Release(gate);
    puts("PASS: bounded native deadline and retained ownership");
}
int main(void)
{
    repeated_revokers_join_the_admitted_native_section(); deadlines_and_retained_native_ownership(); return 0;
}
