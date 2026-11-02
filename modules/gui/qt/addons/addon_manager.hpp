// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_package_store.hpp"
#include "addon_credentials.hpp"
#include "addon_device_authorization.hpp"
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
#include "addon_native_notifications.hpp"
#include <QObject>
#include <QSet>
#include <QElapsedTimer>
#include <QVariantList>
#include <atomic>

class AddonArtworkAssets;

namespace vlc::addons {
class AddonStorage;
class AddonCatalogueView;
class AddonRepositoryJobs;
class AddonNativeWake;
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
    // Store writes are queued or running (see the request calls).
    Q_PROPERTY(bool writing READ writing NOTIFY revisionChanged FINAL)
    // A package read for a GUI query ended (see retainedGenerations).
    Q_PROPERTY(quint64 readRevision READ readRevision NOTIFY readsChanged FINAL)
    Q_PROPERTY(QVariantMap pendingPackage READ pendingPackage NOTIFY revisionChanged FINAL)
    Q_PROPERTY(bool safeMode READ safeMode NOTIFY revisionChanged FINAL)
    Q_PROPERTY(QVariantMap capabilities READ capabilities NOTIFY capabilitiesChanged FINAL)
    Q_PROPERTY(QVariantMap updateResult READ updateResult NOTIFY revisionChanged FINAL)
    // Bundled add-ons whose app copy is newer than the installed release,
    // from the startup package check: {id, name, installedVersion, version,
    // enabled, newPermissions}. An approved update leaves the list.
    Q_PROPERTY(QVariantList appUpdates READ appUpdates NOTIFY appUpdatesChanged FINAL)
public:
    explicit AddonManager(const QString& storageDirectory, QObject* parent = nullptr);
    // Native construction only. No runtime transport setter is available.
    static std::unique_ptr<AddonManager> withNetworkTransport(const QString& storageDirectory,
        std::unique_ptr<QNetworkAccessManager>&, QObject* parent = nullptr);
    ~AddonManager() override;
    // The app turns this on before it makes its manager: package and profile
    // reads for the GUI then run on background work. Tools and tests read on
    // the calling thread unless they turn it on. It needs the app lifetime.
    static void setBackgroundReads(bool);
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
    void loadProviderOrder(bool profile);
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
    bool writing() const;
    quint64 readRevision() const noexcept { return m_readRevision; }
    // The app keeps the value read on background work; tools read the file.
    bool safeMode() const { return m_inlineStore ? m_store.safeMode() : m_safeMode; }
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
    // Store writes for the app. Each call returns a request ID at once and
    // ends with writeFinished. The writes run one at a time, in request
    // order, on background work: the store lock, the shared profile's
    // permit, package files and the platform credential store never block
    // this thread. A write checks its arguments when it starts.
    Q_INVOKABLE QString requestEnabled(const QString& packageId, bool enabled);
    Q_INVOKABLE QString requestProviderOrder(const QString& feature, const QStringList& ids);
    Q_INVOKABLE QString requestSaveSettings(const QString& packageId, const QVariantMap& values, const QVariantMap& secretOperations);
    Q_INVOKABLE QString requestSaveDisplaySettings(const QString& packageId, const QString& expectedStamp,
                                                   const QVariantMap& values, const QVariantMap& secretOperations);
    Q_INVOKABLE QString requestApprovePending(bool enableAfterInstall = false);
    Q_INVOKABLE QString requestRollback(const QString& packageId);
    Q_INVOKABLE QString requestSafeMode(bool enabled);
    Q_INVOKABLE QString requestRecoverPackage(const QString& packageId);
    QString requestConnectAccount(const QString& packageId, const QString& field, const DeviceTokens&, bool sessionOnly);
    // The same writes, done on the calling thread before they return. For
    // tests and tools only: the app uses the request calls above.
    bool setProviderOrder(const QString& feature, const QStringList& ids);
    void registerBuiltinProvider(const QString& feature, const QVariantMap& descriptor);
    void unregisterBuiltinProvider(const QString& contributionId);
    Q_INVOKABLE QVariantMap settingsSchema(const QString& packageId) const;
    Q_INVOKABLE QVariantMap settingsMessages(const QString& packageId) const;
    Q_INVOKABLE QVariantMap settingsValues(const QString& packageId) const;
    Q_INVOKABLE QVariantMap settingsSecretStatus(const QString& packageId) const;
    // Tests and tools only, like setProviderOrder.
    bool saveAddonSettings(const QString& packageId, const QVariantMap& values, const QVariantMap& secretOperations);
    bool saveDisplaySettings(const QString& packageId,const QString& expectedStamp,const QVariantMap& values,const QVariantMap& secretOperations);
    // Tests and tools only: these read and write package files on the calling thread.
    QVariantMap inspectLocalPackage(const QString& directory) const;
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
    QVariantList appUpdates() const { return m_appUpdates; }
    QString distributionBootstrapError() const { return m_distributionBootstrapError; }
    Q_INVOKABLE bool restoreDistributionPackage(const QString& packageId);
    Q_INVOKABLE bool stageDistributionPackage(const QString& packageId);
    Q_INVOKABLE bool stageLocalPackage(const QString& directory);
    Q_INVOKABLE bool stageRepository(const QString& source, const QString& tag);
    Q_INVOKABLE bool checkRepositoryUpdates(const QString& packageId, const QString& versionRange, bool allowPrerelease = false);
    Q_INVOKABLE void discardPendingPackage();
    // Worker health (finding F3): recent crashes and the restart wait. Memory
    // only, per process.
    Q_INVOKABLE QVariantMap health(const QString& packageId) const;
    // Tests and tools only, like setProviderOrder.
    bool approvePendingPackage(bool enableAfterInstall = false);
    bool setEnabled(const QString& packageId, bool enabled);
    bool rollback(const QString& packageId);
    bool setSafeMode(bool enabled);
    bool recoverPackage(const QString& packageId);
    // From the last package read; a stale one starts a new read and
    // readsChanged follows.
    Q_INVOKABLE QVariantList retainedGenerations(const QString& packageId);
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
    void cancelAsset(quint64 job);
    NetworkBroker* networkBroker() { return &m_network; }
    // C++ only. The resolver keeps the release, permission, and account scope.
    NetworkBroker::CredentialResolver credentialResolver(const InstalledPackage&, NetworkBroker::RequestGuard* current = nullptr) const;
    AddonHostServices* hostServices() { return &m_hostServices; }

