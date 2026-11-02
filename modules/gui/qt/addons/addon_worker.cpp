// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_worker.hpp"
#include "addon_process_launch.hpp"
#include "addon_precompiled_modules.hpp"
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QUuid>

namespace vlc::addons {
AddonWorker::AddonWorker(const InstalledPackage& package, NetworkBroker* network, QObject* parent)
    : QObject(parent), m_package(package), m_network(network)
{
    m_attemptWorkerId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_startup.setSingleShot(true);
    connect(&m_startup, &QTimer::timeout, this, [this] { fail(tr("The add-on did not start before the time limit.")); });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &AddonWorker::read);
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] { m_process.readAllStandardError(); });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        const QPointer<AddonWorker> alive(this);
        if (!m_stopping) fail(tr("The add-on worker is not available."));
        if (alive) notifyPreparedRetired();
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this, &AddonWorker::processFinished);
    m_preparedDeadline.setSingleShot(true);
    connect(&m_preparedDeadline, &QTimer::timeout, this, [this] {
        // A forced exit cannot certify a transferred completion receipt.
        retirePrepared(true);
    });
}
AddonWorker::~AddonWorker() { stop(); }
void AddonWorker::enablePreparedLifecycle()
{
    Q_ASSERT(m_process.state() == QProcess::NotRunning && m_invocations.isEmpty());
    m_prepared = true;
}
void AddonWorker::processFinished(int code, QProcess::ExitStatus status)
{
    if (!m_prepared) {
        if (!m_stopping) fail(tr("The add-on stopped because of an error."));
        return;
    }
    const QPointer<AddonWorker> alive(this);
    if (!m_preparedAbort && !m_failed) read();
    if (!alive) return;
    m_preparedCleanExit = !m_preparedAbort && !m_failed && status == QProcess::NormalExit
        && code == 0 && m_invocations.isEmpty() && m_queued.isEmpty() && m_buffer.isEmpty();
    auto terminal = std::move(m_preparedTerminal); m_preparedTerminal.reset();
    if (terminal) {
        if (m_preparedCleanExit) terminal->attempt->terminal(terminal->invocation, terminal->status, terminal->data, terminal->error);
        else terminal->attempt->blocked();
        if (!alive) return;
    }
    if (!m_preparedCleanExit && !m_failed && !m_preparedAbort)
        fail(tr("The add-on stopped before its output was complete."));
    if (alive) notifyPreparedRetired();
}
void AddonWorker::retirePrepared(bool revoke)
{
    Q_ASSERT(m_prepared);
    const QPointer<AddonWorker> alive(this);
    m_preparedRetiring = true;
    if (revoke || !m_invocations.isEmpty()) {
        m_preparedAbort = true;
        if (m_preparedTerminal) m_preparedTerminal->attempt->blocked();
        m_preparedTerminal.reset();
        revokeAttemptGates();
        if (!alive) return;
        m_stopping = true; m_startup.stop(); m_queued.clear();
        const auto ids = m_invocations.keys();
        for (const auto& id : ids) { cancel(id); if (!alive) return; }
        if (m_process.state() != QProcess::NotRunning) m_process.kill();
    } else if (m_process.state() != QProcess::NotRunning) {
        // Read through clean EOF. Late frames still pass the normal validator.
        m_process.closeWriteChannel();
        if (!m_preparedDeadline.isActive()) m_preparedDeadline.start(2000);
    }
    if (alive) notifyPreparedRetired();
}
void AddonWorker::notifyPreparedRetired()
{
    if (!m_prepared || !m_preparedRetiring || m_preparedNotified
        || m_process.state() != QProcess::NotRunning) return;
    m_preparedDeadline.stop();
    const QPointer<AddonWorker> alive(this);
    if (!m_preparedAbort && !m_preparedCleanExit) revokeAttemptGates();
    if (!alive) return;
    // Forget normal terminal weak records only after the final read and exit.
    m_attemptGates.clear(); m_stopping = true; m_preparedNotified = true;
    QTimer::singleShot(0, this, [this] { emit preparedRetired(); });
}
void AddonWorker::setExecutable(const QString& executable) { m_executable = executable; }
void AddonWorker::setConfiguration(const QJsonObject& settings, const QJsonObject& status, NetworkBroker::CredentialResolver resolver)
{
    m_settings = settings; m_credentials = {}; m_resolver = std::move(resolver);
    for (auto it = status.begin(); it != status.end(); ++it)
        m_credentials.insert(it.key(), QJsonObject{{QStringLiteral("present"), it.value().toObject().value(QStringLiteral("present")).toBool()}});
}

