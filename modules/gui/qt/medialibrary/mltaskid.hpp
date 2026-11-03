// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <cstdint>
#include <limits>

// IDs belong to one process. Zero means that submission did not occur.
inline std::uint64_t nextMLTaskId() noexcept
{
    static std::atomic<std::uint64_t> next{1};
    auto value=next.load(std::memory_order_relaxed);
    while (value!=std::numeric_limits<std::uint64_t>::max())
        if (next.compare_exchange_weak(value,value+1,std::memory_order_relaxed)) return value;
    return 0;
}
