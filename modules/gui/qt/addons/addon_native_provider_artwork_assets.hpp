// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_native_artwork_assets.hpp"
#include "addon_native_provider_context.hpp"
#include <QPointer>

// Only the native host can use this materializer. It accepts a sealed claim;
// no URL, guest descriptor, or embedded-artwork permit can create one.
class AddonNativeProviderArtworkAssets final : public QObject
{
public:
    using Cancel = AddonNativeArtworkAssets::Cancel;
    struct Result {
        QUrl file;
        QString error;
        // A direct manual caller must admit its final callback-free Apply.
        // Automatic pipeline work also retains its native parent anchor.
        AddonNativeArtworkAssets::Permit permit;
    };
    using Completion = std::function<void(const Result&)>;
    AddonNativeProviderArtworkAssets(AddonNativeArtworkAssets*, AddonNativeProviderContext*, QObject* parent = nullptr);
    Cancel resolve(const QString& claim, vlc_metadata_job_t*, meta_fetcher_provider_t, Completion);
    bool available() const;
private:
    QPointer<AddonNativeArtworkAssets> m_store;
    QPointer<AddonNativeProviderContext> m_context;
};
