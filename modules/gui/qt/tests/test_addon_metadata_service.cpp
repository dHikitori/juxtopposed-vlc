// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QBuffer>
#include <QImage>
#include <QScopeGuard>
#include <QFileInfo>
#include <QJsonDocument>
#include <atomic>
#include <chrono>
#include <thread>
#include <utility>
#include <vlc_common.h>
#include <vlc_cxx_helpers.hpp>
#include <vlc_input_item.h>
#include <vlc_metadata_backend.h>
#include <vlc_artwork_policy.h>
#include "../../../../src/preparser/metadata_local.h"
#include "addons/addon_artwork_assets.hpp"
#include "addons/addon_metadata_service.hpp"
#include "addons/addon_metadata_attempt.hpp"
#include "addons/addon_native_artwork_assets.hpp"
#include "addon_native_artwork_test_owner.hpp"
#include "util/media_identity_vlc.hpp"

namespace {
class RegistrationAdmission final : public vlc::addons::AddonMetadataNativeAdmission
{
public:
    bool current() const noexcept override { return live.load(); }
    void revoke() noexcept override { live.store(false); ++revokes; }
    std::atomic_bool live{true};
    std::atomic_uint revokes{0};
};
QVariantMap identity()
{ return {{"kind", "movie"}, {"title", "Example Movie"}, {"year", 2020}, {"ids", QVariantMap{{"imdb", "tt1234567"}}}}; }
QVariantMap provider(const QString& id = QStringLiteral("example/details"))
{
    return {{"id", id}, {"packageId", id.section('/', 0, 0)}, {"name", "Example"}, {"enabled", true},
        {"configured", true}, {"features", QStringList{"video.details", "video.identity"}}, {"mediaKinds", QStringList{"movie"}},
        {"cachePartition", "release-1:account-1:settings-1"}};
}
QVariantMap claims(const QVariantMap& fields)
{ return {{"identity", identity()}, {"fields", fields}}; }
struct Fake {
    struct Call { QString id, provider, operation; QVariantMap context; };
    QVariantList providers{provider()};
    QList<Call> calls;
    QStringList canceled;
    AddonMetadataService::Backend backend()
    {
        AddonMetadataService::Backend result;
        result.providers = {
            [this](const QString&) { return providers; },
            [this](const QString& provider, const QString& operation, const QVariantMap& context) {
                const auto id = QStringLiteral("portable-%1").arg(calls.size() + 1);
                calls.append({id, provider, operation, context}); return id;
            },
            [this](const QString& id) { canceled.append(id); }};
        return result;
    }
    void complete(AddonMetadataService& service, const QVariantMap& fields)
    {
        const auto call = calls.last();
        service.operationFinished(call.id, call.provider, QStringLiteral("match"), claims(fields), {});
    }
};
struct Ended {
    std::atomic_int count{0};
    std::atomic_int status{0};
    static void callback(vlc_metadata_job_t*, int status, void* opaque)
    {
        auto* value = static_cast<Ended*>(opaque); value->status.store(status); ++value->count;
    }
};
struct DeleteOnEnd {
    AddonMetadataService* service = nullptr;
    int count = 0;
    static void callback(vlc_metadata_job_t*, int, void* opaque) {
        auto* state = static_cast<DeleteOnEnd*>(opaque);
        ++state->count;
        auto* service = state->service; state->service = nullptr; delete service;
    }
};
vlc_metadata_job_t* coreJob(vlc_metadata_backend_t* backend, Ended& ended, input_item_t* item = nullptr,
                           unsigned flags = VLC_METADATA_REQUEST_NETWORK)
{
    const char* fields[]{"description"};
    const QByteArray context = QJsonDocument::fromVariant(QVariantMap{{"identity", identity()}}).toJson(QJsonDocument::Compact);
    vlc_metadata_request request{};
    request.feature = "video.details"; request.operation = "enrich";
    request.fields = fields; request.field_count = 1;
    request.context_json = context.constData(); request.context_size = context.size();
    request.item = item; request.generation = 17; request.target_revision = 23; request.flags = flags;
    return vlc_metadata_job_New(backend, &request, Ended::callback, &ended);
}
std::shared_ptr<vlc_metadata_job_t> nativeArtworkJob(vlc_metadata_backend_t* backend, Ended& ended,
    input_item_t* item, const char* operation = "materializeNativeArtwork")
{
    const char* fields[]{"artwork"};
    vlc_metadata_request request{};
    request.feature = "artwork"; request.operation = operation; request.item = item;
    request.fields = fields; request.field_count = 1;
    request.flags = VLC_METADATA_REQUEST_NETWORK | VLC_METADATA_REQUEST_AUTOMATIC;
    return {vlc_metadata_job_New(backend, &request, Ended::callback, &ended), vlc_metadata_job_Release};
}
}

class TestAddonMetadataService : public QObject
{
    Q_OBJECT
private slots:
    void registrationCaptureRetainsEveryBuiltinAndRefusesOtherThreads()
    {
        Fake fake; AddonMetadataService service(fake.backend()); QObject lifetime;
        const auto id = QStringLiteral("hakone.builtin.capture/details");
        auto descriptor = provider(id); descriptor.insert(QStringLiteral("configured"), false);
        QVERIFY(service.registerBuiltinProvider(QStringLiteral("video.details"), descriptor, &lifetime,
            [](const QString&, const QString&, const QVariantMap&) {}));
        const auto capture = service.captureArtworkRegistrations(); QVERIFY(capture); QVERIFY(capture->token.current());
        QCOMPARE(capture->builtins.size(), 1);
        QCOMPARE(capture->builtins.first().descriptor.value(QStringLiteral("id")).toString(), id);
        QVERIFY(!capture->builtins.first().descriptor.value(QStringLiteral("configured")).toBool());
        QVERIFY(capture->builtins.first().available); QVERIFY(!capture->builtins.first().binding.isEmpty());
        bool refused = false;
        std::thread worker([&] { refused = !service.captureArtworkRegistrations(); }); worker.join();
        QVERIFY(refused); QVERIFY(capture->token.current());
    }
    void registrationBarrierPrecedesPublication_data()
    {
        QTest::addColumn<int>("action");
        const char* names[]{"replace", "remove", "lifetime", "set-plan", "clear-plan", "invalidate",
            "artwork-adapter", "network-policy", "shutdown", "destruction"};
        for (int i = 0; i != 10; ++i) QTest::newRow(names[i]) << i;
    }
    void registrationBarrierPrecedesPublication()
    {
        QFETCH(int, action);
        Fake fake; auto lifetime = std::make_unique<QObject>();
        vlc::addons::AddonStreamLifetime application;
        auto gate = std::make_shared<RegistrationAdmission>();
        vlc::addons::MetadataRegistryToken token;
        AddonMetadataService* owner = nullptr; bool armed = false; int callbacks = 0;
        auto backend = fake.backend();
        const auto published = [&] {
            if (!armed) return;
            ++callbacks; QVERIFY(!token.current()); QVERIFY(!gate->current());
            QVERIFY(!owner->captureArtworkRegistrations());
        };
        backend.registerBuiltin = [&](const QString&, const QVariantMap&) { published(); };
        backend.unregisterBuiltin = [&](const QString&) { published(); };
        auto service = std::make_unique<AddonMetadataService>(std::move(backend)); owner = service.get();
        QVERIFY(service->bindApplicationLifetime(application.handle())); QVERIFY(service->reserveNativeOwner(lifetime.get()));
        const auto id = QStringLiteral("hakone.builtin.barrier/details");
        QVERIFY(service->registerBuiltinProvider(QStringLiteral("video.details"), provider(id), lifetime.get(),
            [](const QString&, const QString&, const QVariantMap&) {}));
        if (action == 4) service->setProviderPlan(lifetime.get(),
            [](const QString&, const QString&, const QVariantList& rows, bool) { return rows; });
        const auto capture = service->captureArtworkRegistrations(); QVERIFY(capture);
        token = capture->token; QVERIFY(capture->registry->arm(token, gate)); armed = true;
        switch (action) {
        case 0: QVERIFY(service->registerBuiltinProvider(QStringLiteral("video.details"), provider(id), lifetime.get(),
            [](const QString&, const QString&, const QVariantMap&) {})); break;
        case 1: service->unregisterBuiltinProvider(id); break;
        case 2: lifetime.reset(); break;
        case 3: service->setProviderPlan(lifetime.get(),
            [](const QString&, const QString&, const QVariantList& rows, bool) { return rows; }); break;
        case 4: service->clearProviderPlan(lifetime.get()); break;
        case 5: service->invalidate(); break;
        case 6: service->setArtworkAdapter({}, {}); break;
        case 7: service->setAutomaticNetworkAllowed(true); break;
        case 8: service->shutdown(); break;
        case 9: service.reset(); break;
        }
        QVERIFY(!token.current()); QVERIFY(!gate->current()); QVERIFY(gate->revokes.load() > 0);
        if (action <= 2 || action >= 8) QVERIFY(callbacks > 0);
        if (service && action < 8) {
            const auto fresh = service->captureArtworkRegistrations(); QVERIFY(fresh); QVERIFY(fresh->token.current());
            QVERIFY(fresh->token.identity() != token.identity());
        }
        armed = false;
    }
    void privateCaptureRefusesForeignAndExpiredPlanOwners()
    {
        Fake fake; AddonMetadataService service(fake.backend());
        vlc::addons::AddonStreamLifetime application; auto owner = std::make_unique<QObject>(); QObject foreign;
        QVERIFY(service.bindApplicationLifetime(application.handle())); QVERIFY(service.reserveNativeOwner(owner.get()));
        const auto plan = [](const QString&, const QString&, const QVariantList& rows, bool) { return rows; };
        service.setProviderPlan(&foreign, plan); QVERIFY(!service.captureArtworkRegistrations());
        service.setProviderPlan(owner.get(), plan); QVERIFY(service.captureArtworkRegistrations());
        owner.reset(); QVERIFY(!service.captureArtworkRegistrations());
    }
    void unownedNativeBindingsRemainInTheRawCapture()
    {
        Fake fake; AddonMetadataService service(fake.backend()); QObject owner;
        auto descriptor = provider(QStringLiteral("hakone.builtin.unowned/artwork"));
        descriptor.insert(QStringLiteral("requiresNativeAnchor"), true);
        QVERIFY(service.registerBuiltinProvider(QStringLiteral("artwork"), descriptor, &owner,
            [](const QString&, const QString&, const QVariantMap&) {}));
        const auto capture = service.captureArtworkRegistrations(); QVERIFY(capture); QCOMPARE(capture->builtins.size(), 1);
        QVERIFY(!capture->builtins.first().available); QVERIFY(!capture->builtins.first().binding.isEmpty());
        vlc::addons::AddonStreamLifetime application;
        QVERIFY(service.bindApplicationLifetime(application.handle())); QVERIFY(service.reserveNativeOwner(&owner));
        const auto owned = service.captureArtworkRegistrations(); QVERIFY(owned); QVERIFY(owned->builtins.first().available);
        QVERIFY(!capture->token.current());
    }
    void nativeOwnerRetirementKeepsRegistrationCaptureClosed()
    {
        Fake fake; AddonMetadataService service(fake.backend()); QObject owner, other;
        vlc::addons::AddonStreamLifetime application;
        QVERIFY(service.bindApplicationLifetime(application.handle())); QVERIFY(service.reserveNativeOwner(&owner));
        const auto capture = service.captureArtworkRegistrations(); QVERIFY(capture);
        auto gate = std::make_shared<RegistrationAdmission>(); QVERIFY(capture->registry->arm(capture->token, gate));
        QVERIFY(!service.beginNativeOwnerRetirement(&other)); QVERIFY(capture->token.current());
        {
            const auto retirement = service.beginNativeOwnerRetirement(&owner); QVERIFY(retirement);
            QVERIFY(!gate->current()); QVERIFY(!capture->token.current()); QVERIFY(!service.captureArtworkRegistrations());
            service.invalidate(); QVERIFY(!service.captureArtworkRegistrations());
            service.releaseNativeOwnerFor(&owner); QVERIFY(!service.captureArtworkRegistrations());
        }
        QVERIFY(service.captureArtworkRegistrations());
        service.shutdown(); QVERIFY(!service.captureArtworkRegistrations());
        QVERIFY(!capture->registry->capture().current());
    }
    void identityTargetsRequireNativeNetworkAuthority_data()
    {
        QTest::addColumn<unsigned>("flags"); QTest::addColumn<bool>("consent");
        QTest::newRow("manual-local") << 0u << true;
        QTest::newRow("automatic-local") << unsigned(VLC_METADATA_REQUEST_AUTOMATIC) << true;
        QTest::newRow("automatic-without-consent") << unsigned(VLC_METADATA_REQUEST_NETWORK | VLC_METADATA_REQUEST_AUTOMATIC) << false;
    }
    void identityTargetsRequireNativeNetworkAuthority()
    {
        QFETCH(unsigned, flags); QFETCH(bool, consent); Fake fake; AddonMetadataService service(fake.backend());
        service.setAutomaticNetworkAllowed(consent);
        auto* backend = service.holdCoreBackend(); auto* item = input_item_New("file:///private/Example.Movie.2020.mkv", "Original"); Ended ended;
        auto* job = coreJob(backend, ended, item, flags);
        const auto cleanup = qScopeGuard([&] { vlc_metadata_job_Release(job); input_item_Release(item); vlc_metadata_backend_Release(backend); });
        QCOMPARE(vlc_metadata_job_StartLocal(job), VLC_SUCCESS);
        QVERIFY(service.bindIdentityTarget(job, item, {{"manual", true}, {"query", "A manual work query"}}).isEmpty());
        QCOMPARE(service.identityTargetCount(), 0); QCOMPARE(fake.calls.size(), 0);
        QVERIFY(vlc_metadata_job_Cancel(job)); QCOMPARE(ended.count.load(), 1);
    }

