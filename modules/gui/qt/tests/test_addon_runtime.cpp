// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QScopeGuard>
#include <memory>
#include "addons/addon_manager.hpp"
#include "addons/addon_stream_lifetime.hpp"

using namespace vlc::addons;

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
    QString reference() const { return QStringLiteral(VLC_ADDON_SOURCE_ROOT "/contrib-addons/examples/thesubtitledb"); }
    static QVariantMap replace(const QString& key) {
        return {{"apiKey", QVariantMap{{"operation", "replace"}, {"value", key}, {"sessionOnly", true}}}};
    }
private slots:
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
        const QString directory = QStringLiteral(VLC_ADDON_SOURCE_ROOT "/contrib-addons/examples/tvmaze");
        QVERIFY(manager.installLocalPackage(directory, true));
        const auto descriptor = manager.contributionsOfType(QStringLiteral("metadata.provider"), true).first().toMap();
        const auto account = descriptor.value("accountReference").toString(); QVERIFY(account.startsWith(QStringLiteral("public:")));
        QVERIFY(manager.saveAddonSettings(QStringLiteral("org.videolan.tvmaze"), {}, {}));
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
        QVERIFY(manager.capabilities().value("privateStorageAvailable").toBool());
        QVERIFY(manager.capabilities().value("webSocketAvailable").toBool());
        lifetime.reset(); QVERIFY(!manager.capabilities().value("webSocketAvailable").toBool());
        QVERIFY(manager.capabilities().value("privateStorageAvailable").toBool());
        media.reset(); QVERIFY(!manager.capabilities().value("media").toMap().value("known").toBool());
    }

    void reviewCapturesPackageAndCancellationDoesNotInstall() {
        QTemporaryDir storage; AddonManager manager(storage.path());
        QVERIFY(manager.stageLocalPackage(reference()));
        QCOMPARE(manager.pendingPackage().value("id").toString(), QStringLiteral("org.hakone.thesubtitledb"));
        QVERIFY(manager.packages().isEmpty());
        manager.discardPendingPackage(); QVERIFY(manager.pendingPackage().isEmpty());
        QVERIFY(!manager.approvePendingPackage()); QVERIFY(manager.packages().isEmpty());
        QVERIFY(manager.stageLocalPackage(reference())); QVERIFY(manager.approvePendingPackage());
        QVERIFY(manager.pendingPackage().isEmpty()); QCOMPARE(manager.packages().size(), 1);
        QVERIFY(!manager.packages().first().toMap().value("enabled").toBool());
    }
    void builtinProvidersShareOrderingAndKeepDisabledPreference() {
        QTemporaryDir storage; AddonManager manager(storage.path());
        const QVariantMap descriptor{{"id", "vlc.builtin.example/search"}, {"packageId", "vlc.builtin.example"},
            {"name", "Built-in provider"}, {"enabled", true}, {"configured", true}, {"features", QStringList{QStringLiteral("subtitles")}}};
        manager.registerBuiltinProvider(QStringLiteral("subtitles"), descriptor);
        QCOMPARE(manager.providersForFeature(QStringLiteral("subtitles")).size(), 1);
        QVERIFY(manager.setEnabled(QStringLiteral("vlc.builtin.example"), false));
        manager.registerBuiltinProvider(QStringLiteral("subtitles"), descriptor);
        QVERIFY(!manager.providersForFeature(QStringLiteral("subtitles")).first().toMap().value("enabled").toBool());
        QVERIFY(manager.setProviderOrder(QStringLiteral("subtitles"), {QStringLiteral("vlc.builtin.example/search")}));
        QCOMPARE(manager.providerOrder(QStringLiteral("subtitles")), QStringList{QStringLiteral("vlc.builtin.example/search")});
        AddonManager restored(storage.path()); restored.registerBuiltinProvider(QStringLiteral("subtitles"), descriptor);
        QVERIFY(!restored.providersForFeature(QStringLiteral("subtitles")).first().toMap().value("enabled").toBool());
        manager.unregisterBuiltinProvider(QStringLiteral("vlc.builtin.example/search"));
        QVERIFY(manager.providersForFeature(QStringLiteral("subtitles")).isEmpty());
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
    void nativeAssetRequestsRejectAnExternalConfigurationChange() {
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
        manager.fetchImageAsset(id + QStringLiteral("/search"), request, callback);
        PackageStore store(storage.path()); QJsonObject settings; quint64 revision = 0;
        QVERIFY(store.settings(id, &settings, &revision)); settings.insert("language", "fr");
        QVERIFY(store.saveSettings(id, settings, revision)); QTRY_COMPARE(completed, 2);
    }
    void fullRemovalRevokesAllCredentialGenerations() {
        QTemporaryDir storage; AddonManager manager(storage.path());
        const QString id = QStringLiteral("org.hakone.thesubtitledb");
        QVERIFY(manager.installLocalPackage(reference(), true));
        QVERIFY(manager.saveAddonSettings(id, {{"language", "en"}}, replace(QStringLiteral("first-unit-secret"))));
        QVERIFY(manager.saveAddonSettings(id, {{"language", "en"}}, replace(QStringLiteral("second-unit-secret"))));
        PackageStore store(storage.path()); const auto references = store.credentialReferences(id);
        QCOMPARE(references.size(), 2);
        QVERIFY(manager.uninstall(id, true)); QVERIFY(manager.installLocalPackage(reference(), true));
        quint64 revision = 0;
        for (const auto& reference : references) {
            QVERIFY(store.saveSettings(id, {{"language", "en"},
                {"_credentials", QJsonObject{{"apiKey", reference}}}}, revision++));
            QVERIFY(!manager.settingsSecretStatus(id).value("apiKey").toMap().value("present").toBool());
        }
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
