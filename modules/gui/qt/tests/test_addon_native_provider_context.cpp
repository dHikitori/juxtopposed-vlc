// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <QtTest/QtTest>
#include <QPointer>
#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QThread>
#include <atomic>
#include <thread>
#include <utility>
#include <vector>
#include <vlc/vlc.h>
#include <vlc_common.h>
#include <vlc_input_item.h>
#include <vlc_artwork_policy.h>
#include "lib/libvlc_internal.h"
#include "addons/addon_native_provider_context.hpp"
#include "addons/addon_native_provider_artwork_assets.hpp"
#include "fixtures/addon_native_provider_fixture.h"

namespace {
struct Job {
    vlc_metadata_backend_t* backend = nullptr;
    input_item_t* item = nullptr;
    vlc_metadata_job_t* job = nullptr;
    std::atomic_int ended{0};
    static int dispatch(void*, vlc_metadata_job_t*) { return VLC_SUCCESS; }
    static void finished(vlc_metadata_job_t*, int, void* opaque) { ++static_cast<Job*>(opaque)->ended; }
    static void destroy(void* opaque) { if (opaque) ++*static_cast<std::atomic_int*>(opaque); }
    explicit Job(std::atomic_int* destroyed = nullptr, const char* uri = "file:///native/song.flac", const char* art = nullptr) {
        const vlc_metadata_backend_operations operations{dispatch, nullptr, destroy};
        backend = vlc_metadata_backend_New(&operations, destroyed); item = input_item_New(uri, "Native song");
        input_item_SetArtist(item, "Native artist"); input_item_SetAlbum(item, "Native album");
        if (art) input_item_SetArtURL(item, art);
        const char* fields[]{"artwork"}; vlc_metadata_request request{};
        request.feature = "artwork"; request.operation = "enrich"; request.fields = fields; request.field_count = 1;
        request.item = item; request.flags = VLC_METADATA_REQUEST_NETWORK; request.generation = 17; request.target_revision = 23;
        job = vlc_metadata_job_New(backend, &request, finished, this); if (job) vlc_metadata_job_Start(job);
    }
    void clear() {
        if (backend) vlc_metadata_backend_Close(backend);
        if (job) vlc_metadata_job_Release(job);
        if (backend) vlc_metadata_backend_Release(backend);
        if (item) input_item_Release(item);
        backend = nullptr; item = nullptr; job = nullptr;
    }
    ~Job() { clear(); }
};
struct Parent {
    libvlc_instance_t* instance;
    vlc::addons::AddonStreamLifetime lifetime;
    Parent() {
        const char* args[]{"--ignore-config", "--no-plugins-scan", "--no-plugins-cache"};
        instance = libvlc_new(3, args);
        if (!instance) qFatal("The test cannot create its native parent.");
        addon_test_native_provider_queued(false); addon_test_native_provider_block_delete(false);
    }
    ~Parent() {
        addon_test_native_provider_block_delete(false); lifetime.close();
        if (!lifetime.handle().waitQt(std::chrono::seconds(5))) qFatal("The test native join did not end.");
        libvlc_release(instance);
    }
    vlc_object_t* object() const { return VLC_OBJECT(instance->p_libvlc_int); }
};
}
class TestAddonNativeProviderContext : public QObject
{
    Q_OBJECT
private slots:
    void nativeExecutorsRequireAnAppJoin()
    {
        Parent parent; const auto before = addon_test_native_provider_deleted();
        { AddonNativeProviderContext owner({}, parent.object()); QVERIFY(!owner.available()); }
        QCOMPARE(addon_test_native_provider_deleted(), before);
    }
    void inlineNativeResultOffersOnlyAnExactOpaqueClaim()
    {
        Parent parent; AddonNativeProviderContext owner(parent.lifetime.handle(), parent.object()); QVERIFY(owner.available());
        Job job, other; int completed = 0; AddonNativeProviderContext::Result result;
        QVERIFY(owner.probe(job.job, META_FETCHER_PROVIDER_LASTFM, [&](const auto& value) { result = value; ++completed; }));
        QCOMPARE(completed, 0); QTRY_COMPARE(completed, 1); QCOMPARE(result.status, VLC_SUCCESS);
        QVERIFY(!result.claim.isEmpty()); QVERIFY(!result.claim.contains("http"));
        QVERIFY(owner.validates(result.claim, job.job, META_FETCHER_PROVIDER_LASTFM));
        QVERIFY(!owner.validates(result.claim, other.job, META_FETCHER_PROVIDER_LASTFM));
        QVERIFY(!owner.validates(result.claim, job.job, META_FETCHER_PROVIDER_MUSICBRAINZ));
        QVERIFY(!owner.validates("http://127.0.0.1/native-family.png", job.job, META_FETCHER_PROVIDER_LASTFM));
        input_item_SetArtist(vlc_metadata_job_GetRequest(job.job)->item, "Native artist");
        QVERIFY(!owner.validates(result.claim, job.job, META_FETCHER_PROVIDER_LASTFM));
    }
    void scopedAdmissionAndNativeCancelDoNotDeliverAClaim()
    {
        Parent parent; AddonNativeProviderContext owner(parent.lifetime.handle(), parent.object());
        Job scoped(nullptr, "HaKoNe-AdDoN-SoUrCe:v1:import"); int rejected = 0;
        QVERIFY(owner.probe(scoped.job, META_FETCHER_PROVIDER_LASTFM, [&](const auto& result) {
            QVERIFY(result.claim.isEmpty()); QVERIFY(result.status != VLC_SUCCESS); ++rejected;
        }));
        QTRY_COMPARE(rejected, 1);
        addon_test_native_provider_queued(true); Job job; int completed = 0;
        auto cancel = owner.probe(job.job, META_FETCHER_PROVIDER_LASTFM, [&](const auto&) { ++completed; }); QVERIFY(cancel);
        std::thread native([cancel] { cancel(); }); native.join();
        QCOMPARE(job.ended.load(), 1); addon_test_native_provider_complete();
        QTest::qWait(50); QCOMPARE(completed, 0);
    }
    void closeDropsGuiCapturesBeforeTheBlockedNativeJoin()
    {
        Parent parent; auto* owner = new AddonNativeProviderContext(parent.lifetime.handle(), parent.object());
        addon_test_native_provider_queued(true); addon_test_native_provider_block_delete(true); Job job;
        const auto ownerThread = QThread::currentThreadId(); std::atomic_int dropped{0}; std::atomic_bool correctThread{false};
        auto captured = std::shared_ptr<int>(new int, [&](int* value) { correctThread = QThread::currentThreadId() == ownerThread; ++dropped; delete value; });
        const auto cleanup = qScopeGuard([&] {
            owner->close(); addon_test_native_provider_block_delete(false);
            if (!parent.lifetime.handle().waitQt(std::chrono::seconds(5))) qFatal("The fixture join did not end.");
            delete owner;
        });
        QVERIFY(owner->probe(job.job, META_FETCHER_PROVIDER_LASTFM, [captured](const auto&) {})); captured.reset();
        const auto start = std::chrono::steady_clock::now(); owner->close();
        QVERIFY(std::chrono::steady_clock::now() - start < std::chrono::seconds(1));
        QCOMPARE(dropped.load(), 1); QVERIFY(correctThread.load()); QCOMPARE(job.ended.load(), 1);
        QVERIFY(!parent.lifetime.handle().waitQt(std::chrono::milliseconds(20)));
        addon_test_native_provider_block_delete(false); QVERIFY(parent.lifetime.handle().waitQt(std::chrono::seconds(3)));
    }
    void aRetainedCancelCannotRetainCoreStateAfterTheJoin_data()
    {
        QTest::addColumn<bool>("publish"); QTest::newRow("pending") << false; QTest::newRow("published-claim") << true;
    }
    void aRetainedCancelCannotRetainCoreStateAfterTheJoin()
    {
        QFETCH(bool, publish); Parent parent; AddonNativeProviderContext owner(parent.lifetime.handle(), parent.object());
        std::atomic_int destroyed{0}; Job job(&destroyed); addon_test_native_provider_queued(!publish); int completed = 0;
        const auto cleanup = qScopeGuard([&] {
            owner.close(); if (!parent.lifetime.handle().waitQt(std::chrono::seconds(5))) qFatal("The fixture join did not end.");
        });
        const auto cancel = owner.probe(job.job, META_FETCHER_PROVIDER_LASTFM, [&](const auto&) { ++completed; }); QVERIFY(cancel);
        if (publish) QTRY_COMPARE(completed, 1);
        job.clear(); owner.close(); QVERIFY(parent.lifetime.handle().waitQt(std::chrono::seconds(3)));
        QCOMPARE(destroyed.load(), 1); cancel(); QCOMPARE(destroyed.load(), 1);
    }
    void aDeliveryCanDeleteTheGuiOwner()
    {
        Parent parent; Job job; auto* owner = new AddonNativeProviderContext(parent.lifetime.handle(), parent.object());
        QPointer<AddonNativeProviderContext> guard(owner); int completed = 0;
        const auto cleanup = qScopeGuard([&] {
            delete guard.data(); if (!parent.lifetime.handle().waitQt(std::chrono::seconds(5))) qFatal("The fixture join did not end.");
        });
        QVERIFY(owner->probe(job.job, META_FETCHER_PROVIDER_LASTFM, [&](const auto&) { ++completed; delete owner; }));
        QTRY_VERIFY(!guard); QCOMPARE(completed, 1); QVERIFY(parent.lifetime.handle().waitQt(std::chrono::seconds(3)));
    }
    void thePendingLimitDoesNotEvictAnExistingJob()
    {
        Parent parent; AddonNativeProviderContext owner(parent.lifetime.handle(), parent.object());
        addon_test_native_provider_queued(true); std::vector<std::unique_ptr<Job>> jobs; int completed = 0;
        for (unsigned i = 0; i != 32; ++i) {
            jobs.push_back(std::make_unique<Job>());
            QVERIFY(owner.probe(jobs.back()->job, META_FETCHER_PROVIDER_LASTFM, [&](const auto&) { ++completed; }));
        }
        Job refused; QVERIFY(!owner.probe(refused.job, META_FETCHER_PROVIDER_LASTFM, [&](const auto&) { ++completed; }));
        for (const auto& job : jobs) QCOMPARE(job->ended.load(), 0);
        owner.close(); QVERIFY(parent.lifetime.handle().waitQt(std::chrono::seconds(3)));
        QCOMPARE(completed, 0); for (const auto& job : jobs) QCOMPARE(job->ended.load(), 1);
        QCOMPARE(refused.ended.load(), 0);
    }
    void aLastCompletionCaptureCanDeleteTheOwnerWithTwoReadyResults()
    {
        Parent parent; Job first, second;
        auto* owner = new AddonNativeProviderContext(parent.lifetime.handle(), parent.object());
        QPointer<AddonNativeProviderContext> guard(owner); int delivered = 0, dropped = 0;
        const auto dispose = [&](int* value) { ++dropped; delete std::exchange(owner, nullptr); delete value; };
        auto firstCapture = std::shared_ptr<int>(new int, dispose);
        auto secondCapture = std::shared_ptr<int>(new int, dispose);
        const auto cleanup = qScopeGuard([&] {
            delete std::exchange(owner, nullptr);
            if (!parent.lifetime.handle().waitQt(std::chrono::seconds(5))) qFatal("The fixture join did not end.");
        });
        QVERIFY(owner->probe(first.job, META_FETCHER_PROVIDER_LASTFM, [firstCapture, &delivered](const auto&) { ++delivered; }));
        QVERIFY(owner->probe(second.job, META_FETCHER_PROVIDER_LASTFM, [secondCapture, &delivered](const auto&) { ++delivered; }));
        firstCapture.reset(); secondCapture.reset();
        QTRY_VERIFY(!guard); QCOMPARE(delivered, 1); QCOMPARE(dropped, 2);
    }
    void aGuiDeliveryRetainsItsNativeReferencesThroughReturn()
    {
        Parent parent; Job job; auto* owner = new AddonNativeProviderContext(parent.lifetime.handle(), parent.object());
        QPointer<AddonNativeProviderContext> guard(owner); bool blocked = false;
        const auto cleanup = qScopeGuard([&] {
            delete guard.data(); if (!parent.lifetime.handle().waitQt(std::chrono::seconds(5))) qFatal("The fixture join did not end.");
        });
        QVERIFY(owner->probe(job.job, META_FETCHER_PROVIDER_LASTFM, [&](const auto&) {
            delete owner;
            std::thread observer([&] { blocked = !parent.lifetime.handle().waitQt(std::chrono::milliseconds(30)); });
            observer.join();
        }));
        QTRY_VERIFY(!guard); QVERIFY(blocked); QVERIFY(parent.lifetime.handle().waitQt(std::chrono::seconds(3)));
    }
    void aSealedFamilyClaimUsesItsSeparateMaterializer_data()
    {
        QTest::addColumn<bool>("withdraw"); QTest::newRow("allowed") << false; QTest::newRow("consent-withdrawn") << true;
    }
    void aSealedFamilyClaimUsesItsSeparateMaterializer()
    {
        QFETCH(bool, withdraw);
        Parent parent; QTemporaryDir cache;
        QImage pixels(4, 4, QImage::Format_RGB32); pixels.fill(Qt::blue);
        QByteArray bytes; QBuffer output(&bytes); QVERIFY(output.open(QIODevice::WriteOnly)); QVERIFY(pixels.save(&output, "PNG"));
        std::atomic_int reads{0};
        AddonNativeArtworkAssets assets(parent.lifetime.handle(), {{[bytes, &reads](const QUrl& url, qint64, const auto&) {
            ++reads;
            if (url != QUrl("http://127.0.0.1/native-family.png")) return AddonNativeArtworkAssets::ReadResult{{}, "The fixture address does not match."};
            return AddonNativeArtworkAssets::ReadResult{bytes, {}};
        }}}, cache.path(), {});
        assets.setNetworkAllowed(true); AddonNativeProviderContext owner(parent.lifetime.handle(), parent.object());
        AddonNativeProviderArtworkAssets materializer(&assets, &owner);
        const auto cleanup = qScopeGuard([&] {
            assets.close(); owner.close();
            if (!parent.lifetime.handle().waitQt(std::chrono::seconds(5))) qFatal("The fixture join did not end.");
        });
        Job job, other;
        QString claim; QVERIFY(owner.probe(job.job, META_FETCHER_PROVIDER_LASTFM, [&](const auto& value) { claim = value.claim; }));
        QTRY_VERIFY(!claim.isEmpty()); QVERIFY(!assets.resolve(job.job, [](const auto&) {}));
        QVERIFY(!materializer.resolve(claim, other.job, META_FETCHER_PROVIDER_LASTFM, [](const auto&) {}));
        QVERIFY(!materializer.resolve(claim, job.job, META_FETCHER_PROVIDER_MUSICBRAINZ, [](const auto&) {}));
        AddonNativeProviderArtworkAssets::Result asset; int completed = 0;
        QVERIFY(materializer.resolve(claim, job.job, META_FETCHER_PROVIDER_LASTFM, [&](const auto& value) { asset = value; ++completed; }));
        QTRY_COMPARE(completed, 1); QCOMPARE(reads.load(), 1); QVERIFY2(asset.error.isEmpty(), qPrintable(asset.error)); QVERIFY(asset.file.isLocalFile());
        QVERIFY(!owner.validates(claim, job.job, META_FETCHER_PROVIDER_LASTFM));
        QFile image(asset.file.toLocalFile()); QVERIFY(image.open(QIODevice::ReadOnly));
        QCOMPARE(QImage::fromData(image.readAll()).convertToFormat(QImage::Format_ARGB32), pixels.convertToFormat(QImage::Format_ARGB32));
        auto* result = input_item_Copy(vlc_metadata_job_GetRequest(job.job)->item); QVERIFY(result);
        QCOMPARE(vlc_artwork_item_SetApprovedURI(result, asset.file.toEncoded().constData()), VLC_SUCCESS);
        QVERIFY(vlc_metadata_job_Complete(job.job, VLC_SUCCESS, result, nullptr, 0)); input_item_Release(result);
        if (withdraw) assets.setNetworkAllowed(false);
        int applied = 0;
        QCOMPARE(asset.permit.admit([&] { ++applied; return vlc_metadata_job_Apply(job.job, job.item, 0, nullptr); }),
            withdraw ? VLC_EACCES : VLC_SUCCESS);
        QCOMPARE(applied, withdraw ? 0 : 1);
        char* artwork = vlc_artwork_item_GetURI(job.item); QCOMPARE(QByteArray(artwork), withdraw ? QByteArray{} : asset.file.toEncoded()); free(artwork);
        char* artist = input_item_GetArtist(job.item); QCOMPARE(QByteArray(artist), QByteArray("Native artist")); free(artist);
        assets.close(); owner.close(); QVERIFY(parent.lifetime.handle().waitQt(std::chrono::seconds(3)));
    }
    void aNativeFamilyCannotReplaceAnExistingDisplayAsset()
    {
        Parent parent; QTemporaryDir cache; std::atomic_int reads{0};
        AddonNativeArtworkAssets assets(parent.lifetime.handle(), {{[&reads](const QUrl&, qint64, const auto&) {
            ++reads; return AddonNativeArtworkAssets::ReadResult{};
        }}}, cache.path(), {});
        assets.setNetworkAllowed(true); AddonNativeProviderContext owner(parent.lifetime.handle(), parent.object());
        AddonNativeProviderArtworkAssets materializer(&assets, &owner);
        const auto cleanup = qScopeGuard([&] {
            assets.close(); owner.close();
            if (!parent.lifetime.handle().waitQt(std::chrono::seconds(5))) qFatal("The fixture join did not end.");
        });
        Job job(nullptr, "file:///native/song.flac", "file:///native/user-cover.png"); QString claim;
        QVERIFY(owner.probe(job.job, META_FETCHER_PROVIDER_LASTFM, [&](const auto& value) { claim = value.claim; }));
        QTRY_VERIFY(!claim.isEmpty());
        QVERIFY(!materializer.resolve(claim, job.job, META_FETCHER_PROVIDER_LASTFM, [](const auto&) {}));
        QCOMPARE(reads.load(), 0); char* artwork = input_item_GetArtURL(job.item);
        QCOMPARE(QByteArray(artwork), QByteArray("file:///native/user-cover.png")); free(artwork);
        assets.close(); owner.close(); QVERIFY(parent.lifetime.handle().waitQt(std::chrono::seconds(3)));
    }
};
QTEST_GUILESS_MAIN(TestAddonNativeProviderContext)
#include "test_addon_native_provider_context.moc"
