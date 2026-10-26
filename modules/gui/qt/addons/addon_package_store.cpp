// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_package_store.hpp"
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <algorithm>
#include <functional>
#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#endif

namespace vlc::addons {
namespace {
bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}
bool packageId(const QString& id)
{
    return id.size() <= 128 && QRegularExpression(QStringLiteral("^[a-z][a-z0-9]*(?:[.-][a-z0-9]+)+$")).match(id).hasMatch();
}
bool digestId(const QString& value)
{
    return QRegularExpression(QStringLiteral("^[0-9a-f]{64}$")).match(value).hasMatch();
}
bool gitId(const QString& value)
{
    return QRegularExpression(QStringLiteral("^(?:[0-9a-f]{40}|[0-9a-f]{64})$")).match(value).hasMatch();
}
QJsonObject identityJson(const ReleaseIdentity& identity)
{
    return {{"source", identity.source}, {"tag", identity.tag}, {"tagObjectId", identity.tagObjectId},
            {"commitId", identity.commitId}, {"objectFormat", identity.objectFormat},
            {"developerSource", identity.developerSource}};
}
ReleaseIdentity readIdentity(const QJsonObject& value)
{
    ReleaseIdentity result;
    result.source = value.value("source").toString();
    result.tag = value.value("tag").toString();
    result.tagObjectId = value.value("tagObjectId").toString();
    result.commitId = value.value("commitId").toString();
    result.objectFormat = value.value("objectFormat").toString();
    result.developerSource = value.value("developerSource").toBool();
    return result;
}
bool writeJson(const QString& path, const QJsonObject& value, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return fail(error, QStringLiteral("Cannot create add-on state file"));
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size() || !file.commit())
        return fail(error, QStringLiteral("Cannot commit add-on state"));
    return true;
}
bool readJson(const QString& path, QJsonObject* value, QString* error)
{
    QFile file(path);
    if (QFileInfo(file).isSymLink() || !file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        return fail(error, QStringLiteral("Cannot read add-on state"));
    QJsonParseError parseError;
    const auto result = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !result.isObject())
        return fail(error, QStringLiteral("Invalid add-on state"));
    *value = result.object();
    return true;
}
bool subset(const QJsonValue& requested, const QJsonValue& declared)
{
    if (requested.type() != declared.type()) return false;
    if (requested.isObject()) {
        const auto a = requested.toObject(), b = declared.toObject();
        for (auto it = a.begin(); it != a.end(); ++it)
            if (!b.contains(it.key()) || !subset(it.value(), b.value(it.key()))) return false;
        return true;
    }
    if (requested.isArray()) {
        const auto b = declared.toArray();
        for (const auto& item : requested.toArray()) if (!b.contains(item)) return false;
        return true;
    }
    return requested == declared;
}
bool validateGrants(const Manifest& manifest, const QJsonObject& grants, QString* error)
{
    for (auto it = manifest.requiredPermissions.begin(); it != manifest.requiredPermissions.end(); ++it)
        if (!grants.contains(it.key()) || grants.value(it.key()) != it.value())
            return fail(error, QStringLiteral("Required permission not approved: ") + it.key());
    for (auto it = grants.begin(); it != grants.end(); ++it)
        if (!manifest.permissionDeclared(it.key()) || !subset(it.value(), manifest.permissionScope(it.key())))
            return fail(error, QStringLiteral("Grant exceeds declared permission: ") + it.key());
    return true;
}
qint64 directoryBytes(const QString& path)
{
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
        if (total > 128 * 1024 * 1024) return total;
    }
    return total;
}

