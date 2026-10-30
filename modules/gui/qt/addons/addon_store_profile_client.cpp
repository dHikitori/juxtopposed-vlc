// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "addon_store_profile_client.hpp"
#include <QRandomGenerator>
#include <QtEndian>

namespace vlc::addons {
namespace {
using Generation = StoreProfileGeneration;
using Kind = StoreProfileProtocol::Kind;

QByteArray requestNonce()
{
    QByteArray value(16, '\0');
    for (unsigned attempt = 0; attempt < 4 && value.count('\0') == value.size(); ++attempt) {
        for (qsizetype at = 0; at < value.size(); at += 4)
            qToBigEndian<quint32>(QRandomGenerator::system()->generate(), value.data() + at);
    }
    return value;
}
}

StoreProfileClient::StoreProfileClient(Options options, std::unique_ptr<StoreAuthorityPeer> peer, QObject* parent)
    : QObject(parent), m_options(std::move(options)), m_work(m_options.work, this), m_peer(std::move(peer))
{
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, [this] { connectToCoordinator(); });
}

StoreProfileClient::~StoreProfileClient()
{
    close();
}

std::optional<StoreProfileClient::Epoch> StoreProfileClient::epoch() const
{
    return m_state == State::Ready ? m_ready : std::nullopt;
}

bool StoreProfileClient::start()
{
    if (m_state != State::Starting || !m_peer || m_options.directory.isEmpty() || !m_work.valid())
        return false;
    const auto directory = m_options.directory;
    const auto runtime = m_options.unixRuntime;
    setState(State::Connecting);
    const bool started = m_work.run([this, directory, runtime]() -> StoreProfileControlWork::Result {
        const auto endpoint = StoreProfileEndpoint::forProfile(directory, runtime);
        return [this, endpoint] {
            if (!endpoint) {
                setState(State::Unavailable);
                return;
            }
            m_endpoint.emplace(*endpoint);
            connectToCoordinator();
        };
    });
    if (!started)
        setState(State::Unavailable);
    return started;
}

void StoreProfileClient::connectToCoordinator()
{
    if (m_state == State::Closed || !m_endpoint || m_socket || m_revoking)
        return;
    setState(State::Connecting);
    m_connection.clear();
    m_socket = std::make_unique<StoreProfileSocket>();
    StoreProfileSocket::Callbacks callbacks;
    // Native only. Loss and socket destruction close admission at once.
    callbacks.revoke = m_peer->closeHandle();
    callbacks.received = [this](const Message& message) { received(message); };
    callbacks.lost = [this] { lost(); };
    // A refused connection calls lost(), which schedules the next try.
    m_socket->connectTo(*m_endpoint, std::move(callbacks));
}

void StoreProfileClient::received(const Message& message)
{
    if (m_state == State::Closed)
        return;
    if (message.kind == Kind::Welcome) {
        if (!m_connection.isEmpty()) {
            protocolError();
            return;
        }
        m_connection = message.connection;
        m_reconnectUntil = 0;
        beginJoin();
        return;
    }
    if (m_connection.isEmpty() || message.connection != m_connection) {
        protocolError();
        return;
    }
    switch (message.kind) {
    case Kind::Hold:
        if (message.request == m_request) {
            m_request.clear();
            setState(State::Waiting);
        }
        return;
    case Kind::Available:
        beginJoin();
        return;
    case Kind::Candidate:
        if (m_state != State::Joining || m_preparing || m_candidate || message.request != m_request) {
            protocolError();
            return;
        }
        prepare(message);
        return;
    case Kind::Ready: {
        if (m_state != State::Joining || !m_candidate || !m_fence || m_admitting
            || message.request != m_request || message.epoch != m_candidate->epoch) {
            protocolError();
            return;
        }
        const auto peer = m_peer;
        const auto attempt = m_attempt;
        const auto epoch = message.epoch;
        auto fence = std::move(m_fence);
        m_admitting = true;
        const bool started = m_work.run([this, peer, attempt, epoch, fence]() mutable -> StoreProfileControlWork::Result {
            const bool admitted = peer->admit(attempt, fence);
            // Release this copy before the result. A revocation then knows
            // that only the peer can still hold the fence.
            fence.reset();
            return [this, attempt, epoch, admitted] { this->admitted(attempt, epoch, admitted); };
        });
        if (!started) {
            m_admitting = false;
            setState(State::Unavailable);
        }
        return;
    }
    case Kind::Revoke:
        revoke(message);
        return;
    case Kind::Recover:
        revoke(std::nullopt);
        return;
    default:
        protocolError();
        return;
    }
}

