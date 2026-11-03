// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_ADDON_PACKAGE_STORE_HPP
#define VLC_QT_ADDON_PACKAGE_STORE_HPP

#include "addon_manifest.hpp"
#include "addon_store_authority.hpp"
#include "addon_store_profile_generation.hpp"
#include <QJsonArray>
#include <QVariantMap>
#include <functional>
#include <memory>

namespace vlc::addons {
struct PrivateStorageContext;
struct DistributionInstallResult;

struct ReleaseIdentity
{
    QString source, tag, tagObjectId, commitId;
    QString objectFormat = QStringLiteral("sha1");
    bool developerSource = false;
    bool distributionSource = false;
};

struct PackageCandidate
{
    Manifest manifest;
    ReleaseIdentity identity;
    QMap<QString, QByteArray> contents;
    QByteArray digest;
};

struct InstalledPackage
{
    Manifest manifest;
    ReleaseIdentity identity;
    QByteArray digest;
    QString directory;
    QString previousDigest;
    QJsonObject grants;
    bool enabled = false;
    bool recoveryRequired = false;
    quint64 generation = 0;
};

struct ProviderPreferences
{
    QJsonObject values;
    StoreAuthorityToken authority;
};

// This raw catalogue retains each package and contribution before the caller
// selects eligible providers. The caller derives configuration, secret presence,
// and account scope from these settings and schema plus its captured presence.
struct MetadataCataloguePackage
{
    InstalledPackage package;
    QJsonObject settings, schema;
    quint64 settingsRevision = 0;
    QList<Contribution> contributions;
    bool savedEnabled = false;
};
struct MetadataCatalogueSnapshot
{
    static constexpr qsizetype MaximumCandidates = 256;
    static constexpr qsizetype MaximumContributions = 256;
    // This is the canonical retained descriptor limit. Verification reads each
    // complete package under the separate Manifest file and package limits.
    static constexpr qsizetype MaximumBytes = 1024 * 1024;
    QList<MetadataCataloguePackage> packages;
    QJsonObject providerPreferences, distributionPolicy;
    StoreAuthorityToken queuedAuthority, authority;
    QByteArray digest;
    qsizetype retainedBytes = 0;
    qsizetype candidateCount = 0;
    qsizetype rawContributionCount = 0;
    bool safeMode = false;
    // The read was refused only because another reader or a writer held
    // store.lock; trying again later can succeed.
    bool storeBusy = false;
    bool current() const noexcept { return queuedAuthority.current() && authority.current(); }
};
using MetadataCatalogueReader = std::function<bool(MetadataCatalogueSnapshot*, QString*)>;

struct DisplayCatalogueSnapshot;
struct DisplayCatalogueRequest;
class StoreProfileWritePermit;
class PackageStore
{
public:
    explicit PackageStore(QString storageDirectory);
    QString storageDirectory() const;
    StoreAuthorityToken authorityToken() const noexcept { return m_authority.capture(); }
    void invalidateAuthority() const noexcept { m_authority.invalidate(); }
    StorePackageRetirement retirePackage(const QString& id) const { return m_authority.retirePackage(id); }
    bool packageRetiring(const QString& id) const noexcept { return m_authority.packageRetiring(id); }
    static bool inspectDirectory(const QString&, const ReleaseIdentity&, PackageCandidate*, QString* error = nullptr);
    // No shell or checkout. Local fixtures require explicit developerLocalSource.
    static bool fetchGitTag(const QString& source, const QString& tag, PackageCandidate*,
                            QString* error = nullptr, bool developerLocalSource = false,
                            int timeoutMs = 30000);
    bool fetchRepositoryUpdate(const QString& id, const QString& versionRange, bool allowPrerelease,
                               PackageCandidate*, QString* error = nullptr,
                               bool developerLocalSource = false, int timeoutMs = 30000) const;
    // Distribution approval is host-only and commits with the current release pointer.
    bool install(const PackageCandidate&, const QJsonObject& approvedGrants, QString* error = nullptr, bool distributionApproval = false);
    StoreProfileGeneration::Change install(const StoreProfileWriteContext&,
        const PackageCandidate&, const QJsonObject& approvedGrants,
        QString* error = nullptr, bool distributionApproval = false);
    // Host distribution bootstrap. This retains one Store lock across the
    // suppression/current-state decision and an absent install plus enable.
    bool ensureDistributionPackage(const QString& directory,const Manifest& verifiedOffer,
        DistributionInstallResult*,QString* error=nullptr);
    StoreProfileGeneration::Change ensureDistributionPackage(const StoreProfileWriteContext&,
        const QString& directory, const Manifest& verifiedOffer,
        DistributionInstallResult*, QString* error = nullptr);
    bool installed(const QString& id, InstalledPackage*, QString* error = nullptr) const;
    QStringList installedIds() const;
    // These IDs permit saved-data cleanup only. They grant no package authority.
    QStringList savedDataIds() const;
    bool setEnabled(const QString& id, bool enabled, QString* error = nullptr);
    StoreProfileGeneration::Change setEnabled(const StoreProfileWriteContext&,
        const QString& id, bool enabled, QString* error = nullptr);
    // These operations use store.lock. Capture on joined background work for
    // native metadata plans. Retain authority watches through final Apply.
    bool providerPreferences(ProviderPreferences*, QString* error = nullptr) const;
    // Call this memory-only factory on the owner thread. Run its callable on
    // joined background work. One store.lock covers preferences and all package
    // reads. The snapshot retains both queued and locked authority tokens.
    MetadataCatalogueReader metadataCatalogueReader() const;
    std::function<bool(const DisplayCatalogueRequest&,DisplayCatalogueSnapshot*,QString*)> displayCatalogueReader(QString locale) const;
    StorePackageStamp packageStamp(const QString& id) const { return m_authority.packageStamp(id); }
    StoreAuthorityChanges onDisplayChanged(std::function<void()> nativeWake) const noexcept
    { return m_authority.onChanged(std::move(nativeWake)); }
    bool saveProviderOrder(const QString& feature, const QStringList& ids,
                           QJsonObject* saved = nullptr, QString* error = nullptr);
    bool saveBuiltinProviderEnabled(const QString& id, bool enabled,
                                   QJsonObject* saved = nullptr, QString* error = nullptr);
    // Host only. These paths use the callback's existing lock and fence.
    // Run them only inside StoreProfileGeneration::write on joined work.
    // An uncertain write requires recovery before peer admission.
    StoreProfileGeneration::Change saveProviderOrder(const StoreProfileWriteContext&,
        const QString& feature, const QStringList& ids,
        QJsonObject* saved = nullptr, QString* error = nullptr);
    StoreProfileGeneration::Change saveBuiltinProviderEnabled(const StoreProfileWriteContext&,
        const QString& id, bool enabled, QJsonObject* saved = nullptr, QString* error = nullptr);
    bool rollback(const QString& id, QString* error = nullptr);
    StoreProfileGeneration::Change rollback(const StoreProfileWriteContext&,
        const QString& id, QString* error = nullptr);
    bool uninstall(const QString& id, bool removeData, QString* error = nullptr);
    StoreProfileGeneration::Change uninstall(const StoreProfileWriteContext&,
        const QString& id, bool removeData, QString* error = nullptr);
    bool canUninstall(const QString& id, bool removeData, QString* error = nullptr) const;
    // Versioned nonsecret settings live in the same state pointer as the release.
    bool settings(const QString& id, QJsonObject*, quint64* revision = nullptr, QString* error = nullptr) const;
    bool networkConfiguration(const InstalledPackage&, QJsonObject*, quint64* revision = nullptr) const;
    bool networkConfigurationMatches(const InstalledPackage&, const QJsonObject&, quint64 revision) const;
    bool saveSettings(const QString& id, const QJsonObject&, quint64 expectedRevision, QString* error = nullptr);
    bool saveSettings(const QString& id,const QJsonObject&,quint64 expectedRevision,const QString& expectedStamp,QString* error=nullptr);
    // A guarded save requires the exact release, generation and settings stamp.
    StoreProfileGeneration::Change saveSettings(const StoreProfileWriteContext&,
        const QString& id, const QJsonObject&, quint64 expectedRevision,
        const QString& expectedStamp, QString* error = nullptr);
    bool privateStorage(const PrivateStorageContext&, const QString& operation, const QJsonObject& request,
                        QJsonObject* response, const std::function<bool()>& pending,
                        const std::function<bool()>& beginCommit, QString* error = nullptr) const;
    QJsonArray retainedGenerations(const QString& id, QString* error = nullptr) const;
    bool recover(const QString& id, QString* error = nullptr);
    // This repairs one package. It does not validate the whole profile.
    StoreProfileGeneration::Change recover(const StoreProfileWriteContext&,
        const QString& id, QString* error = nullptr);
    bool safeMode() const;
    bool setSafeMode(bool enabled, QString* error = nullptr);
    StoreProfileGeneration::Change setSafeMode(const StoreProfileWriteContext&,
        bool enabled, QString* error = nullptr);
    QStringList credentialReferences(const QString& id, const QString& field = {}) const;
    bool distributionSuppressed(const QString& id) const;
    bool setDistributionSuppressed(const QString& id, bool suppressed, QString* error = nullptr);
    StoreProfileGeneration::Change setDistributionSuppressed(const StoreProfileWriteContext&,
        const QString& id, bool suppressed, QString* error = nullptr);

