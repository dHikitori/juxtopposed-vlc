// SPDX-License-Identifier: LGPL-2.1-or-later
#ifndef VLC_METADATA_REVISION_INTERNAL_H
#define VLC_METADATA_REVISION_INTERNAL_H 1
#include <stdint.h>
#include <stdatomic.h>

// meta.c is the only caller. Tokens also distinguish different metadata objects.
static inline uint64_t vlc_metadata_revision_Next(void)
{
    static atomic_uint_fast64_t sequence;
    uint_fast64_t current = atomic_load_explicit(&sequence, memory_order_relaxed);
    while (current < UINT64_MAX) {
        if (atomic_compare_exchange_weak_explicit(&sequence, &current, current + 1,
                                                  memory_order_relaxed, memory_order_relaxed))
            return current + 1;
    }
    return UINT64_MAX;
}
#endif
