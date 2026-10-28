// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_playable_gate.hpp"
#include <QDateTime>
#include <QJsonArray>
#include <QUuid>
#include <cmath>

namespace vlc::addons {
namespace { QString token() { return QUuid::createUuid().toString(QUuid::WithoutBraces); } }
AddonPlayableGate::AddonPlayableGate(AddonManager* manager, QObject* parent) : AddonPlayableGate(manager, {}, parent) {}
AddonPlayableGate::AddonPlayableGate(AddonManager* manager, Backend backend, QObject* parent)
    : QObject(parent), m_manager(manager), m_backend(std::move(backend))
{
    m_clock.start(); m_expiry.setInterval(250); connect(&m_expiry, &QTimer::timeout, this, &AddonPlayableGate::expire); m_expiry.start();
    if (manager) {
        connect(manager, &QObject::destroyed, this, &AddonPlayableGate::close);
        connect(manager, &AddonManager::packageWithdrawn, this, [this](const QString& package) {
            const QPointer<AddonPlayableGate> owner(this);
            const auto jobs = m_pending.keys(); for (const auto& job : jobs) { if (m_pending.value(job).context.package == package) cancel(job); if (!owner) return; }
            const auto handles = m_handles.keys(); for (const auto& handle : handles) { if (m_handles.value(handle).context.package == package) release(handle); if (!owner) return; }
            const auto active = m_active.keys(); for (const auto& handle : active) { if (m_active.value(handle).context.package == package) release(handle); if (!owner) return; }
        });
    }
}
AddonPlayableGate::~AddonPlayableGate() { close(); }
bool AddonPlayableGate::supported() const { return !m_closing && m_manager && m_backend.authority && m_backend.prepare && m_backend.cancel; }
QVariantMap AddonPlayableGate::registration(const QString& source) const
{
    if (!m_manager) return {};
    for (const auto& value : m_manager->contributionsOfType(QStringLiteral("discovery.source"))) {
        const auto item = value.toMap(); if (item.value("id").toString() == source) return item;
    }
    return {};
}
bool AddonPlayableGate::current(const Context& context) const
{
    const auto source = registration(context.source);
    return supported() && !source.isEmpty() && source.value("cachePartition").toString() == context.partition;
}
QString AddonPlayableGate::authorize(const QString& contribution, const QVariantMap& proposal, Completion completion)
{
    if (m_closing || m_pending.size() >= 64 || m_handles.size() >= 128) return {};
    const auto job = token(); const auto source = registration(contribution); const auto package = source.value("packageId").toString();
    const auto host = proposal.value("hostContext").toMap();
    Context context{job, contribution, package, source.value("cachePartition").toString(), host.value("intent").toString(),
        host.value("view").toString(), host.value("generation").toULongLong(), {}};
    if (m_manager) context.grants = {{"network.http", m_manager->grantedScope(package, QStringLiteral("network.http"))}};
    auto* timer = new QTimer(this); timer->setSingleShot(true);
    m_pending.insert(job, {context, proposal, std::move(completion), {}, {}, timer});
    QString error;
    const QUrl url(proposal.value("url").toString());
    QUrl origin = url; origin.setPath(QString()); origin.setQuery(QString()); origin.setFragment(QString());
    const auto normalizedOrigin = exactHttpsOrigin(origin.toString());
    bool approvedOrigin = false;
    for (const auto& value : context.grants.value("network.http").toObject().value("origins").toArray())
        approvedOrigin |= !normalizedOrigin.isEmpty() && exactHttpsOrigin(value.toString()) == normalizedOrigin;
    if (!supported()) error = tr("The app cannot play media from this source.");
    else if (source.isEmpty() || context.partition.isEmpty() || context.view.isEmpty() || context.view.size() > 128
        || (context.intent != QLatin1String("play") && context.intent != QLatin1String("enqueue"))) error = tr("The playback source or action is not available.");
    else if (!url.isValid() || url.scheme() != QLatin1String("https") || url.host().isEmpty() || !url.userInfo().isEmpty()
        || url.hasFragment() || url.toString().size() > 8192) error = tr("The source did not supply a permitted stream address.");
    else if (!approvedOrigin) error = tr("This stream address needs permission.");
    else {
        const auto authority = m_backend.authority; const QPointer<AddonPlayableGate> owner(this);
        const bool permitted = authority(context, proposal);
        if (!owner) return job;
        if (!permitted) error = tr("This playback action needs user permission.");
    }
    if (!error.isEmpty()) { QTimer::singleShot(0, this, [this, job, error] { deliver(job, error); }); return job; }
    if (!m_pending.contains(job) || !current(context)) { deliver(job, tr("The playback source changed.")); return job; }
    connect(timer, &QTimer::timeout, this, [this, job] {
        const auto native = m_pending.value(job).native;
        const auto cancelNative = m_backend.cancel; const QPointer<AddonPlayableGate> owner(this);
        if (!native.isEmpty()) cancelNative(native);
        if (!owner) return;
        deliver(job, tr("The stream preparation time limit was reached."));
    }); timer->start(10000);
    const auto weak = QPointer<AddonPlayableGate>(this);
    const auto prepare = m_backend.prepare; const auto cancelNative = m_backend.cancel;
    const auto native = prepare(context, proposal, [weak, job](Result result) {
        if (!weak) return;
        QTimer::singleShot(0, weak, [weak, job, result = std::move(result)]() mutable { if (weak) weak->prepared(job, std::move(result)); });
    });
    if (!weak) { if (!native.isEmpty()) cancelNative(native); return job; }
    if (m_pending.contains(job)) m_pending[job].native = native;
    else if (!native.isEmpty()) m_backend.cancel(native);
    return job;
}
void AddonPlayableGate::prepared(const QString& job, Result result)
{
    auto pending = m_pending.find(job); if (pending == m_pending.end() || pending->ready) return;
    pending->ready = true;
    if (!result.error.isEmpty() || !result.lease || !result.lease->item || !current(pending->context)) {
        deliver(job, result.error.isEmpty() ? tr("The prepared stream is not available.") : result.error.left(512)); return;
    }
    qint64 lifetime = 30000;
    if (pending->proposal.contains("expiresAt")) {
        const auto expires = pending->proposal.value("expiresAt"); const double value = expires.toDouble();
        if ((expires.metaType().id() != QMetaType::LongLong && expires.metaType().id() != QMetaType::ULongLong && expires.metaType().id() != QMetaType::Double)
            || !std::isfinite(value) || value != expires.toLongLong()) { deliver(job, tr("The stream expiry time is not correct.")); return; }
        lifetime = qMin<qint64>(lifetime, expires.toLongLong() - QDateTime::currentMSecsSinceEpoch());
    }
    if (lifetime < 1) { deliver(job, tr("The prepared stream expired.")); return; }
    const auto handle = token(); m_handles.insert(handle, {pending->context, pending->proposal, std::move(result.lease), m_clock.elapsed() + lifetime});
    m_issued.insert(job, handle); m_pending[job].handle = handle;
    // Keep the job-to-handle link until consumption or release. cancel(job)
    // can then revoke a result that the coordinator has not accepted yet.
    QTimer::singleShot(0, this, [this, job] { deliver(job); });
}
void AddonPlayableGate::deliver(const QString& job, const QString& error)
{
    if (!m_pending.contains(job)) return;
    const auto pending = m_pending.take(job);
    if (pending.timer) { pending.timer->stop(); pending.timer->deleteLater(); }
    const QPointer<AddonPlayableGate> owner(this);
    if (!error.isEmpty() && !pending.handle.isEmpty()) release(pending.handle);
    if (!owner) return;
    if (pending.completion) pending.completion(error.isEmpty() ? pending.handle : QString{}, error);
}
void AddonPlayableGate::cancel(const QString& job)
{
    const auto pending = m_pending.take(job);
    if (pending.timer) { pending.timer->stop(); pending.timer->deleteLater(); }
    const auto cancelNative = m_backend.cancel;
    const auto handle = m_issued.take(job); if (!handle.isEmpty()) release(handle);
    if (!pending.native.isEmpty() && cancelNative) cancelNative(pending.native);
}
void AddonPlayableGate::release(const QString& handle)
{
    const auto prepared = m_handles.take(handle); const auto active = m_active.take(handle);
    const auto jobs = m_issued.keys(handle); for (const auto& job : jobs) m_issued.remove(job);
}
bool AddonPlayableGate::consume(const QString& handle, const QString& intent, NativeConsumer consumer)
{
    const auto found = m_handles.constFind(handle);
    if (!consumer || found == m_handles.cend()) return false;
    const auto playable = *found;
    if (playable.expires <= m_clock.elapsed() || playable.context.intent != intent || !current(playable.context)
        || m_active.size() >= 4) { release(handle); return false; }
    const QPointer<AddonPlayableGate> owner(this); const auto authority = m_backend.authority;
    const bool permitted = authority(playable.context, playable.proposal);
    if (!owner) return false;
    if (!permitted) { release(handle); return false; }
    if (!m_handles.contains(handle) || !current(playable.context) || m_active.size() >= 4) { release(handle); return false; }
    m_handles.remove(handle); const auto jobs = m_issued.keys(handle); for (const auto& job : jobs) m_issued.remove(job);
    m_active.insert(handle, playable);
    const bool consumed = consumer(playable.lease->item.get());
    if (!owner) return false;
    if (!consumed) { release(handle); return false; }
    return m_active.contains(handle);
}
void AddonPlayableGate::expire()
{
    const QPointer<AddonPlayableGate> owner(this);
    const auto handles = m_handles.keys();
    for (const auto& handle : handles) {
        if (m_handles.value(handle).expires <= m_clock.elapsed() || !current(m_handles.value(handle).context)) release(handle);
        if (!owner) return;
    }
}
void AddonPlayableGate::close()
{
    if (m_closing) return;
    m_closing = true; m_expiry.stop(); const auto jobs = m_pending.keys();
    const QPointer<AddonPlayableGate> owner(this);
    for (const auto& job : jobs) { cancel(job); if (!owner) return; }
    const auto handles = std::move(m_handles); const auto active = std::move(m_active); m_issued.clear();
}
}
