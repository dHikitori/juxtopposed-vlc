// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <QtTest>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QScopeGuard>
#include <atomic>
#include <thread>
#include <cstring>
#include <vlc/vlc.h>
#include <vlc_common.h>
#include <vlc_meta.h>
#include <vlc_input_item.h>
#include <vlc_artwork_policy.h>
#include <QBuffer>
#include <QImage>
#include "addons/addon_native_provider_artwork_assets.hpp"
#include "lib/libvlc_internal.h"
#include "addons/addon_artwork_attempt_aggregate.hpp"
#include "addons/addon_artwork_lookup_runner.hpp"
#include "addons/addon_metadata_service.hpp"
#include "addons/addon_native_metadata_owner.hpp"
#include "addons/addon_artwork_assets.hpp"
#include "addons/addon_metadata_registry_epoch.hpp"
#include "addons/addon_manager.hpp"
#include "addons/addon_metadata_family_plan.hpp"
#include "addons/addon_metadata_coordinator.hpp"
#include <vlc_input_item.h>
#include <QBuffer>
#include <QImage>
#include "addons/addon_native_provider_artwork_assets.hpp"
#include "fixtures/addon_artwork_attempt_keeper.h"
#include "fixtures/addon_native_provider_fixture.h"
#include "fixtures/receipt_tls_fixture.hpp"
using namespace vlc::addons;
using Aggregate = ArtworkAttemptAggregate;
using Plan = ArtworkAttemptPlan;
namespace {
using Native = std::unique_ptr<keeper_fixture, decltype(&keeper_fixture_delete)>;
bool write(const QString& path, const QByteArray& value)
{ QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(value) == value.size(); }
ArtworkBuiltinRegistration builtin(const QString& family)
{
    for (const auto& binding : AddonMetadataFamilyPlan::bindings()) if (binding.family == family) {
        ArtworkBuiltinRegistration value;
        value.descriptor = {{QStringLiteral("id"), binding.nativeContribution},
            {QStringLiteral("packageId"), binding.nativeContribution.section(QLatin1Char('/'), 0, 0)},
            {QStringLiteral("enabled"), true}, {QStringLiteral("configured"), true},
            {QStringLiteral("features"), QJsonArray{QStringLiteral("artwork")}},
            {QStringLiteral("operations"), QJsonArray{QStringLiteral("enrich")}},
            {QStringLiteral("cachePartition"), QString(QStringLiteral("native:") + family)}};
        value.binding = QStringLiteral("binding:") + family; value.available = true; return value;
    }
    return {};
}
struct Fixture {
    QTemporaryDir profile, packages;
    AddonStreamLifetime lifetime;
    MetadataRegistryEpoch registry;
    Native native{keeper_fixture_new(), keeper_fixture_delete};
    std::unique_ptr<AddonManager> manager;
    libvlc_instance_t* instance = nullptr;
    std::unique_ptr<AddonNativeProviderContext> context;
    std::shared_ptr<CoreMetadataAdmission> gate;
    std::shared_ptr<const Plan> plan;
    std::unique_ptr<Aggregate> aggregate;
    QList<ArtworkBuiltinRegistration> builtins;
    std::shared_ptr<std::atomic<qint64>> clock;
    QString error, returnedStatus;
    QJsonObject returnedData;
    bool realWorker = false, registered = false;
    explicit Fixture(std::unique_ptr<QNetworkAccessManager>* transport=nullptr) {
        registered = AddonStreamLifetime::registerApplication(lifetime.handle());
        if (!registered) qFatal("The aggregate fixture could not register its app lifetime.");
        manager = transport ? AddonManager::withNetworkTransport(profile.path(),*transport) : std::make_unique<AddonManager>(profile.path());
        manager->setWorkerExecutable(QStringLiteral(HAKONE_AGGREGATE_PROCESS_FIXTURE));
    }
    ~Fixture() {
        aggregate.reset(); plan.reset(); manager.reset();
        if (context) context->close(); context.reset(); registry.close();
        lifetime.close();
        QElapsedTimer drain; drain.start();
        while (!lifetime.handle().qtDrained() && drain.elapsed() < 10000) {
            QCoreApplication::processEvents(); QTest::qWait(1);
        }
        if (!lifetime.handle().qtDrained()) qFatal("The aggregate fixture join did not end.");
        if (registered && !AddonStreamLifetime::unregisterApplication(lifetime.handle()))
            qFatal("The aggregate fixture app lifetime did not unregister.");
        native.reset(); gate.reset();
        if (instance) libvlc_release(instance);
    }
    vlc_metadata_job_t* child() const { return keeper_fixture_child(native.get()); }
    bool install(const QString& id, bool rust = false, bool network = false) {
        const auto directory = packages.filePath(id); if (!QDir().mkpath(directory)) return false;
        const QJsonArray fields{QStringLiteral("kind"), QStringLiteral("type"), QStringLiteral("title"), QStringLiteral("artist"), QStringLiteral("ids")};
        QJsonObject permissions{{QStringLiteral("metadata.context"), QJsonObject{{QStringLiteral("fields"), fields}}}};
        if(network) {
            permissions.insert(QStringLiteral("network.http"),QJsonObject{{QStringLiteral("origins"),QJsonArray{QStringLiteral("https://8.8.8.8")}},
                {QStringLiteral("methods"),QJsonArray{QStringLiteral("GET")}}});
            permissions.insert(QStringLiteral("credentials.use"),QJsonObject{{QStringLiteral("bindings"),QJsonArray{QJsonObject{
                {QStringLiteral("field"),QStringLiteral("apiKey")},{QStringLiteral("origin"),QStringLiteral("https://8.8.8.8")},
                {QStringLiteral("header"),QStringLiteral("X-Key")}}}}});
        }
        const QJsonObject contribution{{QStringLiteral("id"), QStringLiteral("artwork")}, {QStringLiteral("type"), QStringLiteral("metadata.provider")},
            {QStringLiteral("contract"), QStringLiteral("metadata@1")}, {QStringLiteral("features"), QJsonArray{QStringLiteral("artwork")}},
            {QStringLiteral("operations"), QJsonArray{QStringLiteral("enrich")}}, {QStringLiteral("requires"), QJsonArray{QStringLiteral("metadata.context")}}};
        QJsonObject manifest{{QStringLiteral("formatVersion"), 1}, {QStringLiteral("id"), id},
            {QStringLiteral("name"), QStringLiteral("Aggregate fixture")}, {QStringLiteral("version"), QStringLiteral("1.0.0")},
            {QStringLiteral("publisher"), QStringLiteral("Tests")}, {QStringLiteral("license"), QStringLiteral("GPL-2.0-or-later")},
            {QStringLiteral("source"), QStringLiteral("https://github.com/hakone-org/aggregate-fixture")},
            {QStringLiteral("api"), QJsonObject{{QStringLiteral("addon"), QStringLiteral("^1.0.0")},
                {QStringLiteral("contracts"), QJsonObject{{QStringLiteral("metadata"), QStringLiteral("^1.0.0")}}}}},
            {QStringLiteral("runtime"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("wasm-message-v1")}, {QStringLiteral("entry"), QStringLiteral("worker.wasm")}}},
            {QStringLiteral("files"), QJsonArray{QStringLiteral("addon.json"), QStringLiteral("LICENSE"), QStringLiteral("worker.wasm")}},
            {QStringLiteral("permissions"), QJsonObject{{QStringLiteral("required"), permissions}}},
            {QStringLiteral("contributions"), QJsonArray{contribution}}};
        if(network) {
            auto api=manifest.value(QStringLiteral("api")).toObject();auto contracts=api.value(QStringLiteral("contracts")).toObject();
            contracts.insert(QStringLiteral("settings"),QStringLiteral("^1.0.0"));api.insert(QStringLiteral("contracts"),contracts);manifest.insert(QStringLiteral("api"),api);
            manifest.insert(QStringLiteral("settings"),QJsonObject{{QStringLiteral("schema"),QStringLiteral("schema.json")},{QStringLiteral("version"),1}});
            auto files=manifest.value(QStringLiteral("files")).toArray();files.append(QStringLiteral("schema.json"));manifest.insert(QStringLiteral("files"),files);
            const QJsonObject keySchema{{QStringLiteral("type"),QStringLiteral("string")},{QStringLiteral("x-hakone-secret"),true}};
            const QJsonObject properties{{QStringLiteral("apiKey"),keySchema}};
            const QJsonObject schema{{QStringLiteral("type"),QStringLiteral("object")},{QStringLiteral("properties"),properties},
                {QStringLiteral("required"),QJsonArray{QStringLiteral("apiKey")}},{QStringLiteral("additionalProperties"),false}};
            if(!write(QDir(directory).filePath(QStringLiteral("schema.json")),QJsonDocument(schema).toJson())) return false;
        }
        if (!write(QDir(directory).filePath(QStringLiteral("addon.json")), QJsonDocument(manifest).toJson())
            || !write(QDir(directory).filePath(QStringLiteral("LICENSE")), QByteArray("Fixture license.\n"))) return false;
        if (rust) {
            if (!QFile::copy(QStringLiteral(HAKONE_AGGREGATE_FRENCH_TV_WASM), QDir(directory).filePath(QStringLiteral("worker.wasm")))) return false;
            manager->setWorkerExecutable(QStringLiteral(HAKONE_AGGREGATE_RUST_WORKER)); realWorker = true;
        } else if (!write(QDir(directory).filePath(QStringLiteral("worker.wasm")), QByteArray::fromHex("0061736d01000000"))) return false;
        if (!manager->installLocalPackage(directory, true)) { error = manager->errorMessage(); return false; }
        if(network) {
            const QVariantMap operation{{QStringLiteral("operation"),QStringLiteral("replace")},
                {QStringLiteral("value"),QStringLiteral("unit-image-key")},{QStringLiteral("sessionOnly"),true}};
            if(!manager->saveAddonSettings(id,{},{{QStringLiteral("apiKey"),operation}})) return false;
        }
        if (!manager->setEnabled(id, true)) { error = manager->errorMessage(); return false; }
        return true;
    }
    bool prepare(const QStringList& families = {}, const QStringList& order = {}, bool workerAvailable = true,
                 const QJsonObject& extraContext = {}) {
        builtins.clear(); for (const auto& family : families) builtins.append(builtin(family));
        if (!order.isEmpty() && !manager->setProviderOrder(QStringLiteral("artwork"), order)) return false;
        auto source = manager->metadataCatalogueSource(); if (!source) return false;
        const auto token = registry.capture();
        const auto deadline = vlc_tick_now() + VLC_TICK_FROM_SEC(20);
        gate = CoreMetadataAdmission::create(deadline); if (!gate || !registry.arm(token, gate)) return false;
        Plan::Capture capture; capture.catalogue = source->catalogueReader;
        capture.credentialReader = source->credentialReader; capture.knownPresence = source->credentialPresence;
        capture.builtins = builtins;
        const auto identity = token.identity(); capture.registryIdentity = QByteArray(reinterpret_cast<const char*>(identity.data()), identity.size());
        static std::atomic_uint subjects{0};
        capture.subject = QCryptographicHash::hash(QByteArray::number(++subjects), QCryptographicHash::Sha256);
        capture.privateScope = QStringLiteral("aggregate-private-scope"); capture.deadline = deadline;
        capture.requestContext = {{QStringLiteral("identity"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("movie")},
            {QStringLiteral("title"), QStringLiteral("Exact subject")}}}, {QStringLiteral("media"), QStringLiteral("opaque-media")}};
        for (auto it = extraContext.begin(); it != extraContext.end(); ++it) capture.requestContext.insert(it.key(), it.value());
        clock = std::make_shared<std::atomic<qint64>>(vlc_tick_now());
        capture.clock = [clock = clock] { return clock->load(); };
        capture.live = [token, gate = gate, app = lifetime.handle()] { return token.current() && gate->current() && app.current(); };
        capture.workerAvailable = workerAvailable;
        std::thread read([&] { plan = Plan::capture(std::move(capture), &error); }); read.join();
        if (!plan) return false;
        aggregate = Aggregate::create(plan, child(), gate, &error); return bool(aggregate);
    }
    std::optional<AddonMetadataCompletionReceipt> receipt(unsigned ordinal, int mismatch = 0,
        std::shared_ptr<CoreMetadataAdmission> otherGate = {}) {
        const auto& slot = plan->attempts.at(ordinal);
        AddonMetadataAttemptAdmission::Capture capture;
        capture.nativeSubject = mismatch == 1 ? QByteArray(32, 'x') : plan->subject;
        capture.nativeChildHold = std::shared_ptr<void>(vlc_metadata_job_Hold(child()), [](void* value) { vlc_metadata_job_Release(static_cast<vlc_metadata_job_t*>(value)); });
        capture.ordinal = mismatch == 2 ? ordinal + 1 : ordinal;
        capture.contribution = slot.contribution; capture.feature = slot.feature; capture.operation = slot.operation;
        capture.releaseDigest = slot.releaseDigest; capture.releaseGeneration = slot.releaseGeneration;
        capture.settingsRevision = slot.settingsRevision; capture.accountReference = slot.accountReference; capture.partition = slot.partition;
        capture.approvedGrants = slot.approvedGrants; capture.requestedFields = slot.requestedFields;
        capture.lifetime = lifetime.handle(); capture.admission = otherGate ? otherGate : gate;
        capture.live = [queued = plan->queuedAuthority, current = plan->authority] { return queued.current() && current.current(); };
        auto attempt = AddonMetadataAttemptAdmission::create(std::move(capture)); if (!attempt) return {};
        auto context = slot.projectedContext.toVariantMap();
        if (mismatch == 3) {
            auto identity = context.value(QStringLiteral("identity")).toMap(); identity.insert(QStringLiteral("title"), QStringLiteral("Another subject"));
            context.insert(QStringLiteral("identity"), identity);
        }
        QSignalSpy result(manager.get(), &AddonManager::operationFinished);
        QString invocation;
        if (!mismatch && !slot.blocked) {
            auto prepare = manager->metadataInvocationPreparer(plan, ordinal);
            std::unique_ptr<PreparedMetadataInvocation> recipe;
            std::thread work([&] { if (prepare) recipe = prepare(&error); }); work.join();
            if (!recipe) return {};
            invocation = manager->invokePreparedMetadata(std::move(recipe), {}, attempt);
        } else {
            // Deliberately obtain an otherwise valid, wrongly bound receipt
            // through the legacy host path. A blocked slot also uses this
            // adversarial fixture: production preparation refuses it first.
            invocation = manager->invoke(slot.contribution, QStringLiteral("enrich"), context, {}, attempt);
        }
        if (invocation.isEmpty()) return {};
        if (result.isEmpty() && !result.wait(5000)) return {};
        if (result.size() != 1) return {};
        returnedStatus = result.first()[2].toString(); returnedData = QJsonObject::fromVariantMap(result.first()[3].toMap());
        if (returnedStatus != QLatin1String("no_match") && returnedStatus != QLatin1String("match")
            && returnedStatus != QLatin1String("partial")) { error = QStringLiteral("The receipt fixture did not complete."); return {}; }
        return attempt->take();
    }
    static QJsonObject positiveContext(const QString& mode = QStringLiteral("positive")) {
        const QJsonObject record{
            {QStringLiteral("identity"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("movie")}, {QStringLiteral("title"), QStringLiteral("Exact subject")}}},
            {QStringLiteral("fields"), QJsonObject{{QStringLiteral("artwork"), QJsonObject{{QStringLiteral("id"), QStringLiteral("opaque-image")}}}}}};
        const QJsonObject result{{QStringLiteral("records"), QJsonArray{record}}};
        return {{QStringLiteral("query"), mode}, {QStringLiteral("media"), QJsonObject{{QStringLiteral("fixtureData"), result}}}};
    }
    bool consumerAccepts(unsigned ordinal) {
        const auto provider = plan->attempts.at(ordinal).contribution;
        AddonMetadataCoordinator* owner = nullptr;
        AddonMetadataCoordinator::Backend backend;
        backend.providersForFeature = [provider](const auto&) { return QVariantList{QVariantMap{
            {QStringLiteral("id"), provider}, {QStringLiteral("enabled"), true}, {QStringLiteral("configured"), true},
            {QStringLiteral("features"), QStringList{QStringLiteral("artwork")}}, {QStringLiteral("operations"), QStringList{QStringLiteral("enrich")}}}}; };
        backend.invoke = [&](const auto& contribution, const auto&, const auto&) {
            QTimer::singleShot(0, owner, [&, contribution] { owner->operationFinished(QStringLiteral("accepted-result"), contribution,
                returnedStatus, returnedData.toVariantMap(), {}); });
            return QStringLiteral("accepted-result");
        };
        AddonMetadataCoordinator consumer(std::move(backend)); owner = &consumer;
        QSignalSpy done(&consumer, &AddonMetadataCoordinator::finished);
        consumer.enrichNative(QStringLiteral("artwork"), plan->attempts.at(ordinal).projectedContext.toVariantMap(),
            {QStringLiteral("artwork")}, plan->privateScope);
        if (done.isEmpty() && !done.wait(5000)) return false;
        return done.size() == 1 && done.first()[1].toString() == QLatin1String("match")
            && done.first()[2].toMap().value(QStringLiteral("fields")).toMap().contains(QStringLiteral("artwork"));
    }
    bool nativeContext() {
        if (context) return true;
        const char* args[]{"--ignore-config", "--no-plugins-scan", "--no-plugins-cache"}; instance = libvlc_new(3, args); if (!instance) return false;
        addon_test_native_provider_queued(false); addon_test_native_provider_block_delete(false);
        context = std::make_unique<AddonNativeProviderContext>(lifetime.handle(), VLC_OBJECT(instance->p_libvlc_int));
        return context->available();
    }
    QString claim(meta_fetcher_provider_t family, meta_fetcher_outcome_t outcome, vlc_metadata_job_t* exact = nullptr) {
        if (!nativeContext()) return {};
        addon_test_native_provider_outcome(true, outcome);
        bool done = false; QString reference;
        auto cancel = context->probeOutcome(exact ? exact : child(), family, [&](const auto& result) { reference = result.claim; done = true; });
        if (!cancel) return {};
        QElapsedTimer clock; clock.start(); while (!done && clock.elapsed() < 5000) QTest::qWait(5);
        if (!done) cancel();
        return reference;
    }
    ArtworkLookupRunner::Options runnerOptions(QJsonObject extra = {}) {
        ArtworkLookupRunner::Options options;
        options.child = {vlc_metadata_job_Hold(child()),vlc_metadata_job_Release};
        options.privateScope = QStringLiteral("runner-private-scope");
        options.context = {{QStringLiteral("identity"), QJsonObject{{QStringLiteral("kind"),QStringLiteral("movie")},
            {QStringLiteral("title"),QStringLiteral("Exact subject")}}}, {QStringLiteral("media"),QStringLiteral("opaque-media")}};
        for (auto it=extra.begin();it!=extra.end();++it) options.context.insert(it.key(),it.value());
        options.lifetime = lifetime.handle();
        options.registry = std::shared_ptr<const MetadataRegistryEpoch>(&registry,[](const auto*) {});
        options.registryToken = registry.capture(); options.builtins = builtins;
        options.nativeContext = context.get(); options.live = [] { return true; };
        return options;
    }
    bool sealAndApply() {
        if (aggregate->trySeal(child()) != VLC_SUCCESS) return false;
        if (keeper_fixture_prepare_anchor(native.get()) != VLC_SUCCESS) return false;
        return keeper_fixture_apply(native.get()) == VLC_SUCCESS;
    }
};
}
class AggregateTests : public QObject {
    Q_OBJECT
private slots:
    void init() { QCOMPARE(CoreMetadataAdmission::processCount(), 0u); }
    void cleanup() { QCOMPARE(CoreMetadataAdmission::processCount(), 0u); }
    void portableServiceUsesRealImageBrokerThroughFinalCoreApply_data() {
        QTest::addColumn<bool>("revoke");QTest::newRow("apply")<<false;QTest::newRow("revoke before final Apply")<<true;
    }
    void portableServiceUsesRealImageBrokerThroughFinalCoreApply() {
        QFETCH(bool,revoke);QImage image(4,4,QImage::Format_RGB32);image.fill(Qt::blue);QByteArray bytes;QBuffer output(&bytes);output.open(QIODevice::WriteOnly);QVERIFY(image.save(&output,"PNG"));
        ReceiptTlsServer server;server.response="HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: "+QByteArray::number(bytes.size())+"\r\nConnection: close\r\n\r\n"+bytes;
        QVERIFY(server.start());const auto state=std::make_shared<ReceiptTransportState>();
        std::unique_ptr<QNetworkAccessManager> network=std::make_unique<ReceiptRoutedNetwork>(server,state);
        Fixture f(&network);const auto package=QStringLiteral("org.hakone.portable-image");QVERIFY2(f.install(package,false,true),qPrintable(f.error));
        auto service=std::make_unique<AddonMetadataService>(f.manager.get());QVERIFY(service->bindApplicationLifetime(f.lifetime.handle()));service->setAutomaticNetworkAllowed(true);
        const auto backend=std::shared_ptr<vlc_metadata_backend_t>(service->holdCoreBackend(),vlc_metadata_backend_Release);
        const auto context=QJsonDocument(QJsonObject{{QStringLiteral("query"),QStringLiteral("image-broker-positive")},
            {QStringLiteral("identity"),QJsonObject{{QStringLiteral("kind"),QStringLiteral("movie")},{QStringLiteral("title"),QStringLiteral("Exact subject")}}}}).toJson(QJsonDocument::Compact);
        Native mounted(keeper_fixture_new_backend(backend.get(),context.constData()),keeper_fixture_delete);QVERIFY(mounted);
        QTRY_VERIFY(keeper_fixture_child_done(mounted.get()));QCOMPARE(keeper_fixture_child_status(mounted.get()),VLC_SUCCESS);
        QCOMPARE(server.requests,1u);QCOMPARE(state->admitted,1u);QCOMPARE(keeper_fixture_prepare_completed(mounted.get(),true),VLC_SUCCESS);
        if(revoke) QVERIFY(f.manager->setEnabled(package,false));
        const auto applied=keeper_fixture_apply(mounted.get());
        if(revoke) { QVERIFY(applied!=VLC_SUCCESS);QVERIFY(!(keeper_fixture_status(mounted.get())&ITEM_ART_FETCHED)); }
        else { QCOMPARE(applied,VLC_SUCCESS);QVERIFY(keeper_fixture_status(mounted.get())&ITEM_ART_FETCHED); }
        QCOMPARE(service->nativeJobCount(),0);QCOMPARE(service->mediaCount(),0);service.reset();
    }
    void mountedServiceUsesTypedRunnerAndRetainsRevocation_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("portable no-match")<<0; QTest::newRow("native no-match")<<1;
        QTest::newRow("revoke after child completion")<<2; QTest::newRow("cancel pending native from Core")<<3;
        QTest::newRow("native image match")<<4;
    }
    void mountedServiceUsesTypedRunnerAndRetainsRevocation() {
        QFETCH(int,mode); Fixture f;
        if(mode==0 || mode==2) QVERIFY(f.install(QStringLiteral("org.hakone.mounted")));
        auto service=std::make_unique<AddonMetadataService>(f.manager.get());
        QVERIFY(service->bindApplicationLifetime(f.lifetime.handle())); service->setAutomaticNetworkAllowed(true);
        std::unique_ptr<AddonNativeMetadataOwner> owner;
        if(mode==1 || mode==3 || mode==4) {
            const char* args[]{"--ignore-config","--no-plugins-scan","--no-plugins-cache"};
            f.instance=libvlc_new(3,args); QVERIFY(f.instance);
            addon_test_native_provider_outcome(true,mode==4 ? META_FETCHER_OUTCOME_MATCH : META_FETCHER_OUTCOME_NO_MATCH);
            addon_test_native_provider_queued(mode==3);
            AddonNativeMetadataOwner::Options options; options.cacheDirectory=f.profile.filePath(QStringLiteral("mounted-images"));
            if(mode==4) {
                QImage pixels(4,4,QImage::Format_RGB32);pixels.fill(Qt::blue);QByteArray bytes;QBuffer output(&bytes);output.open(QIODevice::WriteOnly);QVERIFY(pixels.save(&output,"PNG"));
                options.imageReader.read=[bytes](const auto&,qint64,const auto&) { return AddonNativeArtworkAssets::ReadResult{bytes,{}}; };
            }
            owner=AddonNativeMetadataOwner::create(service.get(),f.manager.get(),VLC_OBJECT(f.instance->p_libvlc_int),options);
            QVERIFY(owner); QVERIFY(owner->ready());
        }
        const auto backend=std::shared_ptr<vlc_metadata_backend_t>(service->holdCoreBackend(),vlc_metadata_backend_Release);
        Native mounted(keeper_fixture_new_backend(backend.get(),nullptr),keeper_fixture_delete); QVERIFY(mounted);
        if(mode==3) {
            QTRY_COMPARE(addon_test_native_provider_pending(),1u);
            std::thread stop([&] { vlc_metadata_job_Cancel(keeper_fixture_child(mounted.get())); });stop.join();
            QTRY_VERIFY(keeper_fixture_child_done(mounted.get())); QTRY_COMPARE(addon_test_native_provider_pending(),0u);
            QVERIFY(keeper_fixture_child_status(mounted.get())!=VLC_SUCCESS); addon_test_native_provider_queued(false);
        } else {
            QTRY_VERIFY(keeper_fixture_child_done(mounted.get())); QCOMPARE(keeper_fixture_child_status(mounted.get()),VLC_SUCCESS);
            QCOMPARE(keeper_fixture_prepare_completed(mounted.get(),mode==4),VLC_SUCCESS);
            if(mode==2) service->setAutomaticNetworkAllowed(false);
            const auto status=keeper_fixture_apply(mounted.get());
            if(mode==2) { QVERIFY(status!=VLC_SUCCESS); QVERIFY(!(keeper_fixture_status(mounted.get())&ITEM_ART_NOTFOUND)); }
            else { QCOMPARE(status,VLC_SUCCESS); QVERIFY(keeper_fixture_status(mounted.get())&(mode==4 ? ITEM_ART_FETCHED : ITEM_ART_NOTFOUND)); }
        }
        QCOMPARE(service->nativeJobCount(),0); QTRY_COMPARE(service->mediaCount(),0);
        owner.reset(); service.reset();
    }
    void runnerNativeNegativeRequiresEverySelectedOutcome_data() {
        QTest::addColumn<int>("outcome");QTest::addColumn<bool>("success");
        QTest::newRow("all four no-match")<<int(META_FETCHER_OUTCOME_NO_MATCH)<<true;
        QTest::newRow("all not-applicable")<<int(META_FETCHER_OUTCOME_NOT_APPLICABLE)<<false;
        QTest::newRow("unknown")<<int(META_FETCHER_OUTCOME_UNKNOWN)<<false;
    }
    void runnerNativeNegativeRequiresEverySelectedOutcome() {
        QFETCH(int,outcome);QFETCH(bool,success);Fixture f;QVERIFY(f.nativeContext());
        for(const auto& name : {QStringLiteral("musicbrainz"),QStringLiteral("google-images"),QStringLiteral("french-tv"),QStringLiteral("lastfm")}) f.builtins.append(builtin(name));
        addon_test_native_provider_outcome(true,static_cast<meta_fetcher_outcome_t>(outcome));
        int completed=0;ArtworkLookupRunner::Result result;
        ArtworkLookupRunner runner(f.manager.get(),f.runnerOptions(),[&](auto value) { result=std::move(value);++completed; });
        QVERIFY(runner.start());QTRY_COMPARE(completed,1);
        QCOMPARE(result.status==VLC_SUCCESS,success);
        if(success) {
            QCOMPARE(keeper_fixture_prepare_anchor(f.native.get()),VLC_SUCCESS);QCOMPARE(keeper_fixture_apply(f.native.get()),VLC_SUCCESS);
            QVERIFY(keeper_fixture_status(f.native.get())&ITEM_ART_NOTFOUND);
        } else QVERIFY(!(keeper_fixture_status(f.native.get())&ITEM_ART_NOTFOUND));
    }
    void runnerPortableImageBrokerDecodesAndPublishes() {
        Fixture f;QVERIFY(f.install(QStringLiteral("org.hakone.image")));
        QImage pixels(4,4,QImage::Format_RGB32);pixels.fill(Qt::blue);QByteArray bytes;QBuffer output(&bytes);output.open(QIODevice::WriteOnly);QVERIFY(pixels.save(&output,"PNG"));
        int fetches=0;
        AddonArtworkAssets::Backend backend;
        backend.registration=[](const auto&) { return QVariantMap{{QStringLiteral("enabled"),true},{QStringLiteral("configured"),true},
            {QStringLiteral("cachePartition"),QStringLiteral("controlled-image-partition")}}; };
        backend.fetch=[&](const auto&,const auto&,auto complete) {
            ++fetches;NetworkBroker::Response response;response.status=200;response.body=bytes;
            QTimer::singleShot(0,QCoreApplication::instance(),[complete,response] { complete(response); });return quint64(1);
        };
        AddonArtworkAssets assets(std::move(backend),f.profile.filePath(QStringLiteral("portable-images")),{});
        auto extra=Fixture::positiveContext();
        auto media=extra.value(QStringLiteral("media")).toObject();auto payload=media.value(QStringLiteral("fixtureData")).toObject();
        auto records=payload.value(QStringLiteral("records")).toArray();auto record=records.at(0).toObject();
        record.insert(QStringLiteral("fields"),QJsonObject{{QStringLiteral("artwork"),QJsonObject{{QStringLiteral("url"),QStringLiteral("https://images.example.org/art.png")}}}});
        payload.insert(QStringLiteral("records"),QJsonArray{record});media.insert(QStringLiteral("fixtureData"),payload);extra.insert(QStringLiteral("media"),media);
        auto options=f.runnerOptions(extra);
        options.resolvePortable=[&](const auto& id,const auto& descriptor,auto complete) { return assets.resolve(id,descriptor,std::move(complete)); };
        options.cancelPortable=[&](const auto& id) { assets.cancel(id); };
        ArtworkLookupRunner::Result result;int completed=0;
        ArtworkLookupRunner runner(f.manager.get(),std::move(options),[&](auto value) { result=std::move(value);++completed; });
        QVERIFY(runner.start());QTRY_COMPARE(completed,1);QCOMPARE(result.status,VLC_SUCCESS);QCOMPARE(fetches,1);QVERIFY(result.item);
        char* uri=vlc_artwork_item_GetURI(result.item.get());QVERIFY(uri);const auto file=QUrl(QString::fromUtf8(uri));free(uri);
        QVERIFY(file.isLocalFile());QVERIFY(QFileInfo::exists(file.toLocalFile()));QVERIFY(!QImage(file.toLocalFile()).isNull());
        QCOMPARE(keeper_fixture_prepare_fetched(f.native.get(),result.item.get()),VLC_SUCCESS);QCOMPARE(keeper_fixture_apply(f.native.get()),VLC_SUCCESS);
        QVERIFY(keeper_fixture_status(f.native.get())&ITEM_ART_FETCHED);
    }
    void mountedServiceCannotUseLegacyAfterManagerRetires() {
        Fixture f;auto service=std::make_unique<AddonMetadataService>(f.manager.get());
        QVERIFY(service->bindApplicationLifetime(f.lifetime.handle()));service->setAutomaticNetworkAllowed(true);
        const auto backend=std::shared_ptr<vlc_metadata_backend_t>(service->holdCoreBackend(),vlc_metadata_backend_Release);
        unsigned completed=0;
        const auto idle=std::shared_ptr<vlc_metadata_job_t>(vlc_metadata_job_New(backend.get(),vlc_metadata_job_GetRequest(f.child()),
            [](vlc_metadata_job_t*,int,void* opaque) { ++*static_cast<unsigned*>(opaque); },&completed),vlc_metadata_job_Release);
        QVERIFY(idle); f.manager.reset(); QVERIFY(vlc_metadata_backend_IsClosed(backend.get()));
        QVERIFY(vlc_metadata_job_Start(idle.get())!=VLC_SUCCESS);
        QCoreApplication::processEvents();QCOMPARE(completed,0u);
        QTRY_COMPARE(service->mediaCount(),0);QCOMPARE(service->nativeJobCount(),0);
    }
    void runnerSerialPortableNoMatchPublishesNativeStatus() {
        Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.first"))); QVERIFY(f.install(QStringLiteral("org.hakone.second")));
        const QStringList order{QStringLiteral("org.hakone.second/artwork"),QStringLiteral("org.hakone.first/artwork")};
        QVERIFY(f.manager->setProviderOrder(QStringLiteral("artwork"),order));
        QStringList calls;
        connect(f.manager.get(),&AddonManager::operationFinished,this,[&](const auto&,const auto& provider,const auto&,const auto&,const auto&) { calls.append(provider); });
        ArtworkLookupRunner::Result result; int completed=0;
        ArtworkLookupRunner runner(f.manager.get(),f.runnerOptions(),[&](auto value) { result=std::move(value);++completed; });
        QElapsedTimer elapsed;elapsed.start(); QVERIFY(runner.start()); QVERIFY(elapsed.elapsed()<100);
        QTRY_COMPARE(completed,1); QCOMPARE(result.status,VLC_SUCCESS); QCOMPARE(calls,order);
        QCOMPARE(keeper_fixture_prepare_anchor(f.native.get()),VLC_SUCCESS);
        QCOMPARE(keeper_fixture_apply(f.native.get()),VLC_SUCCESS);
        QVERIFY(keeper_fixture_status(f.native.get())&ITEM_ART_NOTFOUND);
    }
    void runnerPortablePositiveUsesAcceptanceAndFreshRecheck_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("inline image")<<0; QTest::newRow("queued image")<<1;
        QTest::newRow("config changes before seal")<<2; QTest::newRow("registry changes after seal")<<3;
        QTest::newRow("inline callback deletes runner")<<4;
    }
    void runnerPortablePositiveUsesAcceptanceAndFreshRecheck() {
        QFETCH(int,mode); Fixture f; const auto id=QStringLiteral("org.hakone.positive"); QVERIFY(f.install(id));
        auto options=f.runnerOptions(Fixture::positiveContext());
        ArtworkLookupRunner::Result result; int completed=0,images=0,cancels=0;
        std::unique_ptr<ArtworkLookupRunner> runner;
        options.resolvePortable=[&](const auto&,const auto&,auto complete) {
            ++images;
            if(mode==2 && !f.manager->setEnabled(id,false)) return QString{};
            if(mode==1) QTimer::singleShot(0,QCoreApplication::instance(),[complete] { complete(QUrl(QStringLiteral("file:///runner/image.png")),{}); });
            else complete(QUrl(QStringLiteral("file:///runner/image.png")),{});
            return QStringLiteral("runner-image");
        };
        options.cancelPortable=[&](const auto&) { ++cancels; };
        runner=std::make_unique<ArtworkLookupRunner>(f.manager.get(),std::move(options),[&](auto value) {
            result=std::move(value);++completed; if(mode==4) runner.reset();
        });
        QVERIFY(runner->start()); QTRY_COMPARE(completed,1); QCOMPARE(images,1); QCOMPARE(cancels,0);
        if(mode==2) { QVERIFY(result.status!=VLC_SUCCESS); QVERIFY(!(keeper_fixture_status(f.native.get())&ITEM_ART_FETCHED)); return; }
        QCOMPARE(result.status,VLC_SUCCESS); QVERIFY(result.item);
        QCOMPARE(keeper_fixture_prepare_fetched(f.native.get(),result.item.get()),VLC_SUCCESS);
        if(mode==3) { const auto mutation=f.registry.mutation(); QVERIFY(mutation); }
        const auto applied=keeper_fixture_apply(f.native.get());
        if(mode==3) { QVERIFY(applied!=VLC_SUCCESS); QVERIFY(!(keeper_fixture_status(f.native.get())&ITEM_ART_FETCHED)); }
        else { QCOMPARE(applied,VLC_SUCCESS); QVERIFY(keeper_fixture_status(f.native.get())&ITEM_ART_FETCHED); }
    }
    void runnerCancellationDiscardsQueuedWork_data() {
        QTest::addColumn<bool>("native"); QTest::newRow("portable")<<false; QTest::newRow("native")<<true;
    }
    void runnerCancellationDiscardsQueuedWork() {
        QFETCH(bool,native); Fixture f;
        if(native) { QVERIFY(f.nativeContext()); f.builtins={builtin(QStringLiteral("lastfm"))}; addon_test_native_provider_outcome(true,META_FETCHER_OUTCOME_NO_MATCH); addon_test_native_provider_queued(true); }
        else QVERIFY(f.install(QStringLiteral("org.hakone.cancel")));
        auto options=f.runnerOptions({{QStringLiteral("query"),QStringLiteral("hold")}});
        int completed=0; ArtworkLookupRunner::Result result;
        ArtworkLookupRunner runner(f.manager.get(),std::move(options),[&](auto value) { result=std::move(value);++completed; });
        const auto cancel=runner.nativeCancellation(); QVERIFY(runner.start());
        if(native) QTRY_COMPARE(addon_test_native_provider_pending(),1u);
        std::thread stop(cancel);stop.join(); runner.cancel();
        QTRY_COMPARE(completed,1); QVERIFY(result.status!=VLC_SUCCESS);
        if(native) { addon_test_native_provider_complete(); addon_test_native_provider_queued(false); }
        QTest::qWait(25); QCOMPARE(completed,1); QVERIFY(!(keeper_fixture_status(f.native.get())&ITEM_ART_NOTFOUND));
    }
    void runnerNativePositiveUsesActualImageAndCore_data() {
        QTest::addColumn<bool>("revoke"); QTest::newRow("current")<<false; QTest::newRow("revoke image before Apply")<<true;
    }
    void runnerNativePositiveUsesActualImageAndCore() {
        QFETCH(bool,revoke); Fixture f; QVERIFY(f.nativeContext()); f.builtins={builtin(QStringLiteral("lastfm"))};
        addon_test_native_provider_outcome(true,META_FETCHER_OUTCOME_MATCH);
        QImage pixels(4,4,QImage::Format_RGB32);pixels.fill(Qt::blue);QByteArray bytes;QBuffer output(&bytes);output.open(QIODevice::WriteOnly);QVERIFY(pixels.save(&output,"PNG"));
        AddonNativeArtworkAssets assets(f.lifetime.handle(),AddonNativeArtworkAssets::Backend{
            [bytes](const auto&,qint64,const auto&) { return AddonNativeArtworkAssets::ReadResult{bytes,{}}; }},f.profile.filePath(QStringLiteral("runner-cache")),{});
        assets.setNetworkAllowed(true); AddonNativeProviderArtworkAssets images(&assets,f.context.get());
        auto options=f.runnerOptions();options.nativeImages=&images;
        ArtworkLookupRunner::Result result;int completed=0;
        ArtworkLookupRunner runner(f.manager.get(),std::move(options),[&](auto value) { result=std::move(value);++completed; });
        QVERIFY(runner.start());QTRY_COMPARE(completed,1);QCOMPARE(result.status,VLC_SUCCESS);QVERIFY(result.item);
        QCOMPARE(keeper_fixture_prepare_fetched(f.native.get(),result.item.get()),VLC_SUCCESS);
        if(revoke) assets.setNetworkAllowed(false);
        const auto applied=keeper_fixture_apply(f.native.get());
        if(revoke) { QVERIFY(applied!=VLC_SUCCESS);QVERIFY(!(keeper_fixture_status(f.native.get())&ITEM_ART_FETCHED)); }
        else { QCOMPARE(applied,VLC_SUCCESS);QVERIFY(keeper_fixture_status(f.native.get())&ITEM_ART_FETCHED); }
    }
    void nativePositiveKeepsClaimThroughImageAndFinalApply_data() {
        QTest::addColumn<QString>("family"); QTest::addColumn<int>("provider");
        QTest::newRow("musicbrainz") << QStringLiteral("musicbrainz") << int(META_FETCHER_PROVIDER_MUSICBRAINZ);
        QTest::newRow("google-images") << QStringLiteral("google-images") << int(META_FETCHER_PROVIDER_GOOGLE_IMAGES);
        QTest::newRow("french-tv") << QStringLiteral("french-tv") << int(META_FETCHER_PROVIDER_FRENCH_TV);
        QTest::newRow("lastfm") << QStringLiteral("lastfm") << int(META_FETCHER_PROVIDER_LASTFM);
    }
    void nativePositiveKeepsClaimThroughImageAndFinalApply() {
        QFETCH(QString,family); QFETCH(int,provider); Fixture f; QVERIFY(f.prepare({family})); QVERIFY(f.aggregate->reserve(0));
        const auto nativeFamily=static_cast<meta_fetcher_provider_t>(provider); const auto reference=f.claim(nativeFamily,META_FETCHER_OUTCOME_MATCH);
        QVERIFY(!reference.isEmpty()); QVERIFY(f.aggregate->acceptNativePositive(0,*f.context,reference));
        QVERIFY(f.context->validates(reference,f.child(),nativeFamily));
        QImage pixels(4,4,QImage::Format_RGB32); pixels.fill(Qt::blue); QByteArray bytes; QBuffer output(&bytes); output.open(QIODevice::WriteOnly); QVERIFY(pixels.save(&output,"PNG"));
        unsigned reads=0; AddonNativeArtworkAssets assets(f.lifetime.handle(),AddonNativeArtworkAssets::Backend{
            [&](const auto&,qint64,const auto&) { ++reads; return AddonNativeArtworkAssets::ReadResult{bytes,{}}; }},f.profile.filePath(QStringLiteral("image-cache")),{});
        assets.setNetworkAllowed(true); AddonNativeProviderArtworkAssets images(&assets,f.context.get());
        AddonNativeProviderArtworkAssets::Result image; bool done=false;
        QVERIFY(images.resolve(reference,f.child(),nativeFamily,[&](const auto& value) { image=value; done=true; })); QTRY_VERIFY(done);
        QCOMPARE(reads,1u); QVERIFY(!image.file.isEmpty()); QVERIFY(!f.context->validates(reference,f.child(),nativeFamily));
        const auto uri=image.file.toEncoded(); std::unique_ptr<input_item_t,decltype(&input_item_Release)> approved(keeper_fixture_approved(f.native.get(),uri.constData()),input_item_Release); QVERIFY(approved);
        QCOMPARE(f.aggregate->trySealNativePositive(0,f.child(),approved.get(),uri.constData(),image.permit),VLC_SUCCESS);
        f.aggregate.reset(); QVERIFY(f.gate->current()); QCOMPARE(keeper_fixture_prepare_fetched(f.native.get(),approved.get()),VLC_SUCCESS);
        QCOMPARE(keeper_fixture_apply(f.native.get()),VLC_SUCCESS); QVERIFY(keeper_fixture_status(f.native.get())&ITEM_ART_FETCHED);
        QVERIFY(!(keeper_fixture_status(f.native.get())&ITEM_ART_NOTFOUND)); assets.close(); QVERIFY(!f.gate->current());
    }
    void nativePositiveRefusesRevokedProviderOrImageConsent_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("network revoked after seal") << 0; QTest::newRow("provider closed after seal") << 1;
        QTest::newRow("image owner closed after seal") << 2; QTest::newRow("consent toggled") << 3;
        QTest::newRow("consent revoked before seal") << 4; QTest::newRow("same consent stays valid") << 5;
    }
    void nativePositiveRefusesRevokedProviderOrImageConsent() {
        QFETCH(int,mode); Fixture f; QVERIFY(f.prepare({QStringLiteral("lastfm")})); QVERIFY(f.aggregate->reserve(0));
        const auto reference=f.claim(META_FETCHER_PROVIDER_LASTFM,META_FETCHER_OUTCOME_MATCH); QVERIFY(!reference.isEmpty());
        QVERIFY(f.aggregate->acceptNativePositive(0,*f.context,reference));
        QImage pixels(4,4,QImage::Format_RGB32); pixels.fill(Qt::blue); QByteArray bytes; QBuffer output(&bytes); output.open(QIODevice::WriteOnly); QVERIFY(pixels.save(&output,"PNG"));
        AddonNativeArtworkAssets assets(f.lifetime.handle(),AddonNativeArtworkAssets::Backend{
            [bytes](const auto&,qint64,const auto&) { return AddonNativeArtworkAssets::ReadResult{bytes,{}}; }},f.profile.filePath(QStringLiteral("image-cache")),{});
        assets.setNetworkAllowed(true); AddonNativeProviderArtworkAssets images(&assets,f.context.get());
        AddonNativeProviderArtworkAssets::Result image; bool done=false;
        QVERIFY(images.resolve(reference,f.child(),META_FETCHER_PROVIDER_LASTFM,[&](const auto& value) { image=value; done=true; })); QTRY_VERIFY(done); QVERIFY(!image.file.isEmpty());
        const auto uri=image.file.toEncoded(); std::unique_ptr<input_item_t,decltype(&input_item_Release)> approved(keeper_fixture_approved(f.native.get(),uri.constData()),input_item_Release); QVERIFY(approved);
        if(mode==4) assets.setNetworkAllowed(false);
        const auto sealed=f.aggregate->trySealNativePositive(0,f.child(),approved.get(),uri.constData(),image.permit);
        if(mode==4) { QVERIFY(sealed!=VLC_SUCCESS); QVERIFY(!f.gate->current()); return; }
        QCOMPARE(sealed,VLC_SUCCESS); f.aggregate.reset(); QCOMPARE(keeper_fixture_prepare_fetched(f.native.get(),approved.get()),VLC_SUCCESS);
        if(mode==0) assets.setNetworkAllowed(false);
        if(mode==1) f.context->close();
        if(mode==2) assets.close();
        if(mode==3) { assets.setNetworkAllowed(false); assets.setNetworkAllowed(true); }
        if(mode==5) assets.setNetworkAllowed(true);
        const auto applied=keeper_fixture_apply(f.native.get());
        if(mode==5) { QCOMPARE(applied,VLC_SUCCESS); QVERIFY(keeper_fixture_status(f.native.get())&ITEM_ART_FETCHED); }
        else { QVERIFY(applied!=VLC_SUCCESS); QVERIFY(!f.gate->current()); QVERIFY(!(keeper_fixture_status(f.native.get())&ITEM_ART_FETCHED)); }
    }
    void nativePositiveCannotUseAnEmptyImagePermit() {
        Fixture f; QVERIFY(f.prepare({QStringLiteral("lastfm")})); QVERIFY(f.aggregate->reserve(0));
        const auto reference=f.claim(META_FETCHER_PROVIDER_LASTFM,META_FETCHER_OUTCOME_MATCH); QVERIFY(f.aggregate->acceptNativePositive(0,*f.context,reference));
        const auto uri="file:///cache/unrelated.png"; std::unique_ptr<input_item_t,decltype(&input_item_Release)> approved(keeper_fixture_approved(f.native.get(),uri),input_item_Release); QVERIFY(approved);
        QVERIFY(f.aggregate->trySealNativePositive(0,f.child(),approved.get(),uri,{})!=VLC_SUCCESS); QVERIFY(!f.gate->current());
    }
    void portablePositiveTransfersThroughActualCoreApply_data() {
        QTest::addColumn<QString>("mode");
        QTest::newRow("match") << QStringLiteral("positive");
        QTest::newRow("partial") << QStringLiteral("positive-partial");
    }
    void portablePositiveTransfersThroughActualCoreApply() {
        QFETCH(QString,mode); Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.aggregate")));
        QVERIFY2(f.prepare({}, {}, true, Fixture::positiveContext(mode)), qPrintable(f.error));
        QVERIFY(f.aggregate->reserve(0)); auto receipt=f.receipt(0); QVERIFY2(receipt,qPrintable(f.error));
        QVERIFY(f.aggregate->acceptPortablePositive(0,std::move(receipt),f.returnedData));
        QVERIFY(f.aggregate->negativeForbidden()); QVERIFY(!f.aggregate->nextOrdinal());
        QCOMPARE(f.aggregate->terminal(0),Aggregate::Terminal::Reserved); QVERIFY(f.consumerAccepts(0));
        const auto image="file:///cache/aggregate-positive.png";
        std::unique_ptr<input_item_t,decltype(&input_item_Release)> approved(keeper_fixture_approved(f.native.get(),image),input_item_Release); QVERIFY(approved);
        QCOMPARE(f.aggregate->trySealPositive(0,f.child(),approved.get(),image),VLC_SUCCESS);
        QVERIFY(f.aggregate->sealed()); f.aggregate.reset(); QVERIFY(f.gate->current());
        QCOMPARE(keeper_fixture_prepare_fetched(f.native.get(),approved.get()),VLC_SUCCESS);
        QCOMPARE(keeper_fixture_apply(f.native.get()),VLC_SUCCESS);
        QVERIFY(keeper_fixture_status(f.native.get())&ITEM_ART_FETCHED);
        QVERIFY(keeper_fixture_status(f.native.get())&ITEM_PREPARSED);
        QVERIFY(!(keeper_fixture_status(f.native.get())&ITEM_ART_NOTFOUND));
    }
    void portablePositiveRejectsChangedBytesAndNegativeReceipts_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("changed result") << 0; QTest::newRow("negative receipt") << 1; QTest::newRow("positive in negative slot") << 2;
        QTest::newRow("duplicate positive delivery") << 3;
    }
    void portablePositiveRejectsChangedBytesAndNegativeReceipts() {
        QFETCH(int,mode); Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.aggregate")));
        QVERIFY(f.prepare({}, {}, true, mode==1 ? QJsonObject{} : Fixture::positiveContext()));
        QVERIFY(f.aggregate->reserve(0)); auto receipt=f.receipt(0); QVERIFY(receipt);
        if(mode==0) { auto data=f.returnedData; data.insert(QStringLiteral("foreign"),true); QVERIFY(!f.aggregate->acceptPortablePositive(0,std::move(receipt),data)); }
        if(mode==1) QVERIFY(!f.aggregate->acceptPortablePositive(0,std::move(receipt),f.returnedData));
        if(mode==2) QVERIFY(!f.aggregate->acceptPortable(0,std::move(receipt)));
        if(mode==3) {
            QVERIFY(f.aggregate->acceptPortablePositive(0,std::move(receipt),f.returnedData));
            auto duplicate=f.receipt(0); QVERIFY(duplicate); QVERIFY(!f.aggregate->acceptPortablePositive(0,std::move(duplicate),f.returnedData));
        }
        QVERIFY(f.aggregate->negativeForbidden()); QVERIFY(f.aggregate->trySeal(f.child())!=VLC_SUCCESS);
        QVERIFY(!(keeper_fixture_status(f.native.get())&ITEM_ART_FETCHED));
    }
    void positiveReceiptRequiresTheExactAttempt_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("subject") << 1; QTest::newRow("ordinal") << 2;
        QTest::newRow("context") << 3; QTest::newRow("admission") << 4; QTest::newRow("contribution") << 5;
    }
    void positiveReceiptRequiresTheExactAttempt() {
        QFETCH(int,mode); Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.aggregate-a")));
        if(mode==5) QVERIFY(f.install(QStringLiteral("org.hakone.aggregate-b")));
        QVERIFY(f.prepare({}, {}, true, Fixture::positiveContext())); QVERIFY(f.aggregate->reserve(0));
        auto other=mode==4 ? CoreMetadataAdmission::create(vlc_tick_now()+VLC_TICK_FROM_SEC(5)) : std::shared_ptr<CoreMetadataAdmission>{};
        auto receipt=f.receipt(mode==5 ? 1 : 0,mode<4 ? mode : 0,other); QVERIFY2(receipt,qPrintable(f.error));
        QVERIFY(!f.aggregate->acceptPortablePositive(0,std::move(receipt),f.returnedData));
        QVERIFY(f.aggregate->negativeForbidden()); QVERIFY(!(keeper_fixture_status(f.native.get())&ITEM_ART_FETCHED));
    }
    void positiveSealRequiresItsReceiptAndNativeImage_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("no receipt") << 0; QTest::newRow("raw artwork text") << 1; QTest::newRow("foreign child") << 2;
    }
    void positiveSealRequiresItsReceiptAndNativeImage() {
        QFETCH(int,mode); Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.aggregate")));
        QVERIFY(f.prepare({}, {}, true, Fixture::positiveContext())); QVERIFY(f.aggregate->reserve(0));
        if(mode) { auto receipt=f.receipt(0); QVERIFY(receipt); QVERIFY(f.aggregate->acceptPortablePositive(0,std::move(receipt),f.returnedData)); }
        const auto image="file:///cache/aggregate-positive.png";
        std::unique_ptr<input_item_t,decltype(&input_item_Release)> approved(keeper_fixture_approved(f.native.get(),image),input_item_Release); QVERIFY(approved);
        if(mode==1) input_item_SetArtURL(approved.get(),image);
        Native other{mode==2 ? keeper_fixture_new() : nullptr,keeper_fixture_delete};
        QVERIFY(f.aggregate->trySealPositive(0,other ? keeper_fixture_child(other.get()) : f.child(),approved.get(),image)!=VLC_SUCCESS);
        QVERIFY(!f.gate->current()); QVERIFY(!(keeper_fixture_status(f.native.get())&ITEM_ART_FETCHED));
    }
    void positiveAuthorityLivesPastAggregateAndRefusesChangedConfig_data() {
        QTest::addColumn<bool>("prepared"); QTest::newRow("before fetched preparation") << false; QTest::newRow("before final apply") << true;
    }
    void positiveAuthorityLivesPastAggregateAndRefusesChangedConfig() {
        QFETCH(bool,prepared); Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.aggregate")));
        QVERIFY(f.prepare({}, {}, true, Fixture::positiveContext())); QVERIFY(f.aggregate->reserve(0));
        auto receipt=f.receipt(0); QVERIFY(receipt); QVERIFY(f.aggregate->acceptPortablePositive(0,std::move(receipt),f.returnedData));
        const auto image="file:///cache/aggregate-positive.png";
        std::unique_ptr<input_item_t,decltype(&input_item_Release)> approved(keeper_fixture_approved(f.native.get(),image),input_item_Release); QVERIFY(approved);
        QCOMPARE(f.aggregate->trySealPositive(0,f.child(),approved.get(),image),VLC_SUCCESS); f.aggregate.reset(); QVERIFY(f.gate->current());
        if(prepared) QCOMPARE(keeper_fixture_prepare_fetched(f.native.get(),approved.get()),VLC_SUCCESS);
        QVERIFY(f.manager->setEnabled(QStringLiteral("org.hakone.aggregate"),false)); QVERIFY(!f.gate->current());
        if(prepared) QVERIFY(keeper_fixture_apply(f.native.get())!=VLC_SUCCESS);
        else QVERIFY(keeper_fixture_prepare_fetched(f.native.get(),approved.get())!=VLC_SUCCESS);
        QVERIFY(!(keeper_fixture_status(f.native.get())&ITEM_ART_FETCHED));
    }
    void positiveSealRetainsCancellationAndDeadline_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("cancel after seal") << 0; QTest::newRow("late result after seal") << 1; QTest::newRow("deadline during image work") << 2;
    }
    void positiveSealRetainsCancellationAndDeadline() {
        QFETCH(int,mode); Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.aggregate")));
        QVERIFY(f.prepare({}, {}, true, Fixture::positiveContext())); QVERIFY(f.aggregate->reserve(0));
        auto receipt=f.receipt(0); QVERIFY(receipt); QVERIFY(f.aggregate->acceptPortablePositive(0,std::move(receipt),f.returnedData));
        const auto image="file:///cache/aggregate-positive.png";
        std::unique_ptr<input_item_t,decltype(&input_item_Release)> approved(keeper_fixture_approved(f.native.get(),image),input_item_Release); QVERIFY(approved);
        auto cancel=f.aggregate->cancellationControl();
        if(mode==2) {
            f.clock->store(f.plan->deadline); QVERIFY(f.aggregate->trySealPositive(0,f.child(),approved.get(),image)!=VLC_SUCCESS);
        } else {
            QCOMPARE(f.aggregate->trySealPositive(0,f.child(),approved.get(),image),VLC_SUCCESS);
            QCOMPARE(keeper_fixture_prepare_fetched(f.native.get(),approved.get()),VLC_SUCCESS);
            if(mode==0) { f.aggregate.reset(); cancel(); }
            else f.aggregate->block(0,Aggregate::BlockReason::Duplicate);
            QVERIFY(keeper_fixture_apply(f.native.get())!=VLC_SUCCESS);
        }
        QVERIFY(!f.gate->current()); QVERIFY(!(keeper_fixture_status(f.native.get())&ITEM_ART_FETCHED));
    }
    void failedImageAllowsALaterPositiveWithNativeAuthority() {
        Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.aggregate-a"))); QVERIFY(f.install(QStringLiteral("org.hakone.aggregate-b")));
        QVERIFY(f.prepare({}, {}, true, Fixture::positiveContext())); QVERIFY(f.aggregate->reserve(0));
        auto first=f.receipt(0); QVERIFY(first); QVERIFY(f.aggregate->acceptPortablePositive(0,std::move(first),f.returnedData));
        f.aggregate->block(0,Aggregate::BlockReason::ImageFailure); QVERIFY(f.aggregate->reserve(1));
        auto second=f.receipt(1); QVERIFY(second); QVERIFY(f.aggregate->acceptPortablePositive(1,std::move(second),f.returnedData));
        const auto image="file:///cache/aggregate-positive.png";
        std::unique_ptr<input_item_t,decltype(&input_item_Release)> approved(keeper_fixture_approved(f.native.get(),image),input_item_Release); QVERIFY(approved);
        QCOMPARE(f.aggregate->trySealPositive(1,f.child(),approved.get(),image),VLC_SUCCESS);
        QCOMPARE(keeper_fixture_prepare_fetched(f.native.get(),approved.get()),VLC_SUCCESS); QCOMPARE(keeper_fixture_apply(f.native.get()),VLC_SUCCESS);
        QVERIFY(keeper_fixture_status(f.native.get())&ITEM_ART_FETCHED); QVERIFY(!(keeper_fixture_status(f.native.get())&ITEM_ART_NOTFOUND));
    }
    void portableReceiptSealsTheActualNativeTransaction() {
        Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.aggregate"))); QVERIFY2(f.prepare(), qPrintable(f.error));
        QVERIFY(f.aggregate->reserve(0)); auto receipt = f.receipt(0); QVERIFY(receipt); QVERIFY(f.aggregate->acceptPortable(0, std::move(receipt)));
        QVERIFY(f.aggregate->exhausted()); QVERIFY(!f.aggregate->negativeForbidden()); QVERIFY(f.sealAndApply());
        QVERIFY(keeper_fixture_status(f.native.get()) & ITEM_ART_NOTFOUND); QVERIFY(keeper_fixture_status(f.native.get()) & ITEM_PREPARSED);
    }
    void realRustWorkerReceiptSeals() {
        Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.aggregate"), true)); QVERIFY2(f.prepare(), qPrintable(f.error));
        QVERIFY(f.aggregate->reserve(0)); auto receipt = f.receipt(0); QVERIFY2(receipt, qPrintable(f.error));
        QVERIFY(f.aggregate->acceptPortable(0, std::move(receipt))); QVERIFY(f.sealAndApply());
        QVERIFY(keeper_fixture_status(f.native.get()) & ITEM_ART_NOTFOUND);
    }
    void nativeAndPortableNoMatchUseTheSameGate() {
        Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.aggregate"))); QVERIFY(f.prepare({QStringLiteral("musicbrainz")}));
        QVERIFY(f.aggregate->reserve(0)); const auto claim = f.claim(META_FETCHER_PROVIDER_MUSICBRAINZ, META_FETCHER_OUTCOME_NO_MATCH); QVERIFY(!claim.isEmpty());
        QVERIFY(f.aggregate->acceptNativeClaim(0, *f.context, claim));
        QVERIFY(f.aggregate->reserve(1)); auto receipt = f.receipt(1); QVERIFY(receipt); QVERIFY(f.aggregate->acceptPortable(1, std::move(receipt)));
        QVERIFY(f.sealAndApply()); QVERIFY(keeper_fixture_status(f.native.get()) & ITEM_ART_NOTFOUND);
    }
    void noMatchAndNotApplicableSeal() {
        Fixture f; QVERIFY(f.prepare({QStringLiteral("musicbrainz"), QStringLiteral("google-images")}));
        QVERIFY(f.aggregate->reserve(0)); auto claim = f.claim(META_FETCHER_PROVIDER_MUSICBRAINZ, META_FETCHER_OUTCOME_NO_MATCH);
        QVERIFY(f.aggregate->acceptNativeClaim(0, *f.context, claim));
        QVERIFY(f.aggregate->reserve(1)); claim = f.claim(META_FETCHER_PROVIDER_GOOGLE_IMAGES, META_FETCHER_OUTCOME_NOT_APPLICABLE);
        QVERIFY(f.aggregate->acceptNativeClaim(1, *f.context, claim)); QVERIFY(f.sealAndApply());
    }
    void allNotApplicableCannotSeal() {
        Fixture f; QVERIFY(f.prepare({QStringLiteral("lastfm")})); QVERIFY(f.aggregate->reserve(0));
        const auto claim = f.claim(META_FETCHER_PROVIDER_LASTFM, META_FETCHER_OUTCOME_NOT_APPLICABLE);
        QVERIFY(f.aggregate->acceptNativeClaim(0, *f.context, claim)); QVERIFY(f.aggregate->trySeal(f.child()) != VLC_SUCCESS);
        QVERIFY(f.aggregate->negativeForbidden()); QVERIFY(!(keeper_fixture_status(f.native.get()) & ITEM_ART_NOTFOUND));
    }
    void emptyPlanCannotCreateAnAggregate() {
        Fixture f; QVERIFY(!f.prepare()); QVERIFY(f.plan); QVERIFY(f.plan->attempts.isEmpty()); QVERIFY(!f.gate->current());
    }
    void portableBindingMismatch_data() {
        QTest::addColumn<int>("mismatch");
        QTest::newRow("subject") << 1; QTest::newRow("ordinal") << 2; QTest::newRow("projected-context") << 3; QTest::newRow("concrete-gate") << 4;
    }
    void portableBindingMismatch() {
        QFETCH(int, mismatch); Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.aggregate"))); QVERIFY(f.prepare());
        auto other = mismatch == 4 ? CoreMetadataAdmission::create(vlc_tick_now() + VLC_TICK_FROM_SEC(20)) : nullptr;
        QVERIFY(f.aggregate->reserve(0)); auto receipt = f.receipt(0, mismatch, other); QVERIFY(receipt);
        QVERIFY(!f.aggregate->acceptPortable(0, std::move(receipt))); QVERIFY(f.aggregate->negativeForbidden());
        QVERIFY(f.aggregate->trySeal(f.child()) != VLC_SUCCESS);
    }
    void absentAndDuplicateCompletionsStayBlocked() {
        Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.aggregate"))); QVERIFY(f.prepare());
        QVERIFY(f.aggregate->reserve(0)); QVERIFY(!f.aggregate->acceptPortable(0, {})); QVERIFY(f.aggregate->negativeForbidden());
        auto receipt = f.receipt(0); QVERIFY(receipt); QVERIFY(!f.aggregate->acceptPortable(0, std::move(receipt)));
        QVERIFY(f.aggregate->trySeal(f.child()) != VLC_SUCCESS);
    }
    void duplicateOfAValidReceiptCannotRepairTheLedger() {
        Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.aggregate"))); QVERIFY(f.prepare());
        QVERIFY(f.aggregate->reserve(0)); auto first = f.receipt(0); QVERIFY(first); QVERIFY(f.aggregate->acceptPortable(0, std::move(first)));
        auto second = f.receipt(0); QVERIFY(second); QVERIFY(!f.aggregate->acceptPortable(0, std::move(second)));
        QVERIFY(f.aggregate->negativeForbidden()); QVERIFY(f.aggregate->trySeal(f.child()) != VLC_SUCCESS);
    }
    void inlineCompletionSeesItsReservedOrdinal() {
        Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.aggregate"))); QVERIFY(f.prepare());
        auto receipt = f.receipt(0); QVERIFY(receipt); bool accepted = false;
        const auto backend = [&] { accepted = f.aggregate->acceptPortable(0, std::move(receipt)); };
        QVERIFY(f.aggregate->reserve(0)); backend(); QVERIFY(accepted); QVERIFY(f.sealAndApply());
    }
    void completedChildAndWrongThreadCannotAdmit() {
        {
            Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.aggregate"))); QVERIFY(f.prepare());
            QVERIFY(f.aggregate->reserve(0)); auto receipt = f.receipt(0); QVERIFY(receipt);
            QCOMPARE(keeper_fixture_prepare_anchor(f.native.get()), VLC_SUCCESS);
            QVERIFY(!vlc_metadata_job_IsPending(f.child()));
            QVERIFY(!f.aggregate->acceptPortable(0, std::move(receipt))); QVERIFY(!f.gate->current());
        }
        {
            Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.aggregate"))); QVERIFY(f.prepare());
            bool accepted = true; std::thread foreign([&] { accepted = f.aggregate->reserve(0); }); foreign.join();
            QVERIFY(!accepted); QVERIFY(!f.gate->current()); QVERIFY(f.aggregate->negativeForbidden());
        }
    }
    void selectedOverflowCannotCreateAnAggregate() {
        Fixture f; for (int i = 0; i < 16; ++i) QVERIFY(f.install(QStringLiteral("org.hakone.item%1").arg(i)));
        QVERIFY(!f.prepare({QStringLiteral("musicbrainz")})); QVERIFY(!f.plan); QVERIFY(!f.aggregate);
        QVERIFY(!(keeper_fixture_status(f.native.get()) & ITEM_ART_NOTFOUND));
    }
    void aPreblockedSlotCannotBeReplacedByReceiptEvidence() {
        Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.aggregate")));
        QVERIFY(f.prepare({QStringLiteral("musicbrainz")}, {QStringLiteral("org.hakone.aggregate/artwork")}, false));
        QVERIFY(f.aggregate->negativeForbidden()); QVERIFY(f.aggregate->reserve(0));
        QVERIFY(!f.manager->metadataInvocationPreparer(f.plan, 0));
        auto receipt = f.receipt(0); QVERIFY(receipt); QVERIFY(!f.aggregate->acceptPortable(0, std::move(receipt)));
        QVERIFY(f.aggregate->reserve(1)); QVERIFY(f.aggregate->positive(1)); QVERIFY(f.gate->current());
    }
    void completionBeforeReservationCannotEnter() {
        Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.aggregate"))); QVERIFY(f.prepare());
        auto receipt = f.receipt(0); QVERIFY(receipt); QVERIFY(!f.aggregate->acceptPortable(0, std::move(receipt)));
        QVERIFY(f.aggregate->negativeForbidden());
    }
    void serialReservationIsRequired() {
        Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.first"))); QVERIFY(f.install(QStringLiteral("org.hakone.second"))); QVERIFY(f.prepare());
        QVERIFY(f.aggregate->reserve(0)); QVERIFY(!f.aggregate->reserve(1)); QVERIFY(f.aggregate->negativeForbidden());
        auto receipt = f.receipt(0); QVERIFY(receipt); QVERIFY(f.aggregate->acceptPortable(0, std::move(receipt)));
        QVERIFY(f.aggregate->reserve(1)); QVERIFY(f.aggregate->positive(1)); QVERIFY(f.gate->current());
        QVERIFY(f.aggregate->trySeal(f.child()) != VLC_SUCCESS);
    }
    void anErrorAllowsALaterPositiveButNeverANegative_data() {
        QTest::addColumn<int>("reason");
        for (auto value : {Aggregate::BlockReason::Denied, Aggregate::BlockReason::Error, Aggregate::BlockReason::ImageFailure,
                Aggregate::BlockReason::Absent, Aggregate::BlockReason::Unsupported}) QTest::newRow(qPrintable(QString::number(int(value)))) << int(value);
    }
    void anErrorAllowsALaterPositiveButNeverANegative() {
        QFETCH(int, reason); Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.first"))); QVERIFY(f.install(QStringLiteral("org.hakone.second"))); QVERIFY(f.prepare());
        QVERIFY(f.aggregate->reserve(0)); f.aggregate->block(0, Aggregate::BlockReason(reason)); QVERIFY(f.aggregate->negativeForbidden()); QVERIFY(f.gate->current());
        QVERIFY(f.aggregate->reserve(1)); QVERIFY(f.aggregate->positive(1)); QVERIFY(f.gate->current());
        QVERIFY(f.aggregate->trySeal(f.child()) != VLC_SUCCESS); QVERIFY(!(keeper_fixture_status(f.native.get()) & ITEM_ART_NOTFOUND));
    }
    void nativeClaimBindingsAreChecked_data() {
        QTest::addColumn<int>("mode"); QTest::newRow("unknown") << 0; QTest::newRow("wrong-family") << 1; QTest::newRow("wrong-child") << 2;
    }
    void nativeClaimBindingsAreChecked() {
        QFETCH(int, mode); Fixture f; QVERIFY(f.prepare({QStringLiteral("musicbrainz")})); QVERIFY(f.aggregate->reserve(0));
        Native other(keeper_fixture_new(), keeper_fixture_delete);
        QString claim;
        if (mode == 0) { QVERIFY(f.nativeContext()); claim = QStringLiteral("unknown-claim"); }
        else claim = f.claim(mode == 1 ? META_FETCHER_PROVIDER_LASTFM : META_FETCHER_PROVIDER_MUSICBRAINZ,
            META_FETCHER_OUTCOME_NO_MATCH, mode == 2 ? keeper_fixture_child(other.get()) : nullptr);
        QVERIFY(!claim.isEmpty()); QVERIFY(!f.aggregate->acceptNativeClaim(0, *f.context, claim)); QVERIFY(f.aggregate->negativeForbidden());
        QVERIFY(f.aggregate->trySeal(f.child()) != VLC_SUCCESS);
    }
    void storeAndRegistryRevocationRefuseSeal_data() {
        QTest::addColumn<bool>("registry"); QTest::newRow("store") << false; QTest::newRow("registry") << true;
    }
    void storeAndRegistryRevocationRefuseSeal() {
        QFETCH(bool, registry); Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.aggregate"))); QVERIFY(f.prepare());
        QVERIFY(f.aggregate->reserve(0)); auto receipt = f.receipt(0); QVERIFY(receipt); QVERIFY(f.aggregate->acceptPortable(0, std::move(receipt)));
        if (registry) { const auto change = f.registry.mutation(); QVERIFY(bool(change)); }
        else { PackageStore peer(f.profile.path()); peer.invalidateAuthority(); }
        QVERIFY(!f.gate->current()); QVERIFY(f.aggregate->trySeal(f.child()) != VLC_SUCCESS); QVERIFY(!(keeper_fixture_status(f.native.get()) & ITEM_ART_NOTFOUND));
    }
    void staleReceiptAndExpiredPlanCannotSeal() {
        Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.aggregate"))); QVERIFY(f.prepare());
        QVERIFY(f.aggregate->reserve(0)); auto receipt = f.receipt(0); QVERIFY(receipt);
        f.clock->store(f.plan->deadline); QVERIFY(!f.aggregate->acceptPortable(0, std::move(receipt))); QVERIFY(!f.gate->current());
    }
    void prematureSealStaysBlockedAndWrongChildRevokes() {
        Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.aggregate"))); QVERIFY(f.prepare());
        QVERIFY(f.aggregate->trySeal(f.child()) != VLC_SUCCESS); QVERIFY(f.aggregate->negativeForbidden());
        QVERIFY(f.aggregate->reserve(0)); auto receipt = f.receipt(0); QVERIFY(receipt); QVERIFY(f.aggregate->acceptPortable(0, std::move(receipt)));
        QVERIFY(f.aggregate->trySeal(f.child()) != VLC_SUCCESS);
        Native other{keeper_fixture_new(), keeper_fixture_delete}; QVERIFY(f.aggregate->trySeal(keeper_fixture_child(other.get())) != VLC_SUCCESS); QVERIFY(!f.gate->current());
    }
    void normalSealedCleanupKeepsOnlyTheNativeKeeper() {
        Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.aggregate"))); QVERIFY(f.prepare());
        auto plan = std::weak_ptr<const Plan>(f.plan); QVERIFY(f.aggregate->reserve(0)); auto receipt = f.receipt(0); QVERIFY(receipt);
        QVERIFY(f.aggregate->acceptPortable(0, std::move(receipt))); QCOMPARE(f.aggregate->trySeal(f.child()), VLC_SUCCESS);
        f.plan.reset(); f.aggregate.reset(); QVERIFY(plan.expired()); QVERIFY(f.gate->current());
        QCOMPARE(keeper_fixture_prepare_anchor(f.native.get()), VLC_SUCCESS); QCOMPARE(keeper_fixture_apply(f.native.get()), VLC_SUCCESS);
        QVERIFY(keeper_fixture_status(f.native.get()) & ITEM_ART_NOTFOUND);
    }
    void managerRetirementAfterSealWithdrawsAuthority() {
        Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.aggregate"))); QVERIFY(f.prepare());
        QVERIFY(f.aggregate->reserve(0)); auto receipt = f.receipt(0); QVERIFY(receipt); QVERIFY(f.aggregate->acceptPortable(0, std::move(receipt)));
        QCOMPARE(f.aggregate->trySeal(f.child()), VLC_SUCCESS); f.plan.reset(); f.aggregate.reset(); QVERIFY(f.gate->current());
        f.manager.reset(); QVERIFY(!f.gate->current());
        QVERIFY(keeper_fixture_prepare_anchor(f.native.get()) != VLC_SUCCESS || keeper_fixture_apply(f.native.get()) != VLC_SUCCESS);
        QVERIFY(!(keeper_fixture_status(f.native.get()) & ITEM_ART_NOTFOUND));
    }
    void lateCompletionAfterSealRevokesTheSideband() {
        Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.aggregate"))); QVERIFY(f.prepare());
        QVERIFY(f.aggregate->reserve(0)); auto receipt = f.receipt(0); QVERIFY(receipt); QVERIFY(f.aggregate->acceptPortable(0, std::move(receipt)));
        QCOMPARE(f.aggregate->trySeal(f.child()), VLC_SUCCESS); f.aggregate->block(0, Aggregate::BlockReason::Duplicate); QVERIFY(!f.gate->current());
        QVERIFY(keeper_fixture_prepare_anchor(f.native.get()) != VLC_SUCCESS || keeper_fixture_apply(f.native.get()) != VLC_SUCCESS);
        QVERIFY(!(keeper_fixture_status(f.native.get()) & ITEM_ART_NOTFOUND));
    }
    void nativeCancellationControlOutlivesQtAggregate() {
        Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.aggregate"))); QVERIFY(f.prepare());
        auto cancel = f.aggregate->cancellationControl(); QVERIFY(cancel);
        QVERIFY(f.aggregate->reserve(0)); auto receipt = f.receipt(0); QVERIFY(receipt); QVERIFY(f.aggregate->acceptPortable(0, std::move(receipt)));
        QCOMPARE(f.aggregate->trySeal(f.child()), VLC_SUCCESS); f.plan.reset(); f.aggregate.reset();
        QVERIFY(f.gate->current()); cancel(); QVERIFY(!f.gate->current()); QVERIFY(keeper_fixture_prepare_anchor(f.native.get()) != VLC_SUCCESS || keeper_fixture_apply(f.native.get()) != VLC_SUCCESS);
    }
    void nativeControlAndKeeperCanEndAfterTheApplication() {
        QProcess process;
        process.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--native-aggregate-after-app")});
        QVERIFY(process.waitForStarted(3000)); QVERIFY(process.waitForFinished(15000));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit); QCOMPARE(process.exitCode(), 0);
        QVERIFY2(process.readAllStandardError().isEmpty(), "Native aggregate cleanup produced diagnostics");
    }
    void canceledAggregateCannotPublish() {
        Fixture f; QVERIFY(f.install(QStringLiteral("org.hakone.aggregate"))); QVERIFY(f.prepare());
        QVERIFY(f.aggregate->reserve(0)); f.aggregate->cancel(); QVERIFY(!f.gate->current()); QVERIFY(f.aggregate->negativeForbidden());
        QVERIFY(f.aggregate->trySeal(f.child()) != VLC_SUCCESS);
    }
};
int main(int argc, char** argv) {
    if (argc == 2 && !std::strcmp(argv[1], "--native-aggregate-after-app")) {
        auto app = std::make_unique<QCoreApplication>(argc, argv);
        Native native(nullptr, keeper_fixture_delete); Aggregate::CancellationControl cancel;
        {
            Fixture fixture;
            if (!fixture.install(QStringLiteral("org.hakone.aggregate")) || !fixture.prepare()) return 10;
            cancel = fixture.aggregate->cancellationControl();
            if (!fixture.aggregate->reserve(0)) return 11;
            auto receipt = fixture.receipt(0);
            if (!receipt || !fixture.aggregate->acceptPortable(0, std::move(receipt))) return 12;
            if (!fixture.sealAndApply()) return 13;
            native = std::move(fixture.native);
        }
        if (CoreMetadataAdmission::processCount() != 1) return 14;
        app.reset();
        cancel(); // A repeated native join has no Qt object or event dependency.
        native.reset(); cancel = {};
        return CoreMetadataAdmission::processCount() == 0 ? 0 : 15;
    }
    QCoreApplication app(argc, argv); AggregateTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "test_addon_artwork_attempt_aggregate.moc"
