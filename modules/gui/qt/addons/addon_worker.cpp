// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_worker.hpp"
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
    m_startup.setSingleShot(true);
    connect(&m_startup, &QTimer::timeout, this, [this] { fail(tr("The add-on did not start before the time limit.")); });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &AddonWorker::read);
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] { m_process.readAllStandardError(); });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (!m_stopping) fail(tr("The add-on worker is not available."));
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int, QProcess::ExitStatus) { if (!m_stopping) fail(tr("The add-on stopped because of an error.")); });
}
AddonWorker::~AddonWorker() { stop(); }
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
    m_hostCalls.clear(); m_cancelled.clear(); m_cancelledOrder.clear();
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
    m_process.setArguments({QDir(m_package.directory).filePath(m_package.manifest.runtimeEntry)});
    m_startup.start(15000);
    m_process.start();
}

QString AddonWorker::invoke(const QString& contribution, const QString& operation, const QJsonObject& context)
{
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto registration = m_package.manifest.contribution(contribution);
    if (!configurationCurrent()) {
        QTimer::singleShot(0, this, [this, id] { emit completed(id, QStringLiteral("denied"), {}, tr("The add-on release, account, or settings changed.")); });
        return id;
    }
    if (!registration || !registration->operations.contains(operation) || !m_package.enabled || m_invocations.size() >= 32) {
        QTimer::singleShot(0, this, [this, id] { emit completed(id, QStringLiteral("unsupported"), {}, tr("This add-on operation is not available.")); });
        return id;
    }
    for (const auto& permission : registration->requires) if (!m_package.grants.contains(permission)) {
        QTimer::singleShot(0, this, [this, id] { emit completed(id, QStringLiteral("denied"), {}, tr("This add-on operation needs permission.")); });
        return id;
    }
    auto* deadline = new QTimer(this); deadline->setSingleShot(true);
    m_invocations.insert(id, {contribution, deadline, {}, {}});
    connect(deadline, &QTimer::timeout, this, [this] { fail(tr("The add-on used more time than the execution limit.")); });
    deadline->start(30000);
    send({{QStringLiteral("type"), QStringLiteral("invoke")}, {QStringLiteral("id"), id},
        {QStringLiteral("contribution"), contribution}, {QStringLiteral("operation"), operation},
        {QStringLiteral("context"), context}, {QStringLiteral("settings"), m_settings},
        {QStringLiteral("hostCapabilities"), m_hostCapabilities},
        {QStringLiteral("credentials"), m_credentials}});
    return id;
}

void AddonWorker::send(const QJsonObject& message)
{
    const auto bytes = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (bytes.size() > 1024 * 1024) { fail(tr("The add-on message is too large.")); return; }
    if (!m_ready) { m_queued.enqueue(message); start(); return; }
    if (m_process.bytesToWrite() > 2 * 1024 * 1024) { fail(tr("The add-on cannot process requests in time.")); return; }
    m_process.write(bytes + '\n');
}

void AddonWorker::read()
{
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
                fail(tr("VLC cannot use this add-on worker protocol.")); return;
            }
            m_ready = true; m_startup.stop();
            const auto pending = std::move(m_queued); m_queued.clear();
            for (const auto& message : pending) send(message);
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
            if (m_stopping) return;
        }
    }
}