    void nativeIdentityOffersRequireSelectionAndNativeCommit()
    {
        Fake fake; auto registration = provider(); registration.insert("operations", QStringList{"identify", "enrich"});
        fake.providers = {registration}; AddonMetadataService service(fake.backend());
        auto* backend = service.holdCoreBackend(); auto* item = input_item_New("file:///private/Example.Movie.2020.mkv", "Original");
        Ended ended; auto* job = coreJob(backend, ended, item);
        const auto cleanup = qScopeGuard([&] { vlc_metadata_job_Release(job); input_item_Release(item); vlc_metadata_backend_Release(backend); });
        QVERIFY(job); QCOMPARE(vlc_metadata_job_StartLocal(job), VLC_SUCCESS);
        const auto target = service.bindIdentityTarget(job, item, {{"manual", true}, {"query", "Example Movie"}});
        QVERIFY(!target.isEmpty()); QSignalSpy results(&service, &AddonMetadataService::finished);
        const auto consumer = service.identify("video.identity", target); QVERIFY(!consumer.isEmpty());
        QTRY_COMPARE(fake.calls.size(), 1); const auto call = fake.calls.first(); QCOMPARE(call.operation, "identify");
        QVERIFY(!QJsonDocument::fromVariant(call.context).toJson().contains(target.toUtf8()));
        QVERIFY(!call.context.contains("media"));
        const QVariantList scopedIds{QVariantMap{{"namespace", "example.catalog"}, {"scope", "edition"}, {"value", "281"}},
            QVariantMap{{"namespace", "tmdb"}, {"scope", "season"}, {"value", "281"}}};
        auto identified = identity(); identified.insert("externalIds", scopedIds);
        service.operationFinished(call.id, call.provider, "match", {{"candidates", QVariantList{QVariantMap{{"identity", identified}}}}}, {});
        QTRY_COMPARE(results.size(), 1); const auto offered = results.first()[2].toMap();
        QVERIFY(offered.value("requiresSelection").toBool()); QVERIFY(!offered.contains("fields"));
        const auto row = offered.value("candidates").toList().first().toMap();
        QVERIFY(!row.contains("id")); QVERIFY(!row.contains("nativeSubject"));
        const auto selection = service.acceptIdentity(target, row.value("reference").toString()); QVERIFY(!selection.isEmpty());
        QVERIFY(!input_item_GetDescription(item)); QCOMPARE(fake.calls.size(), 1);
        QCOMPARE(service.selectedIdentity(target, selection).value("identity").toMap().value("title").toString(), "Example Movie");
        service.enrichSelected("video.details", target, selection, {"description"}); QTRY_COMPARE(fake.calls.size(), 2);
        const auto projectedIds = fake.calls.last().context.value("identity").toMap().value("externalIds").toList();
        for (const auto& id : scopedIds) QVERIFY(projectedIds.contains(id));
        fake.complete(service, {{"description", "Selected description"}}); QTRY_COMPARE(results.size(), 2);
        QVERIFY(!input_item_GetDescription(item));
        auto* held = service.holdIdentityAnchor(target, selection); QCOMPARE(held, job); vlc_metadata_job_Release(held);
        auto* copy = vlc_metadata_job_CopyLocalItem(job); QVERIFY(copy);
        input_item_SetDescription(copy, "Selected description");
        QCOMPARE(vlc_metadata_job_PrepareLocal(job, copy, nullptr), VLC_SUCCESS);
        QVERIFY(vlc_metadata_job_Complete(job, VLC_SUCCESS, copy, nullptr, 0)); input_item_Release(copy);
        QCOMPARE(vlc_metadata_job_Apply(job, item, 0, nullptr), VLC_SUCCESS);
        char* description = input_item_GetDescription(item); QCOMPARE(QString::fromUtf8(description), "Selected description"); free(description);
        QVERIFY(service.selectedIdentity(target, selection).isEmpty());
        service.releaseIdentityTarget(target); QCOMPARE(service.identityTargetCount(), 0);
    }
    void identitySelectionsBindExactTargetsAndProviderPartitions()
    {
        Fake fake; auto registration = provider(); registration.insert("operations", QStringList{"identify"}); fake.providers = {registration};
        AddonMetadataService service(fake.backend()); auto* backend = service.holdCoreBackend();
        auto* item = input_item_New("file:///private/Example.Movie.2020.mkv", "Original"); Ended firstEnd, secondEnd;
        auto* first = coreJob(backend, firstEnd, item); auto* second = coreJob(backend, secondEnd, item);
        const auto cleanup = qScopeGuard([&] { vlc_metadata_job_Release(first); vlc_metadata_job_Release(second); input_item_Release(item); vlc_metadata_backend_Release(backend); });
        QCOMPARE(vlc_metadata_job_StartLocal(first), VLC_SUCCESS); QCOMPARE(vlc_metadata_job_StartLocal(second), VLC_SUCCESS);
        const auto target = service.bindIdentityTarget(first, item), other = service.bindIdentityTarget(second, item);
        QVERIFY(!target.isEmpty()); QVERIFY(target != other);
        QSignalSpy results(&service, &AddonMetadataService::finished); service.identify("video.identity", target);
        QTRY_COMPARE(fake.calls.size(), 1); const auto call = fake.calls.first();
        service.operationFinished(call.id, call.provider, "ambiguous", {{"candidates", QVariantList{QVariantMap{{"identity", identity()}}}}}, {});
        QTRY_COMPARE(results.size(), 1); const auto reference = results.first()[2].toMap().value("candidates").toList().first().toMap().value("reference").toString();
        QVERIFY(service.acceptIdentity(other, reference).isEmpty());
        const auto accepted = service.acceptIdentity(target, reference); QVERIFY(!accepted.isEmpty());
        registration.insert("cachePartition", "release-2:account-2:settings-2"); fake.providers = {registration};
        QVERIFY(service.acceptIdentity(target, reference).isEmpty()); QVERIFY(service.selectedIdentity(target, accepted).isEmpty());
        QVERIFY(!service.holdIdentityAnchor(target, accepted));
        service.invalidate(); QCOMPARE(service.identityTargetCount(), 0);
        QCOMPARE(firstEnd.count.load(), 1); QCOMPARE(secondEnd.count.load(), 1);
    }
    void nativeIdentityRejectsUriAbaAndCancelsLateDrafts()
    {
        Fake fake; auto registration = provider(); registration.insert("operations", QStringList{"identify", "enrich"}); fake.providers = {registration};
        AddonMetadataService service(fake.backend()); auto* backend = service.holdCoreBackend();
        auto* item = input_item_New("file:///private/Example.Movie.2020.mkv", "Original"); Ended ended; auto* job = coreJob(backend, ended, item);
        const auto cleanup = qScopeGuard([&] { vlc_metadata_job_Release(job); input_item_Release(item); vlc_metadata_backend_Release(backend); });
        QCOMPARE(vlc_metadata_job_StartLocal(job), VLC_SUCCESS); const auto target = service.bindIdentityTarget(job, item);
        QSignalSpy results(&service, &AddonMetadataService::finished), canceled(&service, &AddonMetadataService::canceled);
        service.identify("video.identity", target); QTRY_COMPARE(fake.calls.size(), 1); const auto call = fake.calls.first();
        service.operationFinished(call.id, call.provider, "match", {{"candidates", QVariantList{QVariantMap{{"identity", identity()}}}}}, {});
        QTRY_COMPARE(results.size(), 1); const auto reference = results.first()[2].toMap().value("candidates").toList().first().toMap().value("reference").toString();
        const auto accepted = service.acceptIdentity(target, reference); QVERIFY(!accepted.isEmpty());
        service.enrichSelected("video.details", target, accepted, {"description"}); QTRY_COMPARE(fake.calls.size(), 2);
        input_item_SetURI(item, "file:///private/other.mkv"); input_item_SetURI(item, "file:///private/Example.Movie.2020.mkv");
        QVERIFY(service.selectedIdentity(target, accepted).isEmpty()); QVERIFY(!service.holdIdentityAnchor(target, accepted));
        fake.complete(service, {{"description", "Late text"}}); QTRY_COMPARE(canceled.size(), 1);
        QCOMPARE(results.size(), 1); QVERIFY(!input_item_GetDescription(item)); service.releaseIdentityTarget(target);
    }
    void nativeIdentityTargetReleaseCancelsBeforeReentrantServiceDeletion()
    {
        Fake fake; AddonMetadataService* service = new AddonMetadataService(fake.backend());
        const auto cleanupService = qScopeGuard([&] { delete std::exchange(service, nullptr); });
        auto* backend = service->holdCoreBackend(); auto* item = input_item_New("file:///private/Example.Movie.2020.mkv", "Original");
        Ended secondEnd;
        const char* fields[]{"description"}; vlc_metadata_request definition{}; definition.feature = "video.details";
        definition.operation = "enrich"; definition.flags = VLC_METADATA_REQUEST_NETWORK; definition.item = item; definition.fields = fields; definition.field_count = 1;
        auto* first = vlc_metadata_job_New(backend, &definition, [](vlc_metadata_job_t*, int, void* data) {
            auto** value = static_cast<AddonMetadataService**>(data); delete std::exchange(*value, nullptr);
        }, &service);
        auto* second = coreJob(backend, secondEnd, item);
        const auto cleanup = qScopeGuard([&] { vlc_metadata_job_Release(first); vlc_metadata_job_Release(second); input_item_Release(item); vlc_metadata_backend_Release(backend); });
        QCOMPARE(vlc_metadata_job_StartLocal(first), VLC_SUCCESS); QCOMPARE(vlc_metadata_job_StartLocal(second), VLC_SUCCESS);
        QVERIFY(!service->bindIdentityTarget(first, item).isEmpty()); QVERIFY(!service->bindIdentityTarget(second, item).isEmpty());
        service->invalidate(); QVERIFY(!service); QCOMPARE(secondEnd.count.load(), 1);
    }

