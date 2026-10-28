// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <QtTest/QtTest>
#include <QBuffer>
#include <QImage>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <new>
#include <thread>
#include <utility>
#include <vlc/vlc.h>
#include <vlc_common.h>
#include <vlc_input_item.h>
#include <vlc_artwork_policy.h>
#include <vlc_metadata_owner.h>
#include "lib/libvlc_internal.h"
#include "addons/addon_native_metadata_owner.hpp"
#include "addons/addon_core_metadata_admission.hpp"
#include "fixtures/addon_native_provider_fixture.h"

namespace {
std::atomic<void*> retainedOwnerAllocation{nullptr};
std::atomic_bool retainedOwnerIsLive{false};
thread_local void* nextOwnerAllocation = nullptr;
}

// This test-only slot forces one real owner address to be reused. The malloc
// block remains allocated until both QObject lifetimes and their work end.
#if __SIZEOF_SIZE_T__ == 8
# define HAKONE_OWNER_REAL_NEW __real__Znwm
# define HAKONE_OWNER_WRAP_NEW __wrap__Znwm
# define HAKONE_OWNER_REAL_SIZED_DELETE __real__ZdlPvm
# define HAKONE_OWNER_WRAP_SIZED_DELETE __wrap__ZdlPvm
#elif __SIZEOF_SIZE_T__ == 4
# define HAKONE_OWNER_REAL_NEW __real__Znwj
# define HAKONE_OWNER_WRAP_NEW __wrap__Znwj
# define HAKONE_OWNER_REAL_SIZED_DELETE __real__ZdlPvj
# define HAKONE_OWNER_WRAP_SIZED_DELETE __wrap__ZdlPvj
#else
# error Unsupported test allocation ABI
#endif
extern "C" void* HAKONE_OWNER_REAL_NEW(std::size_t);
extern "C" void __real__ZdlPv(void*) noexcept;
extern "C" void HAKONE_OWNER_REAL_SIZED_DELETE(void*, std::size_t) noexcept;

extern "C" void* HAKONE_OWNER_WRAP_NEW(std::size_t size)
{
    if (nextOwnerAllocation && size == sizeof(AddonNativeMetadataOwner)) {
        void* result = std::exchange(nextOwnerAllocation, nullptr);
        if (retainedOwnerIsLive.exchange(true)) std::abort();
        return result;
    }
    return HAKONE_OWNER_REAL_NEW(size);
}
extern "C" void __wrap__ZdlPv(void* value) noexcept
{
    if (value && value == retainedOwnerAllocation.load()) { retainedOwnerIsLive = false; return; }
    __real__ZdlPv(value);
}
extern "C" void HAKONE_OWNER_WRAP_SIZED_DELETE(void* value, std::size_t size) noexcept
{
    if (value && value == retainedOwnerAllocation.load()) { retainedOwnerIsLive = false; return; }
    HAKONE_OWNER_REAL_SIZED_DELETE(value, size);
}

