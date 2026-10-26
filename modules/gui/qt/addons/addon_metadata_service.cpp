// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include "addon_metadata_service.hpp"
#include <vlc_metadata_backend.h>
#include "addon_manager.hpp"
#include "util/media_identity_vlc.hpp"
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <atomic>
#include <mutex>

namespace {
using NativeJob = std::shared_ptr<vlc_metadata_job_t>;
NativeJob holdJob(vlc_metadata_job_t* job)
{ return NativeJob(vlc_metadata_job_Hold(job), vlc_metadata_job_Release); }
QString nonce(const char* prefix)
{ return QLatin1String(prefix) + QUuid::createUuid().toString(QUuid::WithoutBraces); }
const QMap<QString, vlc_meta_type_t>& nativeFields()
{
    static const QMap<QString, vlc_meta_type_t> fields{
        {QStringLiteral("title"), vlc_meta_Title}, {QStringLiteral("episodeTitle"), vlc_meta_Title},
        {QStringLiteral("artist"), vlc_meta_Artist}, {QStringLiteral("albumArtist"), vlc_meta_AlbumArtist},
        {QStringLiteral("album"), vlc_meta_Album}, {QStringLiteral("showName"), vlc_meta_ShowName},
        {QStringLiteral("description"), vlc_meta_Description}, {QStringLiteral("genre"), vlc_meta_Genre},
        {QStringLiteral("date"), vlc_meta_Date}, {QStringLiteral("season"), vlc_meta_Season},
        {QStringLiteral("episode"), vlc_meta_Episode}, {QStringLiteral("track"), vlc_meta_TrackNumber},
        {QStringLiteral("disc"), vlc_meta_DiscNumber}, {QStringLiteral("artwork"), vlc_meta_ArtworkURL},
        {QStringLiteral("artworkUrl"), vlc_meta_ArtworkURL}};
    return fields;
}
int nativeStatus(const QString& status)
{
    if (status == QStringLiteral("match") || status == QStringLiteral("partial") || status == QStringLiteral("no_match")) return VLC_SUCCESS;
    if (status == QStringLiteral("unsupported")) return VLC_ENOTSUP;
    if (status == QStringLiteral("auth_required")) return VLC_EACCES;
    return VLC_EGENERIC;
}
}

struct AddonMetadataService::CoreState {
    std::mutex lock;
    AddonMetadataService* owner = nullptr;
    QHash<quint64, NativeJob> jobs;
    QHash<vlc_metadata_job_t*, quint64> keys;
    quint64 sequence = 0;
    std::atomic_bool automaticAllowed{false};

    static int dispatch(void* opaque, vlc_metadata_job_t* job)
    {
        const auto state = *static_cast<std::shared_ptr<CoreState>*>(opaque);
        const auto* request = vlc_metadata_job_GetRequest(job);
        if (!(request->flags & VLC_METADATA_REQUEST_NETWORK)
            || ((request->flags & VLC_METADATA_REQUEST_AUTOMATIC) && !state->automaticAllowed.load())) return VLC_EACCES;
        std::lock_guard<std::mutex> guard(state->lock);
        if (!state->owner) return -EINTR;
        const auto key = ++state->sequence;
        state->jobs.insert(key, holdJob(job)); state->keys.insert(job, key);
        if (!vlc_metadata_job_IsPending(job)) {
            state->jobs.remove(key); state->keys.remove(job); return VLC_SUCCESS;
        }
        const bool queued = QMetaObject::invokeMethod(state->owner, [state, key] {
            AddonMetadataService* owner;
            { std::lock_guard<std::mutex> guard(state->lock); owner = state->owner; }
            if (owner) owner->startCore(key);
        }, Qt::QueuedConnection);
        if (!queued) { state->jobs.remove(key); state->keys.remove(job); }
        return queued ? VLC_SUCCESS : VLC_EGENERIC;
    }
    static void cancel(void* opaque, vlc_metadata_job_t* job)
    {
        const auto state = *static_cast<std::shared_ptr<CoreState>*>(opaque);
        NativeJob held;
        std::lock_guard<std::mutex> guard(state->lock);
        const auto key = state->keys.take(job);
        if (!key) return;
        held = state->jobs.take(key);
        if (state->owner) QMetaObject::invokeMethod(state->owner, [state, key] {
            AddonMetadataService* owner;
            { std::lock_guard<std::mutex> guard(state->lock); owner = state->owner; }
            if (owner) owner->cancelCore(key);
        }, Qt::QueuedConnection);
        // held has no Qt dependency. Its reference ends when this call ends.
    }
    static void destroy(void* opaque)
    { delete static_cast<std::shared_ptr<CoreState>*>(opaque); }
    NativeJob find(quint64 key)
    { std::lock_guard<std::mutex> guard(lock); return jobs.value(key); }
    NativeJob take(quint64 key)
    {
        std::lock_guard<std::mutex> guard(lock);
        auto job = jobs.take(key);
        if (job) keys.remove(job.get());
        return job;
    }
};

