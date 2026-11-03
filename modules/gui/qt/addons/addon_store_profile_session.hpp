// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_store_profile_client.hpp"
#include "addon_store_profile_coordinator.hpp"
#include <QObject>
#include <functional>
#include <memory>

namespace vlc::addons {
// Host only. One per process and canonical profile, on an independent joined
// control thread. Every app process runs the same session: the client role
// always, and the coordinator role while it holds the profile owner lock.
//
// Handover: when the client loses its coordinator, or cannot reach one, the
// session tries the owner lock again. One process wins, recovers the profile
// to a new generation and serves. The other clients reconnect and join it.
// The user does not see which process coordinates.
class StoreProfileSession final : public QObject {
public:
    struct Options {
        QString directory;
        StoreProfileGeneration::Recovery recovery;
        AddonStreamLifetime::Handle work;
        QString unixRuntime;
        // Runs on the control thread when the client's state changes.
        std::function<void(StoreProfileClient::State)> changed;
        // Runs on the control thread when this process starts or stops
        // coordinating.
        std::function<void(bool)> coordinating;
    };
    // The peer comes from StoreAuthority::attachPeer() on joined startup work,
    // before any native consumer of this profile starts.
    StoreProfileSession(Options, std::unique_ptr<StoreAuthorityPeer>, QObject* parent = nullptr);
    ~StoreProfileSession() override;
    bool start();
    void close();
    bool coordinating() const noexcept;
    StoreProfileClient::State state() const noexcept;
    std::optional<StoreProfileClient::Epoch> epoch() const;
    StoreProfileClient::ReadyWait readyWait() const;
    StoreProfileClient::WritePort writePort() const;
    bool timerActive() const noexcept;

private:
    void claim();
    void coordinatorChanged(StoreProfileCoordinator::State);
    void clientChanged(StoreProfileClient::State);
    void setCoordinating(bool);

    const Options m_options;
    std::unique_ptr<StoreProfileClient> m_client;
    std::unique_ptr<StoreProfileCoordinator> m_coordinator;
    bool m_coordinating = false;
    bool m_closed = false;
};
}
