// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_network.hpp"
#include "addon_prepared_artwork_asset.hpp"
#include <QPointer>
#include <QSet>
#include <QVariant>

namespace vlc::addons { class AddonManager; }

class AddonArtworkAssets : public QObject
{
    Q_OBJECT
public:
    using Completion = std::function<void(const QUrl&, const QString&)>;
    struct Options {
        int maxEntries = 8192;
        qint64 maxCacheBytes = 128 * 1024 * 1024;
        qint64 maxImageBytes = 1024 * 1024;
        int maxDimension = 1024;
    };
    struct Backend {
        std::function<QVariantMap(const QString&)> registration;
        std::function<quint64(const QString&, const QJsonObject&, vlc::addons::NetworkBroker::Callback)> fetch;
        std::function<void(quint64)> cancel;
        std::function<QString(const QString&,vlc::addons::ArtworkPreparationComplete)> prepare;
        std::function<void(const QString&)> cancelPreparation;
        std::function<quint64(const vlc::addons::PreparedArtwork&,const QJsonObject&,vlc::addons::NetworkBroker::Callback)> fetchPrepared;
        // The production executor uses the bounded, joined Manager queue.
        std::function<QString(std::function<void()>,std::function<void(bool)>)> work;
        std::function<void(const QString&)> cancelWork;
    };
    AddonArtworkAssets(vlc::addons::AddonManager*, const QString& cacheDirectory, QObject* parent = nullptr);
    AddonArtworkAssets(Backend, const QString& cacheDirectory, const Options&, QObject* parent = nullptr);
    ~AddonArtworkAssets() override;
    QString resolve(const QString& contribution, const QVariant& descriptor, Completion);
    void cancel(const QString& request);
    void invalidate();
    static bool imageFormat(const QByteArray&, const Options&, QString* extension);
private:
    struct Waiter { QString key; Completion completion; QString preparation; };
    struct Transfer {
        QString contribution, partition;
        QJsonObject request;
        QSet<QString> waiters;
        quint64 network = 0;
        vlc::addons::PreparedArtwork prepared;
        QString work;
        quint64 stage = 0;
    };
    static Backend managerBackend(vlc::addons::AddonManager*);
    bool current(const Transfer&) const;
    void begin(const QString& id,const QString& contribution,const QJsonObject& request,
               const QVariantMap& registration,vlc::addons::PreparedArtwork = {});
    void failWaiter(const QString& id,const QString& error);
    struct Cache;
    struct CacheResult;
    void cacheWork(const QString&,const vlc::addons::NetworkBroker::Response* = nullptr);
    void startFetch(const QString&);
    void complete(const QString& key, const vlc::addons::NetworkBroker::Response&);
    void finish(const QString& key, const QUrl&, const QString&);
    Backend m_backend;
    QString m_directory;
    Options m_options;
    QHash<QString, Waiter> m_waiters;
    QHash<QString, Transfer> m_transfers;
    const std::shared_ptr<Cache> m_cache;
    quint64 m_workSequence = 0;
};
