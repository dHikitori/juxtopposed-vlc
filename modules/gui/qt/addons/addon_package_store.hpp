// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_ADDON_PACKAGE_STORE_HPP
#define VLC_QT_ADDON_PACKAGE_STORE_HPP

#include "addon_manifest.hpp"
#include <QJsonArray>
#include <QVariantMap>

namespace vlc::addons {

struct ReleaseIdentity
{
    QString source, tag, tagObjectId, commitId;
    QString objectFormat = QStringLiteral("sha1");
    bool developerSource = false;
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
    quint64 generation = 0;
};

class PackageStore
{
public:
    explicit PackageStore(QString storageDirectory);
    QString storageDirectory() const;
    static bool inspectDirectory(const QString&, const ReleaseIdentity&, PackageCandidate*, QString* error = nullptr);
    // No shell or checkout. Local fixtures require explicit developerLocalSource.
    static bool fetchGitTag(const QString& source, const QString& tag, PackageCandidate*,
                            QString* error = nullptr, bool developerLocalSource = false,
                            int timeoutMs = 30000);
    bool install(const PackageCandidate&, const QJsonObject& approvedGrants, QString* error = nullptr);
    bool installed(const QString& id, InstalledPackage*, QString* error = nullptr) const;
    QStringList installedIds() const;
    bool setEnabled(const QString& id, bool enabled, QString* error = nullptr);
    bool rollback(const QString& id, QString* error = nullptr);
    bool uninstall(const QString& id, bool removeData, QString* error = nullptr);
    // Versioned nonsecret settings live in the same state pointer as the release.
    bool settings(const QString& id, QJsonObject*, quint64* revision = nullptr, QString* error = nullptr) const;
    bool saveSettings(const QString& id, const QJsonObject&, quint64 expectedRevision, QString* error = nullptr);

private:
    QString m_root;
    bool loadState(const QString& id, QJsonObject*, QString* error) const;
    bool saveState(const QString& id, const QJsonObject&, QString* error) const;
    QString releaseDirectory(const QString& id, const QString& digest) const;
};

} // namespace vlc::addons
#endif
