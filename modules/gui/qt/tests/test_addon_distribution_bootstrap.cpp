// SPDX-License-Identifier: GPL-2.0-or-later
#include <QtTest>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QThread>
#include <memory>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include "addons/addon_manager.hpp"
#include "addons/addon_distribution_bootstrap.hpp"
#include "addons/addon_package_summaries.hpp"
#include "addons/addon_ui_assets.hpp"
#include "addons/addon_repository_jobs.hpp"
using namespace vlc::addons;
namespace {
bool write(const QString& path,const QByteArray& bytes)
{QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size();}
QByteArray read(const QString& path)
{QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray{};}
struct NativeGate {
    std::mutex mutex;std::condition_variable condition;std::atomic_bool entered{false};std::atomic_uint entries{0};bool open=false;
    void wait(){std::unique_lock<std::mutex> lock(mutex);entered.store(true,std::memory_order_release);condition.wait(lock,[&]{return open;});}
    void release(){std::lock_guard<std::mutex> lock(mutex);open=true;condition.notify_all();}
};
struct Packages {
    QTemporaryDir root;
    QString make(const QString& id,const QString& version=QStringLiteral("1.0.0"),const QString& source=QStringLiteral("https://github.com/hakone-org/startup-fixture"),
        const QJsonObject& permissions={}) {
        const auto directory=root.filePath(id+QLatin1Char('-')+version+QLatin1Char('-')+QString::number(++sequence));
        if(!QDir().mkpath(directory))return {};
        const QJsonObject contribution{{"id","artwork"},{"type","metadata.provider"},{"contract","metadata@1"},
            {"features",QJsonArray{"artwork"}},{"operations",QJsonArray{"enrich"}}};
        const QJsonObject manifest{{"formatVersion",1},{"id",id},{"name","Startup fixture"},{"version",version},{"publisher","Tests"},
            {"license","GPL-2.0-or-later"},{"source",source},
            {"api",QJsonObject{{"addon","^1.0.0"},{"contracts",QJsonObject{{"metadata","^1.0.0"}}}}},
            {"runtime",QJsonObject{{"kind","wasm-message-v1"},{"entry","worker.wasm"}}},
            {"files",QJsonArray{"addon.json","LICENSE","worker.wasm"}},{"contributions",QJsonArray{contribution}}};
        auto document=manifest;
        if(!permissions.isEmpty())document.insert("permissions",QJsonObject{{"required",permissions},{"optional",QJsonObject{}}});
        if(!write(QDir(directory).filePath(QStringLiteral("addon.json")),QJsonDocument(document).toJson())
            ||!write(QDir(directory).filePath(QStringLiteral("LICENSE")),QByteArray("Fixture license.\n"))
            ||!write(QDir(directory).filePath(QStringLiteral("worker.wasm")),QByteArray::fromHex("0061736d01000000")))return {};
        return directory;
    }
private:int sequence=0;
};
QList<DistributionBootstrapEntry> entry(const QString& id,const QString& directory)
{return {{id,QString{},directory}};}
}
#ifdef HAKONE_BOOTSTRAP_RUN_WRAP
namespace {
std::mutex bootstrapGateMutex;
std::shared_ptr<NativeGate> bootstrapGate;
class WorkBlocker {
public:
    const std::shared_ptr<NativeGate> gate=std::make_shared<NativeGate>();
    WorkBlocker(){std::lock_guard<std::mutex> lock(bootstrapGateMutex);bootstrapGate=gate;}
    ~WorkBlocker(){release();std::lock_guard<std::mutex> lock(bootstrapGateMutex);if(bootstrapGate==gate)bootstrapGate.reset();}
    void release(){gate->release();}
};
}
extern "C" DistributionBootstrapBatch __real__ZN3vlc6addons21distributionBootstrap3runENS0_12PackageStoreE5QListINS0_26DistributionBootstrapEntryEE(PackageStore,QList<DistributionBootstrapEntry>);
extern "C" DistributionBootstrapBatch __wrap__ZN3vlc6addons21distributionBootstrap3runENS0_12PackageStoreE5QListINS0_26DistributionBootstrapEntryEE(PackageStore store,QList<DistributionBootstrapEntry> entries)
{
    std::shared_ptr<NativeGate> gate;
    {std::lock_guard<std::mutex> lock(bootstrapGateMutex);gate=bootstrapGate;}
    if(gate){++gate->entries;gate->wait();}
    return __real__ZN3vlc6addons21distributionBootstrap3runENS0_12PackageStoreE5QListINS0_26DistributionBootstrapEntryEE(std::move(store),std::move(entries));
}
#endif
class BootstrapTests:public QObject {
    Q_OBJECT
    std::unique_ptr<AddonStreamLifetime> lifetime;
private slots:
    void init(){QCOMPARE(AddonRepositoryJobs::processCount(),0u);lifetime=std::make_unique<AddonStreamLifetime>();QVERIFY(AddonStreamLifetime::registerApplication(lifetime->handle()));}
    void cleanup(){const auto handle=lifetime->handle();handle.close();QVERIFY(handle.waitQt(std::chrono::seconds(5)));QVERIFY(AddonStreamLifetime::unregisterApplication(handle));lifetime.reset();QCOMPARE(AddonRepositoryJobs::processCount(),0u);}
    void eightRealBundlesPublishOnceOnTheOwner() {
        QTemporaryDir profile;AddonManager manager(profile.path());QSignalSpy state(&manager,&AddonManager::distributionBootstrapChanged),revision(&manager,&AddonManager::revisionChanged);
        QList<DistributionBootstrapEntry> entries;
        for(const auto& item:QList<QPair<QString,QString>>{{"tvmaze","org.videolan.tvmaze"},{"musicbrainz","org.videolan.musicbrainz"},{"lastfm","org.videolan.lastfm"},
            {"french-tv","org.videolan.french-tv"},{"google-images","org.videolan.google-images"},{"radio-browser","org.hakone.radio-browser"},{"thesubtitledb","org.hakone.thesubtitledb"},{"episodes","org.videolan.episodes"}})
            entries.append({item.second,QString{},QDir(QStringLiteral(HAKONE_STARTUP_SOURCE_ROOT "/contrib-addons/examples")).filePath(item.first)});
        connect(&manager,&AddonManager::distributionBootstrapChanged,this,[&]{QCOMPARE(QThread::currentThread(),manager.thread());});
        QVERIFY(manager.beginDistributionBootstrap(entries));QVERIFY(manager.distributionBootstrapInProgress());QCOMPARE(manager.distributionBootstrapGeneration(),quint64(1));QCOMPARE(revision.size(),0);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.distributionBootstrapInProgress(),15000);QCOMPARE(state.size(),2);QCOMPARE(revision.size(),1);QCOMPARE(manager.distributionBootstrapGeneration(),quint64(2));
        const auto results=manager.distributionBootstrapResults();QCOMPARE(results.size(),8);for(const auto& value:results){const auto row=value.toMap();QVERIFY2(row.value("success").toBool(),qPrintable(row.value("error").toString()));}
        QCOMPARE(manager.packages().size(),8);QVERIFY(!manager.beginDistributionBootstrap(entries));
    }
    void suppressedPackageStaysAbsent() {
        QTemporaryDir profile;Packages packages;const auto id=QStringLiteral("org.hakone.suppressed"),directory=packages.make(id);QVERIFY(!directory.isEmpty());
        PackageStore store(profile.path());QString error;QVERIFY(store.setDistributionSuppressed(id,true,&error));AddonManager manager(profile.path());
        QVERIFY(manager.beginDistributionBootstrap(entry(id,directory)));QTRY_VERIFY(!manager.distributionBootstrapInProgress());
        const auto result=manager.distributionBootstrapResults().first().toMap();QVERIFY(result.value("success").toBool());QCOMPARE(result.value("status").toString(),QStringLiteral("suppressed"));
        InstalledPackage installed;QVERIFY(!store.installed(id,&installed));QVERIFY(store.distributionSuppressed(id));
    }
    void validDifferentInstalledReleaseAndGrantsStayByteIdentical() {
        QTemporaryDir profile;Packages packages;const auto id=QStringLiteral("org.hakone.existing");
        const auto current=packages.make(id,QStringLiteral("2.0.0"),QStringLiteral("https://github.com/hakone-org/user-source"));const auto bundled=packages.make(id);
        PackageStore store(profile.path());PackageCandidate candidate;ReleaseIdentity identity;identity.source=QStringLiteral("https://github.com/hakone-org/user-source");identity.tag=QStringLiteral("v2.0.0");identity.developerSource=true;
        QString error;QVERIFY(PackageStore::inspectDirectory(current,identity,&candidate,&error));QVERIFY(store.install(candidate,{},&error));
        const auto path=profile.filePath(QStringLiteral("packages/")+id+QStringLiteral("/state.json"));const auto before=read(path);const auto authority=store.authorityToken();
        AddonManager manager(profile.path());QVERIFY(manager.beginDistributionBootstrap(entry(id,bundled)));QTRY_VERIFY(!manager.distributionBootstrapInProgress());
        QCOMPARE(read(path),before);QVERIFY(authority.current());InstalledPackage actual;QVERIFY(store.installed(id,&actual));QCOMPARE(actual.digest,candidate.digest);QVERIFY(!actual.enabled);QVERIFY(actual.identity.developerSource);
        QCOMPARE(manager.distributionBootstrapResults().first().toMap().value("status").toString(),QStringLiteral("existing"));
    }
    void aNewerBundledReleaseIsAnUpdateUntilApproved() {
        // The startup check lists a newer app copy with the installed version
        // and the permissions it adds; approving it takes it off the list.
        QTemporaryDir profile;Packages packages;const auto id=QStringLiteral("org.hakone.updated");
        const auto first=packages.make(id);
        {AddonManager manager(profile.path());QVERIFY(manager.beginDistributionBootstrap(entry(id,first)));QTRY_VERIFY(!manager.distributionBootstrapInProgress());
         QCOMPARE(manager.distributionBootstrapResults().first().toMap().value("status").toString(),QStringLiteral("installed"));QVERIFY(manager.appUpdates().isEmpty());}
        const QJsonObject network{{"network.http",QJsonObject{{"methods",QJsonArray{"GET"}},{"origins",QJsonArray{"https://api.example.org"}}}}};
        const auto next=packages.make(id,QStringLiteral("1.1.0"),QStringLiteral("https://github.com/hakone-org/startup-fixture"),network);
        AddonManager manager(profile.path());QSignalSpy changed(&manager,&AddonManager::appUpdatesChanged);
        QVERIFY(manager.beginDistributionBootstrap(entry(id,next)));QTRY_VERIFY(!manager.distributionBootstrapInProgress());
        QCOMPARE(changed.count(),1);
        const auto updates=manager.appUpdates();QCOMPARE(updates.size(),1);
        const auto update=updates.first().toMap();
        QCOMPARE(update.value("id").toString(),id);QCOMPARE(update.value("installedVersion").toString(),QStringLiteral("1.0.0"));
        QCOMPARE(update.value("version").toString(),QStringLiteral("1.1.0"));QVERIFY(update.value("enabled").toBool());
        QCOMPARE(update.value("newPermissions").toMap().keys(),QStringList{QStringLiteral("network.http")});
        QVERIFY(manager.stageDistributionPackage(id));QTRY_VERIFY(!manager.busy());
        QCOMPARE(manager.pendingPackage().value("id").toString(),id);
        QVERIFY2(manager.approvePendingPackage(true),qPrintable(manager.errorMessage()));
        QVERIFY(manager.appUpdates().isEmpty());QCOMPARE(changed.count(),2);
        PackageStore store(profile.path());InstalledPackage installed;QVERIFY(store.installed(id,&installed));
        QCOMPARE(installed.manifest.version,QStringLiteral("1.1.0"));QVERIFY(installed.enabled);
        // The same or an older app copy is not an update.
        AddonManager again(profile.path());QVERIFY(again.beginDistributionBootstrap(entry(id,next)));QTRY_VERIFY(!again.distributionBootstrapInProgress());
        QVERIFY(again.appUpdates().isEmpty());
    }
    void damagedStateIsNotOverwrittenAndOtherEntriesContinue() {
        QTemporaryDir profile;Packages packages;const auto damaged=QStringLiteral("org.hakone.damaged"),good=QStringLiteral("org.hakone.good");const auto badDirectory=packages.make(damaged),goodDirectory=packages.make(good);
        {AddonManager seed(profile.path());QVERIFY(seed.ensureDistributionPackage(badDirectory,damaged));}
        const auto path=profile.filePath(QStringLiteral("packages/")+damaged+QStringLiteral("/state.json"));QVERIFY(write(path,QByteArray("broken")));QVERIFY(write(path+QStringLiteral(".backup"),QByteArray("broken backup")));
        AddonManager manager(profile.path());QVERIFY(manager.beginDistributionBootstrap({{damaged,{},badDirectory},{good,{},goodDirectory}}));QTRY_VERIFY(!manager.distributionBootstrapInProgress());
        QCOMPARE(read(path),QByteArray("broken"));QCOMPARE(read(path+QStringLiteral(".backup")),QByteArray("broken backup"));
        const auto results=manager.distributionBootstrapResults();QCOMPARE(results.size(),2);QVERIFY(!results[0].toMap().value("success").toBool());QVERIFY(!results[0].toMap().value("error").toString().isEmpty());QVERIFY(results[1].toMap().value("success").toBool());
        PackageStore store(profile.path());InstalledPackage actual;QVERIFY(store.installed(good,&actual));QVERIFY(actual.enabled);
    }
    void fallbackSelectionRunsOnTheWorkerAndDoesNotHideAnInvalidInstalledPath() {
        QTemporaryDir profile;Packages packages;const auto one=QStringLiteral("org.hakone.fallback"),two=QStringLiteral("org.hakone.invalid");const auto validOne=packages.make(one),validTwo=packages.make(two);
        const auto invalid=packages.root.filePath(QStringLiteral("invalid-installed"));QVERIFY(QDir().mkpath(invalid));
        AddonManager manager(profile.path());QVERIFY(manager.beginDistributionBootstrap({{one,packages.root.filePath(QStringLiteral("missing")),validOne},{two,invalid,validTwo}}));QTRY_VERIFY(!manager.distributionBootstrapInProgress());
        const auto results=manager.distributionBootstrapResults();QCOMPARE(results[0].toMap().value("directory").toString(),validOne);QVERIFY(results[0].toMap().value("success").toBool());QVERIFY(!results[1].toMap().value("success").toBool());
        QCOMPARE(results[1].toMap().value("directory").toString(),invalid);
    }
