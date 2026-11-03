// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_stream_lifetime.hpp"
#include <QObject>
#include <functional>
#include <memory>

namespace vlc::addons {
class AddonNativeWake;
// Joined work for a profile control owner. A task runs on the lifetime's
// joined Qt work. Its result runs later on the owner's thread, through a
// native wake. After close(), no result runs and current() is false. The
// owner must call close() before it is destroyed.
class StoreProfileControlWork final {
public:
    using Result = std::function<void()>;
    StoreProfileControlWork(AddonStreamLifetime::Handle, QObject* owner);
    ~StoreProfileControlWork();
    StoreProfileControlWork(const StoreProfileControlWork&) = delete;
    StoreProfileControlWork& operator=(const StoreProfileControlWork&) = delete;
    bool valid() const noexcept;
    bool run(std::function<Result()> task);
    // Native-safe. Use it as a joined task's cancellation guard.
    std::function<bool()> current() const;
    void close();
private:
    struct Inbox;
    void deliver();
    const AddonStreamLifetime::Handle m_work;
    const std::shared_ptr<Inbox> m_inbox;
    AddonNativeWake* m_wake = nullptr;
};
}