    void identityTargetsHaveASeparateBoundAndCloseReleasesEveryHold()
    {
        Fake fake; AddonMetadataService service(fake.backend()); auto* backend = service.holdCoreBackend();
        auto* item = input_item_New("file:///private/Example.Movie.2020.mkv", "Original"); Ended ended;
        auto* anchor = coreJob(backend, ended, item);
        const auto cleanup = qScopeGuard([&] { vlc_metadata_job_Release(anchor); input_item_Release(item); vlc_metadata_backend_Release(backend); });
        QCOMPARE(vlc_metadata_job_StartLocal(anchor), VLC_SUCCESS);
        for (int i = 0; i < 64; ++i) QVERIFY(!service.bindIdentityTarget(anchor, item).isEmpty());
        QCOMPARE(service.identityTargetCount(), 64); QVERIFY(service.bindIdentityTarget(anchor, item).isEmpty());
        service.shutdown(); QCOMPARE(service.identityTargetCount(), 0); QCOMPARE(service.mediaCount(), 0);
        QCOMPARE(ended.count.load(), 1); QVERIFY(!vlc_metadata_job_IsCurrentFor(anchor, item));
    }
    void identityAdmissionCannotSurviveAProviderQueryThatInvalidatesTheTarget()
    {
        Fake fake; AddonMetadataService* owner = nullptr; bool armed = false;
        auto backendCallbacks = fake.backend();
        backendCallbacks.providers.providersForFeature = [&](const QString&) {
            if (armed) owner->invalidate(); return fake.providers;
        };
        AddonMetadataService service(std::move(backendCallbacks)); owner = &service;
        auto* backend = service.holdCoreBackend(); auto* item = input_item_New("file:///private/Example.Movie.2020.mkv", "Original"); Ended ended;
        auto* anchor = coreJob(backend, ended, item);
        const auto cleanup = qScopeGuard([&] { vlc_metadata_job_Release(anchor); input_item_Release(item); vlc_metadata_backend_Release(backend); });
        QCOMPARE(vlc_metadata_job_StartLocal(anchor), VLC_SUCCESS); const auto target = service.bindIdentityTarget(anchor, item);
        armed = true; service.identify("video.identity", target); QCoreApplication::processEvents();
        QCOMPARE(service.identityTargetCount(), 0); QCOMPARE(service.mediaCount(), 0); QCOMPARE(fake.calls.size(), 0);
        QCOMPARE(ended.count.load(), 1);
    }

    void anIdentityProviderQueryCanDeleteTheService()
    {
        Fake fake; AddonMetadataService* service = nullptr; bool armed = false; auto callbacks = fake.backend();
        callbacks.providers.providersForFeature = [&](const QString&) {
            if (armed) delete std::exchange(service, nullptr); return fake.providers;
        };
        service = new AddonMetadataService(std::move(callbacks));
        const auto cleanupService = qScopeGuard([&] { delete std::exchange(service, nullptr); });
        auto* backend = service->holdCoreBackend(); auto* item = input_item_New("file:///private/Example.Movie.2020.mkv", "Original"); Ended ended;
        auto* anchor = coreJob(backend, ended, item);
        const auto cleanup = qScopeGuard([&] { vlc_metadata_job_Release(anchor); input_item_Release(item); vlc_metadata_backend_Release(backend); });
        QCOMPARE(vlc_metadata_job_StartLocal(anchor), VLC_SUCCESS); const auto target = service->bindIdentityTarget(anchor, item);
        armed = true; service->identify("video.identity", target); QVERIFY(!service); QCOMPARE(ended.count.load(), 1);
    }

    void outstandingIdentityWorkEndsWhenAnAnchorDeletesTheService_data()
    {
        QTest::addColumn<bool>("native"); QTest::newRow("portable") << false; QTest::newRow("native") << true;
    }
    void outstandingIdentityWorkEndsWhenAnAnchorDeletesTheService()
    {
        QFETCH(bool, native); Fake fake; QObject lifetime; QString nativeInvocation; int nativeCanceled = 0;
        auto registration = provider(native ? QStringLiteral("hakone.builtin.identity/search") : QStringLiteral("portable/identity"));
        registration.insert("operations", QStringList{"identify"});
        fake.providers = native ? QVariantList{} : QVariantList{registration};
        AddonMetadataService* service = new AddonMetadataService(fake.backend());
        const auto cleanupService = qScopeGuard([&] { delete std::exchange(service, nullptr); });
        if (native) QVERIFY(service->registerBuiltinProvider("video.identity", registration, &lifetime,
            [&](const QString& id, const QString&, const QVariantMap&) { nativeInvocation = id; },
            [&](const QString& id) { QCOMPARE(id, nativeInvocation); ++nativeCanceled; }));
        auto* backend = service->holdCoreBackend(); auto* item = input_item_New("file:///private/Example.Movie.2020.mkv", "Original");
        const char* fields[]{"description"}; vlc_metadata_request request{}; request.feature = "video.details";
        request.operation = "enrich"; request.flags = VLC_METADATA_REQUEST_NETWORK; request.item = item; request.fields = fields; request.field_count = 1;
        auto* anchor = vlc_metadata_job_New(backend, &request, [](vlc_metadata_job_t*, int, void* data) {
            auto** owner = static_cast<AddonMetadataService**>(data); delete std::exchange(*owner, nullptr);
        }, &service);
        const auto cleanup = qScopeGuard([&] { vlc_metadata_job_Release(anchor); input_item_Release(item); vlc_metadata_backend_Release(backend); });
        QCOMPARE(vlc_metadata_job_StartLocal(anchor), VLC_SUCCESS);
        const auto target = service->bindIdentityTarget(anchor, item); QVERIFY(!target.isEmpty());
        service->identify("video.identity", target);
        if (native) QTRY_VERIFY(!nativeInvocation.isEmpty()); else QTRY_COMPARE(fake.calls.size(), 1);
        service->invalidate(); QVERIFY(!service);
        if (native) { QCOMPARE(nativeCanceled, 1); QVERIFY(fake.canceled.isEmpty()); }
        else QCOMPARE(fake.canceled, QStringList{fake.calls.first().id});
    }

    void sourceIdentityProjectionUsesDisplayTagsOnly_data()
    {
        QTest::addColumn<QString>("scheme"); QTest::addColumn<bool>("display");
        QTest::newRow("hakone-empty") << QString("hakone-addon-source") << false;
        QTest::newRow("hakone-display") << QString("hakone-addon-source") << true;
        QTest::newRow("legacy-empty") << QString("VLC-ADDON-SOURCE") << false;
        QTest::newRow("legacy-display") << QString("VLC-ADDON-SOURCE") << true;
    }
    void sourceIdentityProjectionUsesDisplayTagsOnly()
    {
        QFETCH(QString, scheme); QFETCH(bool, display); Fake fake;
        auto registration = provider(); registration.insert("operations", QStringList{"enrich", "identify"});
        registration.insert("mediaKinds", QStringList{}); fake.providers = {registration};
        AddonMetadataService service(fake.backend()); auto* backend = service.holdCoreBackend();
        const QString address = scheme + QStringLiteral(":PRIVATE_LOCATOR_ONLY_987654321");
        auto* item = input_item_New(address.toUtf8().constData(), "Source");
        if (display) input_item_SetTitle(item, "Admitted display title");
        Ended remoteEnd, anchorEnd;
        const auto inferred = MediaIdentity::toMap(MediaIdentity::parse(QUrl(address).fileName()));
        const auto privateContext = QJsonDocument::fromVariant(QVariantMap{{"identity", inferred}}).toJson(QJsonDocument::Compact);
        const char* fields[]{"description"}; vlc_metadata_request request{};
        request.feature = "video.details"; request.operation = "enrich"; request.item = item;
        request.fields = fields; request.field_count = 1; request.flags = VLC_METADATA_REQUEST_NETWORK;
        request.context_json = privateContext.constData(); request.context_size = privateContext.size();
        auto* remote = vlc_metadata_job_New(backend, &request, Ended::callback, &remoteEnd);
        auto* anchor = coreJob(backend, anchorEnd, item);
        const auto cleanup = qScopeGuard([&] { vlc_metadata_job_Release(remote); vlc_metadata_job_Release(anchor); input_item_Release(item); vlc_metadata_backend_Release(backend); });
        QVERIFY(remote); QVERIFY(anchor); QCOMPARE(vlc_metadata_job_Start(remote), VLC_SUCCESS);
        QTRY_COMPARE(fake.calls.size(), 1);
        const auto checkProjection = [&](const QVariantMap& context) {
            const auto identity = context.value("identity").toMap();
            QCOMPARE(identity.value("title").toString(), display ? QString("Admitted display title") : QString{});
            const auto serialized = QJsonDocument::fromVariant(context).toJson(QJsonDocument::Compact);
            QVERIFY(!serialized.contains("PRIVATE_LOCATOR_ONLY")); QVERIFY(!serialized.contains("native-subject:"));
            QVERIFY(identity.value("release").toMap().isEmpty());
        };
        checkProjection(fake.calls.first().context);
        service.operationFinished(fake.calls.first().id, fake.calls.first().provider, "no_match", {}, {});
        QTRY_COMPARE(remoteEnd.count.load(), 1);
        QCOMPARE(vlc_metadata_job_StartLocal(anchor), VLC_SUCCESS);
        const auto target = service.bindIdentityTarget(anchor, item, {{"manual", true}, {"query", "A work title"}, {"identity", inferred}});
        QVERIFY(!target.isEmpty()); service.identify("video.identity", target); QTRY_COMPARE(fake.calls.size(), 2);
        checkProjection(fake.calls.last().context);
        service.releaseIdentityTarget(target); QCOMPARE(anchorEnd.count.load(), 1);
    }

