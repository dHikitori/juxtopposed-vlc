#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_core_metadata_admission.hpp"
#include "../../../../src/preparser/metadata_artwork_outcome.h"
#include <utility>
#include <atomic>
namespace vlc::addons {
namespace { std::atomic_uint plans{0}; }
struct CoreMetadataAdmission::Permit {
    bool counted = false;
    ~Permit() { if (counted) plans.fetch_sub(1, std::memory_order_acq_rel); }
};
CoreMetadataAdmission::CoreMetadataAdmission(vlc_metadata_artwork_admission_t* gate,
    std::shared_ptr<Permit> permit) noexcept : m_permit(std::move(permit)), m_gate(gate) {}
std::shared_ptr<CoreMetadataAdmission> CoreMetadataAdmission::create(vlc_tick_t deadline) noexcept
{
    vlc_metadata_artwork_admission_t* gate = nullptr;
    try {
        auto permit = std::make_shared<Permit>();
        auto count = plans.load(std::memory_order_acquire);
        for (;;) {
            if (count >= ProcessLimit) return {};
            if (plans.compare_exchange_weak(count, count + 1, std::memory_order_acq_rel)) break;
        }
        permit->counted = true;
        gate = vlc_metadata_artwork_admission_New(deadline);
        if (!gate) return {};
        auto* owner = new CoreMetadataAdmission(gate, std::move(permit));
        gate = nullptr; // shared_ptr deletes owner if its control allocation fails.
        return std::shared_ptr<CoreMetadataAdmission>(owner);
    } catch (...) {
        if (gate) vlc_metadata_artwork_admission_Release(gate);
        return {};
    }
}
CoreMetadataAdmission::~CoreMetadataAdmission()
{ vlc_metadata_artwork_admission_Release(m_gate); }
bool CoreMetadataAdmission::current() const noexcept
{ return vlc_metadata_artwork_admission_IsCurrent(m_gate); }
void CoreMetadataAdmission::revoke() noexcept
{
    // Core closes admission before its mutex and joins on EVERY call, including
    // a second revoker while the first waits for a commit. Never shortcut here.
    vlc_metadata_artwork_admission_Revoke(m_gate);
}
unsigned CoreMetadataAdmission::processCount() noexcept
{ return plans.load(std::memory_order_acquire); }
}
