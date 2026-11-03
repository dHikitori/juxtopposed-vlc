// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include "addon_metadata_service.hpp"
#include "addon_native_artwork_assets.hpp"
#include "addon_native_provider_context.hpp"
#include <QHash>
#include <QPointer>
#include <memory>
#include <optional>
class AddonNativeProviderArtworkAssets;
namespace vlc::addons { class AddonManager; }

// Create, use, and destroy this owner on the service thread. The native parent
// must remain valid through app drain. create() never stores that raw parent.
// The caller must attach the returned owner to its interface lifetime.
class AddonNativeMetadataOwner final : public QObject
{
    Q_OBJECT
public:
    struct Options {
        QString cacheDirectory;
        AddonNativeArtworkAssets::Options images;
        AddonNativeArtworkAssets::Backend imageReader;
        bool activateCoreOwner = false;
        // Families whose built-in member (the Core script) is still offered.
        // Unset offers all of them. Leave out a family once its add-on passes
        // test_parity.py: the plan then chooses the add-on.
        std::optional<QStringList> nativeFamilies;
    };
    static std::unique_ptr<AddonNativeMetadataOwner> create(AddonMetadataService*,
        vlc::addons::AddonManager*, vlc_object_t*, const Options&);
    ~AddonNativeMetadataOwner() override;
    bool ready() const;
    bool active() const { return m_active && ready(); }
    void close();
private:
    AddonNativeMetadataOwner(AddonMetadataService*, vlc::addons::AddonManager*);
    bool initialize(vlc_object_t*, const Options&);
    void invoke(const QString& contribution, meta_fetcher_provider_t,
        const QString& invocation, const QString& operation);
    void cancel(const QString& invocation);
    QPointer<AddonMetadataService> m_service;
    QPointer<vlc::addons::AddonManager> m_manager;
    QPointer<AddonNativeProviderContext> m_context;
    QPointer<AddonNativeArtworkAssets> m_images;
    QPointer<AddonNativeProviderArtworkAssets> m_materializer;
    std::shared_ptr<vlc_metadata_backend_t> m_backend;
    QHash<QString, AddonNativeProviderContext::Cancel> m_calls;
    QStringList m_providers;
    QString m_partition;
    bool m_ready = false, m_active = false, m_closed = false;
};
