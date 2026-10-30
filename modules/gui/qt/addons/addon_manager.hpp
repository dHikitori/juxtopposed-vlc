// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_package_store.hpp"
#include "addon_credentials.hpp"
#include "addon_network.hpp"
#include "addon_worker.hpp"
#include "addon_native_authority.hpp"
#include "addon_package_reader.hpp"
#include "addon_metadata_catalogue_source.hpp"
#include "addon_display_catalogue_reader.hpp"
#include "addon_distribution_bootstrap.hpp"
#include "addon_prepared_metadata_invocation.hpp"
#include "addon_prepared_artwork_asset.hpp"
#include "addon_metadata_coordinator.hpp"
#include <QObject>
#include <QElapsedTimer>
#include <QVariantList>
#include <atomic>

class AddonArtworkAssets;

namespace vlc::addons {
class AddonStorage;
class AddonCatalogueView;
class AddonRepositoryJobs;
struct UiAssetCapture;
struct PrivateStorageContext;
class AddonManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged FINAL)
    Q_PROPERTY(quint64 catalogueRevision READ displayCatalogueRevision NOTIFY catalogueChanged FINAL)
    Q_PROPERTY(bool catalogueReady READ displayCatalogueReady NOTIFY catalogueFreshnessChanged FINAL)
    Q_PROPERTY(bool catalogueLoading READ displayCatalogueLoading NOTIFY catalogueFreshnessChanged FINAL)
    Q_PROPERTY(QString catalogueError READ displayCatalogueError NOTIFY catalogueFreshnessChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY revisionChanged FINAL)
    Q_PROPERTY(QVariantList packages READ packages NOTIFY revisionChanged FINAL)
    Q_PROPERTY(bool credentialPersistenceAvailable READ credentialPersistenceAvailable CONSTANT FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY revisionChanged FINAL)
    Q_PROPERTY(QVariantMap pendingPackage READ pendingPackage NOTIFY revisionChanged FINAL)
    Q_PROPERTY(bool safeMode READ safeMode NOTIFY revisionChanged FINAL)
    Q_PROPERTY(QVariantMap capabilities READ capabilities NOTIFY capabilitiesChanged FINAL)
    Q_PROPERTY(QVariantMap updateResult READ updateResult NOTIFY revisionChanged FINAL)