    void sourceContextCanUseAnExplicitPublicIdentity()
    {
        Fake fake; AddonMetadataService service(fake.backend()); auto* backend = service.holdCoreBackend();
        auto* item = input_item_New("hakone-addon-source:PRIVATE_LOCATOR_ONLY", "Source"); Ended ended;
        auto* job = coreJob(backend, ended, item);
        const auto cleanup = qScopeGuard([&] { vlc_metadata_job_Release(job); input_item_Release(item); vlc_metadata_backend_Release(backend); });
        QCOMPARE(vlc_metadata_job_Start(job), VLC_SUCCESS); QTRY_COMPARE(fake.calls.size(), 1);
        QCOMPARE(fake.calls.first().context.value("identity").toMap().value("title").toString(), "Example Movie");
        QVERIFY(!QJsonDocument::fromVariant(fake.calls.first().context).toJson().contains("PRIVATE_LOCATOR_ONLY"));
        fake.complete(service, {{"description", "A public claim"}}); QTRY_COMPARE(ended.count.load(), 1);
    }

    void selectiveIdentityRetirementDrainsAnExternalAnchorAfterOwnerDeletion()
    {
        Fake fake; AddonMetadataService* service = new AddonMetadataService(fake.backend());
        const auto cleanupService = qScopeGuard([&] { delete std::exchange(service, nullptr); });
        auto* ownBackend = service->holdCoreBackend();
        // StartLocal has no dispatched work. The native backend still runs its
        // real cancellation and terminal callback paths.
        const vlc_metadata_backend_operations operations{
            [](void*, vlc_metadata_job_t*) { return VLC_ENOTSUP; },
            [](void*, vlc_metadata_job_t*) {}, nullptr};
        auto* external = vlc_metadata_backend_New(&operations, nullptr); QVERIFY(external);
        auto* item = input_item_New("file:///private/Example.Movie.2020.mkv", "Original"); Ended externalEnd;
        const char* fields[]{"description"}; vlc_metadata_request request{};
        request.feature = "video.details"; request.operation = "enrich"; request.item = item;
        request.fields = fields; request.field_count = 1; request.flags = VLC_METADATA_REQUEST_NETWORK;
        auto* first = vlc_metadata_job_New(ownBackend, &request, [](vlc_metadata_job_t*, int, void* data) {
            auto** owner = static_cast<AddonMetadataService**>(data); delete std::exchange(*owner, nullptr);
        }, &service);
        auto* second = coreJob(external, externalEnd, item);
        const auto cleanup = qScopeGuard([&] {
            vlc_metadata_job_Release(first); vlc_metadata_job_Release(second);
            vlc_metadata_backend_Close(external); vlc_metadata_backend_Release(external);
            input_item_Release(item); vlc_metadata_backend_Release(ownBackend);
        });
        QVERIFY(first); QVERIFY(second); QCOMPARE(vlc_metadata_job_StartLocal(first), VLC_SUCCESS);
        QCOMPARE(vlc_metadata_job_StartLocal(second), VLC_SUCCESS);
        const auto target = service->bindIdentityTarget(first, item); QVERIFY(!target.isEmpty());
        QVERIFY(!service->bindIdentityTarget(second, item).isEmpty());
        service->releaseIdentityTarget(target); QVERIFY(!service);
        QCOMPARE(externalEnd.count.load(), 1); QVERIFY(!vlc_metadata_job_IsPending(second));
        QVERIFY(!vlc_metadata_job_IsCurrentFor(second, item));
        QVERIFY(!vlc_metadata_backend_IsClosed(external));
    }

    void providerInvalidationRevokesACompletedAutomaticAnchor()
    {
        Fake fake; AddonMetadataService service(fake.backend()); service.setAutomaticNetworkAllowed(true);
        auto* backend = service.holdCoreBackend(); auto* item = input_item_New("file:///private/item.mkv", "Item");
        const auto cleanup = qScopeGuard([&] { vlc_metadata_backend_Release(backend); input_item_Release(item); });
        Ended ended; auto* job = coreJob(backend, ended, item, VLC_METADATA_REQUEST_NETWORK | VLC_METADATA_REQUEST_AUTOMATIC);
        QVERIFY(job); const auto release = qScopeGuard([&] { vlc_metadata_job_Release(job); });
        QCOMPARE(vlc_metadata_job_StartLocal(job), VLC_SUCCESS);
        auto* copy = vlc_metadata_job_CopyLocalItem(job); QVERIFY(copy);
        const auto releaseCopy = qScopeGuard([&] { input_item_Release(copy); });
        input_item_SetDescription(copy, "Provider text"); QCOMPARE(vlc_metadata_job_PrepareLocal(job, copy, nullptr), VLC_SUCCESS);
        QVERIFY(vlc_metadata_job_Complete(job, VLC_SUCCESS, copy, nullptr, 0)); QCOMPARE(ended.count.load(), 1);
        QVERIFY(vlc_metadata_job_IsCurrentFor(job, item)); service.invalidate();
        QVERIFY(!vlc_metadata_job_IsCurrentFor(job, item)); QVERIFY(vlc_metadata_job_Apply(job, item, 0, nullptr) != VLC_SUCCESS);
        QVERIFY(!input_item_GetDescription(item)); QCOMPARE(ended.count.load(), 1);
    }

    void anArtworkResolverCanDeleteTheOwnerBeforeItReturnsItsRequest()
    {
        Fake fake; AddonMetadataService* service = nullptr; auto backend = fake.backend(); QStringList canceled;
        backend.resolveArtwork = [&](const QString&, const QVariant&, AddonMetadataService::ArtworkCallback) {
            delete std::exchange(service, nullptr); return QStringLiteral("returned-artwork");
        };
        backend.cancelArtwork = [&](const QString& id) { canceled.append(id); };
        service = new AddonMetadataService(std::move(backend));
        const auto cleanup = qScopeGuard([&] { delete std::exchange(service, nullptr); });
        service->enrich("video.details", {{"identity", identity()}}, {"artwork"});
        QTRY_COMPARE(fake.calls.size(), 1); fake.complete(*service, {{"artwork", "https://image.invalid/art.png"}});
        QTRY_VERIFY(!service); QCOMPARE(canceled, QStringList{QStringLiteral("returned-artwork")});
    }

    void anOldBuiltinLifetimeCannotRemoveItsReplacement()
    {
        Fake fake; fake.providers.clear(); AddonMetadataService service(fake.backend());
        auto first = std::make_unique<QObject>(); auto second = std::make_unique<QObject>();
        const QString id = QStringLiteral("hakone.builtin.binding/details"); int oldCalls = 0, newCalls = 0;
        QVERIFY(service.registerBuiltinProvider("video.details", provider(id), first.get(),
            [&](const QString&, const QString&, const QVariantMap&) { ++oldCalls; }));
        service.unregisterBuiltinProviderForOwner(id, first.get());
        QVERIFY(service.registerBuiltinProvider("video.details", provider(id), second.get(),
            [&](const QString& call, const QString&, const QVariantMap&) { ++newCalls; service.completeBuiltin(call, "match", claims({{"description", "New provider"}})); }));
        service.unregisterBuiltinProviderForOwner(id, first.get()); first.reset();
        QSignalSpy results(&service, &AddonMetadataService::finished);
        service.enrich("video.details", {{"identity", identity()}}, {"description"}); QTRY_COMPARE(results.size(), 1);
        QCOMPARE(oldCalls, 0); QCOMPARE(newCalls, 1); QCOMPARE(results[0][2].toMap().value("fields").toMap().value("description").toString(), "New provider");
        second.reset(); service.enrich("video.details", {{"identity", identity()}}, {"description"}); QTRY_COMPARE(results.size(), 2);
        QCOMPARE(results[1][1].toString(), QStringLiteral("no_match")); QCOMPARE(newCalls, 1);
    }

    void aNativeArtworkResolverCanDeleteTheOwnerBeforeItReturnsItsControl()
    {
        Fake fake; fake.providers.clear(); AddonMetadataService* service = new AddonMetadataService(fake.backend());
        const auto cleanup = qScopeGuard([&] { delete std::exchange(service, nullptr); });
        service->setAutomaticNetworkAllowed(true); QObject lifetime; unsigned canceled = 0;
        auto descriptor = provider("hakone.builtin.delete/artwork");
        descriptor.insert("features", QStringList{"artwork"}); descriptor.insert("requiresNativeAnchor", true);
        descriptor.remove("mediaKinds");
        AddonMetadataService::NativeArtworkHandlers artwork;
        artwork.validate = [](const QVariantMap&, vlc_metadata_job_t*) -> std::optional<AddonMetadataCoordinator::NativeArtwork> {
            return AddonMetadataCoordinator::NativeArtwork{QVariantMap{{"nativeClaim", "test-claim"}}, "musicbrainz", {}};
        };
        artwork.resolve = [&](const QVariant&, vlc_metadata_job_t*, AddonMetadataService::ArtworkCallback) {
            delete std::exchange(service, nullptr);
            return AddonMetadataService::NativeArtworkRequest{"returned-native-artwork", [&] { ++canceled; }};
        };
        QVERIFY(service->registerBuiltinProvider("artwork", descriptor, &lifetime,
            [&](const QString& id, const QString&, const QVariantMap&) { service->completeBuiltin(id, "match", {}); },
            {}, std::move(artwork)));
        const auto backend = std::shared_ptr<vlc_metadata_backend_t>(service->holdCoreBackend(), vlc_metadata_backend_Release);
        const auto item = std::shared_ptr<input_item_t>(input_item_New("file:///private/song.flac", "Song"), input_item_Release);
        Ended ended; const auto job = nativeArtworkJob(backend.get(), ended, item.get(), "enrich"); QVERIFY(job);
        QCOMPARE(vlc_metadata_job_Start(job.get()), VLC_SUCCESS);
        QTRY_VERIFY(!service); QCOMPARE(canceled, 1u); QCOMPARE(ended.count.load(), 1);
        QCOMPARE(ended.status.load(), -EINTR); QVERIFY(!input_item_GetArtURL(item.get()));
    }

    void aPortableInvokeCanDeleteTheServiceWithoutAnOrphan()
    {
        Fake fake; AddonMetadataService* service = nullptr; auto backend = fake.backend();
        auto token = std::make_shared<int>(1); const std::weak_ptr<int> weak = token;
        bool aliveDuringReturn = false;
        backend.providers.invoke = [&, held=token](const QString& provider, const QString& operation, const QVariantMap& projected) {
            const auto id = QStringLiteral("returned-portable-request"); fake.calls.append({id, provider, operation, projected});
            delete std::exchange(service, nullptr); aliveDuringReturn = !weak.expired(); return id;
        };
        service = new AddonMetadataService(std::move(backend)); token.reset();
        const auto cleanup = qScopeGuard([&] { delete std::exchange(service, nullptr); });
        service->enrich("video.details", {{"identity", identity()}}, {"description"});
        QTRY_VERIFY(!service); QCOMPARE(fake.calls.size(), 1);
        QCOMPARE(fake.canceled, QStringList{fake.calls.first().id});
        QVERIFY(aliveDuringReturn); QVERIFY(weak.expired());
    }

