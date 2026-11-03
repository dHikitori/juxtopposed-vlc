// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_stream_proxy.hpp"
#include "addon_native_source_resolver.hpp"
namespace vlc::addons {
// Call only from the host resolver after consent and source admission.
AddonNativeSourceResolver::Cancel prepareContainedSource(const AddonNativeSourceResolver::Admission&,
    AddonNativeSourceResolver::Current, AddonStreamProxy::Spec, AddonNativeSourceResolver::PreparedCallback);
// Explicit native fixture. The app mount must use prepareContainedSource.
AddonNativeSourceResolver::Cancel prepareContainedSourceForTesting(const AddonNativeSourceResolver::Admission&,
    AddonNativeSourceResolver::Current, AddonStreamProxy::Spec, AddonStreamProxy::Testing, AddonNativeSourceResolver::PreparedCallback);
}