public:
    explicit AddonManager(const QString& storageDirectory, QObject* parent = nullptr);
    // Native construction only. No runtime transport setter is available.
    static std::unique_ptr<AddonManager> withNetworkTransport(const QString& storageDirectory,
        std::unique_ptr<QNetworkAccessManager>&, QObject* parent = nullptr);
    ~AddonManager() override;
    int revision() const { return m_revision; }
    QString errorMessage() const { return m_error; }
    QVariantList packages() const;
    // Memory-only preparation. Invoke the owned reader on joined Qt work.
    PackageSummaryReader packageReader() const;
    // Display-only, owner-thread memory capture. Never an invocation recipe.
    std::optional<DisplayCatalogueSource> displayCatalogueSource() const;
    StoreAuthorityChanges watchDisplayCatalogue(std::function<void()> nativeWake) const;
    AddonCatalogueView* displayCatalogue() const;
    quint64 displayCatalogueRevision() const;
    bool displayCatalogueReady() const;
    bool displayCatalogueLoading() const;
    QString displayCatalogueError() const;
    Q_INVOKABLE void refreshDisplayCatalogue();
    Q_INVOKABLE QVariantList displayProvidersForFeature(const QString&) const;
    Q_INVOKABLE QVariantMap displayCapabilities() const;
    Q_INVOKABLE QString prepareDisplayFeature(const QString&);
    Q_INVOKABLE QString prepareDisplaySettings(const QString&);
    Q_INVOKABLE void releaseDisplayRequest(const QString&);
    Q_INVOKABLE bool displayRequestCurrent(const QString&) const;
    Q_INVOKABLE QString displaySettingsState(const QString&) const;
    Q_INVOKABLE QString displaySettingsStamp(const QString&) const;
    Q_INVOKABLE bool displayCatalogueAttached() const;
    Q_INVOKABLE QVariantMap displaySettingsSchema(const QString&) const;
    Q_INVOKABLE QVariantMap displaySettingsValues(const QString&) const;
    Q_INVOKABLE QVariantMap displaySettingsMessages(const QString&) const;
    Q_INVOKABLE QVariantMap displaySettingsSecretStatus(const QString&) const;
    // C++ only. This call reads memory on the Manager thread.
    std::optional<MetadataCatalogueSource> metadataCatalogueSource() const;
    MetadataInvocationPreparer metadataInvocationPreparer(std::shared_ptr<const ArtworkAttemptPlan>, unsigned slot) const;
    // Owner admission uses memory only. An empty ID means admission refused.
    QString invokePreparedMetadata(std::unique_ptr<PreparedMetadataInvocation>, NetworkBroker::RequestGuard,
                                   std::shared_ptr<AddonMetadataAttemptAdmission>);
    // These entry points return immediately. All saved-state/key reads run on
    // joined work. Neither a display row nor its cache can authorize execution.
    AddonMetadataCoordinator::SelectionCancel selectMetadataProviders(const QString&, AddonMetadataCoordinator::SelectionComplete);
    QString invokeMetadata(const QString&, const QString&, const QVariantMap&);
    QVariantMap builtinMetadataProvider(const QString&) const;
    QString storageDirectory() const { return m_store.storageDirectory(); }
    // The profile host supplies the shared profile's writer. Store writes
    // in a shared profile then go through the coordinator's permits.
    void setProfileWriter(PackageStore::ProfileWriter writer) { m_store.setProfileWriter(std::move(writer)); }
    // GUI thread. Another app process changed the shared profile. Reload the
    // cached provider order and the display catalogue.
    void profileChanged();
    // Read these memory snapshots on the manager thread. They contain no key bytes.
    QString workerExecutable() const { return m_workerExecutable; }
    QHash<QString, QHash<QString, bool>> credentialPresenceSnapshot() const { return m_secrets.presenceSnapshot(); }
    StoreAuthorityToken storeAuthorityToken() const noexcept;
    QVariantList contributionsOfType(const QString& type, bool includeDisabled = false) const;
    QJsonObject contributionResource(const QString& globalId, const QString& resourceField) const;
    QByteArray contributionFile(const QString& globalId, const QString& path, qint64 maximumBytes) const;
    std::function<UiAssetCapture()> uiAssetReader(const QString& globalId) const;
    // Prepare on the manager thread. This preparation reads memory only.
    AddonNativeAuthorityReader nativeAuthorityReader(const QString& globalId) const;
    QJsonObject grantedScope(const QString& packageId, const QString& capability) const;
    bool credentialPersistenceAvailable() const { return CredentialStore::persistentAvailable(); }
    bool busy() const;
    bool safeMode() const { return m_store.safeMode(); }
    QVariantMap pendingPackage() const;
    QVariantMap capabilities() const;
    QVariantMap updateResult() const { return m_updateResult; }
    void setMediaCapabilitiesSource(QObject*);
    // C++ only. The app reports the mounted native source route.
    void setNativeStreamAvailable(bool);
    void setNativeStreamTransportHint(bool known,bool available);
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
    Q_INVOKABLE bool saveDisplaySettings(const QString& packageId,const QString& expectedStamp,const QVariantMap& values,const QVariantMap& secretOperations);
    Q_INVOKABLE QVariantMap inspectLocalPackage(const QString& directory) const;
    bool installLocalPackage(const QString& directory, bool approveRequiredPermissions);
    // Only the app distribution calls this API with its trusted package path.
    bool ensureDistributionPackage(const QString& directory, const QString& expectedPackageId);
    // Startup only. Call once on a fresh Manager, before display attachment.
    // Paths and IDs are copied; all directory/manifest/install I/O is joined work.
    bool beginDistributionBootstrap(QList<DistributionBootstrapEntry>);
    void cancelDistributionBootstrap();
    bool distributionBootstrapInProgress() const noexcept { return m_distributionBootstrapInProgress; }
    quint64 distributionBootstrapGeneration() const noexcept { return m_distributionBootstrapGeneration; }
    QVariantList distributionBootstrapResults() const { return m_distributionBootstrapResults; }
    QString distributionBootstrapError() const { return m_distributionBootstrapError; }
    Q_INVOKABLE bool restoreDistributionPackage(const QString& packageId);
    Q_INVOKABLE bool stageDistributionPackage(const QString& packageId);
    Q_INVOKABLE bool stageLocalPackage(const QString& directory);
    Q_INVOKABLE bool stageRepository(const QString& source, const QString& tag);
    Q_INVOKABLE bool checkRepositoryUpdates(const QString& packageId, const QString& versionRange, bool allowPrerelease = false);
    Q_INVOKABLE bool approvePendingPackage(bool enableAfterInstall = false);
    Q_INVOKABLE void discardPendingPackage();
    Q_INVOKABLE bool setEnabled(const QString& packageId, bool enabled);
    // Worker health (finding F3): recent crashes and the restart wait. Memory
    // only, per process.
    Q_INVOKABLE QVariantMap health(const QString& packageId) const;
    Q_INVOKABLE bool rollback(const QString& packageId);
    Q_INVOKABLE bool setSafeMode(bool enabled);
    Q_INVOKABLE bool recoverPackage(const QString& packageId);
    Q_INVOKABLE QVariantList retainedGenerations(const QString& packageId) const;
    Q_INVOKABLE bool uninstall(const QString& packageId, bool removeData);
    QString invoke(const QString& contributionId, const QString& operation, const QVariantMap& context);
    // C++ only. Native owners supply a memory-only request guard.
    QString invoke(const QString& contributionId, const QString& operation, const QVariantMap& context,
                   NetworkBroker::RequestGuard);
    QString invoke(const QString& contributionId, const QString& operation, const QVariantMap& context,
                   NetworkBroker::RequestGuard, std::shared_ptr<AddonMetadataAttemptAdmission>);
    void cancel(const QString& invocation);
    quint64 fetchAsset(const QString& contributionId, const QJsonObject& descriptor, NetworkBroker::Callback);
    QString prepareArtworkAsset(const QString& contribution, ArtworkPreparationComplete);
    quint64 fetchPreparedArtwork(const PreparedArtwork&, const QJsonObject&, NetworkBroker::Callback);
    void cancelAsset(quint64 job) { m_network.cancel(job); }
    NetworkBroker* networkBroker() { return &m_network; }
    // C++ only. The resolver keeps the release, permission, and account scope.
    NetworkBroker::CredentialResolver credentialResolver(const InstalledPackage&, NetworkBroker::RequestGuard* current = nullptr) const;
    AddonHostServices* hostServices() { return &m_hostServices; }
