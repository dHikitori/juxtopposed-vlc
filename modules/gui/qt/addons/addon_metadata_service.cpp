// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include "addon_metadata_service.hpp"
#include "addon_metadata_choices.hpp"
#include "../../../../src/preparser/metadata_local.h"
#include <vlc_metadata_backend.h>
#include <vlc_artwork_policy.h>
#include "addon_manager.hpp"
#include "addon_artwork_assets.hpp"
#include "addon_native_artwork_assets.hpp"
#include "util/media_identity_vlc.hpp"
#include <QCryptographicHash>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QScopeGuard>
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
QVariantMap publicNativeIdentity(input_item_t* item, const QUrl& url, const QVariantMap& explicitIdentity = {})
{
    bool locatorEvidence = false;
    if (MediaIdentity::isSourceLocator(url)) {
        const auto provenance = explicitIdentity.value(QStringLiteral("provenance")).toMap();
        for (const auto& value : provenance)
            if (value == QStringLiteral("filename") || value == QStringLiteral("folder")) locatorEvidence = true;
        QVariantList titles = explicitIdentity.value(QStringLiteral("titles")).toList();
        titles.append(explicitIdentity.value(QStringLiteral("title")));
        titles.append(explicitIdentity.value(QStringLiteral("aliases")).toList());
        const auto payload = MediaIdentity::compact(url.path());
        for (const auto& value : titles) {
            const auto title = value.toString().trimmed();
            if (MediaIdentity::isSourceLocator(QUrl(title))
                || (!payload.isEmpty() && MediaIdentity::compact(title) == payload)) locatorEvidence = true;
        }
    }
    if (!explicitIdentity.isEmpty() && !locatorEvidence) return explicitIdentity;
    // Source contexts use native display tags. A context must not restore
    // filename evidence from a private source locator.
    return MediaIdentity::toMap(MediaIdentity::identify(url, MediaIdentity::inputMetadata(item)));
}
int nativeStatus(const QString& status)
{
    if (status == QStringLiteral("match") || status == QStringLiteral("partial") || status == QStringLiteral("no_match")) return VLC_SUCCESS;
    if (status == QStringLiteral("unsupported")) return VLC_ENOTSUP;
    if (status == QStringLiteral("auth_required")) return VLC_EACCES;
    return VLC_EGENERIC;
}
bool automaticNativeArtwork(vlc_metadata_job_t* job)
{
    if (!job || !vlc_metadata_job_IsPending(job)) return false;
    const auto* request = vlc_metadata_job_GetRequest(job);
    return (request->flags & VLC_METADATA_REQUEST_NETWORK) && (request->flags & VLC_METADATA_REQUEST_AUTOMATIC)
        && request->item && !vlc_artwork_item_IsRestricted(request->item)
        && !strcmp(request->feature, "artwork") && !strcmp(request->operation, "enrich");
}
}

struct AddonMetadataService::CoreState {
    std::mutex lock;
    AddonMetadataService* owner = nullptr;
    QHash<quint64, NativeJob> jobs;
    QHash<vlc_metadata_job_t*, quint64> keys;
    QHash<quint64, QString> scopes;
    QHash<QString, quint64> scopeKeys;
    QHash<quint64, AddonNativeArtworkAssets::Cancel> nativeCancels;
    quint64 sequence = 0;
    std::atomic_bool automaticAllowed{false};
    vlc::addons::AddonStreamLifetime::Handle lifetime;
    bool applicationBound = false;
    bool mayPostLocked() const { return !applicationBound || lifetime.current(); }

