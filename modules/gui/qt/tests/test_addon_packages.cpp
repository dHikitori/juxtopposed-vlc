// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QLockFile>
#include "addons/addon_manifest.hpp"
#include "addons/addon_package_store.hpp"
#include "addons/addon_manager.hpp"
#include "addons/addon_repository_cancellation.hpp"

using namespace vlc::addons;

class TestAddonPackages : public QObject
{
    Q_OBJECT
    static QJsonObject manifest(QString version = QStringLiteral("1.0.0"))
    {
        return {{"formatVersion", 1}, {"id", "org.example.package"}, {"name", "Package"},
            {"version", version}, {"publisher", "Tests"}, {"license", "GPL-2.0-or-later"},
            {"source", "https://git.example.org/package.git"},
            {"api", QJsonObject{{"addon", ">=1.0.0 <2.0.0"}, {"contracts", QJsonObject{{"theme", "^1.0.0"}}}}},
            {"files", QJsonArray{"addon.json", "LICENSE", "theme.json"}},
            {"contributions", QJsonArray{QJsonObject{{"id", "theme"}, {"type", "theme"},
                {"contract", "theme@1"}, {"resource", "theme.json"}}}}};
    }
    static bool write(const QString& path, const QByteArray& bytes)
    {
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
    static bool populate(const QString& directory, const QJsonObject& document)
    {
        return write(QDir(directory).filePath("addon.json"), QJsonDocument(document).toJson())
            && write(QDir(directory).filePath("LICENSE"), "test license\n")
            && write(QDir(directory).filePath("theme.json"), "{\"contract\":\"theme@1\"}\n");
    }
    static bool settingsPackage(const QString& directory, const QString& packageVersion,
                                int settingsVersion, const QString& field, const QJsonArray& operations = {})
    {
        auto document = manifest(packageVersion);
        auto api = document.value("api").toObject(), contracts = api.value("contracts").toObject();
        contracts.insert("settings", "^1.0.0"); api.insert("contracts", contracts); document.insert("api", api);
        auto files = document.value("files").toArray(); files.append("settings/schema.json");
        QJsonObject settings{{"version", settingsVersion}, {"schema", "settings/schema.json"}};
        if (!operations.isEmpty()) {
            files.append("settings/migrate.json");
            settings.insert("migrations", QJsonArray{QJsonObject{{"from", settingsVersion - 1},
                {"to", settingsVersion}, {"resource", "settings/migrate.json"}}});
            if (!write(QDir(directory).filePath("settings/migrate.json"), QJsonDocument(QJsonObject{
                {"formatVersion", 1}, {"operations", operations}}).toJson())) return false;
        }
        document.insert("files", files); document.insert("settings", settings);
        return populate(directory, document) && write(QDir(directory).filePath("settings/schema.json"),
            QJsonDocument(QJsonObject{{"type", "object"}, {"properties", QJsonObject{{field, QJsonObject{{"type", "string"}}}}},
                {"required", QJsonArray{field}}, {"additionalProperties", false}}).toJson());
    }
    static ReleaseIdentity release(QString version = QStringLiteral("1.0.0"), QChar object = QLatin1Char('a'))
    {
        ReleaseIdentity identity;
        identity.source = QStringLiteral("https://git.example.org/package.git");
        identity.tag = QLatin1Char('v') + version;
        identity.tagObjectId = QString(40, object);
        identity.commitId = QString(40, object);
        return identity;
    }
    static bool git(const QString& directory, const QStringList& arguments)
    {
        QProcess process;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("GIT_CONFIG_NOSYSTEM", "1");
#ifdef Q_OS_WIN
        environment.insert("GIT_CONFIG_GLOBAL", "NUL");
#else
        environment.insert("GIT_CONFIG_GLOBAL", "/dev/null");
#endif
        process.setProcessEnvironment(environment);
        process.setWorkingDirectory(directory);
        process.start("git", QStringList{"-c", "user.name=Addon Tests", "-c", "user.email=addons@example.invalid",
            "-c", "commit.gpgsign=false", "-c", "core.hooksPath="} + arguments);
        return process.waitForFinished(10000) && process.exitCode() == 0;
    }
private slots:
    void storeAuthorityFollowsCommittedStateAcrossOwners()
    {
        QTemporaryDir source, storage, other; QString error;
        QVERIFY(populate(source.path(), manifest())); PackageCandidate first, next;
        QVERIFY(PackageStore::inspectDirectory(source.path(), release(), &first, &error));
        PackageStore store(storage.path()), peer(storage.path());
        auto token = store.authorityToken();
        const auto unrelated = captureStoreAuthority(other.path());
        QVERIFY(token.current()); QVERIFY(!StoreAuthorityToken{}.current());
        QVERIFY(store.install(first, {}, &error)); QVERIFY(!token.current()); QVERIFY(unrelated.current());
        token = captureStoreAuthority(storage.path() + QStringLiteral("/./unused/.."));
        QVERIFY(peer.setEnabled(first.manifest.id, true)); QVERIFY(!token.current());
        token = captureStoreAuthority(storage.path());
        {
            QLockFile lock(QDir(storage.path()).filePath(QStringLiteral("store.lock"))); QVERIFY(lock.tryLock(0));
            QVERIFY(!peer.setEnabled(first.manifest.id, false)); QVERIFY(token.current());
        }
        QJsonObject values; quint64 revision = 0; QVERIFY(store.settings(first.manifest.id, &values, &revision));
        QVERIFY(!store.saveSettings(first.manifest.id, {{"language", "en"}}, revision + 1)); QVERIFY(token.current());
        QVERIFY(store.saveSettings(first.manifest.id, {{"language", "en"}}, revision)); QVERIFY(!token.current());
        QVERIFY(populate(source.path(), manifest(QStringLiteral("1.1.0"))));
        QVERIFY(PackageStore::inspectDirectory(source.path(), release(QStringLiteral("1.1.0"), QLatin1Char('b')), &next, &error));
        token = captureStoreAuthority(storage.path()); QVERIFY(store.install(next, {}, &error)); QVERIFY(!token.current());
        token = captureStoreAuthority(storage.path()); QVERIFY(peer.rollback(first.manifest.id)); QVERIFY(!token.current());
        token = captureStoreAuthority(storage.path()); QVERIFY(store.setSafeMode(true)); QVERIFY(!token.current());
        token = captureStoreAuthority(storage.path()); QVERIFY(peer.setSafeMode(false)); QVERIFY(!token.current());
        token = captureStoreAuthority(storage.path()); QVERIFY(store.setDistributionSuppressed(first.manifest.id, true)); QVERIFY(!token.current());
        token = captureStoreAuthority(storage.path()); QVERIFY(peer.setDistributionSuppressed(first.manifest.id, false)); QVERIFY(!token.current());
        const auto path = QDir(storage.path()).filePath(QStringLiteral("packages/") + first.manifest.id + QStringLiteral("/state.json"));
        QVERIFY(write(path, "broken")); token = captureStoreAuthority(storage.path());
        QVERIFY(peer.recover(first.manifest.id, &error)); QVERIFY(!token.current());
        token = captureStoreAuthority(storage.path()); QVERIFY(store.uninstall(first.manifest.id, true)); QVERIFY(!token.current());
        QVERIFY(unrelated.current());
    }
    void repositoryUpdateSelectsACompatibleTagWithoutActivation()
    {
        QTemporaryDir repository, storage; QString error;
        QVERIFY(git(repository.path(), {"init", "--template="}));
        QVERIFY(populate(repository.path(), manifest()));
        QVERIFY(git(repository.path(), {"add", "--all"})); QVERIFY(git(repository.path(), {"commit", "-m", "initial"}));
        QVERIFY(git(repository.path(), {"tag", "v1.0.0"}));
        PackageCandidate initial, candidate;
        QVERIFY(PackageStore::fetchGitTag(repository.path(), "v1.0.0", &initial, &error, true));
        PackageStore store(storage.path()); QVERIFY(store.install(initial, {}));
        for (const auto* value : {"1.9.0", "1.10.0", "2.0.0", "1.11.0-beta.1"}) {
            const auto version = QString::fromLatin1(value);
            QVERIFY(populate(repository.path(), manifest(version)));
            QVERIFY(git(repository.path(), {"add", "--all"})); QVERIFY(git(repository.path(), {"commit", "-m", version}));
            QVERIFY(git(repository.path(), {"tag", QStringLiteral("v") + version}));
        }
        QVERIFY(!store.fetchRepositoryUpdate(initial.manifest.id, QStringLiteral("^1.0.0"), false, &candidate, &error));
        QVERIFY2(store.fetchRepositoryUpdate(initial.manifest.id, QStringLiteral("^1.0.0"), false, &candidate, &error, true), qPrintable(error));
        QCOMPARE(candidate.manifest.version, QStringLiteral("1.10.0"));
        InstalledPackage active; QVERIFY(store.installed(initial.manifest.id, &active)); QCOMPARE(active.digest, initial.digest);
        QVERIFY(store.install(candidate, {}));
        QVERIFY(store.fetchRepositoryUpdate(initial.manifest.id, QStringLiteral("^1.0.0"), false, &candidate, &error, true));
        QVERIFY(candidate.manifest.id.isEmpty());
        QVERIFY(store.fetchRepositoryUpdate(initial.manifest.id, QStringLiteral("^1.0.0"), true, &candidate, &error, true));
        QCOMPARE(candidate.manifest.version, QStringLiteral("1.11.0-beta.1"));
        auto wrong = manifest(QStringLiteral("1.12.0")); wrong.insert("id", "org.example.other");
        QVERIFY(populate(repository.path(), wrong)); QVERIFY(git(repository.path(), {"add", "--all"}));
        QVERIFY(git(repository.path(), {"commit", "-m", "wrong package"})); QVERIFY(git(repository.path(), {"tag", "v1.12.0"}));
        QVERIFY(!store.fetchRepositoryUpdate(initial.manifest.id, QStringLiteral("^1.0.0"), false, &candidate, &error, true));
        QVERIFY(error.contains(QStringLiteral("different package ID")));
        QVERIFY(store.installed(initial.manifest.id, &active)); QCOMPARE(active.manifest.version, QStringLiteral("1.10.0"));
    }

    void removedDistributionPackageNeedsExplicitHostRestore()
    {
        QTemporaryDir source, storage; const QString id = QStringLiteral("org.example.package");
        QVERIFY(populate(source.path(), manifest()));
        {
            AddonManager manager(storage.path());
            QVERIFY(manager.ensureDistributionPackage(source.path(), id));
            QVERIFY(manager.uninstall(id, true));
        }
        PackageStore store(storage.path()); InstalledPackage installed;
        QVERIFY(store.distributionSuppressed(id)); QVERIFY(!store.installed(id, &installed));
        AddonManager restarted(storage.path());
        QVERIFY(restarted.ensureDistributionPackage(source.path(), id));
        QVERIFY(!store.installed(id, &installed));
        QVERIFY(restarted.packages().first().toMap().value("suppressed").toBool());
        QVERIFY(!restarted.restoreDistributionPackage(QStringLiteral("org.example.untrusted")));
        QVERIFY(restarted.restoreDistributionPackage(id));
        QVERIFY(!store.distributionSuppressed(id)); QVERIFY(store.installed(id, &installed)); QVERIFY(installed.enabled);
        QVERIFY(restarted.setEnabled(id, false));
        AddonManager afterDisable(storage.path()); QVERIFY(afterDisable.ensureDistributionPackage(source.path(), id));
        QVERIFY(store.installed(id, &installed)); QVERIFY(!installed.enabled);
        PackageCandidate gitUpdate;
        QVERIFY(PackageStore::inspectDirectory(source.path(), release(), &gitUpdate));
        QVERIFY(store.install(gitUpdate, {}));
        QVERIFY(afterDisable.ensureDistributionPackage(source.path(), id));
        QVERIFY(store.installed(id, &installed)); QVERIFY(!installed.identity.distributionSource); QVERIFY(!installed.enabled);
    }
    void bundledArtworkPackagesHaveCompleteInventories()
    {
        QTemporaryDir storage; AddonManager manager(storage.path());
        PackageStore store(storage.path());
        for (const auto& name : {QStringLiteral("musicbrainz"), QStringLiteral("lastfm"),
                QStringLiteral("french-tv"), QStringLiteral("google-images")}) {
            const auto directory = QDir(QStringLiteral(VLC_ADDON_SOURCE_ROOT)).filePath(QStringLiteral("contrib-addons/examples/") + name);
            const auto id = QStringLiteral("org.videolan.") + name;
            Manifest manifest; QMap<QString, QByteArray> contents; QString error;
            QVERIFY2(Manifest::loadDirectory(directory, &manifest, &contents, &error), qPrintable(error));
            QCOMPARE(manifest.id, id);
            QVERIFY(!contents.value(QStringLiteral("THIRD_PARTY_NOTICES.md")).isEmpty());
            QVERIFY(contents.value(QStringLiteral("dist/addon.wasm")).startsWith(QByteArray::fromHex("0061736d01000000")));
            QVERIFY(manager.ensureDistributionPackage(directory, id));
            InstalledPackage installed; QVERIFY(store.installed(id, &installed));
            QVERIFY(installed.enabled && installed.identity.distributionSource);
            QCOMPARE(installed.digest, packageDigest(contents));
            QVERIFY(manager.setEnabled(id, false));
            QVERIFY(manager.ensureDistributionPackage(directory, id));
            QVERIFY(store.installed(id, &installed)); QVERIFY(!installed.enabled);
        }
    }
    void settingsMigrationCommitsWithMatchingRollbackSnapshot()
    {
        QTemporaryDir source, storage; QString error;
        QVERIFY(settingsPackage(source.path(), QStringLiteral("1.0.0"), 1, QStringLiteral("language")));
        PackageCandidate first, next; PackageStore store(storage.path());
        QVERIFY(PackageStore::inspectDirectory(source.path(), release(), &first, &error));
        QVERIFY(store.install(first, {}, &error));
        QVERIFY(store.saveSettings(first.manifest.id, QJsonObject{{"language", "en"}}, 0, &error));
        QVERIFY(settingsPackage(source.path(), QStringLiteral("1.1.0"), 2, QStringLiteral("locale"),
            QJsonArray{QJsonObject{{"op", "rename"}, {"from", "language"}, {"to", "locale"}}}));
        QVERIFY2(PackageStore::inspectDirectory(source.path(), release(QStringLiteral("1.1.0")), &next, &error), qPrintable(error));
        QVERIFY2(store.install(next, {}, &error), qPrintable(error));
        QJsonObject settings; quint64 revision;
        QVERIFY(store.settings(first.manifest.id, &settings, &revision)); QCOMPARE(settings, QJsonObject({{"locale", "en"}}));
        QCOMPARE(revision, quint64(2));
        QVERIFY(store.saveSettings(first.manifest.id, QJsonObject{{"locale", "fr"}}, revision, &error));
        QVERIFY(store.rollback(first.manifest.id, &error));
        QVERIFY(store.settings(first.manifest.id, &settings, &revision)); QCOMPARE(settings, QJsonObject({{"language", "en"}}));
        QCOMPARE(revision, quint64(1));
        QVERIFY(store.rollback(first.manifest.id, &error));
        QVERIFY(store.settings(first.manifest.id, &settings)); QCOMPARE(settings, QJsonObject({{"locale", "fr"}}));
    }

    void failedMigrationAndCommitKeepTheActiveGeneration()
    {
        QTemporaryDir source, storage; QString error;
        QVERIFY(settingsPackage(source.path(), QStringLiteral("1.0.0"), 1, QStringLiteral("language")));
        PackageCandidate first, next; PackageStore store(storage.path());
        QVERIFY(PackageStore::inspectDirectory(source.path(), release(), &first)); QVERIFY(store.install(first, {}));
        QVERIFY(store.saveSettings(first.manifest.id, QJsonObject{{"language", "en"}}, 0));
        QVERIFY(settingsPackage(source.path(), QStringLiteral("1.1.0"), 2, QStringLiteral("locale"),
            QJsonArray{QJsonObject{{"op", "remove"}, {"field", "language"}},
                QJsonObject{{"op", "default"}, {"field", "locale"}, {"value", true}}}));
        QVERIFY(PackageStore::inspectDirectory(source.path(), release(QStringLiteral("1.1.0")), &next));
        QVERIFY(!store.install(next, {}, &error));
        InstalledPackage active; QVERIFY(store.installed(first.manifest.id, &active)); QCOMPARE(active.digest, first.digest);
        QJsonObject settings; QVERIFY(store.settings(first.manifest.id, &settings)); QCOMPARE(settings, QJsonObject({{"language", "en"}}));
        QVERIFY(settingsPackage(source.path(), QStringLiteral("1.1.0"), 2, QStringLiteral("locale"),
            QJsonArray{QJsonObject{{"op", "remove"}, {"field", "language"}}}));
        QVERIFY(PackageStore::inspectDirectory(source.path(), release(QStringLiteral("1.1.0")), &next));
        QVERIFY(!store.install(next, {}, &error)); QVERIFY(error.contains(QStringLiteral("required settings field")));
        QVERIFY(settingsPackage(source.path(), QStringLiteral("1.2.0"), 3, QStringLiteral("locale"),
            QJsonArray{QJsonObject{{"op", "rename"}, {"from", "language"}, {"to", "locale"}}}));
        QVERIFY(PackageStore::inspectDirectory(source.path(), release(QStringLiteral("1.2.0")), &next));
        QVERIFY(!store.install(next, {}, &error)); QVERIFY(error.contains(QStringLiteral("step is missing")));
        QVERIFY(settingsPackage(source.path(), QStringLiteral("1.1.0"), 2, QStringLiteral("locale"),
            QJsonArray{QJsonObject{{"op", "rename"}, {"from", "language"}, {"to", "locale"}}}));
        QVERIFY(PackageStore::inspectDirectory(source.path(), release(QStringLiteral("1.1.0")), &next));
        const auto backup = QDir(storage.path()).filePath(QStringLiteral("packages/") + first.manifest.id + QStringLiteral("/state.json.backup"));
        QVERIFY(QFile::remove(backup)); QVERIFY(QDir().mkdir(backup));
        QVERIFY(!store.install(next, {}, &error));
        QVERIFY(store.installed(first.manifest.id, &active)); QCOMPARE(active.digest, first.digest);
        QVERIFY(QDir(backup).removeRecursively());
        QVERIFY2(store.install(next, {}, &error), qPrintable(error));
    }

    void safeModeAndStateBackupNeedExplicitRecovery()
    {
        QTemporaryDir source, storage; QString error;
        QVERIFY(populate(source.path(), manifest())); PackageCandidate candidate;
        QVERIFY(PackageStore::inspectDirectory(source.path(), release(), &candidate));
        PackageStore store(storage.path()); QVERIFY(store.install(candidate, {}));
        QVERIFY(store.setEnabled(candidate.manifest.id, true));
        QVERIFY(store.setSafeMode(true)); InstalledPackage active;
        QVERIFY(store.installed(candidate.manifest.id, &active)); QVERIFY(!active.enabled);
        QVERIFY(store.setSafeMode(false)); QVERIFY(store.installed(candidate.manifest.id, &active)); QVERIFY(active.enabled);
        const auto state = QDir(storage.path()).filePath(QStringLiteral("packages/") + candidate.manifest.id + QStringLiteral("/state.json"));
        QVERIFY(write(state, "interrupted-state-fixture"));
        QVERIFY(store.installed(candidate.manifest.id, &active)); QVERIFY(active.recoveryRequired); QVERIFY(!active.enabled);
        QVERIFY(!store.install(candidate, {}, &error));
        QVERIFY2(store.recover(candidate.manifest.id, &error), qPrintable(error));
        QVERIFY(store.installed(candidate.manifest.id, &active)); QVERIFY(!active.recoveryRequired); QVERIFY(!active.enabled);
        QVERIFY(store.setEnabled(candidate.manifest.id, true)); QVERIFY(store.installed(candidate.manifest.id, &active)); QVERIFY(active.enabled);
        QVERIFY(QFile::remove(state)); QVERIFY(store.installedIds().contains(candidate.manifest.id));
        QVERIFY(!store.install(candidate, {}, &error)); QVERIFY(store.recover(candidate.manifest.id, &error));
    }

    void retainedGenerationsHaveABoundedCoherentHistory()
    {
        QTemporaryDir source, storage; PackageStore store(storage.path()); const QString id = QStringLiteral("org.example.package");
        for (int index = 0; index < 6; ++index) {
            const QString version = QStringLiteral("1.0.") + QString::number(index);
            QVERIFY(populate(source.path(), manifest(version))); PackageCandidate candidate;
            QVERIFY(PackageStore::inspectDirectory(source.path(), release(version, QChar(QLatin1Char('a').unicode() + index)), &candidate));
            QVERIFY(store.install(candidate, {})); quint64 revision;
            QVERIFY(store.settings(id, nullptr, &revision)); QVERIFY(store.saveSettings(id, QJsonObject{{"value", index}}, revision));
        }
        QCOMPARE(store.retainedGenerations(id).size(), 4);
        QVERIFY(store.rollback(id)); QJsonObject settings;
        QVERIFY(store.settings(id, &settings)); QCOMPARE(settings, QJsonObject({{"value", 4}}));
        QVERIFY(store.rollback(id)); QVERIFY(store.settings(id, &settings)); QCOMPARE(settings, QJsonObject({{"value", 5}}));
    }

    void recoveryRejectsCorruptReleaseAndRetainsNewerSettings()
    {
        QTemporaryDir source, storage; PackageStore store(storage.path()); QString error;
        PackageCandidate first, next;
        QVERIFY(populate(source.path(), manifest())); QVERIFY(PackageStore::inspectDirectory(source.path(), release(), &first));
        QVERIFY(store.install(first, {})); QVERIFY(store.saveSettings(first.manifest.id, QJsonObject{{"value", 1}}, 0));
        QVERIFY(populate(source.path(), manifest(QStringLiteral("1.1.0"))));
        QVERIFY(PackageStore::inspectDirectory(source.path(), release(QStringLiteral("1.1.0")), &next));
        QVERIFY(store.install(next, {})); QVERIFY(store.saveSettings(first.manifest.id, QJsonObject{{"value", 2}}, 1));
        InstalledPackage active; QVERIFY(store.installed(first.manifest.id, &active));
        const auto resource = QDir(active.directory).filePath(QStringLiteral("theme.json"));
        QVERIFY(QFile::setPermissions(resource, QFile::ReadOwner | QFile::WriteOwner)); QVERIFY(write(resource, "corrupt-release-fixture"));
        QVERIFY(!store.installed(first.manifest.id, &active)); QVERIFY2(store.recover(first.manifest.id, &error), qPrintable(error));
        QVERIFY(store.installed(first.manifest.id, &active)); QCOMPARE(active.digest, first.digest); QVERIFY(!active.enabled);
        QJsonObject settings; QVERIFY(store.settings(first.manifest.id, &settings)); QCOMPARE(settings, QJsonObject({{"value", 1}}));
        QVERIFY(store.retainedGenerations(first.manifest.id).size() >= 2);
    }

    void visualizerRejectsRequiredAndOptionalBackgroundSessions()
    {
        auto document = manifest();
        auto api = document.value("api").toObject();
        api.insert("contracts", QJsonObject{{"visualizer", "^1.0.0"}});
        document.insert("api", api);
        document.insert("contributions", QJsonArray{QJsonObject{{"id", "visualizer"},
            {"type", "visualizer"}, {"contract", "visualizer@1"}, {"scene", "theme.json"}}});
        Manifest parsed; QString error;
        QVERIFY2(Manifest::parse(QJsonDocument(document).toJson(), &parsed, &error), qPrintable(error));
        for (const auto* category : {"required", "optional"}) {
            document.insert("permissions", QJsonObject{{QLatin1String(category),
                QJsonObject{{"session.background", QJsonObject{}}}}});
            QVERIFY(!Manifest::parse(QJsonDocument(document).toJson(), &parsed, &error));
            QVERIFY(error.contains(QStringLiteral("background session")));
        }
    }

    void distributionBootstrapPreservesUserStateAndGitUpdates()
    {
        QTemporaryDir source, storage; QVERIFY(source.isValid()); QVERIFY(storage.isValid());
        QVERIFY(populate(source.path(), manifest()));
        AddonManager manager(storage.path());
        QVERIFY(manager.ensureDistributionPackage(source.path(), QStringLiteral("org.example.package")));
        PackageStore store(storage.path()); InstalledPackage installed;
        QVERIFY(store.installed(QStringLiteral("org.example.package"), &installed));
        QVERIFY(installed.enabled); QVERIFY(installed.identity.distributionSource); QVERIFY(!installed.identity.developerSource);
        QCOMPARE(installed.identity.objectFormat, QStringLiteral("distribution-sha256"));
        QVERIFY(manager.packages().first().toMap().value(QStringLiteral("commit")).toString().isEmpty());
        QCOMPARE(manager.packages().first().toMap().value(QStringLiteral("publisherVerified")).toBool(), false);
        QVERIFY(manager.setEnabled(QStringLiteral("org.example.package"), false));
        QVERIFY(store.saveSettings(QStringLiteral("org.example.package"), {{QStringLiteral("saved"), 7}}, 0));
        QVERIFY(manager.ensureDistributionPackage(source.path(), QStringLiteral("org.example.package")));
        QVERIFY(store.installed(QStringLiteral("org.example.package"), &installed)); QVERIFY(!installed.enabled);
        const auto digest = installed.digest;
        PackageCandidate gitRelease;
        QVERIFY(PackageStore::inspectDirectory(source.path(), release(), &gitRelease));
        QVERIFY(store.install(gitRelease, {}));
        QVERIFY(store.installed(QStringLiteral("org.example.package"), &installed));
        QVERIFY(!installed.identity.distributionSource); QCOMPARE(installed.identity.commitId, release().commitId);
        QCOMPARE(installed.digest, digest); QVERIFY(!installed.enabled);
        QJsonObject settings; QVERIFY(store.settings(QStringLiteral("org.example.package"), &settings));
        QCOMPARE(settings.value(QStringLiteral("saved")).toInt(), 7);
        QVERIFY(populate(source.path(), manifest(QStringLiteral("1.1.0"))));
        QVERIFY(manager.ensureDistributionPackage(source.path(), QStringLiteral("org.example.package")));
        QVERIFY(store.installed(QStringLiteral("org.example.package"), &installed));
        QCOMPARE(installed.manifest.version, QStringLiteral("1.0.0")); QVERIFY(!installed.identity.distributionSource);
        QVERIFY(!manager.ensureDistributionPackage(source.path(), QStringLiteral("org.example.wrong")));
    }

    void semanticVersionOrdering()
    {
        SemanticVersion a, b;
        QVERIFY(SemanticVersion::parse("1.2.3-alpha.9", &a));
        QVERIFY(SemanticVersion::parse("1.2.3-alpha.10", &b));
        QVERIFY(a.compare(b) < 0);
        QVERIFY(SemanticVersion::parse("1.2.3", &a));
        QVERIFY(a.compare(b) > 0);
        QVERIFY(!SemanticVersion::parse("01.2.3", &a));
        QVERIFY(!SemanticVersion::parse("1.2.3-01", &a));
        QVERIFY(versionSatisfies("1.5.9", ">=1.0.0 <2.0.0"));
        QVERIFY(!versionSatisfies("2.0.0", "^1.0.0"));
        QVERIFY(versionSatisfies("0.2.4", "^0.2.0"));
        QVERIFY(!versionSatisfies("0.3.0", "^0.2.0"));
        QVERIFY(!versionSatisfies("1.1.0-beta", "^1.0.0"));
        QVERIFY(versionSatisfies("1.1.0-beta.2", ">=1.1.0-beta.1 <2.0.0"));
    }
    void portablePaths()
    {
        for (const auto& path : {"../a", "/a", "a\\b", "C:a", "a/../../b", "a//b", "a/CON.txt",
                                "a/COM1", "a/.git/config", "a/b.", "a/b ", "a/*"})
            QVERIFY2(!portablePackagePath(QString::fromUtf8(path)), path);
        QVERIFY(portablePackagePath("settings/schema.json"));
        QCOMPARE(exactHttpsOrigin("https://API.example.org:443/"), QStringLiteral("https://api.example.org"));
        QVERIFY(exactHttpsOrigin("http://example.org").isEmpty());
        QVERIFY(exactHttpsOrigin("https://user:secret@example.org").isEmpty());
        QVERIFY(exactHttpsOrigin("https://example.org/path").isEmpty());
        QVERIFY(exactHttpsOrigin("https://example.org?query").isEmpty());
    }
    void strictManifest()
    {
        Manifest parsed;
        QString error;
        const auto base = manifest();
        QVERIFY2(Manifest::parse(QJsonDocument(base).toJson(), &parsed, &error), qPrintable(error));
        QCOMPARE(parsed.id, QStringLiteral("org.example.package"));
        QCOMPARE(parsed.contributions.size(), 1);
        auto bad = base;
        bad.insert("postInstall", "arbitrary command");
        QVERIFY(!Manifest::parse(QJsonDocument(bad).toJson(), &parsed, &error));
        bad = base;
        bad.insert("files", QJsonArray{"addon.json", "LICENSE", "Theme.json", "theme.json"});
        QVERIFY(!Manifest::parse(QJsonDocument(bad).toJson(), &parsed, &error));
        bad = base;
        bad.insert("permissions", QJsonObject{{"required", QJsonObject{{"shell.execute", QJsonObject{}}}}});
        QVERIFY(!Manifest::parse(QJsonDocument(bad).toJson(), &parsed, &error));
        bad = base;
        bad.insert("permissions", QJsonObject{{"required", QJsonObject{{"network.http", QJsonObject{{"origins", QJsonArray{"http://example.org"}}}}}}});
        QVERIFY(!Manifest::parse(QJsonDocument(bad).toJson(), &parsed, &error));
        bad = base;
        bad.insert("api", QJsonObject{{"addon", "^2.0.0"}, {"contracts", QJsonObject{{"theme", "^1.0.0"}}}});
        QVERIFY(!Manifest::parse(QJsonDocument(bad).toJson(), &parsed, &error));
    }
    void inventoryAndContentDigest()
    {
        QTemporaryDir directory;
        QVERIFY(populate(directory.path(), manifest()));
        Manifest parsed;
        QMap<QString, QByteArray> files;
        QString error;
        QVERIFY2(Manifest::loadDirectory(directory.path(), &parsed, &files, &error), qPrintable(error));
        QCOMPARE(files.size(), 3);
        const auto digest = packageDigest(files);
        auto changed = files;
        changed["theme.json"] += ' ';
        QVERIFY(digest != packageDigest(changed));
        QVERIFY(QFile::remove(directory.filePath("theme.json")));
        QVERIFY(!Manifest::loadDirectory(directory.path(), &parsed, &files, &error));
    }
    void refusesResourceSymlink()
    {
#ifdef Q_OS_WIN
        QSKIP("Symbolic-link creation needs platform privileges");
#else
        QTemporaryDir directory, outside;
        QVERIFY(populate(directory.path(), manifest()));
        QVERIFY(write(outside.filePath("other.json"), "{}"));
        QVERIFY(QFile::remove(directory.filePath("theme.json")));
        QVERIFY(QFile::link(outside.filePath("other.json"), directory.filePath("theme.json")));
        Manifest parsed;
        QVERIFY(!Manifest::loadDirectory(directory.path(), &parsed));
#endif
    }
    void installUpdateRollbackAndRevision()
    {
        QTemporaryDir package, data;
        QVERIFY(populate(package.path(), manifest()));
        PackageCandidate first;
        QString error;
        QVERIFY2(PackageStore::inspectDirectory(package.path(), release(), &first, &error), qPrintable(error));
        PackageStore store(data.path());
        QVERIFY2(store.install(first, {}, &error), qPrintable(error));
        InstalledPackage installed;
        QVERIFY(store.installed(first.manifest.id, &installed, &error));
        QVERIFY(!installed.enabled);
        QVERIFY(store.setEnabled(first.manifest.id, true, &error));
        QVERIFY(store.saveSettings(first.manifest.id, QJsonObject{{"value", 1}}, 0, &error));
        QVERIFY(!store.saveSettings(first.manifest.id, QJsonObject{{"value", 2}}, 0, &error));
        QVERIFY(populate(package.path(), manifest("1.1.0")));
        PackageCandidate second;
        QVERIFY(PackageStore::inspectDirectory(package.path(), release("1.1.0", QLatin1Char('b')), &second, &error));
        QVERIFY2(store.install(second, {}, &error), qPrintable(error));
        QVERIFY(store.installed(first.manifest.id, &installed, &error));
        QVERIFY(installed.enabled);
        QCOMPARE(installed.manifest.version, QStringLiteral("1.1.0"));
        QVERIFY(store.saveSettings(first.manifest.id, QJsonObject{{"value", 2}}, 1, &error));
        QVERIFY2(store.rollback(first.manifest.id, &error), qPrintable(error));
        QVERIFY(store.installed(first.manifest.id, &installed, &error));
        QCOMPARE(installed.manifest.version, QStringLiteral("1.0.0"));
        QJsonObject settings;
        quint64 revision = 0;
        QVERIFY(store.settings(first.manifest.id, &settings, &revision, &error));
        QCOMPARE(settings.value("value"), QJsonValue(1));
        QCOMPARE(revision, quint64(1));
        QCOMPARE(store.installedIds(), QStringList{first.manifest.id});
        QVERIFY(store.uninstall(first.manifest.id, false, &error));
        QVERIFY(store.installedIds().isEmpty());
        QVERIFY(QFileInfo::exists(data.filePath("retained-settings/org.example.package.json")));
    }
    void rejectsMovedTagChangedSourceAndTampering()
    {
        QTemporaryDir package, data;
        QVERIFY(populate(package.path(), manifest()));
        PackageCandidate first;
        QString error;
        QVERIFY(PackageStore::inspectDirectory(package.path(), release(), &first, &error));
        PackageStore store(data.path());
        QVERIFY(store.install(first, {}, &error));
        auto moved = first;
        moved.identity.commitId = QString(40, QLatin1Char('c'));
        QVERIFY(!store.install(moved, {}, &error));
        auto changedSource = first;
        changedSource.identity.source = "https://git.example.org/other.git";
        QVERIFY(!store.install(changedSource, {}, &error));
        auto tampered = first;
        tampered.contents["theme.json"] = "tampered";
        QVERIFY(!store.install(tampered, {}, &error));
        InstalledPackage installed;
        QVERIFY(store.installed(first.manifest.id, &installed, &error));
        QFile resource(QDir(installed.directory).filePath("theme.json"));
        QVERIFY(resource.setPermissions(QFile::ReadOwner | QFile::WriteOwner));
        QVERIFY(resource.open(QIODevice::WriteOnly));
        resource.write("tampered");
        resource.close();
        QVERIFY(!store.installed(first.manifest.id, &installed, &error));
        QVERIFY(!store.setEnabled(first.manifest.id, true, &error));
    }
    void developerSnapshotIsNotAGitIdentity()
    {
        QTemporaryDir package;
        QVERIFY(populate(package.path(), manifest()));
        auto identity = release();
        identity.commitId.clear();
        identity.tagObjectId.clear();
        PackageCandidate candidate;
        QString error;
        QVERIFY(!PackageStore::inspectDirectory(package.path(), identity, &candidate, &error));
        identity.developerSource = true;
        QVERIFY2(PackageStore::inspectDirectory(package.path(), identity, &candidate, &error), qPrintable(error));
        QCOMPARE(candidate.identity.objectFormat, QStringLiteral("developer-sha256"));
        QCOMPARE(candidate.identity.commitId, QString::fromLatin1(candidate.digest));
        QCOMPARE(candidate.identity.tagObjectId, QString::fromLatin1(candidate.digest));
    }
    void permissionConsentRequired()
    {
        QTemporaryDir package, data;
        auto object = manifest();
        object.insert("permissions", QJsonObject{{"required", QJsonObject{{"network.http", QJsonObject{{"origins", QJsonArray{"https://api.example.org"}}}}}}});
        QVERIFY(populate(package.path(), object));
        PackageCandidate candidate;
        QString error;
        QVERIFY(PackageStore::inspectDirectory(package.path(), release(), &candidate, &error));
        PackageStore store(data.path());
        QVERIFY(!store.install(candidate, {}, &error));
        QVERIFY(store.install(candidate, candidate.manifest.requiredPermissions, &error));
        auto grants = candidate.manifest.requiredPermissions;
        grants.insert("queue.modify", QJsonObject{});
        QVERIFY(!store.install(candidate, grants, &error));
    }
    void explicitWssCredentialBindings_data()
    {
        QTest::addColumn<QString>("bindingOrigin"); QTest::addColumn<QString>("capability");
        QTest::addColumn<QString>("networkOrigin"); QTest::addColumn<QString>("header"); QTest::addColumn<bool>("valid");
        const auto wss=QStringLiteral("wss://relay.example"),http=QStringLiteral("https://relay.example");
        QTest::newRow("explicit-wss")<<wss<<QStringLiteral("network.websocket")<<wss<<QStringLiteral("Authorization")<<true;
        QTest::newRow("https-unchanged")<<http<<QStringLiteral("network.http")<<http<<QStringLiteral("Authorization")<<true;
        QTest::newRow("no-https-to-wss")<<http<<QStringLiteral("network.websocket")<<wss<<QStringLiteral("Authorization")<<false;
        QTest::newRow("no-wss-to-https")<<wss<<QStringLiteral("network.http")<<http<<QStringLiteral("Authorization")<<false;
        QTest::newRow("other-host")<<QStringLiteral("wss://other.example")<<QStringLiteral("network.websocket")<<wss<<QStringLiteral("Authorization")<<false;
        for(const auto& value:QStringList{QStringLiteral("wss://relay.example/"),QStringLiteral("wss://relay.example:443"),
                QStringLiteral("wss://relay.example:8443"),QStringLiteral("wss://@relay.example"),QStringLiteral("wss://relay.example?room=x"),QStringLiteral("wss://RELAY.EXAMPLE")})
            QTest::newRow(qPrintable(value))<<value<<QStringLiteral("network.websocket")<<wss<<QStringLiteral("Authorization")<<false;
        for(const auto& value:QStringList{QStringLiteral("Upgrade"),QStringLiteral("Cookie"),QStringLiteral("Origin"),QStringLiteral("Sec-WebSocket-Key"),QStringLiteral("Sec-WebSocket-Protocol")})
            QTest::newRow(qPrintable(value))<<wss<<QStringLiteral("network.websocket")<<wss<<value<<false;
    }
    void explicitWssCredentialBindings()
    {
        QFETCH(QString,bindingOrigin); QFETCH(QString,capability); QFETCH(QString,networkOrigin); QFETCH(QString,header); QFETCH(bool,valid);
        QTemporaryDir directory; QVERIFY(settingsPackage(directory.path(),QStringLiteral("1.0.0"),1,QStringLiteral("apiKey")));
        QFile file(directory.filePath(QStringLiteral("addon.json"))); QVERIFY(file.open(QIODevice::ReadOnly));
        auto document=QJsonDocument::fromJson(file.readAll()).object(); file.close();
        const QJsonObject binding{{"field","apiKey"},{"origin",bindingOrigin},{"header",header},{"prefix","Bearer "}};
        document.insert("permissions",QJsonObject{{"required",QJsonObject{{capability,QJsonObject{{"origins",QJsonArray{networkOrigin}}}},
            {"credentials.use",QJsonObject{{"bindings",QJsonArray{binding}}}}}}});
        QVERIFY(write(directory.filePath(QStringLiteral("addon.json")),QJsonDocument(document).toJson()));
        QVERIFY(write(directory.filePath(QStringLiteral("settings/schema.json")),R"({"type":"object","properties":{"apiKey":{"type":"string","x-hakone-secret":true}}})"));
        Manifest parsed; QString error; const bool result=Manifest::loadDirectory(directory.path(),&parsed,nullptr,&error);
        QCOMPARE(result,valid); if(valid) {
            QVERIFY2(error.isEmpty(),qPrintable(error));
            if(bindingOrigin.startsWith(QStringLiteral("wss:"))) {
                QCOMPARE(exactWssOrigin(bindingOrigin),bindingOrigin); QVERIFY(exactHttpsOrigin(bindingOrigin).isEmpty());
            }
        } else QVERIFY(!error.isEmpty());
    }
    void protectedCredentialReferencesAreNotSecretValues()
    {
        QTemporaryDir package, data;
        auto object = manifest();
        auto api = object.value("api").toObject();
        auto contracts = api.value("contracts").toObject();
        contracts.insert("settings", "^1.0.0");
        api.insert("contracts", contracts);
        object.insert("api", api);
        auto files = object.value("files").toArray();
        files.append("settings/schema.json");
        object.insert("files", files);
        object.insert("settings", QJsonObject{{"version", 1}, {"schema", "settings/schema.json"}});
        object.insert("permissions", QJsonObject{{"required", QJsonObject{
            {"network.http", QJsonObject{{"origins", QJsonArray{"https://api.example.org"}}, {"methods", QJsonArray{"GET"}}}},
            {"credentials.use", QJsonObject{{"bindings", QJsonArray{QJsonObject{{"field", "apiKey"},
                {"origin", "https://api.example.org"}, {"header", "Authorization"}, {"prefix", "Bearer "}}}}}}
        }}});
        QVERIFY(populate(package.path(), object));
        QVERIFY(write(package.filePath("settings/schema.json"), R"({"type":"object","properties":{"apiKey":{"type":"string","x-hakone-secret":true}}})"));
        PackageCandidate candidate;
        QString error;
        QVERIFY2(PackageStore::inspectDirectory(package.path(), release(), &candidate, &error), qPrintable(error));
        PackageStore store(data.path());
        QVERIFY(store.install(candidate, candidate.manifest.requiredPermissions, &error));
        QVERIFY(!store.saveSettings(candidate.manifest.id, QJsonObject{{"apiKey", "unit-secret"}}, 0, &error));
        QVERIFY(!store.saveSettings(candidate.manifest.id, QJsonObject{{"nested", QJsonObject{{"apiKey", "unit-secret"}}}}, 0, &error));
        QVERIFY(!store.saveSettings(candidate.manifest.id, QJsonObject{{"_credentials", QJsonObject{{"apiKey", "unit-secret"}}}}, 0, &error));
        QVERIFY(!store.saveSettings(candidate.manifest.id, QJsonObject{{"_credentials", QJsonObject{{"otherField", "12345678-1234-1234-1234-123456789abc"}}}}, 0, &error));
        const QJsonObject valid{{"_credentials", QJsonObject{{"apiKey", "12345678-1234-1234-1234-123456789abc"}}}};
        QVERIFY2(store.saveSettings(candidate.manifest.id, valid, 0, &error), qPrintable(error));
        QJsonObject persisted;
        QVERIFY(store.settings(candidate.manifest.id, &persisted));
        QCOMPARE(persisted, valid);
        object.insert("version", "1.1.0");
        auto permissions = object.value("permissions").toObject();
        auto required = permissions.value("required").toObject();
        auto credentials = required.value("credentials.use").toObject();
        auto binding = credentials.value("bindings").toArray().first().toObject();
        binding.insert("header", "X-Api-Key"); credentials.insert("bindings", QJsonArray{binding});
        required.insert("credentials.use", credentials); permissions.insert("required", required); object.insert("permissions", permissions);
        QVERIFY(populate(package.path(), object)); PackageCandidate update;
        QVERIFY(PackageStore::inspectDirectory(package.path(), release(QStringLiteral("1.1.0")), &update, &error));
        QVERIFY(!store.install(update, update.manifest.requiredPermissions, &error));
        QVERIFY(error.contains(QStringLiteral("credential binding")));
        QVERIFY(store.settings(candidate.manifest.id, &persisted)); QCOMPARE(persisted, valid);
    }
    void obsoleteSettingsAnnotationsAreRefused_data()
    {
        QTest::addColumn<QJsonObject>("schema");
        const QJsonObject oldSecret{{"type", "string"}, {"x-vlc-secret", true}};
        const auto root = [](const QJsonObject& field) {
            return QJsonObject{{"type", "object"}, {"properties", QJsonObject{{"value", field}}}};
        };
        QTest::newRow("secret") << root(oldSecret);
        QTest::newRow("nested-properties") << root(root(oldSecret));
        QTest::newRow("array-items") << root(QJsonObject{{"type", "array"}, {"items", root(oldSecret)}});
        QTest::newRow("old-label") << root(QJsonObject{{"type", "string"}, {"x-vlc-label", "settings.value"}});
        QJsonObject deep{{"type", "string"}};
        for (unsigned i = 0; i < 40; ++i) deep = root(deep);
        QTest::newRow("depth-limit") << deep;
        QJsonArray many;
        for (int i = 0; i < 8192; ++i) many.append(i);
        QTest::newRow("node-limit") << root(QJsonObject{{"type", "integer"}, {"enum", many}});
    }
    void obsoleteSettingsAnnotationsAreRefused()
    {
        QFETCH(QJsonObject, schema);
        QTemporaryDir package, storage;
        QVERIFY(settingsPackage(package.path(), QStringLiteral("1.0.0"), 1, QStringLiteral("value")));
        QVERIFY(write(package.filePath("settings/schema.json"), QJsonDocument(schema).toJson()));
        Manifest parsed; QString error;
        QVERIFY(!Manifest::loadDirectory(package.path(), &parsed, nullptr, &error));
        QVERIFY(error.contains(QStringLiteral("annotation")));
        PackageCandidate candidate;
        QVERIFY(!PackageStore::inspectDirectory(package.path(), release(), &candidate, &error));
        PackageStore store(storage.path());
        QVERIFY(!store.saveSettings(QStringLiteral("org.example.package"), QJsonObject{{"value", "ordinary value"}}, 0, &error));
        QVERIFY(store.installedIds().isEmpty());
    }
    void oldInstalledSecretSchemaCannotExposeOrSaveOrdinarySettings()
    {
        QTemporaryDir source, storage;
        QVERIFY(settingsPackage(source.path(), QStringLiteral("1.0.0"), 1, QStringLiteral("apiKey")));
        PackageCandidate candidate; QString error;
        QVERIFY(PackageStore::inspectDirectory(source.path(), release(), &candidate, &error));
        PackageStore store(storage.path());
        QVERIFY(store.install(candidate, {}, &error));
        QVERIFY(store.saveSettings(candidate.manifest.id, QJsonObject{{"apiKey", "old value"}}, 0, &error));
        InstalledPackage installed;
        QVERIFY(store.installed(candidate.manifest.id, &installed, &error));

        // Create a complete prior-format snapshot with its own correct digest.
        auto files = candidate.contents;
        files[QStringLiteral("settings/schema.json")] = R"({"type":"object","properties":{"apiKey":{"type":"string","x-vlc-secret":true}}})";
        const QString digest = QString::fromLatin1(packageDigest(files));
        const QString directory = QDir(QFileInfo(installed.directory).absolutePath()).filePath(digest);
        for (auto it = files.begin(); it != files.end(); ++it)
            QVERIFY(write(QDir(directory).filePath(it.key()), it.value()));
        const QString statePath = storage.filePath(QStringLiteral("packages/") + candidate.manifest.id + QStringLiteral("/state.json"));
        QFile stateFile(statePath); QVERIFY(stateFile.open(QIODevice::ReadOnly));
        auto state = QJsonDocument::fromJson(stateFile.readAll()).object(); stateFile.close();
        auto current = state.value("current").toObject(); current.insert("digest", digest); state.insert("current", current);
        auto versions = state.value("lockedVersions").toObject(); versions.insert(candidate.manifest.version, digest); state.insert("lockedVersions", versions);
        auto tags = state.value("lockedTags").toObject();
        auto tag = tags.value(candidate.identity.tag).toObject(); tag.insert("digest", digest); tags.insert(candidate.identity.tag, tag); state.insert("lockedTags", tags);
        const QByteArray snapshot = QJsonDocument(state).toJson(); QVERIFY(write(statePath, snapshot));

        Manifest refused;
        QVERIFY(!Manifest::loadDirectory(directory, &refused, nullptr, &error));
        QVERIFY(error.contains(QStringLiteral("x-hakone")));
        QJsonObject values; quint64 revision = 0;
        QVERIFY(!store.settings(candidate.manifest.id, &values, &revision, &error));
        QVERIFY(values.isEmpty());
        QVERIFY(!store.saveSettings(candidate.manifest.id, QJsonObject{{"apiKey", "new value"}}, 1, &error));
        QVERIFY(stateFile.open(QIODevice::ReadOnly)); QCOMPARE(stateFile.readAll(), snapshot);
    }
    void referenceSubtitleAddonManifest()
    {
        const auto file = QFINDTESTDATA("../../../../contrib-addons/examples/thesubtitledb/addon.json");
        QVERIFY(!file.isEmpty());
        Manifest parsed;
        QString error;
        QVERIFY2(Manifest::loadDirectory(QFileInfo(file).absolutePath(), &parsed, nullptr, &error), qPrintable(error));
        QCOMPARE(parsed.id, QStringLiteral("org.hakone.thesubtitledb"));
        QVERIFY(parsed.contribution("search"));
        QVERIFY(parsed.permissionDeclared("credentials.use"));
        QCOMPARE(parsed.permissionScope("network.http").value("methods").toArray(), QJsonArray{"GET"});
    }
    void canceledRepositoryScopeRefusesTheGitProcess()
    {
        const auto flag=std::make_shared<std::atomic_bool>(true);
        RepositoryCancellation cancellation(flag);
        PackageCandidate candidate; QString error;
        QVERIFY(!PackageStore::fetchGitTag(QStringLiteral("https://example.org/never-read.git"),
            QStringLiteral("v1.0.0"),&candidate,&error));
        QCOMPARE(error,QStringLiteral("The Git request was canceled."));
        QVERIFY(candidate.manifest.id.isEmpty());
    }
    void exactGitTagAndNoCheckoutExecution()
    {
        QTemporaryDir repository, storeDirectory;
        QVERIFY(git(repository.path(), {"init", "--template="}));
        QVERIFY(populate(repository.path(), manifest()));
        QVERIFY(write(repository.filePath(".gitattributes"), "* filter=malicious\n"));
        QVERIFY(git(repository.path(), {"add", "--all"}));
        QVERIFY(git(repository.path(), {"commit", "-m", "release"}));
        QVERIFY(git(repository.path(), {"tag", "-a", "v1.0.0", "-m", "version"}));
        PackageCandidate candidate;
        QString error;
        QVERIFY(!PackageStore::fetchGitTag(repository.path(), "v1.0.0", &candidate, &error));
        QVERIFY2(PackageStore::fetchGitTag(repository.path(), "v1.0.0", &candidate, &error, true), qPrintable(error));
        QCOMPARE(candidate.manifest.id, QStringLiteral("org.example.package"));
        QVERIFY(candidate.identity.tagObjectId != candidate.identity.commitId);
        QVERIFY(candidate.identity.developerSource);
        PackageStore store(storeDirectory.path());
        QVERIFY2(store.install(candidate, {}, &error), qPrintable(error));
        QVERIFY(write(repository.filePath("theme.json"), "{\"changed\":true}"));
        QVERIFY(git(repository.path(), {"add", "--all"}));
        QVERIFY(git(repository.path(), {"commit", "-m", "moved"}));
        QVERIFY(git(repository.path(), {"tag", "-fa", "v1.0.0", "-m", "moved"}));
        PackageCandidate changed;
        QVERIFY2(PackageStore::fetchGitTag(repository.path(), "v1.0.0", &changed, &error, true), qPrintable(error));
        QVERIFY(!store.install(changed, {}, &error));
        QVERIFY(git(repository.path(), {"tag", "1.0.0"}));
        QVERIFY(PackageStore::fetchGitTag(repository.path(), "1.0.0", &changed, &error, true));
        QVERIFY(!store.install(changed, {}, &error));
        QVERIFY(error.contains(QStringLiteral("version already has different locked contents")));
        QVERIFY(!PackageStore::fetchGitTag(repository.path(), "main", &changed, &error, true));
    }
};

QTEST_GUILESS_MAIN(TestAddonPackages)
#include "test_addon_packages.moc"