#ifdef HAKONE_BOOTSTRAP_RUN_WRAP
    void cancelBeforeQueuedWorkStartsInstallsNothing() {
        WorkBlocker blocker;QTemporaryDir profile;Packages packages;const auto id=QStringLiteral("org.hakone.cancel");AddonManager manager(profile.path());
        QVERIFY(manager.beginDistributionBootstrap(entry(id,packages.make(id))));QTRY_VERIFY(blocker.gate->entered.load());QCOMPARE(blocker.gate->entries.load(),1u);QVERIFY(!manager.metadataCatalogueSource());QVERIFY(!manager.packageReader());QVERIFY(!manager.nativeAuthorityReader(id+QStringLiteral("/artwork")));QVERIFY(!manager.storeAuthorityToken().current());manager.cancelDistributionBootstrap();QVERIFY(manager.distributionBootstrapInProgress());blocker.release();QTRY_VERIFY(!manager.distributionBootstrapInProgress());
        PackageStore store(profile.path());QVERIFY(store.installedIds().isEmpty());QCOMPARE(manager.distributionBootstrapResults().first().toMap().value("status").toString(),QStringLiteral("canceled"));
    }
#endif
    void cancelFromTheStartSignalIsNotLostBeforeJobSubmission() {
        QTemporaryDir profile;Packages packages;const auto id=QStringLiteral("org.hakone.inline-cancel");AddonManager manager(profile.path());
        connect(&manager,&AddonManager::distributionBootstrapChanged,this,[&]{if(manager.distributionBootstrapInProgress())manager.cancelDistributionBootstrap();});
        QVERIFY(manager.beginDistributionBootstrap(entry(id,packages.make(id))));QTRY_VERIFY(!manager.distributionBootstrapInProgress());PackageStore store(profile.path());QVERIFY(store.installedIds().isEmpty());
        QCOMPARE(manager.distributionBootstrapResults().first().toMap().value("status").toString(),QStringLiteral("canceled"));
    }
    void cancelDuringCommitFinishesThatEntryThenStops() {
        QTemporaryDir profile;Packages packages;const auto one=QStringLiteral("org.hakone.commit-one"),two=QStringLiteral("org.hakone.commit-two");AddonManager manager(profile.path());
        auto gate=std::make_shared<NativeGate>();PackageStore observed(profile.path());const auto watch=observed.authorityToken().onInvalidated([gate]{gate->wait();});QVERIFY(watch);const auto release=qScopeGuard([gate]{gate->release();});
        QVERIFY(manager.beginDistributionBootstrap({{one,{},packages.make(one)},{two,{},packages.make(two)}}));QTRY_VERIFY_WITH_TIMEOUT(gate->entered.load(),5000);manager.cancelDistributionBootstrap();
        PackageStore store(profile.path());QString error;QVERIFY(!store.setDistributionSuppressed(one,true,&error));QVERIFY(error.contains(QStringLiteral("busy")));QVERIFY(!store.uninstall(one,false,&error));
        gate->release();QTRY_VERIFY(!manager.distributionBootstrapInProgress());InstalledPackage installed;QVERIFY(store.installed(one,&installed));QVERIFY(installed.enabled);QCOMPARE(installed.generation,quint64(2));QVERIFY(!store.installed(two,&installed));
        const auto results=manager.distributionBootstrapResults();QCOMPARE(results[0].toMap().value("status").toString(),QStringLiteral("installed"));QCOMPARE(results[1].toMap().value("status").toString(),QStringLiteral("canceled"));
        QVERIFY(store.setDistributionSuppressed(one,true,&error));QVERIFY(store.uninstall(one,false,&error));QVERIFY(store.distributionSuppressed(one));
    }
