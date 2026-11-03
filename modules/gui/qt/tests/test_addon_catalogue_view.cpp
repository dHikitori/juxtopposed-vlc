// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_catalogue_view.hpp"
#include "addons/addon_catalogue_store_lock.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QThreadStorage>
#include <QTimer>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
using namespace vlc::addons;
namespace {
struct Scope {
    AddonStreamLifetime owner;
    ~Scope(){owner.close();owner.handle().waitQt(std::chrono::seconds(5));}
};
struct Wait {
    std::mutex mutex;std::condition_variable changed;bool released=false;
    std::atomic_bool entered{false};
    void wait(){std::unique_lock<std::mutex> lock(mutex);entered=true;changed.wait(lock,[&]{return released;});}
    void release(){{std::lock_guard<std::mutex> lock(mutex);released=true;}changed.notify_all();}
};
struct TlsTail { std::shared_ptr<Wait> wait; ~TlsTail(){wait->wait();} };
DisplayCataloguePackage package(QString id=QStringLiteral("org.example.display"),int documents=1)
{
    DisplayCataloguePackage row;auto& p=row.package;p.manifest.id=id;p.manifest.name=QStringLiteral("Display example");
    p.manifest.version=QStringLiteral("1.0.0");p.identity.source=QStringLiteral("https://example.org/display");
    p.digest=QByteArray(64,'a');p.generation=1;p.enabled=true;row.savedEnabled=true;
    row.schema={{"type","object"},{"properties",QJsonObject{}}};
    for(int i=0;i<documents;++i){
        Contribution c;c.id=QStringLiteral("section%1").arg(i);c.type=QStringLiteral("ui.section");
        c.features={QStringLiteral("subtitles")};c.operations={QStringLiteral("search")};
        const QString path=QStringLiteral("view%1.json").arg(i);p.manifest.files.append(path);
        c.document={{"id",c.id},{"type",c.type},{"view",path},{"name",QStringLiteral("Section %1").arg(i)}};
        p.manifest.contributions.append(c);
    }
    return row;
}
struct Fixture {
    QTemporaryDir directory;StoreAuthority authority{directory.path()};
    DisplayCatalogueSnapshot snapshot;
    std::atomic_uint reads{0};std::atomic_bool readOnGui{false};
    QThread* gui=QThread::currentThread();
    QString name=QStringLiteral("first"),locale=QStringLiteral("en");bool refuse=false,bootstrapping=false;quint64 bootstrapGeneration=0;
    QVariantList bootstrapResults;CredentialStore::Reader credentials;std::function<std::optional<bool>()> tlsProbe;
    QHash<QString,QHash<QString,bool>> presence;QMap<QString,QVariantMap> distributions;QStringList distributionIds;QSet<QString> removing,withdrawing;
    std::shared_ptr<Wait> block,tailWait;
    std::shared_ptr<QThreadStorage<TlsTail*>> tailStorage;
    std::mutex requestMutex;QStringList lastDocuments,lastSettings;
    Fixture(){snapshot.packages={package()};snapshot.displayPreferences={{"appearance",QJsonObject{{"highContrast",true},{"reducedMotion",false}}}};}
    AddonCatalogueView::Backend backend(){
        return {[this]()->std::optional<DisplayCatalogueSource>{
            DisplayCatalogueSource source;source.queuedAuthority=authority.capture();source.workerExecutable=QStringLiteral("/bin/true");
            source.locale=locale;
            source.bootstrapping=bootstrapping;source.bootstrapGeneration=bootstrapGeneration;source.bootstrapResults=bootstrapResults;
            source.credentials=credentials;source.credentialPresence=presence;source.tlsProbe=tlsProbe;source.distributions=distributions;source.distributionIds=distributionIds;source.removing=removing;source.withdrawing=withdrawing;
            auto state=snapshot;if(!state.packages.isEmpty())state.packages[0].package.manifest.name=name;
            state.authority=source.queuedAuthority;state.queuedAuthority=source.queuedAuthority;
            const auto wait=block;const auto tlsWait=tailWait;const auto tls=tailStorage;const bool denied=refuse;
            source.reader=[this,state=std::move(state),wait,tlsWait,tls,denied](const DisplayCatalogueRequest& request,DisplayCatalogueSnapshot* out,QString* error)mutable{
                ++reads;readOnGui=QThread::currentThread()==gui;
                {std::lock_guard<std::mutex> lock(requestMutex);lastDocuments=request.documents;lastSettings=request.settingsPackages;}
                if(wait)wait->wait();
                if(denied){*error=QStringLiteral("fixture refusal");return false;}
                for(auto& row:state.packages){
                    const auto id=row.package.manifest.id;
                    for(const auto& c:row.package.manifest.contributions){const QString key=id+QLatin1Char('/')+c.id+QStringLiteral("/view");
                        if(request.documents.contains(key))row.resources.insert(c.id+QStringLiteral("/view"),QJsonObject{{"text",c.id}});}
                    if(request.settingsPackages.contains(id))row.messages={{"save","Save"}};
                }
                *out=state;if(tls)tls->setLocalData(new TlsTail{tlsWait});return true;
            };return source;
        },[this](std::function<void()> wake){return authority.onChanged(std::move(wake));}};
    }
};
}
class CatalogueViewTest final:public QObject {
    Q_OBJECT
private slots:
    void cleanup(){QTRY_COMPARE_WITH_TIMEOUT(AddonRepositoryJobs::processCount(),0u,5000);QTRY_COMPARE_WITH_TIMEOUT(AddonStreamLifetime::processQtWorkCount(),0u,5000);}
    void postUnlockWakeBeforeRefusedCompletionTriggersOneReadyRetry(){
        Scope scope;Fixture fixture;const auto refused=std::make_shared<Wait>();
        const auto path=fixture.directory.filePath(QStringLiteral("store.lock"));
        auto backend=fixture.backend();const auto capture=backend.capture;
        backend.capture=[capture,refused,path] {
            auto source=capture();if(!source)return source;const auto reader=source->reader;
            source->reader=[reader,refused,path](const DisplayCatalogueRequest& request,DisplayCatalogueSnapshot* out,QString* error) {
                QLockFile lock(path);
                if(!lock.tryLock(0)){out->refusal=DisplayCatalogueSnapshot::Refusal::StoreBusy;refused->wait();*error=QStringLiteral("Actual store.lock refusal");return false;}
                lock.unlock();return reader(request,out,error);
            };return source;
        };
        const auto release=qScopeGuard([&]{refused->release();scope.owner.close();scope.owner.handle().waitQt(std::chrono::seconds(5));});
        AddonCatalogueView view(std::move(backend),scope.owner.handle());QTRY_VERIFY(view.isCurrent());
        auto writer=std::make_unique<CatalogueStoreLock>(path,fixture.authority);QVERIFY(writer->tryLock(0));
        fixture.name=QStringLiteral("after actual unlock");view.refresh();QTRY_VERIFY(refused->entered.load());
        const auto before=view.nativeWakes();writer.reset();QTRY_VERIFY(view.nativeWakes()>before);
        // The post-unlock event is serviced while the exact refused reader is
        // still pending. No further Store write/event or explicit query follows.
        refused->release();QTRY_VERIFY_WITH_TIMEOUT(view.isCurrent(),3000);
        QCOMPARE(view.rows().first().toMap().value(QStringLiteral("name")).toString(),QStringLiteral("after actual unlock"));
        QCOMPARE(fixture.reads.load(),2u);const auto reads=fixture.reads.load();QTest::qWait(80);QCOMPARE(fixture.reads.load(),reads);
    }
    void successfulPendingReaderConsumesNativeWakeWithoutAnotherRead(){
        Scope scope;Fixture fixture;AddonCatalogueView view(fixture.backend(),scope.owner.handle());QTRY_VERIFY(view.isCurrent());
        fixture.block=std::make_shared<Wait>();const auto wait=fixture.block;
        const auto release=qScopeGuard([&]{wait->release();scope.owner.close();scope.owner.handle().waitQt(std::chrono::seconds(5));});
        view.refresh();QTRY_VERIFY(wait->entered.load());const auto before=view.nativeWakes();
        {CatalogueStoreLock writer(fixture.directory.filePath(QStringLiteral("store.lock")),fixture.authority);QVERIFY(writer.tryLock(0));}
        QTRY_VERIFY(view.nativeWakes()>before);wait->release();QTRY_VERIFY(view.isCurrent());
        QTRY_COMPARE(AddonRepositoryJobs::processCount(),0u);QTest::qWait(80);QCOMPARE(fixture.reads.load(),2u);
    }
    void pendingRefreshRetriesAfterActualTlsPermitReleaseWithoutAnotherStoreEvent(){
        Scope scope;Fixture fixture;
        const auto tail=std::make_shared<Wait>(),other=std::make_shared<Wait>();
        const auto storage=std::make_shared<QThreadStorage<TlsTail*>>();
        const auto release=qScopeGuard([&]{tail->release();other->release();scope.owner.close();scope.owner.handle().waitQt(std::chrono::seconds(5));});
        fixture.tailWait=tail;fixture.tailStorage=storage;
        AddonCatalogueView view(fixture.backend(),scope.owner.handle());
        QTRY_VERIFY(tail->entered.load());QTRY_VERIFY(view.isCurrent());
        // The GUI has consumed completion, but the actual worker TLS cleanup
        // still retains its process permit. Later reads must not block again.
        fixture.tailStorage.reset();fixture.tailWait.reset();
        AddonRepositoryJobs held(scope.owner.handle());
        QVERIFY(held.submit([other]{other->wait();},[](auto){}));QTRY_VERIFY(other->entered.load());
        QCOMPARE(AddonRepositoryJobs::processCount(),2u);
        const auto oldRows=view.rows();const auto oldReads=fixture.reads.load();
        fixture.name=QStringLiteral("after TLS cleanup");fixture.authority.invalidate();
        QTRY_VERIFY(!view.error().isEmpty());QVERIFY(view.loading());QVERIFY(!view.isCurrent());
        QCOMPARE(view.rows(),oldRows);QCOMPARE(fixture.reads.load(),oldReads);
        unsigned beats=0;QTimer heartbeat;heartbeat.setInterval(2);connect(&heartbeat,&QTimer::timeout,this,[&]{++beats;});heartbeat.start();
        QTRY_VERIFY(beats>=3);QVERIFY(!other->released);
        // No extra Store mutation/manual refresh follows this actual release.
        tail->release();QTRY_VERIFY_WITH_TIMEOUT(view.isCurrent(),3000);
        QCOMPARE(view.rows().first().toMap().value(QStringLiteral("name")).toString(),QStringLiteral("after TLS cleanup"));
        QCOMPARE(fixture.reads.load(),oldReads+1);QVERIFY(!other->released);
        const auto count=fixture.reads.load();QTest::qWait(80);QCOMPARE(fixture.reads.load(),count);
    }
    void closeWhileWaitingForProcessQuotaNeverStartsLateWork(){
        Scope scope;Fixture fixture;AddonRepositoryJobs first(scope.owner.handle()),second(scope.owner.handle());
        const auto one=std::make_shared<Wait>(),two=std::make_shared<Wait>();
        const auto release=qScopeGuard([&]{one->release();two->release();scope.owner.close();scope.owner.handle().waitQt(std::chrono::seconds(5));});
        QVERIFY(first.submit([one]{one->wait();},[](auto){}));QTRY_VERIFY(one->entered.load());
        QVERIFY(second.submit([two]{two->wait();},[](auto){}));QTRY_VERIFY(two->entered.load());
        auto view=std::make_unique<AddonCatalogueView>(fixture.backend(),scope.owner.handle());
        QTRY_VERIFY(!view->error().isEmpty());QVERIFY(view->loading());view.reset();
        one->release();two->release();QTRY_COMPARE(AddonRepositoryJobs::processCount(),0u);
        QCoreApplication::processEvents();QCOMPARE(fixture.reads.load(),0u);
    }
    void readyGettersAndUnchangedInputsNeverReadAgain(){
        Scope scope;Fixture fixture;AddonCatalogueView view(fixture.backend(),scope.owner.handle());QTRY_VERIFY(view.isCurrent());
        QCOMPARE(fixture.reads.load(),1u);QVERIFY(!fixture.readOnGui);QVERIFY(view.preferences(QStringLiteral("appearance")).value(QStringLiteral("highContrast")).toBool());QCOMPARE(view.rows().size(),1);QCOMPARE(view.contributions(QStringLiteral("ui.section")).size(),1);
        const auto reads=fixture.reads.load(),wakes=view.nativeWakes(),publications=view.publications();
        for(int i=0;i<200;++i){view.rows();view.themes();view.selectedTheme();view.providers(QStringLiteral("subtitles"));view.contributions(QStringLiteral("ui.section"));view.capabilities();view.resource(QStringLiteral("org.example.display/section0"),QStringLiteral("view"));view.settingsSchema(QStringLiteral("org.example.display"));view.settingsValues(QStringLiteral("org.example.display"));view.settingsSecretStatus(QStringLiteral("org.example.display"));view.inputsChanged();}
        QTest::qWait(100);QCOMPARE(fixture.reads.load(),reads);QCOMPARE(view.nativeWakes(),wakes);QCOMPARE(view.publications(),publications);
        QVERIFY(!view.capabilities().value(QStringLiteral("networkHttpKnown")).toBool());QVERIFY(!view.capabilities().value(QStringLiteral("webSocketKnown")).toBool());
    }
    void genericParentAttachmentDoesNotRequireAManagerFactory(){
        Scope scope;Fixture fixture;QObject parent;
        auto* view=new AddonCatalogueView(fixture.backend(),scope.owner.handle(),&parent);
        QVERIFY(!AddonCatalogueView::attached(&parent));view->setObjectName(QStringLiteral("hakoneDisplayCatalogue"));
        QCOMPARE(AddonCatalogueView::attached(&parent),view);QTRY_VERIFY(view->isCurrent());
        delete view;QVERIFY(!AddonCatalogueView::attached(&parent));
    }
    void safeModeKnownDistinguishesLoadingEmptyAndRetainedData(){
        Scope scope;Fixture fixture;fixture.snapshot.packages.clear();AddonCatalogueView view(fixture.backend(),scope.owner.handle());
        QVERIFY(!view.capabilities().value(QStringLiteral("safeModeKnown")).toBool());QTRY_VERIFY(view.isCurrent());QVERIFY(view.rows().isEmpty());QVERIFY(view.capabilities().value(QStringLiteral("safeModeKnown")).toBool());QVERIFY(!view.capabilities().value(QStringLiteral("safeMode")).toBool());
        fixture.snapshot.safeMode=true;fixture.authority.invalidate();QTRY_VERIFY(view.isCurrent());QVERIFY(view.capabilities().value(QStringLiteral("safeMode")).toBool());
        fixture.block=std::make_shared<Wait>();const auto release=qScopeGuard([&]{fixture.block->release();});fixture.authority.invalidate();QTRY_VERIFY(fixture.block->entered.load());QVERIFY(!view.isCurrent());QVERIFY(view.capabilities().value(QStringLiteral("safeModeKnown")).toBool());QVERIFY(view.capabilities().value(QStringLiteral("safeMode")).toBool());fixture.block->release();QTRY_VERIFY(view.isCurrent());
    }
    void blockedRefreshKeepsSafeRowsAndGuiHeartbeat(){
        Scope scope;Fixture fixture;AddonCatalogueView view(fixture.backend(),scope.owner.handle());QTRY_VERIFY(view.isCurrent());
        const auto before=view.rows();fixture.name=QStringLiteral("new");fixture.block=std::make_shared<Wait>();const auto release=qScopeGuard([&]{fixture.block->release();});
        unsigned beats=0;QTimer timer;timer.setInterval(2);connect(&timer,&QTimer::timeout,this,[&]{++beats;});timer.start();
        fixture.authority.invalidate();QVERIFY(!view.isCurrent());QVERIFY(!view.retainedCatalogueCurrent());QCOMPARE(view.rows(),before);QTRY_VERIFY(fixture.block->entered.load());QTRY_VERIFY(beats>=5);QVERIFY(view.loading());QCOMPARE(view.rows(),before);
        fixture.block->release();QTRY_VERIFY(view.isCurrent());QCOMPARE(view.rows().first().toMap().value(QStringLiteral("name")).toString(),QStringLiteral("new"));
    }
    void writerBurstPublishesOneAcceptedBatch(){
        Scope scope;Fixture fixture;AddonCatalogueView view(fixture.backend(),scope.owner.handle());QTRY_VERIFY(view.isCurrent());QSignalSpy changed(&view,&AddonCatalogueView::catalogueChanged);
        const auto reads=fixture.reads.load();fixture.name=QStringLiteral("latest");
        for(int i=0;i<100;++i){auto mutation=fixture.authority.mutation();}
        QTRY_VERIFY(view.isCurrent());QCOMPARE(fixture.reads.load(),reads+1);QCOMPARE(changed.size(),1);QCOMPARE(view.rows().first().toMap().value(QStringLiteral("name")).toString(),QStringLiteral("latest"));
    }
    void pendingOldSnapshotCannotPublishAfterRevocation(){
        Scope scope;Fixture fixture;fixture.block=std::make_shared<Wait>();const auto blocked=fixture.block;const auto release=qScopeGuard([&]{blocked->release();});
        AddonCatalogueView view(fixture.backend(),scope.owner.handle());QTRY_VERIFY(blocked->entered.load());
        fixture.name=QStringLiteral("current");fixture.block.reset();fixture.authority.invalidate();blocked->release();QTRY_VERIFY(view.isCurrent());
        QCOMPARE(fixture.reads.load(),2u);QCOMPARE(view.rows().first().toMap().value(QStringLiteral("name")).toString(),QStringLiteral("current"));QCOMPARE(view.publications(),1u);
    }
    void refusedRefreshPreservesDataAndNeedsAnActualRetry(){
        Scope scope;Fixture fixture;AddonCatalogueView view(fixture.backend(),scope.owner.handle());QTRY_VERIFY(view.isCurrent());const auto before=view.rows();
        fixture.refuse=true;view.refresh();QTRY_VERIFY(!view.loading());QVERIFY(!view.isCurrent());QVERIFY(view.retainedCatalogueCurrent());QVERIFY(!view.error().isEmpty());QCOMPARE(view.rows(),before);const auto reads=fixture.reads.load();QTest::qWait(100);QCOMPARE(fixture.reads.load(),reads);
        fixture.refuse=false;view.refresh();QTRY_VERIFY_WITH_TIMEOUT(view.isCurrent(),1000);QCOMPARE(view.rows(),before);
    }
    void fatalReadIgnoresLateUnlockButInputsAndEpochCanRecover(){
        Scope scope;Fixture fixture;fixture.refuse=true;const auto authority=fixture.authority.capture();
        AddonCatalogueView view(fixture.backend(),scope.owner.handle());
        QTRY_VERIFY(!view.loading()&&!view.error().isEmpty());QCOMPARE(fixture.reads.load(),1u);
        const auto wakes=view.nativeWakes();
        {CatalogueStoreLock reader(fixture.directory.filePath(QStringLiteral("store.lock")),fixture.authority);QVERIFY(reader.tryLock(0));}
        QTRY_VERIFY(view.nativeWakes()>wakes);QCOMPARE(fixture.reads.load(),1u);QVERIFY(authority.current());
        fixture.refuse=false;fixture.locale=QStringLiteral("fr");view.inputsChanged();
        QTRY_VERIFY(view.isCurrent());QCOMPARE(fixture.reads.load(),2u);QVERIFY(authority.current());
        fixture.refuse=true;view.refresh();QTRY_VERIFY(!view.loading()&&!view.error().isEmpty());QCOMPARE(fixture.reads.load(),3u);
        fixture.refuse=false;fixture.authority.invalidate();QTRY_VERIFY(view.isCurrent());
        QCOMPARE(fixture.reads.load(),4u);QVERIFY(!authority.current());
    }
    void bootstrapHoldsAllReadsUntilOneFinalPublication(){
        Scope scope;Fixture fixture;fixture.bootstrapping=true;AddonCatalogueView view(fixture.backend(),scope.owner.handle());
        for(int i=0;i<100;++i){fixture.authority.invalidate();view.inputsChanged();}QTest::qWait(50);QCOMPARE(fixture.reads.load(),0u);QVERIFY(view.loading());QVERIFY(!view.isCurrent());
        fixture.bootstrapResults={QVariantMap{{"id","org.example.failed"},{"success",false},{"error","Install failed"},{"status","failed"},{"recoveryRequired",true}}};
        fixture.bootstrapping=false;++fixture.bootstrapGeneration;view.inputsChanged();QTRY_VERIFY(view.isCurrent());QCOMPARE(fixture.reads.load(),1u);QCOMPARE(view.rows().size(),2);
        const auto failed=view.rows().last().toMap();QVERIFY(!failed.value(QStringLiteral("enabled")).toBool());QVERIFY(failed.value(QStringLiteral("recoveryRequired")).toBool());QCOMPARE(view.contributions(QStringLiteral("ui.section")).size(),1);
    }
    void unknownCredentialsStayUnknownUntilSelectedJoinedRequest(){
        Scope scope;Fixture fixture;const QString id=QStringLiteral("org.example.display"),reference=QStringLiteral("00000000-0000-4000-8000-000000000001");
        auto& row=fixture.snapshot.packages[0];row.schema={{"type","object"},{"required",QJsonArray{QStringLiteral("apiKey")}},{"properties",QJsonObject{{"apiKey",QJsonObject{{"type","string"},{"writeOnly",true}}}}}};row.settings={{"_credentials",QJsonObject{{"apiKey",reference}}}};
        std::atomic_uint calls{0};std::atomic_bool onGui{false};fixture.credentials=[&](const QString& packageId,const QString& ref){++calls;onGui=QThread::currentThread()==fixture.gui;return packageId==id&&ref==reference?QByteArray("private-test-value"):QByteArray{};};
        AddonCatalogueView view(fixture.backend(),scope.owner.handle());QTRY_VERIFY(view.isCurrent());QCOMPARE(calls.load(),0u);
        auto summary=view.rows().first().toMap();QVERIFY(summary.value(QStringLiteral("enabled")).toBool());QVERIFY(!summary.value(QStringLiteral("configurationKnown")).toBool());QCOMPARE(summary.value(QStringLiteral("configurationState")).toString(),QStringLiteral("unknown"));
        QVERIFY(view.requestPresence(QStringLiteral("subtitles")));QTRY_COMPARE(calls.load(),1u);QTRY_VERIFY(view.rows().first().toMap().value(QStringLiteral("configured")).toBool());QVERIFY(!onGui);
        const auto encoded=QJsonDocument(QJsonArray::fromVariantList(view.rows()+view.providers(QStringLiteral("subtitles")))).toJson();QVERIFY(!encoded.contains("private-test-value"));
        const auto ticket=view.acquireSettings(id);QVERIFY(!ticket.isEmpty());QTRY_VERIFY(view.settingsReady(id));QCOMPARE(calls.load(),1u);QVERIFY(view.settingsValues(id).isEmpty());QVERIFY(view.settingsSecretStatus(id).value(QStringLiteral("apiKey")).toMap().value(QStringLiteral("present")).toBool());
    }
    void featureTlsPreparationIsExplicitJoinedAndNeverHidesUnknownControllers(){
        Scope scope;Fixture fixture;Contribution controller;controller.id=QStringLiteral("controller");controller.type=QStringLiteral("session.controller");controller.contract=QStringLiteral("session@1");controller.document={{"id",controller.id},{"type",controller.type},{"contract",controller.contract},{"operations",QJsonArray{QStringLiteral("run")}}};fixture.snapshot.packages[0].package.manifest.contributions.append(controller);
        std::atomic_uint calls{0};std::atomic_bool gui{false};fixture.tlsProbe=[&]()->std::optional<bool>{++calls;gui=QThread::currentThread()==fixture.gui;return true;};
        AddonCatalogueView view(fixture.backend(),scope.owner.handle());QTRY_VERIFY(view.isCurrent());QCOMPARE(calls.load(),0u);QCOMPARE(view.contributions(QStringLiteral("session.controller")).size(),1);QVERIFY(!view.capabilities().value(QStringLiteral("webSocketKnown")).toBool());
        const auto ticket=view.acquireFeature(QStringLiteral("session.controller"));QVERIFY(!ticket.isEmpty());QTRY_VERIFY(view.capabilities().value(QStringLiteral("webSocketKnown")).toBool());QCOMPARE(calls.load(),1u);QVERIFY(!gui);QVERIFY(view.capabilities().value(QStringLiteral("webSocketAvailable")).toBool());
        const auto doc=view.acquireDocument(QStringLiteral("org.example.display/section0"),QStringLiteral("view"));QVERIFY(!doc.isEmpty());QTRY_COMPARE(view.documentState(QStringLiteral("org.example.display/section0"),QStringLiteral("view")),QStringLiteral("ready"));QCOMPARE(calls.load(),1u);
        view.release(doc);view.release(ticket);
        const auto reopened=view.acquireFeature(QStringLiteral("session.controller"));QVERIFY(!reopened.isEmpty());QTRY_COMPARE(calls.load(),2u);view.release(reopened);
        fixture.authority.invalidate();QTRY_VERIFY(view.isCurrent());QCOMPARE(calls.load(),2u);QCOMPARE(view.contributions(QStringLiteral("session.controller")).size(),1);QVERIFY(!view.capabilities().value(QStringLiteral("webSocketKnown")).toBool());
    }
    void closingFeatureBeforeWorkerProjectionPreventsKeyAndTlsReads(){
        Scope scope;Fixture fixture;Contribution controller;controller.id=QStringLiteral("controller");controller.type=QStringLiteral("session.controller");controller.contract=QStringLiteral("session@1");controller.document={{"id",controller.id},{"type",controller.type},{"contract",controller.contract}};fixture.snapshot.packages[0].package.manifest.contributions.append(controller);
        auto& row=fixture.snapshot.packages[0];row.schema={{"type","object"},{"required",QJsonArray{QStringLiteral("key")}},{"properties",QJsonObject{{"key",QJsonObject{{"type","string"},{"writeOnly",true}}}}}};row.settings={{"_credentials",QJsonObject{{"key","00000000-0000-4000-8000-000000000002"}}}};
        std::atomic_uint keys{0},tls{0};fixture.credentials=[&](const auto&,const auto&){++keys;return QByteArray("temporary-test-key");};fixture.tlsProbe=[&]()->std::optional<bool>{++tls;return true;};
        AddonCatalogueView view(fixture.backend(),scope.owner.handle());QTRY_VERIFY(view.isCurrent());fixture.block=std::make_shared<Wait>();const auto release=qScopeGuard([&]{fixture.block->release();});
        const auto ticket=view.acquireFeature(QStringLiteral("session.controller"));QVERIFY(!ticket.isEmpty());QTRY_VERIFY(fixture.block->entered.load());view.release(ticket);fixture.block->release();QTRY_VERIFY(!AddonRepositoryJobs::processCount());QCOMPARE(keys.load(),0u);QCOMPARE(tls.load(),0u);QVERIFY(view.isCurrent());
    }
    void featureTicketsShareTheActive32QuotaAndHaveNoInactiveRetention(){
        Scope scope;Fixture fixture;AddonCatalogueView view(fixture.backend(),scope.owner.handle());QTRY_VERIFY(view.isCurrent());QStringList tickets;
        for(int i=0;i<32;++i){const auto ticket=view.acquireFeature(QStringLiteral("subtitles"));QVERIFY(!ticket.isEmpty());tickets.append(ticket);}
        QVERIFY(view.acquireFeature(QStringLiteral("subtitles")).isEmpty());for(const auto& ticket:tickets)view.release(ticket);
        const auto replacement=view.acquireFeature(QStringLiteral("session.controller"));QVERIFY(!replacement.isEmpty());view.release(replacement);
        QTRY_VERIFY_WITH_TIMEOUT(view.publications()>=2,1000);QTRY_VERIFY(!AddonRepositoryJobs::processCount());
        const auto reads=fixture.reads.load();QTest::qWait(80);QCOMPARE(fixture.reads.load(),reads);
    }
    void unopenedDocumentsAreAbsentAndRequestsPreserveCurrentActions(){
        Scope scope;Fixture fixture;AddonCatalogueView view(fixture.backend(),scope.owner.handle());QTRY_VERIFY(view.isCurrent());
        const QString id=QStringLiteral("org.example.display/section0");QCOMPARE(view.retainedDocumentBytes(),0);QCOMPARE(view.documentState(id,QStringLiteral("view")),QStringLiteral("pending"));QVERIFY(view.resource(id,QStringLiteral("view")).isEmpty());
        const auto revision=view.revision();const auto ticket=view.acquireDocument(id,QStringLiteral("view"));QVERIFY(!ticket.isEmpty());QVERIFY(view.isCurrent());QTRY_COMPARE(view.documentState(id,QStringLiteral("view")),QStringLiteral("ready"));QVERIFY(view.isCurrent());QCOMPARE(view.revision(),revision);QVERIFY(view.retainedDocumentBytes()>0);
        {std::lock_guard<std::mutex> lock(fixture.requestMutex);QCOMPARE(fixture.lastDocuments,QStringList{id+QStringLiteral("/view")});}
        QCOMPARE(view.resource(id,QStringLiteral("view")).value(QStringLiteral("text")).toString(),QStringLiteral("section0"));view.release(ticket);
    }
    void inactiveDocumentLruAndActiveTicketsAreBounded(){
        Scope scope;Fixture fixture;fixture.snapshot.packages={package(QStringLiteral("org.example.display"),40)};AddonCatalogueView view(fixture.backend(),scope.owner.handle());QTRY_VERIFY(view.isCurrent());
        QStringList tickets;
        for(int i=0;i<32;++i){const auto t=view.acquireDocument(QStringLiteral("org.example.display/section%1").arg(i),QStringLiteral("view"));QVERIFY(!t.isEmpty());tickets.append(t);}
        QVERIFY(view.acquireDocument(QStringLiteral("org.example.display/section32"),QStringLiteral("view")).isEmpty());
        QTRY_COMPARE(view.documentState(QStringLiteral("org.example.display/section31"),QStringLiteral("view")),QStringLiteral("ready"));
        for(const auto& ticket:tickets)view.release(ticket);
        QVERIFY(view.resource(QStringLiteral("org.example.display/section0"),QStringLiteral("view")).isEmpty());QVERIFY(!view.resource(QStringLiteral("org.example.display/section31"),QStringLiteral("view")).isEmpty());
        const auto before=view.retainedDocumentBytes();QVERIFY(before>0);
        QVERIFY(view.ensureDocument(QStringLiteral("org.example.display/section35"),QStringLiteral("view")));QTRY_COMPARE(view.documentState(QStringLiteral("org.example.display/section35"),QStringLiteral("view")),QStringLiteral("ready"));
        QVERIFY(view.resource(QStringLiteral("org.example.display/section16"),QStringLiteral("view")).isEmpty());
        int retained=0;for(int i=0;i<40;++i)retained+=!view.resource(QStringLiteral("org.example.display/section%1").arg(i),QStringLiteral("view")).isEmpty();QCOMPARE(retained,16);
    }
    void settingsLruEvictionReturnsToPendingRatherThanUnavailable(){
        Scope scope;Fixture fixture;fixture.snapshot.packages.clear();
        for(int i=0;i<17;++i)fixture.snapshot.packages.append(package(QStringLiteral("org.example.setting%1").arg(i)));
        AddonCatalogueView view(fixture.backend(),scope.owner.handle());QTRY_VERIFY(view.isCurrent());
        QStringList tickets;for(int i=0;i<17;++i){const auto ticket=view.acquireSettings(QStringLiteral("org.example.setting%1").arg(i));QVERIFY(!ticket.isEmpty());tickets.append(ticket);}
        QTRY_VERIFY(view.settingsReady(QStringLiteral("org.example.setting16")));
        for(const auto& ticket:tickets)view.release(ticket);
        QCOMPARE(view.settingsState(QStringLiteral("org.example.setting0")),QStringLiteral("pending"));QVERIFY(!view.settingsReady(QStringLiteral("org.example.setting0")));
        const auto reopened=view.acquireSettings(QStringLiteral("org.example.setting0"));QVERIFY(!reopened.isEmpty());QTRY_VERIFY(view.settingsReady(QStringLiteral("org.example.setting0")));
    }
    void packageProjectionKeepsEveryOldOrdinaryFieldAndSeparateDisplayPartition(){
        Scope scope;Fixture fixture;const QString id=QStringLiteral("org.example.display");fixture.withdrawing.insert(id);
        AddonCatalogueView view(fixture.backend(),scope.owner.handle());QTRY_VERIFY(view.isCurrent());const auto row=view.rows().first().toMap();
        const QVariantMap old{{"id",id},{"name","first"},{"version","1.0.0"},{"source","https://example.org/display"},{"tag",""},{"commit",""},{"distributionSource",false},{"publisherVerified",false},{"developerSource",false},{"digest",QString(64,QLatin1Char('a'))},{"enabled",true},{"configured",false},{"hasPrevious",false},{"installed",true},{"recoveryRequired",false},{"removing",false},{"appUpdateAvailable",false},{"appUpdateVersion",""}};
        for(auto it=old.cbegin();it!=old.cend();++it){QVERIFY2(row.contains(it.key()),qPrintable(it.key()));QCOMPARE(row.value(it.key()),it.value());}
        const auto contribution=view.contributions(QStringLiteral("ui.section"),true).first().toMap();QCOMPARE(contribution.value(QStringLiteral("cachePartition")).toString(),QString(64,QLatin1Char('a'))+QStringLiteral(":1"));QCOMPARE(contribution.value(QStringLiteral("displayPartition")).toString(),QString(64,QLatin1Char('a'))+QStringLiteral(":1:0"));QVERIFY(!contribution.value(QStringLiteral("enabled")).toBool());
    }
    void suppressedSavedAndCorruptRowsKeepOldFalseKeysWithoutAnOffer(){
        Scope scope;Fixture fixture;const QString suppressed=QStringLiteral("org.example.suppressed"),saved=QStringLiteral("org.example.saved");fixture.distributionIds={suppressed};fixture.snapshot.distributionPolicy={{"removed",QJsonObject{{suppressed,true}}}};fixture.snapshot.savedDataIds={saved};
        auto bad=package(QStringLiteral("org.example.bad"));bad.corrupt=true;bad.error=QStringLiteral("fixture corruption");fixture.snapshot.packages.append(bad);
        AddonCatalogueView view(fixture.backend(),scope.owner.handle());QTRY_VERIFY(view.isCurrent());
        QMap<QString,QVariantMap> rows;for(const auto& value:view.rows()){const auto row=value.toMap();rows.insert(row.value(QStringLiteral("id")).toString(),row);}
        QVERIFY(rows.value(suppressed).contains(QStringLiteral("publisherVerified")));QVERIFY(!rows.value(suppressed).value(QStringLiteral("publisherVerified")).toBool());QCOMPARE(rows.value(suppressed).value(QStringLiteral("name")).toString(),suppressed);
        QVERIFY(rows.value(saved).contains(QStringLiteral("removing")));QVERIFY(!rows.value(saved).value(QStringLiteral("removing")).toBool());QVERIFY(rows.value(QStringLiteral("org.example.bad")).contains(QStringLiteral("removing")));QVERIFY(!rows.value(QStringLiteral("org.example.bad")).value(QStringLiteral("removing")).toBool());
    }
    void sharedRetirementRefusesConfiguredWithoutInventingSetupFromEnabled(){
        Scope scope;Fixture fixture;auto& row=fixture.snapshot.packages[0];row.retiring=true;row.package.enabled=false;
        AddonCatalogueView view(fixture.backend(),scope.owner.handle());QTRY_VERIFY(view.isCurrent());QVERIFY(!view.rows().first().toMap().value(QStringLiteral("configured")).toBool());
    }
    void corruptRowsNeverCreateContributions(){
        Scope scope;Fixture fixture;auto bad=package(QStringLiteral("org.example.corrupt"));bad.corrupt=true;bad.error=QStringLiteral("Invalid state");fixture.snapshot.packages.append(bad);
        AddonCatalogueView view(fixture.backend(),scope.owner.handle());QTRY_VERIFY(view.isCurrent());QCOMPARE(view.rows().size(),2);QCOMPARE(view.contributions(QStringLiteral("ui.section"),true).size(),1);QVERIFY(view.rows().last().toMap().value(QStringLiteral("recoveryRequired")).toBool());
    }
    void captureCanDeleteOwner(){
        Scope scope;Fixture fixture;AddonCatalogueView* view=nullptr;auto backend=fixture.backend();const auto original=backend.capture;backend.capture=[&]{delete view;view=nullptr;return original();};
        view=new AddonCatalogueView(std::move(backend),scope.owner.handle());const QPointer<AddonCatalogueView> alive(view);QTRY_VERIFY(!alive);QCOMPARE(fixture.reads.load(),0u);
    }
    void publicationObserversCanDeleteCloseOrRefresh_data(){QTest::addColumn<int>("action");QTest::newRow("delete")<<0;QTest::newRow("close")<<1;QTest::newRow("refresh")<<2;}
    void publicationObserversCanDeleteCloseOrRefresh(){
        QFETCH(int,action);Scope scope;Fixture fixture;auto* view=new AddonCatalogueView(fixture.backend(),scope.owner.handle());const QPointer<AddonCatalogueView> alive(view);bool acted=false;
        connect(view,&AddonCatalogueView::catalogueChanged,this,[&]{if(acted)return;acted=true;if(action==0)delete view;else if(action==1)view->close();else view->refresh();});
        QTRY_VERIFY(acted);if(action==0)QVERIFY(!alive);else if(action==1){QVERIFY(!view->isCurrent());delete view;}else{QTRY_VERIFY(view->isCurrent());QCOMPARE(fixture.reads.load(),2u);delete view;}
    }
    void closeDoesNotWaitForBlockedReaderButAppDrainDoes(){
        Scope scope;Fixture fixture;fixture.block=std::make_shared<Wait>();const auto release=qScopeGuard([&]{fixture.block->release();});auto* view=new AddonCatalogueView(fixture.backend(),scope.owner.handle());QTRY_VERIFY(fixture.block->entered.load());
        view->close();delete view;scope.owner.close();QVERIFY(!scope.owner.handle().qtDrained());fixture.block->release();QTRY_VERIFY(scope.owner.handle().qtDrained());
    }
};
QTEST_GUILESS_MAIN(CatalogueViewTest)
#include "test_addon_catalogue_view.moc"