void AddonWorker::start()
{
    if (m_process.state() != QProcess::NotRunning) return;
    m_stopping = false; m_ready = false; m_buffer.clear();
    if (m_hostCallCount) { fail(tr("The add-on worker request state is not ready.")); return; }
    m_retired.clear(); m_retiredOrder.clear();
    auto executable = m_executable;
    if (executable.isEmpty()) executable = QStandardPaths::findExecutable(QStringLiteral("vlc-addon-worker"));
    if (executable.isEmpty()) { QTimer::singleShot(0, this, [this] { fail(tr("Install the add-on worker to use this add-on.")); }); return; }
    QProcessEnvironment environment;
#ifdef Q_OS_WIN
    environment.insert(QStringLiteral("SystemRoot"), qEnvironmentVariable("SystemRoot"));
#endif
    m_process.setProcessEnvironment(environment);
    m_process.setWorkingDirectory(m_package.directory);
    m_process.setProgram(executable);
    bool missing = m_precompileMissing;
    if (m_launchArguments.isEmpty() || executable != m_executable)
        m_process.setArguments(AddonPrecompiledModules::arguments(m_precompiled, executable, m_package, &missing));
    else m_process.setArguments(m_launchArguments);
    m_precompileMissing = false;
    // Compile once for the next start (finding F1).
    if (missing)
        AddonPrecompiledModules::schedule(m_precompiled, executable, m_package);
    m_startup.start(15000);
    prepareHelperLaunch(m_process);
    m_process.start();
}

QString AddonWorker::invoke(const QString& contribution, const QString& operation, const QJsonObject& context)
{
    return invoke(contribution, operation, context, {});
}
bool AddonWorker::configurationCurrent() const
{
    const auto current = m_requestGuard;
    return NetworkBroker::guardCurrent(current);
}
QString AddonWorker::invoke(const QString& contribution, const QString& operation, const QJsonObject& context,
                           NetworkBroker::RequestGuard nativeCurrent)
{
    return invoke(contribution, operation, context, std::move(nativeCurrent), {});
}
QString AddonWorker::invoke(const QString& contribution, const QString& operation, const QJsonObject& context,
                           NetworkBroker::RequestGuard nativeCurrent, std::shared_ptr<AddonMetadataAttemptAdmission> attempt,
                           const QString& requestId)
{
    const QPointer<AddonWorker> alive(this);
    const auto id = requestId.isEmpty() || m_invocations.contains(requestId) ? QUuid::createUuid().toString(QUuid::WithoutBraces) : requestId;
    if (m_failed) {
        QTimer::singleShot(0, this, [this, id] { emit completed(id, QStringLiteral("error"), {}, tr("The add-on worker is not available.")); });
        return id;
    }
    const auto configuration = m_requestGuard;
    NetworkBroker::RequestGuard authority = [configuration, nativeCurrent = std::move(nativeCurrent)] {
        return NetworkBroker::guardCurrent(nativeCurrent) && NetworkBroker::guardCurrent(configuration);
    };
    const bool permitted = NetworkBroker::guardCurrent(authority);
    if (!alive) return id;
    if (!permitted) {
        QTimer::singleShot(0, this, [this, id] { emit completed(id, QStringLiteral("denied"), {}, tr("The add-on release, account, or settings changed.")); });
        return id;
    }
    const auto registration = m_package.manifest.contribution(contribution);
    if (!registration || !registration->operations.contains(operation) || !m_package.enabled || m_invocations.size() >= 32) {
        QTimer::singleShot(0, this, [this, id] { emit completed(id, QStringLiteral("unsupported"), {}, tr("This add-on operation is not available.")); });
        return id;
    }
    for (const auto& permission : registration->requires) if (!m_package.grants.contains(permission)) {
        QTimer::singleShot(0, this, [this, id] { emit completed(id, QStringLiteral("denied"), {}, tr("This add-on operation needs permission.")); });
        return id;
    }
    if (attempt) {
        for (auto it = m_attemptGates.begin(); it != m_attemptGates.end();) {
            const auto gate = it->lock();
            if (!gate || !gate->current()) it = m_attemptGates.erase(it); else ++it;
        }
        if (m_attemptGates.size() >= 1024 || !attempt->bindWorker(m_attemptWorkerId, id, contribution, operation, context)) {
            attempt->blocked();
            QTimer::singleShot(0, this, [this, id] { emit completed(id, QStringLiteral("denied"), {}, tr("The metadata request is not available.")); });
            return id;
        }
        m_attemptGates.insert(id, attempt->m_capture.admission);
    }
    auto* deadline = new QTimer(this); deadline->setSingleShot(true);
    auto active = std::make_shared<std::atomic_bool>(true);
    NetworkBroker::RequestGuard current = [authority, active] {
        if (!active->load()) return false;
        if (!NetworkBroker::guardCurrent(authority)) { active->store(false); return false; }
        return active->load();
    };
    m_invocations.insert(id, {contribution, deadline, {}, {}, {}, authority, std::move(current), std::move(active), std::move(attempt)});
    connect(deadline, &QTimer::timeout, this, [this] { fail(tr("The add-on used more time than the execution limit.")); });
    deadline->start(30000);
    send({{QStringLiteral("type"), QStringLiteral("invoke")}, {QStringLiteral("id"), id},
        {QStringLiteral("contribution"), contribution}, {QStringLiteral("operation"), operation},
        {QStringLiteral("context"), context}, {QStringLiteral("settings"), m_settings},
        {QStringLiteral("hostCapabilities"), m_hostCapabilities},
        {QStringLiteral("credentials"), m_credentials}});
    return id;
}
bool AddonWorker::invocationCurrent(const QString& id)
{
    const auto found = m_invocations.constFind(id);
    if (found == m_invocations.cend()) return false;
    const auto current = found->current;
    const QPointer<AddonWorker> alive(this);
    const bool permitted = NetworkBroker::guardCurrent(current);
    if (!alive || !m_invocations.contains(id)) return false;
    if (!permitted) { expire(id); return false; }
    return true;
}
void AddonWorker::expire(const QString& id)
{
    const QString selected = id;
    const QPointer<AddonWorker> alive(this);
    cancel(selected);
    if (!alive) return;
    QTimer::singleShot(0, this, [this, selected] {
        emit completed(selected, QStringLiteral("denied"), {}, tr("The add-on request is no longer available."));
    });
}

