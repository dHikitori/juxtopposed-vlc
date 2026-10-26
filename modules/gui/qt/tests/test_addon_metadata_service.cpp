// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QJsonDocument>
#include <atomic>
#include <thread>
#include <vlc_common.h>
#include <vlc_input_item.h>
#include <vlc_metadata_backend.h>
#include "addons/addon_metadata_service.hpp"
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
}

class TestAddonMetadataService : public QObject
{
    Q_OBJECT
private slots:
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
};
QTEST_GUILESS_MAIN(TestAddonMetadataService)
#include "test_addon_metadata_service.moc"
