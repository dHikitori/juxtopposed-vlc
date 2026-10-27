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

namespace vlc::addons { class AddonManager; }

/** One metadata owner for a host. Call the Qt API on the owner thread.
 * The native backend API can receive a request from a different thread.
 */
class AddonMetadataService : public QObject
{
    Q_OBJECT
public:
    using ArtworkCallback = std::function<void(const QUrl&, const QString&)>;
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

    explicit AddonMetadataService(Backend, QObject* parent = nullptr);
    explicit AddonMetadataService(vlc::addons::AddonManager*, QObject* parent = nullptr);
    ~AddonMetadataService() override;

    bool registerBuiltinProvider(const QString& feature, const QVariantMap& descriptor,
                                 QObject* lifetime, NativeInvoke, NativeCancel = {});
    void unregisterBuiltinProvider(const QString& contribution);
    void completeBuiltin(const QString& invocation, const QString& status,
                         const QVariantMap& data = {}, const QString& error = {});
    QUrl privateUrlForInvocation(const QString& invocation) const;
    // Return a held exact native job for an active native invocation only.
    // A URL or media reference cannot create this binding. The caller releases
    // the job after its native work and claim have ended.
    vlc_metadata_job_t* holdNativeJobForInvocation(const QString& invocation) const;

    // These references are private host data. They are not QML API methods.
    QString acquireMedia(const QUrl& privateUrl, const QVariantMap& identity);
    void releaseMedia(const QString& reference);
    int mediaCount() const { return m_media.size(); }

    QString searchAssets(const QString& feature, const QVariantMap& context, bool aggregate = false);
    QString enrich(const QString& feature, const QVariantMap& context, const QStringList& fields);
    void cancel(const QString& consumer);
    void invalidate();
    void setArtworkAdapter(ArtworkResolver, std::function<void(const QString&)> cancel);
    // This host-only stage uses the pipeline's retained native commit anchor.
    // It never sends the original native artwork address to a provider.
    void setNativeArtworkAdapter(AddonNativeArtworkAssets*);
    // Bind before native dispatch. The owner closes the exact backend before
    // app drain; this memory gate prevents a late native post to Qt.
    bool bindApplicationLifetime(vlc::addons::AddonStreamLifetime::Handle);
    void setAutomaticNetworkAllowed(bool allowed);
    bool automaticNetworkAllowed() const;
    int nativeJobCount() const;
    vlc_metadata_backend_t* holdCoreBackend() const;
    void shutdown();

public slots:
    void operationFinished(const QString& invocation, const QString& contribution,
                           const QString& status, const QVariantMap& data, const QString& error);

signals:
    void finished(const QString& consumer, const QString& status,
                  const QVariantMap& data, const QString& error);
    void canceled(const QString& consumer);
    void invalidated();

private:
    struct CoreState;
    struct Media { QUrl url; QVariantMap identity; QString key; unsigned refs = 0; };
    struct Builtin {
        QStringList features;
        QVariantMap descriptor;
        QPointer<QObject> lifetime;
        NativeInvoke invoke;
        NativeCancel cancel;
    };
    struct NativeCall {
        QString contribution, media;
        QPointer<QObject> lifetime;
        NativeCancel cancel;
        quint64 core = 0;
    };
    struct Consumer { QString media; quint64 core = 0; bool automatic = false; };
    struct PendingResult {
        QString status, error;
        QVariantMap data;
        QHash<QString, QString> assets;
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
    bool applicationCurrent() const;
    static Backend managerBackend(vlc::addons::AddonManager*);

    Backend m_backend;
    AddonMetadataCoordinator m_coordinator;
    QHash<QString, Media> m_media;
    QHash<QString, QString> m_mediaKeys;
    QHash<QString, Builtin> m_builtins;
    QHash<QString, NativeCall> m_nativeCalls;
    QHash<QString, Consumer> m_consumers;
    QHash<QString, PendingResult> m_results;
    QHash<quint64, QString> m_coreConsumers;
    std::shared_ptr<CoreState> m_core;
    QPointer<AddonNativeArtworkAssets> m_nativeArtwork;
    vlc_metadata_backend_t* m_coreBackend = nullptr;
    bool m_closed = false;
};
