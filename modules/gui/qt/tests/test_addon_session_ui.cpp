// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_session_ui.hpp"
#include "addons/addon_session_host.hpp"
#include <QTest>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QScopeGuard>
using namespace vlc::addons;
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
}
class TestAddonSessionUi : public QObject {
    Q_OBJECT
private slots:
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