AddonMetadataService::Backend AddonMetadataService::managerBackend(vlc::addons::AddonManager* manager)
{
    QPointer<vlc::addons::AddonManager> target(manager);
    Backend backend;
    backend.providers.providersForFeature = [target](const QString& feature) {
        return target ? target->providersForFeature(feature) : QVariantList{};
    };
    backend.providers.invoke = [target](const QString& id, const QString& operation, const QVariantMap& context) {
        auto projection = context;
        projection.remove(QStringLiteral("media"));
        return target ? target->invoke(id, operation, projection) : QString{};
    };
    backend.providers.cancel = [target](const QString& id) { if (target) target->cancel(id); };
    backend.registerBuiltin = [target](const QString& feature, const QVariantMap& descriptor) {
        if (target) target->registerBuiltinProvider(feature, descriptor);
    };
    backend.unregisterBuiltin = [target](const QString& id) { if (target) target->unregisterBuiltinProvider(id); };
    return backend;
}
AddonMetadataService::AddonMetadataService(vlc::addons::AddonManager* manager, QObject* parent)
    : AddonMetadataService(managerBackend(manager), parent)
{
    if (manager) {
        connect(manager, &vlc::addons::AddonManager::operationFinished, this, &AddonMetadataService::operationFinished);
        connect(manager, &vlc::addons::AddonManager::revisionChanged, this, &AddonMetadataService::invalidate);
    }
}
AddonMetadataService::AddonMetadataService(Backend backend, QObject* parent)
    : QObject(parent), m_backend(std::move(backend)), m_coordinator({
          [this](const QString& feature) { return providers(feature); },
          [this](const QString& id, const QString& operation, const QVariantMap& context) { return invoke(id, operation, context); },
          [this](const QString& invocation) { cancelInvocation(invocation); }}, this),
      m_core(std::make_shared<CoreState>())
{
    m_core->owner = this;
    const vlc_metadata_backend_operations operations{CoreState::dispatch, CoreState::cancel, CoreState::destroy};
    auto* state = new std::shared_ptr<CoreState>(m_core);
    m_coreBackend = vlc_metadata_backend_New(&operations, state);
    if (!m_coreBackend) delete state;
    connect(&m_coordinator, &AddonMetadataCoordinator::finished, this, &AddonMetadataService::coordinatorFinished);
    connect(&m_coordinator, &AddonMetadataCoordinator::canceled, this, [this](const QString& consumer) {
        const auto found = m_consumers.constFind(consumer);
        if (found == m_consumers.cend()) return;
        const auto record = *found;
        m_consumers.remove(consumer);
        releaseMedia(record.media);
        const auto result = m_results.take(consumer);
        for (const auto& id : result.assets) if (m_backend.cancelArtwork && !id.isEmpty()) m_backend.cancelArtwork(id);
        if (record.core) {
            m_coreConsumers.remove(record.core);
            auto job = m_core->take(record.core);
            if (job) vlc_metadata_job_Cancel(job.get());
            return;
        }
        emit canceled(consumer);
    });
}
AddonMetadataService::~AddonMetadataService()
{
    shutdown();
    if (m_coreBackend) vlc_metadata_backend_Release(m_coreBackend);
}

