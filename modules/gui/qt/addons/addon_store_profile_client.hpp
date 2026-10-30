// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_store_authority.hpp"
#include "addon_store_profile_control.hpp"
#include "addon_store_profile_socket.hpp"
#include <QObject>
#include <QTimer>
#include <functional>
#include <memory>
#include <optional>

namespace vlc::addons {
// Host only. One per process and canonical profile, on the same independent
// joined control thread as the coordinator, never on the GUI thread. It owns
// the process's StoreAuthorityPeer and its control socket.
//
// Join: close local admission, drain on joined work, Join, check the offered
// epoch under the shared native fence on joined work, Confirm, and admit
// only on the server's exact Ready.
//
// Revoke, Recover and connection loss close native admission at once, in
// memory. Pending join preparation is joined and its unpublished fence
// released. The peer drains on joined work and releases its fence. Only
// then does the client send Revoked (for Revoke) or wait for Available. No
// step waits for the GUI thread.
//
// The only timer runs while the client tries to reach the coordinator after
// a loss or a refused connection. A ready client has no timer.
class StoreProfileClient final : public QObject {
public:
    using Epoch = StoreProfileGeneration::Epoch;
    using Message = StoreProfileProtocol::Message;
    enum class State { Starting, Connecting, Joining, Ready, Revoking, Waiting, Unavailable, Closed };
    struct Options {
        QString directory;
        AddonStreamLifetime::Handle work;
        // The endpoint's Unix runtime directory. Empty uses the default.
        QString unixRuntime;
        // Microseconds that one join can spend on its saved-record check.
        qint64 joinLimit = 10 * 1000 * 1000;
        // Microseconds that the client keeps trying to reach a coordinator
        // after a loss. Each try waits retryInterval milliseconds.
        qint64 reconnectLimit = 10 * 1000 * 1000;
        int retryInterval = 100;
        // Runs on the control thread after each state change.
        std::function<void(State)> changed;
    };
    // The peer comes from StoreAuthority::attachPeer() on joined startup work,
    // before any native consumer of this profile starts.
    StoreProfileClient(Options, std::unique_ptr<StoreAuthorityPeer>, QObject* parent = nullptr);
    ~StoreProfileClient() override;
    bool start();
    void close();
    State state() const noexcept { return m_state; }
    // The current ready epoch, only while admitted.
    std::optional<Epoch> epoch() const;
    QByteArray connection() const { return m_connection; }
    bool timerActive() const noexcept { return m_retry.isActive(); }

private:
    void connectToCoordinator();
    void received(const Message&);
    void lost();
    void beginJoin();
    void sendJoin(quint64 attempt);
    void prepare(const Message& candidate);
    void prepared(quint64 attempt, const Message& candidate, std::shared_ptr<const StoreProcessFence>, bool ready);
    void admitted(quint64 attempt, const Epoch&, bool admitted);
    void revoke(std::optional<Message> acknowledge);
    void finishRevoke();
    void drained(quint64 attempt, bool drained);
    void scheduleReconnect();
    void protocolError();
    void setState(State);

    const Options m_options;
    StoreProfileControlWork m_work;
    std::shared_ptr<StoreAuthorityPeer> m_peer;
    std::optional<StoreProfileEndpoint> m_endpoint;
    std::unique_ptr<StoreProfileSocket> m_socket;
    QByteArray m_connection;
    QByteArray m_request;
    // The admission stamp of the current join attempt or revocation.
    quint64 m_attempt = 0;
    std::optional<Message> m_candidate;
    std::optional<Epoch> m_ready;
    // The checked shared fence between Confirm and admission. It is never
    // published to native consumers until the peer admits it.
    std::shared_ptr<const StoreProcessFence> m_fence;
    // A Revoke waits for join preparation and admission tasks to end, so no
    // fence copy remains, before its drain starts.
    std::optional<Message> m_revoke;
    bool m_revoking = false;
    bool m_preparing = false;
    bool m_admitting = false;
    // Available arrived during a drain. Join again after it.
    bool m_rejoin = false;
    qint64 m_reconnectUntil = 0;
    QTimer m_retry;
    State m_state = State::Starting;
};
}
