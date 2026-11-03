// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "addon_store_profile_client.hpp"
#include "addon_native_wake.hpp"
#include <QRandomGenerator>
#include <QtEndian>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>

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

struct StoreProfileWritePermit::State {
    enum class Phase { Waiting, Granted, Refused, Finished };
    std::mutex mutex;
    std::condition_variable changed;
    Phase phase = Phase::Waiting;
    Epoch epoch;
    QByteArray request;
    qint64 deadline = 0;
    // Native-safe binding to the live connection that sent Begin.
    std::atomic_bool live{false};
    std::weak_ptr<StoreProfileClient::Mailbox> mailbox;
    void settle(Phase next)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (phase != Phase::Waiting && !(phase == Phase::Granted && next != Phase::Granted))
                return;
            phase = next;
        }
        if (next != Phase::Granted)
            live.store(false, std::memory_order_release);
        changed.notify_all();
    }
};

// Commands from writers on joined work. The control thread takes them after a
// native wake. A closed mailbox refuses new permits.
struct StoreProfileClient::Mailbox {
    struct Command {
        std::shared_ptr<StoreProfileWritePermit::State> permit;
        bool finish = false;
        Generation::Result result;
    };
    std::mutex mutex;
    std::deque<Command> commands;
    std::function<void()> wake;
    bool open = true;
    // The client's joined state, for writers that wait for it.
    std::condition_variable readyChanged;
    bool ready = false;
    bool post(Command command)
    {
        std::function<void()> signal;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!open)
                return false;
            commands.push_back(std::move(command));
            signal = wake;
        }
        if (signal)
            signal();
        return true;
    }
};

StoreProfileWritePermit::StoreProfileWritePermit(std::shared_ptr<State> state) : m_state(std::move(state)) {}

StoreProfileWritePermit::~StoreProfileWritePermit()
{
    // A permit that was never finished ends its write here.
    finish({});
}

bool StoreProfileWritePermit::wait()
{
    std::unique_lock<std::mutex> lock(m_state->mutex);
    const auto remaining = std::chrono::microseconds(qMax<qint64>(0, m_state->deadline - Generation::now()));
    m_state->changed.wait_for(lock, remaining, [this] { return m_state->phase != State::Phase::Waiting; });
    return m_state->phase == State::Phase::Granted && Generation::now() < m_state->deadline;
}

bool StoreProfileWritePermit::current() const noexcept
{
    return m_state->live.load(std::memory_order_acquire) && Generation::now() < m_state->deadline;
}

StoreProfileGeneration::Ticket StoreProfileWritePermit::ticket() const
{
    std::lock_guard<std::mutex> lock(m_state->mutex);
    const auto state = m_state;
    return {m_state->epoch, m_state->request,
            {[state] { return state->live.load(std::memory_order_acquire); }, Generation::now, m_state->deadline}};
}

void StoreProfileWritePermit::finish(const StoreProfileGeneration::Result& result)
{
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        if (m_state->phase == State::Phase::Finished)
            return;
    }
    // Close the ticket first. The control thread then reports the result.
    m_state->live.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        m_state->phase = State::Phase::Finished;
    }
    m_state->changed.notify_all();
    if (const auto mailbox = m_state->mailbox.lock())
        mailbox->post({m_state, true, result});
}

StoreProfileClient::StoreProfileClient(Options options, std::unique_ptr<StoreAuthorityPeer> peer, QObject* parent)
    : QObject(parent), m_options(std::move(options)), m_work(m_options.work, this), m_peer(std::move(peer)),
      m_mailbox(std::make_shared<Mailbox>())
{
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, [this] { connectToCoordinator(); });
    m_permitDeadline.setSingleShot(true);
    connect(&m_permitDeadline, &QTimer::timeout, this, [this] { endPermit(); });
    m_mail = new AddonNativeWake([this] { takeMail(); }, this);
    std::lock_guard<std::mutex> lock(m_mailbox->mutex);
    if (m_mail->valid())
        m_mailbox->wake = m_mail->nativeCallback();
    else
        m_mailbox->open = false;
}

StoreProfileClient::ReadyWait StoreProfileClient::readyWait() const
{
    const std::weak_ptr<Mailbox> weak = m_mailbox;
    return [weak](qint64 deadline) {
        const auto mailbox = weak.lock();
        if (!mailbox)
            return false;
        std::unique_lock<std::mutex> lock(mailbox->mutex);
        const auto remaining = std::chrono::microseconds(qMax<qint64>(0, deadline - Generation::now()));
        mailbox->readyChanged.wait_for(lock, remaining, [&] { return mailbox->ready || !mailbox->open; });
        return mailbox->ready && mailbox->open;
    };
}

