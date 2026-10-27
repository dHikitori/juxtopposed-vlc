// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QQuickWindow>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QTemporaryDir>
#include "addons/addon_visualizer_surface.hpp"
using namespace vlc::addons;
namespace {
bool put(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath()); QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
bool package(const QString& path)
{
    const QJsonObject scene{{"contract", "visualizer.scene@1"}, {"background", "#ff0000"}, {"nodes", QJsonArray{}}};
    const QJsonObject manifest{{"formatVersion", 1}, {"id", "org.example.surface"}, {"name", "Surface visualizer"}, {"version", "1.0.0"},
        {"publisher", "Tests"}, {"license", "GPL-2.0-or-later"}, {"source", "https://example.invalid/surface.git"},
        {"api", QJsonObject{{"addon", "^1.0.0"}, {"contracts", QJsonObject{{"visualizer", "^1.0.0"}}}}},
        {"files", QJsonArray{"addon.json", "scene.json", "LICENSE"}},
        {"contributions", QJsonArray{QJsonObject{{"id", "red"}, {"type", "visualizer"}, {"contract", "visualizer@1"}, {"scene", "scene.json"},
            {"analysisChannels", QJsonArray{"bands"}}, {"qualityTiers", QJsonArray{"low", "normal"}}}}},
        {"permissions", QJsonObject{{"required", QJsonObject{{"audio.analysis", QJsonObject{{"channels", QJsonArray{"bands"}}}}}}}}};
    return put(path + QStringLiteral("/addon.json"), QJsonDocument(manifest).toJson())
        && put(path + QStringLiteral("/scene.json"), QJsonDocument(scene).toJson())
        && put(path + QStringLiteral("/LICENSE"), "SPDX-License-Identifier: GPL-2.0-or-later\n");
}
struct Audio {
    AddonAudioAnalysis* analysis = nullptr; std::uint64_t generation = 0;
    int starts = 0, stops = 0;
    AddonVisualizers::Backend backend()
    {
        return {[] { return AddonVisualizers::Snapshot{1, 1000000, 1, true}; },
            [this](AddonAudioAnalysis* target, std::uint64_t epoch) { analysis = target; generation = epoch; ++starts; return true; },
            [this] { analysis = nullptr; ++stops; }};
    }
    void send()
    {
        if (!analysis) return;
        std::array<float, 1024> samples{}; samples.fill(0.5f);
        analysis->append(generation, AddonAudioAnalysis::Format::Float32, samples.data(), sizeof(samples), samples.size(), 1, 48000, 1000000, 1);
    }
};
}
class TestAddonVisualizerSurface : public QObject
{
    Q_OBJECT
private slots:
    void realWindowRecreationAndGraphRecoveryKeepArtworkFallback()
    {
        QTemporaryDir files; const auto source = files.filePath(QStringLiteral("package")); QVERIFY(package(source));
        AddonManager manager(files.filePath(QStringLiteral("profile"))); QVERIFY(manager.installLocalPackage(source, true));
        QVERIFY(manager.setEnabled(QStringLiteral("org.example.surface"), true)); Audio audio; AddonVisualizers controller(&manager, audio.backend());
        QTRY_VERIFY_WITH_TIMEOUT(controller.availableVisualizers().size() > 1, 5000); QVERIFY(controller.selectVisualizer(QStringLiteral("org.example.surface/red")));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.loading(), 5000);
        QGuiApplication::setQuitOnLastWindowClosed(false);
        std::unique_ptr<QQuickWindow> window; AddonVisualizerSurface* surface = nullptr;
        const auto createWindow = [&] {
            window = std::make_unique<QQuickWindow>(); window->setPersistentGraphics(false); window->setPersistentSceneGraph(false);
            window->setColor(Qt::blue); window->resize(80, 80);
            surface = new AddonVisualizerSurface(window->contentItem()); surface->setSize({80, 80}); surface->setController(&controller);
        };
        createWindow();
        QTimer input; input.setInterval(10); connect(&input, &QTimer::timeout, &controller, [&] { audio.send(); }); input.start();
        QCOMPARE(audio.starts, 0); window->show(); QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        QTRY_VERIFY_WITH_TIMEOUT(controller.active(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(window->grabWindow().pixelColor(40, 40), QColor(Qt::red), 5000);
        window->sceneGraphInvalidated(); window->afterRendering();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.active(), 5000); QVERIFY(!audio.analysis);
        QTRY_COMPARE_WITH_TIMEOUT(window->grabWindow().pixelColor(40, 40), QColor(Qt::blue), 5000);
        window->sceneGraphInitialized(); QTRY_VERIFY_WITH_TIMEOUT(controller.active(), 5000);
        window->hide(); QTRY_VERIFY_WITH_TIMEOUT(!controller.active(), 5000); QVERIFY(!audio.analysis);
        window->releaseResources(); const QPointer<AddonVisualizerSurface> previous(surface); window.reset(); QVERIFY(!previous);
        const int starts = audio.starts; createWindow(); window->show(); QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        QTRY_VERIFY_WITH_TIMEOUT(controller.active(), 5000); QVERIFY(audio.starts > starts);
        QTRY_COMPARE_WITH_TIMEOUT(window->grabWindow().pixelColor(40, 40), QColor(Qt::red), 5000);
        // This is a native error notification, not a physical GPU failure.
        window->sceneGraphError(QQuickWindow::ContextNotAvailable, QStringLiteral("Test scene graph error"));
        QTRY_VERIFY_WITH_TIMEOUT(!controller.active(), 5000); QVERIFY(!audio.analysis);
        window->update(); QCoreApplication::processEvents(); QVERIFY(!controller.active());
        window->hide(); window->releaseResources(); window.reset(); createWindow(); window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        QTRY_VERIFY_WITH_TIMEOUT(controller.active(), 5000);
    }
};
QTEST_MAIN(TestAddonVisualizerSurface)
#include "test_addon_visualizer_surface.moc"
