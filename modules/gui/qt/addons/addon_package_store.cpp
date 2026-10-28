// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_package_store.hpp"
#include "addon_settings_migrations.hpp"
#include "addon_storage.hpp"
#include "addon_storage_data.hpp"
#include <QCryptographicHash>
#include <cmath>
#include "addon_repository_updates.hpp"
#include "addon_repository_cancellation.hpp"
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
#include <filesystem>
#include <optional>
#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#elif defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#include <fcntl.h>
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
            {"developerSource", identity.developerSource}, {"distributionSource", identity.distributionSource}};
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
    result.distributionSource = value.value("distributionSource").toBool();
    return result;
}
bool writeJson(const QString& path, const QJsonObject& value, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return fail(error, QStringLiteral("The app cannot create the add-on state file."));
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size() || !file.commit())
        return fail(error, QStringLiteral("The app cannot save the add-on state."));
    return true;
}
bool readJson(const QString& path, QJsonObject* value, QString* error)
{
    QFile file(path);
    if (QFileInfo(file).isSymLink() || !file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024)
        return fail(error, QStringLiteral("The app cannot read the add-on state."));
    QJsonParseError parseError;
    const auto result = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !result.isObject())
        return fail(error, QStringLiteral("The add-on state is not correct."));
    *value = result.object();
    return true;
}
QString currentBuiltinId(const QString& id)
{
    const auto legacy = QLatin1String("vlc.builtin.");
    return id.startsWith(legacy) ? QString(QStringLiteral("hakone.builtin.") + id.mid(legacy.size())) : id;
}
QJsonObject currentBuiltinPreferences(const QJsonObject& saved)
{
    auto result = saved;
    if (saved.value(QStringLiteral("_builtinEnabled")).isObject()) {
        const auto previous = saved.value(QStringLiteral("_builtinEnabled")).toObject();
        auto enabled = previous;
        for (auto it = previous.begin(); it != previous.end(); ++it) {
            const auto id = currentBuiltinId(it.key());
            if (id == it.key()) continue;
            if (!previous.contains(id)) enabled.insert(id, it.value());
            enabled.remove(it.key());
        }
        result.insert(QStringLiteral("_builtinEnabled"), enabled);
    }
    for (auto it = saved.begin(); it != saved.end(); ++it) {
        if (it.key().startsWith(QLatin1Char('_')) || !it.value().isArray()) continue;
        const auto previous = it.value().toArray();
        QSet<QString> explicitIds, seen;
        for (const auto& entry : previous) if (entry.isString()) explicitIds.insert(entry.toString());
        QJsonArray order;
        for (const auto& entry : previous) {
            if (!entry.isString()) { order.append(entry); continue; }
            const auto raw = entry.toString(), id = currentBuiltinId(raw);
            if (raw != id && explicitIds.contains(id)) continue;
            if (id.startsWith(QLatin1String("hakone.builtin.")) && seen.contains(id)) continue;
            seen.insert(id); order.append(id);
        }
        result.insert(it.key(), order);
    }
    return result;
}
bool providerFeature(const QString& feature)
{
    return !feature.isEmpty() && feature.size() <= 128 && !feature.startsWith(QLatin1Char('_'));
}
bool providerIds(const QJsonArray& ids)
{
    if (ids.size() > 128) return false;
    QSet<QString> seen;
    for (const auto& value : ids) {
        const auto id = value.toString();
        if (!value.isString() || id.isEmpty() || id.size() > 256 || seen.contains(id)) return false;
        seen.insert(id);
    }
    return true;
}
bool providerPreferencesValid(const QJsonObject& values, QString* error)
{
    if (values.size() > 256 || QJsonDocument(values).toJson(QJsonDocument::Compact).size() > 256 * 1024)
        return fail(error, QStringLiteral("The provider settings exceed the host limits."));
    for (auto it = values.begin(); it != values.end(); ++it) {
        if (it.key() == QStringLiteral("_builtinEnabled")) {
            if (!it.value().isObject() || it.value().toObject().size() > 256)
                return fail(error, QStringLiteral("The provider settings are not correct."));
            const auto enabled = it.value().toObject();
            for (auto value = enabled.begin(); value != enabled.end(); ++value)
                if (!packageId(value.key()) || !value.value().isBool())
                    return fail(error, QStringLiteral("The provider settings are not correct."));
        } else if (!providerFeature(it.key()) || !it.value().isArray() || !providerIds(it.value().toArray()))
            return fail(error, QStringLiteral("The provider settings are not correct."));
    }
    return true;
}
bool catalogueFile(const QString&, qint64, QByteArray*, QString*);
bool readProviderPreferences(const QString& root, QJsonObject* values, QString* error)
{
    const auto path = QDir(root).filePath(QStringLiteral("provider-order.json"));
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink()) { *values = {}; return true; }
    QByteArray bytes;
    if (!catalogueFile(path, 256 * 1024, &bytes, error)) return false;
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(error, QStringLiteral("The provider settings are not correct."));
    const auto raw = document.object();
    // Bound raw rows before alias normalization can remove duplicates.
    if (raw.size() > 256) return fail(error, QStringLiteral("The provider settings exceed the host limits."));
    for (auto it = raw.begin(); it != raw.end(); ++it) {
        if ((it.value().isArray() && it.value().toArray().size() > 128)
            || (it.value().isObject() && it.value().toObject().size() > 256))
            return fail(error, QStringLiteral("The provider settings exceed the host limits."));
    }
    auto current = currentBuiltinPreferences(raw);
    if (!providerPreferencesValid(current, error)) return false;
    *values = std::move(current); return true;
}
bool catalogueFile(const QString& path, qint64 limit, QByteArray* output, QString* error)
{
    const QFileInfo info(path);
    if (info.isSymLink() || !info.isFile() || info.size() < 0 || info.size() > limit)
        return fail(error, QStringLiteral("The metadata catalogue file is missing or too large."));
    QFile file(path);
#ifdef Q_OS_UNIX
    const auto descriptor = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (descriptor < 0)
        return fail(error, QStringLiteral("The app cannot open the metadata catalogue file."));
    struct stat status;
    if (::fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode) || status.st_size < 0 || status.st_size > limit) {
        ::close(descriptor);
        return fail(error, QStringLiteral("The metadata catalogue file is not a bounded regular file."));
    }
    if (!file.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(descriptor);
        return fail(error, QStringLiteral("The app cannot read the metadata catalogue file."));
    }
