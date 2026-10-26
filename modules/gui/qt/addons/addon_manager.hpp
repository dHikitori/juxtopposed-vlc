// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_package_store.hpp"
#include "addon_credentials.hpp"
#include "addon_network.hpp"
#include "addon_worker.hpp"
#include <QObject>
#include <QVariantList>
#include <QThread>

namespace vlc::addons {
class AddonManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY revisionChanged FINAL)
    Q_PROPERTY(QVariantList packages READ packages NOTIFY revisionChanged FINAL)
    Q_PROPERTY(bool credentialPersistenceAvailable READ credentialPersistenceAvailable CONSTANT FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY revisionChanged FINAL)
    Q_PROPERTY(QVariantMap pendingPackage READ pendingPackage NOTIFY revisionChanged FINAL)
public:
    explicit AddonManager(const QString& storageDirectory, QObject* parent = nullptr);
    ~AddonManager() override;
    int revision() const { return m_revision; }
    QString errorMessage() const { return m_error; }
    QVariantList packages() const;
    QString storageDirectory() const { return m_store.storageDirectory(); }
    QVariantList contributionsOfType(const QString& type, bool includeDisabled = false) const;
    QJsonObject contributionResource(const QString& globalId, const QString& resourceField) const;
    QJsonObject grantedScope(const QString& packageId, const QString& capability) const;
    bool credentialPersistenceAvailable() const { return CredentialStore::persistentAvailable(); }
    bool busy() const { return m_repositoryThread != nullptr; }
    QVariantMap pendingPackage() const;
    void setWorkerExecutable(const QString&);
    Q_INVOKABLE QVariantList providersForFeature(const QString& feature) const;
    Q_INVOKABLE QStringList providerOrder(const QString& feature) const;
    Q_INVOKABLE bool setProviderOrder(const QString& feature, const QStringList& ids);
    void registerBuiltinProvider(const QString& feature, const QVariantMap& descriptor);
    void unregisterBuiltinProvider(const QString& contributionId);
    Q_INVOKABLE QVariantMap settingsSchema(const QString& packageId) const;
    Q_INVOKABLE QVariantMap settingsMessages(const QString& packageId) const;
    Q_INVOKABLE QVariantMap settingsValues(const QString& packageId) const;
    Q_INVOKABLE QVariantMap settingsSecretStatus(const QString& packageId) const;
    Q_INVOKABLE bool saveAddonSettings(const QString& packageId, const QVariantMap& values, const QVariantMap& secretOperations);
    Q_INVOKABLE QVariantMap inspectLocalPackage(const QString& directory) const;
    bool installLocalPackage(const QString& directory, bool approveRequiredPermissions);
    Q_INVOKABLE bool stageLocalPackage(const QString& directory);
    Q_INVOKABLE bool stageRepository(const QString& source, const QString& tag);
    Q_INVOKABLE bool approvePendingPackage(bool enableAfterInstall = false);
    Q_INVOKABLE void discardPendingPackage();
    Q_INVOKABLE bool setEnabled(const QString& packageId, bool enabled);
    Q_INVOKABLE bool rollback(const QString& packageId);
    Q_INVOKABLE bool uninstall(const QString& packageId, bool removeData);
    QString invoke(const QString& contributionId, const QString& operation, const QVariantMap& context);
    void cancel(const QString& invocation);
    quint64 fetchAsset(const QString& contributionId, const QJsonObject& descriptor, NetworkBroker::Callback);
    void cancelAsset(quint64 job) { m_network.cancel(job); }
    NetworkBroker* networkBroker() { return &m_network; }
signals:
    void revisionChanged();
    void operationFinished(const QString& invocation, const QString& contribution, const QString& status,
                           const QVariantMap& data, const QString& error);
private:
    QJsonObject schema(const InstalledPackage&) const;
    QJsonObject savedSettings(const QString&) const;
    bool configured(const InstalledPackage&) const;
    QByteArray credential(const QString& package, const QString& field) const;
    AddonWorker* worker(const InstalledPackage&);
    void withdraw(const QString& packageId);
    bool changed(bool success, const QString& error = {});
    PackageStore m_store;
    mutable CredentialStore m_secrets;
    NetworkBroker m_network;
    QHash<QString, AddonWorker*> m_workers;
    QHash<QString, QString> m_invocationPackages, m_invocationContributions;
    QJsonObject m_providerOrder;
    QMap<QString, QVariantMap> m_builtinProviders;
    std::shared_ptr<PackageCandidate> m_pendingPackage;
    QThread* m_repositoryThread = nullptr;
    QString m_workerExecutable, m_error;
    int m_revision = 0;
};
}
