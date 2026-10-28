// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
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
};
struct DistributionBootstrapItem {
    QString id,directory,error,status=QStringLiteral("error");
    QVariantMap offer;
    bool success=false,recoveryRequired=false;
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
