// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "addon_artwork_attempt_aggregate.hpp"
#include "addon_metadata_family_plan.hpp"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QThread>
#include <algorithm>
#include <array>
#include <cstring>
#include <vlc_metadata_backend.h>
#include <vlc_threads.h>

namespace vlc::addons {
namespace {
using Plan = ArtworkAttemptPlan;
meta_fetcher_provider_t nativeFamily(const Plan::Slot& slot)
{
    const auto& bindings = AddonMetadataFamilyPlan::bindings();
    const auto found = std::find_if(bindings.cbegin(), bindings.cend(), [&](const auto& binding) {
        return binding.nativeContribution == slot.contribution && binding.family == slot.family;
    });
    if (found == bindings.cend()) return META_FETCHER_PROVIDER_NONE;
    if (slot.family == QLatin1String("musicbrainz")) return META_FETCHER_PROVIDER_MUSICBRAINZ;
    if (slot.family == QLatin1String("google-images")) return META_FETCHER_PROVIDER_GOOGLE_IMAGES;
    if (slot.family == QLatin1String("french-tv")) return META_FETCHER_PROVIDER_FRENCH_TV;
    if (slot.family == QLatin1String("lastfm")) return META_FETCHER_PROVIDER_LASTFM;
    return META_FETCHER_PROVIDER_NONE;
}
bool stamp(const QByteArray& value)
{ return value.size() == 32 && value != QByteArray(32, '\0'); }
}
ArtworkAttemptAggregate::ArtworkAttemptAggregate(std::shared_ptr<const Plan> plan,
    std::shared_ptr<vlc_metadata_job_t> child, std::shared_ptr<CoreMetadataAdmission> admission,
    std::vector<StoreAuthorityWatch> watches)
    : m_admission(std::move(admission)), m_thread(QThread::currentThreadId()),
      m_forbidden(plan->hasBlockedSlot()), m_plan(std::move(plan)), m_child(std::move(child)),
      m_watches(std::move(watches)), m_terminals(m_plan->attempts.size(), Terminal::Idle),
      m_results(m_plan->attempts.size(), META_FETCHER_OUTCOME_UNKNOWN), m_receipts(m_plan->attempts.size()),
      m_nativeAuthorities(m_plan->attempts.size()) {}
bool ArtworkAttemptAggregate::childPending(vlc_metadata_job_t* child) noexcept
{
    if (!child || !vlc_metadata_job_IsPending(child)) return false;
    const auto* request = vlc_metadata_job_GetRequest(child);
    return request && request->feature && request->operation
        && !std::strcmp(request->feature, "artwork") && !std::strcmp(request->operation, "enrich")
        && (request->flags & VLC_METADATA_REQUEST_NETWORK) && request->field_count == 1
        && request->fields && request->fields[0] && !std::strcmp(request->fields[0], "artwork");
}
std::unique_ptr<ArtworkAttemptAggregate> ArtworkAttemptAggregate::create(
    std::shared_ptr<const Plan> plan, vlc_metadata_job_t* child,
    std::shared_ptr<CoreMetadataAdmission> admission, QString* error)
{
    const auto reject = [&]() -> std::unique_ptr<ArtworkAttemptAggregate> {
        if (admission) admission->revoke();
        if (error) *error = QStringLiteral("The artwork attempt plan is not available.");
        return {};
    };
    if (!plan || !admission || !admission->current() || !plan->current() || !childPending(child)
        || !stamp(plan->subject) || !stamp(plan->requestDigest) || !stamp(plan->rawCatalogueDigest)
        || !stamp(plan->planDigest) || plan->attempts.isEmpty() || plan->attempts.size() > Plan::MaximumSlots
        || plan->deadline <= vlc_tick_now()) return reject();
    for (qsizetype i = 0; i < plan->attempts.size(); ++i) {
        const auto& slot = plan->attempts.at(i);
        const auto projected = QCryptographicHash::hash(QJsonDocument(slot.projectedContext).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256);
        if (slot.ordinal != unsigned(i) || slot.feature != QLatin1String("artwork")
            || slot.operation != QLatin1String("enrich") || slot.requestedFields != QStringList{QStringLiteral("artwork")}
            || !stamp(slot.projectedContextDigest) || slot.projectedContextDigest != projected
            || (!slot.blocked && slot.implementation == Plan::Implementation::Missing)
            || (!slot.blocked && slot.implementation == Plan::Implementation::Native && nativeFamily(slot) == META_FETCHER_PROVIDER_NONE))
            return reject();
    }
    try {
        std::vector<StoreAuthorityWatch> watches;
        watches.reserve(2);
        watches.push_back(NativeArtworkAttemptKeeper::watch(plan->queuedAuthority, admission));
        watches.push_back(NativeArtworkAttemptKeeper::watch(plan->authority, admission));
        if (!watches[0] || !watches[1] || !plan->current() || !admission->current() || !childPending(child)) return reject();
        auto held = std::shared_ptr<vlc_metadata_job_t>(vlc_metadata_job_Hold(child), vlc_metadata_job_Release);
        auto result = std::unique_ptr<ArtworkAttemptAggregate>(new ArtworkAttemptAggregate(
            std::move(plan), std::move(held), admission, std::move(watches)));
        if (error) error->clear();
        return result;
    } catch (...) { return reject(); }
}
ArtworkAttemptAggregate::~ArtworkAttemptAggregate() { retire(); }
bool ArtworkAttemptAggregate::ownerThread() const noexcept { return QThread::currentThreadId() == m_thread; }
bool ArtworkAttemptAggregate::current() const noexcept
{
    return ownerThread() && m_phase == Phase::Open && m_admission->current() && m_plan
        && m_plan->current() && childPending(m_child.get());
}
bool ArtworkAttemptAggregate::exhausted() const noexcept
{ return ownerThread() && !m_active && m_next == m_terminals.size(); }
std::optional<unsigned> ArtworkAttemptAggregate::nextOrdinal() const noexcept
{
    if (!ownerThread() || m_phase != Phase::Open || m_active || m_next >= m_terminals.size()) return {};
    return m_next;
}
ArtworkAttemptAggregate::Terminal ArtworkAttemptAggregate::terminal(unsigned ordinal) const noexcept
{
    return ownerThread() && ordinal < m_terminals.size() ? m_terminals[ordinal] : Terminal::Blocked;
}
bool ArtworkAttemptAggregate::fail(unsigned ordinal, BlockReason reason, bool revoke)
{
    m_forbidden.store(true, std::memory_order_release);
    if (!ownerThread()) { m_admission->revoke(); return false; }
    if (m_firstBlock == BlockReason::None) m_firstBlock = reason;
    if (revoke || m_sealed) m_admission->revoke();
    if (m_phase == Phase::Open && m_active && *m_active == ordinal && ordinal < m_terminals.size()
        && m_terminals[ordinal] == Terminal::Reserved) {
        m_terminals[ordinal] = Terminal::Blocked;
        m_active.reset(); ++m_next;
    }
    return false;
}
bool ArtworkAttemptAggregate::reserve(unsigned ordinal)
{
    if (!ownerThread()) return fail(ordinal, BlockReason::Binding, true);
    if (m_phase != Phase::Open) return fail(ordinal, BlockReason::Duplicate);
    if (!current()) return fail(ordinal, BlockReason::Stale, true);
    if (ordinal >= m_terminals.size() || ordinal != m_next || m_active
        || m_terminals[ordinal] != Terminal::Idle) return fail(ordinal, BlockReason::Binding);
    m_active = ordinal; m_terminals[ordinal] = Terminal::Reserved;
    return true;
}
bool ArtworkAttemptAggregate::binding(unsigned ordinal, Plan::Implementation implementation)
{
    if (!ownerThread()) return fail(ordinal, BlockReason::Binding, true);
    if (m_phase != Phase::Open) return fail(ordinal, BlockReason::Duplicate);
    if (!current()) return fail(ordinal, BlockReason::Stale, true);
    if (ordinal >= m_terminals.size() || !m_active || *m_active != ordinal
        || m_terminals[ordinal] != Terminal::Reserved) return fail(ordinal, BlockReason::Duplicate);
    const auto& slot = m_plan->attempts.at(ordinal);
    if (slot.blocked || slot.implementation != implementation) return fail(ordinal, BlockReason::Binding);
    return true;
}
bool ArtworkAttemptAggregate::acceptPortable(unsigned ordinal, std::optional<AddonMetadataCompletionReceipt>&& receipt)
{
    if (!binding(ordinal, Plan::Implementation::Portable)) return false;
    if (m_receipts[ordinal]) return fail(ordinal, BlockReason::Duplicate);
    if (!receipt) return fail(ordinal, BlockReason::Absent);
    const auto& slot = m_plan->attempts.at(ordinal);
    if (receipt->outcome() != AddonMetadataCompletionReceipt::Outcome::NoMatch || !receipt->current()
        || !receipt->matches(m_plan->subject, ordinal, slot.contribution, slot.projectedContextDigest, slot.partition)
        || receipt->nativeAdmission().get() != m_admission.get()) return fail(ordinal, BlockReason::Binding);
    m_receipts[ordinal].emplace(std::move(*receipt));
    m_results[ordinal] = META_FETCHER_OUTCOME_NO_MATCH;
    m_terminals[ordinal] = Terminal::NoMatch;
    m_active.reset(); ++m_next;
    return true;
}
bool ArtworkAttemptAggregate::acceptPortablePositive(unsigned ordinal,
    std::optional<AddonMetadataCompletionReceipt>&& receipt, const QJsonObject& data)
{
    if (!binding(ordinal, Plan::Implementation::Portable)) return false;
    if (m_receipts[ordinal]) return fail(ordinal, BlockReason::Duplicate);
    if (!receipt) return fail(ordinal, BlockReason::Absent);
    const auto& slot = m_plan->attempts.at(ordinal);
    using Outcome = AddonMetadataCompletionReceipt::Outcome;
    if ((receipt->outcome() != Outcome::Match && receipt->outcome() != Outcome::Partial)
        || !receipt->current() || !receipt->matchesResult(data)
        || !receipt->matches(m_plan->subject, ordinal, slot.contribution, slot.projectedContextDigest, slot.partition)
        || receipt->nativeAdmission().get() != m_admission.get()) return fail(ordinal, BlockReason::Binding);
    m_receipts[ordinal].emplace(std::move(*receipt));
    m_forbidden.store(true, std::memory_order_release);
    return true;
}
int ArtworkAttemptAggregate::trySealPositive(unsigned ordinal, vlc_metadata_job_t* child,
    input_item_t* approved, const char* imageUri)
{
    if (!binding(ordinal, Plan::Implementation::Portable)) return VLC_EGENERIC;
    if (child != m_child.get()) { fail(ordinal, BlockReason::Binding, true); return VLC_EGENERIC; }
    const auto& slot = m_plan->attempts.at(ordinal);
    const auto& receipt = m_receipts[ordinal];
    using Outcome = AddonMetadataCompletionReceipt::Outcome;
    if (!receipt || (receipt->outcome() != Outcome::Match && receipt->outcome() != Outcome::Partial)
        || !receipt->current()
        || !receipt->matches(m_plan->subject, ordinal, slot.contribution, slot.projectedContextDigest, slot.partition)
        || receipt->nativeAdmission().get() != m_admission.get()) {
        fail(ordinal, BlockReason::Binding, true); return VLC_EGENERIC;
    }
    try {
        NativeArtworkAttemptKeeper::Parts parts; parts.admission = m_admission;
        for (auto& authority : m_nativeAuthorities) if (authority) parts.outcomes.push_back(std::move(authority));
        parts.watches = std::move(m_watches);
        auto keeper = NativeArtworkAttemptKeeper::create(std::move(parts));
        if (!keeper) { fail(ordinal, BlockReason::SealFailure, true); return VLC_EGENERIC; }
        const auto status = NativeArtworkAttemptKeeper::sealSource(child, approved, imageUri, keeper);
        if (status != VLC_SUCCESS) { fail(ordinal, BlockReason::SealFailure, true); return status; }
        m_forbidden.store(true, std::memory_order_release);
        m_terminals[ordinal] = Terminal::Positive; ++m_next;
        m_sealed = true; m_phase = Phase::Sealed; clearQtData();
        return VLC_SUCCESS;
    } catch (...) { fail(ordinal, BlockReason::SealFailure, true); return VLC_ENOMEM; }
}

bool ArtworkAttemptAggregate::acceptNativeClaim(unsigned ordinal, AddonNativeProviderContext& context, const QString& claim)
{
    if (!binding(ordinal, Plan::Implementation::Native)) return false;
    const auto& slot = m_plan->attempts.at(ordinal);
    const auto family = nativeFamily(slot);
    if (claim.isEmpty() || family == META_FETCHER_PROVIDER_NONE) return fail(ordinal, BlockReason::Absent);
    auto result = context.takeOutcome(claim, m_child.get(), family, m_admission->nativeHandle());
    if (!result || (result.value != META_FETCHER_OUTCOME_NO_MATCH && result.value != META_FETCHER_OUTCOME_NOT_APPLICABLE)
        || !current()) return fail(ordinal, BlockReason::Binding);
    m_nativeAuthorities[ordinal] = std::move(result.authority);
    m_results[ordinal] = result.value;
    m_terminals[ordinal] = result.value == META_FETCHER_OUTCOME_NO_MATCH ? Terminal::NoMatch : Terminal::NotApplicable;
    m_active.reset(); ++m_next;
    return true;
}
void ArtworkAttemptAggregate::block(unsigned ordinal, BlockReason reason) { fail(ordinal, reason); }
bool ArtworkAttemptAggregate::positive(unsigned ordinal)
{
    if (!ownerThread()) return fail(ordinal, BlockReason::Binding, true);
    if (!current()) return fail(ordinal, BlockReason::Stale, true);
    if (!m_active || *m_active != ordinal || ordinal >= m_terminals.size()
        || m_terminals[ordinal] != Terminal::Reserved || m_plan->attempts.at(ordinal).blocked)
        return fail(ordinal, BlockReason::Binding);
    m_forbidden.store(true, std::memory_order_release);
    m_terminals[ordinal] = Terminal::Positive; m_active.reset(); ++m_next; m_phase = Phase::Positive;
    clearQtData();
    return true;
}
int ArtworkAttemptAggregate::trySeal(vlc_metadata_job_t* child)
{
    if (!ownerThread()) { fail(0, BlockReason::Binding, true); return VLC_EGENERIC; }
    if (m_phase != Phase::Open) { fail(0, BlockReason::Duplicate); return VLC_EGENERIC; }
    if (child != m_child.get() || !current()) { fail(0, BlockReason::Stale, true); return VLC_EGENERIC; }
    if (negativeForbidden() || !exhausted()
        || std::none_of(m_results.cbegin(), m_results.cend(), [](auto value) { return value == META_FETCHER_OUTCOME_NO_MATCH; })) {
        fail(m_active.value_or(m_next), BlockReason::Incomplete); return VLC_EGENERIC;
    }
    for (unsigned i = 0; i < m_terminals.size(); ++i) {
        const auto& slot = m_plan->attempts.at(i);
        if ((m_terminals[i] != Terminal::NoMatch && m_terminals[i] != Terminal::NotApplicable)
            || (m_terminals[i] == Terminal::NotApplicable && slot.implementation != Plan::Implementation::Native)) {
            fail(i, BlockReason::Incomplete); return VLC_EGENERIC;
        }
        if (slot.implementation == Plan::Implementation::Portable) {
            const auto& receipt = m_receipts[i];
            if (!receipt || !receipt->current() || receipt->outcome() != AddonMetadataCompletionReceipt::Outcome::NoMatch
                || !receipt->matches(m_plan->subject, i, slot.contribution, slot.projectedContextDigest, slot.partition)
                || receipt->nativeAdmission().get() != m_admission.get()) {
                fail(i, BlockReason::Binding, true); return VLC_EGENERIC;
            }
        } else if (!m_nativeAuthorities[i]) { fail(i, BlockReason::Absent, true); return VLC_EGENERIC; }
    }
    try {
        NativeArtworkAttemptKeeper::Parts parts; parts.admission = m_admission;
        parts.outcomes.reserve(m_nativeAuthorities.size());
        for (auto& authority : m_nativeAuthorities) if (authority) parts.outcomes.push_back(std::move(authority));
        parts.watches = std::move(m_watches);
        auto keeper = NativeArtworkAttemptKeeper::create(std::move(parts));
        if (!keeper) { fail(0, BlockReason::SealFailure, true); return VLC_EGENERIC; }
        std::array<unsigned char, 32> planDigest;
        std::copy(m_plan->planDigest.cbegin(), m_plan->planDigest.cend(), planDigest.begin());
        const auto status = NativeArtworkAttemptKeeper::sealPending(child, planDigest, m_results, keeper);
        if (status != VLC_SUCCESS) { fail(0, BlockReason::SealFailure, true); return status; }
        m_sealed = true; m_phase = Phase::Sealed;
        clearQtData();
        return VLC_SUCCESS;
    } catch (...) { fail(0, BlockReason::SealFailure, true); return VLC_ENOMEM; }
}
void ArtworkAttemptAggregate::clearQtData()
{
    m_active.reset();
    auto plan = std::move(m_plan); auto child = std::move(m_child);
    auto receipts = std::move(m_receipts); auto authorities = std::move(m_nativeAuthorities);
    auto watches = std::move(m_watches);
}
void ArtworkAttemptAggregate::cancel()
{
    m_forbidden.store(true, std::memory_order_release); m_admission->revoke();
    if (!ownerThread()) return;
    m_phase = Phase::Canceled; clearQtData();
}
void ArtworkAttemptAggregate::retire()
{
    if (!ownerThread()) { m_admission->revoke(); return; }
    if (m_phase == Phase::Open) { cancel(); return; }
    m_phase = Phase::Retired; clearQtData();
}
}
