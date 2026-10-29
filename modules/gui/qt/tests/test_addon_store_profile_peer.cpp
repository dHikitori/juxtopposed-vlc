// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_store_authority.hpp"
#include "addons/addon_store_profile_generation.hpp"
#include <QTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QScopeGuard>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <cstdio>
#include <cstdlib>
using namespace vlc::addons;
using G = StoreProfileGeneration;
namespace {
G::Control control() { return {[] { return true; }, G::now, G::now() + 20 * 1000 * 1000}; }
struct Pause {
    std::mutex mutex; std::condition_variable changed; bool entered = false, released = false;
    void waitHere() { std::unique_lock<std::mutex> lock(mutex); entered = true; changed.notify_all(); changed.wait(lock, [&] { return released; }); }
    bool wait() { std::unique_lock<std::mutex> lock(mutex); return changed.wait_for(lock, std::chrono::seconds(3), [&] { return entered; }); }
    void release() { std::lock_guard<std::mutex> lock(mutex); released = true; changed.notify_all(); }
};
struct Fixture {
    QTemporaryDir root; StoreAuthority authority{root.path()}; std::unique_ptr<G> coordinator;
    std::unique_ptr<StoreAuthorityPeer> peer; G::Epoch epoch; quint64 stamp = 0; bool valid = false;
    Fixture() {
        G::Status status; coordinator = G::acquireCoordinator(root.path(), &status); if (!coordinator) return;
        const auto recovered = coordinator->recover(control(), [](const auto& context, auto*) { return context.current(); });
        if (recovered.status != G::Status::Ready) return;
        epoch = recovered.epoch; peer = authority.attachPeer(); if (!peer) return;
        stamp = peer->close(); valid = stamp && peer->drain(stamp);
    }
    std::shared_ptr<const StoreProcessFence> fence() {
        G::Status status; return G::prepareJoin(root.path(), epoch, control(), &status).fence;
    }
    bool admit() { return peer->admit(stamp, fence()); }
    bool exclusive() {
        StoreProcessFence::Status status; return bool(StoreProcessFence::acquire(root.filePath(QStringLiteral("hakone-authority.fence")), StoreProcessFence::Mode::Replacement, &status));
    }
};
struct Child {
    QProcess process;
    ~Child() { if (process.state() != QProcess::NotRunning) { process.kill(); if (!process.waitForFinished(3000)) qFatal("The peer test child did not stop."); } }
    QByteArray line() {
        QElapsedTimer time; time.start();
        while (!process.canReadLine() && time.elapsed() < 3000) {
            process.waitForReadyRead(100); if (process.state() == QProcess::NotRunning && !process.canReadLine()) return {};
        }
        return process.readLine().trimmed();
    }
    bool send(const char* command) { return process.write(command, 1) == 1; }
};
int child(const QStringList& args) {
    const auto root = args.at(2); G::Epoch epoch{QByteArray::fromHex(args.at(3).toLatin1()), args.at(4).toULongLong()};
    StoreAuthority authority(root); auto peer = authority.attachPeer(); if (!peer) return 2;
    auto stamp = peer->close(); if (!stamp || !peer->drain(stamp)) return 3;
    G::Status status; auto prepared = G::prepareJoin(root, epoch, control(), &status);
    if (!prepared.fence || !peer->admit(stamp, std::move(prepared.fence))) return 4;
    const auto old = authority.capture(); if (!old.current()) return 5;
    std::atomic_uint calls{0};
    const auto watch = old.onInvalidated([&] {
        ++calls; std::puts("cancelling"); std::fflush(stdout);
        if (std::getc(stdin) != 'd') std::abort();
    });
    if (!bool(watch)) return 6;
    std::puts("ready"); std::fflush(stdout); if (std::getc(stdin) != 'r') return 7;
    stamp = peer->close(); if (old.current() || authority.capture().current() || bool(watch)) return 8;
    std::puts("closed"); std::fflush(stdout);
    if (!peer->drain(stamp) || calls.load() != 1) return 9;
    std::puts("drained"); std::fflush(stdout); if (std::getc(stdin) != 'j') return 10;
    stamp = peer->close(); if (!peer->drain(stamp)) return 11;
    ++epoch.revision; prepared = G::prepareJoin(root, epoch, control(), &status);
    if (!prepared.fence || !peer->admit(stamp, std::move(prepared.fence))) return 12;
    if (old.current() || bool(watch) || !authority.capture().current()) return 13;
    std::puts("rejoined"); std::fflush(stdout); if (std::getc(stdin) != 'x') return 14;
    peer.reset(); return !authority.capture().current() && !old.current() && calls.load() == 1 ? 0 : 15;
}
}
class TestAddonStoreProfilePeer : public QObject {
    Q_OBJECT
private slots:
    void attachClosesOldTokensAndNeverRestoresUngatedAdmission() {
        QTemporaryDir root; StoreAuthority authority(root.path()); const auto old = authority.capture(); QVERIFY(old.current());
        auto peer = authority.attachPeer(); QVERIFY(peer); QVERIFY(!old.current() && !authority.capture().current());
        const auto stamp = peer->close(); QVERIFY(peer->pending(stamp)()); QVERIFY(peer->drain(stamp));
        peer.reset(); QVERIFY(!old.current() && !authority.capture().current());
        auto replacement = authority.attachPeer(); QVERIFY(replacement); QVERIFY(!authority.capture().current());
    }
    void onePeerOwnsEachCanonicalStore() {
        Fixture f; QVERIFY(f.valid); StoreAuthority other(f.root.path() + QStringLiteral("/.")); QVERIFY(!other.attachPeer());
        const auto close = f.peer->closeHandle(); const auto pending = f.peer->pending(f.stamp);
        f.peer.reset(); QCOMPARE(close(), quint64(0)); QVERIFY(!pending()); QVERIFY(other.attachPeer());
    }
    void admissionNeedsALiveFenceAndItsExactDrainedStamp() {
        Fixture f; QVERIFY(f.valid); QVERIFY(!f.peer->admit(f.stamp, {})); QVERIFY(!f.peer->admit(f.stamp + 1, f.fence()));
        const auto next = f.peer->close(); QVERIFY(!f.peer->admit(next, f.fence())); QVERIFY(!f.peer->admit(f.stamp, f.fence()));
        QVERIFY(f.peer->drain(next)); QVERIFY(f.peer->admit(next, f.fence())); QVERIFY(f.authority.capture().current()); QVERIFY(!f.exclusive());
        QVERIFY(!f.peer->admit(next, f.fence()));
    }
    void pendingJoinHandlesCannotReviveAnOldAttempt() {
        Fixture f; QVERIFY(f.valid); const auto old = f.peer->pending(f.stamp); const auto close = f.peer->closeHandle(); const auto next = close();
        QVERIFY(!old()); QVERIFY(f.peer->pending(next)()); QVERIFY(!f.peer->drain(f.stamp)); QVERIFY(f.peer->drain(next));
        QVERIFY(!f.peer->admit(f.stamp, f.fence())); QVERIFY(f.peer->admit(next, f.fence())); QVERIFY(!f.peer->pending(next)());
    }
    void closeIsImmediateWhileDrainWaitsForNativeCancellation() {
        Fixture f; QVERIFY(f.valid && f.admit()); const auto old = f.authority.capture(); Pause pause;
        std::atomic_uint calls{0}; const auto watch = old.onInvalidated([&] { ++calls; pause.waitHere(); }); QVERIFY(bool(watch));
        const auto close = f.peer->closeHandle(); QElapsedTimer time; time.start(); const auto stamp = close(); QVERIFY(time.elapsed() < 100);
        QVERIFY(!old.current() && !bool(watch) && !f.authority.capture().current()); QVERIFY(!f.exclusive());
        std::atomic_bool done{false}, drained{false}; std::thread work([&] { drained.store(f.peer->drain(stamp)); done.store(true); });
        const auto joined = qScopeGuard([&] { pause.release(); work.join(); });
        QVERIFY(pause.wait()); QVERIFY(!done.load() && !f.exclusive()); QVERIFY(!f.authority.attachPeer());
        pause.release(); QTRY_VERIFY(done.load()); QVERIFY(drained.load()); QCOMPARE(calls.load(), 1u); QVERIFY(f.exclusive()); QVERIFY(!old.current() && !bool(watch));
    }
    void aRefusedNativeRegistrationAlsoKeepsTheFenceUntilDrainEnds() {
        Fixture f; QVERIFY(f.valid && f.admit()); const auto old = f.authority.capture(); const auto stamp = f.peer->close(); Pause pause;
        std::atomic_bool registered{false}, done{false};
        std::thread registration([&] { const auto refused = old.onInvalidated([&] { pause.waitHere(); }); registered.store(!bool(refused)); });
        std::thread work;
        const auto joined = qScopeGuard([&] { pause.release(); registration.join(); if (work.joinable()) work.join(); });
        QVERIFY(pause.wait()); work = std::thread([&] { f.peer->drain(stamp); done.store(true); });
        QVERIFY(!f.exclusive() && !done.load()); pause.release(); QTRY_VERIFY(done.load() && registered.load()); QVERIFY(f.exclusive());
    }
    void aNewCloseDuringDrainInvalidatesItsCompletion() {
        Fixture f; QVERIFY(f.valid && f.admit()); Pause pause; const auto token = f.authority.capture();
        const auto watch = token.onInvalidated([&] { pause.waitHere(); }); QVERIFY(bool(watch));
        const auto first = f.peer->close(); std::atomic_bool done{false}, drained{true};
        std::thread work([&] { drained.store(f.peer->drain(first)); done.store(true); });
        const auto joined = qScopeGuard([&] { pause.release(); work.join(); }); QVERIFY(pause.wait());
        const auto second = f.peer->close(); QVERIFY(second != first); QVERIFY(!f.peer->pending(first)());
        pause.release(); QTRY_VERIFY(done.load()); QVERIFY(!drained.load()); QVERIFY(f.exclusive());
        QVERIFY(!f.peer->admit(first, f.fence())); QVERIFY(f.peer->drain(second)); QVERIFY(f.peer->admit(second, f.fence())); QVERIFY(!token.current());
    }
    void ownerDestructionDrainsBeforeAReplacementCanAttach() {
        Fixture f; QVERIFY(f.valid && f.admit()); Pause pause; const auto old = f.authority.capture();
        const auto watch = old.onInvalidated([&] { pause.waitHere(); }); QVERIFY(bool(watch));
        const auto close = f.peer->closeHandle(); std::atomic_bool done{false}; auto owner = std::move(f.peer);
        std::thread work([&] { owner.reset(); done.store(true); }); const auto joined = qScopeGuard([&] { pause.release(); work.join(); });
        QVERIFY(pause.wait()); QCOMPARE(close(), quint64(0)); QVERIFY(!done.load() && !old.current()); QVERIFY(!f.exclusive() && !f.authority.attachPeer());
        pause.release(); QTRY_VERIFY(done.load()); QVERIFY(f.exclusive()); QVERIFY(!f.authority.capture().current()); QVERIFY(f.authority.attachPeer());
    }
    void localInvalidationAlsoRequiresANewReadyConfirmation_data() { QTest::addColumn<bool>("mutation"); QTest::newRow("mutation") << true; QTest::newRow("invalidate") << false; }
    void localInvalidationAlsoRequiresANewReadyConfirmation() {
        QFETCH(bool, mutation); Fixture f; QVERIFY(f.valid && f.admit()); const auto old = f.authority.capture(); std::atomic_uint calls{0};
        const auto watch = old.onInvalidated([&] { ++calls; }); QVERIFY(bool(watch));
        if (mutation) { const auto writer = f.authority.mutation(); QVERIFY(!f.authority.capture().current()); }
        else f.authority.invalidate();
        QVERIFY(!old.current() && !f.authority.capture().current() && !bool(watch)); QCOMPARE(calls.load(), 1u);
        f.stamp = f.peer->close(); QVERIFY(f.peer->drain(f.stamp) && f.admit()); QVERIFY(f.authority.capture().current()); QVERIFY(!old.current());
    }
    void retainedTokensAndWatchesDoNotKeepTheOldFence() {
        Fixture f; QVERIFY(f.valid && f.admit()); const auto old = f.authority.capture(); unsigned calls = 0;
        const auto watch = old.onInvalidated([&] { ++calls; }); QVERIFY(bool(watch)); f.stamp = f.peer->close(); QVERIFY(f.peer->drain(f.stamp));
        QVERIFY(f.exclusive()); QVERIFY(!old.current() && !bool(watch)); QVERIFY(f.admit()); QCOMPARE(calls, 1u); QVERIFY(!old.current() && !bool(watch)); QVERIFY(f.authority.capture().current());
    }
    void callbackCaptureDestructionFinishesBeforeFenceRelease() {
        struct Capture { Pause* pause; bool armed = false; ~Capture() { if (armed) pause->waitHere(); } };
        Fixture f; QVERIFY(f.valid && f.admit()); Pause cancel, destruction;
        auto capture = std::make_shared<Capture>(); capture->pause = &destruction;
        auto watch = f.authority.capture().onInvalidated([capture, &cancel] { cancel.waitHere(); }); QVERIFY(bool(watch));
        const auto stamp = f.peer->close(); std::atomic_bool done{false}; std::thread work([&] { f.peer->drain(stamp); done.store(true); });
        const auto joined = qScopeGuard([&] { cancel.release(); destruction.release(); work.join(); });
        QVERIFY(cancel.wait()); capture->armed = true; capture.reset(); watch = {}; cancel.release(); QVERIFY(destruction.wait()); QVERIFY(!done.load() && !f.exclusive());
        destruction.release(); QTRY_VERIFY(done.load()); QVERIFY(f.exclusive());
    }
    void aRacingCloseCannotLeaveAdmissionOpen() {
        Fixture f; QVERIFY(f.valid);
        for (unsigned i = 0; i < 50; ++i) {
            f.stamp = f.peer->close(); QVERIFY(f.peer->drain(f.stamp)); auto fence = f.fence(); QVERIFY(fence);
            std::atomic_bool start{false}; quint64 closed = 0;
            std::thread ready([&] { while (!start.load()) std::this_thread::yield(); f.peer->admit(f.stamp, std::move(fence)); });
            std::thread stop([&] { while (!start.load()) std::this_thread::yield(); closed = f.peer->close(); });
            start.store(true); ready.join(); stop.join(); QVERIFY(closed && !f.authority.capture().current()); QVERIFY(f.peer->drain(closed)); QVERIFY(f.exclusive());
        }
    }
    void aRealPeerClosesBeforeAcknowledgingAndCannotReviveOldTokens() {
        QTemporaryDir root; G::Status status; auto coordinator = G::acquireCoordinator(root.path(), &status); QVERIFY(coordinator);
        const auto initial = coordinator->recover(control(), [](const auto& context, auto*) { return context.current(); }); QCOMPARE(initial.status, G::Status::Ready);
        Child peer; peer.process.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--hakone-peer-child"), root.path(), QString::fromLatin1(initial.epoch.generation.toHex()), QString::number(initial.epoch.revision)});
        QVERIFY(peer.process.waitForStarted(3000)); QCOMPARE(peer.line(), QByteArray("ready"));
        const auto current = control(); auto writer = G::prepareWriter(root.path(), current, &status); QVERIFY(writer); QVERIFY(peer.send("r"));
        QCOMPARE(peer.line(), QByteArray("closed")); QCOMPARE(peer.line(), QByteArray("cancelling"));
        StoreProcessFence::Status fenceStatus;
        QVERIFY(!StoreProcessFence::acquire(root.filePath(QStringLiteral("hakone-authority.fence")), StoreProcessFence::Mode::Replacement, &fenceStatus)); QCOMPARE(fenceStatus, StoreProcessFence::Status::Busy);
        QVERIFY(peer.send("d")); QCOMPARE(peer.line(), QByteArray("drained"));
        const auto changed = writer->write({initial.epoch, QByteArray(16, 'w'), current}, [](const auto& context, auto*) { return context.current() ? G::Change::Applied : G::Change::NeedsRecovery; }); QCOMPARE(changed.status, G::Status::Ready);
        QVERIFY(peer.send("j")); QCOMPARE(peer.line(), QByteArray("rejoined"));
        QVERIFY(!StoreProcessFence::acquire(root.filePath(QStringLiteral("hakone-authority.fence")), StoreProcessFence::Mode::Replacement, &fenceStatus)); QCOMPARE(fenceStatus, StoreProcessFence::Status::Busy);
        QVERIFY(peer.send("x")); QVERIFY(peer.process.waitForFinished(3000)); QCOMPARE(peer.process.exitStatus(), QProcess::NormalExit); QCOMPARE(peer.process.exitCode(), 0); QVERIFY(peer.process.readAllStandardError().isEmpty());
        QCOMPARE(coordinator->recover(control(), [](const auto&, auto*) { return true; }).status, G::Status::Ready);
    }
};
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv); const auto args = app.arguments();
    if (args.size() == 5 && args.at(1) == QLatin1String("--hakone-peer-child")) return child(args);
    TestAddonStoreProfilePeer test; return QTest::qExec(&test, argc, argv);
}
#include "test_addon_store_profile_peer.moc"
