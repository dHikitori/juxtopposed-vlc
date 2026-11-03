// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QVariantMap>
#include <functional>

namespace vlc::addons {
// Host-only authorization methods are not invokable by package view documents.
// The actual player adapter copies snapshots outside core locks and serializes
// apply() onto its normal command thread.
class AddonSessions : public QObject
{
    Q_OBJECT
public:
    struct Backend {
        std::function<QVariantMap()> snapshot;
        // origin is host-assigned so player adapters can distinguish a session
        // command from a newer local user action and prevent echo loops.
        std::function<bool(const QString& operation, const QVariantMap& parameters, const QString& origin)> apply;
        std::function<qint64()> monotonicUs;
    };
    explicit AddonSessions(Backend, QObject* parent = nullptr);
    ~AddonSessions() override;
    void close();
    QString authorize(const QString& package, const QStringList& operations, int lifetimeMs = 30000);
    bool renew(const QString& package, const QString& lease, int lifetimeMs = 30000);
    bool resume(const QString& package, const QString& lease);
    void end(const QString& package, const QString& lease);
    void revokePackage(const QString& package);
    void localIntervention(const QString& operation);
    QVariantMap observe(const QString& package, const QString& lease) const;
    QString submit(const QString& package, const QString& lease, const QVariantMap& command);
    void cancel(const QString& package, const QString& request);
signals:
    void commandFinished(const QString& request, const QString& status, const QString& error);
    void sessionChanged(const QString& lease, const QString& status);
private:
    struct Lease {
        QString package, mediaEpoch;
        QSet<QString> operations, seen;
        qint64 expires = 0;
        bool suspended = false, ending = false;
        double baseRate = 1, correctedRate = 1;
        bool ownsRate = false;
        QPointer<QTimer> restore;
    };
    struct Pending { QString package, lease; QVariantMap command; QPointer<QTimer> timer; };
    qint64 now() const;
    QString validate(const QString& package, const QString& lease, const QVariantMap& command) const;
    void execute(const QString& request);
    void finish(const QString& request, const QString& status, const QString& error = {});
    void restoreRate(const QString& lease);
    void expire();
    Backend m_backend;
    QElapsedTimer m_clock;
    QTimer m_expiry;
    QHash<QString, Lease> m_leases;
    QHash<QString, Pending> m_pending;
    QString m_controller;
    bool m_closing = false;
};
}
