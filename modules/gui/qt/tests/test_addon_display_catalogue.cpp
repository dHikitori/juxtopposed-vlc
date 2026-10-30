// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_display_catalogue_reader.hpp"
#include "addons/addon_catalogue_view.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QTemporaryDir>
#include <QTest>
#include <QScopeGuard>
#include <QThread>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#ifdef HAKONE_DISPLAY_READER_LOCK_WRAP
#include <fcntl.h>
#include <cstdarg>
namespace {
struct DisplayReadBarrier {
    std::mutex mutex;
    std::condition_variable changed;
    std::atomic_bool entered{false};
    bool released=false;
    void wait() {
        std::unique_lock<std::mutex> lock(mutex);
        entered.store(true,std::memory_order_release);
        changed.wait(lock,[this]{return released;});
    }
    void release() {
        {std::lock_guard<std::mutex> lock(mutex);released=true;}
        changed.notify_all();
    }
};
struct DisplayOpenBarrier {
    std::string path;
    std::shared_ptr<DisplayReadBarrier> hold=std::make_shared<DisplayReadBarrier>();
    std::atomic_bool armed{true};
    std::atomic_uint held{0};
};
std::mutex displayOpenMutex;
std::shared_ptr<DisplayOpenBarrier> displayOpenBarrier;
}
// With _FILE_OFFSET_BITS=64, glibc headers turn open() into open64().
extern "C" int __real_open(const char*,int,...);
extern "C" int __real_open64(const char*,int,...);
static void holdDisplayOpen(const char* path)
{
    std::shared_ptr<DisplayOpenBarrier> barrier;
    {std::lock_guard<std::mutex> lock(displayOpenMutex);barrier=displayOpenBarrier;}
    if(barrier && path && barrier->path==path && barrier->armed.exchange(false)) {
        ++barrier->held;
        barrier->hold->wait();
    }
}
extern "C" int __wrap_open(const char* path,int flags,...)
{
    holdDisplayOpen(path);
    if(flags&O_CREAT) {
        va_list arguments;va_start(arguments,flags);const auto mode=va_arg(arguments,int);va_end(arguments);
        return __real_open(path,flags,mode);
    }
    return __real_open(path,flags);
}
extern "C" int __wrap_open64(const char* path,int flags,...)
{
    holdDisplayOpen(path);
    if(flags&O_CREAT) {
        va_list arguments;va_start(arguments,flags);const auto mode=va_arg(arguments,int);va_end(arguments);
        return __real_open64(path,flags,mode);
    }
    return __real_open64(path,flags);
}
#endif
using namespace vlc::addons;
namespace {
struct Scope { AddonStreamLifetime owner;~Scope(){owner.close();owner.handle().waitQt(std::chrono::seconds(5));} };
bool write(const QString& path,const QByteArray& bytes){if(!QDir().mkpath(QFileInfo(path).absolutePath()))return false;QFile file(path);if(file.exists())file.setPermissions(QFile::ReadOwner|QFile::WriteOwner);return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size();}
bool document(const QString& path,const QJsonObject& value){return write(path,QJsonDocument(value).toJson(QJsonDocument::Compact));}
QJsonObject read(const QString& path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};return QJsonDocument::fromJson(file.readAll()).object();}
QString statePath(const QString& root,const QString& id){return QDir(root).filePath(QStringLiteral("packages/")+id+QStringLiteral("/state.json"));}
bool candidate(const QString& directory,const QString& id,PackageCandidate* output,int count=2,int payload=8)
{
    QJsonArray declarations,files{QStringLiteral("addon.json"),QStringLiteral("LICENSE"),QStringLiteral("settings.json"),QStringLiteral("worker.wasm"),QStringLiteral("locale.json"),QStringLiteral("settings-ui.json")};
    for(int i=0;i<count;++i){const QString path=QStringLiteral("view%1.json").arg(i);files.append(path);declarations.append(QJsonObject{{"id",QStringLiteral("section%1").arg(i)},{"type","ui.section"},{"contract","ui@1"},{"view",path},{"features",QJsonArray{QStringLiteral("subtitles")}}});if(!document(QDir(directory).filePath(path),{{"text",QStringLiteral("section%1").arg(i)}}))return false;}
    files.append(QStringLiteral("theme.json"));declarations.append(QJsonObject{{"id","theme"},{"type","theme"},{"contract","theme@1"},{"theme","theme.json"}});
    const QJsonObject manifest{{"formatVersion",1},{"id",id},{"name","Display package"},{"version","1.0.0"},{"publisher","Tests"},{"license","GPL-2.0-or-later"},{"source","https://github.com/hakone-org/display-fixture"},
        {"api",QJsonObject{{"addon",">=1.0.0 <2.0.0"},{"contracts",QJsonObject{{"ui","^1.0.0"},{"theme","^1.0.0"},{"settings","^1.0.0"}}}}},
        {"runtime",QJsonObject{{"kind","wasm-message-v1"},{"entry","worker.wasm"}}},{"files",files},{"contributions",declarations},
        {"locales",QJsonObject{{"default","en"},{"files",QJsonObject{{"en","locale.json"}}}}},
        {"settings",QJsonObject{{"version",1},{"schema","settings.json"},{"ui","settings-ui.json"},{"accountFields",QJsonArray{QStringLiteral("account")}}}}};
    const QJsonObject schema{{"type","object"},{"properties",QJsonObject{{"account",QJsonObject{{"type","string"}}},{"apiKey",QJsonObject{{"type","string"},{"writeOnly",true}}}}},{"required",QJsonArray{QStringLiteral("account")}},{"additionalProperties",false}};
    QByteArray wasm(std::max(8,payload),'\0');wasm.replace(0,8,QByteArray::fromHex("0061736d01000000"));
    if(!document(QDir(directory).filePath(QStringLiteral("addon.json")),manifest)||!document(QDir(directory).filePath(QStringLiteral("settings.json")),schema)
       ||!document(QDir(directory).filePath(QStringLiteral("settings-ui.json")),{{"fields",QJsonArray{}}})||!document(QDir(directory).filePath(QStringLiteral("theme.json")),{{"contract","theme@1"},{"id","display"},{"variants",QJsonObject{{"dark",QJsonObject{}}}}})
       ||!document(QDir(directory).filePath(QStringLiteral("locale.json")),{{"save","Save settings"}})||!write(QDir(directory).filePath(QStringLiteral("LICENSE")),"Fixture\n")||!write(QDir(directory).filePath(QStringLiteral("worker.wasm")),wasm))return false;
    ReleaseIdentity release;release.source=QStringLiteral("https://github.com/hakone-org/display-fixture");release.tag=QStringLiteral("v1.0.0");release.tagObjectId=release.commitId=QString(40,QLatin1Char('a'));
    return PackageStore::inspectDirectory(directory,release,output);
}
bool install(PackageStore& store,const QString& id,int count=2,int payload=8){QTemporaryDir source;PackageCandidate value;return candidate(source.path(),id,&value,count,payload)&&store.install(value,{});}
}
class DisplayCatalogueTest final:public QObject {
    Q_OBJECT
private slots:
#ifdef HAKONE_DISPLAY_READER_LOCK_WRAP
    void actualDisplayReaderUnlockRetriesAContendingView_data() {
        QTest::addColumn<bool>("cancelHolder");
        QTest::newRow("completed-reader")<<false;
        QTest::newRow("canceled-reader")<<true;
    }
    void actualDisplayReaderUnlockRetriesAContendingView() {
        QFETCH(bool,cancelHolder);
        Scope scope;QTemporaryDir root;PackageStore store(root.path());
        QVERIFY(install(store,QStringLiteral("org.example.display"),1));
        QVERIFY(document(root.filePath(QStringLiteral("recovery.json")),{{"safeMode",false}}));
        const auto authority=store.authorityToken();
        const auto barrier=std::make_shared<DisplayOpenBarrier>();
        barrier->path=QFile::encodeName(root.filePath(QStringLiteral("recovery.json"))).toStdString();
        {std::lock_guard<std::mutex> lock(displayOpenMutex);displayOpenBarrier=barrier;}
        const auto release=qScopeGuard([&]{
            barrier->hold->release();
            std::lock_guard<std::mutex> lock(displayOpenMutex);displayOpenBarrier.reset();
        });
        struct HolderResult {bool success=false;QString error;};
        const auto held=std::make_shared<HolderResult>();
        AddonRepositoryJobs holder(scope.owner.handle());
        bool completed=false;
        auto state=AddonRepositoryJobs::Result::Failed;
        const auto reader=store.displayCatalogueReader(QStringLiteral("en"));
        QVERIFY(holder.submit([reader,held]{DisplayCatalogueSnapshot snapshot;held->success=reader({},&snapshot,&held->error);},
            [&](auto result){state=result;completed=true;}));
        QTRY_VERIFY(barrier->hold->entered.load(std::memory_order_acquire));
        AddonCatalogueView view({[store]()->std::optional<DisplayCatalogueSource>{
            DisplayCatalogueSource source;source.queuedAuthority=store.authorityToken();
            source.reader=store.displayCatalogueReader(QStringLiteral("en"));return source;
        },[store](auto wake){return store.onDisplayChanged(std::move(wake));}},scope.owner.handle());
        QTRY_VERIFY(!view.loading()&&!view.error().isEmpty());
        QCOMPARE(view.captures(),1u);QVERIFY(!view.isCurrent());QVERIFY(authority.current());
        if(cancelHolder)holder.cancel();
        barrier->hold->release();
        QTRY_VERIFY(completed);
        QCOMPARE(state,cancelHolder?AddonRepositoryJobs::Result::Canceled:AddonRepositoryJobs::Result::Finished);
        QCOMPARE(held->success,!cancelHolder);
        QTRY_VERIFY_WITH_TIMEOUT(view.isCurrent(),3000);
        QCOMPARE(view.captures(),2u);QCOMPARE(view.publications(),1u);QCOMPARE(view.rows().size(),1);
        QCOMPARE(barrier->held.load(),1u);QVERIFY(authority.current());
        QTRY_COMPARE(AddonRepositoryJobs::processCount(),0u);
        QTest::qWait(20);
        const auto captures=view.captures(),wakes=view.nativeWakes(),publications=view.publications();
        QTest::qWait(80);
        QCOMPARE(view.captures(),captures);QCOMPARE(view.nativeWakes(),wakes);QCOMPARE(view.publications(),publications);
    }
    void actualUnlockRetriesSettingsWithoutInvalidatingCurrentRows_data() {
        QTest::addColumn<bool>("unlockBeforeCompletion");
        QTest::newRow("unlock-after-refused-completion")<<false;
        QTest::newRow("unlock-before-refused-completion")<<true;
    }
    void actualUnlockRetriesSettingsWithoutInvalidatingCurrentRows() {
        QFETCH(bool,unlockBeforeCompletion);
        Scope scope;QTemporaryDir root;PackageStore store(root.path());
        const QString id=QStringLiteral("org.example.display");QVERIFY(install(store,id,1));
        QVERIFY(document(root.filePath(QStringLiteral("recovery.json")),{{"safeMode",false}}));
        const auto authority=store.authorityToken();
        const auto refused=std::make_shared<DisplayReadBarrier>();
        const auto releaseRefused=qScopeGuard([&]{refused->release();});
        AddonCatalogueView view({[store,refused,unlockBeforeCompletion]()->std::optional<DisplayCatalogueSource>{
            DisplayCatalogueSource source;source.queuedAuthority=store.authorityToken();
            const auto reader=store.displayCatalogueReader(QStringLiteral("en"));
            source.reader=[reader,refused,unlockBeforeCompletion](const auto& request,auto* output,auto* error){
                const bool success=reader(request,output,error);
                if(!success&&unlockBeforeCompletion&&output->refusal==DisplayCatalogueSnapshot::Refusal::StoreBusy)refused->wait();
                return success;
            };
            return source;
        },[store](auto wake){return store.onDisplayChanged(std::move(wake));}},scope.owner.handle());
        QTRY_VERIFY(view.isCurrent());QTRY_COMPARE(AddonRepositoryJobs::processCount(),0u);QTest::qWait(20);
        const auto rows=view.rows();bool baseWasInvalidated=false;
        connect(&view,&AddonCatalogueView::freshnessChanged,&view,[&]{if(!view.isCurrent())baseWasInvalidated=true;});
        const auto closeBeforeLocalState=qScopeGuard([&view]{view.close();});
        const auto barrier=std::make_shared<DisplayOpenBarrier>();
        barrier->path=QFile::encodeName(root.filePath(QStringLiteral("recovery.json"))).toStdString();
        {std::lock_guard<std::mutex> lock(displayOpenMutex);displayOpenBarrier=barrier;}
        const auto releaseHolder=qScopeGuard([&]{
            barrier->hold->release();
            std::lock_guard<std::mutex> lock(displayOpenMutex);displayOpenBarrier.reset();
        });
        AddonRepositoryJobs holder(scope.owner.handle());const auto reader=store.displayCatalogueReader(QStringLiteral("en"));
        QVERIFY(holder.submit([reader]{DisplayCatalogueSnapshot snapshot;reader({},&snapshot,nullptr);},[](auto){}));
        QTRY_VERIFY(barrier->hold->entered.load(std::memory_order_acquire));
        const auto ticket=view.acquireSettings(id);QVERIFY(!ticket.isEmpty());
        if(unlockBeforeCompletion)QTRY_VERIFY(refused->entered.load(std::memory_order_acquire));
        else QTRY_VERIFY(!view.error().isEmpty());
        QVERIFY(view.isCurrent());QCOMPARE(view.settingsState(id),QStringLiteral("pending"));QCOMPARE(view.rows(),rows);
        const auto wakes=view.nativeWakes();barrier->hold->release();
        if(unlockBeforeCompletion){QTRY_VERIFY(view.nativeWakes()>wakes);refused->release();}
        QTRY_COMPARE_WITH_TIMEOUT(view.settingsState(id),QStringLiteral("ready"),3000);
        QVERIFY(view.isCurrent());QVERIFY(!baseWasInvalidated);QCOMPARE(view.rows(),rows);QVERIFY(authority.current());
        QCOMPARE(view.captures(),3u);QCOMPARE(barrier->held.load(),1u);
        view.release(ticket);view.close();
    }
    void successfulReadUnlockIsQuietAndDoesNotAdvanceAuthority() {
        Scope scope;QTemporaryDir root;PackageStore store(root.path());
        QVERIFY(document(root.filePath(QStringLiteral("recovery.json")),{{"safeMode",false}}));
        const auto before=store.authorityToken();
        const auto notices=std::make_shared<std::atomic_uint>(0),revocations=std::make_shared<std::atomic_uint>(0);
        const auto changed=store.onDisplayChanged([notices]{++*notices;});
        const auto revoked=before.onInvalidated([revocations]{++*revocations;});
        QVERIFY(changed);QVERIFY(revoked);
        AddonCatalogueView view({[store]()->std::optional<DisplayCatalogueSource>{
            DisplayCatalogueSource source;source.queuedAuthority=store.authorityToken();
            source.reader=store.displayCatalogueReader(QStringLiteral("en"));return source;
        },[store](auto wake){return store.onDisplayChanged(std::move(wake));}},scope.owner.handle());
        QTRY_VERIFY(view.isCurrent());QTRY_VERIFY(notices->load()>0);
        QTRY_COMPARE(AddonRepositoryJobs::processCount(),0u);
        QCOMPARE(view.captures(),1u);QCOMPARE(view.publications(),1u);
        QVERIFY(before.current());QCOMPARE(revocations->load(),0u);
        QTest::qWait(20);
        const auto captures=view.captures(),wakes=view.nativeWakes(),publications=view.publications();
        const auto notificationCount=notices->load();QTest::qWait(80);
        QCOMPARE(view.captures(),captures);QCOMPARE(view.nativeWakes(),wakes);QCOMPARE(view.publications(),publications);
        QCOMPARE(notices->load(),notificationCount);QVERIFY(before.current());QCOMPARE(revocations->load(),0u);
    }
    void fatalReadDoesNotRetryItsOwnUnlockAndExplicitRefreshCanRecover() {
        Scope scope;QTemporaryDir root;PackageStore store(root.path());
        QVERIFY(document(root.filePath(QStringLiteral("recovery.json")),{{"safeMode",QStringLiteral("invalid")}}));
        const auto before=store.authorityToken();
        const auto failedTail=std::make_shared<DisplayReadBarrier>();
        const auto hold=std::make_shared<std::atomic_bool>(true);
        const auto release=qScopeGuard([&]{failedTail->release();});
        AddonCatalogueView view({[store,failedTail,hold]()->std::optional<DisplayCatalogueSource>{
            DisplayCatalogueSource source;source.queuedAuthority=store.authorityToken();
            const auto reader=store.displayCatalogueReader(QStringLiteral("en"));
            source.reader=[reader,failedTail,hold](const auto& request,auto* output,auto* error){
                const bool success=reader(request,output,error);
                if(!success&&hold->exchange(false))failedTail->wait();
                return success;
            };
            return source;
        },[store](auto wake){return store.onDisplayChanged(std::move(wake));}},scope.owner.handle());
        QTRY_VERIFY(failedTail->entered.load(std::memory_order_acquire));
        // Force the actual reader's own post-unlock notice to arrive before
        // the failed owner completion, rather than relying on event order.
        QTRY_VERIFY(view.nativeWakes()>0);QCOMPARE(view.captures(),1u);
        failedTail->release();
        QTRY_VERIFY_WITH_TIMEOUT(!view.loading()&&!view.error().isEmpty(),3000);
        QTRY_COMPARE(AddonRepositoryJobs::processCount(),0u);
        QTest::qWait(80);QCOMPARE(view.captures(),1u);QCOMPARE(view.publications(),0u);QVERIFY(before.current());
        // Repair raw fixture bytes without changing its authority. A plain
        // unlock notification alone is insufficient; explicit refresh works.
        QVERIFY(document(root.filePath(QStringLiteral("recovery.json")),{{"safeMode",false}}));
        view.refresh();QTRY_VERIFY(view.isCurrent());QCOMPARE(view.captures(),2u);QVERIFY(before.current());
        // A genuine Store mutation must still recover a same-input view.
        QVERIFY(store.saveProviderOrder(QStringLiteral("subtitles"),{}));
        QTRY_VERIFY(view.isCurrent());QVERIFY(!before.current());
    }
#endif
    void memoryFactoryDoesNotCreateStoreAndPrequeueTokenCannotRebase(){
        QTemporaryDir root;PackageStore store(root.filePath(QStringLiteral("new-store")));const auto stale=store.displayCatalogueReader(QStringLiteral("en"));QVERIFY(!QFileInfo::exists(store.storageDirectory()));
        QVERIFY(store.saveProviderOrder(QStringLiteral("subtitles"),{QStringLiteral("org.example.display/section0")}));DisplayCatalogueSnapshot out;QString error;QVERIFY(!stale({},&out,&error));QVERIFY(out.packages.isEmpty());QVERIFY(!out.current());
        QVERIFY(store.displayCatalogueReader(QStringLiteral("en"))({},&out,&error));QVERIFY(out.current());QCOMPARE(out.preferences.value(QStringLiteral("subtitles")).toArray(),QJsonArray{QStringLiteral("org.example.display/section0")});
    }
    void unusedDocumentsAndWasmAreNotRetained(){
        QTemporaryDir root;PackageStore store(root.path());QVERIFY(install(store,QStringLiteral("org.example.display"),40,2*1024*1024));DisplayCatalogueSnapshot out;
        QVERIFY(store.displayCatalogueReader(QStringLiteral("en"))({},&out,nullptr));QCOMPARE(out.packages.size(),1);QCOMPARE(out.packages.first().package.manifest.contributions.size(),41);QVERIFY(out.packages.first().resources.isEmpty());QVERIFY(out.packages.first().messages.isEmpty());QCOMPARE(out.themes.size(),1);QVERIFY(out.retainedDescriptorBytes<64*1024);QVERIFY(out.retainedResourceBytes<128);
        QVERIFY(!out.packages.first().savedEnabled);QVERIFY(!out.packages.first().package.enabled);QVERIFY(out.packages.first().settings.isEmpty());
    }
    void oneRequestedDocumentUsesDeclaredResourceAndLocaleOnly(){
        QTemporaryDir root;PackageStore store(root.path());const QString id=QStringLiteral("org.example.display");QVERIFY(install(store,id,12));DisplayCatalogueSnapshot out;
        DisplayCatalogueRequest request;request.documents={id+QStringLiteral("/section3/view")};QVERIFY(store.displayCatalogueReader(QStringLiteral("en-AU"))(request,&out,nullptr));
        const auto& row=out.packages.first();QCOMPARE(row.resources.size(),1);QCOMPARE(row.resources.value(QStringLiteral("section3/view")).value(QStringLiteral("text")).toString(),QStringLiteral("section3"));QCOMPARE(row.messages.value(QStringLiteral("save")).toString(),QStringLiteral("Save settings"));
        QVERIFY(!row.resources.contains(QStringLiteral("section4/view")));QVERIFY(!row.resources.contains(QStringLiteral("@settingsUi")));
        request.documents={QStringLiteral("../../outside/view")};QVERIFY(store.displayCatalogueReader(QStringLiteral("en"))(request,&out,nullptr));QVERIFY(out.packages.first().resources.isEmpty());
    }
    void selectedThemeAndBoundedAppearancePreferencesAreJoinedInputs(){
        QTemporaryDir root;PackageStore store(root.path());const QString id=QStringLiteral("org.example.display");QVERIFY(install(store,id));
        QVERIFY(document(root.filePath(QStringLiteral("theme-selection.json")),{{"selectedTheme",QString(id+QStringLiteral("/theme"))},{"highContrast",true},{"reducedMotion",true}}));
        QVERIFY(document(root.filePath(QStringLiteral("ui-replacements.json")),{{"home.content@1",QString(id+QStringLiteral("/section0"))}}));QVERIFY(document(root.filePath(QStringLiteral("visualizers.json")),{{"selected","org.example.visualizer/scene"}}));
        DisplayCatalogueSnapshot out;QVERIFY(store.displayCatalogueReader(QStringLiteral("en"))({},&out,nullptr));QCOMPARE(out.selectedTheme,id+QStringLiteral("/theme"));QCOMPARE(out.packages.first().resources.size(),1);QVERIFY(out.packages.first().resources.contains(QStringLiteral("theme/theme")));QVERIFY(out.packages.first().messages.isEmpty());
        QVERIFY(out.displayPreferences.value(QStringLiteral("appearance")).toObject().value(QStringLiteral("highContrast")).toBool());QCOMPARE(out.displayPreferences.value(QStringLiteral("uiReplacements")).toObject().value(QStringLiteral("home.content@1")).toString(),id+QStringLiteral("/section0"));
    }
    void explicitSettingsRequestIncludesSchemaFormAndMessages(){
        QTemporaryDir root;PackageStore store(root.path());const QString id=QStringLiteral("org.example.display");QVERIFY(install(store,id));QVERIFY(store.saveSettings(id,{{"account","Public account"}},0));
        DisplayCatalogueRequest request;request.settingsPackages={id};DisplayCatalogueSnapshot out;QVERIFY(store.displayCatalogueReader(QStringLiteral("en"))(request,&out,nullptr));
        const auto& row=out.packages.first();QVERIFY(row.resources.contains(QStringLiteral("@settingsUi")));QCOMPARE(row.settings.value(QStringLiteral("account")).toString(),QStringLiteral("Public account"));QVERIFY(!row.schema.isEmpty());QVERIFY(!row.messages.isEmpty());
    }
    void corruptSiblingIsVisibleButStrictCatalogueStillPoisonsWholeCapture(){
        QTemporaryDir root;PackageStore store(root.path());QVERIFY(install(store,QStringLiteral("org.example.bad")));QVERIFY(install(store,QStringLiteral("org.example.good")));
        QVERIFY(write(statePath(root.path(),QStringLiteral("org.example.bad")),"{broken"));store.invalidateAuthority();DisplayCatalogueSnapshot display;MetadataCatalogueSnapshot strict;
        QVERIFY(store.displayCatalogueReader(QStringLiteral("en"))({},&display,nullptr));QCOMPARE(display.packages.size(),2);QVERIFY(display.packages[0].corrupt);QVERIFY(display.packages[0].package.manifest.contributions.isEmpty());QVERIFY(!display.packages[1].corrupt);
        QVERIFY(!store.metadataCatalogueReader()(&strict,nullptr));QVERIFY(strict.packages.isEmpty());QVERIFY(strict.digest.isEmpty());
    }
    void corruptSecretStateBecomesRecoveryWithoutPublishingItsValue(){
        QTemporaryDir root;PackageStore store(root.path());const QString id=QStringLiteral("org.example.display");QVERIFY(install(store,id));auto state=read(statePath(root.path(),id));auto current=state.value(QStringLiteral("current")).toObject();current.insert(QStringLiteral("settings"),QJsonObject{{"apiKey","DO-NOT-RETAIN"}});state.insert(QStringLiteral("current"),current);QVERIFY(document(statePath(root.path(),id),state));store.invalidateAuthority();
        DisplayCatalogueSnapshot out;QVERIFY(store.displayCatalogueReader(QStringLiteral("en"))({},&out,nullptr));QVERIFY(out.packages.first().corrupt);QVERIFY(out.packages.first().settings.isEmpty());QVERIFY(out.packages.first().schema.isEmpty());QVERIFY(out.packages.first().package.manifest.contributions.isEmpty());QVERIFY(!out.packages.first().error.contains(QStringLiteral("DO-NOT-RETAIN")));
    }
    void rootPolicyFailureAndBusyLockRefuseWholeDisplayCapture(){
        QTemporaryDir root;PackageStore store(root.path());QVERIFY(install(store,QStringLiteral("org.example.display")));DisplayCatalogueSnapshot out;const auto reader=store.displayCatalogueReader(QStringLiteral("en"));
        QLockFile lock(root.filePath(QStringLiteral("store.lock")));QVERIFY(lock.tryLock(0));QVERIFY(!reader({},&out,nullptr));QVERIFY(out.packages.isEmpty());lock.unlock();QVERIFY(reader({},&out,nullptr));
        QVERIFY(document(root.filePath(QStringLiteral("recovery.json")),{{"safeMode","false"}}));store.invalidateAuthority();QVERIFY(!store.displayCatalogueReader(QStringLiteral("en"))({},&out,nullptr));QVERIFY(out.packages.isEmpty());QVERIFY(!out.current());
    }
    void safeModePreservesRawDisabledAndUnconfiguredDisplayRows(){
        QTemporaryDir root;PackageStore store(root.path());const QString id=QStringLiteral("org.example.display");QVERIFY(install(store,id));QVERIFY(store.setEnabled(id,true));QVERIFY(store.setSafeMode(true));DisplayCatalogueSnapshot out;QVERIFY(store.displayCatalogueReader(QStringLiteral("en"))({},&out,nullptr));
        QVERIFY(out.safeMode);QVERIFY(out.packages.first().savedEnabled);QVERIFY(!out.packages.first().package.enabled);QCOMPARE(out.packages.first().package.manifest.contributions.size(),3);
    }
    void declaredLocaleCompletionGatesUiAndSettingsReadiness_data(){
        QTest::addColumn<int>("mode");QTest::newRow("none")<<0;QTest::newRow("empty-object")<<1;QTest::newRow("array")<<2;QTest::newRow("oversize")<<3;
    }
    void declaredLocaleCompletionGatesUiAndSettingsReadiness(){
        QFETCH(int,mode);Scope scope;QTemporaryDir root,source;PackageStore store(root.path());const QString id=QStringLiteral("org.example.display");PackageCandidate value;QVERIFY(candidate(source.path(),id,&value));
        if(mode==0){auto manifest=read(source.filePath(QStringLiteral("addon.json")));manifest.remove(QStringLiteral("locales"));QVERIFY(document(source.filePath(QStringLiteral("addon.json")),manifest));}
        else if(mode==1)QVERIFY(document(source.filePath(QStringLiteral("locale.json")),{}));
        else if(mode==2)QVERIFY(write(source.filePath(QStringLiteral("locale.json")),"[]"));
        else QVERIFY(document(source.filePath(QStringLiteral("locale.json")),{{"large",QString(256*1024,QLatin1Char('x'))}}));
        PackageCandidate final;QVERIFY(PackageStore::inspectDirectory(source.path(),value.identity,&final));QVERIFY(store.install(final,{}));QVERIFY(store.setEnabled(id,true));
        AddonCatalogueView view({[&]()->std::optional<DisplayCatalogueSource>{DisplayCatalogueSource capture;capture.queuedAuthority=store.authorityToken();capture.reader=store.displayCatalogueReader(QStringLiteral("en"));return capture;},[&](auto wake){return store.onDisplayChanged(std::move(wake));}},scope.owner.handle());QTRY_VERIFY(view.isCurrent());
        const auto doc=view.acquireDocument(id+QStringLiteral("/section0"),QStringLiteral("view")),settings=view.acquireSettings(id);QVERIFY(!doc.isEmpty());QVERIFY(!settings.isEmpty());
        const auto state=mode<2?QStringLiteral("ready"):QStringLiteral("unavailable");QTRY_COMPARE(view.documentState(id+QStringLiteral("/section0"),QStringLiteral("view")),state);QTRY_COMPARE(view.settingsState(id),state);QCOMPARE(view.rows().size(),1);QCOMPARE(view.contributions(QStringLiteral("ui.section")).size(),2);QCOMPARE(view.themes().size(),1);
    }
    void pendingRemovalKeepsItsRowAfterActualStoreCleanup(){
        Scope scope;QTemporaryDir root;PackageStore store(root.path());const QString id=QStringLiteral("org.example.display");QVERIFY(install(store,id));QVERIFY(store.uninstall(id,true));
        AddonCatalogueView view({[&]()->std::optional<DisplayCatalogueSource>{DisplayCatalogueSource source;source.queuedAuthority=store.authorityToken();source.reader=store.displayCatalogueReader(QStringLiteral("en"));source.removing.insert(id);return source;},[&](auto wake){return store.onDisplayChanged(std::move(wake));}},scope.owner.handle());
        QTRY_VERIFY(view.isCurrent());QCOMPARE(view.rows().size(),1);const auto row=view.rows().first().toMap();QCOMPARE(row.value(QStringLiteral("id")).toString(),id);QVERIFY(row.value(QStringLiteral("removing")).toBool());QVERIFY(!row.value(QStringLiteral("installed")).toBool());QVERIFY(view.contributions(QStringLiteral("ui.section"),true).isEmpty());
    }
    void corruptPendingRemovalAndWithdrawnThemeUseCopiedOwnerState(){
        Scope scope;QTemporaryDir root;PackageStore store(root.path());const QString good=QStringLiteral("org.example.good"),bad=QStringLiteral("org.example.bad");QVERIFY(install(store,good));QVERIFY(store.setEnabled(good,true));QVERIFY(install(store,bad));QVERIFY(write(statePath(root.path(),bad),"broken"));store.invalidateAuthority();
        AddonCatalogueView view({[&]()->std::optional<DisplayCatalogueSource>{DisplayCatalogueSource source;source.queuedAuthority=store.authorityToken();source.reader=store.displayCatalogueReader(QStringLiteral("en"));source.removing.insert(bad);source.withdrawing.insert(good);return source;},[&](auto wake){return store.onDisplayChanged(std::move(wake));}},scope.owner.handle());
        QTRY_VERIFY(view.isCurrent());QCOMPARE(view.rows().size(),2);QVERIFY(view.rows().first().toMap().value(QStringLiteral("removing")).toBool());QVERIFY(view.rows().first().toMap().value(QStringLiteral("recoveryRequired")).toBool());QVERIFY(view.themes().isEmpty());QVERIFY(view.contributions(QStringLiteral("ui.section")).isEmpty());
    }
    void extractionPreservesCanonicalStrictDigestAndEveryRawDecision(){
        QTemporaryDir root;PackageStore store(root.path());const QString id=QStringLiteral("org.example.display");QVERIFY(install(store,id));QVERIFY(store.saveSettings(id,{{"account","A"}},0));QVERIFY(store.saveProviderOrder(QStringLiteral("subtitles"),{id+QStringLiteral("/section1")}));
        MetadataCatalogueSnapshot out;QVERIFY(store.metadataCatalogueReader()(&out,nullptr));const auto state=read(statePath(root.path(),id)),current=state.value(QStringLiteral("current")).toObject();const auto& row=out.packages.first();QJsonArray declarations;for(const auto& c:row.package.manifest.contributions)declarations.append(c.document);
        const QJsonObject expectedRow{{"manifest",row.package.manifest.document},{"identity",current.value(QStringLiteral("identity"))},{"digest",current.value(QStringLiteral("digest"))},{"generation",1},{"directory",row.package.directory},{"previousDigest",""},{"grants",QJsonObject{}},{"savedEnabled",false},{"enabled",false},{"recoveryRequired",false},{"settings",QJsonObject{{"account","A"}}},{"schema",row.schema},{"settingsRevision",1},{"contributions",declarations}};
        const auto bytes=QJsonDocument(QJsonObject{{"preferences",QJsonObject{{"subtitles",QJsonArray{QString(id+QStringLiteral("/section1"))}}}},{"distributionPolicy",QJsonObject{}},{"safeMode",false},{"candidates",1},{"packages",QJsonArray{expectedRow}}}).toJson(QJsonDocument::Compact);
        QCOMPARE(out.digest,QCryptographicHash::hash(bytes,QCryptographicHash::Sha256));QCOMPARE(out.retainedBytes,bytes.size());QCOMPARE(out.rawContributionCount,3);QCOMPARE(out.candidateCount,1);
    }
};
QTEST_GUILESS_MAIN(DisplayCatalogueTest)
#include "test_addon_display_catalogue.moc"