    // Account recipes (settings fields with x-hakone-control "account").
    // C++ only: the app's setup view drives the sign-in; tokens never reach
    // QML or the add-on.
    std::optional<DeviceRecipe> accountRecipe(const QString& packageId, const QString& field) const;
    bool connectAccount(const QString& packageId, const QString& field, const DeviceTokens&, bool sessionOnly,
                        QString* error = nullptr);
    DevicePost accountTransport();
    // Native construction and tests only.
    void setAccountTransport(DevicePost);
    // A provider asked for sign-in: exchange each saved refresh token once a
    // minute at most. This runs on demand only.
    void refreshAccounts(const QString& packageId, bool expiredOnly = false);
    // The recipe for the sign-in view, read on background work.
    void requestAccountRecipe(const QString& packageId, const QString& field, std::function<void(std::optional<DeviceRecipe>)> done);

    // File and folder settings. The app's picker registers the chosen path
    // and gets an opaque handle; the add-on sees only the handle. A pick is
    // a draft until Save.
    QVariantMap registerPickedPath(const QString& packageId, const QString& field, const QString& localPath);
    // The app installs the native file dialog. done receives the chosen
    // local path, or an empty one when the user cancels.
    using PathPicker = std::function<void(const QString& kind, std::function<void(const QString& localPath)> done)>;
    void setPathPicker(PathPicker picker) { m_pathPicker = std::move(picker); }
    // Opens the picker for a file or folder field; the result arrives as
    // settingsPathPicked.
    Q_INVOKABLE bool pickSettingsPath(const QString& packageId, const QString& field);
    Q_INVOKABLE QVariantMap settingsHandleNames(const QString& packageId);

