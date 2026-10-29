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
#include "lib/libvlc_internal.h"
#include "addons/addon_artwork_attempt_aggregate.hpp"
#include "addons/addon_metadata_registry_epoch.hpp"
#include "addons/addon_manager.hpp"
#include "addons/addon_metadata_family_plan.hpp"
#include "addons/addon_metadata_coordinator.hpp"
#include <vlc_input_item.h>
#include "fixtures/addon_artwork_attempt_keeper.h"
#include "fixtures/addon_native_provider_fixture.h"
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
    Fixture() {
        registered = AddonStreamLifetime::registerApplication(lifetime.handle());
        if (!registered) qFatal("The aggregate fixture could not register its app lifetime.");
        manager = std::make_unique<AddonManager>(profile.path());
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
    bool install(const QString& id, bool rust = false) {
        const auto directory = packages.filePath(id); if (!QDir().mkpath(directory)) return false;
        const QJsonArray fields{QStringLiteral("kind"), QStringLiteral("type"), QStringLiteral("title"), QStringLiteral("artist"), QStringLiteral("ids")};
        const QJsonObject permissions{{QStringLiteral("metadata.context"), QJsonObject{{QStringLiteral("fields"), fields}}}};
        const QJsonObject contribution{{QStringLiteral("id"), QStringLiteral("artwork")}, {QStringLiteral("type"), QStringLiteral("metadata.provider")},
            {QStringLiteral("contract"), QStringLiteral("metadata@1")}, {QStringLiteral("features"), QJsonArray{QStringLiteral("artwork")}},
            {QStringLiteral("operations"), QJsonArray{QStringLiteral("enrich")}}, {QStringLiteral("requires"), QJsonArray{QStringLiteral("metadata.context")}}};
        const QJsonObject manifest{{QStringLiteral("formatVersion"), 1}, {QStringLiteral("id"), id},
            {QStringLiteral("name"), QStringLiteral("Aggregate fixture")}, {QStringLiteral("version"), QStringLiteral("1.0.0")},
            {QStringLiteral("publisher"), QStringLiteral("Tests")}, {QStringLiteral("license"), QStringLiteral("GPL-2.0-or-later")},
            {QStringLiteral("source"), QStringLiteral("https://github.com/hakone-org/aggregate-fixture")},
            {QStringLiteral("api"), QJsonObject{{QStringLiteral("addon"), QStringLiteral("^1.0.0")},
                {QStringLiteral("contracts"), QJsonObject{{QStringLiteral("metadata"), QStringLiteral("^1.0.0")}}}}},
            {QStringLiteral("runtime"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("wasm-message-v1")}, {QStringLiteral("entry"), QStringLiteral("worker.wasm")}}},
            {QStringLiteral("files"), QJsonArray{QStringLiteral("addon.json"), QStringLiteral("LICENSE"), QStringLiteral("worker.wasm")}},
            {QStringLiteral("permissions"), QJsonObject{{QStringLiteral("required"), permissions}}},
            {QStringLiteral("contributions"), QJsonArray{contribution}}};
        if (!write(QDir(directory).filePath(QStringLiteral("addon.json")), QJsonDocument(manifest).toJson())
            || !write(QDir(directory).filePath(QStringLiteral("LICENSE")), QByteArray("Fixture license.\n"))) return false;
        if (rust) {
            if (!QFile::copy(QStringLiteral(HAKONE_AGGREGATE_FRENCH_TV_WASM), QDir(directory).filePath(QStringLiteral("worker.wasm")))) return false;
            manager->setWorkerExecutable(QStringLiteral(HAKONE_AGGREGATE_RUST_WORKER)); realWorker = true;
        } else if (!write(QDir(directory).filePath(QStringLiteral("worker.wasm")), QByteArray::fromHex("0061736d01000000"))) return false;
        if (!manager->installLocalPackage(directory, true) || !manager->setEnabled(id, true)) { error = manager->errorMessage(); return false; }
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