void StoreProfileClient::beginJoin()
{
    if (m_state == State::Closed)
        return;
    if (m_revoking) {
        m_rejoin = true;
        return;
    }
    const auto attempt = m_peer->close();
    if (!attempt) {
        setState(State::Unavailable);
        return;
    }
    m_attempt = attempt;
    m_ready.reset();
    m_candidate.reset();
    m_fence.reset();
    m_request = requestNonce();
    setState(State::Joining);
    const auto peer = m_peer;
    const bool started = m_work.run([this, peer, attempt]() -> StoreProfileControlWork::Result {
        const bool drained = peer->drain(attempt);
        return [this, attempt, drained] {
            if (attempt != m_attempt || m_state != State::Joining)
                return;
            if (!drained) {
                setState(State::Unavailable);
                return;
            }
            sendJoin(attempt);
        };
    });
    if (!started)
        setState(State::Unavailable);
}

void StoreProfileClient::sendJoin(quint64 attempt)
{
    if (attempt != m_attempt || !m_socket)
        return;
    m_socket->send({Kind::Join, m_connection, {}, m_request, 0});
}

void StoreProfileClient::prepare(const Message& candidate)
{
    m_candidate = candidate;
    m_preparing = true;
    const auto directory = m_options.directory;
    const auto attempt = m_attempt;
    const auto pending = m_peer->pending(attempt);
    const auto current = m_work.current();
    const auto limit = qBound<qint64>(1, m_options.joinLimit, Generation::MaximumDuration);
    const bool started = m_work.run([this, directory, attempt, candidate, pending, current, limit]() -> StoreProfileControlWork::Result {
        // The pending guard ends when a revocation closes this attempt.
        const Generation::Control control{[pending, current] { return pending() && current(); },
                                          Generation::now, Generation::now() + limit};
        Generation::Status status = Generation::Status::Error;
        auto prepared = Generation::prepareJoin(directory, candidate.epoch, control, &status);
        const bool ready = status == Generation::Status::Ready && prepared.fence && prepared.epoch == candidate.epoch;
        auto fence = ready ? std::move(prepared.fence) : std::shared_ptr<const StoreProcessFence>{};
        prepared.fence.reset();
        return [this, attempt, candidate, fence, ready] { this->prepared(attempt, candidate, fence, ready); };
    });
    if (!started) {
        m_preparing = false;
        setState(State::Unavailable);
    }
}

void StoreProfileClient::prepared(quint64 attempt, const Message& candidate,
    std::shared_ptr<const StoreProcessFence> fence, bool ready)
{
    m_preparing = false;
    if (attempt != m_attempt || m_state != State::Joining || !m_socket) {
        // A revocation closed this attempt. Its fence was never published.
        fence.reset();
        if (m_revoking)
            finishRevoke();
        return;
    }
    if (!ready) {
        // The coordinator is changing the profile. Revoke, Recover or
        // Available follows.
        m_candidate.reset();
        setState(State::Waiting);
        return;
    }
    m_fence = std::move(fence);
    m_socket->send({Kind::Confirm, m_connection, candidate.epoch, m_request, 0});
}

