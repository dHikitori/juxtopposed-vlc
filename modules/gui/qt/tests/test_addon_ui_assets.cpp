// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include "addon_test_application_work.hpp"
#include <QSignalSpy>
#include <QGuiApplication>
#include <QBuffer>
#include <QImageReader>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QSemaphore>
#include <QScopeGuard>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtEndian>
#include <vector>
#include <atomic>
#include <thread>
#include <QTimer>
#include "addons/addon_ui_assets.hpp"
#include "addons/addon_asset_image_provider.hpp"
#include "addons/addon_ui_registry.hpp"
#include "addon_ui_asset_io_probe.hpp"

using namespace vlc::addons;
namespace {
QByteArray raster(const char* format = "PNG", int width = 4, int height = 4, QColor color = Qt::red)
{
    QImage image(width, height, QImage::Format_ARGB32); image.fill(color); image.setText(QStringLiteral("Comment"), QStringLiteral("Private image metadata"));
    QByteArray bytes; QBuffer output(&bytes); output.open(QIODevice::WriteOnly); image.save(&output, format); return bytes;
}
struct Fake {
    QString identity = QStringLiteral("release-1/account-1/grant-1");
    bool enabled = true;
    QTemporaryDir notificationDirectory;
    StoreAuthority notifications{notificationDirectory.path()};
    std::shared_ptr<std::atomic_uint> prepares=std::make_shared<std::atomic_uint>(0),reads=std::make_shared<std::atomic_uint>(0),liveChecks=std::make_shared<std::atomic_uint>(0);
    std::shared_ptr<std::atomic<unsigned>> epoch = std::make_shared<std::atomic<unsigned>>(0);
    QMap<QString, QByteArray> images{{QStringLiteral("cover"), raster()}};
    void setIdentity(const QString& value) { identity = value; ++*epoch; notifications.invalidate(); }
    void setEnabled(bool value) { enabled = value; ++*epoch; notifications.invalidate(); }
    AddonUiAssets::Backend backend()
    {
        return {[this](const QString&) {
            ++*prepares;QVariantMap assets; for (auto it = images.begin(); it != images.end(); ++it) {
                const QString path = it.key() + QStringLiteral(".png"); assets.insert(it.key(), QVariantMap{{"path", path}});
            }
            UiAssetCapture captured;
            captured.registration = {{"packageId", "org.example.images"}, {"enabled", enabled}, {"configured", true},
                {"accountReference", "account-1"}, {"assetStamp", identity}, {"assets", assets}};
            captured.assets = images;
            captured.current = [epoch = epoch, expected = epoch->load()] { return epoch->load() == expected; };
            captured.live = [current=captured.current,checks=liveChecks] {++*checks;return current();};
            return [captured,count=reads] {++*count;return captured;};
        },{},[source=notifications](std::function<void()> wake){return source.onChanged(std::move(wake));}};
    }
};

void write(const QString& path, const QByteArray& bytes)
{
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), bytes.size());
}
QJsonObject manifest()
{
    return {{"formatVersion", 1}, {"id", "org.example.images"}, {"name", "Image example"}, {"version", "1.0.0"},
        {"publisher", "Tests"}, {"license", "GPL-2.0-or-later"}, {"source", "https://example.org/images.git"},
        {"api", QJsonObject{{"addon", "^1.0.0"}, {"contracts", QJsonObject{{"ui", "^1.0.0"}}}}},
        {"files", QJsonArray{"addon.json", "LICENSE", "view.json", "cover.png"}},
        {"contributions", QJsonArray{QJsonObject{{"id", "view"}, {"type", "ui.section"}, {"contract", "ui@1"},
            {"hook", "home.sections@1"}, {"view", "view.json"},
            {"assets", QJsonObject{{"cover", QJsonObject{{"path", "cover.png"}}}}}}}},
        {"permissions", QJsonObject{{"required", QJsonObject{{"ui.contribute", QJsonObject{{"hooks", QJsonArray{"home.sections@1"}}}}}}}}};
}
void package(const QString& directory, const QJsonObject& value = manifest())
{
    write(directory + QStringLiteral("/addon.json"), QJsonDocument(value).toJson());
    write(directory + QStringLiteral("/LICENSE"), "Test fixture\n"); write(directory + QStringLiteral("/cover.png"), raster());
    write(directory + QStringLiteral("/view.json"), QJsonDocument(QJsonObject{{"contract", "ui@1"}, {"id", "image-view"},
        {"root", QJsonObject{{"type", "Column"}, {"children", QJsonArray{QJsonObject{{"type", "Image"}, {"artwork", "asset:cover"}, {"text", "Cover"}}}}}}}).toJson());
}
QString providerId(const QString& url) { return url.mid(QStringLiteral("image://addonassets/").size()); }
QByteArray pngChunk(const QByteArray& type, const QByteArray& payload)
{
    QByteArray result(4, '\0'); qToBigEndian<quint32>(quint32(payload.size()), result.data());
    const QByteArray body = type + payload; result.append(body); quint32 crc = 0xffffffff;
    for (const auto value : body) {
        crc ^= quint8(value);
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
    }
    QByteArray checksum(4, '\0'); qToBigEndian<quint32>(~crc, checksum.data()); result.append(checksum); return result;
}
QQuickItem* visualItem(QQuickItem* root, const QString& name)
{
    if (root->objectName() == name) return root;
    for (auto* child : root->childItems()) if (auto* found = visualItem(child, name)) return found;
    return nullptr;
}
}