#elif defined(Q_OS_WIN)
    const auto handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    BY_HANDLE_FILE_INFORMATION status;
    if (handle == INVALID_HANDLE_VALUE)
        return fail(error, QStringLiteral("The app cannot open the metadata catalogue file."));
    if (GetFileType(handle) != FILE_TYPE_DISK || !GetFileInformationByHandle(handle, &status)
        || (status.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        || status.nFileSizeHigh != 0 || status.nFileSizeLow > quint64(limit)) {
        CloseHandle(handle);
        return fail(error, QStringLiteral("The metadata catalogue file is not a bounded regular file."));
    }
    const auto descriptor = _open_osfhandle(reinterpret_cast<intptr_t>(handle), _O_RDONLY | _O_BINARY);
    if (descriptor < 0) {
        CloseHandle(handle);
        return fail(error, QStringLiteral("The app cannot read the metadata catalogue file."));
    }
    if (!file.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        _close(descriptor);
        return fail(error, QStringLiteral("The app cannot read the metadata catalogue file."));
    }
#else
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("The app cannot read the metadata catalogue file."));
#endif
    auto bytes = file.read(limit + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > limit
        || bytes.size() != file.size() || !file.atEnd())
        return fail(error, QStringLiteral("The metadata catalogue file changed or cannot be read."));
    *output = std::move(bytes); return true;
}
bool catalogueObject(const QString& path, qint64 limit, QJsonObject* output, QString* error)
{
    QByteArray bytes;
    if (!catalogueFile(path, limit, &bytes, error)) return false;
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
        return fail(error, QStringLiteral("The metadata catalogue document is not correct."));
    *output = document.object(); return true;
}
bool catalogueInteger(const QJsonValue& value, quint64* output, bool positive = false)
{
    const auto number = value.toDouble(-1);
    if (!value.isDouble() || !std::isfinite(number) || number < (positive ? 1 : 0)
        || number > 9007199254740991. || std::floor(number) != number) return false;
    *output = quint64(number); return true;
}
bool catalogueDirectory(const QString& path, bool missing, QString* error)
{
    const QFileInfo info(path);
    if (missing && !info.exists() && !info.isSymLink()) return true;
    return (!info.isSymLink() && info.isDir() && info.isReadable())
        || fail(error, QStringLiteral("The metadata catalogue directory is not correct."));
}
bool catalogueCandidates(const QString& root, QStringList* output, QString* error)
{
    if (!catalogueDirectory(root, true, error)) return false;
    if (!QFileInfo::exists(root)) return true;
    namespace fs = std::filesystem;
#ifdef Q_OS_WIN
    const fs::path path(root.toStdWString());
#else
    const fs::path path(QFile::encodeName(root).constData());
#endif
    std::error_code code;
    fs::directory_iterator entry(path, code), end;
    if (code) return fail(error, QStringLiteral("The app cannot read the metadata catalogue directory."));
    while (entry != end) {
        if (output->size() >= MetadataCatalogueSnapshot::MaximumCandidates)
            return fail(error, QStringLiteral("The metadata catalogue has too many packages."));
        const auto status = entry->symlink_status(code);
        if (code || !fs::is_directory(status) || fs::is_symlink(status))
            return fail(error, QStringLiteral("A metadata catalogue entry is not correct."));
#ifdef Q_OS_WIN
        const auto id = QString::fromStdWString(entry->path().filename().native());
#else
        const auto id = QFile::decodeName(entry->path().filename().native().c_str());
#endif
        if (!packageId(id)) return fail(error, QStringLiteral("A metadata catalogue package ID is not correct."));
        output->append(id);
        entry.increment(code);
        if (code) return fail(error, QStringLiteral("The app cannot read the complete metadata catalogue."));
    }
    output->sort(); return true;
}
bool catalogueContents(const QString& directory, Manifest* manifest,
                       QMap<QString,QByteArray>* contents, QString* error)
{
    if (!catalogueDirectory(directory, false, error)) return false;
    QByteArray bytes;
    if (!catalogueFile(QDir(directory).filePath(QStringLiteral("addon.json")), Manifest::MaximumManifestBytes, &bytes, error)
        || !Manifest::parse(bytes, manifest, error)) return false;
    auto prefix = QFileInfo(directory).canonicalFilePath();
    if (prefix.isEmpty()) return fail(error, QStringLiteral("The package directory is not available."));
    // Qt uses '/' in canonical paths on each platform.
    if (!prefix.endsWith(QLatin1Char('/'))) prefix += QLatin1Char('/');
    qint64 total = 0;
    for (const auto& path : manifest->files) {
        auto checked = directory;
        for (const auto& part : path.split(QLatin1Char('/'))) {
            checked = QDir(checked).filePath(part);
            if (QFileInfo(checked).isSymLink())
                return fail(error, QStringLiteral("A metadata package resource is a symbolic link."));
        }
        if (!QFileInfo(checked).canonicalFilePath().startsWith(prefix)
            || !catalogueFile(checked, Manifest::MaximumFileBytes, &bytes, error)
            || (total += bytes.size()) > Manifest::MaximumPackageBytes)
            return fail(error, QStringLiteral("A metadata package resource is missing or too large."));
        contents->insert(path, bytes);
    }
    return Manifest::validateContents(*manifest, *contents, error);
}
bool subset(const QJsonValue& requested, const QJsonValue& declared)
{
    if (requested.type() != declared.type()) return false;
    if (requested.isObject()) {
        const auto a = requested.toObject(), b = declared.toObject();
        for (auto it = a.begin(); it != a.end(); ++it) {
            if (it.key() == QStringLiteral("quotaBytes") && it.value().isDouble() && b.value(it.key()).isDouble()) {
                const auto value = it.value().toDouble(-1);
                if (!std::isfinite(value) || value < 0 || std::floor(value) != value || value > b.value(it.key()).toDouble(-1)) return false;
            } else if (!b.contains(it.key()) || !subset(it.value(), b.value(it.key()))) return false;
        }
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
        if (!grants.contains(it.key()) || (it.key() == QStringLiteral("storage.private")
            ? !grants.value(it.key()).toObject().contains("quotaBytes") || !subset(grants.value(it.key()), it.value())
            : grants.value(it.key()) != it.value()))
            return fail(error, QStringLiteral("The required permission is not approved: ") + it.key());
    for (auto it = grants.begin(); it != grants.end(); ++it)
        if (!manifest.permissionDeclared(it.key()) || !subset(it.value(), manifest.permissionScope(it.key())))
            return fail(error, QStringLiteral("The grant is outside the declared permission: ") + it.key());
    return true;
}
qint64 storageQuota(const Manifest& manifest, const QJsonObject& grants)
{
    const auto declared = manifest.permissionScope(QStringLiteral("storage.private")).value("quotaBytes");
    const auto approved = grants.value("storage.private").toObject().value("quotaBytes");
    if (!manifest.permissionDeclared(QStringLiteral("storage.private")) || !declared.isDouble() || !approved.isDouble()) return -1;
    const auto a = declared.toDouble(-1), b = approved.toDouble(-1);
    if (!std::isfinite(a) || !std::isfinite(b) || a < 0 || b < 0 || std::floor(a) != a || std::floor(b) != b) return -1;
    return qint64(std::min({a, b, double(PrivateStorageLimits::MaximumBytes)}));
}
bool storageSnapshot(const QString& directory, const QJsonObject& release, const Manifest& manifest,
                     QJsonObject* output, QString* error)
{
    if (!release.contains("storage")) { *output = emptyPrivateStorage(privateStorageVersion(manifest)); return true; }
    const auto descriptor = release.value("storage").toObject();
    const double revision = descriptor.value("revision").toDouble(-1);
    if (descriptor.size() != 3 || !descriptor.value("version").isDouble() || descriptor.value("version").toInt() < 1
        || !descriptor.value("revision").isDouble() || revision < 0 || revision > 9007199254740991.0 || std::floor(revision) != revision
        || !readPrivateStorage(directory, descriptor.value("digest").toString(), output, error)
        || output->value("schemaVersion") != descriptor.value("version")
        || (manifest.permissionDeclared(QStringLiteral("storage.private")) && descriptor.value("version").toInt() != privateStorageVersion(manifest)))
        return fail(error, QStringLiteral("The private storage snapshot does not agree with the release."));
    return true;
}
QJsonArray retainSnapshot(QJsonArray snapshots, const QJsonObject& snapshot)
{
    if (snapshot.isEmpty()) return snapshots;
    for (qsizetype index = snapshots.size(); index > 0; --index)
        if (snapshots.at(index - 1) == snapshot) snapshots.removeAt(index - 1);
    snapshots.append(snapshot);
    while (snapshots.size() > 4) snapshots.removeFirst();
    return snapshots;
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
            return fail(error, QStringLiteral("The Git tool is not available. Install the packaged Git tool."));
        if (!m_temporary.isValid())
            return fail(error, QStringLiteral("The app cannot create a private Git directory."));
        m_repo = QDir(m_temporary.path()).filePath(QStringLiteral("objects.git"));
        return true;
    }
    bool initializeRepository(const QString& format, QString* error)
    {
        if (format != QLatin1String("sha1") && format != QLatin1String("sha256"))
            return fail(error, QStringLiteral("The Git object format is not supported."));
        m_format = format;
        QStringList arguments{QStringLiteral("init"), QStringLiteral("--bare"), QStringLiteral("--template=")};
        // SHA-1 keeps the existing command for supported tools without SHA-256.
        // SHA-256 must succeed explicitly. Never retry with another format.
        if (format == QLatin1String("sha256")) arguments.append(QStringLiteral("--object-format=sha256"));
        arguments.append(m_repo);
        return run(arguments, nullptr, error, false);
    }
    bool run(QStringList arguments, QByteArray* output, QString* error, bool repository = true,
             qint64 maximumOutput = Manifest::MaximumPackageBytes)
    {
        if (RepositoryCancellation::requested() || QThread::currentThread()->isInterruptionRequested())
            return fail(error, QStringLiteral("The Git request was canceled."));
        const auto remaining = m_timeout - m_elapsed.elapsed();
        if (remaining <= 0) return fail(error, QStringLiteral("The Git request reached its time limit."));
        QProcess process;
        QProcessEnvironment environment;
        const auto inherited = QProcessEnvironment::systemEnvironment();
        for (const auto& name : {QStringLiteral("PATH"), QStringLiteral("SystemRoot"),
                QStringLiteral("WINDIR"), QStringLiteral("TEMP"), QStringLiteral("TMP"),
                QStringLiteral("SSL_CERT_FILE"), QStringLiteral("SSL_CERT_DIR"),
                QStringLiteral("GIT_SSL_CAINFO"),
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
        if (!m_format.isEmpty()) environment.insert(QStringLiteral("GIT_DEFAULT_HASH"), m_format);
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
        if (!process.waitForStarted(int(std::min<qint64>(3000, remaining))))
            return fail(error, QStringLiteral("The app cannot start the Git tool."));
        QByteArray data, diagnostic;
        while (process.state() != QProcess::NotRunning) {
            process.waitForFinished(25);
            data += process.readAllStandardOutput();
            diagnostic += process.readAllStandardError();
            const bool interrupted = RepositoryCancellation::requested() || QThread::currentThread()->isInterruptionRequested();
            if (interrupted || data.size() > maximumOutput || diagnostic.size() > 256 * 1024
                || m_elapsed.elapsed() > m_timeout || directoryBytes(m_temporary.path()) > 128 * 1024 * 1024) {
#ifdef Q_OS_UNIX
                const auto pid = process.processId();
                if (pid > 0) ::kill(-pid, SIGKILL);
#endif
                process.kill();
                process.waitForFinished(3000);
                return fail(error, interrupted ? QStringLiteral("The Git request was canceled.")
                    : QStringLiteral("The Git request reached its time or size limit."));
            }
        }
        data += process.readAllStandardOutput();
        diagnostic += process.readAllStandardError();
        if (RepositoryCancellation::requested() || QThread::currentThread()->isInterruptionRequested())
            return fail(error, QStringLiteral("The Git request was canceled."));
        if (data.size() > maximumOutput || diagnostic.size() > 256 * 1024
            || m_elapsed.elapsed() > m_timeout)
            return fail(error, QStringLiteral("The Git request reached its time or size limit."));
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
            return fail(error, QStringLiteral("The Git request failed. Examine the repository access and tag."));
        if (output) *output = data;
        return true;
    }
    bool developer = false;
    bool withinBudget(QString* error) const
    {
        return m_elapsed.elapsed() <= m_timeout
            || fail(error, QStringLiteral("The Git request reached its time limit."));
    }
private:
    QString m_executable, m_repo, m_format;
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
        return fail(error, QStringLiteral("The candidate manifest changed after its check."));
    if (candidate.contents.size() != parsed.files.size())
        return fail(error, QStringLiteral("The candidate file list does not agree with its manifest."));
    qint64 total = 0;
    for (const auto& path : parsed.files) {
        if (!candidate.contents.contains(path)) return fail(error, QStringLiteral("A candidate resource is missing."));
        total += candidate.contents.value(path).size();
        if (candidate.contents.value(path).size() > Manifest::MaximumFileBytes || total > Manifest::MaximumPackageBytes)
            return fail(error, QStringLiteral("The candidate is too large."));
    }
    if (candidate.digest != packageDigest(candidate.contents) || !digestId(QString::fromLatin1(candidate.digest)))
        return fail(error, QStringLiteral("The candidate failed its integrity check."));
    if (!candidate.identity.developerSource && QUrl(candidate.identity.source) != parsed.source)
        return fail(error, QStringLiteral("The manifest source does not agree with the approved repository."));
    QString tag = candidate.identity.tag;
    if (tag.startsWith(QLatin1Char('v'))) tag.remove(0, 1);
    if (!SemanticVersion::parse(tag, nullptr) || tag != parsed.version)
        return fail(error, QStringLiteral("The release tag and manifest version do not agree."));
    if (candidate.identity.developerSource && candidate.identity.distributionSource)
        return fail(error, QStringLiteral("A distribution package cannot be a developer snapshot."));
    if (candidate.identity.distributionSource && candidate.identity.objectFormat != QStringLiteral("distribution-sha256"))
        return fail(error, QStringLiteral("The distribution snapshot format is not correct."));
    if ((candidate.identity.developerSource && candidate.identity.objectFormat == QStringLiteral("developer-sha256"))
        || (candidate.identity.distributionSource && candidate.identity.objectFormat == QStringLiteral("distribution-sha256"))) {
        if (candidate.identity.commitId != QString::fromLatin1(candidate.digest)
            || candidate.identity.tagObjectId != QString::fromLatin1(candidate.digest))
            return fail(error, QStringLiteral("The snapshot identity does not agree with its contents."));
    } else {
        if (!gitId(candidate.identity.commitId) || !gitId(candidate.identity.tagObjectId)
            || candidate.identity.commitId.size() != candidate.identity.tagObjectId.size())
            return fail(error, QStringLiteral("The release needs resolved tag and commit IDs."));
        if (candidate.identity.objectFormat != (candidate.identity.commitId.size() == 40
                ? QStringLiteral("sha1") : QStringLiteral("sha256")))
            return fail(error, QStringLiteral("The Git object format does not agree with its identity."));
    }
    return true;
}
} // namespace

PackageStore::PackageStore(QString root) : m_root(QDir::cleanPath(std::move(root))), m_authority(m_root) {}
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
    if ((candidate.identity.developerSource || candidate.identity.distributionSource) && candidate.identity.commitId.isEmpty()
        && candidate.identity.tagObjectId.isEmpty()) {
        candidate.identity.objectFormat = candidate.identity.distributionSource
            ? QStringLiteral("distribution-sha256") : QStringLiteral("developer-sha256");
        candidate.identity.commitId = candidate.identity.tagObjectId = QString::fromLatin1(candidate.digest);
    }
    if (!validateCandidate(candidate, error)) return false;
    if (output) *output = candidate;
    return true;
}
bool PackageStore::fetchGitTag(const QString& source, const QString& tag, PackageCandidate* output,
                              QString* error, bool developerLocalSource, int timeoutMs)
{
    if (output) *output = {};
    QString normalized = tag;
    if (normalized.startsWith(QLatin1Char('v'))) normalized.remove(0, 1);
    if (!SemanticVersion::parse(normalized, nullptr) || tag.contains(QLatin1Char('/'))
        || tag.startsWith(QLatin1Char('-')))
        return fail(error, QStringLiteral("Select an exact SemVer release tag."));
    const QUrl url(source, QUrl::StrictMode);
    if (!developerLocalSource && (url.scheme() != QStringLiteral("https") || url.host().isEmpty()
        || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment()))
        return fail(error, QStringLiteral("Use an HTTPS Git source."));
    if (developerLocalSource && (!QFileInfo(source).isDir() || QFileInfo(source).isSymLink()))
        return fail(error, QStringLiteral("The developer source must be an existing local repository."));
    GitObjects git(std::clamp(timeoutMs, 1000, 120000));
    git.developer = developerLocalSource;
    if (!git.initialize(error)) return false;
    const QString ref = QStringLiteral("refs/tags/") + tag;
    QByteArray advertisement;
    if (!git.run({QStringLiteral("ls-remote"), QStringLiteral("--tags"), QStringLiteral("--refs"),
                  QStringLiteral("--"), source, ref}, &advertisement, error, false, 8192)) return false;
    QList<RepositoryTag> advertised;
    if (!parseRepositoryTags(advertisement, &advertised, error)) return false;
    if (advertised.size() != 1 || advertised.first().name != tag)
        return fail(error, QStringLiteral("The repository did not advertise the exact release tag."));
    const auto advertisedId = advertised.first().objectId;
    const auto objectFormat = advertisedId.size() == 64 ? QStringLiteral("sha256") : QStringLiteral("sha1");
    if (!git.initializeRepository(objectFormat, error)) return false;
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
    if (candidate.identity.tagObjectId != advertisedId)
        return fail(error, QStringLiteral("The release tag changed during package preparation. Select the tag again."));
    if (!git.run({QStringLiteral("rev-parse"), QStringLiteral("--verify"), ref + QStringLiteral("^{commit}")},
                 &value, error)) return false;
    candidate.identity.commitId = QString::fromLatin1(value.trimmed());
    candidate.identity.objectFormat = objectFormat;
    if (!gitId(candidate.identity.tagObjectId) || !gitId(candidate.identity.commitId)
        || candidate.identity.commitId.size() != advertisedId.size())
        return fail(error, QStringLiteral("The resolved Git identity is not correct."));
    if (!git.run({QStringLiteral("ls-tree"), QStringLiteral("-rz"), QStringLiteral("--full-tree"),
                  candidate.identity.commitId}, &value, error)) return false;
    QMap<QString, QString> objects;
    QSet<QString> folded;
    int count = 0;
    for (const auto& entry : value.split('\0')) {
        if (entry.isEmpty()) continue;
        if (++count > 4096) return fail(error, QStringLiteral("The repository tree has too many entries."));
        const auto tab = entry.indexOf('\t');
        const auto header = entry.left(tab).split(' ');
        const auto encodedPath = entry.mid(tab + 1);
        const auto path = QString::fromUtf8(encodedPath);
        if (tab < 0 || header.size() != 3 || path.toUtf8() != encodedPath || !portablePackagePath(path)
            || folded.contains(path.toCaseFolded()))
            return fail(error, QStringLiteral("The repository tree path is not permitted."));
        folded.insert(path.toCaseFolded());
        if (header[0] != "100644" && header[0] != "100755")
            return fail(error, QStringLiteral("The repository contains links or submodules."));
        if (header[1] != "blob" || !gitId(QString::fromLatin1(header[2])) || header[2].size() != advertisedId.size())
            return fail(error, QStringLiteral("The git tree object is not correct."));
        objects.insert(path, QString::fromLatin1(header[2]));
    }
    if (!objects.contains(QStringLiteral("addon.json")))
        return fail(error, QStringLiteral("The tag does not contain an add-on manifest."));
    if (!git.run({QStringLiteral("cat-file"), QStringLiteral("blob"), objects.value(QStringLiteral("addon.json"))},
                 &value, error) || !Manifest::parse(value, &candidate.manifest, error)) return false;
    qint64 total = 0;
    for (const auto& path : candidate.manifest.files) {
        if (!objects.contains(path)) return fail(error, QStringLiteral("A declared resource is missing from the tag."));
        QByteArray size;
        if (!git.run({QStringLiteral("cat-file"), QStringLiteral("-s"), objects.value(path)}, &size, error)) return false;
        bool ok = false;
        const auto bytes = size.trimmed().toLongLong(&ok);
        if (!ok || bytes < 0 || bytes > Manifest::MaximumFileBytes || (total += bytes) > Manifest::MaximumPackageBytes)
            return fail(error, QStringLiteral("The Git package is too large."));
        if (!git.run({QStringLiteral("cat-file"), QStringLiteral("blob"), objects.value(path)}, &value, error)
            || value.size() != bytes) return fail(error, QStringLiteral("The app cannot read the package Git object."));
        candidate.contents.insert(path, value);
    }
    candidate.digest = packageDigest(candidate.contents);
    if (!validateCandidate(candidate, error)) return false;
    QTemporaryDir check;
    if (!check.isValid()) return fail(error, QStringLiteral("The app cannot examine the staged package."));
    for (auto it = candidate.contents.begin(); it != candidate.contents.end(); ++it) {
        const auto destination = QDir(check.path()).filePath(it.key());
        QDir().mkpath(QFileInfo(destination).absolutePath());
        QFile file(destination);
        if (!file.open(QIODevice::WriteOnly) || file.write(it.value()) != it.value().size())
            return fail(error, QStringLiteral("The app cannot stage the package check."));
    }
    Manifest validated;
    if (!Manifest::loadDirectory(check.path(), &validated, nullptr, error) || !git.withinBudget(error)) return false;
    if (output) *output = candidate;
    return true;
}
bool PackageStore::fetchRepositoryUpdate(const QString& id, const QString& range, bool allowPrerelease,
                                        PackageCandidate* output, QString* error,
                                        bool developerLocalSource, int timeoutMs) const
{
    if (output) *output = {};
    InstalledPackage active;
    if (!installed(id, &active, error)) return false;
    RepositoryTag rangeCheck;
    if (!selectRepositoryUpdate({}, active.manifest.version, range, allowPrerelease, &rangeCheck, error)) return false;
    if (active.recoveryRequired) return fail(error, QStringLiteral("Restore the package before an update check."));
    if (active.identity.developerSource && !developerLocalSource)
        return fail(error, QStringLiteral("Developer packages use explicit release selection."));
    const auto source = active.identity.source;
    const QUrl url(source, QUrl::StrictMode);
    if (!developerLocalSource && (url.scheme() != QStringLiteral("https") || url.host().isEmpty()
        || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment()))
        return fail(error, QStringLiteral("Use an HTTPS Git source."));
    if (developerLocalSource && (!QFileInfo(source).isDir() || QFileInfo(source).isSymLink()))
        return fail(error, QStringLiteral("The developer repository directory is not available."));
    QElapsedTimer elapsed; elapsed.start();
    const int budget = std::clamp(timeoutMs, 1000, 120000);
    GitObjects git(budget); git.developer = developerLocalSource;
    QByteArray advertisement;
    if (!git.initialize(error) || !git.run({QStringLiteral("ls-remote"), QStringLiteral("--tags"),
        QStringLiteral("--refs"), QStringLiteral("--"), source}, &advertisement, error, false, 1024 * 1024)) return false;
    QList<RepositoryTag> tags; RepositoryTag selected;
    if (!parseRepositoryTags(advertisement, &tags, error)
        || !selectRepositoryUpdate(tags, active.manifest.version, range, allowPrerelease, &selected, error)) return false;
    if (selected.name.isEmpty()) return true;
    const int remaining = budget - int(elapsed.elapsed());
    if (remaining < 1000) return fail(error, QStringLiteral("The repository update check reached its time limit."));
    PackageCandidate candidate;
    if (!fetchGitTag(source, selected.name, &candidate, error, developerLocalSource, remaining)) return false;
    if (candidate.identity.tagObjectId != selected.objectId)
        return fail(error, QStringLiteral("The release tag changed during the update check. Select a new release tag."));
    if (candidate.manifest.id != id)
        return fail(error, QStringLiteral("The release has a different package ID."));
    QJsonObject state;
    if (!loadState(id, &state, error)) return false;
    const auto current = state.value("current").toObject();
    if (current.value("identity").toObject().value("source") != source)
        return fail(error, QStringLiteral("The installed source changed during the update check."));
    SemanticVersion currentVersion;
    if (!SemanticVersion::parse(current.value("version").toString(), &currentVersion))
        return fail(error, QStringLiteral("The installed version is not correct."));
    if (selected.parsedVersion.compare(currentVersion) <= 0) return true;
    const auto locked = state.value("lockedTags").toObject().value(selected.name).toObject();
    if ((!locked.isEmpty() && (locked.value("commitId") != candidate.identity.commitId
            || locked.value("tagObjectId") != candidate.identity.tagObjectId
            || locked.value("digest") != QString::fromLatin1(candidate.digest)))
        || (state.value("lockedVersions").toObject().contains(candidate.manifest.version)
            && state.value("lockedVersions").toObject().value(candidate.manifest.version) != QString::fromLatin1(candidate.digest)))
        return fail(error, QStringLiteral("The selected release changed its locked identity or contents."));
    if (output) *output = candidate;
    if (error) error->clear();
    return true;
}