void AddonWorker::send(const QJsonObject& message)
{
    const auto type = message.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("invoke") && !invocationCurrent(message.value(QStringLiteral("id")).toString())) return;
    if (type == QLatin1String("host-result") && !invocationCurrent(message.value(QStringLiteral("invocation")).toString())) return;
    const auto bytes = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (bytes.size() > 1024 * 1024) { fail(tr("The add-on message is too large.")); return; }
    if (!m_ready) { m_queued.enqueue(message); start(); return; }
    if (m_process.bytesToWrite() > 2 * 1024 * 1024) { fail(tr("The add-on cannot process requests in time.")); return; }
    const auto written = m_process.write(bytes + '\n');
    if (type == QLatin1String("invoke")) {
        const auto id = message.value(QStringLiteral("id")).toString();
        const auto attempt = m_invocations.value(id).attempt;
        if (attempt && (written != bytes.size() + 1 || !attempt->dispatched(id))) attempt->blocked();
    }
}

void AddonWorker::read()
{
    if (m_stopping || m_failed) return;
    const QPointer<AddonWorker> alive(this);
    m_buffer += m_process.readAllStandardOutput();
    if (m_buffer.size() > 2 * 1024 * 1024) { fail(tr("The add-on sent too much data.")); return; }
    while (m_buffer.contains('\n')) {
        const auto end = m_buffer.indexOf('\n'); const auto line = m_buffer.left(end); m_buffer.remove(0, end + 1);
        if (line.size() > 1024 * 1024) { fail(tr("The add-on sent too much data.")); return; }
        QJsonParseError error; const auto document = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) { fail(tr("The add-on sent a message that is not correct.")); return; }
        const auto object = document.object();
        if (!m_ready) {
            if (object.value(QStringLiteral("type")).toString() != QLatin1String("ready")
                || object.value(QStringLiteral("protocol")).toString() != QLatin1String("wasm-message-v1")) {
                fail(tr("The app cannot use this add-on worker protocol.")); return;
            }
            m_ready = true; m_startup.stop();
            const auto pending = std::move(m_queued); m_queued.clear();
            for (const auto& message : pending) { send(message); if (!alive || m_stopping) return; }
            continue;
        }
        if (object.value(QStringLiteral("type")).toString() != QLatin1String("messages")
            || !object.value(QStringLiteral("messages")).isArray()
            || object.value(QStringLiteral("messages")).toArray().size() > 64) {
            fail(tr("The add-on sent a message that is not correct.")); return;
        }
        for (const auto& value : object.value(QStringLiteral("messages")).toArray()) {
            if (!value.isObject()) { fail(tr("The add-on sent a message that is not correct.")); return; }
            message(value.toObject());
            if (!alive || m_stopping) return;
        }
    }
}

