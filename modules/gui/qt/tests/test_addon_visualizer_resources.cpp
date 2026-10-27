// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include "addons/addon_manager.hpp"
#include "addons/addon_visualizer_catalog.hpp"
#include "addons/addon_visualizer_resources.hpp"
#include "addons/addon_visual_scene.hpp"
using namespace vlc::addons;
namespace {
const QString packageId = QStringLiteral("org.example.visual-resources");
const QString contributionId = packageId + QStringLiteral("/texture");
bool put(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath()); QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
QByteArray raster(QSize size = {800, 400})
{
    QImage image(size, QImage::Format_ARGB32); image.fill(Qt::red); QByteArray bytes; QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return {};
    return bytes;
}
bool package(const QString& path, const QByteArray& image = raster(), bool settings = false)
{
    QJsonObject scene{{"contract", "visualizer.scene@1"}, {"background", "theme.background"},
        {"assets", QJsonObject{{"tile", "tile.png"}}}, {"nodes", QJsonArray{QJsonObject{{"type", "texture"}, {"asset", "tile"}}}}};
    QJsonObject manifest{{"formatVersion", 1}, {"id", packageId}, {"name", "Image visualizer"}, {"version", "1.0.0"},
        {"publisher", "Tests"}, {"license", "GPL-2.0-or-later"}, {"source", "https://example.invalid/visual.git"},
        {"api", QJsonObject{{"addon", "^1.0.0"}, {"contracts", QJsonObject{{"visualizer", "^1.0.0"}, {"settings", "^1.0.0"}}}}},
        {"files", QJsonArray{"addon.json", "scene.json", "tile.png", "LICENSE"}},
        {"contributions", QJsonArray{QJsonObject{{"id", "texture"}, {"type", "visualizer"}, {"contract", "visualizer@1"},
            {"scene", "scene.json"}, {"analysisChannels", QJsonArray{"bands"}}, {"qualityTiers", QJsonArray{"low", "normal"}}}}},
        {"permissions", QJsonObject{{"required", QJsonObject{{"audio.analysis", QJsonObject{{"channels", QJsonArray{"bands"}}}}}}}}};
    if (settings) {
        auto files = manifest.value("files").toArray(); files.append("settings.json"); manifest.insert("files", files);
        manifest.insert("settings", QJsonObject{{"version", 1}, {"schema", "settings.json"}, {"accountFields", QJsonArray{"username"}}});
        const QJsonObject schema{{"type", "object"}, {"properties", QJsonObject{{"username", QJsonObject{{"type", "string"}, {"default", "guest"}, {"maxLength", 64}}}}},
            {"required", QJsonArray{"username"}}, {"additionalProperties", false}};
        if (!put(path + QStringLiteral("/settings.json"), QJsonDocument(schema).toJson())) return false;
    }
    return put(path + QStringLiteral("/addon.json"), QJsonDocument(manifest).toJson())
        && put(path + QStringLiteral("/scene.json"), QJsonDocument(scene).toJson())
        && put(path + QStringLiteral("/tile.png"), image)
        && put(path + QStringLiteral("/LICENSE"), "SPDX-License-Identifier: GPL-2.0-or-later\n");
}
bool install(AddonManager& manager, const QString& source)
{ return manager.installLocalPackage(source, true) && manager.setEnabled(packageId, true); }
}
class TestAddonVisualizerResources : public QObject
{
    Q_OBJECT
private slots:
    void verifiedTexturesKeepTheirNativeSizeAndRenderPixels()
    {
        QTemporaryDir files; const auto source = files.filePath(QStringLiteral("package")); QVERIFY(package(source));
        AddonManager manager(files.filePath(QStringLiteral("profile"))); QVERIFY(install(manager, source));
        AddonVisualizerResources resources(&manager); QObject surface; QVERIFY(resources.load(contributionId, &surface));
        QTRY_VERIFY_WITH_TIMEOUT(resources.ready(), 5000); const auto prepared = resources.activation(); QVERIFY(prepared);
        QVERIFY(prepared->current()); QVERIFY(prepared->live()); QCOMPARE(resources.images().value("tile").size(), QSize(800, 400));
        AddonVisualScene renderer; AddonAudioAnalysis::Frame frame;
        const auto image = renderer.render(prepared->scene, frame, 0, {320, 200}, {Qt::black, Qt::white, Qt::green}, 1, false, resources.images());
        QVERIFY(!image.isNull()); QCOMPARE(image.pixelColor(160, 100), QColor(Qt::red));
        resources.close(); QVERIFY(!resources.ready()); QVERIFY(resources.images().isEmpty()); QVERIFY(!prepared->live());
    }
    void unsupportedImagesKeepTheArtworkFallback_data()
    {
        QTest::addColumn<QByteArray>("image");
        QTest::newRow("svg") << QByteArray("<svg xmlns='http://www.w3.org/2000/svg' width='1' height='1'/>");
        QTest::newRow("jpeg-spoof") << QByteArray::fromHex("ffd8ffd9");
        QTest::newRow("pixel-limit") << raster({1025, 1});
        auto animated = raster(); animated.insert(33, QByteArray::fromHex("000000086163544c000000020000000000000000"));
        QTest::newRow("animation") << animated;
    }
    void unsupportedImagesKeepTheArtworkFallback()
    {
        QFETCH(QByteArray, image); QTemporaryDir files; const auto source = files.filePath(QStringLiteral("package")); QVERIFY(package(source, image));
        AddonManager manager(files.filePath(QStringLiteral("profile"))); QVERIFY(install(manager, source));
        AddonVisualizerResources resources(&manager); QObject surface; QVERIFY(resources.load(contributionId, &surface));
        QTRY_VERIFY_WITH_TIMEOUT(!resources.loading(), 5000); QVERIFY(!resources.ready()); QVERIFY(!resources.errorMessage().isEmpty());
        QVERIFY(resources.images().isEmpty());
    }
    void anotherStoreWithdrawsPublishedPixelsWithoutAManagerSignal()
    {
        QTemporaryDir files; const auto source = files.filePath(QStringLiteral("package")); QVERIFY(package(source));
        const auto profile = files.filePath(QStringLiteral("profile")); AddonManager manager(profile); QVERIFY(install(manager, source));
        AddonVisualizerResources resources(&manager); QObject surface; QVERIFY(resources.load(contributionId, &surface));
        QTRY_VERIFY_WITH_TIMEOUT(resources.ready(), 5000); const auto prepared = resources.activation();
        PackageStore other(profile); QVERIFY(other.setEnabled(packageId, false)); QVERIFY(!prepared->live());
        QVERIFY(!resources.ready()); QVERIFY(resources.images().isEmpty());
        QVERIFY(other.setEnabled(packageId, true)); QVERIFY(!prepared->live());
        QVERIFY(resources.load(contributionId, &surface)); QTRY_VERIFY_WITH_TIMEOUT(resources.ready(), 5000);
    }
    void hiddenSelectionCanValidateBeforeItClaimsAMountedImage()
    {
        QTemporaryDir files; const auto source = files.filePath(QStringLiteral("package")); QVERIFY(package(source));
        AddonManager manager(files.filePath(QStringLiteral("profile"))); QVERIFY(install(manager, source));
        AddonVisualizerResources resources(&manager); QVERIFY(resources.load(contributionId));
        QTRY_VERIFY_WITH_TIMEOUT(resources.valid(), 5000); QVERIFY(!resources.ready()); QVERIFY(resources.images().isEmpty());
        QObject surface; QVERIFY(resources.setSurface(&surface)); QVERIFY(resources.ready());
        QCOMPARE(resources.images().value("tile").size(), QSize(800, 400));
        QVERIFY(!resources.setSurface(nullptr)); QVERIFY(resources.valid()); QVERIFY(!resources.ready());
    }
    void accountSettingsStayInTheCapturedAuthority()
    {
        QTemporaryDir files; const auto source = files.filePath(QStringLiteral("package")); QVERIFY(package(source, raster(), true));
        const auto profile = files.filePath(QStringLiteral("profile")); AddonManager manager(profile); QVERIFY(install(manager, source));
        AddonVisualizerResources resources(&manager); QObject surface; QVERIFY(resources.load(contributionId, &surface));
        QTRY_VERIFY_WITH_TIMEOUT(resources.ready(), 5000); const auto before = resources.activation();
        QCOMPARE(before->account, manager.contributionsOfType(QStringLiteral("visualizer")).first().toMap().value("accountReference").toString());
        PackageStore other(profile); QJsonObject values; quint64 revision = 0; QVERIFY(other.settings(packageId, &values, &revision));
        values.insert("username", "alice"); QVERIFY(other.saveSettings(packageId, values, revision)); QVERIFY(!before->live());
        QVERIFY(resources.load(contributionId, &surface)); QTRY_VERIFY_WITH_TIMEOUT(resources.ready(), 5000);
        QVERIFY(resources.activation()->account != before->account); QCOMPARE(resources.activation()->settings.value("username").toString(), QStringLiteral("alice"));
    }
    void surfaceRemovalAndPendingCloseRejectLateResources()
    {
        QTemporaryDir files; const auto source = files.filePath(QStringLiteral("package")); QVERIFY(package(source));
        AddonManager manager(files.filePath(QStringLiteral("profile"))); QVERIFY(install(manager, source));
        AddonVisualizerResources resources(&manager); auto* surface = new QObject;
        QVERIFY(resources.load(contributionId, surface)); QTRY_VERIFY_WITH_TIMEOUT(resources.ready(), 5000);
        delete surface; QVERIFY(!resources.ready());
        QObject next; QVERIFY(resources.load(contributionId, &next)); QElapsedTimer elapsed; elapsed.start(); resources.close();
        QVERIFY(elapsed.elapsed() < 100); QVERIFY(!resources.loading());
        QCoreApplication::processEvents(); QVERIFY(!resources.ready()); QVERIFY(resources.images().isEmpty());
    }
    void catalogShowsOnlyGrantedEnabledChoices()
    {
        QTemporaryDir files; const auto source = files.filePath(QStringLiteral("package")); QVERIFY(package(source));
        AddonManager manager(files.filePath(QStringLiteral("profile"))); QVERIFY(install(manager, source));
        AddonVisualizerCatalog catalog(&manager); QTRY_VERIFY_WITH_TIMEOUT(!catalog.loading(), 5000);
        QCOMPARE(catalog.choices().size(), 1); const auto entry = catalog.choices().first().toMap();
        QCOMPARE(entry.value("id").toString(), contributionId); QVERIFY(entry.value("enabled").toBool());
        QVERIFY(manager.setEnabled(packageId, false)); QTRY_VERIFY_WITH_TIMEOUT(!catalog.loading(), 5000);
        QCOMPARE(catalog.choices().size(), 1); QVERIFY(!catalog.choices().first().toMap().value("enabled").toBool());
    }
};
QTEST_GUILESS_MAIN(TestAddonVisualizerResources)
#include "test_addon_visualizer_resources.moc"
