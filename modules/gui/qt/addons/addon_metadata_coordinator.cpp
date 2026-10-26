// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_metadata_coordinator.hpp"
#include "util/media_identity.hpp"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <algorithm>

namespace {
constexpr int MaxConsumers = 64;
constexpr int MaxProviders = 16;
constexpr int MaxCandidates = 500;
constexpr int MaxCache = 128;

bool populated(const QVariant& value)
{
    return value.isValid() && !value.isNull()
        && (value.metaType().id() != QMetaType::QString || !value.toString().isEmpty());
}

QVariantMap normalizedIdentity(QVariantMap identity)
{
    // Keep the transport schema explicit: an accidental nested local URL or
    // caller-only property must not reach a provider through identity context.
    const QSet<QString> allowed{QStringLiteral("kind"), QStringLiteral("type"), QStringLiteral("title"),
        QStringLiteral("titles"), QStringLiteral("aliases"), QStringLiteral("year"), QStringLiteral("season"),
        QStringLiteral("episodes"), QStringLiteral("episode"), QStringLiteral("absoluteEpisode"), QStringLiteral("airDate"),
        QStringLiteral("ids"), QStringLiteral("externalIds"), QStringLiteral("release"), QStringLiteral("provenance"),
        QStringLiteral("conflicts"), QStringLiteral("artist"), QStringLiteral("albumArtist"), QStringLiteral("album"),
        QStringLiteral("series"), QStringLiteral("guid"), QStringLiteral("feedUrl"), QStringLiteral("track"), QStringLiteral("disc")};
    for (auto it = identity.begin(); it != identity.end();)
        if (!allowed.contains(it.key())) it = identity.erase(it); else ++it;
    const auto kind = identity.value(QStringLiteral("kind"), identity.value(QStringLiteral("type"))).toString();
    identity.insert(QStringLiteral("type"), kind);
    identity.insert(QStringLiteral("kind"), kind);
    if (identity.value(QStringLiteral("title")).toString().isEmpty()) {
        const auto titles = identity.value(QStringLiteral("titles")).toList();
        if (!titles.isEmpty()) identity.insert(QStringLiteral("title"), titles.first());
    }
    const auto numbering = identity.value(QStringLiteral("episode")).toMap();
    if (numbering.value(QStringLiteral("scheme")).toString() == QStringLiteral("seasonal")) {
        if (!identity.contains(QStringLiteral("season"))) identity.insert(QStringLiteral("season"), numbering.value(QStringLiteral("season")));
        if (!identity.contains(QStringLiteral("episodes"))) identity.insert(QStringLiteral("episodes"), numbering.value(QStringLiteral("episodes")));
    }
    auto ids = identity.value(QStringLiteral("ids")).toMap();
    for (const auto& entry : identity.value(QStringLiteral("externalIds")).toList()) {
        const auto id = entry.toMap();
        const auto space = id.value(QStringLiteral("namespace")).toString();
        const auto scope = id.value(QStringLiteral("scope")).toString();
        QString key;
        if (space == QStringLiteral("imdb")) {
            if (scope == QStringLiteral("movie")) key = QStringLiteral("imdb");
            else if (scope == QStringLiteral("series")) key = QStringLiteral("seriesImdb");
            else if (scope == QStringLiteral("episode")) key = QStringLiteral("episodeImdb");
        } else if (space == QStringLiteral("tmdb")) {
            if (scope == QStringLiteral("movie")) key = QStringLiteral("tmdbMovie");
            else if (scope == QStringLiteral("series")) key = QStringLiteral("seriesTmdb");
            else if (scope == QStringLiteral("episode")) key = QStringLiteral("tmdbEpisode");
        }
        if (!key.isEmpty() && !ids.contains(key)) ids.insert(key, id.value(QStringLiteral("value")));
    }
    identity.insert(QStringLiteral("ids"), ids);
    return identity;
}

MediaIdentity::Comparison compare(const QVariantMap& target, const QVariantMap& candidate)
{
    return MediaIdentity::compare(MediaIdentity::fromMap(normalizedIdentity(target)),
                                 MediaIdentity::fromMap(normalizedIdentity(candidate)));
}

QString keyFor(const QString& provider, const QString& operation, const QVariantMap& context,
               const QString& partition, quint64 policy)
{
    const QVariantMap key{{QStringLiteral("provider"), provider}, {QStringLiteral("operation"), operation},
        {QStringLiteral("context"), context}, {QStringLiteral("partition"), partition},
        {QStringLiteral("policy"), QVariant::fromValue(policy)}};
    return QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(QJsonObject::fromVariantMap(key)).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}

int statusWeight(const QString& status)
{
    if (status == QStringLiteral("auth_required")) return 5;
    if (status == QStringLiteral("rate_limited")) return 4;
    if (status == QStringLiteral("error")) return 3;
    if (status == QStringLiteral("offline")) return 2;
    if (status == QStringLiteral("unsupported")) return 1;
    return 0;
}
}

