// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_manager.hpp"
#include "addons/addon_package_store.hpp"
#include "addons/addon_process_launch.hpp"
#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
using namespace vlc::addons;
namespace {
const QString Package = QStringLiteral("org.hakone.discovery-fixture");
const QString FixtureContribution = Package + QStringLiteral("/catalogue");
// The discovery fixture with a module that is not Wasm: every worker start
// fails before it is ready.
bool brokenPackage(const QString& target)
{
    const QDir source(QStringLiteral(VLC_ADDON_SOURCE_ROOT "/contrib-addons/examples/discovery-fixture"));
    if (!QDir().mkpath(QDir(target).filePath(QStringLiteral("dist")))) return false;
    for (const auto* file : {"addon.json", "LICENSE"})
        if (!QFile::copy(source.filePath(QLatin1String(file)), QDir(target).filePath(QLatin1String(file)))) return false;
    QFile module(QDir(target).filePath(QStringLiteral("dist/addon.wasm")));
    return module.open(QIODevice::WriteOnly) && module.write("not a Wasm module") > 0;
}
int crashes(const AddonManager& manager) { return manager.health(Package).value("crashes").toInt(); }
}
class TestAddonWorkerHealth : public QObject
{
    Q_OBJECT
private slots:
    void aCrashingPackageWaitsThenIsQuarantined() {
        const auto worker = qEnvironmentVariable("VLC_ADDON_WORKER");
        if (worker.isEmpty()) QSKIP("VLC_ADDON_WORKER names no worker");
        QTemporaryDir profile, package;
        QVERIFY(brokenPackage(package.path()));
        AddonManager manager(profile.path());
        manager.setWorkerExecutable(worker);
        QVERIFY2(manager.installLocalPackage(package.path(), true), qPrintable(manager.errorMessage()));
        QVERIFY2(manager.setEnabled(Package, true), qPrintable(manager.errorMessage()));
        // First crash: the next start waits about 2 s.
        QVERIFY(!manager.invoke(FixtureContribution, QStringLiteral("roots"), {}).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(crashes(manager), 1, 15000);
        QVERIFY(manager.health(Package).value("retryInMs").toLongLong() > 1000);
        // No worker starts during the wait.
        const auto launches = helperLaunchCount();
        QVERIFY(manager.invoke(FixtureContribution, QStringLiteral("roots"), {}).isEmpty());
        QCOMPARE(helperLaunchCount(), launches);
        // Second crash after the wait; the next wait is about 4 s.
        QTRY_COMPARE_WITH_TIMEOUT(manager.health(Package).value("retryInMs").toLongLong(), 0, 5000);
        QVERIFY(!manager.invoke(FixtureContribution, QStringLiteral("roots"), {}).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(crashes(manager), 2, 15000);
        QVERIFY(manager.health(Package).value("retryInMs").toLongLong() > 3000);
        // The third crash in five minutes turns the package off.
        QTRY_COMPARE_WITH_TIMEOUT(manager.health(Package).value("retryInMs").toLongLong(), 0, 7000);
        QVERIFY(!manager.invoke(FixtureContribution, QStringLiteral("roots"), {}).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(manager.errorMessage().contains(QLatin1String("failed three times")), 15000);
        InstalledPackage installed;
        QVERIFY(PackageStore(profile.path()).installed(Package, &installed));
        QVERIFY(!installed.enabled);
        QCOMPARE(crashes(manager), 0);
        // Turning it on again is the explicit retry: the ledger starts over.
        QVERIFY(manager.setEnabled(Package, true));
        QVERIFY(!manager.invoke(FixtureContribution, QStringLiteral("roots"), {}).isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(crashes(manager), 1, 15000);
    }
};
QTEST_GUILESS_MAIN(TestAddonWorkerHealth)
#include "test_addon_worker_health.moc"
