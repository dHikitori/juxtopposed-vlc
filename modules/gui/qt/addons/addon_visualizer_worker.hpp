// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QTimer>
#include <functional>
#include <memory>

namespace vlc::addons {
struct AddonVisualizerActivation;
class AddonVisualizerWorker : public QObject
{
    Q_OBJECT
public:
    // Control methods use the owner thread. A backend can reply on any thread.
    using Completion = std::function<void(const QString&, const QJsonObject&)>;
    using Cancel = std::function<void()>;
    struct Backend {
        std::function<Cancel(const QJsonObject&, Completion)> invoke;
        std::function<void()> close;
    };
    using Clock = std::function<qint64()>;
    AddonVisualizerWorker(QJsonObject scene, Backend, Clock = {}, QObject* parent = nullptr);
    ~AddonVisualizerWorker() override;
    static Backend production(std::shared_ptr<const AddonVisualizerActivation>, const QString& contribution);
    static int liveWorkers();
    bool update(const QJsonObject& context);
    void poll();
    void resetTimeline();
    void close();
    QJsonObject values() const { return m_values; }
    QString errorMessage() const { return m_error; }
    bool suspended() const { return m_suspended; }
signals:
    void parametersChanged();
    void statusChanged();
private:
    struct Reply;
    void completed(quint64 ticket, quint64 generation, const QString&, const QJsonObject&);
    void failed();
    QJsonObject m_scene, m_values;
    Backend m_backend;
    Clock m_clock;
    std::shared_ptr<Reply> m_reply;
    QElapsedTimer m_elapsed;
    QTimer m_timer;
    QString m_session, m_error;
    Cancel m_cancel;
    quint64 m_generation = 1, m_sequence = 0, m_pending = 0;
    qint64 m_started = -1, m_lastRequest = -1, m_lastResult = -1;
    int m_failures = 0;
    bool m_closing = false, m_suspended = false, m_hadRequest = false, m_coldRequest = false;
};
}