AddonMetadataCoordinator::AddonMetadataCoordinator(Backend backend, QObject* parent)
    : QObject(parent), m_backend(std::move(backend))
{
    m_clock.start();
}

AddonMetadataCoordinator::~AddonMetadataCoordinator()
{
    for (const auto& job : m_jobs)
        if (!job.invocation.isEmpty() && m_backend.cancel) m_backend.cancel(job.invocation);
}

QString AddonMetadataCoordinator::searchAssets(const QString& feature, const QVariantMap& context, bool aggregate)
{
    return start(feature, QStringLiteral("searchAssets"), context, {}, aggregate);
}

QString AddonMetadataCoordinator::enrich(const QString& feature, const QVariantMap& context, const QStringList& fields)
{
    return start(feature, QStringLiteral("enrich"), context, fields, false);
}

QString AddonMetadataCoordinator::start(const QString& feature, const QString& operation,
                                       const QVariantMap& context, const QStringList& fields, bool aggregate)
{
    const auto id = QStringLiteral("metadata-%1").arg(++m_sequence);
    if (feature.isEmpty() || feature.size() > 128 || fields.size() > 64 || m_consumers.size() >= MaxConsumers) {
        QTimer::singleShot(0, this, [this, id] { emit finished(id, QStringLiteral("error"), {}, tr("The metadata request exceeds its limits.")); });
        return id;
    }
    Consumer consumer;
    consumer.feature = feature;
    consumer.operation = operation;
    consumer.context = context;
    consumer.context.insert(QStringLiteral("identity"), normalizedIdentity(context.value(QStringLiteral("identity")).toMap()));
    consumer.required = fields;
    consumer.required.removeDuplicates();
    consumer.aggregate = aggregate;
    const auto registrations = m_backend.providersForFeature ? m_backend.providersForFeature(feature) : QVariantList{};
    QHash<QString, QVariantMap> eligible;
    QStringList defaults;
    const auto kind = consumer.context.value(QStringLiteral("identity")).toMap().value(QStringLiteral("kind")).toString();
    for (const auto& registration : registrations) {
        const auto provider = registration.toMap();
        const auto providerId = provider.value(QStringLiteral("id")).toString();
        if (providerId.isEmpty() || eligible.contains(providerId) || !provider.value(QStringLiteral("enabled")).toBool()) continue;
        const auto kinds = provider.value(QStringLiteral("mediaKinds")).toStringList();
        if (!kinds.isEmpty() && !kinds.contains(kind)) continue;
        if (!provider.value(QStringLiteral("configured")).toBool()) {
            consumer.status = QStringLiteral("auth_required");
            consumer.error = tr("Set up a metadata provider in Add-on settings.");
            continue;
        }
        eligible.insert(providerId, provider);
        defaults.append(providerId);
    }
    const auto language = consumer.context.value(QStringLiteral("language")).toString();
    auto order = m_order.value(feature + '/' + kind + '/' + language,
        m_order.value(feature + '/' + kind, m_order.value(feature)));
    for (const auto& providerId : defaults) if (!order.contains(providerId)) order.append(providerId);
    for (const auto& providerId : order) {
        if (eligible.contains(providerId)) consumer.providers.append(eligible.take(providerId));
        if (consumer.providers.size() >= MaxProviders) break;
    }
    m_consumers.insert(id, consumer);
    const int timeout = std::clamp(context.value(QStringLiteral("timeoutMs"), 15000).toInt(), 100, 30000);
    QTimer::singleShot(timeout, this, [this, id] {
        const auto it = m_consumers.find(id);
        if (it == m_consumers.end()) return;
        it->status = QStringLiteral("error");
        it->error = tr("The metadata lookup timed out.");
        finish(id);
    });
    QTimer::singleShot(0, this, [this, id] { advance(id); });
    return id;
}

