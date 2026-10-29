// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_prepared_metadata_invocation.hpp"
#include "addon_manager.hpp"
#include "addon_artwork_attempt_plan.hpp"
#include "addon_metadata_completion_policy.hpp"
#include "addon_package_configuration.hpp"
#include "addon_storage.hpp"
#include "addon_repository_jobs.hpp"
#include "addon_repository_cancellation.hpp"
#include "addon_native_wake.hpp"
#include <QUuid>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QScopeGuard>
#include <QSslSocket>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>
#include <algorithm>

namespace vlc::addons {
struct PreparedMetadataOwner { std::atomic_bool open{true}; };
namespace {
std::atomic_uint processes{0};
struct ProcessPermit {
    ~ProcessPermit() { processes.fetch_sub(1, std::memory_order_acq_rel); }
    static std::shared_ptr<ProcessPermit> acquire() {
        auto count = processes.load(std::memory_order_acquire);
        do { if (count >= PreparedMetadataInvocation::ProcessLimit) return {}; }
        while (!processes.compare_exchange_weak(count, count + 1, std::memory_order_acq_rel));
        try { return std::make_shared<ProcessPermit>(); }
        catch (...) { --processes; return {}; }
    }
};
struct RecipePermit {
    struct Global { std::mutex mutex; unsigned count = 0; qsizetype bytes = 0; };
    std::shared_ptr<Global> global;
    qsizetype bytes = 0;
    ~RecipePermit() {
        std::lock_guard<std::mutex> lock(global->mutex); --global->count; global->bytes -= bytes;
    }
    static std::shared_ptr<RecipePermit> acquire() {
        static const auto all = std::make_shared<Global>();
        std::lock_guard<std::mutex> lock(all->mutex);
        if (all->count >= PreparedMetadataInvocation::RecipeLimit) return {};
        auto value = std::make_shared<RecipePermit>(); value->global = all; ++all->count; return value;
    }
    bool charge(qsizetype amount) {
        std::lock_guard<std::mutex> lock(global->mutex);
        if (amount < 0 || amount > PreparedMetadataInvocation::ProcessSecretBytes - global->bytes) return false;
        bytes += amount; global->bytes += amount; return true;
    }
};
struct Secrets {
    QHash<QString, QByteArray> fields;
    ~Secrets() { wipe(); }
    void wipe() { for (auto& value : fields) value.fill('\0'); fields.clear(); }
};
// This state and its cancellation path contain native/std values only.
struct ProcessCompletion {
    const std::shared_ptr<ProcessPermit> permit;
    std::weak_ptr<AddonMetadataNativeAdmission> gate;
    std::atomic_bool canceled{false};
    std::mutex mutex;
    std::condition_variable changed;
    bool done = false;
    explicit ProcessCompletion(std::shared_ptr<ProcessPermit> value) : permit(std::move(value)) {}
    void cancel() noexcept {
        canceled.store(true, std::memory_order_release);
        if (const auto value = gate.lock()) value->revoke();
    }
    void finish() noexcept {
        { std::lock_guard<std::mutex> lock(mutex); done = true; }
        changed.notify_all();
    }
    void wait() { std::unique_lock<std::mutex> lock(mutex); changed.wait(lock, [this] { return done; }); }
};
bool sameIdentity(const ReleaseIdentity& a, const ReleaseIdentity& b)
{
    return a.source == b.source && a.tag == b.tag && a.tagObjectId == b.tagObjectId
        && a.commitId == b.commitId && a.objectFormat == b.objectFormat
        && a.developerSource == b.developerSource && a.distributionSource == b.distributionSource;
}
bool samePackage(const InstalledPackage& a, const InstalledPackage& b)
{
    return a.manifest.document == b.manifest.document && a.directory == b.directory
        && a.digest == b.digest && a.generation == b.generation && a.grants == b.grants
        && a.enabled == b.enabled && a.recoveryRequired == b.recoveryRequired && sameIdentity(a.identity, b.identity);
}
QByteArray digest(const QJsonObject& value)
{ return QCryptographicHash::hash(QJsonDocument(value).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256); }
}
struct PreparedMetadataInvocation::Data {
    // Release this charge after every secret and Qt value has been destroyed.
    const std::shared_ptr<RecipePermit> permit;
    std::shared_ptr<const PreparedMetadataOwner> owner;
    StoreAuthorityToken sourceToken, catalogueToken, preparationToken;
    AddonStreamLifetime::Handle lifetime;
    ArtworkAttemptPlan::Slot slot;
    QByteArray subject;
    QString capturedExecutable, executable;
    QJsonObject settings, credentialStatus;
    QSet<int> absentHttpStatuses;
    bool tlsAvailable = false, requiresAttempt = true;
    const std::shared_ptr<Secrets> secrets = std::make_shared<Secrets>();
    explicit Data(std::shared_ptr<RecipePermit> value) : permit(std::move(value)) {}
    ~Data() { secrets->wipe(); }
    bool stage(AddonNativeAuthority&, const std::function<bool()>&, QString*);
    bool current() const noexcept {
        return owner && owner->open.load(std::memory_order_acquire) && lifetime.current()
            && sourceToken.current() && catalogueToken.current() && preparationToken.current();
    }
};
bool PreparedMetadataInvocation::Data::stage(AddonNativeAuthority& authority,
    const std::function<bool()>& current, QString* error)
{
    const auto refuse = [error](const QString& text) { if (error) *error = text; return false; };
    const QFileInfo worker(capturedExecutable);
    if (capturedExecutable.isEmpty() || !worker.isFile() || !worker.isExecutable())
        return refuse(QStringLiteral("The add-on worker is not available."));
    executable = worker.absoluteFilePath();
    tlsAvailable = QSslSocket::supportsSsl();
    const auto properties = slot.schema.value(QStringLiteral("properties")).toObject();
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        const auto field = it.value().toObject();
        if (it.key().startsWith(QLatin1Char('_')) || packageConfiguration::secretField(field)) continue;
        if (slot.settings.contains(it.key())) settings.insert(it.key(), slot.settings.value(it.key()));
        else if (field.contains(QStringLiteral("default"))) settings.insert(it.key(), field.value(QStringLiteral("default")));
    }
    const auto declared = authority.package.manifest.permissionScope(QStringLiteral("credentials.use")).value(QStringLiteral("bindings")).toArray();
    const auto approved = slot.approvedGrants.value(QStringLiteral("credentials.use")).toObject().value(QStringLiteral("bindings")).toArray();
    QSet<QString> fields;
    for (const auto& binding : declared) {
        if (!approved.contains(binding)) continue;
        const auto value = binding.toObject(); const auto origin = value.value(QStringLiteral("origin")).toString();
        if (exactHttpsOrigin(origin).isEmpty())
            return refuse(QStringLiteral("The credential origin is not correct."));
        fields.insert(value.value(QStringLiteral("field")).toString());
    }
    qsizetype bytes = 0;
    for (const auto& field : fields) {
        if (!current() || !authority.current()) return refuse(QStringLiteral("The credential configuration changed."));
        auto secret = authority.credentials(field);
        const auto wipe = qScopeGuard([&] { secret.fill('\0'); });
        if (!current() || !authority.current() || secret.size() > PreparedMetadataInvocation::MaximumFieldBytes
            || secret.size() > PreparedMetadataInvocation::MaximumSecretBytes - bytes || !permit->charge(secret.size()))
            return refuse(QStringLiteral("The prepared credential is not available or exceeds the limit."));
        bytes += secret.size();
        credentialStatus.insert(field, QJsonObject{{QStringLiteral("present"), !secret.isEmpty()}});
        secrets->fields.insert(field, std::move(secret));
    }
    // The full saved-state reader and resolver end here on joined work.
    if (!current() || !authority.current() || !this->current())
        return refuse(QStringLiteral("The metadata preparation changed before completion."));
    return true;
}
struct PreparedArtworkAsset::Data {
    const std::shared_ptr<RecipePermit> permit;
    std::shared_ptr<const PreparedMetadataOwner> owner;
    StoreAuthorityToken token;
    AddonStreamLifetime::Handle lifetime;
    Manifest manifest;
    QJsonObject grants;
    QVariantMap registration;
    std::shared_ptr<Secrets> secrets = std::make_shared<Secrets>();
    bool tlsAvailable = false;
    explicit Data(std::shared_ptr<RecipePermit> value) : permit(std::move(value)) {}
    ~Data() { secrets->wipe(); }
    bool current() const noexcept {
        return owner && owner->open.load(std::memory_order_acquire) && token.current() && lifetime.current();
    }
};
PreparedArtworkAsset::PreparedArtworkAsset(std::shared_ptr<Data> data) : m_data(std::move(data)) {}
PreparedArtworkAsset::~PreparedArtworkAsset() = default;
bool PreparedArtworkAsset::current() const noexcept { return m_data && m_data->current(); }
QVariantMap PreparedArtworkAsset::registration() const { return current() ? m_data->registration : QVariantMap{}; }
struct PreparedMetadataRuntime {
    struct Entry {
        QPointer<AddonWorker> worker;
        std::shared_ptr<PreparedMetadataInvocation::Data> data;
        std::shared_ptr<ProcessCompletion> completion;
        QString request, publicRequest, package, contribution;
        QString status, error;
        QJsonObject result;
        bool terminal = false, canceled = false;
    };
    struct Gate { QString package; std::weak_ptr<AddonMetadataNativeAdmission> admission; };
    struct Preparation {
        QString package;
        std::shared_ptr<std::atomic_bool> canceled = std::make_shared<std::atomic_bool>(false);
        std::function<void()> work;
        std::function<void(bool)> complete;
    };
    std::unique_ptr<AddonRepositoryJobs> jobs;
    std::unique_ptr<AddonNativeWake> wake;
    NativeNotification available;
    QStringList pending;
    QHash<QString, Preparation> preparations;
    QHash<QString, QString> aliases;
    QString running;
    // Fail closed until joined work has read recovery/safe-mode state. The
    // snapshot must retain its Store token and exact owner revision.
    bool safeMode = true;
    int safeModeRevision = -1;
    std::function<bool()> safeModeCurrent;
    const std::shared_ptr<PreparedMetadataOwner> owner = std::make_shared<PreparedMetadataOwner>();
    QHash<AddonWorker*, std::shared_ptr<Entry>> workers;
    QHash<QString, std::shared_ptr<Entry>> requests;
    QHash<QString, Gate> gates;
};
struct PreparedDeliveryTail {
    const std::shared_ptr<ProcessCompletion> completion;
    explicit PreparedDeliveryTail(std::shared_ptr<ProcessCompletion> value) : completion(std::move(value)) {}
    ~PreparedDeliveryTail() { completion->finish(); }
};
struct PreparedDelivery {
    // FIRST member: its native tail is destroyed after every Qt-rich member.
    std::shared_ptr<PreparedDeliveryTail> tail;
    std::shared_ptr<PreparedMetadataRuntime> runtime;
    std::shared_ptr<PreparedMetadataRuntime::Entry> entry;
    QPointer<AddonManager> owner;
    NetworkBroker::RequestGuard current;
};
PreparedMetadataInvocation::PreparedMetadataInvocation(std::shared_ptr<Data> data) : m_data(std::move(data)) {}
PreparedMetadataInvocation::~PreparedMetadataInvocation() = default;
bool PreparedMetadataInvocation::current() const noexcept { return m_data && m_data->current(); }
unsigned PreparedMetadataInvocation::processCount() noexcept { return processes.load(std::memory_order_acquire); }
void AddonManager::initializePreparedMetadata() { m_preparedMetadata = std::make_shared<PreparedMetadataRuntime>(); }
QString AddonManager::queueMetadataPreparation(const QString& package, std::function<void()> work,
    std::function<void(bool)> complete)
{
    const auto runtime = m_preparedMetadata;
    const auto life = AddonStreamLifetime::applicationHandle();
    if (QThread::currentThread() != thread() || m_retiring || !runtime || !work || !complete
        || !runtime->owner->open.load(std::memory_order_acquire) || !life.current()
        || runtime->preparations.size() >= 64) return {};
    if (!runtime->jobs) runtime->jobs = std::make_unique<AddonRepositoryJobs>(life);
    const QString id = QStringLiteral("metadata-prepare:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    PreparedMetadataRuntime::Preparation task;
    task.package = package; task.work = std::move(work); task.complete = std::move(complete);
    runtime->preparations.insert(id, std::move(task)); runtime->pending.append(id);
    QTimer::singleShot(0, this, &AddonManager::pumpMetadataPreparation);
    return id;
}
void AddonManager::pumpMetadataPreparation()
{
    const auto runtime = m_preparedMetadata;
    if (m_retiring || !runtime || !runtime->owner->open.load(std::memory_order_acquire)
        || !runtime->jobs || runtime->jobs->busy()) return;
    while (!runtime->pending.isEmpty() && !runtime->preparations.contains(runtime->pending.first()))
        runtime->pending.removeFirst();
    if (runtime->pending.isEmpty()) { runtime->available = {}; return; }
    const auto id = runtime->pending.first();
    const auto task = runtime->preparations.value(id);
    const auto canceled = task.canceled;
    const QPointer<AddonManager> owner(this);
    AddonRepositoryJobs::Refusal refusal;
    const bool started = runtime->jobs->submit([work = task.work, canceled] {
        if (!canceled->load(std::memory_order_acquire)) work();
    }, [owner, runtime, id](AddonRepositoryJobs::Result result) {
        runtime->running.clear();
        auto found = runtime->preparations.find(id);
        std::function<void(bool)> complete;
        bool current = false;
        if (found != runtime->preparations.end()) {
            current = !found->canceled->load(std::memory_order_acquire);
            complete = std::move(found->complete); runtime->preparations.erase(found);
        }
        if (runtime->owner->open.load(std::memory_order_acquire) && owner) {
            QTimer::singleShot(0, owner, &AddonManager::pumpMetadataPreparation);
            if (current && complete) complete(result == AddonRepositoryJobs::Result::Finished);
        }
    }, &refusal);
    if (started) {
        runtime->pending.removeFirst(); runtime->running = id;
        // The background job owns the reader. Keep only cancellation and the
        // completion on the owner; do not retain another credential resolver.
        runtime->preparations[id].work = {};
        runtime->available = {}; return;
    }
    if (refusal == AddonRepositoryJobs::Refusal::ProcessQuota) {
        if (!runtime->wake) runtime->wake = std::make_unique<AddonNativeWake>([owner] {
            if (owner) owner->pumpMetadataPreparation();
        });
        if (runtime->wake->valid()) {
            if (!runtime->available)
                runtime->available = AddonRepositoryJobs::onProcessAvailable(runtime->wake->nativeCallback());
            if (runtime->available) {
                // Subscribe before rechecking: a concurrent release cannot be lost.
                if (AddonRepositoryJobs::processCount() < AddonRepositoryJobs::ProcessLimit)
                    QTimer::singleShot(0, this, &AddonManager::pumpMetadataPreparation);
                return;
            }
        }
    }
    runtime->pending.removeFirst(); runtime->preparations.remove(id);
    QTimer::singleShot(0, this, &AddonManager::pumpMetadataPreparation);
    if (task.complete) task.complete(false);
}
bool AddonManager::cancelMetadataPreparation(const QString& id)
{
    const auto runtime = m_preparedMetadata;
    if (!runtime) return false;
    const auto found = runtime->preparations.find(id);
    if (found == runtime->preparations.end()) return false;
    found->canceled->store(true, std::memory_order_release);
    runtime->preparations.erase(found); runtime->pending.removeAll(id);
    if (runtime->running == id && runtime->jobs) runtime->jobs->cancel();
    if (runtime->pending.isEmpty()) runtime->available = {};
    return true;
}
QVariantMap AddonManager::builtinMetadataProvider(const QString& id) const
{
    const auto runtime = m_preparedMetadata;
    if (m_retiring || m_distributionBootstrapInProgress || !runtime || runtime->safeMode
        || runtime->safeModeRevision != m_revision || !runtime->safeModeCurrent || !runtime->safeModeCurrent()) return {};
    return m_builtinProviders.value(id);
}
AddonMetadataCoordinator::SelectionCancel AddonManager::selectMetadataProviders(const QString& feature,
    AddonMetadataCoordinator::SelectionComplete complete)
{
    const auto source = metadataCatalogueSource();
    const auto runtime = m_preparedMetadata;
    const QPointer<AddonManager> owner(this);
    const auto revision = m_revision;
    const auto result = std::make_shared<AddonMetadataCoordinator::ProviderSelection>();
    const auto safeMode = std::make_shared<bool>(true);
    QVariantList builtins;
    for (const auto& row : m_builtinProviders)
        if (row.value(QStringLiteral("features")).toStringList().contains(feature)) builtins.append(row);
    QString id;
    if (source && runtime) {
        const auto alive = runtime->owner;
        id = queueMetadataPreparation({}, [source = *source, alive, builtins, feature, result, safeMode] {
            MetadataCatalogueSnapshot catalogue;
            if (!source.current() || !alive->open.load(std::memory_order_acquire)
                || !source.catalogueReader || !source.catalogueReader(&catalogue, &result->error) || !catalogue.current()) {
                if (result->error.isEmpty()) result->error = QStringLiteral("The metadata provider list is not available.");
                return;
            }
            *safeMode = catalogue.safeMode;
            for (const auto& row : std::as_const(catalogue.packages)) {
                if (RepositoryCancellation::requested()) return;
                QList<Contribution> declarations;
                for (const auto& declaration : row.contributions)
                    if (declaration.type == QLatin1String("metadata.provider") && declaration.features.contains(feature))
                        declarations.append(declaration);
                if (declarations.isEmpty()) continue;
                const auto present = [&source, &row](const QString& reference) {
                    auto value = source.credentialReader ? source.credentialReader(row.package.manifest.id, reference) : QByteArray{};
                    const bool found = !value.isEmpty(); value.fill('\0'); return found;
                };
                const bool enabled = !catalogue.safeMode && row.savedEnabled && row.package.enabled;
                const bool configured = enabled && packageConfiguration::configured(row.settings, row.schema, present);
                const auto account = enabled ? packageConfiguration::packageAccountReference(row.package, row.settings, row.schema, present) : QString{};
                const QString partition = QString::fromLatin1(row.package.digest) + QLatin1Char(':')
                    + QString::number(row.package.generation) + QLatin1Char(':') + QString::number(row.settingsRevision);
                for (const auto& declaration : std::as_const(declarations))
                    result->providers.append(QVariantMap{{"id", QString(row.package.manifest.id + '/' + declaration.id)},
                        {"packageId", row.package.manifest.id}, {"name", row.package.manifest.name},
                        {"enabled", enabled}, {"configured", configured}, {"accountReference", account},
                        {"cachePartition", partition}, {"features", declaration.features},
                        {"operations", declaration.operations}, {"mediaKinds", declaration.mediaKinds}});
            }
            for (auto row : builtins) {
                if (catalogue.safeMode) { auto map = row.toMap(); map.insert(QStringLiteral("enabled"), false); row = map; }
                result->providers.append(row);
            }
            const auto order = catalogue.providerPreferences.value(feature).toArray();
            const auto rank = [&order](const QVariant& row) {
                const auto id = row.toMap().value(QStringLiteral("id")).toString();
                for (qsizetype i = 0; i < order.size(); ++i) if (order.at(i).toString() == id) return i;
                return order.size();
            };
            std::stable_sort(result->providers.begin(), result->providers.end(), [&](const auto& a, const auto& b) { return rank(a) < rank(b); });
            const auto queued = source.queuedAuthority, authority = catalogue.authority;
            result->current = [alive, queued, authority] {
                return alive->open.load(std::memory_order_acquire) && queued.current() && authority.current();
            };
        }, [owner, revision, result, safeMode, complete](bool success) {
            if (!owner || !complete) return;
            if (!success || owner->revision() != revision) {
                result->providers.clear(); result->error = QStringLiteral("The metadata provider configuration changed.");
            }
            const auto runtime = owner->m_preparedMetadata;
            runtime->safeMode = *safeMode; runtime->safeModeRevision = revision;
            runtime->safeModeCurrent = success && result->error.isEmpty() ? result->current : std::function<bool()>{};
            complete(std::move(*result));
        });
    }
    if (id.isEmpty()) {
        QTimer::singleShot(0, this, [complete = std::move(complete)] {
            if (complete) complete({{}, {}, QStringLiteral("The metadata service is not available. Try again.")});
        });
    }
    return [owner, id] { if (owner && !id.isEmpty()) owner->cancelMetadataPreparation(id); };
}
QString AddonManager::prepareArtworkAsset(const QString& contribution, ArtworkPreparationComplete complete)
{
    if (!complete || QThread::currentThread() != thread() || m_retiring || !m_preparedMetadata) return {};
    const auto reader = nativeAuthorityReader(contribution);
    const auto token = storeAuthorityToken();
    const auto owner = m_preparedMetadata->owner;
    const auto lifetime = AddonStreamLifetime::applicationHandle();
    struct Result { PreparedArtwork value; QString error; };
    const auto result = std::make_shared<Result>();
    return queueMetadataPreparation(contribution.section(QLatin1Char('/'),0,0),
        [reader,token,owner,lifetime,result,contribution] {
            result->error = QStringLiteral("The artwork provider is not available.");
            const auto live = [&] {
                return owner->open.load(std::memory_order_acquire) && token.current()
                    && lifetime.current() && !RepositoryCancellation::requested();
            };
            if (!reader || !live()) return;
            auto authority = reader();
            if (!authority || !live() || !authority->current() || !authority->live()
                || authority->globalId != contribution || authority->contribution.type != QLatin1String("metadata.provider")
                || !authority->contribution.features.contains(QStringLiteral("artwork"))) return;
            const auto permit = RecipePermit::acquire();
            if (!permit) { result->error = QStringLiteral("Artwork preparation is busy. Try again."); return; }
            auto data = std::make_shared<PreparedArtworkAsset::Data>(permit);
            data->owner = owner; data->token = token; data->lifetime = lifetime;
            data->manifest = authority->package.manifest; data->grants = authority->package.grants;
            data->tlsAvailable = QSslSocket::supportsSsl();
            const auto declared = data->manifest.permissionScope(QStringLiteral("credentials.use")).value(QStringLiteral("bindings")).toArray();
            const auto approved = data->grants.value(QStringLiteral("credentials.use")).toObject().value(QStringLiteral("bindings")).toArray();
            QSet<QString> fields;
            for (const auto& binding : declared) if (approved.contains(binding)) {
                const auto value = binding.toObject();
                if (exactHttpsOrigin(value.value(QStringLiteral("origin")).toString()).isEmpty()) return;
                fields.insert(value.value(QStringLiteral("field")).toString());
            }
            qsizetype bytes = 0;
            for (const auto& field : fields) {
                if (!live() || !authority->current()) return;
                auto secret = authority->credentials(field);
                const auto wipe = qScopeGuard([&] { secret.fill('\0'); });
                if (!live() || !authority->current() || secret.size() > PreparedMetadataInvocation::MaximumFieldBytes
                    || secret.size() > PreparedMetadataInvocation::MaximumSecretBytes - bytes || !permit->charge(secret.size())) return;
                bytes += secret.size(); data->secrets->fields.insert(field,std::move(secret));
            }
            const QString partition = QString::fromLatin1(authority->package.digest) + QLatin1Char(':')
                + QString::number(authority->package.generation) + QLatin1Char(':') + QString::number(authority->settingsRevision);
            data->registration = {{QStringLiteral("id"),contribution},{QStringLiteral("enabled"),true},
                {QStringLiteral("configured"),true},{QStringLiteral("cachePartition"),partition},
                {QStringLiteral("accountReference"),authority->accountReference}};
            if (!live() || !authority->current() || !data->current()) return;
            result->value = PreparedArtwork(new PreparedArtworkAsset(std::move(data))); result->error.clear();
        },[result,complete=std::move(complete)](bool ok) {
            if (!ok || !result->value || !result->value->current()) {
                complete({},result->error.isEmpty() ? QStringLiteral("The artwork provider configuration changed.") : result->error);
                return;
            }
            complete(std::move(result->value),{});
        });
}
quint64 AddonManager::fetchPreparedArtwork(const PreparedArtwork& prepared, const QJsonObject& descriptor,
    NetworkBroker::Callback complete)
{
    if (QThread::currentThread() != thread() || m_retiring || !m_preparedMetadata || !prepared || !prepared->current()
        || !prepared->m_data->tlsAvailable || prepared->m_data->owner != m_preparedMetadata->owner) return 0;
    const auto data = prepared->m_data;
    auto request = descriptor; request.insert(QStringLiteral("method"),QStringLiteral("GET")); request.remove(QStringLiteral("body"));
    const auto current = [data] { return data->current(); };
    const auto credentials = [data](const QString& field) {
        return data->current() ? data->secrets->fields.value(field) : QByteArray{};
    };
    return m_network.fetch(data->manifest,data->grants,request,credentials,std::move(complete),
        1024*1024,NetworkBroker::RequestKind::ImageAsset,current);
}

QString AddonManager::invokeMetadata(const QString& contribution, const QString& operation, const QVariantMap& context)
{
    const auto prepare = metadataInvocationPreparer(contribution, operation, QJsonObject::fromVariantMap(context));
    struct Result { std::unique_ptr<PreparedMetadataInvocation> prepared; QString error; };
    const auto result = std::make_shared<Result>();
    const QPointer<AddonManager> owner(this);
    const auto publicId = std::make_shared<QString>();
    const auto id = queueMetadataPreparation(contribution.section(QLatin1Char('/'), 0, 0), [prepare, result] {
        if (prepare) result->prepared = prepare(&result->error);
    }, [owner, publicId, result, contribution](bool success) {
        if (!owner) return;
        QString request;
        if (success && result->prepared) request = owner->invokePreparedMetadata(std::move(result->prepared), {}, {});
        if (!owner) return;
        if (request.isEmpty()) {
            emit owner->operationFinished(*publicId, contribution, QStringLiteral("error"), {},
                result->error.isEmpty() ? QStringLiteral("The metadata provider could not start.") : result->error);
            return;
        }
        const auto runtime = owner->m_preparedMetadata;
        const auto entry = runtime->requests.value(request);
        if (!entry) { owner->cancel(request); return; }
        entry->publicRequest = *publicId; runtime->aliases.insert(*publicId, request);
    });
    *publicId = id; return id;
}

MetadataInvocationPreparer AddonManager::metadataInvocationPreparer(std::shared_ptr<const ArtworkAttemptPlan> plan, unsigned ordinal) const
{
    if (QThread::currentThread() != thread() || !QCoreApplication::instance()
        || thread() != QCoreApplication::instance()->thread() || m_retiring || !m_preparedMetadata
        || !m_preparedMetadata->owner->open.load(std::memory_order_acquire)
        || !m_withdrawingPackages.isEmpty() || !m_removingPackages.isEmpty()
        || !plan || ordinal >= unsigned(plan->attempts.size()) || !plan->current()) return {};
    const auto& slot = plan->attempts.at(ordinal);
    if (slot.ordinal != ordinal || slot.implementation != ArtworkAttemptPlan::Implementation::Portable || slot.blocked) return {};
    const auto owner = m_preparedMetadata->owner;
    const auto queued = m_store.authorityToken();
    const auto authorityReader = nativeAuthorityReader(slot.contribution);
    const auto executable = m_workerExecutable;
    const auto lifetime = AddonStreamLifetime::applicationHandle();
    if (!queued.current() || !lifetime.current()) return {};
    const auto ownerThread = std::this_thread::get_id();
    return [plan = std::move(plan), ordinal, owner, queued, authorityReader, executable, lifetime, ownerThread]
        (QString* error) -> std::unique_ptr<PreparedMetadataInvocation> {
        const auto refuse = [error](const QString& text) -> std::unique_ptr<PreparedMetadataInvocation> {
            if (error) *error = text;
            return {};
        };
        const auto current = [&] {
            return std::this_thread::get_id() != ownerThread && owner->open.load(std::memory_order_acquire)
                && queued.current() && lifetime.current() && plan->current();
        };
        if (!current()) return refuse(QStringLiteral("The metadata preparation is no longer current."));
        const auto& slot = plan->attempts.at(ordinal);
        auto authority = authorityReader();
        if (!current() || !authority || !authority->current() || !authority->live()
            || !samePackage(authority->package, slot.package) || authority->globalId != slot.contribution
            || authority->settings != slot.settings || authority->settingsRevision != slot.settingsRevision
            || authority->accountReference != slot.accountReference || authority->contribution.document != slot.declaration.document)
            return refuse(QStringLiteral("The metadata provider configuration changed."));
        const auto projected = packageConfiguration::projectMetadataContext(authority->package.manifest, slot.approvedGrants, slot.projectedContext);
        const auto policy = metadataCompletionAbsentStatuses(authority->contribution.document, slot.feature, slot.operation);
        if (slot.approvedGrants != authority->package.grants || projected != slot.projectedContext
            || digest(projected) != slot.projectedContextDigest || !policy
            || authority->contribution.type != QLatin1String("metadata.provider")
            || authority->contribution.contract != QLatin1String("metadata@1")
            || !authority->contribution.features.contains(slot.feature) || !authority->contribution.operations.contains(slot.operation))
            return refuse(QStringLiteral("The metadata provider scope is not correct."));
        const auto permit = RecipePermit::acquire();
        if (!permit) return refuse(QStringLiteral("Metadata preparation is busy. Try again."));
        auto data = std::make_shared<PreparedMetadataInvocation::Data>(permit);
        data->owner = owner; data->sourceToken = plan->queuedAuthority; data->catalogueToken = plan->authority;
        data->preparationToken = queued; data->lifetime = lifetime; data->slot = slot; data->subject = plan->subject;
        data->capturedExecutable = executable; data->absentHttpStatuses = *policy;
        if (!data->stage(*authority, current, error)) return {};
        return std::unique_ptr<PreparedMetadataInvocation>(new PreparedMetadataInvocation(std::move(data)));
    };
}
MetadataInvocationPreparer AddonManager::metadataInvocationPreparer(const QString& contribution,
    const QString& operation, const QJsonObject& context) const
{
    if (QThread::currentThread() != thread() || m_retiring || !m_preparedMetadata
        || !m_preparedMetadata->owner->open.load(std::memory_order_acquire)) return {};
    const auto owner = m_preparedMetadata->owner;
    const auto queued = m_store.authorityToken();
    const auto authorityReader = nativeAuthorityReader(contribution);
    const auto executable = m_workerExecutable;
    const auto lifetime = AddonStreamLifetime::applicationHandle();
    if (!queued.current() || !lifetime.current() || !authorityReader) return {};
    const auto ownerThread = std::this_thread::get_id();
    return [owner, queued, authorityReader, executable, lifetime, ownerThread, contribution, operation, context]
        (QString* error) -> std::unique_ptr<PreparedMetadataInvocation> {
        const auto refuse = [error](const QString& text) -> std::unique_ptr<PreparedMetadataInvocation> {
            if (error) *error = text;
            return {};
        };
        const auto current = [&] {
            return std::this_thread::get_id() != ownerThread && owner->open.load(std::memory_order_acquire)
                && queued.current() && lifetime.current() && !RepositoryCancellation::requested();
        };
        if (!current()) return refuse(QStringLiteral("The metadata request is no longer current."));
        auto authority = authorityReader();
        if (!current() || !authority || !authority->live() || !authority->current())
            return refuse(QStringLiteral("Enable this add-on. Then enter its settings."));
        ArtworkAttemptPlan::Slot slot;
        slot.contribution = contribution; slot.contract = authority->contribution.contract;
        slot.operation = operation; slot.feature = context.value(QStringLiteral("feature")).toString();
        slot.package = authority->package; slot.declaration = authority->contribution;
        slot.settings = authority->settings; slot.schema = authority->schema;
        slot.settingsRevision = authority->settingsRevision; slot.accountReference = authority->accountReference;
        slot.approvedGrants = authority->package.grants;
        slot.projectedContext = packageConfiguration::projectMetadataContext(authority->package.manifest, slot.approvedGrants, context);
        slot.projectedContextDigest = digest(slot.projectedContext);
        slot.releaseDigest = slot.package.digest; slot.releaseGeneration = slot.package.generation;
        slot.partition = QString::fromLatin1(slot.releaseDigest) + QLatin1Char(':')
            + QString::number(slot.releaseGeneration) + QLatin1Char(':') + QString::number(slot.settingsRevision);
        slot.requestedFields.clear();
        const auto policy = metadataCompletionAbsentStatuses(slot.declaration.document, slot.feature, operation);
        if (slot.declaration.type != QLatin1String("metadata.provider") || slot.contract != QLatin1String("metadata@1")
            || !slot.declaration.features.contains(slot.feature) || !slot.declaration.operations.contains(operation) || !policy)
            return refuse(QStringLiteral("The metadata provider scope is not correct."));
        const auto permit = RecipePermit::acquire();
        if (!permit) return refuse(QStringLiteral("Metadata preparation is busy. Try again."));
        auto data = std::make_shared<PreparedMetadataInvocation::Data>(permit);
        data->owner = owner; data->sourceToken = queued; data->catalogueToken = queued;
        data->preparationToken = queued; data->lifetime = lifetime; data->slot = slot; data->requiresAttempt = false;
        data->capturedExecutable = executable; data->absentHttpStatuses = *policy;
        if (!data->stage(*authority, current, error)) return {};
        return std::unique_ptr<PreparedMetadataInvocation>(new PreparedMetadataInvocation(std::move(data)));
    };
}
QString AddonManager::invokePreparedMetadata(std::unique_ptr<PreparedMetadataInvocation> prepared,
    NetworkBroker::RequestGuard nativeCurrent, std::shared_ptr<AddonMetadataAttemptAdmission> attempt)
{
    if (QThread::currentThread() != thread()) return {};
    const auto refuse = [&attempt] { if (attempt) attempt->blocked(); return QString{}; };
    if (!QCoreApplication::instance()
        || thread() != QCoreApplication::instance()->thread() || m_retiring || !m_preparedMetadata
        || !prepared || !prepared->m_data) return refuse();
    const auto runtime = m_preparedMetadata;
    const auto data = prepared->m_data;
    const auto& slot = data->slot;
    if (data->owner != runtime->owner || !data->current() || (data->requiresAttempt && !attempt) || (attempt && !attempt->current())
        || data->capturedExecutable != m_workerExecutable || m_withdrawingPackages.contains(slot.package.manifest.id)
        || m_removingPackages.contains(slot.package.manifest.id) || m_store.packageRetiring(slot.package.manifest.id)
        || (attempt && (attempt->m_capture.nativeSubject != data->subject || attempt->m_capture.ordinal != slot.ordinal))) return refuse();
    const QPointer<AddonManager> owner(this);
    const auto gate = attempt ? attempt->m_capture.admission : std::shared_ptr<AddonMetadataNativeAdmission>{};
    const auto lifetime = data->lifetime;
    const auto captureLife = attempt ? attempt->m_capture.lifetime : lifetime;
    const auto capturedLive = attempt ? attempt->m_capture.live : NetworkBroker::RequestGuard{};
    NetworkBroker::RequestGuard current = [data, nativeCurrent, gate, lifetime, captureLife, capturedLive] {
        return data->current() && lifetime.current() && captureLife.current() && (!gate || gate->current())
            && NetworkBroker::guardCurrent(nativeCurrent) && NetworkBroker::guardCurrent(capturedLive);
    };
    if (!NetworkBroker::guardCurrent(current) || !owner) return refuse();
    for (auto it = runtime->gates.begin(); it != runtime->gates.end();) {
        const auto held = it->admission.lock();
        if (!held || !held->current()) it = runtime->gates.erase(it); else ++it;
    }
    if (runtime->gates.size() >= 1024) return refuse();
    const auto permit = ProcessPermit::acquire();
    if (!permit) return refuse();
    auto completion = std::make_shared<ProcessCompletion>(permit); completion->gate = gate;
    // Reserve the joined retirement obligation before a process can start.
    if (!lifetime.startQtWork([completion] { completion->wait(); }, [completion] { completion->cancel(); })) {
        completion->finish(); return refuse();
    }
    auto unfinished = qScopeGuard([completion] { completion->finish(); });
    if (!NetworkBroker::guardCurrent(current) || !owner) return refuse();
    if (attempt && !attempt->bindManager({slot.contribution, slot.contract, slot.feature, slot.operation,
            slot.accountReference, slot.partition, slot.releaseDigest, slot.releaseGeneration,
            slot.settingsRevision, slot.approvedGrants, slot.projectedContext,
            slot.requestedFields, data->absentHttpStatuses})) { completion->finish(); return refuse(); }
    auto entry = std::make_shared<PreparedMetadataRuntime::Entry>();
    entry->data = data; entry->completion = completion; entry->package = slot.package.manifest.id;
    entry->contribution = slot.contribution;
    auto* worker = new AddonWorker(slot.package, &m_network);
    entry->worker = worker; runtime->workers.insert(worker, entry);
    worker->enablePreparedLifecycle(); worker->setExecutable(data->executable);
    worker->setHostServices(&m_hostServices); worker->setRequestGuard(current);
    const auto secretFields = data->secrets;
    worker->setConfiguration(data->settings, data->credentialStatus, [secretFields, current](const QString& field) {
        if (!NetworkBroker::guardCurrent(current)) return QByteArray{};
        auto value = secretFields->fields.value(field);
        if (!NetworkBroker::guardCurrent(current)) { value.fill('\0'); value.clear(); }
        return value;
    });
    const auto operations = m_hostServices.availableOperations();
    const auto all = [&operations](const QStringList& names) {
        for (const auto& name : names) if (!operations.contains(name)) return false;
        return true;
    };
    worker->setHostCapabilities({{QStringLiteral("workerAvailable"), true},
        {QStringLiteral("workerProtocol"), QStringLiteral("wasm-message-v1")},
        {QStringLiteral("safeMode"), false}, {QStringLiteral("coreOperations"), QJsonArray::fromStringList(operations)},
        {QStringLiteral("networkHttpAvailable"), data->tlsAvailable},
        {QStringLiteral("privateStorageAvailable"), all({QStringLiteral("storage.get"), QStringLiteral("storage.set"), QStringLiteral("storage.remove")})},
        {QStringLiteral("webSocketAvailable"), all({QStringLiteral("network.websocket.open"), QStringLiteral("network.websocket.send"), QStringLiteral("network.websocket.receive"), QStringLiteral("network.websocket.close")})},
        {QStringLiteral("mediaStreamAvailable"), property("hakoneNativeStreamAvailable").toBool()},
        {QStringLiteral("media"), QJsonObject{{QStringLiteral("known"), false}}}});
    const QPointer<AddonWorker> weak(worker);
    connect(worker, &AddonWorker::completed, worker, [entry, weak](const QString& request, const QString& status,
        const QJsonObject& result, const QString& error) {
        if (entry->request != request || entry->canceled || entry->terminal) return;
        entry->terminal = true; entry->status = status; entry->result = result; entry->error = error;
        if (weak) weak->retirePrepared(false);
    });
    connect(worker, &AddonWorker::unavailable, worker, [weak](const QString&) {
        if (weak) weak->retirePrepared(true);
    });
    auto* poll = new QTimer(worker); poll->setInterval(25);
    connect(poll, &QTimer::timeout, worker, [weak, completion, current] {
        if (completion->canceled.load(std::memory_order_acquire) || !NetworkBroker::guardCurrent(current))
            if (weak) weak->retirePrepared(true);
    });
    // Both callbacks share the same capture. The native join ends only after
    // the signal connection and its queued delivery release all Qt values.
    const auto delivery = std::make_shared<PreparedDelivery>();
    delivery->tail = std::make_shared<PreparedDeliveryTail>(entry->completion);
    delivery->runtime = runtime; delivery->entry = entry; delivery->owner = owner; delivery->current = current;
    connect(worker, &AddonWorker::preparedRetired, QCoreApplication::instance(), [delivery] {
        // Delete from a different QObject's event stack, after QProcess signals.
        QTimer::singleShot(0, QCoreApplication::instance(), [delivery] {
            const auto& runtime = delivery->runtime; const auto& entry = delivery->entry;
            const auto owner = delivery->owner; const auto& current = delivery->current;
            const auto request = entry->request, contribution = entry->contribution;
            const auto publicRequest = entry->publicRequest.isEmpty() ? request : entry->publicRequest;
            auto status = entry->status, error = entry->error;
            const auto result = entry->result;
            const bool canceled = entry->canceled || entry->completion->canceled.load(std::memory_order_acquire);
            const bool permitted = NetworkBroker::guardCurrent(current) && entry->worker && entry->worker->preparedCleanExit();
            const bool deliver = !canceled && entry->terminal && owner;
            const auto worker = entry->worker;
            runtime->workers.remove(worker.data());
            if (runtime->requests.value(request) == entry) runtime->requests.remove(request);
            if (entry->data) entry->data->secrets->wipe();
            if (worker) delete worker.data();
            entry->worker = nullptr; entry->data.reset();
            // QObject weak pointers can remain valid during member destruction.
            // Check the retained owner flag before any Manager member access.
            if (!runtime->owner->open.load(std::memory_order_acquire) || !owner) return;
            owner->m_invocationPackages.remove(request); owner->m_invocationContributions.remove(request);
            runtime->aliases.remove(publicRequest);
            if (!deliver) return;
            if (!permitted) { status = entry->publicRequest.isEmpty() ? QStringLiteral("denied") : QStringLiteral("error"); error = QStringLiteral("The metadata invocation is no longer current."); }
            emit owner->operationFinished(publicRequest, contribution, status, permitted ? result.toVariantMap() : QVariantMap{}, error);
        });
    });
    poll->start();
    unfinished.dismiss(); // The preparedRetired cleanup now owns this obligation.
    const auto request = worker->invoke(slot.declaration.id, slot.operation, slot.projectedContext, current, attempt);
    if (!runtime->owner->open.load(std::memory_order_acquire) || !owner || !weak) return {};
    entry->request = request;
    if (request.isEmpty() || !NetworkBroker::guardCurrent(current)
        || !runtime->owner->open.load(std::memory_order_acquire) || !owner || !weak) {
        entry->canceled = true; if (weak) weak->retirePrepared(true); return refuse();
    }
    runtime->requests.insert(request, entry);
    if (gate) runtime->gates.insert(request, {entry->package, gate});
    m_invocationPackages.insert(request, entry->package); m_invocationContributions.insert(request, slot.contribution);
    return request;
}
std::optional<bool> AddonManager::preparedStorageContext(const AddonHostServices::Context& host,
    PrivateStorageContext* output, QString* error) const
{
    if (!m_preparedMetadata) return std::nullopt;
    const auto entry = m_preparedMetadata->requests.value(host.invocation);
    if (!entry) return std::nullopt;
    const auto refuse = [error] {
        if (error) *error = QStringLiteral("The prepared storage invocation is no longer current.");
        return false;
    };
    if (!output || m_retiring || entry->canceled || !entry->data || !entry->data->current()
        || entry->package != host.package || entry->contribution != host.contribution
        || entry->data->slot.approvedGrants != host.grants || !NetworkBroker::guardCurrent(host.current)) return refuse();
    output->package = entry->data->slot.package; output->settings = entry->data->slot.settings;
    output->settingsRevision = entry->data->slot.settingsRevision;
    output->accountReference = entry->data->slot.accountReference;
    return true;
}
bool AddonManager::cancelPreparedMetadata(const QString& request)
{
    const auto runtime = m_preparedMetadata;
    if (!runtime) return false;
    if (cancelMetadataPreparation(request)) return true;
    const auto actual = runtime->aliases.take(request);
    const auto key = actual.isEmpty() ? request : actual;
    const auto entry = runtime->requests.take(key);
    const auto captured = runtime->gates.take(key);
    if (!actual.isEmpty()) { m_invocationPackages.remove(key); m_invocationContributions.remove(key); }
    const auto gate = captured.admission.lock();
    if (!entry && !gate) return false;
    if (entry) {
        entry->canceled = true;
        if (entry->data) entry->data->secrets->wipe();
        entry->completion->canceled.store(true, std::memory_order_release);
    }
    if (gate) gate->revoke();
    else if (entry) entry->completion->cancel();
    if (entry && entry->worker) entry->worker->retirePrepared(true);
    return true;
}
void AddonManager::retirePreparedMetadata(const QString& package)
{
    const auto runtime = m_preparedMetadata;
    if (!runtime) return;
    if (package.isEmpty()) runtime->owner->open.store(false, std::memory_order_release);
    const auto preparations = runtime->preparations.keys();
    for (const auto& id : preparations)
        if (package.isEmpty() || runtime->preparations.value(id).package == package) cancelMetadataPreparation(id);
    if (package.isEmpty()) {
        runtime->available = {}; runtime->wake.reset();
        if (runtime->jobs) runtime->jobs->close();
        runtime->jobs.reset(); runtime->pending.clear(); runtime->aliases.clear();
    }
    QList<std::shared_ptr<PreparedMetadataRuntime::Entry>> entries;
    QList<std::shared_ptr<AddonMetadataNativeAdmission>> gates;
    for (auto it = runtime->workers.begin(); it != runtime->workers.end();) {
        if (!package.isEmpty() && it.value()->package != package) { ++it; continue; }
        entries.append(it.value()); it = runtime->workers.erase(it);
    }
    for (auto it = runtime->requests.begin(); it != runtime->requests.end();) {
        if (!package.isEmpty() && it.value()->package != package) { ++it; continue; }
        it = runtime->requests.erase(it);
    }
    for (auto it = runtime->gates.begin(); it != runtime->gates.end();) {
        if (!package.isEmpty() && it->package != package) { ++it; continue; }
        if (const auto gate = it->admission.lock()) gates.append(gate);
        it = runtime->gates.erase(it);
    }
    // Finish owner mutations and wipe bytes before any native cancellation.
    for (const auto& entry : entries) {
        entry->canceled = true;
        if (entry->data) entry->data->secrets->wipe();
        entry->completion->canceled.store(true, std::memory_order_release);
        if (const auto gate = entry->completion->gate.lock()) gates.append(gate);
    }
    for (const auto& gate : gates) gate->revoke();
    for (const auto& entry : entries) if (entry->worker) entry->worker->retirePrepared(true);
}
}