    static int dispatch(void* opaque, vlc_metadata_job_t* job)
    {
        const auto state = *static_cast<std::shared_ptr<CoreState>*>(opaque);
        const auto* request = vlc_metadata_job_GetRequest(job);
        if (!(request->flags & VLC_METADATA_REQUEST_NETWORK)
            || ((request->flags & VLC_METADATA_REQUEST_AUTOMATIC) && !state->automaticAllowed.load())) return VLC_EACCES;
        std::lock_guard<std::mutex> guard(state->lock);
        if (!state->owner || !state->mayPostLocked()) return -EINTR;
        const auto key = ++state->sequence;
        state->jobs.insert(key, holdJob(job)); state->keys.insert(job, key);
        if (!vlc_metadata_job_IsPending(job)) {
            state->jobs.remove(key); state->keys.remove(job); return VLC_SUCCESS;
        }
        const bool queued = QMetaObject::invokeMethod(state->owner, [state, key] {
            AddonMetadataService* owner;
            { std::lock_guard<std::mutex> guard(state->lock); owner = state->mayPostLocked() ? state->owner : nullptr; }
            if (owner) owner->startCore(key);
        }, Qt::QueuedConnection);
        if (!queued) { state->jobs.remove(key); state->keys.remove(job); }
        return queued ? VLC_SUCCESS : VLC_EGENERIC;
    }
    static void cancel(void* opaque, vlc_metadata_job_t* job)
    {
        const auto state = *static_cast<std::shared_ptr<CoreState>*>(opaque);
        NativeJob held;
        AddonNativeArtworkAssets::Cancel cancelNative;
        {
            std::lock_guard<std::mutex> guard(state->lock);
            const auto key = state->keys.take(job);
            if (!key) return;
            held = state->jobs.take(key);
            state->scopeKeys.remove(state->scopes.take(key));
            cancelNative = state->nativeCancels.take(key);
            if (state->owner && state->mayPostLocked()) QMetaObject::invokeMethod(state->owner, [state, key] {
                AddonMetadataService* owner;
                { std::lock_guard<std::mutex> guard(state->lock); owner = state->mayPostLocked() ? state->owner : nullptr; }
                if (owner) owner->cancelCore(key);
            }, Qt::QueuedConnection);
        }
        // Native cancellation must not wait for delivery on the GUI thread.
        if (cancelNative) cancelNative();
        // held has no Qt dependency. Its reference ends when this call ends.
    }
    static void destroy(void* opaque)
    { delete static_cast<std::shared_ptr<CoreState>*>(opaque); }
    NativeJob find(quint64 key)
    { std::lock_guard<std::mutex> guard(lock); return jobs.value(key); }
    QString issueScope(quint64 key)
    {
        const auto candidate = nonce("native-anchor:");
        std::lock_guard<std::mutex> guard(lock);
        if (!mayPostLocked() || !jobs.contains(key)) return {};
        if (!scopes.contains(key)) { scopes.insert(key, candidate); scopeKeys.insert(candidate, key); }
        return scopes.value(key);
    }
    quint64 scopeKey(const QString& scope)
    { std::lock_guard<std::mutex> guard(lock); return scopeKeys.value(scope); }
    NativeJob take(quint64 key)
    {
        NativeJob job;
        AddonNativeArtworkAssets::Cancel retired;
        {
            std::lock_guard<std::mutex> guard(lock);
            job = jobs.take(key);
            if (job) keys.remove(job.get());
            scopeKeys.remove(scopes.take(key));
            retired = nativeCancels.take(key);
        }
        return job;
    }
    void setNativeCancel(quint64 key, AddonNativeArtworkAssets::Cancel cancel)
    {
        {
            std::lock_guard<std::mutex> guard(lock);
            if (jobs.contains(key)) { nativeCancels.insert(key, std::move(cancel)); return; }
        }
        // Cancellation can win between resolve() and callback registration.
        if (cancel) cancel();
    }
    void addNativeCancel(quint64 key, std::function<void()> cancel)
    {
        {
            std::lock_guard<std::mutex> guard(lock);
            if (jobs.contains(key)) {
                auto prior = nativeCancels.take(key);
                nativeCancels.insert(key, [prior=std::move(prior), cancel=std::move(cancel)] {
                    if (prior) prior();
                    if (cancel) cancel();
                });
                return;
            }
        }
        if (cancel) cancel();
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
        QPointer<AddonArtworkAssets> assets(new AddonArtworkAssets(manager,
            QDir(manager->storageDirectory()).filePath(QStringLiteral("artwork-cache")), this));
        setArtworkAdapter([assets](const QString& provider, const QVariant& descriptor, ArtworkCallback completed) {
            return assets ? assets->resolve(provider, descriptor, std::move(completed)) : QString{};
        }, [assets](const QString& request) { if (assets) assets->cancel(request); });
    }
}
AddonMetadataService::AddonMetadataService(Backend backend, QObject* parent)
    : QObject(parent), m_backend(std::move(backend)), m_coordinator({
          [owner=QPointer<AddonMetadataService>(this)](const QString& feature) {
              return owner ? owner->providers(feature) : QVariantList{};
          },
          [owner=QPointer<AddonMetadataService>(this)](const QString& id, const QString& operation, const QVariantMap& context) {
              return owner ? owner->invoke(id, operation, context) : QString{};
          },
          [owner=QPointer<AddonMetadataService>(this), portableCancel=m_backend.providers.cancel](const QString& invocation) {
              if (owner) owner->cancelInvocation(invocation);
              else if (portableCancel) portableCancel(invocation);
          },
          [owner=QPointer<AddonMetadataService>(this)](const QString& id) {
              return owner && owner->m_builtins.value(id).descriptor.value(QStringLiteral("requiresNativeAnchor")).toBool();
          },
          [owner=QPointer<AddonMetadataService>(this)](const QString& id, const QString& operation, const QVariantMap& context, const QString& scope) {
              return owner ? owner->invoke(id, operation, context, scope) : QString{};
          },
          [owner=QPointer<AddonMetadataService>(this)](const QString& feature, const QString& operation,
              const QVariantList& rows, const QString& scope) {
              if (!owner || !owner->m_planOwner || !owner->m_providerPlan) return rows;
              const auto plan = owner->m_providerPlan;
              const auto job = owner->m_core->find(owner->m_core->scopeKey(scope));
              const bool native = automaticNativeArtwork(job.get());
              return plan(feature, operation, rows, native);
          },
          [owner=QPointer<AddonMetadataService>(this)](const QString& id, const QVariantMap& data,
              const QString& scope) -> std::optional<AddonMetadataCoordinator::NativeArtwork> {
              if (!owner || owner->m_closed || !owner->applicationCurrent()) return {};
              const auto found = owner->m_builtins.constFind(id);
              if (found == owner->m_builtins.cend() || !found->lifetime || !found->artwork.validate
                  || !found->descriptor.value(QStringLiteral("requiresNativeAnchor")).toBool()) return {};
              const auto job = owner->m_core->find(owner->m_core->scopeKey(scope));
              if (!automaticNativeArtwork(job.get())) return {};
              const auto validate = found->artwork.validate;
              return validate(data, job.get());
          }}, this),
      m_core(std::make_shared<CoreState>())
{
    const QPointer<AddonMetadataService> owner(this);
    m_choices = std::make_unique<AddonMetadataChoices>(AddonMetadataChoices::Backend{
        [owner](const QString& media) -> std::optional<QVariantMap> {
            return owner ? owner->identitySubject(media) : std::nullopt;
        },
        [owner](const QString& media) {
            return owner && owner->identitySubject(media) && owner->holdMedia(media);
        },
        [owner](const QString& media) { if (owner) owner->releaseMedia(media); },
        [owner](const QString& feature, const QString& id) {
            const auto rows = owner ? owner->providers(feature) : QVariantList{};
            for (const auto& value : rows) if (value.toMap().value(QStringLiteral("id")) == id) return value.toMap();
            return QVariantMap{};
        }});
    m_core->owner = this;
    const vlc_metadata_backend_operations operations{CoreState::dispatch, CoreState::cancel, CoreState::destroy};
    auto* state = new std::shared_ptr<CoreState>(m_core);
    m_coreBackend = vlc_metadata_backend_New(&operations, state);
    if (!m_coreBackend) delete state;
    connect(&m_coordinator, &AddonMetadataCoordinator::finished, this, &AddonMetadataService::coordinatorFinished);
    connect(&m_coordinator, &AddonMetadataCoordinator::canceled, this, [this](const QString& consumer) {
        retireConsumer(consumer);
    });
}
AddonMetadataService::~AddonMetadataService()
{
    { std::lock_guard<std::mutex> lock(m_core->lock); m_core->owner = nullptr; }
    shutdown();
    // A callback can enter destruction during invalidate(). Drain here even
    // when its reentry guard prevented the ordinary shutdown invalidation.
    // All cancellation maps must remain alive until the coordinator is empty.
    m_coordinator.invalidate();
    const auto consumers = m_consumers.keys();
    for (const auto& consumer : consumers) retireConsumer(consumer);
    QHash<QString, NativeCall> calls;
    calls.swap(m_nativeCalls);
    for (auto it = calls.cbegin(); it != calls.cend(); ++it)
        if (it->lifetime && it->cancel) it->cancel(it.key());
    if (m_coreBackend) {
        vlc_metadata_backend_Close(m_coreBackend);
        vlc_metadata_backend_Release(m_coreBackend);
    }
}