QStringList AddonMetadataCoordinator::missing(const Consumer& consumer) const
{
    QStringList result;
    const auto existing = consumer.context.value(QStringLiteral("existingFields")).toMap();
    const bool refresh = consumer.context.value(QStringLiteral("refresh")).toBool();
    const auto protectedFields = consumer.context.value(QStringLiteral("protectedFields")).toStringList();
    const auto userFields = consumer.context.value(QStringLiteral("userFields")).toMap();
    for (const auto& field : consumer.required) {
        if (protectedFields.contains(field) || userFields.contains(field) || populated(consumer.fields.value(field))) continue;
        if (!refresh && populated(existing.value(field))) continue;
        result.append(field);
    }
    return result;
}

QVariantMap AddonMetadataCoordinator::providerContext(const Consumer& consumer) const
{
    QVariantMap projection;
    // No target URL, local path, core settings, live-item pointer or protected
    // user data crosses this boundary. The manager applies manifest grants too.
    for (const auto& key : {QStringLiteral("identity"), QStringLiteral("language"), QStringLiteral("locale"),
            QStringLiteral("query"), QStringLiteral("manual"), QStringLiteral("cursor"), QStringLiteral("fingerprints")})
        if (consumer.context.contains(key)) projection.insert(key, consumer.context.value(key));
    projection.insert(QStringLiteral("feature"), consumer.feature);
    if (consumer.operation == QStringLiteral("enrich")) projection.insert(QStringLiteral("fields"), missing(consumer));
    return projection;
}

void AddonMetadataCoordinator::advance(const QString& consumerId)
{
    auto it = m_consumers.find(consumerId);
    if (it == m_consumers.end()) return;
    if ((it->operation == QStringLiteral("enrich") && missing(*it).isEmpty())
        || (!it->aggregate && it->operation == QStringLiteral("searchAssets") && !it->candidates.isEmpty())) {
        finish(consumerId); return;
    }
    const int concurrency = it->aggregate ? 4 : 1;
    while ((it = m_consumers.find(consumerId)) != m_consumers.end()
           && it->next < it->providers.size() && it->outstanding < concurrency) {
        const auto registration = it->providers.at(it->next++).toMap();
        subscribe(consumerId, registration);
    }
    it = m_consumers.find(consumerId);
    if (it != m_consumers.end() && it->outstanding == 0 && it->next >= it->providers.size()) finish(consumerId);
}