// Only a resolved Git binary runs. Bare-object access never executes attributes,
// filters, hooks, repository build files or third-party commands.
class GitObjects
{
public:
    explicit GitObjects(int timeout) : m_timeout(timeout) {}
    bool initialize(QString* error)
    {
        m_elapsed.start();
        m_executable = QStandardPaths::findExecutable(QStringLiteral("git"));
        if (m_executable.isEmpty())
            return fail(error, QStringLiteral("Git transport is unavailable; install the packaged Git tool"));
        if (!m_temporary.isValid())
            return fail(error, QStringLiteral("Cannot create isolated Git transport directory"));
        m_repo = QDir(m_temporary.path()).filePath(QStringLiteral("objects.git"));
        return run({QStringLiteral("init"), QStringLiteral("--bare"), QStringLiteral("--template="), m_repo},
                   nullptr, error, false);
    }
    bool run(QStringList arguments, QByteArray* output, QString* error, bool repository = true)
    {
        if (QThread::currentThread()->isInterruptionRequested())
            return fail(error, QStringLiteral("Git transport was cancelled"));
        QProcess process;
        QProcessEnvironment environment;
        const auto inherited = QProcessEnvironment::systemEnvironment();
        for (const auto& name : {QStringLiteral("PATH"), QStringLiteral("SystemRoot"),
                QStringLiteral("WINDIR"), QStringLiteral("TEMP"), QStringLiteral("TMP"),
                QStringLiteral("SSL_CERT_FILE"), QStringLiteral("SSL_CERT_DIR"),
                QStringLiteral("HTTPS_PROXY"), QStringLiteral("https_proxy"),
                QStringLiteral("NO_PROXY"), QStringLiteral("no_proxy")})
            if (inherited.contains(name)) environment.insert(name, inherited.value(name));
        environment.insert(QStringLiteral("HOME"), m_temporary.path());
        environment.insert(QStringLiteral("XDG_CONFIG_HOME"), m_temporary.path());
        environment.insert(QStringLiteral("GIT_CONFIG_NOSYSTEM"), QStringLiteral("1"));
        environment.insert(QStringLiteral("GIT_CONFIG_SYSTEM"),
                           QDir(m_temporary.path()).filePath(QStringLiteral("no-system-config")));
        environment.insert(QStringLiteral("GIT_CONFIG_GLOBAL"),
                           QDir(m_temporary.path()).filePath(QStringLiteral("no-global-config")));
        environment.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
        environment.insert(QStringLiteral("GIT_LFS_SKIP_SMUDGE"), QStringLiteral("1"));
        environment.insert(QStringLiteral("GIT_ATTR_NOSYSTEM"), QStringLiteral("1"));
        environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
        process.setProcessEnvironment(environment);
        process.setWorkingDirectory(m_temporary.path());
#ifdef Q_OS_UNIX
        process.setChildProcessModifier([] { ::setpgid(0, 0); });
#endif
        QStringList fixed{
            QStringLiteral("-c"), QStringLiteral("core.hooksPath="),
            QStringLiteral("-c"), QStringLiteral("credential.helper="),
            QStringLiteral("-c"), QStringLiteral("protocol.allow=never"),
            QStringLiteral("-c"), QStringLiteral("protocol.https.allow=always"),
            QStringLiteral("-c"), QStringLiteral("protocol.file.allow=") +
                (developer ? QStringLiteral("always") : QStringLiteral("never")),
            QStringLiteral("-c"), QStringLiteral("http.followRedirects=false"),
            QStringLiteral("-c"), QStringLiteral("fetch.recurseSubmodules=false"),
            QStringLiteral("-c"), QStringLiteral("core.attributesFile="),
            QStringLiteral("-c"), QStringLiteral("gc.auto=0"),
            QStringLiteral("-c"), QStringLiteral("maintenance.auto=false")
        };
        if (repository) fixed << QStringLiteral("--git-dir=") + m_repo;
        fixed.append(arguments);
        process.start(m_executable, fixed, QIODevice::ReadOnly);
        if (!process.waitForStarted(3000))
            return fail(error, QStringLiteral("Cannot start Git transport"));
        QByteArray data, diagnostic;
        while (process.state() != QProcess::NotRunning) {
            process.waitForFinished(25);
            data += process.readAllStandardOutput();
            diagnostic += process.readAllStandardError();
            const bool interrupted = QThread::currentThread()->isInterruptionRequested();
            if (interrupted || data.size() > Manifest::MaximumPackageBytes || diagnostic.size() > 256 * 1024
                || m_elapsed.elapsed() > m_timeout || directoryBytes(m_temporary.path()) > 128 * 1024 * 1024) {
#ifdef Q_OS_UNIX
                const auto pid = process.processId();
                if (pid > 0) ::kill(-pid, SIGKILL);
#endif
                process.kill();
                process.waitForFinished(3000);
                return fail(error, interrupted ? QStringLiteral("Git transport was cancelled")
                    : QStringLiteral("Git transport exceeded its time or size budget"));
            }
        }
        data += process.readAllStandardOutput();
        diagnostic += process.readAllStandardError();
        if (QThread::currentThread()->isInterruptionRequested())
            return fail(error, QStringLiteral("Git transport was cancelled"));
        if (data.size() > Manifest::MaximumPackageBytes || diagnostic.size() > 256 * 1024
            || m_elapsed.elapsed() > m_timeout)
            return fail(error, QStringLiteral("Git transport exceeded its time or size budget"));
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
            return fail(error, QStringLiteral("Git transport failed; verify repository access and tag"));
        if (output) *output = data;
        return true;
    }
    bool developer = false;
private:
    QString m_executable, m_repo;
    QTemporaryDir m_temporary;
    QElapsedTimer m_elapsed;
    int m_timeout;
};