class TestAddonUiAssets : public QObject
{
    Q_OBJECT
private slots:
    void idleScopesDoNotPollAuthority_data()
    {
        QTest::addColumn<int>("mode");QTest::newRow("no-scope")<<0;QTest::newRow("static-no-images")<<1;QTest::newRow("static-one-image")<<2;
    }
    void idleScopesDoNotPollAuthority()
    {
        QFETCH(int,mode);Fake fake;if(mode==1)fake.images.clear();AddonUiAssets assets(fake.backend(),{});
        if(mode) {QVERIFY(assets.openSession("idle","example/view"));QTRY_VERIFY(!assets.sessionLoading("idle"));}
        QTRY_COMPARE(AddonStreamLifetime::applicationHandle().qtWorkCount(),0u);
        QCoreApplication::sendPostedEvents(nullptr,QEvent::MetaCall);QCoreApplication::processEvents();QTest::qWait(20);
        const auto checks=fake.liveChecks->load(),prepares=fake.prepares->load(),reads=fake.reads->load();
        QSignalSpy changed(&assets,&AddonUiAssets::sessionAssetsChanged),budget(&assets,&AddonUiAssets::imageBudgetChanged);
        QVERIFY(!assets.statistics().value("startRetryPending").toBool());QTest::qWait(350);
        QCOMPARE(fake.liveChecks->load(),checks);QCOMPARE(fake.prepares->load(),prepares);QCOMPARE(fake.reads->load(),reads);
        QCOMPARE(changed.size(),0);QCOMPARE(budget.size(),0);QVERIFY(!assets.statistics().value("startRetryPending").toBool());
        QCOMPARE(assets.statistics().value("jobs").toInt(),0);QCOMPARE(assets.statistics().value("queuedJobs").toInt(),0);
    }
    void nativeNoticesOnlyCloseStaleScopes()
    {
        Fake fake;auto backend=fake.backend();const auto prepare=backend.prepare;
        const auto first=std::make_shared<std::atomic_bool>(true),second=std::make_shared<std::atomic_bool>(true);
        backend.prepare=[prepare,first,second](const QString& id){const auto reader=prepare(id);return [reader,live=id==QLatin1String("first/view")?first:second]{auto capture=reader();capture.live=[live]{return live->load();};return capture;};};
        AddonUiAssets assets(backend,{});AddonAssetImageProvider provider(&assets);QObject a,b;
        QVERIFY(assets.openSession("first","first/view"));QVERIFY(assets.openSession("second","second/view"));
        QTRY_COMPARE(assets.sessionAssets("first").size(),1);QTRY_COMPARE(assets.sessionAssets("second").size(),1);
        const auto claimA=assets.claimImage("first",&a),claimB=assets.claimImage("second",&b);
        const auto urlA=assets.assetUrlForClaim("first","asset:cover",claimA),urlB=assets.assetUrlForClaim("second","asset:cover",claimB);
        QTRY_COMPARE(AddonStreamLifetime::applicationHandle().qtWorkCount(),0u);const auto reads=fake.reads->load();
        fake.notifications.displayUnlocked();QTest::qWait(20);QCOMPARE(assets.statistics().value("claims").toInt(),2);QCOMPARE(fake.reads->load(),reads);
        first->store(false);fake.notifications.displayUnlocked();
        QVERIFY(provider.requestImage(providerId(urlA),nullptr,{}).isNull());QTRY_COMPARE(assets.statistics().value("claims").toInt(),1);
        QVERIFY(!provider.requestImage(providerId(urlB),nullptr,{}).isNull());QCOMPARE(assets.assetUrlForClaim("second","asset:cover",claimB),urlB);QCOMPARE(fake.reads->load(),reads);
    }
    void nativeStoreChangesRevokeWithoutManagerRevision()
    {
        QTemporaryDir source,storage;package(source.path());AddonManager manager(storage.path());QVERIFY(manager.installLocalPackage(source.path(),true));QVERIFY(manager.setEnabled("org.example.images",true));
        AddonUiAssets assets(&manager);AddonAssetImageProvider provider(&assets);QObject owner;QVERIFY(assets.openSession("native","org.example.images/view"));QTRY_COMPARE(assets.sessionAssets("native").size(),1);
        const auto claim=assets.claimImage("native",&owner),url=assets.assetUrlForClaim("native","asset:cover",claim);QVERIFY(!provider.requestImage(providerId(url),nullptr,{}).isNull());
        QSignalSpy revisions(&manager,&AddonManager::revisionChanged);PackageStore store(storage.path());QVERIFY(store.setEnabled("org.example.images",false));QCOMPARE(revisions.size(),0);
        QVERIFY(provider.requestImage(providerId(url),nullptr,{}).isNull());QTRY_COMPARE(assets.statistics().value("claims").toInt(),0);QVERIFY(assets.sessionAssets("native").isEmpty());QCOMPARE(revisions.size(),0);
    }
    void notificationRefusalDoesNotStartAnUnwatchedSession()
    {
        Fake fake;auto backend=fake.backend();backend.subscribe=[](std::function<void()>){return StoreAuthorityChanges{};};AddonUiAssets assets(backend,{});
        QVERIFY(!assets.openSession("refused","example/view"));QVERIFY(!assets.errorMessage().isEmpty());QCOMPARE(fake.prepares->load(),0u);QCOMPARE(fake.reads->load(),0u);
        QVERIFY(!assets.statistics().value("startRetryPending").toBool());
    }
    void nativeNoticeCanOutliveTheClosedQtOwner()
    {
        Fake fake;auto backend=fake.backend();const auto subscribe=backend.subscribe;std::function<void()> late;
        backend.subscribe=[&late,subscribe](std::function<void()> wake){late=wake;return subscribe(std::move(wake));};
        auto assets=std::make_unique<AddonUiAssets>(backend,AddonUiAssets::Options{});QVERIFY(bool(late));assets.reset();
        std::thread thread([late]{for(unsigned i=0;i<100;++i)late();});thread.join();QCoreApplication::processEvents();
    }
    void refusedWorkStartsStopAfterABoundedRetry()
    {
        const auto lifetime=AddonStreamLifetime::applicationHandle();QTRY_COMPARE(lifetime.qtWorkCount(),0u);
        struct Control {QSemaphore entered,release;};const auto control=std::make_shared<Control>();
        const auto cleanup=qScopeGuard([control]{control->release.release(AddonStreamLifetime::QtWorkLimit);});
        for(unsigned i=0;i<AddonStreamLifetime::QtWorkLimit;++i)QVERIFY(lifetime.startQtWork([control]{control->entered.release();control->release.acquire();},[control]{control->release.release(AddonStreamLifetime::QtWorkLimit);}));
        QTRY_COMPARE(control->entered.available(),int(AddonStreamLifetime::QtWorkLimit));Fake fake;AddonUiAssets::Options options;options.maximumStartRetries=2;AddonUiAssets assets(fake.backend(),options);
        QVERIFY(assets.openSession("busy","example/view"));QVERIFY(assets.statistics().value("startRetryPending").toBool());
        QTRY_VERIFY(!assets.sessionLoading("busy"));QVERIFY(!assets.errorMessage().isEmpty());QCOMPARE(fake.reads->load(),0u);QCOMPARE(assets.statistics().value("queuedJobs").toInt(),0);QVERIFY(!assets.statistics().value("startRetryPending").toBool());
        control->release.release(AddonStreamLifetime::QtWorkLimit);QTRY_COMPARE(lifetime.qtWorkCount(),0u);
        QVERIFY(assets.openSession("retry","example/view"));QTRY_COMPARE(assets.sessionAssets("retry").size(),1);QVERIFY(!assets.statistics().value("startRetryPending").toBool());
    }
    void staticRasterFormats_data()
    {
        QTest::addColumn<QByteArray>("format"); QTest::addColumn<QString>("suffix");
        QTest::newRow("png") << QByteArray("PNG") << QStringLiteral("png");
        QTest::newRow("jpeg") << QByteArray("JPEG") << QStringLiteral("jpg");
        QTest::newRow("webp") << QByteArray("WEBP") << QStringLiteral("webp");
    }
    void staticRasterFormats()
    {
        QFETCH(QByteArray, format); QFETCH(QString, suffix);
        const auto bytes = raster(format.constData()); QVERIFY(!bytes.isEmpty()); QString error;
        const auto image = AddonUiAssets::decodeImage(bytes, suffix, {}, &error);
        QVERIFY2(!image.isNull(), qPrintable(error)); QCOMPARE(image.size(), QSize(4, 4));
        QCOMPARE(image.format(), QImage::Format_ARGB32_Premultiplied); QVERIFY(image.textKeys().isEmpty());
    }
    void activeSpoofedTruncatedAndOversizeImagesFail()
    {
        const AddonUiAssets::Options options;
        const QByteArray svg("<svg xmlns='http://www.w3.org/2000/svg'><image href='https://image.invalid/a'/><image href='file:///private'/></svg>");
        QVERIFY(AddonUiAssets::decodeImage(svg, "png", options).isNull());
        QVERIFY(AddonUiAssets::decodeImage(raster(), "jpg", options).isNull());
        QVERIFY(AddonUiAssets::decodeImage(raster(), "svg", options).isNull());
        QVERIFY(AddonUiAssets::decodeImage(raster().chopped(1), "png", options).isNull());
        QVERIFY(AddonUiAssets::decodeImage(raster("PNG", 1025, 1), "png", options).isNull());
        QVERIFY(AddonUiAssets::decodeImage(QByteArray(1024 * 1024 + 1, 'x'), "png", options).isNull());
        auto animated = raster();
        animated.insert(33, QByteArray::fromHex("000000086163544c000000020000000000000000"));
        QVERIFY(AddonUiAssets::decodeImage(animated, "png", options).isNull());
        auto webp = raster("WEBP"); QVERIFY(!webp.isEmpty()); webp.append(QByteArray::fromHex("414e494d06000000000000000000"));
        qToLittleEndian<quint32>(quint32(webp.size() - 8), webp.data() + 4);
        QVERIFY(AddonUiAssets::decodeImage(webp, "webp", options).isNull());
        const auto scaled = AddonUiAssets::decodeImage(raster("PNG", 800, 400), "png", options);
        QCOMPARE(scaled.size(), QSize(512, 256)); QVERIFY(scaled.sizeInBytes() <= 512 * 512 * 4);
    }
    void compressedAndLargeMetadataFailBeforeDecode()
    {
        auto compressed = raster(); QByteArray text("note\0", 5); text.append('\0'); text.append(qCompress(QByteArray(256 * 1024, 'x')).mid(4));
        compressed.insert(33, pngChunk("zTXt", text));
        QBuffer input(&compressed); QVERIFY(input.open(QIODevice::ReadOnly)); QImageReader control(&input, "png");
        QVERIFY(!control.read().isNull());
        QVERIFY(AddonUiAssets::decodeImage(compressed, "png", {}).isNull());
        auto large = raster(); large.insert(33, pngChunk("tEXt", QByteArray("note\0", 5) + QByteArray(20 * 1024, 'x')));
        QVERIFY(AddonUiAssets::decodeImage(large, "png", {}).isNull());
        auto jpeg = raster("JPEG"); QByteArray marker = QByteArray::fromHex("ffe3"); QByteArray length(2, '\0');
        qToBigEndian<quint16>(quint16(20 * 1024 + 2), length.data()); marker.append(length); marker.append(QByteArray(20 * 1024, 'x')); jpeg.insert(2, marker);
        QVERIFY(AddonUiAssets::decodeImage(jpeg, "jpg", {}).isNull());
        auto webp = raster("WEBP"); webp.append("XMP "); QByteArray webpLength(4, '\0');
        qToLittleEndian<quint32>(20 * 1024, webpLength.data()); webp.append(webpLength); webp.append(QByteArray(20 * 1024, 'x'));
        qToLittleEndian<quint32>(quint32(webp.size() - 8), webp.data() + 4);
        QVERIFY(AddonUiAssets::decodeImage(webp, "webp", {}).isNull());
    }
    void ticketsNeedAClaimFromTheSameLiveSession()
    {
        Fake fake; AddonUiAssets assets(fake.backend(), {}); AddonAssetImageProvider provider(&assets);
        QVERIFY(assets.openSession("first", "example/view")); QVERIFY(assets.openSession("second", "example/view"));
        QTRY_COMPARE(assets.sessionAssets("first").size(), 1); QTRY_COMPARE(assets.sessionAssets("second").size(), 1);
        QObject owner, other; const auto claim = assets.claimImage("first", &owner), foreign = assets.claimImage("second", &other);
        const auto url = assets.assetUrlForClaim("first", "asset:cover", claim); QVERIFY(!url.isEmpty());
        QSize size; const auto image = provider.requestImage(providerId(url), &size, QSize(10000, 10000));
        QCOMPARE(size, QSize(4, 4)); QCOMPARE(image.pixelColor(1, 1), QColor(Qt::red));
        QVERIFY(provider.requestImage(providerId(assets.sessionAssets("first").value("asset:cover").toString()), &size, {}).isNull());
        QVERIFY(assets.assetUrlForClaim("first", "asset:cover", foreign).isEmpty());
        const auto ticket = providerId(url).section('/', 0, 0);
        QVERIFY(provider.requestImage(ticket + '/' + foreign, &size, {}).isNull());
        for (const auto& suffix : {QStringLiteral("?file=/private"), QStringLiteral("#fragment"), QStringLiteral("/extra")})
            QVERIFY(provider.requestImage(providerId(url) + suffix, &size, {}).isNull());
        assets.closeSession("first"); QVERIFY(provider.requestImage(providerId(url), &size, {}).isNull());
        QVERIFY(assets.openSession("first", "example/view")); QTRY_COMPARE(assets.sessionAssets("first").size(), 1);
        const auto next = assets.claimImage("first", &owner); QVERIFY(assets.assetUrlForClaim("first", "asset:cover", next) != url);
        assets.suspendSession("first", true); QCOMPARE(assets.statistics().value("claims").toInt(), 1);
        QVERIFY(assets.sessionAssets("first").isEmpty()); QVERIFY(provider.requestImage(providerId(url), &size, {}).isNull());
        assets.suspendSession("first", false); QTRY_COMPARE(assets.sessionAssets("first").size(), 1);
        assets.invalidatePackage("org.example.images"); QVERIFY(assets.sessionAssets("second").isEmpty());
        QCOMPARE(assets.statistics().value("claims").toInt(), 0);
    }
    void ownerDestructionAndImageLimitsReleaseSlots()
    {
        Fake fake; AddonUiAssets::Options limits; limits.maximumSessionImages = 1; limits.maximumImages = 2;
        AddonUiAssets assets(fake.backend(), limits);
        for (const auto& session : {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}) QVERIFY(assets.openSession(session, "example/view"));
        auto* first = new QObject; QObject duplicate, second, third;
        QVERIFY(!assets.claimImage("a", first).isEmpty()); QVERIFY(assets.claimImage("a", &duplicate).isEmpty());
        QVERIFY(!assets.claimImage("b", &second).isEmpty()); QVERIFY(assets.claimImage("c", &third).isEmpty());
        delete first; QCOMPARE(assets.statistics().value("claims").toInt(), 1);
        QVERIFY(!assets.claimImage("c", &third).isEmpty());
        QVERIFY(assets.claimImage("a", &third).isEmpty());
    }
    void nativeCompositionUsesItsTrustedProfileAndRevokesSnapshots()
    {
        Fake fake; fake.images["cover"] = raster("PNG", 800, 400);
        AddonUiAssets::Options nativeProfile; nativeProfile.maximumOutputDimension = 1024;
        AddonUiAssets native(fake.backend(), nativeProfile), ui(fake.backend(), {});
        QVERIFY(native.openSession("native", "example/view")); QVERIFY(ui.openSession("ui", "example/view"));
        QTRY_VERIFY(!native.sessionLoading("native") && native.sessionPixels("native").size() == 1);
        QTRY_VERIFY(!ui.sessionLoading("ui") && ui.sessionPixels("ui").size() == 1);
        QCOMPARE(native.sessionPixels("native").value("cover").size(), QSize(800, 400));
        QCOMPARE(ui.sessionPixels("ui").value("cover").size(), QSize(512, 256));
        QCOMPARE(native.sessionRegistration("native").value("assetStamp").toString(), fake.identity);
        QCOMPARE(native.metaObject()->indexOfMethod("sessionPixels(QString)"), -1);
        QCOMPARE(native.metaObject()->indexOfMethod("sessionRegistration(QString)"), -1);
        native.closeSession("native"); QVERIFY(native.sessionPixels("native").isEmpty());
        QVERIFY(native.sessionRegistration("native").isEmpty()); QVERIFY(!native.sessionLoading("native"));
    }
    void imageOwnerLimitCoversAllServices()
    {
        Fake fake; AddonUiAssets first(fake.backend(), {}), second(fake.backend(), {});
        QVERIFY(first.openSession("a", "example/view")); QVERIFY(first.openSession("b", "example/view"));
        QVERIFY(second.openSession("c", "example/view"));
        std::vector<std::unique_ptr<QObject>> owners;
        for (int index = 0; index < 64; ++index) {
            owners.push_back(std::make_unique<QObject>());
            QVERIFY(!first.claimImage(index < 32 ? "a" : "b", owners.back().get()).isEmpty());
        }
        QObject waiting; QVERIFY(second.claimImage("c", &waiting).isEmpty());
        QCOMPARE(second.statistics().value("globalClaims").toInt(), 64);
        QSignalSpy available(&second, &AddonUiAssets::imageBudgetChanged);
        owners.pop_back(); QTRY_VERIFY(available.count() > 0);
        QVERIFY(!second.claimImage("c", &waiting).isEmpty());
        first.close(); QCOMPARE(second.statistics().value("globalClaims").toInt(), 1);
        second.close(); QCOMPARE(second.statistics().value("globalClaims").toInt(), 0);
    }
    void staleDecodeAndClosedScopesCannotPublish()
    {
        Fake fake; AddonUiAssets assets(fake.backend(), {});
        QVERIFY(assets.openSession("old", "example/view")); fake.setIdentity("release-2/account-2/grant-2");
        QTRY_COMPARE(assets.statistics().value("jobs").toInt(), 0); QVERIFY(assets.sessionAssets("old").isEmpty());
        QCOMPARE(assets.statistics().value("cacheBytes").toLongLong(), 0);
        QVERIFY(assets.openSession("closed", "example/view")); assets.closeSession("closed");
        QTRY_COMPARE(assets.statistics().value("jobs").toInt(), 0); QVERIFY(assets.sessionAssets("closed").isEmpty());
        QVERIFY(assets.openSession("disabled", "example/view")); fake.setEnabled(false); assets.invalidate();
        QTRY_COMPARE(assets.statistics().value("jobs").toInt(), 0); QVERIFY(assets.sessionAssets("disabled").isEmpty());
    }
    void authorityChangeBetweenWorkerGuardAndDelivery_data()
    { QTest::addColumn<bool>("warm"); QTest::newRow("cold") << false; QTest::newRow("warm-cache") << true; }
    void authorityChangeBetweenWorkerGuardAndDelivery()
    {
        QFETCH(bool, warm);
        struct Control { QSemaphore checked; std::atomic_bool armed{false}, live{true}; };
        const auto control = std::make_shared<Control>(); Fake fake; auto backend = fake.backend(); const auto prepare = backend.prepare;
        backend.prepare = [prepare, control](const QString& id) {
            const auto reader = prepare(id);
            return [reader, control] {
                auto capture = reader(); capture.live = [control] { return control->live.load(); };
                capture.current = [control] { if (control->armed.load()) control->checked.release(); return true; };
                return capture;
            };
        };
        AddonUiAssets assets(backend, {});
        if (warm) {
            QVERIFY(assets.openSession("warm", "example/view")); QTRY_COMPARE(assets.sessionPixels("warm").size(), 1);
            assets.closeSession("warm"); QVERIFY(assets.statistics().value("cacheBytes").toLongLong() > 0);
        }
        QTRY_COMPARE(assets.statistics().value("globalIntegrityCaptures").toInt(), 0);
        control->armed.store(true); QVERIFY(assets.openSession("late", "example/view"));
        // No GUI event can deliver the queued result before this guard changes.
        QVERIFY(control->checked.tryAcquire(1, 5000)); control->live.store(false);
        QTRY_COMPARE(assets.statistics().value("jobs").toInt(), 0);
        QVERIFY(assets.sessionAssets("late").isEmpty()); QVERIFY(assets.sessionPixels("late").isEmpty());
        QVERIFY(assets.sessionRegistration("late").isEmpty());
    }
    void publishedTicketsConsultAuthorityAndReleaseExpiredOwners()
    {
        const auto live = std::make_shared<std::atomic_bool>(true); Fake fake; auto backend = fake.backend(); const auto prepare = backend.prepare;
        backend.prepare = [prepare, live](const QString& id) {
            const auto reader = prepare(id);
            return [reader, live] { auto capture = reader(); capture.live = [live] { return live->load(); }; return capture; };
        };
        AddonUiAssets assets(backend, {}); AddonAssetImageProvider provider(&assets); QObject owner;
        QVERIFY(assets.openSession("published", "example/view")); QTRY_COMPARE(assets.sessionAssets("published").size(), 1);
        const auto claim = assets.claimImage("published", &owner), url = assets.assetUrlForClaim("published", "asset:cover", claim);
        QVERIFY(!provider.requestImage(providerId(url), nullptr, {}).isNull()); live->store(false);
        QVERIFY(provider.requestImage(providerId(url), nullptr, {}).isNull()); QVERIFY(assets.sessionPixels("published").isEmpty());
        QVERIFY(assets.assetUrlForClaim("published", "asset:cover", claim).isEmpty());
        fake.notifications.displayUnlocked();QTRY_COMPARE(assets.statistics().value("claims").toInt(), 0);
    }
    void pendingDecodeKeepsItsBudgetWithoutBlockingClose_data()
    {
        QTest::addColumn<bool>("destroy"); QTest::newRow("close") << false; QTest::newRow("destroy") << true;
    }
    void pendingDecodeKeepsItsBudgetWithoutBlockingClose()
    {
        QFETCH(bool, destroy);
        struct Control { QSemaphore entered, release; };
        const auto control = std::make_shared<Control>();
        const auto cleanup = qScopeGuard([control] { control->release.release(2); });
        Fake fake; fake.images.insert("second", raster("PNG", 4, 4, Qt::blue));
        auto backend = fake.backend();
        backend.decode = [control](const QByteArray& bytes, const QString& suffix, const AddonUiAssets::Options& options, QString* error) {
            control->entered.release();
            if (!control->release.tryAcquire(1, 5000)) { *error = QStringLiteral("The test decode did not resume."); return QImage{}; }
            return AddonUiAssets::decodeImage(bytes, suffix, options, error);
        };
        auto* assets = new AddonUiAssets(backend, {});
        QVERIFY(assets->openSession("pending", "example/view")); QTRY_VERIFY(control->entered.available() > 0);
        QCOMPARE(assets->statistics().value("globalJobs").toInt(), 2);
        QElapsedTimer elapsed; elapsed.start();
        if (destroy) { delete assets; assets = nullptr; } else assets->close();
        QVERIFY(elapsed.elapsed() < 100);
        Fake nextFake; AddonUiAssets next(nextFake.backend(), {});
        QVERIFY(next.openSession("next", "example/view"));
        QCOMPARE(next.statistics().value("globalJobs").toInt(), 2);
        QTRY_VERIFY(next.statistics().value("globalEncodedBytes").toLongLong() > nextFake.images.first().size());
        QVERIFY(next.sessionAssets("next").isEmpty());
        control->release.release(2); QTRY_COMPARE(next.sessionAssets("next").size(), 1);
        QTRY_COMPARE(next.statistics().value("globalJobs").toInt(), 0);
        QTRY_COMPARE(next.statistics().value("globalEncodedBytes").toLongLong(), 0);
        if (assets) { QVERIFY(assets->sessionAssets("pending").isEmpty()); QVERIFY(!assets->openSession("closed", "example/view")); delete assets; }
    }
    void slowIntegrityIoNeverBlocksTheUi_data()
    {
        QTest::addColumn<bool>("publishGuard"); QTest::addColumn<bool>("destroy");
        QTest::newRow("reader-close") << false << false; QTest::newRow("reader-destroy") << false << true;
        QTest::newRow("publish-close") << true << false; QTest::newRow("publish-destroy") << true << true;
    }
    void slowIntegrityIoNeverBlocksTheUi()
    {
        QFETCH(bool, publishGuard); QFETCH(bool, destroy);
        struct Control { QSemaphore entered, release; std::atomic<int> reads{0}, guards{0}, wrongThread{0}; };
        const auto control = std::make_shared<Control>(); const auto gui = QThread::currentThreadId();
        const auto cleanup = qScopeGuard([control] { control->release.release(4); });
        Fake fake; auto backend = fake.backend(); const auto prepare = backend.prepare;
        backend.prepare = [prepare, control, gui, publishGuard](const QString& id) {
            const auto reader = prepare(id);
            return [reader, control, gui, publishGuard] {
                if (QThread::currentThreadId() == gui) ++control->wrongThread;
                ++control->reads;
                if (!publishGuard) { control->entered.release(); control->release.tryAcquire(1, 5000); }
                auto capture = reader(); const auto current = capture.current;
                capture.current = [current, control, gui, publishGuard] {
                    if (QThread::currentThreadId() == gui) ++control->wrongThread;
                    if (++control->guards == 3 && publishGuard) { control->entered.release(); control->release.tryAcquire(1, 5000); }
                    return current();
                };
                return capture;
            };
        };
        auto* assets = new AddonUiAssets(backend, {}); QElapsedTimer elapsed; elapsed.start();
        QVERIFY(assets->openSession("slow", "example/view")); QVERIFY(elapsed.elapsed() < 100);
        QTRY_VERIFY(control->entered.available() > 0); QVERIFY(assets->sessionAssets("slow").isEmpty());
        bool heartbeat = false; QTimer::singleShot(0, this, [&heartbeat] { heartbeat = true; }); QTRY_VERIFY(heartbeat);
        elapsed.restart(); if (destroy) { delete assets; assets = nullptr; } else assets->close(); QVERIFY(elapsed.elapsed() < 100);
        QCOMPARE(control->wrongThread.load(), 0); QCOMPARE(control->reads.load(), 1);
        Fake nextFake; AddonUiAssets next(nextFake.backend(), {}); QVERIFY(next.openSession("next", "example/view"));
        if (!publishGuard) { QCOMPARE(next.statistics().value("globalIntegrityCaptures").toInt(), 1); QVERIFY(next.sessionAssets("next").isEmpty()); }
        control->release.release(); QTRY_COMPARE(next.sessionAssets("next").size(), 1);
        QTRY_COMPARE(next.statistics().value("globalIntegrityCaptures").toInt(), 0);
        QTRY_COMPARE(next.statistics().value("globalJobs").toInt(), 0);
        QCOMPARE(control->wrongThread.load(), 0);
        if (assets) { QVERIFY(assets->sessionAssets("slow").isEmpty()); delete assets; }
    }
    void byteJobTicketAndPinnedCacheLimits()
    {
        Fake fake; AddonUiAssets::Options limits; limits.maximumQueuedBytes = fake.images.first().size() - 1;
        AddonUiAssets tooManyBytes(fake.backend(), limits); QVERIFY(tooManyBytes.openSession("one", "example/view"));
        QTRY_COMPARE(tooManyBytes.statistics().value("jobs").toInt(), 0); QVERIFY(tooManyBytes.sessionAssets("one").isEmpty());
        limits = {}; limits.maximumCacheBytes = 64; limits.maximumJobs = 1;
        fake.images.insert("second", raster("PNG", 4, 4, Qt::blue));
        AddonUiAssets assets(fake.backend(), limits); QVERIFY(assets.openSession("one", "example/view"));
        QVERIFY(assets.statistics().value("jobs").toInt() <= 1);
        QVERIFY(assets.statistics().value("decodePixelReservation").toLongLong() <= 16 * 1024 * 1024);
        QTRY_COMPARE(assets.sessionAssets("one").size(), 1); QTRY_COMPARE(assets.statistics().value("queuedBytes").toLongLong(), 0); QCOMPARE(assets.statistics().value("cacheBytes").toLongLong(), 64);
        limits = {}; limits.maximumTickets = 1; AddonUiAssets tooManyTickets(fake.backend(), limits);
        QVERIFY(tooManyTickets.openSession("one", "example/view"));
        QTRY_COMPARE(tooManyTickets.statistics().value("jobs").toInt(), 0); QVERIFY(tooManyTickets.sessionAssets("one").isEmpty());
    }
    void manifestAssetsAreOnlyDeclaredRasterPaths()
    {
        Manifest parsed; auto correct = manifest(); QVERIFY(Manifest::parse(QJsonDocument(correct).toJson(), &parsed));
        const auto bad = QList<QJsonObject>{QJsonObject{{"__proto__", QJsonObject{{"path", "cover.png"}}}},
            QJsonObject{{"cover", QJsonObject{{"path", "https://example.org/a.png"}}}},
            QJsonObject{{"cover", QJsonObject{{"path", "../cover.png"}}}}, QJsonObject{{"cover", QJsonObject{{"path", "missing.png"}}}},
            QJsonObject{{"cover", QJsonObject{{"path", "cover.svg"}}}}, QJsonObject{{"cover", QJsonObject{{"path", "cover.png"}, {"url", "file:///private"}}}}};
        for (const auto& assets : bad) {
            auto value = correct; auto contributions = value.value("contributions").toArray(); auto contribution = contributions[0].toObject();
            contribution.insert("assets", assets); contributions[0] = contribution; value.insert("contributions", contributions);
            QVERIFY(!Manifest::parse(QJsonDocument(value).toJson(), &parsed));
        }
        auto contributions = correct.value("contributions").toArray(); auto theme = contributions[0].toObject(); theme.insert("type", "theme");
        contributions[0] = theme; correct.insert("contributions", contributions); QVERIFY(!Manifest::parse(QJsonDocument(correct).toJson(), &parsed));
    }
    void managerBundlesAndMountedOwnersStayScoped()
    {
        QTemporaryDir source, storage; package(source.path()); AddonManager manager(storage.path());
        QVERIFY2(manager.installLocalPackage(source.path(), true), qPrintable(manager.errorMessage())); QVERIFY(manager.setEnabled("org.example.images", true));
        const auto registration = manager.uiAssetReader("org.example.images/view")().registration; QVERIFY(!registration.isEmpty());
        QString error; const auto capture = manager.uiAssetReader("org.example.images/view")(); const auto bundle = capture.assets; error = capture.error;
        QVERIFY(error.isEmpty()); QCOMPARE(bundle.value("cover"), raster());
        AddonUiAssets assets(&manager); AddonUiRegistry registry(&manager); registry.setAssetService(&assets);
        const auto session = registry.createSession("org.example.images/view"); QVERIFY(!session.isEmpty());
        QObject mount, foreign; QObject image(&mount), invalid(&foreign);
        QVERIFY(registry.claimImage(session, &image).isEmpty()); QVERIFY(registry.bindSessionOwner(session, &mount));
        QVERIFY(!registry.bindSessionOwner(session, &foreign)); QVERIFY(registry.claimImage(session, &invalid).isEmpty());
        const auto claim = registry.claimImage(session, &image); QVERIFY(!claim.isEmpty());
        QTRY_COMPARE(registry.session(session).value("assetUrls").toMap().size(), 1);
        const auto url = registry.assetUrlForClaim(session, "asset:cover", claim); QVERIFY(!url.isEmpty());
        AddonAssetImageProvider provider(&assets); QVERIFY(!provider.requestImage(providerId(url), nullptr, {}).isNull());
        QVERIFY(manager.setSafeMode(true)); QVERIFY(provider.requestImage(providerId(url), nullptr, {}).isNull());
        QVERIFY(registry.session(session).isEmpty());
    }
    void realQmlMountRendersPixelsAndRevokesThem()
    {
        QTemporaryDir source, storage; package(source.path()); AddonManager manager(storage.path());
        QVERIFY(manager.installLocalPackage(source.path(), true)); QVERIFY(manager.setEnabled("org.example.images", true));
        AddonUiAssets assets(&manager); AddonUiRegistry registry(&manager), replacementRegistry(&manager);
        registry.setAssetService(&assets); replacementRegistry.setAssetService(&assets);
        QQmlEngine engine; engine.addImageProvider(QStringLiteral("addonassets"), new AddonAssetImageProvider(&assets));
        engine.rootContext()->setContextProperty(QStringLiteral("imageRegistry"), &registry);
        QQmlComponent component(&engine);
        const auto base = QUrl::fromLocalFile(QStringLiteral(VLC_ADDON_SOURCE_ROOT "/modules/gui/qt/player/qml/redesign/ImageProbe.qml"));
        component.setData("import QtQuick\nimport \".\"\nItem { width: 400; height: 200; RedesignAddonMount { objectName: \"imageMount\"; width: parent.width; registry: imageRegistry; contributionId: \"org.example.images/view\" } }", base);
        QScopedPointer<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
        auto* item = qobject_cast<QQuickItem*>(object.data()); QVERIFY(item);
        QQuickWindow window; window.setGeometry(0, 0, 400, 200); item->setParentItem(window.contentItem()); window.show();
        const auto detach = qScopeGuard([&object, item = QPointer<QQuickItem>(item)] { if (item) item->setParentItem(nullptr); object.reset(); });
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTRY_COMPARE(assets.statistics().value("claims").toInt(), 1);
        const QPointer<QObject> image = visualItem(item, QStringLiteral("addonAssetImage")); QVERIFY(image);
        QTRY_COMPARE(image->property("status").toInt(), 1); QCOMPARE(image->property("cache").toBool(), false);
        QImage capture;
        QTRY_VERIFY([&] {
            capture = window.grabWindow(); if (capture.isNull()) return false;
            const auto center = capture.pixelColor(capture.width() / 2, capture.height() * 2 / 5);
            return center.red() > 240 && center.green() < 20 && center.blue() < 20;
        }());
        auto* mount = visualItem(item, QStringLiteral("imageMount")); QVERIFY(mount);
        const auto oldSession = mount->property("sessionId").toString(), oldSource = image->property("source").toUrl().toString();
        QVERIFY(mount->setProperty("registry", QVariant::fromValue(static_cast<QObject*>(&replacementRegistry))));
        QTRY_VERIFY(mount->property("sessionId").toString() != oldSession);
        QVERIFY(registry.session(oldSession).isEmpty()); QTRY_COMPARE(assets.statistics().value("claims").toInt(), 1);
        AddonAssetImageProvider provider(&assets); QVERIFY(provider.requestImage(providerId(oldSource), nullptr, {}).isNull());
        const QPointer<QObject> currentImage = visualItem(item, QStringLiteral("addonAssetImage")); QVERIFY(currentImage);
        QTRY_COMPARE(currentImage->property("status").toInt(), 1);
        QVERIFY(manager.setEnabled("org.example.images", false)); QTRY_COMPARE(assets.statistics().value("claims").toInt(), 0);
        QTRY_VERIFY(!currentImage || currentImage->property("source").toUrl().isEmpty());
    }
    void externalImageRoutesHavePositiveControlsAndStayUnusedByViews()
    {
        QQmlEngine engine; auto* probe = new AddonAssetIoProbe(&engine);
        QQmlComponent control(&engine); control.setData("import QtQuick\nImage { width: 4; height: 4; cache: false; asynchronous: true }", {});
        for (const auto& url : {QStringLiteral("image://addon-image-outside/control"),
                QStringLiteral("https://addon-image-probe.invalid/control.png"), probe->fileUrl()}) {
            QScopedPointer<QObject> image(control.createWithInitialProperties({{"source", QUrl(url)}})); QVERIFY(image);
            QTRY_COMPARE(image->property("status").toInt(), 1);
        }
        QVERIFY(probe->state->images > 0); QVERIFY(probe->state->network > 0);
#ifdef Q_OS_LINUX
        QVERIFY(probe->state->watch >= 0); QVERIFY(probe->fileReads() > 0);
#endif
        probe->reset();
        QTemporaryDir source, storage; auto value = manifest(); auto files = value.value("files").toArray(); files.append("bad.png"); value.insert("files", files);
        auto contributions = value.value("contributions").toArray(); auto contribution = contributions[0].toObject();
        auto declarations = contribution.value("assets").toObject(); declarations.insert("bad", QJsonObject{{"path", "bad.png"}});
        contribution.insert("assets", declarations); contributions[0] = contribution; value.insert("contributions", contributions); package(source.path(), value);
        const auto remote = QStringLiteral("https://addon-image-probe.invalid/hidden.png"), external = QStringLiteral("image://addon-image-outside/hidden");
        const auto svg = QStringLiteral("<svg xmlns='http://www.w3.org/2000/svg'><image href='%1'/><image href='%2'/><image href='%3'/></svg>")
            .arg(remote, external, probe->fileUrl()).toUtf8();
        write(source.path() + QStringLiteral("/bad.png"), svg);
        QJsonArray children;
        const QString dataUrl = QStringLiteral("data:image/svg+xml,") + QString::fromLatin1(svg.toPercentEncoding());
        for (const auto& handle : {QStringLiteral("asset:cover"), QStringLiteral("asset:bad"), remote, external, probe->fileUrl(), QStringLiteral("qrc:/outside.svg"), dataUrl})
            children.append(QJsonObject{{"type", "Image"}, {"artwork", handle}, {"text", "Image"}});
        write(source.path() + QStringLiteral("/view.json"), QJsonDocument(QJsonObject{{"contract", "ui@1"}, {"id", "image-io"},
            {"root", QJsonObject{{"type", "Column"}, {"children", children}}}}).toJson());
        AddonManager manager(storage.path()); QVERIFY(manager.installLocalPackage(source.path(), true)); QVERIFY(manager.setEnabled("org.example.images", true));
        AddonUiAssets assets(&manager); AddonUiRegistry registry(&manager); registry.setAssetService(&assets);
        engine.addImageProvider(QStringLiteral("addonassets"), new AddonAssetImageProvider(&assets));
        engine.rootContext()->setContextProperty(QStringLiteral("imageRegistry"), &registry);
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport \".\"\nItem { width: 400; height: 200; RedesignAddonMount { width: parent.width; registry: imageRegistry; contributionId: \"org.example.images/view\" } }",
            QUrl::fromLocalFile(QStringLiteral(VLC_ADDON_SOURCE_ROOT "/modules/gui/qt/player/qml/redesign/ImageProbe.qml")));
        QScopedPointer<QObject> object(component.create()); QVERIFY2(object, qPrintable(component.errorString()));
        auto* item = qobject_cast<QQuickItem*>(object.data()); QVERIFY(item);
        QQuickWindow window; window.setGeometry(0, 0, 400, 200); item->setParentItem(window.contentItem()); window.show();
        const auto detach = qScopeGuard([&object, item = QPointer<QQuickItem>(item)] { if (item) item->setParentItem(nullptr); object.reset(); });
        QVERIFY(QTest::qWaitForWindowExposed(&window)); QTRY_COMPARE(assets.statistics().value("jobs").toInt(), 0);
        QTRY_COMPARE(assets.statistics().value("claims").toInt(), 1);
        QImage pixels;
        QTRY_VERIFY([&] {
            pixels = window.grabWindow(); if (pixels.isNull()) return false;
            const auto color = pixels.pixelColor(pixels.width() / 2, pixels.height() * 2 / 5);
            return color.red() > 240 && color.green() < 20 && color.blue() < 20;
        }());
        auto* renderer = visualItem(item, QStringLiteral("addonUiRenderer")); QVERIFY(renderer);
        QVERIFY(renderer->setProperty("assetUrls", QVariantMap{{"file", probe->fileUrl()}, {"qrc", "qrc:/outside.svg"}, {"network", remote}, {"data", dataUrl}}));
        for (const auto& alias : {QStringLiteral("file"), QStringLiteral("qrc"), QStringLiteral("network"), QStringLiteral("data")}) {
            QVariant result;
            QVERIFY(QMetaObject::invokeMethod(renderer, "assetFor", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, alias)));
            QVERIFY(result.toString().isEmpty());
        }
        QTRY_COMPARE(assets.statistics().value("claims").toInt(), 0);
        QCOMPARE(probe->state->images.load(), 0); QVERIFY2(probe->state->network.load() == 0, qPrintable(probe->networkUrls()));
