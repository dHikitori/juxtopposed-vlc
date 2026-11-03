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
#include <condition_variable>
#include <mutex>
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
#include "../../../../src/preparser/metadata_artwork_outcome.h"

namespace {
struct NativePause {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, released = false, timedOut = false;
    void wait() {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true; changed.notify_all();
        if (!changed.wait_for(lock, std::chrono::seconds(5), [this] { return released; })) timedOut = true;
    }
    bool waitEntered() {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(3), [this] { return entered; });
    }
    void release() { std::lock_guard<std::mutex> lock(mutex); released = true; changed.notify_all(); }
};
#ifdef HAKONE_NATIVE_OUTCOME_WRAP
std::atomic<NativePause*> admissionPause{nullptr};
#endif
struct Job {
    vlc_metadata_backend_t* backend = nullptr;
    input_item_t* item = nullptr;
    vlc_metadata_job_t* job = nullptr;
    std::atomic_int ended{0};
    std::function<void()> onEnded;
    static int dispatch(void*, vlc_metadata_job_t*) { return VLC_SUCCESS; }
    static void finished(vlc_metadata_job_t*, int, void* opaque) {
        auto* self = static_cast<Job*>(opaque); ++self->ended;
        const auto callback = self->onEnded; if (callback) callback();
    }
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
        addon_test_native_provider_outcome(false, META_FETCHER_OUTCOME_UNKNOWN);
    }
    ~Parent() {
        addon_test_native_provider_block_delete(false); lifetime.close();
        if (!lifetime.handle().waitQt(std::chrono::seconds(5))) qFatal("The test native join did not end.");
        libvlc_release(instance);
    }
    vlc_object_t* object() const { return VLC_OBJECT(instance->p_libvlc_int); }
};
}
#ifdef HAKONE_NATIVE_OUTCOME_WRAP
extern "C" vlc_metadata_native_outcome_authority_t* __real_vlc_metadata_native_claim_AdmitOutcome(
    vlc_metadata_native_claim_t*, vlc_metadata_job_t*, meta_fetcher_provider_t, vlc_metadata_artwork_admission_t*);
