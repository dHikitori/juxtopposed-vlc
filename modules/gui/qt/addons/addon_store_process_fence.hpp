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
    // Unsupported: the file system has no byte-range locks (for example the
    // WSL share \\wsl.localhost on Windows, or some network shares).
    enum class Status { Acquired, Busy, Error, Unsupported };
    static std::shared_ptr<const StoreProcessFence> acquire(
        const QString& absoluteFile, Mode, Status*, QString* error = nullptr);
    // False only when the directory's file system cannot hold a fence. The
    // probe uses its own file, so it never contends with a real fence.
    static bool supported(const QString& directory);
    ~StoreProcessFence();
    StoreProcessFence(const StoreProcessFence&) = delete;
    StoreProcessFence& operator=(const StoreProcessFence&) = delete;
private:
    struct Native;
    explicit StoreProcessFence(std::unique_ptr<Native>);
    const std::unique_ptr<Native> m_native;
};
}
