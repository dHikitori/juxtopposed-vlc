// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_manager.hpp"
#include "addons/addon_package_store.hpp"
#include "addons/addon_process_launch.hpp"
#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
using namespace vlc::addons;
namespace {
const QString Package = QStringLiteral("org.hakone.discovery-fixture");
const QString FixtureContribution = Package + QStringLiteral("/catalogue");
// The discovery fixture with a module that is not Wasm: every worker start
// fails before it is ready.
bool brokenPackage(const QString& target, const QString& version = {})
{
    const QDir source(QStringLiteral(VLC_ADDON_SOURCE_ROOT "/modules/gui/qt/tests/fixtures/addons/discovery-fixture"));
    if (!QDir().mkpath(QDir(target).filePath(QStringLiteral("dist")))) return false;
    for (const auto* file : {"addon.json", "LICENSE"})
        if (!QFile::copy(source.filePath(QLatin1String(file)), QDir(target).filePath(QLatin1String(file)))) return false;
    if (!version.isEmpty()) {
        // A later version, so the store accepts it as an update.
        QFile manifest(QDir(target).filePath(QStringLiteral("addon.json")));
        if (!manifest.open(QIODevice::ReadWrite)) return false;
        auto document = QJsonDocument::fromJson(manifest.readAll()).object();
        document.insert(QStringLiteral("version"), version);
        manifest.resize(0); manifest.seek(0);
        if (manifest.write(QJsonDocument(document).toJson()) <= 0) return false;
    }
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
    void anUpdateThatCrashesRestoresThePreviousRelease() {
        const auto worker = qEnvironmentVariable("VLC_ADDON_WORKER");
        if (worker.isEmpty()) QSKIP("VLC_ADDON_WORKER names no worker");
        const QString good = QStringLiteral(VLC_ADDON_SOURCE_ROOT "/modules/gui/qt/tests/fixtures/addons/discovery-fixture");
        QTemporaryDir profile, broken;
        QVERIFY(brokenPackage(broken.path(), QStringLiteral("9.9.9")));
        AddonManager manager(profile.path());
        manager.setWorkerExecutable(worker);
        QVERIFY2(manager.installLocalPackage(good, true), qPrintable(manager.errorMessage()));
        QVERIFY(manager.setEnabled(Package, true));
        InstalledPackage before;
        QVERIFY(PackageStore(profile.path()).installed(Package, &before));
        QVERIFY(!manager.health(Package).value("observing").toBool());
        // The update is installed and enters its health period.
        QVERIFY2(manager.installLocalPackage(broken.path(), true), qPrintable(manager.errorMessage()));
        InstalledPackage updated;
        QVERIFY(PackageStore(profile.path()).installed(Package, &updated));
        QVERIFY(updated.digest != before.digest);
        QVERIFY(manager.health(Package).value("observing").toBool());
        // Its first crash restores the previous release, without a wait or quarantine.
        QVERIFY(!manager.invoke(FixtureContribution, QStringLiteral("roots"), {}).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(manager.errorMessage().contains(QLatin1String("restored the previous version")), 15000);
        InstalledPackage restored;
        QVERIFY(PackageStore(profile.path()).installed(Package, &restored));
        QCOMPARE(restored.digest, before.digest);
        QVERIFY(restored.enabled);
        QCOMPARE(crashes(manager), 0);
        QVERIFY(!manager.health(Package).value("observing").toBool());
    }
};
QTEST_GUILESS_MAIN(TestAddonWorkerHealth)
#include "test_addon_worker_health.moc"
