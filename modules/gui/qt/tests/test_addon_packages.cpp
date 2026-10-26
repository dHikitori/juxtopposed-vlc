// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include "addons/addon_manifest.hpp"
#include "addons/addon_package_store.hpp"

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
        QVERIFY(write(package.filePath("settings/schema.json"), R"({"type":"object","properties":{"apiKey":{"type":"string","x-vlc-secret":true}}})"));
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
    }
    void referenceSubtitleAddonManifest()
    {
        const auto file = QFINDTESTDATA("../../../../contrib-addons/examples/thesubtitledb/addon.json");
        QVERIFY(!file.isEmpty());
        Manifest parsed;
        QString error;
        QVERIFY2(Manifest::loadDirectory(QFileInfo(file).absolutePath(), &parsed, nullptr, &error), qPrintable(error));
        QCOMPARE(parsed.id, QStringLiteral("org.videolan.thesubtitledb"));
        QVERIFY(parsed.contribution("search"));
        QVERIFY(parsed.permissionDeclared("credentials.use"));
        QCOMPARE(parsed.permissionScope("network.http").value("methods").toArray(), QJsonArray{"GET"});
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
        QVERIFY(!PackageStore::fetchGitTag(repository.path(), "main", &changed, &error, true));
    }
};

QTEST_GUILESS_MAIN(TestAddonPackages)
#include "test_addon_packages.moc"
