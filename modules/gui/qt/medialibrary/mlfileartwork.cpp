// SPDX-License-Identifier: GPL-2.0-or-later
#include "mlfileartwork.hpp"
#include "medialib.hpp"
#include "mlmediastore.hpp"
#include "maininterface/mainctx.hpp"
#include "qt.hpp"
#include "util/artwork_discovery.hpp"
#include "util/artwork_store.hpp"
#include "util/media_identity_vlc.hpp"
#include "util/asynctask.hpp"
#include <QSaveFile>
#include <vlc_preparser.h>
#include <vlc_artwork_policy.h>
#include <vlc_input_item.h>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QUuid>
#include <QTimer>

MLFileArtwork::ExportedFile::~ExportedFile()
{
    if (!imported) QFile::remove(path);
}

struct ArtworkResult {
    QString artwork;
    QString source;
    QString reason;
    QList<QUrl> children;
    QImage image;
    QByteArray fingerprint;
    bool directory = false;
    bool success = false;
};
namespace {
class ArtworkTask : public AsyncTask<ArtworkResult> {
public:
    explicit ArtworkTask(std::function<ArtworkResult()> fn) : executeTask(std::move(fn)) {}
    ArtworkResult execute() override { return executeTask(); }
private:
    std::function<ArtworkResult()> executeTask;
};
QString usableImage(const QString& artwork)
{
    const auto url = QUrl::fromEncoded(artwork.toUtf8());
    if (url.isLocalFile()) return ArtworkStore::read(url.toLocalFile()).isNull() ? QString() : artwork;
    // Existing provider thumbnails are passed through, never downloaded by discovery.
    return url.scheme() == "https" || url.scheme() == "http" ? artwork : QString();
}
}

MLFileArtwork::MLFileArtwork(QObject* parent) : QObject(parent)
{
    const auto invalidate = [this] { clear(); emit invalidated(); };
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, invalidate);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, invalidate);
}

void MLFileArtwork::runTask(std::function<ArtworkResult()> execute, std::function<void(const ArtworkResult&)> completed, bool persistent)
{
    const auto token = m_cancelled;
    auto task = new ArtworkTask([token, persistent, execute = std::move(execute)] {
        return !persistent && token->load() ? ArtworkResult{} : execute();
    });
    m_tasks.insert(task);
    if (persistent) m_persistentTasks.insert(task);
    connect(task, &BaseAsyncTask::result, this, [this, task, token, persistent, completed = std::move(completed)] {
        auto result = task->takeResult();
        m_tasks.remove(task); m_persistentTasks.remove(task); task->abandon();
        if (persistent || !token->load()) completed(result);
    });
    task->start(*QThreadPool::globalInstance());
}

void MLFileArtwork::setSelectedArtwork(const QUrl& url, const QUrl& image)
{
    runTask([url, image] {
        ArtworkResult result; result.success = ArtworkStore::save(url, image); return result;
    }, [this, url](const ArtworkResult& result) {
        const bool success = result.success;
        if (success) { clear(); emit invalidated(); }
        emit selectionFinished(url, success);
    }, true);
}
MLFileArtwork::~MLFileArtwork()
{
    for (auto task : std::as_const(m_persistentTasks)) {
        m_tasks.remove(task); static_cast<ArtworkTask*>(task)->abandon();
    }
    m_persistentTasks.clear();
    clear();
    if (m_preparser) vlc_preparser_Delete(m_preparser);
}

void MLFileArtwork::setCtx(MainCtx* ctx)
{
    if (m_ctx == ctx) return;
    clear();
    if (m_preparser) vlc_preparser_Delete(m_preparser);
    m_preparser = nullptr;
    m_ctx = ctx;
    setMediaLibrary(ctx ? ctx->getMediaLibrary() : nullptr);
    emit ctxChanged();
}