#ifdef HAKONE_BOOTSTRAP_RUN_WRAP
    void managerDestructionDoesNotJoinQueuedWorkOrReleaseItsPermitEarly() {
        WorkBlocker blocker;QTemporaryDir profile;Packages packages;const auto id=QStringLiteral("org.hakone.destroy");auto manager=std::make_unique<AddonManager>(profile.path());
        QVERIFY(manager->beginDistributionBootstrap(entry(id,packages.make(id))));QTRY_VERIFY(blocker.gate->entered.load());QCOMPARE(blocker.gate->entries.load(),1u);QCOMPARE(AddonRepositoryJobs::processCount(),1u);manager.reset();QCOMPARE(AddonRepositoryJobs::processCount(),1u);
        bool served=false;QTimer::singleShot(0,[&]{served=true;});QTRY_VERIFY(served);blocker.release();QTRY_COMPARE(AddonRepositoryJobs::processCount(),0u);PackageStore store(profile.path());QVERIFY(store.installedIds().isEmpty());
    }
#endif
    void managerCanBeDestroyedFromTheStartSignal() {
        QTemporaryDir profile;Packages packages;const auto id=QStringLiteral("org.hakone.reentrant");auto* manager=new AddonManager(profile.path());QPointer<AddonManager> alive(manager);
        connect(manager,&AddonManager::distributionBootstrapChanged,this,[&]{if(manager->distributionBootstrapInProgress())delete manager;});
        QVERIFY(!manager->beginDistributionBootstrap(entry(id,packages.make(id))));QVERIFY(!alive);QCOMPARE(AddonRepositoryJobs::processCount(),0u);
    }
