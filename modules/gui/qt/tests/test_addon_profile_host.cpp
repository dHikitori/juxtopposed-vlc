// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_profile_host.hpp"
#include "addons/addon_manager.hpp"
#include "addons/addon_package_store.hpp"
#include <QTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QSignalSpy>
#include <QCoreApplication>
#include <QTimer>
#include <QDir>
#include <QElapsedTimer>
#include <cstdio>
#include <thread>
using namespace vlc::addons;
namespace {
// Another app process: a manager with its own profile host. 'w' turns safe
// mode on through the shared profile. 'q' quits.
int child(const QStringList& args)
{
    AddonStreamLifetime lifetime;
    auto* manager = new AddonManager(args.at(2));
    auto* host = new AddonProfileHost(manager, lifetime.handle());
    if (!host->start())
        return 2;
    std::thread([manager, host] {
        for (int command; (command = std::getchar()) != EOF;) {
            QMetaObject::invokeMethod(qApp, [manager, host, command] {
                if (command == 'w') {
                    const bool written = manager->setSafeMode(true);
                    std::puts(written ? "wrote" : "refused");
                    std::fflush(stdout);
                } else if (command == 'q') {
                    host->close();
                    qApp->quit();
                }
            }, Qt::QueuedConnection);
        }
    }).detach();
    QTimer ready;
    QObject::connect(&ready, &QTimer::timeout, [host, &ready] {
        if (!host->ready()) return;
        std::puts("ready"); std::fflush(stdout); ready.stop();
    });
    ready.start(10);
    const int code = qApp->exec();
    delete host;
    delete manager;
    lifetime.close();
    lifetime.handle().waitQt(std::chrono::seconds(10));
    return code;
}
struct Fixture {
    QTemporaryDir root;
    AddonStreamLifetime lifetime;
    AddonManager* manager = nullptr;
    AddonProfileHost* host = nullptr;
    Fixture() {
        manager = new AddonManager(root.path());
        host = new AddonProfileHost(manager, lifetime.handle());
    }
    ~Fixture() {
        delete host;
        delete manager;
        lifetime.close();
        lifetime.handle().waitQt(std::chrono::seconds(10));
    }
};
}
class TestAddonProfileHost : public QObject
{
    Q_OBJECT
private slots:
    void aSharedManagerWritesThroughThePermit() {
        Fixture f;
        QVERIFY(f.host->start());
        QTRY_VERIFY_WITH_TIMEOUT(f.host->ready(), 15000);
        QVERIFY(f.host->coordinating());
        QVERIFY(f.host->shared());
        // The profile is shared now: a direct ordinary write is refused.
        PackageStore direct(f.manager->storageDirectory());
        QVERIFY(direct.sharedProfile());
        QString error;
        QVERIFY(!direct.setSafeMode(true, &error));
        // The manager's write goes through the coordinator's permit.
        QVERIFY2(f.manager->setSafeMode(true), qPrintable(f.manager->errorMessage()));
        QVERIFY(direct.safeMode());
        QTRY_VERIFY_WITH_TIMEOUT(f.host->ready(), 15000);
    }
    void aDriveWithoutLocksLeavesTheProfileUnshared() {
        // Set to a directory on such a drive, for example \\wsl.localhost\...
        // on Windows.
        const auto unlocked = qEnvironmentVariable("HAKONE_TEST_UNLOCKED_DIR");
        if (unlocked.isEmpty()) QSKIP("HAKONE_TEST_UNLOCKED_DIR names no directory without file locks");
        QTemporaryDir root(QDir(unlocked).filePath(QStringLiteral("profile-XXXXXX")));
        QVERIFY(root.isValid());
        AddonStreamLifetime lifetime;
        auto* manager = new AddonManager(root.path());
        auto* host = new AddonProfileHost(manager, lifetime.handle());
        QVERIFY(host->start());
        QVERIFY(!host->shared());
        // Writes go straight to the store, without waiting for a coordinator.
        QElapsedTimer clock; clock.start();
        QVERIFY2(manager->setSafeMode(true), qPrintable(manager->errorMessage()));
        QVERIFY(clock.elapsed() < 2000);
        QVERIFY(PackageStore(root.path()).safeMode());
        delete host; delete manager;
        lifetime.close();
        QVERIFY(lifetime.handle().waitQt(std::chrono::seconds(10)));
    }
    void afterCloseWritesAreRefused() {
        Fixture f;
        QVERIFY(f.host->start());
        QTRY_VERIFY_WITH_TIMEOUT(f.host->ready(), 15000);
        f.host->close();
        QVERIFY(!f.host->ready());
        QVERIFY(!f.manager->setSafeMode(true));
        QVERIFY(f.manager->errorMessage().contains(QLatin1String("shared add-on profile")));
    }
    void aWriteInAnotherProcessRefreshesThisManager() {
        Fixture f;
        QVERIFY(f.host->start());
        QTRY_VERIFY_WITH_TIMEOUT(f.host->ready(), 15000);
        QProcess other;
        other.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--hakone-host-child"), f.root.path()});
        QVERIFY(other.waitForStarted(5000));
        QTRY_VERIFY_WITH_TIMEOUT(other.canReadLine() || other.waitForReadyRead(10), 15000);
        QCOMPARE(other.readLine().trimmed(), QByteArray("ready"));
        QSignalSpy catalogue(f.manager, &AddonManager::catalogueChanged);
        QVERIFY(other.write("w", 1) == 1);
        QVERIFY(other.waitForReadyRead(15000) || other.canReadLine());
        QTRY_VERIFY_WITH_TIMEOUT(other.canReadLine() || other.waitForReadyRead(10), 15000);
        QCOMPARE(other.readLine().trimmed(), QByteArray("wrote"));
        // This process rejoins the next revision and reloads its manager.
        QTRY_VERIFY_WITH_TIMEOUT(catalogue.count() > 0, 15000);
        QVERIFY(PackageStore(f.root.path()).safeMode());
        QTRY_VERIFY_WITH_TIMEOUT(f.host->ready(), 15000);
        QVERIFY(other.write("q", 1) == 1);
        QVERIFY(other.waitForFinished(15000));
        QCOMPARE(other.exitCode(), 0);
    }
};
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() == 3 && args.at(1) == QLatin1String("--hakone-host-child"))
        return child(args);
    TestAddonProfileHost test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_addon_profile_host.moc"
