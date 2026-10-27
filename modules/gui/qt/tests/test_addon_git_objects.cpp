// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include "addons/addon_package_store.hpp"
#include "addons/addon_repository_updates.hpp"

using namespace vlc::addons;
namespace {
class Environment final {
    QMap<QByteArray, QByteArray> saved;
public:
    Environment() {
        for (const auto* name : {"PATH", "GIT_SSL_CAINFO", "NO_PROXY", "no_proxy"}) saved.insert(name, qgetenv(name));
    }
    ~Environment() {
        for (auto it = saved.cbegin(); it != saved.cend(); ++it)
            if (it.value().isNull()) qunsetenv(it.key().constData()); else qputenv(it.key().constData(), it.value());
    }
};
class HttpsRepository final {
    QTemporaryDir files;
    QProcess server;
public:
    QString certificate, source;
    ~HttpsRepository() {
        if (server.state() != QProcess::NotRunning) {
            server.terminate(); if (!server.waitForFinished(1000)) { server.kill(); server.waitForFinished(1000); }
        }
    }
    bool start(const QString& projectRoot) {
        const auto openssl = QStandardPaths::findExecutable(QStringLiteral("openssl"));
        const auto python = QStandardPaths::findExecutable(QStringLiteral("python3"));
        const auto git = QStandardPaths::findExecutable(QStringLiteral("git"));
        if (openssl.isEmpty() || python.isEmpty() || git.isEmpty() || !files.isValid()) return false;
        certificate = files.filePath(QStringLiteral("certificate.pem"));
        const auto key = files.filePath(QStringLiteral("fixture-key.pem"));
        QProcess makeCertificate;
        makeCertificate.start(openssl, {QStringLiteral("req"), QStringLiteral("-x509"), QStringLiteral("-newkey"), QStringLiteral("rsa:2048"),
            QStringLiteral("-nodes"), QStringLiteral("-days"), QStringLiteral("1"), QStringLiteral("-subj"), QStringLiteral("/CN=127.0.0.1"),
            QStringLiteral("-addext"), QStringLiteral("subjectAltName=IP:127.0.0.1"), QStringLiteral("-keyout"), key, QStringLiteral("-out"), certificate});
        if (!makeCertificate.waitForFinished(10000) || makeCertificate.exitCode() != 0) return false;
        const auto fixture = QFINDTESTDATA("fixtures/addon-git-http-fixture.py");
        if (fixture.isEmpty()) return false;
        server.start(python, {fixture, projectRoot, certificate, key, git});
        if (!server.waitForStarted(3000) || !server.waitForReadyRead(3000)) return false;
        bool valid = false; const auto port = server.readLine().trimmed().toInt(&valid);
        if (!valid || port < 1 || port > 65535) return false;
        source = QStringLiteral("https://127.0.0.1:%1/repo.git").arg(port); return true;
    }
};
}

