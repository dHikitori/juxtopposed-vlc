// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_artwork_attempt_plan.hpp"
#include "addon_artwork_attempt_keeper.hpp"
#include "addon_native_provider_context.hpp"
#include "addon_native_artwork_assets.hpp"
#include <atomic>
#include <optional>
#include <vector>

namespace vlc::addons {
// Host C++ only. Use and destroy this object on its owner thread. Retire its
// Qt data before app drain. A caller retains the exact child through Complete.
class ArtworkAttemptAggregate final {
public:
    enum class Terminal { Idle, Reserved, NoMatch, NotApplicable, Blocked, Positive };
    enum class BlockReason { None, Absent, Unknown, Binding, Stale, Duplicate,
        Denied, Error, ImageFailure, Unsupported, Incomplete, SealFailure };
    // This value contains only the native gate. A late callback can revoke it
    // after the aggregate and Qt app are gone, without accessing either object.
    class CancellationControl final {
    public:
        CancellationControl() = default;
        void operator()() const noexcept { if (m_admission) m_admission->revoke(); }
        explicit operator bool() const noexcept { return bool(m_admission); }
    private:
        friend class ArtworkAttemptAggregate;
        explicit CancellationControl(std::shared_ptr<CoreMetadataAdmission> value) noexcept : m_admission(std::move(value)) {}
        std::shared_ptr<CoreMetadataAdmission> m_admission;
    };
    // The caller must arm the captured native registry token against this SAME
    // admission before create/dispatch, and include token.current() in plan.live.
    // create arms both exact Store tokens and keeps their native watches.
    static std::unique_ptr<ArtworkAttemptAggregate> create(
        std::shared_ptr<const ArtworkAttemptPlan>, vlc_metadata_job_t* exactPendingChild,
        std::shared_ptr<CoreMetadataAdmission>, QString* error = nullptr);
    ~ArtworkAttemptAggregate();
    ArtworkAttemptAggregate(const ArtworkAttemptAggregate&) = delete;
    ArtworkAttemptAggregate& operator=(const ArtworkAttemptAggregate&) = delete;
    // Reserve before invoking a backend. A completion can arrive inline.
    bool reserve(unsigned ordinal);
    bool acceptPortable(unsigned ordinal, std::optional<AddonMetadataCompletionReceipt>&&);
    // Transport completion only. The runner must pass these SAME result bytes
    // through shared consumer acceptance, then approve the resulting image.
    // The slot remains active during image resolution; failure must call block.
    bool acceptPortablePositive(unsigned ordinal, std::optional<AddonMetadataCompletionReceipt>&&,
                                const QJsonObject& exactResult);
    int trySealPositive(unsigned ordinal, vlc_metadata_job_t* exactPendingChild,
                        input_item_t* approved, const char* imageUri);
    // This method obtains authority from Context itself. No raw enum, JSON
    // result, or caller-constructed NegativeOutcome can enter the ledger.
    bool acceptNativeClaim(unsigned ordinal, AddonNativeProviderContext&, const QString& opaqueClaim);
    bool acceptNativePositive(unsigned ordinal, AddonNativeProviderContext&, const QString& opaqueClaim);
    int trySealNativePositive(unsigned ordinal, vlc_metadata_job_t* exactPendingChild,
        input_item_t* approved, const char* imageUri, const AddonNativeArtworkAssets::Permit&);
    void block(unsigned ordinal, BlockReason);
    // Call only after the existing positive image approval path has succeeded.
    // This grants no image authority; it permanently ends negative admission.
    bool positive(unsigned ordinal);
    int trySeal(vlc_metadata_job_t* exactPendingChild);
    void cancel();
    // Normal sealed/positive cleanup does not revoke the transferred gate.
    // An unfinished aggregate is canceled by retire/destruction.
    void retire();
    CancellationControl cancellationControl() const noexcept { return CancellationControl(m_admission); }
    bool current() const noexcept; // Open, current plan and pending child only.
    bool exhausted() const noexcept;
    bool sealed() const noexcept { return m_sealed; }
    bool negativeForbidden() const noexcept { return m_forbidden.load(std::memory_order_acquire); }
    std::optional<unsigned> nextOrdinal() const noexcept;
    Terminal terminal(unsigned ordinal) const noexcept;
    BlockReason firstBlock() const noexcept { return m_firstBlock; }
private:
    enum class Phase { Open, Positive, Sealed, Retired, Canceled };
    ArtworkAttemptAggregate(std::shared_ptr<const ArtworkAttemptPlan>,
        std::shared_ptr<vlc_metadata_job_t>, std::shared_ptr<CoreMetadataAdmission>,
        std::vector<StoreAuthorityWatch>);
    bool ownerThread() const noexcept;
    bool binding(unsigned ordinal, ArtworkAttemptPlan::Implementation);
    bool fail(unsigned ordinal, BlockReason, bool revoke = false);
    int sealPositiveSource(unsigned ordinal, vlc_metadata_job_t*, input_item_t*, const char*);
    void clearQtData();
    static bool childPending(vlc_metadata_job_t*) noexcept;
    const std::shared_ptr<CoreMetadataAdmission> m_admission;
    const void* const m_thread;
    std::atomic_bool m_forbidden{false};
    std::shared_ptr<const ArtworkAttemptPlan> m_plan;
    std::shared_ptr<vlc_metadata_job_t> m_child;
    std::vector<StoreAuthorityWatch> m_watches;
    std::vector<Terminal> m_terminals;
    std::vector<meta_fetcher_outcome_t> m_results;
    std::vector<std::optional<AddonMetadataCompletionReceipt>> m_receipts;
    std::vector<std::shared_ptr<vlc_metadata_native_outcome_authority_t>> m_nativeAuthorities;
    std::optional<unsigned> m_active;
    unsigned m_next = 0;
    BlockReason m_firstBlock = BlockReason::None;
    Phase m_phase = Phase::Open;
    bool m_sealed = false;
};
}
