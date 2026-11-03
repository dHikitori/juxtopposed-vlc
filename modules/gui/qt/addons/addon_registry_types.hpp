// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QStringList>
#include <QVariantMap>
#include <optional>

// The hosted add-on registry's API v1 (api.hakone.org/v1). The registry names
// a Git repository, a release tag and the identities the app must find after
// it fetches that tag itself. It never supplies package bytes. Each parser
// checks every field it keeps and refuses the whole response when one is
// wrong; unknown fields are ignored, because v1 only adds fields.
namespace vlc::addons {

struct RegistryInstall
{
    QString source, tag, tagObjectId, commitId, objectFormat;
    QByteArray digest;
};

struct RegistryRelease
{
    QString addonId, version, state, yankReason, addonApi, changelog, webUrl, sourceUrl;
    QJsonObject contracts, permissions;
    QJsonArray contributions;
    QStringList locales;
    RegistryInstall install;
    QDateTime publishedAt;
    qint64 packageBytes = 0;
    bool prerelease = false, runtime = false;
    bool yanked() const { return state == QLatin1String("yanked"); }
    QVariantMap toVariant() const;
};

struct RegistryAddon
{
    QString id, name, summary, license, publisherName, publisherSlug, webUrl;
    QStringList categories, contributionTypes;
    // From "latest"; empty when every release is yanked.
    QString latestVersion, latestAddonApi;
    QDateTime updatedAt;
    bool publisherVerified = false, latestPrerelease = false, featured = false;
    // Detail only.
    QString source, homepageUrl, description, state;
    std::optional<RegistryRelease> latestRelease;
    QVariantMap toVariant() const;
};

struct RegistryIndexEntry
{
    QString id, source;
    // Update candidates, newest first. Each has its install identity.
    QList<RegistryRelease> releases;
    QStringList yanked;
};

struct RegistryIndex
{
    qint64 revision = 0;
    QList<RegistryIndexEntry> addons;
    const RegistryIndexEntry* find(const QString& id) const;
};

struct RegistryPage
{
    QList<RegistryAddon> items;
    QString next;
};

namespace registry {
inline constexpr qsizetype MaximumIndexBytes = 2 * 1024 * 1024;
inline constexpr qsizetype MaximumResponseBytes = 512 * 1024;
inline constexpr qsizetype MaximumIndexEntries = 2000;
inline constexpr qsizetype MaximumIndexReleases = 10;
inline constexpr qsizetype MaximumPageItems = 100;

// allowHttpLinks: the registry itself is a local development server, so its
// web links may use http.
bool parseIndex(const QByteArray&, RegistryIndex*, QString* error = nullptr);
bool parsePage(const QByteArray&, RegistryPage*, bool allowHttpLinks, QString* error = nullptr);
bool parseAddon(const QByteArray&, RegistryAddon*, bool allowHttpLinks, QString* error = nullptr);
bool parseRelease(const QByteArray&, RegistryRelease*, bool allowHttpLinks, QString* error = nullptr);
// The error answer's code, such as "not_found", or empty.
QString errorCode(const QByteArray&);
}
}
