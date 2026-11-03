// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include "addon_native_provider_artwork_assets.hpp"
#include "../../../../src/preparser/metadata_native_provider.h"
#include <vlc_artwork_policy.h>

AddonNativeProviderArtworkAssets::AddonNativeProviderArtworkAssets(AddonNativeArtworkAssets* store,
    AddonNativeProviderContext* context, QObject* parent)
    : QObject(parent), m_store(store), m_context(context) {}
bool AddonNativeProviderArtworkAssets::available() const
{ return m_store && m_store->available() && m_context && m_context->available(); }
AddonNativeProviderArtworkAssets::Cancel AddonNativeProviderArtworkAssets::resolve(
    const QString& reference, vlc_metadata_job_t* job, meta_fetcher_provider_t family, Completion completed)
{
    if (!available() || !completed || !job) return {};
    const auto held = m_context->takeClaim(reference, job, family);
    if (!held) return {};
    const QPointer<AddonNativeProviderArtworkAssets> guard(this);
    return m_store->resolveProviderClaim(held.get(), job, [guard, held, job, completed=std::move(completed)](const auto& result) {
        if (!guard) return;
        if (!result.permit.current() || !vlc_metadata_native_claim_MatchesJob(held.get(), job)) {
            completed({{}, QStringLiteral("The native artwork claim is no longer current."), {}}); return;
        }
        completed({result.file, result.error, result.permit});
    });
}
