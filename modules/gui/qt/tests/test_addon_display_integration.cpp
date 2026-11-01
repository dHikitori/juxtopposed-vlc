// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_catalogue_view.hpp"
#include "addons/addon_manager.hpp"
#include "addons/addon_package_summaries.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <memory>
#include <functional>
#include <thread>
using namespace vlc::addons;
#ifdef HAKONE_DISPLAY_CREDENTIAL_CREATE_WRAP
namespace {
// The build stages the bundled add-ons with their modules; the others are test fixtures.
QString packageRoot(const QString& name)
{
    const auto bundled=QStringLiteral(VLC_ADDON_BUNDLED_ROOT "/")+name;
    return QFile::exists(bundled+QStringLiteral("/addon.json"))?bundled:QStringLiteral(VLC_ADDON_SOURCE_ROOT "/modules/gui/qt/tests/fixtures/addons/")+name;
}
}
namespace {
std::function<void(const CredentialStore::Created&)> afterCredentialCreate;
unsigned credentialCreateCalls=0;
}
extern "C" CredentialStore::Created __real__ZN3vlc6addons15CredentialStore6createERK7QStringS4_RK10QByteArrayb(CredentialStore*,const QString&,const QString&,const QByteArray&,bool);
extern "C" CredentialStore::Created __wrap__ZN3vlc6addons15CredentialStore6createERK7QStringS4_RK10QByteArrayb(CredentialStore* owner,const QString& package,const QString& field,const QByteArray& value,bool sessionOnly)
{
    ++credentialCreateCalls;
    auto result=__real__ZN3vlc6addons15CredentialStore6createERK7QStringS4_RK10QByteArrayb(owner,package,field,value,sessionOnly);
    const auto hook=afterCredentialCreate;if(hook)hook(result);return result;
}
#endif
#ifdef HAKONE_DISPLAY_ACQUIRE_WRAP
namespace { std::function<void(AddonCatalogueView*)> afterDisplayAcquire; }
extern "C" QString __real__ZN3vlc6addons18AddonCatalogueView15acquireSettingsERK7QString(AddonCatalogueView*,const QString&);
extern "C" QString __wrap__ZN3vlc6addons18AddonCatalogueView15acquireSettingsERK7QString(AddonCatalogueView* owner,const QString& package)
{
    auto ticket=__real__ZN3vlc6addons18AddonCatalogueView15acquireSettingsERK7QString(owner,package);
    const auto hook=afterDisplayAcquire;if(hook)hook(owner);return ticket;
}
#endif
namespace {
QList<DistributionBootstrapEntry> bundles()
{
    QList<DistributionBootstrapEntry> result;
    for(const auto& pair:QList<QPair<QString,QString>>{
        {"tvmaze","org.videolan.tvmaze"},{"musicbrainz","org.videolan.musicbrainz"},{"lastfm","org.videolan.lastfm"},
        {"french-tv","org.videolan.french-tv"},{"google-images","org.videolan.google-images"},
        {"radio-browser","org.hakone.radio-browser"},{"episodes","org.videolan.episodes"},{"thesubtitledb","org.hakone.thesubtitledb"}})
        result.append({pair.second,{},packageRoot(pair.first)});
    return result;
}
bool write(const QString& path,const QByteArray& bytes)
{QFile file(path);return file.open(QIODevice::WriteOnly)&&file.write(bytes)==bytes.size();}
QByteArray read(const QString& path)
{QFile file(path);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray{};}
bool sameLegacyRows(const QVariantList& oldRows,const QVariantList& displayRows,QString* reason)
{
    if(oldRows.size()!=displayRows.size()){*reason=QStringLiteral("Row count differs");return false;}
    QMap<QString,QVariantMap> actual;
    for(const auto& value:displayRows){const auto row=value.toMap();const auto id=row.value("id").toString();if(actual.contains(id)){*reason=QStringLiteral("Duplicate display ID");return false;}actual.insert(id,row);}
    QJsonArray added;unsigned compared=0;
    for(const auto& value:oldRows){
        const auto row=value.toMap();const auto id=row.value("id").toString();
        if(!actual.contains(id)){*reason=id+QStringLiteral(": missing row");return false;}
        const auto display=actual.value(id);QJsonArray newKeys;
        for(auto it=row.cbegin();it!=row.cend();++it){
            if(!display.contains(it.key()) || display.value(it.key()).metaType()!=it.value().metaType() || display.value(it.key())!=it.value()){
                *reason=id+QLatin1Char(':')+it.key()+QStringLiteral(": legacy key/type/value differs");return false;}
            ++compared;
        }
        for(auto it=display.cbegin();it!=display.cend();++it)if(!row.contains(it.key()))newKeys.append(it.key());
        added.append(QJsonObject{{"id",id},{"displayOnlyKeys",newKeys}});
    }
    qInfo().noquote()<<"Legacy key/type/value checks:"<<compared<<"Added display keys:"<<QJsonDocument(added).toJson(QJsonDocument::Compact);
    return true;
}
bool settingsPackage(const QString& directory,const QString& version)
{
    if(!QDir().mkpath(directory))return false;
    const QJsonObject manifest{{"formatVersion",1},{"id","org.example.settings"},{"name","Settings fixture"},{"version",version},{"publisher","Tests"},
        {"license","GPL-2.0-or-later"},{"source","https://github.com/hakone-org/settings-fixture"},
        {"api",QJsonObject{{"addon","^1.0.0"},{"contracts",QJsonObject{{"metadata","^1.0.0"},{"settings","^1.0.0"}}}}},
        {"runtime",QJsonObject{{"kind","wasm-message-v1"},{"entry","worker.wasm"}}},
        {"files",QJsonArray{"addon.json","LICENSE","worker.wasm","settings.json"}},{"settings",QJsonObject{{"version",1},{"schema","settings.json"}}},
        {"contributions",QJsonArray{QJsonObject{{"id","artwork"},{"type","metadata.provider"},{"contract","metadata@1"},{"features",QJsonArray{"artwork"}},{"operations",QJsonArray{"enrich"}}}}}};
    return write(QDir(directory).filePath(QStringLiteral("addon.json")),QJsonDocument(manifest).toJson())
        &&write(QDir(directory).filePath(QStringLiteral("LICENSE")),QByteArray("Test license"))
        &&write(QDir(directory).filePath(QStringLiteral("worker.wasm")),QByteArray::fromHex("0061736d01000000"))
        &&write(QDir(directory).filePath(QStringLiteral("settings.json")),QJsonDocument(QJsonObject{{"type","object"},{"properties",QJsonObject{{"label",QJsonObject{{"type","string"},{"default","seed"}}}}},{"required",QJsonArray{"label"}},{"additionalProperties",false}}).toJson());
}
std::optional<PackageSummarySnapshot> legacyReader(AddonManager& manager)
{
    const auto reader=manager.packageReader();std::optional<PackageSummarySnapshot> result;
    // Baseline only: the owned legacy reader has no Manager on this joined thread.
    std::thread thread([&]{if(reader)result=reader();});thread.join();return result;
}
}
// These two tests exercise real Manager outer-ticket identity. Their display
// reader is in memory so an unrelated concurrent store.lock cannot decide the
// ABA result; the actual eight-package/store tests below keep real readers.
AddonCatalogueView* ticketView(AddonManager* manager,AddonStreamLifetime::Handle lifetime)
{
    const QPointer<AddonManager> owner(manager);
    AddonCatalogueView::Backend backend;
    backend.capture=[owner]() -> std::optional<DisplayCatalogueSource> {
        if(!owner)return std::nullopt;
        auto source=owner->displayCatalogueSource();if(!source)return std::nullopt;
        DisplayCatalogueSnapshot snapshot;snapshot.authority=source->queuedAuthority;snapshot.queuedAuthority=source->queuedAuthority;
        DisplayCataloguePackage row;row.package.manifest.id=QStringLiteral("org.example.settings");row.package.manifest.name=QStringLiteral("Ticket fixture");
        row.package.manifest.version=QStringLiteral("1.0.0");row.package.identity.source=QStringLiteral("https://github.com/hakone-org/ticket-fixture");
        row.package.digest=QByteArray(64,'a');row.package.generation=1;row.package.enabled=true;row.savedEnabled=true;row.messagesReady=true;
        row.schema={{"type","object"},{"properties",QJsonObject{{"label",QJsonObject{{"type","string"},{"default","seed"}}}}}};
        snapshot.packages.append(row);
        source->reader=[snapshot](const DisplayCatalogueRequest&,DisplayCatalogueSnapshot* out,QString*) { *out=snapshot;return true; };
        return source;
    };
    backend.subscribe=[owner](std::function<void()> wake) {return owner?owner->watchDisplayCatalogue(std::move(wake)):StoreAuthorityChanges{};};
    auto* view=new AddonCatalogueView(std::move(backend),std::move(lifetime),manager);
    view->setObjectName(QStringLiteral("hakoneDisplayCatalogue"));
    QObject::connect(manager,&AddonManager::retiring,view,&AddonCatalogueView::close,Qt::DirectConnection);
    QObject::connect(view,&AddonCatalogueView::freshnessChanged,manager,&AddonManager::catalogueFreshnessChanged);
    return view;
}
class DisplayIntegrationTest final:public QObject {
    Q_OBJECT
    std::unique_ptr<AddonStreamLifetime> lifetime;
private slots:
    void init(){
        QCOMPARE(AddonRepositoryJobs::processCount(),0u);lifetime=std::make_unique<AddonStreamLifetime>();
        QVERIFY(AddonStreamLifetime::registerApplication(lifetime->handle()));
    }
    void cleanup(){
        const auto handle=lifetime->handle();handle.close();QVERIFY(handle.waitQt(std::chrono::seconds(5)));
        QVERIFY(AddonStreamLifetime::unregisterApplication(handle));lifetime.reset();
        QCOMPARE(AddonRepositoryJobs::processCount(),0u);
    }
    void outerTicketsCannotReleaseOrReadASuccessorViewsInnerTicket(){
        QTemporaryDir profile;AddonManager manager(profile.path());
        auto first=std::unique_ptr<AddonCatalogueView>(ticketView(&manager,lifetime->handle()));QTRY_VERIFY(first->isCurrent());
        const auto old=manager.prepareDisplayFeature(QStringLiteral("subtitles"));QVERIFY(!old.isEmpty());QVERIFY(manager.displayRequestCurrent(old));
        QVERIFY(first->requestCurrent(QStringLiteral("1")));first->setObjectName(QStringLiteral("retiredDisplayCatalogue"));
        auto second=std::unique_ptr<AddonCatalogueView>(ticketView(&manager,lifetime->handle()));QTRY_VERIFY(second->isCurrent());
        QVERIFY(!manager.displayRequestCurrent(old));
        const auto fresh=manager.prepareDisplayFeature(QStringLiteral("subtitles"));QVERIFY(!fresh.isEmpty());QVERIFY(old!=fresh);
        QVERIFY(second->requestCurrent(QStringLiteral("1")));QVERIFY(!first->requestCurrent(QStringLiteral("1")));
        manager.releaseDisplayRequest(old);QVERIFY(manager.displayRequestCurrent(fresh));QVERIFY(second->requestCurrent(QStringLiteral("1")));
        QStringList other;for(int i=0;i<31;++i){const auto ticket=manager.prepareDisplayFeature(QStringLiteral("subtitles"));QVERIFY(!ticket.isEmpty());other.append(ticket);}
        QVERIFY(manager.prepareDisplayFeature(QStringLiteral("subtitles")).isEmpty());
        for(const auto& ticket:other)manager.releaseDisplayRequest(ticket);
        second->close();QVERIFY(!manager.displayRequestCurrent(fresh));manager.releaseDisplayRequest(fresh);
    }
    void aDestroyedViewNotifiesAndNeverRetargetsItsOuterRequest(){
        QTemporaryDir profile;AddonManager manager(profile.path());
        auto view=std::make_unique<AddonCatalogueView>(&manager,lifetime->handle());QTRY_VERIFY(view->isCurrent());
        const auto ticket=manager.prepareDisplayFeature(QStringLiteral("subtitles"));QVERIFY(!ticket.isEmpty());
        QSignalSpy changed(&manager,&AddonManager::catalogueFreshnessChanged);view.reset();QVERIFY(!manager.displayRequestCurrent(ticket));QVERIFY(changed.size()>0);
        auto successor=std::make_unique<AddonCatalogueView>(&manager,lifetime->handle());QTRY_VERIFY(successor->isCurrent());
        const auto fresh=manager.prepareDisplayFeature(QStringLiteral("subtitles"));QVERIFY(!fresh.isEmpty());manager.releaseDisplayRequest(ticket);
        QVERIFY(manager.displayRequestCurrent(fresh));manager.releaseDisplayRequest(fresh);
    }
#ifdef HAKONE_DISPLAY_ACQUIRE_WRAP
    void reentryDuringAcquisitionCannotPublishALateOuterRequest_data(){
        QTest::addColumn<int>("action");QTest::newRow("closed-view")<<0;QTest::newRow("deleted-manager")<<1;QTest::newRow("replacement-view")<<2;
    }
    void reentryDuringAcquisitionCannotPublishALateOuterRequest(){
        QFETCH(int,action);QTemporaryDir profile,input;QVERIFY(settingsPackage(input.path(),QStringLiteral("1.0.0")));
        auto manager=std::make_unique<AddonManager>(profile.path());QVERIFY(manager->installLocalPackage(input.path(),true));QVERIFY(manager->setEnabled(QStringLiteral("org.example.settings"),true));
        QPointer<AddonCatalogueView> first=action==2?ticketView(manager.get(),lifetime->handle()):new AddonCatalogueView(manager.get(),lifetime->handle()),successor;
        QTRY_VERIFY(first->isCurrent());const auto reset=qScopeGuard([]{afterDisplayAcquire={};});
        afterDisplayAcquire=[&](AddonCatalogueView* view){
            if(action==0)view->close();
            else if(action==1)manager.reset();
            else {view->setObjectName(QStringLiteral("retiredDisplayCatalogue"));successor=ticketView(manager.get(),lifetime->handle());}
        };
        const auto request=manager->prepareDisplaySettings(QStringLiteral("org.example.settings"));QVERIFY(request.isEmpty());afterDisplayAcquire={};
        if(action==1){QVERIFY(!manager);QVERIFY(!first);}
        else if(action==2){QTRY_VERIFY(successor->isCurrent());QVERIFY(!first->requestCurrent(QStringLiteral("1")));
            const auto fresh=manager->prepareDisplaySettings(QStringLiteral("org.example.settings"));QVERIFY(!fresh.isEmpty());QVERIFY(manager->displayRequestCurrent(fresh));manager->releaseDisplayRequest(fresh);}
        else QVERIFY(!first->requestCurrent(QStringLiteral("1")));
    }
#endif
    void managerDestructionReleasesAnExactViewThatOutlivesItsParent(){
        QTemporaryDir profile;auto manager=std::make_unique<AddonManager>(profile.path());const QPointer<AddonManager> owner(manager.get());
        AddonCatalogueView::Backend backend{
            [owner]{return owner?owner->displayCatalogueSource():std::nullopt;},
            [owner](std::function<void()> wake){return owner?owner->watchDisplayCatalogue(std::move(wake)):StoreAuthorityChanges{};}};
        auto view=std::make_unique<AddonCatalogueView>(std::move(backend),lifetime->handle(),manager.get());
        view->setObjectName(QStringLiteral("hakoneDisplayCatalogue"));QTRY_VERIFY(view->isCurrent());
        const auto request=manager->prepareDisplayFeature(QStringLiteral("subtitles"));QVERIFY(!request.isEmpty());QVERIFY(view->requestCurrent(QStringLiteral("1")));
        // This injected View has no Manager-retiring connection. Its exact
        // request must be released by the detached destruction hook itself.
        view->setParent(nullptr);manager.reset();QVERIFY(!owner);
        QStringList tickets;for(int i=0;i<32;++i){const auto ticket=view->acquireFeature(QStringLiteral("subtitles"));QVERIFY(!ticket.isEmpty());tickets.append(ticket);}
        QVERIFY(view->acquireFeature(QStringLiteral("subtitles")).isEmpty());
        for(const auto& ticket:tickets)view->release(ticket);view->close();
    }
    void documentLoadingAndUnrelatedMutationKeepTheCapturedSettingsStamp(){
        QTemporaryDir profile;AddonManager manager(profile.path());QVERIFY(manager.beginDistributionBootstrap(bundles()));
        AddonCatalogueView view(&manager,lifetime->handle());AddonPackageSummaries summary(&manager,lifetime->handle());summary.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(view.isCurrent() && summary.isCurrent(),15000);QVERIFY(summary.retainedAvailable());
        const auto id=QStringLiteral("org.videolan.tvmaze"),stamp=manager.displaySettingsStamp(id);QVERIFY(!stamp.isEmpty());
        const auto ticket=manager.prepareDisplaySettings(id);QVERIFY(!ticket.isEmpty());QVERIFY(summary.isCurrent());QVERIFY(summary.retainedAvailable());QCOMPARE(manager.displaySettingsState(id),QStringLiteral("pending"));
        QTRY_VERIFY(view.isCurrent() && manager.displaySettingsState(id)==QLatin1String("ready"));QCOMPARE(manager.displaySettingsStamp(id),stamp);
        AddonManager other(profile.path());QVERIFY(other.setEnabled(QStringLiteral("org.videolan.musicbrainz"),false));
        QVERIFY(!summary.retainedAvailable());QVERIFY(!summary.isCurrent());
        QTRY_VERIFY(view.isCurrent() && summary.isCurrent());QVERIFY(summary.retainedAvailable());QCOMPARE(manager.displaySettingsStamp(id),stamp);
        QVERIFY(manager.saveDisplaySettings(id,stamp,{},{}));manager.releaseDisplayRequest(ticket);
    }
    void selectedPackageUpdateAndRollbackNeverReviveADisplayedStamp(){
        QTemporaryDir profile,first,second;QVERIFY(settingsPackage(first.path(),QStringLiteral("1.0.0")));QVERIFY(settingsPackage(second.path(),QStringLiteral("1.1.0")));
        AddonManager manager(profile.path());QVERIFY(manager.installLocalPackage(first.path(),true));QVERIFY(manager.setEnabled(QStringLiteral("org.example.settings"),true));
        AddonCatalogueView view(&manager,lifetime->handle());QTRY_VERIFY(view.isCurrent());const auto id=QStringLiteral("org.example.settings"),ticket=manager.prepareDisplaySettings(id);QVERIFY(!ticket.isEmpty());QTRY_VERIFY(view.isCurrent() && view.settingsReady(id));
        const auto initial=view.settingsStamp(id);QVERIFY(!initial.isEmpty());QVERIFY(manager.installLocalPackage(second.path(),true));QTRY_VERIFY(view.isCurrent() && view.settingsReady(id));QVERIFY(view.settingsStamp(id)!=initial);
        QVERIFY(!manager.saveDisplaySettings(id,initial,{{"label","old draft"}},{}));QVERIFY(manager.rollback(id));QTRY_VERIFY(view.isCurrent() && view.settingsReady(id));
        QVERIFY(view.settingsStamp(id)!=initial);QVERIFY(!manager.saveDisplaySettings(id,initial,{{"label","old draft"}},{}));
        QVERIFY(manager.saveDisplaySettings(id,view.settingsStamp(id),{{"label","fresh draft"}},{}));manager.releaseDisplayRequest(ticket);
    }
    void nativeTransportHintsStayMemoryOnlyAndFollowRouteBinding(){
        QTemporaryDir profile;AddonManager manager(profile.path());AddonCatalogueView view(&manager,lifetime->handle());QTRY_VERIFY(view.isCurrent());
        manager.setNativeStreamAvailable(true);QVERIFY(!manager.displayCapabilities().value("mediaStreamKnown").toBool());QVERIFY(!manager.displayCapabilities().value("mediaStreamAvailable").toBool());
        const auto captures=view.captures();manager.setNativeStreamTransportHint(true,false);QVERIFY(manager.displayCapabilities().value("mediaStreamKnown").toBool());QVERIFY(!manager.displayCapabilities().value("mediaStreamAvailable").toBool());
        manager.setNativeStreamTransportHint(true,true);QVERIFY(manager.displayCapabilities().value("mediaStreamAvailable").toBool());manager.setNativeStreamAvailable(false);QVERIFY(!manager.displayCapabilities().value("mediaStreamKnown").toBool());QVERIFY(!manager.displayCapabilities().value("mediaStreamAvailable").toBool());
        QTest::qWait(50);QCOMPARE(view.captures(),captures);
    }
#ifdef HAKONE_DISPLAY_CREDENTIAL_CREATE_WRAP
    void aStaleDisplayedStampDoesNotCreateAKey(){
        QTemporaryDir profile;AddonManager manager(profile.path());const auto input=bundles().last();QVERIFY(manager.beginDistributionBootstrap({input}));
        AddonCatalogueView view(&manager,lifetime->handle());QTRY_VERIFY_WITH_TIMEOUT(view.isCurrent(),15000);const auto id=input.expectedId,ticket=manager.prepareDisplaySettings(id);QVERIFY(!ticket.isEmpty());QTRY_VERIFY(view.isCurrent() && view.settingsReady(id));
        const auto stamp=view.settingsStamp(id);AddonManager other(profile.path());QVERIFY(other.setEnabled(id,false));credentialCreateCalls=0;
        QVERIFY(!manager.saveDisplaySettings(id,stamp,{{"language","en"}},{{"apiKey",QVariantMap{{"operation","replace"},{"value","unit-stale-draft"},{"sessionOnly",true}}}}));
        QCOMPARE(credentialCreateCalls,0u);QVERIFY(manager.credentialPresenceSnapshot().value(id).isEmpty());manager.releaseDisplayRequest(ticket);
    }
    void aPeerPackageChangeAtTheFinalCasRollsBackOnlyTheNewKey(){
        const auto oldPath=qgetenv("PATH");qputenv("PATH",QByteArray{});const auto environment=qScopeGuard([&]{qputenv("PATH",oldPath);});
        QTemporaryDir profile;AddonManager manager(profile.path());const auto input=bundles().last();QVERIFY(manager.beginDistributionBootstrap({input}));
        AddonCatalogueView view(&manager,lifetime->handle());QTRY_VERIFY_WITH_TIMEOUT(view.isCurrent(),15000);const auto id=input.expectedId;
        QVERIFY(manager.saveAddonSettings(id,{{"language","en"}},{{"apiKey",QVariantMap{{"operation","replace"},{"value","unit-original-key"},{"sessionOnly",true}}}}));
        QTRY_VERIFY(view.isCurrent());const auto ticket=manager.prepareDisplaySettings(id);QVERIFY(!ticket.isEmpty());QTRY_VERIFY(view.isCurrent() && view.settingsReady(id));
        PackageStore store(profile.path());QJsonObject saved;quint64 beforeRevision=0;QVERIFY(store.settings(id,&saved,&beforeRevision));const auto original=saved.value("_credentials").toObject().value("apiKey").toString();QVERIFY(!original.isEmpty());
        const auto stamp=view.settingsStamp(id);AddonManager other(profile.path());QString created;bool changed=false;credentialCreateCalls=0;
        afterCredentialCreate=[&](const CredentialStore::Created& value){created=value.reference;changed=other.setEnabled(id,false);};const auto reset=qScopeGuard([]{afterCredentialCreate={};});
        QVERIFY(!manager.saveDisplaySettings(id,stamp,{{"language","en"}},{{"apiKey",QVariantMap{{"operation","replace"},{"value","unit-new-draft"},{"sessionOnly",true}}}}));
        QVERIFY(changed);QCOMPARE(credentialCreateCalls,1u);QVERIFY(!created.isEmpty());QVERIFY(created!=original);
        QJsonObject after;quint64 afterRevision=0;QVERIFY(store.settings(id,&after,&afterRevision));QCOMPARE(afterRevision,beforeRevision);QCOMPARE(after.value("_credentials"),saved.value("_credentials"));
        const auto presence=manager.credentialPresenceSnapshot().value(id);QVERIFY(presence.value(original));QVERIFY(!presence.contains(created));manager.releaseDisplayRequest(ticket);
    }
#endif
    void actualEightBundleStartupHasFullLegacyRowParityAndMemoryFacades(){
        QTemporaryDir profile;AddonManager manager(profile.path());manager.setWorkerExecutable(QStringLiteral("/bin/true"));
        QSignalSpy revision(&manager,&AddonManager::revisionChanged);
        QVERIFY(manager.beginDistributionBootstrap(bundles()));
        AddonCatalogueView view(&manager,lifetime->handle());AddonPackageSummaries summary(&manager,lifetime->handle());summary.refresh();
        QVERIFY(view.loading());QVERIFY(!view.isCurrent());QVERIFY(summary.loading());
        QTRY_VERIFY_WITH_TIMEOUT(!manager.distributionBootstrapInProgress() && view.isCurrent() && summary.isCurrent(),15000);
        QCOMPARE(revision.size(),1);QCOMPARE(manager.distributionBootstrapResults().size(),8);QCOMPARE(view.rows().size(),8);
        for(const auto& value:manager.distributionBootstrapResults())QVERIFY2(value.toMap().value("success").toBool(),qPrintable(value.toMap().value("error").toString()));
        const auto baseline=legacyReader(manager);QVERIFY(baseline);QVERIFY(baseline->authority.current());
        QString error;QVERIFY2(sameLegacyRows(baseline->rows,view.rows(),&error),qPrintable(error));
        QVERIFY2(sameLegacyRows(manager.packages(),view.rows(),&error),qPrintable(error));QCOMPARE(summary.rows(),view.rows());
        QObject player;player.setProperty("isStarted",true);player.setProperty("seekable",true);player.setProperty("pausable",true);player.setProperty("ratechangable",false);player.setProperty("hasVideoOutput",false);
        manager.setMediaCapabilitiesSource(&player);
        const auto media=manager.displayCapabilities().value("media").toMap();QVERIFY(media.value("known").toBool());QVERIFY(media.value("loaded").toBool());QVERIFY(media.value("seekable").toBool());QVERIFY(!media.value("rateChange").toBool());
        const auto captures=view.captures(),wakes=view.nativeWakes(),publications=view.publications();
        player.setProperty("isStarted",false);player.setProperty("seekable",false);player.setProperty("hasVideoOutput",true);manager.capabilitiesChanged();
        const auto changedMedia=manager.displayCapabilities().value("media").toMap();QVERIFY(changedMedia.value("known").toBool());QVERIFY(!changedMedia.value("loaded").toBool());QVERIFY(!changedMedia.value("seekable").toBool());QVERIFY(changedMedia.value("videoOutput").toBool());
        for(int i=0;i<200;++i){
            manager.displayCapabilities();manager.displayProvidersForFeature(QStringLiteral("subtitles"));manager.displaySettingsSchema(QStringLiteral("org.hakone.thesubtitledb"));
            manager.displaySettingsValues(QStringLiteral("org.hakone.thesubtitledb"));manager.displaySettingsMessages(QStringLiteral("org.hakone.thesubtitledb"));manager.displaySettingsSecretStatus(QStringLiteral("org.hakone.thesubtitledb"));
            summary.rows();view.contributions(QStringLiteral("ui.section"));view.themes();view.capabilities();
        }
        QTest::qWait(80);QCOMPARE(view.captures(),captures);QCOMPARE(view.nativeWakes(),wakes);QCOMPARE(view.publications(),publications);
        bool found=false;for(const auto& value:view.rows())if(value.toMap().value("id").toString()==QLatin1String("org.hakone.thesubtitledb")){
            found=true;QVERIFY(value.toMap().value("configurationKnown").toBool());QVERIFY(!value.toMap().value("configured").toBool());}
        QVERIFY(found);
    }
    void unsafeBackupCannotBecomeARichRecoverySummary_data(){
        QTest::addColumn<int>("mode");QTest::newRow("malformed")<<0;QTest::newRow("wrong-package")<<1;QTest::newRow("over-cap")<<2;
#ifdef Q_OS_UNIX
        QTest::newRow("external-symlink")<<3;
#endif
        QTest::newRow("valid-current-envelope-bad-settings")<<4;
    }
    void unsafeBackupCannotBecomeARichRecoverySummary(){
        QFETCH(int,mode);QTemporaryDir profile;AddonManager manager(profile.path());
        const auto input=bundles().first();QVERIFY(manager.beginDistributionBootstrap({input}));
        AddonCatalogueView view(&manager,lifetime->handle());QTRY_VERIFY_WITH_TIMEOUT(!manager.distributionBootstrapInProgress() && view.isCurrent(),15000);
        const QString id=input.expectedId,path=profile.filePath(QStringLiteral("packages/")+id+QStringLiteral("/state.json")),backup=path+QStringLiteral(".backup");
        const auto current=read(path),retained=read(backup);QVERIFY(!retained.isEmpty());
        PackageStore store(profile.path());store.invalidateAuthority();
        if(mode==4){
            auto state=QJsonDocument::fromJson(current).object();auto release=state.value("current").toObject();release.insert("settings",QJsonArray{});state.insert("current",release);
            QVERIFY(write(path,QJsonDocument(state).toJson()));
        } else {
            QVERIFY(write(path,QByteArray("broken")));
            if(mode==0)QVERIFY(write(backup,QByteArray("[]")));
            if(mode==1){auto state=QJsonDocument::fromJson(retained).object();state.insert("id","org.example.foreign");QVERIFY(write(backup,QJsonDocument(state).toJson()));}
            if(mode==2)QVERIFY(write(backup,QByteArray(4*1024*1024+1,'x')));
#ifdef Q_OS_UNIX
            if(mode==3){const auto sentinel=profile.filePath(QStringLiteral("external-backup.json"));QVERIFY(write(sentinel,retained));QVERIFY(QFile::remove(backup));QVERIFY(QFile::link(sentinel,backup));QVERIFY(QFileInfo(backup).isSymLink());}
#endif
        }
        QTRY_VERIFY_WITH_TIMEOUT(view.isCurrent(),5000);QCOMPARE(view.rows().size(),1);
        const auto row=view.rows().first().toMap();QCOMPARE(row.value("name").toString(),id);QVERIFY(row.value("recoveryRequired").toBool());QVERIFY(!row.value("enabled").toBool());
        QVERIFY(view.contributions(QStringLiteral("metadata.provider"),true).isEmpty());QVERIFY(view.acquireSettings(id).isEmpty());
    }
    void validBackupKeepsRichLegacySummaryButNoContributionsOrStrictAuthority(){
        QTemporaryDir profile;AddonManager manager(profile.path());QVERIFY(manager.beginDistributionBootstrap(bundles()));
        AddonCatalogueView view(&manager,lifetime->handle());QTRY_VERIFY_WITH_TIMEOUT(!manager.distributionBootstrapInProgress() && view.isCurrent(),15000);
        const auto id=QStringLiteral("org.videolan.tvmaze"),path=profile.filePath(QStringLiteral("packages/")+id+QStringLiteral("/state.json"));
        const auto backup=read(path+QStringLiteral(".backup"));QVERIFY(!backup.isEmpty());
        PackageStore store(profile.path());store.invalidateAuthority();QVERIFY(write(path,QByteArray("broken current state")));
        QTRY_VERIFY_WITH_TIMEOUT(view.isCurrent(),5000);
        const auto baseline=legacyReader(manager);QVERIFY(baseline);QString error;
        QVERIFY2(sameLegacyRows(baseline->rows,view.rows(),&error),qPrintable(error));
        QVERIFY2(sameLegacyRows(manager.packages(),view.rows(),&error),qPrintable(error));
        QVariantMap recovery;for(const auto& value:view.rows())if(value.toMap().value("id").toString()==id)recovery=value.toMap();
        QVERIFY(recovery.value("recoveryRequired").toBool());QVERIFY(!recovery.value("enabled").toBool());QVERIFY(!recovery.value("name").toString().isEmpty());QVERIFY(recovery.value("name").toString()!=id);
        QVERIFY(view.acquireSettings(id).isEmpty());for(const auto& value:view.contributions(QStringLiteral("metadata.provider"),true))QVERIFY(value.toMap().value("packageId").toString()!=id);
        const auto raw=store.metadataCatalogueReader();MetadataCatalogueSnapshot strict;QString rawError;bool accepted=true;
        std::thread thread([&]{accepted=raw(&strict,&rawError);});thread.join();QVERIFY(!accepted);QVERIFY(!rawError.isEmpty());
        QCOMPARE(read(path+QStringLiteral(".backup")),backup);QCOMPARE(read(path),QByteArray("broken current state"));
    }
};
QTEST_GUILESS_MAIN(DisplayIntegrationTest)
#include "test_addon_display_integration.moc"
