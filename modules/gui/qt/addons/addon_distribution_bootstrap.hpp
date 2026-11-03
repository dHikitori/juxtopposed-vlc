// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QVariantMap>
namespace vlc::addons {
class PackageStore;
struct DistributionBootstrapEntry {
    QString expectedId,installedDirectory,fallbackDirectory;
};
struct DistributionInstallResult {
    enum class State { Suppressed,Existing,Installed };
    State state=State::Existing;
    bool recoveryRequired=false;
    // For an existing package: what is installed, and the permissions the
    // app's copy asks for that its grants do not already give. The startup
    // update prompt uses these without another package read.
    QString installedVersion;
    bool installedEnabled=false;
    QJsonObject newPermissions;
};
struct DistributionBootstrapItem {
    QString id,directory,error,status=QStringLiteral("error");
    QVariantMap offer;
    bool success=false,recoveryRequired=false;
    QString installedVersion;
    bool installedEnabled=false;
    QJsonObject newPermissions;
    QVariantMap row() const;
};
struct DistributionBootstrapBatch {
    QList<DistributionBootstrapItem> items;
    bool canceled=false;
};
namespace distributionBootstrap {
inline constexpr qsizetype MaximumEntries=16;
inline constexpr qsizetype MaximumPathCharacters=4096;
bool validEntries(const QList<DistributionBootstrapEntry>&);
// Host data only. The synchronous tool path and joined startup path use this
// same operation. No Manager, QObject, credential or guest enters this work.
DistributionBootstrapItem ensure(PackageStore&,const QString& directory,
    const QString& expectedId,const QString& previousSource={});
DistributionBootstrapBatch run(PackageStore,QList<DistributionBootstrapEntry>);
}
}