    // True once this process shares the profile with other app processes.
    // The ordinary write methods above then refuse; use the guarded methods
    // inside a coordinator-permitted StoreProfileGeneration write.
    bool sharedProfile() const noexcept { return m_authority.peerAttached(); }
    // The app's profile host supplies these. All copies of this store share
    // them. waitReady blocks until the profile session is joined; permit asks
    // the coordinator for one write. A null permit means no session.
    struct ProfileWriter {
        std::function<bool(qint64 deadline)> waitReady;
        std::function<std::unique_ptr<StoreProfileWritePermit>(qint64 deadline)> permit;
    };
    using GuardedChange = std::function<StoreProfileGeneration::Change(const StoreProfileWriteContext&, QString*)>;
    void setProfileWriter(ProfileWriter) const;
    // One store change. An unshared profile runs the ordinary method. A shared
    // profile takes a prepared writer and the coordinator's permit, then runs
    // the guarded method inside the permitted write. It can wait for other app
    // processes up to WriteLimit. Never call it from a store or native callback.
    bool change(const std::function<bool(QString*)>& ordinary, const GuardedChange& guarded,
                QString* error = nullptr) const;
    static constexpr qint64 WriteLimit = 10 * 1000 * 1000;
    // Profile recovery for the coordinator. It runs with the owner lock,
    // store.lock and the exclusive fence held. It removes release staging left
    // by an interrupted install. Package states that need recovery keep their
    // flag for the user's explicit Restore action.
    static bool recoverProfile(const StoreProfileWriteContext&, QString* error = nullptr);

private:
    bool ordinaryWriteAllowed(QString* error) const;
    struct WriteSlot;
    QString m_root;
    StoreAuthority m_authority;
    std::shared_ptr<WriteSlot> m_writeSlot;
    StoreProfileGeneration::Change installLocked(const PackageCandidate&, const QJsonObject& grants,
        QString* error, bool distributionApproval, bool cancelBeforeCommit = false,
        const StoreProfileWriteContext* = nullptr, bool initiallyEnabled = false);
    StoreProfileGeneration::Change uninstallLocked(const QString&, bool, QString*, const StoreProfileWriteContext*);
    StoreProfileGeneration::Change setEnabledLocked(const QString&, bool, QString* error,
        const StoreProfileWriteContext* = nullptr);
    StoreProfileGeneration::Change rollbackLocked(const QString&, QString*, const StoreProfileWriteContext*);
    StoreProfileGeneration::Change saveSettingsLocked(const QString&, const QJsonObject&, quint64,
        const QString&, QString*, const StoreProfileWriteContext*);
    StoreProfileGeneration::Change recoverLocked(const QString&, QString*, const StoreProfileWriteContext*);
    bool cataloguePackage(const QString&,bool,const QJsonObject&,MetadataCataloguePackage*,QMap<QString,QByteArray>*,QString*) const;
    bool catalogueStatePackage(const QString&,const QJsonObject&,bool,const QJsonObject&,
        MetadataCataloguePackage*,QMap<QString,QByteArray>*,QString*) const;
    bool cataloguePolicy(MetadataCatalogueSnapshot*,QString*) const;
    bool readDisplayCatalogue(const StoreAuthorityToken&,const QString&,const DisplayCatalogueRequest&,DisplayCatalogueSnapshot*,QString*) const;
    bool readMetadataCatalogue(const StoreAuthorityToken&, MetadataCatalogueSnapshot*, QString*) const;
    bool loadState(const QString& id, QJsonObject*, QString* error,
        const StoreProfileWriteContext* = nullptr) const;
    bool saveState(const QString& id, const QJsonObject&, QString* error, bool authority = true) const;
    StoreProfileGeneration::Change saveStateLocked(const QString&, const QJsonObject&, QString*,
        bool authority, const StoreProfileWriteContext*) const;
    QString releaseDirectory(const QString& id, const QString& digest) const;
    bool validateSnapshot(const QString& id, const QJsonObject&, QString* error) const;
    QString privateStorageDirectory(const QString& id) const;
    bool collectPrivateStorage(const QString& id, const QJsonObject& state, QString* error) const;
    bool saveProviderPreference(const QString&, const QJsonValue&, bool builtin,
                                QJsonObject* saved, QString* error);
    StoreProfileGeneration::Change saveProviderPreferenceLocked(const QString&, const QJsonValue&,
        bool builtin, QJsonObject* saved, QString* error, const StoreProfileWriteContext*);
    StoreProfileGeneration::Change setSafeModeLocked(bool, QString* error, const StoreProfileWriteContext*);
    StoreProfileGeneration::Change setDistributionSuppressedLocked(const QString&, bool,
        QString* error, const StoreProfileWriteContext*);
};

} // namespace vlc::addons
#endif