void AddonMetadataCoordinator::subscribe(const QString& consumerId, const QVariantMap& registration)
{
    auto it = m_consumers.find(consumerId);
    if (it == m_consumers.end()) return;
    const auto provider = registration.value(QStringLiteral("id")).toString();
    const auto context = providerContext(*it);
    const auto partition = registration.value(QStringLiteral("cachePartition")).toString();
    // Missing release/account/config identity must disable reuse, not create a
    // shared anonymous cache partition across account changes.
    const auto key = keyFor(provider, it->operation, context, partition, m_policy)
        + (partition.isEmpty() ? consumerId : QString{});
    ++it->outstanding;
    it->pending.insert(key);
    const auto cached = m_cache.constFind(key);
    const auto blocked = m_backoff.constFind(provider + '\n' + partition);
    const bool waiting = !partition.isEmpty() && blocked != m_backoff.cend() && blocked->expires > m_clock.elapsed();
    if (waiting || (cached != m_cache.cend() && cached->expires > m_clock.elapsed())) {
        const auto value = waiting ? *blocked : *cached;
        QTimer::singleShot(0, this, [this, consumerId, key, registration, value] {
            auto it = m_consumers.find(consumerId);
            if (it == m_consumers.end() || !it->pending.remove(key)) return;
            --it->outstanding;
            consume(consumerId, registration, value.status, value.data, value.error);
            advance(consumerId);
        });
        return;
    }
    auto existing = m_jobs.find(key);
    if (existing != m_jobs.end()) { existing->waiters.insert(consumerId); return; }
    Job job;
    job.provider = provider; job.operation = it->operation; job.context = context; job.registration = registration;
    job.waiters.insert(consumerId);
    m_jobs.insert(key, job);
    QTimer::singleShot(0, this, [this, key] {
        auto job = m_jobs.find(key);
        if (job == m_jobs.end()) return;
        const auto provider = job->provider, operation = job->operation;
        const auto context = job->context;
        const auto invocation = m_backend.invoke ? m_backend.invoke(provider, operation, context) : QString{};
        job = m_jobs.find(key);
        if (job == m_jobs.end()) { if (!invocation.isEmpty() && m_backend.cancel) m_backend.cancel(invocation); return; }
        if (invocation.isEmpty() || m_invocations.contains(invocation)) {
            const auto waiters = job->waiters;
            const auto registration = job->registration;
            m_jobs.erase(job);
            for (const auto& waiter : waiters) {
                auto consumer = m_consumers.find(waiter);
                if (consumer == m_consumers.end() || !consumer->pending.remove(key)) continue;
                --consumer->outstanding;
                consume(waiter, registration, QStringLiteral("error"), {}, tr("The provider could not start."));
                advance(waiter);
            }
            return;
        }
        job->invocation = invocation;
        m_invocations.insert(invocation, key);
    });
}

void AddonMetadataCoordinator::operationFinished(const QString& invocation, const QString& contribution,
                                                const QString& status, const QVariantMap& data, const QString& error)
{
    // Even a synchronous backend rejection is processed only after invoke()
    // returns its correlation ID and subscribe() records it.
    QTimer::singleShot(0, this, [this, invocation, contribution, status, data, error] {
        complete(invocation, contribution, status, data, error);
    });
}

void AddonMetadataCoordinator::complete(const QString& invocation, const QString& contribution,
                                       const QString& status, const QVariantMap& data, const QString& error)
{
    const auto index = m_invocations.constFind(invocation);
    if (index == m_invocations.cend()) return;
    const auto key = *index;
    const auto found = m_jobs.constFind(key);
    if (found == m_jobs.cend() || found->provider != contribution) return;
    const auto job = *found;
    m_invocations.remove(invocation);
    m_jobs.remove(key);
    const auto partition = job.registration.value(QStringLiteral("cachePartition")).toString();
    const auto backoffKey = job.provider + '\n' + partition;
    if (!partition.isEmpty() && (status == QStringLiteral("rate_limited") || status == QStringLiteral("auth_required")
            || status == QStringLiteral("error") || status == QStringLiteral("offline"))) {
        const int seconds = status == QStringLiteral("rate_limited")
            ? std::clamp(data.value(QStringLiteral("retryAfterSeconds"), 60).toInt(), 1, 86400)
            : status == QStringLiteral("auth_required") ? 30 : 2;
        if (m_backoff.size() >= MaxCache) m_backoff.clear();
        m_backoff.insert(backoffKey, {status, data, error.left(512), m_clock.elapsed() + qint64(seconds) * 1000});
    }
    if (status == QStringLiteral("match") || status == QStringLiteral("partial") || status == QStringLiteral("no_match")) {
        m_backoff.remove(backoffKey);
        if (m_cache.size() >= MaxCache) m_cache.clear();
        m_cache.insert(key, {status, data, {}, m_clock.elapsed() + (status == QStringLiteral("no_match") ? 15000 : 60000)});
    }
    for (const auto& waiter : job.waiters) {
        auto consumer = m_consumers.find(waiter);
        if (consumer == m_consumers.end() || !consumer->pending.remove(key)) continue;
        --consumer->outstanding;
        consume(waiter, job.registration, status, data, error);
        advance(waiter);
    }
}

