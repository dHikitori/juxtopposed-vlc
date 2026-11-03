// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_registry_install.hpp"
#include "addon_package_store.hpp"
#include "addon_repository_updates.hpp"

namespace vlc::addons {
namespace {
bool fail(QString* error, const QString& message)
{ if (error) *error = message; return false; }
}

bool verifyRegistryIdentity(const PackageCandidate& candidate, const RegistryRelease& release, QString* error)
{
    const auto& fetched = candidate.identity;
    const auto& named = release.install;
    const char* differs = fetched.source != named.source ? "source"
        : fetched.tag != named.tag ? "tag"
        : fetched.objectFormat != named.objectFormat ? "object format"
        : fetched.tagObjectId != named.tagObjectId ? "tag object"
        : fetched.commitId != named.commitId ? "commit"
        : candidate.digest != named.digest ? "package digest"
        : candidate.manifest.id != release.addonId ? "package ID"
        : candidate.manifest.version != release.version ? "version" : nullptr;
    if (fetched.developerSource || fetched.distributionSource)
        differs = "kind of package";
    if (differs) {
        qWarning("Add-on registry: the fetched %s of %s %s differs from the registry",
                 differs, qUtf8Printable(release.addonId), qUtf8Printable(release.version));
        return fail(error, QStringLiteral("The release in the repository does not match the registry. The app did not install it."));
    }
    if (error) error->clear();
    return true;
}

std::optional<RegistryRelease> selectRegistryUpdate(const RegistryIndexEntry& entry, const QString& installedVersion,
    const QString& installedSource, const QString& range, bool allowPrerelease, QString* error)
{
    if (error) error->clear();
    if (entry.source != installedSource) return std::nullopt;
    QList<RepositoryTag> tags;
    for (const auto& release : entry.releases) {
        if (!hostAcceptsApi(release.addonApi, release.contracts)) continue;
        RepositoryTag tag;
        tag.name = release.install.tag; tag.version = release.version; tag.objectId = release.install.tagObjectId;
        if (!SemanticVersion::parse(tag.version, &tag.parsedVersion)) continue;
        tags.append(tag);
    }
    RepositoryTag selected;
    if (!selectRepositoryUpdate(tags, installedVersion, range, allowPrerelease, &selected, error) || selected.name.isEmpty())
        return std::nullopt;
    for (const auto& release : entry.releases)
        if (release.install.tag == selected.name) return release;
    return std::nullopt;
}
}