QVariantList AddonMetadataService::providers(const QString& feature) const
{
    auto list = m_backend.providers.providersForFeature ? m_backend.providers.providersForFeature(feature) : QVariantList{};
    QSet<QString> registered;
    for (const auto& entry : list) registered.insert(entry.toMap().value(QStringLiteral("id")).toString());
    for (auto it = m_builtins.cbegin(); it != m_builtins.cend(); ++it)
        if (it->lifetime && it->features.contains(feature) && !registered.contains(it.key())) list.append(it->descriptor);
    return list;
}
bool AddonMetadataService::registerBuiltinProvider(const QString& feature, const QVariantMap& descriptor,
                                                  QObject* lifetime, NativeInvoke invoke, NativeCancel cancel)
{
    const auto id = descriptor.value(QStringLiteral("id")).toString();
    if (m_closed || !lifetime || !invoke || feature.isEmpty() || id.isEmpty() || m_builtins.size() >= 128) return false;
    const auto existing = m_builtins.constFind(id);
    const bool isNew = existing == m_builtins.cend();
    if (existing != m_builtins.cend() && existing->lifetime != lifetime) return false;
    auto builtin = existing == m_builtins.cend() ? Builtin{} : *existing;
    if (!builtin.features.contains(feature)) builtin.features.append(feature);
    builtin.descriptor = descriptor;
    builtin.descriptor.insert(QStringLiteral("builtin"), true);
    builtin.descriptor.insert(QStringLiteral("features"), builtin.features);
    builtin.lifetime = lifetime; builtin.invoke = std::move(invoke); builtin.cancel = std::move(cancel);
    m_builtins.insert(id, builtin);
    if (isNew) connect(lifetime, &QObject::destroyed, this, [this, id] { unregisterBuiltinProvider(id); });
    if (m_backend.registerBuiltin) m_backend.registerBuiltin(feature, builtin.descriptor);
    invalidate();
    return true;
}
void AddonMetadataService::unregisterBuiltinProvider(const QString& contribution)
{
    if (!m_builtins.remove(contribution)) return;
    if (m_backend.unregisterBuiltin) m_backend.unregisterBuiltin(contribution);
    invalidate();
}
QString AddonMetadataService::invoke(const QString& contribution, const QString& operation, const QVariantMap& context)
{
    if (m_closed) return {};
    const auto found = m_builtins.constFind(contribution);
    if (found == m_builtins.cend()) {
        auto projection = context;
        projection.remove(QStringLiteral("media"));
        return m_backend.providers.invoke ? m_backend.providers.invoke(contribution, operation, projection) : QString{};
    }
    if (!found->lifetime) return {};
    const auto id = nonce("native-metadata:");
    m_nativeCalls.insert(id, {contribution, context.value(QStringLiteral("media")).toString()});
    const auto callback = found->invoke;
    callback(id, operation, context);
    return id;
}
void AddonMetadataService::cancelInvocation(const QString& invocation)
{
    const auto call = m_nativeCalls.find(invocation);
    if (call == m_nativeCalls.end()) { if (m_backend.providers.cancel) m_backend.providers.cancel(invocation); return; }
    const auto contribution = call->contribution;
    m_nativeCalls.erase(call);
    const auto found = m_builtins.constFind(contribution);
    if (found != m_builtins.cend() && found->lifetime && found->cancel) found->cancel(invocation);
}
void AddonMetadataService::completeBuiltin(const QString& invocation, const QString& status,
                                          const QVariantMap& data, const QString& error)
{
    const auto found = m_nativeCalls.find(invocation);
    if (found == m_nativeCalls.end()) return;
    const auto contribution = found->contribution;
    m_nativeCalls.erase(found);
    m_coordinator.operationFinished(invocation, contribution, status, data, error);
}
QUrl AddonMetadataService::privateUrlForInvocation(const QString& invocation) const
{
    const auto call = m_nativeCalls.constFind(invocation);
    return call == m_nativeCalls.cend() ? QUrl{} : m_media.value(call->media).url;
}
void AddonMetadataService::operationFinished(const QString& invocation, const QString& contribution,
                                            const QString& status, const QVariantMap& data, const QString& error)
{
    // Portable completions cannot claim a native invocation ID.
    if (!m_nativeCalls.contains(invocation)) m_coordinator.operationFinished(invocation, contribution, status, data, error);
}

