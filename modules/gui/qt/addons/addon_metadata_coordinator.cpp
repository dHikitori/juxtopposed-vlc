// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_metadata_coordinator.hpp"
#include "addon_metadata_identity.hpp"
#include "util/media_identity.hpp"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QPointer>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace {
constexpr int MaxConsumers = 64;
constexpr int MaxProviders = 16;
constexpr int MaxCandidates = 500;
constexpr qsizetype MaxCandidateBytes = 8192;
constexpr int MaxCache = 128;

bool populated(const QVariant& value)
{
    return value.isValid() && !value.isNull()
        && (value.metaType().id() != QMetaType::QString || !value.toString().isEmpty());
}

QVariantMap attribution(const QVariant& value)
{
    const auto input = value.toMap();
    const auto name = input.value(QStringLiteral("name")).toString().trimmed();
    const auto license = input.value(QStringLiteral("license")).toString().trimmed();
    const auto address = input.value(QStringLiteral("url")).toString();
    const QUrl url(address, QUrl::StrictMode);
    if (name.isEmpty() || name.size() > 128 || license.size() > 128 || address.size() > 2048
        || !url.isValid() || url.scheme() != QStringLiteral("https") || url.host().isEmpty()
        || !url.userInfo().isEmpty()) return {};
    return {{QStringLiteral("name"), name}, {QStringLiteral("license"), license},
        {QStringLiteral("url"), url.toString(QUrl::FullyEncoded)}};
}

bool isArtwork(const QString& field)
{ return field == QStringLiteral("artwork") || field == QStringLiteral("artworkUrl"); }
bool sameArtworkLabel(const QVariantMap& target, const QVariantMap& candidate)
{
    const auto title = MediaIdentity::normalize(target.value(QStringLiteral("title")).toString());
    if (title.isEmpty() || title != MediaIdentity::normalize(candidate.value(QStringLiteral("title")).toString())) return false;
    for (const auto& field : {QStringLiteral("artist"), QStringLiteral("albumArtist"), QStringLiteral("album")}) {
        const auto value = MediaIdentity::normalize(target.value(field).toString());
        if (!value.isEmpty() && value != MediaIdentity::normalize(candidate.value(field).toString())) return false;
    }
    return true;
}

std::optional<QVariantMap> normalizedIdentity(const QVariantMap& input, bool requireEntity = false)
{ return vlc::addons::normalizeMetadataIdentity(input, requireEntity); }

std::optional<QVariantMap> identifiedIdentity(const QVariantMap& input)
{ return normalizedIdentity(input, true); }

MediaIdentity::Comparison compare(const QVariantMap& target, const QVariantMap& candidate)
{ return MediaIdentity::compare(MediaIdentity::fromMap(target), MediaIdentity::fromMap(candidate)); }

QString keyFor(const QString& provider, const QString& operation, const QVariantMap& context,
               const QString& partition, quint64 policy)
{
    const QVariantMap key{{QStringLiteral("provider"), provider}, {QStringLiteral("operation"), operation},
        {QStringLiteral("context"), context}, {QStringLiteral("partition"), partition},
        {QStringLiteral("policy"), QVariant::fromValue(policy)}};
    return QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(QJsonObject::fromVariantMap(key)).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}

bool manualVideoIdentity(const QString& feature, const QString& operation, const QVariantMap& context)
{
    return feature == QStringLiteral("video.identity") && operation == QStringLiteral("identify")
        && context.value(QStringLiteral("manual")).toBool()
        && !context.value(QStringLiteral("query")).toString().trimmed().isEmpty();
}
bool videoKind(const QString& kind)
{ return kind == QStringLiteral("movie") || kind == QStringLiteral("series") || kind == QStringLiteral("episode"); }

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
    for (const auto& consumer : std::as_const(m_consumers))
        if (consumer.cancelSelection) consumer.cancelSelection();
    for (const auto& job : m_jobs)
        if (!job.invocation.isEmpty() && m_backend.cancel) m_backend.cancel(job.invocation);
}

QString AddonMetadataCoordinator::identify(const QString& feature, const QVariantMap& context)
{ return start(feature, QStringLiteral("identify"), context, {}, false); }

