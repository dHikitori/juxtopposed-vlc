// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_stream_lifetime.hpp"
#include <QObject>
#include <QImage>
#include <QMap>
#include <QPointer>
#include <QVariantMap>
#include <QTimer>
#include <functional>
#include <memory>

namespace vlc::addons {
class AddonManager;
class AddonAssetImageProvider;
struct UiAssetCapture {
    QVariantMap registration;
    QMap<QString, QByteArray> assets;
    QMap<QString, QString> cacheKeys;
    std::function<bool()> current;
    // This guard reads memory only, including from an image-provider thread.
    std::function<bool()> live;
    QString error;
};
using UiAssetReader = std::function<UiAssetCapture()>;

class AddonUiAssets : public QObject
{
    Q_OBJECT
public:
    // These quotas cover encoded data, pixel buffers, and the retained cache.
    // Codec workspace and Qt render allocations are separate.
    struct Options {
        qint64 maximumImageBytes = 1024 * 1024;
        qint64 maximumQueuedBytes = 8 * 1024 * 1024;
        qint64 maximumCacheBytes = 32 * 1024 * 1024;
        int maximumDimension = 1024;
        int maximumOutputDimension = 512;
        int maximumJobs = 2;
        int maximumAssets = 64;
        int maximumTickets = 256;
        int maximumSessionImages = 32;
        int maximumImages = 64;
    };
    struct Backend {
        std::function<UiAssetReader(const QString&)> prepare;
        std::function<QImage(const QByteArray&, const QString&, const Options&, QString*)> decode = {};
    };
    struct SharedImages;
    explicit AddonUiAssets(AddonManager*, QObject* parent = nullptr);
    AddonUiAssets(Backend, const Options&, QObject* parent = nullptr);
    ~AddonUiAssets() override;
    bool openSession(const QString& session, const QString& contribution);
    QVariantMap sessionAssets(const QString& session) const;
    // Trusted native consumers can compose these pixels into one surface.
    // These functions are not available through QML or guest messages.
    QVariantMap sessionRegistration(const QString& session) const;
    QMap<QString, QImage> sessionPixels(const QString& session) const;
    bool sessionLoading(const QString& session) const;
    void closeSession(const QString& session, bool notify = true);
    void suspendSession(const QString& session, bool suspended);
    QString claimImage(const QString& session, QObject* owner);
    void releaseImage(const QString& claim);
    QString assetUrlForClaim(const QString& session, const QString& alias, const QString& claim) const;
    void invalidatePackage(const QString& package);
    void invalidate();
    void close();
    QVariantMap statistics() const;
    QString errorMessage() const { return m_error; }
    static QImage decodeImage(const QByteArray&, const QString& suffix, const Options&, QString* error = nullptr);
signals:
    void sessionAssetsChanged(const QString& session);
    void imageBudgetChanged();
private:
    friend class AddonAssetImageProvider;
    struct Scope { QString contribution, package, stamp; quint64 epoch = 0; bool suspended = false; QVariantMap urls; QMap<QString, QString> tickets; QVariantMap registration; std::function<bool()> live; };
    struct Work { QString id, session, alias, suffix, cacheKey; quint64 epoch = 0; QByteArray bytes; std::shared_ptr<void> bytesLease; std::function<bool()> current, live; };
    struct CaptureWork { QString id, session; quint64 epoch; UiAssetReader read; };
    struct Cache { QImage image; qint64 bytes = 0; int pins = 0; quint64 use = 0; };
    struct Claim { QString session; QPointer<QObject> owner; QMetaObject::Connection destroyed; std::shared_ptr<void> lease; };
    static Backend managerBackend(AddonManager*);
    static QString stamp(const QVariantMap&);
    static QImage image(const std::shared_ptr<SharedImages>&, const QString&, QSize*);
    void pump();
    void shutdown(bool notify);
    void expireScopes();
    void decoded(const QString& work, QImage image, const QString& error);
    void captured(const QString& work, UiAssetCapture capture);
    bool publish(const QString& session, const QString& alias, const QString& cacheKey);
    void removeTicket(const QString& ticket);
    bool makeRoom(qint64 bytes);
    Backend m_backend;
    Options m_options;
    std::shared_ptr<SharedImages> m_images;
    AddonStreamLifetime::Handle m_lifetime;
    QHash<QString, Scope> m_scopes;
    QHash<QString, Work> m_jobs;
    QList<Work> m_queue;
    QHash<QString, CaptureWork> m_captureJobs;
    QList<CaptureWork> m_captureQueue;
    QHash<QString, Cache> m_cache;
    QHash<QString, Claim> m_claims;
    QTimer m_authorityTimer;
    qint64 m_queuedBytes = 0, m_cacheBytes = 0;
    quint64 m_epoch = 0, m_use = 0;
    bool m_closing = false;
    QString m_error;
};
}