void AddonWorker::message(const QJsonObject& object)
{
    if (!configurationCurrent()) { fail(tr("The add-on release, account, or settings changed.")); return; }
    const auto type = object.value(QStringLiteral("type")).toString();
    const auto id = object.value(QStringLiteral("id")).toString();
    if (type == QLatin1String("result")) {
        if (!m_invocations.contains(id)) return; // Cancelled/stale terminal result.
        static const QSet<QString> statuses = {QStringLiteral("match"), QStringLiteral("partial"), QStringLiteral("no_match"),
            QStringLiteral("auth_required"), QStringLiteral("rate_limited"), QStringLiteral("offline"), QStringLiteral("ambiguous"),
            QStringLiteral("unsupported"), QStringLiteral("error")};
        const auto status = object.value(QStringLiteral("status")).toString();
        if (!statuses.contains(status) || !object.value(QStringLiteral("data")).isObject()) { fail(tr("The add-on sent a result that is not correct.")); return; }
        // Guest errors are untrusted strings: only a bounded plain-text value is exposed.
        finish(id, status, object.value(QStringLiteral("data")).toObject(), object.value(QStringLiteral("error")).toString().left(512));
        return;
    }
    const auto invocation = object.value(QStringLiteral("invocation")).toString();
    // A request already in transit can arrive after host cancellation.
    if (type == QLatin1String("host-call") && m_cancelled.contains(invocation)) return;
    if (type != QLatin1String("host-call") || id.isEmpty() || id.size() > 128 || m_hostCalls.contains(id)
        || !m_invocations.contains(invocation)) { fail(tr("The add-on used a request ID that is not correct.")); return; }
    if (m_hostCalls.size() >= 4096) { fail(tr("The add-on sent too many requests.")); return; }
    m_hostCalls.insert(id);
    const auto operation = object.value(QStringLiteral("operation")).toString();
    if (operation != QLatin1String("network.http") && m_services) {
        if (!object.value(QStringLiteral("request")).isObject()) { fail(tr("The add-on sent a host request that is not correct.")); return; }
        const auto contribution = QString(m_package.manifest.id + '/' + m_invocations.value(invocation).contribution);
        const auto weak = QPointer<AddonWorker>(this);
        const auto services = m_services;
        const auto job = services->dispatch({m_package.manifest.id, contribution, invocation, m_package.grants, m_requestGuard}, operation,
            object.value(QStringLiteral("request")).toObject(), [weak, invocation, id](AddonHostServices::Result result) {
                if (!weak || weak->m_stopping || !weak->m_invocations.contains(invocation)) return;
                weak->send({{"type", "host-result"}, {"id", id}, {"invocation", invocation},
                    {"status", result.error.isEmpty() ? QStringLiteral("ok") : QStringLiteral("error")}, {"response", result.response}, {"error", result.error}});
            });
        // A native handler can cancel this invocation during dispatch.
        if (!weak || weak->m_stopping || !weak->m_invocations.contains(invocation)) {
            if (job && services) services->cancel(job);
            return;
        }
        if (job) m_invocations[invocation].serviceJobs.insert(job);
        else send({{"type", "host-result"}, {"id", id}, {"invocation", invocation}, {"status", "error"}, {"error", tr("The add-on sent too many host requests.")}});
        return;
    }
    if (operation != QLatin1String("network.http") || !m_package.grants.contains(operation)) {
        send({{QStringLiteral("type"), QStringLiteral("host-result")}, {QStringLiteral("id"), id},
            {QStringLiteral("invocation"), invocation}, {QStringLiteral("status"), QStringLiteral("error")},
            {QStringLiteral("error"), tr("This host capability is not available or needs permission.")}});
        return;
    }
    const auto weak = QPointer<AddonWorker>(this);
    const auto network = QPointer<NetworkBroker>(m_network);
    const auto job = network->fetch(m_package.manifest, m_package.grants, object.value(QStringLiteral("request")).toObject(), m_resolver,
        [weak, invocation, id](const NetworkBroker::Response& response) {
            if (!weak || !weak->m_invocations.contains(invocation) || weak->m_stopping) return;
            weak->send({{QStringLiteral("type"), QStringLiteral("host-result")}, {QStringLiteral("id"), id},
                {QStringLiteral("invocation"), invocation}, {QStringLiteral("status"), response.error.isEmpty() ? QStringLiteral("ok") : QStringLiteral("error")},
                {QStringLiteral("response"), QJsonObject{{QStringLiteral("status"), response.status},
                    {QStringLiteral("body"), QString::fromUtf8(response.body)}, {QStringLiteral("headers"), response.headers},
                    {QStringLiteral("truncated"), response.truncated}}},
                {QStringLiteral("error"), response.error}});
        }, 512 * 1024, NetworkBroker::RequestKind::Document, m_requestGuard);
    if (!weak || weak->m_stopping || !weak->m_invocations.contains(invocation)) {
        if (network) network->cancel(job);
        return;
    }
    m_invocations[invocation].jobs.insert(job);
}

void AddonWorker::finish(const QString& id, const QString& status, const QJsonObject& data, const QString& error)
{
    const auto invocation = m_invocations.take(id);
    if (invocation.deadline) { invocation.deadline->stop(); invocation.deadline->deleteLater(); }
    for (auto job : invocation.jobs) m_network->cancel(job);
    if (m_services) for (auto job : invocation.serviceJobs) m_services->cancel(job);
    emit completed(id, status, data, error);
}
void AddonWorker::cancel(const QString& id)
{
    if (!m_invocations.contains(id)) return;
    m_cancelled.insert(id); m_cancelledOrder.enqueue(id);
    while (m_cancelledOrder.size() > 2048) m_cancelled.remove(m_cancelledOrder.dequeue());
    const auto invocation = m_invocations.take(id);
    if (invocation.deadline) { invocation.deadline->stop(); invocation.deadline->deleteLater(); }
    for (auto job : invocation.jobs) m_network->cancel(job);
    if (m_services) for (auto job : invocation.serviceJobs) m_services->cancel(job);
    QQueue<QJsonObject> retained;
    while (!m_queued.isEmpty()) { const auto message = m_queued.dequeue(); if (message.value(QStringLiteral("id")).toString() != id) retained.enqueue(message); }
    m_queued = retained;
    if (m_ready && !m_stopping && m_process.state() == QProcess::Running)
        send({{QStringLiteral("type"), QStringLiteral("cancel")}, {QStringLiteral("id"), id}});
}
void AddonWorker::fail(const QString& error)
{
    if (m_stopping) return;
    m_stopping = true; m_ready = false; m_startup.stop(); m_queued.clear();
    if (m_process.state() != QProcess::NotRunning) m_process.kill();
    const auto ids = m_invocations.keys();
    for (const auto& id : ids) finish(id, QStringLiteral("error"), {}, error);
    emit unavailable(error);
}
void AddonWorker::stop()
{
    m_stopping = true; m_startup.stop(); m_ready = false; m_queued.clear();
    const auto ids = m_invocations.keys(); for (const auto& id : ids) cancel(id);
    if (m_process.state() != QProcess::NotRunning) { m_process.kill(); m_process.waitForFinished(1000); }
}
}