void AddonMetadataCoordinator::consume(const QString& consumerId, const QVariantMap& registration,
                                      const QString& status, const QVariantMap& data, const QString& error)
{
    auto it = m_consumers.find(consumerId);
    if (it == m_consumers.end()) return;
    if (statusWeight(status) > statusWeight(it->status)) { it->status = status; it->error = error.left(512); }
    if (status == QStringLiteral("ambiguous")) it->ambiguous = true;
    if (status != QStringLiteral("match") && status != QStringLiteral("partial")) return;
    const auto target = it->context.value(QStringLiteral("identity")).toMap();
    const bool manual = it->context.value(QStringLiteral("manual")).toBool()
        || (it->operation == QStringLiteral("searchAssets") && it->context.value(QStringLiteral("interactive")).toBool());
    const auto provider = registration.value(QStringLiteral("id")).toString();
    const QVariantMap provenance{{QStringLiteral("provider"), provider}, {QStringLiteral("feature"), it->feature},
        {QStringLiteral("cachePartition"), registration.value(QStringLiteral("cachePartition"))}};
    if (it->operation == QStringLiteral("searchAssets")) {
        for (const auto& value : data.value(QStringLiteral("candidates")).toList()) {
            if (it->candidates.size() >= MaxCandidates) break;
            auto candidate = value.toMap();
            if (candidate.value(QStringLiteral("id")).toString().isEmpty()) continue;
            const auto match = compare(target, candidate.value(QStringLiteral("identity")).toMap());
            if (!manual && (match.conflict || !match.strong)) { if (!match.conflict) it->ambiguous = true; continue; }
            candidate.insert(QStringLiteral("provider"), provider);
            candidate.insert(QStringLiteral("provenance"), provenance);
            candidate.insert(QStringLiteral("hostMatch"), QVariantMap{{QStringLiteral("score"), match.score},
                {QStringLiteral("strong"), match.strong}, {QStringLiteral("conflicts"), match.conflicts},
                {QStringLiteral("requiresConfirmation"), match.conflict || !match.strong}});
            const auto duplicate = std::any_of(it->candidates.cbegin(), it->candidates.cend(), [&](const QVariant& other) {
                const auto existing = other.toMap();
                return existing.value(QStringLiteral("provider")) == provider
                    && existing.value(QStringLiteral("id")) == candidate.value(QStringLiteral("id"));
            });
            if (!duplicate) it->candidates.append(candidate);
        }
        return;
    }
    auto records = data.value(QStringLiteral("records")).toList();
    if (records.isEmpty() && data.contains(QStringLiteral("fields"))) records.append(data);
    QVariantList accepted;
    for (const auto& value : records.mid(0, 64)) {
        const auto record = value.toMap();
        const auto match = compare(target, record.value(QStringLiteral("identity")).toMap());
        if (match.conflict) continue;
        if (!match.strong) { it->ambiguous = true; continue; }
        accepted.append(record);
    }
    // Multiple records for one target are ambiguous. Do not let record order
    // choose an entity or combine unrelated field claims.
    if (accepted.size() > 1) { it->ambiguous = true; return; }
    if (accepted.isEmpty()) return;
    const auto fields = accepted.first().toMap().value(QStringLiteral("fields")).toMap();
    for (const auto& field : missing(*it)) {
        const auto value = fields.value(field);
        if (!populated(value)) continue;
        it->fields.insert(field, value);
        it->provenance.insert(field, provenance);
    }
}

