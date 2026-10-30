// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include "addon_native_metadata_owner.hpp"
#include "addon_native_provider_artwork_assets.hpp"
#include "addon_metadata_family_plan.hpp"
#include "addon_manager.hpp"
#include <vlc_metadata_owner.h>
#include <QDir>
#include <QUuid>
#include <utility>

namespace {
struct Family { meta_fetcher_provider_t value; const char* label; const char* credit; const char* url; };
Family family(const QString& name)
{
    if (name == QLatin1String("musicbrainz")) return {META_FETCHER_PROVIDER_MUSICBRAINZ, "MusicBrainz", "MusicBrainz, Cover Art Archive, Amazon", "https://musicbrainz.org/"};
    if (name == QLatin1String("google-images")) return {META_FETCHER_PROVIDER_GOOGLE_IMAGES, "Google Images", "Google Images", "https://images.google.com/"};
    if (name == QLatin1String("french-tv")) return {META_FETCHER_PROVIDER_FRENCH_TV, "French TV images", "French TV and Wikimedia Commons", "https://commons.wikimedia.org/"};
    if (name == QLatin1String("lastfm")) return {META_FETCHER_PROVIDER_LASTFM, "Last.fm", "Last.fm", "https://www.last.fm/"};
    return {META_FETCHER_PROVIDER_NONE, "", "", ""};
}
QString reference(const QVariant& descriptor)
{
    const auto map = descriptor.toMap();
    if (map.size() != 1 || !map.contains(QStringLiteral("nativeClaim"))) return {};
    const auto value = map.value(QStringLiteral("nativeClaim")).toString();
    return value.size() <= 128 ? value : QString{};
}
}
AddonNativeMetadataOwner::AddonNativeMetadataOwner(AddonMetadataService* service, vlc::addons::AddonManager* manager)
    : m_service(service), m_manager(manager),
      m_partition(QUuid::createUuid().toString(QUuid::WithoutBraces)) {}
