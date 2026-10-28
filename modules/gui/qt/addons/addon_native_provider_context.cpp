// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include "addon_native_provider_context.hpp"
#include "../../../../src/preparser/metadata_native_provider.h"
#include "../../../../src/preparser/metadata_artwork_outcome.h"
#include <QPointer>
#include <QUuid>
#include <array>
#include <condition_variable>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct AddonNativeProviderContext::State : std::enable_shared_from_this<State> {
    using Job = std::shared_ptr<vlc_metadata_job_t>;
    struct Pending {
        quint64 id = 0;
        std::string reference;
        Job job;
        Claim claim;
        meta_fetcher_provider_t family = META_FETCHER_PROVIDER_NONE;
        int status = VLC_EGENERIC;
        bool ready = false, delivered = false, canceled = false, outcome = false;
    };
    struct Callback { std::shared_ptr<State> state; std::shared_ptr<Pending> pending; };
    struct Located { std::shared_ptr<Pending> pending; Claim claim; };
    struct Ready {
        std::shared_ptr<Pending> pending;
        int status;
        bool canceled;
    };
    struct Call {
        std::shared_ptr<State> state;
        vlc_metadata_native_provider_t* provider = nullptr;
        explicit operator bool() const { return provider != nullptr; }
        ~Call() {
            if (!provider) return;
            vlc_metadata_native_provider_Release(provider);
            { std::lock_guard<std::mutex> lock(state->mutex); --state->calls; }
            state->changed.notify_all();
        }
        Call(std::shared_ptr<State> state, vlc_metadata_native_provider_t* provider)
            : state(std::move(state)), provider(provider) {}
        Call(const Call&) = delete;
    };
    std::mutex mutex;
    std::condition_variable changed;
    vlc::addons::AddonStreamLifetime::Handle lifetime;
    vlc_metadata_native_provider_t* provider = nullptr;
    std::unordered_map<quint64, std::shared_ptr<Pending>> pending;
    unsigned calls = 0, retireCalls = 0;
    bool initialized = false, closed = false, retirePrepared = false;

    Call begin()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (closed || !provider || !lifetime.current()) return {shared_from_this(), nullptr};
        ++calls; return {shared_from_this(), vlc_metadata_native_provider_Hold(provider)};
    }
    void install(vlc_metadata_native_provider_t* value)
    {
        bool retire = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            provider = value; retire = closed;
        }
        if (retire && value) vlc_metadata_native_provider_Close(value);
        { std::lock_guard<std::mutex> lock(mutex); initialized = true; }
        changed.notify_all();
    }
    static void completed(vlc_metadata_native_claim_t* claim, int status, void* opaque)
    {
        const std::unique_ptr<Callback> callback(static_cast<Callback*>(opaque));
        callback->state->finish(callback->pending, claim, status);
    }
    void finish(const std::shared_ptr<Pending>& value, vlc_metadata_native_claim_t* claim, int status)
    {
        Claim held;
        if (claim) held = {vlc_metadata_native_claim_Hold(claim), vlc_metadata_native_claim_Release};
        std::lock_guard<std::mutex> lock(mutex);
        const auto found = pending.find(value->id);
        if (found == pending.end() || found->second != value || value->ready) return;
        value->status = status; value->ready = true;
        if (!closed && !value->canceled) value->claim = std::move(held);
    }
    void cancel(quint64 id)
    {
        auto call = begin();
        if (!call) return;
        std::shared_ptr<Pending> value;
        Claim retired;
        {
            std::lock_guard<std::mutex> lock(mutex);
            const auto found = pending.find(id);
            if (found == pending.end()) return;
            value = found->second; value->canceled = true; retired = std::move(value->claim);
        }
        vlc_metadata_job_Cancel(value->job.get());
        vlc_metadata_native_provider_Cancel(call.provider, value->job.get());
    }
    void retire()
    {
        vlc_metadata_native_provider_t* context = nullptr;
        std::array<std::shared_ptr<Pending>, 32> values;
        unsigned count = 0;
        bool first;
        {
            std::lock_guard<std::mutex> lock(mutex);
            first = !closed; closed = true; ++retireCalls;
            if (provider) context = vlc_metadata_native_provider_Hold(provider);
            if (first) for (const auto& row : pending) { row.second->canceled = true; values[count++] = row.second; }
        }
        for (unsigned i = 0; i < count; ++i) vlc_metadata_job_Cancel(values[i]->job.get());
        if (context) { vlc_metadata_native_provider_Close(context); vlc_metadata_native_provider_Release(context); }
        for (auto& value : values) value.reset();
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (first) retirePrepared = true;
            --retireCalls;
        }
        changed.notify_all();
    }
    void join()
    {
        vlc_metadata_native_provider_t* context;
        {
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait(lock, [this] { return initialized && retirePrepared && !calls && !retireCalls; });
            context = std::exchange(provider, nullptr);
        }
        if (context) vlc_metadata_native_provider_Delete(context);
        std::unordered_map<quint64, std::shared_ptr<Pending>> retired;
        { std::lock_guard<std::mutex> lock(mutex); retired.swap(pending); }
        // Delete drains native callbacks first. Release every remaining claim
        // and job before the app permits native parent and Qt teardown.
        retired.clear();
    }
    Located findClaim(const std::string& reference, vlc_metadata_job_t* job, meta_fetcher_provider_t family)
    {
        Located found;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (closed || !lifetime.current()) return {};
            for (const auto& row : pending)
                if (row.second->reference == reference && row.second->job.get() == job && row.second->family == family
                    && row.second->ready && !row.second->canceled && row.second->claim) {
                    found = {row.second, row.second->claim}; break;
                }
        }
        return found.claim && vlc_metadata_native_claim_MatchesJob(found.claim.get(), job) ? found : Located{};
    }
};

