// SPDX-License-Identifier: GPL-2.0-or-later
// The app's add-on store writes run on background work, in request order.
#include "addons/addon_manager.hpp"
#include "addons/addon_package_store.hpp"
#include "addons/addon_repository_jobs.hpp"
#include "addons/addon_stream_lifetime.hpp"
#include <QSaveFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
using namespace vlc::addons;
namespace {
// Holds each store commit while closed, and notes a commit on the GUI thread.
// A held commit on the GUI thread gives up after a few seconds, so a
// regression fails instead of hanging.
struct CommitGate {
    std::mutex mutex;
    std::condition_variable changed;
    bool open = true;
    std::atomic_uint held{0};
    std::atomic_bool onGui{false};
    std::atomic<Qt::HANDLE> gui{nullptr};
    void close() { std::lock_guard<std::mutex> lock(mutex); open = false; }
    void release() { { std::lock_guard<std::mutex> lock(mutex); open = true; } changed.notify_all(); }
};
CommitGate gate;
}
#ifdef HAKONE_MANAGER_WRITE_WRAP
extern "C" bool __real__ZN9QSaveFile6commitEv(QSaveFile*);
extern "C" bool __wrap__ZN9QSaveFile6commitEv(QSaveFile* file)
{
    if (QThread::currentThreadId() == gate.gui.load()) gate.onGui = true;
    {
        std::unique_lock<std::mutex> lock(gate.mutex);
        if (!gate.open) {
            ++gate.held;
            gate.changed.wait_for(lock, std::chrono::seconds(3), [] { return gate.open; });
        }
    }
    return __real__ZN9QSaveFile6commitEv(file);
}
#endif
namespace {
const QString Package = QStringLiteral("org.hakone.discovery-fixture");
const QString Fixture = QStringLiteral(VLC_ADDON_SOURCE_ROOT "/modules/gui/qt/tests/fixtures/addons/discovery-fixture");
enum Operation { Enable, ProviderOrder, Settings, SafeMode, Approve };
bool enabled(const QString& root)
{
    InstalledPackage package;
    return PackageStore(root).installed(Package, &package) && package.enabled;
}
}
class TestAddonManagerWrites : public QObject
{
    Q_OBJECT
    std::unique_ptr<AddonStreamLifetime> m_lifetime;
private slots:
    void init() {
        gate.release(); gate.held = 0; gate.onGui = false; gate.gui = QThread::currentThreadId();
        m_lifetime = std::make_unique<AddonStreamLifetime>();
        QVERIFY(AddonStreamLifetime::registerApplication(m_lifetime->handle()));
    }
    void cleanup() {
        gate.release();
        const auto handle = m_lifetime->handle();
        handle.close(); QVERIFY(handle.waitQt(std::chrono::seconds(5)));
        QVERIFY(AddonStreamLifetime::unregisterApplication(handle));
        QTRY_COMPARE(AddonRepositoryJobs::processCount(), 0u);
    }
    void aRequestWritesOnBackgroundWorkWhileTheGuiRuns_data() {
        QTest::addColumn<int>("operation");
        QTest::newRow("enable") << int(Enable); QTest::newRow("provider order") << int(ProviderOrder);
        QTest::newRow("settings") << int(Settings); QTest::newRow("safe mode") << int(SafeMode);
        QTest::newRow("approve") << int(Approve);
    }
    void aRequestWritesOnBackgroundWorkWhileTheGuiRuns() {
#ifndef HAKONE_MANAGER_WRITE_WRAP
        QSKIP("This build cannot hold a store commit.");
#endif
        QFETCH(int, operation);
        QTemporaryDir profile; AddonManager manager(profile.path());
        if (operation == Approve) {
            QVERIFY(manager.stageLocalPackage(Fixture)); QTRY_VERIFY(!manager.busy());
            QCOMPARE(manager.pendingPackage().value("id").toString(), Package);
        } else QVERIFY2(manager.installLocalPackage(Fixture, true), qPrintable(manager.errorMessage()));
        QSignalSpy finished(&manager, &AddonManager::writeFinished);
        gate.close();
        QString request;
        switch (operation) {
        case Enable: request = manager.requestEnabled(Package, true); break;
        case ProviderOrder: request = manager.requestProviderOrder(QStringLiteral("discovery"), {Package + QStringLiteral("/catalogue")}); break;
        case Settings: request = manager.requestSaveSettings(Package, {}, {}); break;
        case SafeMode: request = manager.requestSafeMode(true); break;
        case Approve: request = manager.requestApprovePending(true); break;
        }
        // The call returns before the write, and the GUI thread keeps running
        // while the write waits for its commit.
        QVERIFY(!request.isEmpty()); QVERIFY(manager.writing()); QCOMPARE(finished.size(), 0);
        QTRY_VERIFY(gate.held.load() > 0);
        bool ticked = false; QTimer::singleShot(0, &manager, [&] { ticked = true; }); QTRY_VERIFY(ticked);
        QVERIFY(manager.writing()); QCOMPARE(finished.size(), 0);
        gate.release();
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished.first().at(0).toString(), request);
        QVERIFY2(finished.first().at(1).toBool(), qPrintable(finished.first().at(2).toString()));
        QVERIFY(!manager.writing()); QVERIFY(manager.errorMessage().isEmpty());
        QVERIFY(!gate.onGui.load());
        if (operation == Enable || operation == Approve) QVERIFY(enabled(profile.path()));
        if (operation == ProviderOrder) QCOMPARE(manager.providerOrder(QStringLiteral("discovery")), QStringList{Package + QStringLiteral("/catalogue")});
        if (operation == SafeMode) QVERIFY(PackageStore(profile.path()).safeMode());
        if (operation == Approve) QVERIFY(manager.pendingPackage().isEmpty());
    }
    void requestsFinishInOrder() {
        QTemporaryDir profile; AddonManager manager(profile.path());
        QVERIFY(manager.installLocalPackage(Fixture, true));
        QSignalSpy finished(&manager, &AddonManager::writeFinished);
        const QStringList requests{manager.requestEnabled(Package, true), manager.requestEnabled(Package, false),
            manager.requestEnabled(Package, true), manager.requestEnabled(Package, false)};
        QTRY_COMPARE(finished.size(), 4);
        for (int i = 0; i < 4; ++i) {
            QCOMPARE(finished.at(i).at(0).toString(), requests.at(i));
            QVERIFY(finished.at(i).at(1).toBool());
        }
        QVERIFY(!enabled(profile.path())); QVERIFY(!manager.writing());
    }
    void aRefusedRequestEndsWithItsError() {
        QTemporaryDir profile; AddonManager manager(profile.path());
        QSignalSpy finished(&manager, &AddonManager::writeFinished);
        const auto request = manager.requestProviderOrder({}, {});
        QVERIFY(!request.isEmpty()); QCOMPARE(finished.size(), 0);
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished.first().at(0).toString(), request);
        QVERIFY(!finished.first().at(1).toBool());
        QVERIFY(!finished.first().at(2).toString().isEmpty());
        QCOMPARE(finished.first().at(2).toString(), manager.errorMessage());
    }
    void aWriteWaitsForAFreeBackgroundSlot() {
        QTemporaryDir profile; AddonManager manager(profile.path());
        QVERIFY(manager.installLocalPackage(Fixture, true));
        // Other add-on work holds every slot.
        std::mutex mutex; std::condition_variable changed; bool release = false;
        const auto work = [&] { std::unique_lock<std::mutex> lock(mutex); changed.wait(lock, [&] { return release; }); };
        AddonRepositoryJobs first(m_lifetime->handle()), second(m_lifetime->handle());
        QVERIFY(first.submit(work, [](AddonRepositoryJobs::Result) {}));
        QVERIFY(second.submit(work, [](AddonRepositoryJobs::Result) {}));
        QTRY_COMPARE(AddonRepositoryJobs::processCount(), AddonRepositoryJobs::ProcessLimit);
        QSignalSpy finished(&manager, &AddonManager::writeFinished);
        const auto request = manager.requestEnabled(Package, true);
        QTest::qWait(100); QCOMPARE(finished.size(), 0); QVERIFY(manager.writing());
        { std::lock_guard<std::mutex> lock(mutex); release = true; } changed.notify_all();
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished.first().at(0).toString(), request);
        QVERIFY(finished.first().at(1).toBool()); QVERIFY(enabled(profile.path()));
    }
    void closingTheManagerDropsItsWrites() {
#ifndef HAKONE_MANAGER_WRITE_WRAP
        QSKIP("This build cannot hold a store commit.");
#endif
        QTemporaryDir profile;
        auto manager = std::make_unique<AddonManager>(profile.path());
        QVERIFY(manager->installLocalPackage(Fixture, true));
        QSignalSpy finished(manager.get(), &AddonManager::writeFinished);
        gate.close();
        QVERIFY(!manager->requestEnabled(Package, true).isEmpty());
        QVERIFY(!manager->requestEnabled(Package, false).isEmpty());
        QTRY_VERIFY(gate.held.load() > 0);
        manager.reset();
        gate.release();
        // The running write ends on its work; nothing reports to the closed manager.
        QTRY_COMPARE(AddonRepositoryJobs::processCount(), 0u);
        QCOMPARE(finished.size(), 0);
    }
};
QTEST_GUILESS_MAIN(TestAddonManagerWrites)
#include "test_addon_manager_writes.moc"
