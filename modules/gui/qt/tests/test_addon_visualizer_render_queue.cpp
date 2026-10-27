// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QSemaphore>
#include <QScopeGuard>
#include <QJsonArray>
#include <atomic>
#include <thread>
#include "addons/addon_visualizer_render_queue.hpp"
using namespace vlc::addons;
class TestAddonVisualizerRenderQueue : public QObject
{
    Q_OBJECT
    static AddonVisualizerRenderQueue::Request request(quint64 sequence = 1)
    {
        AddonVisualizerRenderQueue::Request value;
        value.scene = {{"contract", "visualizer.scene@1"}, {"background", "#ff0000"}, {"nodes", QJsonArray{}}};
        value.analysis.epoch = 1; value.analysis.sequence = sequence; value.mediaTimeUs = 1000000;
        value.size = {640, 480}; value.colors = {Qt::black, Qt::white, Qt::green}; value.current = [] { return true; }; return value;
    }
    struct Gate { QSemaphore entered, release; };
private slots:
    void realPainterUsesBoundedPixelsOffTheOwnerThread()
    {
        AddonVisualizerRenderQueue queue; QSignalSpy images(&queue, &AddonVisualizerRenderQueue::frameReady);
        QVERIFY(queue.submit(request())); QTRY_COMPARE_WITH_TIMEOUT(images.size(), 1, 5000);
        const auto image = qvariant_cast<QImage>(images[0][0]); QCOMPARE(image.size(), QSize(512, 384));
        QCOMPARE(image.pixelColor(100, 100), QColor(Qt::red)); QVERIFY(images[0][1].toLongLong() > 0);
        QVERIFY(queue.statistics().value("maximumCostNs").toLongLong() > 0);
    }
    void overloadKeepsOnlyTheLatestPendingFrameAndTheOwnerRuns()
    {
        const auto gate = std::make_shared<Gate>(); const auto release = qScopeGuard([gate] { gate->release.release(4); });
        std::atomic_int calls{0};
        AddonVisualizerRenderQueue queue([&](AddonVisualScene& renderer, const AddonVisualizerRenderQueue::Request& frame) {
            if (calls.fetch_add(1) == 0) { gate->entered.release(); gate->release.acquire(); }
            return renderer.render(frame.scene, frame.analysis, frame.mediaTimeUs, frame.size, frame.colors, frame.quality, false);
        });
        QSignalSpy images(&queue, &AddonVisualizerRenderQueue::frameReady); QVERIFY(queue.submit(request(1)));
        QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available() > 0, 5000);
        bool eventRan = false; QTimer::singleShot(0, &queue, [&] { eventRan = true; }); QTRY_VERIFY(eventRan);
        QVERIFY(queue.submit(request(2))); QVERIFY(queue.submit(request(3))); QCOMPARE(queue.statistics().value("replaced").toULongLong(), qulonglong(1));
        gate->release.release(); QTRY_COMPARE_WITH_TIMEOUT(images.size(), 2, 5000);
        QCOMPARE(images[0][4].toULongLong(), qulonglong(1)); QCOMPARE(images[1][4].toULongLong(), qulonglong(3));
    }
    void destructionDoesNotWaitAndRetiringWorkKeepsTheGlobalSlot()
    {
        const auto gate = std::make_shared<Gate>(); const auto release = qScopeGuard([gate] { gate->release.release(4); });
        auto queue = std::make_unique<AddonVisualizerRenderQueue>([gate](AddonVisualScene&, const AddonVisualizerRenderQueue::Request&) {
            gate->entered.release(); gate->release.acquire(); return QImage(1, 1, QImage::Format_RGB32);
        });
        QVERIFY(queue->submit(request())); QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available() > 0, 5000);
        QElapsedTimer elapsed; elapsed.start(); queue.reset(); QVERIFY(elapsed.elapsed() < 100);
        QCOMPARE(AddonVisualizerRenderQueue::liveJobs(), 1);
        AddonVisualizerRenderQueue next; QSignalSpy images(&next, &AddonVisualizerRenderQueue::frameReady);
        QVERIFY(next.submit(request())); QCOMPARE(AddonVisualizerRenderQueue::liveJobs(), 1); QVERIFY(images.isEmpty());
        gate->release.release(); QTRY_COMPARE_WITH_TIMEOUT(images.size(), 1, 5000);
        QTRY_COMPARE(AddonVisualizerRenderQueue::liveJobs(), 0);
    }
    void authorityChangesAfterTheWorkerGuardBlockDelivery()
    {
        struct Control { std::atomic_bool live{true}; std::atomic_int workerChecks{0}; QSemaphore admitted; };
        const auto control = std::make_shared<Control>(); const auto owner = std::this_thread::get_id();
        auto frame = request(); frame.current = [control, owner] {
            const bool value = control->live.load();
            if (std::this_thread::get_id() != owner && control->workerChecks.fetch_add(1) == 1) control->admitted.release();
            return value;
        };
        AddonVisualizerRenderQueue queue; QSignalSpy images(&queue, &AddonVisualizerRenderQueue::frameReady);
        QVERIFY(queue.submit(std::move(frame))); QVERIFY(control->admitted.tryAcquire(1, 5000)); control->live.store(false);
        QTRY_VERIFY_WITH_TIMEOUT(!queue.statistics().value("running").toBool(), 5000); QVERIFY(images.isEmpty());
    }
    void timelineInvalidationDropsAnActiveResult()
    {
        const auto gate = std::make_shared<Gate>(); const auto release = qScopeGuard([gate] { gate->release.release(4); });
        AddonVisualizerRenderQueue queue([gate](AddonVisualScene&, const AddonVisualizerRenderQueue::Request&) {
            gate->entered.release(); gate->release.acquire(); return QImage(1, 1, QImage::Format_RGB32);
        });
        QSignalSpy images(&queue, &AddonVisualizerRenderQueue::frameReady); QVERIFY(queue.submit(request()));
        QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available() > 0, 5000); queue.invalidate(); gate->release.release();
        QTRY_VERIFY_WITH_TIMEOUT(!queue.statistics().value("running").toBool(), 5000); QVERIFY(images.isEmpty());
    }
    void aFrameObserverCanRemoveTheQueue()
    {
        auto* queue = new AddonVisualizerRenderQueue; QPointer<AddonVisualizerRenderQueue> owner(queue);
        connect(queue, &AddonVisualizerRenderQueue::frameReady, queue, [queue] { delete queue; });
        QVERIFY(queue->submit(request())); QTRY_VERIFY_WITH_TIMEOUT(!owner, 5000);
    }
    void sceneCostsAreMeasuredAtEachBoundedTier_data()
    {
        QTest::addColumn<QJsonObject>("node");
        QTest::newRow("spectrum") << QJsonObject{{"type", "spectrum"}, {"count", 64}, {"color", "theme.accent"}};
        QTest::newRow("waveform") << QJsonObject{{"type", "waveform"}, {"points", 256}, {"color", "theme.accent"}};
        QTest::newRow("spectrogram") << QJsonObject{{"type", "spectrogram"}, {"color", "theme.accent"}};
        QTest::newRow("particles") << QJsonObject{{"type", "particles"}, {"count", 2048}, {"seed", 7}, {"speed", 1}, {"size", 0.01}, {"color", "theme.accent"}};
    }
    void sceneCostsAreMeasuredAtEachBoundedTier()
    {
        QFETCH(QJsonObject, node); AddonVisualizerRenderQueue queue; QSignalSpy images(&queue, &AddonVisualizerRenderQueue::frameReady);
        for (int quality = 0; quality < 3; ++quality) {
            qint64 maximum = 0, total = 0;
            for (int frame = 0; frame < 8; ++frame) {
                auto input = request(frame + 1); input.scene.insert("nodes", QJsonArray{node}); input.size = {1600, 1600}; input.quality = quality;
                input.analysis.rms = 0.4f; input.analysis.bands.fill(0.4f); input.analysis.waveform.fill(0.4f); input.mediaTimeUs += frame * 33333;
                const auto count = images.size(); QVERIFY(queue.submit(std::move(input))); QTRY_COMPARE_WITH_TIMEOUT(images.size(), count + 1, 5000);
                const auto output = qvariant_cast<QImage>(images.last()[0]); const int limit = quality == 0 ? 256 : quality == 1 ? 512 : 1024;
                QCOMPARE(output.size(), QSize(limit, limit)); QVERIFY(output.sizeInBytes() <= 4 * 1024 * 1024);
                const auto cost = images.last()[1].toLongLong(); total += cost; maximum = qMax(maximum, cost);
            }
            qInfo().noquote() << "Render measurements" << QTest::currentDataTag() << "quality" << quality << "mean ns" << total / 8 << "maximum ns" << maximum;
        }
    }
};
QTEST_GUILESS_MAIN(TestAddonVisualizerRenderQueue)
#include "test_addon_visualizer_render_queue.moc"