void MLFileArtwork::setMediaLibrary(MediaLib* library)
{
    clear();
    m_store.reset();
    m_metadataStore.reset();
    m_ml = library;
    if (m_ml) {
        m_store = std::make_unique<MLMediaStore>(m_ml);
        m_metadataStore = std::make_unique<MLMediaStore>(m_ml);
        m_store->setReadAudioMetadata(true);
        m_store->setReadIdentityMetadata(true);
        m_metadataStore->setReadAudioMetadata(true);
        connect(m_store.get(), &MLMediaStore::updated, this, &MLFileArtwork::mediaUpdated);
        connect(m_metadataStore.get(), &MLMediaStore::updated, this, [this](const QString& mrl, const MLMedia& media) {
            if (m_stateRequested.contains(mrl)) {
                emit mediaPlayedChanged(QUrl::fromEncoded(mrl.toUtf8()), media.valid() && media.playCount() > 0);
                emit mediaProgressChanged(QUrl::fromEncoded(mrl.toUtf8()), media.valid() ? media.progress() : 0.0);
            }
        });
        for (const auto store : {m_store.get(), m_metadataStore.get()})
            connect(store, &MLMediaStore::audioMetadataUpdated, this,
                [this](const QString& mrl, const QString& title, const QString& artist) {
                    if (m_requested.contains(mrl) || m_stateRequested.contains(mrl))
                        emit mediaMetadataChanged(QUrl::fromEncoded(mrl.toUtf8()), title, artist);
                });
    }
}

void MLFileArtwork::clear()
{
    ++m_generation;
    m_cancelled->store(true);
    m_cancelled = std::make_shared<std::atomic_bool>(false);
    for (auto task : std::as_const(m_tasks))
        if (!m_persistentTasks.contains(task)) static_cast<ArtworkTask*>(task)->abandon();
    m_tasks = m_persistentTasks;
    m_preferred.clear(); m_completed.clear(); m_fallback.clear(); m_cached.clear(); m_metadata.clear(); m_folders.clear();
    const auto watched = m_watcher.directories() + m_watcher.files();
    if (!watched.isEmpty()) m_watcher.removePaths(watched);
    if (m_request) {
        vlc_preparser_Cancel(m_preparser, m_request);
        vlc_preparser_req_Release(m_request);
        m_request = nullptr;
    }
    m_pending.clear();
    m_requested.clear();
    m_stateRequested.clear();
    m_attempted.clear();
    if (m_store) m_store->clear();
    if (m_metadataStore) m_metadataStore->clear();
}

void MLFileArtwork::request(const QUrl& url, bool generateVideo, const QString& fallback)
{
    if (url.scheme().compare(QLatin1String("vlc-addon-source"), Qt::CaseInsensitive) == 0) {
        emit artworkChanged(url, {}); return;
    }
    if (!m_store || (url.scheme() != "file" && url.scheme() != "http" && url.scheme() != "https")) return;
    const auto normalized = ArtworkDiscovery::normalizeUrl(url);
    const auto mrl = normalized.toString(QUrl::FullyEncoded);
    if (!fallback.isEmpty()) m_fallback.insert(mrl, fallback);
    if (m_requested.contains(mrl)) {
        if (m_completed.contains(mrl))
            QTimer::singleShot(0, this, [this, mrl, generation = m_generation.load()] {
                if (generation == m_generation) emit artworkChanged(QUrl(mrl), m_completed.value(mrl));
            });
        return;
    }
    const auto identity = MediaIdentity::identify(normalized);
    m_requested.insert(mrl, generateVideo && !MediaIdentity::isAudio(identity.kind));
    discover(mrl);
}

