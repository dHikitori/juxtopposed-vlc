// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the real prepared Manager/Worker path. No Core layout is linked.
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <QTest>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QScopeGuard>
#include <QNetworkProxy>
#include <QImageReader>
#include <QBuffer>
#include "addons/addon_artwork_assets.hpp"
#include "fixtures/receipt_tls_fixture.hpp"
#include "addons/addon_manager.hpp"
#include "addons/addon_artwork_attempt_plan.hpp"
#include "fixtures/metadata_preparation_probe.hpp"
#include "addons/addon_repository_jobs.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
using namespace vlc::addons;
#ifdef HAKONE_PREPARED_OWNER_WRAP
namespace { QPointer<AddonWorker> observedPreparedWorker; }
extern "C" void __real__ZN3vlc6addons11AddonWorker23enablePreparedLifecycleEv(AddonWorker*);
extern "C" void __wrap__ZN3vlc6addons11AddonWorker23enablePreparedLifecycleEv(AddonWorker* worker)
{
    __real__ZN3vlc6addons11AddonWorker23enablePreparedLifecycleEv(worker);
    observedPreparedWorker = worker;
}
#endif
#ifdef HAKONE_ARTWORK_READ_WRAP
namespace { std::shared_ptr<metadataPreparationTest::Probe> artworkRead; }
extern "C" QImage __real__ZN12QImageReader4readEv(QImageReader*);
extern "C" QImage __wrap__ZN12QImageReader4readEv(QImageReader* reader)
{
    const auto probe=std::atomic_load(&artworkRead);
    if(probe) probe->wait();
    return __real__ZN12QImageReader4readEv(reader);
}
#endif
namespace {
const QString packageId = QStringLiteral("org.hakone.prepared-fixture");
const QString contributionId = packageId + QStringLiteral("/artwork");
struct Gate final : AddonMetadataNativeAdmission {
    std::atomic_bool open{true};
    bool current() const noexcept override { return open.load(); }
    void revoke() noexcept override { open.store(false); }
};
bool write(const QString& path, const QByteArray& bytes)
{ QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
qint64 now()
{ return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
bool drained(const AddonStreamLifetime::Handle& life)
{ return life.qtDrained() && PreparedMetadataInvocation::processCount() == 0; }
struct Fixture {
    QTemporaryDir package, store;
    AddonStreamLifetime lifetime;
    const std::shared_ptr<Gate> planGate = std::make_shared<Gate>();
    std::unique_ptr<AddonManager> manager;
    std::shared_ptr<const ArtworkAttemptPlan> plan;
    QList<StoreAuthorityWatch> watches;
    QString error;
    bool valid = false, registered = false;
    Fixture(const QString& mode = QStringLiteral("zero"), bool secret = false,
        std::unique_ptr<QNetworkAccessManager>* transport = nullptr, const QString& module = {}, const QString& executable = {}, bool credentialApproved = true, const QString& credentialOrigin = QStringLiteral("https://8.8.8.8")) {
        registered = AddonStreamLifetime::registerApplication(lifetime.handle());
        if (!registered) { error = QStringLiteral("The test app lifetime was not registered."); return; }
        QJsonObject permissions{{QStringLiteral("metadata.context"), QJsonObject{{QStringLiteral("fields"), QJsonArray{"kind", "title", "artist"}}}},
            {QStringLiteral("network.http"), QJsonObject{{QStringLiteral("origins"), QJsonArray{"https://8.8.8.8"}}, {QStringLiteral("methods"), QJsonArray{"GET"}}}}};
        QJsonObject properties{{QStringLiteral("language"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("default"), QStringLiteral("en")}}}};
        QJsonArray required;
        if (secret) {
            permissions.insert(QStringLiteral("credentials.use"), QJsonObject{{QStringLiteral("bindings"), QJsonArray{
                QJsonObject{{QStringLiteral("field"), QStringLiteral("apiKey")}, {QStringLiteral("origin"), credentialOrigin},
                    {QStringLiteral("header"), QStringLiteral("X-Key")}}}}});
            properties.insert(QStringLiteral("apiKey"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("maxLength"), 4096}, {QStringLiteral("x-hakone-secret"), true}});
            required.append(QStringLiteral("apiKey"));
        }
        const QJsonObject declaration{{"id", "artwork"}, {"type", "metadata.provider"}, {"contract", "metadata@1"},
            {"features", QJsonArray{"artwork"}}, {"operations", QJsonArray{"enrich"}},
            {"mediaKinds", QJsonArray{"movie", "episode", "series"}}, {"requires", QJsonArray{"metadata.context"}}};
        QJsonObject optional;
        if (secret && !credentialApproved) optional.insert(QStringLiteral("credentials.use"), permissions.take(QStringLiteral("credentials.use")));
        const QJsonObject manifest{{"formatVersion", 1}, {"id", packageId}, {"name", "Prepared fixture"}, {"version", "1.0.0"},
            {"publisher", "Host tests"}, {"license", "GPL-2.0-or-later"}, {"source", "https://github.com/hakone-org/prepared-fixture.git"},
            {"api", QJsonObject{{"addon", "^1.0.0"}, {"contracts", QJsonObject{{"metadata", "^1.0.0"}, {"settings", "^1.0.0"}}}}},
            {"runtime", QJsonObject{{"kind", "wasm-message-v1"}, {"entry", "dist/addon.wasm"}}},
            {"files", QJsonArray{"addon.json", "LICENSE", "settings/schema.json", "dist/addon.wasm"}},
            {"settings", QJsonObject{{"schema", "settings/schema.json"}, {"version", 1}}},
            {"permissions", QJsonObject{{"required", permissions}, {"optional", optional}}}, {"contributions", QJsonArray{declaration}}};
        const QJsonObject schema{{"type", "object"}, {"properties", properties}, {"required", required}, {"additionalProperties", false}};
        QDir().mkpath(package.filePath(QStringLiteral("dist"))); QDir().mkpath(package.filePath(QStringLiteral("settings")));
        if (!write(package.filePath(QStringLiteral("addon.json")), QJsonDocument(manifest).toJson())
            || !write(package.filePath(QStringLiteral("LICENSE")), QByteArray("Test fixture.\n"))
            || !write(package.filePath(QStringLiteral("settings/schema.json")), QJsonDocument(schema).toJson())
            || !(module.isEmpty() ? write(package.filePath(QStringLiteral("dist/addon.wasm")), QByteArray("\0asm\1\0\0\0", 8))
                                 : QFile::copy(module, package.filePath(QStringLiteral("dist/addon.wasm"))))) return;
        manager = transport ? AddonManager::withNetworkTransport(store.path(), *transport) : std::make_unique<AddonManager>(store.path());
        if (!manager) return;
        manager->setWorkerExecutable(executable.isEmpty() ? QStringLiteral(HAKONE_PREPARED_PROCESS_FIXTURE) : executable);
        if (!manager->installLocalPackage(package.path(), true)) { error = manager->errorMessage(); return; }
        if (secret && !manager->saveAddonSettings(packageId, {{"language", "en"}}, {{"apiKey", QVariantMap{
                {"operation", "replace"}, {"value", "unit-prepared-key"}, {"sessionOnly", true}}}})) { error = manager->errorMessage(); return; }
        if (!manager->setEnabled(packageId, true)) { error = manager->errorMessage(); return; }
        auto source = manager->metadataCatalogueSource(); if (!source) return;
        ArtworkAttemptPlan::Capture input;
        input.catalogue = source->catalogueReader; input.credentialReader = source->credentialReader;
        input.knownPresence = source->credentialPresence; input.workerAvailable = true;
        input.subject = QByteArray(32, 's'); input.registryIdentity = QByteArray(32, 'r'); input.privateScope = QStringLiteral("scope");
        input.requestContext = {{"identity", QJsonObject{{"kind", "movie"}, {"title", "Fixture"}}},
            {"feature", "artwork"}, {"fields", QJsonArray{"artwork"}}, {"query", mode}};
        QJsonObject media;
        if (mode == QLatin1String("eof-hold")) media.insert(QStringLiteral("fixtureEofMarker"), store.filePath(QStringLiteral("eof-seen")));
        if (mode == QLatin1String("network") || mode == QLatin1String("network-denied")) {
            input.requestContext.insert(QStringLiteral("query"), QStringLiteral("network"));
            QJsonObject request{{QStringLiteral("url"),
                mode == QLatin1String("network") ? QStringLiteral("https://8.8.8.8/test") : QStringLiteral("https://1.1.1.1/test")}};
            if (secret) request.insert(QStringLiteral("credential"), QStringLiteral("apiKey"));
            media.insert(QStringLiteral("fixtureRequest"), request);
        }
        if (!media.isEmpty()) input.requestContext.insert(QStringLiteral("media"), media);
        input.clock = now; input.deadline = now() + 20 * 1000 * 1000;
        const auto gate = planGate; const auto life = lifetime.handle(); input.live = [gate, life] { return gate->current() && life.current(); };
        std::thread task([&] { plan = ArtworkAttemptPlan::capture(std::move(input), &error); }); task.join();
        valid = plan && plan->attempts.size() == 1 && !plan->attempts.first().blocked;
    }
    ~Fixture() {
        manager.reset(); lifetime.close();
        QElapsedTimer timer; timer.start();
        while (!drained(lifetime.handle()) && timer.elapsed() < 10000) {
            QCoreApplication::processEvents(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!drained(lifetime.handle())) qFatal("The prepared fixture did not drain.");
        if (registered && !AddonStreamLifetime::unregisterApplication(lifetime.handle())) qFatal("The prepared fixture lifetime did not unregister.");
    }
    std::unique_ptr<PreparedMetadataInvocation> recipe() {
        auto prepare = manager->metadataInvocationPreparer(plan, 0); if (!prepare) return {};
        std::unique_ptr<PreparedMetadataInvocation> result;
        std::thread task([&] { result = prepare(&error); }); task.join(); return result;
    }
    std::shared_ptr<AddonMetadataAttemptAdmission> attempt(const std::shared_ptr<Gate>& gate, unsigned ordinal = 0, const QString& account = {}) {
        if (!plan) return {};
        const auto& slot = plan->attempts.first(); AddonMetadataAttemptAdmission::Capture capture;
        capture.nativeSubject = plan->subject; capture.nativeChildHold = std::make_shared<int>(1); capture.ordinal = ordinal;
        capture.contribution = slot.contribution; capture.feature = slot.feature; capture.operation = slot.operation;
        capture.releaseDigest = slot.releaseDigest; capture.releaseGeneration = slot.releaseGeneration;
        capture.settingsRevision = slot.settingsRevision; capture.accountReference = account.isEmpty() ? slot.accountReference : account; capture.partition = slot.partition;
        capture.approvedGrants = slot.approvedGrants; capture.requestedFields = slot.requestedFields;
        capture.lifetime = lifetime.handle(); capture.admission = gate;
        const auto queued = plan->queuedAuthority, authority = plan->authority;
        capture.live = [queued, authority] { return queued.current() && authority.current(); };
        watches.append(queued.onInvalidated([gate] { gate->revoke(); }));
        return AddonMetadataAttemptAdmission::create(std::move(capture));
    }
};
}
struct PreparedTransportObservation { QByteArray key; };
class PreparedTransport final : public QNetworkAccessManager {
    QPointer<ReceiptTlsServer> m_server;
    std::shared_ptr<ReceiptTransportState> m_state;
    std::shared_ptr<PreparedTransportObservation> m_observation;
public:
    PreparedTransport(ReceiptTlsServer& server, std::shared_ptr<ReceiptTransportState> state,
        std::shared_ptr<PreparedTransportObservation> observation)
        : m_server(&server), m_state(std::move(state)), m_observation(std::move(observation)) {}
    ~PreparedTransport() override { ++m_state->destroyed; }
protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request, QIODevice* body) override {
        if (!m_server || operation != GetOperation || request.url().scheme() != QLatin1String("https")
            || request.url().host() != QLatin1String("8.8.8.8") || request.peerVerifyName() != QLatin1String("8.8.8.8")
            || request.rawHeader("Host") != QByteArray("8.8.8.8") || proxy().type() != QNetworkProxy::NoProxy)
            qFatal("The prepared test transport did not receive an admitted request.");
        ++m_state->admitted; m_observation->key = request.rawHeader("X-Key");
        auto routed = request; auto url = routed.url(); url.setHost(QStringLiteral("127.0.0.1"));
        url.setPort(m_server->serverPort()); routed.setUrl(url);
        auto tls = routed.sslConfiguration(); auto certificates = tls.caCertificates(); certificates.append(m_server->certificate);
        tls.setCaCertificates(certificates); routed.setSslConfiguration(tls);
        return QNetworkAccessManager::createRequest(operation, routed, body);
    }
};
class TestAddonPreparedMetadataInvocation : public QObject {
    Q_OBJECT
private slots:
    void init() { QCOMPARE(PreparedMetadataInvocation::processCount(), 0u); }
    void managerRetirementStopsPendingArtworkWork() {
        ReceiptTlsServer server; server.keepOpen = true; QVERIFY(server.start());
        const auto state = std::make_shared<ReceiptTransportState>();
        const auto observation = std::make_shared<PreparedTransportObservation>();
        std::unique_ptr<QNetworkAccessManager> network = std::make_unique<PreparedTransport>(server, state, observation);
        Fixture fixture(QStringLiteral("zero"), true, &network); QVERIFY(fixture.valid);
        AddonArtworkAssets assets(fixture.manager.get(), fixture.store.filePath(QStringLiteral("image-cache")));
        int completed = 0;
        QVERIFY(!assets.resolve(contributionId, QStringLiteral("https://8.8.8.8/image.png"),
            [&](const auto&, const auto&) { ++completed; }).isEmpty());
        QTRY_COMPARE(server.requests, 1u);
        fixture.manager.reset();
        QCOMPARE(state->destroyed, 1u);
        QTRY_VERIFY(fixture.lifetime.handle().qtDrained()); QTest::qWait(10); QCOMPARE(completed, 0);
    }
#ifdef HAKONE_METADATA_PREPARATION_WRAP
    void artworkAuthorityPreparationKeepsTheOwnerResponsive_data() {
        QTest::addColumn<bool>("destroy");QTest::newRow("cancel")<<false;QTest::newRow("retire Manager")<<true;
    }
    void artworkAuthorityPreparationKeepsTheOwnerResponsive() {
        QFETCH(bool,destroy);Fixture fixture;QVERIFY(fixture.valid);
        AddonArtworkAssets assets(fixture.manager.get(),fixture.store.filePath(QStringLiteral("image-cache")));
        const auto probe=std::make_shared<metadataPreparationTest::Probe>();metadataPreparationTest::authority=probe;
        const auto release=qScopeGuard([&] { metadataPreparationTest::authority.reset();probe->release(); });
        int done=0;QElapsedTimer elapsed;elapsed.start();
        const auto id=assets.resolve(contributionId,QStringLiteral("https://8.8.8.8/image.png"),[&](const auto&,const auto&) { ++done; });
        QVERIFY(!id.isEmpty());QVERIFY(elapsed.elapsed()<100);QTRY_VERIFY(probe->entered.load());QVERIFY(!probe->onOwner.load());
        bool heartbeat=false;QTimer::singleShot(0,[&] { heartbeat=true; });QTRY_VERIFY(heartbeat);
        elapsed.restart();if(destroy) fixture.manager.reset();else assets.cancel(id);QVERIFY(elapsed.elapsed()<100);
        probe->release();QTRY_VERIFY(fixture.lifetime.handle().qtDrained());QTest::qWait(10);QCOMPARE(done,0);
    }
#ifdef HAKONE_ARTWORK_READ_WRAP
    void preparedArtworkUsesRealBrokerAndJoinedImageWork_data() {
        QTest::addColumn<int>("mode");QTest::newRow("download")<<0;QTest::newRow("cached image")<<1;
        QTest::newRow("cancel decode")<<2;QTest::newRow("disable during decode")<<3;
        QTest::newRow("retire during decode")<<4;
    }
    void preparedArtworkUsesRealBrokerAndJoinedImageWork() {
        QFETCH(int,mode);QImage image(4,4,QImage::Format_RGB32);image.fill(Qt::blue);QByteArray bytes;QBuffer output(&bytes);output.open(QIODevice::WriteOnly);QVERIFY(image.save(&output,"PNG"));
        ReceiptTlsServer server;server.response="HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: "+QByteArray::number(bytes.size())+"\r\nConnection: close\r\n\r\n"+bytes;
        QVERIFY(server.start());const auto state=std::make_shared<ReceiptTransportState>();const auto observation=std::make_shared<PreparedTransportObservation>();
        std::unique_ptr<QNetworkAccessManager> network=std::make_unique<PreparedTransport>(server,state,observation);
        Fixture fixture(QStringLiteral("zero"),true,&network);QVERIFY2(fixture.valid,qPrintable(fixture.error));
        const auto cache=fixture.store.filePath(QStringLiteral("image-cache"));
        const QVariantMap descriptor{{QStringLiteral("url"),QStringLiteral("https://8.8.8.8/image.png")},{QStringLiteral("credential"),QStringLiteral("apiKey")}};
        if(mode==1) {
            AddonArtworkAssets seed(fixture.manager.get(),cache);bool done=false;QUrl file;
            QVERIFY(!seed.resolve(contributionId,descriptor,[&](const auto& value,const auto& error) { file=value;done=error.isEmpty(); }).isEmpty());
            QTRY_VERIFY(done);QVERIFY(file.isLocalFile());QCOMPARE(server.requests,1u);
        }
        AddonArtworkAssets assets(fixture.manager.get(),cache);
        const auto probe=std::make_shared<metadataPreparationTest::Probe>();std::atomic_store(&artworkRead,probe);
        const auto release=qScopeGuard([&] { std::atomic_store(&artworkRead,std::shared_ptr<metadataPreparationTest::Probe>{});probe->release(); });
        int done=0;QUrl selected;QString error;QElapsedTimer elapsed;elapsed.start();
        const auto id=assets.resolve(contributionId,descriptor,[&](const auto& file,const auto& failure) { selected=file;error=failure;++done; });
        QVERIFY(!id.isEmpty());QVERIFY(elapsed.elapsed()<100);QTRY_VERIFY(probe->entered.load());QVERIFY(!probe->onOwner.load());QCOMPARE(done,0);
        bool heartbeat=false;QTimer::singleShot(0,[&] { heartbeat=true; });QTRY_VERIFY(heartbeat);
        if(mode==2) assets.cancel(id);if(mode==3) QVERIFY(fixture.manager->setEnabled(packageId,false));
        if(mode==4) fixture.manager.reset();
        probe->release();QTRY_VERIFY(fixture.lifetime.handle().qtDrained());
        std::atomic_store(&artworkRead,std::shared_ptr<metadataPreparationTest::Probe>{});
        if(mode>=2) { QTest::qWait(10);QCOMPARE(done,0);QVERIFY(!QFileInfo::exists(cache)); }
        else { QTRY_COMPARE(done,1);QVERIFY2(error.isEmpty(),qPrintable(error));QVERIFY(selected.isLocalFile());QVERIFY(!QImage(selected.toLocalFile()).isNull()); }
        QCOMPARE(server.requests,1u);QCOMPARE(state->admitted,1u);QCOMPARE(observation->key,QByteArray("unit-prepared-key"));
        QCOMPARE(PreparedMetadataInvocation::processCount(),0u);
    }
#endif
    void preparedArtworkRefusesAnotherOwnerAndChangedConfiguration() {
        Fixture fixture;QVERIFY(fixture.valid);PreparedArtwork prepared;QString error;bool done=false;
        QVERIFY(!fixture.manager->prepareArtworkAsset(contributionId,[&](auto value,const auto& failure) { prepared=std::move(value);error=failure;done=true; }).isEmpty());
        QTRY_VERIFY(done);QVERIFY2(error.isEmpty(),qPrintable(error));QVERIFY(prepared && prepared->current());
        QTemporaryDir profile;AddonManager other(profile.path());bool networkDone=false;
        const QJsonObject request{{QStringLiteral("url"),QStringLiteral("https://8.8.8.8/image.png")}};
        QCOMPARE(other.fetchPreparedArtwork(prepared,request,[&](const auto&) { networkDone=true; }),quint64(0));
        QVERIFY(fixture.manager->setEnabled(packageId,false));QVERIFY(!prepared->current());
        QCOMPARE(fixture.manager->fetchPreparedArtwork(prepared,request,[&](const auto&) { networkDone=true; }),quint64(0));
        QTest::qWait(10);QVERIFY(!networkDone);
    }
    void preparedArtworkNetworkRejectsAnExternalConfigurationChange() {
        Fixture fixture;QVERIFY(fixture.valid);PreparedArtwork prepared;bool done=false;
        QVERIFY(!fixture.manager->prepareArtworkAsset(contributionId,[&](auto value,const auto&) { prepared=std::move(value);done=true; }).isEmpty());
        QTRY_VERIFY(done);QVERIFY(prepared && prepared->current());
        bool networkDone=false;QString error;
        QVERIFY(fixture.manager->fetchPreparedArtwork(prepared,{{QStringLiteral("url"),QStringLiteral("https://8.8.8.8/image.png")}},
            [&](const auto& response) { networkDone=true;error=response.error;QVERIFY(response.body.isEmpty()); })!=0);
        PackageStore store(fixture.store.path());QJsonObject settings;quint64 revision=0;
        QVERIFY(store.settings(packageId,&settings,&revision));settings.insert(QStringLiteral("language"),QStringLiteral("fr"));
        QVERIFY(store.saveSettings(packageId,settings,revision));QTRY_VERIFY(networkDone);QVERIFY(error.contains(QStringLiteral("changed")));
    }
    void providerSelectionDoesNotBlockTheOwner() {
        Fixture fixture; QVERIFY(fixture.valid);
        const auto probe = std::make_shared<metadataPreparationTest::Probe>();
        metadataPreparationTest::catalogue = probe;
        const auto release = qScopeGuard([&] { metadataPreparationTest::catalogue.reset(); probe->release(); });
        bool done = false; AddonMetadataCoordinator::ProviderSelection selection;
        QElapsedTimer elapsed; elapsed.start();
        auto cancel = fixture.manager->selectMetadataProviders("artwork", [&](auto result) { selection = std::move(result); done = true; });
        QVERIFY(elapsed.elapsed() < 100); QVERIFY(cancel);
        QTRY_VERIFY(probe->entered.load()); QVERIFY(!probe->onOwner.load()); QVERIFY(!done);
        bool heartbeat = false; QTimer::singleShot(0, [&] { heartbeat = true; }); QTRY_VERIFY(heartbeat);
        probe->release(); QTRY_VERIFY(done); QVERIFY2(selection.error.isEmpty(), qPrintable(selection.error));
        QVERIFY(selection.current && selection.current()); QCOMPARE(selection.providers.size(), 1);
        QVERIFY(selection.providers.first().toMap().value("configured").toBool());
    }
    void cancelSelectionAndDestroyManagerWhileItsReadIsBlocked() {
        for (const bool destroy : {false, true}) {
            Fixture fixture; QVERIFY(fixture.valid);
            const auto probe = std::make_shared<metadataPreparationTest::Probe>();
            metadataPreparationTest::catalogue = probe;
            const auto release = qScopeGuard([&] { metadataPreparationTest::catalogue.reset(); probe->release(); });
            bool done = false;
            auto cancel = fixture.manager->selectMetadataProviders("artwork", [&](auto) { done = true; });
            QTRY_VERIFY(probe->entered.load()); QVERIFY(!probe->onOwner.load());
            QElapsedTimer elapsed; elapsed.start();
            if (destroy) fixture.manager.reset(); else cancel();
            QVERIFY(elapsed.elapsed() < 100); probe->release();
            QTRY_VERIFY(fixture.lifetime.handle().qtDrained()); QTest::qWait(10); QVERIFY(!done);
        }
    }
    void disabledProviderCannotReturnFromPendingSelection() {
        Fixture fixture; QVERIFY(fixture.valid);
        const auto probe = std::make_shared<metadataPreparationTest::Probe>();
        metadataPreparationTest::catalogue = probe;
        const auto release = qScopeGuard([&] { metadataPreparationTest::catalogue.reset(); probe->release(); });
        bool done = false; AddonMetadataCoordinator::ProviderSelection selection;
        fixture.manager->selectMetadataProviders("artwork", [&](auto result) { selection = std::move(result); done = true; });
        QTRY_VERIFY(probe->entered.load()); QVERIFY(fixture.manager->setEnabled(packageId, false));
        probe->release(); QTRY_VERIFY(done); QVERIFY(!selection.error.isEmpty()); QVERIFY(selection.providers.isEmpty());
    }
    void invocationPreparationAndCancellationDoNotBlockTheOwner() {
        for (const bool cancel : {false, true}) {
            Fixture fixture; QVERIFY(fixture.valid);
            const auto probe = std::make_shared<metadataPreparationTest::Probe>();
            metadataPreparationTest::authority = probe;
            const auto release = qScopeGuard([&] { metadataPreparationTest::authority.reset(); probe->release(); });
            QSignalSpy result(fixture.manager.get(), &AddonManager::operationFinished);
            QElapsedTimer elapsed; elapsed.start();
            const auto request = fixture.manager->invokeMetadata(contributionId, "enrich", fixture.plan->attempts.first().projectedContext.toVariantMap());
            QVERIFY(!request.isEmpty()); QVERIFY(elapsed.elapsed() < 100);
            QTRY_VERIFY(probe->entered.load()); QVERIFY(!probe->onOwner.load()); QCOMPARE(PreparedMetadataInvocation::processCount(), 0u);
            bool heartbeat = false; QTimer::singleShot(0, [&] { heartbeat = true; }); QTRY_VERIFY(heartbeat);
            if (cancel) fixture.manager->cancel(request);
            probe->release();
            if (cancel) { QTRY_VERIFY(fixture.lifetime.handle().qtDrained()); QVERIFY(result.isEmpty()); }
            else { QTRY_COMPARE(result.size(), 1); QCOMPARE(result.first()[0].toString(), request); QCOMPARE(result.first()[2].toString(), QStringLiteral("no_match")); }
        }
    }
    void preparationDoesNotStartADisabledProvider() {
        Fixture fixture; QVERIFY(fixture.valid);
        const auto probe = std::make_shared<metadataPreparationTest::Probe>();
        metadataPreparationTest::authority = probe;
        const auto release = qScopeGuard([&] { metadataPreparationTest::authority.reset(); probe->release(); });
        QSignalSpy result(fixture.manager.get(), &AddonManager::operationFinished);
        const auto request = fixture.manager->invokeMetadata(contributionId, "enrich", fixture.plan->attempts.first().projectedContext.toVariantMap());
        QVERIFY(!request.isEmpty()); QTRY_VERIFY(probe->entered.load());
        QVERIFY(fixture.manager->setEnabled(packageId, false)); probe->release();
        QTRY_VERIFY(fixture.lifetime.handle().qtDrained()); QVERIFY(result.isEmpty()); QCOMPARE(PreparedMetadataInvocation::processCount(), 0u);
    }
#endif
    void asynchronousSelectionsKeepSafeModeDenial() {
        Fixture fixture; QVERIFY(fixture.valid);
        const QString builtin = QStringLiteral("hakone.builtin.prepared/artwork");
        fixture.manager->registerBuiltinProvider("artwork", {{"id", builtin}, {"packageId", "hakone.builtin.prepared"},
            {"name", "Native fixture"}, {"enabled", true}, {"configured", true}});
        QVERIFY(fixture.manager->builtinMetadataProvider(builtin).isEmpty());
        bool done = false;
        fixture.manager->selectMetadataProviders("artwork", [&](auto result) { QVERIFY(result.error.isEmpty()); done = true; });
        QTRY_VERIFY(done); QVERIFY(!fixture.manager->builtinMetadataProvider(builtin).isEmpty());
        QVERIFY(fixture.manager->setSafeMode(true)); QVERIFY(fixture.manager->builtinMetadataProvider(builtin).isEmpty());
        done = false;
        fixture.manager->selectMetadataProviders("artwork", [&](auto result) {
            QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
            for (const auto& row : result.providers) QVERIFY(!row.toMap().value("enabled").toBool());
            done = true;
        });
        QTRY_VERIFY(done); QVERIFY(fixture.manager->builtinMetadataProvider(builtin).isEmpty());
    }
    void ordinaryPreparedCallsUseTheRealRustWorker() {
        Fixture fixture(QStringLiteral("zero"), false, nullptr, QStringLiteral(HAKONE_PREPARED_FRENCH_TV_WASM), QStringLiteral(HAKONE_PREPARED_RUST_WORKER));
        QVERIFY2(fixture.valid, qPrintable(fixture.error)); QSignalSpy result(fixture.manager.get(), &AddonManager::operationFinished);
        const auto request = fixture.manager->invokeMetadata(contributionId, "enrich", fixture.plan->attempts.first().projectedContext.toVariantMap());
        QVERIFY(!request.isEmpty()); QTRY_COMPARE(result.size(), 1); QCOMPARE(result.first()[0].toString(), request);
        QCOMPARE(result.first()[2].toString(), QStringLiteral("no_match")); QTRY_VERIFY(drained(fixture.lifetime.handle()));
    }
    void ordinaryPreparedCallsRequireCleanEof_data() {
        QTest::addColumn<QString>("mode"); QTest::addColumn<bool>("accepted");
        QTest::newRow("clean") << QStringLiteral("zero") << true;
        QTest::newRow("immediate") << QStringLiteral("immediate") << true;
        QTest::newRow("duplicate") << QStringLiteral("duplicate") << false;
        QTest::newRow("eof-duplicate") << QStringLiteral("eof-duplicate") << false;
        QTest::newRow("late-host") << QStringLiteral("late-host") << false;
        QTest::newRow("fragment") << QStringLiteral("eof-fragment") << false;
        QTest::newRow("crash") << QStringLiteral("eof-crash") << false;
        QTest::newRow("timeout") << QStringLiteral("eof-hold") << false;
    }
    void ordinaryPreparedCallsRequireCleanEof() {
        QFETCH(QString, mode); QFETCH(bool, accepted);
        Fixture fixture(mode); QVERIFY(fixture.valid); QSignalSpy result(fixture.manager.get(), &AddonManager::operationFinished);
        const auto request = fixture.manager->invokeMetadata(contributionId, "enrich", fixture.plan->attempts.first().projectedContext.toVariantMap());
        QVERIFY(!request.isEmpty()); QTRY_COMPARE(result.size(), 1); QCOMPARE(result.first()[0].toString(), request);
        QCOMPARE(result.first()[2].toString() == QLatin1String("no_match"), accepted);
        QTRY_VERIFY(drained(fixture.lifetime.handle()));
    }
    void normalEofKeepsTransferredReceiptCurrent() {
        Fixture fixture; QVERIFY2(fixture.valid, qPrintable(fixture.error));
        auto recipe = fixture.recipe(); QVERIFY2(recipe, qPrintable(fixture.error));
        auto gate = std::make_shared<Gate>(); auto attempt = fixture.attempt(gate); QVERIFY(attempt);
        QSignalSpy result(fixture.manager.get(), &AddonManager::operationFinished);
        const auto request = fixture.manager->invokePreparedMetadata(std::move(recipe), {}, attempt); QVERIFY(!request.isEmpty());
        QTRY_COMPARE(result.size(), 1); QCOMPARE(result.first()[2].toString(), QStringLiteral("no_match"));
        QTRY_COMPARE(PreparedMetadataInvocation::processCount(), 0u);
        auto receipt = attempt->take(); QVERIFY(receipt); QVERIFY(receipt->current()); QVERIFY(gate->current());
        QVERIFY(receipt->matches(fixture.plan->subject, 0, contributionId,
            fixture.plan->attempts.first().projectedContextDigest, fixture.plan->attempts.first().partition));
        fixture.manager->cancel(request); QVERIFY(!receipt->current()); QVERIFY(!gate->current());
    }
    void actualRustWorkerAndWasmUseThePreparedPath() {
        Fixture fixture(QStringLiteral("zero"), false, nullptr, QStringLiteral(HAKONE_PREPARED_FRENCH_TV_WASM), QStringLiteral(HAKONE_PREPARED_RUST_WORKER));
        QVERIFY2(fixture.valid, qPrintable(fixture.error)); auto recipe = fixture.recipe(); QVERIFY2(recipe, qPrintable(fixture.error));
        auto gate = std::make_shared<Gate>(); auto attempt = fixture.attempt(gate); QVERIFY(attempt);
        QSignalSpy result(fixture.manager.get(), &AddonManager::operationFinished);
        QVERIFY(!fixture.manager->invokePreparedMetadata(std::move(recipe), {}, attempt).isEmpty());
        QTRY_COMPARE(result.size(), 1); QCOMPARE(result.first()[2].toString(), QStringLiteral("no_match"));
        QVERIFY(attempt->take()); QVERIFY(gate->current()); QTRY_COMPARE(PreparedMetadataInvocation::processCount(), 0u);
    }
    void approvedCredentialBytesUseOnlyTheDeclaredOrigin_data() {
        QTest::addColumn<bool>("approved");
        QTest::addColumn<QString>("origin");
        QTest::newRow("canonical-approved") << true << QStringLiteral("https://8.8.8.8");
        QTest::newRow("default-port-approved") << true << QStringLiteral("https://8.8.8.8:443");
        QTest::newRow("root-path-approved") << true << QStringLiteral("https://8.8.8.8/");
        QTest::newRow("canonical-unapproved") << false << QStringLiteral("https://8.8.8.8");
        QTest::newRow("default-port-unapproved") << false << QStringLiteral("https://8.8.8.8:443");
    }
    void approvedCredentialBytesUseOnlyTheDeclaredOrigin() {
        QFETCH(bool, approved); QFETCH(QString, origin);
        ReceiptTlsServer server; server.response = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n{}";
        QVERIFY(server.start()); const auto state = std::make_shared<ReceiptTransportState>();
        const auto observation = std::make_shared<PreparedTransportObservation>();
        std::unique_ptr<QNetworkAccessManager> network = std::make_unique<PreparedTransport>(server, state, observation);
        Fixture fixture(QStringLiteral("network"), true, &network, {}, {}, approved, origin);
        QVERIFY2(fixture.valid, qPrintable(fixture.error)); auto recipe = fixture.recipe(); QVERIFY2(recipe, qPrintable(fixture.error));
        auto gate = std::make_shared<Gate>(); auto attempt = fixture.attempt(gate); QVERIFY(attempt);
        QSignalSpy result(fixture.manager.get(), &AddonManager::operationFinished);
        QVERIFY(!fixture.manager->invokePreparedMetadata(std::move(recipe), {}, attempt).isEmpty());
        QTRY_COMPARE(result.size(), 1); QCOMPARE(server.requests, 1u); QCOMPARE(server.handshakes, 1u);
        QCOMPARE(state->admitted, 1u); QCOMPARE(observation->key, approved ? QByteArray("unit-prepared-key") : QByteArray{});
        QCOMPARE(result.first()[2].toString(), QStringLiteral("no_match")); QVERIFY(attempt->take());
    }
    void undeclaredNetworkOriginCannotCertify() {
        Fixture fixture(QStringLiteral("network-denied"), true); QVERIFY2(fixture.valid, qPrintable(fixture.error));
        auto recipe = fixture.recipe(); QVERIFY(recipe); auto gate = std::make_shared<Gate>(); auto attempt = fixture.attempt(gate); QVERIFY(attempt);
        QSignalSpy result(fixture.manager.get(), &AddonManager::operationFinished);
        QVERIFY(!fixture.manager->invokePreparedMetadata(std::move(recipe), {}, attempt).isEmpty());
        QTRY_COMPARE(result.size(), 1); QVERIFY(!attempt->take());
    }
    void preparationRefusesTheOwnerThreadAndSurvivesOwnerDeletion() {
        Fixture fixture; QVERIFY(fixture.valid);
        auto prepare = fixture.manager->metadataInvocationPreparer(fixture.plan, 0); QVERIFY(prepare);
        QVERIFY(!prepare(&fixture.error));
        fixture.manager.reset(); std::unique_ptr<PreparedMetadataInvocation> result;
        std::thread task([&] { result = prepare(&fixture.error); }); task.join(); QVERIFY(!result);
    }
    void settingsChangeRefusesTheSealedRecipe() {
        Fixture fixture; QVERIFY(fixture.valid); auto recipe = fixture.recipe(); QVERIFY(recipe);
        auto gate = std::make_shared<Gate>(); auto attempt = fixture.attempt(gate); QVERIFY(attempt);
        QVERIFY(fixture.manager->saveAddonSettings(packageId, {{"language", "fr"}}, {})); QVERIFY(!recipe->current());
        QVERIFY(fixture.manager->invokePreparedMetadata(std::move(recipe), {}, attempt).isEmpty());
        QVERIFY(!attempt->take()); QCOMPARE(PreparedMetadataInvocation::processCount(), 0u);
    }
    void anotherManagerCannotUseTheRecipe() {
        Fixture fixture; QVERIFY(fixture.valid); auto recipe = fixture.recipe(); QVERIFY(recipe);
        AddonManager other(fixture.store.path()); other.setWorkerExecutable(QStringLiteral(HAKONE_PREPARED_PROCESS_FIXTURE));
        auto gate = std::make_shared<Gate>(); auto attempt = fixture.attempt(gate); QVERIFY(attempt);
        QVERIFY(other.invokePreparedMetadata(std::move(recipe), {}, attempt).isEmpty()); QVERIFY(!attempt->take());
    }
    void wrongSlotAndChangedWorkerRefuse() {
        Fixture fixture; QVERIFY(fixture.valid);
        QVERIFY(!fixture.manager->metadataInvocationPreparer(fixture.plan, 1));
        auto recipe = fixture.recipe(); QVERIFY(recipe);
        auto gate = std::make_shared<Gate>(); auto attempt = fixture.attempt(gate, 1); QVERIFY(attempt);
        QVERIFY(fixture.manager->invokePreparedMetadata(std::move(recipe), {}, attempt).isEmpty());
        recipe = fixture.recipe(); QVERIFY(recipe); attempt = fixture.attempt(gate); QVERIFY(attempt);
        fixture.manager->setWorkerExecutable(fixture.store.filePath(QStringLiteral("different-worker")));
        QVERIFY(fixture.manager->invokePreparedMetadata(std::move(recipe), {}, attempt).isEmpty());
    }
    void mismatchedAccountBlocksTheActualReceiptBinding() {
        Fixture fixture; QVERIFY(fixture.valid); auto recipe = fixture.recipe(); QVERIFY(recipe);
        auto gate = std::make_shared<Gate>(); auto attempt = fixture.attempt(gate, 0, QStringLiteral("public:other")); QVERIFY(attempt);
        QVERIFY(fixture.manager->invokePreparedMetadata(std::move(recipe), {}, attempt).isEmpty());
        QVERIFY(!attempt->take()); QTRY_COMPARE(PreparedMetadataInvocation::processCount(), 0u);
    }
    void recipeQuotaRecoversAfterOwnedRecipesAreDestroyed() {
        Fixture fixture; QVERIFY(fixture.valid);
        std::vector<std::unique_ptr<PreparedMetadataInvocation>> recipes;
        for (unsigned i = 0; i < PreparedMetadataInvocation::RecipeLimit; ++i) {
            auto recipe = fixture.recipe(); QVERIFY2(recipe, qPrintable(fixture.error)); recipes.push_back(std::move(recipe));
        }
        QVERIFY(!fixture.recipe()); recipes.clear(); QVERIFY(fixture.recipe());
    }
    void ownerAdmissionUsesTheCapturedStateOnly() {
        Fixture fixture; QVERIFY(fixture.valid); auto recipe = fixture.recipe(); QVERIFY(recipe);
        auto gate = std::make_shared<Gate>(); auto attempt = fixture.attempt(gate); QVERIFY(attempt);
        const auto state = fixture.store.filePath(QStringLiteral("packages/") + packageId + QStringLiteral("/state.json"));
        const auto held = state + QStringLiteral(".held"); QVERIFY(QFile::rename(state, held));
        QSignalSpy result(fixture.manager.get(), &AddonManager::operationFinished);
        const auto request = fixture.manager->invokePreparedMetadata(std::move(recipe), {}, attempt);
        const auto restore = qScopeGuard([&] { QFile::rename(held, state); });
        QVERIFY(!request.isEmpty()); QTRY_COMPARE(result.size(), 1);
        QCOMPARE(result.first()[2].toString(), QStringLiteral("no_match")); QVERIFY(attempt->take());
        // Final saved-state admission is a separate joined runner obligation.
    }
    void immediateCleanExitDoesNotLoseTheTerminalResult() {
        Fixture fixture(QStringLiteral("immediate")); QVERIFY(fixture.valid); auto recipe = fixture.recipe(); QVERIFY(recipe);
        auto gate = std::make_shared<Gate>(); auto attempt = fixture.attempt(gate); QVERIFY(attempt);
        QSignalSpy result(fixture.manager.get(), &AddonManager::operationFinished);
        QVERIFY(!fixture.manager->invokePreparedMetadata(std::move(recipe), {}, attempt).isEmpty());
        QTRY_COMPARE(result.size(), 1); QCOMPARE(result.first()[2].toString(), QStringLiteral("no_match")); QVERIFY(attempt->take());
    }
    void incompleteRetirementCannotCertify_data() {
        QTest::addColumn<QString>("mode");
        for (const auto* mode : {"eof-duplicate", "eof-fragment", "eof-crash", "eof-hold"})
            QTest::newRow(mode) << QString::fromLatin1(mode);
    }
    void incompleteRetirementCannotCertify() {
        QFETCH(QString, mode); Fixture fixture(mode); QVERIFY(fixture.valid); auto recipe = fixture.recipe(); QVERIFY(recipe);
        auto gate = std::make_shared<Gate>(); auto attempt = fixture.attempt(gate); QVERIFY(attempt);
        QSignalSpy result(fixture.manager.get(), &AddonManager::operationFinished);
        QVERIFY(!fixture.manager->invokePreparedMetadata(std::move(recipe), {}, attempt).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 6000); QCOMPARE(result.first()[2].toString(), QStringLiteral("denied"));
        QVERIFY(!attempt->take()); QVERIFY(!gate->current()); QTRY_COMPARE(PreparedMetadataInvocation::processCount(), 0u);
    }
    void failedToStartStillDrains() {
        Fixture fixture; QVERIFY(fixture.valid);
        const auto executable = fixture.store.filePath(QStringLiteral("not-a-program"));
        QVERIFY(write(executable, QByteArray("#!/missing-prepared-test-interpreter\n")));
        QVERIFY(QFile::setPermissions(executable, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        fixture.manager->setWorkerExecutable(executable); auto recipe = fixture.recipe(); QVERIFY(recipe);
        auto gate = std::make_shared<Gate>(); auto attempt = fixture.attempt(gate); QVERIFY(attempt);
        QSignalSpy result(fixture.manager.get(), &AddonManager::operationFinished);
        QVERIFY(!fixture.manager->invokePreparedMetadata(std::move(recipe), {}, attempt).isEmpty());
        QTRY_COMPARE(result.size(), 1); QVERIFY(result.first()[2].toString() != QLatin1String("no_match"));
        QVERIFY(!attempt->take()); QTRY_COMPARE(PreparedMetadataInvocation::processCount(), 0u);
    }
    void cancelBeforeProcessStartReleasesTheReservation() {
        Fixture fixture; QVERIFY(fixture.valid); auto recipe = fixture.recipe(); QVERIFY(recipe);
        auto gate = std::make_shared<Gate>(); auto attempt = fixture.attempt(gate); QVERIFY(attempt);
        unsigned admissions = 0;
        auto guard = [&] { if (++admissions == 3) gate->revoke(); return gate->current(); };
        QVERIFY(fixture.manager->invokePreparedMetadata(std::move(recipe), guard, attempt).isEmpty());
        QVERIFY(!attempt->take()); QTRY_COMPARE(PreparedMetadataInvocation::processCount(), 0u);
    }
    void cancelWhileWaitingForEofSuppressesDelivery() {
        Fixture fixture(QStringLiteral("eof-hold")); QVERIFY(fixture.valid); auto recipe = fixture.recipe(); QVERIFY(recipe);
        auto gate = std::make_shared<Gate>(); auto attempt = fixture.attempt(gate); QVERIFY(attempt);
        QSignalSpy result(fixture.manager.get(), &AddonManager::operationFinished);
        const auto request = fixture.manager->invokePreparedMetadata(std::move(recipe), {}, attempt); QVERIFY(!request.isEmpty());
        // The child writes this only after the host has closed stdin in
        // response to its terminal frame. This is not a startup time guess.
        QTRY_VERIFY(QFile::exists(fixture.store.filePath(QStringLiteral("eof-seen"))));
        QCOMPARE(result.size(), 0); QVERIFY(!attempt->take()); fixture.manager->cancel(request);
        QTRY_COMPARE(PreparedMetadataInvocation::processCount(), 0u); QCOMPARE(result.size(), 0); QVERIFY(!attempt->take());
    }
    void ownerDeletionWhileTheProcessLivesIsJoined() {
        Fixture fixture(QStringLiteral("hold")); QVERIFY(fixture.valid); auto recipe = fixture.recipe(); QVERIFY(recipe);
        auto gate = std::make_shared<Gate>(); auto attempt = fixture.attempt(gate); QVERIFY(attempt);
        QVERIFY(!fixture.manager->invokePreparedMetadata(std::move(recipe), {}, attempt).isEmpty());
        QTest::qWait(50); QCOMPARE(PreparedMetadataInvocation::processCount(), 1u);
        fixture.manager.reset(); QVERIFY(!gate->current());
        QTRY_COMPARE(PreparedMetadataInvocation::processCount(), 0u); QVERIFY(!attempt->take());
    }
#ifdef HAKONE_PREPARED_OWNER_WRAP
    void cleanupDuringManagerMemberDestructionUsesTheClosedOwnerFlag() {
        Fixture fixture; QVERIFY(fixture.valid); auto recipe = fixture.recipe(); QVERIFY(recipe);
        auto gate = std::make_shared<Gate>(); auto attempt = fixture.attempt(gate); QVERIFY(attempt);
        QObject observer;
        QPointer<AddonManager> weakManager(fixture.manager.get());
        auto* broker = fixture.manager->findChild<NetworkBroker*>(); QVERIFY(broker);
        bool memberDestroyed = false, weakStillLive = false, cleanupRan = false;
        QPointer<AddonWorker> worker;
        connect(broker, &QObject::destroyed, &observer, [&] {
            memberDestroyed = true;
            weakStillLive = bool(weakManager);
            // The Manager maps have ended, but its QObject base has not begun.
            QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
            cleanupRan = worker.isNull();
        });
        QSignalSpy result(fixture.manager.get(), &AddonManager::operationFinished);
        QVERIFY(!fixture.manager->invokePreparedMetadata(std::move(recipe), {}, attempt).isEmpty());
        worker = observedPreparedWorker; QVERIFY(worker);
        connect(worker, &AddonWorker::preparedRetired, &observer, [&] { fixture.manager.reset(); });
        QTRY_VERIFY(!fixture.manager);
        QVERIFY(memberDestroyed); QVERIFY(weakStillLive); QVERIFY(cleanupRan);
        QCOMPARE(result.size(), 0); QVERIFY(!gate->current()); QVERIFY(!attempt->take());
        QTRY_COMPARE(PreparedMetadataInvocation::processCount(), 0u);
    }
#endif
    void appRetirementWhileTheProcessLivesIsJoined() {
        Fixture fixture(QStringLiteral("hold")); QVERIFY(fixture.valid); auto recipe = fixture.recipe(); QVERIFY(recipe);
        auto gate = std::make_shared<Gate>(); auto attempt = fixture.attempt(gate); QVERIFY(attempt);
        QVERIFY(!fixture.manager->invokePreparedMetadata(std::move(recipe), {}, attempt).isEmpty());
        fixture.lifetime.close(); QTRY_COMPARE(PreparedMetadataInvocation::processCount(), 0u);
        QVERIFY(!gate->current()); QVERIFY(!attempt->take());
    }
    void appJoinRetainsTheFinalCallbackCaptureTail() {
        Fixture fixture; QVERIFY(fixture.valid); auto recipe = fixture.recipe(); QVERIFY(recipe);
        struct Observed { std::atomic_bool dropped{false}, held{false}; };
        struct Drop {
            AddonStreamLifetime::Handle life; std::shared_ptr<Observed> observed;
            ~Drop() {
                life.close();
                std::thread native([life = life, observed = observed] {
                    observed->held.store(!life.waitQt(std::chrono::milliseconds(250)));
                });
                native.join(); observed->dropped.store(true);
            }
        };
        const auto observed = std::make_shared<Observed>();
        auto drop = std::make_shared<Drop>(); drop->life = fixture.lifetime.handle(); drop->observed = observed;
        NetworkBroker::RequestGuard guard = [drop] { return true; }; drop.reset();
        auto gate = std::make_shared<Gate>(); auto attempt = fixture.attempt(gate); QVERIFY(attempt);
        QSignalSpy result(fixture.manager.get(), &AddonManager::operationFinished);
        QVERIFY(!fixture.manager->invokePreparedMetadata(std::move(recipe), std::move(guard), attempt).isEmpty()); guard = {};
        QTRY_COMPARE(result.size(), 1); QTRY_VERIFY(observed->dropped.load()); QVERIFY(observed->held.load());
        QTRY_COMPARE(PreparedMetadataInvocation::processCount(), 0u);
    }
    void processQuotaRefusesTheNinthWithoutFallback() {
        Fixture fixture(QStringLiteral("hold")); QVERIFY(fixture.valid);
        std::vector<std::shared_ptr<AddonMetadataAttemptAdmission>> attempts;
        for (unsigned i = 0; i < PreparedMetadataInvocation::ProcessLimit; ++i) {
            auto recipe = fixture.recipe(); QVERIFY(recipe); auto gate = std::make_shared<Gate>();
            auto attempt = fixture.attempt(gate); QVERIFY(attempt);
            QVERIFY(!fixture.manager->invokePreparedMetadata(std::move(recipe), {}, attempt).isEmpty()); attempts.push_back(attempt);
        }
        QCOMPARE(PreparedMetadataInvocation::processCount(), PreparedMetadataInvocation::ProcessLimit);
        auto recipe = fixture.recipe(); QVERIFY(recipe); auto attempt = fixture.attempt(std::make_shared<Gate>()); QVERIFY(attempt);
        QVERIFY(fixture.manager->invokePreparedMetadata(std::move(recipe), {}, attempt).isEmpty()); QVERIFY(!attempt->take());
        fixture.manager.reset(); QTRY_COMPARE(PreparedMetadataInvocation::processCount(), 0u);
    }
};
QTEST_GUILESS_MAIN(TestAddonPreparedMetadataInvocation)
#include "test_addon_prepared_metadata_invocation.moc"
