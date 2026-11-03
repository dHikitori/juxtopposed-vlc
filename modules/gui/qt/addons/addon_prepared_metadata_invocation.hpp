// SPDX-License-Identifier: GPL-2.0-or-later
// Host C++ only. No guest or QML constructor is available.
#pragma once
#include <QString>
#include <functional>
#include <memory>

namespace vlc::addons {
class AddonManager;
class ArtworkAttemptPlan;
struct PreparedMetadataRuntime;
struct PreparedMetadataOwner;

class PreparedMetadataInvocation final
{
public:
    ~PreparedMetadataInvocation();
    PreparedMetadataInvocation(const PreparedMetadataInvocation&) = delete;
    PreparedMetadataInvocation& operator=(const PreparedMetadataInvocation&) = delete;
    bool current() const noexcept;
    static constexpr unsigned ProcessLimit = 8;
    static constexpr unsigned RecipeLimit = 64;
    static constexpr qsizetype MaximumFieldBytes = 16 * 1024;
    static constexpr qsizetype MaximumSecretBytes = 1024 * 1024;
    static constexpr qsizetype ProcessSecretBytes = 16 * 1024 * 1024;
    static unsigned processCount() noexcept;
private:
    friend class AddonManager;
    friend struct PreparedMetadataRuntime;
    struct Data;
    explicit PreparedMetadataInvocation(std::shared_ptr<Data>);
    std::shared_ptr<Data> m_data;
};
// Create this callable on the Manager thread. Run it on joined Qt work.
// Discard it before native final admission. It can own a credential reader.
using MetadataInvocationPreparer = std::function<std::unique_ptr<PreparedMetadataInvocation>(QString*)>;
}
