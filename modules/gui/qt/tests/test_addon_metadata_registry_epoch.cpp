#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "addons/addon_metadata_registry_epoch.hpp"
#include "addons/addon_artwork_attempt_keeper.hpp"
#include "fixtures/addon_artwork_attempt_keeper.h"
#include "src/preparser/metadata_artwork_outcome.h"
#include <vlc_meta.h>
#include <vlc_threads.h>
#include <QProcess>
#include <QTest>
#include <QTemporaryDir>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>
using namespace vlc::addons;
namespace {
using Fixture = std::unique_ptr<keeper_fixture, decltype(&keeper_fixture_delete)>;
Fixture fixture() { return Fixture(keeper_fixture_new(), keeper_fixture_delete); }
auto admission() { return CoreMetadataAdmission::create(vlc_tick_now() + VLC_TICK_FROM_SEC(20)); }
bool wait(const std::function<bool()>& condition) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!condition() && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return condition();
}
std::array<unsigned char,32> digest() { std::array<unsigned char,32> value{}; value[0]=33; return value; }
auto sideband(const std::shared_ptr<CoreMetadataAdmission>& gate) {
    // The keeper independently requires a package authority watch. Its native
    // State/callback is retained; this local Qt directory is not in the keeper.
    QTemporaryDir directory;
    if (!directory.isValid()) return std::unique_ptr<NativeArtworkAttemptKeeper>{};
    StoreAuthority store(directory.path());
    NativeArtworkAttemptKeeper::Parts parts; parts.admission=gate;
    parts.watches.push_back(NativeArtworkAttemptKeeper::watch(store.capture(),gate));
    return NativeArtworkAttemptKeeper::create(std::move(parts));
}
bool prepare(const Fixture& native, const std::shared_ptr<CoreMetadataAdmission>& gate) {
    auto keeper=sideband(gate);
    return keeper && NativeArtworkAttemptKeeper::sealPending(keeper_fixture_child(native.get()),digest(),
        {META_FETCHER_OUTCOME_NO_MATCH},keeper)==VLC_SUCCESS
        && keeper_fixture_prepare_anchor(native.get())==VLC_SUCCESS;
}
// All quota/race controls still call actual Core gate operations. The raw form
// bypasses only the C++ 64-plan quota so the registry's independent 65th arm
// refusal can be exercised, without inventing a fake bool authority.
class RawAdmission final : public AddonMetadataNativeAdmission {
public:
    RawAdmission() : gate(vlc_metadata_artwork_admission_New(vlc_tick_now()+VLC_TICK_FROM_SEC(20))) {}
    ~RawAdmission() override { vlc_metadata_artwork_admission_Release(gate); }
    bool current() const noexcept override { return gate && vlc_metadata_artwork_admission_IsCurrent(gate); }
    void revoke() noexcept override { if(gate) vlc_metadata_artwork_admission_Revoke(gate); }
    vlc_metadata_artwork_admission_t* const gate;
};
}
class RegistryTests : public QObject {
    Q_OBJECT
private slots:
    void init() { QCOMPARE(CoreMetadataAdmission::processCount(),0u); }
    void cleanup() { QCOMPARE(CoreMetadataAdmission::processCount(),0u); }
    void tokenHasExactFixedIdentityAndNeverRevives() {
        MetadataRegistryToken missing; QVERIFY(!missing.current()); QCOMPARE(missing.identity(),(std::array<unsigned char,32>{}));
        MetadataRegistryEpoch one,two; const auto old=one.capture(),foreign=two.capture();
        QVERIFY(old.current()); QVERIFY(foreign.current()); QVERIFY(old.identity()!=foreign.identity());
        const auto stamp=old.identity(); { auto change=one.mutation(); QVERIFY(change); QVERIFY(!old.current()); QVERIFY(!one.capture().current()); }
        const auto fresh=one.capture(); QVERIFY(fresh.current()); QVERIFY(!old.current()); QVERIFY(stamp!=fresh.identity());
    }
    void nestedAndMovedScopesKeepCaptureClosed() {
        MetadataRegistryEpoch epoch; const auto old=epoch.capture();
        { auto outer=epoch.mutation(); QVERIFY(outer); QVERIFY(!old.current());
          { auto inner=epoch.mutation(); QVERIFY(inner); auto moved=std::move(inner); QVERIFY(moved); QVERIFY(!inner); QVERIFY(!epoch.capture().current()); }
          QVERIFY(!epoch.capture().current()); QVERIFY(outer);
        }
        QVERIFY(epoch.capture().current()); QVERIFY(!old.current());
    }
    void armThenMutationRevokesExactCoreAdmission() {
        MetadataRegistryEpoch epoch; const auto token=epoch.capture(); auto gate=admission(); QVERIFY(gate);
        QVERIFY(epoch.arm(token,gate)); QVERIFY(epoch.arm(token,gate));
        { auto change=epoch.mutation(); QVERIFY(change); QVERIFY(!gate->current()); QVERIFY(!token.current()); }
        QVERIFY(epoch.capture().current());
    }
    void staleForeignAndChangingArmRevokeBeforeReturn() {
        MetadataRegistryEpoch epoch,foreign; const auto old=epoch.capture();
        { auto change=epoch.mutation(); auto gate=admission(); QVERIFY(!epoch.arm(old,gate)); QVERIFY(!gate->current());
          auto other=admission(); QVERIFY(!epoch.arm(foreign.capture(),other)); QVERIFY(!other->current()); }
        auto stale=admission(); QVERIFY(!epoch.arm(old,stale)); QVERIFY(!stale->current());
    }
    void sixtyFourWeakGatesAndExpiredCleanup() {
        MetadataRegistryEpoch epoch; const auto token=epoch.capture();
        std::vector<std::shared_ptr<RawAdmission>> gates;
        for(unsigned i=0;i<MetadataRegistryEpoch::ArmedLimit;++i) { auto gate=std::make_shared<RawAdmission>(); QVERIFY(gate->current()); QVERIFY(epoch.arm(token,gate)); gates.push_back(gate); }
        QVERIFY(epoch.arm(token,gates.front()));
        auto full=std::make_shared<RawAdmission>(); QVERIFY(full->current()); QVERIFY(!epoch.arm(token,full)); QVERIFY(!full->current());
        std::weak_ptr<RawAdmission> released=gates.front(); gates.front().reset(); QVERIFY(released.expired());
        auto next=std::make_shared<RawAdmission>(); QVERIFY(epoch.arm(token,next)); QVERIFY(next->current());
        epoch.close(); QVERIFY(!next->current());
        for(const auto& gate:gates) if(gate) QVERIFY(!gate->current());
    }
    void expiredNativeDeadlineCannotArm() {
        MetadataRegistryEpoch epoch; auto gate=CoreMetadataAdmission::create(vlc_tick_now()+VLC_TICK_FROM_MS(30)); QVERIFY(gate);
        QTest::qWait(50); QVERIFY(!epoch.arm(epoch.capture(),gate)); QVERIFY(!gate->current());
    }
    void armAndMutationRaceUsesActualCoreGates() {
        MetadataRegistryEpoch epoch;
        for(unsigned i=0;i<200;++i) {
            const auto token=epoch.capture(); auto gate=std::make_shared<RawAdmission>(); QVERIFY(gate->current());
            std::atomic_bool start{false},armed{false},changed{false};
            std::thread arm([&]{while(!start.load())std::this_thread::yield();armed=epoch.arm(token,gate);});
            std::thread mutate([&]{while(!start.load())std::this_thread::yield();auto change=epoch.mutation();changed=bool(change);});
            start=true; arm.join();mutate.join();
            QVERIFY(changed); QVERIFY(!gate->current()); QVERIFY(!token.current()); QVERIFY(epoch.capture().current());
        }
    }
    void concurrentMutationsWaitForRealCorePublication() {
        MetadataRegistryEpoch epoch; const auto token=epoch.capture(); auto native=fixture();auto gate=admission();
        QVERIFY(epoch.arm(token,gate)); QVERIFY(prepare(native,gate)); keeper_fixture_lock_target(native.get());
        std::atomic_int status{VLC_EGENERIC},published{0}; std::atomic_bool applied{false},secondStarted{false};
        std::thread apply([&]{status=keeper_fixture_apply(native.get());applied=true;});
        const bool entered=wait([&]{return keeper_fixture_gate_locked(gate->nativeHandle());});
        std::thread first([&]{auto change=epoch.mutation();if(change)++published;});
        const bool revoked=wait([&]{return !gate->current();});
        std::thread second([&]{secondStarted=true;auto change=epoch.mutation();if(change)++published;});
        const bool started=wait([&]{return secondStarted.load();});
        const auto capture=epoch.capture(); // atomic, cannot wait on the native lock
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        const bool held=!applied.load()&&published.load()==0&&!capture.current();
        keeper_fixture_unlock_target(native.get());apply.join();first.join();second.join();
        QVERIFY(entered);QVERIFY(revoked);QVERIFY(started);QVERIFY(held);QCOMPARE(status.load(),VLC_SUCCESS);QCOMPARE(published.load(),2);
        QVERIFY(keeper_fixture_status(native.get())&ITEM_ART_NOTFOUND); QVERIFY(epoch.capture().current());
    }
    void repeatedCloseJoinsRealCorePublication() {
        MetadataRegistryEpoch epoch; const auto token=epoch.capture(); auto native=fixture();auto gate=admission();
        QVERIFY(epoch.arm(token,gate)); QVERIFY(prepare(native,gate)); keeper_fixture_lock_target(native.get());
        std::atomic_int status{VLC_EGENERIC}; std::atomic_bool applied{false},firstDone{false},secondDone{false},secondStarted{false};
        std::thread apply([&]{status=keeper_fixture_apply(native.get());applied=true;});
        const bool entered=wait([&]{return keeper_fixture_gate_locked(gate->nativeHandle());});
        std::thread first([&]{epoch.close();firstDone=true;});
        const bool revoked=wait([&]{return !gate->current();});
        std::thread second([&]{secondStarted=true;epoch.close();secondDone=true;});
        const bool started=wait([&]{return secondStarted.load();});
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        const bool held=!applied.load()&&!firstDone.load()&&!secondDone.load()&&!epoch.capture().current();
        keeper_fixture_unlock_target(native.get());apply.join();first.join();second.join();
        QVERIFY(entered);QVERIFY(revoked);QVERIFY(started);QVERIFY(held);QCOMPARE(status.load(),VLC_SUCCESS);QVERIFY(firstDone&&secondDone);
        auto cannotReopen=epoch.mutation();QVERIFY(!cannotReopen);QVERIFY(!epoch.capture().current());QVERIFY(!token.current());
    }
    void repeatedCloseAlsoJoinsRefusedRegistration() {
        MetadataRegistryEpoch epoch; const auto stale=epoch.capture();epoch.close();auto gate=admission();
        QVERIFY(keeper_fixture_enter(gate->nativeHandle()));
        std::atomic_bool armed{true},returned{false},closed{false},closeStarted{false};
        std::thread arm([&]{armed=epoch.arm(stale,gate);returned=true;});
        const bool revoked=wait([&]{return !gate->current();});
        std::thread repeat([&]{closeStarted=true;epoch.close();closed=true;});
        const bool started=wait([&]{return closeStarted.load();});
        std::this_thread::sleep_for(std::chrono::milliseconds(25));const bool held=!returned.load()&&!closed.load();
        keeper_fixture_leave(gate->nativeHandle());arm.join();repeat.join();
        QVERIFY(revoked);QVERIFY(started);QVERIFY(held);QVERIFY(returned&&closed);QVERIFY(!armed);
    }
    void revokeBeforeFinalApplyRefusesAllStatusChanges() {
        MetadataRegistryEpoch epoch;auto token=epoch.capture();auto native=fixture();auto gate=admission();
        QVERIFY(epoch.arm(token,gate));QVERIFY(prepare(native,gate));
        {auto change=epoch.mutation();QVERIFY(change);}
        QVERIFY(keeper_fixture_apply(native.get())!=VLC_SUCCESS);
        const int status=keeper_fixture_status(native.get());QVERIFY(!(status&ITEM_ART_NOTFOUND));QVERIFY(status&ITEM_PREPARSED);
    }
    void closeDuringOuterScopeCannotPublishOrReopen() {
        MetadataRegistryEpoch epoch;const auto old=epoch.capture();
        {auto outer=epoch.mutation();QVERIFY(outer);{auto inner=epoch.mutation();QVERIFY(inner);epoch.close();QVERIFY(!inner);}QVERIFY(!outer);}
        QVERIFY(!old.current());QVERIFY(!epoch.capture().current());epoch.close();
    }
    void tokensDoNotKeepAdmissionsAliveAndOwnerDestructionCloses() {
        MetadataRegistryToken retained; std::weak_ptr<CoreMetadataAdmission> weak;
        { auto epoch=std::make_unique<MetadataRegistryEpoch>();retained=epoch->capture();auto gate=admission();weak=gate;
          QVERIFY(epoch->arm(retained,gate));gate.reset();QVERIFY(weak.expired());QVERIFY(retained.current()); }
        QVERIFY(!retained.current());
    }
    void nativeStateAndSidebandCanReleaseAfterApp() {
        QProcess child;child.start(QCoreApplication::applicationFilePath(),{QStringLiteral("--after-app")});
        QVERIFY(child.waitForStarted(3000));QVERIFY(child.waitForFinished(10000));QCOMPARE(child.exitStatus(),QProcess::NormalExit);QCOMPARE(child.exitCode(),0);
        QVERIFY(child.readAllStandardError().isEmpty());
    }
};
int main(int argc,char**argv) {
    if(argc==2&&!std::strcmp(argv[1],"--after-app")) {
        auto app=std::make_unique<QCoreApplication>(argc,argv);auto native=fixture();MetadataRegistryToken token;
        {MetadataRegistryEpoch epoch;token=epoch.capture();auto gate=admission();if(!gate||!epoch.arm(token,gate)||!prepare(native,gate))return 10;
         if(keeper_fixture_apply(native.get())!=VLC_SUCCESS)return 11;}
        if(token.current())return 12;
        app.reset();token={};native.reset();
        return CoreMetadataAdmission::processCount()==0?0:13;
    }
    QCoreApplication app(argc,argv);RegistryTests test;return QTest::qExec(&test,argc,argv);
}
#include "test_addon_metadata_registry_epoch.moc"