QVariantList AddonMetadataService::providers(const QString& feature) const
{
    const auto query = m_backend.providers.providersForFeature;
    const QPointer<const AddonMetadataService> owner(this);
    auto list = query ? query(feature) : QVariantList{};
    if (!owner) return {};

    QSet<QString> registered;
    for (const auto& entry : list) registered.insert(entry.toMap().value(QStringLiteral("id")).toString());
    for (auto it = m_builtins.cbegin(); it != m_builtins.cend(); ++it)
        if (it->lifetime && it->features.contains(feature) && !registered.contains(it.key())) list.append(it->descriptor);
    return list;
}
bool AddonMetadataService::registerBuiltinProvider(const QString& feature, const QVariantMap& descriptor,
    QObject* lifetime, NativeInvoke invoke, NativeCancel cancel, NativeArtworkHandlers artwork)
{
    const auto id = descriptor.value(QStringLiteral("id")).toString();
    if (m_closed || !lifetime || !invoke || feature.isEmpty() || id.isEmpty() || m_builtins.size() >= 128) return false;
    QPointer<AddonMetadataService> guard(this);
    const auto existing = m_builtins.constFind(id);
    if (existing != m_builtins.cend() && existing->lifetime != lifetime) return false;
    const auto retired = existing == m_builtins.cend() ? Builtin{} : *existing;
    auto builtin = retired;
    if (!builtin.features.contains(feature)) builtin.features.append(feature);
    builtin.descriptor = descriptor;
    builtin.descriptor.insert(QStringLiteral("builtin"), true);
    builtin.descriptor.insert(QStringLiteral("features"), builtin.features);
    builtin.lifetime = lifetime; builtin.invoke = std::move(invoke); builtin.cancel = std::move(cancel);
    builtin.artwork = std::move(artwork); builtin.binding = nonce("builtin-binding:");
    if (retired.destroyed) disconnect(retired.destroyed);
    const auto binding = builtin.binding;
    builtin.destroyed = connect(lifetime, &QObject::destroyed, this, [this, id, binding] {
        if (m_builtins.value(id).binding == binding) unregisterBuiltinProvider(id);
    });
    m_builtins.insert(id, builtin);
    const auto registerBuiltin = m_backend.registerBuiltin;
    if (registerBuiltin) registerBuiltin(feature, builtin.descriptor);
    if (!guard) return false;
    invalidate();
    return true;
}
void AddonMetadataService::unregisterBuiltinProvider(const QString& contribution)
{
    QPointer<AddonMetadataService> guard(this);
    if (!m_builtins.contains(contribution)) return;
    const auto retired = m_builtins.take(contribution);
    if (retired.destroyed) disconnect(retired.destroyed);
    const auto unregisterBuiltin = m_backend.unregisterBuiltin;
    if (unregisterBuiltin) unregisterBuiltin(contribution);
    if (!guard) return;
    invalidate();
}
void AddonMetadataService::unregisterBuiltinProviderForOwner(const QString& id, QObject* exactOwner)
{
    const auto found = m_builtins.constFind(id);
    if (found != m_builtins.cend() && found->lifetime == exactOwner) unregisterBuiltinProvider(id);
}
void AddonMetadataService::setProviderPlan(QObject* lifetime, ProviderPlan plan)
{
    if (m_closed || !lifetime || !plan) return;
    const auto retired = std::move(m_providerPlan);
    m_planOwner = lifetime; m_providerPlan = std::move(plan);
    invalidate();
}
void AddonMetadataService::clearProviderPlan(QObject* exactOwner)
{
    if (m_planOwner != exactOwner) return;
    const auto retired = std::move(m_providerPlan);
    m_planOwner.clear(); invalidate();
}
QString AddonMetadataService::invoke(const QString& contribution, const QString& operation,
                                     const QVariantMap& context, const QString& privateScope)
{
    if (m_closed || !applicationCurrent()) return {};
    const auto found = m_builtins.constFind(contribution);
    if (found == m_builtins.cend()) {
        auto projection = context;
        projection.remove(QStringLiteral("media"));
        const auto invokePortable = m_backend.providers.invoke;
        return invokePortable ? invokePortable(contribution, operation, projection) : QString{};
    }
    if (!found->lifetime) return {};
    const bool native = found->descriptor.value(QStringLiteral("requiresNativeAnchor")).toBool();
    const auto core = native ? m_core->scopeKey(privateScope) : 0;
    if (native && !core) return {};
    const auto id = nonce("native-metadata:");
    m_nativeCalls.insert(id, {contribution, context.value(QStringLiteral("media")).toString(), found->lifetime, found->cancel, core});
    const auto callback = found->invoke;
    // The coordinator records the returned ID before a native handler can
    // complete inline. This event owns its capture on the Qt owner thread.
    QTimer::singleShot(0, this, [this, id, operation, context, callback] {
        const auto call = m_nativeCalls.constFind(id);
        if (call == m_nativeCalls.cend()) return;
        if (m_closed || !applicationCurrent() || !call->lifetime) {
            completeBuiltin(id, QStringLiteral("unsupported")); return;
        }
        callback(id, operation, context);
    });
    return id;
}
void AddonMetadataService::cancelInvocation(const QString& invocation)
{
    const auto call = m_nativeCalls.take(invocation);
    if (call.contribution.isEmpty()) {
        const auto cancel = m_backend.providers.cancel;
        if (cancel) cancel(invocation);
        return;
    }
    if (call.lifetime && call.cancel) call.cancel(invocation);
}
void AddonMetadataService::completeBuiltin(const QString& invocation, const QString& status,
                                          const QVariantMap& data, const QString& error)
{
    const auto call = m_nativeCalls.take(invocation);
    if (call.contribution.isEmpty()) return;
    m_coordinator.operationFinished(invocation, call.contribution, status, data, error);
}
QUrl AddonMetadataService::privateUrlForInvocation(const QString& invocation) const
{
    const auto call = m_nativeCalls.constFind(invocation);
    return call == m_nativeCalls.cend() ? QUrl{} : m_media.value(call->media).url;
}
vlc_metadata_job_t* AddonMetadataService::holdNativeJobForInvocation(const QString& invocation) const
{
    if (m_closed || !applicationCurrent()) return nullptr;
    const auto call = m_nativeCalls.constFind(invocation);
    if (call == m_nativeCalls.cend() || !call->lifetime || !call->core) return nullptr;
    const auto job = m_core->find(call->core);
    if (!job || !vlc_metadata_job_IsPending(job.get())
        || !(vlc_metadata_job_GetRequest(job.get())->flags & VLC_METADATA_REQUEST_NETWORK)) return nullptr;
    return vlc_metadata_job_Hold(job.get());
}
void AddonMetadataService::setNativeCancellationForInvocation(const QString& invocation, std::function<void()> cancel)
{
    const auto call = m_nativeCalls.constFind(invocation);
    const auto core = !m_closed && applicationCurrent() && call != m_nativeCalls.cend() && call->lifetime ? call->core : 0;
    const auto state = m_core;
    if (core) state->addNativeCancel(core, std::move(cancel));
    else if (cancel) cancel();
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
    if (m_closed || !applicationCurrent() || !url.isValid() || url.isEmpty() || url.toEncoded().size() > 32768 || serialized.size() > 65536) return {};
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
bool AddonMetadataService::identityCurrent(const QString& reference) const
{
    if (m_closed || m_identityInvalidating || m_invalidateActive || !applicationCurrent()) return false;
    const auto found = m_identityTargets.constFind(reference);
    if (found == m_identityTargets.cend()) return false;
    const auto& value = *found;
    const auto* request = vlc_metadata_job_GetRequest(value.anchor.get());
    return (request->flags & VLC_METADATA_REQUEST_NETWORK)
        && (!(request->flags & VLC_METADATA_REQUEST_AUTOMATIC) || automaticNetworkAllowed())
        && vlc_metadata_job_IsCurrentFor(value.anchor.get(), value.target.get());
}
std::optional<QVariantMap> AddonMetadataService::identitySubject(const QString& media) const
{
    const auto reference = m_identityMedia.value(media);
    if (reference.isEmpty() || !identityCurrent(reference)) return {};
    const auto value = m_identityTargets.value(reference);
    // The anchor seals the native URI, policy, owner, and revision tokens.
    // Only this host binding uses the nonce. Providers and views never receive it.
    return QVariantMap{{QStringLiteral("nativeSubject"), value.nonce}};
}
QString AddonMetadataService::bindIdentityTarget(vlc_metadata_job_t* anchor, input_item_t* target,
                                                const QVariantMap& hostContext)
{
    if (!anchor || !target || m_closed || m_identityInvalidating || m_invalidateActive || !applicationCurrent()
        || m_identityTargets.size() >= 64 || m_media.size() >= 8192
        || QJsonDocument::fromVariant(hostContext).toJson(QJsonDocument::Compact).size() > 32768
        || !vlc_metadata_job_IsCurrentFor(anchor, target)) return {};
    const auto* request = vlc_metadata_job_GetRequest(anchor);
    if (!request->item || !(request->flags & VLC_METADATA_REQUEST_NETWORK)
        || ((request->flags & VLC_METADATA_REQUEST_AUTOMATIC) && !automaticNetworkAllowed())) return {};
    char* address = input_item_GetURI(request->item);
    const auto url = address ? QUrl(QString::fromUtf8(address)) : QUrl{}; free(address);
    if (url.isEmpty() || !url.isValid() || url.toEncoded().size() > 32768) return {};
    const auto entity = publicNativeIdentity(request->item, url, MediaIdentity::isSourceLocator(url)
        ? hostContext.value(QStringLiteral("identity")).toMap() : QVariantMap{});
    const auto reference = nonce("metadata-target:");
    const auto media = nonce("metadata-media:");
    auto context = hostContext;
    context.insert(QStringLiteral("media"), media);
    context.insert(QStringLiteral("identity"), entity);
    context.insert(QStringLiteral("generation"), QString::number(request->generation));
    context.insert(QStringLiteral("targetRevision"), QString::number(request->target_revision));
    context.insert(QStringLiteral("automatic"), bool(request->flags & VLC_METADATA_REQUEST_AUTOMATIC));
    m_media.insert(media, {url, entity, {}, 1});
    m_identityTargets.insert(reference, {holdJob(anchor),
        std::shared_ptr<input_item_t>(input_item_Hold(target), input_item_Release), media, nonce("native-subject:"), context});
    m_identityMedia.insert(media, reference);
    return reference;
}
void AddonMetadataService::retireIdentityTargets(const QString& reference)
{
    if (m_identityInvalidating && (!reference.isEmpty() || m_identityTargets.isEmpty())) return;
    const bool outerRetirement = m_identityInvalidating;
    const QPointer<AddonMetadataService> owner(this);
    m_identityInvalidating = true;
    QList<IdentityTarget> retired;
    const auto references = reference.isEmpty() ? m_identityTargets.keys() : QStringList{reference};
    QSet<QString> removed;
    for (const auto& id : references) {
        if (!m_identityTargets.contains(id)) continue;
        const auto value = m_identityTargets.take(id); removed.insert(id);
        m_identityMedia.remove(value.media); retired.append(value);
        m_choices->invalidateMedia(value.media); releaseMedia(value.media);
    }
    QStringList consumers;
    for (auto it = m_identityRequests.cbegin(); it != m_identityRequests.cend(); ++it)
        if (removed.contains(it->target)) consumers.append(it.key());
    // Finish every detached native obligation even if one callback deletes us.
    for (const auto& value : retired) vlc_metadata_job_Cancel(value.anchor.get());
    if (!owner) return;
    for (const auto& id : consumers) { cancel(id); if (!owner) return; }
    m_identityInvalidating = outerRetirement;
}
void AddonMetadataService::releaseIdentityTarget(const QString& target)
{ retireIdentityTargets(target); }
QString AddonMetadataService::identify(const QString& featureInput, const QString& targetInput)
{
    const auto feature = featureInput, target = targetInput;
    if (!identityCurrent(target)) return {};
    const auto value = m_identityTargets.value(target);
    if (!holdMedia(value.media)) return {};
    const QPointer<AddonMetadataService> owner(this);
    const auto id = m_coordinator.identify(feature, value.context);
    if (!owner) return id;
    if (!identityCurrent(target)) { releaseMedia(value.media); m_coordinator.cancel(id); return id; }
    m_consumers.insert(id, {value.media, 0, value.context.value(QStringLiteral("automatic")).toBool()});
    m_identityRequests.insert(id, {target, {}, feature, value.context});
    return id;
}
QString AddonMetadataService::acceptIdentity(const QString& target, const QString& choice)
{
    if (!identityCurrent(target)) return {};
    const auto context = m_identityTargets.value(target).context;
    return m_choices->select(choice, context);
}
QVariantMap AddonMetadataService::selectedIdentity(const QString& target, const QString& selection) const
{
    if (!identityCurrent(target)) return {};
    return m_choices->selected(selection, m_identityTargets.value(target).context);
}
QString AddonMetadataService::enrichSelected(const QString& featureInput, const QString& targetInput,
                                            const QString& selectionInput, const QStringList& fieldsInput)
{
    const auto feature = featureInput, target = targetInput, selection = selectionInput;
    const auto fields = fieldsInput;
    const QPointer<AddonMetadataService> owner(this);
    const auto accepted = selectedIdentity(target, selection);
    if (!owner || accepted.isEmpty() || !identityCurrent(target)) return {};
    const auto value = m_identityTargets.value(target);
    auto context = value.context; context.insert(QStringLiteral("identity"), accepted.value(QStringLiteral("identity")));
    if (!holdMedia(value.media)) return {};
    const auto id = m_coordinator.enrich(feature, context, fields);
    if (!owner) return id;
    if (!identityCurrent(target)) { releaseMedia(value.media); m_coordinator.cancel(id); return id; }
    m_consumers.insert(id, {value.media, 0, context.value(QStringLiteral("automatic")).toBool()});
    m_identityRequests.insert(id, {target, selection, feature, context});
    return id;
}
vlc_metadata_job_t* AddonMetadataService::holdIdentityAnchor(const QString& targetInput, const QString& selectionInput) const
{
    const auto target = targetInput, selection = selectionInput;
    const QPointer<const AddonMetadataService> owner(this);
    if (selectedIdentity(target, selection).isEmpty() || !owner || !identityCurrent(target)) return nullptr;
    return vlc_metadata_job_Hold(m_identityTargets.value(target).anchor.get());
}
void AddonMetadataService::releaseIdentityChoiceSet(const QString& set) { m_choices->releaseSet(set); }
void AddonMetadataService::releaseIdentitySelection(const QString& selection) { m_choices->releaseSelection(selection); }

QString AddonMetadataService::searchAssets(const QString& feature, const QVariantMap& context, bool aggregate)
{ return start(feature, context, {}, aggregate, true); }
QString AddonMetadataService::enrich(const QString& feature, const QVariantMap& context, const QStringList& fields)
{ return start(feature, context, fields, false, false); }
QString AddonMetadataService::start(const QString& feature, QVariantMap context, const QStringList& fields,
                                    bool aggregate, bool assets, const QString& privateScope)
{
    const auto media = context.value(QStringLiteral("media")).toString();
    const bool automatic = context.value(QStringLiteral("automatic")).toBool();
    if (m_closed || !applicationCurrent() || (automatic && !automaticNetworkAllowed()) || !holdMedia(media)) {
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
    const QPointer<AddonMetadataService> owner(this);
    const auto consumer = assets ? m_coordinator.searchAssets(feature, context, aggregate)
        : privateScope.isEmpty() ? m_coordinator.enrich(feature, context, fields)
        : m_coordinator.enrichNative(feature, context, fields, privateScope);
    if (!owner) return consumer;
    m_consumers.insert(consumer, {media, 0, automatic});
    return consumer;
}
void AddonMetadataService::cancel(const QString& consumer)
{
    if (!m_consumers.contains(consumer)) return;
    const QPointer<AddonMetadataService> alive(this);
    m_coordinator.cancel(consumer);
    if (!alive) return;
    // A result can wait for an asset after its coordinator request ends.
    retireConsumer(consumer);
}
void AddonMetadataService::retireConsumer(const QString& consumer)
{
    if (!m_consumers.contains(consumer)) return;
    const QPointer<AddonMetadataService> alive(this);
    const auto record = m_consumers.take(consumer);
    m_identityRequests.remove(consumer);
    const auto result = m_results.take(consumer);
    releaseMedia(record.media);
    NativeJob job;
    if (record.core) {
        m_coreConsumers.remove(record.core);
        job = m_core->take(record.core);
    }
    // Complete owner state before a native callback can delete the service.
    if (job) vlc_metadata_job_Cancel(job.get());
    // Detached work must still end if a cancellation destroys the owner.
    for (const auto& asset : result.assets) if (asset.cancel) asset.cancel();
    if (alive && !record.core) emit canceled(consumer);
}
void AddonMetadataService::invalidate()
{
    if (m_invalidateActive) return;
    QPointer<AddonMetadataService> guard(this);
    m_invalidateActive = true;
    const auto reset = qScopeGuard([guard] { if (guard) guard->m_invalidateActive = false; });
    retireIdentityTargets();
    if (!guard) return;
    if (m_coreBackend) {
        const auto backend = std::shared_ptr<vlc_metadata_backend_t>(
            vlc_metadata_backend_Hold(m_coreBackend), vlc_metadata_backend_Release);
        // A completed child can still wait in a retained parent anchor.
        // Provider changes must revoke that unpublished automatic result too.
        vlc_metadata_backend_CancelAutomatic(backend.get());
        if (!guard) return;
    }
    const auto consumers = m_consumers.keys();
    for (const auto& consumer : consumers) { cancel(consumer); if (!guard) return; }
    m_coordinator.invalidate();
    if (!guard) return;
    emit invalidated();
}
void AddonMetadataService::setArtworkAdapter(ArtworkResolver resolver, std::function<void(const QString&)> cancel)
{
    QPointer<AddonMetadataService> guard(this);
    invalidate();
    if (!guard) return;
    const auto retiredResolve = std::move(m_backend.resolveArtwork);
    const auto retiredCancel = std::move(m_backend.cancelArtwork);
    m_backend.resolveArtwork = std::move(resolver);
    m_backend.cancelArtwork = std::move(cancel);
}
void AddonMetadataService::setNativeArtworkAdapter(AddonNativeArtworkAssets* adapter)
{
    if (m_closed || m_nativeArtwork == adapter) return;
    QPointer<AddonMetadataService> guard(this);
    QList<NativeJob> jobs;
    {
        std::lock_guard<std::mutex> lock(m_core->lock);
        for (auto it = m_core->nativeCancels.cbegin(); it != m_core->nativeCancels.cend(); ++it)
            if (const auto job = m_core->jobs.value(it.key())) jobs.append(job);
    }
    m_nativeArtwork = adapter;
    if (adapter) adapter->setNetworkAllowed(automaticNetworkAllowed());
    if (!guard) return;
    for (const auto& job : jobs) { vlc_metadata_job_Cancel(job.get()); if (!guard) return; }
}
void AddonMetadataService::clearNativeArtworkAdapterIf(AddonNativeArtworkAssets* exact)
{ if (m_nativeArtwork == exact) setNativeArtworkAdapter(nullptr); }
bool AddonMetadataService::reserveNativeOwner(QObject* owner)
{
    if (!owner || m_closed || m_nativeOwner || !applicationLifetime().current()
        || !m_coreBackend || vlc_metadata_backend_IsClosed(m_coreBackend)) return false;
    m_nativeOwner = owner; return true;
}
bool AddonMetadataService::nativeOwnerIs(QObject* owner) const
{ return owner && m_nativeOwner == owner; }
void AddonMetadataService::releaseNativeOwnerFor(QObject* owner)
{ if (owner && m_nativeOwner == owner) m_nativeOwner.clear(); }
bool AddonMetadataService::bindApplicationLifetime(vlc::addons::AddonStreamLifetime::Handle lifetime)
{
    if (m_closed || !lifetime.current()) return false;
    std::lock_guard<std::mutex> lock(m_core->lock);
    if (m_core->applicationBound || !m_core->jobs.isEmpty()) return false;
    m_core->lifetime = std::move(lifetime); m_core->applicationBound = true;
    return true;
}
vlc::addons::AddonStreamLifetime::Handle AddonMetadataService::applicationLifetime() const
{
    std::lock_guard<std::mutex> lock(m_core->lock);
    return !m_closed && m_core->applicationBound ? m_core->lifetime : vlc::addons::AddonStreamLifetime::Handle{};
}
bool AddonMetadataService::applicationCurrent() const
{ std::lock_guard<std::mutex> lock(m_core->lock); return m_core->mayPostLocked(); }
bool AddonMetadataService::automaticNetworkAllowed() const { return m_core->automaticAllowed.load(); }
int AddonMetadataService::nativeJobCount() const
{ std::lock_guard<std::mutex> guard(m_core->lock); return m_core->jobs.size(); }
void AddonMetadataService::setAutomaticNetworkAllowed(bool allowed)
{
    QPointer<AddonMetadataService> guard(this);
    if (m_core->automaticAllowed.exchange(allowed) == allowed) return;
    if (!allowed && m_coreBackend) {
        const auto backend = std::shared_ptr<vlc_metadata_backend_t>(
            vlc_metadata_backend_Hold(m_coreBackend), vlc_metadata_backend_Release);
        // Include native anchors which never enter the Qt dispatch map.
        vlc_metadata_backend_CancelAutomatic(backend.get());
        if (!guard) return;
    }
    if (m_nativeArtwork) m_nativeArtwork->setNetworkAllowed(allowed);
    if (!guard) return;
    invalidate();
}
vlc_metadata_backend_t* AddonMetadataService::holdCoreBackend() const
{ return m_coreBackend ? vlc_metadata_backend_Hold(m_coreBackend) : nullptr; }
void AddonMetadataService::shutdown()
{
    if (m_closed) return;
    QPointer<AddonMetadataService> guard(this);
    m_closed = true;
    retireIdentityTargets();
    if (!guard) return;
    const auto retiredPlan = std::move(m_providerPlan);
    m_planOwner.clear(); m_nativeOwner.clear();
    { std::lock_guard<std::mutex> guard(m_core->lock); m_core->owner = nullptr; }
    if (m_coreBackend) vlc_metadata_backend_Close(m_coreBackend);
    if (!guard) return;
    emit retiring();
    if (!guard) return;
    invalidate();
    if (!guard) return;
    QHash<QString, Builtin> retired;
    retired.swap(m_builtins);
    for (const auto& builtin : retired) if (builtin.destroyed) disconnect(builtin.destroyed);
    const auto builtins = retired.keys();
    const auto unregisterBuiltin = m_backend.unregisterBuiltin;
    for (const auto& id : builtins) {
        if (unregisterBuiltin) unregisterBuiltin(id);
        if (!guard) return;
    }
    m_media.clear(); m_mediaKeys.clear();
}

void AddonMetadataService::coordinatorFinished(const QString& consumer, const QString& status,
                                              const QVariantMap& data, const QString& error)
{
    if (!m_consumers.contains(consumer)) return;
    if (!applicationCurrent()) { cancel(consumer); return; }
    const auto identityRequest = m_identityRequests.value(consumer);
    if (!identityRequest.target.isEmpty()) {
        const QPointer<AddonMetadataService> owner(this);
        if (!identityCurrent(identityRequest.target)) { cancel(consumer); return; }
        if (identityRequest.selection.isEmpty()) {
            auto result = data;
            result.remove(QStringLiteral("candidates"));
            if (status == QStringLiteral("match") || status == QStringLiteral("ambiguous") || status == QStringLiteral("partial")) {
                const auto offer = m_choices->issue(identityRequest.feature, identityRequest.context,
                    data.value(QStringLiteral("candidates")).toList());
                if (!owner) return;
                if (!m_consumers.contains(consumer)) {
                    m_choices->releaseSet(offer.value(QStringLiteral("choiceSet")).toString()); return;
                }
                for (auto it = offer.cbegin(); it != offer.cend(); ++it) result.insert(it.key(), it.value());
                if (!offer.contains(QStringLiteral("choiceSet"))) { deliver(consumer, QStringLiteral("unsupported"), result,
                    offer.value(QStringLiteral("error")).toString()); return; }
            }
            deliver(consumer, status, result, error); return;
        }
        if (selectedIdentity(identityRequest.target, identityRequest.selection).isEmpty()) {
            if (owner) cancel(consumer); return;
        }
        if (!owner) return;
    }
    auto fields = data.value(QStringLiteral("fields")).toMap();
    QStringList assets;
    for (const auto& field : {QStringLiteral("artwork"), QStringLiteral("artworkUrl")}) if (fields.contains(field)) assets.append(field);
    if (assets.isEmpty()) { deliver(consumer, status, data, error); return; }
    m_results.insert(consumer, {status, error, data, {}, int(assets.size())});
    for (const auto& field : assets) {
        const auto provider = data.value(QStringLiteral("provenance")).toMap().value(field).toMap().value(QStringLiteral("provider")).toString();
        const QPointer<AddonMetadataService> target(this);
        auto complete = [target, consumer, field](const QUrl& url, const QString& error) {
            if (target) QMetaObject::invokeMethod(target, [target, consumer, field, url, error] {
                if (target) target->artworkFinished(consumer, field, url, error);
            }, Qt::QueuedConnection);
        };
        const auto builtin = m_builtins.value(provider);
        const bool native = builtin.descriptor.value(QStringLiteral("requiresNativeAnchor")).toBool();
        const auto nativeResolve = builtin.artwork.resolve;
        const auto portableResolve = m_backend.resolveArtwork;
        const auto portableCancel = m_backend.cancelArtwork;
        const auto core = m_consumers.value(consumer).core;
        const auto state = m_core;
        const auto anchor = core ? state->find(core) : NativeJob{};
        PendingResult::Asset asset;
        bool adapter = false;
        if (native) {
            adapter = builtin.lifetime && nativeResolve && automaticNativeArtwork(anchor.get());
            if (adapter) {
                auto request = nativeResolve(fields.value(field), anchor.get(), complete);
                asset.id = std::move(request.id); asset.cancel = std::move(request.cancel);
            }
        } else {
            adapter = bool(portableResolve);
            asset.id = adapter ? portableResolve(provider, fields.value(field), complete) : QString{};
            if (!asset.id.isEmpty() && portableCancel) asset.cancel = [id=asset.id, portableCancel] { portableCancel(id); };
        }
        // A resolver can withdraw the consumer or destroy this service before
        // it returns its request. Retire that returned work through its copy.
        if (!target) { if (asset.cancel) asset.cancel(); return; }
        auto result = m_results.find(consumer);
        if (result == m_results.end()) { if (asset.cancel) asset.cancel(); return; }
        result->assets.insert(field, asset);
        if (native && core && asset.cancel) state->addNativeCancel(core, asset.cancel);
        if (!target) return;
        if (asset.id.isEmpty()) complete({}, adapter ? tr("The artwork request did not start.") : tr("No artwork asset adapter is available."));
    }
}
void AddonMetadataService::artworkFinished(const QString& consumer, const QString& field, const QUrl& url, const QString& error)
{
    auto found = m_results.find(consumer);
    if (found == m_results.end() || !found->assets.contains(field)) return;
    const auto retired = found->assets.take(field);
    if (!applicationCurrent() || (m_consumers.value(consumer).automatic && !automaticNetworkAllowed())) { cancel(consumer); return; }
    auto fields = found->data.value(QStringLiteral("fields")).toMap();
    if (url.isLocalFile() && vlc_artwork_uri_IsLocalFile(url.toEncoded().constData())
        && QFileInfo(url.toLocalFile()).isFile() && QFileInfo(url.toLocalFile()).isReadable()) {
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
    const auto identityRequest = m_identityRequests.value(consumer);
    if (!identityRequest.target.isEmpty()) {
        const QPointer<AddonMetadataService> owner(this);
        const bool valid = identityRequest.selection.isEmpty() ? identityCurrent(identityRequest.target)
            : !selectedIdentity(identityRequest.target, identityRequest.selection).isEmpty();
        if (!owner) return;
        if (!valid) { cancel(consumer); return; }
    }
    if (!m_consumers.contains(consumer)) return;
    const auto record = m_consumers.take(consumer);
    m_identityRequests.remove(consumer);
    releaseMedia(record.media);
    if (record.core) { m_coreConsumers.remove(record.core); finishCore(record.core, status, data); return; }
    emit finished(consumer, status, data, error);
}

void AddonMetadataService::startCore(quint64 key)
{
    const auto job = m_core->find(key);
    if (!job) return;
    if (!vlc_metadata_job_IsPending(job.get())) { m_core->take(key); return; }
    const auto* request = vlc_metadata_job_GetRequest(job.get());
    if (m_closed || !applicationCurrent() || ((request->flags & VLC_METADATA_REQUEST_AUTOMATIC) && !automaticNetworkAllowed())) {
        auto owned = m_core->take(key); if (owned) vlc_metadata_job_Cancel(owned.get()); return;
    }
    if (!strcmp(request->operation, "materializeNativeArtwork")) { startNativeArtwork(key); return; }
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
        const auto identity = publicNativeIdentity(request->item, url, context.value(QStringLiteral("identity")).toMap());
        media = acquireMedia(url, identity);
        if (media.isEmpty()) {
            auto owned = m_core->take(key); if (owned) vlc_metadata_job_Complete(owned.get(), VLC_EINVAL, nullptr, nullptr, 0); return;
        }
        context.insert(QStringLiteral("media"), media);
        auto existing = context.value(QStringLiteral("existingFields")).toMap();
        for (auto it = nativeFields().cbegin(); it != nativeFields().cend(); ++it) {
            char* raw = it.value() == vlc_meta_ArtworkURL ? vlc_artwork_item_GetURI(request->item)
                : input_item_GetMeta(request->item, it.value());
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
    const QPointer<AddonMetadataService> owner(this);
    if (operation == QStringLiteral("enrich"))
        consumer = start(QString::fromUtf8(request->feature), context, fields, false, false, m_core->issueScope(key));
    else if (operation == QStringLiteral("searchAssets")) consumer = searchAssets(QString::fromUtf8(request->feature), context, context.value(QStringLiteral("aggregate")).toBool());
    else {
        releaseMedia(media);
        auto owned = m_core->take(key); if (owned) vlc_metadata_job_Complete(owned.get(), VLC_ENOTSUP, nullptr, nullptr, 0); return;
    }
    if (!owner) return;
    releaseMedia(media);
    m_consumers[consumer].core = key; m_coreConsumers.insert(key, consumer);
    if (!vlc_metadata_job_IsPending(job.get())) cancelCore(key);
}
void AddonMetadataService::startNativeArtwork(quint64 key)
{
    const auto job = m_core->find(key);
    if (!job || !vlc_metadata_job_IsPending(job.get())) return;
    const auto* request = vlc_metadata_job_GetRequest(job.get());
    const bool valid = !strcmp(request->feature, "artwork") && request->item
        && (request->flags & VLC_METADATA_REQUEST_AUTOMATIC)
        && request->field_count == 1 && !strcmp(request->fields[0], "artwork");
    if (!valid || !m_nativeArtwork) {
        const auto owned = m_core->take(key);
        if (owned) vlc_metadata_job_Complete(owned.get(), valid ? VLC_ENOTSUP : VLC_EINVAL, nullptr, nullptr, 0);
        return;
    }
    const auto state = m_core;
    auto cancel = m_nativeArtwork->resolve(job.get(), [state, key](const AddonNativeArtworkAssets::Result& asset) {
        const auto owned = state->take(key);
        if (!owned || !vlc_metadata_job_IsPending(owned.get())) return;
        if (!asset.permit.current()) { vlc_metadata_job_Cancel(owned.get()); return; }
        const auto* request = vlc_metadata_job_GetRequest(owned.get());
        auto* result = asset.error.isEmpty() && asset.file.isLocalFile() ? input_item_Copy(request->item) : nullptr;
        int status = result ? VLC_SUCCESS : VLC_EGENERIC;
        if (result) {
            const auto file = asset.file.toEncoded();
            status = vlc_artwork_item_SetApprovedURI(result, file.constData());
        }
        // The parent anchor remains cancel-visible through final publication.
        // Complete can call a native consumer which destroys the Qt service.
        constexpr char provenance[] = "{\"source\":\"native-artwork\"}";
        vlc_metadata_job_Complete(owned.get(), status, result, provenance, sizeof(provenance) - 1);
        if (result) input_item_Release(result);
    });
    if (cancel) state->setNativeCancel(key, std::move(cancel));
    else {
        const auto owned = state->take(key);
        if (owned) vlc_metadata_job_Complete(owned.get(), VLC_ENOTSUP, nullptr, nullptr, 0);
    }
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
        if (!value.isEmpty() && value.size() <= 65536) {
            if (*native == vlc_meta_ArtworkURL) {
                if (vlc_artwork_item_SetApprovedURI(result, value.constData()) != VLC_SUCCESS) code = VLC_EINVAL;
            } else input_item_SetMeta(result, *native, value.constData());
        }
    }
    auto provenance = data;
    provenance.insert(QStringLiteral("status"), status);
    const auto serialized = QJsonDocument::fromVariant(provenance).toJson(QJsonDocument::Compact);
    // Complete can call a native consumer which destroys this service.
    vlc_metadata_job_Complete(job.get(), code, result, serialized.constData(), serialized.size());
    if (result) input_item_Release(result);
}
