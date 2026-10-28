// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_distribution_bootstrap.hpp"
#include "addon_package_store.hpp"
#include "addon_repository_cancellation.hpp"
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
namespace vlc::addons {
QVariantMap DistributionBootstrapItem::row() const
{
    QVariantMap result=offer;result.insert(QStringLiteral("id"),id);
    result.insert(QStringLiteral("directory"),directory);result.insert(QStringLiteral("status"),status);
    result.insert(QStringLiteral("success"),success);result.insert(QStringLiteral("error"),error);
    result.insert(QStringLiteral("recoveryRequired"),recoveryRequired);return result;
}
namespace distributionBootstrap {
bool validEntries(const QList<DistributionBootstrapEntry>& entries)
{
    if(entries.isEmpty() || entries.size()>MaximumEntries)return false;
    static const QRegularExpression id(QStringLiteral("^[a-z][a-z0-9]*(?:[.-][a-z0-9]+)+$"));
    QSet<QString> unique;
    for(const auto& entry:entries) {
        if(entry.expectedId.size()>128 || !id.match(entry.expectedId).hasMatch() || unique.contains(entry.expectedId)
            || (entry.installedDirectory.isEmpty() && entry.fallbackDirectory.isEmpty()))return false;
        for(const auto& path:{entry.installedDirectory,entry.fallbackDirectory})
            if(path.size()>MaximumPathCharacters || path.contains(QChar::Null))return false;
        unique.insert(entry.expectedId);
    }
    return true;
}
DistributionBootstrapItem ensure(PackageStore& store,const QString& directory,
    const QString& expectedId,const QString& previousSource)
{
    DistributionBootstrapItem result;result.id=expectedId;result.directory=directory;
    if(RepositoryCancellation::requested()){result.status=QStringLiteral("canceled");result.error=QStringLiteral("The app canceled the startup package check.");return result;}
    Manifest manifest;
    if(!Manifest::loadDirectory(directory,&manifest,nullptr,&result.error)){result.error=result.error.left(1024);return result;}
    if(manifest.id!=expectedId){result.error=QStringLiteral("The distribution package ID is not correct.");return result;}
    const auto source=manifest.source.toString();
    if(!previousSource.isEmpty() && previousSource!=source){result.error=QStringLiteral("The app package source changed.");return result;}
    result.offer={{QStringLiteral("name"),manifest.name},{QStringLiteral("version"),manifest.version},{QStringLiteral("source"),source}};
    DistributionInstallResult installed;
    if(!store.ensureDistributionPackage(directory,manifest,&installed,&result.error)) {
        result.error=result.error.left(1024);
        if(RepositoryCancellation::requested())result.status=QStringLiteral("canceled");
        return result;
    }
    result.success=true;result.recoveryRequired=installed.recoveryRequired;
    switch(installed.state) {
        case DistributionInstallResult::State::Suppressed:result.status=QStringLiteral("suppressed");break;
        case DistributionInstallResult::State::Existing:result.status=QStringLiteral("existing");break;
        case DistributionInstallResult::State::Installed:result.status=QStringLiteral("installed");break;
    }
    return result;
}
DistributionBootstrapBatch run(PackageStore store,QList<DistributionBootstrapEntry> entries)
{
    DistributionBootstrapBatch result;
    if(!validEntries(entries))return result;
    result.items.reserve(entries.size());
    for(const auto& entry:entries) {
        if(RepositoryCancellation::requested()) {
            result.canceled=true;DistributionBootstrapItem item;item.id=entry.expectedId;item.status=QStringLiteral("canceled");
            item.error=QStringLiteral("The app canceled the startup package check.");result.items.append(std::move(item));continue;
        }
        // Filesystem selection belongs to the worker, including a missing
        // installed directory. An invalid existing directory does not fall back.
        const auto directory=!entry.installedDirectory.isEmpty() && QFileInfo::exists(entry.installedDirectory)
            ?entry.installedDirectory:entry.fallbackDirectory;
        result.items.append(ensure(store,directory,entry.expectedId));
    }
    result.canceled|=RepositoryCancellation::requested();return result;
}
}
}