QString AddonMetadataCoordinator::searchAssets(const QString& feature, const QVariantMap& context, bool aggregate)
{
    return start(feature, QStringLiteral("searchAssets"), context, {}, aggregate);
}

QString AddonMetadataCoordinator::enrich(const QString& feature, const QVariantMap& context, const QStringList& fields)
{
    return start(feature, QStringLiteral("enrich"), context, fields, false);
}
QString AddonMetadataCoordinator::enrichNative(const QString& feature, const QVariantMap& context,
                                              const QStringList& fields, const QString& scope)
{
    return start(feature, QStringLiteral("enrich"), context, fields, false, scope);
}

QString AddonMetadataCoordinator::start(const QString& featureInput, const QString& operationInput,
                                       const QVariantMap& contextInput, const QStringList& fieldsInput, bool aggregate,
                                       const QString& privateScopeInput)
{
    const auto feature = featureInput, operation = operationInput, privateScope = privateScopeInput;
    const auto context = contextInput;
    const auto fields = fieldsInput;
    const auto id = QStringLiteral("metadata-%1").arg(++m_sequence);
    if (feature.isEmpty() || feature.size() > 128 || fields.size() > 64
        || privateScope.size() > 128 || m_consumers.size() >= MaxConsumers) {
        QTimer::singleShot(0, this, [this, id] { emit finished(id, QStringLiteral("error"), {}, tr("The metadata request exceeds its limits.")); });
        return id;
    }
    Consumer consumer;
    consumer.feature = feature;
    consumer.operation = operation;
    consumer.privateScope = privateScope;
    consumer.context = context;
    const auto target = normalizedIdentity(context.value(QStringLiteral("identity")).toMap());
    if (!target || (context.contains(QStringLiteral("identity")) && !context.value(QStringLiteral("identity")).canConvert<QVariantMap>())) {
        QTimer::singleShot(0, this, [this, id] { emit finished(id, QStringLiteral("error"), {}, tr("The media identity is not valid.")); });
        return id;
    }
    consumer.context.insert(QStringLiteral("identity"), *target);
    consumer.required = fields;
    consumer.required.removeDuplicates();
    consumer.aggregate = aggregate;
    consumer.selecting = true;
    m_consumers.insert(id, consumer);
    const int timeout = std::clamp(context.value(QStringLiteral("timeoutMs"), 15000).toInt(), 100, 30000);
    QTimer::singleShot(timeout, this, [this, id] {
        const auto it = m_consumers.find(id);
        if (it == m_consumers.end()) return;
        it->status = QStringLiteral("error");
        it->error = tr("The metadata request time limit has ended.");
        finish(id);
    });
    const QPointer<AddonMetadataCoordinator> owner(this);
    if (m_backend.selectProviders) {
        const auto select = m_backend.selectProviders;
        auto cancel = select(feature, [owner, id](ProviderSelection result) {
            if (owner) owner->selected(id, std::move(result));
        });
        if (!owner) { if (cancel) cancel(); return id; }
        auto found = m_consumers.find(id);
        if (found == m_consumers.end() || !found->selecting) { if (cancel) cancel(); }
        else found->cancelSelection = std::move(cancel);
    } else {
        const auto query = m_backend.providersForFeature;
        const auto rows = query ? query(feature) : QVariantList{};
        if (owner) selected(id, {rows, {}, {}});
    }
    return id;
}