    void aNativeCancelCanDeleteTheService()
    {
        for (const bool timeout : {false, true}) {
            Fake fake; fake.providers.clear(); auto* service = new AddonMetadataService(fake.backend()); QObject lifetime;
            const auto cleanup = qScopeGuard([&] { delete std::exchange(service, nullptr); });
            QString invocation; unsigned canceled = 0;
            QVERIFY(service->registerBuiltinProvider("video.details", provider("hakone.builtin.cancel/details"), &lifetime,
                [&](const QString& id, const QString&, const QVariantMap&) { invocation = id; },
                [&](const QString& id) { QCOMPARE(id, invocation); ++canceled; delete std::exchange(service, nullptr); }));
            QSignalSpy finished(service, &AddonMetadataService::finished);
            QVariantMap request{{"identity", identity()}}; if (timeout) request.insert("timeoutMs", 100);
            const auto consumer = service->enrich("video.details", request, {"description"});
            QTRY_VERIFY(!invocation.isEmpty());
            if (!timeout) service->cancel(consumer);
            QTRY_VERIFY(!service); QCOMPARE(canceled, 1u); QCOMPARE(finished.size(), 0);
            QVERIFY(fake.canceled.isEmpty());
        }
    }

    void anArtworkCancelCanDeleteTheServiceAfterConsumerRemoval()
    {
        Fake fake; auto* service = new AddonMetadataService(fake.backend());
        const auto cleanup = qScopeGuard([&] { delete std::exchange(service, nullptr); });
        unsigned resolved = 0, canceled = 0;
        service->setArtworkAdapter([&](const QString&, const QVariant&, AddonMetadataService::ArtworkCallback) {
            ++resolved; return QStringLiteral("native-asset-request");
        }, [&](const QString& id) {
            QCOMPARE(id, QStringLiteral("native-asset-request")); ++canceled;
            QCOMPARE(service->mediaCount(), 0); delete std::exchange(service, nullptr);
        });
        const auto media = service->acquireMedia(QUrl(QStringLiteral("file:///private/movie.mkv")), identity());
        const auto consumer = service->enrich("video.details", {{"media", media}}, {"artwork"});
        service->releaseMedia(media); QTRY_COMPARE(fake.calls.size(), 1);
        fake.complete(*service, {{"artwork", QVariantMap{{"asset", "opaque"}}}});
        QTRY_COMPARE(resolved, 1u); service->cancel(consumer);
        QVERIFY(!service); QCOMPARE(canceled, 1u);
    }

    void allDetachedAssetsCancelAfterOwnerDeletion_data()
    {
        QTest::addColumn<bool>("nativeCompletion");
        QTest::newRow("first-asset-cancel") << false;
        QTest::newRow("native-job-cancel") << true;
    }
    void allDetachedAssetsCancelAfterOwnerDeletion()
    {
        QFETCH(bool, nativeCompletion);
        Fake fake; DeleteOnEnd state; state.service = new AddonMetadataService(fake.backend());
        AddonMetadataService*& service = state.service;
        const auto cleanup = qScopeGuard([&] { delete std::exchange(service, nullptr); });
        unsigned resolved = 0; QSet<QString> canceled;
        service->setArtworkAdapter([&](const QString&, const QVariant& descriptor, AddonMetadataService::ArtworkCallback) {
            ++resolved; return descriptor.toMap().value("asset").toString();
        }, [&](const QString& id) {
            QVERIFY(!canceled.contains(id)); canceled.insert(id);
            if (nativeCompletion || canceled.size() > 1) QVERIFY(!service);
            else { QCOMPARE(service->mediaCount(), 0); delete std::exchange(service, nullptr); }
        });
        const auto backend = std::shared_ptr<vlc_metadata_backend_t>(service->holdCoreBackend(), vlc_metadata_backend_Release);
        const auto item = std::shared_ptr<input_item_t>(input_item_New("file:///private/movie.mkv", "Example Movie"), input_item_Release);
        std::shared_ptr<vlc_metadata_job_t> job; QString consumer;
        if (nativeCompletion) {
            const char* fields[]{"artwork", "artworkUrl"};
            const auto context = QJsonDocument::fromVariant(QVariantMap{{"identity", identity()}}).toJson(QJsonDocument::Compact);
            vlc_metadata_request request{}; request.feature = "video.details"; request.operation = "enrich";
            request.fields = fields; request.field_count = 2; request.item = item.get();
            request.context_json = context.constData(); request.context_size = context.size(); request.flags = VLC_METADATA_REQUEST_NETWORK;
            job = {vlc_metadata_job_New(backend.get(), &request, DeleteOnEnd::callback, &state), vlc_metadata_job_Release};
            QVERIFY(job); QCOMPARE(vlc_metadata_job_Start(job.get()), VLC_SUCCESS);
        } else {
            const auto media = service->acquireMedia(QUrl(QStringLiteral("file:///private/movie.mkv")), identity());
            consumer = service->enrich("video.details", {{"media", media}}, {"artwork", "artworkUrl"});
            service->releaseMedia(media);
        }
        QTRY_COMPARE(fake.calls.size(), 1);
        fake.complete(*service, {{"artwork", QVariantMap{{"asset", "asset-a"}}}, {"artworkUrl", QVariantMap{{"asset", "asset-b"}}}});
        QTRY_COMPARE(resolved, 2u);
        if (nativeCompletion) service->invalidate(); else service->cancel(consumer);
        QVERIFY(!service); QCOMPARE(canceled, QSet<QString>({"asset-a", "asset-b"}));
        QCOMPARE(state.count, nativeCompletion ? 1 : 0);
    }

    void nativeInvocationsRetainOnlyTheirExactCoreJobs()
    {
        Fake fake; fake.providers.clear(); AddonMetadataService service(fake.backend()); QObject lifetime;
        auto descriptor = provider("hakone.builtin.exact/details"); descriptor.insert("requiresNativeAnchor", true);
        QStringList invocations; QList<vlc_metadata_job_t*> original;
        QVERIFY(service.registerBuiltinProvider("video.details", descriptor, &lifetime,
            [&](const QString& id, const QString&, const QVariantMap& projected) {
                QVERIFY(!projected.contains("privateScope"));
                const auto held = std::shared_ptr<vlc_metadata_job_t>(service.holdNativeJobForInvocation(id), vlc_metadata_job_Release);
                QVERIFY(held); original.append(held.get()); invocations.append(id);
            }));
        const auto backend = std::shared_ptr<vlc_metadata_backend_t>(service.holdCoreBackend(), vlc_metadata_backend_Release);
        const auto item = std::shared_ptr<input_item_t>(input_item_New("file:///private/movie.mkv", "Example Movie"), input_item_Release);
        Ended one, two;
        const auto first = std::shared_ptr<vlc_metadata_job_t>(coreJob(backend.get(), one, item.get()), vlc_metadata_job_Release);
        const auto second = std::shared_ptr<vlc_metadata_job_t>(coreJob(backend.get(), two, item.get()), vlc_metadata_job_Release);
        QCOMPARE(vlc_metadata_job_Start(first.get()), VLC_SUCCESS); QCOMPARE(vlc_metadata_job_Start(second.get()), VLC_SUCCESS);
        QTRY_COMPARE(invocations.size(), 2); QCOMPARE(original, QList<vlc_metadata_job_t*>({first.get(), second.get()}));
        QVERIFY(!service.holdNativeJobForInvocation("native-metadata:unknown"));
        QVERIFY(vlc_metadata_job_Cancel(first.get()));
        QVERIFY(!service.holdNativeJobForInvocation(invocations.first()));
        {
            const auto held = std::shared_ptr<vlc_metadata_job_t>(service.holdNativeJobForInvocation(invocations.last()), vlc_metadata_job_Release);
            QCOMPARE(held.get(), second.get());
        }
        service.completeBuiltin(invocations.last(), "match", claims({{"description", "Exact native description"}}));
        QVERIFY(!service.holdNativeJobForInvocation(invocations.last()));
        QTRY_COMPARE(two.count.load(), 1); QCOMPARE(one.count.load(), 1);
        QSignalSpy finished(&service, &AddonMetadataService::finished);
        service.enrich("video.details", {{"identity", identity()}, {"privateScope", "native-anchor:forged"}}, {"description"});
        QTRY_COMPARE(finished.size(), 1); QCOMPARE(invocations.size(), 2);
    }

    void coreScopesDoNotChangePortableCoalescing()
    {
        Fake fake; AddonMetadataService service(fake.backend());
        const auto backend = std::shared_ptr<vlc_metadata_backend_t>(service.holdCoreBackend(), vlc_metadata_backend_Release);
        const auto item = std::shared_ptr<input_item_t>(input_item_New("file:///private/movie.mkv", "Example Movie"), input_item_Release);
        Ended one, two;
        const auto first = std::shared_ptr<vlc_metadata_job_t>(coreJob(backend.get(), one, item.get()), vlc_metadata_job_Release);
        const auto second = std::shared_ptr<vlc_metadata_job_t>(coreJob(backend.get(), two, item.get()), vlc_metadata_job_Release);
        QCOMPARE(vlc_metadata_job_Start(first.get()), VLC_SUCCESS); QCOMPARE(vlc_metadata_job_Start(second.get()), VLC_SUCCESS);
        QTRY_COMPARE(fake.calls.size(), 1);
        QVERIFY(!QJsonDocument::fromVariant(fake.calls.first().context).toJson().contains("native-anchor:"));
        fake.complete(service, {{"description", "Shared portable description"}});
        QTRY_COMPARE(one.count.load(), 1); QTRY_COMPARE(two.count.load(), 1);
    }