bool validateCandidate(const PackageCandidate& candidate, QString* error)
{
    Manifest parsed;
    if (!Manifest::parse(candidate.contents.value(QStringLiteral("addon.json")), &parsed, error)) return false;
    if (parsed.id != candidate.manifest.id || parsed.version != candidate.manifest.version
        || parsed.document != candidate.manifest.document)
        return fail(error, QStringLiteral("Candidate manifest changed after validation"));
    if (candidate.contents.size() != parsed.files.size())
        return fail(error, QStringLiteral("Candidate inventory mismatch"));
    qint64 total = 0;
    for (const auto& path : parsed.files) {
        if (!candidate.contents.contains(path)) return fail(error, QStringLiteral("Candidate resource missing"));
        total += candidate.contents.value(path).size();
        if (candidate.contents.value(path).size() > Manifest::MaximumFileBytes || total > Manifest::MaximumPackageBytes)
            return fail(error, QStringLiteral("Candidate exceeds size budget"));
    }
    if (candidate.digest != packageDigest(candidate.contents) || !digestId(QString::fromLatin1(candidate.digest)))
        return fail(error, QStringLiteral("Candidate integrity mismatch"));
    if (!candidate.identity.developerSource && QUrl(candidate.identity.source) != parsed.source)
        return fail(error, QStringLiteral("Manifest source differs from approved repository"));
    QString tag = candidate.identity.tag;
    if (tag.startsWith(QLatin1Char('v'))) tag.remove(0, 1);
    if (!SemanticVersion::parse(tag, nullptr) || tag != parsed.version)
        return fail(error, QStringLiteral("Release tag and manifest version disagree"));
    if (candidate.identity.developerSource && candidate.identity.objectFormat == QStringLiteral("developer-sha256")) {
        if (candidate.identity.commitId != QString::fromLatin1(candidate.digest)
            || candidate.identity.tagObjectId != QString::fromLatin1(candidate.digest))
            return fail(error, QStringLiteral("Developer snapshot identity does not match its contents"));
    } else {
        if (!gitId(candidate.identity.commitId) || !gitId(candidate.identity.tagObjectId)
            || candidate.identity.commitId.size() != candidate.identity.tagObjectId.size())
            return fail(error, QStringLiteral("Release needs resolved tag and commit IDs"));
        if (candidate.identity.objectFormat != (candidate.identity.commitId.size() == 40
                ? QStringLiteral("sha1") : QStringLiteral("sha256")))
            return fail(error, QStringLiteral("Git object format mismatch"));
    }
    return true;
}
} // namespace

