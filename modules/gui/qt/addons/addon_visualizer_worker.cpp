// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_visualizer_worker.hpp"
#include "addon_visualizer_parameters.hpp"
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QUuid>
#include <QSet>

namespace vlc::addons {
struct AddonVisualizerWorker::Reply {
    QMutex mutex;
    QPointer<AddonVisualizerWorker> owner;
};
AddonVisualizerWorker::AddonVisualizerWorker(QJsonObject scene, Backend backend, Clock clock, QObject* parent)
    : QObject(parent), m_scene(std::move(scene)), m_backend(std::move(backend)), m_clock(std::move(clock)), m_reply(std::make_shared<Reply>())
{
    m_elapsed.start(); if (!m_clock) m_clock = [this] { return m_elapsed.elapsed(); };
    m_reply->owner = this; m_session = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_timer.setInterval(50); connect(&m_timer, &QTimer::timeout, this, &AddonVisualizerWorker::poll); m_timer.start();
    if (!AddonVisualizerParameters::validate(m_scene)) m_suspended = true;
}
AddonVisualizerWorker::~AddonVisualizerWorker() { close(); }
void AddonVisualizerWorker::close()
{
    if (m_closing) return;
    m_closing = true; m_timer.stop(); m_pending = 0; ++m_generation; m_values = {};
    { QMutexLocker lock(&m_reply->mutex); m_reply->owner = nullptr; }
    auto cancel = std::move(m_cancel), close = std::move(m_backend.close); m_backend.invoke = {};
    if (cancel) cancel(); if (close) close();
}
void AddonVisualizerWorker::resetTimeline()
{
    if (m_closing) return;
    ++m_generation; m_pending = 0; m_values = {}; m_lastRequest = m_lastResult = -1;
    auto cancel = std::move(m_cancel); QPointer<AddonVisualizerWorker> guard(this);
    if (cancel) cancel(); if (guard) emit parametersChanged();
}
void AddonVisualizerWorker::poll()
{
    if (m_closing) return;
    QPointer<AddonVisualizerWorker> guard(this); const auto now = m_clock();
    if (!m_values.isEmpty() && m_lastResult >= 0 && now - m_lastResult >= 2000) { m_values = {}; emit parametersChanged(); if (!guard) return; }
    if (m_pending && now - m_started >= (m_coldRequest ? 2000 : 500)) {
        m_pending = 0; auto cancel = std::move(m_cancel); if (cancel) cancel(); if (guard) failed();
    }
}
bool AddonVisualizerWorker::update(const QJsonObject& context)
{
    QPointer<AddonVisualizerWorker> guard(this); poll();
    if (!guard || m_closing || m_suspended || m_pending || !m_backend.invoke || context.value("reducedMotion").toBool()) return false;
    static const QSet<QString> allowed{"clock", "analysis", "viewport", "theme", "reducedMotion"};
    for (auto it = context.begin(); it != context.end(); ++it) if (!allowed.contains(it.key())) return false;
    if (QJsonDocument(context).toJson(QJsonDocument::Compact).size() > 48 * 1024) return false;
    const auto now = m_clock(); if (now < 0 || (m_lastRequest >= 0 && now - m_lastRequest < 100)) return false;
    QJsonObject request = context; request.insert("contract", "visualizer.parameters@1"); request.insert("session", m_session);
    request.insert("generation", QString::number(m_generation)); request.insert("parameterSchema", m_scene.value("parameters"));
    if (QJsonDocument(request).toJson(QJsonDocument::Compact).size() > 64 * 1024) return false;
    const auto ticket = ++m_sequence, generation = m_generation; m_pending = ticket; m_started = m_lastRequest = now;
    m_coldRequest = !m_hadRequest; m_hadRequest = true;
    const auto reply = m_reply; const auto invoke = m_backend.invoke;
    auto cancel = invoke(request, [reply, ticket, generation](const QString& status, const QJsonObject& data) {
        QMutexLocker lock(&reply->mutex); const auto owner = reply->owner;
        if (owner) QMetaObject::invokeMethod(owner, [owner, ticket, generation, status, data] {
            if (owner) owner->completed(ticket, generation, status, data);
        }, Qt::QueuedConnection);
    });
    if (!guard) { if (cancel) cancel(); return false; }
    if (m_pending == ticket && m_generation == generation && !m_closing) m_cancel = std::move(cancel);
    else if (cancel) cancel();
    return true;
}
void AddonVisualizerWorker::failed()
{
    if (++m_failures >= 3) {
        m_suspended = true; m_error = tr("The visualizer worker is not available. The static scene will continue.");
        auto close = std::move(m_backend.close); m_backend.invoke = {}; QPointer<AddonVisualizerWorker> guard(this);
        if (close) close(); if (guard) emit statusChanged();
    }
}
void AddonVisualizerWorker::completed(quint64 ticket, quint64 generation, const QString& status, const QJsonObject& data)
{
    if (m_closing || m_pending != ticket || m_generation != generation) return;
    m_pending = 0; m_cancel = {};
    if (status == QLatin1String("busy")) { if (m_coldRequest) m_hadRequest = false; return; }
    if ((status != QLatin1String("match") && status != QLatin1String("partial")) || data.size() != 1 || !data.value("parameters").isObject()
        || QJsonDocument(data).toJson(QJsonDocument::Compact).size() > 16 * 1024) { failed(); return; }
    auto values = m_values; const auto update = data.value("parameters").toObject();
    for (auto it = update.begin(); it != update.end(); ++it) values.insert(it.key(), it.value());
    QJsonObject scene;
    if (!AddonVisualizerParameters::apply(m_scene, values, &scene)) { failed(); return; }
    m_values = std::move(values); m_lastResult = m_clock(); m_failures = 0; emit parametersChanged();
}
}
