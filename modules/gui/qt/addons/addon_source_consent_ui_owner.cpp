// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_source_consent_ui.hpp"
#include "addon_discovery_source_owner.hpp"

namespace vlc::addons {
namespace {
AddonSourceConsentUi::Backend backend(AddonDiscoverySourceOwner* source) {
    const QPointer<AddonDiscoverySourceOwner> owner(source);
    AddonSourceConsentUi::Backend result;
    result.lifetime=source;
    result.active=[owner] { return owner && owner->active(); };
    result.request=[owner](const QString& contribution) {
        return owner ? owner->requestApproval(contribution) : QString{};
    };
    result.read=[owner](const QString& request) {
        return owner ? owner->approval(request) : QVariantMap{};
    };
    result.change=[owner](const QString& request,bool approve) {
        return owner && (approve ? owner->approve(request) : owner->revoke(request));
    };
    result.discard=[owner](const QString& request) { if(owner) owner->discardApproval(request); };
    return result;
}
}
AddonSourceConsentUi::AddonSourceConsentUi(AddonDiscoverySourceOwner* owner,QObject* parent)
    :AddonSourceConsentUi(backend(owner),parent) {
    if(owner) {
        connect(owner,&AddonDiscoverySourceOwner::approvalReady,this,&AddonSourceConsentUi::approvalReady);
        connect(owner,&AddonDiscoverySourceOwner::actionFinished,this,&AddonSourceConsentUi::actionFinished);
        connect(owner,&AddonDiscoverySourceOwner::changed,this,&AddonSourceConsentUi::refresh);
    }
}
}
