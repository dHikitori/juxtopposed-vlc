// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "addon_metadata_coordinator.hpp"
#include "addon_stream_lifetime.hpp"
#include <QPointer>
#include <QUrl>
#include <memory>
typedef struct vlc_metadata_backend vlc_metadata_backend_t;
typedef struct vlc_metadata_job vlc_metadata_job_t;
class AddonNativeArtworkAssets;
class AddonMetadataChoices;
struct input_item_t;

namespace vlc::addons { class AddonManager; }

/** One metadata owner for a host. Call the Qt API on the owner thread.
 * The native backend API can receive a request from a different thread.
 */
class AddonMetadataService : public QObject
{
    Q_OBJECT
public:
    using ArtworkCallback = std::function<void(const QUrl&, const QString&)>;
    // Run this resolver and its completion on the service owner thread.
    using ArtworkResolver = std::function<QString(const QString&, const QVariant&, ArtworkCallback)>;
    struct Backend {
        AddonMetadataCoordinator::Backend providers;
        std::function<void(const QString&, const QVariantMap&)> registerBuiltin;
        std::function<void(const QString&)> unregisterBuiltin;
        // The adapter must use the provider's asset broker. It must return a
        // host-owned local file. A guest URL is not an approved output file.
        ArtworkResolver resolveArtwork;
        std::function<void(const QString&)> cancelArtwork;
    };
    using NativeInvoke = std::function<void(const QString&, const QString&, const QVariantMap&)>;
    using NativeCancel = std::function<void(const QString&)>;
    struct NativeArtworkRequest { QString id; std::function<void()> cancel; };
    struct NativeArtworkHandlers {
        // This query must not do I/O, call a callback, or change Qt state.
        std::function<std::optional<AddonMetadataCoordinator::NativeArtwork>(const QVariantMap&, vlc_metadata_job_t*)> validate;
        // Only automatic native jobs can use this route. Their parent anchor
        // must remain current through final publication. Cancel is native only.
        std::function<NativeArtworkRequest(const QVariant&, vlc_metadata_job_t*, ArtworkCallback)> resolve;
    };
    using ProviderPlan = std::function<QVariantList(const QString&, const QString&, const QVariantList&, bool)>;

    explicit AddonMetadataService(Backend, QObject* parent = nullptr);
    explicit AddonMetadataService(vlc::addons::AddonManager*, QObject* parent = nullptr);
    ~AddonMetadataService() override;

    bool registerBuiltinProvider(const QString& feature, const QVariantMap& descriptor,
                                 QObject* lifetime, NativeInvoke, NativeCancel = {}, NativeArtworkHandlers = {});
    void unregisterBuiltinProvider(const QString& contribution);
    void unregisterBuiltinProviderForOwner(const QString& contribution, QObject* exactOwner);
    void setProviderPlan(QObject* lifetime, ProviderPlan);
    void clearProviderPlan(QObject* exactOwner);
    void completeBuiltin(const QString& invocation, const QString& status,
                         const QVariantMap& data = {}, const QString& error = {});
    QUrl privateUrlForInvocation(const QString& invocation) const;
    // Return a held exact native job for an active native invocation only.
    // A URL or media reference cannot create this binding. The caller releases
    // the job after its native work and claim have ended.
    vlc_metadata_job_t* holdNativeJobForInvocation(const QString& invocation) const;
    // Install a native-only cancel control for this exact active invocation.
    // A native job cancel must not wait for a Qt event to interrupt its I/O.
    void setNativeCancellationForInvocation(const QString&, std::function<void()>);

    // These references are private host data. They are not QML API methods.
    QString acquireMedia(const QUrl& privateUrl, const QVariantMap& identity);
    void releaseMedia(const QString& reference);
    int mediaCount() const { return m_media.size(); }

    // The caller owns the retained StartLocal anchor and the exact target.
    // This API requires NETWORK and adds native holds. It does not start,
    // complete, or apply the job. Automatic jobs also require network consent.
    // Change the host context through a new binding, not an existing reference.
    QString bindIdentityTarget(vlc_metadata_job_t* anchor, input_item_t* exactTarget,
                               const QVariantMap& hostContext = {});
    void releaseIdentityTarget(const QString& target);
    int identityTargetCount() const { return m_identityTargets.size(); }
    QString identify(const QString& feature, const QString& target);
    QString acceptIdentity(const QString& target, const QString& choice);
    QVariantMap selectedIdentity(const QString& target, const QString& selection) const;
    QString enrichSelected(const QString& feature, const QString& target, const QString& selection,
                           const QStringList& fields);
    // The host must use the normal native Complete/Apply path. A held job does
    // not permit a write. Apply must still validate its exact original target.
    // The write adapter must retain provider/account authority through commit.
    vlc_metadata_job_t* holdIdentityAnchor(const QString& target, const QString& selection) const;
    void releaseIdentityChoiceSet(const QString& set);
    void releaseIdentitySelection(const QString& selection);