QString AddonMetadataService::acquireMedia(const QUrl& url, const QVariantMap& identity)
{
    const auto serialized = QJsonDocument::fromVariant(identity).toJson(QJsonDocument::Compact);
    if (m_closed || !url.isValid() || url.isEmpty() || url.toEncoded().size() > 32768 || serialized.size() > 65536) return {};
    const QByteArray privateKey = url.toEncoded() + '\n' + serialized;
    const QString key = QString::fromLatin1(QCryptographicHash::hash(privateKey, QCryptographicHash::Sha256).toHex());
    const auto existing = m_mediaKeys.value(key);
    if (!existing.isEmpty()) { holdMedia(existing); return existing; }
    if (m_media.size() >= 8192) return {};
    const auto reference = nonce("metadata-media:");
    m_media.insert(reference, {url, identity, key, 1}); m_mediaKeys.insert(key, reference);
    return reference;
}
bool AddonMetadataService::holdMedia(const QString& reference)
{
    if (reference.isEmpty()) return true;
    auto found = m_media.find(reference);
    if (found == m_media.end() || found->refs == UINT_MAX) return false;
    ++found->refs; return true;
}
void AddonMetadataService::releaseMedia(const QString& reference)
{
    const auto found = m_media.find(reference);
    if (found == m_media.end() || --found->refs) return;
    m_mediaKeys.remove(found->key); m_media.erase(found);
}
QString AddonMetadataService::searchAssets(const QString& feature, const QVariantMap& context, bool aggregate)
{ return start(feature, context, {}, aggregate, true); }
QString AddonMetadataService::enrich(const QString& feature, const QVariantMap& context, const QStringList& fields)
{ return start(feature, context, fields, false, false); }
QString AddonMetadataService::start(const QString& feature, QVariantMap context, const QStringList& fields, bool aggregate, bool assets)
{
    const auto media = context.value(QStringLiteral("media")).toString();
    const bool automatic = context.value(QStringLiteral("automatic")).toBool();
    if (m_closed || (automatic && !automaticNetworkAllowed()) || !holdMedia(media)) {
        const auto consumer = nonce("metadata-rejected:");
        m_consumers.insert(consumer, {});
        QTimer::singleShot(0, this, [this, consumer, context] {
            deliver(consumer, QStringLiteral("unsupported"), {
                {QStringLiteral("contextGeneration"), context.value(QStringLiteral("generation"))},
                {QStringLiteral("targetRevision"), context.value(QStringLiteral("targetRevision"))}},
                tr("The metadata request is not permitted."));
        });
        return consumer;
    }
    if (!media.isEmpty()) context.insert(QStringLiteral("identity"), m_media.value(media).identity);
    const auto consumer = assets ? m_coordinator.searchAssets(feature, context, aggregate) : m_coordinator.enrich(feature, context, fields);
    m_consumers.insert(consumer, {media, 0, automatic});
    return consumer;
}
void AddonMetadataService::cancel(const QString& consumer)
{
    if (!m_consumers.contains(consumer)) return;
    m_coordinator.cancel(consumer);
    // A result can wait for an asset after its coordinator request ends.
    if (!m_consumers.contains(consumer)) return;
    const auto record = m_consumers.take(consumer);
    const auto result = m_results.take(consumer);
    for (const auto& id : result.assets) if (m_backend.cancelArtwork && !id.isEmpty()) m_backend.cancelArtwork(id);
    releaseMedia(record.media);
    if (record.core) {
        m_coreConsumers.remove(record.core);
        auto job = m_core->take(record.core);
        if (job) vlc_metadata_job_Cancel(job.get());
        return;
    }
    emit canceled(consumer);
}
void AddonMetadataService::invalidate()
{
    QPointer<AddonMetadataService> guard(this);
    const auto consumers = m_consumers.keys();
    for (const auto& consumer : consumers) { cancel(consumer); if (!guard) return; }
    m_coordinator.invalidate();
    emit invalidated();
}
void AddonMetadataService::setArtworkAdapter(ArtworkResolver resolver, std::function<void(const QString&)> cancel)
{
    QPointer<AddonMetadataService> guard(this);
    invalidate();
    if (!guard) return;
    m_backend.resolveArtwork = std::move(resolver);
    m_backend.cancelArtwork = std::move(cancel);
}
bool AddonMetadataService::automaticNetworkAllowed() const { return m_core->automaticAllowed.load(); }
int AddonMetadataService::nativeJobCount() const
{ std::lock_guard<std::mutex> guard(m_core->lock); return m_core->jobs.size(); }
void AddonMetadataService::setAutomaticNetworkAllowed(bool allowed)
{
    QPointer<AddonMetadataService> guard(this);
    if (m_core->automaticAllowed.exchange(allowed) == allowed) return;
    if (!allowed) {
        QList<NativeJob> jobs;
        { std::lock_guard<std::mutex> guard(m_core->lock);
          for (const auto& job : m_core->jobs)
              if (vlc_metadata_job_GetRequest(job.get())->flags & VLC_METADATA_REQUEST_AUTOMATIC) jobs.append(job); }
        for (const auto& job : jobs) { vlc_metadata_job_Cancel(job.get()); if (!guard) return; }
    }
    invalidate();
}
vlc_metadata_backend_t* AddonMetadataService::holdCoreBackend() const
{ return m_coreBackend ? vlc_metadata_backend_Hold(m_coreBackend) : nullptr; }
void AddonMetadataService::shutdown()
{
    if (m_closed) return;
    QPointer<AddonMetadataService> guard(this);
    m_closed = true;
    { std::lock_guard<std::mutex> guard(m_core->lock); m_core->owner = nullptr; }
    if (m_coreBackend) vlc_metadata_backend_Close(m_coreBackend);
    if (!guard) return;
    invalidate();
    if (!guard) return;
    const auto builtins = m_builtins.keys();
    m_builtins.clear();
    for (const auto& id : builtins) if (m_backend.unregisterBuiltin) m_backend.unregisterBuiltin(id);
    m_media.clear(); m_mediaKeys.clear();
}