std::unique_ptr<AddonNativeMetadataOwner> AddonNativeMetadataOwner::create(AddonMetadataService* service,
    vlc::addons::AddonManager* manager, vlc_object_t* parent, const Options& options)
{
    if (!service || !parent || !QDir::isAbsolutePath(options.cacheDirectory)) return {};
    auto owner = std::unique_ptr<AddonNativeMetadataOwner>(new AddonNativeMetadataOwner(service, manager));
    if (!service->reserveNativeOwner(owner.get()) || !owner->initialize(parent, options)) return {};
    return owner;
}
AddonNativeMetadataOwner::~AddonNativeMetadataOwner() { close(); }
bool AddonNativeMetadataOwner::initialize(vlc_object_t* parent, const Options& options)
{
    const auto lifetime = m_service->applicationLifetime();
    if (!lifetime.current()) return false;
    m_backend = {m_service->holdCoreBackend(), vlc_metadata_backend_Release};
    if (!m_backend) return false;
    m_context = new AddonNativeProviderContext(lifetime, parent, this);
    if (!m_context->available()) return false;
    m_images = options.imageReader.read
        ? new AddonNativeArtworkAssets(lifetime, options.imageReader, options.cacheDirectory, options.images, this)
        : new AddonNativeArtworkAssets(lifetime, parent, options.cacheDirectory, options.images, this);
    if (!m_images->available()) return false;
    m_materializer = new AddonNativeProviderArtworkAssets(m_images, m_context, this);
    const QPointer<AddonNativeMetadataOwner> owner(this);
    connect(m_service, &AddonMetadataService::retiring, this, &AddonNativeMetadataOwner::close, Qt::DirectConnection);
    connect(m_service, &QObject::destroyed, this, &AddonNativeMetadataOwner::close);
    if (m_manager) {
        connect(m_manager, &vlc::addons::AddonManager::retiring, this, &AddonNativeMetadataOwner::close, Qt::DirectConnection);
        connect(m_manager, &QObject::destroyed, this, &AddonNativeMetadataOwner::close);
    }
    m_service->setNativeArtworkAdapter(m_images);
    if (!m_service || !m_service->nativeOwnerIs(this)) return false;
    for (const auto& binding : AddonMetadataFamilyPlan::bindings()) {
        if (options.nativeFamilies && !options.nativeFamilies->contains(binding.family)) continue;
        const auto native = family(binding.family);
        const QString partition = QStringLiteral("native-family-artwork@1:") + m_partition + ':' + binding.family;
        const QVariantMap descriptor{{QStringLiteral("id"), binding.nativeContribution},
            {QStringLiteral("packageId"), binding.nativeContribution.section('/', 0, 0)},
            {QStringLiteral("name"), QString::fromUtf8(native.label)}, {QStringLiteral("enabled"), true},
            {QStringLiteral("configured"), true}, {QStringLiteral("cachePartition"), partition},
            {QStringLiteral("operations"), QStringList{QStringLiteral("enrich")}},
            {QStringLiteral("requiresNativeAnchor"), true}, {QStringLiteral("source"), QStringLiteral("app")}};
        AddonMetadataService::NativeArtworkHandlers artwork;
        const QPointer<AddonNativeProviderContext> context(m_context);
        artwork.validate = [context, native, name=binding.family](const QVariantMap& data, vlc_metadata_job_t* job)
            -> std::optional<AddonMetadataCoordinator::NativeArtwork> {
            const auto claim = reference(data);
            if (!context || claim.isEmpty() || !context->validates(claim, job, native.value)) return {};
            return AddonMetadataCoordinator::NativeArtwork{QVariantMap{{QStringLiteral("nativeClaim"), claim}}, name,
                {{QStringLiteral("name"), QString::fromUtf8(native.credit)}, {QStringLiteral("url"), QString::fromUtf8(native.url)}}};
        };
        const QPointer<AddonNativeProviderArtworkAssets> materializer(m_materializer);
        artwork.resolve = [materializer, native](const QVariant& descriptor, vlc_metadata_job_t* job,
            AddonMetadataService::ArtworkCallback complete) {
            AddonMetadataService::NativeArtworkRequest result;
            if (!materializer) return result;
            result.cancel = materializer->resolve(reference(descriptor), job, native.value,
                [complete=std::move(complete)](const auto& value) {
                    if (!value.permit.current()) complete({}, QStringLiteral("The native artwork request is no longer current."));
                    else complete(value.file, value.error);
                });
            if (result.cancel) result.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            return result;
        };
        const auto registered = m_service->registerBuiltinProvider(QStringLiteral("artwork"), descriptor, this,
            [owner, id=binding.nativeContribution, value=native.value](const QString& invocation, const QString& operation, const QVariantMap&) {
                if (owner) owner->invoke(id, value, invocation, operation);
            }, [owner](const QString& invocation) { if (owner) owner->cancel(invocation); }, std::move(artwork));
        if (!m_service || !registered || !m_service->nativeOwnerIs(this)) return false;
        m_providers.append(binding.nativeContribution);
    }
    const QPointer<vlc::addons::AddonManager> manager(m_manager);
    m_service->setProviderPlan(this, [manager](const QString& feature, const QString& operation,
        const QVariantList& rows, bool nativeAllowed) {
        return AddonMetadataFamilyPlan::select(feature, operation, rows,
            manager ? manager->providerOrder(feature) : QStringList{}, nativeAllowed);
    });
    if (!m_service || !m_service->nativeOwnerIs(this) || !m_context->available() || !m_images->available()) return false;
    if (!m_service->setTypedArtworkSources(this, m_context, m_materializer)) return false;
    m_ready = true;
    if (options.activateCoreOwner) {
        const auto status = vlc_metadata_owner_Set(parent, m_backend.get());
        if (status != VLC_SUCCESS) return false;
        m_active = true;
    }
    return true;
}
bool AddonNativeMetadataOwner::ready() const
{
    return m_ready && !m_closed && m_service && m_service->nativeOwnerIs(const_cast<AddonNativeMetadataOwner*>(this))
        && m_service->applicationLifetime().current() && m_context && m_context->available()
        && m_images && m_images->available() && m_backend && !vlc_metadata_backend_IsClosed(m_backend.get());
}
void AddonNativeMetadataOwner::invoke(const QString&, meta_fetcher_provider_t value,
    const QString& invocation, const QString& operation)
{
    const QPointer<AddonMetadataService> service(m_service);
    if (!ready() || operation != QLatin1String("enrich")) {
        if (service) service->completeBuiltin(invocation, QStringLiteral("unsupported"));
        return;
    }
    const auto job = std::shared_ptr<vlc_metadata_job_t>(service->holdNativeJobForInvocation(invocation), vlc_metadata_job_Release);
    if (!job) { service->completeBuiltin(invocation, QStringLiteral("unsupported")); return; }
    const QPointer<AddonNativeMetadataOwner> owner(this);
    auto cancel = m_context->probe(job.get(), value, [owner, service, invocation](const auto& result) {
        if (!owner || owner->m_closed || !owner->m_calls.contains(invocation)) return;
        owner->m_calls.remove(invocation);
        if (!service) return;
        if (result.status == VLC_SUCCESS && !result.claim.isEmpty())
            service->completeBuiltin(invocation, QStringLiteral("match"), {{QStringLiteral("nativeClaim"), result.claim}});
        else service->completeBuiltin(invocation, result.status == VLC_ENOENT ? QStringLiteral("no_match")
            : result.status == -EACCES || result.status == VLC_EACCES ? QStringLiteral("unsupported") : QStringLiteral("error"));
    });
    if (cancel) {
        m_calls.insert(invocation, cancel);
        service->setNativeCancellationForInvocation(invocation, std::move(cancel));
    }
    else if (service) service->completeBuiltin(invocation, QStringLiteral("unsupported"));
}
void AddonNativeMetadataOwner::cancel(const QString& invocation)
{
    const auto cancel = m_calls.take(invocation);
    if (cancel) cancel();
}
void AddonNativeMetadataOwner::close()
{
    if (m_closed) return;
    // Revoke final publication before the native context or registration
    // lifetime changes. This scope uses no Qt callback during the barrier.
    const auto registration = m_service ? m_service->beginNativeOwnerRetirement(this)
        : vlc::addons::MetadataRegistryMutation{};
    m_closed = true; m_ready = false;
    const auto service = m_service;
    const auto context = m_context;
    const auto images = m_images;
    const auto providers = m_providers;
    const auto backend = m_backend;
    const auto active = m_active;
    auto calls = std::move(m_calls); m_calls.clear(); m_providers.clear(); m_active = false;
    const QPointer<AddonNativeMetadataOwner> identity(this);
    if (active && (!service || service->nativeOwnerIs(identity) || !service->applicationLifetime().current())) {
        if (backend) vlc_metadata_backend_Close(backend.get());
        if (identity && service && service->nativeOwnerIs(identity)) service->shutdown();
    }
    if (context) context->close();
    if (images) images->close();
    for (const auto& cancel : calls) if (cancel) cancel();
    if (!identity) return;
    if (service) service->clearProviderPlan(identity);
    if (!identity) return;
    for (const auto& id : providers) {
        if (service) service->unregisterBuiltinProviderForOwner(id, identity);
        if (!identity) return;
    }
    if (service) service->clearNativeArtworkAdapterIf(images);
    if (!identity) return;
    if (service) service->releaseNativeOwnerFor(identity);
}
