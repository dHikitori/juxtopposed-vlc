// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_session_ui.hpp"
#include "addons/addon_session_host.hpp"
#include "addons/addon_manager.hpp"
#include "addons/addon_catalogue_view.hpp"
#include <QTest>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QCoreApplication>
#include <QThread>
#include <atomic>
#include <condition_variable>
#include <mutex>
using namespace vlc::addons;
namespace vlc::addons {
class AddonSessionUiTestAccess {
public:
    static bool reviewTimerActive(const AddonSessionUi& ui) { return ui.m_timer.isActive(); }
    static void expireReview(AddonSessionUi& ui) { ui.m_expires=0; }
};
}
namespace {
const QString controller=QStringLiteral("org.example.session/controller");
QVariantList catalogRows() { return {QVariantMap{{"id",controller},{"name",QStringLiteral("<b>Controller</b>")},{"enabled",true},{"configured",true},
    {"origins",QStringList{QStringLiteral("wss://relay.example")}},{"operations",QStringList{QStringLiteral("pause"),QStringLiteral("seek")}}}}; }
QVariantMap captured() { return {{"name",QStringLiteral("<b>Controller</b>")},{"relay",QStringLiteral("wss://relay.example")},{"room",QStringLiteral("<i>Room</i>")},
    {"operations",QStringList{QStringLiteral("pause"),QStringLiteral("seek")}},{"observationFields",QStringList{QStringLiteral("positionUs"),QStringLiteral("paused")}},
    {"continuesAfterViewClose",true},{"sharesMediaPaths",false},{"sharesMediaBytes",false}}; }
struct Fake {
    QObject owner,catalogOwner; AddonSessionUi* ui=nullptr; QVariantList controllers=catalogRows(),sessions;
    QVariantMap review=captured(); QStringList selected,canceled; unsigned prepared=0,accepted=0,resumed=0,ended=0;
    bool inlineReady=false,closeDuringPrepare=false,inlineRefusal=false;
    AddonSessionUi::Backend backend() {
        AddonSessionUi::Backend b; b.lifetime=&owner; b.catalog={&catalogOwner,[this] { return controllers; }};
        b.prepare=[this](const auto&,const auto&,const auto&,const auto&) {
            ++prepared; if(closeDuringPrepare) ui->viewClosed(); if(inlineReady) ui->approvalReady(QStringLiteral("ticket-1")); return QStringLiteral("ticket-1");
        };
        b.review=[this](const auto&) { return review; };
        b.accept=[this](const auto&,const auto& operations) {
            ++accepted; selected=operations;
            if(inlineRefusal) ui->sessionChanged(QStringLiteral("session-1"),QStringLiteral("ended"));
            else sessions={QVariantMap{{"id",QStringLiteral("session-1")},{"name",QStringLiteral("<b>Controller</b>")},{"room",QStringLiteral("<i>Room</i>")},{"starting",true},{"suspended",false},{"connected",false}}};
            return QStringLiteral("session-1");
        };
        b.cancel=[this](const auto& ticket) { canceled.append(ticket); };
        b.resume=[this](const auto&) { ++resumed; return true; };
        b.end=[this](const auto&) { ++ended; sessions.clear(); };
        b.sessions=[this] { return sessions; }; return b;
    }
};
struct DisplayFake : Fake {
    bool current=true,ready=true,loading=false,active=false;
    QString displayError;
    unsigned activations=0,releases=0,retries=0;
    AddonSessionUi::Backend attachedBackend() {
        auto b=backend(); b.catalog.attached=true;
        for(auto& value:controllers) { auto row=value.toMap(); row.insert("ready",true); value=row; }
        b.catalog.state=[this] { return QVariantMap{{"current",current},{"ready",ready},{"loading",loading},{"error",displayError}}; };
        b.catalog.setActive=[this](bool value) { if(active==value) return; active=value; if(value) ++activations; else ++releases; };
        b.catalog.retry=[this] { ++retries; }; return b;
    }
};
struct Actual {
    QObject owner; QTemporaryDir directory; StoreAuthority store{directory.path()}; AddonStreamLifetime lifetime;
    struct Native { std::mutex mutex; SessionPlayerSnapshot state; std::atomic_uint grants{0},revokes{0}; };
    std::shared_ptr<Native> native=std::make_shared<Native>(); qint64 now=1000000; unsigned transports=0;
    std::unique_ptr<AddonSessionHost> host;
    unsigned grants() const { return native->grants.load(); }
    unsigned revokes() const { return native->revokes.load(); }
    void mediaChanged() { std::lock_guard<std::mutex> lock(native->mutex); ++native->state.stamp.media; }
    Actual() {
        auto& state=native->state;
        state.closed=false; state.media=state.input=state.pausable=state.seekable=state.rateChange=true; state.stamp={{1,2},3,4,5,6};
        SessionPlayerBackend b; b.lifetime=&owner;
        b.snapshot=[held=native] { std::lock_guard<std::mutex> lock(held->mutex); return held->state; }; b.monotonicUs=[this] { return now; };
        b.grant=[held=native](const auto& stamp,auto lease,quint32,qint64) { std::lock_guard<std::mutex> lock(held->mutex); auto& state=held->state; if(stamp!=state.stamp) return SessionPlayerResult::Stale; ++held->grants; state.active=true; state.lease=lease; ++state.leaseEpoch; return SessionPlayerResult::Accepted; };
        b.renew=[](const auto&,auto,qint64) { return SessionPlayerResult::Accepted; }; b.resume=b.renew;
        b.revoke=[held=native](auto) { std::lock_guard<std::mutex> lock(held->mutex); held->state.active=false; ++held->revokes; return true; };
        b.cancellation=[held=native](auto lease) { return [held,lease] { std::lock_guard<std::mutex> lock(held->mutex); if(held->state.active && held->state.lease==lease) { held->state.active=false; ++held->revokes; } }; };
        b.apply=[](const auto&) { return SessionPlayerResult::Accepted; }; b.completions=[] { return QList<SessionPlayerCompletion>{}; };
        const auto token=store.capture(); AddonNativeAuthority value; value.package.enabled=true; value.package.manifest.id=QStringLiteral("org.example.session");
        value.package.manifest.name=QStringLiteral("<b>Controller</b>"); value.globalId=controller; value.accountReference=QStringLiteral("account");
        value.contribution.type=QStringLiteral("session.controller"); value.contribution.contract=QStringLiteral("session@1"); value.contribution.operations={QStringLiteral("run")};
        value.package.manifest.requiredPermissions={{"playback.observe",QJsonObject{{"fields",QJsonArray{"positionUs","paused"}}}},
            {"playback.control",QJsonObject{{"operations",QJsonArray{"pause","seek"}}}},{"session.background",QJsonObject{}},
            {"network.websocket",QJsonObject{{"origins",QJsonArray{"wss://relay.example"}}}}};
        value.package.grants=value.package.manifest.requiredPermissions; value.live=[token] { return token.current(); }; value.current=value.live;
        value.onInvalidated=[token](auto cancel) { return token.onInvalidated(std::move(cancel)); };
        host=std::make_unique<AddonSessionHost>(std::move(b),AddonSessionHost::AuthoritySource{&owner,[value](const auto&) { return [value] { return std::optional<AddonNativeAuthority>(value); }; }},lifetime.handle());
        host->setTransportFactoryForTesting([this](auto,auto) { ++transports; return std::shared_ptr<AddonSessionWebSocket>{}; });
    }
    ~Actual() { host->close(); lifetime.close(); lifetime.handle().waitQt(std::chrono::seconds(5)); }
};
struct DisplayWait {
    std::mutex mutex; std::condition_variable changed; bool released=false;
    std::atomic_bool entered{false};
    void wait() { std::unique_lock<std::mutex> lock(mutex); entered=true; changed.wait(lock,[&] { return released; }); }
    void release() { { std::lock_guard<std::mutex> lock(mutex); released=true; } changed.notify_all(); }
};
struct DisplayFixture {
    QTemporaryDir directory;
    StoreAuthority authority{directory.path()};
    AddonStreamLifetime lifetime;
    std::unique_ptr<AddonManager> manager;
    QPointer<AddonCatalogueView> view;
    DisplayCatalogueSnapshot snapshot;
    std::shared_ptr<DisplayWait> block;
    std::atomic_uint reads{0},keys{0},probes{0};
    std::atomic_bool onOwner{false};
    QThread* ownerThread=QThread::currentThread();
    DisplayFixture() {
        manager=std::make_unique<AddonManager>(directory.filePath(QStringLiteral("manager")));
        DisplayCataloguePackage row; auto& package=row.package;
        package.manifest.id=QStringLiteral("org.example.session"); package.manifest.name=QStringLiteral("<b>Controller</b>");
        package.manifest.version=QStringLiteral("1.0.0"); package.digest=QByteArray(64,'a'); package.generation=1;
        package.enabled=true; row.savedEnabled=true;
        package.grants={{"network.websocket",QJsonObject{{"origins",QJsonArray{"wss://relay.example"}}}},
            {"playback.control",QJsonObject{{"operations",QJsonArray{"pause","seek"}}}}};
        Contribution contribution; contribution.id=QStringLiteral("controller"); contribution.type=QStringLiteral("session.controller");
        contribution.contract=QStringLiteral("session@1"); contribution.operations={QStringLiteral("run")};
        contribution.document={{"id",contribution.id},{"type",contribution.type},{"contract",contribution.contract},
            {"operations",QJsonArray{QStringLiteral("run")}}}; package.manifest.contributions={contribution};
        row.schema={{"type","object"},{"required",QJsonArray{"key"}},
            {"properties",QJsonObject{{"key",QJsonObject{{"type","string"},{"writeOnly",true}}}}}};
        row.settings={{"_credentials",QJsonObject{{"key","00000000-0000-4000-8000-000000000002"}}}};
        snapshot.packages={row};
    }
    AddonCatalogueView* attach() {
        AddonCatalogueView::Backend backend;
        backend.capture=[this]()->std::optional<DisplayCatalogueSource> {
            DisplayCatalogueSource source; source.queuedAuthority=authority.capture();
            source.workerExecutable=QCoreApplication::applicationFilePath();
            source.credentials=[this](const auto&,const auto&) {
                ++keys; onOwner=onOwner || QThread::currentThread()==ownerThread; return QByteArray("temporary-test-key");
            };
            // This display fixture supplies a bounded platform observation. It
            // proves demand scheduling, not an actual TLS transport handshake.
            source.tlsProbe=[this]()->std::optional<bool> {
                ++probes; onOwner=onOwner || QThread::currentThread()==ownerThread; return true;
            };
            auto state=snapshot; state.queuedAuthority=state.authority=source.queuedAuthority; const auto waiting=block;
            source.reader=[this,state=std::move(state),waiting](const DisplayCatalogueRequest&,DisplayCatalogueSnapshot* out,QString*) {
                ++reads; onOwner=onOwner || QThread::currentThread()==ownerThread;
                if(waiting) waiting->wait(); *out=state; return true;
            }; return source;
        };
        backend.subscribe=[this](auto wake) { return authority.onChanged(std::move(wake)); };
        view=new AddonCatalogueView(std::move(backend),lifetime.handle(),manager.get());
        view->setObjectName(QStringLiteral("hakoneDisplayCatalogue")); return view;
    }
    ~DisplayFixture() {
        if(view) view->close(); lifetime.close(); lifetime.handle().waitQt(std::chrono::seconds(5)); manager.reset();
    }
};
}
class TestAddonSessionUi : public QObject {
    Q_OBJECT
private slots:
    void actualAttachedManagerHasNoInactiveSessionReadsOrPlatformProbe() {
        DisplayFixture display; display.snapshot.packages.clear(); display.attach(); QTRY_VERIFY(display.view->isCurrent());
        Actual actual; QVERIFY(actual.host->mount(display.manager->hostServices()));
        AddonSessionUi ui(actual.host.get(),display.manager.get());
        QCOMPARE(display.reads.load(),1u); QCOMPARE(display.keys.load(),0u); QCOMPARE(display.probes.load(),0u);
        for(int i=0;i<50;++i) {
            ui.refresh(); QVERIFY(QMetaObject::invokeMethod(display.manager.get(),"capabilitiesChanged",Qt::DirectConnection));
        }
        ui.setDisplayActive(true); QTest::qWait(40);
        QVERIFY(ui.displayCurrent()); QVERIFY(ui.controllers().isEmpty()); QCOMPARE(display.reads.load(),1u);
        QCOMPARE(display.keys.load(),0u); QCOMPARE(display.probes.load(),0u); QVERIFY(!display.onOwner.load());
    }
    void actualAttachedManagerKeepsUnknownRowsAndOwnsOnlyOneVisibleDemand() {
        DisplayFixture display;
        auto disabled=display.snapshot.packages[0]; disabled.package.manifest.id=QStringLiteral("org.example.disabled");
        disabled.package.enabled=false; display.snapshot.packages.append(disabled);
        display.attach(); QTRY_VERIFY(display.view->isCurrent()); Actual actual;
        QVERIFY(actual.host->mount(display.manager->hostServices()));
        AddonSessionUi ui(actual.host.get(),display.manager.get());
        QCOMPARE(ui.controllers().size(),2); QCOMPARE(display.keys.load(),0u); QCOMPARE(display.probes.load(),0u);
        QVERIFY(!ui.controllers()[0].toMap().value("ready").toBool());
        QCOMPARE(ui.controllers()[0].toMap().value("state").toString(),QStringLiteral("Settings not checked"));
        QCOMPARE(ui.controllers()[1].toMap().value("state").toString(),QStringLiteral("Disabled"));
        ui.setDisplayActive(true); QTRY_VERIFY(ui.controllers()[0].toMap().value("ready").toBool());
        QCOMPARE(display.keys.load(),1u); QCOMPARE(display.probes.load(),1u); QVERIFY(!display.onOwner.load());
        const auto reads=display.reads.load();
        for(int i=0;i<50;++i) { ui.refresh(); QMetaObject::invokeMethod(display.manager.get(),"capabilitiesChanged",Qt::DirectConnection); }
        QTest::qWait(40); QCOMPARE(display.reads.load(),reads); QCOMPARE(display.probes.load(),1u);
        QStringList other;
        for(int i=0;i<32;++i) { const auto ticket=display.view->acquireFeature(QStringLiteral("session.controller")); if(!ticket.isEmpty()) other.append(ticket); }
        QCOMPARE(other.size(),31); for(const auto& ticket:other) display.view->release(ticket);
        ui.viewClosed(); other.clear();
        for(int i=0;i<32;++i) { const auto ticket=display.view->acquireFeature(QStringLiteral("session.controller")); if(!ticket.isEmpty()) other.append(ticket); }
        QCOMPARE(other.size(),32); for(const auto& ticket:other) display.view->release(ticket);
        QCOMPARE(actual.grants(),0u); QCOMPARE(actual.transports,0u);
    }
    void actualAttachedManagerDoesNotProbeDisabledOrUnsupportedControllers() {
        DisplayFixture display; display.snapshot.packages[0].package.enabled=false;
        auto unsupported=display.snapshot.packages[0]; unsupported.package.manifest.id=QStringLiteral("org.example.unsupported");
        unsupported.package.enabled=true; unsupported.package.manifest.contributions[0].document.insert("contract",QStringLiteral("future@2"));
        display.snapshot.packages.append(unsupported); display.attach(); QTRY_VERIFY(display.view->isCurrent());
        Actual actual; QVERIFY(actual.host->mount(display.manager->hostServices()));
        AddonSessionUi ui(actual.host.get(),display.manager.get()); ui.setDisplayActive(true); QTest::qWait(40);
        QCOMPARE(ui.controllers().size(),1); QCOMPARE(display.reads.load(),1u); QCOMPARE(display.probes.load(),0u); QCOMPARE(display.keys.load(),0u);
    }
    void actualAttachedStaleRefreshKeepsEndOnTheNativeHost() {
        DisplayFixture display; display.attach(); QTRY_VERIFY(display.view->isCurrent()); Actual actual;
        QVERIFY(actual.host->mount(display.manager->hostServices()));
        AddonSessionUi ui(actual.host.get(),display.manager.get()); ui.setDisplayActive(true);
        QTRY_VERIFY(ui.controllers()[0].toMap().value("ready").toBool());
        QVERIFY(ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room")));
        QTRY_VERIFY(!ui.review().isEmpty()); QVERIFY(ui.accept()); QTRY_COMPARE(ui.sessions().size(),1);
        const auto id=ui.sessions()[0].toMap().value("id").toString();
        const auto blocked=std::make_shared<DisplayWait>(); display.block=blocked;
        const auto release=qScopeGuard([&] { blocked->release(); }); display.authority.invalidate();
        QTRY_VERIFY(blocked->entered.load()); ui.refresh(); QVERIFY(!ui.displayCurrent());
        QVERIFY(!ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room")));
        QVERIFY(ui.end(id)); QVERIFY(actual.host->sessions().isEmpty());
        blocked->release(); QTRY_VERIFY(display.view->isCurrent());
    }
    void actualAttachedViewDestructionNeverAdoptsASuccessorOrLegacyReader() {
        DisplayFixture display; display.attach(); QTRY_VERIFY(display.view->isCurrent()); Actual actual;
        QVERIFY(actual.host->mount(display.manager->hostServices()));
        AddonSessionUi ui(actual.host.get(),display.manager.get()); ui.setDisplayActive(true);
        QTRY_VERIFY(ui.controllers()[0].toMap().value("ready").toBool());
        delete display.view.data(); ui.refresh(); QVERIFY(ui.controllers().isEmpty()); QVERIFY(!ui.displayCurrent());
        QVERIFY(!ui.displayError().isEmpty()); const auto probes=display.probes.load();
        display.attach(); QTRY_VERIFY(display.view->isCurrent()); ui.retryDisplay(); ui.refresh();
        QVERIFY(ui.controllers().isEmpty()); QVERIFY(!ui.displayCurrent()); QCOMPARE(display.probes.load(),probes);
        QVERIFY(!ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room"))); QCOMPARE(actual.grants(),0u);
    }
    void attachedDisplayNeedsExplicitActivityAndReadyState() {
        DisplayFake fake; AddonSessionUi ui(fake.attachedBackend()); fake.ui=&ui;
        QCOMPARE(fake.activations,0u); QVERIFY(!ui.displayCurrent()); QCOMPARE(ui.controllers().size(),1);
        QVERIFY(!ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room")));
        QCOMPARE(fake.prepared,0u); ui.setDisplayActive(true); ui.setDisplayActive(true); QCOMPARE(fake.activations,1u);
        fake.ready=false; ui.refresh(); QVERIFY(ui.displayCurrent()); QVERIFY(!ui.controllers()[0].toMap().value("ready").toBool());
        QVERIFY(!ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room"))); QCOMPARE(fake.prepared,0u);
        fake.ready=true; ui.refresh(); QVERIFY(ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room")));
        ui.viewClosed(); QCOMPARE(fake.releases,1u); QVERIFY(!ui.displayCurrent()); QCOMPARE(fake.canceled.size(),1);
    }
    void attachedDisabledAndUnknownRowsStayVisibleWithoutAdmission() {
        DisplayFake fake; auto backend=fake.attachedBackend();
        auto disabled=fake.controllers[0].toMap(); disabled["id"]=QStringLiteral("org.example.disabled/controller");
        disabled["enabled"]=false; disabled["ready"]=false; disabled["state"]=QStringLiteral("Disabled");
        auto unknown=fake.controllers[0].toMap(); unknown["configured"]=false; unknown["ready"]=false;
        unknown["state"]=QStringLiteral("Settings not checked"); fake.controllers={disabled,unknown};
        AddonSessionUi ui(std::move(backend)); fake.ui=&ui; ui.setDisplayActive(true);
        QCOMPARE(ui.controllers().size(),2); QCOMPARE(ui.controllers()[0].toMap().value("state").toString(),QStringLiteral("Disabled"));
        QVERIFY(!ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room"))); QCOMPARE(fake.prepared,0u);
    }
    void attachedAuthorityIsCheckedAtRequestAndFinalAcceptance() {
        DisplayFake fake; fake.inlineReady=true; AddonSessionUi ui(fake.attachedBackend()); fake.ui=&ui; ui.setDisplayActive(true);
        fake.current=false;
        QVERIFY(!ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room"))); QCOMPARE(fake.prepared,0u);
        fake.current=true; QVERIFY(ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room")));
        // No UI signal is delivered. The action must read current authority.
        fake.ready=false; QVERIFY(!ui.accept()); QCOMPARE(fake.accepted,0u); QCOMPARE(fake.canceled.size(),1);
        QVERIFY(ui.review().isEmpty());
        fake.ready=true; QVERIFY(ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room")));
        auto disabled=fake.controllers[0].toMap(); disabled["ready"]=false; fake.controllers={disabled};
        QVERIFY(!ui.accept()); QCOMPARE(fake.accepted,0u); QCOMPARE(fake.canceled.size(),2);
    }
    void attachedStaleDisplayKeepsSessionEndAvailable() {
        DisplayFake fake; fake.sessions={QVariantMap{{"id",QStringLiteral("session-1")},{"starting",false},{"suspended",false},{"connected",true}}};
        AddonSessionUi ui(fake.attachedBackend()); fake.ui=&ui; ui.setDisplayActive(true);
        fake.current=false; fake.loading=true; fake.displayError=QStringLiteral("List changed"); ui.refresh();
        QVERIFY(!ui.displayCurrent()); QVERIFY(ui.displayLoading()); QCOMPARE(ui.sessions().size(),1);
        QVERIFY(ui.end(QStringLiteral("session-1"))); QCOMPARE(fake.ended,1u); QVERIFY(ui.sessions().isEmpty());
        ui.retryDisplay(); QCOMPARE(fake.retries,1u); ui.viewClosed(); ui.retryDisplay(); QCOMPARE(fake.retries,1u);
    }
    void closeFinishesDetachedReviewCancellationAfterDisplayCallbackDeletesUi() {
        DisplayFake fake; fake.inlineReady=true; auto backend=fake.attachedBackend();
        AddonSessionUi* ui=nullptr; bool armed=false;
        backend.catalog.setActive=[&](bool value) { fake.active=value; if(!value && armed) { armed=false; delete ui; ui=nullptr; } };
        ui=new AddonSessionUi(std::move(backend)); fake.ui=ui; ui->setDisplayActive(true);
        QVERIFY(ui->prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room")));
        armed=true; ui->close(); QVERIFY(!ui); QCOMPARE(fake.canceled,QStringList{QStringLiteral("ticket-1")});
    }
    void activityCallbackCanHideOrDeleteThePageFacade() {
        for(const bool destroy:{false,true}) {
            DisplayFake fake; auto backend=fake.attachedBackend(); AddonSessionUi* ui=nullptr;
            backend.catalog.setActive=[&](bool value) {
                if(!value) { ++fake.releases; return; }
                ++fake.activations;
                if(destroy) { delete ui; ui=nullptr; } else ui->viewClosed();
            };
            ui=new AddonSessionUi(std::move(backend)); fake.ui=ui; ui->setDisplayActive(true);
            QCOMPARE(fake.activations,1u); QCOMPARE(fake.releases,1u);
            if(destroy) QVERIFY(!ui);
            else { QVERIFY(!ui->displayCurrent()); delete ui; }
        }
    }
    void reviewCancellationCanReopenThePageWithoutRetiringItsNewDemand() {
        DisplayFake fake; fake.inlineReady=true; auto backend=fake.attachedBackend(); bool reopen=true;
        backend.cancel=[&](const auto& ticket) {
            fake.canceled.append(ticket);
            if(reopen) { reopen=false; fake.ui->setDisplayActive(true); }
        };
        AddonSessionUi ui(std::move(backend)); fake.ui=&ui; ui.setDisplayActive(true);
        QVERIFY(ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room")));
        ui.viewClosed(); QVERIFY(ui.displayCurrent()); QVERIFY(fake.active);
        QCOMPARE(fake.releases,0u); QCOMPARE(fake.canceled.size(),1);
        QVERIFY(ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room")));
        ui.viewClosed(); QVERIFY(!fake.active); QCOMPARE(fake.releases,1u);
    }
    void displayStateCallbackCannotAcceptAnOlderReviewAfterReplacement() {
        DisplayFake fake; fake.inlineReady=true; auto backend=fake.attachedBackend(); bool armed=false;
        backend.catalog.state=[&] {
            if(armed) {
                armed=false; fake.ui->viewClosed(); fake.ui->setDisplayActive(true);
                fake.review.insert("name",QStringLiteral("Replacement"));
                fake.ui->prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("New room"));
            }
            return QVariantMap{{"current",true},{"ready",true}};
        };
        AddonSessionUi ui(std::move(backend)); fake.ui=&ui; ui.setDisplayActive(true);
        QVERIFY(ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room")));
        armed=true; QVERIFY(!ui.accept()); QCOMPARE(fake.accepted,0u);
        QCOMPARE(ui.review().value("name").toString(),QStringLiteral("Replacement"));
    }
    void idleFacadeHasNoReviewTimer() {
        AddonSessionUi unavailable(AddonSessionUi::Backend{});
        QVERIFY(!AddonSessionUiTestAccess::reviewTimerActive(unavailable));
        Fake fake; AddonSessionUi ui(fake.backend()); fake.ui=&ui;
        QCOMPARE(ui.controllers().size(),1);
        QVERIFY(!AddonSessionUiTestAccess::reviewTimerActive(ui));
        ui.refresh(); QVERIFY(!AddonSessionUiTestAccess::reviewTimerActive(ui));
    }
    void reviewTimerRunsOnlyUntilCancelAcceptOrClose() {
        for(const bool inlineReady:{false,true}) {
            Fake fake; fake.inlineReady=inlineReady; AddonSessionUi ui(fake.backend()); fake.ui=&ui;
            for(int cycle=0;cycle<2;++cycle) {
                QVERIFY(ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room")));
                QVERIFY(AddonSessionUiTestAccess::reviewTimerActive(ui));
                ui.viewClosed(); QVERIFY(!AddonSessionUiTestAccess::reviewTimerActive(ui));
                QCOMPARE(fake.canceled.size(),cycle+1);
            }
            QVERIFY(ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room")));
            ui.approvalReady(QStringLiteral("ticket-1"));
            QVERIFY(AddonSessionUiTestAccess::reviewTimerActive(ui));
            QVERIFY(ui.accept()); QVERIFY(ui.starting());
            QVERIFY(!AddonSessionUiTestAccess::reviewTimerActive(ui));
            QVERIFY(ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room")));
            QVERIFY(AddonSessionUiTestAccess::reviewTimerActive(ui)); ui.close();
            QVERIFY(!AddonSessionUiTestAccess::reviewTimerActive(ui));
        }
    }
    void expiredOrChangedReviewStopsTheTimer() {
        for(const bool expired:{false,true}) {
            Fake fake; fake.inlineReady=true; AddonSessionUi ui(fake.backend()); fake.ui=&ui;
            QVERIFY(ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room")));
            QVERIFY(!ui.review().isEmpty()); QVERIFY(AddonSessionUiTestAccess::reviewTimerActive(ui));
            if(expired) AddonSessionUiTestAccess::expireReview(ui); else fake.review.clear();
            QTRY_VERIFY(!AddonSessionUiTestAccess::reviewTimerActive(ui));
            QVERIFY(ui.review().isEmpty()); QVERIFY(!ui.errorMessage().isEmpty());
            QCOMPARE(fake.canceled.size(),1);
        }
    }
    void refusedPreparationOrAcceptanceLeavesNoIdleTimer() {
        Fake fake; auto backend=fake.backend();
        backend.prepare=[](const auto&,const auto&,const auto&,const auto&) { return QString{}; };
        AddonSessionUi refused(std::move(backend)); fake.ui=&refused;
        QVERIFY(!refused.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room")));
        QVERIFY(!AddonSessionUiTestAccess::reviewTimerActive(refused));
        fake.inlineReady=true; backend=fake.backend();
        backend.accept=[](const auto&,const auto&) { return QString{}; };
        AddonSessionUi ui(std::move(backend)); fake.ui=&ui;
        QVERIFY(ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room")));
        QVERIFY(AddonSessionUiTestAccess::reviewTimerActive(ui)); QVERIFY(!ui.accept());
        QVERIFY(!AddonSessionUiTestAccess::reviewTimerActive(ui));
    }
    void reviewDoesNotApproveAndDefaultsToObservationOnly() {
        Fake fake; AddonSessionUi ui(fake.backend()); fake.ui=&ui;
        QCOMPARE(ui.controllers().size(),1); QVERIFY(ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("<i>Room</i>")));
        QCOMPARE(fake.prepared,1u); QCOMPARE(fake.accepted,0u); QVERIFY(ui.preparing()); QVERIFY(!ui.accept());
        ui.approvalReady(QStringLiteral("ticket-1")); QVERIFY(!ui.preparing()); QCOMPARE(ui.review(),captured());
        QVERIFY(ui.accept()); QCOMPARE(fake.accepted,1u); QVERIFY(fake.selected.isEmpty()); QVERIFY(ui.review().isEmpty()); QVERIFY(ui.starting());
        QVERIFY(!ui.accept()); QCOMPARE(fake.accepted,1u);
    }
    void choicesCannotExpandOrReplaceTheCapturedScope() {
        Fake fake; fake.inlineReady=true; AddonSessionUi ui(fake.backend()); fake.ui=&ui;
        QVERIFY(ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room")));
        QVERIFY(!ui.accept({QStringLiteral("setRate")})); QVERIFY(!ui.accept({QStringLiteral("pause"),QStringLiteral("pause")})); QCOMPARE(fake.accepted,0u);
        auto copy=ui.review(); copy.insert("operations",QStringList{QStringLiteral("setRate")}); QVERIFY(!ui.accept({QStringLiteral("setRate")}));
        QVERIFY(ui.accept({QStringLiteral("pause")})); QCOMPARE(fake.selected,QStringList{QStringLiteral("pause")});
    }
    void inputAndControllerBoundsFailBeforePrepare() {
        Fake fake; AddonSessionUi ui(fake.backend()); fake.ui=&ui;
        QVERIFY(!ui.prepare(QStringLiteral("foreign"),QStringLiteral("wss://relay.example/room"),QStringLiteral("Room")));
        QVERIFY(!ui.prepare(controller,QStringLiteral("wss://other.example/room"),QStringLiteral("Room")));
        QVERIFY(!ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room\nName")));
        QVERIFY(!ui.prepare(controller,QString(2049,QLatin1Char('x')),QStringLiteral("Room"))); QCOMPARE(fake.prepared,0u);
        auto rows=catalogRows(); auto disabled=rows[0].toMap(); disabled["enabled"]=false; fake.controllers={disabled}; ui.refresh(); QVERIFY(ui.controllers().isEmpty());
    }
    void changedOrExpiredCaptureCannotApprove() {
        Fake fake; fake.inlineReady=true; AddonSessionUi ui(fake.backend()); fake.ui=&ui;
        QVERIFY(ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room")));
        fake.review.insert("room",QStringLiteral("Other")); QVERIFY(!ui.accept()); QCOMPARE(fake.accepted,0u); QVERIFY(ui.review().isEmpty()); QVERIFY(!fake.canceled.isEmpty());
        fake.review=captured(); QVERIFY(ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room")));
        fake.review.clear(); ui.refresh(); QVERIFY(ui.review().isEmpty()); QVERIFY(!ui.accept()); QCOMPARE(fake.accepted,0u);
    }
    void closingTheViewCancelsOnlyUnapprovedWork() {
        Fake fake; fake.inlineReady=true; AddonSessionUi ui(fake.backend()); fake.ui=&ui;
        QVERIFY(ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room"))); ui.viewClosed(); QCOMPARE(fake.canceled.size(),1); QCOMPARE(fake.accepted,0u);
        QVERIFY(ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room"))); QVERIFY(ui.accept()); ui.viewClosed(); QCOMPARE(fake.ended,0u); QCOMPARE(fake.sessions.size(),1);
        fake.closeDuringPrepare=true; QVERIFY(!ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room"))); QVERIFY(ui.review().isEmpty()); QCOMPARE(fake.canceled.size(),2);
    }
    void resumeAndEndAreExplicitHostActions() {
        Fake fake; fake.sessions={QVariantMap{{"id",QStringLiteral("session-1")},{"starting",false},{"suspended",false},{"connected",true}}};
        AddonSessionUi ui(fake.backend()); fake.ui=&ui; QVERIFY(!ui.resume(QStringLiteral("session-1"))); QCOMPARE(fake.resumed,0u);
        fake.sessions[0]=QVariantMap{{"id",QStringLiteral("session-1")},{"starting",false},{"suspended",true},{"connected",true}}; ui.refresh();
        QVERIFY(ui.resume(QStringLiteral("session-1"))); QCOMPARE(fake.resumed,1u); QVERIFY(ui.end(QStringLiteral("session-1"))); QCOMPARE(fake.ended,1u);
        QVERIFY(!ui.end(QStringLiteral("foreign")));
    }
    void deletionAndInlineRefusalDoNotLeavePendingReview() {
        Fake fake; fake.inlineReady=true; auto* ui=new AddonSessionUi(fake.backend()); fake.ui=ui;
        QVERIFY(ui->prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room"))); delete ui; QCOMPARE(fake.canceled.size(),1);
        AddonSessionUi next(fake.backend()); fake.ui=&next; fake.inlineRefusal=true;
        QVERIFY(next.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room"))); QVERIFY(!next.accept()); QVERIFY(!next.starting()); QVERIFY(!next.errorMessage().isEmpty());
    }
    void backendCanDestroyTheFacadeDuringPrepare() {
        Fake fake; auto backend=fake.backend(); AddonSessionUi* ui=nullptr;
        backend.prepare=[&](const auto&,const auto&,const auto&,const auto&) { delete ui; ui=nullptr; return QStringLiteral("ticket-late"); };
        ui=new AddonSessionUi(std::move(backend)); fake.ui=ui;
        QVERIFY(!ui->prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room"))); QVERIFY(!ui); QCOMPARE(fake.canceled,QStringList{QStringLiteral("ticket-late")});
    }
    void catalogOwnerDeletionCancelsThePendingTicket() {
        Fake fake; fake.inlineReady=true; auto backend=fake.backend(); auto* catalogOwner=new QObject; backend.catalog.lifetime=catalogOwner;
        AddonSessionUi ui(std::move(backend)); fake.ui=&ui;
        QVERIFY(ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room"))); delete catalogOwner;
        QCOMPARE(fake.canceled.size(),1); QVERIFY(ui.review().isEmpty()); QVERIFY(ui.controllers().isEmpty()); QVERIFY(!ui.accept());
    }
    void destructorDoesNotPublishAnotherUiChange() {
        Fake fake; auto* ui=new AddonSessionUi(fake.backend()); unsigned changes=0;
        QObject::connect(ui,&AddonSessionUi::changed,&fake.owner,[&] { ++changes; }); delete ui; QCOMPARE(changes,0u);
    }
    void reviewCallbackCanDeleteTheFacadeAndFinishItsOwnedCapture() {
        Fake fake; auto backend=fake.backend(); AddonSessionUi* ui=nullptr; bool finished=false;
        backend.review=[&ui,&finished,payload=std::string(128,'x')](const auto&) {
            delete ui; ui=nullptr; finished=payload.size()==128; return captured();
        };
        ui=new AddonSessionUi(std::move(backend)); fake.ui=ui;
        QVERIFY(ui->prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room")));
        ui->approvalReady(QStringLiteral("ticket-1")); QVERIFY(!ui); QVERIFY(finished);
    }
    void reviewCallbackCannotApproveOrOverwriteAReplacement_data() {
        QTest::addColumn<bool>("duringAccept"); QTest::newRow("ready")<<false; QTest::newRow("accept")<<true;
    }
    void reviewCallbackCannotApproveOrOverwriteAReplacement() {
        QFETCH(bool,duringAccept); Fake fake; auto backend=fake.backend(); bool armed=!duringAccept,fired=false;
        backend.review=[&](const auto&) {
            const auto old=fake.review;
            if(armed && !fired) {
                fired=true; fake.ui->cancelReview(); fake.review.insert("name",QStringLiteral("Replacement")); fake.inlineReady=true;
                fake.ui->prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Replacement room"));
            }
            return old;
        };
        AddonSessionUi ui(std::move(backend)); fake.ui=&ui;
        QVERIFY(ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room"))); ui.approvalReady(QStringLiteral("ticket-1"));
        if(duringAccept) { armed=true; QVERIFY(!ui.accept()); }
        QVERIFY(fired); QCOMPARE(fake.accepted,0u); QCOMPARE(ui.review().value("name").toString(),QStringLiteral("Replacement"));
        QVERIFY(AddonSessionUiTestAccess::reviewTimerActive(ui));
    }
    void actualHostWaitsForAcceptAndChangedMediaRefusesPendingStart() {
        Actual actual; AddonSessionUi ui(actual.host.get(),{&actual.owner,[] { return catalogRows(); }});
        QVERIFY(ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("<i>Room</i>"))); QTRY_VERIFY(!ui.review().isEmpty());
        QCOMPARE(actual.grants(),0u); QCOMPARE(actual.transports,0u); QCOMPARE(ui.review().value("room").toString(),QStringLiteral("<i>Room</i>"));
        actual.mediaChanged(); QVERIFY(ui.accept({QStringLiteral("pause")})); QVERIFY(ui.starting());
        QTRY_VERIFY(!ui.starting()); QVERIFY(ui.sessions().isEmpty()); QVERIFY(!ui.errorMessage().isEmpty()); QCOMPARE(actual.grants(),0u); QCOMPARE(actual.transports,0u);
    }
    void actualHostObserveOnlySurvivesViewCloseAndEndRevokesControl() {
        Actual actual; AddonSessionUi ui(actual.host.get(),{&actual.owner,[] { return catalogRows(); }});
        QVERIFY(ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room"))); QTRY_VERIFY(!ui.review().isEmpty()); QVERIFY(ui.accept());
        QTRY_VERIFY(!ui.starting()); QCOMPARE(ui.sessions().size(),1); QCOMPARE(actual.grants(),0u); ui.viewClosed(); QCOMPARE(actual.host->sessions().size(),1);
        const auto id=ui.sessions()[0].toMap().value("id").toString(); QVERIFY(ui.end(id)); QVERIFY(actual.host->sessions().isEmpty());
        QVERIFY(ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room"))); QTRY_VERIFY(!ui.review().isEmpty()); QVERIFY(ui.accept({QStringLiteral("pause")}));
        QTRY_VERIFY(!ui.starting()); QCOMPARE(actual.grants(),1u); QVERIFY(ui.end(ui.sessions()[0].toMap().value("id").toString())); QCOMPARE(actual.revokes(),1u);
    }
    void actualHostExpiredTicketCannotGrantControl() {
        Actual actual; AddonSessionUi ui(actual.host.get(),{&actual.owner,[] { return catalogRows(); }});
        QVERIFY(ui.prepare(controller,QStringLiteral("wss://relay.example/room"),QStringLiteral("Room"))); QTRY_VERIFY(!ui.review().isEmpty()); actual.now+=30000001;
        QVERIFY(!ui.accept({QStringLiteral("pause")})); QCOMPARE(actual.grants(),0u); QCOMPARE(actual.transports,0u);
    }
};
QTEST_GUILESS_MAIN(TestAddonSessionUi)
#include "test_addon_session_ui.moc"
