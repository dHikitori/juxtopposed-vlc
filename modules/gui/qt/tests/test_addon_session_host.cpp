// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_session_host.hpp"
#include "addons/addon_session_controller.hpp"
#include "session-runtime/session_test_relay.hpp"
#include <QFile>
#include <QJsonArray>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <condition_variable>

using namespace vlc::addons;
namespace vlc::addons {
// Exercise the handler's inline completion boundary without the queued broker.
class AddonSessionHostTestAccess {
public:
    static void tick(AddonSessionHost& host) { host.tick(); }
    static bool polling(const AddonSessionHost& host) { return host.m_timer.isActive(); }
    static AddonHostServices::Cancel receive(AddonSessionHost& host,const AddonHostServices::Context& context,const QString& id,AddonHostServices::Completion complete) {
        return host.handle(context,QStringLiteral("network.websocket.receive"),{{"session",id}},std::move(complete));
    }
};
}
namespace {
const QString globalId=QStringLiteral("org.example.session/controller");
qint64 steadyDeadline() { return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()+25000000; }
struct Flags { std::atomic_bool live{true},saved{true}; std::atomic_uint reads{0}; };
struct Player final : QObject {
    struct Native { std::mutex mutex; SessionPlayerSnapshot state; std::atomic_uint revokes{0},revokeCalls{0}; };
    std::shared_ptr<Native> native=std::make_shared<Native>();
    QList<SessionPlayerCommand> commands; QList<SessionPlayerCompletion> completed;
    qint64 now=10000000; unsigned grants=0,resumes=0,snapshots=0,completionReads=0,clockReads=0;
    std::function<void()> afterGrant;
    Player() { auto& s=native->state; s.closed=false; s.media=s.input=s.pausable=s.seekable=s.rateChange=true;
        s.stamp={{1,2},3,4,5,6}; s.positionUs=2000000; s.durationUs=100000000; }
    SessionPlayerSnapshot snapshot() { std::lock_guard<std::mutex> lock(native->mutex); return native->state; }
    void local() { std::lock_guard<std::mutex> lock(native->mutex); auto& s=native->state;
        ++s.stamp.revision; ++s.stamp.localRevision; s.suspended=true; s.origin=SessionPlayerOrigin::Local; }
    void mediaChanged() { std::lock_guard<std::mutex> lock(native->mutex); ++native->state.stamp.media; }
    void complete(int index,bool success=true) {
        const auto value=commands.at(index); auto s=snapshot();
        completed.append({value.expected,value.lease,value.sequence,s.leaseEpoch,value.operation,success});
    }
    SessionPlayerBackend backend() {
        SessionPlayerBackend value; value.lifetime=this; value.snapshot=[this] { ++snapshots; return snapshot(); };
        value.monotonicUs=[this] { ++clockReads; return now; };
        value.grant=[this](const auto& stamp,auto lease,quint32,qint64 duration) {
            {
                std::lock_guard<std::mutex> lock(native->mutex); auto& s=native->state;
                if(s.stamp!=stamp || s.active) return SessionPlayerResult::Stale;
                ++grants; s.active=true; s.suspended=false; s.lease=lease; ++s.leaseEpoch; s.leaseDeadlineUs=now+duration;
            }
            if(afterGrant) afterGrant();
            return SessionPlayerResult::Accepted;
        };
        value.renew=[this](const auto& stamp,auto lease,qint64 duration) {
            std::lock_guard<std::mutex> lock(native->mutex); auto& s=native->state;
            if(!s.active || s.suspended || s.stamp!=stamp || s.lease!=lease) return SessionPlayerResult::Stale;
            s.leaseDeadlineUs=now+duration; return SessionPlayerResult::Accepted;
        };
        value.resume=[this](const auto& stamp,auto lease,qint64) {
            std::lock_guard<std::mutex> lock(native->mutex); auto& s=native->state;
            if(!s.active || s.stamp!=stamp || s.lease!=lease) return SessionPlayerResult::Stale;
            ++resumes; s.suspended=false; ++s.leaseEpoch; return SessionPlayerResult::Accepted;
        };
        value.cancellation=[held=native](auto lease) { return [held,lease] {
            ++held->revokeCalls;
            std::lock_guard<std::mutex> lock(held->mutex);
            if(held->state.active && held->state.lease==lease) { held->state.active=false; ++held->revokes; }
        }; };
        value.revoke=[held=native](auto lease) {
            std::lock_guard<std::mutex> lock(held->mutex);
            if(!held->state.active || held->state.lease!=lease) return false;
            held->state.active=false; ++held->revokes; return true;
        };
        value.apply=[this](const auto& command) {
            const auto s=snapshot();
            if(!s.active || s.suspended || s.stamp!=command.expected || s.lease!=command.lease) return SessionPlayerResult::Stale;
            commands.append(command); return SessionPlayerResult::Accepted;
        };
        value.completions=[this] { ++completionReads; QList<SessionPlayerCompletion> result; result.swap(completed); return result; }; return value;
    }
};
AddonNativeAuthority authority(const std::shared_ptr<Flags>& flags,const StoreAuthorityToken& token) {
    AddonNativeAuthority value; value.package.enabled=true; value.package.manifest.id=QStringLiteral("org.example.session");
    value.package.manifest.name=QStringLiteral("Test session"); value.package.manifest.version=QStringLiteral("1.0.0");
    value.contribution.id=QStringLiteral("controller"); value.contribution.type=QStringLiteral("session.controller");
    value.contribution.contract=QStringLiteral("session@1"); value.contribution.operations={QStringLiteral("run")};
    value.globalId=globalId; value.accountReference=QStringLiteral("test-account");
    value.package.manifest.requiredPermissions={
        {"playback.observe",QJsonObject{{"fields",QJsonArray{"positionUs","durationUs","rate","paused","seekable","buffering"}}}},
        {"playback.control",QJsonObject{{"operations",QJsonArray{"play","pause","seek","setRate"}}}},
        {"session.background",QJsonObject{}},
        {"network.websocket",QJsonObject{{"origins",QJsonArray{"wss://relay.example"}}}}};
    value.package.grants=value.package.manifest.requiredPermissions;
    value.live=[flags,token] { return token.current() && flags->live.load(); };
    value.current=[flags,token] { ++flags->reads; return token.current() && flags->saved.load(); };
    value.onInvalidated=[token](std::function<void()> cancel) { return token.onInvalidated(std::move(cancel)); };
    return value;
}
struct Rig {
    QObject source; Player player; AddonStreamLifetime lifetime; AddonHostServices services;
    QTemporaryDir directory; StoreAuthority store{directory.path()};
    std::shared_ptr<Flags> flags=std::make_shared<Flags>(); AddonNativeAuthority approved=authority(flags,store.capture());
    std::unique_ptr<AddonSessionHost> host;
    bool managed=false; QString invocation=QStringLiteral("invoke-1");
    Rig() { host=std::make_unique<AddonSessionHost>(player.backend(),AddonSessionHost::AuthoritySource{&source,[this](const QString&) {
        const auto copy=approved; return [copy] { return std::optional<AddonNativeAuthority>(copy); };
    }},lifetime.handle()); }
    ~Rig() { if(host) host->close(); lifetime.close(); lifetime.handle().waitQt(std::chrono::seconds(5)); }
    AddonHostServices::Context context() const { return {approved.package.manifest.id,globalId,invocation,approved.package.grants,approved.live}; }
    QString ticket(const QStringList& operations={QStringLiteral("pause"),QStringLiteral("seek"),QStringLiteral("setRate")}) {
        return host->prepareApproval(globalId,QUrl(QStringLiteral("wss://relay.example/room")),QStringLiteral("Test room"),operations);
    }
    quint64 call(const QString& operation,const QJsonObject& request,AddonHostServices::Completion complete) {
        return services.dispatch(context(),operation,request,std::move(complete));
    }
};
}
class TestAddonSessionHost : public QObject {
    Q_OBJECT
    QTemporaryDir files; QSslCertificate certificate; QSslKey key;
    void transport(Rig& rig,session_test::Server& relay) {
        const auto testing=relay.fixture(certificate);
        QVERIFY(rig.host->setTransportFactoryForTesting([testing](auto spec,auto events) {
            return AddonSessionWebSocket::openForTesting(std::move(spec),testing,std::move(events));
        }));
        QVERIFY(rig.host->mount(&rig.services));
    }
    QString activate(Rig& rig,bool control=true) {
        const auto ticket=rig.ticket(); if(ticket.isEmpty()) return {};
        for(int i=0;i<100 && rig.host->approval(ticket).isEmpty();++i) QTest::qWait(10);
        if(rig.host->approval(ticket).isEmpty()) return {};
        const auto id=rig.host->approve(ticket,control ? QStringList{QStringLiteral("pause"),QStringLiteral("seek"),QStringLiteral("setRate")} : QStringList{});
        for(int i=0;i<100 && !rig.host->current(id);++i) QTest::qWait(10);
        if(rig.host->current(id) && !rig.managed && !rig.host->bindInvocation(id,rig.invocation,steadyDeadline())) return {};
        return rig.host->current(id) ? id : QString{};
    }
    bool connectSession(Rig& rig,const QString& id) {
        unsigned calls=0; bool connected=false;
        if(!rig.call(QStringLiteral("network.websocket.open"),{{"session",id}},[&](auto result) { ++calls; connected=result.error.isEmpty() && result.response.value("connected").toBool(); })) return false;
        for(int i=0;i<200 && calls==0;++i) QTest::qWait(10);
        return calls==1 && connected;
    }
    QJsonObject command(Rig& rig,const QString& id,quint64 sequence=1,const QString& operation=QStringLiteral("pause"),QJsonObject parameters={}) {
        QJsonObject result; bool called=false;
        rig.call(QStringLiteral("playback.observe"),{{"session",id}},[&](auto value) { called=true; result=value.response; });
        for(int i=0;i<100 && !called;++i) QTest::qWait(10);
        const QString commandId=QStringLiteral("command-")+QString::number(sequence);
        return {{"session",id},{"operation",operation},{"commandId",commandId},
            {"sequence",QString::number(sequence)},{"mediaEpoch",result.value("mediaEpoch")},{"expectedRevision",result.value("transportRevision")},{"parameters",parameters}};
    }
private slots:
    void initTestCase() {
        const auto cert=files.filePath(QStringLiteral("certificate.pem")),secret=files.filePath(QStringLiteral("private.pem"));
        QProcess openssl; openssl.start(QStringLiteral("openssl"),{QStringLiteral("req"),QStringLiteral("-x509"),QStringLiteral("-newkey"),QStringLiteral("rsa:2048"),QStringLiteral("-nodes"),
            QStringLiteral("-keyout"),secret,QStringLiteral("-out"),cert,QStringLiteral("-days"),QStringLiteral("2"),QStringLiteral("-subj"),QStringLiteral("/CN=relay.example"),
            QStringLiteral("-addext"),QStringLiteral("subjectAltName=DNS:relay.example")});
        QVERIFY(openssl.waitForFinished(10000)); QCOMPARE(openssl.exitCode(),0);
        QFile certFile(cert),keyFile(secret); QVERIFY(certFile.open(QIODevice::ReadOnly)); QVERIFY(keyFile.open(QIODevice::ReadOnly));
        certificate=QSslCertificate(certFile.readAll()); key=QSslKey(keyFile.readAll(),QSsl::Rsa); QVERIFY(!certificate.isNull()); QVERIFY(!key.isNull());
    }
    void cleanup() {
        QTRY_COMPARE_WITH_TIMEOUT(AddonSessionWebSocket::processCount(),0u,5000);
        QTRY_COMPARE_WITH_TIMEOUT(AddonStreamLifetime::processQtWorkCount(),0u,5000);
        QTRY_COMPARE_WITH_TIMEOUT(AddonStreamLifetime::processNetworkCount(),0u,5000);
    }
    void idleMountDoesNotReadThePlayer() {
        Rig rig; QVERIFY(rig.host->mount(&rig.services));
        const auto snapshots=rig.player.snapshots,completions=rig.player.completionReads,clocks=rig.player.clockReads;
        AddonSessionHostTestAccess::tick(*rig.host);
        QCOMPARE(rig.player.snapshots,snapshots); QCOMPARE(rig.player.completionReads,completions);
        QCOMPARE(rig.player.clockReads,clocks); QVERIFY(!AddonSessionHostTestAccess::polling(*rig.host));
        QTest::qWait(180);
        QCOMPARE(rig.player.snapshots,snapshots); QCOMPARE(rig.player.completionReads,completions);
        QCOMPARE(rig.player.clockReads,clocks);
    }
    void approvalExpiryUsesNoPlayerSnapshotOrCompletionRead() {
        Rig rig; const auto ticket=rig.ticket(); QVERIFY(!ticket.isEmpty());
        QTRY_VERIFY(!rig.host->approval(ticket).isEmpty());
        QVERIFY(AddonSessionHostTestAccess::polling(*rig.host));
        const auto snapshots=rig.player.snapshots,completions=rig.player.completionReads;
        const auto clocks=rig.player.clockReads;
        QTest::qWait(150); QCOMPARE(rig.player.clockReads,clocks);
        AddonSessionHostTestAccess::tick(*rig.host);
        QCOMPARE(rig.player.snapshots,snapshots); QCOMPARE(rig.player.completionReads,completions);
        rig.player.now+=30000001; AddonSessionHostTestAccess::tick(*rig.host);
        QVERIFY(rig.host->approval(ticket).isEmpty()); QVERIFY(!AddonSessionHostTestAccess::polling(*rig.host));
        QCOMPARE(rig.player.snapshots,snapshots); QCOMPARE(rig.player.completionReads,completions);
    }
    void lastApprovalRemovalStopsPolling_data() {
        QTest::addColumn<int>("action");
        QTest::newRow("cancel")<<0; QTest::newRow("withdraw")<<1; QTest::newRow("refused")<<2;
    }
    void lastApprovalRemovalStopsPolling() {
        QFETCH(int,action); Rig rig;
        if(action==2) rig.approved.package.enabled=false;
        QSignalSpy ready(rig.host.get(),&AddonSessionHost::approvalReady);
        const auto ticket=rig.ticket(); QVERIFY(!ticket.isEmpty());
        QTRY_COMPARE(ready.size(),1);
        if(action==0) rig.host->cancelApproval(ticket);
        else if(action==1) rig.host->revokePackage(rig.approved.package.manifest.id);
        QVERIFY(rig.host->approval(ticket).isEmpty()); QVERIFY(!AddonSessionHostTestAccess::polling(*rig.host));
        const auto snapshots=rig.player.snapshots,completions=rig.player.completionReads;
        QTest::qWait(120); QCOMPARE(rig.player.snapshots,snapshots); QCOMPARE(rig.player.completionReads,completions);
    }
    void activePollingStopsAndRestartsWithSessions() {
        Rig rig;
        for(unsigned cycle=0;cycle!=2;++cycle) {
            const auto id=activate(rig); QVERIFY(!id.isEmpty()); QVERIFY(AddonSessionHostTestAccess::polling(*rig.host));
            const auto snapshots=rig.player.snapshots,completions=rig.player.completionReads;
            AddonSessionHostTestAccess::tick(*rig.host);
            QVERIFY(rig.player.snapshots>snapshots); QVERIFY(rig.player.completionReads>completions);
            rig.host->end(id); QVERIFY(!AddonSessionHostTestAccess::polling(*rig.host));
            const auto idleSnapshots=rig.player.snapshots,idleCompletions=rig.player.completionReads;
            QTest::qWait(120); QCOMPARE(rig.player.snapshots,idleSnapshots); QCOMPARE(rig.player.completionReads,idleCompletions);
        }
    }
    void consentIsOneUseAndBindsTheCapturedNativeStamp() {
        Rig rig; const auto ticket=rig.ticket(); QVERIFY(!ticket.isEmpty()); QTRY_VERIFY(!rig.host->approval(ticket).isEmpty());
        const auto display=rig.host->approval(ticket); QCOMPARE(display.value("relay").toString(),QStringLiteral("wss://relay.example"));
        QVERIFY(display.value("observationFields").toStringList().contains(QStringLiteral("positionUs"))); QCOMPARE(rig.player.grants,0u);
        rig.player.local(); const auto id=rig.host->approve(ticket,{QStringLiteral("pause")}); QVERIFY(!id.isEmpty());
        QTRY_VERIFY(rig.host->sessions().isEmpty()); QCOMPARE(rig.player.grants,0u); QVERIFY(rig.host->approve(ticket,{}).isEmpty());
    }
    void registrationObserversCanDestroyEitherOwner_data() {
        QTest::addColumn<bool>("destroyHost");
        QTest::newRow("host")<<true; QTest::newRow("dispatcher")<<false;
    }
    void registrationObserversCanDestroyEitherOwner() {
        QFETCH(bool,destroyHost); Rig rig; auto* host=rig.host.release(); auto* dispatcher=new AddonHostServices;
        QPointer<AddonSessionHost> liveHost(host); QPointer<AddonHostServices> liveDispatcher(dispatcher); bool fired=false;
        const auto cleanup=qScopeGuard([&] { if(liveHost) delete host; if(liveDispatcher) delete dispatcher; });
        QObject::connect(dispatcher,&AddonHostServices::availabilityChanged,&rig.source,[&] {
            if(fired) return; fired=true; if(destroyHost) delete host; else delete dispatcher;
        });
        QVERIFY(!host->mount(dispatcher)); QVERIFY(fired);
        if(destroyHost) { QVERIFY(!liveHost); QVERIFY(liveDispatcher); QVERIFY(dispatcher->availableOperations().isEmpty()); }
        else { QVERIFY(liveHost); QVERIFY(!liveDispatcher); QVERIFY(host->mount(&rig.services)); }
    }
    void changedAccountPermissionAndExpiredTicketCannotApprove() {
        Rig rig; auto ticket=rig.ticket(); QTRY_VERIFY(!rig.host->approval(ticket).isEmpty()); rig.flags->live=false;
        QVERIFY(rig.host->approve(ticket,{}).isEmpty()); rig.flags->live=true; rig.host->cancelApproval(ticket);
        ticket=rig.ticket(); QTRY_VERIFY(!rig.host->approval(ticket).isEmpty()); rig.player.now+=30000001;
        QVERIFY(rig.host->approve(ticket,{}).isEmpty());
        rig.approved.package.grants.remove(QStringLiteral("session.background")); ticket=rig.ticket();
        QSignalSpy ready(rig.host.get(),&AddonSessionHost::approvalReady); QTRY_COMPARE(ready.size(),1); QVERIFY(rig.host->approval(ticket).isEmpty());
    }
    void observationOnlyUsesNoControlLeaseAndProjectsOnlyApprovedFields() {
        Rig rig; QVERIFY(rig.host->mount(&rig.services)); const auto id=activate(rig,false); QVERIFY(!id.isEmpty()); QCOMPARE(rig.player.grants,0u);
        unsigned count=0; QJsonObject observed;
        QVERIFY(rig.call(QStringLiteral("playback.observe"),{{"session",id}},[&](auto result) { ++count; QVERIFY(result.error.isEmpty()); observed=result.response; }));
        QTRY_COMPARE(count,1u); QCOMPARE(observed.value("positionUs").toInteger(),qint64(2000000)); QVERIFY(observed.value("buffering").isNull());
        QVERIFY(!observed.contains("path")); QVERIFY(!observed.contains("account")); QVERIFY(!observed.contains("seekablePath"));
        QVERIFY(!observed.value("mediaEpoch").toString().isEmpty()); rig.host->end(id); QCOMPARE(rig.player.native->revokes.load(),0u);
    }
    void savedAuthorityFailureEndsBeforeGuestObservation() {
        Rig rig; QVERIFY(rig.host->mount(&rig.services)); const auto id=activate(rig); QVERIFY(!id.isEmpty()); rig.flags->saved=false;
        unsigned count=0; rig.call(QStringLiteral("playback.observe"),{{"session",id}},[&](auto result) { ++count; QVERIFY(!result.error.isEmpty()); });
        QTRY_COMPARE(count,1u); QTRY_VERIFY(rig.host->sessions().isEmpty()); QCOMPARE(rig.player.native->revokes.load(),1u);
    }
    void nativeAdmissionNeedsMatchingExecutionCompletion() {
        Rig rig; session_test::Server relay(certificate,key); transport(rig,relay); const auto id=activate(rig); QVERIFY(!id.isEmpty()); QVERIFY(connectSession(rig,id));
        unsigned replies=0; QJsonObject response; const auto request=command(rig,id);
        rig.call(QStringLiteral("playback.command"),request,[&](auto result) { ++replies; QVERIFY(result.error.isEmpty()); response=result.response; });
        QTRY_COMPARE(rig.player.commands.size(),1); QTest::qWait(80); QCOMPARE(replies,0u);
        auto wrong=rig.player.commands[0]; ++wrong.sequence;
        rig.player.completed.append({wrong.expected,wrong.lease,wrong.sequence,rig.player.snapshot().leaseEpoch,wrong.operation,true});
        QTest::qWait(80); QCOMPARE(replies,0u); rig.player.complete(0); QTRY_COMPARE(replies,1u);
        QCOMPARE(response.value("status").toString(),QStringLiteral("applied"));
        rig.call(QStringLiteral("playback.command"),request,[&](auto result) { ++replies; QVERIFY(!result.error.isEmpty()); }); QTRY_COMPARE(replies,2u);
    }
    void localInputSuspendsAndGuestRenewCannotResume() {
        Rig rig; session_test::Server relay(certificate,key); transport(rig,relay); const auto id=activate(rig); QVERIFY(!id.isEmpty()); QVERIFY(connectSession(rig,id));
        rig.player.local(); QTRY_VERIFY(rig.host->sessions().first().toMap().value("suspended").toBool());
        unsigned count=0; rig.call(QStringLiteral("session.renew"),{{"session",id}},[&](auto result) { ++count; QVERIFY(!result.error.isEmpty()); });
        QTRY_COMPARE(count,1u); QCOMPARE(rig.player.resumes,0u); QVERIFY(rig.host->resume(id)); QTRY_COMPARE(rig.player.resumes,1u);
        QTRY_VERIFY(!rig.host->sessions().first().toMap().value("suspended").toBool());
    }
    void cancellationAfterAdmissionRevokesTheExactLease() {
        Rig rig; session_test::Server relay(certificate,key); transport(rig,relay); const auto id=activate(rig); QVERIFY(!id.isEmpty()); QVERIFY(connectSession(rig,id));
        unsigned count=0; const auto job=rig.call(QStringLiteral("playback.command"),command(rig,id),[&](auto) { ++count; });
        QTRY_COMPARE(rig.player.commands.size(),1); rig.services.cancel(job); QCOMPARE(count,1u); QCOMPARE(rig.player.native->revokes.load(),1u);
        rig.player.complete(0); QTest::qWait(80); QCOMPARE(count,1u); QTRY_VERIFY(rig.host->sessions().isEmpty());
    }
    void receiveIsSingleWaiterAndCountsCompleteSerializedJson() {
        Rig rig; session_test::Server relay(certificate,key,QByteArray::fromHex("81026869")); transport(rig,relay);
        const auto id=activate(rig); QVERIFY(!id.isEmpty()); QVERIFY(connectSession(rig,id));
        unsigned count=0; rig.call(QStringLiteral("network.websocket.receive"),{{"session",id}},[&](auto result) {
            ++count; QVERIFY(result.error.isEmpty()); const auto messages=result.response.value("messages").toArray(); QCOMPARE(messages.size(),1); QCOMPARE(messages[0].toObject().value("data").toString(),QStringLiteral("hi"));
        }); QTRY_COMPARE(count,1u);
        const auto pending=rig.call(QStringLiteral("network.websocket.receive"),{{"session",id}},[&](auto) { ++count; }); QVERIFY(pending);
        rig.call(QStringLiteral("network.websocket.receive"),{{"session",id}},[&](auto result) { ++count; QVERIFY(!result.error.isEmpty()); });
        QTRY_COMPARE(count,2u); rig.services.cancel(pending); QCOMPARE(count,3u); QVERIFY(rig.host->current(id));
    }
    void receivedMessageMustFitTheGuestEnvelope_data() {
        QTest::addColumn<bool>("binary");
        QTest::newRow("json-escapes-count")<<false;
        QTest::newRow("binary-base64-limit")<<true;
    }
    void bufferedReceiveCompletionCanDestroyTheHost() {
        Rig rig; session_test::Server relay(certificate,key,QByteArray::fromHex("81026869")); transport(rig,relay);
        const auto id=activate(rig); QVERIFY(!id.isEmpty()); QVERIFY(connectSession(rig,id));
        auto* host=rig.host.release(); QPointer<AddonSessionHost> alive(host); unsigned completions=0;
        const auto cleanup=qScopeGuard([&] { if(alive) delete host; });
        for(int i=0;i<100 && completions==0;++i) {
            const auto cancel=AddonSessionHostTestAccess::receive(*host,rig.context(),id,[&](auto result) {
                ++completions; QVERIFY(result.error.isEmpty()); QCOMPARE(result.response.value("messages").toArray().size(),1); delete host;
            });
            if(completions) QVERIFY(!cancel); else { if(cancel) cancel(); QTest::qWait(10); }
        }
        QCOMPARE(completions,1u); QVERIFY(!alive); QCOMPARE(rig.player.native->revokes.load(),1u);
    }
    void receivedMessageMustFitTheGuestEnvelope() {
        QFETCH(bool,binary); Rig rig; QByteArray frame;
        if(binary) { frame=QByteArray::fromHex("827ec001"); frame+=QByteArray(49153,'x'); }
        else { frame=QByteArray::fromHex("817f0000000000010000"); frame+=QByteArray(65536,'\x01'); }
        session_test::Server relay(certificate,key,frame); transport(rig,relay); const auto id=activate(rig); QVERIFY(!id.isEmpty());
        unsigned count=0; rig.call(QStringLiteral("network.websocket.open"),{{"session",id}},[&](auto) { ++count; });
        QTRY_VERIFY(rig.host->sessions().isEmpty()); QTRY_COMPARE(count,1u); QCOMPARE(rig.player.native->revokes.load(),1u);
    }
    void closeRevokesBeforeABlockedSavedCheckReturns() {
        Rig rig; const auto blocked=std::make_shared<std::atomic_bool>(false),entered=std::make_shared<std::atomic_bool>(false);
        const auto released=std::make_shared<std::atomic_bool>(false);
        rig.approved.current=[blocked,entered,released] {
            if(blocked->load()) { *entered=true; while(!released->load()) std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
            return true;
        };
        const auto release=qScopeGuard([&] { *released=true; }); QVERIFY(rig.host->mount(&rig.services)); const auto id=activate(rig); QVERIFY(!id.isEmpty());
        *blocked=true; unsigned count=0; rig.call(QStringLiteral("playback.observe"),{{"session",id}},[&](auto result) { ++count; QVERIFY(!result.error.isEmpty()); });
        QTRY_VERIFY(entered->load()); rig.host->close(); QCOMPARE(rig.player.native->revokes.load(),1u); QTRY_COMPARE(count,1u);
        rig.host.reset(); *released=true; QVERIFY(rig.lifetime.handle().waitQt(std::chrono::seconds(5))); QTest::qWait(20); QCOMPARE(count,1u);
    }
    void endSignalCanDestroyTheHost() {
        Rig rig; const auto id=activate(rig); QVERIFY(!id.isEmpty()); auto* host=rig.host.release(); QPointer<AddonSessionHost> alive(host);
        QObject::connect(host,&AddonSessionHost::sessionChanged,&rig.source,[host](const QString&,const QString& status) { if(status==QLatin1String("ended")) delete host; });
        host->close(); QVERIFY(!alive); QCOMPARE(rig.player.native->revokes.load(),1u);
    }
    void foreignContributionAndChangedGrantsCannotUseSession() {
        Rig rig; QVERIFY(rig.host->mount(&rig.services)); const auto id=activate(rig); QVERIFY(!id.isEmpty());
        unsigned count=0; auto context=rig.context(); context.contribution=QStringLiteral("org.example.session/other");
        rig.services.dispatch(context,QStringLiteral("playback.observe"),{{"session",id}},[&](auto result) { ++count; QVERIFY(!result.error.isEmpty()); }); QTRY_COMPARE(count,1u);
        context=rig.context(); context.grants.insert(QStringLiteral("playback.observe"),QJsonObject{});
        rig.services.dispatch(context,QStringLiteral("playback.observe"),{{"session",id}},[&](auto result) { ++count; QVERIFY(!result.error.isEmpty()); }); QTRY_COMPARE(count,2u);
    }
    void nativeMediaChangeAndHostCloseReleaseAllSessions() {
        Rig rig; const auto id=activate(rig); QVERIFY(!id.isEmpty()); rig.player.mediaChanged(); QTRY_VERIFY(rig.host->sessions().isEmpty());
        QCOMPARE(rig.player.native->revokes.load(),1u); const auto next=activate(rig); QVERIFY(!next.isEmpty());
        rig.host->close(); QVERIFY(rig.host->sessions().isEmpty()); QCOMPARE(rig.player.native->revokes.load(),2u); QVERIFY(rig.ticket().isEmpty());
    }
    void missingOrRefusedNativeWatchCannotExposeAnApprovedSession() {
        Rig rig; rig.approved.onInvalidated={}; const auto ticket=rig.ticket(); QVERIFY(!ticket.isEmpty());
        QSignalSpy ready(rig.host.get(),&AddonSessionHost::approvalReady); QTRY_COMPARE(ready.size(),1);
        QVERIFY(rig.host->approval(ticket).isEmpty()); QCOMPARE(rig.player.grants,0u);
        rig.approved=authority(rig.flags,rig.store.capture()); const auto registerWatch=rig.approved.onInvalidated;
        rig.approved.onInvalidated=[&rig,registerWatch](auto cancel) { auto watch=registerWatch(std::move(cancel)); rig.store.invalidate(); return watch; };
        const auto next=rig.ticket(); QTRY_VERIFY(!rig.host->approval(next).isEmpty());
        const auto id=rig.host->approve(next,{QStringLiteral("pause")}); QVERIFY(!id.isEmpty());
        QTRY_VERIFY(rig.host->sessions().isEmpty()); QCOMPARE(rig.player.grants,0u);
    }
    void invalidationAfterNativeGrantRevokesBeforeGuestExposure() {
        Rig rig; rig.player.afterGrant=[&rig] { rig.store.invalidate(); };
        QSignalSpy status(rig.host.get(),&AddonSessionHost::sessionChanged);
        const auto ticket=rig.ticket(); QTRY_VERIFY(!rig.host->approval(ticket).isEmpty());
        const auto id=rig.host->approve(ticket,{QStringLiteral("pause")}); QVERIFY(!id.isEmpty());
        QTRY_VERIFY(rig.host->sessions().isEmpty()); QCOMPARE(rig.player.grants,1u); QCOMPARE(rig.player.native->revokes.load(),1u);
        for(const auto& value:status) QVERIFY(value.at(1).toString()!=QLatin1String("active"));
    }
    void concurrentSwapOnceCancelCannotLetAWriterPassTheNativeRevoke() {
        Rig rig; const auto blocked=std::make_shared<std::atomic_bool>(false),entered=std::make_shared<std::atomic_bool>(false);
        const auto released=std::make_shared<std::atomic_bool>(false);
        rig.approved.current=[blocked,entered,released] {
            if(!blocked->load()) return true;
            *entered=true; while(!released->load()) std::this_thread::sleep_for(std::chrono::milliseconds(1)); return false;
        };
        const auto release=qScopeGuard([&] { *released=true; }); QVERIFY(rig.host->mount(&rig.services));
        const auto id=activate(rig); QVERIFY(!id.isEmpty()); *blocked=true;
        unsigned replies=0; rig.call(QStringLiteral("playback.observe"),{{"session",id}},[&](auto result) { ++replies; QVERIFY(!result.error.isEmpty()); });
        QTRY_VERIFY(entered->load()); std::unique_lock<std::mutex> nativeLock(rig.player.native->mutex);
        *released=true;
        auto wait=[](auto ready) {
            const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);
            while(!ready() && std::chrono::steady_clock::now()<until) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return ready();
        };
        QVERIFY(wait([&] { return rig.player.native->revokeCalls.load()>=1; }));
        const auto before=rig.store.capture(); const auto path=rig.directory.filePath(QStringLiteral("settings.json"));
        std::atomic_bool published{false}; std::thread writer([&] {
            const auto mutation=rig.store.mutation(); QFile file(path);
            published=file.open(QIODevice::WriteOnly) && file.write("{\"enabled\":false}")==17;
        });
        const auto join=qScopeGuard([&] { if(nativeLock.owns_lock()) nativeLock.unlock(); writer.join(); });
        QVERIFY(wait([&] { return !before.current(); }));
        QVERIFY(wait([&] { return rig.player.native->revokeCalls.load()>=2; }));
        QVERIFY(!published.load()); QVERIFY(!QFile::exists(path)); QCOMPARE(rig.player.native->revokes.load(),0u);
        nativeLock.unlock(); QTRY_VERIFY(published.load()); QCOMPARE(rig.player.native->revokes.load(),1u);
        QTRY_VERIFY(rig.host->sessions().isEmpty()); QTRY_COMPARE(replies,1u);
    }
    void endingTheSessionReleasesItsNativeWatch() {
        Rig rig; const auto id=activate(rig); QVERIFY(!id.isEmpty()); rig.host->end(id);
        const auto calls=rig.player.native->revokeCalls.load(); rig.store.invalidate();
        QCOMPARE(rig.player.native->revokeCalls.load(),calls); QCOMPARE(rig.player.native->revokes.load(),1u);
    }
    void onlyTheBoundInvocationCanUseTheSessionAndBindingsHaveOneUse() {
        Rig rig; rig.managed=true; QVERIFY(rig.host->mount(&rig.services)); const auto id=activate(rig); QVERIFY(!id.isEmpty());
        unsigned replies=0; rig.call(QStringLiteral("playback.observe"),{{"session",id}},[&](auto result) { ++replies; QVERIFY(!result.error.isEmpty()); }); QTRY_COMPARE(replies,1u);
        QVERIFY(rig.host->bindInvocation(id,rig.invocation,steadyDeadline()));
        QVERIFY(!rig.host->bindInvocation(id,QStringLiteral("other"),steadyDeadline()));
        auto foreign=rig.context(); foreign.invocation=QStringLiteral("other");
        rig.services.dispatch(foreign,QStringLiteral("playback.observe"),{{"session",id}},[&](auto result) { ++replies; QVERIFY(!result.error.isEmpty()); }); QTRY_COMPARE(replies,2u);
        QVERIFY(rig.host->releaseInvocation(id,rig.invocation));
        QVERIFY(!rig.host->bindInvocation(id,rig.invocation,steadyDeadline()));
        rig.invocation=QStringLiteral("invoke-2"); QVERIFY(rig.host->bindInvocation(id,rig.invocation,steadyDeadline()));
        rig.player.local(); QTRY_VERIFY(rig.host->sessions().first().toMap().value("suspended").toBool());
        rig.call(QStringLiteral("playback.observe"),{{"session",id}},[&](auto result) { ++replies; QVERIFY(result.error.isEmpty()); QVERIFY(result.response.value("suspended").toBool()); }); QTRY_COMPARE(replies,3u);
        QCOMPARE(rig.player.grants,1u); QCOMPARE(rig.player.resumes,0u);
    }
    void aSavedCheckCannotPublishAfterItsInvocationDeadline() {
        Rig rig; const auto block=std::make_shared<std::atomic_bool>(false),entered=std::make_shared<std::atomic_bool>(false),released=std::make_shared<std::atomic_bool>(false);
        rig.approved.current=[block,entered,released] { if(block->load()) { *entered=true; while(!released->load()) std::this_thread::sleep_for(std::chrono::milliseconds(1)); } return true; };
        const auto release=qScopeGuard([&] { *released=true; }); QVERIFY(rig.host->mount(&rig.services)); const auto id=activate(rig); QVERIFY(!id.isEmpty());
        QVERIFY(rig.host->releaseInvocation(id,rig.invocation)); rig.invocation=QStringLiteral("short-slice");
        QVERIFY(rig.host->bindInvocation(id,rig.invocation,steadyDeadline()-24800000)); *block=true;
        unsigned replies=0; rig.call(QStringLiteral("playback.observe"),{{"session",id}},[&](auto result) { ++replies; QVERIFY(!result.error.isEmpty()); QVERIFY(result.response.isEmpty()); });
        QTRY_VERIFY(entered->load()); QTest::qWait(220); *released=true; QTRY_COMPARE(replies,1u);
        QVERIFY(!rig.host->bindInvocation(id,QStringLiteral("replacement"),steadyDeadline()));
        QVERIFY(rig.host->releaseInvocation(id,rig.invocation));
    }
    void aPendingNativeCommandEndsTheSessionAtSliceRelease() {
        Rig rig; session_test::Server relay(certificate,key); transport(rig,relay); const auto id=activate(rig); QVERIFY(!id.isEmpty()); QVERIFY(connectSession(rig,id));
        unsigned replies=0; rig.call(QStringLiteral("playback.command"),command(rig,id),[&](auto result) { ++replies; QVERIFY(!result.error.isEmpty()); });
        QTRY_COMPARE(rig.player.commands.size(),1); QVERIFY(!rig.host->releaseInvocation(id,rig.invocation));
        QVERIFY(rig.host->sessions().isEmpty()); QCOMPARE(rig.player.native->revokes.load(),1u); QTRY_COMPARE(replies,1u);
        QVERIFY(!rig.host->bindInvocation(id,QStringLiteral("next"),steadyDeadline()));
        rig.player.complete(0); QTest::qWait(80); QCOMPARE(replies,1u);
    }
    void oldReceiveCancellationCannotRemoveTheNextSliceWaiter() {
        Rig rig; session_test::Server relay(certificate,key); transport(rig,relay); const auto id=activate(rig); QVERIFY(!id.isEmpty()); QVERIFY(connectSession(rig,id));
        unsigned replies=0; const auto old=rig.call(QStringLiteral("network.websocket.receive"),{{"session",id}},[&](auto result) { ++replies; QVERIFY(!result.error.isEmpty()); }); QVERIFY(old);
        QVERIFY(rig.host->releaseInvocation(id,rig.invocation)); rig.invocation=QStringLiteral("invoke-2");
        QVERIFY(rig.host->bindInvocation(id,rig.invocation,steadyDeadline()));
        const auto next=rig.call(QStringLiteral("network.websocket.receive"),{{"session",id}},[&](auto result) { ++replies; QVERIFY(!result.error.isEmpty()); }); QVERIFY(next);
        rig.services.cancel(old); QTRY_COMPARE(replies,1u);
        unsigned duplicate=0; rig.call(QStringLiteral("network.websocket.receive"),{{"session",id}},[&](auto result) { ++duplicate; QVERIFY(!result.error.isEmpty()); });
        QTRY_COMPARE(duplicate,1u); rig.services.cancel(next); QCOMPARE(replies,2u); QVERIFY(rig.host->current(id));
    }
    void controllerRunsOnlyAfterApprovalAndWaitsBetweenSlices() {
        Rig rig; rig.managed=true; QObject worker; unsigned invokes=0,cancels=0; QString request,session; bool exact=true;
        AddonSessionController controller(rig.host.get(),{&worker,[&](const QString& contribution,const QString& operation,const QVariantMap& context) {
            exact &= contribution==globalId && operation==QStringLiteral("run") && context.value("sliceMs").toInt()==25000;
            session=context.value("session").toString(); request=QStringLiteral("slice-")+QString::number(++invokes); return request;
        },[&](const QString&) { ++cancels; }});
        QTest::qWait(30); QCOMPARE(invokes,0u); const auto id=activate(rig); QVERIFY(!id.isEmpty()); QTRY_COMPARE(invokes,1u); QCOMPARE(session,id); QVERIFY(exact);
        controller.completed(request,globalId,QStringLiteral("partial"),{{"continue",true}},{}); QTest::qWait(200); QCOMPARE(invokes,1u);
        QTRY_COMPARE_WITH_TIMEOUT(invokes,2u,1500); controller.completed(request,globalId,QStringLiteral("match"),{{"continue",false}},{});
        QTRY_VERIFY(rig.host->sessions().isEmpty()); QCOMPARE(cancels,0u); QCOMPARE(rig.player.native->revokes.load(),1u);
    }
    void controllerHandlesInlineCompletionAndCrashBetweenSlices() {
        Rig rig; rig.managed=true; auto* worker=new QObject; unsigned invokes=0,cancels=0; AddonSessionController* controller=nullptr;
        controller=new AddonSessionController(rig.host.get(),{worker,[&](const QString&,const QString&,const QVariantMap&) {
            ++invokes; controller->completed(QStringLiteral("inline"),globalId,QStringLiteral("match"),{{"continue",true}},{}); return QStringLiteral("inline");
        },[&](const QString&) { ++cancels; }});
        const auto cleanup=qScopeGuard([&] { delete controller; delete worker; }); const auto id=activate(rig); QVERIFY(!id.isEmpty()); QTRY_COMPARE(invokes,1u);
        delete worker; worker=nullptr; QTRY_VERIFY(rig.host->sessions().isEmpty()); QTest::qWait(600); QCOMPARE(invokes,1u); QCOMPARE(cancels,0u); QCOMPARE(rig.player.native->revokes.load(),1u);
    }
    void controllerRejectsMalformedResultsAndCancelsPendingOnRetirement() {
        Rig rig; rig.managed=true; QObject worker; unsigned invokes=0,cancels=0; QString request;
        AddonSessionController controller(rig.host.get(),{&worker,[&](const QString&,const QString&,const QVariantMap&) { request=QStringLiteral("slice-")+QString::number(++invokes); return request; },[&](const QString&) { ++cancels; }});
        auto id=activate(rig); QVERIFY(!id.isEmpty()); QTRY_COMPARE(invokes,1u); controller.completed(request,globalId,QStringLiteral("match"),{{"continue",1}},{});
        QTRY_VERIFY(rig.host->sessions().isEmpty()); id=activate(rig); QVERIFY(!id.isEmpty()); QTRY_COMPARE(invokes,2u);
        controller.close(); QVERIFY(rig.host->sessions().isEmpty()); QCOMPARE(cancels,1u); QCOMPARE(rig.player.native->revokes.load(),2u);
        controller.completed(request,globalId,QStringLiteral("match"),{{"continue",true}},{}); QTest::qWait(550); QCOMPARE(invokes,2u);
    }
    void invokeCanDestroyControllerBeforeItReturnsANewRequest() {
        Rig rig; rig.managed=true; QObject worker; unsigned cancels=0; AddonSessionController* controller=nullptr;
        controller=new AddonSessionController(rig.host.get(),{&worker,[&](const QString&,const QString&,const QVariantMap&) {
            delete controller; controller=nullptr; return QStringLiteral("created-before-delete");
        },[&](const QString& request) { if(request==QLatin1String("created-before-delete")) ++cancels; }});
        const auto cleanup=qScopeGuard([&] { delete controller; }); const auto ticket=rig.ticket(); QTRY_VERIFY(!rig.host->approval(ticket).isEmpty());
        const auto id=rig.host->approve(ticket,{QStringLiteral("pause")}); QVERIFY(!id.isEmpty()); QTRY_VERIFY(!controller);
        QCOMPARE(cancels,1u); QVERIFY(rig.host->sessions().isEmpty()); QCOMPARE(rig.player.native->revokes.load(),1u);
    }
    void sliceDeadlineStopsANonresponsiveGuest() {
        Rig rig; rig.managed=true; QObject worker; unsigned invokes=0,cancels=0;
        AddonSessionController controller(rig.host.get(),{&worker,[&](const QString&,const QString&,const QVariantMap&) { ++invokes; return QStringLiteral("waits-without-result"); },[&](const QString&) { ++cancels; }});
        const auto id=activate(rig); QVERIFY(!id.isEmpty()); QTRY_COMPARE(invokes,1u);
        QTRY_VERIFY_WITH_TIMEOUT(rig.host->sessions().isEmpty(),27000); QCOMPARE(cancels,1u); QCOMPARE(rig.player.native->revokes.load(),1u);
        controller.completed(QStringLiteral("waits-without-result"),globalId,QStringLiteral("match"),{{"continue",true}},{});
        QTest::qWait(550); QCOMPARE(invokes,1u);
    }
};
QTEST_GUILESS_MAIN(TestAddonSessionHost)
#include "test_addon_session_host.moc"
