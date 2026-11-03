// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "addon_store_profile_coordinator.hpp"
#include <QLocalServer>
#include <QRandomGenerator>
#include <QtEndian>
#include <chrono>
#include <limits>
#include <thread>

namespace vlc::addons {
namespace {
using Generation = StoreProfileGeneration;
using Message = StoreProfileProtocol::Message;

QByteArray instanceNonce()
{
    QByteArray value(16, '\0');
    for (unsigned attempt = 0; attempt < 4 && value.count('\0') == value.size(); ++attempt) {
        for (qsizetype at = 0; at < value.size(); at += 4)
            qToBigEndian<quint32>(QRandomGenerator::system()->generate(), value.data() + at);
    }
    return value;
}
}

StoreProfileCoordinator::StoreProfileCoordinator(Options options, QObject* parent)
    : QObject(parent), m_options(std::move(options)), m_work(m_options.work, this)
{
    m_wake.setSingleShot(true);
    connect(&m_wake, &QTimer::timeout, this, [this] { expire(); });
}

StoreProfileCoordinator::~StoreProfileCoordinator()
{
    close();
}

std::optional<StoreProfileCoordinator::Epoch> StoreProfileCoordinator::epoch() const
{
    if (!m_arbiter || m_arbiter->phase() == StoreProfileArbiter::Phase::Recovering)
        return std::nullopt;
    return m_arbiter->epoch();
}

bool StoreProfileCoordinator::start()
{
    if (m_started || m_state != State::Starting || m_options.directory.isEmpty() || !m_options.recovery)
        return false;
    m_started = true;
    if (!m_work.valid()) {
        fail();
        return false;
    }
    const auto directory = m_options.directory;
    const auto runtime = m_options.unixRuntime;
    const bool started = m_work.run([this, directory, runtime]() -> StoreProfileControlWork::Result {
        Generation::Status status = Generation::Status::Error;
        QString error;
        std::shared_ptr<Generation> owner(Generation::acquireCoordinator(directory, &status, &error));
        const auto endpoint = owner ? StoreProfileEndpoint::forProfile(directory, runtime)
                                    : std::optional<StoreProfileEndpoint>{};
#ifndef Q_OS_WIN
        // Only the owner-lock holder removes a stale Unix endpoint.
        if (endpoint)
            QLocalServer::removeServer(endpoint->address());
#endif
        return [this, owner, endpoint, status, error] { claimed(owner, endpoint, status, error); };
    });
    if (!started)
        fail();
    return started;
}

void StoreProfileCoordinator::claimed(std::shared_ptr<StoreProfileGeneration> owner,
    std::optional<StoreProfileEndpoint> endpoint, StoreProfileGeneration::Status status, const QString&)
{
    if (!owner) {
        // Another process coordinates this profile.
        if (status == Generation::Status::Busy)
            setState(State::Peer);
        else
            fail();
        return;
    }
    if (!endpoint) {
        fail();
        return;
    }
    m_generation = std::move(owner);
    m_arbiter = std::make_unique<StoreProfileArbiter>(instanceNonce());
    m_listener = std::make_unique<StoreProfileListener>([this](std::unique_ptr<StoreProfileSocket> socket) {
        accepted(std::move(socket));
    });
    // Listen during recovery: a Join gets Hold until the first generation.
    if (!m_listener->listen(*endpoint)) {
        fail();
        return;
    }
    setState(State::Recovering);
    startRecovery();
}

void StoreProfileCoordinator::startRecovery()
{
    if (m_recovering || !m_generation)
        return;
    m_recovering = true;
    const auto generation = m_generation;
    const auto recovery = m_options.recovery;
    const auto current = m_work.current();
    const auto limit = qBound<qint64>(1, m_options.recoveryLimit, Generation::MaximumDuration);
    const bool started = m_work.run([this, generation, recovery, current, limit]() -> StoreProfileControlWork::Result {
        const auto deadline = Generation::now() + limit;
        const Generation::Control control{current, Generation::now, deadline};
        auto result = generation->recover(control, recovery);
        // Peers release their shared fences after Recover. Retry a busy
        // fence only while this recovery is active.
        while (result.status == Generation::Status::Busy && Generation::now() < deadline && current()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            result = generation->recover(control, recovery);
        }
        return [this, result] { recoveryFinished(result); };
    });
    if (!started)
        fail();
}

void StoreProfileCoordinator::recoveryFinished(const StoreProfileGeneration::Result& result)
{
    m_recovering = false;
    if (!m_arbiter || result.status != Generation::Status::Ready) {
        fail();
        return;
    }
    const auto effects = m_arbiter->recovered(result.epoch);
    if (!effects.accepted) {
        fail();
        return;
    }
    apply(effects);
    updateState();
    rearm();
}

void StoreProfileCoordinator::startCheck(const Message& receipt)
{
    const auto directory = m_options.directory;
    const auto current = m_work.current();
    const bool started = m_work.run([this, directory, receipt, current]() -> StoreProfileControlWork::Result {
        // The writer released both locks before Complete. Read the exact ready
        // record under the shared native fence, then release that fence.
        const auto deadline = qMin(receipt.deadline, Generation::now() + Generation::MaximumDuration);
        const Generation::Control control{current, Generation::now, deadline};
        Generation::Status status = Generation::Status::Error;
        auto prepared = Generation::prepareJoin(directory, receipt.epoch, control, &status);
        while (status == Generation::Status::Busy && Generation::now() < deadline && current()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            prepared = Generation::prepareJoin(directory, receipt.epoch, control, &status);
        }
        const bool coherent = status == Generation::Status::Ready && prepared.fence && prepared.epoch == receipt.epoch;
        prepared.fence.reset();
        return [this, receipt, coherent] { checkFinished(receipt, coherent); };
    });
    if (!started)
        fail();
}

void StoreProfileCoordinator::checkFinished(const Message& receipt, bool coherent)
{
    if (!m_arbiter)
        return;
    apply(m_arbiter->checked(receipt, coherent, Generation::now()));
    updateState();
    rearm();
}

void StoreProfileCoordinator::accepted(std::unique_ptr<StoreProfileSocket> socket)
{
    if (!m_arbiter || m_state == State::Closed || m_state == State::Failed) {
        socket->stop();
        return;
    }
    const auto effects = m_arbiter->accept();
    if (!effects.accepted || effects.send.isEmpty()) {
        socket->stop();
        return;
    }
    const auto connection = effects.send.first().connection;
    auto* raw = socket.release();
    raw->setParent(this);
    m_sockets.insert(connection, raw);
    m_handshakes.insert(connection, Generation::now() + m_options.handshake);
    StoreProfileSocket::Callbacks callbacks;
    // The coordinator's side holds no native authority.
    callbacks.revoke = [] {};
    callbacks.received = [this, connection](const Message& message) { received(connection, message); };
    callbacks.lost = [this, connection] { lost(connection); };
    if (!raw->startAccepted(std::move(callbacks))) {
        // A failed start can call lost() already. It removes the binding once.
        lost(connection);
        return;
    }
    apply(effects);
    rearm();
}

void StoreProfileCoordinator::received(const QByteArray& connection, const Message& message)
{
    if (!m_arbiter || !m_sockets.contains(connection))
        return;
    if (message.kind == StoreProfileProtocol::Kind::Join)
        m_handshakes.remove(connection);
    apply(m_arbiter->receive(connection, message, Generation::now()));
    updateState();
    rearm();
}

void StoreProfileCoordinator::lost(const QByteArray& connection)
{
    auto* socket = m_sockets.take(connection);
    if (!socket)
        return;
    m_handshakes.remove(connection);
    // A socket callback can be running. Delete the socket later.
    socket->deleteLater();
    if (!m_arbiter || m_state == State::Closed)
        return;
    apply(m_arbiter->disconnected(connection));
    updateState();
    rearm();
}

void StoreProfileCoordinator::apply(const StoreProfileArbiter::Effects& effects)
{
    // Apply every effect, including when the arbiter refused the message.
    for (const auto& message : effects.send) {
        if (auto* socket = m_sockets.value(message.connection))
            socket->send(message);
    }
    for (const auto& connection : effects.close) {
        if (auto* socket = m_sockets.value(connection))
            socket->stop();
    }
    if (m_state == State::Closed || m_state == State::Failed)
        return;
    if (effects.recoveryRequired) {
        setState(State::Recovering);
        startRecovery();
    }
    if (effects.check)
        startCheck(*effects.check);
}

void StoreProfileCoordinator::expire()
{
    if (!m_arbiter || m_state == State::Closed)
        return;
    const auto now = Generation::now();
    QList<QByteArray> silent;
    for (auto handshake = m_handshakes.cbegin(); handshake != m_handshakes.cend(); ++handshake) {
        if (handshake.value() <= now)
            silent.append(handshake.key());
    }
    for (const auto& connection : silent) {
        m_handshakes.remove(connection);
        if (auto* socket = m_sockets.value(connection))
            socket->stop();
    }
    apply(m_arbiter->advance(now));
    updateState();
    rearm();
}

void StoreProfileCoordinator::rearm()
{
    if (!m_arbiter || m_state == State::Closed || m_state == State::Failed) {
        m_wake.stop();
        return;
    }
    auto next = std::numeric_limits<qint64>::max();
    if (const auto deadline = m_arbiter->deadline())
        next = *deadline;
    for (const auto deadline : std::as_const(m_handshakes))
        next = qMin(next, deadline);
    if (next == std::numeric_limits<qint64>::max()) {
        m_wake.stop();
        return;
    }
    const auto remaining = qMax<qint64>(0, next - Generation::now());
    // Round up, so the wake does not run before the deadline.
    m_wake.start(int(qMin<qint64>(remaining / 1000 + 1, std::numeric_limits<int>::max())));
}

void StoreProfileCoordinator::updateState()
{
    if (!m_arbiter || m_state == State::Closed || m_state == State::Failed)
        return;
    setState(m_arbiter->phase() == StoreProfileArbiter::Phase::Recovering ? State::Recovering : State::Serving);
}

void StoreProfileCoordinator::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    const auto changed = m_options.changed;
    if (changed)
        changed(state);
}

void StoreProfileCoordinator::fail()
{
    if (m_state == State::Closed || m_state == State::Failed)
        return;
    close();
    m_state = State::Failed;
    const auto changed = m_options.changed;
    if (changed)
        changed(State::Failed);
}

void StoreProfileCoordinator::close()
{
    if (m_state == State::Closed)
        return;
    const bool failed = m_state == State::Failed;
    m_state = State::Closed;
    m_work.close();
    m_wake.stop();
    m_handshakes.clear();
    if (m_listener)
        m_listener->stop();
    const auto sockets = m_sockets;
    m_sockets.clear();
    for (auto* socket : sockets) {
        socket->stop();
        socket->deleteLater();
    }
    m_listener.reset();
    m_arbiter.reset();
    // Joined work can still hold the generation. The owner lock ends with its
    // last reference.
    m_generation.reset();
    if (failed)
        m_state = State::Failed;
}
}