bool PackageStore::loadState(const QString& id, QJsonObject* output, QString* error) const
{
    if (!packageId(id)) return fail(error, QStringLiteral("The package ID is not correct."));
    const auto path = QDir(m_root).filePath(QStringLiteral("packages/") + id + QStringLiteral("/state.json"));
    QJsonObject state;
    const auto valid = [&id](const QJsonObject& value) {
        return value.value("formatVersion") == 1 && value.value("id") == id && value.value("current").isObject();
    };
    if (!readJson(path, &state, nullptr) || !valid(state)) {
        if (!readJson(path + QStringLiteral(".backup"), &state, error) || !valid(state))
            return fail(error, QStringLiteral("The package state and backup are not correct."));
        state.insert("recoveryRequired", true);
        state.insert("enabled", false);
    }
    *output = state;
    return true;
}
bool PackageStore::saveState(const QString& id, const QJsonObject& state, QString* error, bool authority) const
{
    if (!packageId(id)) return fail(error, QStringLiteral("The package ID is not correct."));
    const auto folder = QDir(m_root).filePath(QStringLiteral("packages/") + id);
    if (!QDir().mkpath(folder)) return fail(error, QStringLiteral("The app cannot create the package state directory."));
    if (QJsonDocument(state).toJson(QJsonDocument::Compact).size() > 4 * 1024 * 1024)
        return fail(error, QStringLiteral("The retained package state is too large."));
    const auto path = QDir(folder).filePath(QStringLiteral("state.json"));
    auto mutation = authority ? std::optional<StoreAuthorityMutation>(m_authority.mutation()) : std::nullopt;
    QJsonObject previous;
    if (QFileInfo::exists(path) && readJson(path, &previous, nullptr)
        && previous.value("id") == id && previous.value("current").isObject()
        && !writeJson(path + QStringLiteral(".backup"), previous, error)) return false;
    return writeJson(path, state, error);
}
QString PackageStore::privateStorageDirectory(const QString& id) const
{ return QDir(m_root).filePath(QStringLiteral("private-storage/") + id + QStringLiteral("/objects")); }
bool PackageStore::collectPrivateStorage(const QString& id, const QJsonObject& state, QString* error) const
{
    QSet<QString> keep;
    const auto snapshot = [&keep](const QJsonObject& value) {
        const auto hash = value.value("storage").toObject().value("digest").toString();
        if (!hash.isEmpty()) keep.insert(hash);
    };
    const auto document = [&snapshot](const QJsonObject& value) {
        snapshot(value.value("current").toObject()); snapshot(value.value("previous").toObject());
        for (const auto& item : value.value("retained").toArray()) snapshot(item.toObject());
    };
    document(state);
    const auto backupPath = QDir(m_root).filePath(QStringLiteral("packages/") + id + QStringLiteral("/state.json.backup"));
    QJsonObject backup;
    if (QFileInfo::exists(backupPath)) { if (!readJson(backupPath, &backup, error)) return false; document(backup); }
    const auto archivePath = QDir(privateStorageDirectory(id)).filePath(QStringLiteral("../archive.json"));
    if (QFileInfo::exists(archivePath)) { if (!readJson(archivePath, &backup, error)) return false; snapshot(backup); }
    if (keep.size() > PrivateStorageLimits::MaximumRetainedSnapshots)
        return fail(error, QStringLiteral("The retained private storage limit was reached."));
    for (const auto& hash : keep) if (!digestId(hash)) return fail(error, QStringLiteral("A private storage digest is not correct."));
    const QDir directory(privateStorageDirectory(id));
    if (!directory.exists()) return true;
    if (QFileInfo(directory.path()).isSymLink()) return fail(error, QStringLiteral("The private storage directory is not correct."));
    QDirIterator files(directory.path(), QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
    const QRegularExpression temporary(QStringLiteral("\\A\\.?[0-9a-f]{64}\\.json\\.[A-Za-z0-9]{6}\\z"));
    int count = 0; qint64 physicalBytes = 0;
    while (files.hasNext()) {
        files.next(); const auto info = files.fileInfo(); const auto name = info.fileName();
        const bool staged = temporary.match(name).hasMatch();
        if (++count > PrivateStorageLimits::MaximumPhysicalSnapshots || info.isSymLink() || !info.isFile()
            || (!staged && (!name.endsWith(QStringLiteral(".json")) || !digestId(name.chopped(5)))))
            return fail(error, QStringLiteral("The private storage files are not correct."));
        if (info.size() > PrivateStorageLimits::MaximumBytes + 256)
            return fail(error, QStringLiteral("A private storage snapshot is too large."));
        physicalBytes += info.size();
        if (physicalBytes > PrivateStorageLimits::MaximumPhysicalSnapshots * (PrivateStorageLimits::MaximumBytes + 256))
            return fail(error, QStringLiteral("The private storage file byte limit was reached."));
        if (staged || !keep.contains(name.chopped(5))) {
            if (!QFile::remove(info.absoluteFilePath())) return fail(error, QStringLiteral("The app cannot remove an old private storage snapshot."));
        }
    }
    return true;
}
bool PackageStore::privateStorage(const PrivateStorageContext& context, const QString& operation, const QJsonObject& request,
                                 QJsonObject* response, const std::function<bool()>& pending,
                                 const std::function<bool()>& beginCommit, QString* error) const
{
    if (response) *response = {};
    const bool get = operation == QStringLiteral("storage.get"), set = operation == QStringLiteral("storage.set");
    const bool remove = operation == QStringLiteral("storage.remove");
    const auto key = request.value("key").toString();
    if ((!get && !set && !remove) || !privateStorageKey(key) || (set && !privateStorageValue(request.value("value"))))
        return fail(error, QStringLiteral("The private storage operation, key, or value is not correct."));
    for (auto it = request.begin(); it != request.end(); ++it)
        if (it.key() != QStringLiteral("key") && !(set && it.key() == QStringLiteral("value"))
            && !(!get && it.key() == QStringLiteral("expectedRevision")))
            return fail(error, QStringLiteral("The private storage request contains a field outside the contract."));
    const auto& package = context.package; const auto id = package.manifest.id;
    if (!packageId(id) || context.accountReference.isEmpty() || context.accountReference.size() > 512 || !pending || !pending())
        return fail(error, QStringLiteral("The private storage request is no longer current."));
    const auto quota = storageQuota(package.manifest, package.grants);
    if (quota < 0) return fail(error, QStringLiteral("Private storage needs permission and a byte limit."));
    QLockFile lock(QDir(m_root).filePath(QStringLiteral("store.lock")));
    if (!lock.tryLock(0)) return fail(error, QStringLiteral("The add-on store is busy. Try again."));
    if (!networkConfigurationMatches(package, context.settings, context.settingsRevision) || !pending())
        return fail(error, QStringLiteral("The add-on release, settings, or account changed."));
    QJsonObject state;
    if (!loadState(id, &state, error) || !collectPrivateStorage(id, state, error)) return false;
    auto current = state.value("current").toObject(); QJsonObject data;
    if (!storageSnapshot(privateStorageDirectory(id), current, package.manifest, &data, error)) return false;
    auto descriptor = current.value("storage").toObject();
    const double revision = descriptor.value("revision").toDouble(0);
    if (request.contains("expectedRevision")) {
        const auto expected = request.value("expectedRevision"); const auto number = expected.toDouble(-1);
        if (!expected.isDouble() || number < 0 || number > 9007199254740991.0 || std::floor(number) != number || number != revision)
            return fail(error, QStringLiteral("Private storage changed. Read the value again."));
    }
    const auto partition = QString::fromLatin1(QCryptographicHash::hash(context.accountReference.toUtf8(), QCryptographicHash::Sha256).toHex());
    auto accounts = data.value("namespaces").toObject(), entries = accounts.value(partition).toObject();
    const bool found = entries.contains(key);
    QJsonObject result{{"found", found}, {"revision", revision}, {"quotaBytes", double(quota)}};
    if (get && found) result.insert(QStringLiteral("value"), entries.value(key));
    qint64 used = 0;
    if (!validatePrivateStorage(data, quota, &used, error)) return false;
    if (!get && ((set && (!found || entries.value(key) != request.value("value"))) || (remove && found))) {
        if (set) entries.insert(key, request.value("value")); else entries.remove(key);
        if (entries.isEmpty()) accounts.remove(partition); else accounts.insert(partition, entries);
        data.insert(QStringLiteral("namespaces"), accounts);
        if (!validatePrivateStorage(data, quota, &used, error)) return false;
        if (revision >= 9007199254740991.0)
            return fail(error, QStringLiteral("The private storage revision limit was reached."));
        QString hash;
        if (!writePrivateStorage(privateStorageDirectory(id), data, &hash, error)) return false;
        if (!pending() || !beginCommit || !beginCommit()) return fail(error, QStringLiteral("The private storage request was canceled."));
        current.insert("storage", QJsonObject{{"version", data.value("schemaVersion")}, {"digest", hash}, {"revision", revision + 1}});
        state.insert("current", current);
        if (!saveState(id, state, error, false)) return false;
        result.insert(QStringLiteral("revision"), revision + 1);
    } else if (!pending()) return fail(error, QStringLiteral("The private storage request was canceled."));
    result.insert(QStringLiteral("usedBytes"), double(used));
    if (remove) result.insert(QStringLiteral("removed"), found);
    if (response) *response = result;
    return true;
}
bool PackageStore::install(const PackageCandidate& candidate, const QJsonObject& grants, QString* error, bool distributionApproval)
{
    if (!validateCandidate(candidate, error) || !validateGrants(candidate.manifest, grants, error)) return false;
    if (!QDir().mkpath(m_root)) return fail(error, QStringLiteral("The app cannot create the add-on store."));
    QLockFile lock(QDir(m_root).filePath(QStringLiteral("store.lock")));
    if (!lock.tryLock(0)) return fail(error, QStringLiteral("The add-on store is busy."));
    const auto id = candidate.manifest.id;
    if (distributionApproval && (!candidate.identity.distributionSource || candidate.identity.developerSource))
        return fail(error, QStringLiteral("Only an app distribution package can use this approval."));
    QJsonObject state;
    const auto statePath = QDir(m_root).filePath(QStringLiteral("packages/") + id + QStringLiteral("/state.json"));
    if ((QFileInfo::exists(statePath) || QFileInfo::exists(statePath + QStringLiteral(".backup")))
        && !loadState(id, &state, error)) return false;
    const auto old = state.value("current").toObject();
    if (state.value("recoveryRequired").toBool())
        return fail(error, QStringLiteral("Restore the package state before an update."));
    Manifest oldManifest;
    QJsonObject nextSettings;
    auto versions = state.value("lockedVersions").toObject();
    if (!old.isEmpty() && !versions.contains(old.value("version").toString()))
        versions.insert(old.value("version").toString(), old.value("digest"));
    const auto lockedVersion = versions.value(candidate.manifest.version).toString();
    if (!lockedVersion.isEmpty() && lockedVersion != QString::fromLatin1(candidate.digest))
        return fail(error, QStringLiteral("This package version already has different locked contents."));
    if (!old.isEmpty()) {
        const auto identity = readIdentity(old.value("identity").toObject());
        if (identity.source != candidate.identity.source || identity.developerSource != candidate.identity.developerSource
            || (!identity.distributionSource && candidate.identity.distributionSource))
            return fail(error, QStringLiteral("The package source changed. Approve the new source before an update."));
        const QString historyTag = (candidate.identity.distributionSource ? QStringLiteral("distribution:") : QString{}) + candidate.identity.tag;
        const auto locked = state.value("lockedTags").toObject().value(historyTag).toObject();
        if (!locked.isEmpty() && (locked.value("commitId") != candidate.identity.commitId
            || locked.value("tagObjectId") != candidate.identity.tagObjectId
            || locked.value("digest") != QString::fromLatin1(candidate.digest)))
            return fail(error, QStringLiteral("A previously locked tag changed."));
        InstalledPackage verified;
        if (!installed(id, &verified, error)) return false;
        oldManifest = verified.manifest;
        if (old.value("digest") == QString::fromLatin1(candidate.digest)
            && old.value("identity").toObject() == identityJson(candidate.identity)) {
            if (!distributionApproval) return true;
            state.insert("distributionApproval", QString::fromLatin1(candidate.digest));
            return saveState(id, state, error);
        }
    }
    QJsonObject priorSettings = old.value("settings").toObject();
    bool retainedSettings = false;
    const auto savedPath = QDir(m_root).filePath(QStringLiteral("retained-settings/") + id + QStringLiteral(".json"));
    if (old.isEmpty() && QFileInfo::exists(savedPath)) {
        QJsonObject archive;
        if (!readJson(savedPath, &archive, error)) return false;
        if (archive.value("formatVersion") == 2 && archive.value("id") == id) {
            const auto identity = readIdentity(archive.value("identity").toObject());
            if (identity.source == candidate.identity.source && identity.developerSource == candidate.identity.developerSource
                && identity.distributionSource == candidate.identity.distributionSource) {
                if (!Manifest::parse(QJsonDocument(archive.value("manifest").toObject()).toJson(), &oldManifest, error)
                    || oldManifest.id != id) return fail(error, QStringLiteral("The saved settings manifest is not correct."));
                priorSettings = archive.value("settings").toObject(); retainedSettings = true;
            }
        }
    }
    if (!migrateSettings(candidate.manifest, candidate.contents, old.isEmpty() && !retainedSettings ? nullptr : &oldManifest,
                         priorSettings, grants, &nextSettings, error)) return false;
    if (!collectPrivateStorage(id, state, error)) return false;
    QJsonObject priorDataRelease = old;
    if (old.isEmpty()) {
        const auto archivePath = QDir(privateStorageDirectory(id)).filePath(QStringLiteral("../archive.json"));
        if (QFileInfo::exists(archivePath)) {
            QJsonObject archive;
            if (!readJson(archivePath, &archive, error) || archive.value("formatVersion") != 1 || archive.value("id") != id)
                return fail(error, QStringLiteral("The saved private storage archive is not correct."));
            const auto identity = readIdentity(archive.value("identity").toObject());
            if (identity.source == candidate.identity.source && identity.developerSource == candidate.identity.developerSource
                && identity.distributionSource == candidate.identity.distributionSource) priorDataRelease = archive;
        }
    }
    QJsonObject nextStorage;
    if (candidate.manifest.permissionDeclared(QStringLiteral("storage.private")) || priorDataRelease.contains("storage")) {
        QJsonObject data;
        if (priorDataRelease.contains("storage")) {
            if (!storageSnapshot(privateStorageDirectory(id), priorDataRelease, oldManifest, &data, error)) return false;
            if (candidate.manifest.permissionDeclared(QStringLiteral("storage.private"))) {
                auto quota = storageQuota(candidate.manifest, grants);
                if (quota < 0) quota = PrivateStorageLimits::MaximumBytes;
                QJsonObject migrated;
                if (!migratePrivateStorage(candidate.manifest, candidate.contents, data.value("schemaVersion").toInt(), data, quota, &migrated, error)) return false;
                data = migrated;
            }
        } else data = emptyPrivateStorage(privateStorageVersion(candidate.manifest));
        QString hash;
        if (!writePrivateStorage(privateStorageDirectory(id), data, &hash, error)) return false;
        const auto prior = priorDataRelease.value("storage").toObject();
        auto revision = prior.value("revision").toDouble(0);
        if (!prior.isEmpty() && prior.value("digest").toString() != hash) {
            if (revision >= 9007199254740991.0)
                return fail(error, QStringLiteral("The private storage revision limit was reached."));
            ++revision;
        }
        nextStorage = {{"version", data.value("schemaVersion")}, {"digest", hash},
            {"revision", revision}};
    }
    const auto destination = releaseDirectory(id, QString::fromLatin1(candidate.digest));
    if (QFileInfo::exists(destination)) {
        Manifest verified;
        QMap<QString, QByteArray> files;
        if (!Manifest::loadDirectory(destination, &verified, &files, error) || packageDigest(files) != candidate.digest)
            return fail(error, QStringLiteral("The installed immutable release failed its integrity check."));
    } else {
        const auto releaseParent = QFileInfo(destination).absolutePath();
        if (!QDir().mkpath(releaseParent)) return fail(error, QStringLiteral("The app cannot create the release directory."));
        QTemporaryDir staging(QDir(releaseParent).filePath(QStringLiteral(".staging-XXXXXX")));
        if (!staging.isValid()) return fail(error, QStringLiteral("The app cannot stage the release."));
        for (auto it = candidate.contents.begin(); it != candidate.contents.end(); ++it) {
            const auto path = QDir(staging.path()).filePath(it.key());
            if (!QDir().mkpath(QFileInfo(path).absolutePath()))
                return fail(error, QStringLiteral("The app cannot stage the resource directory."));
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(it.value()) != it.value().size())
                return fail(error, QStringLiteral("The app cannot stage the package resource."));
            file.close();
            file.setPermissions(QFile::ReadOwner);
        }
        Manifest verified;
        QMap<QString, QByteArray> files;
        if (!Manifest::loadDirectory(staging.path(), &verified, &files, error) || packageDigest(files) != candidate.digest)
            return fail(error, QStringLiteral("The staged release failed its integrity check."));
        if (!QDir().rename(staging.path(), destination))
            return fail(error, QStringLiteral("The app cannot activate the immutable release directory."));
        staging.setAutoRemove(false);
    }
    QJsonObject current{
        {"digest", QString::fromLatin1(candidate.digest)}, {"identity", identityJson(candidate.identity)},
        {"version", candidate.manifest.version}, {"grants", grants},
        {"settingsVersion", candidate.manifest.settingsVersion}, {"settings", nextSettings},
        {"storageGeneration", QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"settingsRevision", old.value("settingsRevision").toDouble(0)
            + (!old.isEmpty() && (nextSettings != old.value("settings").toObject()
                || old.value("settingsVersion").toInt() != candidate.manifest.settingsVersion) ? 1 : 0)}
    };
    if (!nextStorage.isEmpty()) current.insert(QStringLiteral("storage"), nextStorage);
    auto history = state.value("lockedTags").toObject();
    const QString historyTag = (candidate.identity.distributionSource ? QStringLiteral("distribution:") : QString{}) + candidate.identity.tag;
    history.insert(historyTag, QJsonObject{{"commitId", candidate.identity.commitId},
        {"tagObjectId", candidate.identity.tagObjectId}, {"digest", QString::fromLatin1(candidate.digest)}});
    versions.insert(candidate.manifest.version, QString::fromLatin1(candidate.digest));
    if (history.size() > 1024 || versions.size() > 1024)
        return fail(error, QStringLiteral("The package release history is full. The installed release stays active."));
    QJsonObject next{
        {"formatVersion", 1}, {"id", id}, {"generation", state.value("generation").toDouble(0) + 1},
        {"enabled", old.isEmpty() ? false : state.value("enabled").toBool()},
        {"current", current}, {"previous", old}, {"lockedTags", history}, {"lockedVersions", versions},
        {"retained", retainSnapshot(state.value("retained").toArray(), old)},
        {"credentialReferences", state.value("credentialReferences").toObject()}
    };
    if (distributionApproval) next.insert("distributionApproval", QString::fromLatin1(candidate.digest));
    return saveState(id, next, error);
}
bool PackageStore::installed(const QString& id, InstalledPackage* output, QString* error) const
{
    QJsonObject state;
    if (!loadState(id, &state, error)) return false;
    const auto current = state.value("current").toObject();
    const auto digest = current.value("digest").toString();
    if (!digestId(digest)) return fail(error, QStringLiteral("The installed content digest is not correct."));
    InstalledPackage result;
    result.directory = releaseDirectory(id, digest);
    QMap<QString, QByteArray> data;
    if (!Manifest::loadDirectory(result.directory, &result.manifest, &data, error)
        || packageDigest(data) != digest.toLatin1() || result.manifest.id != id)
        return fail(error, QStringLiteral("The installed package failed its integrity check."));
    result.digest = digest.toLatin1();
    result.identity = readIdentity(current.value("identity").toObject());
    result.grants = current.value("grants").toObject();
    if (!validateGrants(result.manifest, result.grants, error)) return false;
    result.recoveryRequired = state.value("recoveryRequired").toBool();
    result.enabled = state.value("enabled").toBool() && !result.recoveryRequired && !safeMode() && !packageRetiring(id);
    if (result.enabled && result.identity.distributionSource) {
        const auto policyPath = QDir(m_root).filePath(QStringLiteral("distribution-policy.json"));
        QJsonObject policy;
        if (QFileInfo::exists(policyPath) && (!readJson(policyPath, &policy, nullptr)
            || (policy.value("removed").toObject().value(id).toBool()
                && state.value("distributionApproval").toString() != digest))) result.enabled = false;
    }
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
        if (packageId(id) && (QFileInfo::exists(directory.filePath(id + QStringLiteral("/state.json")))
            || QFileInfo::exists(directory.filePath(id + QStringLiteral("/state.json.backup")))))
            result.append(id);
    return result;
}
bool PackageStore::setEnabled(const QString& id, bool enabled, QString* error)
{
    QLockFile lock(QDir(m_root).filePath(QStringLiteral("store.lock")));
    if (!lock.tryLock(0)) return fail(error, QStringLiteral("The add-on store is busy."));
    QJsonObject state;
    if (!loadState(id, &state, error)) return false;
    if (enabled) {
        InstalledPackage verified;
        if (!installed(id, &verified, error)) return false;
        if (verified.identity.distributionSource && distributionSuppressed(id))
            return fail(error, QStringLiteral("Review and approve the app package before it can be enabled."));
    }
    state.insert("enabled", enabled);
    state.insert("generation", state.value("generation").toDouble() + 1);
    return saveState(id, state, error);
}
bool PackageStore::rollback(const QString& id, QString* error)
{
    QLockFile lock(QDir(m_root).filePath(QStringLiteral("store.lock")));
    if (!lock.tryLock(0)) return fail(error, QStringLiteral("The add-on store is busy."));
    QJsonObject state;
    if (!loadState(id, &state, error)) return false;
    const auto previous = state.value("previous").toObject();
    const auto digest = previous.value("digest").toString();
    if (!digestId(digest)) return fail(error, QStringLiteral("A retained rollback release is not available."));
    if (!validateSnapshot(id, previous, error))
        return fail(error, QStringLiteral("The rollback release failed its check."));
    state.insert("retained", retainSnapshot(state.value("retained").toArray(), state.value("current").toObject()));
    state.insert("previous", state.value("current"));
    state.insert("current", previous);
    state.remove("distributionApproval");
    state.insert("generation", state.value("generation").toDouble() + 1);
    return saveState(id, state, error);
}
namespace {
bool ownedRemovalPath(const QString& root, const QString& relative, bool tree, QString* error)
{
    QString path = root;
    if ((QFileInfo(path).isSymLink() || QFileInfo(path).isJunction())) return fail(error, QStringLiteral("The add-on store path is a symbolic link."));
    const auto parts = relative.split(QLatin1Char('/'));
    for (qsizetype i = 0; i < parts.size(); ++i) {
        path = QDir(path).filePath(parts.at(i));
        const QFileInfo info(path);
        if ((info.isSymLink() || info.isJunction()) || (info.exists() && i + 1 < parts.size() && !info.isDir()))
            return fail(error, QStringLiteral("The saved package path is not a contained directory."));
    }
    if (!tree || !QFileInfo::exists(path)) return true;
    if (!QFileInfo(path).isDir()) return fail(error, QStringLiteral("The saved package path is not a directory."));
    QDirIterator entries(path, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    unsigned count = 0;
    while (entries.hasNext()) {
        entries.next(); const auto info = entries.fileInfo();
        if (++count > 65536 || (info.isSymLink() || info.isJunction()) || (!info.isDir() && !info.isFile()))
            return fail(error, QStringLiteral("The package contains an unsafe path or too many files."));
    }
    return true;
}
}
QStringList PackageStore::savedDataIds() const
{
    QSet<QString> found;
    const QStringList parents{QStringLiteral("retained-settings"), QStringLiteral("private-storage"), QStringLiteral("retained-corrupt")};
    unsigned inspected = 0;
    for (const auto& parent : parents) {
        if (!ownedRemovalPath(m_root, parent, false, nullptr)) continue;
        const auto directory = QDir(m_root).filePath(parent);
        if (!QFileInfo(directory).isDir()) continue;
        QDirIterator entries(directory, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        while (entries.hasNext()) {
            if (++inspected > 1024 || found.size() >= 256) break;
            entries.next(); const auto info = entries.fileInfo();
            if ((info.isSymLink() || info.isJunction())) continue;
            const bool settings = parent == QLatin1String("retained-settings");
            if ((settings && (!info.isFile() || !info.fileName().endsWith(QLatin1String(".json")))) || (!settings && !info.isDir())) continue;
            const auto id = settings ? info.fileName().chopped(5) : info.fileName();
            if (packageId(id)) found.insert(id);
        }
    }
    auto ids = found.values(); ids.sort(); return ids;
}
bool PackageStore::canUninstall(const QString& id, bool removeData, QString* error) const
{
    if (!packageId(id)) return fail(error, QStringLiteral("The package ID is not correct."));
    QLockFile lock(QDir(m_root).filePath(QStringLiteral("store.lock")));
    if (!lock.tryLock(0)) return fail(error, QStringLiteral("The add-on store is busy."));
    const QStringList paths{QStringLiteral("packages/") + id, QStringLiteral("retained-settings/") + id + QStringLiteral(".json"),
        QStringLiteral("private-storage/") + id, QStringLiteral("retained-corrupt/") + id};
    for (qsizetype i = 0; i < paths.size(); ++i)
        if (!ownedRemovalPath(m_root, paths.at(i), i != 1, error)) return false;
    if (QFileInfo::exists(QDir(m_root).filePath(paths.first()))) return true;
    if (removeData) for (qsizetype i = 1; i < paths.size(); ++i)
        if (QFileInfo::exists(QDir(m_root).filePath(paths.at(i)))) return true;
    return fail(error, QStringLiteral("The package has no installed files or saved data."));
}
bool PackageStore::uninstall(const QString& id, bool removeData, QString* error)
{
    if (!packageId(id)) return fail(error, QStringLiteral("The package ID is not correct."));
    QLockFile lock(QDir(m_root).filePath(QStringLiteral("store.lock")));
    if (!lock.tryLock(0)) return fail(error, QStringLiteral("The add-on store is busy."));
    const auto retirement = m_authority.retirePackage(id);
    const QString packagePath = QStringLiteral("packages/") + id;
    const QString savedPath = QStringLiteral("retained-settings/") + id + QStringLiteral(".json");
    const QString privatePath = QStringLiteral("private-storage/") + id;
    const QString corruptPath = QStringLiteral("retained-corrupt/") + id;
    if (!ownedRemovalPath(m_root, packagePath, true, error)
        || !ownedRemovalPath(m_root, savedPath, false, error)
        || !ownedRemovalPath(m_root, privatePath, true, error)
        || !ownedRemovalPath(m_root, corruptPath, true, error)) return false;
    const auto folder = QDir(m_root).filePath(packagePath);
    if (!QFileInfo::exists(folder) && (!removeData || (!QFileInfo::exists(QDir(m_root).filePath(corruptPath))
            && !QFileInfo::exists(QDir(m_root).filePath(privatePath)) && !QFileInfo::exists(QDir(m_root).filePath(savedPath)))))
        return fail(error, QStringLiteral("The package is not installed."));
    QJsonObject state;
    InstalledPackage verified;
    const bool valid = loadState(id, &state, nullptr) && installed(id, &verified, nullptr);
    const auto mutation = m_authority.mutation();
    if (!removeData && valid) {
        const auto retained = QDir(m_root).filePath(QStringLiteral("retained-settings"));
        const auto current = state.value("current").toObject();
        const QJsonObject archive{{"formatVersion", 2}, {"id", id}, {"identity", current.value("identity")},
            {"manifest", verified.manifest.document}, {"settings", current.value("settings")}};
        if (!QDir().mkpath(retained) || !writeJson(QDir(m_root).filePath(savedPath), archive, error)) return false;
        if (current.contains("storage")) {
            const auto parent = QFileInfo(privateStorageDirectory(id)).absolutePath();
            if (!QDir().mkpath(parent) || !writeJson(QDir(parent).filePath(QStringLiteral("archive.json")),
                {{"formatVersion", 1}, {"id", id}, {"identity", current.value("identity")}, {"storage", current.value("storage")}}, error)) return false;
        }
    }
    if (!removeData && !valid) {
        const auto retained = QDir(m_root).filePath(corruptPath);
        if (!QDir().mkpath(retained) || !QDir().rename(folder, QDir(retained).filePath(QUuid::createUuid().toString(QUuid::Id128))))
            return fail(error, QStringLiteral("The app cannot retain the damaged package files."));
        return true;
    }
    if (removeData) {
        const auto saved = QDir(m_root).filePath(savedPath);
        if ((QFileInfo::exists(saved) && !QFile::remove(saved))
            || (QFileInfo::exists(QDir(m_root).filePath(privatePath)) && !QDir(QDir(m_root).filePath(privatePath)).removeRecursively())
            || (QFileInfo::exists(QDir(m_root).filePath(corruptPath)) && !QDir(QDir(m_root).filePath(corruptPath)).removeRecursively()))
            return fail(error, QStringLiteral("The app cannot delete all saved package data. The add-on stays withdrawn. Try deletion again."));
    }
    if (!QFileInfo::exists(folder)) return true;
    const QString tombstone = folder + QStringLiteral(".removing-") + QUuid::createUuid().toString(QUuid::Id128);
    if (!QDir().rename(folder, tombstone)) return fail(error, QStringLiteral("The app cannot withdraw the installed package."));
    if (!QDir(tombstone).removeRecursively()) {
        if (QDir().rename(tombstone, folder) && !QFileInfo::exists(QDir(folder).filePath(QStringLiteral("state.json"))))
            writeJson(QDir(folder).filePath(QStringLiteral("state.json")), {}, nullptr);
        return fail(error, QStringLiteral("The package cleanup did not finish. Try deletion again."));
    }
    return true;
}
bool PackageStore::settings(const QString& id, QJsonObject* output, quint64* revision, QString* error) const
{
    InstalledPackage package;
    if (!installed(id, &package, error)) return false;
    QJsonObject state;
    if (!loadState(id, &state, error)) return false;
    const auto current = state.value("current").toObject();
    if (output) *output = current.value("settings").toObject();
    if (revision) *revision = quint64(current.value("settingsRevision").toDouble());
    return true;
}
bool PackageStore::networkConfiguration(const InstalledPackage& package, QJsonObject* settings, quint64* revision) const
{
    if (packageRetiring(package.manifest.id)) return false;
    QJsonObject state;
    if (!package.enabled || !loadState(package.manifest.id, &state, nullptr) || !state.value("enabled").toBool()
        || state.value("recoveryRequired").toBool() || safeMode()
        || quint64(state.value("generation").toDouble()) != package.generation) return false;
    const auto current = state.value("current").toObject();
    if (current.value("digest").toString().toLatin1() != package.digest
        || identityJson(readIdentity(current.value("identity").toObject())) != identityJson(package.identity)
        || current.value("grants").toObject() != package.grants) return false;
    if (settings) *settings = current.value("settings").toObject();
    if (revision) *revision = quint64(current.value("settingsRevision").toDouble());
    return !packageRetiring(package.manifest.id);
}
bool PackageStore::networkConfigurationMatches(const InstalledPackage& package, const QJsonObject& settings, quint64 revision) const
{
    QJsonObject current; quint64 currentRevision = 0;
    return networkConfiguration(package, &current, &currentRevision) && currentRevision == revision && current == settings;
}
bool PackageStore::saveSettings(const QString& id, const QJsonObject& settings, quint64 revision, QString* error)
{
    if (QJsonDocument(settings).toJson(QJsonDocument::Compact).size() > 256 * 1024)
        return fail(error, QStringLiteral("The settings are too large."));
    QLockFile lock(QDir(m_root).filePath(QStringLiteral("store.lock")));
    if (!lock.tryLock(0)) return fail(error, QStringLiteral("The add-on store is busy."));
    QJsonObject state;
    if (!loadState(id, &state, error)) return false;
    auto current = state.value("current").toObject();
    if (quint64(current.value("settingsRevision").toDouble()) != revision)
        return fail(error, QStringLiteral("The settings changed. Read the settings again before a save."));
    InstalledPackage package;
    if (!installed(id, &package, error)) return false;
    QSet<QString> secretFields;
    for (const auto& binding : package.manifest.permissionScope(QStringLiteral("credentials.use")).value("bindings").toArray())
        secretFields.insert(binding.toObject().value("field").toString());
    if (!package.manifest.settingsSchema.isEmpty()) {
        QFile schema(QDir(package.directory).filePath(package.manifest.settingsSchema));
        if (!schema.open(QIODevice::ReadOnly)) return fail(error, QStringLiteral("The app cannot examine the protected settings fields."));
        const auto properties = QJsonDocument::fromJson(schema.readAll()).object().value("properties").toObject();
        for (auto it = properties.begin(); it != properties.end(); ++it) {
            const auto field = it.value().toObject();
            if (field.value("x-hakone-secret").toBool() || field.value("writeOnly").toBool()
                || field.value("format").toString() == QLatin1String("password")) secretFields.insert(it.key());
        }
    }
    if (settings.contains("_credentials")) {
        if (!settings.value("_credentials").isObject())
            return fail(error, QStringLiteral("The host credential reference object is not correct."));
        const auto references = settings.value("_credentials").toObject();
        static const QRegularExpression uuid(QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
        for (auto it = references.begin(); it != references.end(); ++it)
            if (!secretFields.contains(it.key()) || !it.value().isString()
                || !uuid.match(it.value().toString()).hasMatch())
                return fail(error, QStringLiteral("The protected credential reference is not correct."));
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
        return fail(error, QStringLiteral("Do not save credentials in ordinary add-on settings."));
    current.insert("settings", settings);
    current.insert("settingsRevision", double(revision) + 1);
    state.insert("current", current);
    auto referenceIndex = state.value("credentialReferences").toObject();
    const auto references = settings.value("_credentials").toObject();
    for (auto it = references.begin(); it != references.end(); ++it) {
        auto values = referenceIndex.value(it.key()).toArray();
        if (!values.contains(it.value())) values.append(it.value());
        referenceIndex.insert(it.key(), values);
    }
    if (QJsonDocument(referenceIndex).toJson(QJsonDocument::Compact).size() > 256 * 1024)
        return fail(error, QStringLiteral("The credential reference history is full. Disconnect the account before a new credential update."));
    state.insert("credentialReferences", referenceIndex);
    return saveState(id, state, error);
}

bool PackageStore::validateSnapshot(const QString& id, const QJsonObject& snapshot, QString* error) const
{
    const auto digest = snapshot.value("digest").toString();
    if (!digestId(digest)) return fail(error, QStringLiteral("The recovery content digest is not correct."));
    PackageCandidate candidate;
    if (!Manifest::loadDirectory(releaseDirectory(id, digest), &candidate.manifest, &candidate.contents, error)
        || candidate.manifest.id != id || snapshot.value("version") != candidate.manifest.version
        || snapshot.value("settingsVersion").toInt() != candidate.manifest.settingsVersion)
        return fail(error, QStringLiteral("The recovery release does not agree with its saved state."));
    candidate.identity = readIdentity(snapshot.value("identity").toObject());
    candidate.digest = digest.toLatin1();
    QJsonObject checked, privateData;
    if (!storageSnapshot(privateStorageDirectory(id), snapshot, candidate.manifest, &privateData, error)) return false;
    auto quota = storageQuota(candidate.manifest, snapshot.value("grants").toObject());
    if (quota < 0) quota = PrivateStorageLimits::MaximumBytes;
    if (!validatePrivateStorage(privateData, quota, nullptr, error)) return false;
    return validateCandidate(candidate, error)
        && validateGrants(candidate.manifest, snapshot.value("grants").toObject(), error)
        && migrateSettings(candidate.manifest, candidate.contents, &candidate.manifest,
                           snapshot.value("settings").toObject(), snapshot.value("grants").toObject(), &checked, error);
}

QJsonArray PackageStore::retainedGenerations(const QString& id, QString* error) const
{
    QJsonObject state;
    if (!loadState(id, &state, error)) return {};
    QJsonArray result;
    for (const auto& entry : state.value("retained").toArray()) {
        const auto snapshot = entry.toObject();
        result.append(QJsonObject{{"digest", snapshot.value("digest")}, {"version", snapshot.value("version")},
            {"storageGeneration", snapshot.value("storageGeneration")}, {"settingsRevision", snapshot.value("settingsRevision")}});
    }
    return result;
}

bool PackageStore::recover(const QString& id, QString* error)
{
    QLockFile lock(QDir(m_root).filePath(QStringLiteral("store.lock")));
    if (!lock.tryLock(0)) return fail(error, QStringLiteral("The add-on store is busy."));
    QJsonObject state;
    if (!loadState(id, &state, error)) return false;
    QJsonArray candidates{state.value("current"), state.value("previous")};
    const auto retained = state.value("retained").toArray();
    for (qsizetype index = retained.size(); index > 0; --index) candidates.append(retained.at(index - 1));
    for (const auto& entry : candidates) {
        const auto snapshot = entry.toObject();
        if (!validateSnapshot(id, snapshot, nullptr)) continue;
        state.insert("retained", retainSnapshot(retained, state.value("current").toObject()));
        if (snapshot != state.value("current").toObject()) state.insert("previous", state.value("current"));
        state.insert("current", snapshot);
        state.insert("enabled", false);
        state.insert("recoveryRequired", false);
        state.insert("generation", state.value("generation").toDouble() + 1);
        state.remove("distributionApproval");
        return saveState(id, state, error);
    }
    return fail(error, QStringLiteral("A correct retained release is not available. Keep safe mode active and restore a verified package."));
}

bool PackageStore::safeMode() const
{
    if (qEnvironmentVariable("VLC_ADDON_SAFE_MODE") == QStringLiteral("1")) return true;
    const auto path = QDir(m_root).filePath(QStringLiteral("recovery.json"));
    if (!QFileInfo::exists(path)) return false;
    QJsonObject recovery;
    return !readJson(path, &recovery, nullptr) || recovery.value("safeMode").toBool(true);
}

MetadataCatalogueReader PackageStore::metadataCatalogueReader() const
{
    // Copying this retained Store and capturing its epoch are memory-only.
    // Never replace queued with a newly captured token on the worker.
    return [store = *this, queued = m_authority.capture()](MetadataCatalogueSnapshot* output, QString* error) {
        return store.readMetadataCatalogue(queued, output, error);
    };
}
bool PackageStore::readMetadataCatalogue(const StoreAuthorityToken& queued,
                                         MetadataCatalogueSnapshot* output, QString* error) const
{
    if (!output) return fail(error, QStringLiteral("The metadata catalogue output is not available."));
    *output = {};
    if (!queued.current()) return fail(error, QStringLiteral("The metadata catalogue changed. Start the request again."));
    if (!QDir().mkpath(m_root)) return fail(error, QStringLiteral("The add-on store directory is not available."));
    QLockFile lock(QDir(m_root).filePath(QStringLiteral("store.lock")));
    if (!lock.tryLock(0)) return fail(error, QStringLiteral("The add-on store is busy."));
    MetadataCatalogueSnapshot captured;
    captured.queuedAuthority = queued;
    captured.authority = m_authority.capture();
    if (!captured.current()) return fail(error, QStringLiteral("The metadata catalogue changed. Start the request again."));
    if (!readProviderPreferences(m_root, &captured.providerPreferences, error)) return false;
    const auto recoveryPath = QDir(m_root).filePath(QStringLiteral("recovery.json"));
    QJsonObject recovery;
    if (QFileInfo::exists(recoveryPath) || QFileInfo(recoveryPath).isSymLink()) {
        if (!catalogueObject(recoveryPath, 256 * 1024, &recovery, error)
            || !recovery.value(QStringLiteral("safeMode")).isBool())
            return fail(error, QStringLiteral("The saved safe-mode state is not correct."));
    }
    captured.safeMode = qEnvironmentVariable("VLC_ADDON_SAFE_MODE") == QStringLiteral("1")
        || recovery.value(QStringLiteral("safeMode")).toBool();
    const auto distributionPath = QDir(m_root).filePath(QStringLiteral("distribution-policy.json"));
    if (QFileInfo::exists(distributionPath) || QFileInfo(distributionPath).isSymLink()) {
        if (!catalogueObject(distributionPath, MetadataCatalogueSnapshot::MaximumBytes, &captured.distributionPolicy, error)
            || captured.distributionPolicy.value(QStringLiteral("formatVersion")) != 1
            || !captured.distributionPolicy.value(QStringLiteral("removed")).isObject())
            return fail(error, QStringLiteral("The distribution policy is not correct."));
        const auto removed = captured.distributionPolicy.value(QStringLiteral("removed")).toObject();
        for (auto it = removed.begin(); it != removed.end(); ++it)
            if (!packageId(it.key()) || !it.value().isBool())
                return fail(error, QStringLiteral("The distribution policy entry is not correct."));
    }
    QStringList ids;
    if (!catalogueCandidates(QDir(m_root).filePath(QStringLiteral("packages")), &ids, error)) return false;
    captured.candidateCount = ids.size();
    QJsonArray rows;
    const auto encoded = [&] {
        return QJsonDocument(QJsonObject{{QStringLiteral("preferences"), captured.providerPreferences},
            {QStringLiteral("distributionPolicy"), captured.distributionPolicy},
            {QStringLiteral("safeMode"), captured.safeMode}, {QStringLiteral("candidates"), double(captured.candidateCount)},
            {QStringLiteral("packages"), rows}}).toJson(QJsonDocument::Compact);
    };
    if (encoded().size() > MetadataCatalogueSnapshot::MaximumBytes)
        return fail(error, QStringLiteral("The metadata catalogue exceeds the byte limit."));
    qsizetype contributions = 0;
    for (const auto& id : ids) {
        if (!captured.current()) return fail(error, QStringLiteral("The metadata catalogue changed. Start the request again."));
        QJsonObject state;
        const auto folder = QDir(m_root).filePath(QStringLiteral("packages/") + id);
        if (!catalogueObject(QDir(folder).filePath(QStringLiteral("state.json")), 4 * 1024 * 1024, &state, error)
            || state.value(QStringLiteral("formatVersion")) != 1 || state.value(QStringLiteral("id")) != id
            || !state.value(QStringLiteral("enabled")).isBool() || !state.value(QStringLiteral("current")).isObject())
            return fail(error, QStringLiteral("The current package state is not correct."));
        const auto current = state.value(QStringLiteral("current")).toObject();
        MetadataCataloguePackage row;
        row.savedEnabled = state.value(QStringLiteral("enabled")).toBool();
        row.package.recoveryRequired = state.value(QStringLiteral("recoveryRequired")).toBool();
        if (!catalogueInteger(state.value(QStringLiteral("generation")), &row.package.generation, true)
            || !catalogueInteger(current.value(QStringLiteral("settingsRevision")), &row.settingsRevision)
            || !current.value(QStringLiteral("settings")).isObject()
            || !current.value(QStringLiteral("identity")).isObject() || !current.value(QStringLiteral("grants")).isObject()
            || (state.contains(QStringLiteral("recoveryRequired")) && !state.value(QStringLiteral("recoveryRequired")).isBool()))
            return fail(error, QStringLiteral("The current package generation or settings are not correct."));
        const auto digest = current.value(QStringLiteral("digest")).toString();
        if (!digestId(digest) || !catalogueDirectory(QDir(folder).filePath(QStringLiteral("releases")), false, error))
            return fail(error, QStringLiteral("The current package release is not correct."));
        row.package.directory = releaseDirectory(id, digest);
        PackageCandidate candidate;
        if (!catalogueContents(row.package.directory, &candidate.manifest, &candidate.contents, error)) return false;
        candidate.digest = digest.toLatin1();
        const auto identity = current.value(QStringLiteral("identity")).toObject();
        candidate.identity = readIdentity(identity);
        if (identity != identityJson(candidate.identity) || candidate.manifest.id != id
            || current.value(QStringLiteral("version")) != candidate.manifest.version
            || current.value(QStringLiteral("settingsVersion")) != candidate.manifest.settingsVersion
            || !validateCandidate(candidate, error)
            || !validateGrants(candidate.manifest, current.value(QStringLiteral("grants")).toObject(), error))
            return fail(error, QStringLiteral("The installed package failed its complete catalogue check."));
        row.package.manifest = std::move(candidate.manifest);
        row.package.identity = std::move(candidate.identity);
        row.package.digest = candidate.digest;
        row.package.grants = current.value(QStringLiteral("grants")).toObject();
        row.package.previousDigest = state.value(QStringLiteral("previous")).toObject().value(QStringLiteral("digest")).toString();
        row.package.enabled = row.savedEnabled && !row.package.recoveryRequired && !captured.safeMode && !packageRetiring(id);
        if (row.package.identity.distributionSource
            && captured.distributionPolicy.value(QStringLiteral("removed")).toObject().value(id).toBool()
            && state.value(QStringLiteral("distributionApproval")).toString() != digest) row.package.enabled = false;
        row.settings = current.value(QStringLiteral("settings")).toObject();
        row.schema = {{QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"), QJsonObject{}}, {QStringLiteral("additionalProperties"), false}};
        if (!row.package.manifest.settingsSchema.isEmpty())
            row.schema = QJsonDocument::fromJson(candidate.contents.value(row.package.manifest.settingsSchema)).object();
        for (const auto& contribution : row.package.manifest.contributions) {
            if (++contributions > MetadataCatalogueSnapshot::MaximumContributions)
                return fail(error, QStringLiteral("The metadata catalogue has too many contributions."));
            row.contributions.append(contribution);
        }
        QJsonArray declarations;
        for (const auto& contribution : row.contributions) declarations.append(contribution.document);
        rows.append(QJsonObject{{QStringLiteral("manifest"), row.package.manifest.document},
            {QStringLiteral("identity"), identityJson(row.package.identity)}, {QStringLiteral("digest"), digest},
            {QStringLiteral("generation"), double(row.package.generation)}, {QStringLiteral("directory"), row.package.directory},
            {QStringLiteral("previousDigest"), row.package.previousDigest}, {QStringLiteral("grants"), row.package.grants},
            {QStringLiteral("savedEnabled"), row.savedEnabled}, {QStringLiteral("enabled"), row.package.enabled},
            {QStringLiteral("recoveryRequired"), row.package.recoveryRequired}, {QStringLiteral("settings"), row.settings},
            {QStringLiteral("schema"), row.schema}, {QStringLiteral("settingsRevision"), double(row.settingsRevision)},
            {QStringLiteral("contributions"), declarations}});
        if (encoded().size() > MetadataCatalogueSnapshot::MaximumBytes)
            return fail(error, QStringLiteral("The metadata catalogue exceeds the byte limit."));
        captured.packages.append(std::move(row));
    }
    const auto bytes = encoded();
    if (!captured.current()) return fail(error, QStringLiteral("The metadata catalogue changed. Start the request again."));
    captured.rawContributionCount = contributions;
    captured.retainedBytes = bytes.size();
    captured.digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    *output = std::move(captured);
    if (error) error->clear();
    return true;
}

bool PackageStore::providerPreferences(ProviderPreferences* output, QString* error) const
{
    if (!output) return fail(error, QStringLiteral("The provider settings output is not available."));
    *output = {};
    if (!QDir().mkpath(m_root)) return fail(error, QStringLiteral("The add-on store directory is not available."));
    QLockFile lock(QDir(m_root).filePath(QStringLiteral("store.lock")));
    if (!lock.tryLock(0)) return fail(error, QStringLiteral("The add-on store is busy."));
    ProviderPreferences captured;
    captured.authority = m_authority.capture();
    if (!captured.authority.current() || !readProviderPreferences(m_root, &captured.values, error)
        || !captured.authority.current())
        return fail(error, QStringLiteral("The provider settings snapshot is not available."));
    *output = std::move(captured); return true;
}

bool PackageStore::saveProviderPreference(const QString& key, const QJsonValue& value,
                                          bool builtin, QJsonObject* saved, QString* error)
{
    if (saved) *saved = {};
    if (!QDir().mkpath(m_root)) return fail(error, QStringLiteral("The add-on store directory is not available."));
    QLockFile lock(QDir(m_root).filePath(QStringLiteral("store.lock")));
    if (!lock.tryLock(0)) return fail(error, QStringLiteral("The add-on store is busy."));
    QJsonObject values;
    if (!readProviderPreferences(m_root, &values, error)) return false;
    if (builtin) {
        auto enabled = values.value(QStringLiteral("_builtinEnabled")).toObject();
        enabled.insert(key, value); values.insert(QStringLiteral("_builtinEnabled"), enabled);
    } else values.insert(key, value);
    if (!providerPreferencesValid(values, error)) return false;
    {
        // Native watches finish before writeJson can publish the new file.
        // The mutation remains active through commit and any failed write.
        const auto mutation = m_authority.mutation();
        if (!writeJson(QDir(m_root).filePath(QStringLiteral("provider-order.json")), values, error)) return false;
    }
    if (saved) *saved = std::move(values);
    return true;
}

bool PackageStore::saveProviderOrder(const QString& feature, const QStringList& ids,
                                     QJsonObject* saved, QString* error)
{
    const auto order = QJsonArray::fromStringList(ids);
    if (!providerFeature(feature) || !providerIds(order))
        return fail(error, QStringLiteral("The provider order is not correct."));
    return saveProviderPreference(feature, order, false, saved, error);
}

bool PackageStore::saveBuiltinProviderEnabled(const QString& id, bool enabled,
                                              QJsonObject* saved, QString* error)
{
    if (!packageId(id) || !id.startsWith(QLatin1String("hakone.builtin.")))
        return fail(error, QStringLiteral("The provider ID is not correct."));
    return saveProviderPreference(id, enabled, true, saved, error);
}

bool PackageStore::setSafeMode(bool enabled, QString* error)
{
    if (!QDir().mkpath(m_root)) return fail(error, QStringLiteral("The add-on store directory is not available."));
    QLockFile lock(QDir(m_root).filePath(QStringLiteral("store.lock")));
    if (!lock.tryLock(0)) return fail(error, QStringLiteral("The add-on store is busy."));
    const auto mutation = m_authority.mutation();
    return writeJson(QDir(m_root).filePath(QStringLiteral("recovery.json")), QJsonObject{{"safeMode", enabled}}, error);
}

QStringList PackageStore::credentialReferences(const QString& id, const QString& field) const
{
    if (!packageId(id)) return {};
    QSet<QString> references;
    static const QRegularExpression uuid(QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
    const auto add = [&](const QJsonValue& value) {
        if (value.isString() && uuid.match(value.toString()).hasMatch()) references.insert(value.toString());
    };
    const auto collect = [&](const QJsonObject& state) {
        const auto index = state.value("credentialReferences").toObject();
        for (auto it = index.begin(); it != index.end(); ++it)
            if (field.isEmpty() || it.key() == field) for (const auto& reference : it.value().toArray()) add(reference);
        QJsonArray snapshots = state.value("retained").toArray();
        snapshots.append(state.value("current")); snapshots.append(state.value("previous"));
        for (const auto& snapshot : snapshots) {
            const auto fields = snapshot.toObject().value("settings").toObject().value("_credentials").toObject();
            for (auto it = fields.begin(); it != fields.end(); ++it) if (field.isEmpty() || it.key() == field) add(it.value());
        }
    };
    QJsonObject state;
    if (loadState(id, &state, nullptr)) collect(state);
    if (readJson(QDir(m_root).filePath(QStringLiteral("packages/") + id + QStringLiteral("/state.json.backup")), &state, nullptr)
        && state.value("id") == id) collect(state);
    auto result = references.values(); result.sort(); return result;
}

bool PackageStore::distributionSuppressed(const QString& id) const
{
    if (!packageId(id)) return false;
    const auto path = QDir(m_root).filePath(QStringLiteral("distribution-policy.json"));
    if (!QFileInfo::exists(path)) return false;
    QJsonObject policy;
    if (!readJson(path, &policy, nullptr)) return true;
    if (!policy.value("removed").toObject().value(id).toBool()) return false;
    QJsonObject state;
    if (!loadState(id, &state, nullptr) || state.value("recoveryRequired").toBool()) return true;
    const auto current = state.value("current").toObject();
    const auto digest = current.value("digest").toString();
    return !digestId(digest) || state.value("distributionApproval").toString() != digest
        || !readIdentity(current.value("identity").toObject()).distributionSource
        || !validateSnapshot(id, current, nullptr);
}

bool PackageStore::setDistributionSuppressed(const QString& id, bool suppressed, QString* error)
{
    if (!packageId(id)) return fail(error, QStringLiteral("The distribution package ID is not correct."));
    if (!QDir().mkpath(m_root)) return fail(error, QStringLiteral("The add-on store directory is not available."));
    QLockFile lock(QDir(m_root).filePath(QStringLiteral("store.lock")));
    if (!lock.tryLock(0)) return fail(error, QStringLiteral("The add-on store is busy."));
    const auto path = QDir(m_root).filePath(QStringLiteral("distribution-policy.json"));
    QJsonObject policy;
    if (QFileInfo::exists(path) && !readJson(path, &policy, error)) return false;
    auto removed = policy.value("removed").toObject();
    if (suppressed) removed.insert(id, true); else removed.remove(id);
    policy.insert("formatVersion", 1); policy.insert("removed", removed);
    const auto mutation = m_authority.mutation();
    return writeJson(path, policy, error);
}
} // namespace vlc::addons
