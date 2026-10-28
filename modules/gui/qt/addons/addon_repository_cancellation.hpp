// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <memory>

namespace vlc::addons {
// This scope contains native state only. Git polling does not call Qt owners.
class RepositoryCancellation final {
public:
    explicit RepositoryCancellation(std::shared_ptr<const std::atomic_bool> flag) noexcept
        :m_previous(std::move(s_current)) { s_current=std::move(flag); }
    ~RepositoryCancellation() { s_current.swap(m_previous); }
    RepositoryCancellation(const RepositoryCancellation&)=delete;
    RepositoryCancellation& operator=(const RepositoryCancellation&)=delete;
    static bool requested() noexcept { return s_current && s_current->load(std::memory_order_acquire); }
private:
    static thread_local std::shared_ptr<const std::atomic_bool> s_current;
    std::shared_ptr<const std::atomic_bool> m_previous;
};
}
