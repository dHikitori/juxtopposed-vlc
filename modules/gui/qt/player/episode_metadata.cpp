// SPDX-License-Identifier: GPL-2.0-or-later
#include "episode_metadata.hpp"
#include "maininterface/mainctx.hpp"
#include "player_controller.hpp"
#include "addons/addon_metadata_service.hpp"
#include "medialibrary/medialib.hpp"
#include "medialibrary/mlhelper.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QTimer>
#include <vlc_cxx_helpers.hpp>

namespace {
const QByteArray prefix("vlc-episode-v1\n");
struct Title { QUrl url; QString title; QUrl source; };

Title cached(vlc_medialibrary_t* ml, const QUrl& url, const QVariantMap& metadata = {})
{
    Title result {url, {}, {}};
    ml_unique_ptr<vlc_ml_media_t> media(vlc_ml_get_media_by_mrl(ml, url.toEncoded().constData()));
    if (!media || media->p_podcast) return result;
    char* value = nullptr;
    if (vlc_ml_media_get_playback_state(ml, media->i_id, VLC_ML_PLAYBACK_STATE_APP_SPECIFIC, &value) == VLC_SUCCESS && value) {
        const QByteArray bytes(value); free(value);
        if (bytes.startsWith(prefix) && bytes.size() <= 16384) {
            const auto record = QJsonDocument::fromJson(bytes.mid(prefix.size())).object();
            const auto identity = SeriesDiscovery::identify(url, metadata);
            if (record.value("series").toString() == MediaIdentity::normalize(identity.title)
                && record.value("year").toInt(-1) == identity.year
                && record.value("season").toInt(-1) == identity.season
                && record.value("episodes").toArray() == QJsonArray::fromVariantList(MediaIdentity::toMap(identity).value("episodes").toList())) {
                result.title = MediaIdentity::episodeTitle(url, {{"episodeTitle", record.value("title").toString()}}, false);
                const QUrl source(record.value("source").toString());
                if (source.scheme() == "https" && !source.host().isEmpty() && source.userInfo().isEmpty()
                    && source.toEncoded().size() <= 2048) result.source = source;
            }
        }
    }
    if (result.title.isEmpty())
        result.title = MediaIdentity::episodeTitle(url, {{"episodeTitle", QString::fromUtf8(media->psz_title ? media->psz_title : "")}}, false);
    return result;
}
}

