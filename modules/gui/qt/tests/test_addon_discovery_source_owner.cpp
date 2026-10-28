// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <QTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QProcess>
#include <QElapsedTimer>
#include <QScopeGuard>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <vlc_source_owner.h>
#include <vlc_source_backend.h>
#include <vlc_input_policy.h>
#include <vlc/vlc.h>
#include <vlc_playlist.h>
#include <vlc_input_item.h>
#include <vlc_player.h>
#include "lib/libvlc_internal.h"
#include "addons/addon_discovery_source_owner.hpp"
#include "addons/addon_native_wake.hpp"
#include "addons/addon_source_admissions.hpp"
#include "addons/addon_manager.hpp"
#include "addons/addon_native_queue_store.hpp"
#include "addons/addon_player_discovery.hpp"
#include "addons/addon_discovery_service.hpp"
#include "addons/addon_stream_proxy_native.hpp"
#include "addons/addon_source_consent_ui.hpp"
#include "fixtures/addon_source_tls_fixture.hpp"
using namespace vlc::addons;
#ifdef HAKONE_SOURCE_TLS_PROBE_WRAP
namespace {
std::atomic_int tlsOverride{-1};
std::atomic_uint tlsCalls{0},tlsOwnerThreadCalls{0};
std::atomic_bool tlsEntered{false},tlsReleased{false};
std::mutex tlsMutex;
std::condition_variable tlsCondition;
const auto tlsOwnerThread=std::this_thread::get_id();
}
extern "C" bool __real__ZN10QSslSocket11supportsSslEv();
extern "C" bool __wrap__ZN10QSslSocket11supportsSslEv() {
    ++tlsCalls;
    if(std::this_thread::get_id()==tlsOwnerThread) ++tlsOwnerThreadCalls;
    const auto mode=tlsOverride.load(std::memory_order_acquire);
    if(mode==2) {
        tlsEntered.store(true,std::memory_order_release);
        std::unique_lock<std::mutex> lock(tlsMutex);
        tlsCondition.wait(lock,[] { return tlsReleased.load(std::memory_order_acquire); });
    }
    if(mode==0) return false;
    return __real__ZN10QSslSocket11supportsSslEv();
}
#endif
namespace vlc::addons {
struct DiscoverySourceTestAccess {
    static void prepare(AddonDiscoverySourceOwner& owner,AddonDiscoverySourceOwner::PrepareTransport value) { owner.m_prepareTransport=std::move(value); }
    static bool polling(const AddonDiscoverySourceOwner& owner) { return owner.m_poll.isActive(); }
    static void closeWake(AddonDiscoverySourceOwner& owner) { owner.m_wake->close(); }
    static int remaining(const AddonDiscoverySourceOwner& owner) { return owner.m_poll.remainingTime(); }
    static void notify(AddonDiscoverySourceOwner& owner) { owner.notify(); }

};
}
namespace {
const QString source=QStringLiteral("org.hakone.discovery-fixture/catalogue");
QString workerName() {
#ifdef Q_OS_WIN
    return QStringLiteral("source-worker.exe");
#else
    return QStringLiteral("source-worker");
#endif
}
bool copyWorker(const QString& path) {
    return QFile::copy(QStringLiteral(VLC_ADDON_DISCOVERY_PROCESS_FIXTURE),path)
        && QFile::setPermissions(path,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
}
struct NativeModuleLog {
    std::mutex mutex; std::vector<std::string> selected;
    static void record(void* raw,int,const libvlc_log_t*,const char* format,va_list args) {
        char line[1024]; vsnprintf(line,sizeof(line),format,args); if(!strstr(line,"using ")) return;
        auto& self=*static_cast<NativeModuleLog*>(raw); std::lock_guard<std::mutex> lock(self.mutex);
        if(self.selected.size()<512) self.selected.emplace_back(line);
    }
};
const auto nativeModuleLog=std::make_shared<NativeModuleLog>();
struct PlaylistLock {
    vlc_playlist_t* playlist; explicit PlaylistLock(vlc_playlist_t* value):playlist(value) { vlc_playlist_Lock(playlist); }
    ~PlaylistLock() { vlc_playlist_Unlock(playlist); }
};
struct Lease {
    std::shared_ptr<std::atomic_bool> active;
    std::shared_ptr<std::atomic_uint> canceled;
    static void cancel(void* raw) { auto* self=static_cast<Lease*>(raw); self->active->store(false); ++*self->canceled; }
    static void destroy(void* raw) { delete static_cast<Lease*>(raw); }
};
struct NativeJob {
    vlc_source_job_t* value=nullptr; std::atomic_int ended{0},status{VLC_EGENERIC};
    static void complete(vlc_source_job_t*,int status,void* raw) { auto* self=static_cast<NativeJob*>(raw); self->status=status; ++self->ended; }
    NativeJob(vlc_object_t* root,const QString& uri,quint64 generation) {
        auto* backend=vlc_source_owner_Hold(root); if(!backend) return;
        const auto bytes=uri.toUtf8(); const vlc_source_request request{bytes.constData(),generation};
        value=vlc_source_job_New(backend,&request,complete,this); vlc_source_backend_Release(backend);
    }
    ~NativeJob() { if(value) { vlc_source_job_Cancel(value); vlc_source_job_Release(value); } }
    int start() { int result=VLC_EGENERIC; std::thread native([&] { if(value) result=vlc_source_job_Start(value); }); native.join(); return result; }
};
struct Fixture {
    QTemporaryDir profile,package,tools;
    AddonStreamLifetime lifetime;
    libvlc_int_t* instance=libvlc_InternalCreate();
    libvlc_instance_t* native=nullptr;
    std::unique_ptr<AddonManager> manager;
    std::shared_ptr<AddonSourceAdmissions> admissions;
    std::unique_ptr<AddonDiscoverySourceOwner> owner;
    std::shared_ptr<std::atomic_uint> starts=std::make_shared<std::atomic_uint>(0),canceled=std::make_shared<std::atomic_uint>(0);
    bool valid=false;
    Fixture(bool permission=true,bool playback=false,bool privateAccount=false) {
        if(playback) {
            libvlc_InternalDestroy(instance); instance=nullptr;
            const char* args[]={"--ignore-config","--quiet","--no-plugins-cache","--no-media-library","--no-metadata-network-access","--aout=adummy","--vout=vdummy","--text-renderer=tdummy"};
            native=libvlc_new(int(std::size(args)),args); if(!native) return; instance=native->p_libvlc_int;
        }
        const QString base=QStringLiteral(VLC_ADDON_SOURCE_ROOT "/contrib-addons/examples/discovery-fixture");
        QDir().mkpath(package.filePath("dist"));
        if(!QFile::copy(base+"/LICENSE",package.filePath("LICENSE")) || !QFile::copy(base+"/dist/addon.wasm",package.filePath("dist/addon.wasm"))) return;
        QFile manifestFile(base+"/addon.json"); if(!manifestFile.open(QIODevice::ReadOnly)) return;
        auto manifest=QJsonDocument::fromJson(manifestFile.readAll()).object();
        if(permission) manifest.insert("permissions",QJsonObject{{"required",QJsonObject{{"media.stream",QJsonObject{{"profiles",QJsonArray{"public-https-media@1"}}}}}},{"optional",QJsonObject{}}});
        if(privateAccount) {
            QDir().mkpath(package.filePath("settings"));
            const QJsonObject schema{{"type","object"},{"additionalProperties",false},{"properties",QJsonObject{{"apiKey",QJsonObject{{"type","string"},{"format","password"},{"writeOnly",true},{"x-hakone-secret",true},{"minLength",1}}}}},{"required",QJsonArray{"apiKey"}}};
            QFile schemaFile(package.filePath("settings/schema.json")); if(!schemaFile.open(QIODevice::WriteOnly)) return;
            schemaFile.write(QJsonDocument(schema).toJson()); schemaFile.close();
            auto files=manifest.value("files").toArray(); files.append("settings/schema.json"); manifest.insert("files",files);
            manifest.insert("settings",QJsonObject{{"version",1},{"schema","settings/schema.json"}});
            auto api=manifest.value("api").toObject(),contracts=api.value("contracts").toObject(); contracts.insert("settings","^1.0.0"); api.insert("contracts",contracts); manifest.insert("api",api);
            auto permissions=manifest.value("permissions").toObject(),required=permissions.value("required").toObject();
            required.insert("network.http",QJsonObject{{"origins",QJsonArray{"https://api.example.test"}},{"methods",QJsonArray{"GET"}}});
            required.insert("credentials.use",QJsonObject{{"bindings",QJsonArray{QJsonObject{{"field","apiKey"},{"origin","https://api.example.test"},{"header","Authorization"},{"prefix","Bearer "}}}}});
            permissions.insert("required",required); manifest.insert("permissions",permissions);
        }
        QFile output(package.filePath("addon.json")); if(!output.open(QIODevice::WriteOnly)) return; output.write(QJsonDocument(manifest).toJson()); output.close();
        const auto worker=tools.filePath(workerName());
        if(!copyWorker(worker)) return;
        manager=std::make_unique<AddonManager>(profile.path()); manager->setWorkerExecutable(worker);
        if(!manager->installLocalPackage(package.path(),true)) return;
        if(privateAccount && !manager->saveAddonSettings("org.hakone.discovery-fixture",{},{{"apiKey",QVariantMap{{"operation","replace"},{"value","unit-source-key"},{"sessionOnly",true}}}})) return;
        if(!manager->setEnabled("org.hakone.discovery-fixture",true)) return;
        const QPointer<AddonManager> current(manager.get());
        admissions=std::make_shared<AddonSourceAdmissions>(profile.filePath("admissions"),[current](const QVariantMap& reference) {
            if(!current) return false;
            for(const auto& entry:current->contributionsOfType(QStringLiteral("discovery.source"))) {
                const auto row=entry.toMap();
                if(row.value("id")==reference.value("source") && row.value("accountReference")==reference.value("accountReference")) return true;
            }
            return false;
        });
        owner=std::make_unique<AddonDiscoverySourceOwner>(manager.get(),VLC_OBJECT(instance),admissions,lifetime.handle());
        const auto started=starts,stopped=canceled;
        DiscoverySourceTestAccess::prepare(*owner,[started,stopped](const auto&,auto check,auto,auto done) {
            auto active=std::make_shared<std::atomic_bool>(true); static const vlc_source_lease_operations ops{Lease::cancel,Lease::destroy};
            auto* lease=vlc_source_lease_New(&ops,new Lease{active,stopped});
            auto* policy=vlc_input_policy_NewProxy(32123,"/source_fixture/",nullptr,false);
            done(AddonNativeSourceResolver::Prepared::adopt("http://127.0.0.1:32123/source_fixture/main",policy,lease,active,
                [started](auto current,auto ready) { if(current()) { ++*started; ready(VLC_SUCCESS); } else ready(VLC_EACCES); return AddonNativeSourceResolver::Cancel{}; }));
            return AddonNativeSourceResolver::Cancel{};
        });
        valid=true;
    }
    ~Fixture() {
        owner.reset(); manager.reset(); lifetime.close();
        if(!lifetime.handle().waitQt(std::chrono::seconds(5))) qFatal("The source fixture join did not end.");
        if(native) libvlc_release(native); else if(instance) libvlc_InternalDestroy(instance);
    }
    QVariantMap reference() const {
        const auto rows=manager->contributionsOfType(QStringLiteral("discovery.source")); if(rows.isEmpty()) return {};
        return {{"format","hakone-addon-source-v1"},{"source",source},{"accountReference",rows.first().toMap().value("accountReference")},
            {"itemId","station"},{"snapshot",QVariantMap{{"title","Station"},{"mediaKind","musicTrack"}}}};
    }
    QString uri() { QStringList values; return admissions->admit({reference()},&values)?values.first():QString{}; }
    int resolves() const { QFile file(tools.filePath(workerName()+QStringLiteral(".resolves"))); return file.open(QIODevice::ReadOnly)?file.readAll().count('\n'):0; }
};
}
class TestAddonDiscoverySourceOwner final:public QObject {
    Q_OBJECT
private slots:
#ifdef HAKONE_SOURCE_TLS_PROBE_WRAP
    void anEmptyBoundOwnerDoesNotInitializeTheTlsBackend() {
        Fixture f; QVERIFY(f.valid); tlsOverride=0; tlsCalls=0; tlsOwnerThreadCalls=0;
        const auto reset=qScopeGuard([] { tlsOverride=-1; });
        QVERIFY(f.owner->activate()); QTRY_VERIFY(f.owner->active() || !f.owner->errorMessage().isEmpty());
        QCOMPARE(tlsCalls.load(),0u); QVERIFY(f.owner->active());
        QVERIFY(!f.owner->transportKnown()); QVERIFY(!f.owner->transportAvailable());
        QTest::qWait(80); QCOMPARE(tlsCalls.load(),0u); QCOMPARE(tlsOwnerThreadCalls.load(),0u);
        QVERIFY(!DiscoverySourceTestAccess::polling(*f.owner)); QCOMPARE(f.resolves(),0);
    }
    void anUnavailableTlsHintStillAllowsReviewApprovalAndStableReferences() {
        Fixture f; QVERIFY(f.valid); tlsOverride=0; tlsCalls=0; tlsOwnerThreadCalls=0;
        const auto reset=qScopeGuard([] { tlsOverride=-1; });
        QVERIFY(f.owner->activate()); QTRY_VERIFY(f.owner->active());
        AddonSourceConsentUi panel(f.owner.get()); QVERIFY(panel.available());
        QVERIFY(!f.owner->transportKnown()); QVERIFY(panel.prepare(source));
        QTRY_VERIFY(!panel.review().isEmpty()); QVERIFY(f.owner->transportKnown());
        QVERIFY(!f.owner->transportAvailable()); QVERIFY(panel.available());
        QCOMPARE(tlsCalls.load(),1u); QCOMPARE(tlsOwnerThreadCalls.load(),0u);
        QVERIFY(panel.approve()); QTRY_VERIFY(f.owner->canUseSource(source));
        const auto guard=f.owner->referenceGuard({f.reference()}); QVERIFY(guard); QVERIFY(guard());
        QCOMPARE(f.resolves(),0); QCOMPARE(f.starts->load(),0u);
        panel.viewClosed(); tlsOverride=-1; QVERIFY(panel.prepare(source));
        QTRY_VERIFY(!panel.review().isEmpty()); QVERIFY(panel.review().value("approved").toBool());
        QVERIFY(f.owner->transportAvailable()); QCOMPARE(tlsOwnerThreadCalls.load(),0u);
    }
    void actualPrimaryAdmissionRequiresAJoinedTlsProbeBeforeTheProvider() {
        Fixture f; QVERIFY(f.valid); tlsOverride=-1;
        const auto reset=qScopeGuard([] { tlsOverride=-1; });
        QVERIFY(f.owner->activate()); QTRY_VERIFY(f.owner->active());
        QSignalSpy review(f.owner.get(),&AddonDiscoverySourceOwner::approvalReady);
        const auto id=f.owner->requestApproval(source); QTRY_COMPARE(review.size(),1);
        QVERIFY(f.owner->approve(id)); QTRY_VERIFY(f.owner->canUseSource(source)); f.owner->discardApproval(id);
        const auto uri=f.uri(); QVERIFY(!uri.isEmpty()); tlsOverride=0; tlsCalls=0; tlsOwnerThreadCalls=0;
        { NativeJob denied(VLC_OBJECT(f.instance),uri,1); QCOMPARE(denied.start(),VLC_SUCCESS); QTRY_COMPARE(denied.ended.load(),1);
          QCOMPARE(denied.status.load(),VLC_EACCES); }
        QCOMPARE(f.resolves(),0); QCOMPARE(f.starts->load(),0u); QCOMPARE(tlsCalls.load(),1u); QCOMPARE(tlsOwnerThreadCalls.load(),0u);
        QVERIFY(f.owner->active()); QVERIFY(f.owner->canUseSource(source)); QVERIFY(!f.owner->transportAvailable());
        tlsOverride=-1;
        { NativeJob retry(VLC_OBJECT(f.instance),uri,2); QCOMPARE(retry.start(),VLC_SUCCESS); QTRY_COMPARE(retry.ended.load(),1);
          QCOMPARE(retry.status.load(),VLC_SUCCESS); }
        QCOMPARE(f.resolves(),1); QCOMPARE(f.starts->load(),1u); QVERIFY(f.owner->transportAvailable());
    }
    void retirementWaitsForTheExplicitProbeWithoutKeepingTheOwnerAlive() {
        Fixture f; QVERIFY(f.valid); tlsOverride=2; tlsEntered=false; tlsReleased=false;
        const auto release=qScopeGuard([] { tlsOverride=-1; tlsReleased.store(true,std::memory_order_release); tlsCondition.notify_all(); });
        QVERIFY(f.owner->activate()); QTRY_VERIFY(f.owner->active());
        QVERIFY(!f.owner->requestApproval(source).isEmpty()); QTRY_VERIFY(tlsEntered.load(std::memory_order_acquire));
        f.owner->close(); f.owner.reset(); f.lifetime.close();
        QVERIFY(!f.lifetime.handle().waitQt(std::chrono::milliseconds(20)));
        tlsReleased.store(true,std::memory_order_release); tlsCondition.notify_all();
        QVERIFY(f.lifetime.handle().waitQt(std::chrono::seconds(3))); QCOMPARE(f.resolves(),0);
    }
#endif
    void subscriptionFailureEndsActivationAndCannotRetryAClosedInitializer() {
        Fixture f; QVERIFY(f.valid); StoreAuthority authority(f.profile.path());
        std::vector<StoreAuthorityChanges> occupied;
        for(unsigned index=0;index<StoreAuthority::ChangeLimit;++index) {
            auto change=authority.onChanged([] {}); QVERIFY(bool(change)); occupied.push_back(std::move(change));
        }
        QVERIFY(f.owner->activate());
        QTRY_VERIFY(!f.owner->errorMessage().isEmpty());
        QVERIFY(!f.owner->active()); QVERIFY(!DiscoverySourceTestAccess::polling(*f.owner));
        QTRY_COMPARE(f.lifetime.handle().qtWorkCount(),0u);
        occupied.clear(); QVERIFY(!f.owner->activate());
        QCoreApplication::processEvents(); QVERIFY(!f.owner->active());
        QVERIFY(!vlc_source_owner_Hold(VLC_OBJECT(f.instance))); QCOMPARE(f.resolves(),0);
    }
    void repeatedNoticesDoNotPostponeThePendingReviewDeadline() {
        Fixture f; QVERIFY(f.valid); QVERIFY(f.owner->activate()); QTRY_VERIFY(f.owner->active());
        QSignalSpy review(f.owner.get(),&AddonDiscoverySourceOwner::approvalReady);
        const auto id=f.owner->requestApproval(source); QTRY_COMPARE(review.size(),1);
        QVERIFY(DiscoverySourceTestAccess::polling(*f.owner));
        const auto before=DiscoverySourceTestAccess::remaining(*f.owner); QVERIFY(before>0);
        QTest::qWait(30);
        for(unsigned index=0;index<100;++index) DiscoverySourceTestAccess::notify(*f.owner);
        const auto after=DiscoverySourceTestAccess::remaining(*f.owner);
        QVERIFY(after>=0); QVERIFY(after<before);
        f.owner->discardApproval(id); QVERIFY(!DiscoverySourceTestAccess::polling(*f.owner));
    }
    void anUnavailableWakeCannotStartTheSourceInitializer() {
        Fixture f; QVERIFY(f.valid); DiscoverySourceTestAccess::closeWake(*f.owner);
        QVERIFY(!f.owner->activate()); QVERIFY(!f.owner->active()); QVERIFY(!f.owner->errorMessage().isEmpty());
        QVERIFY(!DiscoverySourceTestAccess::polling(*f.owner));
        QVERIFY(!vlc_source_owner_Hold(VLC_OBJECT(f.instance)));
        QVERIFY(f.lifetime.handle().waitQt(std::chrono::seconds(1)));
    }
    void anEmptyExternalCoreCloseCannotAuthorizeACachedSource() {
        Fixture f; QVERIFY(f.valid); QVERIFY(f.owner->activate()); QTRY_VERIFY(f.owner->active());
        QSignalSpy review(f.owner.get(),&AddonDiscoverySourceOwner::approvalReady);
        const auto id=f.owner->requestApproval(source); QTRY_COMPARE(review.size(),1);
        QVERIFY(f.owner->approve(id)); QTRY_VERIFY(f.owner->canUseSource(source)); f.owner->discardApproval(id);
        QVERIFY(!DiscoverySourceTestAccess::polling(*f.owner)); QCOMPARE(f.resolves(),0);
        auto* backend=vlc_source_owner_Hold(VLC_OBJECT(f.instance)); QVERIFY(backend);
        vlc_source_backend_Close(backend); vlc_source_backend_Release(backend);
        // An empty arbitrary Core close has no notification hook. Explicit
        // queries still reject immediately. Production retirement calls close.
        QVERIFY(!f.owner->active()); QVERIFY(!f.owner->canUseSource(source));
        QVERIFY(f.owner->refreshSource(source).isEmpty()); QVERIFY(f.owner->requestApproval(source).isEmpty());
        f.owner->close(); f.lifetime.close();
        QVERIFY(f.lifetime.handle().waitQt(std::chrono::seconds(3))); QCOMPARE(f.resolves(),0);
    }
    void anActivatedEmptySourceOwnerHasNoRecurringTimer() {
        Fixture f; QVERIFY(f.valid); QVERIFY(f.owner->activate()); QTRY_VERIFY(f.owner->active());
        QVERIFY(!DiscoverySourceTestAccess::polling(*f.owner));
        QTest::qWait(80); QVERIFY(!DiscoverySourceTestAccess::polling(*f.owner)); QCOMPARE(f.resolves(),0);
    }
    void cachedAvailabilityWakesOnStoreChangeWithoutRecurringTimer() {
        Fixture f; QVERIFY(f.valid); QVERIFY(f.owner->activate()); QTRY_VERIFY(f.owner->active());
        QSignalSpy review(f.owner.get(),&AddonDiscoverySourceOwner::approvalReady);
        const auto id=f.owner->requestApproval(source); QVERIFY(!id.isEmpty()); QTRY_COMPARE(review.size(),1);
        QVERIFY(f.owner->approve(id)); QTRY_VERIFY(f.owner->canUseSource(source));
        f.owner->discardApproval(id); QVERIFY(!DiscoverySourceTestAccess::polling(*f.owner));
        QCoreApplication::processEvents(); QSignalSpy changed(f.owner.get(),&AddonDiscoverySourceOwner::changed);
        const auto directory=f.profile.path(); std::thread writer([directory] { PackageStore(directory).invalidateAuthority(); }); writer.join();
        QVERIFY(!f.owner->canUseSource(source));
        QTRY_VERIFY(!changed.isEmpty()); QVERIFY(!DiscoverySourceTestAccess::polling(*f.owner)); QCOMPARE(f.resolves(),0);
    }
    void portableWorkerKeepsFramingAndOneResolveRecord() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString path=directory.filePath(QString::fromUtf8("Source \xe9\x9f\xb3\xe6\xa5\xbd"));
        QVERIFY(QDir().mkpath(path)); const auto executable=QDir(path).filePath(workerName());
        QVERIFY(copyWorker(executable));
        QProcess process; process.start(executable,{QStringLiteral("unused.wasm")}); QVERIFY(process.waitForStarted());
        QByteArray output;
        const auto readLines=[&](int count) {
            QElapsedTimer elapsed; elapsed.start();
            while(output.count('\n')<count && elapsed.elapsed()<5000) {
                output+=process.readAllStandardOutput();
                if(output.count('\n')<count) process.waitForReadyRead(50);
            }
            output+=process.readAllStandardOutput(); return output.count('\n')==count;
        };
        QVERIFY(readLines(1));
        QCOMPARE(QJsonDocument::fromJson(output.trimmed()).object().value("type").toString(),QStringLiteral("ready"));
        const QByteArray requests="{\"type\":\"invoke\",\"id\":\"roots\",\"operation\":\"roots\",\"context\":{}}\n"
            "{\"type\":\"cancel\",\"id\":\"ignored\"}\n"
            "{\"type\":\"invoke\",\"id\":\"resolve\\\"quoted\",\"operation\":\"resolve\",\"context\":{\"itemRef\":\"station\"}}\n";
        QCOMPARE(process.write(requests),qint64(requests.size())); process.closeWriteChannel();
        QVERIFY(readLines(3));
        if(process.state()!=QProcess::NotRunning) QVERIFY(process.waitForFinished());
        QCOMPARE(process.exitStatus(),QProcess::NormalExit); QCOMPARE(process.exitCode(),0);
        const auto lines=output.trimmed().split('\n'); QCOMPARE(lines.size(),3);
        const auto root=QJsonDocument::fromJson(lines.at(1)).object().value("messages").toArray().first().toObject();
        QCOMPARE(root.value("id").toString(),QStringLiteral("roots")); QCOMPARE(root.value("data").toObject().value("items").toArray().size(),1);
        const auto resolved=QJsonDocument::fromJson(lines.at(2)).object().value("messages").toArray().first().toObject();
        QCOMPARE(resolved.value("id").toString(),QStringLiteral("resolve\"quoted"));
        QCOMPARE(resolved.value("data").toObject().value("format").toString(),QStringLiteral("progressive"));
        QFile log(executable+QStringLiteral(".resolves")); QVERIFY(log.open(QIODevice::ReadOnly)); QCOMPARE(log.readAll(),QByteArray("resolve\n"));
        QVERIFY(process.readAllStandardError().isEmpty());
    }
    void realQueuePlaybackUsesOneFreshResolveAndContainedTlsPerOpen() {
        AddonSourceTlsFixture tls; QVERIFY(tls.start()); Fixture f(true,true); QVERIFY(f.valid);
        libvlc_log_set(f.native,NativeModuleLog::record,nativeModuleLog.get());
        const auto stopLog=qScopeGuard([&] { libvlc_log_unset(f.native); });
        DiscoverySourceTestAccess::prepare(*f.owner,[testing=tls.testing()](const auto& admission,auto current,auto spec,auto ready) {
            return prepareContainedSourceForTesting(admission,std::move(current),std::move(spec),testing,std::move(ready));
        });
        QVERIFY(f.owner->activate()); QTRY_VERIFY(f.owner->active());
        QSignalSpy review(f.owner.get(),&AddonDiscoverySourceOwner::approvalReady);
        const auto approval=f.owner->requestApproval(source); QTRY_COMPARE(review.size(),1); QVERIFY(f.owner->approve(approval)); QTRY_VERIFY(f.owner->canUseSource(source));
        const auto playlist=std::unique_ptr<vlc_playlist_t,decltype(&vlc_playlist_Delete)>(vlc_playlist_New(VLC_OBJECT(f.instance),VLC_PLAYLIST_PREPARSING_DISABLED,1,0),vlc_playlist_Delete); QVERIFY(playlist);
        AddonNativeQueueStore queue(playlist.get(),f.admissions);
        const auto service=std::unique_ptr<AddonDiscoveryService>(createPlayerDiscoveryWithSources(f.manager.get(),f.owner.get(),queue.backend(),nullptr));
        const auto view=service->openSource(source); QVERIFY(!view.isEmpty()); QTRY_COMPARE(service->view(view).value("items").toList().size(),1);
        const auto item=service->view(view).value("items").toList().first().toMap().value("ref").toString();
        QVERIFY(service->canEnqueue(view)); QVERIFY(!service->enqueue(view,{item}).isEmpty()); QCOMPARE(f.resolves(),0); QCOMPARE(tls.requests.load(),0u);
        quint64 itemId=0; QString uri;
        { PlaylistLock locked(playlist.get()); QCOMPARE(vlc_playlist_Count(playlist.get()),size_t(1));
        auto* entry=vlc_playlist_Get(playlist.get(),0); itemId=vlc_playlist_item_GetId(entry);
        char* raw=input_item_GetURI(vlc_playlist_item_GetMedia(entry)); uri=QString::fromUtf8(raw); free(raw); }
        QVERIFY(uri.startsWith(QLatin1String("hakone-addon-source:v1:")));
        struct Stats { std::atomic<quint64> decoded{0}; std::atomic_uint stopped{0}; } stats;
        static const vlc_player_cbs callbacks=[] { vlc_player_cbs value{};
            value.on_statistics_changed=[](vlc_player_t*,const input_stats_t* value,void* raw) { static_cast<Stats*>(raw)->decoded=value->i_decoded_audio; };
            value.on_state_changed=[](vlc_player_t*,vlc_player_state value,void* raw) { if(value==VLC_PLAYER_STATE_STOPPED) ++static_cast<Stats*>(raw)->stopped; }; return value; }();
        auto* player=vlc_playlist_GetPlayer(playlist.get()); vlc_player_Lock(player); auto* listener=vlc_player_AddListener(player,&callbacks,&stats); vlc_player_Unlock(player); QVERIFY(listener);
        const auto cleanup=qScopeGuard([&] { vlc_player_Lock(player); vlc_player_Stop(player); vlc_player_RemoveListener(player,listener); vlc_player_Unlock(player); });
        QVERIFY(!service->play(view,item).isEmpty()); QTRY_VERIFY_WITH_TIMEOUT(stats.decoded.load()>0,10000);
        QCOMPARE(f.resolves(),1); QVERIFY(tls.requests.load()>0);
        { PlaylistLock locked(playlist.get()); QCOMPARE(vlc_playlist_Count(playlist.get()),size_t(1)); QCOMPARE(vlc_playlist_item_GetId(vlc_playlist_Get(playlist.get(),0)),itemId); vlc_playlist_Stop(playlist.get()); }
        QTRY_VERIFY(stats.stopped.load()>0); stats.decoded=0; const auto before=tls.requests.load();
        QVERIFY(!service->play(view,item).isEmpty()); QTRY_VERIFY_WITH_TIMEOUT(stats.decoded.load()>0,10000);
        QCOMPARE(f.resolves(),2); QVERIFY(tls.requests.load()>before); QCOMPARE(queue.queuedReference(itemId).value("itemId").toString(),QStringLiteral("station"));
        QJsonArray modules; { std::lock_guard<std::mutex> lock(nativeModuleLog->mutex); for(const auto& line:nativeModuleLog->selected) modules.append(QString::fromStdString(line)); }
        QFile selected(QDir::tempPath()+QStringLiteral("/hakone-source-selected-modules.json"));
        QVERIFY(selected.open(QIODevice::WriteOnly)); selected.write(QJsonDocument(modules).toJson());
    }
    void keyReplacementOrDeletionRevokesTheOldAccountAndQueuedLocator_data() {
        QTest::addColumn<bool>("removeKey"); QTest::newRow("replace")<<false; QTest::newRow("delete")<<true;
    }
    void keyReplacementOrDeletionRevokesTheOldAccountAndQueuedLocator() {
        QFETCH(bool,removeKey); Fixture f(true,false,true); QVERIFY2(f.valid,qPrintable(f.manager?f.manager->errorMessage():QStringLiteral("The fixture package is not complete."))); QVERIFY(f.owner->activate()); QTRY_VERIFY(f.owner->active());
        QSignalSpy review(f.owner.get(),&AddonDiscoverySourceOwner::approvalReady);
        const auto id=f.owner->requestApproval(source); QTRY_COMPARE(review.size(),1); QVERIFY(f.owner->approve(id)); QTRY_VERIFY(f.owner->canUseSource(source));
        const auto old=f.reference(); QVERIFY(old.value("accountReference").toString().startsWith(QLatin1String("account:")));
        const auto nativeGuard=f.owner->referenceGuard({old}); QVERIFY(nativeGuard); QVERIFY(nativeGuard());
        const auto uri=f.uri(); NativeJob active(VLC_OBJECT(f.instance),uri,1); QCOMPARE(active.start(),VLC_SUCCESS); QTRY_COMPARE(active.ended.load(),1); QCOMPARE(active.status.load(),VLC_SUCCESS);
        const QVariantMap operation=removeKey?QVariantMap{{"operation","delete"}}:QVariantMap{{"operation","replace"},{"value","unit-source-replacement"},{"sessionOnly",true}};
        QVERIFY(f.manager->saveAddonSettings("org.hakone.discovery-fixture",{},{{"apiKey",operation}}));
        QCOMPARE(f.canceled->load(),1u); QVERIFY(!f.owner->canUseReference(old));
        std::atomic_bool current{true}; std::thread native([&] { current=nativeGuard(); }); native.join(); QVERIFY(!current.load());
        NativeJob stale(VLC_OBJECT(f.instance),uri,2); QCOMPARE(stale.start(),VLC_SUCCESS); QTRY_COMPARE(stale.ended.load(),1); QCOMPARE(stale.status.load(),VLC_EACCES); QCOMPARE(f.resolves(),1);
        if(!removeKey) {
            QVERIFY(f.reference().value("accountReference")!=old.value("accountReference"));
            const auto next=f.owner->requestApproval(source); QTRY_COMPARE(review.size(),2); QVERIFY(!f.owner->approval(next).value("approved").toBool());
            QVERIFY(f.owner->approve(next)); QTRY_VERIFY(f.owner->canUseSource(source)); QVERIFY(!f.owner->canUseReference(old));
        }
    }
    void discardedReviewsAndAppRetirementCannotGrantPlayback() {
        Fixture f; QVERIFY(f.valid); QVERIFY(f.owner->activate()); QTRY_VERIFY(f.owner->active());
        QSignalSpy review(f.owner.get(),&AddonDiscoverySourceOwner::approvalReady);
        auto id=f.owner->requestApproval(source); QTRY_COMPARE(review.size(),1); f.owner->discardApproval(id);
        QVERIFY(f.owner->approval(id).isEmpty()); QVERIFY(!f.owner->approve(id)); QVERIFY(!f.owner->canUseSource(source));
        id=f.owner->requestApproval(source); QTRY_COMPARE(review.size(),2); QVERIFY(f.owner->approve(id)); QTRY_VERIFY(f.owner->canUseSource(source));
        NativeJob active(VLC_OBJECT(f.instance),f.uri(),1); QCOMPARE(active.start(),VLC_SUCCESS); QTRY_COMPARE(active.ended.load(),1); QCOMPARE(active.status.load(),VLC_SUCCESS);
        f.owner->close(); f.lifetime.close(); QCOMPARE(f.canceled->load(),1u); QVERIFY(!f.owner->active()); QVERIFY(!f.owner->canUseSource(source));
        QVERIFY(f.lifetime.handle().waitQt(std::chrono::seconds(3)));
    }
    void activationAndApprovalAreSeparate() {
        Fixture f; QVERIFY(f.valid); QVERIFY(!f.owner->active()); QVERIFY(!f.owner->canUseSource(source));
        QVERIFY(f.owner->activate()); QTRY_VERIFY(f.owner->active()); QVERIFY(!f.owner->canUseSource(source));
        QSignalSpy review(f.owner.get(),&AddonDiscoverySourceOwner::approvalReady),finished(f.owner.get(),&AddonDiscoverySourceOwner::actionFinished);
        const auto id=f.owner->requestApproval(source); QVERIFY(!id.isEmpty()); QTRY_COMPARE(review.size(),1);
        const auto shown=f.owner->approval(id); QCOMPARE(shown.value("id").toString(),source); QCOMPARE(shown.value("profile").toString(),QStringLiteral("public-https-media@1"));
        QCOMPARE(shown.value("accountLabel").toString(),QStringLiteral("No account")); QVERIFY(!shown.value("approved").toBool()); QCOMPARE(finished.size(),0);
        QCOMPARE(f.resolves(),0); QVERIFY(f.owner->approve(id)); QVERIFY(!f.owner->approve(id)); QTRY_COMPARE(finished.size(),1);
        QVERIFY(finished.first()[1].toBool()); QTRY_VERIFY(f.owner->canUseSource(source)); QCOMPARE(f.resolves(),0);
    }
    void aMissingMediaGrantCannotCreateAReview() {
        Fixture f(false); QVERIFY(f.valid); QVERIFY(f.owner->activate()); QTRY_VERIFY(f.owner->active());
        QSignalSpy finished(f.owner.get(),&AddonDiscoverySourceOwner::actionFinished);
        const auto id=f.owner->requestApproval(source); QVERIFY(!id.isEmpty()); QTRY_COMPARE(finished.size(),1);
        QVERIFY(!finished.first()[1].toBool()); QVERIFY(f.owner->approval(id).isEmpty()); QVERIFY(!f.owner->approve(id));
    }
    void anUnknownImportCannotAskTheProviderForAStream() {
        Fixture f; QVERIFY(f.valid); QVERIFY(f.owner->activate()); QTRY_VERIFY(f.owner->active());
        QSignalSpy review(f.owner.get(),&AddonDiscoverySourceOwner::approvalReady);
        const auto id=f.owner->requestApproval(source); QTRY_COMPARE(review.size(),1); QVERIFY(f.owner->approve(id)); QTRY_VERIFY(f.owner->canUseSource(source));
        auto forged=f.reference(); forged.insert("itemId","foreign");
        QStringList known; QVERIFY(f.admissions->admit({f.reference()},&known));
        auto uri=known.first(); uri.replace("station","foreign");
        // A valid serialized locator from another profile still lacks this profile's private admission.
        QTemporaryDir other; AddonSourceAdmissions external(other.path(),[](const auto&){return true;});
        QStringList imported; QVERIFY(external.admit({forged},&imported));
        NativeJob job(VLC_OBJECT(f.instance),imported.first(),1); QCOMPARE(job.start(),VLC_SUCCESS); QTRY_COMPARE(job.ended.load(),1);
        QCOMPARE(job.status.load(),VLC_EACCES); QCOMPARE(f.resolves(),0); QCOMPARE(f.starts->load(),0u);
    }
    void onlyNativeOpenResolvesAndEveryReplayIsFresh() {
        Fixture f; QVERIFY(f.valid); QVERIFY(f.owner->activate()); QTRY_VERIFY(f.owner->active());
        QSignalSpy review(f.owner.get(),&AddonDiscoverySourceOwner::approvalReady);
        const auto id=f.owner->requestApproval(source); QTRY_COMPARE(review.size(),1); QVERIFY(f.owner->approve(id)); QTRY_VERIFY(f.owner->canUseSource(source));
        const auto uri=f.uri(); QVERIFY(!uri.isEmpty()); QCOMPARE(f.resolves(),0);
        { NativeJob first(VLC_OBJECT(f.instance),uri,1); QCOMPARE(first.start(),VLC_SUCCESS); QTRY_COMPARE(first.ended.load(),1); QCOMPARE(first.status.load(),VLC_SUCCESS); QCOMPARE(f.resolves(),1); }
        { NativeJob second(VLC_OBJECT(f.instance),uri,2); QCOMPARE(second.start(),VLC_SUCCESS); QTRY_COMPARE(second.ended.load(),1); QCOMPARE(second.status.load(),VLC_SUCCESS); QCOMPARE(f.resolves(),2); }
        QCOMPARE(f.starts->load(),2u); QCOMPARE(f.canceled->load(),2u);
    }
    void revokeClosesNativeTransportBeforeItsSavedWriteCompletes() {
        Fixture f; QVERIFY(f.valid); QVERIFY(f.owner->activate()); QTRY_VERIFY(f.owner->active());
        QSignalSpy review(f.owner.get(),&AddonDiscoverySourceOwner::approvalReady);
        auto id=f.owner->requestApproval(source); QTRY_COMPARE(review.size(),1); QVERIFY(f.owner->approve(id)); QTRY_VERIFY(f.owner->canUseSource(source));
        NativeJob job(VLC_OBJECT(f.instance),f.uri(),1); QCOMPARE(job.start(),VLC_SUCCESS); QTRY_COMPARE(job.ended.load(),1); QCOMPARE(job.status.load(),VLC_SUCCESS);
        id=f.owner->requestApproval(source); QTRY_COMPARE(review.size(),2); QVERIFY(f.owner->approval(id).value("approved").toBool());
        QVERIFY(f.owner->revoke(id)); QCOMPARE(f.canceled->load(),1u); QVERIFY(!f.owner->canUseSource(source));
    }
    void targetAccountAndManagerRetirementCloseReadiness() {
        Fixture f; QVERIFY(f.valid); QVERIFY(f.owner->activate()); QTRY_VERIFY(f.owner->active());
        QSignalSpy review(f.owner.get(),&AddonDiscoverySourceOwner::approvalReady);
        const auto id=f.owner->requestApproval(source); QTRY_COMPARE(review.size(),1); QVERIFY(f.owner->approve(id)); QTRY_VERIFY(f.owner->canUseSource(source));
        auto foreign=f.reference(); foreign.insert("accountReference","account:foreign"); QVERIFY(!f.owner->canUseReference(foreign));
        QVERIFY(f.owner->canUseReference(f.reference())); f.manager.reset(); QVERIFY(!f.owner->active()); QVERIFY(!f.owner->canUseSource(source));
    }
};
QTEST_GUILESS_MAIN(TestAddonDiscoverySourceOwner)
#include "test_addon_discovery_source_owner.moc"