#ifdef HAKONE_BOOTSTRAP_RUN_WRAP
    void legacySummaryWaitsForTheWholeBootstrap() {
        WorkBlocker blocker;QTemporaryDir profile;Packages packages;const auto id=QStringLiteral("org.hakone.summary");AddonManager manager(profile.path());
        QVERIFY(manager.beginDistributionBootstrap(entry(id,packages.make(id))));QTRY_VERIFY(blocker.gate->entered.load());QCOMPARE(blocker.gate->entries.load(),1u);AddonPackageSummaries summaries(&manager,lifetime->handle());QSignalSpy finished(&summaries,&AddonPackageSummaries::loadingFinished);summaries.refresh();
        QCoreApplication::processEvents();QVERIFY(summaries.loading());QVERIFY(!summaries.isCurrent());QCOMPARE(finished.size(),0);QCOMPARE(AddonRepositoryJobs::processCount(),1u);
        blocker.release();QTRY_VERIFY(!manager.distributionBootstrapInProgress());QTRY_VERIFY(summaries.isCurrent());QCOMPARE(summaries.rows().size(),1);QCOMPARE(finished.size(),1);summaries.close();
    }
#endif
    void syncSourceChangeAndUnchangedRevisionSemanticsRemain() {
        QTemporaryDir profile;Packages packages;const auto id=QStringLiteral("org.hakone.sync"),first=packages.make(id),other=packages.make(id,QStringLiteral("1.0.0"),QStringLiteral("https://github.com/hakone-org/other"));
        AddonManager manager(profile.path());QVERIFY(manager.ensureDistributionPackage(first,id));const auto revision=manager.revision();QVERIFY(manager.ensureDistributionPackage(first,id));QCOMPARE(manager.revision(),revision);
        QVERIFY(!manager.ensureDistributionPackage(other,id));QVERIFY(manager.errorMessage().contains(QStringLiteral("source changed")));QVERIFY(!manager.beginDistributionBootstrap(entry(id,first)));
    }
    void capturedManagerReadersRefuseBootstrapWithoutRevocation_data() {
        QTest::addColumn<int>("factory");for(int i=0;i<6;++i)QTest::newRow(qPrintable(QString::number(i)))<<i;
    }
    void capturedManagerReadersRefuseBootstrapWithoutRevocation() {
        QFETCH(int,factory);QTemporaryDir profile;Packages packages;const auto id=QStringLiteral("org.hakone.capture");AddonManager manager(profile.path());
        const auto external=PackageStore(profile.path()).authorityToken();
        switch(factory) {
            case 0:QVERIFY(manager.metadataCatalogueSource());break;
            case 1:QVERIFY(manager.packageReader());break;
            case 2:QVERIFY(manager.nativeAuthorityReader(id+QStringLiteral("/artwork")));break;
            case 3:QVERIFY(manager.storeAuthorityToken().current());break;
            case 4:QVERIFY(manager.uiAssetReader(id+QStringLiteral("/artwork")));break;
            case 5:{InstalledPackage package;package.manifest.id=id;QVERIFY(manager.credentialResolver(package));break;}
        }
        QVERIFY(!manager.beginDistributionBootstrap(entry(id,packages.make(id))));QVERIFY(external.current());QCOMPARE(AddonRepositoryJobs::processCount(),0u);QCOMPARE(manager.distributionBootstrapGeneration(),quint64(0));
    }
    void backupRecoveryIsKeptWithoutReplacingTheSavedState() {
        QTemporaryDir profile;Packages packages;const auto id=QStringLiteral("org.hakone.recovery"),directory=packages.make(id);
        {AddonManager seed(profile.path());QVERIFY(seed.ensureDistributionPackage(directory,id));}
        const auto path=profile.filePath(QStringLiteral("packages/")+id+QStringLiteral("/state.json"));const auto backup=read(path+QStringLiteral(".backup"));QVERIFY(!backup.isEmpty());QVERIFY(write(path,QByteArray("broken")));
        AddonManager manager(profile.path());QVERIFY(manager.beginDistributionBootstrap(entry(id,directory)));QTRY_VERIFY(!manager.distributionBootstrapInProgress());
        const auto row=manager.distributionBootstrapResults().first().toMap();QVERIFY(row.value("success").toBool());QVERIFY(row.value("recoveryRequired").toBool());QCOMPARE(read(path),QByteArray("broken"));QCOMPARE(read(path+QStringLiteral(".backup")),backup);
    }
