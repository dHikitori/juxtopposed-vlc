// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QLockFile>
#include <thread>
#include <vector>
#include <map>
#include <vlc/vlc.h>
#include "lib/libvlc_internal.h"
#include <vlc_addon_medialibrary.h>
#include <sqlite3.h>
#include "addons/addon_native_library_store.hpp"
#include "addons/addon_native_library_targets.hpp"
#include "addons/addon_manager.hpp"
#include "addons/addon_player_discovery.hpp"
#ifdef VLC_ADDON_REAL_ML_DISPATCHER
#include "medialibrary/medialib.hpp"
#include "maininterface/compositor.hpp"
#include <QThread>
#include <QTimer>
#include <QSemaphore>
#include <vlc_medialibrary_work.h>
#include <chrono>
#endif

using namespace vlc::addons;
namespace {
template<typename Function> auto native(Function function)
{
    using Result = decltype(function());
    Result result{};
    std::thread thread([&] { result = function(); }); thread.join();
    return result;
}
struct Library {
    QTemporaryDir directory;
    libvlc_instance_t* instance = nullptr;
    vlc_medialibrary_t* ml = nullptr;
    explicit Library(bool start = true) {
        qputenv("VLC_USERDATA_PATH", directory.path().toUtf8());
        const char* options[] = {"--ignore-config", "--no-plugins-cache", "--media-library", "--quiet"};
        instance = libvlc_new(4, options);
        if (instance) ml = vlc_ml_instance_get(instance->p_libvlc_int);
        if (ml && start) native([&] {
            auto* values=vlc_ml_list_playlists(ml,nullptr,VLC_ML_PLAYLIST_TYPE_ALL);
            if (values) vlc_ml_playlist_list_release(values); return true;
        });
    }
    ~Library() { if (instance) libvlc_release(instance); }
    bool available() { return native([&] { return vlc_addon_playlist_cas_available(ml); }); }
    qint64 playlist() { return native([&] {
        auto* value = ml ? vlc_ml_playlist_create(ml, "Library target") : nullptr;
        const auto id = value ? value->i_id : 0; if (value) vlc_ml_playlist_release(value); return id;
    }); }
    std::vector<std::string> members(qint64 id) { return native([&] {
        std::vector<std::string> result;
        auto* values = vlc_ml_list_playlist_media(ml, nullptr, id);
        if (values) {
            for (size_t index = 0; index < values->i_nb_items; ++index) {
                const auto* files = values->p_items[index].p_files;
                if (files && files->i_nb_items && files->p_items[0].psz_mrl) result.emplace_back(files->p_items[0].psz_mrl);
            }
            vlc_ml_media_list_release(values);
        }
        return result;
    }); }
    bool sql(const std::string& request) {
        sqlite3* connection = nullptr;
        const auto path = QDir(directory.path()).filePath("ml/ml.db").toUtf8();
        if (sqlite3_open_v2(path.constData(), &connection, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
            if (connection) sqlite3_close(connection); return false;
        }
        const auto close = qScopeGuard([&] { sqlite3_close(connection); });
        sqlite3_busy_timeout(connection, 5000);
        return sqlite3_exec(connection, request.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
    }
};
struct Source {
    QTemporaryDir directory, package;
    AddonManager manager{directory.filePath("addons")};
    const QString id = QStringLiteral("org.example.library-source");
    bool start() {
        QJsonObject manifest{{"formatVersion",1},{"id",id},{"name","Library source"},{"version","1.0.0"},
            {"publisher","Tests"},{"license","GPL-2.0-or-later"},{"source","https://example.org/library-source.git"},
            {"api",QJsonObject{{"addon","^1.0.0"},{"contracts",QJsonObject{{"discovery","^1.0.0"},{"settings","^1.0.0"}}}}},
            {"runtime",QJsonObject{{"kind","wasm-message-v1"},{"entry","worker.wasm"}}},
            {"files",QJsonArray{"addon.json","LICENSE","worker.wasm","schema.json"}},
            {"contributions",QJsonArray{QJsonObject{{"id","source"},{"type","discovery.source"},{"contract","discovery@1"},
                {"operations",QJsonArray{"roots","resolve"}},{"requires",QJsonArray{"network.http","credentials.use"}}}}},
            {"permissions",QJsonObject{{"required",QJsonObject{
                {"network.http",QJsonObject{{"origins",QJsonArray{"https://example.org"}},{"methods",QJsonArray{"GET"}}}},
                {"credentials.use",QJsonObject{{"bindings",QJsonArray{QJsonObject{{"field","apiKey"},{"origin","https://example.org"},
                    {"header","Authorization"},{"prefix","Bearer "}}}}}}}}}},
            {"settings",QJsonObject{{"version",1},{"schema","schema.json"},{"accountFields",QJsonArray{"username"}}}}};
        const QMap<QString,QByteArray> files{{"addon.json",QJsonDocument(manifest).toJson()},{"LICENSE","Test license"},
            {"worker.wasm",QByteArray::fromHex("0061736d01000000")},
            {"schema.json",R"({"type":"object","properties":{"apiKey":{"type":"string","x-hakone-secret":true,"minLength":1},"language":{"type":"string","default":"en"},"username":{"type":"string","default":"alice"}},"required":["apiKey"],"additionalProperties":false})"}};
        for (auto it=files.begin(); it!=files.end(); ++it) { QFile file(package.filePath(it.key())); if (!file.open(QIODevice::WriteOnly) || file.write(it.value())!=it.value().size()) return false; }
        return manager.installLocalPackage(package.path(),true) && replace(QStringLiteral("first-test-key")) && manager.setEnabled(id,true);
    }
    bool replace(const QString& key) { return manager.saveAddonSettings(id,{{"language","en"},{"username","alice"}},
        {{"apiKey",QVariantMap{{"operation","replace"},{"value",key},{"sessionOnly",true}}}}); }
    QVariantMap reference(const QString& item = QStringLiteral("item-1")) const {
        const auto sources = manager.contributionsOfType(QStringLiteral("discovery.source"),true);
        return {{"format","hakone-addon-source-v1"},{"source",QString(id+QStringLiteral("/source"))},{"itemId",item},
            {"accountReference",sources.isEmpty()?QString():sources.first().toMap().value("accountReference").toString()},
            {"snapshot",QVariantMap{{"title","Source item"},{"mediaKind","audio"}}}};
    }
    std::shared_ptr<AddonSourceAdmissions> admissions() { return std::make_shared<AddonSourceAdmissions>(directory.filePath("admissions"),
        [this](const auto& reference) { return discoveryReferenceAvailable(&manager,reference); }); }
};
class Dispatcher : public QObject {
public:
    struct Task { QPointer<const QObject> receiver; AddonNativeLibraryStore::NativeWork work; AddonNativeLibraryStore::GuiCompletion done; };
    vlc_medialibrary_t* ml;
    quint64 next = 0; std::map<quint64,Task> tasks;
    explicit Dispatcher(vlc_medialibrary_t* value) : ml(value) {}
    AddonNativeLibraryStore::Dispatcher backend() {
        return {this,[this](const QObject* receiver, auto work, auto done) {
            const auto id=++next; tasks.emplace(id,Task{receiver,std::move(work),std::move(done)}); return id;
        },[this](const QObject*,quint64 id) { tasks.erase(id); }};
    }
    Task take() { auto it=tasks.begin(); auto value=std::move(it->second); tasks.erase(it); return value; }
    void run() { auto task=take(); std::thread worker([&] { task.work(ml); }); worker.join(); if (task.receiver) task.done(); }
};
AddonQueueTransactions::Transaction transaction(AddonNativeLibraryStore& store,const QString& target,const QVariantList& references)
{
    const auto selected=store.snapshot(target); AddonQueueTransactions::Transaction value;
    value.id="test"; value.view="view"; value.target=target; value.targetIdentity=selected.identity;
    value.expectedRevision=selected.revision; value.references=references; return value;
}
}
class TestAddonNativeLibraryStore : public QObject {
    Q_OBJECT
    QByteArray originalUserData;
private slots:
    void initTestCase() { originalUserData=qgetenv("VLC_USERDATA_PATH"); qputenv("VLC_PLUGIN_PATH",VLC_ADDON_NATIVE_BUILD_ROOT "/modules"); }
    void cleanupTestCase() { if (originalUserData.isNull()) qunsetenv("VLC_USERDATA_PATH"); else qputenv("VLC_USERDATA_PATH",originalUserData); }
#ifdef VLC_ADDON_REAL_ML_DISPATCHER
    void actualMediaLibShutdownDoesNotNeedQtDelivery() {
        Library library; QVERIFY(library.available()); auto* ml=library.ml;
        auto* intf=vlc_object_create<qt_intf_t>(library.instance->p_libvlc_int); QVERIFY(intf);
        auto mediaLibrary=std::make_unique<MediaLib>(intf,nullptr); QObject receiver;
        QSemaphore started,resume; std::atomic_bool nativeDone{false},cleanupDone{false}; std::atomic_int queuedCalls{0}; int uiCalls=0;
        auto* passive=vlc_ml_work_Acquire(ml);
        const auto finish=qScopeGuard([&] {
            mediaLibrary.reset(); if (intf) vlc_object_delete(intf);
            resume.release();
            if (library.instance) { libvlc_release(library.instance); library.instance=nullptr; }
            if (passive) vlc_ml_work_Release(passive);
        });
        QVERIFY(passive);
        QVERIFY(mediaLibrary->runOnMLThread(&receiver,[&](vlc_medialibrary_t*) { started.release(); resume.acquire(); nativeDone=true; },
            [&] { ++uiCalls; },"addon-lease-shutdown")>0);
        QVERIFY(started.tryAcquire(1,5000));
        QVERIFY(mediaLibrary->runOnMLThread(&receiver,[&](vlc_medialibrary_t*) { ++queuedCalls; },
            [&] { ++uiCalls; },"addon-lease-shutdown")>0);
        mediaLibrary.reset(); QVERIFY(!nativeDone.load()); vlc_object_delete(intf); intf=nullptr;
        auto* instance=library.instance; library.instance=nullptr;
        std::thread cleanup([&] { libvlc_release(instance); cleanupDone=true; });
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        bool retired=false;
        while (std::chrono::steady_clock::now()<deadline) {
            auto* probe=vlc_ml_work_Acquire(ml);
            if (!probe) { retired=true; break; }
            vlc_ml_work_Release(probe); std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const bool waited=!cleanupDone.load();
        std::thread release([&] { resume.release(); }); release.join(); cleanup.join();
        QVERIFY(retired); QVERIFY(waited); QVERIFY(nativeDone.load()); QCOMPARE(queuedCalls.load(),0); QCOMPARE(uiCalls,0);
        QVERIFY(vlc_ml_work_Begin(passive)==nullptr); vlc_ml_work_Release(passive); passive=nullptr;
        QCoreApplication::processEvents(); QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QCOMPARE(uiCalls,0);
    }
    void actualMediaLibDispatcherCommitsTheDiscoverySelection() {
        Library library; QVERIFY(library.available()); const auto id=library.playlist(); Source source; QVERIFY(source.start());
        auto* intf=vlc_object_create<qt_intf_t>(library.instance->p_libvlc_int); QVERIFY(intf);
        const auto cleanup=qScopeGuard([&] { vlc_object_delete(intf); });
        MediaLib mediaLibrary(intf,nullptr); const auto dispatcher=AddonNativeLibraryStore::dispatcherFor(&mediaLibrary);
        AddonNativeLibraryStore store(dispatcher,source.admissions(),&source.manager); AddonNativeLibraryTargets picker(dispatcher,&store);
        AddonDiscoveryCoordinator::Backend provider;
        provider.sources=[&] { return source.manager.contributionsOfType(QStringLiteral("discovery.source")); };
        AddonDiscoveryService* current=nullptr;
        provider.invoke=[&](const QString& contribution,const QString&,const QVariantMap&) {
            QTimer::singleShot(0,current,[&,contribution] { current->operationFinished("roots-job",contribution,"match",
                {{"items",QVariantList{QVariantMap{{"id","station"},{"kind","media"},{"title","Catalog title"},{"mediaKind","audio"}}}}},{}); });
            return QStringLiteral("roots-job");
        };
        AddonQueueTransactions::Backend transactions; auto native=combineDiscoveryBackends({},store.backend());
        transactions.snapshot=native.snapshot; transactions.append=native.append;
        AddonDiscoveryService discovery(provider,transactions); current=&discovery; discovery.setPlaylistBackend(picker.backend());
        const auto view=discovery.openSource(QString(source.id+QStringLiteral("/source"))); QVERIFY(!view.isEmpty());
        QTRY_VERIFY(!discovery.view(view).value("items").toList().isEmpty());
        QTRY_VERIFY(!discovery.view(view).value("playlists").toList().isEmpty());
        const auto choice=discovery.view(view).value("playlists").toList().first().toMap().value("ref").toString();
        QVERIFY(discovery.choosePlaylist(view,choice)); const auto target=discovery.view(view).value("playlistTarget").toString();
        const auto item=discovery.view(view).value("items").toList().first().toMap().value("ref").toString();
        QSignalSpy finished(&discovery,&AddonDiscoveryService::actionFinished);
        QVERIFY(!discovery.enqueue(view,{item},target).isEmpty()); QCOMPARE(finished.size(),0);
        QTRY_COMPARE(finished.size(),1); QVERIFY2(finished.first()[1].toBool(),qPrintable(finished.first()[2].toString()));
        QCOMPARE(library.members(id).size(),size_t(1)); discovery.closeView(view); picker.close(); store.close();
    }
#endif
    void corePickerExposesOnlyOpaqueChoicesAndConsumesItsSnapshot() {
        Library library; QVERIFY(library.available()); const auto id=library.playlist(); Source source; QVERIFY(source.start()); Dispatcher dispatch(library.ml);
        AddonNativeLibraryStore store(dispatch.backend(),source.admissions(),&source.manager);
        AddonNativeLibraryTargets picker(dispatch.backend(),&store); auto backend=picker.backend(); QVariantList choices; int calls=0;
        backend.list({},[&](bool available,const QVariantList& values,bool more,const QString& error) { ++calls; QVERIFY2(available,qPrintable(error)); QVERIFY(!more); choices=values; });
        QCOMPARE(calls,0); dispatch.run(); QCOMPARE(calls,1); QCOMPARE(choices.size(),1);
        const auto choice=choices.first().toMap(); QCOMPARE(choice.size(),2); QVERIFY(choice.contains("ref")); QVERIFY(choice.contains("name"));
        QVERIFY(choice.value("ref").toString().startsWith("playlist-choice:")); QString target;
        backend.prepare(choice.value("ref").toString(),[&](const QString& value,const QString& error) { QVERIFY2(!value.isEmpty(),qPrintable(error)); target=value; });
        QVERIFY(store.snapshot(target).available); QVERIFY(dispatch.tasks.empty());
        QString reused; backend.prepare(choice.value("ref").toString(),[&](const QString& value,const QString&) { reused=value; }); QVERIFY(reused.isEmpty());
        store.append(transaction(store,target,{source.reference()}),[](bool success,const QString& error) { QVERIFY2(success,qPrintable(error)); });
        dispatch.run(); QCOMPARE(library.members(id).size(),size_t(1));
    }
    void pickerCannotRebindAChoiceAfterNativeIdReuse() {
        Library library; QVERIFY(library.available()); const auto id=library.playlist(); Source source; QVERIFY(source.start()); Dispatcher dispatch(library.ml);
        AddonNativeLibraryStore store(dispatch.backend(),source.admissions(),&source.manager); AddonNativeLibraryTargets picker(dispatch.backend(),&store);
        auto backend=picker.backend(); QString choice,target;
        backend.list({},[&](bool,const QVariantList& values,bool,const QString&) { choice=values.first().toMap().value("ref").toString(); }); dispatch.run();
        QVERIFY(native([&] { return vlc_ml_playlist_delete(library.ml,id)==VLC_SUCCESS; })); const auto replacement=library.playlist();
        QVERIFY(library.sql("UPDATE Playlist SET id_playlist="+std::to_string(id)+" WHERE id_playlist="+std::to_string(replacement)));
        backend.prepare(choice,[&](const QString& value,const QString&) { target=value; }); QVERIFY(!target.isEmpty());
        int calls=0; store.append(transaction(store,target,{source.reference()}),[&](bool success,const QString&) { ++calls; QVERIFY(!success); });
        dispatch.run(); QCOMPARE(calls,1); QVERIFY(library.members(id).empty());
    }
    void pickerCancellationAndCloseDiscardLateListings() {
        Library library; QVERIFY(library.available()); QVERIFY(library.playlist()>0); Source source; QVERIFY(source.start()); Dispatcher dispatch(library.ml);
        AddonNativeLibraryStore store(dispatch.backend(),source.admissions(),&source.manager); auto picker=std::make_unique<AddonNativeLibraryTargets>(dispatch.backend(),&store);
        auto backend=picker->backend(); int calls=0;
        auto cancel=backend.list({},[&](bool,const QVariantList&,bool,const QString&) { ++calls; }); cancel(); dispatch.run(); QCOMPARE(calls,0);
        backend.list({},[&](bool,const QVariantList&,bool,const QString&) { ++calls; }); auto task=dispatch.take();
        picker.reset(); std::thread worker([&] { task.work(library.ml); }); worker.join(); task.done(); QCOMPARE(calls,0);
    }
    void newCatalogTitleIsSanitizedAndExistingManualTitleStays() {
        Library library; QVERIFY(library.available()); const auto id=library.playlist(); Source source; QVERIFY(source.start()); auto admissions=source.admissions(); Dispatcher dispatch(library.ml);
        AddonNativeLibraryStore store(dispatch.backend(),admissions,&source.manager);
        auto reference=source.reference(); reference.insert("snapshot",QVariantMap{{"title","Line\nbreak"},{"mediaKind","audio"}});
        auto target=store.selectPlaylist(id); dispatch.run(); store.append(transaction(store,target,{reference}),[](bool success,const QString& error) { QVERIFY2(success,qPrintable(error)); }); dispatch.run();
        const auto uri=admissions->uriForReference(reference).toUtf8(); int64_t mediaId=0;
        const auto title=native([&] { auto* media=vlc_ml_get_media_by_mrl(library.ml,uri.constData()); if (!media) return QString();
            mediaId=media->i_id; const auto value=QString::fromUtf8(media->psz_title); vlc_ml_media_release(media); return value; });
        QCOMPARE(title,QStringLiteral("Line break")); QVERIFY(mediaId>0);
        QVERIFY(library.sql("UPDATE Media SET title='Manual title' WHERE id_media="+std::to_string(mediaId)));
        reference.insert("snapshot",QVariantMap{{"title","New catalog title"},{"mediaKind","audio"}});
        target=store.selectPlaylist(id); dispatch.run(); store.append(transaction(store,target,{reference}),[](bool success,const QString& error) { QVERIFY2(success,qPrintable(error)); }); dispatch.run();
        QCOMPARE(native([&] { auto* media=vlc_ml_get_media_by_mrl(library.ml,uri.constData()); const auto value=media?QString::fromUtf8(media->psz_title):QString(); if (media) vlc_ml_media_release(media); return value; }),QStringLiteral("Manual title"));
    }
    void asyncTargetAndOneCasGroupPersistExactStableReferences() {
        Library library; QVERIFY(library.available()); const auto id=library.playlist(); QVERIFY(id>0);
        Source source; QVERIFY2(source.start(),qPrintable(source.manager.errorMessage())); auto admissions=source.admissions(); Dispatcher dispatch(library.ml);
        AddonNativeLibraryStore store(dispatch.backend(),admissions,&source.manager); QSignalSpy ready(&store,&AddonNativeLibraryStore::targetReady);
        const auto target=store.selectPlaylist(id); QVERIFY(target.startsWith("library:")); QVERIFY(!store.snapshot(target).available); QCOMPARE(ready.size(),0);
        dispatch.run(); QVERIFY(store.snapshot(target).available); QCOMPARE(ready.size(),1); QCOMPARE(ready.first()[1].toBool(),true);
        QVERIFY(store.metaObject()->indexOfMethod("selectPlaylist(qlonglong)")<0);
        const QVariantList selected{source.reference(),source.reference("item-2"),source.reference()}; int calls=0; bool saved=false;
        store.append(transaction(store,target,selected),[&](bool success,const QString& error) { ++calls; saved=success; QVERIFY2(success,qPrintable(error)); });
        QCOMPARE(calls,0); QVERIFY(!store.snapshot(target).available); dispatch.run(); QCOMPARE(calls,1); QVERIFY(saved);
        QVERIFY(!store.snapshot(target).available); const auto members=library.members(id); QCOMPARE(members.size(),size_t(3));
        QCOMPARE(members[0],admissions->uriForReference(selected[0].toMap()).toStdString()); QCOMPARE(members[2],members[0]);
        QVERIFY(members[0].find("hakone-addon-source:v1:")==0); QVERIFY(members[0].find("https://")==std::string::npos);
    }
    void coldSnapshotStartsTheNativeDatabase() {
        Library library(false); QVERIFY(library.available());
        QVERIFY(native([&] {
            auto* value=vlc_addon_playlist_snapshot_create(library.ml,123456);
            if (value) vlc_addon_playlist_snapshot_destroy(value); return value==nullptr;
        }));
        QVERIFY(library.playlist()>0);
    }
    void cancellationBeforeAdmissionLeavesThePlaylistEmpty() {
        Library library; QVERIFY(library.available()); const auto id=library.playlist(); Source source; QVERIFY(source.start()); Dispatcher dispatch(library.ml);
        AddonNativeLibraryStore store(dispatch.backend(),source.admissions(),&source.manager);
        const auto target=store.selectPlaylist(id); dispatch.run(); int calls=0;
        const auto cancel=store.append(transaction(store,target,{source.reference()}),[&](bool success,const QString&) { ++calls; QVERIFY(!success); });
        QVERIFY(bool(cancel)); cancel(); dispatch.run(); QCOMPARE(calls,1); QVERIFY(library.members(id).empty());
    }
    void nativeWriterChangesAndTargetReuseRejectOldSelections() {
        Library library; QVERIFY(library.available()); const auto id=library.playlist(); Source source; QVERIFY(source.start()); Dispatcher dispatch(library.ml);
        AddonNativeLibraryStore store(dispatch.backend(),source.admissions(),&source.manager);
        const auto target=store.selectPlaylist(id); dispatch.run();
        QVERIFY(native([&] { auto* media=vlc_ml_new_external_media(library.ml,"file:///local-writer.mkv"); if (!media) return false;
            const auto mediaId=media->i_id; vlc_ml_media_release(media); return vlc_ml_playlist_append(library.ml,id,&mediaId,1)==VLC_SUCCESS; }));
        int calls=0; store.append(transaction(store,target,{source.reference()}),[&](bool success,const QString&) { ++calls; QVERIFY(!success); });
        dispatch.run(); QCOMPARE(calls,1); QCOMPARE(library.members(id).size(),size_t(1));
        const auto stale=store.selectPlaylist(id); dispatch.run();
        QVERIFY(native([&] { return vlc_ml_playlist_delete(library.ml,id)==VLC_SUCCESS; }));
        const auto replacement=library.playlist(); QVERIFY(replacement>0);
        QVERIFY(library.sql("UPDATE Playlist SET id_playlist="+std::to_string(id)+" WHERE id_playlist="+std::to_string(replacement)));
        store.append(transaction(store,stale,{source.reference()}),[&](bool success,const QString&) { ++calls; QVERIFY(!success); });
        dispatch.run(); QCOMPARE(calls,2); QVERIFY(library.members(id).empty());
    }
    void externalSettingsChangeNeedsNoQtEventAtTheCommitGuard() {
        Library library; QVERIFY(library.available()); const auto id=library.playlist(); Source source; QVERIFY(source.start()); auto admissions=source.admissions(); Dispatcher dispatch(library.ml);
        AddonNativeLibraryStore store(dispatch.backend(),admissions,&source.manager); const auto target=store.selectPlaylist(id); dispatch.run();
        const auto reference=source.reference(); int calls=0;
        store.append(transaction(store,target,{reference}),[&](bool success,const QString&) { ++calls; QVERIFY(!success); });
        PackageStore packages(source.manager.storageDirectory()); InstalledPackage package; QJsonObject settings; quint64 revision;
        QVERIFY(packages.installed(source.id,&package)); QVERIFY(packages.networkConfiguration(package,&settings,&revision));
        settings.insert("language","fr"); QVERIFY(packages.saveSettings(source.id,settings,revision));
        dispatch.run(); QCOMPARE(calls,1); QVERIFY(library.members(id).empty());
        const auto uri=admissions->uriForReference(reference).toUtf8();
        QVERIFY(native([&] { auto* media=vlc_ml_get_media_by_mrl(library.ml,uri.constData()); if (!media) return false; vlc_ml_media_release(media); return true; }));
        const auto retry=store.selectPlaylist(id); dispatch.run();
        store.append(transaction(store,retry,{reference}),[&](bool success,const QString& error) { ++calls; QVERIFY2(success,qPrintable(error)); });
        dispatch.run(); QCOMPARE(calls,2); QCOMPARE(library.members(id).size(),size_t(1));
    }
    void accountRotationAndTargetReleaseWithdrawPendingWork() {
        Library library; QVERIFY(library.available()); const auto id=library.playlist(); Source source; QVERIFY(source.start()); Dispatcher dispatch(library.ml);
        AddonNativeLibraryStore store(dispatch.backend(),source.admissions(),&source.manager); const auto target=store.selectPlaylist(id); dispatch.run(); int calls=0;
        store.append(transaction(store,target,{source.reference()}),[&](bool success,const QString&) { ++calls; QVERIFY(!success); });
        QVERIFY(source.replace("second-test-key")); dispatch.run(); QCOMPARE(calls,1); QVERIFY(library.members(id).empty());
        const auto next=store.selectPlaylist(id); dispatch.run();
        store.append(transaction(store,next,{source.reference()}),[&](bool success,const QString&) { ++calls; QVERIFY(!success); });
        store.releaseTarget(next); dispatch.run(); QCOMPARE(calls,2); QVERIFY(library.members(id).empty());
    }
    void externalDisableAndSharedStoreLockStopNativeAdmission() {
        Library library; QVERIFY(library.available()); const auto id=library.playlist(); Source source; QVERIFY(source.start()); Dispatcher dispatch(library.ml);
        AddonNativeLibraryStore store(dispatch.backend(),source.admissions(),&source.manager); const auto target=store.selectPlaylist(id); dispatch.run(); int calls=0;
        store.append(transaction(store,target,{source.reference()}),[&](bool success,const QString&) { ++calls; QVERIFY(!success); });
        PackageStore packages(source.manager.storageDirectory()); QVERIFY(packages.setEnabled(source.id,false));
        dispatch.run(); QCOMPARE(calls,1); QVERIFY(library.members(id).empty());
        QVERIFY(packages.setEnabled(source.id,true)); const auto next=store.selectPlaylist(id); dispatch.run();
        store.append(transaction(store,next,{source.reference()}),[&](bool success,const QString& error) { ++calls; QVERIFY(!success); QVERIFY(error.contains("busy")); });
        QLockFile lock(QDir(source.manager.storageDirectory()).filePath("store.lock")); QVERIFY(lock.tryLock(0));
        dispatch.run(); QCOMPARE(calls,2); QVERIFY(library.members(id).empty());
    }
    void cancellationAfterAdmissionDoesNotClaimRollback() {
        Library library; QVERIFY(library.available()); const auto id=library.playlist(); Source source; QVERIFY(source.start()); Dispatcher dispatch(library.ml);
        AddonNativeLibraryStore store(dispatch.backend(),source.admissions(),&source.manager); const auto target=store.selectPlaylist(id); dispatch.run(); int calls=0;
        const auto cancel=store.append(transaction(store,target,{source.reference()}),[&](bool success,const QString& error) { ++calls; QVERIFY2(success,qPrintable(error)); });
        auto task=dispatch.take(); std::thread worker([&] { task.work(library.ml); }); worker.join();
        cancel(); task.done(); QCOMPARE(calls,1); QCOMPARE(library.members(id).size(),size_t(1));
    }
    void anotherLibraryOwnerAndReadOnlyTargetsAreRefused() {
        Library first; QVERIFY(first.available()); const auto id=first.playlist(); Source source; QVERIFY(source.start()); Dispatcher dispatch(first.ml);
        AddonNativeLibraryStore store(dispatch.backend(),source.admissions(),&source.manager); const auto target=store.selectPlaylist(id); dispatch.run();
        Library second; QVERIFY(second.available()); QCOMPARE(second.playlist(),id); dispatch.ml=second.ml; int calls=0;
        store.append(transaction(store,target,{source.reference()}),[&](bool success,const QString&) { ++calls; QVERIFY(!success); });
        dispatch.run(); QCOMPARE(calls,1); QVERIFY(second.members(id).empty()); QVERIFY(first.members(id).empty());
        QVERIFY(native([&] { auto* media=vlc_ml_new_external_media(second.ml,"file:///read-only.m3u"); if (!media) return false; vlc_ml_media_release(media); return true; }));
        QVERIFY(second.sql("UPDATE File SET playlist_id="+std::to_string(id)+" WHERE mrl='file:///read-only.m3u'"));
        QSignalSpy ready(&store,&AddonNativeLibraryStore::targetReady); const auto readonly=store.selectPlaylist(id); dispatch.run();
        QVERIFY(!store.snapshot(readonly).available); QCOMPARE(ready.last()[1].toBool(),false);
    }
    void closeAndLateCompletionNeedNoUiWait() {
        auto library=std::make_unique<Library>(); QVERIFY(library->available()); const auto id=library->playlist(); Source source; QVERIFY(source.start()); Dispatcher dispatch(library->ml);
        auto store=std::make_unique<AddonNativeLibraryStore>(dispatch.backend(),source.admissions(),&source.manager);
        QVERIFY(!store->selectPlaylist(id).isEmpty()); auto task=dispatch.take();
        std::thread worker([&] { task.work(library->ml); }); worker.join();
        store.reset(); library.reset(); task.done();
        // Destruction of this task also frees the flat snapshot after shutdown.
        task={};
    }
    void dispatcherReentryCancelsNewlyQueuedWork() {
        Library library; QVERIFY(library.available()); const auto id=library.playlist(); Source source; QVERIFY(source.start()); Dispatcher dispatch(library.ml);
        std::unique_ptr<AddonNativeLibraryStore> store; int cancellations=0; quint64 submitted=0;
        auto backend=dispatch.backend(); const auto run=backend.run; const auto cancel=backend.cancel;
        backend.run=[&](const QObject* receiver,auto work,auto done) {
            const auto task=run(receiver,std::move(work),std::move(done)); submitted=task; store.reset(); return task;
        };
        backend.cancel=[&](const QObject* receiver,quint64 task) { ++cancellations; cancel(receiver,task); };
        store=std::make_unique<AddonNativeLibraryStore>(backend,source.admissions(),&source.manager);
        QPointer<AddonNativeLibraryStore> alive=store.get();
        QVERIFY(!store->selectPlaylist(id).isEmpty()); QVERIFY(!store); QVERIFY(!alive); QCOMPARE(submitted,quint64(1)); QCOMPARE(cancellations,1); QVERIFY(dispatch.tasks.empty());

        bool destroy=false; backend.run=[&](const QObject* receiver,auto work,auto done) {
            const auto task=run(receiver,std::move(work),std::move(done)); if (destroy) store.reset(); return task;
        };
        store=std::make_unique<AddonNativeLibraryStore>(backend,source.admissions(),&source.manager);
        const auto target=store->selectPlaylist(id); dispatch.run(); destroy=true; int completions=0;
        store->append(transaction(*store,target,{source.reference()}),[&](bool,const QString&) { ++completions; });
        QVERIFY(!store); QCOMPARE(cancellations,2); QVERIFY(dispatch.tasks.empty()); QCOMPARE(completions,0); QVERIFY(library.members(id).empty());

        backend.run=[&](const QObject* receiver,auto work,auto done) {
            const auto task=run(receiver,std::move(work),std::move(done)); store->close(); return task;
        };
        store=std::make_unique<AddonNativeLibraryStore>(backend,source.admissions(),&source.manager);
        QVERIFY(!store->selectPlaylist(id).isEmpty()); QVERIFY(store); QCOMPARE(cancellations,3); QVERIFY(dispatch.tasks.empty());
    }
    void pendingAndTargetLimitsStayBounded() {
        Library library; QVERIFY(library.available()); const auto id=library.playlist(); Source source; QVERIFY(source.start()); Dispatcher dispatch(library.ml);
        AddonNativeLibraryStore store(dispatch.backend(),source.admissions(),&source.manager); QStringList targets;
        for (int i=0;i<4;++i) { const auto ref=store.selectPlaylist(id); QVERIFY(!ref.isEmpty()); targets.append(ref); }
        QVERIFY(store.selectPlaylist(id).isEmpty()); for (const auto& ref:targets) store.releaseTarget(ref);
        QVERIFY(store.selectPlaylist(id).isEmpty()); while (!dispatch.tasks.empty()) dispatch.run();
        for (int i=0;i<16;++i) { QVERIFY(!store.selectPlaylist(id).isEmpty()); dispatch.run(); }
        QVERIFY(store.selectPlaylist(id).isEmpty()); store.close(); QVERIFY(store.selectPlaylist(id).isEmpty());
    }
    void noNativeCasKeepsTheTargetUnavailable() {
        Source source; QVERIFY(source.start()); Dispatcher dispatch(nullptr);
        AddonNativeLibraryStore store(dispatch.backend(),source.admissions(),&source.manager); QSignalSpy ready(&store,&AddonNativeLibraryStore::targetReady);
        const auto ref=store.selectPlaylist(1); QVERIFY(!ref.isEmpty()); dispatch.run();
        QVERIFY(!store.snapshot(ref).available); QCOMPARE(ready.size(),1); QVERIFY(!ready.first()[1].toBool());
    }
};
QTEST_GUILESS_MAIN(TestAddonNativeLibraryStore)
#include "test_addon_native_library_store.moc"
