// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QDirIterator>
#include <QFile>
#include "addons/addon_manager.hpp"

using namespace vlc::addons;

class TestAddonRuntime : public QObject
{
    Q_OBJECT
    QString reference() const { return QStringLiteral(VLC_ADDON_SOURCE_ROOT "/contrib-addons/examples/thesubtitledb"); }
    static QVariantMap replace(const QString& key) {
        return {{"apiKey", QVariantMap{{"operation", "replace"}, {"value", key}, {"sessionOnly", true}}}};
    }
private slots:
    void reviewCapturesPackageAndCancellationDoesNotInstall() {
        QTemporaryDir storage; AddonManager manager(storage.path());
        QVERIFY(manager.stageLocalPackage(reference()));
        QCOMPARE(manager.pendingPackage().value("id").toString(), QStringLiteral("org.videolan.thesubtitledb"));
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
        const QString id = QStringLiteral("org.videolan.thesubtitledb");
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
        QVERIFY(!manager.saveAddonSettings(id, {{"language", "en"}}, {{"apiKey", QVariantMap{{"operation", "delete"}}}}));
        QVERIFY(manager.settingsSecretStatus(id).value("apiKey").toMap().value("present").toBool());
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
};
QTEST_GUILESS_MAIN(TestAddonRuntime)
#include "test_addon_runtime.moc"
