// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QObject>
#include <QUrl>
#include <QHash>
#include <QSet>
#include <memory>
#include <cstdint>
#include <deque>
#include <atomic>
#include <functional>
#include "util/background_watcher.hpp"
#include <QImage>
#include <QVariantMap>

class BaseAsyncTask;
struct ArtworkResult;

class MainCtx;
class MediaLib;
class MLMedia;
class MLMediaStore;
struct vlc_preparser_t;
struct vlc_preparser_req;
Q_MOC_INCLUDE("maininterface/mainctx.hpp")

/** Lazy file artwork using the media library's persistent thumbnail cache. */
class MLFileArtwork : public QObject
{
    Q_OBJECT
    Q_PROPERTY(MainCtx* ctx READ ctx WRITE setCtx NOTIFY ctxChanged FINAL)
public:
    explicit MLFileArtwork(QObject* parent = nullptr);
    ~MLFileArtwork() override;
    MainCtx* ctx() const { return m_ctx; }
    void setCtx(MainCtx* ctx);
    void setMediaLibrary(MediaLib* library);
    Q_INVOKABLE void request(const QUrl& url, bool generateVideo, const QString& fallback = {});
    Q_INVOKABLE void setSelectedArtwork(const QUrl& url, const QUrl& image);
    Q_INVOKABLE void requestWatchStates(const QList<QUrl>& urls, quint64 serial);
    Q_INVOKABLE void requestAudioMetadata(const QList<QUrl>& urls, quint64 serial);
    Q_INVOKABLE void cancelWatchStates();
    Q_INVOKABLE void clear();
signals:
    void ctxChanged();
    void artworkChanged(const QUrl& url, const QString& artwork);
    void mediaProgressChanged(const QUrl& url, qreal progress);
    void mediaPlayedChanged(const QUrl& url, bool played);
    void watchStatesReady(quint64 serial);
    void audioMetadataReady(quint64 serial);
    void mediaMetadataChanged(const QUrl& url, const QString& title, const QString& artist);
    void artworkResolved(const QUrl& url, const QString& artwork, const QString& source, const QString& reason);
    void selectionFinished(const QUrl& url, bool success);
    void invalidated();
private:
    void runTask(std::function<ArtworkResult()> execute, std::function<void(const ArtworkResult&)> completed, bool persistent = false);
    void discover(const QString& mrl);
    void publish(const QString& mrl, const QString& artwork, const QString& source, const QString& reason = {});
    void advanceFolder(const QString& mrl);
    void resolveParsed(const QString& mrl, int64_t id, quint64 generation, const QString& embedded, const QVariantMap& metadata);
    void generate(const QString& mrl, int64_t id, quint64 generation);
    void requestMetadataBatch(const QList<QUrl>& urls, quint64 serial, bool audio);
    void mediaUpdated(const QString& mrl, const MLMedia& media);
    struct ExportedFile {
        QString path;
        bool imported = false;
        ~ExportedFile();
    };
    struct Work { QString mrl; std::shared_ptr<ExportedFile> file; int64_t id; quint64 generation; bool readMetadata = false; };
    void startNext();
    void finishRequest(const Work& work, const QString& artwork);
    void storeArtwork(const Work& work, const QString& artwork);
    MainCtx* m_ctx = nullptr;
    MediaLib* m_ml = nullptr;
    std::unique_ptr<MLMediaStore> m_store;
    std::unique_ptr<MLMediaStore> m_metadataStore;
    QHash<QString, bool> m_requested;
    QHash<QString, QString> m_preferred;
    QHash<QString, QString> m_completed;
    QHash<QString, QString> m_fallback;
    QHash<QString, QString> m_cached;
    QHash<QString, QVariantMap> m_metadata;
    struct Folder {
        QList<QUrl> children;
        QList<QImage> images;
        QSet<QByteArray> fingerprints;
        QSet<QString> works;
        QString waiting;
        bool busy = false;
    };
    QHash<QString, Folder> m_folders;
    QSet<BaseAsyncTask*> m_tasks;
    QSet<BaseAsyncTask*> m_persistentTasks;
    std::shared_ptr<std::atomic_bool> m_cancelled = std::make_shared<std::atomic_bool>(false);
    // Off the GUI thread: watching a network folder can take seconds.
    BackgroundWatcher m_watcher;
    void watchedChanged(const QString& path);
    QSet<QString> m_watched;
    void watch(const QString& path);
    // Drop one item's results, and its folder's collage, so the next
    // request finds them again. Other items keep theirs.
    void forget(const QString& mrl);
    QSet<QString> m_stateRequested;
    QSet<QString> m_attempted;
    std::deque<Work> m_pending;
    vlc_preparser_t* m_preparser = nullptr;
    vlc_preparser_req* m_request = nullptr;
    std::atomic<quint64> m_generation {0};
};