#ifdef Q_OS_LINUX
        QCOMPARE(probe->fileReads(), 0);
#endif
        item->setParentItem(nullptr); object.reset();
    }
    void realManagerAccountReleaseAndGrantChangesRevokeTickets()
    {
        QTemporaryDir source, storage; auto value = manifest(); auto files = value.value("files").toArray(); files.append("settings.json"); value.insert("files", files);
        auto api = value.value("api").toObject(); auto contracts = api.value("contracts").toObject(); contracts.insert("settings", "^1.0.0"); api.insert("contracts", contracts); value.insert("api", api);
        value.insert("settings", QJsonObject{{"version", 1}, {"schema", "settings.json"}, {"accountFields", QJsonArray{"account"}}});
        package(source.path(), value);
        write(source.path() + QStringLiteral("/settings.json"), QJsonDocument(QJsonObject{{"type", "object"}, {"additionalProperties", false},
            {"properties", QJsonObject{{"account", QJsonObject{{"type", "string"}, {"default", "first"}}}}}}).toJson());
        AddonManager manager(storage.path()); QVERIFY(manager.installLocalPackage(source.path(), true)); QVERIFY(manager.setEnabled("org.example.images", true));
        AddonUiAssets assets(&manager); AddonAssetImageProvider provider(&assets); QObject owner;
        QVERIFY(assets.openSession("account", "org.example.images/view")); QTRY_COMPARE(assets.sessionAssets("account").size(), 1);
        const auto claim = assets.claimImage("account", &owner), oldUrl = assets.assetUrlForClaim("account", "asset:cover", claim);
        const auto oldStamp = manager.uiAssetReader("org.example.images/view")().registration.value("assetStamp");
        QVERIFY(manager.saveAddonSettings("org.example.images", {{"account", "second"}}, {}));
        QVERIFY(manager.uiAssetReader("org.example.images/view")().registration.value("assetStamp") != oldStamp);
        QVERIFY(provider.requestImage(providerId(oldUrl), nullptr, {}).isNull());
        QVERIFY(assets.openSession("release", "org.example.images/view")); QTRY_COMPARE(assets.sessionAssets("release").size(), 1);
        const auto releaseClaim = assets.claimImage("release", &owner), releaseUrl = assets.assetUrlForClaim("release", "asset:cover", releaseClaim);
        value.insert("version", "1.1.0"); write(source.path() + QStringLiteral("/addon.json"), QJsonDocument(value).toJson());
        QVERIFY(manager.installLocalPackage(source.path(), true)); QVERIFY(provider.requestImage(providerId(releaseUrl), nullptr, {}).isNull());
        value.insert("version", "1.2.0"); value.insert("permissions", QJsonObject{{"required", QJsonObject{}}});
        write(source.path() + QStringLiteral("/addon.json"), QJsonDocument(value).toJson()); QVERIFY(manager.installLocalPackage(source.path(), true));
        QVERIFY(manager.uiAssetReader("org.example.images/view")().registration.isEmpty()); QVERIFY(assets.openSession("denied", "org.example.images/view")); QTRY_COMPARE(assets.statistics().value("jobs").toInt(), 0); QVERIFY(assets.sessionAssets("denied").isEmpty());
    }
    void changedPackageBytesCannotBecomeAnAssetBundle()
    {
        QTemporaryDir source, storage; package(source.path()); AddonManager manager(storage.path());
        QVERIFY(manager.installLocalPackage(source.path(), true)); QVERIFY(manager.setEnabled("org.example.images", true));
        const auto registration = manager.uiAssetReader("org.example.images/view")().registration;
        PackageStore store(storage.path()); InstalledPackage installed; QVERIFY(store.installed("org.example.images", &installed));
        const QString image = installed.directory + QStringLiteral("/cover.png"); QVERIFY(QFile::setPermissions(image, QFile::ReadOwner | QFile::WriteOwner));
        write(image, raster("PNG", 4, 4, Qt::blue)); QString error;
        const auto capture = manager.uiAssetReader("org.example.images/view")(); QVERIFY(capture.assets.isEmpty()); QVERIFY(!capture.error.isEmpty());
    }
    void requiredSecretsUsePresenceAndRevokeOldCaptures()
    {
        QTemporaryDir source, storage; auto value = manifest(); auto files = value.value("files").toArray(); files.append("settings.json"); value.insert("files", files);
        auto api = value.value("api").toObject(), contracts = api.value("contracts").toObject(); contracts.insert("settings", "^1.0.0"); api.insert("contracts", contracts); value.insert("api", api);
        value.insert("settings", QJsonObject{{"version", 1}, {"schema", "settings.json"}});
        auto permissions = value.value("permissions").toObject(), required = permissions.value("required").toObject();
        required.insert("network.http", QJsonObject{{"origins", QJsonArray{"https://images.invalid"}}, {"methods", QJsonArray{"GET"}}});
        required.insert("credentials.use", QJsonObject{{"bindings", QJsonArray{QJsonObject{{"field", "apiKey"}, {"origin", "https://images.invalid"}, {"header", "Authorization"}}}}});
        permissions.insert("required", required); value.insert("permissions", permissions); package(source.path(), value);
        write(source.path() + QStringLiteral("/settings.json"), QJsonDocument(QJsonObject{{"type", "object"}, {"additionalProperties", false}, {"required", QJsonArray{"apiKey"}},
            {"properties", QJsonObject{{"apiKey", QJsonObject{{"type", "string"}, {"x-hakone-secret", true}, {"minLength", 1}}}}}}).toJson());
        AddonManager manager(storage.path()); QVERIFY(manager.installLocalPackage(source.path(), true)); QVERIFY(manager.setEnabled("org.example.images", true));
        QVERIFY(manager.uiAssetReader("org.example.images/view")().registration.isEmpty());
        const auto replace = [](const QString& key) { return QVariantMap{{"apiKey", QVariantMap{{"operation", "replace"}, {"value", key}, {"sessionOnly", true}}}}; };
        QVERIFY(manager.saveAddonSettings("org.example.images", {}, replace("test-only-key-one")));
        const auto first = manager.uiAssetReader("org.example.images/view")(); QVERIFY(!first.registration.isEmpty());
        QVERIFY(!QJsonDocument(QJsonObject::fromVariantMap(first.registration)).toJson().contains("test-only-key-one"));
        AddonUiAssets assets(&manager); AddonAssetImageProvider provider(&assets); QObject owner;
        QVERIFY(assets.openSession("key", "org.example.images/view")); QTRY_COMPARE(assets.sessionAssets("key").size(), 1);
        const auto claim = assets.claimImage("key", &owner), url = assets.assetUrlForClaim("key", "asset:cover", claim);
        QVERIFY(manager.saveAddonSettings("org.example.images", {}, replace("test-only-key-two"))); QVERIFY(!first.current());
        const auto second = manager.uiAssetReader("org.example.images/view")(); QVERIFY(!second.registration.isEmpty());
        QVERIFY(first.registration.value("accountReference") != second.registration.value("accountReference"));
        QVERIFY(provider.requestImage(providerId(url), nullptr, {}).isNull());
        QVERIFY(manager.saveAddonSettings("org.example.images", {}, {{"apiKey", QVariantMap{{"operation", "delete"}}}})); QVERIFY(!second.current());
        QVERIFY(manager.uiAssetReader("org.example.images/view")().registration.isEmpty());
    }
};
int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen"); qputenv("QT_QUICK_BACKEND", "software");
    QGuiApplication app(argc, argv); AddonTestApplicationWork work; work.start();
    TestAddonUiAssets test; return QTest::qExec(&test, argc, argv);
}
#include "test_addon_ui_assets.moc"
