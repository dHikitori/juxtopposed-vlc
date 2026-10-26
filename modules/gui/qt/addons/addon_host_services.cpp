// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_host_services.hpp"
#include <QJsonArray>
#include <QMetaObject>
#include <cmath>

namespace vlc::addons {
namespace {
bool bounded(const QJsonValue& value, int& nodes, int& bytes, int depth = 0)
{
    if (++nodes > 4096 || depth > 16) return false;
    if (value.isString()) { if (value.toString().size() > 65536) return false; bytes += int(value.toString().size()) * 3; }
    else if (value.isDouble() && !std::isfinite(value.toDouble())) return false;
    else if (value.isObject()) {
        const auto object = value.toObject(); if (object.size() > 256) return false;
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (it.key().size() > 256) return false;
            bytes += int(it.key().size()) * 3;
            if (bytes > 256 * 1024 || !bounded(it.value(), nodes, bytes, depth + 1)) return false;
        }
    } else if (value.isArray()) {
        const auto array = value.toArray(); if (array.size() > 1024) return false;
        for (const auto& child : array) if (!bounded(child, nodes, bytes, depth + 1)) return false;
    }
    return bytes <= 256 * 1024;
}
bool bounded(const QJsonObject& object) { int nodes = 0, bytes = 0; return bounded(object, nodes, bytes); }
}
AddonHostServices::AddonHostServices(QObject* parent) : QObject(parent), m_gate(std::make_shared<Gate>())
{ m_gate->owner = this; }
AddonHostServices::~AddonHostServices() { close(); }
QString AddonHostServices::capability(const QString& operation)
{
    static const QHash<QString, QString> capabilities{
        {"storage.get", "storage.private"}, {"storage.set", "storage.private"}, {"storage.remove", "storage.private"},
        {"library.query", "library.read"}, {"queue.snapshot", "queue.read"}, {"queue.apply", "queue.modify"},
        {"playback.observe", "playback.observe"}, {"playback.command", "playback.control"},
        {"session.renew", "playback.observe"}, {"session.end", "playback.observe"},
        {"network.websocket.open", "network.websocket"}, {"network.websocket.send", "network.websocket"}, {"network.websocket.close", "network.websocket"},
        {"ui.state.patch", "ui.contribute"}, {"ui.navigate", "ui.contribute"}, {"ui.modal.open", "ui.contribute"}, {"ui.modal.close", "ui.contribute"},
        {"timer.schedule", "session.background"}, {"timer.cancel", "session.background"}
    };
    return capabilities.value(operation);
}
bool AddonHostServices::registerOperation(const QString& operation, QObject* lifetime, Handler handler)
{
    if (m_closing || !lifetime || !handler || capability(operation).isEmpty() || m_handlers.contains(operation)) return false;
    m_handlers.insert(operation, {lifetime, std::move(handler)});
    connect(lifetime, &QObject::destroyed, this, [this, operation] { unregisterOperation(operation); });
    emit availabilityChanged(); return true;
}
QStringList AddonHostServices::availableOperations() const
{
    QStringList result;
    if (m_closing) return result;
    for (auto it = m_handlers.begin(); it != m_handlers.end(); ++it)
        if (it.value().lifetime) result.append(it.key());
    result.sort(); return result;
}
quint64 AddonHostServices::dispatch(const Context& context, const QString& operation, const QJsonObject& request, Completion completion, int timeoutMs)
{
    if (m_closing) return 0;
    const QPointer<AddonHostServices> alive(this);
    int count = 0;
    for (const auto& job : m_jobs) if (job.package == context.package) ++count;
    if (count >= 32 || m_jobs.size() >= 128) return 0;
    const auto id = ++m_next; auto* timer = new QTimer(this); timer->setSingleShot(true);
    const auto registration = m_handlers.value(operation);
    m_jobs.insert(id, {context.package, operation, registration.lifetime, timer, std::move(completion), {}, context.current});
    QString error;
    const auto cap = capability(operation); const auto scope = context.grants.value(cap);
    if (cap.isEmpty() || !scope.isObject()) error = tr("This host operation needs permission.");
    else if (!registration.lifetime || !registration.handler) error = tr("This host operation is not available.");
    else if (context.package.isEmpty() || context.package.size() > 128 || context.contribution.size() > 256 || context.invocation.isEmpty()
        || context.invocation.size() > 128 || !context.contribution.startsWith(context.package + '/')) error = tr("The host request identity is not correct.");
    else if (!bounded(request)) error = tr("The host request is too large.");
    else if ((operation == QLatin1String("playback.command") || operation == QLatin1String("queue.apply"))
        && !scope.toObject().value("operations").toArray().contains(request.value("operation"))) error = tr("This operation is outside the permitted scope.");
    if (error.isEmpty() && context.current && !context.current()) error = tr("The host request configuration changed.");
    if (!alive || !m_jobs.contains(id)) return id;
    if (!error.isEmpty()) { QTimer::singleShot(0, this, [this, id, error] { finish(id, {{}, error}); }); return id; }
    connect(timer, &QTimer::timeout, this, [this, id] { finish(id, {{}, tr("The host operation time limit was reached.")}, true); });
    timer->start(qBound(1, timeoutMs, 30000));
    const auto gate = m_gate;
    auto cancel = registration.handler(context, request, [gate, id](Result result) {
        std::lock_guard<std::mutex> lock(gate->mutex);
        if (gate->owner) QMetaObject::invokeMethod(gate->owner, [owner = gate->owner, id, result = std::move(result)]() mutable { owner->finish(id, std::move(result)); }, Qt::QueuedConnection);
    });
    if (!alive) {
        if (registration.lifetime && cancel) cancel();
        return id;
    }
    if (m_jobs.contains(id)) m_jobs[id].cancel = std::move(cancel);
    else if (registration.lifetime && cancel) cancel();
    return id;
}
void AddonHostServices::finish(quint64 id, Result result, bool abort)
{
    if (!m_jobs.contains(id)) return;
    const auto job = m_jobs.take(id);
    if (job.deadline) { job.deadline->stop(); job.deadline->deleteLater(); }
    if (abort && job.lifetime && job.cancel) job.cancel();
    if (result.error.isEmpty() && job.current && !job.current()) {
        result.response = {}; result.error = tr("The host request configuration changed.");
    }
    if (!bounded(result.response)) { result.response = {}; result.error = tr("The host response is too large."); }
    result.error = result.error.left(512);
    if (job.completion) job.completion(std::move(result));
}
void AddonHostServices::cancel(quint64 id)
{ finish(id, {{}, tr("VLC canceled the host operation.")}, true); }
void AddonHostServices::cancelPackage(const QString& package)
{ const auto ids = m_jobs.keys(); for (const auto id : ids) if (m_jobs.value(id).package == package) cancel(id); }
void AddonHostServices::unregisterOperation(const QString& operation)
{
    QPointer<AddonHostServices> guard(this);
    m_handlers.remove(operation); const auto ids = m_jobs.keys();
    for (const auto id : ids) {
        if (m_jobs.value(id).operation == operation) finish(id, {{}, tr("This host operation is not available.")}, true);
        if (!guard) return;
    }
    emit availabilityChanged();
}
void AddonHostServices::close()
{
    if (m_closing) return;
    QPointer<AddonHostServices> guard(this);
    m_closing = true;
    { std::lock_guard<std::mutex> lock(m_gate->mutex); m_gate->owner = nullptr; }
    const auto ids = m_jobs.keys(); for (const auto id : ids) { cancel(id); if (!guard) return; }
    m_handlers.clear();
    emit availabilityChanged();
}
}
