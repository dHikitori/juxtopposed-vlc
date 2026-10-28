// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_manager.hpp"
#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>
#include <memory>
struct input_item_t;

namespace vlc::addons {
// This API is for trusted host code. No method is a QML invokable.
class AddonPlayableGate : public QObject
{
    Q_OBJECT
public:
    using Completion = std::function<void(const QString& handle, const QString& error)>;
    using NativeConsumer = std::function<bool(input_item_t*)>;
    struct Lease {
        std::shared_ptr<input_item_t> item;
        std::function<void()> cleanup;
        ~Lease() { if (cleanup) cleanup(); }
    };
    struct Context { QString job, source, package, partition, intent, view; quint64 generation = 0; QJsonObject grants; };
    struct Result { std::shared_ptr<Lease> lease; QString error; };
    struct Backend {
        // Both checks run on the owner thread. Installation is not user consent.
        std::function<bool(const Context&, const QVariantMap&)> authority;
        // Prepare must contain every native stream request. Completion runs on
        // the owner thread. A late result still owns its cleanup through Lease.
        std::function<QString(const Context&, const QVariantMap&, std::function<void(Result)>)> prepare;
        std::function<void(const QString&)> cancel;
    };
    explicit AddonPlayableGate(AddonManager*, QObject* parent = nullptr);
    AddonPlayableGate(AddonManager*, Backend, QObject* parent = nullptr);
    ~AddonPlayableGate() override;
    bool supported() const;
    QString authorize(const QString& contribution, const QVariantMap& proposal, Completion);
    void cancel(const QString& job);
    void release(const QString& handle);
    // item is borrowed for this call. A consumer that keeps it must hold it.
    bool consume(const QString& handle, const QString& expectedIntent, NativeConsumer);
    void close();
private:
    friend class AddonPlayableGateTestAccess;
    struct Pending { Context context; QVariantMap proposal; Completion completion; QString native, handle; QPointer<QTimer> timer; bool ready = false; };
    struct Playable { Context context; QVariantMap proposal; std::shared_ptr<Lease> lease; qint64 expires = 0; };
    QVariantMap registration(const QString&) const;
    bool current(const Context&) const;
    void prepared(const QString&, Result);
    void deliver(const QString&, const QString& error = {});
    void expire();
    QPointer<AddonManager> m_manager;
    Backend m_backend;
    QElapsedTimer m_clock;
    QTimer m_expiry;
    QHash<QString, Pending> m_pending;
    QHash<QString, Playable> m_handles, m_active;
    QHash<QString, QString> m_issued;
    bool m_closing = false;
};
}