class TestAddonGitObjects : public QObject {
    Q_OBJECT
    static QJsonObject manifest(const QString& source, const QString& version = QStringLiteral("1.0.0")) {
        return {{"formatVersion", 1}, {"id", "org.example.git"}, {"name", "Git fixture"}, {"version", version},
            {"publisher", "Tests"}, {"license", "GPL-2.0-or-later"}, {"source", source},
            {"api", QJsonObject{{"addon", ">=1.0.0 <2.0.0"}, {"contracts", QJsonObject{{"theme", "^1.0.0"}}}}},
            {"files", QJsonArray{"addon.json", "LICENSE", "theme.json"}},
            {"contributions", QJsonArray{QJsonObject{{"id", "theme"}, {"type", "theme"}, {"contract", "theme@1"}, {"resource", "theme.json"}}}}};
    }
    static bool write(const QString& path, const QByteArray& bytes) {
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
        QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
    static bool populate(const QString& path, const QJsonObject& document) {
        return write(QDir(path).filePath("addon.json"), QJsonDocument(document).toJson())
            && write(QDir(path).filePath("LICENSE"), "Test license\n")
            && write(QDir(path).filePath("theme.json"), "{\"contract\":\"theme@1\"}\n");
    }
    static bool git(const QString& directory, const QStringList& arguments, QByteArray* output = nullptr) {
        QProcess process; auto env = QProcessEnvironment::systemEnvironment();
        env.insert("GIT_CONFIG_NOSYSTEM", "1");
#ifdef Q_OS_WIN
        env.insert("GIT_CONFIG_GLOBAL", "NUL");
#else
        env.insert("GIT_CONFIG_GLOBAL", "/dev/null");
#endif
        process.setProcessEnvironment(env); process.setWorkingDirectory(directory);
        process.start(QStandardPaths::findExecutable(QStringLiteral("git")), QStringList{"-c", "user.name=Git fixture", "-c", "user.email=git@example.invalid",
            "-c", "commit.gpgsign=false", "-c", "tag.gpgsign=false", "-c", "core.hooksPath="} + arguments);
        if (!process.waitForFinished(10000) || process.exitCode() != 0) return false;
        if (output) *output = process.readAllStandardOutput().trimmed(); return true;
    }
    static bool commit(const QString& path) { return git(path, {"add", "--all"}) && git(path, {"commit", "-m", "Test release"}); }
    static bool initialize(const QString& path, const QString& format, const QJsonObject& document) {
        return populate(path, document) && git(path, {"init", "--template=", QStringLiteral("--object-format=") + format}) && commit(path);
    }
private slots:
    void taggedObjectFormats_data() {
        QTest::addColumn<QString>("format"); QTest::addColumn<bool>("annotated"); QTest::addColumn<bool>("https");
        for (const auto& format : {QStringLiteral("sha1"), QStringLiteral("sha256")})
            for (bool annotated : {false, true}) for (bool https : {false, true}) {
                const QString name = format + (annotated ? QStringLiteral("-annotated") : QStringLiteral("-lightweight"))
                    + (https ? QStringLiteral("-https") : QStringLiteral("-developer"));
                QTest::newRow(qPrintable(name)) << format << annotated << https;
            }
    }
    void taggedObjectFormats() {
        QFETCH(QString, format); QFETCH(bool, annotated); QFETCH(bool, https);
        QTemporaryDir project, storage; Environment environment; HttpsRepository server;
        const auto path = project.filePath(QStringLiteral("repo.git"));
        const QString publicSource = https ? (server.start(project.path()) ? server.source : QString{}) : QStringLiteral("https://git.example.invalid/package.git");
        QVERIFY(!publicSource.isEmpty()); QVERIFY(initialize(path, format, manifest(publicSource)));
        QVERIFY(git(path, annotated ? QStringList{"tag", "-a", "v1.0.0", "-m", "Annotated release"} : QStringList{"tag", "v1.0.0"}));
        const auto source = https ? publicSource : path;
        PackageCandidate candidate; QString error;
        if (https) {
            qputenv("NO_PROXY", "127.0.0.1"); qputenv("no_proxy", "127.0.0.1"); qunsetenv("GIT_SSL_CAINFO");
            QVERIFY(!PackageStore::fetchGitTag(source, QStringLiteral("v1.0.0"), &candidate, &error, false, 5000));
            QVERIFY(candidate.manifest.id.isEmpty()); error.clear(); qputenv("GIT_SSL_CAINFO", server.certificate.toUtf8());
        }
        QVERIFY2(PackageStore::fetchGitTag(source, QStringLiteral("v1.0.0"), &candidate, &error, !https), qPrintable(error));
        QCOMPARE(candidate.identity.objectFormat, format);
        QCOMPARE(candidate.identity.commitId.size(), format == QLatin1String("sha1") ? 40 : 64);
        QCOMPARE(candidate.identity.tagObjectId.size(), candidate.identity.commitId.size());
        QCOMPARE(candidate.identity.tagObjectId != candidate.identity.commitId, annotated);
        QCOMPARE(candidate.identity.source, source); QCOMPARE(candidate.identity.developerSource, !https);
        QVERIFY(!candidate.identity.distributionSource); QCOMPARE(candidate.digest, packageDigest(candidate.contents));
        PackageStore store(storage.path()); QVERIFY2(store.install(candidate, {}, &error), qPrintable(error));
        InstalledPackage active; QVERIFY(store.installed(candidate.manifest.id, &active));
        QCOMPARE(active.identity.objectFormat, format); QCOMPARE(active.digest, candidate.digest);
    }
    void exactTagAndCompatibleUpdates_data() {
        QTest::addColumn<QString>("format"); QTest::addColumn<bool>("https");
        for (const auto& format : {QStringLiteral("sha1"), QStringLiteral("sha256")}) for (bool https : {false, true}) {
            const QString name = format + (https ? QStringLiteral("-https") : QStringLiteral("-developer"));
            QTest::newRow(qPrintable(name)) << format << https;
        }
    }
    void exactTagAndCompatibleUpdates() {
        QFETCH(QString, format); QFETCH(bool, https); QTemporaryDir repository, storage;
        Environment environment; HttpsRepository server; QString error;
        const auto path = repository.filePath(QStringLiteral("repo.git"));
        const QString declared = https ? (server.start(repository.path()) ? server.source : QString{}) : QStringLiteral("https://git.example.invalid/package.git");
        QVERIFY(!declared.isEmpty()); const QString source = https ? declared : path;
        if (https) { qputenv("NO_PROXY", "127.0.0.1"); qputenv("no_proxy", "127.0.0.1"); qputenv("GIT_SSL_CAINFO", server.certificate.toUtf8()); }
        QVERIFY(initialize(path, format, manifest(declared))); QVERIFY(git(path, {"tag", "v1.0.0"}));
        PackageCandidate first, update;
        QVERIFY2(PackageStore::fetchGitTag(source, "v1.0.0", &first, &error, !https), qPrintable(error));
        PackageStore store(storage.path()); QVERIFY(store.install(first, {}, &error));
        QVERIFY(populate(path, manifest(declared, QStringLiteral("1.1.0")))); QVERIFY(commit(path));
        QVERIFY(git(path, {"tag", "-a", "v1.1.0", "-m", "Next release"}));
        // A branch with the same name must not replace the requested tag.
        QVERIFY(git(path, {"branch", "v1.0.0"}));
        PackageCandidate exact; QVERIFY(PackageStore::fetchGitTag(source, "v1.0.0", &exact, &error, !https));
        QCOMPARE(exact.identity.tagObjectId, first.identity.tagObjectId); QCOMPARE(exact.manifest.version, QStringLiteral("1.0.0"));
        QVERIFY2(store.fetchRepositoryUpdate(first.manifest.id, QStringLiteral("^1.0.0"), false, &update, &error, !https), qPrintable(error));
        QCOMPARE(update.manifest.version, QStringLiteral("1.1.0")); QCOMPARE(update.identity.objectFormat, format);
        InstalledPackage active; QVERIFY(store.installed(first.manifest.id, &active)); QCOMPARE(active.manifest.version, QStringLiteral("1.0.0"));
        QVERIFY(store.install(update, {}, &error));
        QVERIFY(git(path, {"branch", "v1.2.0"}));
        QVERIFY(!PackageStore::fetchGitTag(source, "v1.2.0", &exact, &error, !https)); QVERIFY(exact.manifest.id.isEmpty());
    }
    void anObjectFormatChangeCannotMoveALockedTag_data() {
        QTest::addColumn<QString>("before"); QTest::addColumn<QString>("after");
        QTest::newRow("sha1-to-sha256") << QStringLiteral("sha1") << QStringLiteral("sha256");
        QTest::newRow("sha256-to-sha1") << QStringLiteral("sha256") << QStringLiteral("sha1");
    }
    void anObjectFormatChangeCannotMoveALockedTag() {
        QFETCH(QString, before); QFETCH(QString, after); QTemporaryDir repository, storage; QString error;
        const auto path = repository.filePath(QStringLiteral("source"));
        const auto document = manifest(QStringLiteral("https://git.example.invalid/package.git"));
        QVERIFY(initialize(path, before, document)); QVERIFY(git(path, {"tag", "v1.0.0"}));
        PackageCandidate first, changed;
        QVERIFY(PackageStore::fetchGitTag(path, "v1.0.0", &first, &error, true));
        PackageStore store(storage.path()); QVERIFY(store.install(first, {}, &error));
        QVERIFY(QDir(path).removeRecursively());
        QVERIFY(initialize(path, after, document)); QVERIFY(git(path, {"tag", "v1.0.0"}));
        QVERIFY(PackageStore::fetchGitTag(path, "v1.0.0", &changed, &error, true));
        QCOMPARE(changed.digest, first.digest); QVERIFY(!store.install(changed, {}, &error));
        QVERIFY(error.contains(QStringLiteral("locked tag")));
        InstalledPackage active; QVERIFY(store.installed(first.manifest.id, &active)); QCOMPARE(active.identity.objectFormat, before);
    }
    void aReleaseChangeBetweenAdvertisementAndFetchIsRejected_data() {
        QTest::addColumn<QString>("format"); QTest::addColumn<bool>("https"); QTest::addColumn<bool>("changeFormat");
        for (const auto& format : {QStringLiteral("sha1"), QStringLiteral("sha256")})
            for (bool https : {false, true}) for (bool changeFormat : {false, true}) {
                const QString name = format + (https ? QStringLiteral("-https") : QStringLiteral("-developer"))
                    + (changeFormat ? QStringLiteral("-format-changed") : QStringLiteral("-tag-moved"));
                QTest::newRow(qPrintable(name)) << format << https << changeFormat;
            }
    }
    void aReleaseChangeBetweenAdvertisementAndFetchIsRejected() {
#ifdef Q_OS_UNIX
        QFETCH(QString, format); QFETCH(bool, https); QFETCH(bool, changeFormat);
        QTemporaryDir project, tools; Environment environment; HttpsRepository server;
        const auto real = QStandardPaths::findExecutable(QStringLiteral("git")); QVERIFY(!real.isEmpty());
        const auto path = project.filePath(QStringLiteral("repo.git"));
        const QString declared = https ? (server.start(project.path()) ? server.source : QString{}) : QStringLiteral("https://git.example.invalid/package.git");
        QVERIFY(!declared.isEmpty()); QVERIFY(initialize(path, format, manifest(declared)));
        QVERIFY(git(path, {"tag", "v1.0.0"}));
        QVERIFY(write(QDir(path).filePath("theme.json"), "{\"contract\":\"theme@1\",\"label\":\"Changed\"}\n"));
        QVERIFY(commit(path)); QByteArray next; QVERIFY(git(path, {"rev-parse", "HEAD"}, &next));
        const auto replacement = project.filePath(QStringLiteral("replacement.git"));
        if (changeFormat) {
            const QString otherFormat = format == QLatin1String("sha1") ? QStringLiteral("sha256") : QStringLiteral("sha1");
            QVERIFY(initialize(replacement, otherFormat, manifest(declared))); QVERIFY(git(replacement, {"tag", "v1.0.0"}));
        }
        const auto marker = tools.filePath(QStringLiteral("changed"));
        QByteArray script = QByteArray("#!/usr/bin/python3\nimport os, shutil, subprocess, sys\nreal = ")
            + QJsonDocument(QJsonArray{real}).toJson(QJsonDocument::Compact) + "[0]\nsource = "
            + QJsonDocument(QJsonArray{path}).toJson(QJsonDocument::Compact) + "[0]\nmarker = "
            + QJsonDocument(QJsonArray{marker}).toJson(QJsonDocument::Compact) + "[0]\nnext_id = "
            + QJsonDocument(QJsonArray{QString::fromLatin1(next)}).toJson(QJsonDocument::Compact) + "[0]\nreplacement = "
            + QJsonDocument(QJsonArray{replacement}).toJson(QJsonDocument::Compact) + "[0]\nif 'fetch' in sys.argv[1:]:\n";
        script += changeFormat ? QByteArray("    shutil.rmtree(source)\n    shutil.copytree(replacement, source)\n")
            : QByteArray("    subprocess.run([real, '-C', source, 'update-ref', 'refs/tags/v1.0.0', next_id], check=True)\n");
        script += "    with open(marker, 'w') as file: file.write('changed')\nos.execv(real, [real] + sys.argv[1:])\n";
        const auto wrapper = tools.filePath(QStringLiteral("git")); QVERIFY(write(wrapper, script));
        QVERIFY(QFile::setPermissions(wrapper, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QByteArray search = tools.path().toUtf8() + ':' + qgetenv("PATH"); qputenv("PATH", search);
        if (https) { qputenv("NO_PROXY", "127.0.0.1"); qputenv("no_proxy", "127.0.0.1"); qputenv("GIT_SSL_CAINFO", server.certificate.toUtf8()); }
        PackageCandidate candidate; QString error;
        QVERIFY(!PackageStore::fetchGitTag(https ? declared : path, "v1.0.0", &candidate, &error, !https));
        QVERIFY(QFileInfo::exists(marker)); QVERIFY(candidate.manifest.id.isEmpty());
        QVERIFY2(error.contains(changeFormat ? QStringLiteral("Git request failed") : QStringLiteral("tag changed")), qPrintable(error));
#else
        QSKIP("This exact-race fixture requires a Unix executable script.");
#endif
    }
    void anHttpsManifestCannotClaimAnotherSource_data() {
        QTest::addColumn<QString>("format");
        QTest::newRow("sha1") << QStringLiteral("sha1"); QTest::newRow("sha256") << QStringLiteral("sha256");
    }
    void anHttpsManifestCannotClaimAnotherSource() {
        QFETCH(QString, format); QTemporaryDir project; Environment environment; HttpsRepository server;
        QVERIFY(server.start(project.path())); const auto path = project.filePath(QStringLiteral("repo.git"));
        QVERIFY(initialize(path, format, manifest(QStringLiteral("https://other.example.invalid/package.git"))));
        QVERIFY(git(path, {"tag", "v1.0.0"}));
        qputenv("NO_PROXY", "127.0.0.1"); qputenv("no_proxy", "127.0.0.1"); qputenv("GIT_SSL_CAINFO", server.certificate.toUtf8());
        PackageCandidate candidate; QString error;
        QVERIFY(!PackageStore::fetchGitTag(server.source, "v1.0.0", &candidate, &error));
        QVERIFY(candidate.manifest.id.isEmpty()); QVERIFY(error.contains(QStringLiteral("manifest source")));
    }
    void allGitCommandsShareOneDeadline() {
#ifdef Q_OS_UNIX
        QTemporaryDir project, tools; Environment environment;
        const auto real = QStandardPaths::findExecutable(QStringLiteral("git")); QVERIFY(!real.isEmpty());
        QVERIFY(initialize(project.path(), QStringLiteral("sha1"), manifest(QStringLiteral("https://git.example.invalid/package.git"))));
        QVERIFY(git(project.path(), {"tag", "v1.0.0"}));
        const auto trace = tools.filePath(QStringLiteral("calls.jsonl"));
        const QByteArray script = QByteArray("#!/usr/bin/python3\nimport json, os, sys, time\nreal = ")
            + QJsonDocument(QJsonArray{real}).toJson(QJsonDocument::Compact) + "[0]\ntrace = "
            + QJsonDocument(QJsonArray{trace}).toJson(QJsonDocument::Compact)
            + "[0]\nwith open(trace, 'a') as file: file.write(json.dumps(sys.argv[1:]) + '\\n')\n"
              "if 'ls-remote' in sys.argv[1:] or 'init' in sys.argv[1:]: time.sleep(0.6)\n"
              "os.execv(real, [real] + sys.argv[1:])\n";
        const auto wrapper = tools.filePath(QStringLiteral("git")); QVERIFY(write(wrapper, script));
        QVERIFY(QFile::setPermissions(wrapper, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QByteArray path = tools.path().toUtf8() + ':' + qgetenv("PATH"); qputenv("PATH", path);
        PackageCandidate candidate; QString error; QElapsedTimer elapsed; elapsed.start();
        QVERIFY(!PackageStore::fetchGitTag(project.path(), "v1.0.0", &candidate, &error, true, 1000));
        QVERIFY2(elapsed.elapsed() < 3500, qPrintable(QString::number(elapsed.elapsed())));
        QVERIFY(candidate.manifest.id.isEmpty()); QVERIFY(error.contains(QStringLiteral("limit")));
        QFile file(trace); QVERIFY(file.open(QIODevice::ReadOnly));
        for (const auto& line : file.readAll().split('\n')) {
            if (!line.isEmpty()) QVERIFY(!QJsonDocument::fromJson(line).array().contains(QStringLiteral("fetch")));
        }
#else
        QSKIP("This deadline fixture requires a Unix executable script.");
#endif
    }
    void aToolWithoutSha256RefusesWithoutAnAlgorithmFallback() {
#ifdef Q_OS_UNIX
        QTemporaryDir projects, tools; Environment environment;
        const auto real = QStandardPaths::findExecutable(QStringLiteral("git")); QVERIFY(!real.isEmpty());
        const auto one = projects.filePath(QStringLiteral("sha1")), two = projects.filePath(QStringLiteral("sha256"));
        const auto document = manifest(QStringLiteral("https://git.example.invalid/package.git"));
        QVERIFY(initialize(one, QStringLiteral("sha1"), document)); QVERIFY(git(one, {"tag", "v1.0.0"}));
        QVERIFY(initialize(two, QStringLiteral("sha256"), document)); QVERIFY(git(two, {"tag", "v1.0.0"}));
        const auto trace = tools.filePath(QStringLiteral("calls.jsonl"));
        const QByteArray script = QByteArray("#!/usr/bin/python3\nimport json, os, sys\nreal = ")
            + QJsonDocument(QJsonArray{real}).toJson(QJsonDocument::Compact) + "[0]\ntrace = "
            + QJsonDocument(QJsonArray{trace}).toJson(QJsonDocument::Compact)
            + "[0]\nwith open(trace, 'a') as file: file.write(json.dumps(sys.argv[1:]) + '\\n')\n"
              "if '--object-format=sha256' in sys.argv[1:]:\n    sys.stderr.write('Test tool does not support SHA-256')\n    sys.exit(129)\n"
              "os.execv(real, [real] + sys.argv[1:])\n";
        const auto wrapper = tools.filePath(QStringLiteral("git")); QVERIFY(write(wrapper, script));
        QVERIFY(QFile::setPermissions(wrapper, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QByteArray path = tools.path().toUtf8() + ':' + qgetenv("PATH"); qputenv("PATH", path);
        PackageCandidate candidate; QString error;
        QVERIFY2(PackageStore::fetchGitTag(one, "v1.0.0", &candidate, &error, true), qPrintable(error));
        QCOMPARE(candidate.identity.objectFormat, QStringLiteral("sha1"));
        QVERIFY(!PackageStore::fetchGitTag(two, "v1.0.0", &candidate, &error, true));
        QVERIFY(candidate.manifest.id.isEmpty()); QVERIFY(!error.isEmpty());
        QFile file(trace); QVERIFY(file.open(QIODevice::ReadOnly)); const auto calls = file.readAll().split('\n');
        int refusals = 0; bool refused = false;
        for (const auto& line : calls) {
            if (line.isEmpty()) continue;
            const auto args = QJsonDocument::fromJson(line).array();
            if (args.contains(QStringLiteral("--object-format=sha256"))) { ++refusals; refused = true; }
            else if (refused) QVERIFY(!args.contains(QStringLiteral("fetch")));
        }
        QCOMPARE(refusals, 1);
#else
        QSKIP("This tool-capability fixture requires a Unix executable script.");
#endif
    }
};
QTEST_GUILESS_MAIN(TestAddonGitObjects)
#include "test_addon_git_objects.moc"
