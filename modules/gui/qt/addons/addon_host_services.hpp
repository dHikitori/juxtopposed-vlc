// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QJsonObject>
#include <QHash>
#include <QPointer>
#include <QTimer>
#include <functional>
#include <memory>
#include <mutex>

namespace vlc::addons {
// Only trusted host adapters can register a handler. Package data cannot add APIs.
class AddonHostServices : public QObject
{
    Q_OBJECT
public:
    struct Context { QString package, contribution, invocation; QJsonObject grants; };
    struct Result { QJsonObject response; QString error; };
    using Completion = std::function<void(Result)>;
    using Cancel = std::function<void()>;
    using Handler = std::function<Cancel(const Context&, const QJsonObject&, Completion)>;
    explicit AddonHostServices(QObject* parent = nullptr);
    ~AddonHostServices() override;
    bool registerOperation(const QString& operation, QObject* lifetime, Handler);
    quint64 dispatch(const Context&, const QString& operation, const QJsonObject&, Completion, int timeoutMs = 10000);
    void cancel(quint64 job);
    void cancelPackage(const QString&);
    void close();
    static QString capability(const QString& operation);
private:
    struct Registration { QPointer<QObject> lifetime; Handler handler; };
    struct Job { QString package, operation; QPointer<QObject> lifetime; QPointer<QTimer> deadline; Completion completion; Cancel cancel; };
    struct Gate { std::mutex mutex; AddonHostServices* owner = nullptr; };
    void finish(quint64, Result, bool abort = false);
    void unregisterOperation(const QString&);
    QHash<QString, Registration> m_handlers;
    QHash<quint64, Job> m_jobs;
    std::shared_ptr<Gate> m_gate;
    quint64 m_next = 0;
    bool m_closing = false;
};
}