void MLFileArtwork::discover(const QString& mrl)
{
    const auto token = m_cancelled;
    const auto fallback = m_fallback.value(mrl);
    runTask([mrl, token, fallback] {
        ArtworkResult result;
        const QUrl url(mrl);
        result.directory = QFileInfo(url.toLocalFile()).isDir();
        result.artwork = usableImage(ArtworkStore::selected(url));
        if (!result.artwork.isEmpty()) { result.source = "user"; result.reason = "User-selected image"; return result; }
        for (const auto& candidate : ArtworkDiscovery::local(url, result.directory, {}, ArtworkDiscovery::Role::Primary,
                 [token] { return token->load(); })) {
            // Non-exact ownership is resolved after reading existing library facts and tags.
            if (!result.directory && candidate.score / 100 < 300) continue;
            result.artwork = usableImage(candidate.url.toString(QUrl::FullyEncoded));
            if (!result.artwork.isEmpty()) { result.source = "local"; result.reason = candidate.reason; return result; }
        }
        if (!url.isLocalFile()) { result.artwork = fallback; result.source = "thumbnail"; return result; }
        if (result.directory) {
            const auto children = ArtworkDiscovery::children(url, [token] { return token->load(); }, [token](const QUrl& child) {
                if (token->load()) return false;
                if (!usableImage(ArtworkStore::selected(child)).isEmpty()) return true;
                for (const auto& candidate : ArtworkDiscovery::local(child, true))
                    if (!usableImage(candidate.url.toString(QUrl::FullyEncoded)).isEmpty()) return true;
                return false;
            });
            result.children = children.urls;
            if (children.limited) result.reason = "Preview uses a bounded search of this folder";
        }
        return result;
    }, [this, mrl](const ArtworkResult& result) {
        if (!QUrl(mrl).isLocalFile()) { publish(mrl, result.artwork, result.source); return; }
        const QFileInfo file(QUrl(mrl).toLocalFile());
        const auto watched = result.directory ? file.absoluteFilePath() : file.absolutePath();
        if (!m_watcher.directories().contains(watched)) m_watcher.addPath(watched);
        if (!result.artwork.isEmpty()) {
            const auto image = QUrl::fromEncoded(result.artwork.toUtf8());
            if (image.isLocalFile() && !m_watcher.files().contains(image.toLocalFile())) m_watcher.addPath(image.toLocalFile());
            m_preferred.insert(mrl, result.artwork);
            publish(mrl, result.artwork, result.source, result.reason);
            if (!result.directory) m_store->insert(mrl, true);
        } else if (result.directory) {
            Folder folder; folder.children = result.children;
            m_folders.insert(mrl, folder); advanceFolder(mrl);
        } else m_store->insert(mrl, true);
    });
}

void MLFileArtwork::publish(const QString& mrl, const QString& artwork, const QString& source, const QString& reason)
{
    m_completed.insert(mrl, artwork);
    emit artworkChanged(QUrl(mrl), artwork);
    emit artworkResolved(QUrl(mrl), artwork, source, reason);
    for (const auto& key : m_folders.keys())
        if (m_folders.value(key).waiting == mrl)
            QTimer::singleShot(0, this, [this, key] { advanceFolder(key); });
}

void MLFileArtwork::advanceFolder(const QString& mrl)
{
    auto it = m_folders.find(mrl);
    if (it == m_folders.end() || it->busy) return;
    auto& folder = it.value();
    if (!folder.waiting.isEmpty()) {
        if (!m_completed.contains(folder.waiting)) return;
        const auto artwork = m_completed.value(folder.waiting);
        const auto work = ArtworkDiscovery::workKey(QUrl(folder.waiting), m_metadata.value(folder.waiting));
        folder.waiting.clear();
        if (!work.isEmpty() && folder.works.contains(work)) { advanceFolder(mrl); return; }
        if (!artwork.isEmpty()) {
            folder.busy = true;
            runTask([artwork] {
                ArtworkResult result;
                const auto url = QUrl::fromEncoded(artwork.toUtf8());
                if (url.isLocalFile()) result.image = ArtworkStore::read(url.toLocalFile(), 512);
                if (!result.image.isNull()) {
                    const auto sample = result.image.scaled(32, 32, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_RGBA8888);
                    result.fingerprint = QCryptographicHash::hash(QByteArray(reinterpret_cast<const char*>(sample.constBits()), sample.sizeInBytes()), QCryptographicHash::Sha256);
                }
                return result;
            }, [this, mrl, work](const ArtworkResult& result) {
                auto it = m_folders.find(mrl); if (it == m_folders.end()) return;
                it->busy = false;
                if (!result.image.isNull() && !it->fingerprints.contains(result.fingerprint)) {
                    it->fingerprints.insert(result.fingerprint); it->images.append(result.image);
                    if (!work.isEmpty()) it->works.insert(work);
                }
                advanceFolder(mrl);
            });
            return;
        }
    }
    if (folder.images.size() == 4 || folder.children.isEmpty()) {
        const auto images = folder.images;
        m_folders.erase(it);
        runTask([images, mrl] {
            ArtworkResult result;
            const auto image = ArtworkStore::collage(images);
            if (image.isNull()) return result;
            const auto directory = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(QStringLiteral("folder-collages"));
            if (!QDir().mkpath(directory)) return result;
            const QByteArray bytes = QByteArray(reinterpret_cast<const char*>(image.constBits()), image.sizeInBytes()) + mrl.toUtf8();
            const auto digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
            const auto path = QDir(directory).filePath(QString::fromLatin1(digest) + QStringLiteral(".png"));
            if (!QFile::exists(path)) {
                QSaveFile output(path);
                if (!output.open(QIODevice::WriteOnly) || !image.save(&output, "PNG") || !output.commit()) return result;
            }
            result.artwork = QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded); return result;
        }, [this, mrl](const ArtworkResult& result) { publish(mrl, result.artwork, "children", "Artwork from up to four children"); });
        return;
    }
    const auto child = folder.children.takeFirst();
    folder.waiting = child.toString(QUrl::FullyEncoded);
    if (m_completed.contains(folder.waiting)) { advanceFolder(mrl); return; }
    request(child, true);
}

