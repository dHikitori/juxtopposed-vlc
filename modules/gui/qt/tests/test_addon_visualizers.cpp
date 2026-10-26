// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QBuffer>
#include "addons/addon_visualizers.hpp"
using namespace vlc::addons;

class TestAddonVisualizers : public QObject
{
    Q_OBJECT
    static bool put(const QString& path, const QByteArray& bytes) {
        QDir().mkpath(QFileInfo(path).absolutePath()); QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
    static bool package(const QString& path, bool malformed = false, const QByteArray& asset = {}) {
        QJsonObject scene{{"contract", "visualizer.scene@1"}, {"background", "theme.background"}, {"nodes", QJsonArray{
            QJsonObject{{"type", malformed ? "shader" : "spectrum"}, {"count", 32}, {"color", "theme.accent"}}}}};
        QJsonObject manifest{{"formatVersion", 1}, {"id", "org.example.visual"}, {"name", "Visual test"}, {"version", "1.0.0"},
            {"publisher", "Tests"}, {"license", "GPL-2.0-or-later"}, {"source", "https://example.org/visual.git"},
            {"api", QJsonObject{{"addon", "^1.0.0"}, {"contracts", QJsonObject{{"visualizer", "^1.0.0"}}}}},
            {"files", QJsonArray{"addon.json", "scene.json", "LICENSE"}}, {"contributions", QJsonArray{QJsonObject{
                {"id", "spectrum"}, {"name", "Spectrum"}, {"type", "visualizer"}, {"contract", "visualizer@1"}, {"scene", "scene.json"},
                {"analysisChannels", QJsonArray{"bands"}}, {"qualityTiers", QJsonArray{"low", "normal"}}}}},
            {"permissions", QJsonObject{{"required", QJsonObject{{"audio.analysis", QJsonObject{{"channels", QJsonArray{"bands"}}}}}}}}};
        if (!asset.isEmpty()) {
            auto files = manifest.value("files").toArray(); files.append("tile.png"); manifest.insert("files", files);
            scene.insert("assets", QJsonObject{{"tile", "tile.png"}});
            scene.insert("nodes", QJsonArray{QJsonObject{{"type", "texture"}, {"asset", "tile"}}});
            if (!put(path + QStringLiteral("/tile.png"), asset)) return false;
        }
        return put(path + QStringLiteral("/addon.json"), QJsonDocument(manifest).toJson())
            && put(path + QStringLiteral("/scene.json"), QJsonDocument(scene).toJson())
            && put(path + QStringLiteral("/LICENSE"), "SPDX-License-Identifier: GPL-2.0-or-later\n");
    }
    struct Audio {
        AddonAudioAnalysis* analysis = nullptr; std::uint64_t generation = 0;
        AddonVisualizers::Snapshot state{1, 1000000, 1, true}; int starts = 0, stops = 0;
        AddonVisualizers::Backend backend() {
            return {[this] { return state; }, [this](AddonAudioAnalysis* value, std::uint64_t epoch) {
                analysis = value; generation = epoch; ++starts; return true;
            }, [this] { ++stops; analysis = nullptr; }};
        }
        bool send() {
            if (!analysis) return false; std::array<float, 1024> data{}; data.fill(0.5f);
            return analysis->append(generation, AddonAudioAnalysis::Format::Float32, data.data(), sizeof data, data.size(), 1, 48000, state.timeUs, state.rate);
        }
    };
private slots:
    void selectionVisibilityAndPackageRevocationControlAnalysis() {
        QTemporaryDir files; QVERIFY(package(files.path() + QStringLiteral("/package")));
        AddonManager manager(files.path() + QStringLiteral("/profile"));
        QVERIFY2(manager.installLocalPackage(files.path() + QStringLiteral("/package"), true), qPrintable(manager.errorMessage()));
        QVERIFY(manager.contributionFile(QStringLiteral("org.example.visual/spectrum"), QStringLiteral("scene.json"), 8192).isEmpty());
        QVERIFY(manager.setEnabled(QStringLiteral("org.example.visual"), true));
        QVERIFY(!manager.contributionFile(QStringLiteral("org.example.visual/spectrum"), QStringLiteral("scene.json"), 8192).isEmpty());
        QVERIFY(manager.contributionFile(QStringLiteral("org.example.visual/spectrum"), QStringLiteral("../scene.json"), 8192).isEmpty());
        QVERIFY(manager.contributionFile(QStringLiteral("org.example.visual/foreign"), QStringLiteral("scene.json"), 8192).isEmpty());
        QVERIFY(manager.contributionFile(QStringLiteral("org.example.visual/spectrum"), QStringLiteral("scene.json"), 1).isEmpty());
        Audio audio; AddonVisualizers visualizers(&manager, audio.backend()); QObject surface;
        QVERIFY(visualizers.selectVisualizer(QStringLiteral("org.example.visual/spectrum")));
        QCOMPARE(audio.starts, 0); QVERIFY(!visualizers.active());
        visualizers.setSurface(&surface, true, {320, 240}); QCOMPARE(audio.starts, 1); QVERIFY(!visualizers.active());
        QVERIFY(audio.send()); QTRY_VERIFY((visualizers.refresh(), visualizers.active())); QVERIFY(!visualizers.image().isNull());
        visualizers.setSurface(&surface, false, {}); QCOMPARE(audio.stops, 1); QVERIFY(!visualizers.active());
        QVERIFY(visualizers.image().isNull());
        visualizers.setSurface(&surface, true, {320, 240}); QCOMPARE(audio.starts, 2);
        QVERIFY(manager.setEnabled(QStringLiteral("org.example.visual"), false)); QVERIFY(!visualizers.active());
        QVERIFY(!audio.analysis); QVERIFY(!visualizers.errorMessage().isEmpty());
    }
    void mediaChangeDropsOldFramesAndPauseFreezesOutput() {
        QTemporaryDir files; QVERIFY(package(files.path() + QStringLiteral("/package")));
        AddonManager manager(files.path() + QStringLiteral("/profile"));
        QVERIFY2(manager.installLocalPackage(files.path() + QStringLiteral("/package"), true), qPrintable(manager.errorMessage())); QVERIFY(manager.setEnabled(QStringLiteral("org.example.visual"), true));
        Audio audio; AddonVisualizers visualizers(&manager, audio.backend()); QObject surface;
        QVERIFY(visualizers.selectVisualizer(QStringLiteral("org.example.visual/spectrum"))); visualizers.setSurface(&surface, true, {320, 240});
        QVERIFY(audio.send()); QTRY_VERIFY((visualizers.refresh(), visualizers.active())); const auto first = visualizers.image();
        audio.state.playing = false; QSignalSpy changed(&visualizers, &AddonVisualizers::imageChanged); visualizers.refresh();
        QCOMPARE(changed.size(), 0); QCOMPARE(visualizers.image(), first);
        audio.state.epoch = 2; visualizers.refresh(); QVERIFY(!visualizers.active()); QVERIFY(visualizers.image().isNull());
        QVERIFY(audio.send()); QTRY_VERIFY((visualizers.refresh(), visualizers.active()));
        visualizers.invalidateTimeline(); QVERIFY(!visualizers.active());
    }
    void oneSurfaceAndExplicitArtworkSelectionReleaseTheObserver() {
        QTemporaryDir files; QVERIFY(package(files.path() + QStringLiteral("/package")));
        AddonManager manager(files.path() + QStringLiteral("/profile"));
        QVERIFY2(manager.installLocalPackage(files.path() + QStringLiteral("/package"), true), qPrintable(manager.errorMessage())); QVERIFY(manager.setEnabled(QStringLiteral("org.example.visual"), true));
        Audio audio; AddonVisualizers visualizers(&manager, audio.backend()); QObject first, second;
        QVERIFY(visualizers.selectVisualizer(QStringLiteral("org.example.visual/spectrum"))); visualizers.setSurface(&first, true, {320, 240});
        visualizers.setSurface(&second, true, {320, 240}); QCOMPARE(audio.starts, 1);
        visualizers.setSurface(&first, false, {}); visualizers.setSurface(&second, true, {320, 240}); QCOMPARE(audio.starts, 2);
        QVERIFY(visualizers.selectVisualizer({})); QVERIFY(!audio.analysis); QVERIFY(!visualizers.active());
        QCOMPARE(visualizers.selectedVisualizer(), QString{});
    }
    void malformedSceneCannotBecomeSelected() {
        QTemporaryDir files; QVERIFY(package(files.path() + QStringLiteral("/package"), true));
        AddonManager manager(files.path() + QStringLiteral("/profile"));
        QVERIFY2(manager.installLocalPackage(files.path() + QStringLiteral("/package"), true), qPrintable(manager.errorMessage())); QVERIFY(manager.setEnabled(QStringLiteral("org.example.visual"), true));
        Audio audio; AddonVisualizers visualizers(&manager, audio.backend());
        QVERIFY(!visualizers.selectVisualizer(QStringLiteral("org.example.visual/spectrum"))); QCOMPARE(audio.starts, 0);
        visualizers.close(); QVERIFY(!visualizers.selectVisualizer({}));
    }
    void decoderRejectsUnsupportedOrOversizedImages() {
        for (const QSize dimensions : {QSize(16, 16), QSize(1025, 1)}) {
            QTemporaryDir files; QImage tile(dimensions, QImage::Format_RGB32); tile.fill(Qt::red);
            QByteArray bytes; QBuffer buffer(&bytes); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(tile.save(&buffer, "PNG"));
            QVERIFY(package(files.path() + QStringLiteral("/package"), false, bytes));
            AddonManager manager(files.path() + QStringLiteral("/profile"));
            QVERIFY2(manager.installLocalPackage(files.path() + QStringLiteral("/package"), true), qPrintable(manager.errorMessage()));
            QVERIFY(manager.setEnabled(QStringLiteral("org.example.visual"), true));
            Audio audio; AddonVisualizers visualizers(&manager, audio.backend()); QObject surface;
            QVERIFY(visualizers.selectVisualizer(QStringLiteral("org.example.visual/spectrum")));
            visualizers.setSurface(&surface, true, {64, 64});
            if (dimensions.width() == 16) {
                QVERIFY(audio.send()); QTRY_VERIFY((visualizers.refresh(), visualizers.active()));
                QCOMPARE(visualizers.image().pixelColor(32, 32), QColor(Qt::red));
            } else { QCOMPARE(audio.starts, 0); QVERIFY(!visualizers.errorMessage().isEmpty()); }
        }
        QTemporaryDir files;
        QVERIFY(package(files.path() + QStringLiteral("/package"), false, QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\"></svg>")));
        AddonManager manager(files.path() + QStringLiteral("/profile"));
        QVERIFY2(manager.installLocalPackage(files.path() + QStringLiteral("/package"), true), qPrintable(manager.errorMessage()));
        QVERIFY(manager.setEnabled(QStringLiteral("org.example.visual"), true));
        Audio audio; AddonVisualizers visualizers(&manager, audio.backend()); QObject surface;
        QVERIFY(visualizers.selectVisualizer(QStringLiteral("org.example.visual/spectrum"))); visualizers.setSurface(&surface, true, {64, 64});
        QCOMPARE(audio.starts, 0); QVERIFY(!visualizers.errorMessage().isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestAddonVisualizers)
#include "test_addon_visualizers.moc"
