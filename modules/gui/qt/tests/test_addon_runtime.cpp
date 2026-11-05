// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QSemaphore>
#include <QDynamicPropertyChangeEvent>
#include <memory>
#include <atomic>
#include <thread>
#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif
#include "addons/addon_manager.hpp"
#include "addons/addon_stream_lifetime.hpp"
#include "addons/addon_repository_jobs.hpp"

using namespace vlc::addons;

#ifdef Q_OS_LINUX
namespace {
class TestCredentialPlatform final {
    QTemporaryDir directory;
    QByteArray oldPath = qgetenv("PATH"), oldBus = qgetenv("DBUS_SESSION_BUS_ADDRESS");
public:
    bool start() {
        QFile helper(directory.filePath(QStringLiteral("secret-tool")));
        if (!helper.open(QIODevice::WriteOnly)) return false;
        const QByteArray script = R"PY(#!/usr/bin/python3
import json, pathlib, sys
root = pathlib.Path(__file__).parent
args = sys.argv[1:]
values = json.loads((root / 'values.json').read_text()) if (root / 'values.json').exists() else {}
options = dict(zip(args[1::2], args[2::2])) if args[0] != 'store' else dict(zip(args[2::2], args[3::2]))
package = options.get('package', '')
reference = options.get('reference', '')
if args[0] == 'store':
    values[package + '/' + reference] = sys.stdin.read()
elif args[0] == 'lookup':
    value = values.get(package + '/' + reference, '')
    sys.stdout.write(value)
    sys.exit(0 if value else 1)
elif args[0] == 'clear':
    (root / 'clear.json').write_text(json.dumps(args))
    if (root / 'gate').exists():
        with (root / 'gate').open() as gate: gate.read(1)
    if (root / 'fail').exists():
        sys.stderr.write('Test platform refusal')
        sys.exit(2)
    values = {key: value for key, value in values.items() if not (key.startswith(package + '/') and (not reference or key == package + '/' + reference))}
else:
    sys.exit(2)
(root / 'values.json').write_text(json.dumps(values))
)PY";
        if (helper.write(script) != script.size()) return false;
        helper.close(); helper.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        const QByteArray path = directory.path().toUtf8() + ':' + oldPath;
        qputenv("PATH", path); qputenv("DBUS_SESSION_BUS_ADDRESS", "test-only");
        return true;
    }
    ~TestCredentialPlatform() { unblock(); qputenv("PATH", oldPath); if (oldBus.isNull()) qunsetenv("DBUS_SESSION_BUS_ADDRESS"); else qputenv("DBUS_SESSION_BUS_ADDRESS", oldBus); }
    QString path(const QString& name) const { return directory.filePath(name); }
    bool block() { return ::mkfifo(QFile::encodeName(path(QStringLiteral("gate"))).constData(), 0600) == 0; }
    void unblock() {
        const int file = ::open(QFile::encodeName(path(QStringLiteral("gate"))).constData(), O_WRONLY | O_NONBLOCK);
        if (file >= 0) { const char value = 'x'; const auto written = ::write(file, &value, 1); Q_UNUSED(written); ::close(file); }
        QFile::remove(path(QStringLiteral("gate")));
    }
};
}
#endif

class RuntimeMediaCapabilities : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isStarted MEMBER loaded NOTIFY changed)
    Q_PROPERTY(bool seekable MEMBER seekable NOTIFY changed)
    Q_PROPERTY(bool pausable MEMBER pausable NOTIFY changed)
    Q_PROPERTY(bool ratechangable MEMBER rateChange NOTIFY changed)
    Q_PROPERTY(bool hasVideoOutput MEMBER video NOTIFY changed)
public:
    bool loaded = true, seekable = true, pausable = true, rateChange = false, video = true;
signals:
    void changed();
};

