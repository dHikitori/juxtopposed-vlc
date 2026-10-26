// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_manifest.hpp"

namespace vlc::addons {
struct RepositoryTag {
    QString name, version, objectId;
    SemanticVersion parsedVersion;
};
bool parseRepositoryTags(const QByteArray&, QList<RepositoryTag>*, QString* error = nullptr);
bool selectRepositoryUpdate(const QList<RepositoryTag>&, const QString& currentVersion,
                            const QString& versionRange, bool allowPrerelease,
                            RepositoryTag* selected, QString* error = nullptr);
}