signals:
    // Direct owner-thread teardown runs before worker shutdown waits.
    void retiring();
    void distributionBootstrapChanged();
    void catalogueChanged();
    void catalogueFreshnessChanged();
    void revisionChanged();
    void capabilitiesChanged();
    void packageWithdrawn(const QString& packageId);
    void operationFinished(const QString& invocation, const QString& contribution, const QString& status,
                           const QVariantMap& data, const QString& error);
private:
    friend class ArtworkLookupRunner;
    friend class ::AddonArtworkAssets;
    AddonManager(const QString&, std::unique_ptr<QNetworkAccessManager>, QObject*);
    QJsonObject schema(const InstalledPackage&) const;
    QJsonObject savedSettings(const QString&) const;
    QString accountReference(const InstalledPackage&) const;
    bool configured(const InstalledPackage&) const;
    QByteArray credential(const QString& package, const QString& field) const;
    AddonWorker* worker(const InstalledPackage&);
    bool withdraw(const QString& packageId);
    bool changed(bool success, const QString& error = {});
    bool saveSettingsImpl(const QString&,const QVariantMap&,const QVariantMap&,const QString& expectedStamp);
    QString prepareDisplayRequest(const QString&,bool settings);
    struct DisplayRequest {
        QPointer<AddonCatalogueView> view;
        QString inner;
        QMetaObject::Connection ownerDestroyed;
    };
    QHash<QString,DisplayRequest> m_displayRequests;
    void finishDistributionBootstrap(const QList<DistributionBootstrapEntry>&,
        const DistributionBootstrapBatch*,bool canceled,const QString& error);
    QString queueMetadataPreparation(const QString& package, std::function<void()>, std::function<void(bool)>);
    void pumpMetadataPreparation();
    bool cancelMetadataPreparation(const QString&);
    MetadataInvocationPreparer metadataInvocationPreparer(const QString&, const QString&, const QJsonObject&) const;
    void initializePreparedMetadata();
    void retirePreparedMetadata(const QString& package = {});
    bool cancelPreparedMetadata(const QString& invocation);
    std::optional<bool> preparedStorageContext(const AddonHostServices::Context&, PrivateStorageContext*, QString*) const;
    std::shared_ptr<PreparedMetadataRuntime> m_preparedMetadata;
    PackageStore m_store;
    mutable CredentialStore m_secrets;
    NetworkBroker m_network;
    AddonHostServices m_hostServices;
    std::unique_ptr<AddonStorage> m_storage;
    QHash<QString, AddonWorker*> m_workers;
    // A package verified for invoke, with the store token from before the
    // read. While no store change has happened the token stays current, so
    // later invocations skip reading and hashing the package again.
    struct VerifiedPackage { StoreAuthorityToken token; InstalledPackage package; bool usable = false; };
    QHash<QString, VerifiedPackage> m_verified;
    bool verifiedPackage(const QString& id, InstalledPackage* package);
    // Crash ledger (finding F3). A crash makes the next start wait 2 s, then
    // 4 s. The third crash in five minutes turns the package off through the
    // guarded writer, so every window applies it. Counts stay in memory and do
    // not revoke other windows. An explicit enable, rollback, install or
    // restore clears the entry.
    struct CrashLedger { QList<qint64> recent; qint64 retryAfter = 0; };
    QHash<QString, CrashLedger> m_crashes;
    QElapsedTimer m_crashClock;
    bool crashBackoff(const QString& packageId) const;
    void recordCrash(const QString& packageId, const QString& error);
