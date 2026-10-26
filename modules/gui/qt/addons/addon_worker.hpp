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
    bool configurationCurrent() const { return !m_requestGuard || m_requestGuard(); }
    QString invoke(const QString& contribution, const QString& operation, const QJsonObject& context);
    void cancel(const QString& invocation);
    void stop();
signals:
    void completed(const QString& invocation, const QString& status, const QJsonObject& data, const QString& error);
    void unavailable(const QString& error);
private:
    struct Invocation { QString contribution; QPointer<QTimer> deadline; QSet<quint64> jobs, serviceJobs; };
    void start();
    void send(const QJsonObject&);
    void read();
    void message(const QJsonObject&);
    void fail(const QString&);
    void finish(const QString&, const QString&, const QJsonObject&, const QString&);
    InstalledPackage m_package;
    NetworkBroker* m_network;
    QPointer<AddonHostServices> m_services;
    QProcess m_process;
    QTimer m_startup;
    QByteArray m_buffer;
    QQueue<QJsonObject> m_queued;
    QHash<QString, Invocation> m_invocations;
    QSet<QString> m_hostCalls;
    QSet<QString> m_cancelled;
    QQueue<QString> m_cancelledOrder;
    QJsonObject m_settings, m_credentials, m_hostCapabilities;
    NetworkBroker::CredentialResolver m_resolver;
    NetworkBroker::RequestGuard m_requestGuard;
    QString m_executable;
    bool m_ready = false, m_stopping = false;
};
}
