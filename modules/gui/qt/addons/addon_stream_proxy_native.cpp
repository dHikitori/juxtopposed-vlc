// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "addon_stream_proxy_native.hpp"
#include <vlc_common.h>
#include <vlc_input_policy.h>
#include <vlc_source_backend.h>
#include <new>
namespace vlc::addons {
namespace {
struct Lease {
    std::shared_ptr<AddonStreamProxy> proxy;
    static void cancel(void* opaque) { static_cast<Lease*>(opaque)->proxy->cancel(); }
    static void destroy(void* opaque) { delete static_cast<Lease*>(opaque); }
};
AddonStreamProxy::Spec authorizedSpec(const AddonNativeSourceResolver::Admission& admission,
    AddonNativeSourceResolver::Current current, AddonStreamProxy::Spec spec)
{
    const auto uri=admission.authorization.uri(); const auto generation=admission.authorization.generation();
    spec.lifetime=admission.lifetime;
    spec.live=[token=admission.authorization.liveToken(),configuration=admission.configurationLive,
               lifetime=admission.lifetime,current=std::move(current)] {
        try { return lifetime.current() && token.current() && current && current()
            && configuration && configuration() && token.current(); } catch (...) { return false; }
    };
    spec.authorize=[admission,uri,generation] { return admission.authorize(uri,generation); };
    return spec;
}
AddonStreamProxy::Ready prepareResult(AddonNativeSourceResolver::PreparedCallback done)
{
    return [done=std::move(done)](std::shared_ptr<AddonStreamProxy> proxy,const QString&) {
        if (!proxy) { done({}); return; }
        auto* policy=vlc_input_policy_NewProxy(proxy->port(),proxy->pathPrefix().constData(),nullptr,false);
        auto* opaque=new (std::nothrow) Lease{proxy};
        static const vlc_source_lease_operations operations{Lease::cancel,Lease::destroy};
        auto* lease=opaque ? vlc_source_lease_New(&operations,opaque) : nullptr;
        if (!policy || !lease) {
            if (!lease) delete opaque; else vlc_source_lease_Release(lease);
            vlc_input_policy_Release(policy); proxy->cancel(); done({}); return;
        }
        const auto policyRelease=[](vlc_input_policy_t* value) { vlc_input_policy_Release(value); };
        const auto leaseRelease=[](vlc_source_lease_t* value) { vlc_source_lease_Release(value); };
        std::unique_ptr<vlc_input_policy_t,decltype(policyRelease)> policyOwner(policy,policyRelease);
        std::unique_ptr<vlc_source_lease_t,decltype(leaseRelease)> leaseOwner(lease,leaseRelease);
        const auto heldPolicy=std::shared_ptr<vlc_input_policy_t>(vlc_input_policy_Hold(policy),policyRelease);
        const auto heldLease=std::shared_ptr<vlc_source_lease_t>(vlc_source_lease_Hold(lease),leaseRelease);
        // Taking the one-shot hook breaks these references before native reentry.
        if (!proxy->onCancelled([heldPolicy,heldLease] { vlc_input_policy_Revoke(heldPolicy.get()); vlc_source_lease_Cancel(heldLease.get()); })) { done({}); return; }
        done(AddonNativeSourceResolver::Prepared::adopt(proxy->uri().toLatin1(),policyOwner.release(),leaseOwner.release(),proxy->activeFlag(),
            [proxy](auto current,auto started) -> AddonNativeSourceResolver::Cancel {
                proxy->start(std::move(current),[started=std::move(started)](bool ready) { started(ready ? VLC_SUCCESS : VLC_EGENERIC); });
                return [proxy] { proxy->cancel(); };
            }));
    };
}
}
AddonNativeSourceResolver::Cancel prepareContainedSource(const AddonNativeSourceResolver::Admission& admission,
    AddonNativeSourceResolver::Current current,AddonStreamProxy::Spec spec,AddonNativeSourceResolver::PreparedCallback done)
{
    if (!done) return {};
    return AddonStreamProxy::prepare(authorizedSpec(admission,std::move(current),std::move(spec)),prepareResult(std::move(done)));
}
AddonNativeSourceResolver::Cancel prepareContainedSourceForTesting(const AddonNativeSourceResolver::Admission& admission,
    AddonNativeSourceResolver::Current current,AddonStreamProxy::Spec spec,AddonStreamProxy::Testing testing,AddonNativeSourceResolver::PreparedCallback done)
{
    if (!done) return {};
    return AddonStreamProxy::prepareForTesting(authorizedSpec(admission,std::move(current),std::move(spec)),std::move(testing),prepareResult(std::move(done)));
}
}
