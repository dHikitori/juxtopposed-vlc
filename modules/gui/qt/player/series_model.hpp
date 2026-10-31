// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QDateTime>
#include <QSet>
#include "util/background_watcher.hpp"
#include <QTimer>
#include <memory>
#include "util/series_discovery.hpp"

Q_DECLARE_METATYPE(SeriesDiscovery::Result)
class SeriesScanResult : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
signals:
    void ready(quint64 serial, const SeriesDiscovery::Result& result, bool complete);
};

// The other episodes of the playing media, from local files or already-parsed
// RSS episodes, for the media.siblings projection. Playback, RSS parsing and
// artwork stay in the existing application adapters.
class SeriesModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY contextChanged FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY statusChanged FINAL)
    Q_PROPERTY(bool limited READ limited NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString message READ message NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString showTitle READ showTitle NOTIFY contextChanged FINAL)
    Q_PROPERTY(QUrl feed READ feed NOTIFY contextChanged FINAL)
    Q_PROPERTY(QUrl folder READ folder NOTIFY statusChanged FINAL)
public:
    explicit SeriesModel(QObject* parent = nullptr);
    ~SeriesModel() override;
    bool available() const;
    bool loading() const { return m_loading; }
    bool limited() const { return m_limited; }
    QString message() const { return m_message; }
    QString showTitle() const;
    QUrl feed() const { return QUrl(m_target.feedUrl); }
    QUrl folder() const { return m_root; }
    Q_INVOKABLE void setCurrent(const QUrl& url, const QVariantMap& metadata);
    Q_INVOKABLE void refresh(const QUrl& folder = {});
    Q_INVOKABLE void setFeedEpisodes(const QUrl& feed, const QVariantList& episodes);
    Q_INVOKABLE void setFeedStatus(bool loading, bool failed);
    Q_INVOKABLE void setProgress(const QUrl& url, qreal progress);
    Q_INVOKABLE void setPlayed(const QUrl& url, bool played);
    Q_INVOKABLE void setArtwork(const QUrl& url, const QString& artwork);
    Q_INVOKABLE void setEpisodeTitle(const QUrl& url, const QString& title, const QUrl& source = {});
    Q_INVOKABLE void loadStates();
    // Every known file or feed entry of the series, one item for each
    // version, in episode order: the source of the media.siblings
    // projection for add-on panels. Memory only.
    struct Sibling {
        QUrl url;
        int feedIndex = -1, version = 0;
        SeriesDiscovery::Identity identity;
        QString title, description, artwork;
        QUrl titleSource;
        QDateTime published;
        qreal progress = 0;
        bool played = false, current = false, video = true;
    };
    QList<Sibling> siblings() const;
    // Artwork for one file, as a panel asks for it.
    Q_INVOKABLE void requestArtwork(const QUrl& url);
signals:
    // A sibling, its state or the scan status changed. Not for the progress
    // of the playing media, which changes all the time.
    void siblingsChanged();
    void contextChanged();
    void statusChanged();
    void artworkRequested(const QUrl& url);
    void watchStatesRequested(const QList<QUrl>& urls, quint64 serial);
    void resetRequested();
private:
    struct Row {
        SeriesDiscovery::Episode episode;
        QString title, description, thumbnail, guid;
        QDateTime published;
        int feedIndex = -1;
        bool videoEpisode = true;
    };
    struct State { qreal progress = 0; bool played = false; QString artwork, title; QUrl titleSource; };
    void cancel();
    void scan(const QUrl& folder = {});
    void publish(const SeriesDiscovery::Result& result, bool complete);
    void requestStates();
    bool isCurrent(const Row& row) const;
    QList<Row> m_rows;
    QHash<QUrl, State> m_states;
    QSet<QUrl> m_requestedStates, m_requestedArtwork;
    QVariantMap m_metadata;
    QUrl m_current, m_root;
    SeriesDiscovery::Identity m_target;
    std::shared_ptr<SeriesScanResult> m_results;
    std::shared_ptr<std::atomic_bool> m_cancelled;
    // Off the GUI thread: watching a network folder can take seconds.
    BackgroundWatcher m_watcher;
    QTimer m_refreshTimer;
    quint64 m_serial = 0;
    bool m_loading = false, m_limited = false, m_extended = false;
    QString m_message;
};
