// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_store_profile_session.hpp"
#include <QObject>
#include <QPointer>
#include <QThread>
#include <atomic>

namespace vlc::addons {
class AddonManager;
// Mounts the shared add-on profile in one app process (Step 2.6). Create it on
// the GUI thread right after the AddonManager, before any native consumer of
// the profile starts.
//
// start() attaches the profile peer, so native admission waits for the
// coordinator from then on, and the store's ordinary writes refuse. It runs
// the profile session on its own control thread and gives the store a write
// port, so the manager's writes go through permits. When another process's
// change brings this process back at a new revision, the manager reloads its
// cached profile state on the GUI thread.
//
// When the profile's drive has no file locks (the coordinator's fence needs
// them), start() leaves the profile unshared: the store keeps its ordinary
// writes, serialized by store.lock, and no other window is told about a
// change. Without this, every write would wait for a coordinator that cannot
// exist.
//
// close() is the retirement hook. It closes the session on its thread, which
// closes native admission at once, and joins that thread. No step waits for
// the GUI thread.
class AddonProfileHost final : public QObject {
public:
    AddonProfileHost(AddonManager*, AddonStreamLifetime::Handle, QObject* parent = nullptr);
    ~AddonProfileHost() override;
    bool start();
    void close();
    bool coordinating() const noexcept { return m_coordinating.load(std::memory_order_acquire); }
    bool ready() const noexcept { return m_ready.load(std::memory_order_acquire); }
    // False after start() when the profile could not be shared.
    bool shared() const noexcept { return m_shared; }

private:
    void sessionChanged(StoreProfileClient::State);

    const QPointer<AddonManager> m_manager;
    const AddonStreamLifetime::Handle m_work;
    QString m_directory;
    QThread m_thread;
    QObject* m_context = nullptr;
    // Lives on the control thread.
    StoreProfileSession* m_session = nullptr;
    std::optional<StoreProfileGeneration::Epoch> m_lastEpoch;
    std::atomic_bool m_coordinating{false};
    std::atomic_bool m_ready{false};
    bool m_started = false;
    bool m_shared = false;
    bool m_closed = false;
};
}
