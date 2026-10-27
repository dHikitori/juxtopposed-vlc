// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <vlc_common.h>
#include <vlc_player.h>
#include <vlc_cxx_helpers.hpp>
#include "qt.hpp"
#include "lib/libvlc_internal.h"
#include <QTest>
#include "addon_test_application_work.hpp"
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <cmath>
#include "addons/addon_native_player_analysis.hpp"
using namespace vlc::addons;
using vlc_player_locker = vlc_locker<vlc_player_t, vlc_player_Lock, vlc_player_Unlock>;

namespace {
bool put(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath()); QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
bool package(const QString& path)
{
    const QJsonObject scene{{"contract", "visualizer.scene@1"}, {"background", "theme.background"},
        {"nodes", QJsonArray{QJsonObject{{"type", "spectrum"}, {"count", 32}, {"color", "theme.accent"}}}}};
    const QJsonObject manifest{{"formatVersion", 1}, {"id", "org.example.native-visual"}, {"name", "Native visualizer"}, {"version", "1.0.0"},
        {"publisher", "Tests"}, {"license", "GPL-2.0-or-later"}, {"source", "https://example.invalid/native-visual.git"},
        {"api", QJsonObject{{"addon", "^1.0.0"}, {"contracts", QJsonObject{{"visualizer", "^1.0.0"}}}}},
        {"files", QJsonArray{"addon.json", "scene.json", "LICENSE"}},
        {"contributions", QJsonArray{QJsonObject{{"id", "spectrum"}, {"type", "visualizer"}, {"contract", "visualizer@1"},
            {"scene", "scene.json"}, {"analysisChannels", QJsonArray{"bands"}}, {"qualityTiers", QJsonArray{"low", "normal"}}}}},
        {"permissions", QJsonObject{{"required", QJsonObject{{"audio.analysis", QJsonObject{{"channels", QJsonArray{"bands"}}}}}}}}};
    return put(path + QStringLiteral("/addon.json"), QJsonDocument(manifest).toJson())
        && put(path + QStringLiteral("/scene.json"), QJsonDocument(scene).toJson())
        && put(path + QStringLiteral("/LICENSE"), "SPDX-License-Identifier: GPL-2.0-or-later\n");
}
struct NativePlayer {
    libvlc_instance_t* instance = nullptr;
    vlc_player_t* player = nullptr;
    std::shared_ptr<AddonNativePlayerAnalysis> analysis;
    std::unique_ptr<AddonVisualizers> visualizers;
    NativePlayer()
    {
#ifdef VLC_ADDON_NATIVE_BUILD_ROOT
        qputenv("VLC_PLUGIN_PATH", QByteArray(VLC_ADDON_NATIVE_BUILD_ROOT "/modules"));
#endif
        const char* args[]{"--ignore-config", "--quiet", "--no-plugins-cache", "--no-media-library",
            "--no-metadata-network-access", "--aout=adummy", "--vout=vdummy", "--text-renderer=tdummy"};
        instance = libvlc_new(ARRAY_SIZE(args), args);
        if (instance) player = vlc_player_New(VLC_OBJECT(instance->p_libvlc_int), VLC_PLAYER_LOCK_NORMAL);
        if (player) analysis = AddonNativePlayerAnalysis::create(player);
    }
    ~NativePlayer()
    {
        visualizers.reset();
        if (analysis) analysis->close(); analysis.reset();
        if (player) vlc_player_Delete(player);
        if (instance) libvlc_release(instance);
    }
    bool start(const char* uri)
    {
        input_item_t* item = input_item_New(uri, "Native analysis source"); if (!item) return false;
        vlc_player_locker lock{player};
        const bool result = vlc_player_SetCurrentMedia(player, item) == VLC_SUCCESS && vlc_player_Start(player) == VLC_SUCCESS;
        input_item_Release(item); return result;
    }
    enum vlc_player_state state() { vlc_player_locker lock{player}; return vlc_player_GetState(player); }
};
constexpr const char* firstSource = "mock://video_track_count=0;audio_track_count=1;audio_rate=48000;audio_channels=2;length=30000000";
constexpr const char* secondSource = "mock://video_track_count=0;audio_track_count=1;audio_rate=44100;audio_channels=1;length=30000000";
}
class TestAddonNativePlayerAnalysis : public QObject
{
    Q_OBJECT
private slots:
    void outputResetWithdrawsTheOldSampleObserver()
    {
        NativePlayer native; QVERIFY(native.player); AddonAudioAnalysis audio;
        const auto backend = native.analysis->backend(); QVERIFY(backend.startAnalysis(&audio, 1));
        const auto epoch = native.analysis->statistics().value("epoch").toULongLong();
        vlc_player_aout_Reset(native.player); backend.snapshot();
        QVERIFY(!native.analysis->statistics().value("subscribed").toBool());
        QVERIFY(native.analysis->statistics().value("epoch").toULongLong() > epoch);
        QCOMPARE(native.analysis->statistics().value("outputChanges").toULongLong(), qulonglong(1));
        QVERIFY(backend.startAnalysis(&audio, 2)); backend.stopAnalysis();
        QVERIFY(!native.analysis->statistics().value("subscribed").toBool());
    }
    void actualSamplesFollowTheMediaClockAndReleaseTheObserver()
    {
        QTemporaryDir files; QVERIFY(package(files.path() + QStringLiteral("/package")));
        AddonManager manager(files.path() + QStringLiteral("/profile"));
        QVERIFY(manager.installLocalPackage(files.path() + QStringLiteral("/package"), true));
        QVERIFY(manager.setEnabled(QStringLiteral("org.example.native-visual"), true));
        NativePlayer native; QVERIFY(native.player); QVERIFY(native.analysis);
        native.visualizers = std::make_unique<AddonVisualizers>(&manager, native.analysis->backend()); QObject surface;
        QTRY_VERIFY_WITH_TIMEOUT(native.visualizers->availableVisualizers().size() > 1, 5000);
        QVERIFY(native.visualizers->selectVisualizer(QStringLiteral("org.example.native-visual/spectrum")));
        QTRY_COMPARE_WITH_TIMEOUT(native.visualizers->selectedVisualizer(), QStringLiteral("org.example.native-visual/spectrum"), 5000);
        native.visualizers->setSurface(&surface, true, {320, 240}); QVERIFY(native.start(firstSource));
        QTRY_VERIFY_WITH_TIMEOUT(native.visualizers->active(), 5000);
        QVERIFY(native.analysis->statistics().value("accepted").toULongLong() > 0);
        const auto before = native.analysis->backend().snapshot(); QVERIFY(before.epoch && before.timeUs >= 0 && before.playing);
        QSignalSpy images(native.visualizers.get(), &AddonVisualizers::imageChanged);
        QTRY_VERIFY_WITH_TIMEOUT(images.size() >= 8, 5000);
        const auto later = native.analysis->backend().snapshot(); QVERIFY(later.timeUs > before.timeUs);
        QVERIFY(!native.visualizers->image().isNull());
        native.visualizers->setSurface(&surface, false, {});
        QVERIFY(!native.analysis->statistics().value("subscribed").toBool()); QVERIFY(native.visualizers->image().isNull());
        const auto blocks = native.analysis->statistics().value("blocks"); const auto hidden = native.analysis->backend().snapshot();
        QTRY_VERIFY_WITH_TIMEOUT(native.analysis->backend().snapshot().timeUs > hidden.timeUs + 100000, 3000);
        QCOMPARE(native.analysis->statistics().value("blocks"), blocks);
        native.visualizers->setSurface(&surface, true, {320, 240}); QTRY_VERIFY_WITH_TIMEOUT(native.visualizers->active(), 5000);
        qInfo().noquote() << "Native visualizer measurements" << native.analysis->statistics();
        qInfo().noquote() << "Native render measurements" << native.visualizers->statistics();
        native.visualizers.reset(); QVERIFY(!native.analysis->statistics().value("subscribed").toBool());
    }
    void pauseSeekRateAndMediaChangesUseNewNativeEpochs()
    {
        QTemporaryDir files; QVERIFY(package(files.path() + QStringLiteral("/package")));
        AddonManager manager(files.path() + QStringLiteral("/profile"));
        QVERIFY(manager.installLocalPackage(files.path() + QStringLiteral("/package"), true));
        QVERIFY(manager.setEnabled(QStringLiteral("org.example.native-visual"), true));
        NativePlayer native; QVERIFY(native.player); native.visualizers = std::make_unique<AddonVisualizers>(&manager, native.analysis->backend()); QObject surface;
        QTRY_VERIFY_WITH_TIMEOUT(native.visualizers->availableVisualizers().size() > 1, 5000);
        QVERIFY(native.visualizers->selectVisualizer(QStringLiteral("org.example.native-visual/spectrum")));
        QTRY_COMPARE_WITH_TIMEOUT(native.visualizers->selectedVisualizer(), QStringLiteral("org.example.native-visual/spectrum"), 5000);
        native.visualizers->setSurface(&surface, true, {320, 240}); QVERIFY(native.start(firstSource));
        QTRY_VERIFY_WITH_TIMEOUT(native.visualizers->active(), 5000);
        { vlc_player_locker lock{native.player}; vlc_player_Pause(native.player); }
        QTRY_COMPARE_WITH_TIMEOUT(native.state(), VLC_PLAYER_STATE_PAUSED, 5000);
        QTRY_VERIFY(!native.analysis->backend().snapshot().playing);
        const auto frozen = native.visualizers->image(); native.visualizers->refresh(); QCOMPARE(native.visualizers->image(), frozen);
        const auto epoch = native.analysis->statistics().value("epoch").toULongLong();
        { vlc_player_locker lock{native.player}; vlc_player_SetTime(native.player, VLC_TICK_FROM_SEC(3)); vlc_player_Resume(native.player); }
        QTRY_VERIFY_WITH_TIMEOUT(native.analysis->statistics().value("epoch").toULongLong() > epoch, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(native.visualizers->active(), 5000);
        const auto rateEpoch = native.analysis->statistics().value("epoch").toULongLong();
        { vlc_player_locker lock{native.player}; vlc_player_ChangeRate(native.player, 1.5f); }
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(native.analysis->backend().snapshot().rate - 1.5) < 0.01, 5000);
        QVERIFY(native.analysis->statistics().value("epoch").toULongLong() > rateEpoch);
        const auto prior = native.analysis->statistics().value("epoch").toULongLong();
        QVERIFY(native.start(secondSource));
        QTRY_VERIFY_WITH_TIMEOUT(native.analysis->statistics().value("epoch").toULongLong() > prior, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(native.visualizers->active(), 5000);
        { vlc_player_locker lock{native.player}; vlc_player_Stop(native.player); }
        QTRY_COMPARE_WITH_TIMEOUT(native.state(), VLC_PLAYER_STATE_STOPPED, 5000);
        QTRY_VERIFY(!native.visualizers->active()); QVERIFY(native.visualizers->image().isNull());
    }
    void trackSwitchChangesTheAudioFormatWithinOneMedia()
    {
        QTemporaryDir files; QVERIFY(package(files.path() + QStringLiteral("/package")));
        AddonManager manager(files.path() + QStringLiteral("/profile"));
        QVERIFY(manager.installLocalPackage(files.path() + QStringLiteral("/package"), true));
        QVERIFY(manager.setEnabled(QStringLiteral("org.example.native-visual"), true));
        NativePlayer native; QVERIFY(native.player); native.visualizers = std::make_unique<AddonVisualizers>(&manager, native.analysis->backend()); QObject surface;
        QTRY_VERIFY_WITH_TIMEOUT(native.visualizers->availableVisualizers().size() > 1, 5000);
        QVERIFY(native.visualizers->selectVisualizer(QStringLiteral("org.example.native-visual/spectrum")));
        QTRY_COMPARE_WITH_TIMEOUT(native.visualizers->selectedVisualizer(), QStringLiteral("org.example.native-visual/spectrum"), 5000);
        native.visualizers->setSurface(&surface, true, {320, 240});
        QVERIFY(native.start("mock://video_track_count=0;audio_track_count=2;length=30000000;config=audio[0]{rate=48000,channels=2}+audio[1]{rate=44100,channels=1}"));
        QTRY_VERIFY_WITH_TIMEOUT(native.visualizers->active(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT([&native] { vlc_player_locker lock{native.player}; return vlc_player_GetTrackCount(native.player, AUDIO_ES) == 2; }(), 5000);
        input_item_t* media;
        { vlc_player_locker lock{native.player}; media = input_item_Hold(vlc_player_GetCurrentMedia(native.player)); }
        const auto releaseMedia = qScopeGuard([media] { input_item_Release(media); });
        const auto changes = native.analysis->statistics().value("formatChanges").toULongLong();
        {
            vlc_player_locker lock{native.player}; const auto* track = vlc_player_GetTrackAt(native.player, AUDIO_ES, 1); QVERIFY(track);
            vlc_player_SelectTrack(native.player, track, VLC_PLAYER_SELECT_EXCLUSIVE);
        }
        QTRY_VERIFY_WITH_TIMEOUT(native.analysis->statistics().value("formatChanges").toULongLong() > changes, 5000);
        { vlc_player_locker lock{native.player}; QCOMPARE(vlc_player_GetCurrentMedia(native.player), media); }
        QTRY_VERIFY_WITH_TIMEOUT(native.visualizers->active(), 5000);
    }
};
HAKONE_QTEST_MAIN(TestAddonNativePlayerAnalysis, QCoreApplication)
#include "test_addon_native_player_analysis.moc"
