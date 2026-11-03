// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_candidate_check.hpp"
#include "addon_package_store.hpp"
#include "addon_process_launch.hpp"
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

namespace vlc::addons {
namespace {
const QString SelfTestId = QStringLiteral("hakone-self-test");
bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}
// One line from the worker, or an empty object when the time is over, the
// check was cancelled or the worker stopped.
QJsonObject readLine(QProcess& process, const QDeadlineTimer& deadline, const std::function<bool()>& cancelled)
{
    while (!process.canReadLine()) {
        if (deadline.hasExpired() || (cancelled && cancelled()) || process.state() == QProcess::NotRunning)
            return {};
        process.waitForReadyRead(50);
    }
    return QJsonDocument::fromJson(process.readLine(1024 * 1024)).object();
}
}

bool AddonCandidateCheck::run(const QString& workerExecutable, const PackageCandidate& candidate, QString* error,
                              const std::function<bool()>& cancelled)
{
    const auto& manifest = candidate.manifest;
    if (manifest.runtimeKind.isEmpty()) return true;
    // Without a worker the app cannot run any add-on code, so there is nothing to check.
    if (workerExecutable.isEmpty()) return true;
    const auto module = candidate.contents.value(manifest.runtimeEntry);
    if (module.isEmpty()) return fail(error, QStringLiteral("The add-on module is missing."));
    QTemporaryDir folder;
    QFile file(QDir(folder.path()).filePath(QStringLiteral("addon.wasm")));
    if (!folder.isValid() || !file.open(QIODevice::WriteOnly) || file.write(module) != module.size())
        return fail(error, QStringLiteral("The app cannot prepare the add-on check."));
    file.close();

    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.setStandardErrorFile(QProcess::nullDevice());
    prepareHelperLaunch(process);
    process.start(workerExecutable, {file.fileName()});
    const QDeadlineTimer deadline(TimeLimitMs);
    const auto finish = [&process](bool result) {
        process.closeWriteChannel();
        if (!process.waitForFinished(1000)) {
            process.kill();
            process.waitForFinished(1000);
        }
        return result;
    };
    if (!process.waitForStarted(5000)) return fail(error, QStringLiteral("The add-on worker did not start."));
    const auto ready = readLine(process, deadline, cancelled);
    if (ready.value(QStringLiteral("type")).toString() != QLatin1String("ready"))
        return finish(fail(error, cancelled && cancelled() ? QStringLiteral("The add-on check was canceled.")
            : QStringLiteral("The add-on module did not start. It is not installed.")));
    if (!manifest.runtimeSelfTest) return finish(true);

    const auto request = QJsonDocument(QJsonObject{{QStringLiteral("type"), QStringLiteral("self-test")},
        {QStringLiteral("id"), SelfTestId}}).toJson(QJsonDocument::Compact) + '\n';
    if (process.write(request) != request.size())
        return finish(fail(error, QStringLiteral("The add-on self-test did not start.")));
    // The worker answers each message with one reply; the self-test result
    // must be in it.
    const auto reply = readLine(process, deadline, cancelled);
    if (reply.isEmpty())
        return finish(fail(error, cancelled && cancelled() ? QStringLiteral("The add-on check was canceled.")
            : QStringLiteral("The add-on self-test did not finish.")));
    for (const auto& value : reply.value(QStringLiteral("messages")).toArray()) {
        const auto message = value.toObject();
        const auto type = message.value(QStringLiteral("type")).toString();
        // The self-test is offline: any request to the host fails it.
        if (type == QLatin1String("host-call"))
            return finish(fail(error, QStringLiteral("The add-on self-test asked for network or other host access.")));
        if (type == QLatin1String("result") && message.value(QStringLiteral("id")).toString() == SelfTestId) {
            if (message.value(QStringLiteral("status")).toString() == QLatin1String("ok")) return finish(true);
            // The install review is a host view: it shows no text from the package.
            return finish(fail(error, QStringLiteral("The add-on self-test failed. It is not installed.")));
        }
    }
    return finish(fail(error, QStringLiteral("The add-on did not answer its self-test. It is not installed.")));
}
}
