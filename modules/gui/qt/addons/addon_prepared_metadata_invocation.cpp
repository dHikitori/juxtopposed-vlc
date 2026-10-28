// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_prepared_metadata_invocation.hpp"
#include "addon_manager.hpp"
#include "addon_artwork_attempt_plan.hpp"
#include "addon_metadata_completion_policy.hpp"
#include "addon_package_configuration.hpp"
#include "addon_storage.hpp"
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
    bool tlsAvailable = false;
    const std::shared_ptr<Secrets> secrets = std::make_shared<Secrets>();
    explicit Data(std::shared_ptr<RecipePermit> value) : permit(std::move(value)) {}
    ~Data() { secrets->wipe(); }
    bool current() const noexcept {
        return owner && owner->open.load(std::memory_order_acquire) && lifetime.current()
            && sourceToken.current() && catalogueToken.current() && preparationToken.current();
    }
};
struct PreparedMetadataRuntime {
    struct Entry {
        QPointer<AddonWorker> worker;
        std::shared_ptr<PreparedMetadataInvocation::Data> data;
        std::shared_ptr<ProcessCompletion> completion;
        QString request, package, contribution;
        QString status, error;
        QJsonObject result;
        bool terminal = false, canceled = false;
    };
    struct Gate { QString package; std::weak_ptr<AddonMetadataNativeAdmission> admission; };
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
            if (error) *error = text; return {};
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
        const QFileInfo worker(executable);
        if (executable.isEmpty() || !worker.isFile() || !worker.isExecutable())
            return refuse(QStringLiteral("The add-on worker is not available."));
        data->executable = worker.absoluteFilePath();
        data->tlsAvailable = QSslSocket::supportsSsl();
        const auto properties = slot.schema.value(QStringLiteral("properties")).toObject();
        for (auto it = properties.begin(); it != properties.end(); ++it) {
            const auto field = it.value().toObject();
            if (it.key().startsWith(QLatin1Char('_')) || packageConfiguration::secretField(field)) continue;
            if (slot.settings.contains(it.key())) data->settings.insert(it.key(), slot.settings.value(it.key()));
            else if (field.contains(QStringLiteral("default"))) data->settings.insert(it.key(), field.value(QStringLiteral("default")));
        }
        const auto declared = authority->package.manifest.permissionScope(QStringLiteral("credentials.use")).value(QStringLiteral("bindings")).toArray();
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
            if (!current() || !authority->current()) return refuse(QStringLiteral("The credential configuration changed."));
            auto secret = authority->credentials(field);
            const auto wipe = qScopeGuard([&] { secret.fill('\0'); });
            if (!current() || !authority->current() || secret.size() > PreparedMetadataInvocation::MaximumFieldBytes
                || secret.size() > PreparedMetadataInvocation::MaximumSecretBytes - bytes || !permit->charge(secret.size()))
                return refuse(QStringLiteral("The prepared credential is not available or exceeds the limit."));
            bytes += secret.size();
            data->credentialStatus.insert(field, QJsonObject{{QStringLiteral("present"), !secret.isEmpty()}});
            data->secrets->fields.insert(field, std::move(secret));
        }
        // The full saved-state reader and resolver end here on joined work.
        if (!current() || !authority->current() || !data->current())
            return refuse(QStringLiteral("The metadata preparation changed before completion."));
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
        || !prepared || !prepared->m_data || !attempt) return refuse();
    const auto runtime = m_preparedMetadata;
    const auto data = prepared->m_data;
    const auto& slot = data->slot;
    if (data->owner != runtime->owner || !data->current() || !attempt->current()
        || data->capturedExecutable != m_workerExecutable || m_withdrawingPackages.contains(slot.package.manifest.id)
        || m_removingPackages.contains(slot.package.manifest.id) || m_store.packageRetiring(slot.package.manifest.id)
        || attempt->m_capture.nativeSubject != data->subject || attempt->m_capture.ordinal != slot.ordinal) return refuse();
    const QPointer<AddonManager> owner(this);
    const auto gate = attempt->m_capture.admission;
    const auto lifetime = data->lifetime;
    const auto captureLife = attempt->m_capture.lifetime;
    const auto capturedLive = attempt->m_capture.live;
    NetworkBroker::RequestGuard current = [data, nativeCurrent, gate, lifetime, captureLife, capturedLive] {
        return data->current() && lifetime.current() && captureLife.current() && gate && gate->current()
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
    if (!attempt->bindManager({slot.contribution, slot.contract, slot.feature, slot.operation,
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
            auto status = entry->status, error = entry->error;
            const auto result = entry->result;
            const bool canceled = entry->canceled || entry->completion->canceled.load(std::memory_order_acquire);
            const bool permitted = NetworkBroker::guardCurrent(current);
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
            if (!deliver) return;
            if (!permitted) { status = QStringLiteral("denied"); error = QStringLiteral("The metadata invocation is no longer current."); }
            emit owner->operationFinished(request, contribution, status, permitted ? result.toVariantMap() : QVariantMap{}, error);
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
    runtime->gates.insert(request, {entry->package, gate});
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
        if (error) *error = QStringLiteral("The prepared storage invocation is no longer current."); return false;
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
    const auto entry = runtime->requests.take(request);
    const auto captured = runtime->gates.take(request);
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
