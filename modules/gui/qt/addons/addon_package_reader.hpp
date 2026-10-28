// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_store_authority.hpp"
#include <QVariantList>
#include <functional>
#include <optional>

namespace vlc::addons {
// Display data only. Retain and recheck this captured authority at publication
// and before a UI action. The existing Manager action remains authoritative.
struct PackageSummarySnapshot {
    QVariantList rows;
    StoreAuthorityToken authority;
};
// Prepare on the Manager thread; invoke on joined Qt work. No QObject is owned
// or accessed by the reader. nullopt means refusal, not an empty catalogue.
using PackageSummaryReader = std::function<std::optional<PackageSummarySnapshot>()>;
}