void MLFileArtwork::mediaUpdated(const QString& mrl, const MLMedia& media)
{
    if (!m_requested.contains(mrl) && !m_stateRequested.contains(mrl)) return;
    emit mediaPlayedChanged(QUrl(mrl), media.valid() && media.playCount() > 0);
    if (!m_requested.contains(mrl)) return;
    emit mediaProgressChanged(QUrl(mrl), media.valid() ? media.progress() : 0.0);
    if (media.valid()) m_metadata.insert(mrl, media.identityMetadata());
    if (m_preferred.contains(mrl) || m_completed.contains(mrl)) return;
    if (!media.valid()) { publish(mrl, {}, "missing"); return; }
    const auto id = media.getId().id;
    if (m_attempted.contains(mrl)) return;
    m_attempted.insert(mrl);
    m_cached.insert(mrl, media.smallCover().isEmpty() ? m_fallback.value(mrl) : media.smallCover());
    if (media.type() == VLC_ML_MEDIA_TYPE_AUDIO) m_requested[mrl] = false;
    if (!m_ctx) { resolveParsed(mrl, id, m_generation.load(), {}, {}); return; }
    m_pending.push_back({mrl, nullptr, id, m_generation.load(), true});
    startNext();
}

void MLFileArtwork::resolveParsed(const QString& mrl, int64_t id, quint64 generation,
                                  const QString& embedded, const QVariantMap& metadata)
{
    const auto cached = m_cached.value(mrl);
    const auto token = m_cancelled;
    auto combinedMetadata = m_metadata.value(mrl);
    for (auto it = metadata.cbegin(); it != metadata.cend(); ++it) combinedMetadata.insert(it.key(), it.value());
    if (metadata.value("type").toString() == "podcastEpisode" && !metadata.contains("year")) combinedMetadata.remove("year");
    runTask([mrl, embedded, metadata = combinedMetadata, cached, token] {
        ArtworkResult result;
        const auto identity = MediaIdentity::identify(QUrl(mrl), metadata);
        for (const auto& candidate : ArtworkDiscovery::local(QUrl(mrl), false, identity, ArtworkDiscovery::Role::Primary,
                 [token] { return token->load(); })) {
            result.artwork = usableImage(candidate.url.toString(QUrl::FullyEncoded));
            if (!result.artwork.isEmpty()) { result.source = "local"; result.reason = candidate.reason; return result; }
        }
        const auto supplied = QUrl::fromEncoded(embedded.toUtf8());
        const auto parent = QFileInfo(QUrl(mrl).toLocalFile()).absolutePath();
        const bool nearby = supplied.isLocalFile() && QFileInfo(supplied.toLocalFile()).absolutePath() == parent;
        // The native local fetcher also finds folder images. Only its exported
        // attachments count as embedded after our sibling ownership checks failed.
        result.artwork = nearby ? QString() : usableImage(embedded);
        if (!result.artwork.isEmpty()) { result.source = "embedded"; result.reason = "Artwork supplied by the media"; return result; }
        result.artwork = usableImage(cached);
        result.source = "thumbnail"; result.reason = "Existing media thumbnail"; return result;
    }, [this, mrl, id, generation](const ArtworkResult& result) {
        if (!result.artwork.isEmpty()) {
            const auto image = QUrl::fromEncoded(result.artwork.toUtf8());
            if (image.isLocalFile() && !m_watcher.files().contains(image.toLocalFile())) m_watcher.addPath(image.toLocalFile());
            publish(mrl, result.artwork, result.source, result.reason);
        } else if (m_requested.value(mrl)) generate(mrl, id, generation);
        else publish(mrl, {}, "missing");
    });
}