void StoreProfileClient::admitted(quint64 attempt, const Epoch& epoch, bool admitted)
{
    m_admitting = false;
    if (m_revoking) {
        // The admission stamp was closed first, so a late admission is
        // already closed. Drain it before the acknowledgement.
        finishRevoke();
        return;
    }
    if (attempt != m_attempt || m_state != State::Joining)
        return;
    if (!admitted) {
        setState(State::Waiting);
        return;
    }
    m_ready = epoch;
    setState(State::Ready);
}

void StoreProfileClient::revoke(std::optional<Message> acknowledge)
{
    // Memory only: close native admission before anything else.
    const auto attempt = m_peer ? m_peer->close() : 0;
    m_attempt = attempt;
    m_ready.reset();
    m_candidate.reset();
    m_request.clear();
    // A Recover or a loss ends the pending write. Its acknowledgement would be
    // refused.
    m_revoke = std::move(acknowledge);
    m_revoking = true;
    setState(State::Revoking);
    if (!m_preparing && !m_admitting)
        finishRevoke();
}

void StoreProfileClient::finishRevoke()
{
    // Only the peer can hold an admitted fence. Release the unpublished one.
    m_fence.reset();
    const auto peer = m_peer;
    const auto attempt = m_attempt;
    if (!peer || !attempt) {
        drained(attempt, false);
        return;
    }
    const bool started = m_work.run([this, peer, attempt]() -> StoreProfileControlWork::Result {
        const bool drained = peer->drain(attempt);
        return [this, attempt, drained] { this->drained(attempt, drained); };
    });
    if (!started)
        drained(attempt, false);
}

void StoreProfileClient::drained(quint64 attempt, bool drained)
{
    // A later revocation has its own drain.
    if (attempt != m_attempt || m_state == State::Closed)
        return;
    m_revoking = false;
    if (!drained) {
        // Without a drain there is no acknowledgement. The coordinator's
        // deadline ends the write.
        m_revoke.reset();
        setState(State::Unavailable);
        return;
    }
    if (m_revoke && m_socket) {
        const auto& revoke = *m_revoke;
        m_socket->send({Kind::Revoked, m_connection, revoke.epoch, revoke.request, revoke.deadline});
    }
    m_revoke.reset();
    setState(State::Waiting);
    if (!m_socket) {
        scheduleReconnect();
        return;
    }
    if (m_rejoin) {
        m_rejoin = false;
        beginJoin();
    }
}

void StoreProfileClient::lost()
{
    if (m_state == State::Closed)
        return;
    // The socket's native callback has already closed admission. The socket
    // can be running this callback, so delete it later.
    if (m_socket)
        m_socket.release()->deleteLater();
    m_connection.clear();
    m_rejoin = false;
    if (!m_reconnectUntil)
        m_reconnectUntil = Generation::now() + m_options.reconnectLimit;
    revoke(std::nullopt);
}

void StoreProfileClient::scheduleReconnect()
{
    if (m_state == State::Closed || m_socket)
        return;
    if (Generation::now() >= m_reconnectUntil) {
        setState(State::Unavailable);
        return;
    }
    m_retry.start(qMax(1, m_options.retryInterval));
}

void StoreProfileClient::protocolError()
{
    if (m_socket)
        m_socket->stop();
}

void StoreProfileClient::setState(State state)
{
    if (m_state == state || m_state == State::Closed)
        return;
    m_state = state;
    const auto changed = m_options.changed;
    if (changed)
        changed(state);
}

void StoreProfileClient::close()
{
    if (m_state == State::Closed)
        return;
    m_state = State::Closed;
    m_retry.stop();
    m_work.close();
    if (m_socket) {
        auto* socket = m_socket.release();
        socket->stop();
        socket->deleteLater();
    }
    if (!m_peer)
        return;
    m_peer->close();
    // Peer destruction drains native callbacks. It belongs on joined work.
    auto peer = std::move(m_peer);
    auto fence = std::move(m_fence);
    const bool started = m_options.work.startQtWork(
        [peer, fence]() mutable {
            fence.reset();
            peer.reset();
        },
        [] {});
    Q_UNUSED(started);
}
}