void AddonMetadataService::coordinatorFinished(const QString& consumer, const QString& status,
                                              const QVariantMap& data, const QString& error)
{
    if (!m_consumers.contains(consumer)) return;
    auto fields = data.value(QStringLiteral("fields")).toMap();
    QStringList assets;
    for (const auto& field : {QStringLiteral("artwork"), QStringLiteral("artworkUrl")}) if (fields.contains(field)) assets.append(field);
    if (assets.isEmpty()) { deliver(consumer, status, data, error); return; }
    m_results.insert(consumer, {status, error, data, {}, int(assets.size())});
    for (const auto& field : assets) {
        const auto provider = data.value(QStringLiteral("provenance")).toMap().value(field).toMap().value(QStringLiteral("provider")).toString();
        QPointer<AddonMetadataService> target(this);
        auto complete = [target, consumer, field](const QUrl& url, const QString& error) {
            if (target) QMetaObject::invokeMethod(target, [target, consumer, field, url, error] {
                if (target) target->artworkFinished(consumer, field, url, error);
            }, Qt::QueuedConnection);
        };
        const auto job = m_backend.resolveArtwork ? m_backend.resolveArtwork(provider, fields.value(field), complete) : QString{};
        auto result = m_results.find(consumer);
        if (result == m_results.end()) { if (!job.isEmpty() && m_backend.cancelArtwork) m_backend.cancelArtwork(job); return; }
        result->assets.insert(field, job);
        if (job.isEmpty()) complete({}, tr("No artwork asset adapter is available."));
    }
}
void AddonMetadataService::artworkFinished(const QString& consumer, const QString& field, const QUrl& url, const QString& error)
{
    auto found = m_results.find(consumer);
    if (found == m_results.end() || !found->assets.remove(field)) return;
    auto fields = found->data.value(QStringLiteral("fields")).toMap();
    if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile() && QFileInfo(url.toLocalFile()).isReadable()) {
        fields.insert(field, url.toString(QUrl::FullyEncoded));
    } else {
        fields.remove(field);
        auto provenance = found->data.value(QStringLiteral("provenance")).toMap(); provenance.remove(field);
        found->data.insert(QStringLiteral("provenance"), provenance);
        auto missing = found->data.value(QStringLiteral("missingFields")).toStringList();
        if (!missing.contains(field)) missing.append(field);
        found->data.insert(QStringLiteral("missingFields"), missing);
        found->status = fields.isEmpty() ? QStringLiteral("unsupported") : QStringLiteral("partial");
        found->error = error.isEmpty() ? tr("The artwork adapter did not supply a local file.") : error;
    }
    found->data.insert(QStringLiteral("fields"), fields);
    if (--found->outstanding) return;
    const auto result = *found; m_results.erase(found);
    deliver(consumer, result.status, result.data, result.error);
}
void AddonMetadataService::deliver(const QString& consumer, const QString& status,
                                  const QVariantMap& data, const QString& error)
{
    const auto found = m_consumers.find(consumer);
    if (found == m_consumers.end()) return;
    const auto record = *found;
    m_consumers.erase(found); releaseMedia(record.media);
    if (record.core) { m_coreConsumers.remove(record.core); finishCore(record.core, status, data); return; }
    emit finished(consumer, status, data, error);
}