    void appRetirementRefusesNativePostsBeforeQtDelivery()
    {
        vlc::addons::AddonStreamLifetime app; Fake fake; AddonMetadataService service(fake.backend());
        QVERIFY(service.bindApplicationLifetime(app.handle()));
        const auto backend = std::shared_ptr<vlc_metadata_backend_t>(service.holdCoreBackend(), vlc_metadata_backend_Release);
        Ended first; const auto job = std::shared_ptr<vlc_metadata_job_t>(coreJob(backend.get(), first), vlc_metadata_job_Release);
        QCOMPARE(vlc_metadata_job_Start(job.get()), VLC_SUCCESS); QCOMPARE(service.nativeJobCount(), 1);
        app.close();
        Ended second; const auto late = std::shared_ptr<vlc_metadata_job_t>(coreJob(backend.get(), second), vlc_metadata_job_Release);
        int lateStatus = VLC_SUCCESS;
        std::thread native([&] {
            lateStatus = vlc_metadata_job_Start(late.get());
            vlc_metadata_backend_Close(backend.get());
        }); native.join();
        QCOMPARE(lateStatus, VLC_SUCCESS);
        QCOMPARE(first.count.load(), 1); QCOMPARE(second.count.load(), 1);
        QCOMPARE(second.status.load(), -EINTR);
        QCOMPARE(service.nativeJobCount(), 0);
        QCoreApplication::processEvents(); QVERIFY(fake.calls.isEmpty()); service.shutdown();
    }
    void appRetirementRefusesAnInFlightPortableClaim()
    {
        vlc::addons::AddonStreamLifetime app; Fake fake; AddonMetadataService service(fake.backend());
        QVERIFY(service.bindApplicationLifetime(app.handle()));
        const auto backend = std::shared_ptr<vlc_metadata_backend_t>(service.holdCoreBackend(), vlc_metadata_backend_Release);
        Ended ended; const auto job = std::shared_ptr<vlc_metadata_job_t>(coreJob(backend.get(), ended), vlc_metadata_job_Release);
        QCOMPARE(vlc_metadata_job_Start(job.get()), VLC_SUCCESS); QTRY_COMPARE(fake.calls.size(), 1);
        app.close(); fake.complete(service, {{QStringLiteral("description"), QStringLiteral("Late claim")}});
        QTRY_COMPARE(ended.count.load(), 1); QCOMPARE(ended.status.load(), -EINTR);
        QCOMPARE(service.nativeJobCount(), 0); service.shutdown();
    }
    void applicationBindingCannotAdoptAnExistingNativeDispatch()
    {
        vlc::addons::AddonStreamLifetime app; Fake fake; AddonMetadataService service(fake.backend());
        const auto backend = std::shared_ptr<vlc_metadata_backend_t>(service.holdCoreBackend(), vlc_metadata_backend_Release);
        Ended ended; const auto job = std::shared_ptr<vlc_metadata_job_t>(coreJob(backend.get(), ended), vlc_metadata_job_Release);
        QCOMPARE(vlc_metadata_job_Start(job.get()), VLC_SUCCESS);
        QVERIFY(!service.bindApplicationLifetime(app.handle()));
        service.shutdown(); QCOMPARE(ended.count.load(), 1);
    }
    void shutdownRegistrationCallbacksCanDestroyTheOwner()
    {
        Fake fake; auto backend = fake.backend(); AddonMetadataService* service = nullptr;
        unsigned unregisters = 0;
        backend.unregisterBuiltin = [&](const QString&) {
            ++unregisters; auto* deleted = std::exchange(service, nullptr); delete deleted;
        };
        service = new AddonMetadataService(std::move(backend)); QObject lifetime;
        for (const auto* id : {"hakone.builtin.first/details", "hakone.builtin.second/details"})
            QVERIFY(service->registerBuiltinProvider("video.details", provider(id), &lifetime,
                [](const auto&, const auto&, const auto&) {}));
        service->shutdown(); QVERIFY(!service); QCOMPARE(unregisters, 1u);
    }
    void singleRegistrationCallbacksCanDestroyTheOwner()
    {
        for (const bool registration : {false, true}) {
            Fake fake; auto backend = fake.backend(); AddonMetadataService* service = nullptr; bool armed = false;
            const auto destroy = [&] { if (armed) { auto* deleted = std::exchange(service, nullptr); delete deleted; } };
            backend.registerBuiltin = [&](const QString&, const QVariantMap&) { if (registration) destroy(); };
            backend.unregisterBuiltin = [&](const QString&) { if (!registration) destroy(); };
            service = new AddonMetadataService(std::move(backend)); QObject lifetime;
            armed = registration;
            const auto registered = service->registerBuiltinProvider("video.details", provider("hakone.builtin.single/details"),
                &lifetime, [](const auto&, const auto&, const auto&) {});
            if (registration) QVERIFY(!registered);
            else { QVERIFY(registered); armed = true; service->unregisterBuiltinProvider("hakone.builtin.single/details"); }
            QVERIFY(!service);
        }
    }
    void retiredBuiltinCaptureCanDestroyTheOwnerAfterMapMutation()
    {
        struct Capture { std::function<void()> drop; ~Capture() { drop(); } };
        for (const int mode : {0, 1, 2}) {
            Fake fake; auto* service = new AddonMetadataService(fake.backend()); QObject lifetime;
            unsigned drops = 0;
            auto capture = std::make_shared<Capture>();
            capture->drop = [&] { ++drops; auto* deleted = std::exchange(service, nullptr); delete deleted; };
            QVERIFY(service->registerBuiltinProvider("video.details", provider("hakone.builtin.capture/details"), &lifetime,
                [capture](const auto&, const auto&, const auto&) {}));
            capture.reset();
            if (mode == 0) service->unregisterBuiltinProvider("hakone.builtin.capture/details");
            else if (mode == 1) service->registerBuiltinProvider("video.details", provider("hakone.builtin.capture/details"),
                &lifetime, [](const auto&, const auto&, const auto&) {});
            else service->shutdown();
            QVERIFY(!service); QCOMPARE(drops, 1u);
        }
    }
    void nativeWithdrawalCancelsTheOriginalHandler()
    {
        for (const bool replace : {false, true}) {
            Fake fake; fake.providers.clear(); AddonMetadataService service(fake.backend()); QObject lifetime;
            QString invocation; unsigned oldCancel = 0, newCancel = 0;
            QVERIFY(service.registerBuiltinProvider("video.details", provider("hakone.builtin.changed/details"), &lifetime,
                [&](const auto& id, const auto&, const auto&) { invocation = id; },
                [&](const auto& id) { QCOMPARE(id, invocation); ++oldCancel; }));
            service.enrich("video.details", {{"identity", identity()}}, {"description"});
            QTRY_VERIFY(!invocation.isEmpty());
            if (replace) QVERIFY(service.registerBuiltinProvider("video.details", provider("hakone.builtin.changed/details"),
                &lifetime, [](const auto&, const auto&, const auto&) {}, [&](const auto&) { ++newCancel; }));
            else service.unregisterBuiltinProvider("hakone.builtin.changed/details");
            QCOMPARE(oldCancel, 1u); QCOMPARE(newCancel, 0u);
        }
    }
    void aNativeHandlerCanCompleteInlineAfterItsIDIsRecorded()
    {
        Fake fake; fake.providers.clear(); AddonMetadataService service(fake.backend()); QObject lifetime;
        QVERIFY(service.registerBuiltinProvider("video.details", provider("hakone.builtin.inline/details"), &lifetime,
            [&](const auto& id, const auto&, const auto&) {
                service.completeBuiltin(id, "match", claims({{"description", "Inline native description"}}));
            }));
        QSignalSpy finished(&service, &AddonMetadataService::finished);
        service.enrich("video.details", {{"identity", identity()}}, {"description"});
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished.first().at(1).toString(), QStringLiteral("match"));
        QCOMPARE(finished.first().at(2).toMap().value("fields").toMap().value("description").toString(),
            QStringLiteral("Inline native description"));
    }
    void nativeTagsKeepArtworkReleaseIdsAndListingType()
    {
        auto* item = input_item_New("file:///private/Song.flac", "Song"); QVERIFY(item);
        input_item_SetMetaExtra(item, "MUSICBRAINZ_ALBUMID", "00000000-0000-0000-0000-000000000001");
        input_item_SetMetaExtra(item, "MB_ALBUMID", "76df3287-6cda-33eb-8e9a-044b5e15ffdd");
        input_item_SetMetaExtra(item, "Listing Type", "radio");
        const auto tags = MediaIdentity::inputMetadata(item);
        QCOMPARE(tags.value("musicBrainzAlbumId").toString(), "76df3287-6cda-33eb-8e9a-044b5e15ffdd");
        QCOMPARE(tags.value("release").toMap().value("listingType").toString(), "radio");
        const auto identity = MediaIdentity::identify(QUrl("file:///private/Song.flac"), tags);
        QCOMPARE(identity.ids.value("musicBrainzAlbumId"), "76df3287-6cda-33eb-8e9a-044b5e15ffdd");
        QCOMPARE(identity.release.value("listingType"), "radio"); input_item_Release(item);
    }

    void nativeCallbackCanDestroyTheSharedOwner()
    {
        for (const bool closing : {false, true}) {
            Fake fake; DeleteOnEnd state;
            state.service = new AddonMetadataService(fake.backend());
            auto* backend = state.service->holdCoreBackend();
            const char* fields[]{"description"};
            const QByteArray context = QJsonDocument::fromVariant(QVariantMap{{"identity", identity()}}).toJson(QJsonDocument::Compact);
            vlc_metadata_request request{}; request.feature = "video.details"; request.operation = "enrich";
            request.fields = fields; request.field_count = 1; request.context_json = context.constData(); request.context_size = context.size();
            request.flags = VLC_METADATA_REQUEST_NETWORK;
            auto* job = vlc_metadata_job_New(backend, &request, DeleteOnEnd::callback, &state); QVERIFY(job);
            QCOMPARE(vlc_metadata_job_Start(job), VLC_SUCCESS);
            if (closing) state.service->shutdown();
            else { QTRY_COMPARE(fake.calls.size(), 1); fake.complete(*state.service, {{"description", "Provider text"}}); }
            QTRY_COMPARE(state.count, 1); QVERIFY(!state.service); QVERIFY(vlc_metadata_backend_IsClosed(backend));
            QVERIFY(vlc_metadata_job_GetResult(job)); vlc_metadata_job_Release(job); vlc_metadata_backend_Release(backend);
        }
    }

    void mediaReferencesShareOneJobAndKeepWaiterGenerations()
    {
        Fake fake; AddonMetadataService service(fake.backend());
        const auto first = service.acquireMedia(QUrl("file:///private/movie.mkv"), identity());
        const auto second = service.acquireMedia(QUrl("file:///private/movie.mkv"), identity());
        QCOMPARE(first, second); QCOMPARE(service.mediaCount(), 1);
        QSignalSpy results(&service, &AddonMetadataService::finished);
        const auto a = service.enrich("video.details", {{"media", first}, {"generation", 1}}, {"description"});
        const auto b = service.enrich("video.details", {{"media", second}, {"generation", 2}}, {"description"});
        service.releaseMedia(first); service.releaseMedia(second);
        QTRY_COMPARE(fake.calls.size(), 1);
        QVERIFY(!fake.calls[0].context.contains("media"));
        QVERIFY(!QJsonDocument::fromVariant(fake.calls[0].context).toJson().contains("private"));
        service.cancel(a); QVERIFY(fake.canceled.isEmpty()); QCOMPARE(service.mediaCount(), 1);
        fake.complete(service, {{"description", "Provider text"}});
        QTRY_COMPARE(results.size(), 1);
        QCOMPARE(results[0][0].toString(), b);
        QCOMPARE(results[0][2].toMap().value("contextGeneration").toInt(), 2);
        QCOMPARE(service.mediaCount(), 0);
    }

    void nativeAdaptersUseIssuedInvocationForPrivateUrl()
    {
        Fake fake; fake.providers.clear(); AddonMetadataService service(fake.backend());
        QObject lifetime; QString invocation; QUrl privateUrl;
        QVERIFY(service.registerBuiltinProvider("video.details", provider("hakone.builtin.example/details"), &lifetime,
            [&](const QString& id, const QString& operation, const QVariantMap& context) {
                invocation = id; privateUrl = service.privateUrlForInvocation(id);
                QCOMPARE(operation, "enrich"); QVERIFY(context.contains("media"));
                service.completeBuiltin(id, "match", claims({{"description", "Native text"}}));
            }));
        QSignalSpy results(&service, &AddonMetadataService::finished);
        const auto media = service.acquireMedia(QUrl("file:///private/native.mkv"), identity());
        service.enrich("video.details", {{"media", media}}, {"description"}); service.releaseMedia(media);
        QTRY_COMPARE(results.size(), 1);
        QCOMPARE(privateUrl, QUrl("file:///private/native.mkv"));
        QVERIFY(service.privateUrlForInvocation(invocation).isEmpty());
        QVERIFY(service.privateUrlForInvocation("forged").isEmpty()); QVERIFY(fake.calls.isEmpty());
        QCOMPARE(service.mediaCount(), 0);
    }

    void nativeLifetimeEndsPendingRequests()
    {
        Fake fake; fake.providers.clear(); AddonMetadataService service(fake.backend());
        auto* lifetime = new QObject; QString invocation;
        QVERIFY(service.registerBuiltinProvider("video.details", provider("hakone.builtin.example/details"), lifetime,
            [&](const QString& id, const QString&, const QVariantMap&) { invocation = id; }));
        QSignalSpy canceled(&service, &AddonMetadataService::canceled);
        const auto media = service.acquireMedia(QUrl("file:///private/native.mkv"), identity());
        service.enrich("video.details", {{"media", media}}, {"description"}); service.releaseMedia(media);
        QTRY_VERIFY(!invocation.isEmpty());
        delete lifetime;
        QCOMPARE(canceled.size(), 1); QCOMPARE(service.mediaCount(), 0);
        QVERIFY(service.privateUrlForInvocation(invocation).isEmpty());
    }

    void coreCancelNeedsNoQtEvent()
    {
        Fake fake; auto* service = new AddonMetadataService(fake.backend());
        auto* backend = service->holdCoreBackend(); QVERIFY(backend);
        Ended ended; auto* job = coreJob(backend, ended); QVERIFY(job);
        QCOMPARE(vlc_metadata_job_Start(job), VLC_SUCCESS); QCOMPARE(service->nativeJobCount(), 1);
        bool won = false;
        std::thread cancel([&] { won = vlc_metadata_job_Cancel(job); }); cancel.join(); QVERIFY(won);
        QCOMPARE(ended.count.load(), 1); QCOMPARE(ended.status.load(), -EINTR);
        QCOMPARE(service->nativeJobCount(), 0); QVERIFY(fake.calls.isEmpty());
        delete service; QCoreApplication::processEvents();
        QVERIFY(vlc_metadata_backend_IsClosed(backend));
        vlc_metadata_job_Release(job); vlc_metadata_backend_Release(backend);
    }

    void coreCloseDrainsActiveRequestsWithoutQt()
    {
        Fake fake; AddonMetadataService service(fake.backend());
        auto* backend = service.holdCoreBackend();
        Ended firstEnded, secondEnded;
        auto* first = coreJob(backend, firstEnded); auto* second = coreJob(backend, secondEnded);
        QCOMPARE(vlc_metadata_job_Start(first), VLC_SUCCESS); QCOMPARE(vlc_metadata_job_Start(second), VLC_SUCCESS);
        QTRY_COMPARE(fake.calls.size(), 1);
        std::thread close([&] { vlc_metadata_backend_Close(backend); }); close.join();
        QCOMPARE(firstEnded.count.load(), 1); QCOMPARE(secondEnded.count.load(), 1);
        QCOMPARE(service.nativeJobCount(), 0);
        const auto call = fake.calls.last();
        service.operationFinished(call.id, call.provider, "match", claims({{"description", "Late text"}}), {});
        QCoreApplication::processEvents();
        QCOMPARE(firstEnded.count.load(), 1); QCOMPARE(secondEnded.count.load(), 1);
        vlc_metadata_job_Release(first); vlc_metadata_job_Release(second); vlc_metadata_backend_Release(backend);
    }

    void coreResultHasNativeBaselinePatch()
    {
        Fake fake; AddonMetadataService service(fake.backend()); auto* backend = service.holdCoreBackend();
        auto* item = input_item_New("file:///private/movie.mkv", "Example Movie"); QVERIFY(item);
        input_item_SetMeta(item, vlc_meta_Title, "Original title");
        Ended ended; auto* job = coreJob(backend, ended, item); QVERIFY(job);
        input_item_SetMeta(item, vlc_meta_Title, "User title");
        QCOMPARE(vlc_metadata_job_Start(job), VLC_SUCCESS); QTRY_COMPARE(fake.calls.size(), 1);
        fake.complete(service, {{"description", "Provider text"}}); QTRY_COMPARE(ended.count.load(), 1);
        QCOMPARE(ended.status.load(), VLC_SUCCESS); QCOMPARE(service.nativeJobCount(), 0);
        const auto* result = vlc_metadata_job_GetResult(job); QVERIFY(result); QVERIFY(result->item);
        QCOMPARE(result->field_count, size_t(1)); QCOMPARE(result->fields[0].type, vlc_meta_Description);
        QVERIFY(!result->fields[0].expected_value); QCOMPARE(QString::fromUtf8(result->fields[0].value), "Provider text");
        char* title = input_item_GetMeta(item, vlc_meta_Title); QCOMPARE(QString::fromUtf8(title), "User title"); free(title);
        QVERIFY(QByteArray(result->provenance_json, result->provenance_size).contains("23"));
        vlc_metadata_job_Release(job); vlc_metadata_backend_Release(backend); input_item_Release(item);
        QCOMPARE(service.mediaCount(), 0);
    }

    void automaticPolicyRejectsBeforeQtAndRevocationDrains()
    {
        Fake fake; AddonMetadataService service(fake.backend()); auto* backend = service.holdCoreBackend();
        Ended denied; auto* first = coreJob(backend, denied, nullptr, VLC_METADATA_REQUEST_NETWORK | VLC_METADATA_REQUEST_AUTOMATIC);
        QCOMPARE(vlc_metadata_job_Start(first), VLC_SUCCESS); QCOMPARE(denied.count.load(), 1);
        QCOMPARE(denied.status.load(), VLC_EACCES); QVERIFY(fake.calls.isEmpty());
        service.setAutomaticNetworkAllowed(true);
        Ended active; auto* second = coreJob(backend, active, nullptr, VLC_METADATA_REQUEST_NETWORK | VLC_METADATA_REQUEST_AUTOMATIC);
        QCOMPARE(vlc_metadata_job_Start(second), VLC_SUCCESS); QCOMPARE(service.nativeJobCount(), 1);
        service.setAutomaticNetworkAllowed(false);
        QCOMPARE(active.count.load(), 1); QCOMPARE(active.status.load(), -EINTR); QCOMPARE(service.nativeJobCount(), 0);
        vlc_metadata_job_Release(first); vlc_metadata_job_Release(second); vlc_metadata_backend_Release(backend);
    }

    void automaticRevocationFindsCompletedNativeAnchors()
    {
        Ended automaticEnded, manualEnded;
        Fake fake; AddonMetadataService service(fake.backend()); service.setAutomaticNetworkAllowed(true);
        const auto backend = std::shared_ptr<vlc_metadata_backend_t>(service.holdCoreBackend(), vlc_metadata_backend_Release);
        const auto automaticItem = std::shared_ptr<input_item_t>(input_item_New("file:///automatic.mkv", "Automatic"), input_item_Release);
        const auto manualItem = std::shared_ptr<input_item_t>(input_item_New("file:///manual.mkv", "Manual"), input_item_Release);
        QVERIFY(automaticItem); QVERIFY(manualItem);
        const auto automatic = std::shared_ptr<vlc_metadata_job_t>(coreJob(backend.get(), automaticEnded,
            automaticItem.get(), VLC_METADATA_REQUEST_NETWORK | VLC_METADATA_REQUEST_AUTOMATIC), vlc_metadata_job_Release);
        const auto manual = std::shared_ptr<vlc_metadata_job_t>(coreJob(backend.get(), manualEnded,
            manualItem.get()), vlc_metadata_job_Release);
        QVERIFY(automatic); QVERIFY(manual);
        for (const auto& job : {automatic, manual}) {
            QCOMPARE(vlc_metadata_job_StartLocal(job.get()), VLC_SUCCESS);
            auto* result = input_item_Copy(vlc_metadata_job_GetRequest(job.get())->item); QVERIFY(result);
            input_item_SetDescription(result, "Native result");
            QVERIFY(vlc_metadata_job_Complete(job.get(), VLC_SUCCESS, result, nullptr, 0)); input_item_Release(result);
        }
        QCOMPARE(service.nativeJobCount(), 0); QVERIFY(fake.calls.isEmpty());
        service.setAutomaticNetworkAllowed(false);
        QCOMPARE(vlc_metadata_job_Apply(automatic.get(), automaticItem.get(), 0, nullptr), -EINTR);
        QCOMPARE(vlc_metadata_job_Apply(manual.get(), manualItem.get(), 0, nullptr), VLC_SUCCESS);
        QCOMPARE(automaticEnded.count.load(), 1); QCOMPARE(manualEnded.count.load(), 1);
        char* automaticText = input_item_GetDescription(automaticItem.get()); QVERIFY(!automaticText);
        char* manualText = input_item_GetDescription(manualItem.get());
        QCOMPARE(QByteArray(manualText), QByteArray("Native result")); free(manualText);
    }

    void nativeArtworkStageKeepsTheOriginalAddressOutOfProviders_data()
    {
        QTest::addColumn<bool>("withdraw"); QTest::addColumn<bool>("edit");
        QTest::newRow("publish") << false << false;
        QTest::newRow("withdraw") << true << false;
        QTest::newRow("same-value-edit") << false << true;
    }
    void nativeArtworkStageKeepsTheOriginalAddressOutOfProviders()
    {
        QFETCH(bool, withdraw); QFETCH(bool, edit);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        Ended childEnded, anchorEnded; std::atomic_int reads{0};
        QImage image(3, 2, QImage::Format_ARGB32); image.fill(Qt::red);
        QByteArray bytes; QBuffer buffer(&bytes); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(image.save(&buffer, "PNG"));
        TestNativeArtwork assets({[&](const QUrl& url, qint64, const std::function<bool()>& live) {
            ++reads;
            if (url != QUrl("http://192.168.1.4/native-cover.png") || !live())
                return AddonNativeArtworkAssets::ReadResult{{}, QStringLiteral("The test request is not current.")};
            return AddonNativeArtworkAssets::ReadResult{bytes, {}};
        }}, directory.path(), {});
        Fake fake; AddonMetadataService service(fake.backend()); service.setAutomaticNetworkAllowed(true);
        service.setNativeArtworkAdapter(&assets);
        const auto backend = std::shared_ptr<vlc_metadata_backend_t>(service.holdCoreBackend(), vlc_metadata_backend_Release);
        const auto item = std::shared_ptr<input_item_t>(input_item_New("file:///private/movie.mkv", "Movie"), input_item_Release);
        QVERIFY(item); input_item_SetArtURL(item.get(), "http://192.168.1.4/native-cover.png");
        const auto anchor = nativeArtworkJob(backend.get(), anchorEnded, item.get(), "native-parent"); QVERIFY(anchor);
        QCOMPARE(vlc_metadata_job_StartLocal(anchor.get()), VLC_SUCCESS);
        const auto child = nativeArtworkJob(backend.get(), childEnded, item.get()); QVERIFY(child);
        QCOMPARE(vlc_metadata_job_Start(child.get()), VLC_SUCCESS);
        QTRY_COMPARE(childEnded.count.load(), 1); QCOMPARE(childEnded.status.load(), VLC_SUCCESS);
        QCOMPARE(reads.load(), 1); QVERIFY(fake.calls.isEmpty()); QCOMPARE(service.mediaCount(), 0);
        const auto* result = vlc_metadata_job_GetResult(child.get()); QVERIFY(result); QVERIFY(result->item);
        char* approved = vlc_artwork_item_GetURI(result->item); QVERIFY(approved);
        const QUrl file = QUrl::fromEncoded(QByteArray(approved)); free(approved);
        QVERIFY(file.isLocalFile()); QVERIFY(QFileInfo::exists(file.toLocalFile()));
        QVERIFY(!QByteArray(result->provenance_json, result->provenance_size).contains("192.168.1.4"));
        QVERIFY(vlc_metadata_job_Complete(anchor.get(), VLC_SUCCESS, result->item, nullptr, 0));
        // The completed child cannot remove the parent's consent boundary.
        if (edit) input_item_SetArtURL(item.get(), "http://192.168.1.4/native-cover.png");
        if (withdraw) service.setAutomaticNetworkAllowed(false);
        QCOMPARE(vlc_metadata_job_Apply(anchor.get(), item.get(), uint32_t(1) << vlc_meta_ArtworkURL, nullptr),
            withdraw ? -EINTR : edit ? -EAGAIN : VLC_SUCCESS);
        char* unchanged = input_item_GetArtURL(item.get());
        QCOMPARE(QByteArray(unchanged), withdraw || edit ? QByteArray("http://192.168.1.4/native-cover.png") : file.toEncoded()); free(unchanged);
    }

    void nativeArtworkCancellationInterruptsIOWithoutGuiDelivery()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        Ended ended; std::atomic_bool entered{false}, left{false};
        TestNativeArtwork assets({[&](const QUrl&, qint64, const std::function<bool()>&) {
            entered.store(true); vlc_poll_i11e(nullptr, 0, 5000); left.store(true);
            return AddonNativeArtworkAssets::ReadResult{{}, QStringLiteral("The test read has ended.")};
        }}, directory.path(), {});
        Fake fake; AddonMetadataService service(fake.backend()); service.setAutomaticNetworkAllowed(true);
        service.setNativeArtworkAdapter(&assets);
        const auto backend = std::shared_ptr<vlc_metadata_backend_t>(service.holdCoreBackend(), vlc_metadata_backend_Release);
        const auto item = std::shared_ptr<input_item_t>(input_item_New("file:///private/movie.mkv", "Movie"), input_item_Release);
        QVERIFY(item); input_item_SetArtURL(item.get(), "http://127.0.0.1/native-cover.png");
        const auto job = nativeArtworkJob(backend.get(), ended, item.get()); QVERIFY(job);
        QCOMPARE(vlc_metadata_job_Start(job.get()), VLC_SUCCESS); QTRY_VERIFY(entered.load());
        std::thread canceler([job] { vlc_metadata_job_Cancel(job.get()); }); canceler.join();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (!left.load() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        QVERIFY(left.load()); QCOMPARE(ended.count.load(), 1); QCOMPARE(ended.status.load(), -EINTR);
        QCOMPARE(service.nativeJobCount(), 0); QVERIFY(fake.calls.isEmpty());
    }

    void guestArtworkNeedsHostAssetAdapter()
    {
        Fake fake; AddonMetadataService service(fake.backend()); QSignalSpy results(&service, &AddonMetadataService::finished);
        service.enrich("video.details", {{"identity", identity()}}, {"artwork"});
        QTRY_COMPARE(fake.calls.size(), 1); fake.complete(service, {{"artwork", "https://remote.invalid/image.jpg"}});
        QTRY_COMPARE(results.size(), 1);
        QCOMPARE(results[0][1].toString(), "unsupported");
        QVERIFY(!results[0][2].toMap().value("fields").toMap().contains("artwork"));
        QVERIFY(!QJsonDocument::fromVariant(results[0][2]).toJson().contains("remote.invalid"));
    }

    void hostArtworkAdapterReturnsLocalFileAndCancelEndsItsJob()
    {
        Fake fake; auto backend = fake.backend();
        AddonMetadataService::ArtworkCallback complete; QStringList canceled; QString source; QVariant asset;
        backend.resolveArtwork = [&](const QString& provider, const QVariant& descriptor, AddonMetadataService::ArtworkCallback callback) {
            source = provider; asset = descriptor;
            complete = std::move(callback); return QStringLiteral("asset-1");
        };
        backend.cancelArtwork = [&](const QString& id) { canceled.append(id); };
        AddonMetadataService service(backend); QSignalSpy results(&service, &AddonMetadataService::finished);
        service.enrich("video.details", {{"identity", identity()}}, {"artwork"});
        QTRY_COMPARE(fake.calls.size(), 1); fake.complete(service, {{"artwork", "https://remote.invalid/image.jpg"}});
        QTRY_VERIFY(bool(complete));
        QCOMPARE(source, "example/details"); QCOMPARE(asset.toString(), "https://remote.invalid/image.jpg");
        QTemporaryFile file; QVERIFY(file.open()); file.write("fixture"); file.flush();
        complete(QUrl::fromLocalFile(file.fileName()), {}); QTRY_COMPARE(results.size(), 1);
        QCOMPARE(results[0][2].toMap().value("fields").toMap().value("artwork").toString(), QUrl::fromLocalFile(file.fileName()).toString());
        service.invalidate(); complete = {};
        const auto consumer = service.enrich("video.details", {{"identity", identity()}}, {"artwork"});
        QTRY_COMPARE(fake.calls.size(), 2); fake.complete(service, {{"artwork", "https://remote.invalid/image.jpg"}});
        QTRY_VERIFY(bool(complete)); service.cancel(consumer); QCOMPARE(canceled, QStringList{"asset-1"});
        complete(QUrl::fromLocalFile(file.fileName()), {}); QCoreApplication::processEvents(); QCOMPARE(results.size(), 1);
    }
    void nativeAssetAdapterProofSurvivesTheCoreCommit()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        vlc::addons::NetworkBroker::Callback network;
        AddonArtworkAssets::Backend assetsBackend{
            [](const QString&) { return QVariantMap{{"enabled", true}, {"configured", true}, {"cachePartition", "release-1:account-1:settings-1"}}; },
            [&](const QString& source, const QJsonObject& request, vlc::addons::NetworkBroker::Callback complete) {
                if (source != QStringLiteral("example/details") || request.value("method").toString() != QStringLiteral("GET")) return quint64(0);
                network = std::move(complete); return quint64(1);
            }, [](quint64) {}};
        AddonArtworkAssets assets(assetsBackend, directory.path(), {});
        Fake fake; auto serviceBackend = fake.backend();
        serviceBackend.resolveArtwork = [&](const QString& source, const QVariant& claim, AddonMetadataService::ArtworkCallback complete) {
            return assets.resolve(source, claim, std::move(complete));
        };
        serviceBackend.cancelArtwork = [&](const QString& id) { assets.cancel(id); };
        AddonMetadataService service(serviceBackend); auto* backend = service.holdCoreBackend();
        auto* item = input_item_New("hakone-addon-source:v1:asset", "Source"); QVERIFY(item);
        input_item_SetArtURL(item, "http://127.0.0.1/unapproved.png");
        const auto cleanup = qScopeGuard([&] { input_item_Release(item); vlc_metadata_backend_Release(backend); });
        const char* fields[]{"description", "artwork"};
        const auto context = QJsonDocument::fromVariant(QVariantMap{{"identity", identity()}}).toJson(QJsonDocument::Compact);
        vlc_metadata_request request{}; request.feature = "video.details"; request.operation = "enrich";
        request.item = item; request.fields = fields; request.field_count = 2;
        request.context_json = context.constData(); request.context_size = context.size(); request.flags = VLC_METADATA_REQUEST_NETWORK;
        Ended ended; auto* job = vlc_metadata_job_New(backend, &request, Ended::callback, &ended); QVERIFY(job);
        const auto release = qScopeGuard([&] { vlc_metadata_job_Release(job); });
        QCOMPARE(vlc_metadata_job_Start(job), VLC_SUCCESS); QTRY_COMPARE(fake.calls.size(), 1);
        QVERIFY(!fake.calls[0].context.value("existingFields").toMap().contains("artwork"));
        fake.complete(service, {{"description", "Provider text"}, {"artwork", "https://images.example.org/art.png"}});
        QTRY_VERIFY(bool(network));
        QImage image(2, 2, QImage::Format_RGB32); image.fill(Qt::red);
        QByteArray bytes; QBuffer output(&bytes); QVERIFY(output.open(QIODevice::WriteOnly)); QVERIFY(image.save(&output, "PNG"));
        vlc::addons::NetworkBroker::Response response; response.status = 200; response.body = bytes; network(response);
        QTRY_COMPARE(ended.count.load(), 1); QCOMPARE(ended.status.load(), VLC_SUCCESS);
        const auto* result = vlc_metadata_job_GetResult(job); QVERIFY(result && result->item);
        const auto approved = vlc::wrap_cptr(vlc_artwork_item_GetURI(result->item)); QVERIFY(approved);
        const QUrl local = QUrl::fromEncoded(approved.get()); QVERIFY(local.isLocalFile()); QVERIFY(QFileInfo::exists(local.toLocalFile()));
        QVERIFY(!vlc_artwork_item_GetURI(item));
        size_t applied = 0; QCOMPARE(vlc_metadata_job_Apply(job, item, 0, &applied), VLC_SUCCESS); QCOMPARE(applied, size_t(2));
        const auto published = vlc::wrap_cptr(vlc_artwork_item_GetURI(item)); QVERIFY(published);
        QCOMPARE(QByteArray(published.get()), QByteArray(approved.get()));
        input_item_SetArtURL(item, published.get()); QVERIFY(!vlc_artwork_item_GetURI(item));
    }
};
QTEST_GUILESS_MAIN(TestAddonMetadataService)
#include "test_addon_metadata_service.moc"
