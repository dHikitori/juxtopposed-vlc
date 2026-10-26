// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_network.hpp"
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
    };
    AddonArtworkAssets(vlc::addons::AddonManager*, const QString& cacheDirectory, QObject* parent = nullptr);
    AddonArtworkAssets(Backend, const QString& cacheDirectory, const Options&, QObject* parent = nullptr);
    ~AddonArtworkAssets() override;
    QString resolve(const QString& contribution, const QVariant& descriptor, Completion);
    void cancel(const QString& request);
    void invalidate();
    static bool imageFormat(const QByteArray&, const Options&, QString* extension);
private:
    struct Waiter { QString key; Completion completion; };
    struct Transfer {
        QString contribution, partition;
        QJsonObject request;
        QSet<QString> waiters;
        quint64 network = 0;
    };
    static Backend managerBackend(vlc::addons::AddonManager*);
    bool current(const Transfer&) const;
    QUrl cached(const QString& key) const;
    void complete(const QString& key, const vlc::addons::NetworkBroker::Response&);
    void finish(const QString& key, const QUrl&, const QString&);
    void measureCache();
    Backend m_backend;
    QString m_directory;
    Options m_options;
    QHash<QString, Waiter> m_waiters;
    QHash<QString, Transfer> m_transfers;
    int m_cacheEntries = 0;
    qint64 m_cacheBytes = 0;
};