void MLFileArtwork::generate(const QString& mrl, int64_t id, quint64 generation)
{
    if (!m_ctx) { publish(mrl, {}, "missing"); return; }
    const auto directory = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath(QStringLiteral("folder-thumbnails"));
    auto file = std::make_shared<ExportedFile>();
    file->path = QDir(directory).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".jpg"));
    const Work work {mrl, file, id, generation};
    runTask([directory] { ArtworkResult result; result.success = QDir().mkpath(directory); return result; },
        [this, work](const ArtworkResult& result) {
            if (result.success) { m_pending.push_back(work); startNext(); }
            else publish(work.mrl, {}, "missing");
        });
}

void MLFileArtwork::requestWatchStates(const QList<QUrl>& urls, quint64 serial)
{
    requestMetadataBatch(urls, serial, false);
}

void MLFileArtwork::requestAudioMetadata(const QList<QUrl>& urls, quint64 serial)
{
    requestMetadataBatch(urls, serial, true);
}

void MLFileArtwork::requestMetadataBatch(const QList<QUrl>& urls, quint64 serial, bool audio)
{
    const auto ready = [this, serial, audio, generation = m_generation.load()] {
        if (generation != m_generation) return;
        if (audio) emit audioMetadataReady(serial);
        else emit watchStatesReady(serial);
    };
    if (!m_metadataStore) {
        QTimer::singleShot(0, this, ready);
        return;
    }
    QStringList mrls;
    mrls.reserve(urls.size());
    for (const auto& url : urls) {
        if (!url.isLocalFile()) continue;
        const auto mrl = url.toString(QUrl::FullyEncoded);
        m_stateRequested.insert(mrl);
        mrls.append(mrl);
    }
    // Keep metadata reads separate from visible artwork lookups and external
    // record creation, so a batch cannot invalidate those in-flight results.
    m_metadataStore->insertMany(mrls, ready);
}

void MLFileArtwork::cancelWatchStates()
{
    m_stateRequested.clear();
    if (m_metadataStore) m_metadataStore->cancelBatch();
}

