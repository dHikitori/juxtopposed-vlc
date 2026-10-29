#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_artwork_attempt_keeper.hpp"
#include "../../../../src/preparser/metadata_artwork_outcome.h"
#include <utility>
#include <algorithm>
namespace vlc::addons {
NativeArtworkAttemptKeeper::NativeArtworkAttemptKeeper(Parts parts) noexcept
    : m_admission(std::move(parts.admission)), m_watches(std::move(parts.watches)),
      m_outcomes(std::move(parts.outcomes)) {}
std::unique_ptr<NativeArtworkAttemptKeeper> NativeArtworkAttemptKeeper::create(Parts parts) noexcept
{
    const auto admission = parts.admission;
    if (!admission || !admission->current() || parts.watches.empty()
        || parts.watches.size() > MaximumWatches || parts.outcomes.size() > MaximumOutcomes
        || std::any_of(parts.watches.begin(), parts.watches.end(), [](const auto& watch) { return !watch; })
        || std::any_of(parts.outcomes.begin(), parts.outcomes.end(), [](const auto& outcome) { return !outcome; })) {
        if (admission) admission->revoke();
        return {};
    }
    try { return std::unique_ptr<NativeArtworkAttemptKeeper>(new NativeArtworkAttemptKeeper(std::move(parts))); }
    catch (...) { admission->revoke(); return {}; }
}
StoreAuthorityWatch NativeArtworkAttemptKeeper::watch(const StoreAuthorityToken& token,
    const std::shared_ptr<CoreMetadataAdmission>& admission) noexcept
{
    if (!admission || !admission->current()) return {};
    try { return token.onInvalidated([admission] { admission->revoke(); }); }
    catch (...) { admission->revoke(); return {}; }
}
bool NativeArtworkAttemptKeeper::current() const noexcept
{
    return m_admission->current()
        && std::all_of(m_watches.begin(), m_watches.end(), [](const auto& watch) { return bool(watch); });
}
void NativeArtworkAttemptKeeper::revoke() noexcept { m_admission->revoke(); }
void NativeArtworkAttemptKeeper::release(void* opaque) noexcept
{ delete static_cast<NativeArtworkAttemptKeeper*>(opaque); }
int NativeArtworkAttemptKeeper::sealPending(vlc_metadata_job_t* child,
    const std::array<unsigned char, 32>& digest,
    const std::vector<meta_fetcher_outcome_t>& outcomes,
    std::unique_ptr<NativeArtworkAttemptKeeper>& owner) noexcept
{
    if (!owner || !owner->current() || outcomes.empty() || outcomes.size() > VLC_METADATA_ARTWORK_PLAN_LIMIT
        || std::none_of(digest.begin(), digest.end(), [](unsigned char value) { return value != 0; })
        || std::none_of(outcomes.begin(), outcomes.end(), [](auto value) { return value == META_FETCHER_OUTCOME_NO_MATCH; })
        || std::any_of(outcomes.begin(), outcomes.end(), [](auto value) {
            return value != META_FETCHER_OUTCOME_NO_MATCH && value != META_FETCHER_OUTCOME_NOT_APPLICABLE;
        })) {
        if (owner) owner->revoke();
        return VLC_EINVAL;
    }
    vlc_metadata_artwork_exhaustion_summary summary{};
    std::copy(digest.begin(), digest.end(), summary.plan_digest);
    summary.count = unsigned(outcomes.size());
    std::copy(outcomes.begin(), outcomes.end(), summary.outcomes);
    const auto status = vlc_metadata_job_SealArtworkNotFound(child,
        owner->m_admission->nativeHandle(), &summary, owner.get(), release);
    if (status == VLC_SUCCESS) owner.release();
    else owner->revoke();
    return status;
}
int NativeArtworkAttemptKeeper::sealSource(vlc_metadata_job_t* child, input_item_t* approved,
    const char* imageUri, std::unique_ptr<NativeArtworkAttemptKeeper>& owner) noexcept
{
    if (!owner || !owner->current()) {
        if (owner) owner->revoke();
        return VLC_EINVAL;
    }
    const auto status = vlc_metadata_job_SealArtworkSource(child, approved, imageUri,
        owner->m_admission->nativeHandle(), owner.get(), release);
    if (status == VLC_SUCCESS) owner.release();
    else owner->revoke();
    return status;
}

}
