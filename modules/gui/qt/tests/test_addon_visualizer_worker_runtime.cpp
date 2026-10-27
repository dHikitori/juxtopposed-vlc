// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include "addon_test_application_work.hpp"
#include <QSignalSpy>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSemaphore>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <atomic>
#include "addons/addon_visualizer_activation.hpp"
#include "addons/addon_visualizer_worker.hpp"
#include "fixtures/addon_visualizer_worker_wasm.hpp"
using namespace vlc::addons;
namespace {
QJsonObject scene()
{
    return {{"nodes", QJsonArray{QJsonObject{{"type", "spectrum"}, {"id", "bars"}, {"count", 32}, {"gain", 1}}}},
        {"parameters", QJsonObject{{"gain", QJsonObject{{"node", "bars"}, {"property", "gain"}, {"minimum", 0}, {"maximum", 4}}}}}};
}
std::shared_ptr<AddonVisualizerActivation> activation()
{
    auto value = std::make_shared<AddonVisualizerActivation>(); value->scene = scene();
    value->package.enabled = true; value->package.manifest.id = QStringLiteral("org.example.visual-worker");
    Contribution entry; entry.id = QStringLiteral("spectrum"); entry.type = QStringLiteral("visualizer"); entry.contract = QStringLiteral("visualizer@1");
    entry.operations = {QStringLiteral("updateParameters")}; value->package.manifest.contributions.append(entry);
    value->runtime = addonVisualizerWorkerFixture(); value->current = value->live = [] { return true; };
    return value;
}
const QString global = QStringLiteral("org.example.visual-worker/spectrum");
}
class TestAddonVisualizerWorkerRuntime : public QObject
{
    Q_OBJECT
private slots:
    void realPortableWorkerPublishesAndStops()
    {
#ifndef VLC_ADDON_NATIVE_BUILD_ROOT
        QSKIP("Set VLC_BUILD_DIR to test the native worker.");
#else
        auto prepared = activation(); prepared->executable = QStringLiteral(VLC_ADDON_NATIVE_BUILD_ROOT "/modules/gui/qt/vlc-addon-worker");
        QVERIFY(QFileInfo::exists(prepared->executable));
        AddonVisualizerWorker worker(prepared->scene, AddonVisualizerWorker::production(prepared, global));
        QVERIFY(worker.update({{"clock", QJsonObject{{"timeUs", 1000000}}}}));
        QTRY_COMPARE_WITH_TIMEOUT(worker.values().value("gain").toDouble(), 2.0, 5000);
        QCOMPARE(AddonVisualizerWorker::liveWorkers(), 1); QElapsedTimer elapsed; elapsed.start(); worker.close();
        QVERIFY(elapsed.elapsed() < 100); QTRY_COMPARE_WITH_TIMEOUT(AddonVisualizerWorker::liveWorkers(), 0, 5000);
#endif
    }
    void pendingProcessClosesWithoutBlockingTheOwner()
    {
        QTemporaryDir files; const auto request = files.filePath(QStringLiteral("request.json")); auto prepared = activation();
        prepared->runtime = QJsonDocument(QJsonObject{{"mode", "pending"}, {"request", request}}).toJson();
        prepared->executable = QStringLiteral(VLC_ADDON_VISUALIZER_PROCESS_FIXTURE);
        QVERIFY(QFileInfo::exists(prepared->executable));
        AddonVisualizerWorker worker(prepared->scene, AddonVisualizerWorker::production(prepared, global));
        QSignalSpy changed(&worker, &AddonVisualizerWorker::parametersChanged); QVERIFY(worker.update({}));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(request).size() > 0, 5000); QFile packet(request); QVERIFY(packet.open(QIODevice::ReadOnly));
        const auto message = QJsonDocument::fromJson(packet.readAll()).object();
        QCOMPARE(message.value("operation").toString(), QStringLiteral("updateParameters"));
        QVERIFY(message.value("hostCapabilities").toObject().value("coreOperations").toArray().isEmpty());
        QCOMPARE(message.value("hostCapabilities").toObject().value("privateStorageAvailable"), QJsonValue(false));
        QElapsedTimer elapsed; elapsed.start(); worker.close(); QVERIFY(elapsed.elapsed() < 100);
        QTRY_COMPARE_WITH_TIMEOUT(AddonVisualizerWorker::liveWorkers(), 0, 5000); QCOMPARE(changed.size(), 0);
    }
    void slowAdmissionRetainsTheRetiringSlotWithoutBlockingDestruction()
    {
        struct Gate { QSemaphore entered, release; ~Gate() { release.release(); } }; const auto gate = std::make_shared<Gate>();
        const auto releaseGate = qScopeGuard([gate] { gate->release.release(); });
        auto prepared = activation(); prepared->current = [gate] { gate->entered.release(); gate->release.acquire(); return true; };
        prepared->executable = QCoreApplication::applicationFilePath();
        auto worker = std::make_unique<AddonVisualizerWorker>(prepared->scene, AddonVisualizerWorker::production(prepared, global));
        QVERIFY(worker->update({})); QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available() > 0, 5000);
        QElapsedTimer elapsed; elapsed.start(); worker.reset(); QVERIFY(elapsed.elapsed() < 100);
        QCOMPARE(AddonVisualizerWorker::liveWorkers(), 1);
        auto next = activation(); AddonVisualizerWorker other(next->scene, AddonVisualizerWorker::production(next, global));
        QVERIFY(other.update({})); QCoreApplication::sendPostedEvents(&other, QEvent::MetaCall); QVERIFY(!other.suspended());
        QCOMPARE(AddonVisualizerWorker::liveWorkers(), 1); gate->release.release();
        QTRY_COMPARE_WITH_TIMEOUT(AddonVisualizerWorker::liveWorkers(), 0, 5000);
    }
    void revokedAdmissionNeverStartsAProcess()
    {
        auto prepared = activation(); prepared->live = [] { return false; }; prepared->executable = QStringLiteral("invalid-executable");
        AddonVisualizerWorker worker(prepared->scene, AddonVisualizerWorker::production(prepared, global));
        QVERIFY(worker.update({})); QCoreApplication::sendPostedEvents(&worker, QEvent::MetaCall);
        QVERIFY(worker.values().isEmpty()); QCOMPARE(AddonVisualizerWorker::liveWorkers(), 0);
    }
};
HAKONE_QTEST_MAIN(TestAddonVisualizerWorkerRuntime, QCoreApplication)
#include "test_addon_visualizer_worker_runtime.moc"