void AddonMetadataService::startCore(quint64 key)
{
    const auto job = m_core->find(key);
    if (!job) return;
    if (!vlc_metadata_job_IsPending(job.get())) { m_core->take(key); return; }
    const auto* request = vlc_metadata_job_GetRequest(job.get());
    if (m_closed || ((request->flags & VLC_METADATA_REQUEST_AUTOMATIC) && !automaticNetworkAllowed())) {
        auto owned = m_core->take(key); if (owned) vlc_metadata_job_Cancel(owned.get()); return;
    }
    QVariantMap context;
    if (request->context_size) {
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(QByteArray(request->context_json, request->context_size), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            auto owned = m_core->take(key); if (owned) vlc_metadata_job_Complete(owned.get(), VLC_EINVAL, nullptr, nullptr, 0); return;
        }
        context = document.object().toVariantMap();
    }
    QString media;
    if (request->item) {
        char* uri = input_item_GetURI(request->item);
        const auto url = uri ? QUrl(QString::fromUtf8(uri)) : QUrl{}; free(uri);
        const auto tags = MediaIdentity::inputMetadata(request->item);
        const auto identity = context.value(QStringLiteral("identity")).toMap().isEmpty()
            ? MediaIdentity::toMap(MediaIdentity::identify(url, tags)) : context.value(QStringLiteral("identity")).toMap();
        media = acquireMedia(url, identity);
        if (media.isEmpty()) {
            auto owned = m_core->take(key); if (owned) vlc_metadata_job_Complete(owned.get(), VLC_EINVAL, nullptr, nullptr, 0); return;
        }
        context.insert(QStringLiteral("media"), media);
        auto existing = context.value(QStringLiteral("existingFields")).toMap();
        for (auto it = nativeFields().cbegin(); it != nativeFields().cend(); ++it) {
            char* raw = input_item_GetMeta(request->item, it.value());
            const auto value = raw ? QString::fromUtf8(raw) : QString{}; free(raw);
            if (!value.isEmpty()) existing.insert(it.key(), value);
        }
        if (MediaIdentity::episodeTitle(url, tags, false).isEmpty()) existing.remove(QStringLiteral("episodeTitle"));
        context.insert(QStringLiteral("existingFields"), existing);
    }
    context.insert(QStringLiteral("generation"), QVariant::fromValue<qulonglong>(request->generation));
    context.insert(QStringLiteral("targetRevision"), QVariant::fromValue<qulonglong>(request->target_revision));
    context.insert(QStringLiteral("automatic"), bool(request->flags & VLC_METADATA_REQUEST_AUTOMATIC));
    QStringList fields;
    for (size_t i = 0; i < request->field_count; ++i) fields.append(QString::fromUtf8(request->fields[i]));
    const auto operation = QString::fromUtf8(request->operation);
    QString consumer;
    if (operation == QStringLiteral("enrich")) consumer = enrich(QString::fromUtf8(request->feature), context, fields);
    else if (operation == QStringLiteral("searchAssets")) consumer = searchAssets(QString::fromUtf8(request->feature), context, context.value(QStringLiteral("aggregate")).toBool());
    else {
        releaseMedia(media);
        auto owned = m_core->take(key); if (owned) vlc_metadata_job_Complete(owned.get(), VLC_ENOTSUP, nullptr, nullptr, 0); return;
    }
    releaseMedia(media);
    m_consumers[consumer].core = key; m_coreConsumers.insert(key, consumer);
    if (!vlc_metadata_job_IsPending(job.get())) cancelCore(key);
}
void AddonMetadataService::cancelCore(quint64 key)
{
    const auto consumer = m_coreConsumers.take(key);
    if (!consumer.isEmpty()) cancel(consumer);
}
void AddonMetadataService::finishCore(quint64 key, const QString& status, const QVariantMap& data)
{
    const auto job = m_core->take(key);
    if (!job || !vlc_metadata_job_IsPending(job.get())) return;
    const auto* request = vlc_metadata_job_GetRequest(job.get());
    auto result = request->item ? input_item_Copy(request->item) : nullptr;
    int code = nativeStatus(status);
    if (request->item && !result) code = VLC_ENOMEM;
    const auto fields = data.value(QStringLiteral("fields")).toMap();
    if (result) for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
        const auto native = nativeFields().constFind(it.key());
        if (native == nativeFields().cend()) continue;
        const auto value = it.value().toString().toUtf8();
        if (!value.isEmpty() && value.size() <= 65536) input_item_SetMeta(result, *native, value.constData());
    }
    auto provenance = data;
    provenance.insert(QStringLiteral("status"), status);
    const auto serialized = QJsonDocument::fromVariant(provenance).toJson(QJsonDocument::Compact);
    // Complete can call a native consumer which destroys this service.
    vlc_metadata_job_Complete(job.get(), code, result, serialized.constData(), serialized.size());
    if (result) input_item_Release(result);
}
