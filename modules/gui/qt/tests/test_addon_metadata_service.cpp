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
#include "addons/addon_artwork_assets.hpp"
#include "addons/addon_metadata_service.hpp"
#include "addons/addon_native_artwork_assets.hpp"
#include "addon_native_artwork_test_owner.hpp"
#include "util/media_identity_vlc.hpp"

namespace {
QVariantMap identity()
{ return {{"kind", "movie"}, {"title", "Example Movie"}, {"year", 2020}, {"ids", QVariantMap{{"imdb", "tt1234567"}}}}; }
QVariantMap provider(const QString& id = QStringLiteral("example/details"))
{
    return {{"id", id}, {"packageId", id.section('/', 0, 0)}, {"name", "Example"}, {"enabled", true},
        {"configured", true}, {"features", QStringList{"video.details"}}, {"mediaKinds", QStringList{"movie"}},
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
    void nativeInvocationsRetainOnlyTheirExactCoreJobs()
    {
        Fake fake; fake.providers.clear(); AddonMetadataService service(fake.backend()); QObject lifetime;
        auto descriptor = provider("vlc.builtin.exact/details"); descriptor.insert("requiresNativeAnchor", true);
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
        for (const auto* id : {"vlc.builtin.first/details", "vlc.builtin.second/details"})
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
            const auto registered = service->registerBuiltinProvider("video.details", provider("vlc.builtin.single/details"),
                &lifetime, [](const auto&, const auto&, const auto&) {});
            if (registration) QVERIFY(!registered);
            else { QVERIFY(registered); armed = true; service->unregisterBuiltinProvider("vlc.builtin.single/details"); }
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
            QVERIFY(service->registerBuiltinProvider("video.details", provider("vlc.builtin.capture/details"), &lifetime,
                [capture](const auto&, const auto&, const auto&) {}));
            capture.reset();
            if (mode == 0) service->unregisterBuiltinProvider("vlc.builtin.capture/details");
            else if (mode == 1) service->registerBuiltinProvider("video.details", provider("vlc.builtin.capture/details"),
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
            QVERIFY(service.registerBuiltinProvider("video.details", provider("vlc.builtin.changed/details"), &lifetime,
                [&](const auto& id, const auto&, const auto&) { invocation = id; },
                [&](const auto& id) { QCOMPARE(id, invocation); ++oldCancel; }));
            service.enrich("video.details", {{"identity", identity()}}, {"description"});
            QTRY_VERIFY(!invocation.isEmpty());
            if (replace) QVERIFY(service.registerBuiltinProvider("video.details", provider("vlc.builtin.changed/details"),
                &lifetime, [](const auto&, const auto&, const auto&) {}, [&](const auto&) { ++newCancel; }));
            else service.unregisterBuiltinProvider("vlc.builtin.changed/details");
            QCOMPARE(oldCancel, 1u); QCOMPARE(newCancel, 0u);
        }
    }
    void aNativeHandlerCanCompleteInlineAfterItsIDIsRecorded()
    {
        Fake fake; fake.providers.clear(); AddonMetadataService service(fake.backend()); QObject lifetime;
        QVERIFY(service.registerBuiltinProvider("video.details", provider("vlc.builtin.inline/details"), &lifetime,
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
        QVERIFY(service.registerBuiltinProvider("video.details", provider("vlc.builtin.example/details"), &lifetime,
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
        QVERIFY(service.registerBuiltinProvider("video.details", provider("vlc.builtin.example/details"), lifetime,
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
