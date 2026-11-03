// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_host_services.hpp"
#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QVariantMap>
#include <functional>
#include <memory>

namespace vlc::addons {

// The media.siblings projection (permission media.siblings, with the sources
// "folder" and "feed") for add-on player panels.
//
// The app finds the other episodes of the playing media: the files of the
// same series in its folder (scanned on a background worker), or the entries
// of its podcast feed. A package reads them as copies with opaque IDs and
// relative names; it never gets a path, a URL or the folder root. A command
// reply that a user click started can then ask the app to play one of these
// IDs, rescan, or open the series choice (runEffect).
class AddonMediaSiblings final : public QObject
{
    Q_OBJECT
    // Changes when the source changes; the app then refreshes open panels.
    Q_PROPERTY(int revision READ revision NOTIFY changed FINAL)
    // Changes when a package's artwork map (artwork()) gains or changes an
    // entry, so a view that resolves media: handles reads it again.
    Q_PROPERTY(int artworkRevision READ artworkRevision NOTIFY artworkChanged FINAL)
public:
    struct Item {
        QUrl url;
        QString kind; // "folder" or "feed"
        QString name, folder, title, description, artwork;
        QUrl titleSource;
        int feedIndex = -1, version = 0, season = -1;
        QList<int> episodes;
        QString absolute;
        QDate airDate;
        QDateTime published;
        double progress = 0;
        bool played = false, current = false, video = true;
    };
    struct Snapshot {
        QString kind; // "folder", "feed" or empty when the media has no series
        QString show, message;
        bool loading = false, limited = false;
        QList<Item> items;
    };
    struct Source {
        std::function<Snapshot()> snapshot;     // owner thread, memory only
        std::function<void(const QUrl&)> artwork; // ask for one file's artwork
    };
    static constexpr int MaxItems = 100;
    static constexpr int MaxDetails = 4;
    static constexpr int WaitMilliseconds = 8000;

    explicit AddonMediaSiblings(QObject* parent = nullptr);
    ~AddonMediaSiblings() override;
    bool mount(AddonHostServices*, Source);
    int revision() const { return m_revision; }
    int artworkRevision() const { return m_artworkRevision; }

    // A play, rescan or identify effect from AddonUiRegistry::panelEffect.
    // Only a package that read the siblings can ask, and only for an ID that
    // it got for the current media.
    Q_INVOKABLE bool runEffect(const QString& packageId, const QString& effect, const QVariantMap& value);
    // The artwork of the items a package got, as media:<id> handles, for the
    // app's own image loading in that package's views.
    Q_INVOKABLE QVariantMap artwork(const QString& packageId) const;

public slots:
    // The source data changed. Waiting reads complete when a scan ends.
    void sourceChanged();

signals:
    void changed();
    void artworkChanged();
    void playRequested(const QUrl& url, int feedIndex, int version, bool fromStart);
    void rescanRequested(bool chooseFolder);
    void identifyRequested();
    void sourceRequested(const QUrl& url);

private:
    struct Waiting {
        quint64 id = 0;
        AddonHostServices::Context context;
        QJsonObject request;
        AddonHostServices::Completion done;
    };
    AddonHostServices::Cancel read(const AddonHostServices::Context&, const QJsonObject&, AddonHostServices::Completion);
    void answer(const AddonHostServices::Context&, const QJsonObject&, const AddonHostServices::Completion&, bool timedOut);
    QString opaque(const QString& package, const QUrl& url) const;
    const Item* find(const Snapshot&, const QString& package, const QString& id) const;
    Source m_source;
    QByteArray m_salt;
    QHash<QString, QHash<QString, QUrl>> m_ids; // package -> id -> url, for the current media
    QHash<QString, QVariantMap> m_artwork;      // package -> media:<id> -> image url
    QList<Waiting> m_waiting;
    quint64 m_nextWait = 0;
    int m_revision = 0;
    int m_artworkRevision = 0;
    std::shared_ptr<bool> m_alive;
};
}