void AddonMetadataCoordinator::selected(const QString& id, ProviderSelection result)
{
    const auto found = m_consumers.constFind(id);
    if (found == m_consumers.cend() || !found->selecting) return;
    auto consumer = *found;
    consumer.selecting = false;
    consumer.cancelSelection = {};
    consumer.selectionCurrent = std::move(result.current);
    const QPointer<AddonMetadataCoordinator> owner(this);
    const auto feature = consumer.feature, operation = consumer.operation, privateScope = consumer.privateScope;
    if (!result.error.isEmpty() || (consumer.selectionCurrent && !consumer.selectionCurrent())) {
        auto live = m_consumers.find(id);
        live->status = QStringLiteral("error");
        live->error = result.error.isEmpty() ? tr("The metadata provider configuration changed.") : result.error;
        finish(id); return;
    }
    const auto nativeQuery = m_backend.requiresNativeScope;
    const auto& registrations = result.providers;
    QHash<QString, QVariantMap> eligible;
    QStringList defaults;
    const auto kind = consumer.context.value(QStringLiteral("identity")).toMap().value(QStringLiteral("kind")).toString();
    for (const auto& registration : registrations) {
        const auto provider = registration.toMap();
        const auto providerId = provider.value(QStringLiteral("id")).toString();
        if (providerId.isEmpty() || eligible.contains(providerId) || !provider.value(QStringLiteral("enabled")).toBool()) continue;
        const bool native = nativeQuery && nativeQuery(providerId);
        if (!owner || !m_consumers.contains(id)) return;
        if (native && (privateScope.isEmpty() || !m_backend.invokeNative)) continue;
        if (operation == QStringLiteral("identify")
            && (!provider.value(QStringLiteral("operations")).toStringList().contains(operation)
                || !provider.value(QStringLiteral("features")).toStringList().contains(feature))) continue;
        const auto kinds = provider.value(QStringLiteral("mediaKinds")).toStringList();
        if (!kinds.isEmpty()) {
            if (manualVideoIdentity(feature, operation, consumer.context)) {
                if (std::none_of(kinds.cbegin(), kinds.cend(), videoKind)) continue;
            } else if (!kinds.contains(kind)) continue;
        }
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
    }
    if (m_backend.planProviders) {
        const auto before = consumer.providers;
        const auto plan = m_backend.planProviders;
        const QPointer<AddonMetadataCoordinator> alive(this);
        const auto planned = plan(feature, operation, before, privateScope);
        if (!alive || !m_consumers.contains(id)) return;
        consumer.providers.clear(); QSet<QString> selected;
        for (const auto& value : planned) {
            const auto providerId = value.toMap().value(QStringLiteral("id")).toString();
            if (selected.contains(providerId)) continue;
            const auto original = std::find_if(before.cbegin(), before.cend(), [&](const QVariant& entry) {
                return entry.toMap().value(QStringLiteral("id")).toString() == providerId;
            });
            if (original == before.cend()) continue;
            selected.insert(providerId); consumer.providers.append(*original);
            if (consumer.providers.size() >= MaxProviders) break;
        }
    }
    consumer.providers = consumer.providers.mid(0, MaxProviders);
    if (!m_consumers.contains(id)) return;
    m_consumers.insert(id, std::move(consumer));
    QTimer::singleShot(0, this, [this, id] { advance(id); });
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
        if (isArtwork(field) && populated(existing.value(field))) continue;
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
            QStringLiteral("query"), QStringLiteral("manual"), QStringLiteral("cursor"), QStringLiteral("fingerprints"),
            QStringLiteral("media")})
        if (consumer.context.contains(key)) projection.insert(key, consumer.context.value(key));
    if (consumer.operation == QStringLiteral("identify") && projection.value(QStringLiteral("manual")).toBool()
        && !projection.value(QStringLiteral("query")).toString().trimmed().isEmpty()) {
        auto identity = projection.value(QStringLiteral("identity")).toMap();
        identity.remove(QStringLiteral("ids")); identity.remove(QStringLiteral("externalIds"));
        projection.insert(QStringLiteral("identity"), identity); projection.remove(QStringLiteral("fingerprints"));
    }
    projection.insert(QStringLiteral("feature"), consumer.feature);
    if (consumer.operation == QStringLiteral("enrich")) projection.insert(QStringLiteral("fields"), missing(consumer));
    return projection;
}