#ifdef HAKONE_BOOTSTRAP_RUN_WRAP
    void canceledLegacySummaryShowsFailedRowsRatherThanEmptyReady() {
        WorkBlocker blocker;QTemporaryDir profile;Packages packages;const auto id=QStringLiteral("org.hakone.failed-summary");AddonManager manager(profile.path());
        QVERIFY(manager.beginDistributionBootstrap(entry(id,packages.make(id))));QTRY_VERIFY(blocker.gate->entered.load());QCOMPARE(blocker.gate->entries.load(),1u);AddonPackageSummaries summaries(&manager,lifetime->handle());summaries.refresh();manager.cancelDistributionBootstrap();blocker.release();
        QTRY_VERIFY(!manager.distributionBootstrapInProgress());QTRY_VERIFY(summaries.isCurrent());QCOMPARE(summaries.rows().size(),1);const auto row=summaries.rows().first().toMap();QVERIFY(row.value("bootstrapFailed").toBool());QVERIFY(!row.value("installed").toBool());QVERIFY(!row.value("enabled").toBool());summaries.close();
    }
#endif
    void boundsAndDisplayAttachmentRefuseBeforeAnyJob() {
        QTemporaryDir profile;AddonManager manager(profile.path());QList<DistributionBootstrapEntry> entries;
        for(int i=0;i<17;++i)entries.append({QStringLiteral("org.hakone.bound-")+QString::number(i),{},QStringLiteral("missing")});
        QVERIFY(!manager.beginDistributionBootstrap(entries));QCOMPARE(AddonRepositoryJobs::processCount(),0u);
        QObject attached(&manager);attached.setObjectName(QStringLiteral("hakoneDisplayCatalogue"));QVERIFY(!manager.beginDistributionBootstrap(entry(QStringLiteral("org.hakone.bound"),QStringLiteral("missing"))));
    }
};
QTEST_GUILESS_MAIN(BootstrapTests)
#include "test_addon_distribution_bootstrap.moc"