    // Temporary settings for this session, over the saved ones. They are
    // not saved and end when the app closes. An empty map clears them.
    Q_INVOKABLE bool setSessionSettings(const QString& packageId, const QVariantMap& values);
    Q_INVOKABLE QVariantMap sessionSettings(const QString& packageId) const { return m_sessionSettings.value(packageId); }
    // Saved values with this session's values over them, for the worker.
    QVariantMap effectiveSettings(const QString& packageId) const;
signals:
    // Direct owner-thread teardown runs before worker shutdown waits.
    void retiring();
    void distributionBootstrapChanged();
    void appUpdatesChanged();
    void catalogueChanged();
    void catalogueFreshnessChanged();
    void revisionChanged();
    void capabilitiesChanged();
    void packageWithdrawn(const QString& packageId);
    void operationFinished(const QString& invocation, const QString& contribution, const QString& status,
                           const QVariantMap& data, const QString& error);
    // A request call's write ended. errorMessage holds the same error.
    void writeFinished(const QString& request, bool success, const QString& error);
    void readsChanged();
    void accountsChanged(const QString& packageId);
    void settingsPathPicked(const QString& packageId, const QString& field, const QString& handle, const QString& name);
private:
    friend class ArtworkLookupRunner;
    friend class ::AddonArtworkAssets;
    AddonManager(const QString&, std::unique_ptr<QNetworkAccessManager>, QObject*);
    // Reads the package's schema file. Any thread.
    static QJsonObject schema(const InstalledPackage&);
    QJsonObject savedSettings(const QString&) const;
    QString accountReference(const InstalledPackage&) const;
    bool configured(const InstalledPackage&) const;
    QByteArray credential(const QString& package, const QString& field) const;
    struct PackageSnapshot;
    // Null when the package cannot run now. retry: read the package again.
    AddonWorker* worker(const std::shared_ptr<const PackageSnapshot>&, bool* retry);
    bool withdraw(const QString& packageId);
    bool changed(bool success, const QString& error = {});
    // One store write in three parts. prepare runs on this thread when the
    // write starts: memory only, and it reports a refusal with changed().
    // work runs on background work (or inline for the synchronous calls)
    // and uses only the copies it captured. finish runs on this thread and
    // reports the result with changed().
    struct StoreWrite {
        std::function<bool()> prepare;
        std::function<void()> work;
        std::function<bool()> finish;
    };
    bool writeNow(StoreWrite);
    QString queueWrite(StoreWrite);
    // A read on the same queue, after the writes requested before it. It
    // does not report: its finish part delivers the result.
    void queueRead(StoreWrite);
    void pumpWrites();
    void startWrite();
    void endWrite(const QString& request, bool success, int revision);
    StoreWrite enabledWrite(const QString&, bool);
    StoreWrite providerOrderWrite(const QString&, const QStringList&);
    StoreWrite settingsWrite(const QString&, const QVariantMap&, const QVariantMap&, const QString& expectedStamp, bool display);
    StoreWrite approveWrite(bool enable);
    StoreWrite packageWrite(const QString&, bool recover);
    StoreWrite safeModeWrite(bool);
    StoreWrite accountWrite(const QString&, const QString&, const DeviceTokens&, bool sessionOnly, QString* error, bool report);
    void countCrash(const QString& packageId, const QString& error);
    void replaceAccountTokens(const QString& id, const QString& field, const QJsonObject& references, const DeviceTokens&);
    // Background copy of the owner's credential store. Its session values are
    // wiped when the last write part that holds it ends.
    struct SecretsCopy {
        explicit SecretsCopy(const CredentialStore& value) : store(value) {}
        ~SecretsCopy() { store.clearSession(); }
        CredentialStore store;
    };
    struct QueuedWrite { QString request; StoreWrite write; bool report = true; };
    QList<QueuedWrite> m_writes;
    std::optional<QueuedWrite> m_runningWrite;
    std::unique_ptr<AddonRepositoryJobs> m_writeJobs;
    std::unique_ptr<AddonNativeWake> m_writeWake;
    NativeNotification m_writeSlot;
    quint64 m_writeRequests = 0, m_writeTickets = 0, m_runningTicket = 0;
    bool m_inlineStore = false, m_inlineWrites = false;
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
    // One package as an invocation and its worker use it, read on background
    // work: the verified release, settings, schema, key presence and network
    // configuration. It is current while the store token taken before the
    // read is current. A stale snapshot still answers, and a new read starts.
    struct PackageSnapshot {
        StoreAuthorityToken token;
        bool installed = false, network = false, configured = false;
        // A new worker process loads the module from disk, so each worker
        // start uses a fresh read.
        mutable bool startedWorker = false;
        InstalledPackage package;
        QJsonObject settings, schema, networkSettings;
        quint64 settingsRevision = 0, networkRevision = 0;
        QVariantMap values, secretStatus;
        QVariantList retained;
        QString accountReference, error;
        CredentialStore::Reader secrets;
        // The worker start: its executable and cache in, its arguments out.
        QString workerExecutable, precompiled;
        QStringList launchArguments;
        bool precompileMissing = false;
        bool current() const noexcept { return token.current(); }
        bool usable() const noexcept { return installed && package.enabled && configured; }
    };
    static void readSnapshot(const PackageStore&, const QString& id, PackageSnapshot*);
    QHash<QString, std::shared_ptr<const PackageSnapshot>> m_snapshots;
    QSet<QString> m_snapshotLoads;
    // The snapshot, current or not. A stale one starts a new read.
    std::shared_ptr<const PackageSnapshot> snapshot(const QString& id);
    void loadSnapshot(const QString& id);
    // The settings schema for a GUI query: the display catalogue's while a
    // settings form holds it, or the last package read.
    QJsonObject querySchema(const QString& id);
    quint64 m_readRevision = 0;
    bool m_safeMode = false;
    bool m_workerAvailable = false;
    // The attached display catalogue when this manager answers GUI queries
    // from memory, or null for tools and before attachment.
    const AddonCatalogueView* queryCatalogue() const;
    struct PendingInvoke {
        QString request, contribution, operation;
        QVariantMap context;
        NetworkBroker::RequestGuard current;
        std::shared_ptr<AddonMetadataAttemptAdmission> attempt;
        int reads = 0;
    };
    QHash<QString, QList<PendingInvoke>> m_pendingInvokes;
    void admit(PendingInvoke);
    NetworkBroker::CredentialResolver credentialResolver(const PackageSnapshot&, NetworkBroker::RequestGuard* current) const;
    NetworkBroker::CredentialResolver credentialResolver(const InstalledPackage&, const QJsonObject& settings, quint64 revision,
        bool captured, const StoreAuthorityToken&, CredentialStore::Reader secrets, NetworkBroker::RequestGuard* current) const;
    // An asset request waits for its package read under an ID from this range.
    struct PendingAsset { QString contribution; QJsonObject descriptor; NetworkBroker::Callback callback; };
    QHash<quint64, PendingAsset> m_pendingAssets;
    QHash<QString, QList<quint64>> m_assetWaits;
    QHash<quint64, quint64> m_startedAssets;
    quint64 m_deferredAssets = quint64(1) << 62;
    quint64 startAsset(const PackageSnapshot&, const QString& contribution, const QJsonObject& descriptor, NetworkBroker::Callback);
    void refuseInvoke(const PendingInvoke&, const QString& status, const QString& error);
    // The same idea for packages(), which lists every package and runs on
    // the GUI thread: one verified read per package until the store changes.
    // The stamp (size and time of state.json and its backup) also catches a
    // change made to the files outside the store.
    struct InstalledRead { StoreAuthorityToken token; QByteArray stamp; bool ok = false; InstalledPackage package; QString error; };
    QByteArray installedStamp(const QString& id) const;
    mutable QHash<QString, InstalledRead> m_installedReads;
    bool installedCached(const QString& id, InstalledPackage* package, QString* error) const;
    // Crash ledger (finding F3). A crash makes the next start wait 2 s, then
    // 4 s. The third crash in five minutes turns the package off through the
    // guarded writer, so every window applies it. Counts stay in memory and do
    // not revoke other windows. An explicit enable, rollback, install or
    // restore clears the entry.
    struct CrashLedger { QList<qint64> recent; qint64 retryAfter = 0; };
    QHash<QString, CrashLedger> m_crashes;
    // Updated packages in their health period (install step 8): the first
    // worker crash restores the previous release; the first answer ends it.
    QSet<QString> m_observing;
    void observeUpdate(const QString& id, bool update);
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
    QVariantList m_appUpdates;
    QString m_distributionBootstrapError;
    QPointer<QObject> m_mediaCapabilitiesSource;
    QList<QMetaObject::Connection> m_mediaCapabilityConnections;
    QVariantMap m_updateResult;
    struct PickedPath { QString packageId, field, kind, path, name; };
    QHash<QString, PickedPath> m_pickedPaths;
    PathPicker m_pathPicker;
    QHash<QString, QVariantMap> m_sessionSettings;
    QHash<QString, quint64> m_sessionGenerations;
    QString sessionPartition(const QString& packageId) const;
    QHash<QString, std::function<void()>> m_accountRefreshes;
    QHash<QString, qint64> m_accountRefreshTried;
    QHash<QString, qint64> m_accountExpiry;
    QElapsedTimer m_accountClock;
    std::unique_ptr<QNetworkAccessManager> m_accountNetwork;
    DevicePost m_accountPost;
};
}
