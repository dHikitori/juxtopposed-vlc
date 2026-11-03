// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_core_metadata_admission.hpp"
#include "addon_store_authority.hpp"
#include <array>
#include <memory>
#include <vector>
#include <vlc_meta_fetcher.h>
typedef struct vlc_metadata_job vlc_metadata_job_t;
typedef struct vlc_metadata_native_outcome_authority vlc_metadata_native_outcome_authority_t;
namespace vlc::addons {
// Native/std-only final sideband payload. Its destructor can run on a native
// Core thread after Qt app drain. The Qt module's existing cannot_unload pin
// keeps the release thunk loaded; the native Core parent remains held by its
// normal pipeline/job lifecycle. No app Handle, job/claim, receipt, Qt container,
// full current()/credential reader, or QObject is permitted in Parts.
class NativeArtworkAttemptKeeper final {
public:
    struct Parts {
        std::shared_ptr<CoreMetadataAdmission> admission;
        std::vector<StoreAuthorityWatch> watches;
        std::vector<std::shared_ptr<vlc_metadata_native_outcome_authority_t>> outcomes;
    };
    static constexpr std::size_t MaximumWatches = 32;
    static constexpr std::size_t MaximumOutcomes = 16;
    static std::unique_ptr<NativeArtworkAttemptKeeper> create(Parts) noexcept;
    // Register BEFORE dispatch. A stale token invokes revoke inline and
    // refuses. Capture only this native admission in the watch callback.
    static StoreAuthorityWatch watch(const StoreAuthorityToken&,
                                     const std::shared_ptr<CoreMetadataAdmission>&) noexcept;
    bool current() const noexcept;
    void revoke() noexcept;
    // Only the typed aggregate may call this after verifying every immutable
    // slot/receipt. Ownership transfers to Core only on success; no Complete or
    // Apply occurs here. Failure revokes admission and leaves owner intact.
    static int sealPending(vlc_metadata_job_t* exactPendingChild,
        const std::array<unsigned char, 32>& planDigest,
        const std::vector<meta_fetcher_outcome_t>& outcomes,
        std::unique_ptr<NativeArtworkAttemptKeeper>& owner) noexcept;
    // Bind an image that already passed consumer and native image approval.
    // This grants no image approval. Failure revokes and keeps owner intact.
    static int sealSource(vlc_metadata_job_t* exactPendingChild, input_item_t* approved,
        const char* imageUri, std::unique_ptr<NativeArtworkAttemptKeeper>& owner) noexcept;
private:
    explicit NativeArtworkAttemptKeeper(Parts) noexcept;
    static void release(void*) noexcept;
    // Admission/64-plan charge is released last, after watches and authorities.
    const std::shared_ptr<CoreMetadataAdmission> m_admission;
    const std::vector<StoreAuthorityWatch> m_watches;
    const std::vector<std::shared_ptr<vlc_metadata_native_outcome_authority_t>> m_outcomes;
};
}