PackageStore::PackageStore(QString root) : m_root(QDir::cleanPath(std::move(root))) {}
QString PackageStore::storageDirectory() const { return m_root; }
QString PackageStore::releaseDirectory(const QString& id, const QString& digest) const
{
    return QDir(m_root).filePath(QStringLiteral("packages/") + id + QStringLiteral("/releases/") + digest);
}
bool PackageStore::inspectDirectory(const QString& directory, const ReleaseIdentity& identity,
                                   PackageCandidate* output, QString* error)
{
    PackageCandidate candidate;
    candidate.identity = identity;
    if (!Manifest::loadDirectory(directory, &candidate.manifest, &candidate.contents, error)) return false;
    candidate.digest = packageDigest(candidate.contents);
    if (candidate.identity.developerSource && candidate.identity.commitId.isEmpty()
        && candidate.identity.tagObjectId.isEmpty()) {
        candidate.identity.objectFormat = QStringLiteral("developer-sha256");
        candidate.identity.commitId = candidate.identity.tagObjectId = QString::fromLatin1(candidate.digest);
    }
    if (!validateCandidate(candidate, error)) return false;
    if (output) *output = candidate;
    return true;
}
bool PackageStore::fetchGitTag(const QString& source, const QString& tag, PackageCandidate* output,
                              QString* error, bool developerLocalSource, int timeoutMs)
{
    QString normalized = tag;
    if (normalized.startsWith(QLatin1Char('v'))) normalized.remove(0, 1);
    if (!SemanticVersion::parse(normalized, nullptr) || tag.contains(QLatin1Char('/'))
        || tag.startsWith(QLatin1Char('-')))
        return fail(error, QStringLiteral("Select an exact SemVer release tag"));
    const QUrl url(source, QUrl::StrictMode);
    if (!developerLocalSource && (url.scheme() != QStringLiteral("https") || url.host().isEmpty()
        || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment()))
        return fail(error, QStringLiteral("Only HTTPS Git sources are accepted"));
    if (developerLocalSource && (!QFileInfo(source).isDir() || QFileInfo(source).isSymLink()))
        return fail(error, QStringLiteral("Developer source must be an existing local repository"));
    GitObjects git(std::clamp(timeoutMs, 1000, 120000));
    git.developer = developerLocalSource;
    if (!git.initialize(error)) return false;
    const QString ref = QStringLiteral("refs/tags/") + tag;
    if (!git.run({QStringLiteral("fetch"), QStringLiteral("--depth=1"), QStringLiteral("--no-tags"),
                  QStringLiteral("--no-recurse-submodules"), QStringLiteral("--no-write-fetch-head"),
                  QStringLiteral("--"), source, ref + QLatin1Char(':') + ref}, nullptr, error))
        return false;
    PackageCandidate candidate;
    candidate.identity.source = source;
    candidate.identity.tag = tag;
    candidate.identity.developerSource = developerLocalSource;
    QByteArray value;
    if (!git.run({QStringLiteral("rev-parse"), QStringLiteral("--verify"), ref}, &value, error)) return false;
    candidate.identity.tagObjectId = QString::fromLatin1(value.trimmed());
    if (!git.run({QStringLiteral("rev-parse"), QStringLiteral("--verify"), ref + QStringLiteral("^{commit}")},
                 &value, error)) return false;
    candidate.identity.commitId = QString::fromLatin1(value.trimmed());
    candidate.identity.objectFormat = candidate.identity.commitId.size() == 64
        ? QStringLiteral("sha256") : QStringLiteral("sha1");
    if (!gitId(candidate.identity.tagObjectId) || !gitId(candidate.identity.commitId))
        return fail(error, QStringLiteral("Invalid resolved Git identity"));
    if (!git.run({QStringLiteral("ls-tree"), QStringLiteral("-rz"), QStringLiteral("--full-tree"),
                  candidate.identity.commitId}, &value, error)) return false;
    QMap<QString, QString> objects;
    QSet<QString> folded;
    int count = 0;
    for (const auto& entry : value.split('\0')) {
        if (entry.isEmpty()) continue;
        if (++count > 4096) return fail(error, QStringLiteral("Repository tree exceeds entry budget"));
        const auto tab = entry.indexOf('\t');
        const auto header = entry.left(tab).split(' ');
        const auto encodedPath = entry.mid(tab + 1);
        const auto path = QString::fromUtf8(encodedPath);
        if (tab < 0 || header.size() != 3 || path.toUtf8() != encodedPath || !portablePackagePath(path)
            || folded.contains(path.toCaseFolded()))
            return fail(error, QStringLiteral("Unsafe repository tree path"));
        folded.insert(path.toCaseFolded());
        if (header[0] != "100644" && header[0] != "100755")
            return fail(error, QStringLiteral("Repository contains links or submodules"));
        if (header[1] != "blob" || !gitId(QString::fromLatin1(header[2])))
            return fail(error, QStringLiteral("Invalid Git tree object"));
        objects.insert(path, QString::fromLatin1(header[2]));
    }
    if (!objects.contains(QStringLiteral("addon.json")))
        return fail(error, QStringLiteral("Tag does not contain an add-on manifest"));
    if (!git.run({QStringLiteral("cat-file"), QStringLiteral("blob"), objects.value(QStringLiteral("addon.json"))},
                 &value, error) || !Manifest::parse(value, &candidate.manifest, error)) return false;
    qint64 total = 0;
    for (const auto& path : candidate.manifest.files) {
        if (!objects.contains(path)) return fail(error, QStringLiteral("Declared resource missing from tag"));
        QByteArray size;
        if (!git.run({QStringLiteral("cat-file"), QStringLiteral("-s"), objects.value(path)}, &size, error)) return false;
        bool ok = false;
        const auto bytes = size.trimmed().toLongLong(&ok);
        if (!ok || bytes < 0 || bytes > Manifest::MaximumFileBytes || (total += bytes) > Manifest::MaximumPackageBytes)
            return fail(error, QStringLiteral("Git package exceeds resource budget"));
        if (!git.run({QStringLiteral("cat-file"), QStringLiteral("blob"), objects.value(path)}, &value, error)
            || value.size() != bytes) return fail(error, QStringLiteral("Cannot read package Git object"));
        candidate.contents.insert(path, value);
    }
    candidate.digest = packageDigest(candidate.contents);
    if (!validateCandidate(candidate, error)) return false;
    QTemporaryDir check;
    if (!check.isValid()) return fail(error, QStringLiteral("Cannot validate staged package"));
    for (auto it = candidate.contents.begin(); it != candidate.contents.end(); ++it) {
        const auto destination = QDir(check.path()).filePath(it.key());
        QDir().mkpath(QFileInfo(destination).absolutePath());
        QFile file(destination);
        if (!file.open(QIODevice::WriteOnly) || file.write(it.value()) != it.value().size())
            return fail(error, QStringLiteral("Cannot stage package validation"));
    }
    Manifest validated;
    if (!Manifest::loadDirectory(check.path(), &validated, nullptr, error)) return false;
    if (output) *output = candidate;
    return true;
}
bool PackageStore::loadState(const QString& id, QJsonObject* output, QString* error) const
{
    if (!packageId(id)) return fail(error, QStringLiteral("Invalid package ID"));
    const auto path = QDir(m_root).filePath(QStringLiteral("packages/") + id + QStringLiteral("/state.json"));
    QJsonObject state;
    if (!readJson(path, &state, error)) return false;
    if (state.value("formatVersion") != 1 || state.value("id") != id || !state.value("current").isObject())
        return fail(error, QStringLiteral("Invalid package state identity"));
    *output = state;
    return true;
}
bool PackageStore::saveState(const QString& id, const QJsonObject& state, QString* error) const
{
    if (!packageId(id)) return fail(error, QStringLiteral("Invalid package ID"));
    const auto folder = QDir(m_root).filePath(QStringLiteral("packages/") + id);
    if (!QDir().mkpath(folder)) return fail(error, QStringLiteral("Cannot create package state directory"));
    return writeJson(QDir(folder).filePath(QStringLiteral("state.json")), state, error);
}
bool PackageStore::install(const PackageCandidate& candidate, const QJsonObject& grants, QString* error)
{
    if (!validateCandidate(candidate, error) || !validateGrants(candidate.manifest, grants, error)) return false;
    if (!QDir().mkpath(m_root)) return fail(error, QStringLiteral("Cannot create add-on store"));
    QLockFile lock(QDir(m_root).filePath(QStringLiteral("store.lock")));
    if (!lock.tryLock(0)) return fail(error, QStringLiteral("Add-on store is busy"));
    const auto id = candidate.manifest.id;
    QJsonObject state;
    const auto statePath = QDir(m_root).filePath(QStringLiteral("packages/") + id + QStringLiteral("/state.json"));
    if (QFileInfo::exists(statePath) && !loadState(id, &state, error)) return false;
    const auto old = state.value("current").toObject();
    if (!old.isEmpty()) {
        const auto identity = readIdentity(old.value("identity").toObject());
        if (identity.source != candidate.identity.source || identity.developerSource != candidate.identity.developerSource)
            return fail(error, QStringLiteral("Package source changed; explicit source reapproval is required"));
        const auto locked = state.value("lockedTags").toObject().value(candidate.identity.tag).toObject();
        if (!locked.isEmpty() && (locked.value("commitId") != candidate.identity.commitId
            || locked.value("tagObjectId") != candidate.identity.tagObjectId
            || locked.value("digest") != QString::fromLatin1(candidate.digest)))
            return fail(error, QStringLiteral("Previously locked tag changed"));
        if (old.value("settingsVersion").toInt() != candidate.manifest.settingsVersion
            && !old.value("settings").toObject().isEmpty())
            return fail(error, QStringLiteral("Settings migration is required before activating this release"));
        if (old.value("digest") == QString::fromLatin1(candidate.digest)) return true;
    }
    const auto destination = releaseDirectory(id, QString::fromLatin1(candidate.digest));
    if (QFileInfo::exists(destination)) {
        Manifest verified;
        QMap<QString, QByteArray> files;
        if (!Manifest::loadDirectory(destination, &verified, &files, error) || packageDigest(files) != candidate.digest)
            return fail(error, QStringLiteral("Installed immutable release failed integrity verification"));
    } else {
        const auto releaseParent = QFileInfo(destination).absolutePath();
        if (!QDir().mkpath(releaseParent)) return fail(error, QStringLiteral("Cannot create release directory"));
        QTemporaryDir staging(QDir(releaseParent).filePath(QStringLiteral(".staging-XXXXXX")));
        if (!staging.isValid()) return fail(error, QStringLiteral("Cannot stage release"));
        for (auto it = candidate.contents.begin(); it != candidate.contents.end(); ++it) {
            const auto path = QDir(staging.path()).filePath(it.key());
            if (!QDir().mkpath(QFileInfo(path).absolutePath()))
                return fail(error, QStringLiteral("Cannot stage resource directory"));
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(it.value()) != it.value().size())
                return fail(error, QStringLiteral("Cannot stage package resource"));
            file.close();
            file.setPermissions(QFile::ReadOwner);
        }
        Manifest verified;
        QMap<QString, QByteArray> files;
        if (!Manifest::loadDirectory(staging.path(), &verified, &files, error) || packageDigest(files) != candidate.digest)
            return fail(error, QStringLiteral("Staged release verification failed"));
        if (!QDir().rename(staging.path(), destination))
            return fail(error, QStringLiteral("Cannot activate immutable release directory"));
        staging.setAutoRemove(false);
    }
    QJsonObject current{
        {"digest", QString::fromLatin1(candidate.digest)}, {"identity", identityJson(candidate.identity)},
        {"version", candidate.manifest.version}, {"grants", grants},
        {"settingsVersion", candidate.manifest.settingsVersion}, {"settings", old.value("settings").toObject()},
        {"settingsRevision", old.value("settingsRevision").toDouble(0)}
    };
    auto history = state.value("lockedTags").toObject();
    history.insert(candidate.identity.tag, QJsonObject{{"commitId", candidate.identity.commitId},
        {"tagObjectId", candidate.identity.tagObjectId}, {"digest", QString::fromLatin1(candidate.digest)}});
    QJsonObject next{
        {"formatVersion", 1}, {"id", id}, {"generation", state.value("generation").toDouble(0) + 1},
        {"enabled", old.isEmpty() ? false : state.value("enabled").toBool()},
        {"current", current}, {"previous", old}, {"lockedTags", history}
    };
    return saveState(id, next, error);
}
bool PackageStore::installed(const QString& id, InstalledPackage* output, QString* error) const
{
    QJsonObject state;
    if (!loadState(id, &state, error)) return false;
    const auto current = state.value("current").toObject();
    const auto digest = current.value("digest").toString();
    if (!digestId(digest)) return fail(error, QStringLiteral("Invalid installed content digest"));
    InstalledPackage result;
    result.directory = releaseDirectory(id, digest);
    QMap<QString, QByteArray> data;
    if (!Manifest::loadDirectory(result.directory, &result.manifest, &data, error)
        || packageDigest(data) != digest.toLatin1() || result.manifest.id != id)
        return fail(error, QStringLiteral("Installed package integrity check failed"));
    result.digest = digest.toLatin1();
    result.identity = readIdentity(current.value("identity").toObject());
    result.grants = current.value("grants").toObject();
    if (!validateGrants(result.manifest, result.grants, error)) return false;
    result.enabled = state.value("enabled").toBool();
    result.generation = quint64(state.value("generation").toDouble());
    result.previousDigest = state.value("previous").toObject().value("digest").toString();
    if (output) *output = result;
    return true;
}
QStringList PackageStore::installedIds() const
{
    QStringList result;
    QDir directory(QDir(m_root).filePath(QStringLiteral("packages")));
    for (const auto& id : directory.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (packageId(id) && QFileInfo::exists(directory.filePath(id + QStringLiteral("/state.json"))))
            result.append(id);
    return result;
}
bool PackageStore::setEnabled(const QString& id, bool enabled, QString* error)
{
    QLockFile lock(QDir(m_root).filePath(QStringLiteral("store.lock")));
    if (!lock.tryLock(0)) return fail(error, QStringLiteral("Add-on store is busy"));
    QJsonObject state;
    if (!loadState(id, &state, error)) return false;
    if (enabled) {
        InstalledPackage verified;
        if (!installed(id, &verified, error)) return false;
    }
    state.insert("enabled", enabled);
    state.insert("generation", state.value("generation").toDouble() + 1);
    return saveState(id, state, error);
}
bool PackageStore::rollback(const QString& id, QString* error)
{
    QLockFile lock(QDir(m_root).filePath(QStringLiteral("store.lock")));
    if (!lock.tryLock(0)) return fail(error, QStringLiteral("Add-on store is busy"));
    QJsonObject state;
    if (!loadState(id, &state, error)) return false;
    const auto previous = state.value("previous").toObject();
    const auto digest = previous.value("digest").toString();
    if (!digestId(digest)) return fail(error, QStringLiteral("No retained rollback release"));
    Manifest manifest;
    QMap<QString, QByteArray> files;
    if (!Manifest::loadDirectory(releaseDirectory(id, digest), &manifest, &files, error)
        || packageDigest(files) != digest.toLatin1()
        || !validateGrants(manifest, previous.value("grants").toObject(), error))
        return fail(error, QStringLiteral("Rollback release failed validation"));
    state.insert("previous", state.value("current"));
    state.insert("current", previous);
    state.insert("generation", state.value("generation").toDouble() + 1);
    return saveState(id, state, error);
}
bool PackageStore::uninstall(const QString& id, bool removeData, QString* error)
{
    QLockFile lock(QDir(m_root).filePath(QStringLiteral("store.lock")));
    if (!lock.tryLock(0)) return fail(error, QStringLiteral("Add-on store is busy"));
    QJsonObject state;
    if (!loadState(id, &state, error)) return false;
    const auto folder = QDir(m_root).filePath(QStringLiteral("packages/") + id);
    if (!removeData) {
        const auto retained = QDir(m_root).filePath(QStringLiteral("retained-settings"));
        if (!QDir().mkpath(retained) || !writeJson(QDir(retained).filePath(id + QStringLiteral(".json")),
                state.value("current").toObject().value("settings").toObject(), error)) return false;
    }
    const QString tombstone = folder + QStringLiteral(".removing-") + QUuid::createUuid().toString(QUuid::Id128);
    if (!QDir().rename(folder, tombstone)) return fail(error, QStringLiteral("Cannot withdraw installed package"));
    // The manager withdraws active registrations and workers before this call.
    if (!QDir(tombstone).removeRecursively())
        return fail(error, QStringLiteral("Package withdrawn; resource cleanup requires retry"));
    if (removeData)
        QFile::remove(QDir(m_root).filePath(QStringLiteral("retained-settings/") + id + QStringLiteral(".json")));
    return true;
}
bool PackageStore::settings(const QString& id, QJsonObject* output, quint64* revision, QString* error) const
{
    QJsonObject state;
    if (!loadState(id, &state, error)) return false;
    const auto current = state.value("current").toObject();
    if (output) *output = current.value("settings").toObject();
    if (revision) *revision = quint64(current.value("settingsRevision").toDouble());
    return true;
}
bool PackageStore::saveSettings(const QString& id, const QJsonObject& settings, quint64 revision, QString* error)
{
    if (QJsonDocument(settings).toJson(QJsonDocument::Compact).size() > 256 * 1024)
        return fail(error, QStringLiteral("Settings exceed size budget"));
    QLockFile lock(QDir(m_root).filePath(QStringLiteral("store.lock")));
    if (!lock.tryLock(0)) return fail(error, QStringLiteral("Add-on store is busy"));
    QJsonObject state;
    if (!loadState(id, &state, error)) return false;
    auto current = state.value("current").toObject();
    if (quint64(current.value("settingsRevision").toDouble()) != revision)
        return fail(error, QStringLiteral("Settings changed; reload before saving"));
    InstalledPackage package;
    if (!installed(id, &package, error)) return false;
    QSet<QString> secretFields;
    for (const auto& binding : package.manifest.permissionScope(QStringLiteral("credentials.use")).value("bindings").toArray())
        secretFields.insert(binding.toObject().value("field").toString());
    if (!package.manifest.settingsSchema.isEmpty()) {
        QFile schema(QDir(package.directory).filePath(package.manifest.settingsSchema));
        if (!schema.open(QIODevice::ReadOnly)) return fail(error, QStringLiteral("Cannot validate protected settings fields"));
        const auto properties = QJsonDocument::fromJson(schema.readAll()).object().value("properties").toObject();
        for (auto it = properties.begin(); it != properties.end(); ++it)
            if (it.value().toObject().value("x-vlc-secret").toBool()) secretFields.insert(it.key());
    }
    if (settings.contains("_credentials")) {
        if (!settings.value("_credentials").isObject())
            return fail(error, QStringLiteral("Invalid host credential reference envelope"));
        const auto references = settings.value("_credentials").toObject();
        static const QRegularExpression uuid(QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
        for (auto it = references.begin(); it != references.end(); ++it)
            if (!secretFields.contains(it.key()) || !it.value().isString()
                || !uuid.match(it.value().toString()).hasMatch())
                return fail(error, QStringLiteral("Invalid protected credential reference"));
    }
    std::function<bool(const QJsonValue&)> containsSecret = [&](const QJsonValue& value) {
        if (value.isObject()) {
            const auto object = value.toObject();
            for (auto it = object.begin(); it != object.end(); ++it)
                if (secretFields.contains(it.key()) || containsSecret(it.value())) return true;
        } else if (value.isArray()) {
            for (const auto& item : value.toArray()) if (containsSecret(item)) return true;
        }
        return false;
    };
    auto ordinary = settings;
    ordinary.remove("_credentials");
    if (containsSecret(ordinary))
        return fail(error, QStringLiteral("Secrets cannot be persisted in ordinary add-on settings"));
    current.insert("settings", settings);
    current.insert("settingsRevision", double(revision) + 1);
    state.insert("current", current);
    return saveState(id, state, error);
}
} // namespace vlc::addons