class TestAddonRuntime : public QObject
{
    Q_OBJECT
    QString reference() const { return QStringLiteral(VLC_ADDON_BUNDLED_ROOT "/thesubtitledb"); }
    static QVariantMap replace(const QString& key) {
        return {{"apiKey", QVariantMap{{"operation", "replace"}, {"value", key}, {"sessionOnly", true}}}};
    }
private slots:
    void completedRemovalKeepsItsUiStateUntilTerminalDelivery()
    {
#ifdef Q_OS_LINUX
        TestCredentialPlatform platform; QVERIFY(platform.start());
        AddonStreamLifetime lifetime; const auto handle = lifetime.handle();
        QVERIFY(AddonStreamLifetime::registerApplication(handle));
        const auto close = qScopeGuard([&] { handle.close(); handle.waitQt(std::chrono::seconds(5)); AddonStreamLifetime::unregisterApplication(handle); });
        QTemporaryDir storage; AddonManager manager(storage.path()); const QString id = QStringLiteral("org.hakone.thesubtitledb");
        QVERIFY(manager.installLocalPackage(reference(), true));
        QVERIFY(manager.uninstall(id, true));
        QElapsedTimer deadline; deadline.start();
        while (!handle.qtDrained() && deadline.elapsed() < 5000) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        QVERIFY(handle.qtDrained()); QVERIFY(manager.busy());
        const auto rows = manager.packages(); QCOMPARE(rows.size(), 1);
        QVERIFY(rows.first().toMap().value("removing").toBool()); QVERIFY(!rows.first().toMap().value("installed").toBool());
        QVERIFY(manager.contributionsOfType(QStringLiteral("metadata.provider")).isEmpty());
        QTRY_VERIFY(!manager.busy()); QVERIFY(manager.packages().isEmpty());
#else
        QSKIP("This fixture uses a temporary Linux credential command.");
#endif
    }
    void removalObserverCanDeleteTheManagerBeforeJobAdmission()
    {
        QTemporaryDir storage; auto* manager = new AddonManager(storage.path());
        const QString id = QStringLiteral("org.hakone.thesubtitledb");
        QVERIFY(manager->installLocalPackage(reference(), true));
        const QPointer<AddonManager> guard(manager);
        connect(manager, &AddonManager::packageWithdrawn, this, [manager] { delete manager; }, Qt::DirectConnection);
        QVERIFY(!manager->uninstall(id, false)); QVERIFY(!guard);
        PackageStore store(storage.path()); InstalledPackage package;
        QVERIFY(store.installed(id, &package)); QVERIFY(!store.packageRetiring(id));
    }
    void removalRefusalKeepsTemporaryKeysAndSavedState()
    {
        QTemporaryDir storage; AddonManager manager(storage.path());
        const QString id = QStringLiteral("org.hakone.thesubtitledb");
        QVERIFY(manager.installLocalPackage(reference(), true));
        QVERIFY(manager.saveAddonSettings(id, {{"language", "en"}}, replace(QStringLiteral("test-temporary-key"))));
        QVERIFY(manager.setEnabled(id, true));
        const auto token = manager.storeAuthorityToken();
        QVERIFY(!manager.uninstall(id, true)); QVERIFY(!manager.busy()); QVERIFY(!token.current());
        QVERIFY(manager.settingsSecretStatus(id).value("apiKey").toMap().value("present").toBool());
        PackageStore store(storage.path()); InstalledPackage package;
        QVERIFY(store.installed(id, &package)); QVERIFY(package.enabled); QVERIFY(!store.packageRetiring(id));
        QVERIFY(!store.distributionSuppressed(id));
    }
    void removalQuotaRefusalDoesNotForgetTemporaryKeys()
    {
        QSemaphore entered, release;
        AddonStreamLifetime lifetime; const auto handle = lifetime.handle();
        QVERIFY(AddonStreamLifetime::registerApplication(handle));
        const auto close = qScopeGuard([&] { release.release(2); handle.close(); handle.waitQt(std::chrono::seconds(5)); AddonStreamLifetime::unregisterApplication(handle); });
        AddonRepositoryJobs first(handle), second(handle);
        const auto work = [&] { entered.release(); release.acquire(); };
        QVERIFY(first.submit(work, [](AddonRepositoryJobs::Result) {}));
        QVERIFY(second.submit(work, [](AddonRepositoryJobs::Result) {}));
        QVERIFY(entered.tryAcquire(2, 5000)); QCOMPARE(AddonRepositoryJobs::processCount(), 2u);
        QTemporaryDir storage; AddonManager manager(storage.path()); const QString id = QStringLiteral("org.hakone.thesubtitledb");
        QVERIFY(manager.installLocalPackage(reference(), true));
        QVERIFY(manager.saveAddonSettings(id, {{"language", "en"}}, replace(QStringLiteral("test-temporary-key"))));
        const auto token = manager.storeAuthorityToken();
        QVERIFY(!manager.uninstall(id, true)); QVERIFY(!manager.busy()); QVERIFY(!token.current());
        QVERIFY(manager.settingsSecretStatus(id).value("apiKey").toMap().value("present").toBool());
        PackageStore store(storage.path()); InstalledPackage package;
        QVERIFY(store.installed(id, &package)); QVERIFY(!store.packageRetiring(id)); QVERIFY(!store.distributionSuppressed(id));
    }
    void safeModeCanWithdrawPackagesDuringRepositoryWork()
    {
        QTemporaryDir storage; AddonStreamLifetime lifetime; const auto handle = lifetime.handle();
        QVERIFY(AddonStreamLifetime::registerApplication(handle));
        const auto close = qScopeGuard([&] { handle.close(); handle.waitQt(std::chrono::seconds(5)); AddonStreamLifetime::unregisterApplication(handle); });
        AddonManager manager(storage.path());
        QVERIFY(manager.stageRepository(QStringLiteral("http://example.invalid/no-network.git"), QStringLiteral("v1.0.0")));
        QVERIFY(manager.busy()); QVERIFY(manager.setSafeMode(true)); QVERIFY(manager.safeMode());
        QVERIFY(manager.busy()); manager.discardPendingPackage(); QTRY_VERIFY(!manager.busy());
        QVERIFY(manager.safeMode()); QVERIFY(manager.setSafeMode(false)); QVERIFY(!manager.safeMode());
    }
    void cachedContributionListingsFollowSafeMode()
    {
        QTemporaryDir storage; AddonManager manager(storage.path());
        QVERIFY(manager.installLocalPackage(reference(), true));
        QVERIFY(manager.setEnabled(QStringLiteral("org.hakone.thesubtitledb"), true));
        const auto type = QStringLiteral("metadata.provider");
        const auto enabled = [&] { return manager.contributionsOfType(type, true).first().toMap().value("enabled").toBool(); };
        QVERIFY(enabled()); QCOMPARE(manager.contributionsOfType(type).size(), 1);
        QVERIFY(manager.setSafeMode(true));
        QVERIFY(!enabled()); QVERIFY(manager.contributionsOfType(type).isEmpty());
        QVERIFY(manager.setSafeMode(false));
        QVERIFY(enabled()); QCOMPARE(manager.contributionsOfType(type).size(), 1);
    }
    void pendingRemovalWithdrawsEveryManagerBeforeTheWorkerStarts()
    {
#ifdef Q_OS_LINUX
        TestCredentialPlatform platform; QVERIFY(platform.start()); QVERIFY(platform.block());
        QTemporaryDir storage; AddonStreamLifetime lifetime; const auto handle = lifetime.handle();
        QVERIFY(AddonStreamLifetime::registerApplication(handle));
        const auto close = qScopeGuard([&] { platform.unblock(); handle.close(); handle.waitQt(std::chrono::seconds(5)); AddonStreamLifetime::unregisterApplication(handle); });
        AddonManager manager(storage.path()), peer(storage.path());
        const QString id = QStringLiteral("org.hakone.thesubtitledb"), contribution = id + QStringLiteral("/search");
        QVERIFY(manager.installLocalPackage(reference(), true));
        QVERIFY(manager.saveAddonSettings(id, {{"language", "en"}}, {{"apiKey", QVariantMap{{"operation", "replace"}, {"value", "test-persistent-key"}}}}));
        QVERIFY(manager.setEnabled(id, true));
        const auto previous = peer.nativeAuthorityReader(contribution)(); QVERIFY(previous.has_value()); QVERIFY(previous->live());
        PackageStore store(storage.path()); InstalledPackage package; QVERIFY(store.installed(id, &package));
        QVERIFY(manager.uninstall(id, true)); QVERIFY(manager.busy());
        QVERIFY(!previous->live()); QVERIFY(store.packageRetiring(id));
        QVERIFY(!peer.nativeAuthorityReader(contribution)().has_value());
        QVERIFY(!store.networkConfiguration(package, nullptr));
        QVERIFY(!peer.setEnabled(id, true)); QVERIFY(!peer.saveAddonSettings(id, {{"language", "en"}}, replace(QStringLiteral("new-key"))));
        QTRY_VERIFY(QFileInfo::exists(platform.path(QStringLiteral("clear.json"))));
        platform.unblock(); QTRY_VERIFY(!manager.busy()); QVERIFY2(manager.errorMessage().isEmpty(), qPrintable(manager.errorMessage()));
        QVERIFY(!store.packageRetiring(id)); QVERIFY(!store.installed(id, &package));
#else
        QSKIP("This fixture uses a temporary Linux credential command.");
#endif
    }
    void failedKeyDeletionKeepsThePackageDisabledForRetry()
    {
#ifdef Q_OS_LINUX
        TestCredentialPlatform platform; QVERIFY(platform.start());
        QFile failure(platform.path(QStringLiteral("fail"))); QVERIFY(failure.open(QIODevice::WriteOnly)); failure.close();
        QTemporaryDir storage; AddonStreamLifetime lifetime; const auto handle = lifetime.handle();
        QVERIFY(AddonStreamLifetime::registerApplication(handle));
        const auto close = qScopeGuard([&] { handle.close(); handle.waitQt(std::chrono::seconds(5)); AddonStreamLifetime::unregisterApplication(handle); });
        AddonManager manager(storage.path()); const QString id = QStringLiteral("org.hakone.thesubtitledb");
        QVERIFY(manager.installLocalPackage(reference(), true)); QVERIFY(manager.setEnabled(id, true));
        QVERIFY(manager.uninstall(id, true)); QTRY_VERIFY(!manager.busy());
        QVERIFY(manager.errorMessage().contains(QStringLiteral("saved keys")));
        PackageStore store(storage.path()); InstalledPackage package; QVERIFY(store.installed(id, &package)); QVERIFY(!package.enabled);
        QVERIFY(QFile::remove(failure.fileName()));
        QVERIFY(manager.uninstall(id, true)); QTRY_VERIFY(!manager.busy()); QVERIFY(manager.errorMessage().isEmpty());
        QVERIFY(!store.installed(id, &package));
#else
        QSKIP("This fixture uses a temporary Linux credential command.");
#endif
    }
    void packageCredentialDeletionFindsUnknownReferencesAndKeepsOtherPackages()
    {
#ifdef Q_OS_LINUX
        TestCredentialPlatform platform; QVERIFY(platform.start());
        CredentialStore credentials;
        const QString id = QStringLiteral("org.example.remove"), sibling = QStringLiteral("org.example.remove-other");
        const auto first = credentials.create(id, QStringLiteral("key"), QByteArray("test-first"));
        const auto lost = credentials.create(id, QStringLiteral("key"), QByteArray("test-lost-reference"));
        const auto kept = credentials.create(sibling, QStringLiteral("key"), QByteArray("test-neighbor"));
        QVERIFY(first.persistent); QVERIFY(lost.persistent); QVERIFY(kept.persistent);
        // The owned cleanup store has no reference index or cached credential.
        CredentialStore cleanup;
        QVERIFY(!cleanup.removePackage(QStringLiteral("org.example.remove/*")));
        QVERIFY(!QFileInfo::exists(platform.path(QStringLiteral("clear.json"))));
        QVERIFY(cleanup.removePackage(id)); credentials.forgetPackage(id);
        QVERIFY(credentials.read(id, first.reference).isEmpty()); QVERIFY(credentials.read(id, lost.reference).isEmpty());
        QCOMPARE(credentials.read(sibling, kept.reference), QByteArray("test-neighbor"));
        QFile call(platform.path(QStringLiteral("clear.json"))); QVERIFY(call.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(call.readAll()).array(), QJsonArray({"clear", "application", "vlc-addons", "package", id}));
#else
        QSKIP("This fixture uses a temporary Linux credential command.");
#endif
    }
    void ordinaryRemovalKeepsSettingsKeysAndALaterCleanupAction()
    {
#ifdef Q_OS_LINUX
        TestCredentialPlatform platform; QVERIFY(platform.start());
        AddonStreamLifetime lifetime; const auto handle = lifetime.handle();
        QVERIFY(AddonStreamLifetime::registerApplication(handle));
        const auto close = qScopeGuard([&] { handle.close(); handle.waitQt(std::chrono::seconds(5)); AddonStreamLifetime::unregisterApplication(handle); });
        QTemporaryDir storage; AddonManager manager(storage.path()); const QString id = QStringLiteral("org.hakone.thesubtitledb");
        QVERIFY(manager.installLocalPackage(reference(), true));
        QVERIFY(manager.saveAddonSettings(id, {{"language", "fr"}}, replace(QStringLiteral("test-kept-key"))));
        QVERIFY(manager.uninstall(id, false)); QTRY_VERIFY(!manager.busy()); QVERIFY(manager.errorMessage().isEmpty());
        const auto rows = manager.packages(); QCOMPARE(rows.size(), 1);
        QVERIFY(rows.first().toMap().value("savedData").toBool()); QVERIFY(!rows.first().toMap().value("installed").toBool());
        QVERIFY(!QFileInfo::exists(platform.path(QStringLiteral("clear.json"))));
        QVERIFY(manager.installLocalPackage(reference(), true));
        QCOMPARE(manager.settingsValues(id).value("language").toString(), QStringLiteral("fr"));
        QVERIFY(manager.settingsSecretStatus(id).value("apiKey").toMap().value("present").toBool());
        QVERIFY(manager.uninstall(id, false)); QTRY_VERIFY(!manager.busy());
        QVERIFY(manager.uninstall(id, true)); QTRY_VERIFY(!manager.busy()); QVERIFY(manager.errorMessage().isEmpty());
        QVERIFY(manager.packages().isEmpty());
#else
        QSKIP("This fixture uses a temporary Linux credential command.");
#endif
    }
    void corruptStateCannotHideKeysFromFullRemoval()
    {
#ifdef Q_OS_LINUX
        TestCredentialPlatform platform; QVERIFY(platform.start());
        AddonStreamLifetime lifetime; const auto handle = lifetime.handle();
        QVERIFY(AddonStreamLifetime::registerApplication(handle));
        const auto close = qScopeGuard([&] { handle.close(); handle.waitQt(std::chrono::seconds(5)); AddonStreamLifetime::unregisterApplication(handle); });
        QTemporaryDir storage; AddonManager manager(storage.path()); const QString id = QStringLiteral("org.hakone.thesubtitledb");
        QVERIFY(manager.installLocalPackage(reference(), true));
        CredentialStore orphan;
        const auto lost = orphan.create(id, QStringLiteral("key"), QByteArray("test-unindexed-key"));
        const auto neighbor = orphan.create(QStringLiteral("org.example.neighbor"), QStringLiteral("key"), QByteArray("test-neighbor-key"));
        QVERIFY(lost.persistent); QVERIFY(neighbor.persistent);
        const auto path = storage.filePath(QStringLiteral("packages/") + id + QStringLiteral("/state.json"));
        for (const auto& filePath : QStringList{path, path + QStringLiteral(".backup")}) {
            QFile file(filePath); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("broken", 6), qint64(6));
        }
        QVERIFY(manager.packages().first().toMap().value("recoveryRequired").toBool());
        QVERIFY(manager.uninstall(id, true)); QTRY_VERIFY(!manager.busy()); QVERIFY2(manager.errorMessage().isEmpty(), qPrintable(manager.errorMessage()));
        CredentialStore fresh;
        QVERIFY(fresh.read(id, lost.reference).isEmpty());
        QCOMPARE(fresh.read(QStringLiteral("org.example.neighbor"), neighbor.reference), QByteArray("test-neighbor-key"));
        QVERIFY(manager.packages().isEmpty());
#else
        QSKIP("This fixture uses a temporary Linux credential command.");
#endif
    }
    void repositoryWorkRequiresAnAppOwnerAndKeepsPublicResultState()
    {
        QTemporaryDir firstDirectory,secondDirectory;
        { AddonManager absent(firstDirectory.path());
          QVERIFY(!absent.stageRepository(QStringLiteral("http://example.org/no-fetch.git"),QStringLiteral("v1.0.0")));
          QVERIFY(!absent.busy()); QVERIFY(!absent.errorMessage().isEmpty()); }
        AddonStreamLifetime lifetime; const auto handle=lifetime.handle();
        QVERIFY(AddonStreamLifetime::registerApplication(handle));
        const auto retire=qScopeGuard([&] { handle.close(); handle.waitQt(std::chrono::seconds(5)); AddonStreamLifetime::unregisterApplication(handle); });
        AddonManager manager(secondDirectory.path());
        QVERIFY(manager.stageRepository(QStringLiteral("http://example.org/no-fetch.git"),QStringLiteral("v1.0.0")));
        QVERIFY(manager.busy()); manager.discardPendingPackage();
        QTRY_VERIFY(!manager.busy()); QVERIFY(manager.pendingPackage().isEmpty());
        QCOMPARE(manager.errorMessage(),QStringLiteral("The app canceled the repository request."));
        QVERIFY(manager.checkRepositoryUpdates(QStringLiteral("org.example.absent"),QStringLiteral("^1.0.0")));
        QCOMPARE(manager.updateResult().value("checking").toBool(),true);
        QTRY_VERIFY(!manager.busy()); QVERIFY(!manager.updateResult().value("checking").toBool());
        QVERIFY(!manager.updateResult().value("available").toBool()); QVERIFY(!manager.errorMessage().isEmpty());
    }
    void accountReferencesBindCredentialsButNotDisplaySettingsOrReleaseBytes() {
        QTemporaryDir storage, update; AddonManager manager(storage.path());
        const QString id = QStringLiteral("org.hakone.thesubtitledb");
        QVERIFY(manager.installLocalPackage(reference(), true));
        const auto account = [&] {
            return manager.contributionsOfType(QStringLiteral("metadata.provider"), true).first().toMap()
                .value("accountReference").toString();
        };
        const auto disconnected = account(); QVERIFY(disconnected.startsWith(QStringLiteral("disconnected:")));
        QVERIFY(manager.saveAddonSettings(id, {{"language", "en"}}, replace(QStringLiteral("unit-account-key"))));
        const auto first = account(); QVERIFY(first.startsWith(QStringLiteral("account:"))); QVERIFY(first != disconnected);
        QVERIFY(!first.contains(QStringLiteral("unit-account-key")));
        Manifest original; QMap<QString, QByteArray> contents; QString error;
        QVERIFY(Manifest::loadDirectory(reference(), &original, &contents, &error));
        auto document = original.document; document.insert("version", "1.0.1");
        contents.insert(QStringLiteral("addon.json"), QJsonDocument(document).toJson());
        for (auto it = contents.begin(); it != contents.end(); ++it) {
            const auto path = update.filePath(it.key()); QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
            QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(it.value()), it.value().size());
        }
        QVERIFY2(manager.installLocalPackage(update.path(), true), qPrintable(manager.errorMessage())); QCOMPARE(account(), first);
        QVERIFY(manager.saveAddonSettings(id, {{"language", "en"}}, replace(QStringLiteral("unit-account-key"))));
        const auto rotated = account(); QVERIFY(rotated != first);
        QVERIFY(manager.saveAddonSettings(id, {{"language", "fr"}}, {})); const auto settingsChanged = account(); QCOMPARE(settingsChanged, rotated);
        QVERIFY(manager.saveAddonSettings(id, {{"language", "fr"}}, {{"apiKey", QVariantMap{{"operation", "delete"}}}}));
        QVERIFY(account() != settingsChanged); QVERIFY(account().startsWith(QStringLiteral("disconnected:")));
    }

    void namedAccountFieldsChangeIdentityWhileDisplayFieldsDoNot() {
        QTemporaryDir storage, source; AddonManager manager(storage.path());
        Manifest original; QMap<QString, QByteArray> contents; QString error;
        QVERIFY(Manifest::loadDirectory(reference(), &original, &contents, &error));
        auto document = original.document; auto settings = document.value("settings").toObject();
        settings.insert("accountFields", QJsonArray{QStringLiteral("username")}); document.insert("settings", settings);
        auto schema = QJsonDocument::fromJson(contents.value(original.settingsSchema)).object();
        auto properties = schema.value("properties").toObject();
        properties.insert("username", QJsonObject{{"type", "string"}, {"maxLength", 128}, {"default", "guest"}}); schema.insert("properties", properties);
        contents.insert(original.settingsSchema, QJsonDocument(schema).toJson());
        contents.insert(QStringLiteral("addon.json"), QJsonDocument(document).toJson());
        const auto write = [&] {
            for (auto it = contents.begin(); it != contents.end(); ++it) {
                const auto path = source.filePath(it.key()); if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
                QFile file(path); if (!file.open(QIODevice::WriteOnly) || file.write(it.value()) != it.value().size()) return false;
            }
            return true;
        };
        QVERIFY(write()); QVERIFY2(manager.installLocalPackage(source.path(), true), qPrintable(manager.errorMessage()));
        const QString id = original.id;
        const auto account = [&] { return manager.contributionsOfType(QStringLiteral("metadata.provider"), true).first().toMap().value("accountReference").toString(); };
        QVERIFY(manager.saveAddonSettings(id, {{"language", "en"}, {"username", "alice"}}, replace(QStringLiteral("unit-account"))));
        const auto alice = account();
        QVERIFY(manager.saveAddonSettings(id, {{"language", "fr"}, {"username", "alice"}}, {})); QCOMPARE(account(), alice);
        QVERIFY(manager.saveAddonSettings(id, {{"language", "fr"}, {"username", "bob"}}, {})); QVERIFY(account() != alice);
        const auto bob = account();
        document.insert("version", "1.0.1"); contents.insert(QStringLiteral("addon.json"), QJsonDocument(document).toJson());
        QVERIFY(write()); QVERIFY2(manager.installLocalPackage(source.path(), true), qPrintable(manager.errorMessage())); QCOMPARE(account(), bob);
        for (const auto& fields : {QJsonArray{QStringLiteral("apiKey")}, QJsonArray{QStringLiteral("missing")},
                QJsonArray{QStringLiteral("username"), QStringLiteral("username")}, QJsonArray{1}}) {
            settings.insert("accountFields", fields); document.insert("settings", settings);
            contents.insert(QStringLiteral("addon.json"), QJsonDocument(document).toJson()); QVERIFY(write());
            Manifest rejected; QVERIFY(!Manifest::loadDirectory(source.path(), &rejected));
        }
        QJsonArray tooMany; for (int i = 0; i < 17; ++i) tooMany.append(QStringLiteral("field%1").arg(i));
        settings.insert("accountFields", tooMany); document.insert("settings", settings);
        QVERIFY(!Manifest::parse(QJsonDocument(document).toJson(), &original));
    }
    void publicAccountMarkersRemainStableWithoutAccountSettings() {
        QTemporaryDir storage; AddonManager manager(storage.path());
        const QString directory = QStringLiteral(VLC_ADDON_BUNDLED_ROOT "/tvmaze");
        QVERIFY(manager.installLocalPackage(directory, true));
        const auto descriptor = manager.contributionsOfType(QStringLiteral("metadata.provider"), true).first().toMap();
        const auto account = descriptor.value("accountReference").toString(); QVERIFY(account.startsWith(QStringLiteral("public:")));
        QVERIFY(manager.saveAddonSettings(QStringLiteral("org.hakone.tvmaze"), {}, {}));
        QCOMPARE(manager.contributionsOfType(QStringLiteral("metadata.provider"), true).first().toMap().value("accountReference").toString(), account);
    }

    void capabilitiesOnlyExposeRegisteredApisAndBoundedMediaState() {
        QTemporaryDir storage; AddonManager manager(storage.path()); manager.setWorkerExecutable({});
        auto view = manager.capabilities(); QVERIFY(!view.value("workerAvailable").toBool());
        QVERIFY(view.value("privateStorageAvailable").toBool()); QVERIFY(!view.value("webSocketAvailable").toBool());
        QCOMPARE(view.value("coreOperations").toStringList(), (QStringList{QStringLiteral("storage.get"), QStringLiteral("storage.remove"), QStringLiteral("storage.set")}));
        QVERIFY(!view.value("media").toMap().value("known").toBool());
        auto media = std::make_unique<RuntimeMediaCapabilities>();
        media->setProperty("url", QStringLiteral("file:///private/test-media.mkv"));
        manager.setMediaCapabilitiesSource(media.get()); QSignalSpy changed(&manager, &AddonManager::capabilitiesChanged);
        view = manager.capabilities(); QVERIFY(view.value("media").toMap().value("loaded").toBool());
        QVERIFY(view.value("media").toMap().value("seekable").toBool());
        QVERIFY(!QJsonDocument::fromVariant(view).toJson().contains("private/test-media"));
        media->loaded = false; emit media->changed(); QVERIFY(!changed.isEmpty());
        QVERIFY(!manager.capabilities().value("media").toMap().value("loaded").toBool());
        auto lifetime = std::make_unique<QObject>();
        const auto handler = [](const auto&, const auto&, auto) -> AddonHostServices::Cancel { return {}; };
        QVERIFY(!manager.hostServices()->registerOperation(QStringLiteral("storage.get"), lifetime.get(), handler));
        QVERIFY(manager.hostServices()->registerOperation(QStringLiteral("network.websocket.open"), lifetime.get(), handler));
        QVERIFY(!manager.capabilities().value("webSocketAvailable").toBool());
        QVERIFY(manager.hostServices()->registerOperation(QStringLiteral("network.websocket.send"), lifetime.get(), handler));
        QVERIFY(manager.hostServices()->registerOperation(QStringLiteral("network.websocket.close"), lifetime.get(), handler));
        QVERIFY(!manager.capabilities().value("webSocketAvailable").toBool());
        QVERIFY(manager.hostServices()->registerOperation(QStringLiteral("network.websocket.receive"), lifetime.get(), handler));
        QVERIFY(manager.capabilities().value("privateStorageAvailable").toBool());
        QVERIFY(manager.capabilities().value("webSocketAvailable").toBool());
        lifetime.reset(); QVERIFY(!manager.capabilities().value("webSocketAvailable").toBool());
        QVERIFY(manager.capabilities().value("privateStorageAvailable").toBool());
        media.reset(); QVERIFY(!manager.capabilities().value("media").toMap().value("known").toBool());
    }

    void nativeSourceCapabilityRequiresAnExplicitAppMount() {
        QTemporaryDir storage; AddonManager manager(storage.path());
        QSignalSpy changed(&manager,&AddonManager::capabilitiesChanged);
        QVERIFY(!manager.capabilities().value("mediaStreamAvailable").toBool());
        QVERIFY(manager.metaObject()->indexOfMethod("setNativeStreamAvailable(bool)")<0);
        manager.setNativeStreamAvailable(false); QCOMPARE(changed.size(),0);
        manager.setNativeStreamAvailable(true); QCOMPARE(changed.size(),1);
        QVERIFY(manager.capabilities().value("mediaStreamAvailable").toBool());
        manager.setNativeStreamAvailable(true); QCOMPARE(changed.size(),1);
        manager.setNativeStreamAvailable(false); QCOMPARE(changed.size(),2);
        QVERIFY(!manager.capabilities().value("mediaStreamAvailable").toBool());
    }

    void retirementCannotPublishNativePlaybackAvailability() {
        QTemporaryDir storage; auto manager=std::make_unique<AddonManager>(storage.path());
        manager->setNativeStreamAvailable(true);
        bool retired=false;
        const auto owner=manager.get();
        connect(owner,&AddonManager::retiring,this,[&] {
            retired=true; QVERIFY(!owner->capabilities().value("mediaStreamAvailable").toBool());
            owner->setNativeStreamAvailable(true);
            QVERIFY(!owner->capabilities().value("mediaStreamAvailable").toBool());
        });
        manager.reset(); QVERIFY(retired);
    }

    void nativeCapabilityPropertyPublicationCanDeleteTheManager() {
        struct DeleteOnProperty final : QObject {
            bool eventFilter(QObject* watched,QEvent* event) override {
                if(event->type()!=QEvent::DynamicPropertyChange
                    || static_cast<QDynamicPropertyChangeEvent*>(event)->propertyName()!=QByteArrayLiteral("hakoneNativeStreamAvailable")) return false;
                delete watched; return true;
            }
        } filter;
        QTemporaryDir storage; QPointer<AddonManager> manager=new AddonManager(storage.path());
        manager->installEventFilter(&filter); manager->setNativeStreamAvailable(true);
        QVERIFY(!manager);
    }

    void nativeCapabilityObserversCanRemoveTheMount() {
        QTemporaryDir storage; AddonManager manager(storage.path());
        QSignalSpy changed(&manager,&AddonManager::capabilitiesChanged);
        connect(&manager,&AddonManager::capabilitiesChanged,this,[&] {
            if(manager.capabilities().value("mediaStreamAvailable").toBool()) manager.setNativeStreamAvailable(false);
        });
        manager.setNativeStreamAvailable(true); QCOMPARE(changed.size(),2);
        QVERIFY(!manager.capabilities().value("mediaStreamAvailable").toBool());
    }

    void reviewCapturesPackageAndCancellationDoesNotInstall() {
        // The package review reads and hashes the package on the app's work.
        AddonStreamLifetime lifetime; const auto handle = lifetime.handle();
        QVERIFY(AddonStreamLifetime::registerApplication(handle));
        const auto close = qScopeGuard([&] { handle.close(); handle.waitQt(std::chrono::seconds(5)); AddonStreamLifetime::unregisterApplication(handle); });
        QTemporaryDir storage; AddonManager manager(storage.path());
        QVERIFY(manager.stageLocalPackage(reference())); QTRY_VERIFY(!manager.busy());
        QCOMPARE(manager.pendingPackage().value("id").toString(), QStringLiteral("org.hakone.thesubtitledb"));
        QVERIFY(manager.packages().isEmpty());
        manager.discardPendingPackage(); QVERIFY(manager.pendingPackage().isEmpty());
        QVERIFY(!manager.approvePendingPackage()); QVERIFY(manager.packages().isEmpty());
        QVERIFY(manager.stageLocalPackage(reference())); QTRY_VERIFY(!manager.busy()); QVERIFY(manager.approvePendingPackage());
        QVERIFY(manager.pendingPackage().isEmpty()); QCOMPARE(manager.packages().size(), 1);
        QVERIFY(!manager.packages().first().toMap().value("enabled").toBool());
    }
    void preferenceWritesMergeAcrossStaleManagers()
    {
        QTemporaryDir directory; AddonManager first(directory.path()), second(directory.path());
        const QVariantMap descriptor{{"id", "hakone.builtin.example/search"}, {"packageId", "hakone.builtin.example"},
            {"name", "Example"}, {"configured", true}, {"enabled", true}};
        first.registerBuiltinProvider(QStringLiteral("subtitles"), descriptor);
        second.registerBuiltinProvider(QStringLiteral("subtitles"), descriptor);
        auto old = captureStoreAuthority(directory.path()); unsigned revoked = 0;
        auto watch = old.onInvalidated([&revoked] { ++revoked; }); QVERIFY(bool(watch));
        QVERIFY(first.setProviderOrder(QStringLiteral("subtitles"), {QStringLiteral("hakone.builtin.example/search")}));
        QCOMPARE(revoked, 1u); QVERIFY(!old.current());
        QVERIFY(second.setEnabled(QStringLiteral("hakone.builtin.example"), false));
        QVERIFY(first.setProviderOrder(QStringLiteral("artwork"), {QStringLiteral("org.hakone.artwork/lookup")}));
        AddonManager restored(directory.path()); restored.registerBuiltinProvider(QStringLiteral("subtitles"), descriptor);
        QCOMPARE(restored.providerOrder(QStringLiteral("subtitles")), QStringList{QStringLiteral("hakone.builtin.example/search")});
        QCOMPARE(restored.providerOrder(QStringLiteral("artwork")), QStringList{QStringLiteral("org.hakone.artwork/lookup")});
        QVERIFY(!restored.providersForFeature(QStringLiteral("subtitles")).first().toMap().value("enabled").toBool());
        PackageStore store(directory.path()); ProviderPreferences captured; QVERIFY(store.providerPreferences(&captured));
        old = captured.authority; watch = old.onInvalidated([&revoked] { ++revoked; }); QVERIFY(bool(watch));
        bool revokedBeforeSignal = false;
        connect(&second, &AddonManager::revisionChanged, &second, [&] { revokedBeforeSignal = !old.current() && revoked == 2; });
        QVERIFY(second.setEnabled(QStringLiteral("hakone.builtin.example"), true));
        QVERIFY(revokedBeforeSignal); QVERIFY(!old.current());
        ProviderPreferences current; QVERIFY(store.providerPreferences(&current));
        QCOMPARE(current.values.value("artwork").toArray(), QJsonArray{"org.hakone.artwork/lookup"});
    }
    void builtinProvidersShareOrderingAndKeepDisabledPreference() {
        QTemporaryDir storage; AddonManager manager(storage.path());
        const QVariantMap descriptor{{"id", "hakone.builtin.example/search"}, {"packageId", "hakone.builtin.example"},
            {"name", "Built-in provider"}, {"enabled", true}, {"configured", true}, {"features", QStringList{QStringLiteral("subtitles")}}};
        manager.registerBuiltinProvider(QStringLiteral("subtitles"), descriptor);
        QCOMPARE(manager.providersForFeature(QStringLiteral("subtitles")).size(), 1);
        QVERIFY(manager.setEnabled(QStringLiteral("hakone.builtin.example"), false));
        manager.registerBuiltinProvider(QStringLiteral("subtitles"), descriptor);
        QVERIFY(!manager.providersForFeature(QStringLiteral("subtitles")).first().toMap().value("enabled").toBool());
        QVERIFY(manager.setProviderOrder(QStringLiteral("subtitles"), {QStringLiteral("hakone.builtin.example/search")}));
        QCOMPARE(manager.providerOrder(QStringLiteral("subtitles")), QStringList{QStringLiteral("hakone.builtin.example/search")});
        AddonManager restored(storage.path()); restored.registerBuiltinProvider(QStringLiteral("subtitles"), descriptor);
        QVERIFY(!restored.providersForFeature(QStringLiteral("subtitles")).first().toMap().value("enabled").toBool());
        manager.unregisterBuiltinProvider(QStringLiteral("hakone.builtin.example/search"));
        QVERIFY(manager.providersForFeature(QStringLiteral("subtitles")).isEmpty());
    }
    void oldBuiltinPreferencesMoveInMemoryBeforeAnExplicitSave()
    {
        QTemporaryDir directory;
        const QString path = directory.filePath(QStringLiteral("provider-order.json"));
        const QJsonObject saved{{"subtitles", QJsonArray{"vlc.builtin.first/search", "org.example.extra/search",
            "vlc.builtin.first/search", "vlc.builtin.second/search", "org.example.vlc.builtin.neighbor/search"}},
            {"_builtinEnabled", QJsonObject{{"vlc.builtin.first", false}, {"vlc.builtin.second", true}, {"org.example.extra", false}}}};
        const auto original = QJsonDocument(saved).toJson(QJsonDocument::Compact);
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(original), original.size()); file.close();
        const auto descriptor = [](const QString& name) {
            const QString package = QStringLiteral("hakone.builtin.") + name;
            const QString id = package + QStringLiteral("/search");
            return QVariantMap{{"id", id}, {"packageId", package}, {"name", name}, {"configured", true}, {"enabled", true}};
        };
        AddonManager manager(directory.path());
        manager.registerBuiltinProvider(QStringLiteral("subtitles"), descriptor(QStringLiteral("second")));
        manager.registerBuiltinProvider(QStringLiteral("subtitles"), descriptor(QStringLiteral("first")));
        QCOMPARE(manager.providerOrder(QStringLiteral("subtitles")), QStringList({QStringLiteral("hakone.builtin.first/search"),
            QStringLiteral("org.example.extra/search"), QStringLiteral("hakone.builtin.second/search"), QStringLiteral("org.example.vlc.builtin.neighbor/search")}));
        const auto providers = manager.providersForFeature(QStringLiteral("subtitles")); QCOMPARE(providers.size(), 2);
        QCOMPARE(providers[0].toMap().value("id").toString(), QStringLiteral("hakone.builtin.first/search"));
        QVERIFY(!providers[0].toMap().value("enabled").toBool()); QVERIFY(providers[1].toMap().value("enabled").toBool());
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), original); file.close();
        QVERIFY(manager.setEnabled(QStringLiteral("hakone.builtin.first"), true));
        QVERIFY(file.open(QIODevice::ReadOnly)); const auto persisted = QJsonDocument::fromJson(file.readAll()).object(); file.close();
        const auto enabled = persisted.value("_builtinEnabled").toObject();
        QVERIFY(!enabled.contains("vlc.builtin.first")); QVERIFY(!enabled.contains("vlc.builtin.second"));
        QVERIFY(enabled.value("hakone.builtin.first").toBool()); QVERIFY(enabled.value("hakone.builtin.second").toBool());
        QVERIFY(!enabled.value("org.example.extra").toBool());
        AddonManager restored(directory.path()); restored.registerBuiltinProvider(QStringLiteral("subtitles"), descriptor(QStringLiteral("first")));
        QVERIFY(restored.providersForFeature(QStringLiteral("subtitles")).first().toMap().value("enabled").toBool());
    }
    void explicitHakoneBuiltinPreferencesWinAliases_data()
    {
        QTest::addColumn<bool>("oldEnabled"); QTest::addColumn<bool>("enabled");
        QTest::newRow("explicit-disabled") << true << false;
        QTest::newRow("explicit-enabled") << false << true;
    }
    void explicitHakoneBuiltinPreferencesWinAliases()
    {
        QFETCH(bool, oldEnabled); QFETCH(bool, enabled);
        QTemporaryDir directory; QFile file(directory.filePath(QStringLiteral("provider-order.json")));
        const QJsonObject saved{{"subtitles", QJsonArray{"vlc.builtin.example/search", "org.example.extra/search",
            "hakone.builtin.example/search", "vlc.builtin.example/search", "hakone.builtin.example/search"}},
            {"_builtinEnabled", QJsonObject{{"vlc.builtin.example", oldEnabled}, {"hakone.builtin.example", enabled}}}};
        const auto original = QJsonDocument(saved).toJson(QJsonDocument::Compact);
        QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(original), original.size()); file.close();
        AddonManager manager(directory.path());
        manager.registerBuiltinProvider(QStringLiteral("subtitles"), {{"id", "hakone.builtin.example/search"},
            {"packageId", "hakone.builtin.example"}, {"name", "Example"}, {"configured", true}, {"enabled", !enabled}});
        QCOMPARE(manager.providerOrder(QStringLiteral("subtitles")), QStringList({QStringLiteral("org.example.extra/search"), QStringLiteral("hakone.builtin.example/search")}));
        QCOMPARE(manager.providersForFeature(QStringLiteral("subtitles")).first().toMap().value("enabled").toBool(), enabled);
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), original);
    }
    void liveBuiltinRegistrationRejectsLegacyAndPackageConfusion()
    {
        QTemporaryDir directory; AddonManager manager(directory.path());
        for (const auto& ids : {qMakePair(QStringLiteral("vlc.builtin.example/search"), QStringLiteral("vlc.builtin.example")),
                 qMakePair(QStringLiteral("org.hakone.example/search"), QStringLiteral("org.hakone.example")),
                 qMakePair(QStringLiteral("hakone.builtin.example/search"), QStringLiteral("hakone.builtin.other"))})
            manager.registerBuiltinProvider(QStringLiteral("subtitles"), {{"id", ids.first}, {"packageId", ids.second}, {"name", "Refused fixture"}});
        QVERIFY(manager.providersForFeature(QStringLiteral("subtitles")).isEmpty());
        const QVariantMap current{{"id", "hakone.builtin.example/search"}, {"packageId", "hakone.builtin.example"}, {"name", "Example"}, {"configured", true}};
        manager.registerBuiltinProvider(QStringLiteral("subtitles"), current); manager.registerBuiltinProvider(QStringLiteral("subtitles"), current);
        QCOMPARE(manager.providersForFeature(QStringLiteral("subtitles")).size(), 1);
    }
    void packageNeedsExplicitGrantAndEnable() {
        QTemporaryDir storage; AddonManager manager(storage.path());
        QVERIFY(manager.inspectLocalPackage(reference()).value("valid").toBool());
        QVERIFY(!manager.installLocalPackage(reference(), false));
        QVERIFY2(manager.installLocalPackage(reference(), true), qPrintable(manager.errorMessage()));
        const auto providers = manager.providersForFeature(QStringLiteral("subtitles"));
        QCOMPARE(providers.size(), 1); const auto provider = providers.first().toMap();
        QVERIFY(!provider.value("enabled").toBool()); QVERIFY(!provider.value("configured").toBool());
        QSignalSpy done(&manager, &AddonManager::operationFinished);
        manager.invoke(provider.value("id").toString(), QStringLiteral("searchAssets"), {{"query", "Film"}});
        QVERIFY(done.wait()); QCOMPARE(done.last()[2].toString(), QStringLiteral("auth_required"));
    }
    void secretsStayOutOfConfigurationAndWorkerState() {
        QTemporaryDir storage; AddonManager manager(storage.path());
        QVERIFY2(manager.installLocalPackage(reference(), true), qPrintable(manager.errorMessage()));
        const QString id = QStringLiteral("org.hakone.thesubtitledb");
        QVERIFY(manager.saveAddonSettings(id, {{"language", "en"}}, replace(QStringLiteral("unit-only-secret"))));
        QVERIFY(manager.settingsSecretStatus(id).value("apiKey").toMap().value("present").toBool());
        QVERIFY(manager.settingsSecretStatus(id).value("apiKey").toMap().value("sessionOnly").toBool());
        QVERIFY(!manager.settingsValues(id).contains("apiKey"));
        QVERIFY(!manager.settingsValues(id).contains("_credentials"));
        QVERIFY(manager.setEnabled(id, true));
        QVERIFY(manager.providersForFeature(QStringLiteral("subtitles")).first().toMap().value("configured").toBool());
        QDirIterator files(storage.path(), QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) { QFile file(files.next()); QVERIFY(file.open(QIODevice::ReadOnly)); QVERIFY(!file.readAll().contains("unit-only-secret")); }
        QVERIFY(manager.saveAddonSettings(id, {{"language", "fr"}}, {}));
        QVERIFY(!manager.saveAddonSettings(id, {{"language", "xx/../../"}}, replace(QStringLiteral("replacement-secret"))));
        QCOMPARE(manager.settingsValues(id).value("language").toString(), QStringLiteral("fr"));
        QSignalSpy withdrawn(&manager, &AddonManager::packageWithdrawn);
        QVERIFY(manager.saveAddonSettings(id, {{"language", "en"}}, {{"apiKey", QVariantMap{{"operation", "delete"}}}}));
        QVERIFY(!withdrawn.isEmpty());
        QVERIFY(!manager.settingsSecretStatus(id).value("apiKey").toMap().value("present").toBool());
        QVERIFY(!manager.providersForFeature(QStringLiteral("subtitles")).first().toMap().value("configured").toBool());
        AddonManager restarted(storage.path());
        QVERIFY(!restarted.settingsSecretStatus(id).value("apiKey").toMap().value("present").toBool());
    }
    void secretReferencesCannotCrossPackages() {
        CredentialStore store;
        const auto secret = store.create(QStringLiteral("org.example.one"), QStringLiteral("key"), "test-only", true);
        QVERIFY(!secret.reference.isEmpty());
        QCOMPARE(store.read(QStringLiteral("org.example.one"), secret.reference), QByteArray("test-only"));
        QVERIFY(store.read(QStringLiteral("org.example.two"), secret.reference).isEmpty());
        store.remove(QStringLiteral("org.example.two"), secret.reference);
        QVERIFY(store.present(QStringLiteral("org.example.one"), secret.reference));
        store.remove(QStringLiteral("org.example.one"), secret.reference);
        QVERIFY(!store.present(QStringLiteral("org.example.one"), secret.reference));
    }
    void capturedCredentialsCannotFollowAChangedAccountOrRelease() {
        QTemporaryDir storage; auto manager = std::make_unique<AddonManager>(storage.path());
        const QString id = QStringLiteral("org.hakone.thesubtitledb");
        QVERIFY(manager->installLocalPackage(reference(), true));
        QVERIFY(manager->saveAddonSettings(id, {{"language", "en"}}, replace(QStringLiteral("first-unit-secret"))));
        QVERIFY(manager->setEnabled(id, true));
        PackageStore store(storage.path()); InstalledPackage package; QVERIFY(store.installed(id, &package));
        NetworkBroker::RequestGuard firstCurrent;
        const auto first = manager->credentialResolver(package, &firstCurrent);
        QVERIFY(firstCurrent()); QCOMPARE(first(QStringLiteral("apiKey")), QByteArray("first-unit-secret"));
        QVERIFY(first(QStringLiteral("otherField")).isEmpty());
        QJsonObject original; quint64 revision = 0; QVERIFY(store.settings(id, &original, &revision));
        QVERIFY(manager->saveAddonSettings(id, {{"language", "en"}}, replace(QStringLiteral("second-unit-secret"))));
        QVERIFY(!firstCurrent()); QVERIFY(first(QStringLiteral("apiKey")).isEmpty());
        QVERIFY(store.installed(id, &package));
        NetworkBroker::RequestGuard secondCurrent;
        const auto second = manager->credentialResolver(package, &secondCurrent);
        QVERIFY(secondCurrent()); QCOMPARE(second(QStringLiteral("apiKey")), QByteArray("second-unit-secret"));
        QJsonObject secondSettings; QVERIFY(store.settings(id, &secondSettings, &revision));
        // Another app instance can change the state file without this manager's signals.
        QVERIFY(store.saveSettings(id, original, revision));
        QVERIFY(!secondCurrent()); QVERIFY(second(QStringLiteral("apiKey")).isEmpty());
        QVERIFY(!firstCurrent()); QVERIFY(first(QStringLiteral("apiKey")).isEmpty());
        QVERIFY(store.installed(id, &package));
        NetworkBroker::RequestGuard releaseCurrent;
        const auto release = manager->credentialResolver(package, &releaseCurrent);
        QVERIFY(releaseCurrent());
        QVERIFY(store.setEnabled(id, false)); QVERIFY(!releaseCurrent()); QVERIFY(release(QStringLiteral("apiKey")).isEmpty());
        QVERIFY(store.setEnabled(id, true)); QVERIFY(!releaseCurrent());
        manager.reset(); QVERIFY(!firstCurrent()); QVERIFY(first(QStringLiteral("apiKey")).isEmpty());
    }
    void nativeBinaryAssetRequestsRejectAnExternalConfigurationChange() {
        QTemporaryDir storage; AddonManager manager(storage.path());
        const QString id = QStringLiteral("org.hakone.thesubtitledb");
        QVERIFY(manager.installLocalPackage(reference(), true));
        QVERIFY(manager.saveAddonSettings(id, {{"language", "en"}}, replace(QStringLiteral("unit-secret"))));
        QVERIFY(manager.setEnabled(id, true));
        int completed = 0;
        const auto callback = [&](const NetworkBroker::Response& response) {
            ++completed; QVERIFY(!response.error.isEmpty()); QVERIFY(response.body.isEmpty());
            QVERIFY(response.error.contains(QStringLiteral("changed")));
        };
        const QJsonObject request{{"url", "https://api.thesubtitledb.com/subtitles"}, {"credential", "apiKey"}};
        manager.fetchAsset(id + QStringLiteral("/search"), request, callback);
        PackageStore store(storage.path()); QJsonObject settings; quint64 revision = 0;
        QVERIFY(store.settings(id, &settings, &revision)); settings.insert("language", "fr");
        QVERIFY(store.saveSettings(id, settings, revision)); QTRY_COMPARE(completed, 1);
    }
    void fullRemovalRevokesAllCredentialGenerations() {
#ifdef Q_OS_LINUX
        TestCredentialPlatform platform; QVERIFY(platform.start());
        AddonStreamLifetime lifetime; const auto handle = lifetime.handle();
        QVERIFY(AddonStreamLifetime::registerApplication(handle));
        const auto close = qScopeGuard([&] { handle.close(); handle.waitQt(std::chrono::seconds(5)); AddonStreamLifetime::unregisterApplication(handle); });
        QTemporaryDir storage; AddonManager manager(storage.path());
        const QString id = QStringLiteral("org.hakone.thesubtitledb");
        QVERIFY(manager.installLocalPackage(reference(), true));
        QVERIFY(manager.saveAddonSettings(id, {{"language", "en"}}, replace(QStringLiteral("first-unit-secret"))));
        QVERIFY(manager.saveAddonSettings(id, {{"language", "en"}}, replace(QStringLiteral("second-unit-secret"))));
        PackageStore store(storage.path()); const auto references = store.credentialReferences(id);
        QCOMPARE(references.size(), 2);
        QVERIFY(manager.uninstall(id, true)); QTRY_VERIFY(!manager.busy()); QVERIFY(manager.errorMessage().isEmpty());
        QVERIFY(manager.installLocalPackage(reference(), true));
        quint64 revision = 0;
        for (const auto& reference : references) {
            QVERIFY(store.saveSettings(id, {{"language", "en"},
                {"_credentials", QJsonObject{{"apiKey", reference}}}}, revision++));
            QVERIFY(!manager.settingsSecretStatus(id).value("apiKey").toMap().value("present").toBool());
        }
#else
        QSKIP("This fixture uses a temporary Linux credential command.");
#endif
    }
    void workerFailureWithdrawsBeforeManagerObservers_data() {
        QTest::addColumn<bool>("deleteManager"); QTest::addColumn<bool>("deleteOnCompletion");
        QTest::newRow("retained-owner") << false << false;
        QTest::newRow("observer-deletes-owner") << true << false;
        QTest::newRow("completion-deletes-owner") << true << true;
    }
    void workerFailureWithdrawsBeforeManagerObservers() {
        QFETCH(bool, deleteManager); QFETCH(bool, deleteOnCompletion);
        QTemporaryDir storage; auto* manager = new AddonManager(storage.path());
        const QPointer<AddonManager> alive(manager);
        const auto cleanup = qScopeGuard([&] { if (alive) delete alive.data(); });
        QVERIFY(manager->installLocalPackage(reference(), true));
        const QString id = QStringLiteral("org.hakone.thesubtitledb");
        QVERIFY(manager->saveAddonSettings(id, {{"language", "en"}}, replace(QStringLiteral("test-only-key"))));
        QVERIFY(manager->setEnabled(id, true));
        manager->setWorkerExecutable(storage.filePath(QStringLiteral("missing-hakone-worker")));
        unsigned withdrawals = 0, completions = 0; bool exact = false;
        connect(manager, &AddonManager::operationFinished, this, [&](const QString&, const QString& contribution, const QString& status, const QVariantMap&, const QString&) {
            ++completions; exact = contribution == id + QStringLiteral("/search") && status == QLatin1String("error");
            if (deleteOnCompletion) delete manager;
        });
        connect(manager, &AddonManager::packageWithdrawn, this, [&](const QString& package) {
            ++withdrawals; exact = package == id;
            if (deleteManager && !deleteOnCompletion) delete manager;
        });
        const auto request = manager->invoke(id + QStringLiteral("/search"), QStringLiteral("searchAssets"), {{"query", "Film"}});
        QVERIFY(!request.isEmpty());
        QTRY_COMPARE(completions, 1u); QVERIFY(exact);
        if (deleteOnCompletion) { QVERIFY(!alive); QCOMPARE(withdrawals, 0u); return; }
        QTRY_COMPARE(withdrawals, 1u); QVERIFY(exact);
        if (deleteManager) QVERIFY(!alive);
        else QVERIFY(!manager->errorMessage().isEmpty());
    }
    void failedWorkerRetryKeepsBothReentrantInvocationsTracked() {
#ifdef Q_OS_UNIX
        QTemporaryDir storage; AddonManager manager(storage.path());
        QVERIFY(manager.installLocalPackage(reference(), true));
        const QString id = QStringLiteral("org.hakone.thesubtitledb");
        const QString contribution = id + QStringLiteral("/search");
        QVERIFY(manager.saveAddonSettings(id, {{"language", "en"}}, replace(QStringLiteral("test-only-key"))));
        QVERIFY(manager.setEnabled(id, true));
        QFile fixture(storage.filePath(QStringLiteral("retry-worker"))); QVERIFY(fixture.open(QIODevice::WriteOnly));
        const QByteArray script = R"PY(#!/usr/bin/python3
import json, pathlib, sys
root = pathlib.Path(__file__).parent
print(json.dumps({"type":"ready","protocol":"wasm-message-v1"}), flush=True)
for line in sys.stdin:
    message = json.loads(line)
    kind = message.get("type")
    if kind in ("invoke", "cancel"):
        with (root / (kind + '.jsonl')).open('a') as output:
            output.write(json.dumps(message['id']) + '\n')
)PY";
        QCOMPARE(fixture.write(script), script.size()); fixture.close();
        QVERIFY(fixture.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        std::atomic_bool revoked{false};
        std::optional<AddonNativeAuthority> authority;
        const auto reader = manager.nativeAuthorityReader(contribution);
        std::thread capture([reader, &authority] { authority = reader(); }); capture.join();
        QVERIFY(authority.has_value()); QVERIFY(authority->live && authority->live());
        const auto watch = authority->onInvalidated([&revoked] { revoked.store(true, std::memory_order_release); });
        QVERIFY(bool(watch));
        QString original, inner, outer, refused;
        bool gated = false, nestedRefused = false, deniedReader = false, revokedBeforeCompletion = false;
        unsigned withdrawals = 0, completions = 0, activeWorkers = 0;
        QObject observer;
        connect(&manager, &AddonManager::operationFinished, &observer, [&](const QString& request, const QString&, const QString&, const QVariantMap&, const QString&) {
            ++completions;
            if (request != original) return;
            revokedBeforeCompletion = revoked.load(std::memory_order_acquire) && !authority->live();
            refused = manager.invoke(contribution, QStringLiteral("searchAssets"), {});
            gated = refused.isEmpty();
            deniedReader = !manager.nativeAuthorityReader(contribution)().has_value();
            nestedRefused = !manager.setEnabled(id, false);
        });
        connect(&manager, &AddonManager::packageWithdrawn, &observer, [&](const QString& package) {
            if (package != id) return;
            ++withdrawals;
            if (withdrawals == 1) inner = manager.invoke(contribution, QStringLiteral("searchAssets"), {{"query", "Inner"}});
        });
        manager.setWorkerExecutable(storage.filePath(QStringLiteral("missing-hakone-worker")));
        original = manager.invoke(contribution, QStringLiteral("searchAssets"), {{"query", "Original"}});
        QVERIFY(!original.isEmpty());
        const auto initial = manager.findChildren<AddonWorker*>(); QCOMPARE(initial.size(), 1);
        connect(initial.first(), &AddonWorker::unavailable, &observer, [&](const QString&) {
            manager.setWorkerExecutable(fixture.fileName());
            outer = manager.invoke(contribution, QStringLiteral("searchAssets"), {{"query", "Outer"}});
            for (auto* worker : manager.findChildren<AddonWorker*>()) if (!worker->failed()) ++activeWorkers;
        });
        QTRY_VERIFY(!outer.isEmpty()); QVERIFY(!inner.isEmpty()); QVERIFY(inner != outer);
        QCOMPARE(withdrawals, 1u); QCOMPARE(activeWorkers, 1u);
        QVERIFY(gated); QVERIFY(refused.isEmpty()); QVERIFY(nestedRefused); QVERIFY(deniedReader); QVERIFY(revokedBeforeCompletion);
        const auto received = [&storage](const QString& kind) {
            QSet<QString> ids; QFile file(storage.filePath(kind + QStringLiteral(".jsonl")));
            if (!file.open(QIODevice::ReadOnly)) return ids;
            for (const auto& line : file.readAll().split('\n')) {
                if (line.isEmpty()) continue;
                const auto array = QJsonDocument::fromJson('[' + line + ']').array();
                if (array.size() == 1 && array.first().isString()) ids.insert(array.first().toString());
            }
            return ids;
        };
        const QSet<QString> expected{inner, outer};
        QTRY_COMPARE(received(QStringLiteral("invoke")), expected);
        manager.cancel(inner); manager.cancel(outer);
        QTRY_COMPARE(received(QStringLiteral("cancel")), expected);
        QCOMPARE(completions, 1u); QCOMPARE(withdrawals, 1u);
#else
        QSKIP("This process-cancellation fixture requires a Unix executable script.");
#endif
    }
    void realPortableWorkerHasNoAmbientCredential() {
        const auto executable = qEnvironmentVariable("VLC_ADDON_WORKER");
        if (executable.isEmpty()) QSKIP("Set VLC_ADDON_WORKER to exercise the packaged portable module");
        Manifest manifest; QVERIFY(Manifest::loadDirectory(reference(), &manifest));
        InstalledPackage package; package.manifest = manifest; package.enabled = true; package.directory = reference();
        package.grants = manifest.requiredPermissions;
        NetworkBroker network; AddonWorker worker(package, &network); worker.setExecutable(executable);
        worker.setConfiguration({}, {{"apiKey", QJsonObject{{"present", false}}}}, {});
        QSignalSpy done(&worker, &AddonWorker::completed);
        const auto request = worker.invoke(QStringLiteral("search"), QStringLiteral("searchAssets"), {{"query", "Film"}});
        QVERIFY(done.wait(15000)); QCOMPARE(done.last()[0].toString(), request);
        QCOMPARE(done.last()[1].toString(), QStringLiteral("auth_required"));
    }
    void workerRejectsAnExpiredConfigurationBeforeStartup() {
        Manifest manifest; QVERIFY(Manifest::loadDirectory(reference(), &manifest));
        InstalledPackage package; package.manifest = manifest; package.enabled = true; package.directory = reference();
        package.grants = manifest.requiredPermissions;
        NetworkBroker network; AddonWorker worker(package, &network); worker.setRequestGuard([] { return false; });
        QSignalSpy done(&worker, &AddonWorker::completed);
        worker.invoke(QStringLiteral("search"), QStringLiteral("searchAssets"), {{"query", "Film"}});
        QTRY_COMPARE(done.size(), 1); QCOMPARE(done[0][1].toString(), QStringLiteral("denied"));
    }
};
QTEST_GUILESS_MAIN(TestAddonRuntime)
#include "test_addon_runtime.moc"
