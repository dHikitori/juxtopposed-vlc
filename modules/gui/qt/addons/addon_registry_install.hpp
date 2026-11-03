// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_registry_types.hpp"
#include <optional>

namespace vlc::addons {
struct PackageCandidate;

// The registry names a release; the candidate is what the app fetched from
// Git itself. Every identity value must match, or the app does not offer the
// release. A registry cannot deliver bytes: at worst it names a release the
// app refuses here.
bool verifyRegistryIdentity(const PackageCandidate&, const RegistryRelease&, QString* error = nullptr);

// The newest release in the index that the installed package can update to:
// the same choice selectRepositoryUpdate makes from Git tags, limited to
// releases this host can run. None when the index names another source, or
// the package is a developer or app copy that updates another way.
std::optional<RegistryRelease> selectRegistryUpdate(const RegistryIndexEntry&, const QString& installedVersion,
    const QString& installedSource, const QString& range, bool allowPrerelease, QString* error = nullptr);
}
