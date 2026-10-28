// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include "addon_test_application_work.hpp"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QBuffer>
#include <QScopeGuard>
#include "addons/addon_visualizers.hpp"
#include "addons/addon_catalogue_view.hpp"
#include "addons/addon_visualizer_catalog.hpp"
#include <mutex>
#include <condition_variable>
#include <atomic>
#include "addons/addon_visualizer_worker.hpp"
#include "addons/addon_visualizer_resources.hpp"
using namespace vlc::addons;
namespace vlc::addons {
struct VisualizerIdleTestAccess {
    static bool timerActive(const AddonVisualizers& controller) { return controller.m_timer.isActive(); }
    static QObject* visibleSurface(const AddonVisualizers& controller) {
        return controller.m_visible ? controller.m_surface.data() : nullptr;
    }
};
}

class TestAddonVisualizers : public QObject
{
    Q_OBJECT
    static bool put(const QString& path, const QByteArray& bytes) {
        QDir().mkpath(QFileInfo(path).absolutePath()); QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
    static bool package(const QString& path, bool malformed = false, const QByteArray& asset = {}, const QByteArray& runtime = {}, const QString& id = QStringLiteral("org.example.visual")) {
        QJsonObject scene{{"contract", "visualizer.scene@1"}, {"background", "theme.background"}, {"nodes", QJsonArray{
            QJsonObject{{"type", malformed ? "shader" : "spectrum"}, {"count", 32}, {"color", "theme.accent"}}}}};
        QJsonObject manifest{{"formatVersion", 1}, {"id", id}, {"name", "Visual test"}, {"version", "1.0.0"},
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
        if (!runtime.isEmpty()) {
            auto files = manifest.value("files").toArray(); files.append("addon.wasm"); manifest.insert("files", files);
            manifest.insert("runtime", QJsonObject{{"kind", "wasm-message-v1"}, {"entry", "addon.wasm"}});
            auto contributions = manifest.value("contributions").toArray(); auto entry = contributions[0].toObject();
            entry.insert("operations", QJsonArray{"updateParameters"}); contributions[0] = entry; manifest.insert("contributions", contributions);
            scene.insert("nodes", QJsonArray{QJsonObject{{"type", "spectrum"}, {"id", "bars"}, {"count", 32}, {"gain", 1}, {"color", "theme.accent"}}});
            scene.insert("parameters", QJsonObject{{"gain", QJsonObject{{"node", "bars"}, {"property", "gain"}, {"minimum", 0}, {"maximum", 4}}}});
            if (!put(path + QStringLiteral("/addon.wasm"), runtime)) return false;
        }
        return put(path + QStringLiteral("/addon.json"), QJsonDocument(manifest).toJson())
            && put(path + QStringLiteral("/scene.json"), QJsonDocument(scene).toJson())
            && put(path + QStringLiteral("/LICENSE"), "SPDX-License-Identifier: GPL-2.0-or-later\n");
    }
    struct Audio {
        AddonAudioAnalysis* analysis = nullptr; std::uint64_t generation = 0;
        AddonVisualizers::Snapshot state{1, 1000000, 1, true}; int starts = 0, stops = 0;
        int snapshots = 0, activityAttempts = 0, observationStarts = 0, observationStops = 0;
        bool observing = false, refuseActivity = false;
        QStringList events;
        std::function<void(bool)> onActivity;
        AddonVisualizers::Backend backend(bool withActivity = false) {
            auto result = AddonVisualizers::Backend{[this] { ++snapshots; events.append(QStringLiteral("snapshot")); return state; }, [this](AddonAudioAnalysis* value, std::uint64_t epoch) {
                analysis = value; generation = epoch; ++starts; return true;
            }, [this] { ++stops; analysis = nullptr; }};
            if (withActivity) result.setActivity = [this](bool active) {
                ++activityAttempts; events.append(active ? QStringLiteral("activity:on") : QStringLiteral("activity:off"));
                const bool accepted = !active || !refuseActivity;
                const bool desired = active && accepted;
                if (desired != observing) {
                    observing = desired;
                    if (desired) ++observationStarts; else ++observationStops;
                }
                const auto callback = onActivity;
                if (callback) callback(active);
                return accepted;
            };
            return result;
        }
        bool send() {
            if (!analysis) return false; std::array<float, 1024> data{}; data.fill(0.5f);
            return analysis->append(generation, AddonAudioAnalysis::Format::Float32, data.data(), sizeof data, data.size(), 1, 48000, state.timeUs, state.rate);
        }
    };
    struct ReaderGate {
        std::mutex mutex; std::condition_variable condition;
        bool block = false, entered = false;
        void wait() { std::unique_lock lock(mutex); if (!block) return; entered = true; condition.notify_all(); condition.wait(lock, [this] { return !block; }); }
        void arm() { std::lock_guard lock(mutex); block = true; entered = false; }
        bool arrived() { std::lock_guard lock(mutex); return entered; }
        void release() { { std::lock_guard lock(mutex); block = false; } condition.notify_all(); }
    };
private slots:
    void explicitArtworkBeforeFirstPublicationCannotRestoreTheOldPreference() {
        QTemporaryDir files; QVERIFY(package(files.filePath(QStringLiteral("package"))));
        AddonManager manager(files.filePath(QStringLiteral("profile")));
        QVERIFY(manager.installLocalPackage(files.filePath(QStringLiteral("package")), true));
        QVERIFY(manager.setEnabled(QStringLiteral("org.example.visual"), true));
        const auto preference = QDir(manager.storageDirectory()).filePath(QStringLiteral("visualizers.json"));
        QVERIFY(put(preference, R"({"selected":"org.example.visual/spectrum"})"));
        auto gate = std::make_shared<ReaderGate>(); gate->arm(); const auto release = qScopeGuard([gate] { gate->release(); });
        AddonCatalogueView::Backend backend;
        backend.capture = [&manager, gate]() -> std::optional<DisplayCatalogueSource> {
            auto source = manager.displayCatalogueSource(); if (!source) return {};
            auto reader = std::move(source->reader);
            source->reader = [reader = std::move(reader), gate](const auto& request, auto* result, auto* error) {
                const bool success = reader(request, result, error); gate->wait(); return success;
            };
            return source;
        };
        backend.subscribe = [&manager](std::function<void()> wake) { return manager.watchDisplayCatalogue(std::move(wake)); };
        AddonCatalogueView view(std::move(backend), AddonStreamLifetime::applicationHandle(), &manager);
        view.setObjectName(QStringLiteral("hakoneDisplayCatalogue")); QTRY_VERIFY(gate->arrived());
        Audio audio; AddonVisualizers visualizers(&manager, audio.backend(true));
        QVERIFY(visualizers.selectVisualizer({})); gate->release(); QTRY_VERIFY(view.isCurrent());
        QCOMPARE(visualizers.selectedVisualizer(), QString{}); QCOMPARE(audio.activityAttempts, 0);
        QTest::qWait(50); QCOMPARE(visualizers.selectedVisualizer(), QString{});
    }
    void attachedChooserAndHiddenPreferenceUseOnlyTheReadyView() {
        QTemporaryDir files; QVERIFY(package(files.filePath(QStringLiteral("package"))));
        AddonManager manager(files.filePath(QStringLiteral("profile")));
        QVERIFY(manager.installLocalPackage(files.filePath(QStringLiteral("package")), true));
        QVERIFY(manager.setEnabled(QStringLiteral("org.example.visual"), true));
        const auto preference = QDir(manager.storageDirectory()).filePath(QStringLiteral("visualizers.json"));
        QVERIFY(put(preference, R"({"selected":"org.example.visual/spectrum"})"));
        AddonCatalogueView view(&manager, AddonStreamLifetime::applicationHandle());
        QTRY_VERIFY(view.isCurrent()); QTRY_COMPARE(AddonRepositoryJobs::processCount(), 0u);
        const auto captures = view.captures(); QVERIFY(QFile::remove(preference));
        Audio audio; AddonVisualizers visualizers(&manager, audio.backend(true));
        QCOMPARE(visualizers.selectedVisualizer(), QStringLiteral("org.example.visual/spectrum"));
        QCOMPARE(visualizers.availableVisualizers().size(), 2);
        auto* catalog = visualizers.findChild<AddonVisualizerCatalog*>(); QVERIFY(catalog);
        QVERIFY(catalog->findChildren<AddonUiAssets*>().isEmpty());
        QVERIFY(!visualizers.loading()); QCOMPARE(AddonRepositoryJobs::processCount(), 0u);
        QTest::qWait(100); QCOMPARE(view.captures(), captures); QCOMPARE(audio.activityAttempts, 0); QCOMPARE(audio.snapshots, 0);
        view.close(); QVERIFY(!visualizers.selectVisualizer(QStringLiteral("org.example.visual/spectrum")));
        QVERIFY(visualizers.selectVisualizer({})); // Artwork remains an available stop action.
    }
    void anUnrelatedRefreshRetainsOnlyImmutableOutput_data() {
        QTest::addColumn<int>("change");
        QTest::newRow("same-partition") << 0;
        QTest::newRow("media-change-while-paused") << 1;
        QTest::newRow("selected-withdrawal-while-paused") << 2;
    }
    void anUnrelatedRefreshRetainsOnlyImmutableOutput() {
        QFETCH(int, change);
        QTemporaryDir files; QVERIFY(package(files.filePath(QStringLiteral("package"))));
        QVERIFY(package(files.filePath(QStringLiteral("other")), false, {}, {}, QStringLiteral("org.example.other")));
        AddonManager manager(files.filePath(QStringLiteral("profile")));
        QVERIFY(manager.installLocalPackage(files.filePath(QStringLiteral("package")), true));
        QVERIFY(manager.setEnabled(QStringLiteral("org.example.visual"), true));
        QVERIFY(manager.installLocalPackage(files.filePath(QStringLiteral("other")), true));
        QVERIFY(manager.setEnabled(QStringLiteral("org.example.other"), true));
        auto gate = std::make_shared<ReaderGate>(); const auto release = qScopeGuard([gate] { gate->release(); });
        AddonCatalogueView::Backend backend;
        backend.capture = [&manager, gate]() -> std::optional<DisplayCatalogueSource> {
            auto source = manager.displayCatalogueSource(); if (!source) return {};
            auto reader = std::move(source->reader);
            source->reader = [reader = std::move(reader), gate](const auto& request, auto* result, auto* error) { gate->wait(); return reader(request, result, error); };
            return source;
        };
        backend.subscribe = [&manager](std::function<void()> wake) { return manager.watchDisplayCatalogue(std::move(wake)); };
        AddonCatalogueView view(std::move(backend), AddonStreamLifetime::applicationHandle(), &manager);
        view.setObjectName(QStringLiteral("hakoneDisplayCatalogue"));
        connect(&manager, &AddonManager::revisionChanged, &view, &AddonCatalogueView::inputsChanged);
        QTRY_VERIFY(view.isCurrent());
        Audio audio; AddonVisualizers visualizers(&manager, audio.backend(true)); QObject surface;
        QVERIFY(visualizers.selectVisualizer(QStringLiteral("org.example.visual/spectrum")));
        QTRY_VERIFY(!visualizers.loading()); visualizers.setSurface(&surface, true, {320, 240});
        QVERIFY(audio.send()); QTRY_VERIFY(visualizers.active()); const auto picture = visualizers.image(); QVERIFY(!picture.isNull());
        gate->arm(); AddonManager other(manager.storageDirectory()); QVERIFY(other.setEnabled(QStringLiteral("org.example.other"), false));
        QTRY_VERIFY(gate->arrived()); QVERIFY(!view.isCurrent()); QVERIFY(!audio.analysis); QVERIFY(!audio.observing);
        QCOMPARE(visualizers.image(), picture); QVERIFY(visualizers.active());
        const auto snapshots = audio.snapshots; QTest::qWait(80); QCOMPARE(audio.snapshots, snapshots);
        if (change == 1) { visualizers.mediaChanged(); QVERIFY(visualizers.image().isNull()); QVERIFY(!visualizers.active()); }
        if (change == 2) { QVERIFY(manager.setEnabled(QStringLiteral("org.example.visual"), false)); QVERIFY(visualizers.image().isNull()); QVERIFY(!visualizers.active()); }
        gate->release(); QTRY_VERIFY(view.isCurrent());
        if (change == 2) { QTRY_VERIFY(!visualizers.loading()); QVERIFY(!audio.analysis); QVERIFY(visualizers.image().isNull()); }
        else {
            QTRY_VERIFY(audio.analysis); if (change == 0) QCOMPARE(visualizers.image(), picture);
            QVERIFY(audio.send()); QTRY_VERIFY(visualizers.active()); QVERIFY(!visualizers.image().isNull());
        }
        visualizers.close(); QVERIFY(!audio.analysis); QVERIFY(!audio.observing);
    }
    void visibleArtworkHasNoRefreshTimerOrObservation() {
        QTemporaryDir files; AddonManager manager(files.filePath(QStringLiteral("profile")));
        Audio audio; AddonVisualizers visualizers(&manager, audio.backend(true)); QObject surface;
        visualizers.setSurface(&surface, true, {320, 240});
        QVERIFY(!VisualizerIdleTestAccess::timerActive(visualizers));
        QTest::qWait(150);
        QVERIFY(!VisualizerIdleTestAccess::timerActive(visualizers));
        QCOMPARE(audio.activityAttempts, 0); QCOMPARE(audio.snapshots, 0); QCOMPARE(audio.starts, 0);
        QVERIFY(!audio.observing);
    }
    void observationStartsBeforeSnapshotAndSurvivesMediaEpochReset() {
        QTemporaryDir files; QVERIFY(package(files.filePath(QStringLiteral("package"))));
        AddonManager manager(files.filePath(QStringLiteral("profile")));
        QVERIFY(manager.installLocalPackage(files.filePath(QStringLiteral("package")), true));
        QVERIFY(manager.setEnabled(QStringLiteral("org.example.visual"), true));
        Audio audio; AddonVisualizers visualizers(&manager, audio.backend(true)); QObject surface;
        QTRY_VERIFY(visualizers.availableVisualizers().size() > 1);
        QVERIFY(visualizers.selectVisualizer(QStringLiteral("org.example.visual/spectrum")));
        QTRY_VERIFY(!visualizers.loading()); QCOMPARE(audio.activityAttempts, 0);
        QVERIFY(!VisualizerIdleTestAccess::timerActive(visualizers));
        visualizers.setSurface(&surface, true, {320, 240});
        QVERIFY(audio.events.size() >= 2); QCOMPARE(audio.events[0], QStringLiteral("activity:on"));
        QCOMPARE(audio.events[1], QStringLiteral("snapshot")); QCOMPARE(audio.observationStarts, 1);
        QVERIFY(audio.send()); QTRY_VERIFY((visualizers.refresh(), visualizers.active()));
        audio.state.epoch = 2; visualizers.refresh();
        QCOMPARE(audio.observationStarts, 1); QCOMPARE(audio.observationStops, 0); QVERIFY(audio.observing);
        QVERIFY(audio.send()); QTRY_VERIFY((visualizers.refresh(), visualizers.active()));
        visualizers.invalidateTimeline(); visualizers.refresh();
        QCOMPARE(audio.observationStarts, 1); QCOMPARE(audio.observationStops, 0);
        visualizers.setSurface(&surface, false, {});
        QCOMPARE(audio.observationStops, 1); QVERIFY(!VisualizerIdleTestAccess::timerActive(visualizers));
    }
    void refusedObservationDoesNotRetryFromTheRefreshTimer() {
        QTemporaryDir files; QVERIFY(package(files.filePath(QStringLiteral("package"))));
        AddonManager manager(files.filePath(QStringLiteral("profile")));
        QVERIFY(manager.installLocalPackage(files.filePath(QStringLiteral("package")), true));
        QVERIFY(manager.setEnabled(QStringLiteral("org.example.visual"), true));
        Audio audio; audio.refuseActivity = true;
        AddonVisualizers visualizers(&manager, audio.backend(true)); QObject surface;
        QTRY_VERIFY(visualizers.availableVisualizers().size() > 1);
        QVERIFY(visualizers.selectVisualizer(QStringLiteral("org.example.visual/spectrum")));
        QTRY_VERIFY(!visualizers.loading()); visualizers.setSurface(&surface, true, {320, 240});
        QCOMPARE(audio.activityAttempts, 1); QCOMPARE(audio.snapshots, 0); QVERIFY(!audio.observing);
        QVERIFY(!visualizers.errorMessage().isEmpty()); QVERIFY(!VisualizerIdleTestAccess::timerActive(visualizers));
        QTest::qWait(180); visualizers.refresh(); QCOMPARE(audio.activityAttempts, 1);
        visualizers.setSurface(&surface, false, {}); audio.refuseActivity = false;
        visualizers.setSurface(&surface, true, {320, 240});
        QCOMPARE(audio.observationStarts, 1); QVERIFY(audio.observing);
    }
    void anActivityCallbackCanDestroyItsController() {
        QTemporaryDir files; QVERIFY(package(files.filePath(QStringLiteral("package"))));
        AddonManager manager(files.filePath(QStringLiteral("profile")));
        QVERIFY(manager.installLocalPackage(files.filePath(QStringLiteral("package")), true));
        QVERIFY(manager.setEnabled(QStringLiteral("org.example.visual"), true));
        Audio audio; AddonVisualizers* controller = new AddonVisualizers(&manager, audio.backend(true));
        const auto cleanup = qScopeGuard([&] { delete controller; }); QObject surface;
        QTRY_VERIFY(controller->availableVisualizers().size() > 1);
        QVERIFY(controller->selectVisualizer(QStringLiteral("org.example.visual/spectrum")));
        QTRY_VERIFY(!controller->loading());
        audio.onActivity = [&](bool active) { if (active) { delete controller; controller = nullptr; } };
        controller->setSurface(&surface, true, {320, 240});
        QVERIFY(!controller); QVERIFY(!audio.observing); QVERIFY(!audio.analysis);
        QCOMPARE(audio.observationStarts, 1); QCOMPARE(audio.observationStops, 1);
    }
    void retiringActivityCanReenterCloseWithoutRemovingTwice() {
        QTemporaryDir files; QVERIFY(package(files.filePath(QStringLiteral("package"))));
        AddonManager manager(files.filePath(QStringLiteral("profile")));
        QVERIFY(manager.installLocalPackage(files.filePath(QStringLiteral("package")), true));
        QVERIFY(manager.setEnabled(QStringLiteral("org.example.visual"), true));
        Audio audio; AddonVisualizers visualizers(&manager, audio.backend(true)); QObject surface;
        QTRY_VERIFY(visualizers.availableVisualizers().size() > 1);
        QVERIFY(visualizers.selectVisualizer(QStringLiteral("org.example.visual/spectrum")));
        QTRY_VERIFY(!visualizers.loading()); visualizers.setSurface(&surface, true, {320, 240});
        audio.onActivity = [&](bool active) { if (!active) visualizers.close(); };
        visualizers.setSurface(&surface, false, {});
        QCOMPARE(audio.observationStarts, 1); QCOMPARE(audio.observationStops, 1);
        QVERIFY(!audio.observing); QVERIFY(!audio.analysis); QVERIFY(!VisualizerIdleTestAccess::timerActive(visualizers));
    }
    void aRetiringSurfaceCannotClearItsReentrantReplacement_data() {
        QTest::addColumn<bool>("destroySurface");
        QTest::newRow("hide") << false; QTest::newRow("destroy") << true;
    }
    void aRetiringSurfaceCannotClearItsReentrantReplacement() {
        QFETCH(bool, destroySurface);
        QTemporaryDir files; QVERIFY(package(files.filePath(QStringLiteral("package"))));
        AddonManager manager(files.filePath(QStringLiteral("profile")));
        QVERIFY(manager.installLocalPackage(files.filePath(QStringLiteral("package")), true));
        QVERIFY(manager.setEnabled(QStringLiteral("org.example.visual"), true));
        Audio audio; AddonVisualizers visualizers(&manager, audio.backend(true));
        auto previous = std::make_unique<QObject>(); QObject replacement;
        QTRY_VERIFY(visualizers.availableVisualizers().size() > 1);
        QVERIFY(visualizers.selectVisualizer(QStringLiteral("org.example.visual/spectrum")));
        QTRY_VERIFY(!visualizers.loading()); visualizers.setSurface(previous.get(), true, {320, 240});
        QVERIFY(audio.send()); QTRY_VERIFY((visualizers.refresh(), visualizers.active()));
        bool replaced = false;
        const auto replacementConnection = connect(&visualizers, &AddonVisualizers::changed, &visualizers, [&] {
            if (!replaced && !VisualizerIdleTestAccess::visibleSurface(visualizers)) {
                replaced = true; visualizers.setSurface(&replacement, true, {320, 240});
            }
        });
        if (destroySurface) previous.reset(); else visualizers.setSurface(previous.get(), false, {});
        QVERIFY(replaced); QCOMPARE(VisualizerIdleTestAccess::visibleSurface(visualizers), &replacement);
        QVERIFY(VisualizerIdleTestAccess::timerActive(visualizers)); QVERIFY(audio.observing);
        QVERIFY(audio.send()); QTRY_VERIFY((visualizers.refresh(), visualizers.active()));
        QCOMPARE(VisualizerIdleTestAccess::visibleSurface(visualizers), &replacement);
        disconnect(replacementConnection); visualizers.close();
    }
    void aResourceCloseObserverCanDeleteItsController() {
        QTemporaryDir files; QVERIFY(package(files.filePath(QStringLiteral("package"))));
        AddonManager manager(files.filePath(QStringLiteral("profile")));
        QVERIFY(manager.installLocalPackage(files.filePath(QStringLiteral("package")), true));
        QVERIFY(manager.setEnabled(QStringLiteral("org.example.visual"), true));
        Audio audio; auto* controller = new AddonVisualizers(&manager, audio.backend(true));
        const auto cleanup = qScopeGuard([&] { delete controller; }); QObject surface;
        QTRY_VERIFY(controller->availableVisualizers().size() > 1);
        QVERIFY(controller->selectVisualizer(QStringLiteral("org.example.visual/spectrum")));
        QTRY_VERIFY(!controller->loading()); controller->setSurface(&surface, true, {320, 240});
        QVERIFY(audio.observing);
        auto* resources = controller->findChild<AddonVisualizerResources*>(); QVERIFY(resources);
        connect(resources, &AddonVisualizerResources::changed, resources, [&] {
            auto* retired = controller; controller = nullptr; delete retired;
        });
        controller->close();
        QVERIFY(!controller); QVERIFY(!audio.observing); QVERIFY(!audio.analysis);
        QCOMPARE(audio.observationStarts, 1); QCOMPARE(audio.observationStops, 1);
    }
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
        QTRY_VERIFY_WITH_TIMEOUT(visualizers.availableVisualizers().size() > 1, 5000);
        QVERIFY(visualizers.selectVisualizer(QStringLiteral("org.example.visual/spectrum")));
        QTRY_VERIFY_WITH_TIMEOUT(!visualizers.loading(), 5000);
        QCOMPARE(audio.starts, 0); QVERIFY(!visualizers.active());
        visualizers.setSurface(&surface, true, {320, 240}); QCOMPARE(audio.starts, 1); QVERIFY(!visualizers.active());
        QVERIFY(audio.send()); QTRY_VERIFY((visualizers.refresh(), visualizers.active())); QVERIFY(!visualizers.image().isNull());
        visualizers.setSurface(&surface, false, {}); QCOMPARE(audio.stops, 1); QVERIFY(!visualizers.active());
        QVERIFY(visualizers.image().isNull());
        visualizers.setSurface(&surface, true, {320, 240}); QCOMPARE(audio.starts, 2);
        QVERIFY(manager.setEnabled(QStringLiteral("org.example.visual"), false)); QVERIFY(!visualizers.active());
        QVERIFY(!audio.analysis); QTRY_VERIFY_WITH_TIMEOUT(!visualizers.errorMessage().isEmpty(), 5000);
    }
    void mediaChangeDropsOldFramesAndPauseFreezesOutput() {
        QTemporaryDir files; QVERIFY(package(files.path() + QStringLiteral("/package")));
        AddonManager manager(files.path() + QStringLiteral("/profile"));
        QVERIFY2(manager.installLocalPackage(files.path() + QStringLiteral("/package"), true), qPrintable(manager.errorMessage())); QVERIFY(manager.setEnabled(QStringLiteral("org.example.visual"), true));
        Audio audio; AddonVisualizers visualizers(&manager, audio.backend()); QObject surface;
        QTRY_VERIFY_WITH_TIMEOUT(visualizers.availableVisualizers().size() > 1, 5000);
        QVERIFY(visualizers.selectVisualizer(QStringLiteral("org.example.visual/spectrum")));
        QTRY_VERIFY_WITH_TIMEOUT(!visualizers.loading(), 5000); visualizers.setSurface(&surface, true, {320, 240});
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
        QTRY_VERIFY_WITH_TIMEOUT(visualizers.availableVisualizers().size() > 1, 5000);
        QVERIFY(visualizers.selectVisualizer(QStringLiteral("org.example.visual/spectrum")));
        QTRY_VERIFY_WITH_TIMEOUT(!visualizers.loading(), 5000); visualizers.setSurface(&first, true, {320, 240});
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
        QTRY_VERIFY_WITH_TIMEOUT(visualizers.availableVisualizers().size() > 1, 5000);
        QVERIFY(visualizers.selectVisualizer(QStringLiteral("org.example.visual/spectrum"))); QTRY_VERIFY_WITH_TIMEOUT(!visualizers.loading(), 5000);
        QCOMPARE(visualizers.selectedVisualizer(), QString{}); QVERIFY(!visualizers.errorMessage().isEmpty()); QCOMPARE(audio.starts, 0);
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
            QTRY_VERIFY_WITH_TIMEOUT(visualizers.availableVisualizers().size() > 1, 5000);
        QVERIFY(visualizers.selectVisualizer(QStringLiteral("org.example.visual/spectrum")));
        QTRY_VERIFY_WITH_TIMEOUT(!visualizers.loading(), 5000);
            visualizers.setSurface(&surface, true, {64, 64});
            if (dimensions.width() == 16) {
                QVERIFY(audio.send()); QTRY_VERIFY((visualizers.refresh(), visualizers.active()));
                QCOMPARE(visualizers.image().pixelColor(32, 32), QColor(Qt::red));
            } else { QCOMPARE(audio.starts, 0); QTRY_VERIFY_WITH_TIMEOUT(!visualizers.errorMessage().isEmpty(), 5000); }
        }
        QTemporaryDir files;
        QVERIFY(package(files.path() + QStringLiteral("/package"), false, QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"16\" height=\"16\"></svg>")));
        AddonManager manager(files.path() + QStringLiteral("/profile"));
        QVERIFY2(manager.installLocalPackage(files.path() + QStringLiteral("/package"), true), qPrintable(manager.errorMessage()));
        QVERIFY(manager.setEnabled(QStringLiteral("org.example.visual"), true));
        Audio audio; AddonVisualizers visualizers(&manager, audio.backend()); QObject surface;
        QTRY_VERIFY_WITH_TIMEOUT(visualizers.availableVisualizers().size() > 1, 5000);
        QVERIFY(visualizers.selectVisualizer(QStringLiteral("org.example.visual/spectrum")));
        QTRY_VERIFY_WITH_TIMEOUT(!visualizers.loading(), 5000); visualizers.setSurface(&surface, true, {64, 64});
        QCOMPARE(audio.starts, 0); QTRY_VERIFY_WITH_TIMEOUT(!visualizers.errorMessage().isEmpty(), 5000);
    }
    void oldAnalysisRestoresArtworkAndFreshAnalysisCanResume() {
        QTemporaryDir files; QVERIFY(package(files.filePath(QStringLiteral("package"))));
        AddonManager manager(files.filePath(QStringLiteral("profile"))); QVERIFY(manager.installLocalPackage(files.filePath(QStringLiteral("package")), true));
        QVERIFY(manager.setEnabled(QStringLiteral("org.example.visual"), true)); Audio audio; AddonVisualizers visualizers(&manager, audio.backend()); QObject surface;
        QTRY_VERIFY_WITH_TIMEOUT(visualizers.availableVisualizers().size() > 1, 5000); QVERIFY(visualizers.selectVisualizer(QStringLiteral("org.example.visual/spectrum")));
        QTRY_VERIFY_WITH_TIMEOUT(!visualizers.loading(), 5000); visualizers.setSurface(&surface, true, {320, 240});
        QVERIFY(audio.send()); QTRY_VERIFY_WITH_TIMEOUT(visualizers.active(), 5000);
        audio.state.timeUs += 300001; visualizers.refresh(); QVERIFY(!visualizers.active()); QVERIFY(visualizers.image().isNull()); QVERIFY(audio.analysis);
        QVERIFY(audio.send()); QTRY_VERIFY_WITH_TIMEOUT(visualizers.active(), 5000);
    }
    void stopCanReenterCloseWithoutRepeatingTheObserverRemoval() {
        QTemporaryDir files; QVERIFY(package(files.filePath(QStringLiteral("package"))));
        AddonManager manager(files.filePath(QStringLiteral("profile"))); QVERIFY(manager.installLocalPackage(files.filePath(QStringLiteral("package")), true));
        QVERIFY(manager.setEnabled(QStringLiteral("org.example.visual"), true)); Audio audio; auto backend = audio.backend();
        AddonVisualizers* current = nullptr; const auto stop = backend.stopAnalysis;
        backend.stopAnalysis = [&] { stop(); current->close(); };
        AddonVisualizers visualizers(&manager, backend); current = &visualizers; QObject surface;
        QTRY_VERIFY_WITH_TIMEOUT(visualizers.availableVisualizers().size() > 1, 5000); QVERIFY(visualizers.selectVisualizer(QStringLiteral("org.example.visual/spectrum")));
        QTRY_VERIFY_WITH_TIMEOUT(!visualizers.loading(), 5000); visualizers.setSurface(&surface, true, {320, 240}); QCOMPARE(audio.starts, 1);
        visualizers.setSurface(&surface, false, {}); QCOMPARE(audio.stops, 1); QVERIFY(!audio.analysis); QVERIFY(!visualizers.active());
    }
    void startingAnObserverCanRemoveTheController() {
        QTemporaryDir files; QVERIFY(package(files.filePath(QStringLiteral("package"))));
        AddonManager manager(files.filePath(QStringLiteral("profile"))); QVERIFY(manager.installLocalPackage(files.filePath(QStringLiteral("package")), true));
        QVERIFY(manager.setEnabled(QStringLiteral("org.example.visual"), true)); Audio audio; auto backend = audio.backend();
        AddonVisualizers* controller = nullptr; const auto start = backend.startAnalysis;
        backend.startAnalysis = [&](AddonAudioAnalysis* target, std::uint64_t epoch) { const bool result = start(target, epoch); delete controller; controller = nullptr; return result; };
        controller = new AddonVisualizers(&manager, backend); const auto cleanup = qScopeGuard([&] { delete controller; }); QObject surface;
        QTRY_VERIFY_WITH_TIMEOUT(controller->availableVisualizers().size() > 1, 5000); QVERIFY(controller->selectVisualizer(QStringLiteral("org.example.visual/spectrum")));
        QTRY_VERIFY_WITH_TIMEOUT(!controller->loading(), 5000); controller->setSurface(&surface, true, {320, 240});
        QVERIFY(!controller); QVERIFY(!audio.analysis);
    }
    void slowRenderingReducesDeclaredQualityThenRestoresArtwork() {
        QTemporaryDir files; QVERIFY(package(files.filePath(QStringLiteral("package"))));
        AddonManager manager(files.filePath(QStringLiteral("profile"))); QVERIFY(manager.installLocalPackage(files.filePath(QStringLiteral("package")), true));
        QVERIFY(manager.setEnabled(QStringLiteral("org.example.visual"), true)); Audio audio;
        AddonVisualizers visualizers(&manager, audio.backend(), nullptr, [](AddonVisualScene& renderer, const AddonVisualizerRenderQueue::Request& request) {
            QTest::qSleep(10);
            return renderer.render(request.scene, request.analysis, request.mediaTimeUs, request.size, request.colors, request.quality, request.reducedMotion, request.assets);
        });
        QObject surface; QTRY_VERIFY_WITH_TIMEOUT(visualizers.availableVisualizers().size() > 1, 5000); QVERIFY(visualizers.selectVisualizer(QStringLiteral("org.example.visual/spectrum")));
        QTRY_VERIFY_WITH_TIMEOUT(!visualizers.loading(), 5000); visualizers.setSurface(&surface, true, {320, 240}); QVERIFY(audio.send());
        for (int index = 0; index < 16 && visualizers.errorMessage().isEmpty(); ++index) {
            const auto count = visualizers.statistics().value("rendered").toULongLong();
            visualizers.setColors({Qt::black, Qt::white, QColor(index + 1, 127, 127)}); visualizers.refresh();
            QTRY_VERIFY_WITH_TIMEOUT(visualizers.statistics().value("rendered").toULongLong() > count, 5000);
        }
        QCOMPARE(visualizers.quality(), 0); QVERIFY(!visualizers.errorMessage().isEmpty()); QVERIFY(!visualizers.active()); QVERIFY(!audio.analysis);
    }
    void aWorkerReceivesOnlyGrantedAnalysisAndStopsForReducedMotion() {
        QTemporaryDir files; const auto request = files.filePath(QStringLiteral("request.json"));
        const auto runtime = QJsonDocument(QJsonObject{{"mode", "pending"}, {"request", request}}).toJson();
        QVERIFY(package(files.filePath(QStringLiteral("package")), false, {}, runtime));
        AddonManager manager(files.filePath(QStringLiteral("profile"))); manager.setWorkerExecutable(QStringLiteral(VLC_ADDON_VISUALIZER_PROCESS_FIXTURE));
        QVERIFY(manager.installLocalPackage(files.filePath(QStringLiteral("package")), true)); QVERIFY(manager.setEnabled(QStringLiteral("org.example.visual"), true));
        Audio audio; AddonVisualizers visualizers(&manager, audio.backend()); QObject surface;
        QTRY_VERIFY_WITH_TIMEOUT(visualizers.availableVisualizers().size() > 1, 5000); QVERIFY(visualizers.selectVisualizer(QStringLiteral("org.example.visual/spectrum")));
        QTRY_VERIFY_WITH_TIMEOUT(!visualizers.loading(), 5000); visualizers.setSurface(&surface, true, {320, 240}); QVERIFY(audio.send());
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(request).size() > 0, 5000); QFile packet(request); QVERIFY(packet.open(QIODevice::ReadOnly));
        const auto context = QJsonDocument::fromJson(packet.readAll()).object().value("context").toObject();
        const auto projection = context.value("analysis").toObject(); QCOMPARE(projection.keys(), QStringList{QStringLiteral("bands")});
        QCOMPARE(projection.value("bands").toArray().size(), 64); QVERIFY(!context.contains("media"));
        visualizers.setReducedMotion(true); QTRY_COMPARE_WITH_TIMEOUT(AddonVisualizerWorker::liveWorkers(), 0, 5000);
        QVERIFY(audio.analysis); visualizers.setSurface(&surface, false, {}); QVERIFY(!audio.analysis);
    }
    void staleSlowResultsStillTriggerArtworkFallback() {
        QTemporaryDir files; QVERIFY(package(files.filePath(QStringLiteral("package"))));
        AddonManager manager(files.filePath(QStringLiteral("profile"))); QVERIFY(manager.installLocalPackage(files.filePath(QStringLiteral("package")), true));
        QVERIFY(manager.setEnabled(QStringLiteral("org.example.visual"), true)); Audio audio; AddonVisualizers visualizers(&manager, audio.backend()); QObject surface;
        QTRY_VERIFY_WITH_TIMEOUT(visualizers.availableVisualizers().size() > 1, 5000); QVERIFY(visualizers.selectVisualizer(QStringLiteral("org.example.visual/spectrum")));
        QTRY_VERIFY_WITH_TIMEOUT(!visualizers.loading(), 5000); visualizers.setSurface(&surface, true, {320, 240});
        auto* queue = visualizers.findChild<AddonVisualizerRenderQueue*>(); QVERIFY(queue);
        const auto generation = visualizers.statistics().value("generation").toULongLong(); QImage image(1, 1, QImage::Format_RGB32); image.fill(Qt::red);
        for (int frame = 0; frame < 3; ++frame) queue->frameReady(image, 9000000, generation, 0, frame + 1, 1, 0);
        QCOMPARE(visualizers.quality(), 0); QVERIFY(!visualizers.active());
        for (int frame = 0; frame < 8; ++frame) queue->frameReady(image, 9000000, generation, 0, frame + 4, 0, 0);
        QVERIFY(!visualizers.errorMessage().isEmpty()); QVERIFY(!audio.analysis); QVERIFY(!visualizers.active());
    }
};
HAKONE_QTEST_MAIN(TestAddonVisualizers, QCoreApplication)
#include "test_addon_visualizers.moc"
