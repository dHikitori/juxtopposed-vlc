// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_package_store.hpp"
#include "addon_network.hpp"
#include "addon_host_services.hpp"
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QQueue>
#include <QPointer>
#include <atomic>

namespace vlc::addons {
class AddonWorker : public QObject
{
    Q_OBJECT
public:
    explicit AddonWorker(const InstalledPackage&, NetworkBroker*, QObject* parent = nullptr);
    ~AddonWorker() override;
    void setExecutable(const QString&);
    void setHostServices(AddonHostServices* services) { m_services = services; }
    void setHostCapabilities(const QJsonObject& capabilities) { m_hostCapabilities = capabilities; }
    void setConfiguration(const QJsonObject& nonsecretSettings, const QJsonObject& credentialStatus,
                          NetworkBroker::CredentialResolver);
    void setRequestGuard(NetworkBroker::RequestGuard guard) { m_requestGuard = std::move(guard); }
    bool configurationCurrent() const;
    bool failed() const noexcept { return m_failed; }
    QString invoke(const QString& contribution, const QString& operation, const QJsonObject& context);
    // C++ only. The guard must own plain native state and must not read files.
    QString invoke(const QString& contribution, const QString& operation, const QJsonObject& context,
                   NetworkBroker::RequestGuard);
    QString invoke(const QString& contribution, const QString& operation, const QJsonObject& context,
                   NetworkBroker::RequestGuard, std::shared_ptr<AddonMetadataAttemptAdmission>);
    void cancel(const QString& invocation);
    void stop();
signals:
    void completed(const QString& invocation, const QString& status, const QJsonObject& data, const QString& error);
    void unavailable(const QString& error);
    void preparedRetired();
private:
    friend class AddonManager;
    // Only the prepared Manager path can select this process lifecycle.
    void enablePreparedLifecycle();
    void retirePrepared(bool revoke);
    struct PreparedTerminal {
        QString invocation, status, error;
        QJsonObject data;
        std::shared_ptr<AddonMetadataAttemptAdmission> attempt;
    };
    std::optional<PreparedTerminal> m_preparedTerminal;
    struct Invocation {
        QString contribution;
        QPointer<QTimer> deadline;
        QSet<quint64> jobs, serviceJobs;
        QSet<QString> hostCalls;
        NetworkBroker::RequestGuard authority, current;
        std::shared_ptr<std::atomic_bool> active;
        std::shared_ptr<AddonMetadataAttemptAdmission> attempt;
    };
    bool invocationCurrent(const QString&);
    void expire(const QString&);
    void start();
    void send(const QJsonObject&);
    void read();
    void message(const QJsonObject&);
    void fail(const QString&);
    void finish(const QString&, const QString&, const QJsonObject&, const QString&);
    void rememberRetired(const QString&);
    void revokeAttemptGates();
    void processFinished(int, QProcess::ExitStatus);
    void notifyPreparedRetired();
    InstalledPackage m_package;
    QString m_attemptWorkerId;
    QHash<QString, std::weak_ptr<AddonMetadataNativeAdmission>> m_attemptGates;
    NetworkBroker* m_network;
    QPointer<AddonHostServices> m_services;
    QProcess m_process;
    QTimer m_startup, m_preparedDeadline;
    QByteArray m_buffer;
    QQueue<QJsonObject> m_queued;
    QHash<QString, Invocation> m_invocations;
    qsizetype m_hostCallCount=0;
    QSet<QString> m_retired;
    QQueue<QString> m_retiredOrder;
    QJsonObject m_settings, m_credentials, m_hostCapabilities;
    NetworkBroker::CredentialResolver m_resolver;
    NetworkBroker::RequestGuard m_requestGuard;
    QString m_executable;
    bool m_ready = false, m_stopping = false, m_failed = false;
    bool m_prepared = false, m_preparedRetiring = false, m_preparedAbort = false;
    bool m_preparedCleanExit = false, m_preparedNotified = false;
};
}