StoreProfileClient::WritePort StoreProfileClient::writePort() const
{
    const std::weak_ptr<Mailbox> weak = m_mailbox;
    return [weak](qint64 deadline) {
        auto state = std::make_shared<StoreProfileWritePermit::State>();
        state->deadline = deadline;
        state->request = requestNonce();
        state->mailbox = weak;
        const auto mailbox = weak.lock();
        const bool posted = deadline > Generation::now() && deadline - Generation::now() <= Generation::MaximumDuration
            && mailbox && mailbox->post({state, false, {}});
        if (!posted)
            state->settle(StoreProfileWritePermit::State::Phase::Refused);
        return std::unique_ptr<StoreProfileWritePermit>(new StoreProfileWritePermit(state));
    };
}

void StoreProfileClient::takeMail()
{
    std::deque<Mailbox::Command> commands;
    {
        std::lock_guard<std::mutex> lock(m_mailbox->mutex);
        commands.swap(m_mailbox->commands);
    }
    for (auto& command : commands) {
        if (command.finish)
            finishPermit(command.permit, command.result);
        else
            beginPermit(command.permit);
    }
}

void StoreProfileClient::beginPermit(std::shared_ptr<StoreProfileWritePermit::State> permit)
{
    // The arbiter accepts one pending write. A second permit is refused. A
    // permit while not joined is refused too: its writer holds store.lock,
    // which a recovery before the join can need.
    if (m_state != State::Ready || m_permit || Generation::now() >= permit->deadline) {
        permit->settle(StoreProfileWritePermit::State::Phase::Refused);
        return;
    }
    m_permit = std::move(permit);
    m_permitSent = false;
    const auto remaining = qMax<qint64>(0, m_permit->deadline - Generation::now());
    m_permitDeadline.start(int(remaining / 1000 + 1));
    sendBegin();
}

void StoreProfileClient::sendBegin()
{
    if (!m_permit || m_permitSent || m_state != State::Ready || !m_ready || !m_socket)
        return;
    {
        std::lock_guard<std::mutex> lock(m_permit->mutex);
        m_permit->epoch = *m_ready;
    }
    m_permitSent = true;
    m_socket->send({Kind::Begin, m_connection, *m_ready, m_permit->request, m_permit->deadline});
}

void StoreProfileClient::endPermit()
{
    m_permitDeadline.stop();
    if (!m_permit)
        return;
    // A loss, Recover, close or the deadline ends the permit. Its ticket
    // closes at once.
    m_permit->live.store(false, std::memory_order_release);
    m_permit->settle(StoreProfileWritePermit::State::Phase::Refused);
    m_permit.reset();
    m_permitSent = false;
}

void StoreProfileClient::finishPermit(std::shared_ptr<StoreProfileWritePermit::State> permit,
    const StoreProfileGeneration::Result& result)
{
    if (permit != m_permit)
        return;
    Epoch epoch;
    QByteArray request;
    qint64 deadline = 0;
    {
        std::lock_guard<std::mutex> lock(permit->mutex);
        epoch = permit->epoch;
        request = permit->request;
        deadline = permit->deadline;
    }
    m_permit.reset();
    m_permitSent = false;
    m_permitDeadline.stop();
    // Only a granted write on the same live connection reports a result.
    if (!m_granted || !m_socket)
        return;
    m_granted = false;
    const bool next = result.status == Generation::Status::Ready && result.epoch.generation == epoch.generation
        && result.epoch.revision == epoch.revision + 1;
    if (next)
        m_socket->send({Kind::Complete, m_connection, result.epoch, request, deadline});
    else
        m_socket->send({Kind::Abort, m_connection, epoch, request, deadline});
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
    case Kind::Grant:
        if (!m_permit || !m_permitSent || m_granted || message.request != m_permit->request
            || message.deadline != m_permit->deadline || message.epoch != m_permit->epoch) {
            protocolError();
            return;
        }
        m_granted = true;
        m_permitDeadline.stop();
        m_permit->live.store(true, std::memory_order_release);
        m_permit->settle(StoreProfileWritePermit::State::Phase::Granted);
        return;
    case Kind::Revoke:
        revoke(message);
        return;
    case Kind::Recover:
        endPermit();
        m_granted = false;
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
    endPermit();
    m_granted = false;
    if (!m_reconnectUntil)
        m_reconnectUntil = Generation::now() + m_options.reconnectLimit;
    revoke(std::nullopt);
}

void StoreProfileClient::retry()
{
    if (m_state != State::Unavailable || m_socket || m_revoking || !m_endpoint)
        return;
    m_reconnectUntil = Generation::now() + m_options.reconnectLimit;
    connectToCoordinator();
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
    {
        std::lock_guard<std::mutex> lock(m_mailbox->mutex);
        m_mailbox->ready = state == State::Ready;
    }
    m_mailbox->readyChanged.notify_all();
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
    {
        std::lock_guard<std::mutex> lock(m_mailbox->mutex);
        m_mailbox->open = false;
        m_mailbox->ready = false;
        m_mailbox->commands.clear();
    }
    m_mailbox->readyChanged.notify_all();
    if (m_mail)
        m_mail->close();
    endPermit();
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
