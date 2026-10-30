// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_store_profile_client.hpp"
#include "addons/addon_store_profile_coordinator.hpp"
#include <QTest>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <atomic>
#include <thread>
#include <memory>
#include <optional>
using namespace vlc::addons;
using C = StoreProfileCoordinator;
using Client = StoreProfileClient;
using G = StoreProfileGeneration;
using P = StoreProfileProtocol;
using K = P::Kind;
using Message = P::Message;
namespace {
struct Fixture;
QString states(const Fixture&);
QByteArray nonce(char c) { return QByteArray(16, c); }
G::Control control(qint64 micros = 20 * 1000 * 1000) { return {[] { return true; }, G::now, G::now() + micros}; }
bool recoverStore(const StoreProfileWriteContext& context, QString*) { return context.current(); }

// The writer side, played by the test with a raw socket. The write permit
// owner is a later step.
struct Writer {
    std::unique_ptr<StoreProfileSocket> socket = std::make_unique<StoreProfileSocket>();
    QList<Message> inbox;
    QByteArray connection;
    bool lostConnection = false;
    std::shared_ptr<const StoreProcessFence> fence;
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
    bool join(const StoreProfileEndpoint& endpoint, const QString& profile) {
        StoreProfileSocket::Callbacks callbacks;
        callbacks.revoke = [] {};
        callbacks.received = [this](const Message& message) {
            if (message.kind == K::Welcome) connection = message.connection;
            inbox.append(message);
        };
        callbacks.lost = [this] { lostConnection = true; };
        if (!socket->connectTo(endpoint, std::move(callbacks)) || !next(K::Welcome)) return false;
        if (!send(K::Join, {}, nonce('j'))) return false;
        const auto candidate = next(K::Candidate);
        if (!candidate) return false;
        G::Status status;
        fence = G::prepareJoin(profile, candidate->epoch, control(), &status).fence;
        return fence && send(K::Confirm, candidate->epoch, nonce('j')) && next(K::Ready);
    }
};
struct Fixture {
    QTemporaryDir profile, runtime;
    AddonStreamLifetime lifetime;
    std::unique_ptr<C> coordinator;
    std::unique_ptr<Client> client;
    std::optional<StoreProfileEndpoint> endpoint;
    QList<Client::State> states;
    StoreAuthority authority{profile.path()};
    bool serve(StoreProfileGeneration::Recovery recovery = recoverStore, bool untilServing = true) {
        C::Options options;
        options.directory = profile.path(); options.recovery = std::move(recovery);
        options.work = lifetime.handle(); options.unixRuntime = runtime.path();
        // Joined work from an earlier owner can hold the owner lock briefly.
        QElapsedTimer timer; timer.start();
        for (;;) {
            coordinator = std::make_unique<C>(options);
            if (!coordinator->start()) return false;
            if (!QTest::qWaitFor([this] { return coordinator->state() != C::State::Starting; }, 15000)) return false;
            if (coordinator->state() != C::State::Peer || timer.elapsed() > 5000) break;
            QTest::qWait(50);
        }
        if (untilServing && !QTest::qWaitFor([this] { return coordinator->state() == C::State::Serving; }, 15000)) return false;
        if (coordinator->state() != C::State::Serving && coordinator->state() != C::State::Recovering) return false;
        endpoint.reset();
        if (const auto found = StoreProfileEndpoint::forProfile(profile.path(), runtime.path())) endpoint.emplace(*found);
        return endpoint.has_value();
    }
    bool startClient(qint64 reconnectLimit = 10 * 1000 * 1000) {
        Client::Options options;
        options.directory = profile.path(); options.work = lifetime.handle(); options.unixRuntime = runtime.path();
        options.reconnectLimit = reconnectLimit; options.retryInterval = 50;
        auto* log = &states;
        options.changed = [log](Client::State state) { log->append(state); };
        auto peer = authority.attachPeer();
        if (!peer) return false;
        client = std::make_unique<Client>(std::move(options), std::move(peer));
        return client->start();
    }
    bool ready(int timeout = 15000) {
        return QTest::qWaitFor([this] { return client->state() == Client::State::Ready; }, timeout);
    }
    ~Fixture() {
        client.reset();
        coordinator.reset();
        lifetime.close();
        lifetime.handle().waitQt(std::chrono::seconds(10));
    }
};
// A writer on its own thread, like a joined task: prepared scope first, then
// the permit, the write, and finish.
struct WriterTask {
    std::thread thread;
    std::atomic_bool granted{false}, done{false};
    G::Result result;
    void start(const QString& profile, Client::WritePort port, qint64 micros,
               std::function<void(StoreProfileWritePermit&)> beforeWrite = {},
               G::Change change = G::Change::Applied) {
        thread = std::thread([this, profile, port, micros, beforeWrite, change] {
            const auto deadline = G::now() + micros;
            G::Status status;
            auto scope = G::prepareWriter(profile, {[] { return true; }, G::now, deadline}, &status);
            auto permit = port(deadline);
            if (scope && permit->wait()) {
                granted = true;
                if (beforeWrite) beforeWrite(*permit);
                result = scope->write(permit->ticket(), [change](const StoreProfileWriteContext& context, QString*) {
                    return context.current() ? change : G::Change::NeedsRecovery;
                });
            }
            scope.reset();
            permit->finish(result);
            done = true;
        });
    }
    ~WriterTask() { if (thread.joinable()) thread.join(); }
};
QString states(const Fixture& f) {
    QStringList names;
    for (auto state : f.states) names << QString::number(int(state));
    return names.join(QLatin1Char(','));
}
}
class TestAddonStoreProfileClient : public QObject
{
    Q_OBJECT
private slots:
    void joinAdmitsNativeAuthority() {
        Fixture f; QVERIFY(f.serve());
        // Before a peer is attached, the store keeps its ungated mode.
        QVERIFY(f.startClient());
        QVERIFY(!f.authority.capture().current());
        QVERIFY(f.ready());
        QVERIFY(f.authority.capture().current());
        QCOMPARE(*f.client->epoch(), *f.coordinator->epoch());
        QVERIFY(!f.client->timerActive());
        QVERIFY(!f.coordinator->timerActive());
    }
    void revokeClosesAtOnceAndAcknowledgesAfterTheDrain() {
        Fixture f; QVERIFY(f.serve());
        Writer writer; QVERIFY(writer.join(*f.endpoint, f.profile.path()));
        QVERIFY(f.startClient()); QVERIFY(f.ready());
        const auto epoch = *f.client->epoch();
        // A native lease whose cancellation is slow.
        const auto token = f.authority.capture(); QVERIFY(token.current());
        auto release = std::make_shared<std::atomic_bool>(false);
        auto cancelled = std::make_shared<std::atomic_bool>(false);
        auto watch = token.onInvalidated([release, cancelled] {
            cancelled->store(true);
            while (!release->load()) QThread::msleep(2);
        });
        QVERIFY(watch);
        const auto deadline = G::now() + 10 * 1000 * 1000;
        G::Status status; auto scope = G::prepareWriter(f.profile.path(), {[] { return true; }, G::now, deadline}, &status);
        QVERIFY(scope);
        QVERIFY(writer.send(K::Begin, epoch, nonce('w'), deadline));
        QVERIFY(writer.next(K::Revoke)); writer.fence.reset();
        QVERIFY(writer.send(K::Revoked, epoch, nonce('w'), deadline));
        // Admission closes at once, before the native cancellation returns.
        QTRY_VERIFY_WITH_TIMEOUT(!token.current(), 5000);
        QVERIFY(!f.authority.capture().current());
        QTRY_VERIFY_WITH_TIMEOUT(cancelled->load(), 5000);
        // No acknowledgement, and so no grant, while cancellation runs.
        QVERIFY(!writer.next(K::Grant, 300));
        release->store(true);
        QVERIFY(writer.next(K::Grant));
        // The client released its shared fence: the exclusive write passes.
        const auto result = scope->write({epoch, nonce('w'), {[] { return true; }, G::now, deadline}},
            [](const StoreProfileWriteContext& context, QString*) { return context.current() ? G::Change::Applied : G::Change::NeedsRecovery; });
        QCOMPARE(result.status, G::Status::Ready);
        scope.reset();
        QVERIFY(writer.send(K::Complete, result.epoch, nonce('w'), deadline));
        QTRY_VERIFY_WITH_TIMEOUT(f.client->epoch() && *f.client->epoch() == result.epoch, 15000);
        QVERIFY(!token.current());
        QVERIFY(f.authority.capture().current());
    }
    void recoverDrainsWithoutAnAcknowledgement() {
        Fixture f; QVERIFY(f.serve());
        Writer writer; QVERIFY(writer.join(*f.endpoint, f.profile.path()));
        QVERIFY(f.startClient()); QVERIFY(f.ready());
        const auto epoch = *f.client->epoch();
        const auto deadline = G::now() + 10 * 1000 * 1000;
        QVERIFY(writer.send(K::Begin, epoch, nonce('w'), deadline));
        QVERIFY(writer.next(K::Revoke)); writer.fence.reset();
        QVERIFY(writer.send(K::Abort, epoch, nonce('w'), deadline));
        // Recovery needs the client's fence; the client drains and rejoins.
        QTRY_VERIFY_WITH_TIMEOUT(f.client->epoch() && f.client->epoch()->generation != epoch.generation, 15000);
        QCOMPARE(f.client->epoch()->revision, 0u);
        QVERIFY(f.authority.capture().current());
    }
    void lossClosesAtOnceAndRejoinsANewCoordinator() {
        Fixture f; QVERIFY(f.serve());
        QVERIFY(f.startClient()); QVERIFY(f.ready());
        const auto first = *f.client->epoch();
        const auto token = f.authority.capture(); QVERIFY(token.current());
        f.coordinator->close();
        // The socket's native callback closes admission first. The control
        // callback follows on a later event.
        QTRY_VERIFY_WITH_TIMEOUT(!token.current(), 5000);
        QTRY_VERIFY2_WITH_TIMEOUT(f.client->state() != Client::State::Ready, qPrintable(states(f)), 5000);
        // Another owner takes over. Recovery needs the client's fence, which
        // the drain released.
        QVERIFY(f.serve());
        QVERIFY(f.ready());
        QVERIFY(f.client->epoch()->generation != first.generation);
        QVERIFY(!token.current());
        QVERIFY(!f.client->timerActive());
    }
    void holdWaitsForAvailable() {
        Fixture f;
        auto release = std::make_shared<std::atomic_bool>(false);
        QVERIFY(f.serve([release](const StoreProfileWriteContext& context, QString*) {
            while (!release->load()) QThread::msleep(5);
            return context.current();
        }, false));
        QVERIFY(f.startClient());
        QTRY_VERIFY_WITH_TIMEOUT(f.states.contains(Client::State::Waiting), 10000);
        QVERIFY(!f.authority.capture().current());
        release->store(true);
        QVERIFY(f.ready());
    }
    void withoutACoordinatorTheClientEndsUnavailable() {
        Fixture f;
        QVERIFY(f.startClient(300 * 1000));
        QTRY_COMPARE_WITH_TIMEOUT(f.client->state(), Client::State::Unavailable, 5000);
        QVERIFY(!f.client->timerActive());
        QVERIFY(!f.authority.capture().current());
    }
    void aWriteDuringJoinStillReachesItsGrant() {
        for (int round = 0; round < 5; ++round) {
            Fixture f; QVERIFY(f.serve());
            Writer writer; QVERIFY(writer.join(*f.endpoint, f.profile.path()));
            const auto epoch = *f.coordinator->epoch();
            QVERIFY(f.startClient());
            // Begin at a different point of the client's join each round.
            QTest::qWait(round * 3);
            const auto deadline = G::now() + 10 * 1000 * 1000;
            G::Status status; auto scope = G::prepareWriter(f.profile.path(), {[] { return true; }, G::now, deadline}, &status);
            QVERIFY(scope);
            QVERIFY(writer.send(K::Begin, epoch, nonce('w'), deadline));
            QVERIFY(writer.next(K::Revoke)); writer.fence.reset();
            QVERIFY(writer.send(K::Revoked, epoch, nonce('w'), deadline));
            QVERIFY2(writer.next(K::Grant), qPrintable(QStringLiteral("round %1").arg(round)));
            const auto result = scope->write({epoch, nonce('w'), {[] { return true; }, G::now, deadline}},
                [](const StoreProfileWriteContext& context, QString*) { return context.current() ? G::Change::Applied : G::Change::NeedsRecovery; });
            QCOMPARE(result.status, G::Status::Ready);
            scope.reset();
            QVERIFY(writer.send(K::Complete, result.epoch, nonce('w'), deadline));
            QVERIFY(f.ready());
            QCOMPARE(*f.client->epoch(), result.epoch);
        }
    }
    void aPermitWritesAndRejoinsTheNextRevision() {
        Fixture f; QVERIFY(f.serve());
        Writer other; QVERIFY(other.join(*f.endpoint, f.profile.path()));
        QVERIFY(f.startClient()); QVERIFY(f.ready());
        const auto epoch = *f.client->epoch();
        WriterTask writer; writer.start(f.profile.path(), f.client->writePort(), 10 * 1000 * 1000);
        const auto revoke = other.next(K::Revoke); QVERIFY(revoke);
        other.fence.reset();
        QVERIFY(other.send(K::Revoked, revoke->epoch, revoke->request, revoke->deadline));
        QTRY_VERIFY_WITH_TIMEOUT(writer.done.load(), 15000);
        QVERIFY(writer.granted.load());
        QCOMPARE(writer.result.status, G::Status::Ready);
        QCOMPARE(writer.result.epoch.revision, epoch.revision + 1);
        QVERIFY(other.next(K::Available));
        QTRY_VERIFY_WITH_TIMEOUT(f.client->epoch() && *f.client->epoch() == writer.result.epoch, 15000);
        QCOMPARE(*f.coordinator->epoch(), writer.result.epoch);
        QVERIFY(!f.client->timerActive());
    }
    void aLossEndsAGrantedPermitBeforeItsWrite() {
        Fixture f; QVERIFY(f.serve());
        QVERIFY(f.startClient()); QVERIFY(f.ready());
        auto go = std::make_shared<std::atomic_bool>(false);
        auto liveAtGrant = std::make_shared<std::atomic_bool>(false);
        auto liveAfterLoss = std::make_shared<std::atomic_bool>(true);
        WriterTask writer;
        writer.start(f.profile.path(), f.client->writePort(), 10 * 1000 * 1000,
            [go, liveAtGrant, liveAfterLoss](StoreProfileWritePermit& permit) {
                liveAtGrant->store(permit.current());
                while (!go->load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
                liveAfterLoss->store(permit.current());
            });
        QTRY_VERIFY_WITH_TIMEOUT(writer.granted.load(), 15000);
        f.coordinator->close();
        QTRY_VERIFY_WITH_TIMEOUT(f.client->state() != Client::State::Ready, 5000);
        go->store(true);
        QTRY_VERIFY_WITH_TIMEOUT(writer.done.load(), 15000);
        QVERIFY(liveAtGrant->load());
        QVERIFY(!liveAfterLoss->load());
        // The ticket was not current, so the store was not written.
        QCOMPARE(writer.result.status, G::Status::Stale);
    }
    void aPermitBeforeTheJoinWaitsForIt() {
        Fixture f; QVERIFY(f.serve());
        QVERIFY(f.startClient());
        WriterTask writer; writer.start(f.profile.path(), f.client->writePort(), 10 * 1000 * 1000);
        QTRY_VERIFY_WITH_TIMEOUT(writer.done.load(), 15000);
        QVERIFY(writer.granted.load());
        QCOMPARE(writer.result.status, G::Status::Ready);
        QVERIFY(f.ready());
        QCOMPARE(*f.client->epoch(), writer.result.epoch);
    }
    void aSecondPermitIsRefusedWhileOneIsPending() {
        Fixture f; QVERIFY(f.serve());
        Writer other; QVERIFY(other.join(*f.endpoint, f.profile.path()));
        QVERIFY(f.startClient()); QVERIFY(f.ready());
        WriterTask writer; writer.start(f.profile.path(), f.client->writePort(), 10 * 1000 * 1000);
        const auto revoke = other.next(K::Revoke); QVERIFY(revoke);
        // The first write waits for the other peer's acknowledgement.
        auto second = f.client->writePort()(G::now() + 5 * 1000 * 1000);
        bool refused = false;
        std::thread waiter([&] { refused = !second->wait(); });
        QTRY_VERIFY_WITH_TIMEOUT(refused, 5000);
        waiter.join();
        second->finish({});
        other.fence.reset();
        QVERIFY(other.send(K::Revoked, revoke->epoch, revoke->request, revoke->deadline));
        QTRY_VERIFY_WITH_TIMEOUT(writer.done.load(), 15000);
        QCOMPARE(writer.result.status, G::Status::Ready);
    }
    void anUnacknowledgedWriteEndsAtTheDeadline() {
        Fixture f; QVERIFY(f.serve());
        Writer silent; QVERIFY(silent.join(*f.endpoint, f.profile.path()));
        QVERIFY(f.startClient()); QVERIFY(f.ready());
        const auto epoch = *f.client->epoch();
        WriterTask writer; writer.start(f.profile.path(), f.client->writePort(), 500 * 1000);
        QVERIFY(silent.next(K::Revoke));
        silent.fence.reset();
        QTRY_VERIFY_WITH_TIMEOUT(writer.done.load(), 15000);
        QVERIFY(!writer.granted.load());
        QTRY_VERIFY_WITH_TIMEOUT(f.client->epoch() && f.client->epoch()->generation != epoch.generation, 15000);
        QVERIFY(!f.client->timerActive());
    }
    void aFailedWriteSendsAbortAndRecovers() {
        Fixture f; QVERIFY(f.serve());
        QVERIFY(f.startClient()); QVERIFY(f.ready());
        const auto epoch = *f.client->epoch();
        WriterTask writer;
        writer.start(f.profile.path(), f.client->writePort(), 10 * 1000 * 1000, {}, G::Change::NeedsRecovery);
        QTRY_VERIFY_WITH_TIMEOUT(writer.done.load(), 15000);
        QVERIFY(writer.granted.load());
        QCOMPARE(writer.result.status, G::Status::NeedsRecovery);
        QTRY_VERIFY_WITH_TIMEOUT(f.client->epoch() && f.client->epoch()->generation != epoch.generation, 15000);
        QCOMPARE(f.client->epoch()->revision, 0u);
    }
};
QTEST_GUILESS_MAIN(TestAddonStoreProfileClient)
#include "test_addon_store_profile_client.moc"