EpisodeMetadata::EpisodeMetadata(QObject* parent) : QObject(parent) {}
EpisodeMetadata::~EpisodeMetadata() { cancel(); }
MainCtx* EpisodeMetadata::ctx() const { return m_ctx; }
bool EpisodeMetadata::online() const
{
    if (m_ownedMetadataProfile) return m_sharedMetadata && m_sharedMetadata->automaticNetworkAllowed();
    return m_ctx && var_InheritBool(m_ctx->getIntf(), "metadata-network-access");
}
void EpisodeMetadata::cancel()
{
    ++m_generation; m_busy = false;
    if (m_sharedMetadata && !m_providerRequest.isEmpty()) m_sharedMetadata->cancel(m_providerRequest);
    m_providerRequest.clear();
    if (m_reply) { auto reply = m_reply; m_reply = nullptr; reply->abort(); }
}
void EpisodeMetadata::setCtx(MainCtx* ctx)
{
    if (m_ctx == ctx) return;
    cancel();
    if (m_ctx) disconnect(m_ctx->getIntf()->p_mainPlayerController, nullptr, this, nullptr);
    m_ctx = ctx;
    if (m_ctx) connect(m_ctx->getIntf()->p_mainPlayerController, &PlayerController::metadataNetworkAccessChanged,
        this, [this](bool enabled) {
            cancel();
            if (enabled) {
                m_attemptedSeasons.clear(); m_searchAttempted = false;
                m_attemptedProviders.clear();
            }
            readCached(m_candidates.values());
        });
    if (m_ctx) {
        auto* player = m_ctx->getIntf()->p_mainPlayerController;
        setMetadataService(player->metadataService());
        connect(player, &PlayerController::metadataServiceChanged, this, [this, player] { setMetadataService(player->metadataService()); });
    } else setMetadataService(nullptr);
    emit ctxChanged();
}
void EpisodeMetadata::setMetadataService(AddonMetadataService* service)
{
    if (m_sharedMetadata == service) return;
    cancel();
    if (m_sharedMetadata) disconnect(m_sharedMetadata, nullptr, this, nullptr);
    m_sharedMetadata = service;
    m_attemptedProviders.clear();
    if (service) {
        m_ownedMetadataProfile = true;
        connect(service, &AddonMetadataService::finished, this, &EpisodeMetadata::providerFinished);
        connect(service, &AddonMetadataService::canceled, this, [this](const QString& consumer) {
            if (consumer != m_providerRequest) return;
            m_providerRequest.clear(); m_busy = false;
        });
        connect(service, &AddonMetadataService::invalidated, this, [this] {
            m_attemptedProviders.clear(); schedule();
        });
    }
    schedule();
}
MediaIdentity::Identity EpisodeMetadata::identity(const QUrl& url) const
{
    return SeriesDiscovery::identify(url, url == m_current ? m_metadata : QVariantMap());
}
void EpisodeMetadata::readCached(const QList<QUrl>& urls)
{
    // Discovery already bounds the directory scan. Limit retained lookup targets
    // as well; no online scan or media-file preparse is needed for siblings.
    for (const auto& url : urls) {
        if (m_candidates.size() >= 2048) break;
        if (url.isLocalFile() && SeriesDiscovery::sameSeries(identity(url), identity(m_current)))
            m_candidates.insert(url);
    }
    if (!m_ctx || !m_ctx->getMediaLibrary()) { applyTitles(); schedule(); return; }
    const auto batch = urls.mid(0, 64);
    const auto remaining = urls.mid(batch.size());
    ++m_cacheReads;
    const auto generation = m_generation;
    const auto current = m_current; const auto metadata = m_metadata;
    m_ctx->getMediaLibrary()->runOnMLThread<QList<Title>>(this,
        [batch, current, metadata](vlc_medialibrary_t* ml, QList<Title>& result) {
            for (const auto& url : batch) if (url.isLocalFile())
                result.append(cached(ml, url, url == current ? metadata : QVariantMap()));
        }, [this, generation, remaining](quint64, const QList<Title>& result) {
            --m_cacheReads;
            if (generation != m_generation) { schedule(); return; }
            for (const auto& entry : result) {
                if (!m_candidates.contains(entry.url) || m_resolved.contains(entry.url) || entry.title.isEmpty()) continue;
                m_resolved.insert(entry.url); emit titleReady(entry.url, entry.title, entry.source);
            }
            if (!remaining.isEmpty()) readCached(remaining);
            schedule();
        }, "episode-metadata");
}
void EpisodeMetadata::request(const QUrl& url, const QVariantMap& metadata)
{
    const auto previous = identity(m_current);
    const auto next = SeriesDiscovery::identify(url, metadata);
    // Duration, description and generated-title updates can arrive while the
    // request is in flight. Keep it when the identifying evidence is unchanged.
    const bool unchanged = url == m_current && MediaIdentity::toMap(previous) == MediaIdentity::toMap(next);
    const bool sameSeries = SeriesDiscovery::sameSeries(previous, next);
    m_metadata = metadata;
    if (!unchanged) {
        cancel(); m_current = url;
        if (!sameSeries) m_candidates.clear();
        m_resolved.remove(url);
        if (!sameSeries || previous.year != next.year || previous.ids != next.ids) {
            m_resolved.clear(); m_titles.clear(); m_seasonIds.clear(); m_attemptedProviders.clear();
            m_attemptedSeasons.clear(); m_showId = 0; m_seasonsRead = false; m_searchAttempted = false;
        }
    }
    if (!url.isLocalFile() || next.kind != MediaIdentity::Kind::Episode) return;
    m_candidates.insert(url);
    const auto title = MediaIdentity::episodeTitle(url, metadata);
    if (!title.isEmpty()) {
        m_resolved.insert(url); emit titleReady(url, title, {}); save(url, metadata, title, {});
    }
    if (!unchanged) readCached(m_candidates.values());
    else { applyTitles(); schedule(); }
}
void EpisodeMetadata::schedule()
{
    QTimer::singleShot(0, this, &EpisodeMetadata::lookup);
}
void EpisodeMetadata::applyTitles()
{
    for (const auto& url : m_candidates) {
        if (m_resolved.contains(url)) continue;
        const auto target = identity(url);
        if (!SeriesDiscovery::sameSeries(target, identity(m_current))) continue;
        const auto metadata = url == m_current ? m_metadata : QVariantMap();
        const auto local = MediaIdentity::episodeTitle(url, metadata);
        if (!local.isEmpty()) {
            m_resolved.insert(url); emit titleReady(url, local, {}); save(url, metadata, local, {});
            continue;
        }
        if (m_cacheReads || target.episodes.size() != 1 || !target.conflicts.isEmpty()) continue;
        const auto title = m_titles.value(target.season).value(target.episodes.first());
        if (title.title.isEmpty()) continue;
        m_resolved.insert(url); emit titleReady(url, title.title, title.source);
        save(url, metadata, title.title, title.source);
    }
}
void EpisodeMetadata::save(const QUrl& url, const QVariantMap& metadata, const QString& title, const QUrl& source)
{
    if (!m_ctx || !m_ctx->getMediaLibrary()) return;
    const auto identity = SeriesDiscovery::identify(url, metadata);
    const QJsonObject record {{"series", MediaIdentity::normalize(identity.title)}, {"year", identity.year}, {"season", identity.season},
        {"episodes", QJsonArray::fromVariantList(MediaIdentity::toMap(identity).value("episodes").toList())},
        {"title", title}, {"source", source.toString()}};
    const QByteArray bytes = prefix + QJsonDocument(record).toJson(QJsonDocument::Compact);
    m_ctx->getMediaLibrary()->runOnMLThread<bool>(this,
        [url, bytes](vlc_medialibrary_t* ml, bool& saved) {
            ml_unique_ptr<vlc_ml_media_t> media(vlc_ml_get_media_by_mrl(ml, url.toEncoded().constData()));
            if (!media) media.reset(vlc_ml_new_external_media(ml, url.toEncoded().constData()));
            if (!media || media->p_podcast) return;
            char* value = nullptr;
            if (vlc_ml_media_get_playback_state(ml, media->i_id, VLC_ML_PLAYBACK_STATE_APP_SPECIFIC, &value) != VLC_SUCCESS) return;
            const QByteArray previous(value ? value : ""); free(value);
            // Do not replace podcast state or another application's data.
            if (!previous.isEmpty() && !previous.startsWith(prefix)) return;
            if (previous == bytes) { saved = true; return; }
            saved = vlc_ml_media_set_playback_state(ml, media->i_id, VLC_ML_PLAYBACK_STATE_APP_SPECIFIC, bytes.constData()) == VLC_SUCCESS;
        }, [](quint64, bool) {}, "episode-metadata");
}
void EpisodeMetadata::get(const QUrl& url, quint64 generation, std::function<void(const QJsonDocument&)> completed)
{
    if (generation != m_generation || !online()) return;
    // One small request at a time; rapid episode changes cannot flood the API.
    const auto delay = m_lastRequest.isValid() ? std::max<qint64>(0, 600 - m_lastRequest.elapsed()) : 0;
    if (delay) {
        QTimer::singleShot(int(delay), this, [this, url, generation, completed] { get(url, generation, completed); });
        return;
    }
    m_lastRequest.start();
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "VLC-Juxtopposed/4.0 (episode titles)");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    request.setMaximumRedirectsAllowed(2); request.setTransferTimeout(8000);
    auto* reply = m_network.get(request); m_reply = reply;
    reply->setReadBufferSize(64 * 1024);
    QTimer::singleShot(8000, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
    auto bytes = std::make_shared<QByteArray>();
    connect(reply, &QNetworkReply::readyRead, this, [reply, bytes] {
        bytes->append(reply->readAll()); if (bytes->size() > 512 * 1024) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, bytes, generation, completed] {
        if (reply->isReadable()) bytes->append(reply->readAll());
        const bool valid = generation == m_generation && online() && bytes->size() <= 512 * 1024
            && reply->error() == QNetworkReply::NoError && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        if (m_reply == reply) m_reply = nullptr;
        reply->deleteLater();
        if (generation != m_generation || !online()) return;
        if (!valid && m_ctx)
            msg_Warn(m_ctx->getIntf(), "episode title request failed (%d): %s",
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), qUtf8Printable(reply->errorString()));
        completed(valid ? QJsonDocument::fromJson(*bytes) : QJsonDocument());
    });
}
void EpisodeMetadata::lookup()
{
    if (m_busy || m_cacheReads || !online() || !m_current.isLocalFile()) return;
    applyTitles();
    if (m_ownedMetadataProfile) { if (m_sharedMetadata) lookupProvider(); return; }
    const auto current = identity(m_current);
    if (current.kind != MediaIdentity::Kind::Episode || current.title.isEmpty() || !current.conflicts.isEmpty()) return;
    int season = -1;
    // Prioritize playback, then finish the discovered seasons in numeric order.
    for (const auto& url : m_candidates) {
        if (m_resolved.contains(url)) continue;
        const auto target = identity(url);
        if (!SeriesDiscovery::sameSeries(current, target) || target.season < 1 || target.episodes.size() != 1
            || target.episodes.first() < 1 || !target.conflicts.isEmpty() || m_attemptedSeasons.contains(target.season)) continue;
        if (url == m_current) { season = target.season; break; }
        if (season < 0 || target.season < season) season = target.season;
    }
    if (season < 0) return;
    const auto generation = m_generation;
    m_busy = true;
    if (m_showId > 0) { fetchSeason(generation, season); return; }
    if (m_searchAttempted) { m_busy = false; return; }
    QUrl search("https://api.tvmaze.com/search/shows"); QUrlQuery query; query.addQueryItem("q", current.title); search.setQuery(query);
    get(search, generation, [this, generation, current, season](const QJsonDocument& document) {
        m_searchAttempted = true;
        QJsonObject match;
        for (const auto& value : document.array()) {
            const auto show = value.toObject().value("show").toObject();
            if (MediaIdentity::normalize(show.value("name").toString()) != MediaIdentity::normalize(current.title)) continue;
            if (current.year > 0 && QDate::fromString(show.value("premiered").toString(), Qt::ISODate).year() != current.year) continue;
            if (!match.isEmpty()) { m_busy = false; return; }
            match = show;
        }
        m_showId = match.value("id").toInt();
        if (m_showId <= 0) { m_busy = false; return; }
        fetchSeason(generation, season);
    });
}
void EpisodeMetadata::lookupProvider()
{
    if (!m_sharedMetadata) return;
    const auto current = identity(m_current);
    auto targets = m_candidates.values();
    std::sort(targets.begin(), targets.end(), [this](const QUrl& a, const QUrl& b) {
        if (a == m_current || b == m_current) return a == m_current && b != m_current;
        const auto left = identity(a), right = identity(b);
        if (left.season != right.season) return left.season < right.season;
        if (left.episodes != right.episodes) return left.episodes < right.episodes;
        return a.toEncoded() < b.toEncoded();
    });
    for (const auto& url : targets) {
        if (m_resolved.contains(url) || m_attemptedProviders.contains(url)) continue;
        const auto target = identity(url);
        if (!SeriesDiscovery::sameSeries(current, target) || target.season < 1 || target.episodes.size() != 1
            || target.episodes.first() < 1 || !target.conflicts.isEmpty()) continue;
        m_attemptedProviders.insert(url);
        m_providerIdentity = MediaIdentity::toMap(target);
        m_providerTarget = url;
        const auto media = m_sharedMetadata->acquireMedia(url, m_providerIdentity);
        if (media.isEmpty()) continue;
        m_busy = true;
        m_providerRequest = m_sharedMetadata->enrich(QStringLiteral("video.details"), {
            {QStringLiteral("media"), media}, {QStringLiteral("automatic"), true},
            {QStringLiteral("generation"), QVariant::fromValue(m_generation)},
            {QStringLiteral("targetRevision"), QVariant::fromValue(m_generation)}}, {QStringLiteral("episodeTitle")});
        m_sharedMetadata->releaseMedia(media);
        return;
    }
}
void EpisodeMetadata::providerFinished(const QString& consumer, const QString& status,
                                       const QVariantMap& data, const QString&)
{
    if (consumer != m_providerRequest || data.value(QStringLiteral("contextGeneration")).toULongLong() != m_generation) return;
    m_providerRequest.clear(); m_busy = false;
    const auto url = m_providerTarget;
    const auto target = identity(url);
    if (!online() || !m_candidates.contains(url) || MediaIdentity::toMap(target) != m_providerIdentity) return;
    const auto metadata = url == m_current ? m_metadata : QVariantMap{};
    if (!MediaIdentity::episodeTitle(url, metadata).isEmpty()) { applyTitles(); schedule(); return; }
    if (status == QStringLiteral("match") || status == QStringLiteral("partial")) {
        const auto fields = data.value(QStringLiteral("fields")).toMap();
        const auto title = MediaIdentity::episodeTitle(url, {{QStringLiteral("episodeTitle"), fields.value(QStringLiteral("episodeTitle"))}}, false);
        const auto credit = data.value(QStringLiteral("provenance")).toMap().value(QStringLiteral("episodeTitle")).toMap()
            .value(QStringLiteral("attribution")).toMap();
        const QUrl source(credit.value(QStringLiteral("url")).toString());
        if (!title.isEmpty()) {
            m_titles[target.season].insert(target.episodes.first(), {title, source});
            applyTitles();
        }
    }
    schedule();
}
void EpisodeMetadata::fetchSeason(quint64 generation, int season)
{
    if (!m_seasonsRead) {
        get(QUrl(QStringLiteral("https://api.tvmaze.com/shows/%1/seasons").arg(m_showId)), generation,
            [this, generation, season](const QJsonDocument& document) {
                m_seasonsRead = document.isArray();
                if (!m_seasonsRead) { m_attemptedSeasons.insert(season); m_busy = false; schedule(); return; }
                QSet<int> duplicates;
                for (const auto& value : document.array()) {
                    const auto object = value.toObject();
                    const int number = object.value("number").toInt(-1), id = object.value("id").toInt();
                    if (number < 1 || id <= 0 || duplicates.contains(number)) continue;
                    if (m_seasonIds.contains(number)) { m_seasonIds.remove(number); duplicates.insert(number); }
                    else m_seasonIds.insert(number, id);
                }
                fetchSeason(generation, season);
            });
        return;
    }
    const auto id = m_seasonIds.value(season);
    if (id <= 0) { m_attemptedSeasons.insert(season); m_busy = false; schedule(); return; }
    get(QUrl(QStringLiteral("https://api.tvmaze.com/seasons/%1/episodes").arg(id)), generation,
        [this, season](const QJsonDocument& document) {
            QMap<int, EpisodeTitle> titles;
            QSet<int> duplicates;
            for (const auto& value : document.array()) {
                const auto episode = value.toObject();
                const int number = episode.value("number").toInt(-1);
                if (episode.value("season").toInt(-1) != season || number < 1 || duplicates.contains(number)) continue;
                const auto name = MediaIdentity::episodeTitle(m_current, {{"episodeTitle", episode.value("name").toString()}}, false);
                const QUrl source(episode.value("url").toString());
                if (name.isEmpty() || source.scheme() != "https" || source.host() != "www.tvmaze.com") continue;
                if (titles.contains(number)) { titles.remove(number); duplicates.insert(number); }
                else titles.insert(number, {name, source});
            }
            m_titles.insert(season, titles); m_attemptedSeasons.insert(season); m_busy = false;
            applyTitles(); schedule();
        });
}