void AddonMetadataCoordinator::advance(const QString& consumerId)
{
    const QPointer<AddonMetadataCoordinator> owner(this);
    auto it = m_consumers.find(consumerId);
    if (it == m_consumers.end() || it->selecting) return;
    if (it->selectionCurrent && !it->selectionCurrent()) {
        it->status = QStringLiteral("error"); it->error = tr("The metadata provider configuration changed.");
        finish(consumerId); return;
    }
    if ((it->operation == QStringLiteral("enrich") && missing(*it).isEmpty())
        || (!it->aggregate && (it->operation == QStringLiteral("searchAssets")
            || it->operation == QStringLiteral("identify")) && !it->candidates.isEmpty())) {
        finish(consumerId); return;
    }
    const int concurrency = it->aggregate ? 4 : 1;
    while ((it = m_consumers.find(consumerId)) != m_consumers.end()
           && it->next < it->providers.size() && it->outstanding < concurrency) {
        const auto registration = it->providers.at(it->next++).toMap();
        subscribe(consumerId, registration);
        if (!owner) return;
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
    const auto query = m_backend.requiresNativeScope;
    const QPointer<AddonMetadataCoordinator> owner(this);
    const bool native = query && query(provider);
    if (!owner) return;
    it = m_consumers.find(consumerId);
    if (it == m_consumers.end()) return;
    // Do not reuse a result without a release, account, and settings identity.
    const QString key = keyFor(provider, it->operation, context, partition, m_policy)
        + (partition.isEmpty() ? consumerId : QString{})
        + (native ? QStringLiteral("/native/") + it->privateScope : QString{});
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
    if (native) job.privateScope = it->privateScope;
    job.waiters.insert(consumerId);
    m_jobs.insert(key, job);
    QTimer::singleShot(0, this, [this, key] {
        auto job = m_jobs.find(key);
        if (job == m_jobs.end()) return;
        const auto provider = job->provider, operation = job->operation;
        const auto context = job->context;
        const auto scope = job->privateScope;
        const auto invoke = m_backend.invoke;
        const auto invokeNative = m_backend.invokeNative;
        const auto cancel = m_backend.cancel;
        const QPointer<AddonMetadataCoordinator> alive(this);
        const auto invocation = !scope.isEmpty()
            ? (invokeNative ? invokeNative(provider, operation, context, scope) : QString{})
            : (invoke ? invoke(provider, operation, context) : QString{});
        if (!alive) { if (!invocation.isEmpty() && cancel) cancel(invocation); return; }
        job = m_jobs.find(key);
        if (job == m_jobs.end()) { if (!invocation.isEmpty() && cancel) cancel(invocation); return; }
        if (invocation.isEmpty() || m_invocations.contains(invocation)) {
            const auto waiters = job->waiters;
            const auto registration = job->registration;
            m_jobs.erase(job);
            for (const auto& waiter : waiters) {
                auto consumer = m_consumers.find(waiter);
                if (consumer == m_consumers.end() || !consumer->pending.remove(key)) continue;
                --consumer->outstanding;
                consume(waiter, registration, QStringLiteral("error"), {}, tr("The provider could not start."));
                if (!alive) return;
                advance(waiter);
                if (!alive) return;
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
    // Process a synchronous backend error after invoke() returns its ID
    // and subscribe() records that ID.
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
    const QString backoffKey = job.provider + '\n' + partition;
    if (!partition.isEmpty() && (status == QStringLiteral("rate_limited") || status == QStringLiteral("auth_required")
            || status == QStringLiteral("error") || status == QStringLiteral("offline"))) {
        const int seconds = status == QStringLiteral("rate_limited")
            ? std::clamp(data.value(QStringLiteral("retryAfterSeconds"), 60).toInt(), 1, 86400)
            : status == QStringLiteral("auth_required") ? 30 : 2;
        if (m_backoff.size() >= MaxCache) m_backoff.clear();
        m_backoff.insert(backoffKey, {status, data, error.left(512), m_clock.elapsed() + qint64(seconds) * 1000});
    }
    bool cacheable = job.privateScope.isEmpty();
    if (job.operation == QStringLiteral("searchAssets") || job.operation == QStringLiteral("identify")) {
        const auto candidates = data.value(QStringLiteral("candidates")).toList();
        cacheable = cacheable && candidates.size() <= MaxCandidates;
        for (const auto& candidate : candidates)
            if (QJsonDocument(QJsonObject::fromVariantMap(candidate.toMap())).toJson(QJsonDocument::Compact).size() > MaxCandidateBytes) { cacheable = false; break; }
    }
    if (cacheable && (status == QStringLiteral("match") || status == QStringLiteral("partial") || status == QStringLiteral("no_match"))) {
        m_backoff.remove(backoffKey);
        if (m_cache.size() >= MaxCache) m_cache.clear();
        m_cache.insert(key, {status, data, {}, m_clock.elapsed() + (status == QStringLiteral("no_match") ? 15000 : 60000)});
    }
    for (const auto& waiter : job.waiters) {
        auto consumer = m_consumers.find(waiter);
        if (consumer == m_consumers.end() || !consumer->pending.remove(key)) continue;
        --consumer->outstanding;
        const QPointer<AddonMetadataCoordinator> alive(this);
        consume(waiter, job.registration, status, data, error);
        if (!alive) return;
        advance(waiter);
        if (!alive) return;
    }
}

void AddonMetadataCoordinator::consume(const QString& consumerId, const QVariantMap& registration,
                                      const QString& status, const QVariantMap& data, const QString& error)
{
    auto it = m_consumers.find(consumerId);
    if (it == m_consumers.end()) return;
    if (statusWeight(status) > statusWeight(it->status)) { it->status = status; it->error = error.left(512); }
    if (status == QStringLiteral("ambiguous")) { it->ambiguous = true; it->entityAmbiguous = true; }
    const bool manual = it->context.value(QStringLiteral("manual")).toBool()
        || (it->operation == QStringLiteral("searchAssets") && it->context.value(QStringLiteral("interactive")).toBool());
    const bool explicitAlternatives = status == QStringLiteral("ambiguous")
        && (it->operation == QStringLiteral("identify") || (manual && it->operation == QStringLiteral("searchAssets")));
    if (status != QStringLiteral("match") && status != QStringLiteral("partial") && !explicitAlternatives) return;
    const auto target = it->context.value(QStringLiteral("identity")).toMap();
    const auto provider = registration.value(QStringLiteral("id")).toString();
    const QVariantMap provenance{{QStringLiteral("provider"), provider}, {QStringLiteral("feature"), it->feature},
        {QStringLiteral("cachePartition"), registration.value(QStringLiteral("cachePartition"))}};
    if (it->operation == QStringLiteral("identify")) {
        QSet<QString> seen;
        for (const auto& value : data.value(QStringLiteral("candidates")).toList().mid(0, MaxCandidates)) {
            const auto candidate = value.toMap();
            if (QJsonDocument::fromVariant(candidate).toJson(QJsonDocument::Compact).size() > MaxCandidateBytes) continue;
            const auto entity = identifiedIdentity(candidate.value(QStringLiteral("identity")).toMap());
            if (!entity) continue;
            const auto kind = entity->value(QStringLiteral("kind")).toString();
            const auto kinds = registration.value(QStringLiteral("mediaKinds")).toStringList();
            if (it->feature == QStringLiteral("video.identity") && !videoKind(kind)) continue;
            if (!manualVideoIdentity(it->feature, it->operation, it->context)) {
                const auto targetKind = target.value(QStringLiteral("kind")).toString();
                if ((!targetKind.isEmpty() && targetKind != QStringLiteral("unknown") && kind != targetKind)
                    || (!kinds.isEmpty() && !kinds.contains(kind))) continue;
            }
            const auto encoded = QJsonDocument::fromVariant(*entity).toJson(QJsonDocument::Compact);
            const auto key = QString::fromLatin1(QCryptographicHash::hash(encoded, QCryptographicHash::Sha256).toHex());
            if (seen.contains(key)) continue;
            seen.insert(key);
            const auto match = compare(target, *entity);
            auto source = provenance;
            source.insert(QStringLiteral("accountReference"), registration.value(QStringLiteral("accountReference")));
            const auto credit = attribution(candidate.value(QStringLiteral("attribution")));
            if (!credit.isEmpty()) source.insert(QStringLiteral("attribution"), credit);
            it->candidates.append(QVariantMap{{QStringLiteral("identity"), *entity},
                {QStringLiteral("provider"), provider}, {QStringLiteral("provenance"), source},
                {QStringLiteral("attribution"), credit}, {QStringLiteral("hostMatch"), QVariantMap{
                    {QStringLiteral("strong"), match.strong}, {QStringLiteral("conflicts"), match.conflicts},
                    {QStringLiteral("requiresConfirmation"), true}}}});
        }
        return;
    }
    const bool native = m_backend.requiresNativeScope && m_backend.requiresNativeScope(provider);
    if (native && it->feature == QStringLiteral("artwork") && it->operation == QStringLiteral("enrich")) {
        if (it->privateScope.isEmpty() || it->entityAmbiguous || !missing(*it).contains(QStringLiteral("artwork"))
            || !m_backend.validateNativeArtwork) return;
        const auto verified = m_backend.validateNativeArtwork(provider, data, it->privateScope);
        static const QSet<QString> families{QStringLiteral("musicbrainz"), QStringLiteral("google-images"),
            QStringLiteral("french-tv"), QStringLiteral("lastfm")};
        if (!verified || !families.contains(verified->family) || !populated(verified->descriptor)
            || QJsonDocument::fromVariant(QVariantMap{{QStringLiteral("value"), verified->descriptor}})
                .toJson(QJsonDocument::Compact).size() > MaxCandidateBytes || it->labelArtwork.size() >= 64) return;
        auto source = provenance;
        source.insert(QStringLiteral("nativeFamily"), verified->family);
        source.insert(QStringLiteral("compatibilityPolicy"), QStringLiteral("native-family-artwork@1"));
        source.insert(QStringLiteral("matchEvidence"), QStringLiteral("retained-native-compatibility"));
        source.insert(QStringLiteral("strong"), false);
        const auto credit = attribution(verified->attribution);
        if (!credit.isEmpty()) source.insert(QStringLiteral("attribution"), credit);
        // This is a retained native behavior. It supplies no entity identity.
        // The existing weak-result order also preserves a higher ambiguity.
        it->labelArtwork.append(QVariantMap{{QStringLiteral("field"), QStringLiteral("artwork")},
            {QStringLiteral("value"), verified->descriptor}, {QStringLiteral("provenance"), source}});
        return;
    }
    if (it->operation == QStringLiteral("searchAssets")) {
        for (const auto& value : data.value(QStringLiteral("candidates")).toList()) {
            if (it->candidates.size() >= MaxCandidates) break;
            auto candidate = value.toMap();
            if (candidate.value(QStringLiteral("id")).toString().isEmpty()) continue;
            if (QJsonDocument(QJsonObject::fromVariantMap(candidate)).toJson(QJsonDocument::Compact).size() > MaxCandidateBytes) {
                it->status = QStringLiteral("error");
                it->error = tr("A subtitle candidate exceeds the size limit.");
                continue;
            }
            if (candidate.contains(QStringLiteral("identity")) && !candidate.value(QStringLiteral("identity")).canConvert<QVariantMap>()) continue;
            const auto entity = normalizedIdentity(candidate.value(QStringLiteral("identity")).toMap());
            if (!entity) continue;
            candidate.insert(QStringLiteral("identity"), *entity);
            const auto match = compare(target, *entity);
            const bool artworkLabel = it->feature == QStringLiteral("artwork")
                && candidate.value(QStringLiteral("evidence")).toMap().value(QStringLiteral("type")) == QStringLiteral("label");
            if (!manual && (match.conflict || !match.strong)) { if (!match.conflict) it->ambiguous = true; continue; }
            candidate.insert(QStringLiteral("provider"), provider);
            candidate.insert(QStringLiteral("provenance"), provenance);
            candidate.insert(QStringLiteral("hostMatch"), QVariantMap{{QStringLiteral("score"), match.score},
                {QStringLiteral("strong"), match.strong}, {QStringLiteral("conflicts"), match.conflicts},
                {QStringLiteral("requiresConfirmation"), explicitAlternatives || artworkLabel || match.conflict || !match.strong}});
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
        if (record.contains(QStringLiteral("identity")) && !record.value(QStringLiteral("identity")).canConvert<QVariantMap>()) continue;
        const auto candidate = normalizedIdentity(record.value(QStringLiteral("identity")).toMap());
        if (!candidate) continue;
        const auto match = compare(target, *candidate);
        if (match.conflict) continue;
        const bool label = record.value(QStringLiteral("evidence")).toMap().value(QStringLiteral("type")) == QStringLiteral("label");
        if (it->feature == QStringLiteral("artwork") && (!match.strong || label) && sameArtworkLabel(target, *candidate)) {
            const auto fields = record.value(QStringLiteral("fields")).toMap();
            for (const auto& field : missing(*it)) {
                if (!isArtwork(field) || !populated(fields.value(field)) || it->labelArtwork.size() >= 64) continue;
                if (QJsonDocument::fromVariant(QVariantMap{{QStringLiteral("value"), fields.value(field)}})
                    .toJson(QJsonDocument::Compact).size() > MaxCandidateBytes) continue;
                auto evidence = provenance;
                evidence.insert(QStringLiteral("matchEvidence"), QStringLiteral("label"));
                const auto credit = attribution(record.value(QStringLiteral("attribution")));
                if (!credit.isEmpty()) evidence.insert(QStringLiteral("attribution"), credit);
                it->labelArtwork.append(QVariantMap{{QStringLiteral("field"), field}, {QStringLiteral("value"), fields.value(field)},
                    {QStringLiteral("identity"), *candidate}, {QStringLiteral("provenance"), evidence}});
            }
            continue;
        }
        if (label) continue;
        if (!match.strong) { it->ambiguous = true; continue; }
        auto normalizedRecord = record;
        normalizedRecord.insert(QStringLiteral("identity"), *candidate);
        accepted.append(normalizedRecord);
    }
    // More than one record for a target is ambiguous. Do not use record
    // order to select an entity or combine unrelated field claims.
    if (accepted.size() > 1) {
        it->ambiguous = true; it->entityAmbiguous = true;
        if (it->feature == QStringLiteral("artwork")) for (const auto& value : accepted) {
            const auto record = value.toMap();
            auto source = provenance;
            const auto credit = attribution(record.value(QStringLiteral("attribution")));
            if (!credit.isEmpty()) source.insert(QStringLiteral("attribution"), credit);
            for (const auto& field : missing(*it)) {
                const auto claim = record.value(QStringLiteral("fields")).toMap().value(field);
                if (!isArtwork(field) || !populated(claim) || it->ambiguousArtwork.size() >= 64) continue;
                if (QJsonDocument::fromVariant(QVariantMap{{QStringLiteral("value"), claim}}).toJson(QJsonDocument::Compact).size() > MaxCandidateBytes) continue;
                it->ambiguousArtwork.append(QVariantMap{{QStringLiteral("field"), field}, {QStringLiteral("value"), claim},
                    {QStringLiteral("identity"), record.value(QStringLiteral("identity"))}, {QStringLiteral("provenance"), source}});
            }
        }
        return;
    }
    if (accepted.isEmpty()) return;
    // A later provider cannot select an entity from an earlier ambiguous result.
    if (it->entityAmbiguous) return;
    const auto record = accepted.first().toMap();
    const auto fields = record.value(QStringLiteral("fields")).toMap();
    auto fieldProvenance = provenance;
    const auto credit = attribution(record.value(QStringLiteral("attribution")));
    if (!credit.isEmpty()) fieldProvenance.insert(QStringLiteral("attribution"), credit);
    for (const auto& field : missing(*it)) {
        const auto value = fields.value(field);
        if (!populated(value)) continue;
        it->fields.insert(field, value);
        it->provenance.insert(field, fieldProvenance);
    }
}

void AddonMetadataCoordinator::finish(const QString& consumerId)
{
    auto found = m_consumers.find(consumerId);
    if (found == m_consumers.end()) return;
    if (found->feature == QStringLiteral("artwork") && !found->labelArtwork.isEmpty() && !found->entityAmbiguous) {
        const auto needed = missing(*found);
        QVariantList ambiguous;
        for (const auto& field : needed) {
            if (!isArtwork(field)) continue;
            QVariantList preferred;
            for (const auto& provider : found->providers) {
                const auto id = provider.toMap().value(QStringLiteral("id")).toString();
                for (const auto& value : found->labelArtwork) {
                    const auto record = value.toMap();
                    if (record.value(QStringLiteral("field")) == field
                        && record.value(QStringLiteral("provenance")).toMap().value(QStringLiteral("provider")) == id) preferred.append(record);
                }
                if (!preferred.isEmpty()) break;
            }
            if (preferred.size() == 1) {
                const auto record = preferred.first().toMap();
                found->fields.insert(field, record.value(QStringLiteral("value")));
                found->provenance.insert(field, record.value(QStringLiteral("provenance")));
            } else if (!preferred.isEmpty()) {
                found->ambiguous = true; ambiguous.append(preferred);
            }
        }
        found->labelArtwork = ambiguous;
    }
    if (found->entityAmbiguous) found->labelArtwork = found->ambiguousArtwork;
    const auto consumer = *found;
    m_consumers.remove(consumerId);
    const QPointer<AddonMetadataCoordinator> alive(this);
    if (consumer.cancelSelection) consumer.cancelSelection();
    if (!alive) return;
    detach(consumerId, consumer.pending);
    if (!alive) return;
    QVariantMap data{{QStringLiteral("contextGeneration"), consumer.context.value(QStringLiteral("generation"), consumer.context.value(QStringLiteral("contextGeneration")))},
        {QStringLiteral("targetRevision"), consumer.context.value(QStringLiteral("targetRevision"))},
        {QStringLiteral("expectedFieldRevisions"), consumer.context.value(QStringLiteral("fieldRevisions"))}};
    QString status = consumer.status;
    if (consumer.operation == QStringLiteral("searchAssets") || consumer.operation == QStringLiteral("identify")) {
        auto candidates = consumer.candidates;
        QHash<QString, int> rank;
        for (int i = 0; i < consumer.providers.size(); ++i)
            rank.insert(consumer.providers.at(i).toMap().value(QStringLiteral("id")).toString(), i);
        std::stable_sort(candidates.begin(), candidates.end(), [&](const QVariant& a, const QVariant& b) {
            return rank.value(a.toMap().value(QStringLiteral("provider")).toString(), MaxProviders)
                < rank.value(b.toMap().value(QStringLiteral("provider")).toString(), MaxProviders);
        });
        data.insert(QStringLiteral("candidates"), candidates);
        if (consumer.operation == QStringLiteral("identify")) data.insert(QStringLiteral("requiresSelection"), true);
        if (!consumer.candidates.isEmpty()) status = consumer.operation == QStringLiteral("identify")
            && (consumer.candidates.size() > 1 || consumer.ambiguous) ? QStringLiteral("ambiguous") : QStringLiteral("match");
        else if (consumer.ambiguous) status = QStringLiteral("ambiguous");
        else if (status == QStringLiteral("match") || status == QStringLiteral("partial")) status = QStringLiteral("no_match");
    } else {
        data.insert(QStringLiteral("fields"), consumer.fields);
        data.insert(QStringLiteral("provenance"), consumer.provenance);
        data.insert(QStringLiteral("missingFields"), missing(consumer));
        if (consumer.ambiguous && !consumer.labelArtwork.isEmpty()) data.insert(QStringLiteral("artworkCandidates"), consumer.labelArtwork);
        if (missing(consumer).isEmpty()) status = QStringLiteral("match");
        else if (!consumer.fields.isEmpty()) status = QStringLiteral("partial");
        else if (consumer.ambiguous) status = QStringLiteral("ambiguous");
        else if (status == QStringLiteral("match") || status == QStringLiteral("partial")) status = QStringLiteral("no_match");
    }
    emit finished(consumerId, status, data, status == QStringLiteral("match") || status == QStringLiteral("partial") ? QString{} : consumer.error);
}

void AddonMetadataCoordinator::detach(const QString& consumerId, const QSet<QString>& keys)
{
    const QPointer<AddonMetadataCoordinator> alive(this);
    for (const auto& key : keys) {
        auto job = m_jobs.find(key);
        if (job == m_jobs.end()) continue;
        job->waiters.remove(consumerId);
        if (!job->waiters.isEmpty()) continue;
        const auto invocation = job->invocation;
        const auto cancel = m_backend.cancel;
        m_jobs.erase(job);
        m_invocations.remove(invocation);
        if (!invocation.isEmpty() && cancel) cancel(invocation);
        if (!alive) return;
    }
}

void AddonMetadataCoordinator::cancel(const QString& consumerId)
{
    const auto it = m_consumers.constFind(consumerId);
    if (it == m_consumers.cend()) return;
    const auto keys = it->pending;
    const auto cancelSelection = it->cancelSelection;
    m_consumers.remove(consumerId);
    const QPointer<AddonMetadataCoordinator> alive(this);
    if (cancelSelection) cancelSelection();
    if (!alive) return;
    detach(consumerId, keys);
    if (!alive) return;
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
    const QPointer<AddonMetadataCoordinator> alive(this);
    for (const auto& consumer : consumers) { cancel(consumer); if (!alive) return; }
}
