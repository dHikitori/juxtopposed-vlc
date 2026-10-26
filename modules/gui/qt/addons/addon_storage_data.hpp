// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_manifest.hpp"

namespace vlc::addons {
struct PrivateStorageLimits {
    static constexpr qint64 MaximumBytes = 1024 * 1024;
    static constexpr qint64 MaximumValueBytes = 64 * 1024;
    static constexpr int MaximumKeys = 1024;
    static constexpr int MaximumAccounts = 16;
    // These limits leave space for the host request and response objects.
    static constexpr int MaximumValueNodes = 4000;
    static constexpr int MaximumValueDepth = 15;
    static constexpr int MaximumRetainedSnapshots = 16;
    static constexpr int MaximumPhysicalSnapshots = MaximumRetainedSnapshots + 1;
};
int privateStorageVersion(const Manifest&);
QJsonObject emptyPrivateStorage(int version);
bool privateStorageKey(const QString&);
bool privateStorageValue(const QJsonValue&);
bool validatePrivateStorage(const QJsonObject&, qint64 quotaBytes, qint64* usedBytes = nullptr, QString* error = nullptr);
bool validatePrivateStorageDeclaration(const Manifest&, const QMap<QString, QByteArray>& contents,
                                       bool resourcesAvailable, QString* error = nullptr);
bool migratePrivateStorage(const Manifest& next, const QMap<QString, QByteArray>& contents,
                           int previousVersion, const QJsonObject& previous,
                           qint64 quotaBytes, QJsonObject* output, QString* error = nullptr);
// The host supplies directory. A guest key never becomes a file path.
bool readPrivateStorage(const QString& directory, const QString& digest, QJsonObject*, QString* error = nullptr);
bool writePrivateStorage(const QString& directory, const QJsonObject&, QString* digest, QString* error = nullptr);
}
