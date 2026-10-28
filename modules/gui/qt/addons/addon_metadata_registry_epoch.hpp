// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <memory>

namespace vlc::addons {
class AddonMetadataNativeAdmission;
struct MetadataRegistryEpochState;

// A token owns only native/std registry state. It has no QObject, Qt value,
// callback, reader, provider registration or strong native admission reference.
class MetadataRegistryToken final {
public:
    MetadataRegistryToken() = default;
    bool current() const noexcept; // atomic reads only
    std::array<unsigned char, 32> identity() const noexcept;
private:
    friend class MetadataRegistryEpoch;
    std::shared_ptr<MetadataRegistryEpochState> m_state;
    std::uint64_t m_generation = 0;
};

class MetadataRegistryMutation final {
public:
    MetadataRegistryMutation() = default;
    MetadataRegistryMutation(MetadataRegistryMutation&&) noexcept;
    MetadataRegistryMutation(const MetadataRegistryMutation&) = delete;
    MetadataRegistryMutation& operator=(const MetadataRegistryMutation&) = delete;
    ~MetadataRegistryMutation();
    // Refuse publication when permanent close or counter exhaustion has won.
    explicit operator bool() const noexcept;
private:
    friend class MetadataRegistryEpoch;
    MetadataRegistryMutation(std::shared_ptr<MetadataRegistryEpochState>, bool) noexcept;
    std::shared_ptr<MetadataRegistryEpochState> m_state;
    bool m_counted = false;
};

// Owner data stays outside this helper. Begin a mutation before changing a
// registration or lifetime, keep its scope through publication, and check the
// returned scope before publishing. Nested owner changes keep capture closed
// until the outer scope ends. This helper does not lock the registration map.
class MetadataRegistryEpoch final {
public:
    MetadataRegistryEpoch();
    ~MetadataRegistryEpoch();
    MetadataRegistryEpoch(const MetadataRegistryEpoch&) = delete;
    MetadataRegistryEpoch& operator=(const MetadataRegistryEpoch&) = delete;
    MetadataRegistryToken capture() const noexcept;
    // Register the SAME concrete native gate held by the plan/keeper before
    // dispatch. Duplicate gates use one slot. A refusal revokes and joins it.
    // Native current/revoke/destruction must not reenter this registry or Qt.
    bool arm(const MetadataRegistryToken&,
             std::shared_ptr<AddonMetadataNativeAdmission>) const noexcept;
    MetadataRegistryMutation mutation() const noexcept;
    // Permanent, repeated barrier: even a later call joins pending revocation.
    void close() const noexcept;
    static constexpr unsigned ArmedLimit = 64;
private:
    std::shared_ptr<MetadataRegistryEpochState> m_state;
};
}
