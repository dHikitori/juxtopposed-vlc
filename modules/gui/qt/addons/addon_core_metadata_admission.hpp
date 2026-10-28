// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_metadata_attempt.hpp"
#include <memory>
#include <vlc_common.h>
#include <vlc_tick.h>
typedef struct vlc_metadata_artwork_admission vlc_metadata_artwork_admission_t;
namespace vlc::addons {
// Contains only a native gate and a std quota. It owns no app Handle, child,
// callback, QObject, Qt value, or reader. Hold a shared owner through each call.
class CoreMetadataAdmission final : public AddonMetadataNativeAdmission {
public:
    static std::shared_ptr<CoreMetadataAdmission> create(vlc_tick_t deadline) noexcept;
    ~CoreMetadataAdmission() override;
    bool current() const noexcept override;
    void revoke() noexcept override;
    vlc_metadata_artwork_admission_t* nativeHandle() const noexcept { return m_gate; }
    static unsigned processCount() noexcept;
    static constexpr unsigned ProcessLimit = 64;
private:
    struct Permit;
    CoreMetadataAdmission(vlc_metadata_artwork_admission_t*, std::shared_ptr<Permit>) noexcept;
    // First declared: release the charge after every owned native capture.
    const std::shared_ptr<Permit> m_permit;
    vlc_metadata_artwork_admission_t* const m_gate;
};
}