    QString searchAssets(const QString& feature, const QVariantMap& context, bool aggregate = false);
    QString enrich(const QString& feature, const QVariantMap& context, const QStringList& fields);
    void cancel(const QString& consumer);
    void invalidate();
    void setArtworkAdapter(ArtworkResolver, std::function<void(const QString&)> cancel);
    // This host-only stage uses the pipeline's retained native commit anchor.
    // It never sends the original native artwork address to a provider.
    void setNativeArtworkAdapter(AddonNativeArtworkAssets*);
    void clearNativeArtworkAdapterIf(AddonNativeArtworkAssets* exactAdapter);
    bool reserveNativeOwner(QObject*);
    bool nativeOwnerIs(QObject*) const;
    void releaseNativeOwnerFor(QObject*);
    // Bind before native dispatch. The owner closes the exact backend before
    // app drain; this memory gate prevents a late native post to Qt.
    bool bindApplicationLifetime(vlc::addons::AddonStreamLifetime::Handle);
    vlc::addons::AddonStreamLifetime::Handle applicationLifetime() const;
    void setAutomaticNetworkAllowed(bool allowed);
    bool automaticNetworkAllowed() const;
    int nativeJobCount() const;
    vlc_metadata_backend_t* holdCoreBackend() const;
    void shutdown();

public slots:
    void operationFinished(const QString& invocation, const QString& contribution,
                           const QString& status, const QVariantMap& data, const QString& error);

signals:
    void retiring();
    void finished(const QString& consumer, const QString& status,
                  const QVariantMap& data, const QString& error);
    void canceled(const QString& consumer);
    void invalidated();

private:
    struct CoreState;
    struct Media { QUrl url; QVariantMap identity; QString key; unsigned refs = 0; };
    struct IdentityTarget {
        std::shared_ptr<vlc_metadata_job_t> anchor;
        std::shared_ptr<input_item_t> target;
        QString media, nonce;
        QVariantMap context;
    };
    struct IdentityRequest { QString target, selection, feature; QVariantMap context; };
    struct Builtin {
        QStringList features;
        QVariantMap descriptor;
        QPointer<QObject> lifetime;
        NativeInvoke invoke;
        NativeCancel cancel;
        NativeArtworkHandlers artwork;
        QString binding;
        QMetaObject::Connection destroyed;
    };
    struct NativeCall {
        QString contribution, media;
        QPointer<QObject> lifetime;
        NativeCancel cancel;
        quint64 core = 0;
    };
    struct Consumer { QString media; quint64 core = 0; bool automatic = false; };
    struct PendingResult {
        struct Asset { QString id; std::function<void()> cancel; };
        QString status, error;
        QVariantMap data;
        QHash<QString, Asset> assets;
        int outstanding = 0;
    };

    QVariantList providers(const QString& feature) const;
    QString invoke(const QString& contribution, const QString& operation, const QVariantMap& context,
                   const QString& privateScope = {});
    void cancelInvocation(const QString& invocation);
    void retireConsumer(const QString& consumer);
    QString start(const QString& feature, QVariantMap context, const QStringList& fields,
                  bool aggregate, bool assets, const QString& privateScope = {});
    void coordinatorFinished(const QString&, const QString&, const QVariantMap&, const QString&);
    void artworkFinished(const QString& consumer, const QString& field, const QUrl&, const QString&);
    void deliver(const QString&, const QString&, const QVariantMap&, const QString&);
    void startCore(quint64 key);
    void startNativeArtwork(quint64 key);
    void cancelCore(quint64 key);
    void finishCore(quint64 key, const QString&, const QVariantMap&);
    bool holdMedia(const QString&);
    std::optional<QVariantMap> identitySubject(const QString& media) const;
    bool identityCurrent(const QString& target) const;
    void retireIdentityTargets(const QString& target = {});
    bool applicationCurrent() const;
    static Backend managerBackend(vlc::addons::AddonManager*);

    Backend m_backend;
    AddonMetadataCoordinator m_coordinator;
    QHash<QString, Media> m_media;
    QHash<QString, QString> m_mediaKeys;
    QHash<QString, IdentityTarget> m_identityTargets;
    QHash<QString, QString> m_identityMedia;
    QHash<QString, IdentityRequest> m_identityRequests;
    std::unique_ptr<AddonMetadataChoices> m_choices;
    bool m_identityInvalidating = false;
    bool m_invalidateActive = false;
    QHash<QString, Builtin> m_builtins;
    QHash<QString, NativeCall> m_nativeCalls;
    QHash<QString, Consumer> m_consumers;
    QHash<QString, PendingResult> m_results;
    QHash<quint64, QString> m_coreConsumers;
    QPointer<QObject> m_planOwner;
    QPointer<QObject> m_nativeOwner;
    ProviderPlan m_providerPlan;
    std::shared_ptr<CoreState> m_core;
    QPointer<AddonNativeArtworkAssets> m_nativeArtwork;
    vlc_metadata_backend_t* m_coreBackend = nullptr;
    bool m_closed = false;
};
