// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_ADDON_PACKAGE_STORE_HPP
#define VLC_QT_ADDON_PACKAGE_STORE_HPP

#include "addon_manifest.hpp"
#include "addon_store_authority.hpp"
#include <QJsonArray>
#include <QVariantMap>
#include <functional>

namespace vlc::addons {
struct PrivateStorageContext;

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
    bool current() const noexcept { return queuedAuthority.current() && authority.current(); }
};
using MetadataCatalogueReader = std::function<bool(MetadataCatalogueSnapshot*, QString*)>;

class PackageStore
{
public:
    explicit PackageStore(QString storageDirectory);
    QString storageDirectory() const;
    StoreAuthorityToken authorityToken() const noexcept { return m_authority.capture(); }
    StoreAuthorityChanges onDisplayChanged(std::function<void()> nativeWake) const noexcept
    { return m_authority.onChanged(std::move(nativeWake)); }
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
    bool installed(const QString& id, InstalledPackage*, QString* error = nullptr) const;
    QStringList installedIds() const;
    // These IDs permit saved-data cleanup only. They grant no package authority.
    QStringList savedDataIds() const;
    bool setEnabled(const QString& id, bool enabled, QString* error = nullptr);
    // These operations use store.lock. Capture on joined background work for
    // native metadata plans. Retain authority watches through final Apply.
    bool providerPreferences(ProviderPreferences*, QString* error = nullptr) const;
    // Call this memory-only factory on the owner thread. Run its callable on
    // joined background work. One store.lock covers preferences and all package
    // reads. The snapshot retains both queued and locked authority tokens.
    MetadataCatalogueReader metadataCatalogueReader() const;
    bool saveProviderOrder(const QString& feature, const QStringList& ids,
                           QJsonObject* saved = nullptr, QString* error = nullptr);
    bool saveBuiltinProviderEnabled(const QString& id, bool enabled,
                                   QJsonObject* saved = nullptr, QString* error = nullptr);
    bool rollback(const QString& id, QString* error = nullptr);
    bool uninstall(const QString& id, bool removeData, QString* error = nullptr);
    bool canUninstall(const QString& id, bool removeData, QString* error = nullptr) const;
    // Versioned nonsecret settings live in the same state pointer as the release.
    bool settings(const QString& id, QJsonObject*, quint64* revision = nullptr, QString* error = nullptr) const;
    bool networkConfiguration(const InstalledPackage&, QJsonObject*, quint64* revision = nullptr) const;
    bool networkConfigurationMatches(const InstalledPackage&, const QJsonObject&, quint64 revision) const;
    bool saveSettings(const QString& id, const QJsonObject&, quint64 expectedRevision, QString* error = nullptr);
    bool privateStorage(const PrivateStorageContext&, const QString& operation, const QJsonObject& request,
                        QJsonObject* response, const std::function<bool()>& pending,
                        const std::function<bool()>& beginCommit, QString* error = nullptr) const;
    QJsonArray retainedGenerations(const QString& id, QString* error = nullptr) const;
    bool recover(const QString& id, QString* error = nullptr);
    bool safeMode() const;
    bool setSafeMode(bool enabled, QString* error = nullptr);
    QStringList credentialReferences(const QString& id, const QString& field = {}) const;
    bool distributionSuppressed(const QString& id) const;
    bool setDistributionSuppressed(const QString& id, bool suppressed, QString* error = nullptr);

private:
    QString m_root;
    StoreAuthority m_authority;
    bool readMetadataCatalogue(const StoreAuthorityToken&, MetadataCatalogueSnapshot*, QString*) const;
    bool loadState(const QString& id, QJsonObject*, QString* error) const;
    bool saveState(const QString& id, const QJsonObject&, QString* error, bool authority = true) const;
    QString releaseDirectory(const QString& id, const QString& digest) const;
    bool validateSnapshot(const QString& id, const QJsonObject&, QString* error) const;
    QString privateStorageDirectory(const QString& id) const;
    bool collectPrivateStorage(const QString& id, const QJsonObject& state, QString* error) const;
    bool saveProviderPreference(const QString&, const QJsonValue&, bool builtin,
                                QJsonObject* saved, QString* error);
};

} // namespace vlc::addons
#endif
