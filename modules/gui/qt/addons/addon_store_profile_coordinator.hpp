// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_store_profile_control.hpp"
#include "addon_store_profile_socket.hpp"
#include <QHash>
#include <QObject>
#include <QTimer>
#include <functional>
#include <memory>
#include <optional>

namespace vlc::addons {
// Host only. Use one per process and canonical profile, on an independent
// joined control thread with an event loop, never on the GUI thread.
//
// start() tries the profile owner lock on joined work. The process that gets
// it recovers the profile, publishes a new generation and serves peers. Every
// other process reports Peer and serves nothing. Lock order: owner lock,
// store.lock, exclusive fence.
//
// The owner applies every arbiter effect, including when a message is
// refused. It routes input by the accepted connection only. Filesystem work
// (claim, recovery, saved-record checks) runs on joined work, and results
// return through a native wake. The single timer runs only while a write or
// an accepted connection's handshake is pending. An idle coordinator has no
// timer.
class StoreProfileCoordinator final : public QObject {
public:
    using Epoch = StoreProfileGeneration::Epoch;
    enum class State { Starting, Peer, Recovering, Serving, Failed, Closed };
    struct Options {
        QString directory;
        // Validates or restores the whole store. It runs on joined work with
        // the owner lock, store.lock and the exclusive fence held.
        StoreProfileGeneration::Recovery recovery;
        AddonStreamLifetime::Handle work;
        // The endpoint's Unix runtime directory. Empty uses the default.
        QString unixRuntime;
        // Microseconds from accept to the connection's first Join.
        qint64 handshake = 5 * 1000 * 1000;
        // Microseconds that one recovery can wait for peers to release the
        // shared fence. It cannot exceed MaximumDuration.
        qint64 recoveryLimit = StoreProfileGeneration::MaximumDuration;
        // Runs on the control thread after each state change.
        std::function<void(State)> changed;
    };
    explicit StoreProfileCoordinator(Options, QObject* parent = nullptr);
    ~StoreProfileCoordinator() override;
    bool start();
    void close();
    State state() const noexcept { return m_state; }
    std::optional<Epoch> epoch() const;
    qsizetype connections() const noexcept { return m_sockets.size(); }
    bool timerActive() const noexcept { return m_wake.isActive(); }

private:
    void claimed(std::shared_ptr<StoreProfileGeneration>, std::optional<StoreProfileEndpoint>,
                 StoreProfileGeneration::Status, const QString& error);
    void startRecovery();
    void recoveryFinished(const StoreProfileGeneration::Result&);
    void startCheck(const StoreProfileProtocol::Message&);
    void checkFinished(const StoreProfileProtocol::Message&, bool coherent);
    void accepted(std::unique_ptr<StoreProfileSocket>);
    void received(const QByteArray& connection, const StoreProfileProtocol::Message&);
    void lost(const QByteArray& connection);
    void apply(const StoreProfileArbiter::Effects&);
    void expire();
    void rearm();
    void updateState();
    void setState(State);
    void fail();

    const Options m_options;
    StoreProfileControlWork m_work;
    std::shared_ptr<StoreProfileGeneration> m_generation;
    std::unique_ptr<StoreProfileArbiter> m_arbiter;
    std::unique_ptr<StoreProfileListener> m_listener;
    QHash<QByteArray, StoreProfileSocket*> m_sockets;
    QHash<QByteArray, qint64> m_handshakes;
    QTimer m_wake;
    State m_state = State::Starting;
    bool m_started = false;
    bool m_recovering = false;
};
}
