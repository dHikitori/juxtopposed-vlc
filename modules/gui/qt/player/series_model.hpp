// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QAbstractListModel>
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

// A common season projection for local files and already-parsed RSS episodes.
// Playback, RSS parsing and artwork stay in the existing application adapters.
class SeriesModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY contextChanged FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY statusChanged FINAL)
    Q_PROPERTY(bool limited READ limited NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString message READ message NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString showTitle READ showTitle NOTIFY contextChanged FINAL)
    Q_PROPERTY(QUrl feed READ feed NOTIFY contextChanged FINAL)
    Q_PROPERTY(QUrl folder READ folder NOTIFY statusChanged FINAL)
    Q_PROPERTY(QVariantList seasons READ seasons NOTIFY seasonsChanged FINAL)
    Q_PROPERTY(int selectedSeason READ selectedSeason WRITE setSelectedSeason NOTIFY selectedSeasonChanged FINAL)
    Q_PROPERTY(int currentSeason READ currentSeason NOTIFY contextChanged FINAL)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged FINAL)
public:
    enum Role { Title = Qt::UserRole + 1, Number, Description, Thumbnail, Urls, Current, CurrentVersion, Progress, Played, FeedIndex, VideoEpisode, TitleSource };
    explicit SeriesModel(QObject* parent = nullptr);
    ~SeriesModel() override;
    bool available() const;
    bool loading() const { return m_loading; }
    bool limited() const { return m_limited; }
    QString message() const { return m_message; }
    QString showTitle() const;
    QUrl feed() const { return QUrl(m_target.feedUrl); }
    QUrl folder() const { return m_root; }
    QVariantList seasons() const { return m_seasons; }
    int selectedSeason() const { return m_selectedSeason; }
    int currentSeason() const { return m_target.season; }
    void setSelectedSeason(int season);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE void setCurrent(const QUrl& url, const QVariantMap& metadata);
    Q_INVOKABLE void refresh(const QUrl& folder = {});
    Q_INVOKABLE void setFeedEpisodes(const QUrl& feed, const QVariantList& episodes);
    Q_INVOKABLE void setFeedStatus(bool loading, bool failed);
    Q_INVOKABLE void setProgress(const QUrl& url, qreal progress);
    Q_INVOKABLE void setPlayed(const QUrl& url, bool played);
    Q_INVOKABLE void setArtwork(const QUrl& url, const QString& artwork);
    Q_INVOKABLE void requestDetails(int row);
    Q_INVOKABLE void setEpisodeTitle(const QUrl& url, const QString& title, const QUrl& source = {});
    Q_INVOKABLE void loadStates();
    Q_INVOKABLE QVariantMap get(int row) const;
signals:
    void contextChanged();
    void statusChanged();
    void seasonsChanged();
    void selectedSeasonChanged();
    void countChanged();
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
    void project(bool resetting = false);
    void publish(const SeriesDiscovery::Result& result, bool complete);
    void requestStates();
    void update(const QUrl& url, int role);
    bool isCurrent(const Row& row) const;
    QList<Row> m_rows;
    QList<int> m_visible;
    QHash<QUrl, State> m_states;
    QSet<QUrl> m_requestedStates, m_requestedArtwork;
    QVariantList m_seasons;
    QVariantMap m_metadata;
    QUrl m_current, m_root;
    SeriesDiscovery::Identity m_target;
    std::shared_ptr<SeriesScanResult> m_results;
    std::shared_ptr<std::atomic_bool> m_cancelled;
    // Off the GUI thread: watching a network folder can take seconds.
    BackgroundWatcher m_watcher;
    QTimer m_refreshTimer;
    quint64 m_serial = 0;
    int m_selectedSeason = -1;
    bool m_loading = false, m_limited = false, m_extended = false;
    QString m_message;
};
