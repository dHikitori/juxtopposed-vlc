// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <QTest>
#include <QScopeGuard>
#include <QSemaphore>
#include <atomic>
#include <limits>
#include <thread>
#include "addons/addon_visualizer_clock.hpp"
using namespace vlc::addons;
class TestAddonVisualizerClock : public QObject
{
    Q_OBJECT
    static vlc_player_timer_point point(vlc_tick_t time, double rate = 1)
    {
        vlc_player_timer_point value{}; value.ts = time + VLC_TICK_0; value.system_date = VLC_TICK_FROM_SEC(10); value.rate = rate; value.length = VLC_TICK_INVALID;
        return value;
    }
private slots:
    void initTestCase() { AddonVisualizerClock clock; QVERIFY(clock.isLockFree()); }
    void seekRejectsOldPointsUntilCompletion()
    {
        AddonVisualizerClock clock; clock.setRunning(true); const auto old = point(VLC_TICK_FROM_SEC(1)); clock.update(old);
        const auto epoch = clock.snapshot(VLC_TICK_FROM_SEC(10)).epoch; QVERIFY(epoch);
        const auto target = point(VLC_TICK_FROM_SEC(30)); clock.seek(&target); clock.update(old);
        QCOMPARE(clock.snapshot(VLC_TICK_FROM_SEC(10)).epoch, std::uint64_t(0));
        clock.seek(nullptr); QCOMPARE(clock.snapshot(VLC_TICK_FROM_SEC(10)).epoch, std::uint64_t(0));
        clock.update(target); const auto current = clock.snapshot(VLC_TICK_FROM_SEC(10));
        QVERIFY(current.epoch > epoch); QCOMPARE(current.timeUs, VLC_TICK_FROM_SEC(30));
    }
    void rateAndFormatChangesCannotReuseAnOldEpoch()
    {
        AddonVisualizerClock clock; clock.setRunning(true); clock.update(point(VLC_TICK_FROM_SEC(1)));
        const auto old = clock.snapshot(VLC_TICK_FROM_SEC(10)).epoch;
        clock.update(point(VLC_TICK_FROM_SEC(1), 1.5)); const auto rate = clock.snapshot(VLC_TICK_FROM_SEC(10));
        QVERIFY(rate.epoch > old); QCOMPARE(rate.rate, 1.5);
        clock.formatChanged(); QCOMPARE(clock.snapshot(VLC_TICK_FROM_SEC(10)).epoch, std::uint64_t(0));
        clock.update(point(VLC_TICK_FROM_SEC(2), 1.5)); QVERIFY(clock.snapshot(VLC_TICK_FROM_SEC(10)).epoch > rate.epoch);
        const auto format = clock.epoch(); clock.pause(VLC_TICK_INVALID);
        clock.update(point(VLC_TICK_FROM_SEC(3), 2)); QVERIFY(clock.snapshot(VLC_TICK_FROM_SEC(10)).epoch > format);
    }
    void pauseAndMediaWithdrawalPreserveClockOwnership()
    {
        AddonVisualizerClock clock; clock.setRunning(true); clock.update(point(VLC_TICK_FROM_SEC(1)));
        clock.pause(VLC_TICK_FROM_SEC(11)); const auto frozen = clock.snapshot(VLC_TICK_FROM_SEC(12));
        QCOMPARE(frozen.timeUs, VLC_TICK_FROM_SEC(2)); QVERIFY(!frozen.playing);
        QCOMPARE(clock.snapshot(VLC_TICK_FROM_SEC(15)).timeUs, frozen.timeUs);
        clock.mediaChanged(); QCOMPARE(clock.snapshot(VLC_TICK_FROM_SEC(15)).epoch, std::uint64_t(0));
        clock.update(point(VLC_TICK_FROM_SEC(3))); QVERIFY(clock.snapshot(VLC_TICK_FROM_SEC(10)).epoch > frozen.epoch);
        clock.setRunning(false); QCOMPARE(clock.snapshot(VLC_TICK_FROM_SEC(10)).epoch, std::uint64_t(0));
    }
    void capturedPointsCannotCrossConcurrentInvalidation_data()
    {
        QTest::addColumn<int>("operation");
        QTest::newRow("format") << 0; QTest::newRow("media") << 1; QTest::newRow("stop") << 2;
    }
    void capturedPointsCannotCrossConcurrentInvalidation()
    {
        QFETCH(int, operation); AddonVisualizerClock clock; clock.setRunning(true);
        QSemaphore entered, release; std::atomic_bool published{true};
        std::thread producer([&] {
            clock.update(point(VLC_TICK_FROM_SEC(1))); const auto captured = clock.captureEpoch();
            entered.release(); release.acquire(); published.store(clock.update(point(VLC_TICK_FROM_SEC(2)), captured));
        });
        const auto cleanup = qScopeGuard([&] { release.release(); if (producer.joinable()) producer.join(); });
        QVERIFY(entered.tryAcquire(1, 5000)); QVERIFY(clock.snapshot(VLC_TICK_FROM_SEC(10)).epoch);
        if (operation == 0) clock.formatChanged(); else if (operation == 1) clock.mediaChanged(); else clock.setRunning(false);
        QCOMPARE(clock.snapshot(VLC_TICK_FROM_SEC(10)).epoch, std::uint64_t(0));
        release.release(); producer.join(); QVERIFY(!published.load());
        QCOMPARE(clock.snapshot(VLC_TICK_FROM_SEC(10)).epoch, std::uint64_t(0));
        clock.setRunning(true); QVERIFY(clock.update(point(VLC_TICK_FROM_SEC(3))));
        QCOMPARE(clock.snapshot(VLC_TICK_FROM_SEC(10)).timeUs, VLC_TICK_FROM_SEC(3));
    }
    void publicationNeverMixesConcurrentPointFields()
    {
        AddonVisualizerClock clock; clock.setRunning(true); auto first = point(VLC_TICK_FROM_SEC(1));
        first.length = VLC_TICK_FROM_SEC(2); first.live = false;
        auto second = point(VLC_TICK_FROM_SEC(3), 2); second.length = VLC_TICK_FROM_SEC(4);
        second.live = true; second.system_date = VLC_TICK_FROM_SEC(9); second.position = 0.75;
        QSemaphore ready, start; std::atomic_bool done{false};
        std::thread producer([&] {
            clock.update(first); ready.release(); start.acquire();
            for (int index = 0; index < 50000; ++index) {
                clock.update(index % 2 ? second : first); if (!(index % 32)) std::this_thread::yield();
            }
            done.store(true);
        });
        const auto cleanup = qScopeGuard([&] { start.release(); if (producer.joinable()) producer.join(); });
        QVERIFY(ready.tryAcquire(1, 5000)); QCOMPARE(clock.snapshot(VLC_TICK_FROM_SEC(10)).timeUs, VLC_TICK_FROM_SEC(1));
        start.release(); unsigned accepted = 0; bool correct = true;
        for (int index = 0; index < 250000 && (!done.load() || index < 50000); ++index) {
            const auto value = clock.snapshot(VLC_TICK_FROM_SEC(10)); if (!value.epoch) continue;
            ++accepted;
            if (!((value.rate == 1 && value.timeUs == VLC_TICK_FROM_SEC(1)) || (value.rate == 2 && value.timeUs == VLC_TICK_FROM_SEC(5)))) { correct = false; break; }
        }
        producer.join(); QVERIFY(correct); QVERIFY(accepted > 0);
        const auto last = clock.snapshot(VLC_TICK_FROM_SEC(10)); QCOMPARE(last.rate, 2.0); QCOMPARE(last.timeUs, VLC_TICK_FROM_SEC(5));
    }
    void stopDuringASeekCannotLeaveTheNextPlaybackSeeking()
    {
        AddonVisualizerClock clock; clock.setRunning(true); const auto target = point(VLC_TICK_FROM_SEC(3));
        clock.seek(&target); clock.setRunning(false); clock.setRunning(true);
        QVERIFY(clock.update(target)); QCOMPARE(clock.snapshot(VLC_TICK_FROM_SEC(10)).timeUs, VLC_TICK_FROM_SEC(3));
    }
    void failedInterpolationHasABoundedFallback()
    {
        AddonVisualizerClock clock; clock.setRunning(true); auto invalid = point(VLC_TICK_FROM_SEC(1));
        invalid.rate = std::numeric_limits<double>::infinity(); QVERIFY(!clock.update(invalid));
        QCOMPARE(clock.snapshot(VLC_TICK_FROM_SEC(10)).epoch, std::uint64_t(0));
        auto excessive = point(VLC_TICK_FROM_SEC(1)); excessive.ts = VLC_TICK_MAX - 1; excessive.system_date = 1;
        QVERIFY(clock.update(excessive)); QCOMPARE(clock.snapshot(VLC_TICK_FROM_SEC(10)).epoch, std::uint64_t(0));
        QVERIFY(clock.update(point(VLC_TICK_FROM_SEC(2)))); QVERIFY(clock.snapshot(VLC_TICK_FROM_SEC(10)).epoch);
    }
};
QTEST_GUILESS_MAIN(TestAddonVisualizerClock)
#include "test_addon_visualizer_clock.moc"
