// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QPointer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QElapsedTimer>
#include <QSet>
#include <QVariantMap>
#include <functional>
#include "util/series_discovery.hpp"

class MainCtx;
class QJsonDocument;
class AddonMetadataService;
Q_MOC_INCLUDE("maininterface/mainctx.hpp")

// Background episode names. Playback and file tags are never modified.
class EpisodeMetadata : public QObject
{
    Q_OBJECT
    Q_PROPERTY(MainCtx* ctx READ ctx WRITE setCtx NOTIFY ctxChanged FINAL)
public:
    explicit EpisodeMetadata(QObject* parent = nullptr);
    ~EpisodeMetadata() override;
    MainCtx* ctx() const;
    void setCtx(MainCtx* ctx);
    void setMetadataService(AddonMetadataService*);
    Q_INVOKABLE void request(const QUrl& url, const QVariantMap& metadata);
    Q_INVOKABLE void readCached(const QList<QUrl>& urls);
signals:
    void ctxChanged();
    void titleReady(const QUrl& url, const QString& title, const QUrl& source);
private:
    bool online() const;
    void cancel();
    void schedule();
    void lookup();
    void lookupProvider();
    void providerFinished(const QString&, const QString&, const QVariantMap&, const QString&);
    void fetchSeason(quint64 generation, int season);
    void applyTitles();
    MediaIdentity::Identity identity(const QUrl& url) const;
    void get(const QUrl& url, quint64 generation, std::function<void(const QJsonDocument&)> completed);
    void save(const QUrl& url, const QVariantMap& metadata, const QString& title, const QUrl& source);
    QPointer<MainCtx> m_ctx;
    QPointer<AddonMetadataService> m_sharedMetadata;
    bool m_ownedMetadataProfile = false;
    QString m_providerRequest;
    QUrl m_providerTarget;
    QVariantMap m_providerIdentity;
    QSet<QUrl> m_attemptedProviders;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QElapsedTimer m_lastRequest;
    QUrl m_current;
    QVariantMap m_metadata;
    struct EpisodeTitle { QString title; QUrl source; };
    QSet<QUrl> m_candidates;
    QSet<QUrl> m_resolved;
    QMap<int, QMap<int, EpisodeTitle>> m_titles;
    QMap<int, int> m_seasonIds;
    QSet<int> m_attemptedSeasons;
    int m_showId = 0;
    int m_cacheReads = 0;
    bool m_seasonsRead = false;
    bool m_searchAttempted = false;
    bool m_busy = false;
    quint64 m_generation = 0;
};
