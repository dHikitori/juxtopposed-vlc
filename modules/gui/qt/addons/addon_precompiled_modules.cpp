// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "addon_precompiled_modules.hpp"
#include "addon_package_store.hpp"
#include "addon_process_launch.hpp"
#include "addon_stream_lifetime.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <mutex>

namespace vlc::addons::AddonPrecompiledModules {
namespace {
// A worker build changes its key: its precompiled code would not load.
QString workerKey(const QString& executable)
{
    const QFileInfo info(executable);
    const auto identity = info.canonicalFilePath() + QLatin1Char('\n') + QString::number(info.size())
        + QLatin1Char('\n') + QString::number(info.lastModified().toMSecsSinceEpoch());
    return QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex().first(16));
}
bool releaseDigest(const QString& digest)
{
    static const QRegularExpression valid(QStringLiteral("^[0-9a-f]{64}$"));
    return valid.match(digest).hasMatch();
}
QString entry(const QString& directory, const QString& executable, const InstalledPackage& package)
{
    const auto digest = QString::fromLatin1(package.digest);
    if (directory.isEmpty() || executable.isEmpty() || !releaseDigest(digest))
        return {};
    return QDir(directory).filePath(digest + QLatin1Char('-') + workerKey(executable));
}
QString modulePath(const InstalledPackage& package)
{
    return QDir(package.directory).filePath(package.manifest.runtimeEntry);
}
std::mutex pendingMutex;
QSet<QString> pending;
}

QStringList arguments(const QString& directory, const QString& executable, const InstalledPackage& package, bool* missing)
{
    if (missing)
        *missing = false;
    const auto module = modulePath(package);
    const auto base = entry(directory, executable, package);
    if (base.isEmpty())
        return {module};
    QFile sidecar(base + QStringLiteral(".sha256"));
    QByteArray digest;
    if (sidecar.open(QIODevice::ReadOnly))
        digest = sidecar.read(65).trimmed();
    static const QRegularExpression valid(QStringLiteral("^[0-9a-f]{64}$"));
    if (!valid.match(QString::fromLatin1(digest)).hasMatch() || !QFileInfo::exists(base + QStringLiteral(".cwasm"))) {
        if (missing)
            *missing = true;
        return {module};
    }
    return {QStringLiteral("--precompiled"), base + QStringLiteral(".cwasm"), QString::fromLatin1(digest), module};
}

bool precompile(const QString& directory, const QString& executable, const InstalledPackage& package)
{
    const auto base = entry(directory, executable, package);
    if (base.isEmpty() || !QDir().mkpath(directory))
        return false;
    const auto temporary = base + QStringLiteral(".") + QString::number(QCoreApplication::applicationPid()) + QStringLiteral(".tmp");
    QProcess process;
    prepareHelperLaunch(process);
    process.start(executable, {QStringLiteral("--precompile"), modulePath(package), temporary});
    const bool done = process.waitForFinished(30000);
    if (!done) {
        process.kill();
        process.waitForFinished(1000);
    }
    QFile compiled(temporary);
    if (!done || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || !compiled.open(QIODevice::ReadOnly)) {
        QFile::remove(temporary);
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&compiled);
    compiled.close();
    const auto digest = hash.result().toHex();
    QFile sidecar(temporary + QStringLiteral(".sha256"));
    if (!sidecar.open(QIODevice::WriteOnly | QIODevice::Truncate) || sidecar.write(digest + '\n') != digest.size() + 1) {
        QFile::remove(temporary);
        QFile::remove(sidecar.fileName());
        return false;
    }
    sidecar.close();
    // Code first, then its digest. A reader that sees a new digest with old
    // code, or the reverse, refuses the pair and compiles from source.
    QFile::remove(base + QStringLiteral(".cwasm"));
    QFile::rename(temporary, base + QStringLiteral(".cwasm"));
    QFile::remove(base + QStringLiteral(".sha256"));
    QFile::rename(sidecar.fileName(), base + QStringLiteral(".sha256"));
    // Remove entries for the same release made by older worker builds.
    const auto release = QFileInfo(base).fileName().section(QLatin1Char('-'), 0, 0);
    for (const auto& old : QDir(directory).entryInfoList({release + QStringLiteral("-*")}, QDir::Files)) {
        if (!old.absoluteFilePath().startsWith(base))
            QFile::remove(old.absoluteFilePath());
    }
    return QFileInfo::exists(base + QStringLiteral(".cwasm"));
}

void schedule(const QString& directory, const QString& executable, const InstalledPackage& package)
{
    const auto base = entry(directory, executable, package);
    if (base.isEmpty())
        return;
    {
        std::lock_guard<std::mutex> lock(pendingMutex);
        if (pending.contains(base))
            return;
        pending.insert(base);
    }
    const auto release = [base] {
        std::lock_guard<std::mutex> lock(pendingMutex);
        pending.remove(base);
    };
    const bool started = AddonStreamLifetime::applicationHandle().startQtWork(
        [directory, executable, package, release] {
            precompile(directory, executable, package);
            release();
        },
        [] {});
    if (!started)
        release();
}
}
