// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_catalogue_view.hpp"
#include "addon_manager.hpp"
#include <QPointer>
namespace vlc::addons {
namespace {
constexpr char childName[]="hakoneDisplayCatalogue";
AddonCatalogueView::Backend backend(AddonManager* manager)
{
    const QPointer<AddonManager> owner(manager);
    return {[owner] {return owner?owner->displayCatalogueSource():std::nullopt;},
        [owner](std::function<void()> signal) {return owner?owner->watchDisplayCatalogue(std::move(signal)):StoreAuthorityChanges{};}};
}
} // namespace
AddonCatalogueView::AddonCatalogueView(AddonManager* manager,AddonStreamLifetime::Handle lifetime)
    :AddonCatalogueView(backend(manager),std::move(lifetime),manager)
{
    if(attached(manager)) { close();return; }
    setObjectName(QString::fromLatin1(childName));
    if(!manager)return;
    connect(manager,&AddonManager::revisionChanged,this,&AddonCatalogueView::inputsChanged);
    connect(manager,&AddonManager::distributionBootstrapChanged,this,&AddonCatalogueView::inputsChanged);
    connect(this,&AddonCatalogueView::catalogueChanged,manager,&AddonManager::catalogueChanged);
    connect(this,&AddonCatalogueView::freshnessChanged,manager,&AddonManager::catalogueFreshnessChanged);
    connect(this,&QObject::destroyed,manager,&AddonManager::catalogueFreshnessChanged);
    connect(manager,&AddonManager::capabilitiesChanged,this,[this,manager] {
        // Player-capability signals do not capture or scan the catalogue.
        if(!m_inputs || manager->workerExecutable()!=m_inputs->workerExecutable)inputsChanged();
    });
    connect(manager,&AddonManager::retiring,this,&AddonCatalogueView::close,Qt::DirectConnection);
}
}
