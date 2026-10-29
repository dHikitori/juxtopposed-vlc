// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_artwork_attempt_aggregate.hpp"
#include "addon_metadata_catalogue_source.hpp"
#include "addon_metadata_coordinator.hpp"
#include "addon_metadata_registry_epoch.hpp"
#include "addon_native_provider_artwork_assets.hpp"
#include "addon_prepared_metadata_invocation.hpp"
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <atomic>
#include <mutex>

namespace vlc::addons {
class AddonManager;
// Host only. Use this owner on the Service thread. The native cancellation
// control owns no QObject, Qt value, reader, claim or application Handle.
class ArtworkLookupRunner final : public QObject {
public:
    using Item = std::shared_ptr<input_item_t>;
    using Job = std::shared_ptr<vlc_metadata_job_t>;
    using Cancel = std::function<void()>;
    using Resolve = std::function<QString(const QString&,const QVariant&,
        std::function<void(const QUrl&,const QString&)>)>;
    struct Options {
        Job child;
        QString privateScope;
        QJsonObject context;
        AddonStreamLifetime::Handle lifetime;
        std::shared_ptr<const MetadataRegistryEpoch> registry;
        MetadataRegistryToken registryToken;
        QList<ArtworkBuiltinRegistration> builtins;
        // Native/memory only. Include exact child and global consent state.
        std::function<bool()> live;
        Resolve resolvePortable;
        std::function<void(const QString&)> cancelPortable;
        AddonNativeProviderContext* nativeContext = nullptr;
        AddonNativeProviderArtworkAssets* nativeImages = nullptr;
    };
    struct Result { int status = VLC_EGENERIC; Item item; QByteArray provenance; };
    using Complete = std::function<void(Result)>;
    ArtworkLookupRunner(AddonManager*,Options,Complete,QObject* parent=nullptr);
    ~ArtworkLookupRunner() override;
    bool start();
    void cancel();
    Cancel nativeCancellation() const;
private:
    struct Control;
    bool current() const;
    ArtworkAttemptPlan::Capture capture() const;
    void capturePlan();
    void advance();
    void portable(unsigned);
    void native(unsigned);
    void portableResult(const QString&,const QString&,const QVariantMap&,const QString&);
    void acceptFields(unsigned,const QString&,const QVariantMap&);
    void resolvePortable(unsigned,const QVariant&);
    void resolveNative(unsigned,const QString&,meta_fetcher_provider_t);
    void recheck(unsigned,Item,const QByteArray&,AddonNativeArtworkAssets::Permit,bool negative);
    void block(unsigned,ArtworkAttemptAggregate::BlockReason);
    void finish(Result,bool sealed=false);
    QPointer<AddonManager> m_manager;
    QPointer<AddonNativeProviderContext> m_nativeContext;
    QPointer<AddonNativeProviderArtworkAssets> m_nativeImages;
    Options m_options;
    Complete m_complete;
    std::shared_ptr<Control> m_control;
    std::optional<MetadataCatalogueSource> m_source;
    std::shared_ptr<const ArtworkAttemptPlan> m_plan;
    std::unique_ptr<ArtworkAttemptAggregate> m_aggregate;
    std::shared_ptr<AddonMetadataAttemptAdmission> m_attempt;
    QPointer<AddonMetadataCoordinator> m_acceptance;
    QTimer m_deadline;
    QString m_preparation,m_invocation,m_asset;
    quint64 m_stage = 0;
    unsigned m_ordinal = 0;
    bool m_started = false,m_terminal = false;
};
}
