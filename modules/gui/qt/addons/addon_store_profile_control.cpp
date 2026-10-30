// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "addon_store_profile_control.hpp"
#include "addon_native_wake.hpp"
#include <atomic>
#include <deque>
#include <mutex>

namespace vlc::addons {
struct StoreProfileControlWork::Inbox {
    std::mutex mutex;
    std::deque<Result> results;
    std::function<void()> wake;
    std::atomic_bool open{true};
    void post(Result result)
    {
        std::function<void()> signal;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!open.load(std::memory_order_acquire))
                return;
            results.push_back(std::move(result));
            signal = wake;
        }
        if (signal)
            signal();
    }
    std::deque<Result> take()
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::deque<Result> taken;
        taken.swap(results);
        return taken;
    }
};

StoreProfileControlWork::StoreProfileControlWork(AddonStreamLifetime::Handle work, QObject* owner)
    : m_work(std::move(work)), m_inbox(std::make_shared<Inbox>())
{
    m_wake = new AddonNativeWake([this] { deliver(); }, owner);
    if (!m_wake->valid())
        return;
    std::lock_guard<std::mutex> lock(m_inbox->mutex);
    m_inbox->wake = m_wake->nativeCallback();
}

StoreProfileControlWork::~StoreProfileControlWork()
{
    close();
}

bool StoreProfileControlWork::valid() const noexcept
{
    return m_wake && m_wake->valid() && m_inbox->open.load(std::memory_order_acquire);
}

std::function<bool()> StoreProfileControlWork::current() const
{
    const auto inbox = m_inbox;
    return [inbox] { return inbox->open.load(std::memory_order_acquire); };
}

bool StoreProfileControlWork::run(std::function<Result()> task)
{
    if (!valid())
        return false;
    const auto inbox = m_inbox;
    return m_work.startQtWork(
        [inbox, task = std::move(task)] {
            if (!inbox->open.load(std::memory_order_acquire))
                return;
            auto result = task();
            if (result)
                inbox->post(std::move(result));
        },
        [inbox] { inbox->open.store(false, std::memory_order_release); });
}

void StoreProfileControlWork::deliver()
{
    for (auto& result : m_inbox->take()) {
        if (!m_inbox->open.load(std::memory_order_acquire))
            return;
        result();
    }
}

void StoreProfileControlWork::close()
{
    {
        std::lock_guard<std::mutex> lock(m_inbox->mutex);
        m_inbox->open.store(false, std::memory_order_release);
        m_inbox->results.clear();
    }
    if (m_wake)
        m_wake->close();
}
}
