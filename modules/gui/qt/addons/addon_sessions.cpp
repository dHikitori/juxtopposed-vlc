// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_sessions.hpp"
#include <QUuid>
#include <cmath>

namespace vlc::addons {
namespace {
QString token() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool numeric(const QVariant& value)
{
    const int type = value.metaType().id();
    return type == QMetaType::Int || type == QMetaType::UInt || type == QMetaType::LongLong
        || type == QMetaType::ULongLong || type == QMetaType::Double || type == QMetaType::Float;
}
}
AddonSessions::AddonSessions(Backend backend, QObject* parent) : QObject(parent), m_backend(std::move(backend))
{
    m_clock.start(); m_expiry.setInterval(250);
    connect(&m_expiry, &QTimer::timeout, this, &AddonSessions::expire); m_expiry.start();
}
AddonSessions::~AddonSessions()
{ close(); }
void AddonSessions::close()
{
    if (m_closing) return;
    m_closing = true; m_expiry.stop();
    const auto leases = m_leases;
    for (auto it = leases.begin(); it != leases.end(); ++it) end(it->package, it.key());
}
qint64 AddonSessions::now() const { return m_backend.monotonicUs ? m_backend.monotonicUs() : m_clock.nsecsElapsed() / 1000; }
QString AddonSessions::authorize(const QString& package, const QStringList& operations, int lifetimeMs)
{
    expire();
    static const QSet<QString> supported{QStringLiteral("play"), QStringLiteral("pause"), QStringLiteral("seek"), QStringLiteral("setRate")};
    if (m_closing || package.isEmpty() || package.size() > 128 || m_leases.size() >= 32 || !m_backend.snapshot || !m_backend.apply) return {};
    for (const auto& operation : operations) if (!supported.contains(operation)) return {};
    if (!operations.isEmpty() && !m_controller.isEmpty()) return {}; // User must end/switch explicitly.
    const auto snapshot = m_backend.snapshot(); const auto epoch = snapshot.value("mediaEpoch").toString();
    if (epoch.isEmpty()) return {};
    const auto id = token(); Lease lease;
    lease.package = package; lease.mediaEpoch = epoch;
    lease.operations = QSet<QString>(operations.begin(), operations.end());
    lease.expires = now() + qint64(qBound(1000, lifetimeMs, 30000)) * 1000;
    m_leases.insert(id, lease); if (!operations.isEmpty()) m_controller = id;
    emit sessionChanged(id, QStringLiteral("active")); return id;
}
bool AddonSessions::renew(const QString& package, const QString& id, int lifetimeMs)
{
    expire(); auto found = m_leases.find(id);
    if (m_closing || found == m_leases.end() || found->package != package || found->suspended || found->ending) return false;
    found->expires = now() + qint64(qBound(1000, lifetimeMs, 30000)) * 1000; return true;
}
bool AddonSessions::resume(const QString& package, const QString& id)
{
    expire(); auto found = m_leases.find(id);
    if (m_closing || found == m_leases.end() || found->package != package || found->ending || !found->suspended
        || (!m_controller.isEmpty() && m_controller != id)
        || m_backend.snapshot().value("mediaEpoch").toString() != found->mediaEpoch) return false;
    found->suspended = false; if (!found->operations.isEmpty()) m_controller = id;
    emit sessionChanged(id, QStringLiteral("active")); return true;
}
void AddonSessions::restoreRate(const QString& id)
{
    auto found = m_leases.find(id); if (found == m_leases.end()) return;
    if (found->restore) { found->restore->stop(); found->restore->deleteLater(); found->restore = nullptr; }
    if (!found->ownsRate) return;
    const auto snapshot = m_backend.snapshot();
    const bool stillOwned = snapshot.value("mediaEpoch").toString() == found->mediaEpoch
        && std::abs(snapshot.value("rate").toDouble() - found->correctedRate) < 0.000001;
    const double base = found->baseRate; found->ownsRate = false;
    if (stillOwned) m_backend.apply(QStringLiteral("setRate"), {{"rate", base}}, id);
}
void AddonSessions::end(const QString& package, const QString& id)
{
    const auto found = m_leases.constFind(id); if (found == m_leases.cend() || found->package != package || found->ending) return;
    m_leases[id].ending = true;
    const auto requests = m_pending.keys();
    for (const auto& request : requests) if (m_pending.value(request).lease == id) finish(request, QStringLiteral("cancelled"));
    restoreRate(id); m_leases.remove(id); if (m_controller == id) m_controller.clear();
    emit sessionChanged(id, QStringLiteral("ended"));
}
void AddonSessions::revokePackage(const QString& package)
{ const auto ids = m_leases.keys(); for (const auto& id : ids) if (m_leases.value(id).package == package) end(package, id); }
void AddonSessions::localIntervention(const QString& operation)
{
    const auto id = m_controller; if (id.isEmpty() || !m_leases.contains(id)) return;
    if (operation == QLatin1String("setRate") || operation.isEmpty()) {
        auto& lease = m_leases[id]; lease.ownsRate = false;
        if (lease.restore) { lease.restore->stop(); lease.restore->deleteLater(); lease.restore = nullptr; }
    } else restoreRate(id);
    auto found = m_leases.find(id);
    if (found == m_leases.end() || found->ending) return;
    found->suspended = true; m_controller.clear();
    const auto requests = m_pending.keys();
    for (const auto& request : requests) if (m_pending.value(request).lease == id) finish(request, QStringLiteral("cancelled"));
    emit sessionChanged(id, QStringLiteral("suspended"));
}
QVariantMap AddonSessions::observe(const QString& package, const QString& id) const
{
    const auto found = m_leases.constFind(id);
    if (m_closing || found == m_leases.cend() || found->package != package || found->ending || found->expires <= now()) return {};
    const auto source = m_backend.snapshot(); QVariantMap result;
    if (source.value("mediaEpoch").toString() != found->mediaEpoch) return {};
    for (const auto& key : {QStringLiteral("mediaEpoch"), QStringLiteral("positionUs"), QStringLiteral("durationUs"),
            QStringLiteral("rate"), QStringLiteral("paused"), QStringLiteral("buffering"), QStringLiteral("seekable"),
            QStringLiteral("live"), QStringLiteral("liveStartUs"), QStringLiteral("liveEndUs"), QStringLiteral("minimumRate"),
            QStringLiteral("maximumRate"), QStringLiteral("transportRevision"), QStringLiteral("discontinuity"),
            QStringLiteral("outputLatencyUs"), QStringLiteral("outputLatencyUncertaintyUs")}) {
        if (!source.contains(key)) continue;
        const auto value = source.value(key);
        if (key == QLatin1String("mediaEpoch")) result.insert(key, value.toString().left(128));
        else if (value.metaType().id() == QMetaType::Bool) result.insert(key, value);
        else if (numeric(value) && std::isfinite(value.toDouble())) result.insert(key, value.toDouble() < 0 ? QVariant{} : value);
    }
    result.insert("observedAtUs", now()); result.insert("suspended", found->suspended); return result;
}
QString AddonSessions::validate(const QString& package, const QString& id, const QVariantMap& command) const
{
    const auto found = m_leases.constFind(id);
    if (m_closing || found == m_leases.cend() || found->package != package || found->expires <= now() || found->suspended || found->ending || m_controller != id)
        return tr("This playback session does not have permission to control the player.");
    const auto operation = command.value("operation").toString(), commandId = command.value("commandId").toString();
    if (!found->operations.contains(operation) || commandId.isEmpty() || commandId.size() > 128)
        return tr("This playback operation needs permission.");
    const auto snapshot = m_backend.snapshot();
    if (command.value("mediaEpoch").toString() != found->mediaEpoch || snapshot.value("mediaEpoch").toString() != found->mediaEpoch)
        return tr("The media changed.");
    if (!numeric(command.value("expectedRevision")) || command.value("expectedRevision").toDouble() < 0
        || command.value("expectedRevision").toDouble() != command.value("expectedRevision").toULongLong()
        || command.value("expectedRevision").toULongLong() != snapshot.value("transportRevision").toULongLong())
        return tr("The playback state changed.");
    const auto parameters = command.value("parameters").toMap();
    for (auto it = parameters.begin(); it != parameters.end(); ++it)
        if ((operation == QLatin1String("seek") && it.key() != QLatin1String("positionUs"))
            || (operation == QLatin1String("setRate") && it.key() != QLatin1String("rate") && it.key() != QLatin1String("durationMs"))
            || (operation != QLatin1String("seek") && operation != QLatin1String("setRate"))) return tr("The app cannot use these playback parameters.");
    if (operation == QLatin1String("seek")) {
        const auto value = parameters.value("positionUs");
        if (!snapshot.value("seekable").toBool() || !numeric(value) || !std::isfinite(value.toDouble()) || value.toDouble() != value.toLongLong()) return tr("The app cannot use that playback position.");
        const auto position = value.toLongLong();
        const auto first = snapshot.value("live").toBool() ? snapshot.value("liveStartUs").toLongLong() : 0;
        const auto last = snapshot.value("live").toBool() ? snapshot.value("liveEndUs").toLongLong() : snapshot.value("durationUs").toLongLong();
        if (position < first || position > last || last <= first) return tr("The selected position is outside the media range.");
    }
    if (operation == QLatin1String("setRate")) {
        const auto value = parameters.value("rate"); const auto rate = value.toDouble();
        const double base = found->ownsRate ? found->baseRate : snapshot.value("rate", 1.0).toDouble();
        if (!numeric(value) || !std::isfinite(rate) || rate < base * 0.95 || rate > base * 1.05
            || rate < snapshot.value("minimumRate", 1.0).toDouble() || rate > snapshot.value("maximumRate", 1.0).toDouble()
            || !numeric(parameters.value("durationMs")) || parameters.value("durationMs").toDouble() != parameters.value("durationMs").toInt()
            || parameters.value("durationMs").toInt() < 1 || parameters.value("durationMs").toInt() > 5000)
            return tr("The app cannot use this speed correction.");
    }
    return {};
}
QString AddonSessions::submit(const QString& package, const QString& id, const QVariantMap& command)
{
    const auto request = token(); QString error = validate(package, id, command);
    auto found = m_leases.find(id); const auto commandId = command.value("commandId").toString();
    if (error.isEmpty() && (found->seen.contains(commandId) || found->seen.size() >= 4096 || m_pending.size() >= 64)) error = tr("The app received this command before, or a session limit prevents it.");
    const auto at = command.value("atUs", now()); const auto deadline = at.toLongLong();
    if (error.isEmpty() && (!numeric(at) || !std::isfinite(at.toDouble()) || at.toDouble() != deadline
        || deadline < now() - 200000 || deadline > now() + 10000000)) error = tr("The command time is outside the permitted range.");
    if (!error.isEmpty()) {
        QTimer::singleShot(0, this, [this, request, error] { emit commandFinished(request, QStringLiteral("rejected"), error); }); return request;
    }
    found->seen.insert(commandId);
    auto* timer = new QTimer(this); timer->setSingleShot(true); timer->setTimerType(Qt::PreciseTimer);
    auto scheduled = command; scheduled.insert("atUs", deadline);
    m_pending.insert(request, {package, id, scheduled, timer});
    connect(timer, &QTimer::timeout, this, [this, request] { execute(request); });
    timer->start(int(qMax<qint64>(0, (deadline - now()) / 1000))); return request;
}
void AddonSessions::execute(const QString& request)
{
    if (!m_pending.contains(request)) return;
    const auto pending = m_pending.value(request); auto error = validate(pending.package, pending.lease, pending.command);
    if (error.isEmpty() && pending.command.value("atUs").toLongLong() < now() - 200000)
        error = tr("The command time limit was reached.");
    if (!error.isEmpty()) { finish(request, QStringLiteral("rejected"), error); return; }
    const auto operation = pending.command.value("operation").toString(); const auto parameters = pending.command.value("parameters").toMap();
    if (operation == QLatin1String("setRate")) {
        auto& lease = m_leases[pending.lease];
        if (!lease.ownsRate) lease.baseRate = m_backend.snapshot().value("rate", 1.0).toDouble();
        if (lease.restore) { lease.restore->stop(); lease.restore->deleteLater(); }
        lease.ownsRate = true; lease.correctedRate = parameters.value("rate").toDouble();
    }
    if (!m_backend.apply(operation, parameters, pending.lease)) {
        if (operation == QLatin1String("setRate")) restoreRate(pending.lease);
        finish(request, QStringLiteral("error"), tr("The player cannot do this command.")); return;
    }
    if (!m_pending.contains(request) || !m_leases.contains(pending.lease) || m_leases.value(pending.lease).suspended) {
        if (operation == QLatin1String("setRate")) restoreRate(pending.lease);
        return;
    }
    if (operation == QLatin1String("setRate")) {
        auto& lease = m_leases[pending.lease]; lease.ownsRate = true; lease.correctedRate = parameters.value("rate").toDouble();
        auto* timer = new QTimer(this); timer->setSingleShot(true); lease.restore = timer;
        connect(timer, &QTimer::timeout, this, [this, id = pending.lease] { restoreRate(id); }); timer->start(parameters.value("durationMs").toInt());
    }
    finish(request, QStringLiteral("applied"));
}
void AddonSessions::finish(const QString& request, const QString& status, const QString& error)
{
    if (!m_pending.contains(request)) return;
    const auto pending = m_pending.take(request);
    if (pending.timer) { pending.timer->stop(); pending.timer->deleteLater(); }
    emit commandFinished(request, status, error);
}
void AddonSessions::cancel(const QString& package, const QString& request)
{ if (m_pending.value(request).package == package && m_pending.contains(request)) finish(request, QStringLiteral("cancelled")); }
void AddonSessions::expire()
{
    const auto ids = m_leases.keys();
    for (const auto& id : ids) {
        const auto lease = m_leases.value(id);
        if (lease.expires <= now() || (m_backend.snapshot && m_backend.snapshot().value("mediaEpoch").toString() != lease.mediaEpoch)) end(lease.package, id);
    }
}
}
