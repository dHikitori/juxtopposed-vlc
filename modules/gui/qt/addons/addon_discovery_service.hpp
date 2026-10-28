// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_discovery_coordinator.hpp"
#include "addon_queue_transactions.hpp"
#include <QPointer>
#include <QSet>

namespace vlc::addons {
class AddonManager;
class AddonCatalogueView;
// One owner for source views and their user actions. Package data stays behind
// the coordinator. The queue adapter receives only validated source references.
class AddonDiscoveryService final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList sources READ sources NOTIFY changed FINAL)
    Q_PROPERTY(int revision READ revision NOTIFY changed FINAL)
    Q_PROPERTY(bool available READ available NOTIFY availabilityChanged FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY availabilityChanged FINAL)
    Q_PROPERTY(QString preparationError READ preparationError NOTIFY availabilityChanged FINAL)
public:
    struct PlaylistBackend {
        using Choices = std::function<void(bool available, const QVariantList&, bool more, const QString&)>;
        using Prepared = std::function<void(const QString& target, const QString& error)>;
        std::function<AddonQueueTransactions::Cancel(const QString& query, Choices)> list;
        std::function<AddonQueueTransactions::Cancel(const QString& choice, Prepared)> prepare;
        std::function<void(const QString& target)> release;
        std::function<void(const QStringList& choices)> releaseChoices;
    };
    AddonDiscoveryService(AddonDiscoveryCoordinator::Backend, AddonQueueTransactions::Backend, QObject* parent = nullptr);
    AddonDiscoveryService(AddonManager*, AddonDiscoveryCoordinator::Backend, AddonQueueTransactions::Backend, QObject* parent = nullptr);
    ~AddonDiscoveryService() override;
    QVariantList sources() const { return m_closing ? QVariantList{} : m_coordinator.sources(); }
    int revision() const { return m_revision; }
    bool available() const;
    bool loading() const;
    QString preparationError() const { return m_preparationError; }
    // Each visible page holds one opaque request. Requests share one feature
    // ticket and never start a guest or a native source on their own.
    Q_INVOKABLE QString acquireDisplay();
    Q_INVOKABLE void releaseDisplay(const QString& request);
    Q_INVOKABLE QString openSource(const QString& source);
    Q_INVOKABLE QVariantMap view(const QString& id) const;
    Q_INVOKABLE bool browse(const QString& view, const QString& node);
    Q_INVOKABLE bool search(const QString& view, const QString& query);
    Q_INVOKABLE bool back(const QString& view);
    Q_INVOKABLE bool loadMore(const QString& view);
    Q_INVOKABLE bool canPlay(const QString& view) const;
    Q_INVOKABLE bool canEnqueue(const QString& view) const;
    struct SourcePlayback {
        std::function<bool(const QString&)> ready;
        std::function<bool(const QVariantMap&)> referenceReady;
        std::function<void(const QString&)> refresh;
    };
    void setSourcePlayback(SourcePlayback);
    void refreshSourcePlayback();
    void setPlaylistBackend(PlaylistBackend);
    Q_INVOKABLE bool loadPlaylists(const QString& view, const QString& query = {});
    Q_INVOKABLE bool choosePlaylist(const QString& view, const QString& choice);
    Q_INVOKABLE void clearPlaylist(const QString& view);
    Q_INVOKABLE QString enqueue(const QString& view, const QStringList& items, const QString& target = QStringLiteral("queue"));
    Q_INVOKABLE QString play(const QString& view, const QString& item);
    Q_INVOKABLE void closeView(const QString& view);
    void invalidate();
    void close();
public slots:
    void operationFinished(const QString& invocation, const QString& contribution, const QString& status,
                           const QVariantMap& data, const QString& error);
signals:
    void changed();
    void availabilityChanged();
    void viewChanged(const QString& view);
    void viewClosed(const QString& view);
    void actionFinished(const QString& view, bool success, const QString& error);
private:
    struct View {
        QString source, pending, cursor, query, error;
        QVariantMap sourceIdentity;
        QStringList path;
        QVariantList items;
        quint64 generation = 1;
        bool busy = false, append = false, started = false;
        QVariantList playlists;
        QString playlistTarget, playlistError;
        AddonQueueTransactions::Cancel playlistCancel;
        quint64 playlistGeneration = 0;
        bool playlistAvailable = false, playlistBusy = false, morePlaylists = false;
    };
    struct Play { QString view, item; AddonQueueTransactions::Snapshot snapshot; };
    static AddonDiscoveryCoordinator::Backend managerBackend(AddonManager*, AddonDiscoveryCoordinator::Backend);
    bool fetch(const QString& view, bool append = false);
    bool visibleMedia(const View&, const QString& item) const;
    void completed(const QString&, const QString&, const QVariantMap&, const QString&);
    void displayChanged();
    void syncDisplayDemand();
    bool needsDisplayDemand() const;
    bool viewCurrent(const View&) const;
    void startView(const QString&);
    AddonDiscoveryCoordinator m_coordinator;
    AddonQueueTransactions m_transactions;
    QHash<QString, View> m_views;
    QHash<QString, Play> m_plays;
    QHash<QString, QString> m_actions;
    QHash<QString, QString> m_playActions;
    QHash<QString, QString> m_libraryActions;
    PlaylistBackend m_playlists;
    SourcePlayback m_sourcePlayback;
    QPointer<AddonCatalogueView> m_display;
    QSet<QString> m_displayRequests;
    QString m_featureTicket;
    QString m_preparationError;
    quint64 m_demandGeneration = 0;
    bool m_displayAttached = false, m_acquiringDemand = false, m_demandRefused = false;
    int m_revision = 0;
    bool m_closing = false;
};
}