public:
    static constexpr int CrashLimit = 3;
    static constexpr qint64 CrashWindowMs = 5 * 60 * 1000;
private:
    QHash<QString, QString> m_invocationPackages, m_invocationContributions;
    QJsonObject m_providerOrder;
    QMap<QString, QVariantMap> m_builtinProviders;
    QMap<QString, QString> m_distributionDirectories;
    QMap<QString, QVariantMap> m_distributionPackages;
    QSet<QString> m_removingPackages;
    QSet<QString> m_withdrawingPackages;
    std::shared_ptr<PackageCandidate> m_pendingPackage;
    std::unique_ptr<AddonRepositoryJobs> m_repositoryJobs;
    QString m_workerExecutable, m_error;
    int m_revision = 0;
    bool m_retiring = false;
    mutable bool m_readerCaptureBegun=false;
    bool m_distributionBootstrapStarted=false,m_distributionBootstrapInProgress=false;
    quint64 m_distributionBootstrapGeneration=0;
    std::shared_ptr<std::atomic_bool> m_distributionBootstrapCancel;
    QVariantList m_distributionBootstrapResults;
    QString m_distributionBootstrapError;
    QPointer<QObject> m_mediaCapabilitiesSource;
    QList<QMetaObject::Connection> m_mediaCapabilityConnections;
    QVariantMap m_updateResult;
};
}
