// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_metadata_registry_epoch.hpp"
#include "addon_metadata_attempt.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>
#include <utility>

namespace vlc::addons {
struct MetadataRegistryEpochState {
    explicit MetadataRegistryEpochState(std::uint64_t value) : registry(value), closed(value == 0) {}
    const std::uint64_t registry;
    std::atomic<std::uint64_t> generation{1};
    std::atomic<unsigned> depth{0};
    std::atomic_bool closed;
    std::mutex mutex;
    std::condition_variable ended;
    struct Armed {
        std::weak_ptr<AddonMetadataNativeAdmission> gate;
        const AddonMetadataNativeAdmission* identity = nullptr;
    };
    std::array<Armed, MetadataRegistryEpoch::ArmedLimit> armed;
    std::uint64_t batches = 0;
};
namespace {
using State = MetadataRegistryEpochState;
using Gates = std::array<std::shared_ptr<AddonMetadataNativeAdmission>, MetadataRegistryEpoch::ArmedLimit>;
std::uint64_t nextRegistry() noexcept
{
    static std::atomic<std::uint64_t> next{1};
    auto value = next.load();
    for (;;) {
        if (value == std::numeric_limits<std::uint64_t>::max()) return 0;
        if (next.compare_exchange_weak(value, value + 1)) return value;
    }
}
void advanceLocked(State& state) noexcept
{
    const auto value = state.generation.load();
    if (value == std::numeric_limits<std::uint64_t>::max()) state.closed.store(true);
    else state.generation.store(value + 1);
}
void beginBatchLocked(State& state, std::unique_lock<std::mutex>& lock) noexcept
{
    // No wrap can let an untracked revoker escape a close barrier.
    if (state.batches == std::numeric_limits<std::uint64_t>::max()) state.closed.store(true);
    state.ended.wait(lock, [&] { return state.batches != std::numeric_limits<std::uint64_t>::max(); });
    ++state.batches;
}
void finishBatch(const std::shared_ptr<State>& state) noexcept
{
    std::unique_lock<std::mutex> lock(state->mutex);
    --state->batches;
    state->ended.notify_all();
    state->ended.wait(lock, [&] { return state->batches == 0; });
}
void revokeBatch(const std::shared_ptr<State>& state, Gates& gates) noexcept
{
    // No registry lock covers native current/revoke or last-reference release.
    for (auto& gate : gates) if (gate) { gate->revoke(); gate.reset(); }
    finishBatch(state);
}
void takeGatesLocked(State& state, Gates& gates) noexcept
{
    unsigned count = 0;
    for (auto& slot : state.armed) {
        if (auto gate = slot.gate.lock()) gates[count++] = std::move(gate);
        slot.gate.reset(); slot.identity = nullptr;
    }
}
bool sameGate(const State::Armed& slot, const std::shared_ptr<AddonMetadataNativeAdmission>& gate) noexcept
{
    return slot.identity == gate.get() && !slot.gate.owner_before(gate) && !gate.owner_before(slot.gate);
}
void encode(std::array<unsigned char, 32>& output, unsigned offset, std::uint64_t value) noexcept
{
    for (unsigned i = 0; i != 8; ++i) output[offset + i] = static_cast<unsigned char>(value >> (56 - 8 * i));
}
}
bool MetadataRegistryToken::current() const noexcept
{
    return m_state && !m_state->closed.load() && !m_state->depth.load()
        && m_state->generation.load() == m_generation
        && !m_state->closed.load() && !m_state->depth.load()
        && m_state->generation.load() == m_generation;
}
std::array<unsigned char, 32> MetadataRegistryToken::identity() const noexcept
{
    std::array<unsigned char, 32> value{};
    if (!m_state) return value;
    constexpr std::array<unsigned char, 16> domain{'h','a','k','o','n','e','-','r','e','g','i','s','t','r','y',1};
    for (unsigned i = 0; i != domain.size(); ++i) value[i] = domain[i];
    encode(value, 16, m_state->registry); encode(value, 24, m_generation);
    return value;
}
MetadataRegistryEpoch::MetadataRegistryEpoch() : m_state(std::make_shared<State>(nextRegistry())) {}
MetadataRegistryEpoch::~MetadataRegistryEpoch() { close(); }
MetadataRegistryToken MetadataRegistryEpoch::capture() const noexcept
{
    MetadataRegistryToken token;
    if (m_state->closed.load() || m_state->depth.load()) return token;
    const auto generation = m_state->generation.load();
    if (m_state->closed.load() || m_state->depth.load() || m_state->generation.load() != generation) return token;
    token.m_state = m_state; token.m_generation = generation;
    return token;
}
bool MetadataRegistryEpoch::arm(const MetadataRegistryToken& token,
    std::shared_ptr<AddonMetadataNativeAdmission> gate) const noexcept
{
    if (!gate) return false;
    const bool live = gate->current();
    bool admitted = false;
    Gates refused;
    {
        std::unique_lock<std::mutex> lock(m_state->mutex);
        if (live && token.m_state == m_state && token.current()) {
            State::Armed* empty = nullptr;
            for (auto& slot : m_state->armed) {
                if (!slot.gate.expired()) {
                    if (sameGate(slot, gate)) { admitted = true; break; }
                } else if (!empty) empty = &slot;
            }
            if (!admitted && empty) { empty->gate = gate; empty->identity = gate.get(); admitted = true; }
        }
        if (!admitted) {
            beginBatchLocked(*m_state, lock);
            refused[0].swap(gate);
        }
    }
    if (!admitted) { revokeBatch(m_state, refused); return false; }
    // A mutation can take this registration immediately after the locked arm.
    // Join its gate here too if it won before dispatch admission returns.
    if (!token.current() || !gate->current()) {
        {
            std::unique_lock<std::mutex> lock(m_state->mutex);
            for (auto& slot : m_state->armed)
                if (sameGate(slot, gate)) { slot.gate.reset(); slot.identity = nullptr; }
            beginBatchLocked(*m_state, lock);
            refused[0].swap(gate);
        }
        revokeBatch(m_state, refused); return false;
    }
    return true;
}
MetadataRegistryMutation::MetadataRegistryMutation(std::shared_ptr<State> state, bool counted) noexcept
    : m_state(std::move(state)), m_counted(counted) {}
MetadataRegistryMutation::MetadataRegistryMutation(MetadataRegistryMutation&& other) noexcept
    : m_state(std::move(other.m_state)), m_counted(std::exchange(other.m_counted, false)) {}
MetadataRegistryMutation::~MetadataRegistryMutation()
{
    if (!m_state || !m_counted) return;
    std::lock_guard<std::mutex> lock(m_state->mutex);
    advanceLocked(*m_state);
    m_state->depth.fetch_sub(1);
}
MetadataRegistryMutation::operator bool() const noexcept
{ return m_state && m_counted && !m_state->closed.load(); }
MetadataRegistryMutation MetadataRegistryEpoch::mutation() const noexcept
{
    Gates gates;
    bool counted = false;
    {
        std::unique_lock<std::mutex> lock(m_state->mutex);
        const auto depth = m_state->depth.load();
        if (depth == std::numeric_limits<unsigned>::max()) m_state->closed.store(true);
        else if (!m_state->closed.load()) { m_state->depth.store(depth + 1); counted = true; }
        advanceLocked(*m_state);
        beginBatchLocked(*m_state, lock);
        takeGatesLocked(*m_state, gates);
    }
    revokeBatch(m_state, gates);
    return MetadataRegistryMutation(m_state, counted);
}
void MetadataRegistryEpoch::close() const noexcept
{
    Gates gates;
    {
        std::unique_lock<std::mutex> lock(m_state->mutex);
        m_state->closed.store(true);
        advanceLocked(*m_state);
        beginBatchLocked(*m_state, lock);
        takeGatesLocked(*m_state, gates);
    }
    revokeBatch(m_state, gates);
}
}
