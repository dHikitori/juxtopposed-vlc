// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_store_profile_coordinator.hpp"
#include <QTest>
#include <QTemporaryDir>
#include <QLocalSocket>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <atomic>
#include <memory>
#include <optional>
using namespace vlc::addons;
using C = StoreProfileCoordinator;
using G = StoreProfileGeneration;
using P = StoreProfileProtocol;
using K = P::Kind;
using Message = P::Message;
namespace {
QByteArray nonce(char c) { return QByteArray(16, c); }
G::Control control(qint64 micros = 20 * 1000 * 1000) { return {[] { return true; }, G::now, G::now() + micros}; }
bool recoverStore(const StoreProfileWriteContext& context, QString*) { return context.current(); }

// A peer played by the test: a real client socket and its received messages.
struct Peer {
    std::unique_ptr<StoreProfileSocket> socket = std::make_unique<StoreProfileSocket>();
    QList<Message> inbox;
    QByteArray connection;
    bool lostConnection = false;
    std::shared_ptr<const StoreProcessFence> fence;
    bool connect(const StoreProfileEndpoint& endpoint) {
        StoreProfileSocket::Callbacks callbacks;
        callbacks.revoke = [] {};
        callbacks.received = [this](const Message& message) {
            if (message.kind == K::Welcome) connection = message.connection;
            inbox.append(message);
        };
        callbacks.lost = [this] { lostConnection = true; };
        return socket->connectTo(endpoint, std::move(callbacks));
    }
    std::optional<Message> next(K kind, int timeout = 10000) {
        QElapsedTimer timer; timer.start();
        while (timer.elapsed() < timeout) {
            for (qsizetype at = 0; at < inbox.size(); ++at)
                if (inbox[at].kind == kind) return inbox.takeAt(at);
            if (lostConnection) return std::nullopt;
            QTest::qWait(5);
        }
        return std::nullopt;
    }
    bool send(K kind, const G::Epoch& epoch, const QByteArray& request, qint64 deadline = 0) {
        return socket->send({kind, connection, epoch, request, deadline});
    }
};
struct Fixture {
    QTemporaryDir profile, runtime;
    AddonStreamLifetime lifetime;
    std::optional<StoreProfileEndpoint> endpoint;
    std::unique_ptr<C> coordinator;
    QList<C::State> states;
    std::unique_ptr<C> make(C::Options options = {}) {
        options.directory = profile.path();
        if (!options.recovery) options.recovery = recoverStore;
        options.work = lifetime.handle();
        options.unixRuntime = runtime.path();
        auto* log = &states;
        if (!options.changed) options.changed = [log](C::State state) { log->append(state); };
        return std::make_unique<C>(std::move(options));
    }
    bool findEndpoint() {
        endpoint.reset();
        if (const auto found = StoreProfileEndpoint::forProfile(profile.path(), runtime.path())) endpoint.emplace(*found);
        return endpoint.has_value();
    }
    bool serve(C::Options options = {}) {
        coordinator = make(std::move(options));
        if (!coordinator->start()) return false;
        if (!QTest::qWaitFor([this] { return coordinator->state() == C::State::Serving; }, 15000)) return false;
        return findEndpoint();
    }
    // Welcome, Join, Candidate, shared fence check, Confirm, Ready.
    bool join(Peer& peer, char request) {
        if (!peer.connect(*endpoint) || !peer.next(K::Welcome)) return false;
        if (!peer.send(K::Join, {}, nonce(request))) return false;
        const auto candidate = peer.next(K::Candidate);
        if (!candidate) return false;
        G::Status status;
        auto prepared = G::prepareJoin(profile.path(), candidate->epoch, control(), &status);
        if (status != G::Status::Ready || !prepared.fence) return false;
        peer.fence = prepared.fence;
        if (!peer.send(K::Confirm, candidate->epoch, nonce(request))) return false;
        const auto ready = peer.next(K::Ready);
        return ready && ready->epoch == candidate->epoch;
    }
    ~Fixture() {
        coordinator.reset();
        lifetime.close();
        lifetime.handle().waitQt(std::chrono::seconds(10));
    }
};
}
class TestAddonStoreProfileCoordinator : public QObject
{
    Q_OBJECT
private slots:
    void theOwnerLockSelectsOneCoordinator() {
        Fixture fixture; QVERIFY(fixture.serve());
        auto second = fixture.make({});
        QVERIFY(second->start());
        QTRY_COMPARE_WITH_TIMEOUT(second->state(), C::State::Peer, 10000);
        QVERIFY(!second->epoch());
        QCOMPARE(fixture.coordinator->state(), C::State::Serving);
    }
    void recoveryPublishesAFreshGenerationAndJoinWaitsForIt() {
        Fixture fixture;
        G::Epoch previous;
        {
            // An earlier coordinator leaves a ready generation behind.
            G::Status status; auto owner = G::acquireCoordinator(fixture.profile.path(), &status);
            QVERIFY(owner); previous = owner->recover(control(), recoverStore).epoch; QVERIFY(!previous.generation.isEmpty());
        }
        auto release = std::make_shared<std::atomic_bool>(false);
        C::Options options;
        options.recovery = [release](const StoreProfileWriteContext& context, QString*) {
            while (!release->load()) QThread::msleep(5);
            return context.current();
        };
        fixture.coordinator = fixture.make(std::move(options));
        QVERIFY(fixture.coordinator->start());
        QTRY_COMPARE_WITH_TIMEOUT(fixture.coordinator->state(), C::State::Recovering, 10000);
        QVERIFY(fixture.findEndpoint());
        Peer peer; QVERIFY(peer.connect(*fixture.endpoint)); QVERIFY(peer.next(K::Welcome));
        QVERIFY(peer.send(K::Join, {}, nonce('j')));
        QVERIFY(peer.next(K::Hold));
        QVERIFY(!fixture.coordinator->epoch());
        release->store(true);
        QVERIFY(peer.next(K::Available));
        QCOMPARE(fixture.coordinator->state(), C::State::Serving);
        const auto epoch = fixture.coordinator->epoch(); QVERIFY(epoch);
        QCOMPARE(epoch->revision, 0u); QVERIFY(epoch->generation != previous.generation);
        QVERIFY(peer.send(K::Join, {}, nonce('k')));
        const auto candidate = peer.next(K::Candidate); QVERIFY(candidate); QCOMPARE(candidate->epoch, *epoch);
    }
    void aWriteRevokesEveryPeerThenPublishesTheNextRevision() {
        Fixture fixture; QVERIFY(fixture.serve());
        Peer writer, other; QVERIFY(fixture.join(writer, 'a')); QVERIFY(fixture.join(other, 'b'));
        const auto epoch = *fixture.coordinator->epoch();
        QVERIFY(!fixture.coordinator->timerActive());
        // The writer holds its prepared scope, and so store.lock, before Begin.
        const auto writeControl = control(10 * 1000 * 1000);
        G::Status status; auto scope = G::prepareWriter(fixture.profile.path(), writeControl, &status);
        QVERIFY(scope);
        QVERIFY(writer.send(K::Begin, epoch, nonce('w'), writeControl.deadline));
        for (auto* peer : {&writer, &other}) {
            const auto revoke = peer->next(K::Revoke); QVERIFY(revoke);
            QCOMPARE(revoke->request, nonce('w'));
        }
        QVERIFY(fixture.coordinator->timerActive());
        // Each peer drains, then releases its shared fence before Revoked.
        for (auto* peer : {&writer, &other}) {
            peer->fence.reset();
            QVERIFY(peer->send(K::Revoked, epoch, nonce('w'), writeControl.deadline));
        }
        QVERIFY(writer.next(K::Grant));
        const auto result = scope->write({epoch, nonce('w'), writeControl},
            [](const StoreProfileWriteContext& context, QString*) { return context.current() ? G::Change::Applied : G::Change::NeedsRecovery; });
        QCOMPARE(result.status, G::Status::Ready); QCOMPARE(result.epoch.revision, epoch.revision + 1);
        scope.reset();
        QVERIFY(writer.send(K::Complete, result.epoch, nonce('w'), writeControl.deadline));
        QVERIFY(other.next(K::Available)); QVERIFY(writer.next(K::Available));
        QCOMPARE(*fixture.coordinator->epoch(), result.epoch);
        QVERIFY(!fixture.coordinator->timerActive());
        // Peers join the new revision with fresh local admission.
        QVERIFY(other.send(K::Join, {}, nonce('c')));
        const auto candidate = other.next(K::Candidate); QVERIFY(candidate); QCOMPARE(candidate->epoch, result.epoch);
    }
    void aCompletionWithoutTheSavedRecordStartsRecovery() {
        Fixture fixture; QVERIFY(fixture.serve());
        Peer writer; QVERIFY(fixture.join(writer, 'a'));
        const auto epoch = *fixture.coordinator->epoch();
        const auto deadline = G::now() + 10 * 1000 * 1000;
        QVERIFY(writer.send(K::Begin, epoch, nonce('w'), deadline));
        QVERIFY(writer.next(K::Revoke)); writer.fence.reset();
        QVERIFY(writer.send(K::Revoked, epoch, nonce('w'), deadline)); QVERIFY(writer.next(K::Grant));
        // No write took place. The claimed next revision is not on disk.
        QVERIFY(writer.send(K::Complete, {epoch.generation, epoch.revision + 1}, nonce('w'), deadline));
        QVERIFY(writer.next(K::Recover));
        QVERIFY(writer.next(K::Available));
        const auto recovered = fixture.coordinator->epoch(); QVERIFY(recovered);
        QVERIFY(recovered->generation != epoch.generation); QCOMPARE(recovered->revision, 0u);
    }
    void aLostRevokingPeerStartsRecoveryWithANewGeneration() {
        Fixture fixture; QVERIFY(fixture.serve());
        Peer writer, other; QVERIFY(fixture.join(writer, 'a')); QVERIFY(fixture.join(other, 'b'));
        const auto epoch = *fixture.coordinator->epoch();
        const auto deadline = G::now() + 10 * 1000 * 1000;
        QVERIFY(writer.send(K::Begin, epoch, nonce('w'), deadline));
        QVERIFY(writer.next(K::Revoke)); QVERIFY(other.next(K::Revoke));
        // The other peer exits without an acknowledgement. Its fence ends with it.
        other.fence.reset(); other.socket->stop();
        QVERIFY(writer.next(K::Recover));
        writer.fence.reset();
        QVERIFY(writer.next(K::Available));
        const auto recovered = fixture.coordinator->epoch(); QVERIFY(recovered);
        QVERIFY(recovered->generation != epoch.generation);
        QVERIFY(!fixture.coordinator->timerActive());
    }
    void recoveryWaitsForARetainedSharedFence() {
        Fixture fixture; QVERIFY(fixture.serve());
        Peer peer; QVERIFY(fixture.join(peer, 'a'));
        const auto epoch = *fixture.coordinator->epoch();
        const auto deadline = G::now() + 10 * 1000 * 1000;
        QVERIFY(peer.send(K::Begin, epoch, nonce('w'), deadline)); QVERIFY(peer.next(K::Revoke));
        // An abort needs recovery, but the peer still holds its shared fence.
        QVERIFY(peer.send(K::Abort, epoch, nonce('w'), deadline)); QVERIFY(peer.next(K::Recover));
        QTest::qWait(300);
        QCOMPARE(fixture.coordinator->state(), C::State::Recovering);
        peer.fence.reset();
        QVERIFY(peer.next(K::Available));
        QCOMPARE(fixture.coordinator->state(), C::State::Serving);
    }
    void aWriteDeadlineUsesTheSingleWake() {
        Fixture fixture; QVERIFY(fixture.serve());
        Peer writer, silent; QVERIFY(fixture.join(writer, 'a')); QVERIFY(fixture.join(silent, 'b'));
        const auto epoch = *fixture.coordinator->epoch();
        const auto deadline = G::now() + 300 * 1000;
        QVERIFY(writer.send(K::Begin, epoch, nonce('w'), deadline));
        QVERIFY(writer.next(K::Revoke)); QVERIFY(silent.next(K::Revoke));
        // The silent peer never acknowledges. The deadline, not EOF, ends the write.
        writer.fence.reset(); silent.fence.reset();
        QVERIFY(writer.send(K::Revoked, epoch, nonce('w'), deadline));
        QVERIFY(fixture.coordinator->timerActive());
        QVERIFY(silent.next(K::Recover, 5000));
        QVERIFY(silent.next(K::Available));
        QVERIFY(!fixture.coordinator->timerActive());
    }
    void aSilentConnectionEndsAtTheHandshakeDeadline() {
        Fixture fixture; C::Options options; options.handshake = 200 * 1000;
        QVERIFY(fixture.serve(std::move(options)));
        Peer silent; QVERIFY(silent.connect(*fixture.endpoint)); QVERIFY(silent.next(K::Welcome));
        QCOMPARE(fixture.coordinator->connections(), 1);
        QVERIFY(fixture.coordinator->timerActive());
        QTRY_VERIFY_WITH_TIMEOUT(silent.lostConnection, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(fixture.coordinator->connections(), 0, 5000);
        QVERIFY(!fixture.coordinator->timerActive());
        // A peer that joins in time keeps its connection.
        Peer prompt; QVERIFY(fixture.join(prompt, 'p'));
        QTest::qWait(400);
        QVERIFY(!prompt.lostConnection); QCOMPARE(fixture.coordinator->connections(), 1);
        QVERIFY(!fixture.coordinator->timerActive());
    }
    void aClaimedConnectionIdentifierIsRefused() {
        Fixture fixture; QVERIFY(fixture.serve());
        Peer first, second; QVERIFY(fixture.join(first, 'a'));
        QVERIFY(second.connect(*fixture.endpoint)); QVERIFY(second.next(K::Welcome));
        // The second peer claims the first peer's identifier.
        QVERIFY(second.socket->send({K::Join, first.connection, {}, nonce('x'), 0}));
        QTRY_VERIFY_WITH_TIMEOUT(second.lostConnection, 5000);
        QVERIFY(!first.lostConnection);
        QCOMPARE(fixture.coordinator->state(), C::State::Serving);
    }
    void closeReleasesTheOwnerLock() {
        Fixture fixture; QVERIFY(fixture.serve());
        Peer peer; QVERIFY(fixture.join(peer, 'a'));
        const auto first = *fixture.coordinator->epoch();
        fixture.coordinator->close();
        QCOMPARE(fixture.coordinator->state(), C::State::Closed);
        QTRY_VERIFY_WITH_TIMEOUT(peer.lostConnection, 5000);
        peer.fence.reset();
        // Joined work can still hold the old owner briefly.
        QVERIFY(fixture.lifetime.handle().waitQt(std::chrono::seconds(10)));
        auto next = fixture.make({});
        QVERIFY(next->start());
        QTRY_COMPARE_WITH_TIMEOUT(next->state(), C::State::Serving, 15000);
        QVERIFY(next->epoch()->generation != first.generation);
    }
};
QTEST_GUILESS_MAIN(TestAddonStoreProfileCoordinator)
#include "test_addon_store_profile_coordinator.moc"