void AddonWorker::message(const QJsonObject& object)
{
    if (m_stopping || m_failed) return;
    const QPointer<AddonWorker> alive(this);
    const bool configured = configurationCurrent();
    if (!alive) return;
    if (!configured) { fail(tr("The add-on release, account, or settings changed.")); return; }
    const auto type = object.value(QStringLiteral("type")).toString();
    const auto id = object.value(QStringLiteral("id")).toString();
    if (type == QLatin1String("result")) {
        if (!m_invocations.contains(id)) {
            if (m_prepared && m_retired.contains(id)) {
                fail(tr("The prepared add-on sent another terminal result.")); return;
            }
            // Keep ordinary stale-result handling, but retire a retained proof.
            if (const auto gate = m_attemptGates.value(id).lock()) gate->revoke();
            return;
        }
        if (!invocationCurrent(id)) return;
        static const QSet<QString> statuses = {QStringLiteral("match"), QStringLiteral("partial"), QStringLiteral("no_match"),
            QStringLiteral("auth_required"), QStringLiteral("rate_limited"), QStringLiteral("offline"), QStringLiteral("ambiguous"),
            QStringLiteral("unsupported"), QStringLiteral("error")};
        const auto status = object.value(QStringLiteral("status")).toString();
        if (!statuses.contains(status) || !object.value(QStringLiteral("data")).isObject()) { fail(tr("The add-on sent a result that is not correct.")); return; }
        if (const auto attempt = m_invocations.value(id).attempt) {
            static const QSet<QString> resultKeys{QStringLiteral("type"), QStringLiteral("id"), QStringLiteral("status"), QStringLiteral("data"), QStringLiteral("error")};
            bool clean = object.value(QStringLiteral("error")).isUndefined() || object.value(QStringLiteral("error")).isString();
            for (auto it = object.begin(); it != object.end(); ++it) if (!resultKeys.contains(it.key())) clean = false;
            if (!clean) attempt->blocked();
            else if (m_prepared) {
                // A prepared receipt cannot be taken before clean process EOF.
                if (m_preparedTerminal) { fail(tr("The prepared add-on sent another terminal result.")); return; }
                m_preparedTerminal = PreparedTerminal{id, status, object.value(QStringLiteral("error")).toString(),
                    object.value(QStringLiteral("data")).toObject(), attempt};
            } else attempt->terminal(id, status, object.value(QStringLiteral("data")).toObject(), object.value(QStringLiteral("error")).toString());
        }
        // Guest errors are untrusted strings: only a bounded plain-text value is exposed.
        finish(id, status, object.value(QStringLiteral("data")).toObject(), object.value(QStringLiteral("error")).toString().left(512));
        return;
    }
    const auto invocation = object.value(QStringLiteral("invocation")).toString();
    // A request can arrive after its invocation ended or was canceled.
    if (type == QLatin1String("host-call") && m_retired.contains(invocation)) {
        if (m_prepared) { fail(tr("The prepared add-on sent a request after its result.")); return; }
        if (const auto gate = m_attemptGates.value(invocation).lock()) gate->revoke();
        return;
    }
    if (type != QLatin1String("host-call") || id.isEmpty() || id.size() > 128
        || !m_invocations.contains(invocation)) { fail(tr("The add-on used a request ID that is not correct.")); return; }
    if (!invocationCurrent(invocation)) return;
    auto& active=m_invocations[invocation];
    if (active.hostCalls.contains(id)) { fail(tr("The add-on used a request ID that is not correct.")); return; }
    if (m_hostCallCount >= 4096) { fail(tr("The add-on sent too many requests.")); return; }
    active.hostCalls.insert(id); ++m_hostCallCount;
    const auto operation = object.value(QStringLiteral("operation")).toString();
    const auto attempt = active.attempt;
    if (attempt) attempt->hostStarted(invocation, id, operation);
    const auto observer = [selected = std::weak_ptr<AddonMetadataAttemptAdmission>(attempt), invocation, id](AddonMetadataHostObservation result) {
        if (const auto owned = selected.lock()) owned->hostFinished(invocation, id, result);
    };
    if (operation != QLatin1String("network.http") && m_services) {
        if (!object.value(QStringLiteral("request")).isObject()) { fail(tr("The add-on sent a host request that is not correct.")); return; }
        const auto contribution = QString(m_package.manifest.id + '/' + m_invocations.value(invocation).contribution);
        const auto weak = QPointer<AddonWorker>(this);
        const auto services = m_services;
        const auto current = m_invocations.value(invocation).current;
        const auto job = services->dispatch({m_package.manifest.id, contribution, invocation, m_package.grants, current}, operation,
            object.value(QStringLiteral("request")).toObject(), [weak, invocation, id](AddonHostServices::Result result) {
                if (!weak || weak->m_stopping || !weak->m_invocations.contains(invocation)) return;
                if (!weak->invocationCurrent(invocation)) return;
                weak->send({{"type", "host-result"}, {"id", id}, {"invocation", invocation},
                    {"status", result.error.isEmpty() ? QStringLiteral("ok") : QStringLiteral("error")}, {"response", result.response}, {"error", result.error}});
            }, 10000, observer);
        // A native handler can cancel this invocation during dispatch.
        if (!weak || weak->m_stopping || !weak->invocationCurrent(invocation)) {
            if (job && services) services->cancel(job);
            return;
        }
        if (job) m_invocations[invocation].serviceJobs.insert(job);
        else {
            if (attempt) attempt->hostFinished(invocation, id, {AddonMetadataHostTerminal::Unavailable, false, 0});
            send({{"type", "host-result"}, {"id", id}, {"invocation", invocation}, {"status", "error"}, {"error", tr("The add-on sent too many host requests.")}});
        }
        return;
    }
    if (operation != QLatin1String("network.http") || !m_package.grants.contains(operation)) {
        if (attempt) attempt->hostFinished(invocation, id, {AddonMetadataHostTerminal::Denied, false, 0});
        send({{QStringLiteral("type"), QStringLiteral("host-result")}, {QStringLiteral("id"), id},
            {QStringLiteral("invocation"), invocation}, {QStringLiteral("status"), QStringLiteral("error")},
            {QStringLiteral("error"), tr("This host capability is not available or needs permission.")}});
        return;
    }
    const auto weak = QPointer<AddonWorker>(this);
    const auto network = QPointer<NetworkBroker>(m_network);
    const auto current = m_invocations.value(invocation).current;
    const auto job = network->fetch(m_package.manifest, m_package.grants, object.value(QStringLiteral("request")).toObject(), m_resolver,
        [weak, invocation, id](const NetworkBroker::Response& response) {
            if (!weak || !weak->m_invocations.contains(invocation) || weak->m_stopping) return;
            if (!weak->invocationCurrent(invocation)) return;
            weak->send({{QStringLiteral("type"), QStringLiteral("host-result")}, {QStringLiteral("id"), id},
                {QStringLiteral("invocation"), invocation}, {QStringLiteral("status"), response.error.isEmpty() ? QStringLiteral("ok") : QStringLiteral("error")},
                {QStringLiteral("response"), QJsonObject{{QStringLiteral("status"), response.status},
                    {QStringLiteral("body"), QString::fromUtf8(response.body)}, {QStringLiteral("headers"), response.headers},
                    {QStringLiteral("truncated"), response.truncated}}},
                {QStringLiteral("error"), response.error}});
        }, 512 * 1024, NetworkBroker::RequestKind::Document, current, observer);
    if (!weak || weak->m_stopping || !weak->invocationCurrent(invocation)) {
        if (network) network->cancel(job);
        return;
    }
    m_invocations[invocation].jobs.insert(job);
}