AddonNativeProviderContext::AddonNativeProviderContext(vlc::addons::AddonStreamLifetime::Handle lifetime,
    vlc_object_t* nativeParent, QObject* parent)
    : QObject(parent), m_state(std::make_shared<State>()), m_poll(this)
{
    m_state->lifetime = lifetime; const auto state = m_state;
    m_admitted = nativeParent && lifetime.startQtWork([state] { state->join(); },
        [state] { state->retire(); }, [this](auto ticket) { m_retirement = std::move(ticket); });
    if (m_admitted) state->install(vlc_metadata_native_provider_New(nativeParent));
    else { state->retire(); state->install(nullptr); }
    m_poll.setInterval(20); connect(&m_poll, &QTimer::timeout, this, [this] { poll(); });
}
AddonNativeProviderContext::~AddonNativeProviderContext() { close(); }

AddonNativeProviderContext::Cancel AddonNativeProviderContext::probe(
    vlc_metadata_job_t* job, meta_fetcher_provider_t family, Completion completion)
{ return probe(job, family, std::move(completion), false); }
AddonNativeProviderContext::Cancel AddonNativeProviderContext::probeOutcome(
    vlc_metadata_job_t* job, meta_fetcher_provider_t family, Completion completion)
{ return probe(job, family, std::move(completion), true); }
AddonNativeProviderContext::Cancel AddonNativeProviderContext::probe(
    vlc_metadata_job_t* job, meta_fetcher_provider_t family, Completion completion, bool outcome)
{
    if (!job || !completion || !available() || m_sequence == UINT64_MAX) return {};
    const auto state = m_state; auto call = state->begin();
    if (!call) return {};
    const auto value = std::make_shared<State::Pending>(); value->id = ++m_sequence;
    value->job = {vlc_metadata_job_Hold(job), vlc_metadata_job_Release}; value->family = family;
    value->outcome = outcome;
    value->reference = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->closed || state->pending.size() >= 32) return {};
        state->pending.emplace(value->id, value);
    }
    m_completions.insert(value->id, std::move(completion)); m_poll.start();
    auto* callback = new State::Callback{state, value};
    const auto status = outcome
        ? vlc_metadata_native_provider_ProbeOutcome(call.provider, job, family, State::completed, callback)
        : vlc_metadata_native_provider_Probe(call.provider, job, family, State::completed, callback);
    if (status != VLC_SUCCESS) { delete callback; state->finish(value, nullptr, status); }
    const std::weak_ptr<State> weak(state); const auto id = value->id;
    return [weak, id] { if (const auto state = weak.lock()) state->cancel(id); };
}

