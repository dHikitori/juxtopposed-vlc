// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addons/addon_native_artwork_assets.hpp"
#include <cstdlib>

// A standalone test has no app shutdown owner. Join before stack fixtures end.
class TestNativeArtwork : public AddonNativeArtworkAssets
{
    using Lifetime = vlc::addons::AddonStreamLifetime;
    std::shared_ptr<Lifetime> m_lifetime;
    TestNativeArtwork(std::shared_ptr<Lifetime> lifetime, Backend backend,
        const QString& directory, const Options& options)
        : AddonNativeArtworkAssets(lifetime->handle(), std::move(backend), directory, options),
          m_lifetime(std::move(lifetime)) {}
public:
    TestNativeArtwork(Backend backend, const QString& directory, const Options& options)
        : TestNativeArtwork(std::make_shared<Lifetime>(), std::move(backend), directory, options) {}
    TestNativeArtwork(vlc_object_t* parent, const QString& directory, const Options& options)
        : TestNativeArtwork(nativeBackend(parent), directory, options) {}
    ~TestNativeArtwork() override
    {
        close(); m_lifetime->close();
        if (!m_lifetime->handle().waitQt(std::chrono::seconds(5))) std::abort();
    }
};
