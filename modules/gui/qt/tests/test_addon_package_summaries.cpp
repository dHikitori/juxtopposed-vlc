// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_package_summaries.hpp"
#include "addons/addon_manager.hpp"
#include "addons/addon_catalogue_view.hpp"
#include <QFile>
#include <QPointer>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QThreadStorage>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
using namespace vlc::addons;
namespace {
struct Wait {
    std::mutex mutex; std::condition_variable changed; bool released = false;
    std::atomic_bool entered{false};
    void wait() { std::unique_lock<std::mutex> lock(mutex); entered = true; changed.wait(lock, [&] { return released; }); }
    void release() { { std::lock_guard<std::mutex> lock(mutex); released = true; } changed.notify_all(); }
};
struct Scope {
    AddonStreamLifetime owner;
    ~Scope() { owner.close(); owner.handle().waitQt(std::chrono::seconds(5)); }
};
PackageSummarySnapshot snapshot(const StoreAuthorityToken& token, const QString& id = {})
{
    return {id.isEmpty() ? QVariantList{} : QVariantList{QVariantMap{{"id", id}}}, token};
}
QString reference(const QString& name)
{
    // Bundled add-ons are at the top of contrib-addons; the others are test fixtures.
    const auto bundled = QStringLiteral(VLC_ADDON_SOURCE_ROOT "/contrib-addons/") + name;
    return QFile::exists(bundled + QStringLiteral("/addon.json")) ? bundled
        : QStringLiteral(VLC_ADDON_SOURCE_ROOT "/modules/gui/qt/tests/fixtures/addons/") + name;
}
}
class TestAddonPackageSummaries : public QObject {
    Q_OBJECT
private slots:
    void cleanup()
    {
        QTRY_COMPARE_WITH_TIMEOUT(AddonRepositoryJobs::processCount(), 0u, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(AddonStreamLifetime::processQtWorkCount(), 0u, 5000);
    }
    void attachedSafeModeDistinguishesInitialUnknownEmptyAndRetainedData()
    {
        Scope scope; QTemporaryDir directory; PackageStore store(directory.path());
        unsigned captures=0;
        AddonCatalogueView::Backend backend{
            [&]() -> std::optional<DisplayCatalogueSource> {
                ++captures; DisplayCatalogueSource source;source.queuedAuthority=store.authorityToken();
                source.reader=store.displayCatalogueReader(QStringLiteral("en"));return source;
            },[&](std::function<void()> wake){return store.onDisplayChanged(std::move(wake));}};
        AddonCatalogueView view(std::move(backend),scope.owner.handle());
        AddonPackageSummaries service(view,scope.owner.handle());service.refresh();
        QVERIFY(service.displayBacked());QVERIFY(!service.safeModeKnown());QVERIFY(!service.safeMode());QVERIFY(!service.retainedAvailable());
        QTRY_VERIFY(service.isCurrent());QVERIFY(service.rows().isEmpty());QVERIFY(service.safeModeKnown());QVERIFY(!service.safeMode());QVERIFY(service.retainedAvailable());
        QString error;QVERIFY(store.setSafeMode(true,&error));QVERIFY(!service.isCurrent());
        QVERIFY(service.safeModeKnown());QVERIFY(!service.safeMode());QVERIFY(service.rows().isEmpty());
        QTRY_VERIFY(service.isCurrent());QVERIFY(service.safeMode());QVERIFY(service.safeModeKnown());
        const auto reads=captures;QTest::qWait(250);QCOMPARE(captures,reads);
        view.close();QVERIFY(!service.isCurrent());QVERIFY(!service.retainedAvailable());
    }
    void unusedServiceAndManagerRevisionsDoNotRead()
    {
        Scope scope; QTemporaryDir directory; AddonManager manager(directory.path());
        AddonPackageSummaries service(&manager, scope.owner.handle());
        QVERIFY(!service.loading()); QVERIFY(!service.isCurrent());
        QVERIFY(manager.setSafeMode(true)); QTest::qWait(130);
        QVERIFY(!service.loading()); QVERIFY(!service.isCurrent());
        QCOMPARE(AddonRepositoryJobs::processCount(), 0u);
    }
    void blockedReaderLeavesTheGuiHeartbeatRunning()
    {
        Scope scope; QTemporaryDir directory; StoreAuthority authority(directory.path());
        const auto gate = std::make_shared<Wait>();
        const auto release = qScopeGuard([&] { gate->release(); });
        const auto uiThread = QThread::currentThread(); std::atomic_bool offThread{false};
        AddonPackageSummaries service([&] {
            const auto token = authority.capture();
            return PackageSummaryReader([gate, token, uiThread, &offThread] {
                offThread = QThread::currentThread() != uiThread;
                gate->wait(); return std::optional<PackageSummarySnapshot>(snapshot(token, QStringLiteral("ready")));
            });
        }, scope.owner.handle());
        unsigned beats = 0; QTimer heartbeat; heartbeat.setInterval(5);
        connect(&heartbeat, &QTimer::timeout, this, [&] { ++beats; }); heartbeat.start();
        service.refresh(); QTRY_VERIFY(gate->entered.load()); QTRY_VERIFY(beats >= 5);
        QVERIFY(offThread.load()); QVERIFY(service.loading()); QVERIFY(service.rows().isEmpty());
        gate->release(); QTRY_VERIFY(service.isCurrent()); QCOMPARE(service.rows().size(), 1);
    }
    void repeatedRefreshCoalescesToTheLatestGeneration()
    {
        Scope scope; QTemporaryDir directory; StoreAuthority authority(directory.path());
        const auto gate = std::make_shared<Wait>(); const auto release = qScopeGuard([&] { gate->release(); });
        int captures = 0, selected = 1;
        AddonPackageSummaries service([&] {
            const auto token = authority.capture(); const auto value = QString::number(selected); const int call = ++captures;
            return PackageSummaryReader([gate, token, value, call] {
                if (call == 1) gate->wait();
                return std::optional<PackageSummarySnapshot>(snapshot(token, value));
            });
        }, scope.owner.handle());
        QSignalSpy done(&service, &AddonPackageSummaries::loadingFinished);
        service.refresh(); QTRY_VERIFY(gate->entered.load());
        for (selected = 2; selected < 8; ++selected) service.refresh();
        selected = 8; gate->release(); QTRY_VERIFY(service.isCurrent());
        QCOMPARE(captures, 2); QCOMPARE(service.rows().first().toMap().value("id").toString(), QStringLiteral("8"));
        QCOMPARE(done.size(), 1);
    }
    void peerRevocationRefusesAnAlreadyCompletedQueuedSnapshot()
    {
        Scope scope; QTemporaryDir directory; StoreAuthority authority(directory.path());
        const auto gate = std::make_shared<Wait>(); const auto release = qScopeGuard([&] { gate->release(); });
        AddonPackageSummaries service([&] {
            const auto token = authority.capture();
            return PackageSummaryReader([gate, token] { gate->wait(); return std::optional<PackageSummarySnapshot>(snapshot(token, QStringLiteral("old"))); });
        }, scope.owner.handle());
        QSignalSpy done(&service, &AddonPackageSummaries::loadingFinished);
        service.refresh(); QTRY_VERIFY(gate->entered.load());
        authority.invalidate(); gate->release(); QTRY_COMPARE(done.size(), 1);
        QVERIFY(!done.first().first().toBool()); QVERIFY(!service.isCurrent()); QVERIFY(service.rows().isEmpty()); QVERIFY(!service.error().isEmpty());
        service.refresh(); QTRY_VERIFY(service.isCurrent());
    }
    void currentReadRefusesPeerMutationBeforeAnyQtDelivery()
    {
        Scope scope; QTemporaryDir directory; StoreAuthority authority(directory.path());
        AddonPackageSummaries service([&] {
            const auto token = authority.capture();
            return PackageSummaryReader([token] { return std::optional<PackageSummarySnapshot>(snapshot(token, QStringLiteral("row"))); });
        }, scope.owner.handle());
        service.refresh(); QTRY_VERIFY(service.isCurrent());
        StoreAuthority peer(directory.path()); peer.invalidate();
        QVERIFY(!service.isCurrent()); QVERIFY(service.rows().isEmpty());
        QTRY_VERIFY(service.isCurrent());
    }
    void genuineEmptyAndReaderRefusalAreDistinct_data()
    {
        QTest::addColumn<bool>("valid"); QTest::newRow("empty") << true; QTest::newRow("refused") << false;
    }
    void genuineEmptyAndReaderRefusalAreDistinct()
    {
        QFETCH(bool, valid); Scope scope; QTemporaryDir directory; StoreAuthority authority(directory.path());
        AddonPackageSummaries service([&] {
            const auto token = authority.capture();
            return PackageSummaryReader([token, valid]() -> std::optional<PackageSummarySnapshot> { return valid ? std::optional<PackageSummarySnapshot>(snapshot(token)) : std::nullopt; });
        }, scope.owner.handle());
        QSignalSpy done(&service, &AddonPackageSummaries::loadingFinished);
        service.refresh(); QTRY_COMPARE(done.size(), 1);
        QCOMPARE(service.isCurrent(), valid); QCOMPARE(service.error().isEmpty(), valid); QVERIFY(service.rows().isEmpty());
    }
    void captureMayDeleteTheService()
    {
        Scope scope; AddonPackageSummaries* service = nullptr;
        service = new AddonPackageSummaries([&] { delete service; return PackageSummaryReader{}; }, scope.owner.handle());
        const QPointer<AddonPackageSummaries> guard(service);
        service->refresh(); QTRY_VERIFY(!guard);
    }
    void stateObserverCanDeleteCloseOrRefresh_data()
    {
        QTest::addColumn<int>("action"); QTest::newRow("delete") << 0; QTest::newRow("close") << 1; QTest::newRow("refresh") << 2;
    }
    void stateObserverCanDeleteCloseOrRefresh()
    {
        QFETCH(int, action); Scope scope; QTemporaryDir directory; StoreAuthority authority(directory.path()); int captures = 0;
        auto* service = new AddonPackageSummaries([&] {
            const auto token = authority.capture(); const auto id = QString::number(++captures);
            return PackageSummaryReader([token, id] { return std::optional<PackageSummarySnapshot>(snapshot(token, id)); });
        }, scope.owner.handle());
        QPointer<AddonPackageSummaries> guard(service); unsigned done = 0; bool acted = false;
        connect(service, &AddonPackageSummaries::loadingFinished, this, [&](bool) { ++done; });
        connect(service, &AddonPackageSummaries::stateChanged, this, [&, service] {
            if (acted || !service->isCurrent()) return; acted = true;
            if (action == 0) delete service; else if (action == 1) service->close(); else service->refresh();
        });
        service->refresh(); QTRY_VERIFY(acted);
        if (action == 0) { QVERIFY(!guard); QCOMPARE(done, 0u); }
        else if (action == 1) { QVERIFY(!service->isCurrent()); QCOMPARE(done, 0u); service->close(); delete service; }
        else { QTRY_VERIFY(service->isCurrent()); QCOMPARE(captures, 2); QCOMPARE(done, 1u); delete service; }
    }
    void closeAndOwnerDestructionDoNotWaitForBlockedWork()
    {
        Scope scope; QTemporaryDir directory; StoreAuthority authority(directory.path());
        const auto gate = std::make_shared<Wait>(); const auto release = qScopeGuard([&] { gate->release(); });
        auto* service = new AddonPackageSummaries([&] {
            const auto token = authority.capture();
            return PackageSummaryReader([gate, token] { gate->wait(); return std::optional<PackageSummarySnapshot>(snapshot(token)); });
        }, scope.owner.handle());
        unsigned finished = 0; connect(service, &AddonPackageSummaries::loadingFinished, this, [&](bool) { ++finished; });
        service->refresh(); QTRY_VERIFY(gate->entered.load());
        QElapsedTimer clock; clock.start(); service->close(); service->close(); delete service;
        QVERIFY(clock.elapsed() < 250); scope.owner.close(); QVERIFY(!scope.owner.handle().waitQt(std::chrono::milliseconds(20)));
        gate->release(); QVERIFY(scope.owner.handle().waitQt(std::chrono::seconds(5))); QCoreApplication::processEvents(); QCOMPARE(finished, 0u);
    }
    void processQuotaRefusesCleanlyAndExplicitRefreshRetries()
    {
        Scope scope; QTemporaryDir directory; StoreAuthority authority(directory.path());
        const auto gate = std::make_shared<Wait>(); const auto release = qScopeGuard([&] { gate->release(); });
        AddonRepositoryJobs first(scope.owner.handle()), second(scope.owner.handle());
        QVERIFY(first.submit([gate] { gate->wait(); }, [](auto) {})); QVERIFY(second.submit([gate] { gate->wait(); }, [](auto) {}));
        QTRY_COMPARE(AddonRepositoryJobs::processCount(), 2u);
        AddonPackageSummaries service([&] { const auto token = authority.capture(); return PackageSummaryReader([token] { return std::optional<PackageSummarySnapshot>(snapshot(token)); }); }, scope.owner.handle());
        QSignalSpy done(&service, &AddonPackageSummaries::loadingFinished); service.refresh(); QTRY_COMPARE(done.size(), 1);
        QVERIFY(!service.loading()); QVERIFY(!service.error().isEmpty()); QVERIFY(!service.isCurrent());
        gate->release(); QTRY_COMPARE(AddonRepositoryJobs::processCount(), 0u);
        service.refresh(); QTRY_VERIFY(service.isCurrent());
    }
    void actualManagerReaderMatchesEveryProjectedField()
    {
        Scope scope; QTemporaryDir directory; AddonManager manager(directory.path());
        const auto radio = QStringLiteral("org.hakone.radio-browser"), subtitles = QStringLiteral("org.hakone.thesubtitledb");
        QVERIFY(manager.ensureDistributionPackage(reference(QStringLiteral("radio-browser")), radio));
        QVERIFY(manager.ensureDistributionPackage(reference(QStringLiteral("thesubtitledb")), subtitles));
        const auto compare = [&] {
            const auto reader = manager.packageReader(); const auto expected = manager.packages();
            std::optional<PackageSummarySnapshot> received;
            std::thread worker([&] { received = reader(); }); worker.join();
            return received && received->authority.current() && received->rows == expected;
        };
        QVERIFY(compare()); QVERIFY(manager.setEnabled(radio, false)); QVERIFY(compare());
        const QVariantMap secret{{"apiKey", QVariantMap{{"operation", "replace"}, {"value", "test-only-key"}, {"sessionOnly", true}}}};
        QVERIFY(manager.saveAddonSettings(subtitles, {{"language", "fr"}}, secret)); QVERIFY(compare());
        QVERIFY(manager.packages().at(1).toMap().value("configured").toBool());
        PackageStore store(directory.path()); QVERIFY(store.uninstall(radio, false)); QVERIFY(store.setDistributionSuppressed(radio, true)); QVERIFY(compare());
        QVERIFY(QDir().mkpath(directory.filePath(QStringLiteral("retained-settings"))));
        QFile saved(directory.filePath(QStringLiteral("retained-settings/org.example.saved.json"))); QVERIFY(saved.open(QIODevice::WriteOnly)); saved.write("{}"); saved.close();
        QVERIFY(compare());
        for (const auto& suffix : {QString(), QStringLiteral(".backup")}) {
            QFile corrupt(directory.filePath(QStringLiteral("packages/") + subtitles + QStringLiteral("/state.json") + suffix));
            QVERIFY(corrupt.open(QIODevice::WriteOnly)); corrupt.write("broken");
        }
        QVERIFY(compare());
    }
    void queuedManagerReaderCannotRefreshItsOwnAuthority()
    {
        QTemporaryDir directory; auto* manager = new AddonManager(directory.path());
        auto reader = manager->packageReader(); QVERIFY(manager->setSafeMode(true)); QVERIFY(!reader());
        reader = manager->packageReader(); delete manager; QVERIFY(!reader());
    }
    void tooManyRawPackageRowsRefuseInsteadOfEmptySuccess()
    {
        QTemporaryDir directory; AddonManager manager(directory.path());
        for (int index = 0; index < 257; ++index) {
            const auto path = directory.filePath(QStringLiteral("packages/org.example.p%1").arg(index));
            QVERIFY(QDir().mkpath(path)); QFile state(QDir(path).filePath(QStringLiteral("state.json")));
            QVERIFY(state.open(QIODevice::WriteOnly)); state.write("broken");
        }
        QVERIFY(!manager.packageReader()());
    }
    void actualBlockedStateFileIsJoinedWithoutStoppingGuiDelivery()
    {
#ifdef Q_OS_UNIX
        Scope scope; QTemporaryDir directory; AddonManager manager(directory.path());
        const auto id = QStringLiteral("org.hakone.thesubtitledb");
        QVERIFY(manager.ensureDistributionPackage(reference(QStringLiteral("thesubtitledb")), id));
        const auto path = directory.filePath(QStringLiteral("packages/") + id + QStringLiteral("/state.json"));
        QFile state(path); QVERIFY(state.open(QIODevice::ReadOnly)); const auto bytes = state.readAll(); state.close();
        QVERIFY(QFile::remove(path)); const auto encoded = QFile::encodeName(path); QVERIFY(::mkfifo(encoded.constData(), 0600) == 0);
        int writer = -1;
        const auto release = qScopeGuard([&] {
            if (writer >= 0) { ::write(writer, bytes.constData(), size_t(bytes.size())); ::close(writer); }
        });
        AddonPackageSummaries service(&manager, scope.owner.handle()); service.refresh();
        QTRY_VERIFY(writer >= 0 || (writer = ::open(encoded.constData(), O_WRONLY | O_NONBLOCK)) >= 0);
        unsigned beats = 0; QTimer heartbeat; heartbeat.setInterval(5); connect(&heartbeat, &QTimer::timeout, this, [&] { ++beats; }); heartbeat.start();
        QTRY_VERIFY(beats >= 5); QVERIFY(service.loading()); service.close(); scope.owner.close();
        QVERIFY(!scope.owner.handle().waitQt(std::chrono::milliseconds(20)));
        QCOMPARE(::write(writer, bytes.constData(), size_t(bytes.size())), ssize_t(bytes.size())); ::close(writer); writer = -1;
        QVERIFY(scope.owner.handle().waitQt(std::chrono::seconds(5))); QVERIFY(!service.isCurrent());
#else
        QSKIP("The controlled FIFO fixture requires Unix; other lifecycle cases are portable.");
#endif
    }
};
QTEST_GUILESS_MAIN(TestAddonPackageSummaries)
#include "test_addon_package_summaries.moc"