void MLFileArtwork::startNext()
{
    if (m_request || m_pending.empty() || !m_ctx) return;
    const auto work = m_pending.front();
    msg_Dbg(m_ctx->getIntf(), "Folder preview request: %s", qtu(work.mrl));
    m_pending.pop_front();
    if (!m_preparser) {
        vlc_preparser_cfg cfg {};
        cfg.types = VLC_PREPARSER_TYPE_THUMBNAIL_TO_FILES | VLC_PREPARSER_TYPE_PARSE
                | VLC_PREPARSER_TYPE_FETCHMETA_LOCAL;
        cfg.max_parser_threads = 1;
        cfg.max_thumbnailer_threads = 1;
        cfg.timeout = VLC_TICK_FROM_SEC(15);
        cfg.external_process = false;
        m_preparser = vlc_preparser_New(VLC_OBJECT(m_ctx->getIntf()), &cfg);
    }
    struct Request { MLFileArtwork* owner; Work work; };
    static const vlc_thumbnailer_to_files_cbs callbacks = {
        [](vlc_preparser_req*, int status, const bool* results, size_t count, void* opaque) {
            std::unique_ptr<Request> request(static_cast<Request*>(opaque));
            const bool success = status == VLC_SUCCESS && count == 1 && results && results[0];
            if (!success || request->work.generation != request->owner->m_generation)
                QFile::remove(request->work.file->path);
            if (request->work.generation != request->owner->m_generation) return;
            QMetaObject::invokeMethod(request->owner, [owner = request->owner, work = request->work, success] {
                if (work.generation != owner->m_generation) { QFile::remove(work.file->path); return; }
                owner->finishRequest(work, success
                    ? QUrl::fromLocalFile(work.file->path).toString(QUrl::FullyEncoded) : QString {});
            });
        }
    };
    static const vlc_preparser_cbs audioCallbacks = {
        [](vlc_preparser_req* handle, int status, void* opaque) {
            std::unique_ptr<Request> request(static_cast<Request*>(opaque));
            if (request->work.generation != request->owner->m_generation) return;
            QString artwork, title, artist;
            QVariantMap metadata;
            if (status == VLC_SUCCESS) {
                const auto item = vlc_preparser_req_GetItem(handle);
                metadata = MediaIdentity::inputMetadata(item);
                char* uri = vlc_artwork_item_GetURI(item);
                const auto url = QUrl::fromEncoded(QByteArray(uri ? uri : ""));
                free(uri);
                if (url.isLocalFile()) artwork = url.toString(QUrl::FullyEncoded);
                if (request->work.readMetadata) {
                    char* value = input_item_GetTitle(item);
                    title = qfu(value); free(value);
                    value = input_item_GetArtist(item);
                    artist = qfu(value); free(value);
                }
            }
            QMetaObject::invokeMethod(request->owner,
                [owner = request->owner, work = request->work, artwork, title, artist, metadata, status] {
                    if (work.generation == owner->m_generation) {
                        if (work.readMetadata && status == VLC_SUCCESS)
                            emit owner->mediaMetadataChanged(QUrl::fromEncoded(work.mrl.toUtf8()), title, artist);
                        vlc_preparser_req_Release(owner->m_request);
                        owner->m_request = nullptr;
                        owner->resolveParsed(work.mrl, work.id, work.generation, artwork, metadata);
                        owner->startNext();
                    }
                });
        }, nullptr, nullptr
    };
    input_item_t* item = input_item_New(qtu(work.mrl), nullptr);
    if (m_preparser && item) {
        auto request = std::make_unique<Request>(Request {this, work});
        if (!work.file) {
            // Read tags and nearby cover files without an online artwork search.
            m_request = vlc_preparser_req_NewParse(m_preparser, item,
                VLC_PREPARSER_TYPE_PARSE | VLC_PREPARSER_TYPE_FETCHMETA_LOCAL,
                &audioCallbacks, request.get());
        } else {
            vlc_thumbnailer_arg args {};
            args.seek.type = decltype(args.seek)::VLC_THUMBNAILER_SEEK_POS;
            args.seek.pos = .15;
            args.seek.speed = decltype(args.seek)::VLC_THUMBNAILER_SEEK_FAST;
            vlc_thumbnailer_output output {};
            output.format = VLC_THUMBNAILER_FORMAT_JPEG;
            output.width = 512; output.height = 320; output.crop = true;
            const auto path = work.file->path.toUtf8();
            output.file_path = path.constData(); output.creat_mode = 0600;
            m_request = vlc_preparser_req_NewThumbnailToFiles(m_preparser, item, &args, &output, 1, &callbacks, request.get());
        }
        if (m_request) {
            Request* context = request.release();
            if (vlc_preparser_Submit(m_preparser, m_request) != VLC_SUCCESS) {
                delete context;
                vlc_preparser_req_Release(m_request);
                m_request = nullptr;
            }
        }
    }
    if (item) input_item_Release(item);
    if (!m_request) {
        if (work.file) publish(work.mrl, {}, "missing");
        else resolveParsed(work.mrl, work.id, work.generation, {}, {});
        QTimer::singleShot(0, this, &MLFileArtwork::startNext);
    }
}

void MLFileArtwork::finishRequest(const Work& work, const QString& artwork)
{
    vlc_preparser_req_Release(m_request);
    m_request = nullptr;
    if (!artwork.isEmpty()) storeArtwork(work, artwork);
    else publish(work.mrl, {}, "missing");
    startNext();
}

void MLFileArtwork::storeArtwork(const Work& work, const QString& artwork)
{
    struct Result { QString artwork; };
    const auto token = m_cancelled;
    m_ml->runOnMLThread<Result>(this, [work, artwork, token](vlc_medialibrary_t* ml, Result& result) {
        if (token->load()) return;
        ml_unique_ptr<vlc_ml_media_t> media(vlc_ml_get_media(ml, work.id));
        if (media) {
            // Keep artwork that became available while this decode was running.
            result.artwork = MLMedia(media.get()).smallCover();
            if (result.artwork.isEmpty()) {
                vlc_ml_media_set_thumbnail(ml, work.id, qtu(artwork), VLC_ML_THUMBNAIL_SMALL);
                media.reset(vlc_ml_get_media(ml, work.id));
                if (media) result.artwork = MLMedia(media.get()).smallCover();
            }
        }
        if (work.file)
            work.file->imported = QUrl::fromEncoded(result.artwork.toUtf8()) == QUrl::fromLocalFile(work.file->path);
    }, [this, work](quint64, Result& result) {
        if (!result.artwork.isEmpty() && work.generation == m_generation && m_requested.contains(work.mrl))
            publish(work.mrl, result.artwork, "generated", "Video frame sampled at 15%");
        else if (work.generation == m_generation) publish(work.mrl, {}, "missing");
    });
}