void AddonWorker::finish(const QString& id, const QString& status, const QJsonObject& data, const QString& error)
{
    const QPointer<AddonWorker> alive(this);
    if (!m_invocations.contains(id)) return;
    const auto invocation = m_invocations.take(id);
    invocation.active->store(false);
    Q_ASSERT(m_hostCallCount>=invocation.hostCalls.size()); m_hostCallCount-=invocation.hostCalls.size(); rememberRetired(id);
    if (invocation.deadline) { invocation.deadline->stop(); invocation.deadline->deleteLater(); }
    for (auto job : invocation.jobs) { m_network->cancel(job); if (!alive) return; }
    if (m_services) for (auto job : invocation.serviceJobs) { m_services->cancel(job); if (!alive) return; }
    QTimer::singleShot(0, this, [this, id, status, data, error, authority = invocation.authority] {
        const auto current = authority;
        const QPointer<AddonWorker> alive(this);
        const bool permitted = NetworkBroker::guardCurrent(current);
        if (!alive) return;
        if (!permitted) emit completed(id, QStringLiteral("denied"), {}, tr("The add-on request is no longer available."));
        else emit completed(id, status, data, error);
    });
}
void AddonWorker::cancel(const QString& id)
{
    const QPointer<AddonWorker> alive(this);
    if (!m_invocations.contains(id)) return;
    const auto invocation = m_invocations.take(id);
    invocation.active->store(false);
    if (invocation.attempt) { invocation.attempt->blocked(); invocation.attempt->revoke(); }
    Q_ASSERT(m_hostCallCount>=invocation.hostCalls.size()); m_hostCallCount-=invocation.hostCalls.size(); rememberRetired(id);
    if (invocation.deadline) { invocation.deadline->stop(); invocation.deadline->deleteLater(); }
    for (auto job : invocation.jobs) { m_network->cancel(job); if (!alive) return; }
    if (m_services) for (auto job : invocation.serviceJobs) { m_services->cancel(job); if (!alive) return; }
    QQueue<QJsonObject> retained;
    while (!m_queued.isEmpty()) { const auto message = m_queued.dequeue(); if (message.value(QStringLiteral("id")).toString() != id) retained.enqueue(message); }
    m_queued = retained;
    if (m_ready && !m_stopping && m_process.state() == QProcess::Running)
        send({{QStringLiteral("type"), QStringLiteral("cancel")}, {QStringLiteral("id"), id}});
}
void AddonWorker::revokeAttemptGates()
{
    const auto gates = std::exchange(m_attemptGates, {});
    for (const auto& weak : gates) if (const auto gate = weak.lock()) gate->revoke();
}
void AddonWorker::rememberRetired(const QString& id)
{
    if (m_retired.contains(id)) return;
    m_retired.insert(id); m_retiredOrder.enqueue(id);
    while (m_retiredOrder.size()>2048) m_retired.remove(m_retiredOrder.dequeue());
}
void AddonWorker::fail(const QString& error)
{
    if (m_stopping) return;
    qWarning("Add-on %s stopped: %s", qUtf8Printable(m_package.manifest.id), qUtf8Printable(error));
    revokeAttemptGates();
    m_failed = true; m_stopping = true; m_ready = false; m_startup.stop(); m_queued.clear();
    if (m_process.state() != QProcess::NotRunning) m_process.kill();
    const auto ids = m_invocations.keys();
    const QString failure = error;
    const QPointer<AddonWorker> alive(this);
    // Observers can destroy this owner. Leave the QProcess signal stack first.
    QTimer::singleShot(0, this, [alive, ids, failure] {
        if (!alive) return;
        for (const auto& id : ids) {
            alive->finish(id, QStringLiteral("error"), {}, failure);
            if (!alive) return;
        }
        emit alive->unavailable(failure);
    });
}
void AddonWorker::stop()
{
    if (m_prepared) {
        Q_ASSERT(m_process.state() == QProcess::NotRunning);
        revokeAttemptGates();
        return;
    }
    const QPointer<AddonWorker> alive(this);
    revokeAttemptGates();
    m_stopping = true; m_startup.stop(); m_ready = false; m_queued.clear();
    const auto ids = m_invocations.keys(); for (const auto& id : ids) { cancel(id); if (!alive) return; }
    if (m_process.state() != QProcess::NotRunning) { m_process.kill(); m_process.waitForFinished(1000); }
}
}