namespace {
class OwnerAllocation
{
public:
    OwnerAllocation() : m_block(std::malloc(sizeof(AddonNativeMetadataOwner))) {
        if (!m_block || retainedOwnerAllocation.exchange(m_block)) std::abort();
    }
    ~OwnerAllocation() {
        if (retainedOwnerIsLive || nextOwnerAllocation) std::abort();
        retainedOwnerAllocation = nullptr; std::free(m_block);
    }
    void arm() { nextOwnerAllocation = m_block; }
    void* address() const { return m_block; }
private:
    void* m_block;
};
struct Providers {
    struct Call { QString id, provider; };
    QVariantList rows; QList<Call> calls; QStringList canceled;
    AddonMetadataService::Backend backend() {
        AddonMetadataService::Backend result;
        result.providers.providersForFeature = [this](const QString&) { return rows; };
        result.providers.invoke = [this](const QString& provider, const QString&, const QVariantMap&) {
            const auto id = QStringLiteral("portable-%1").arg(calls.size() + 1); calls.append({id, provider}); return id;
        };
        result.providers.cancel = [this](const QString& id) { canceled.append(id); };
        return result;
    }
};
struct Reader : std::enable_shared_from_this<Reader> {
    QByteArray bytes;
    std::atomic_int calls{0}, finished{0};
    std::atomic_bool release{true};
    Reader() {
        QImage image(4, 4, QImage::Format_RGB32); image.fill(Qt::blue);
        QBuffer output(&bytes); output.open(QIODevice::WriteOnly); image.save(&output, "PNG");
    }
    AddonNativeArtworkAssets::Backend backend() {
        const auto self = shared_from_this();
        return {[self](const QUrl&, qint64, const std::function<bool()>& live) {
            ++self->calls;
            while (!self->release && live()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            ++self->finished; return AddonNativeArtworkAssets::ReadResult{self->bytes, {}};
        }};
    }
};
struct Fixture {
    libvlc_instance_t* core = nullptr;
    vlc::addons::AddonStreamLifetime lifetime;
    QTemporaryDir cache;
    Providers providers;
    std::shared_ptr<Reader> reader = std::make_shared<Reader>();
    std::unique_ptr<AddonMetadataService> service;
    std::unique_ptr<AddonNativeMetadataOwner> owner;
    explicit Fixture(bool activate = false, bool createOwner = true) {
        const char* args[]{"--ignore-config", "--no-plugins-scan", "--no-plugins-cache"};
        core = libvlc_new(3, args); if (!core) qFatal("The fixture cannot create its native parent.");
        addon_test_native_provider_queued(false); addon_test_native_provider_block_delete(false);
        service = std::make_unique<AddonMetadataService>(providers.backend());
        if (!service->bindApplicationLifetime(lifetime.handle())) qFatal("The fixture cannot bind its app lifetime.");
        service->setAutomaticNetworkAllowed(true);
        AddonNativeMetadataOwner::Options options;
        options.cacheDirectory = cache.path(); options.imageReader = reader->backend(); options.activateCoreOwner = activate;
        if (createOwner) owner = AddonNativeMetadataOwner::create(service.get(), nullptr, VLC_OBJECT(core->p_libvlc_int), options);
    }
    ~Fixture() {
        if (owner) owner->close(); if (service) service->shutdown(); lifetime.close();
        if (!lifetime.handle().waitQt(std::chrono::seconds(5))) qFatal("The fixture native work did not end.");
        owner.reset(); service.reset(); libvlc_release(core);
    }
};
struct Job {
    vlc_metadata_backend_t* backend;
    input_item_t* item;
    vlc_metadata_job_t* job;
    std::atomic_int ended{0}, status{VLC_EGENERIC};
    static void done(vlc_metadata_job_t*, int status, void* opaque) {
        auto& job = *static_cast<Job*>(opaque); job.status = status; ++job.ended;
    }
    Job(AddonMetadataService& service, const char* uri = "file:///native/song.flac", const char* art = nullptr,
        unsigned flags = VLC_METADATA_REQUEST_NETWORK | VLC_METADATA_REQUEST_AUTOMATIC)
        : backend(service.holdCoreBackend()), item(input_item_New(uri, "Native song")) {
        input_item_SetArtist(item, "Artist"); input_item_SetAlbum(item, "Album"); if (art) input_item_SetArtURL(item, art);
        const char* fields[]{"artwork"}; vlc_metadata_request request{};
        request.feature = "artwork"; request.operation = "enrich"; request.item = item;
        request.fields = fields; request.field_count = 1; request.flags = flags; request.generation = 17; request.target_revision = 23;
        job = vlc_metadata_job_New(backend, &request, done, this); if (!job) qFatal("The fixture cannot create its metadata job.");
    }
    ~Job() {
        vlc_metadata_job_Cancel(job); vlc_metadata_job_Release(job); vlc_metadata_backend_Release(backend); input_item_Release(item);
    }
};
QVariantMap portable()
{
    return {{"id", "org.videolan.musicbrainz/artwork"}, {"enabled", true}, {"configured", true},
        {"features", QStringList{"artwork"}}, {"cachePartition", "portable:release:account"}};
}
}

class TestAddonNativeMetadataOwner : public QObject
{
    Q_OBJECT
private slots:
    void actualOwnerRetirementRevokesItsRegisteredCoreGate_data()
    {
        QTest::addColumn<bool>("destroy");
        QTest::newRow("close") << false;
        QTest::newRow("destruction") << true;
    }
    void actualOwnerRetirementRevokesItsRegisteredCoreGate()
    {
        QFETCH(bool, destroy); Fixture f; QVERIFY(f.owner); QVERIFY(f.owner->ready());
        const auto capture = f.service->captureArtworkRegistrations(); QVERIFY(capture); QCOMPARE(capture->builtins.size(), 4);
        for (const auto& row : capture->builtins) QVERIFY(row.available);
        const auto gate = vlc::addons::CoreMetadataAdmission::create(vlc_tick_now() + VLC_TICK_FROM_SEC(10));
        QVERIFY(gate); QVERIFY(capture->registry->arm(capture->token, gate));
        if (destroy) f.owner.reset(); else f.owner->close();
        QVERIFY(!gate->current()); QVERIFY(!capture->token.current());
        const auto fresh = f.service->captureArtworkRegistrations(); QVERIFY(fresh); QVERIFY(fresh->builtins.isEmpty());
    }
    void aFactoryRequiresTheBoundAppAndRejectsASecondOwner()
    {
        Fixture fixture; QVERIFY(fixture.owner); QVERIFY(fixture.owner->ready()); QVERIFY(!fixture.owner->active());
        AddonNativeMetadataOwner::Options options; options.cacheDirectory = fixture.cache.path(); options.imageReader = fixture.reader->backend();
        QVERIFY(!AddonNativeMetadataOwner::create(fixture.service.get(), nullptr, VLC_OBJECT(fixture.core->p_libvlc_int), options));
        QVERIFY(fixture.owner->ready());
        bool owned = true; auto* backend = vlc_metadata_owner_Hold(VLC_OBJECT(fixture.core->p_libvlc_int), &owned);
        QVERIFY(!backend); QVERIFY(!owned);
        Providers providers; AddonMetadataService unbound(providers.backend());
        QVERIFY(!AddonNativeMetadataOwner::create(&unbound, nullptr, VLC_OBJECT(fixture.core->p_libvlc_int), options));
    }
    void theRealServiceAndNativeBridgePublishOnlyArtworkAndHonestCredit()
    {
        Fixture fixture; QVERIFY(fixture.owner); Job job(*fixture.service);
        QCOMPARE(vlc_metadata_job_Start(job.job), VLC_SUCCESS); QTRY_COMPARE(job.ended.load(), 1); QCOMPARE(job.status.load(), VLC_SUCCESS);
        const auto* result = vlc_metadata_job_GetResult(job.job); QVERIFY(result && result->item); QCOMPARE(result->field_count, size_t(1));
        QCOMPARE(result->fields[0].type, vlc_meta_ArtworkURL); QCOMPARE(fixture.reader->calls.load(), 1);
        const auto proof = QJsonDocument::fromJson(QByteArray(result->provenance_json, result->provenance_size)).toVariant().toMap();
        const auto art = proof.value("provenance").toMap().value("artwork").toMap();
        QCOMPARE(art.value("provider").toString(), QStringLiteral("hakone.builtin.musicbrainz/artwork"));
        QCOMPARE(art.value("nativeFamily").toString(), QStringLiteral("musicbrainz")); QVERIFY(!art.value("strong").toBool());
        QCOMPARE(art.value("compatibilityPolicy").toString(), QStringLiteral("native-family-artwork@1"));
        const auto credit = art.value("attribution").toMap();
        QCOMPARE(credit.value("name").toString(), QStringLiteral("MusicBrainz, Cover Art Archive, Amazon"));
        QCOMPARE(credit.value("url").toString(), QStringLiteral("https://musicbrainz.org/"));
        const auto json = QByteArray(result->provenance_json, result->provenance_size);
        QVERIFY(!json.contains("nativeClaim")); QVERIFY(!json.contains("file:///native/song.flac"));
        QCOMPARE(vlc_metadata_job_Apply(job.job, job.item, 0, nullptr), VLC_SUCCESS);
        char* uri = vlc_artwork_item_GetURI(job.item); QVERIFY(uri); QVERIFY(QUrl::fromEncoded(uri).isLocalFile()); free(uri);
        char* artist = input_item_GetArtist(job.item); QCOMPARE(QByteArray(artist), QByteArray("Artist")); free(artist);
    }
    void sourceAndManualRequestsChoosePortableBeforeNativeExecution_data()
    {
        QTest::addColumn<bool>("scoped"); QTest::newRow("source") << true; QTest::newRow("manual") << false;
    }
    void sourceAndManualRequestsChoosePortableBeforeNativeExecution()
    {
        QFETCH(bool, scoped); Fixture fixture; QVERIFY(fixture.owner); fixture.providers.rows = {portable()};
        Job job(*fixture.service, scoped ? "HaKoNe-AdDoN-SoUrCe:v1:import" : "file:///native/song.flac", nullptr,
            VLC_METADATA_REQUEST_NETWORK | (scoped ? VLC_METADATA_REQUEST_AUTOMATIC : 0));
        QCOMPARE(vlc_metadata_job_Start(job.job), VLC_SUCCESS); QTRY_COMPARE(fixture.providers.calls.size(), 1);
        const auto call = fixture.providers.calls.first(); QCOMPARE(call.provider, QStringLiteral("org.videolan.musicbrainz/artwork"));
        fixture.service->operationFinished(call.id, call.provider, "no_match", {}, {});
        QTRY_COMPARE(job.ended.load(), 1); QCOMPARE(fixture.reader->calls.load(), 0);
        QCOMPARE(vlc_metadata_job_GetResult(job.job)->field_count, size_t(0));
    }
    void currentUserArtworkNeverStartsTheNativeImageReader()
    {
        Fixture fixture; QVERIFY(fixture.owner); Job job(*fixture.service, "file:///native/song.flac", "file:///native/user.png");
        QCOMPARE(vlc_metadata_job_Start(job.job), VLC_SUCCESS); QTRY_COMPARE(job.ended.load(), 1);
        QCOMPARE(fixture.reader->calls.load(), 0); QCOMPARE(vlc_metadata_job_GetResult(job.job)->field_count, size_t(0));
        char* art = input_item_GetArtURL(job.item); QCOMPARE(QByteArray(art), QByteArray("file:///native/user.png")); free(art);
    }
    void nativeCancellationStopsImageWorkWithoutQtDelivery()
    {
        Fixture fixture; QVERIFY(fixture.owner); fixture.reader->release = false; Job job(*fixture.service);
        QCOMPARE(vlc_metadata_job_Start(job.job), VLC_SUCCESS); QTRY_COMPARE(fixture.reader->calls.load(), 1);
        bool endedWithoutQt = false;
        std::thread native([&] {
            vlc_metadata_job_Cancel(job.job);
            const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (!fixture.reader->finished && std::chrono::steady_clock::now() < limit)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            endedWithoutQt = fixture.reader->finished.load() == 1;
        });
        native.join(); QVERIFY(endedWithoutQt); QCOMPARE(job.ended.load(), 1); QVERIFY(job.status.load() != VLC_SUCCESS);
        QVERIFY(!input_item_GetArtURL(job.item));
    }
    void nativeCancellationStopsTheFamilyProbeWithoutQtDelivery()
    {
        Fixture fixture; QVERIFY(fixture.owner); addon_test_native_provider_queued(true); Job job(*fixture.service);
        QCOMPARE(vlc_metadata_job_Start(job.job), VLC_SUCCESS); QTRY_COMPARE(addon_test_native_provider_pending(), 1u);
        bool probeEndedWithoutQt = false;
        std::thread native([&] {
            vlc_metadata_job_Cancel(job.job);
            probeEndedWithoutQt = addon_test_native_provider_pending() == 0;
        });
        native.join(); QVERIFY(probeEndedWithoutQt); QCOMPARE(job.ended.load(), 1);
        QCOMPARE(job.status.load(), -EINTR); QCOMPARE(fixture.reader->calls.load(), 0);
    }
    void anOldInterfaceOwnerCannotCloseItsSuccessor()
    {
        Fixture fixture(true); QVERIFY(fixture.owner); QVERIFY(fixture.owner->active());
        fixture.owner->close(); QVERIFY(fixture.lifetime.handle().waitQt(std::chrono::seconds(3)));
        auto replacement = std::make_unique<AddonMetadataService>(fixture.providers.backend());
        QVERIFY(replacement->bindApplicationLifetime(fixture.lifetime.handle())); replacement->setAutomaticNetworkAllowed(true);
        AddonNativeMetadataOwner::Options options; options.cacheDirectory = fixture.cache.path();
        options.imageReader = fixture.reader->backend(); options.activateCoreOwner = true;
        auto owner = AddonNativeMetadataOwner::create(replacement.get(), nullptr, VLC_OBJECT(fixture.core->p_libvlc_int), options);
        QVERIFY(owner); QVERIFY(owner->active()); fixture.owner.reset();
        bool owned = false; auto* current = vlc_metadata_owner_Hold(VLC_OBJECT(fixture.core->p_libvlc_int), &owned);
        auto* expected = replacement->holdCoreBackend(); QVERIFY(owned); QCOMPARE(current, expected); QVERIFY(!vlc_metadata_backend_IsClosed(current));
        vlc_metadata_backend_Release(current); vlc_metadata_backend_Release(expected);
        owner->close(); replacement->shutdown(); QVERIFY(fixture.lifetime.handle().waitQt(std::chrono::seconds(3)));
    }
    void reentrantOwnerDeletionCannotWithdrawAReusedAddress()
    {
        OwnerAllocation storage; Fixture fixture(false, false);
        AddonNativeMetadataOwner::Options options; options.cacheDirectory = fixture.cache.path();
        options.imageReader = fixture.reader->backend(); storage.arm();
        fixture.owner = AddonNativeMetadataOwner::create(fixture.service.get(), nullptr, VLC_OBJECT(fixture.core->p_libvlc_int), options);
        QVERIFY(fixture.owner); QCOMPARE(static_cast<void*>(fixture.owner.get()), storage.address());
        struct Reentry {
            Fixture& fixture; OwnerAllocation& storage; AddonNativeMetadataOwner::Options options;
            std::unique_ptr<AddonNativeMetadataOwner> replacement;
            unsigned calls = 0; bool joined = false;
            static void ended(vlc_metadata_job_t*, int, void* opaque) {
                auto& value = *static_cast<Reentry*>(opaque); ++value.calls;
                value.fixture.owner.reset();
                value.joined = value.fixture.lifetime.handle().waitQt(std::chrono::seconds(3));
                if (!value.joined) return;
                value.storage.arm();
                value.replacement = AddonNativeMetadataOwner::create(value.fixture.service.get(), nullptr,
                    VLC_OBJECT(value.fixture.core->p_libvlc_int), value.options);
            }
        } reentry{fixture, storage, options};
        const auto backend = std::shared_ptr<vlc_metadata_backend_t>(fixture.service->holdCoreBackend(), vlc_metadata_backend_Release);
        vlc_metadata_request request{}; request.feature = "native.transaction"; request.operation = "anchor";
        request.flags = VLC_METADATA_REQUEST_NETWORK | VLC_METADATA_REQUEST_AUTOMATIC;
        const auto anchor = std::shared_ptr<vlc_metadata_job_t>(
            vlc_metadata_job_New(backend.get(), &request, Reentry::ended, &reentry), vlc_metadata_job_Release);
        QVERIFY(anchor); QCOMPARE(vlc_metadata_job_StartLocal(anchor.get()), VLC_SUCCESS);
        fixture.owner->close();
        QCOMPARE(reentry.calls, 1u); QVERIFY(reentry.joined); QVERIFY(reentry.replacement);
        QCOMPARE(static_cast<void*>(reentry.replacement.get()), storage.address());
        QVERIFY(reentry.replacement->ready()); QVERIFY(fixture.service->nativeOwnerIs(reentry.replacement.get()));
        fixture.owner = std::move(reentry.replacement);
        Job job(*fixture.service); QCOMPARE(vlc_metadata_job_Start(job.job), VLC_SUCCESS);
        QTRY_COMPARE(job.ended.load(), 1); QCOMPARE(job.status.load(), VLC_SUCCESS); QCOMPARE(fixture.reader->calls.load(), 1);
    }
};
QTEST_GUILESS_MAIN(TestAddonNativeMetadataOwner)
#include "test_addon_native_metadata_owner.moc"
