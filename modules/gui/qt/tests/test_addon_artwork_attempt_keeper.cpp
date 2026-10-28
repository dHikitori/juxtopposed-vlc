#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "addons/addon_artwork_attempt_keeper.hpp"
#include "fixtures/addon_artwork_attempt_keeper.h"
#include <vlc_meta.h>
#include <vlc_threads.h>
#include <QProcess>
#include <cstring>
#include <QTest>
#include <QTemporaryDir>
#include <atomic>
#include <chrono>
#include <thread>
using namespace vlc::addons;
namespace {
using Fixture = std::unique_ptr<keeper_fixture, decltype(&keeper_fixture_delete)>;
Fixture fixture() { return Fixture(keeper_fixture_new(), keeper_fixture_delete); }
auto admission() { return CoreMetadataAdmission::create(vlc_tick_now() + VLC_TICK_FROM_SEC(20)); }
bool wait(const std::function<bool()>& condition) {
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while (!condition() && std::chrono::steady_clock::now()<until) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return condition();
}
std::array<unsigned char,32> digest() { std::array<unsigned char,32> value{}; value[0]=17; return value; }
const std::vector<meta_fetcher_outcome_t> negative{META_FETCHER_OUTCOME_NO_MATCH,META_FETCHER_OUTCOME_NOT_APPLICABLE};
auto keeper(const std::shared_ptr<CoreMetadataAdmission>& gate, const StoreAuthority& authority) {
    NativeArtworkAttemptKeeper::Parts parts;parts.admission=gate;
    parts.watches.push_back(NativeArtworkAttemptKeeper::watch(authority.capture(),gate));
    return NativeArtworkAttemptKeeper::create(std::move(parts));
}
struct DropMarker {
    std::atomic_bool* dropped;std::atomic_bool* quotaHeld;
    ~DropMarker() { quotaHeld->store(CoreMetadataAdmission::processCount()>0);dropped->store(true); }
};
}
class KeeperTests:public QObject {
    Q_OBJECT
private slots:
    void init() { QCOMPARE(CoreMetadataAdmission::processCount(),0u); }
    void cleanup() { QCOMPARE(CoreMetadataAdmission::processCount(),0u); }
    void deadlineAndCurrent() {
        QVERIFY(!CoreMetadataAdmission::create(vlc_tick_now()-1));
        QVERIFY(!CoreMetadataAdmission::create(vlc_tick_now()+VLC_TICK_FROM_SEC(31)));
        auto gate=CoreMetadataAdmission::create(vlc_tick_now()+VLC_TICK_FROM_MS(50));QVERIFY(gate);QVERIFY(gate->current());
        QTest::qWait(70);QVERIFY(!gate->current());gate->revoke();gate->revoke();
    }
    void sixtyFourPlansAndRetainedWatch() {
        QTemporaryDir directory;QVERIFY(directory.isValid());StoreAuthority authority(directory.path());
        std::vector<std::shared_ptr<CoreMetadataAdmission>> gates;
        for(unsigned i=0;i<64;++i){auto gate=admission();QVERIFY(gate);gates.push_back(gate);}
        QVERIFY(!admission());QCOMPARE(CoreMetadataAdmission::processCount(),64u);
        auto watch=NativeArtworkAttemptKeeper::watch(authority.capture(),gates.front());QVERIFY(watch);
        gates.clear();QCOMPARE(CoreMetadataAdmission::processCount(),1u);
        watch={};QCOMPARE(CoreMetadataAdmission::processCount(),0u);
    }
    void repeatedRevokersJoinNativeSection() {
        auto gate=admission();QVERIFY(gate);QVERIFY(keeper_fixture_enter(gate->nativeHandle()));
        std::atomic_bool first{false},second{false},started{false};
        std::thread one([&]{gate->revoke();first=true;});
        const bool revoked=wait([&]{return !gate->current();});
        std::thread two([&]{started=true;gate->revoke();second=true;});
        const bool secondStarted=wait([&]{return started.load();});
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        const bool waited=!first.load()&&!second.load();
        keeper_fixture_leave(gate->nativeHandle());one.join();two.join();
        QVERIFY(revoked);QVERIFY(secondStarted);QVERIFY(waited);QVERIFY(first&&second);
    }
    void failedSealKeepsCallerOwnership() {
        QTemporaryDir directory;StoreAuthority authority(directory.path());auto gate=admission();QVERIFY(gate);
        auto owner=keeper(gate,authority);QVERIFY(owner);auto* exact=owner.get();
        QVERIFY(NativeArtworkAttemptKeeper::sealPending(nullptr,digest(),negative,owner)!=VLC_SUCCESS);
        QCOMPARE(owner.get(),exact);QVERIFY(!gate->current());
    }
    void invalidSummaryNeverTransfers() {
        QTemporaryDir directory;StoreAuthority authority(directory.path());
        for(const auto& values:std::vector<std::vector<meta_fetcher_outcome_t>>{{},{META_FETCHER_OUTCOME_NOT_APPLICABLE},{META_FETCHER_OUTCOME_MATCH}}) {
            auto gate=admission();auto owner=keeper(gate,authority);QVERIFY(owner);
            QVERIFY(NativeArtworkAttemptKeeper::sealPending(nullptr,digest(),values,owner)!=VLC_SUCCESS);QVERIFY(owner);QVERIFY(!gate->current());
        }
    }
    void successTransfersToRealSidebandAndDestroysCapturesBeforeQuota() {
        QTemporaryDir directory;StoreAuthority authority(directory.path());auto native=fixture();QVERIFY(native);
        auto gate=admission();std::weak_ptr<CoreMetadataAdmission> weak=gate;
        std::atomic_bool dropped{false},quotaHeld{false};
        auto marker=std::make_shared<DropMarker>();marker->dropped=&dropped;marker->quotaHeld=&quotaHeld;
        NativeArtworkAttemptKeeper::Parts parts;parts.admission=gate;
        parts.watches.push_back(authority.capture().onInvalidated([gate,marker]{gate->revoke();}));marker.reset();
        auto owner=NativeArtworkAttemptKeeper::create(std::move(parts));QVERIFY(owner);
        QCOMPARE(NativeArtworkAttemptKeeper::sealPending(keeper_fixture_child(native.get()),digest(),negative,owner),VLC_SUCCESS);
        QVERIFY(!owner);gate.reset();QVERIFY(!weak.expired());QVERIFY(!dropped);
        QCOMPARE(keeper_fixture_prepare_anchor(native.get()),VLC_SUCCESS);
        QCOMPARE(keeper_fixture_apply(native.get()),VLC_SUCCESS);
        QVERIFY(keeper_fixture_status(native.get())&ITEM_ART_NOTFOUND);
        QVERIFY(keeper_fixture_status(native.get())&ITEM_PREPARSED);
        native.reset();QVERIFY(weak.expired());QVERIFY(dropped);QVERIFY(quotaHeld);
    }
    void storeAndSecondRevokeWaitForActualCoreApply() {
        QTemporaryDir directory;StoreAuthority authority(directory.path());auto native=fixture();auto gate=admission();
        auto owner=keeper(gate,authority);QVERIFY(owner);
        QCOMPARE(NativeArtworkAttemptKeeper::sealPending(keeper_fixture_child(native.get()),digest(),negative,owner),VLC_SUCCESS);
        QCOMPARE(keeper_fixture_prepare_anchor(native.get()),VLC_SUCCESS);
        keeper_fixture_lock_target(native.get());
        std::atomic_int status{VLC_EGENERIC};std::atomic_bool applied{false},written{false},second{false},secondStarted{false};
        std::thread apply([&]{status=keeper_fixture_apply(native.get());applied=true;});
        const bool entered=wait([&]{return keeper_fixture_gate_locked(gate->nativeHandle());});
        std::thread writer([&]{authority.invalidate();written=true;});
        const bool revoked=wait([&]{return !gate->current();});
        std::thread repeat([&]{secondStarted=true;gate->revoke();second=true;});
        const bool started=wait([&]{return secondStarted.load();});
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        const bool held=!applied.load()&&!written.load()&&!second.load();
        keeper_fixture_unlock_target(native.get());apply.join();writer.join();repeat.join();
        QVERIFY(entered);QVERIFY(revoked);QVERIFY(started);QVERIFY(held);QCOMPARE(status.load(),VLC_SUCCESS);
        QVERIFY(keeper_fixture_status(native.get())&ITEM_ART_NOTFOUND);QVERIFY(written&&second);
    }
    void nativeKeeperCanBeReleasedAfterApplicationDestruction() {
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--native-after-app")});
        QVERIFY(child.waitForStarted(3000));QVERIFY(child.waitForFinished(10000));
        QCOMPARE(child.exitStatus(),QProcess::NormalExit);QCOMPARE(child.exitCode(),0);
        QVERIFY2(child.readAllStandardError().isEmpty(),"Post-app native cleanup produced diagnostics");
    }
    void storeRevocationBeforeApplyRefusesWholeTransaction() {
        QTemporaryDir directory;StoreAuthority authority(directory.path());auto native=fixture();auto gate=admission();auto owner=keeper(gate,authority);
        QCOMPARE(NativeArtworkAttemptKeeper::sealPending(keeper_fixture_child(native.get()),digest(),negative,owner),VLC_SUCCESS);
        QCOMPARE(keeper_fixture_prepare_anchor(native.get()),VLC_SUCCESS);
        authority.invalidate();QVERIFY(!gate->current());QVERIFY(keeper_fixture_apply(native.get())!=VLC_SUCCESS);
        QVERIFY(!(keeper_fixture_status(native.get())&ITEM_ART_NOTFOUND));QVERIFY(keeper_fixture_status(native.get())&ITEM_PREPARSED);
    }
};
int main(int argc,char**argv) {
    if (argc==2 && !std::strcmp(argv[1],"--native-after-app")) {
        auto app=std::make_unique<QCoreApplication>(argc,argv);
        Fixture native(nullptr,keeper_fixture_delete);
        std::weak_ptr<CoreMetadataAdmission> weak;
        {
            QTemporaryDir directory;if(!directory.isValid())return 10;
            StoreAuthority authority(directory.path());native=fixture();auto gate=admission();weak=gate;
            auto owner=keeper(gate,authority);if(!owner)return 11;
            if(NativeArtworkAttemptKeeper::sealPending(keeper_fixture_child(native.get()),digest(),negative,owner)!=VLC_SUCCESS)return 12;
            if(keeper_fixture_prepare_anchor(native.get())!=VLC_SUCCESS || keeper_fixture_apply(native.get())!=VLC_SUCCESS)return 13;
        }
        if(weak.expired() || CoreMetadataAdmission::processCount()!=1)return 14;
        app.reset(); // All Qt-owning test locals are already gone.
        native.reset(); // Actual Core sideband now destroys native/std keeper.
        return weak.expired() && CoreMetadataAdmission::processCount()==0 ? 0 : 15;
    }
    QCoreApplication app(argc,argv);KeeperTests test;return QTest::qExec(&test,argc,argv);
}
#include "test_addon_artwork_attempt_keeper.moc"
