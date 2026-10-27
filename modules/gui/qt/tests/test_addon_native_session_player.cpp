// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_native_session_player.hpp"
#include "addons/addon_store_authority.hpp"
#include "session-runtime/native_session_fixture.h"
#include <QTest>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QFile>
#include <thread>
#include <cmath>
#include <atomic>
#include <fstream>
using namespace vlc::addons;
namespace {
struct Fixture {
    native_session_fixture* native=native_session_create();
    ~Fixture() { native_session_destroy(native); }
};
SessionPlayerCommand command(SessionPlayerBackend& backend,SessionPlayerId lease,quint64 sequence,SessionPlayerOperation operation) {
    SessionPlayerCommand value; value.expected=backend.snapshot().stamp; value.lease=lease;
    value.sequence=sequence; value.operation=operation; return value;
}
bool completion(SessionPlayerBackend& backend,quint64 sequence,bool success=true) {
    for(int i=0;i<150;++i) {
        for(const auto& value:backend.completions()) if(value.sequence==sequence && value.success==success) return true;
        QTest::qWait(10);
    }
    return false;
}
}
class TestAddonNativeSessionPlayer : public QObject {
    Q_OBJECT
private slots:
    void actualInputExecutesOnlyAfterFifoAdmission() {
        Fixture fixture; QVERIFY(fixture.native); AddonNativeSessionPlayer adapter(native_session_player(fixture.native));
        auto backend=adapter.backend(); const SessionPlayerId lease{100,200}; const auto initial=backend.snapshot();
        QVERIFY(initial.media); QVERIFY(initial.input); QVERIFY(initial.seekable); QVERIFY(initial.pausable);
        QCOMPARE(backend.grant(initial.stamp,lease,15,30000000),SessionPlayerResult::Accepted);
        native_session_block(fixture.native); const auto unblock=qScopeGuard([&] { native_session_release(fixture.native); });
        auto value=command(backend,lease,1,SessionPlayerOperation::Seek); value.positionUs=20000000;
        const auto seeks=native_session_seeks(fixture.native); QCOMPARE(backend.apply(value),SessionPlayerResult::Accepted);
        QCOMPARE(native_session_seeks(fixture.native),seeks); QVERIFY(backend.completions().isEmpty());
        native_session_release(fixture.native); QVERIFY(completion(backend,1)); QCOMPARE(native_session_last_seek(fixture.native),qint64(20000000));
    }
    void localNativeActionInvalidatesAnAlreadyQueuedRemoteSeek() {
        Fixture fixture; AddonNativeSessionPlayer adapter(native_session_player(fixture.native)); auto backend=adapter.backend(); const SessionPlayerId lease{101,201};
        QCOMPARE(backend.grant(backend.snapshot().stamp,lease,15,30000000),SessionPlayerResult::Accepted);
        native_session_block(fixture.native); const auto unblock=qScopeGuard([&] { native_session_release(fixture.native); });
        const auto seeks=native_session_seeks(fixture.native); auto value=command(backend,lease,1,SessionPlayerOperation::Seek); value.positionUs=30000000;
        QCOMPARE(backend.apply(value),SessionPlayerResult::Accepted); native_session_local_pause(fixture.native);
        QVERIFY(backend.snapshot().suspended); native_session_release(fixture.native); QTRY_COMPARE(native_session_retained(fixture.native),0u);
        for(const auto& done:backend.completions()) QVERIFY(done.sequence!=1 || !done.success);
        QCOMPARE(native_session_seeks(fixture.native),seeks); QCOMPARE(backend.apply(value),SessionPlayerResult::Stale);
    }
    void cancellationRevokesQueuedWorkWithoutQtDelivery() {
        Fixture fixture; AddonNativeSessionPlayer adapter(native_session_player(fixture.native)); auto backend=adapter.backend(); const SessionPlayerId lease{102,202};
        QCOMPARE(backend.grant(backend.snapshot().stamp,lease,15,30000000),SessionPlayerResult::Accepted);
        native_session_block(fixture.native); const auto unblock=qScopeGuard([&] { native_session_release(fixture.native); });
        const auto seeks=native_session_seeks(fixture.native); auto value=command(backend,lease,1,SessionPlayerOperation::Seek); value.positionUs=30000000;
        QCOMPARE(backend.apply(value),SessionPlayerResult::Accepted); const auto cancel=backend.cancellation(lease);
        std::thread worker([cancel] { cancel(); }); worker.join(); QVERIFY(!backend.snapshot().active);
        native_session_release(fixture.native); QTRY_COMPARE(native_session_retained(fixture.native),0u);
        for(const auto& done:backend.completions()) QVERIFY(done.sequence!=1 || !done.success);
        QCOMPARE(native_session_seeks(fixture.native),seeks);
    }
    void correctionRestoresItsRateButPreservesALaterUserRate() {
        Fixture fixture; AddonNativeSessionPlayer adapter(native_session_player(fixture.native)); auto backend=adapter.backend(); const SessionPlayerId lease{103,203};
        QCOMPARE(backend.grant(backend.snapshot().stamp,lease,15,30000000),SessionPlayerResult::Accepted);
        auto value=command(backend,lease,1,SessionPlayerOperation::TemporaryRate); value.rate=1.03; value.durationUs=200000;
        QCOMPARE(backend.apply(value),SessionPlayerResult::Accepted); QVERIFY(completion(backend,1));
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(native_session_rate(fixture.native)-1.f)<0.0001f,3000);
        value=command(backend,lease,2,SessionPlayerOperation::TemporaryRate); value.rate=1.03; value.durationUs=300000;
        QCOMPARE(backend.apply(value),SessionPlayerResult::Accepted); QVERIFY(completion(backend,2));
        native_session_local_rate(fixture.native,1.25f); QTRY_VERIFY(std::abs(native_session_rate(fixture.native)-1.25f)<0.0001f);
        QTest::qWait(450); QVERIFY(std::abs(native_session_rate(fixture.native)-1.25f)<0.0001f); QVERIFY(backend.snapshot().suspended);
    }
    void stopAndReplayChangeInputGeneration() {
        Fixture fixture; AddonNativeSessionPlayer adapter(native_session_player(fixture.native)); auto backend=adapter.backend(); const SessionPlayerId lease{104,204};
        const auto initial=backend.snapshot(); QCOMPARE(backend.grant(initial.stamp,lease,15,30000000),SessionPlayerResult::Accepted);
        auto old=command(backend,lease,1,SessionPlayerOperation::Pause); native_session_restart(fixture.native);
        const auto replay=backend.snapshot(); QVERIFY(replay.stamp.input!=initial.stamp.input); QVERIFY(!replay.active);
        QVERIFY(backend.apply(old)!=SessionPlayerResult::Accepted);
    }
    void adapterCannotRevokeAnotherOwnerAndLateClosuresAreSafe() {
        Fixture fixture; auto adapter=std::make_unique<AddonNativeSessionPlayer>(native_session_player(fixture.native)); auto backend=adapter->backend();
        AddonNativeSessionPlayer other(native_session_player(fixture.native)); auto second=other.backend(); const SessionPlayerId lease{105,205};
        QCOMPARE(backend.grant(backend.snapshot().stamp,lease,15,30000000),SessionPlayerResult::Accepted);
        QVERIFY(!second.revoke(lease)); QCOMPARE(second.grant(second.snapshot().stamp,{106,206},15,30000000),SessionPlayerResult::Busy);
        const auto cancel=backend.cancellation(lease); adapter->close(); adapter.reset(); std::thread worker(cancel); worker.join();
        QCOMPARE(backend.grant({},lease,15,30000000),SessionPlayerResult::Closed); QVERIFY(!second.snapshot().active);
    }
    void storePublicationWaitsForConcurrentActualNativeRevocation() {
        Fixture fixture; AddonNativeSessionPlayer adapter(native_session_player(fixture.native)); auto backend=adapter.backend();
        const SessionPlayerId lease{109,209}; QCOMPARE(backend.grant(backend.snapshot().stamp,lease,15,30000000),SessionPlayerResult::Accepted);
        QTemporaryDir directory; StoreAuthority authority(directory.path()); const auto token=authority.capture();
        const auto cancel=backend.cancellation(lease); const auto watch=token.onInvalidated(cancel); QVERIFY(bool(watch));
        native_session_block(fixture.native);
        const auto seeks=native_session_seeks(fixture.native); auto queued=command(backend,lease,1,SessionPlayerOperation::Seek); queued.positionUs=30000000;
        QCOMPARE(backend.apply(queued),SessionPlayerResult::Accepted);
        const auto path=directory.filePath(QStringLiteral("settings.json")); const auto nativePath=path.toStdString();
        bool locked=true; native_session_lock(fixture.native);
        std::atomic_bool started{false},firstDone{false},published{false},revokedBeforePublication{false}; std::thread first,writer;
        const auto cleanup=qScopeGuard([&] {
            if(locked) native_session_unlock(fixture.native);
            if(first.joinable()) first.join(); if(writer.joinable()) writer.join(); native_session_release(fixture.native);
        });
        first=std::thread([&] { started=true; cancel(); firstDone=true; });
        auto wait=[](auto ready) {
            const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);
            while(!ready() && std::chrono::steady_clock::now()<until) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return ready();
        };
        QVERIFY(wait([&] { return started.load(); }));
        writer=std::thread([&] {
            const auto mutation=authority.mutation();
            revokedBeforePublication=!native_session_active(fixture.native);
            std::ofstream file(nativePath); file<<"{\"settings\":2}"; file.flush(); published=file.good();
        });
        // No Qt event runs while the actual player lock prevents cancellation.
        QVERIFY(wait([&] { return !token.current(); })); QVERIFY(!firstDone.load()); QVERIFY(!published.load()); QVERIFY(!QFile::exists(path));
        native_session_unlock(fixture.native); locked=false;
        QVERIFY(wait([&] { return published.load(); })); QVERIFY(revokedBeforePublication.load()); QVERIFY(!bool(watch));
        first.join(); writer.join(); QVERIFY(!backend.snapshot().active);
        native_session_release(fixture.native); QTRY_COMPARE(native_session_retained(fixture.native),0u);
        QCOMPARE(native_session_seeks(fixture.native),seeks);
        for(const auto& done:backend.completions()) QVERIFY(done.sequence!=1 || !done.success);
    }
    void reportedCloseCannotLetAWriterPassAnUnfinishedNativeRevoke() {
        Fixture fixture; AddonNativeSessionPlayer adapter(native_session_player(fixture.native)); auto backend=adapter.backend();
        const SessionPlayerId lease{110,210}; QCOMPARE(backend.grant(backend.snapshot().stamp,lease,15,30000000),SessionPlayerResult::Accepted);
        QTemporaryDir directory; StoreAuthority authority(directory.path()); const auto token=authority.capture();
        const auto watch=token.onInvalidated(backend.cancellation(lease)); QVERIFY(bool(watch));
        const auto path=directory.filePath(QStringLiteral("close-settings.json")).toStdString();
        std::atomic_bool held{false},closing{false},probeStarted{false},published{false},publishedWhileHeld{false},stop{false};
        std::thread holder,probe;
        const auto cleanup=qScopeGuard([&] { stop=true; closing=true; probeStarted=true; if(holder.joinable()) holder.join(); if(probe.joinable()) probe.join(); });
        holder=std::thread([&] {
            native_session_lock(fixture.native); held=true;
            while(!probeStarted.load() && !stop.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);
            while(!published.load() && !stop.load() && std::chrono::steady_clock::now()<until) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            publishedWhileHeld=published.load(); native_session_unlock(fixture.native);
        });
        probe=std::thread([&] {
            while(!closing.load() && !stop.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if(stop.load()) return; probeStarted=true;
            while(adapter.available() && !stop.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if(stop.load()) return;
            const auto mutation=authority.mutation(); std::ofstream file(path); file<<"{\"settings\":3}"; file.flush(); published=file.good();
        });
        const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);
        while(!held.load() && std::chrono::steady_clock::now()<until) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        QVERIFY(held.load()); closing=true; adapter.close();
        holder.join(); probe.join(); QVERIFY(published.load()); QVERIFY(!publishedWhileHeld.load());
        QVERIFY(!adapter.available()); QVERIFY(!native_session_active(fixture.native)); QVERIFY(!bool(watch));
    }
    void twoActualPlayersReduceAKnownSeekOffset() {
        Fixture first,second; AddonNativeSessionPlayer a(native_session_player(first.native)),b(native_session_player(second.native));
        auto left=a.backend(),right=b.backend(); const SessionPlayerId one{107,207},two{108,208};
        QCOMPARE(left.grant(left.snapshot().stamp,one,15,30000000),SessionPlayerResult::Accepted);
        QCOMPARE(right.grant(right.snapshot().stamp,two,15,30000000),SessionPlayerResult::Accepted);
        auto l=command(left,one,1,SessionPlayerOperation::Seek),r=command(right,two,1,SessionPlayerOperation::Seek);
        l.positionUs=20000000; r.positionUs=2000000;
        QCOMPARE(left.apply(l),SessionPlayerResult::Accepted); QCOMPARE(right.apply(r),SessionPlayerResult::Accepted);
        QVERIFY(completion(left,1)); QVERIFY(completion(right,1));
        const auto before=std::abs(native_session_last_seek(first.native)-native_session_last_seek(second.native)); QCOMPARE(before,qint64(18000000));
        r=command(right,two,2,SessionPlayerOperation::Seek); r.positionUs=native_session_last_seek(first.native);
        QCOMPARE(right.apply(r),SessionPlayerResult::Accepted); QVERIFY(completion(right,2));
        const auto after=std::abs(native_session_last_seek(first.native)-native_session_last_seek(second.native)); QCOMPARE(after,qint64(0));
        qInfo("Native seek-target offset: 18000000 us before; 0 us after. This is not an output-clock test.");
    }
};
QTEST_GUILESS_MAIN(TestAddonNativeSessionPlayer)
#include "test_addon_native_session_player.moc"