void AddonMetadataCoordinator::finish(const QString& consumerId)
{
    const auto found = m_consumers.constFind(consumerId);
    if (found == m_consumers.cend()) return;
    const auto consumer = *found;
    m_consumers.remove(consumerId);
    detach(consumerId, consumer.pending);
    QVariantMap data{{QStringLiteral("contextGeneration"), consumer.context.value(QStringLiteral("generation"), consumer.context.value(QStringLiteral("contextGeneration")))},
        {QStringLiteral("targetRevision"), consumer.context.value(QStringLiteral("targetRevision"))},
        {QStringLiteral("expectedFieldRevisions"), consumer.context.value(QStringLiteral("fieldRevisions"))}};
    QString status = consumer.status;
    if (consumer.operation == QStringLiteral("searchAssets")) {
        auto candidates = consumer.candidates;
        QHash<QString, int> rank;
        for (int i = 0; i < consumer.providers.size(); ++i)
            rank.insert(consumer.providers.at(i).toMap().value(QStringLiteral("id")).toString(), i);
        std::stable_sort(candidates.begin(), candidates.end(), [&](const QVariant& a, const QVariant& b) {
            return rank.value(a.toMap().value(QStringLiteral("provider")).toString(), MaxProviders)
                < rank.value(b.toMap().value(QStringLiteral("provider")).toString(), MaxProviders);
        });
        data.insert(QStringLiteral("candidates"), candidates);
        if (!consumer.candidates.isEmpty()) status = QStringLiteral("match");
        else if (consumer.ambiguous) status = QStringLiteral("ambiguous");
    } else {
        data.insert(QStringLiteral("fields"), consumer.fields);
        data.insert(QStringLiteral("provenance"), consumer.provenance);
        data.insert(QStringLiteral("missingFields"), missing(consumer));
        if (missing(consumer).isEmpty()) status = QStringLiteral("match");
        else if (!consumer.fields.isEmpty()) status = QStringLiteral("partial");
        else if (consumer.ambiguous) status = QStringLiteral("ambiguous");
    }
    emit finished(consumerId, status, data, status == QStringLiteral("match") || status == QStringLiteral("partial") ? QString{} : consumer.error);
}

void AddonMetadataCoordinator::detach(const QString& consumerId, const QSet<QString>& keys)
{
    for (const auto& key : keys) {
        auto job = m_jobs.find(key);
        if (job == m_jobs.end()) continue;
        job->waiters.remove(consumerId);
        if (!job->waiters.isEmpty()) continue;
        const auto invocation = job->invocation;
        m_jobs.erase(job);
        m_invocations.remove(invocation);
        if (!invocation.isEmpty() && m_backend.cancel) m_backend.cancel(invocation);
    }
}

void AddonMetadataCoordinator::cancel(const QString& consumerId)
{
    const auto it = m_consumers.constFind(consumerId);
    if (it == m_consumers.cend()) return;
    const auto keys = it->pending;
    m_consumers.remove(consumerId);
    detach(consumerId, keys);
    emit canceled(consumerId);
}

void AddonMetadataCoordinator::setProviderOrder(const QString& feature, const QStringList& providers)
{
    auto order = providers;
    order.removeDuplicates();
    if (m_order.value(feature) == order) return;
    m_order.insert(feature, order);
    invalidate();
}

void AddonMetadataCoordinator::invalidate()
{
    ++m_policy;
    m_cache.clear();
    const auto consumers = m_consumers.keys();
    for (const auto& consumer : consumers) cancel(consumer);
}