extern "C" vlc_metadata_native_outcome_authority_t* __wrap_vlc_metadata_native_claim_AdmitOutcome(
    vlc_metadata_native_claim_t* claim, vlc_metadata_job_t* job, meta_fetcher_provider_t family,
    vlc_metadata_artwork_admission_t* admission)
{
    if (auto* pause = admissionPause.load()) pause->wait();
    return __real_vlc_metadata_native_claim_AdmitOutcome(claim, job, family, admission);
}
#endif
class TestAddonNativeProviderContext : public QObject
{
    Q_OBJECT
private slots:
    void negativeOutcomeHasSeparateOneUseNativeAuthority_data()
    {
        QTest::addColumn<int>("kind");
        QTest::newRow("no-match") << int(META_FETCHER_OUTCOME_NO_MATCH);
        QTest::newRow("not-applicable") << int(META_FETCHER_OUTCOME_NOT_APPLICABLE);
    }
    void negativeOutcomeHasSeparateOneUseNativeAuthority()
    {
        QFETCH(int, kind);
        Parent parent; AddonNativeProviderContext owner(parent.lifetime.handle(), parent.object());
        addon_test_native_provider_outcome(true, meta_fetcher_outcome_t(kind));
        std::atomic_int destroyed{0}; Job job(&destroyed); int completed = 0; AddonNativeProviderContext::Result result;
        QVERIFY(owner.probeOutcome(job.job, META_FETCHER_PROVIDER_LASTFM, [&](const auto& value) { result = value; ++completed; }));
        QTRY_COMPARE(completed, 1); QCOMPARE(result.status, VLC_SUCCESS); QVERIFY(!result.claim.isEmpty());
        QVERIFY(!owner.validates(result.claim, job.job, META_FETCHER_PROVIDER_LASTFM));
        QTemporaryDir cache; std::atomic_int reads{0};
        AddonNativeArtworkAssets images(parent.lifetime.handle(), {{[&reads](const QUrl&, qint64, const auto&) {
            ++reads; return AddonNativeArtworkAssets::ReadResult{{}, "No image request is expected."};
        }}}, cache.path(), {});
        images.setNetworkAllowed(true); AddonNativeProviderArtworkAssets materializer(&images, &owner);
        QVERIFY(materializer.available());
        QVERIFY(!materializer.resolve(result.claim, job.job, META_FETCHER_PROVIDER_LASTFM, [](const auto&) {}));
        QCOMPARE(reads.load(), 0);
        const auto gate = std::shared_ptr<vlc_metadata_artwork_admission_t>(
            vlc_metadata_artwork_admission_New(vlc_tick_now() + VLC_TICK_FROM_SEC(10)), vlc_metadata_artwork_admission_Release);
        QVERIFY(gate); auto accepted = owner.takeOutcome(result.claim, job.job, META_FETCHER_PROVIDER_LASTFM, gate.get());
        QVERIFY(accepted); QCOMPARE(int(accepted.value), kind); QVERIFY(vlc_metadata_artwork_admission_IsCurrent(gate.get()));
        QVERIFY(vlc_metadata_job_IsPending(job.job));
        job.clear(); QCOMPARE(destroyed.load(), 1); QVERIFY(accepted.authority);
        owner.close(); QVERIFY(!vlc_metadata_artwork_admission_IsCurrent(gate.get()));
        // A retained authority has no child reference and cannot hold up Delete.
        images.close();
        QVERIFY(parent.lifetime.handle().waitQt(std::chrono::seconds(3))); QVERIFY(accepted.authority);
        QVERIFY(!owner.takeOutcome(result.claim, job.job, META_FETCHER_PROVIDER_LASTFM, gate.get()));
    }
    void outcomeAuthorityRejectsWrongBindingsAndStaleSnapshots_data()
    {
        QTest::addColumn<int>("mutation");
        for (int i = 0; i != 6; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void outcomeAuthorityRejectsWrongBindingsAndStaleSnapshots()
    {
        QFETCH(int, mutation);
        Parent parent; AddonNativeProviderContext owner(parent.lifetime.handle(), parent.object());
        addon_test_native_provider_outcome(true, META_FETCHER_OUTCOME_NO_MATCH);
        Job job, other; QString claim;
        QVERIFY(owner.probeOutcome(job.job, META_FETCHER_PROVIDER_LASTFM, [&](const auto& value) { claim = value.claim; }));
        QTRY_VERIFY(!claim.isEmpty());
        auto* target = job.job; auto family = META_FETCHER_PROVIDER_LASTFM;
        if (mutation == 0) target = other.job;
        if (mutation == 1) family = META_FETCHER_PROVIDER_MUSICBRAINZ;
        if (mutation == 2) input_item_SetArtist(vlc_metadata_job_GetRequest(job.job)->item, "Native artist");
        if (mutation == 3) { input_item_SetURI(vlc_metadata_job_GetRequest(job.job)->item, "file:///other");
            input_item_SetURI(vlc_metadata_job_GetRequest(job.job)->item, "file:///native/song.flac"); }
        if (mutation == 4) vlc_metadata_job_Cancel(job.job);
        if (mutation == 5) owner.close();
        const auto gate = std::shared_ptr<vlc_metadata_artwork_admission_t>(
            vlc_metadata_artwork_admission_New(vlc_tick_now() + VLC_TICK_FROM_SEC(10)), vlc_metadata_artwork_admission_Release);
        QVERIFY(gate); QVERIFY(!owner.takeOutcome(claim, target, family, gate.get()));
        QVERIFY(!vlc_metadata_artwork_admission_IsCurrent(gate.get()));
    }
    void outcomeFailureCannotEnterAnImageOrNegativeAuthority_data()
    {
        QTest::addColumn<int>("kind");
        for (const auto kind : {META_FETCHER_OUTCOME_UNAVAILABLE, META_FETCHER_OUTCOME_DENIED, META_FETCHER_OUTCOME_ERROR})
            QTest::newRow(qPrintable(QString::number(int(kind)))) << int(kind);
    }
    void outcomeFailureCannotEnterAnImageOrNegativeAuthority()
    {
        QFETCH(int, kind);
        Parent parent; AddonNativeProviderContext owner(parent.lifetime.handle(), parent.object());
        addon_test_native_provider_outcome(true, meta_fetcher_outcome_t(kind));
        Job job; int completed = 0; AddonNativeProviderContext::Result result;
        QVERIFY(owner.probeOutcome(job.job, META_FETCHER_PROVIDER_LASTFM, [&](const auto& value) { result = value; ++completed; }));
        QTRY_COMPARE(completed, 1); QCOMPARE(result.status, VLC_SUCCESS); QVERIFY(!result.claim.isEmpty());
        QVERIFY(!owner.validates(result.claim, job.job, META_FETCHER_PROVIDER_LASTFM));
        const auto gate = std::shared_ptr<vlc_metadata_artwork_admission_t>(
            vlc_metadata_artwork_admission_New(vlc_tick_now() + VLC_TICK_FROM_SEC(10)), vlc_metadata_artwork_admission_Release);
        QVERIFY(gate); QVERIFY(!owner.takeOutcome(result.claim, job.job, META_FETCHER_PROVIDER_LASTFM, gate.get()));
        QVERIFY(!vlc_metadata_artwork_admission_IsCurrent(gate.get()));
    }
    void anOutcomeCanOnlyBeConsumedOnce()
    {
        Parent parent; AddonNativeProviderContext owner(parent.lifetime.handle(), parent.object());
        addon_test_native_provider_outcome(true, META_FETCHER_OUTCOME_NO_MATCH);
        Job job; QString claim;
        QVERIFY(owner.probeOutcome(job.job, META_FETCHER_PROVIDER_LASTFM, [&](const auto& value) { claim = value.claim; }));
        QTRY_VERIFY(!claim.isEmpty());
        const auto gate = std::shared_ptr<vlc_metadata_artwork_admission_t>(
            vlc_metadata_artwork_admission_New(vlc_tick_now() + VLC_TICK_FROM_SEC(10)), vlc_metadata_artwork_admission_Release);
        QVERIFY(gate); auto accepted = owner.takeOutcome(claim, job.job, META_FETCHER_PROVIDER_LASTFM, gate.get());
        QVERIFY(accepted); QVERIFY(!owner.takeOutcome(claim, job.job, META_FETCHER_PROVIDER_LASTFM, gate.get()));
        QVERIFY(!vlc_metadata_artwork_admission_IsCurrent(gate.get()));
    }
    void repeatedCloseRevokesBeforeAnEarlierJobCancelReturns()
    {
        Parent parent; AddonNativeProviderContext owner(parent.lifetime.handle(), parent.object());
        addon_test_native_provider_outcome(true, META_FETCHER_OUTCOME_NO_MATCH);
        Job first; QString claim;
        QVERIFY(owner.probeOutcome(first.job, META_FETCHER_PROVIDER_LASTFM, [&](const auto& value) { claim = value.claim; }));
        QTRY_VERIFY(!claim.isEmpty());
        const auto gate = std::shared_ptr<vlc_metadata_artwork_admission_t>(
            vlc_metadata_artwork_admission_New(vlc_tick_now() + VLC_TICK_FROM_SEC(10)), vlc_metadata_artwork_admission_Release);
        QVERIFY(gate); auto accepted = owner.takeOutcome(claim, first.job, META_FETCHER_PROVIDER_LASTFM, gate.get());
        QVERIFY(accepted);
        NativePause blocked; Job second; second.onEnded = [&] { blocked.wait(); };
        const auto cleanup = qScopeGuard([&] {
            blocked.release(); owner.close();
            if (!parent.lifetime.handle().waitQt(std::chrono::seconds(5))) qFatal("The concurrent retirement did not join.");
        });
        addon_test_native_provider_queued(true);
        QVERIFY(owner.probe(second.job, META_FETCHER_PROVIDER_LASTFM, [](const auto&) {}));
        parent.lifetime.close(); QVERIFY(blocked.waitEntered());
        // The first native retirement is still inside this real job callback.
        // The owner-thread close must independently reach native gate revoke.
        owner.close(); QVERIFY(!vlc_metadata_artwork_admission_IsCurrent(gate.get()));
        QVERIFY(!parent.lifetime.handle().waitQt(std::chrono::milliseconds(30)));
        blocked.release(); QVERIFY(parent.lifetime.handle().waitQt(std::chrono::seconds(3)));
        QVERIFY(!blocked.timedOut); QCOMPARE(second.ended.load(), 1); QVERIFY(accepted.authority);
    }
#ifdef HAKONE_NATIVE_OUTCOME_WRAP
    void aConsumedOutcomeKeepsItsNativeCallCountedThroughConversion()
    {
        Parent parent; AddonNativeProviderContext owner(parent.lifetime.handle(), parent.object());
        addon_test_native_provider_outcome(true, META_FETCHER_OUTCOME_NO_MATCH);
        Job job; QString claim;
        QVERIFY(owner.probeOutcome(job.job, META_FETCHER_PROVIDER_LASTFM, [&](const auto& value) { claim = value.claim; }));
        QTRY_VERIFY(!claim.isEmpty());
        const auto gate = std::shared_ptr<vlc_metadata_artwork_admission_t>(
            vlc_metadata_artwork_admission_New(vlc_tick_now() + VLC_TICK_FROM_SEC(10)), vlc_metadata_artwork_admission_Release);
        QVERIFY(gate);
        NativePause blocked; bool entered = false, joinedBeforeReturn = false;
        admissionPause.store(&blocked);
        const auto reset = qScopeGuard([&] { admissionPause.store(nullptr); blocked.release(); owner.close(); });
        std::thread retiring([&] {
            entered = blocked.waitEntered();
            if (entered) {
                parent.lifetime.close();
                joinedBeforeReturn = parent.lifetime.handle().waitQt(std::chrono::milliseconds(50));
            }
            blocked.release();
        });
        // The test link wrapper pauses only the actual Core admission call,
        // after takeClaim has removed this claim from the owner's pending map.
        const auto accepted = owner.takeOutcome(claim, job.job, META_FETCHER_PROVIDER_LASTFM, gate.get());
        retiring.join();
        QVERIFY(entered); QVERIFY(!blocked.timedOut); QVERIFY(!joinedBeforeReturn);
        QVERIFY(!accepted); QVERIFY(!vlc_metadata_artwork_admission_IsCurrent(gate.get()));
        QVERIFY(parent.lifetime.handle().waitQt(std::chrono::seconds(3)));
    }
#endif
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
        QTest::addColumn<bool>("withdraw"); QTest::addColumn<bool>("outcome");
        QTest::newRow("allowed") << false << false; QTest::newRow("consent-withdrawn") << true << false;
        QTest::newRow("outcome-allowed") << false << true; QTest::newRow("outcome-consent-withdrawn") << true << true;
    }
    void aSealedFamilyClaimUsesItsSeparateMaterializer()
    {
        QFETCH(bool, withdraw); QFETCH(bool, outcome);
        Parent parent; QTemporaryDir cache;
        if (outcome) addon_test_native_provider_outcome(true, META_FETCHER_OUTCOME_MATCH);
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
        QString claim; const auto completedClaim = [&](const auto& value) { claim = value.claim; };
        QVERIFY(outcome ? owner.probeOutcome(job.job, META_FETCHER_PROVIDER_LASTFM, completedClaim)
                        : owner.probe(job.job, META_FETCHER_PROVIDER_LASTFM, completedClaim));
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
