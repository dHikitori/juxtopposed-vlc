// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
#include <memory>

namespace vlc::addons {
// Host only. This lease fences coordinator replacement; it is not a package
// grant or a substitute for store.lock. Acquire it on joined background work.
// Retained leases contain native handles only and may outlive a Qt owner.
class StoreProcessFence final {
public:
    enum class Mode { Peer, Replacement };
    enum class Status { Acquired, Busy, Error };
    static std::shared_ptr<const StoreProcessFence> acquire(
        const QString& absoluteFile, Mode, Status*, QString* error = nullptr);
    ~StoreProcessFence();
    StoreProcessFence(const StoreProcessFence&) = delete;
    StoreProcessFence& operator=(const StoreProcessFence&) = delete;
private:
    struct Native;
    explicit StoreProcessFence(std::unique_ptr<Native>);
    const std::unique_ptr<Native> m_native;
};
}