bool AddonNativeProviderContext::available() const
{
    const auto state = m_state; std::lock_guard<std::mutex> lock(state->mutex);
    return m_admitted && !state->closed && state->provider && state->lifetime.current();
}
bool AddonNativeProviderContext::validates(const QString& reference, vlc_metadata_job_t* job, meta_fetcher_provider_t family) const
{
    if (reference.isEmpty() || reference.size() > 128 || !job) return false;
    const auto state = m_state; auto call = state->begin();
    const auto claim = call ? state->findClaim(reference.toStdString(), job, family).claim : Claim{};
    const auto uri = claim ? vlc_metadata_native_claim_GetArtworkURI(claim.get()) : nullptr;
    return claim && vlc_metadata_native_claim_GetOutcome(claim.get()) == META_FETCHER_OUTCOME_MATCH && uri && uri[0];
}
AddonNativeProviderContext::Claim AddonNativeProviderContext::takeClaim(
    const QString& reference, vlc_metadata_job_t* job, meta_fetcher_provider_t family, bool outcome)
{
    if (reference.isEmpty() || reference.size() > 128 || !job) return {};
    const auto state = m_state;
    auto call = state->begin(); if (!call) return {};
    const auto found = state->findClaim(reference.toStdString(), job, family);
    const auto value = found.pending;
    if (!value) return {};
    const auto kind = vlc_metadata_native_claim_GetOutcome(found.claim.get());
    const auto uri = vlc_metadata_native_claim_GetArtworkURI(found.claim.get());
    if (outcome ? !value->outcome || (kind != META_FETCHER_OUTCOME_NO_MATCH && kind != META_FETCHER_OUTCOME_NOT_APPLICABLE)
                : kind != META_FETCHER_OUTCOME_MATCH || !uri || !uri[0]) return {};
    Claim claim;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        const auto row = state->pending.find(value->id);
        if (state->closed || row == state->pending.end() || row->second != value || value->canceled
            || value->claim != found.claim) return {};
        claim = std::move(value->claim); state->pending.erase(row);
    }
    return claim;
}
AddonNativeProviderContext::NegativeOutcome AddonNativeProviderContext::takeOutcome(
    const QString& reference, vlc_metadata_job_t* job, meta_fetcher_provider_t family,
    vlc_metadata_artwork_admission_t* admission)
{
    // The claim is off-map after takeClaim. Keep the entire conversion counted
    // until that claim (and its child/backend holds) has been released.
    const auto state = m_state; auto call = state->begin();
    if (!call || !vlc_metadata_artwork_admission_IsCurrent(admission)) {
        vlc_metadata_artwork_admission_Revoke(admission); return {};
    }
    const auto claim = takeClaim(reference, job, family, true);
    if (!claim) { vlc_metadata_artwork_admission_Revoke(admission); return {}; }
    const auto value = vlc_metadata_native_claim_GetOutcome(claim.get());
    const auto authority = std::shared_ptr<vlc_metadata_native_outcome_authority_t>(
        vlc_metadata_native_claim_AdmitOutcome(claim.get(), job, family, admission),
        vlc_metadata_native_outcome_authority_Release);
    bool current;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        current = !state->closed && state->lifetime.current();
    }
    if (!authority || !current) { vlc_metadata_artwork_admission_Revoke(admission); return {}; }
    return {value, authority};
}
void AddonNativeProviderContext::poll()
{
    const auto state = m_state; auto call = state->begin();
    if (!call) { close(); return; }
    // Keep local mailbox references inside the counted native call. Owner
    // deletion during delivery cannot release the app join before this stack.
    std::vector<State::Ready> ready;
    std::vector<std::shared_ptr<State::Pending>> retired;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        for (const auto& row : state->pending)
            if (row.second->ready && !row.second->delivered) {
                row.second->delivered = true; ready.push_back({row.second, row.second->status, row.second->canceled});
            }
    }
    const QPointer<AddonNativeProviderContext> alive(this);
    for (const auto& resultReady : ready) {
        const auto& value = resultReady.pending;
        auto completion = m_completions.take(value->id);
        const bool valid = value->outcome
            ? bool(state->findClaim(value->reference, value->job.get(), value->family).claim)
            : validates(QString::fromStdString(value->reference), value->job.get(), value->family);
        Result result{valid ? QString::fromStdString(value->reference) : QString{},
            valid ? VLC_SUCCESS : resultReady.status == VLC_SUCCESS ? -EINTR : resultReady.status};
        if (completion && !resultReady.canceled) completion(result);
        // A final capture can destroy this owner. End that capture before the
        // guard and before the next mailbox or timer access.
        completion = {};
        if (!alive) return;
    }
    bool stop;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        for (auto it = state->pending.begin(); it != state->pending.end();) {
            const auto& value = it->second;
            if (value->ready && value->delivered && (!value->claim || value->canceled || !vlc_metadata_job_IsPending(value->job.get()))) {
                retired.push_back(value); it = state->pending.erase(it);
            } else ++it;
        }
        stop = state->closed || state->pending.empty();
    }
    if (stop) m_poll.stop();
}
void AddonNativeProviderContext::close()
{
    auto retired = std::move(m_completions); m_completions.clear(); m_poll.stop();
    const auto state = m_state; state->retire();
}
