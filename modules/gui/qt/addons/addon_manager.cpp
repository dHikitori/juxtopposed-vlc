// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_manager.hpp"
#include "addon_catalogue_view.hpp"
#include "addon_package_configuration.hpp"
#include "addon_metadata_attempt.hpp"
#include "addon_metadata_completion_policy.hpp"
#include "addon_storage.hpp"
#include "addon_ui_assets.hpp"
#include "addon_store_authority.hpp"
#include "addon_repository_jobs.hpp"
#include "addon_repository_cancellation.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSslSocket>
#include <QMetaProperty>
#include <QTimer>
#include <QThread>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>

namespace vlc::addons {
namespace {
QJsonObject readObject(const QString& path)
{
    QFile file(path); if (!file.open(QIODevice::ReadOnly) || file.size() > 256 * 1024) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
using packageConfiguration::secretField;
using packageConfiguration::valueValid;
using packageConfiguration::packageAccountReference;
QString localPart(const QString& global) { return global.section('/', 1); }
QString packagePart(const QString& global) { return global.section('/', 0, 0); }

}

AddonManager::AddonManager(const QString& directory, QObject* parent) : AddonManager(directory, {}, parent) {}
std::unique_ptr<AddonManager> AddonManager::withNetworkTransport(const QString& directory,
    std::unique_ptr<QNetworkAccessManager>& value, QObject* parent)
{
    if (!NetworkBroker::canAdoptTransport(value.get(), parent)) return {};
    return std::unique_ptr<AddonManager>(new AddonManager(directory, std::move(value), parent));
}
AddonManager::AddonManager(const QString& directory, std::unique_ptr<QNetworkAccessManager> value, QObject* parent)
    : QObject(parent), m_store(directory), m_network(std::move(value), this), m_hostServices(this)
{
    initializePreparedMetadata();
    m_repositoryJobs=std::make_unique<AddonRepositoryJobs>(AddonStreamLifetime::applicationHandle());
    connect(&m_hostServices, &AddonHostServices::availabilityChanged, this, &AddonManager::capabilitiesChanged);
    connect(this, &AddonManager::revisionChanged, this, &AddonManager::capabilitiesChanged);
    m_storage = std::make_unique<AddonStorage>(m_store, [this](const AddonHostServices::Context& host, PrivateStorageContext* output, QString* error) {
        const auto reject = [error] { if (error) *error = QStringLiteral("The private storage invocation is no longer current."); return false; };
        if (const auto prepared = preparedStorageContext(host, output, error); prepared.has_value()) return *prepared;
        const auto instance = QPointer<AddonWorker>(m_workers.value(host.package));
        if (!instance || m_invocationPackages.value(host.invocation) != host.package
            || m_invocationContributions.value(host.invocation) != host.contribution || !instance->configurationCurrent()) return reject();
        PrivateStorageContext captured;
        if (!m_store.installed(host.package, &captured.package) || !captured.package.enabled || !configured(captured.package)
            || captured.package.grants != host.grants || !captured.package.manifest.contribution(localPart(host.contribution))
            || !m_store.networkConfiguration(captured.package, &captured.settings, &captured.settingsRevision)) return reject();
        captured.accountReference = accountReference(captured.package);
        if (!instance || !instance->configurationCurrent() || !m_store.networkConfigurationMatches(captured.package, captured.settings, captured.settingsRevision)) return reject();
        *output = std::move(captured); return true;
    });
    if (!m_storage->mount(&m_hostServices)) m_storage.reset();
    ProviderPreferences preferences;
    if (m_store.providerPreferences(&preferences)) m_providerOrder = preferences.values;
    const auto executable = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("vlc-addon-worker")
#ifdef Q_OS_WIN
        + QStringLiteral(".exe")
#endif
    );
    if (QFileInfo::exists(executable)) m_workerExecutable = executable;
#ifdef VLC_ADDON_WORKER_BUILD_PATH
    if (m_workerExecutable.isEmpty() && QFileInfo::exists(QStringLiteral(VLC_ADDON_WORKER_BUILD_PATH)))
        m_workerExecutable = QStringLiteral(VLC_ADDON_WORKER_BUILD_PATH);
#endif
}
AddonManager::~AddonManager()
{
    m_retiring = true;
    retirePreparedMetadata();
    // All public outer requests are dead before any member teardown. Detached
    // QObject-owned destroyed hooks still release their exact weak View; they
    // contain no Manager access and do not depend on this map's lifetime.
    m_displayRequests.clear();
    if(m_distributionBootstrapInProgress) {
        if(m_distributionBootstrapCancel)m_distributionBootstrapCancel->store(true,std::memory_order_release);
        m_repositoryJobs->cancel();
    }
    m_store.invalidateAuthority();
    emit retiring();
    if (m_storage) m_storage->close();
    disconnect(&m_hostServices, nullptr, this, nullptr);
    for (const auto& connection : m_mediaCapabilityConnections) disconnect(connection);
    m_repositoryJobs->close();
    const auto workers = m_workers; for (auto* worker : workers) worker->stop(); m_secrets.clearSession();
}
bool AddonManager::busy() const { return m_repositoryJobs->busy(); }
StoreAuthorityToken AddonManager::storeAuthorityToken() const noexcept
{
    if(QThread::currentThread()!=thread() || m_retiring || m_distributionBootstrapInProgress)return {};
    m_readerCaptureBegun=true;return m_store.authorityToken();
}
void AddonManager::setWorkerExecutable(const QString& value)
{ m_workerExecutable = value; emit capabilitiesChanged(); }
QVariantMap AddonManager::capabilities() const
{
    const auto operations = m_hostServices.availableOperations();
    const auto all = [&operations](const QStringList& required) {
        for (const auto& operation : required) if (!operations.contains(operation)) return false;
        return true;
    };
    const bool mediaKnown = m_mediaCapabilitiesSource && m_mediaCapabilitiesSource->thread() == QThread::currentThread()
        && m_mediaCapabilitiesSource->property("isStarted").isValid();
    QVariantMap media{{"known", mediaKnown}};
    const std::pair<const char*, const char*> fields[] = {{"isStarted", "loaded"}, {"seekable", "seekable"},
        {"pausable", "pausable"}, {"ratechangable", "rateChange"}, {"hasVideoOutput", "videoOutput"}};
    for (const auto& field : fields)
        media.insert(QString::fromLatin1(field.second), mediaKnown && m_mediaCapabilitiesSource->property(field.first).toBool());
    const QFileInfo worker(m_workerExecutable);
    return {{"workerAvailable", worker.isFile() && worker.isExecutable()}, {"workerProtocol", "wasm-message-v1"},
        {"safeMode", safeMode()}, {"coreOperations", operations}, {"networkHttpAvailable", QSslSocket::supportsSsl()},
        {"mediaStreamAvailable", !m_retiring && property("hakoneNativeStreamAvailable").toBool()},
        {"privateStorageAvailable", all({QStringLiteral("storage.get"), QStringLiteral("storage.set"), QStringLiteral("storage.remove")})},
        {"webSocketAvailable", all({QStringLiteral("network.websocket.open"), QStringLiteral("network.websocket.send"), QStringLiteral("network.websocket.receive"), QStringLiteral("network.websocket.close")})},
        {"media", media}};
}
void AddonManager::setNativeStreamAvailable(bool available)
{
    if (m_retiring || property("hakoneNativeStreamAvailable").toBool() == available) return;
    const QPointer<AddonManager> alive(this);
    setProperty("hakoneNativeStreamAvailable", available);
    if (alive && !m_retiring) emit capabilitiesChanged();
}
void AddonManager::setNativeStreamTransportHint(bool known,bool available)
{
    if(QThread::currentThread()!=thread() || m_retiring)return;
    const int next=known?(available?2:1):0;
    if(property("hakoneNativeStreamTransport").toInt()==next)return;
    const QPointer<AddonManager> alive(this);setProperty("hakoneNativeStreamTransport",next);
    if(alive && !m_retiring)emit capabilitiesChanged();
}
void AddonManager::setMediaCapabilitiesSource(QObject* source)
{
    for (const auto& connection : m_mediaCapabilityConnections) disconnect(connection);
    m_mediaCapabilityConnections.clear();
    m_mediaCapabilitiesSource = source && source->thread() == thread() ? source : nullptr;
    if (m_mediaCapabilitiesSource) {
        const auto notify = QMetaMethod::fromSignal(&AddonManager::capabilitiesChanged);
        for (const auto* name : {"isStarted", "seekable", "pausable", "ratechangable", "hasVideoOutput"}) {
            const int index = m_mediaCapabilitiesSource->metaObject()->indexOfProperty(name);
            if (index < 0) continue;
            const auto property = m_mediaCapabilitiesSource->metaObject()->property(index);
            if (property.hasNotifySignal()) m_mediaCapabilityConnections.append(
                connect(m_mediaCapabilitiesSource, property.notifySignal(), this, notify));
        }
        m_mediaCapabilityConnections.append(connect(m_mediaCapabilitiesSource, &QObject::destroyed, this, &AddonManager::capabilitiesChanged));
    }
    emit capabilitiesChanged();
}
QJsonObject AddonManager::schema(const InstalledPackage& package) const
{
    if (package.manifest.settingsSchema.isEmpty()) return {{"type", "object"}, {"properties", QJsonObject{}}, {"additionalProperties", false}};
    return readObject(QDir(package.directory).filePath(package.manifest.settingsSchema));
}
QJsonObject AddonManager::savedSettings(const QString& id) const
{ QJsonObject values; m_store.settings(id, &values); return values; }
QString AddonManager::accountReference(const InstalledPackage& package) const
{
    return packageAccountReference(package, savedSettings(package.manifest.id), schema(package),
        [this, id = package.manifest.id](const QString& reference) { return m_secrets.present(id, reference); });
}
QVariantMap AddonManager::settingsSchema(const QString& id) const
{ InstalledPackage package; return m_store.installed(id, &package) ? schema(package).toVariantMap() : QVariantMap{}; }
QVariantMap AddonManager::settingsMessages(const QString& id) const
{
    InstalledPackage package; if (!m_store.installed(id, &package)) return {};
    const auto locales = package.manifest.document.value("locales").toObject();
    const auto files = locales.value("files").toObject();
    QString language = QLocale().name().replace('_', '-');
    if (!files.contains(language)) language = language.section('-', 0, 0);
    if (!files.contains(language)) language = locales.value("default").toString();
    const auto path = files.value(language).toString();
    if (!package.manifest.files.contains(path)) return {};
    return readObject(QDir(package.directory).filePath(path)).toVariantMap();
}
QVariantMap AddonManager::settingsValues(const QString& id) const
{
    InstalledPackage package; if (!m_store.installed(id, &package)) return {};
    const auto properties = schema(package).value("properties").toObject();
    const auto saved = savedSettings(id); QJsonObject values;
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        if (secretField(it.value().toObject())) continue;
        if (saved.contains(it.key())) values.insert(it.key(), saved.value(it.key()));
        else if (it.value().toObject().contains("default")) values.insert(it.key(), it.value().toObject().value("default"));
    }
    return values.toVariantMap();
}
QByteArray AddonManager::credential(const QString& id, const QString& field) const
{ return m_secrets.read(id, savedSettings(id).value("_credentials").toObject().value(field).toString()); }
NetworkBroker::CredentialResolver AddonManager::credentialResolver(const InstalledPackage& package, NetworkBroker::RequestGuard* current) const
{
    if(QThread::currentThread()!=thread() || m_distributionBootstrapInProgress) {
        if(current)*current=[] {return false;};
        return [](const QString&) {return QByteArray{};};
    }
    m_readerCaptureBegun=true;
    if (m_retiring || m_withdrawingPackages.contains(package.manifest.id)) {
        if (current) *current = [] { return false; };
        return [](const QString&) { return QByteArray{}; };
    }
    QJsonObject settings; quint64 revision = 0;
    const bool captured = m_store.networkConfiguration(package, &settings, &revision);
    const auto references = settings.value("_credentials").toObject();
    const auto bindings = package.manifest.permissionScope(QStringLiteral("credentials.use")).value("bindings").toArray();
    QSet<QString> fields;
    const auto approved = package.grants.value(QStringLiteral("credentials.use")).toObject().value("bindings").toArray();
    for (const auto& value : bindings) if (approved.contains(value)) fields.insert(value.toObject().value("field").toString());
    const QPointer<AddonManager> owner(const_cast<AddonManager*>(this));
    NetworkBroker::RequestGuard guard = [owner, package, settings, revision, captured] {
        return captured && owner && !owner->m_retiring && !owner->m_withdrawingPackages.contains(package.manifest.id)
            && owner->m_store.networkConfigurationMatches(package, settings, revision);
    };
    if (current) *current = guard;
    return [owner, guard, references, fields, id = package.manifest.id](const QString& field) {
        if (!guard() || !fields.contains(field)) return QByteArray{};
        return owner->m_secrets.read(id, references.value(field).toString());
    };
}
QVariantMap AddonManager::settingsSecretStatus(const QString& id) const
{
    if (m_withdrawingPackages.contains(id) || m_removingPackages.contains(id) || m_store.packageRetiring(id)) return {};
    InstalledPackage package; if (!m_store.installed(id, &package)) return {};
    const auto properties = schema(package).value("properties").toObject();
    const auto references = savedSettings(id).value("_credentials").toObject(); QVariantMap status;
    for (auto it = properties.begin(); it != properties.end(); ++it) if (secretField(it.value().toObject())) {
        const auto reference = references.value(it.key()).toString();
        status.insert(it.key(), QVariantMap{{"present", m_secrets.present(id, reference)}, {"sessionOnly", m_secrets.sessionOnly(reference)}});
    }
    return status;
}
bool AddonManager::configured(const InstalledPackage& package) const
{
    if (m_retiring || m_withdrawingPackages.contains(package.manifest.id) || m_removingPackages.contains(package.manifest.id) || m_store.packageRetiring(package.manifest.id)) return false;
    return packageConfiguration::configured(savedSettings(package.manifest.id), schema(package),
        [this, id = package.manifest.id](const QString& reference) { return m_secrets.present(id, reference); });
}

bool AddonManager::saveAddonSettings(const QString& id,const QVariantMap& input,const QVariantMap& secretOperations)
{ return saveSettingsImpl(id,input,secretOperations,{}); }
bool AddonManager::saveDisplaySettings(const QString& id,const QString& expectedStamp,const QVariantMap& input,const QVariantMap& secretOperations)
{
    const auto* display=displayCatalogue();
    if(expectedStamp.isEmpty() || expectedStamp.size()>128 || !display || !display->isCurrent()
        || !display->settingsReady(id) || display->settingsStamp(id)!=expectedStamp)
        return changed(false,tr("The settings changed. Wait for the current settings before a save."));
    return saveSettingsImpl(id,input,secretOperations,expectedStamp);
}
bool AddonManager::saveSettingsImpl(const QString& id,const QVariantMap& input,const QVariantMap& secretOperations,const QString& expectedStamp)
{
    if (m_removingPackages.contains(id) || m_store.packageRetiring(id)) return changed(false, tr("The add-on is being removed."));
    InstalledPackage package; QString error;
    if (!m_store.installed(id, &package, &error)) return changed(false, error);
    const auto document = schema(package), properties = document.value("properties").toObject();
    if (document.value("type").toString() != QLatin1String("object") || properties.size() > 64) return changed(false, tr("The app cannot use this settings schema."));
    QJsonObject saved; quint64 expectedRevision = 0;
    if (!m_store.settings(id, &saved, &expectedRevision, &error)) return changed(false, error);
    const QString currentStamp=QString::fromLatin1(package.digest)+QLatin1Char(':')+QString::number(package.generation)
        +QLatin1Char(':')+QString::number(expectedRevision);
    if(!expectedStamp.isEmpty() && currentStamp!=expectedStamp)
        return changed(false,tr("The settings changed. Read the current settings before a save."));
    const auto originalReferences = saved.value("_credentials").toObject();
    auto references = originalReferences; QJsonObject values;
    QStringList created, deletedFields;
    auto reject = [&](const QString& message) { for (const auto& ref : created) m_secrets.remove(id, ref); return changed(false, message); };
    const auto incoming = QJsonObject::fromVariantMap(input);
    for (auto it = incoming.begin(); it != incoming.end(); ++it) {
        const auto field = properties.value(it.key()).toObject();
        if (it.key().startsWith('_') || field.isEmpty() || secretField(field) || !valueValid(it.value(), field))
            return reject(tr("Make sure that the add-on settings are correct."));
        values.insert(it.key(), it.value());
    }
    for (auto it = secretOperations.begin(); it != secretOperations.end(); ++it) {
        const auto field = properties.value(it.key()).toObject(); const auto operation = it.value().toMap();
        const auto action = operation.value("operation").toString();
        if (!secretField(field) || (action != QLatin1String("keep") && action != QLatin1String("replace") && action != QLatin1String("delete")))
            return reject(tr("The app cannot use this credential operation."));
        if (action == QLatin1String("delete")) { references.remove(it.key()); deletedFields.append(it.key()); }
        if (action == QLatin1String("replace")) {
            if (!valueValid(QJsonValue::fromVariant(operation.value("value")), field)) return reject(tr("Make sure that the credential is correct."));
            const auto result = m_secrets.create(id, it.key(), operation.value("value").toString().toUtf8(), operation.value("sessionOnly").toBool());
            if (result.reference.isEmpty()) return reject(tr("The secure credential store is not available. Select temporary storage to keep the credential until the app closes."));
            references.insert(it.key(), result.reference); created.append(result.reference);
        }
    }
    for (const auto& required : document.value("required").toArray()) {
        const auto name = required.toString(); const auto field = properties.value(name).toObject();
        if (secretField(field)) {
            if (!deletedFields.contains(name) && !m_secrets.present(id, references.value(name).toString())) return reject(tr("Enter the account data or API key first."));
        } else if (!values.contains(name)) {
            if (field.contains("default") && valueValid(field.value("default"), field)) values.insert(name, field.value("default"));
            else return reject(tr("Enter all necessary settings."));
        }
    }
    values.insert("_credentials", references);
    if (!m_store.change([&](QString* e) { return m_store.saveSettings(id, values, expectedRevision, expectedStamp, e); },
            [&](const StoreProfileWriteContext& c, QString* e) { return m_store.saveSettings(c, id, values, expectedRevision, expectedStamp, e); },
            &error)) return reject(error);
    // Keep old credentials for rollback. Explicit account deletion revokes all generations.
    if (!withdraw(id)) return false;
    for (const auto& field : deletedFields)
        for (const auto& reference : m_store.credentialReferences(id, field)) m_secrets.remove(id, reference);
    return changed(true);
}

QVariantList AddonManager::packages() const
{
    QVariantList result;
    const auto installedIds = m_store.installedIds();
    const auto savedDataIds = m_store.savedDataIds();
    for (const auto& id : installedIds) {
        InstalledPackage package; QString error;
        if (!m_store.installed(id, &package, &error)) {
            result.append(QVariantMap{{"id", id}, {"name", id}, {"installed", true}, {"enabled", false},
                {"configured", false}, {"recoveryRequired", true}, {"error", error}, {"removing", m_removingPackages.contains(id)}});
            continue;
        }
        const auto app = m_distributionPackages.value(id);
        SemanticVersion offered, installed;
        const bool appUpdate = package.identity.distributionSource && !package.recoveryRequired
            && app.value("source").toString() == package.identity.source
            && SemanticVersion::parse(app.value("version").toString(), &offered)
            && SemanticVersion::parse(package.manifest.version, &installed) && offered.compare(installed) > 0;
        result.append(QVariantMap{{"id", id}, {"name", package.manifest.name}, {"version", package.manifest.version},
            {"source", package.identity.source}, {"tag", package.identity.tag},
            {"commit", package.identity.distributionSource ? QString{} : package.identity.commitId},
            {"distributionSource", package.identity.distributionSource}, {"publisherVerified", false},
            {"developerSource", package.identity.developerSource},
            {"digest", QString::fromLatin1(package.digest)}, {"enabled", package.enabled},
            {"configured", configured(package)}, {"hasPrevious", !package.previousDigest.isEmpty()},
            {"installed", true}, {"recoveryRequired", package.recoveryRequired}, {"removing", m_removingPackages.contains(id)},
            {"appUpdateAvailable", appUpdate}, {"appUpdateVersion", appUpdate ? app.value("version").toString() : QString{}}});
    }
    for (auto it = m_distributionDirectories.begin(); it != m_distributionDirectories.end(); ++it)
        if (!installedIds.contains(it.key()) && m_store.distributionSuppressed(it.key())) {
            const auto manifest = m_distributionPackages.value(it.key());
            result.append(QVariantMap{{"id", it.key()}, {"name", manifest.value("name", it.key()).toString()},
                {"distributionSource", true}, {"publisherVerified", false}, {"installed", false},
                {"suppressed", true}, {"enabled", false}, {"configured", false}, {"savedData", savedDataIds.contains(it.key())}});
        }
    for (const auto& id : savedDataIds)
        if (!installedIds.contains(id) && !std::any_of(result.begin(), result.end(), [&id](const QVariant& row) { return row.toMap().value("id").toString() == id; }))
            result.append(QVariantMap{{"id", id}, {"name", id}, {"installed", false}, {"savedData", true},
                {"enabled", false}, {"configured", false}, {"removing", m_removingPackages.contains(id)}});
    // Disk cleanup can finish before its terminal event reaches the UI.
    for (const auto& id : m_removingPackages) {
        bool present = false;
        for (auto& value : result) {
            auto row = value.toMap();
            if (row.value("id").toString() != id) continue;
            row.insert("removing", true); value = row; present = true; break;
        }
        if (!present) result.append(QVariantMap{{"id", id}, {"name", id}, {"installed", false},
            {"enabled", false}, {"configured", false}, {"removing", true}});
    }
    return result;
}
PackageSummaryReader AddonManager::packageReader() const
{
    Q_ASSERT(QThread::currentThread() == thread());
    if(m_distributionBootstrapInProgress)return {};
    m_readerCaptureBegun=true;
    const auto store = m_store;
    const auto authority = store.authorityToken();
    const auto credentials = m_secrets.snapshotReader();
    const auto distributions = m_distributionPackages;
    const auto directories = m_distributionDirectories;
    const auto removing = m_removingPackages, withdrawing = m_withdrawingPackages;
    const auto bootstrapResults=m_distributionBootstrapResults;
    const bool retiring = m_retiring;
    return [store, authority, credentials, distributions, directories, removing, withdrawing, bootstrapResults, retiring]
        () -> std::optional<PackageSummarySnapshot> {
        const auto current = [&] { return !retiring && authority.current() && !RepositoryCancellation::requested(); };
        const auto boundedObject = [](const QString& path) {
            QFile file(path); if (!file.open(QIODevice::ReadOnly)) return QJsonObject{};
            const auto bytes = file.read(256 * 1024 + 1);
            if (bytes.size() > 256 * 1024 || !file.atEnd() || file.error() != QFile::NoError) return QJsonObject{};
            return QJsonDocument::fromJson(bytes).object();
        };
        if (!current()) return std::nullopt;
        QVariantList result;
        const auto installedIds = store.installedIds(), savedDataIds = store.savedDataIds();
        if (installedIds.size() > 256 || savedDataIds.size() > 256 || directories.size() > 256 || removing.size() > 256)
            return std::nullopt;
        for (const auto& id : installedIds) {
            if (!current()) return std::nullopt;
            InstalledPackage package; QString error;
            if (!store.installed(id, &package, &error)) {
                result.append(QVariantMap{{"id", id}, {"name", id}, {"installed", true}, {"enabled", false},
                    {"configured", false}, {"recoveryRequired", true}, {"error", error}, {"removing", removing.contains(id)}});
                continue;
            }
            if (!current()) return std::nullopt;
            const auto document = package.manifest.settingsSchema.isEmpty()
                ? QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}, {"additionalProperties", false}}
                : boundedObject(QDir(package.directory).filePath(package.manifest.settingsSchema));
            QJsonObject saved; store.settings(id, &saved);
            const auto properties = document.value("properties").toObject();
            QJsonObject values;
            for (auto it = properties.begin(); it != properties.end(); ++it) {
                if (secretField(it.value().toObject())) continue;
                if (saved.contains(it.key())) values.insert(it.key(), saved.value(it.key()));
                else if (it.value().toObject().contains("default")) values.insert(it.key(), it.value().toObject().value("default"));
            }
            bool configured = !withdrawing.contains(id) && !removing.contains(id) && !store.packageRetiring(id)
                && document.value("type").toString() == QLatin1String("object");
            if (configured) for (const auto& required : document.value("required").toArray()) {
                const auto name = required.toString();
                if (secretField(properties.value(name).toObject())) {
                    const auto reference = saved.value("_credentials").toObject().value(name).toString();
                    if (reference.isEmpty() || credentials(id, reference).isEmpty()) { configured = false; break; }
                } else if (!values.contains(name) || !valueValid(values.value(name), properties.value(name).toObject())) {
                    configured = false; break;
                }
            }
            const auto app = distributions.value(id);
            SemanticVersion offered, installed;
            const bool appUpdate = package.identity.distributionSource && !package.recoveryRequired
                && app.value("source").toString() == package.identity.source
                && SemanticVersion::parse(app.value("version").toString(), &offered)
                && SemanticVersion::parse(package.manifest.version, &installed) && offered.compare(installed) > 0;
            result.append(QVariantMap{{"id", id}, {"name", package.manifest.name}, {"version", package.manifest.version},
                {"source", package.identity.source}, {"tag", package.identity.tag},
                {"commit", package.identity.distributionSource ? QString{} : package.identity.commitId},
                {"distributionSource", package.identity.distributionSource}, {"publisherVerified", false},
                {"developerSource", package.identity.developerSource}, {"digest", QString::fromLatin1(package.digest)},
                {"enabled", package.enabled}, {"configured", configured}, {"hasPrevious", !package.previousDigest.isEmpty()},
                {"installed", true}, {"recoveryRequired", package.recoveryRequired}, {"removing", removing.contains(id)},
                {"appUpdateAvailable", appUpdate}, {"appUpdateVersion", appUpdate ? app.value("version").toString() : QString{}}});
        }
        for (auto it = directories.begin(); it != directories.end(); ++it) {
            if (!current()) return std::nullopt;
            if (!installedIds.contains(it.key()) && store.distributionSuppressed(it.key())) {
                const auto manifest = distributions.value(it.key());
                result.append(QVariantMap{{"id", it.key()}, {"name", manifest.value("name", it.key()).toString()},
                    {"distributionSource", true}, {"publisherVerified", false}, {"installed", false},
                    {"suppressed", true}, {"enabled", false}, {"configured", false}, {"savedData", savedDataIds.contains(it.key())}});
            }
        }
        for (const auto& id : savedDataIds)
            if (!installedIds.contains(id) && !std::any_of(result.begin(), result.end(), [&id](const QVariant& row) { return row.toMap().value("id").toString() == id; }))
                result.append(QVariantMap{{"id", id}, {"name", id}, {"installed", false}, {"savedData", true},
                    {"enabled", false}, {"configured", false}, {"removing", removing.contains(id)}});
        for (const auto& id : removing) {
            bool present = false;
            for (auto& value : result) {
                auto row = value.toMap(); if (row.value("id").toString() != id) continue;
                row.insert("removing", true); value = row; present = true; break;
            }
            if (!present) result.append(QVariantMap{{"id", id}, {"name", id}, {"installed", false},
                {"enabled", false}, {"configured", false}, {"removing", true}});
        }
        for(const auto& value:bootstrapResults) {
            const auto bootstrap=value.toMap();if(bootstrap.value(QStringLiteral("success")).toBool())continue;
            const auto id=bootstrap.value(QStringLiteral("id")).toString(),error=bootstrap.value(QStringLiteral("error")).toString();
            bool found=false;
            for(auto& existing:result) {
                auto row=existing.toMap();if(row.value(QStringLiteral("id")).toString()!=id)continue;
                row.insert(QStringLiteral("bootstrapFailed"),true);row.insert(QStringLiteral("bootstrapError"),error);
                if(row.value(QStringLiteral("error")).toString().isEmpty())row.insert(QStringLiteral("error"),error);
                existing=row;found=true;break;
            }
            if(!found)result.append(QVariantMap{{"id",id},{"name",bootstrap.value(QStringLiteral("name"),id)},
                {"installed",false},{"enabled",false},{"configured",false},{"bootstrapFailed",true},
                {"error",error},{"recoveryRequired",bootstrap.value(QStringLiteral("recoveryRequired"))}});
        }
        if (!current() || result.size() > 256) return std::nullopt;
        return PackageSummarySnapshot{std::move(result), authority};
    };
}
std::optional<DisplayCatalogueSource> AddonManager::displayCatalogueSource() const
{
    if(QThread::currentThread()!=thread() || m_retiring)return std::nullopt;
    DisplayCatalogueSource result;
    result.bootstrapping=distributionBootstrapInProgress();
    result.bootstrapGeneration=distributionBootstrapGeneration();
    result.bootstrapResults=distributionBootstrapResults();
    result.queuedAuthority=m_store.authorityToken();
    if(result.bootstrapping)return result;
    m_readerCaptureBegun=true;
    result.locale=QLocale().name().replace(QLatin1Char('_'),QLatin1Char('-'));
    result.reader=m_store.displayCatalogueReader(result.locale);
    result.tlsProbe=[authority=result.queuedAuthority]()->std::optional<bool> {
        if(!authority.current())return std::nullopt;
        const bool supported=QSslSocket::supportsSsl();
        return authority.current()?std::optional<bool>(supported):std::nullopt;
    };
    result.credentialPresence=m_secrets.presenceSnapshot();
    const auto credentials=m_secrets.snapshotReader();
    result.credentials=[credentials,authority=result.queuedAuthority](const QString& package,const QString& reference) {
        if(!authority.current())return QByteArray{};
        auto value=credentials(package,reference);
        if(!authority.current()){value.fill('\0');value.clear();}
        return value;
    };
    result.distributions=m_distributionPackages;result.distributionIds=m_distributionDirectories.keys();
    result.withdrawing=m_withdrawingPackages;result.removing=m_removingPackages;
    result.workerExecutable=m_workerExecutable;
    // Only known scalar/list fields enter joined work. No QObject or callable
    // from a host registration is copied into the display recipe.
    for(const auto& source:m_builtinProviders) {
        QVariantMap row;
        for(const auto& key:{QStringLiteral("id"),QStringLiteral("packageId"),QStringLiteral("name"),
                QStringLiteral("accountReference"),QStringLiteral("cachePartition")})
            if(source.value(key).metaType().id()==QMetaType::QString)row.insert(key,source.value(key));
        for(const auto& key:{QStringLiteral("enabled"),QStringLiteral("configured"),QStringLiteral("builtin"),QStringLiteral("requiresNativeAnchor")})
            if(source.value(key).metaType().id()==QMetaType::Bool)row.insert(key,source.value(key));
        for(const auto& key:{QStringLiteral("features"),QStringLiteral("operations"),QStringLiteral("mediaKinds")})
            row.insert(key,source.value(key).toStringList());
        result.builtins.append(row);
    }
    return result;
}
StoreAuthorityChanges AddonManager::watchDisplayCatalogue(std::function<void()> nativeWake) const
{
    if(QThread::currentThread()!=thread() || m_retiring)return {};
    return m_store.onDisplayChanged(std::move(nativeWake));
}
AddonCatalogueView* AddonManager::displayCatalogue() const
{ return QThread::currentThread()==thread()?AddonCatalogueView::attached(const_cast<AddonManager*>(this)):nullptr; }
quint64 AddonManager::displayCatalogueRevision() const
{const auto* display=displayCatalogue();return display?display->revision():0;}
bool AddonManager::displayCatalogueReady() const
{const auto* display=displayCatalogue();return display && display->isCurrent();}
bool AddonManager::displayCatalogueLoading() const
{const auto* display=displayCatalogue();return display && display->loading();}
QString AddonManager::displayCatalogueError() const
{const auto* display=displayCatalogue();return display?display->error():tr("The extension list is not available.");}
void AddonManager::profileChanged()
{
    if (m_retiring)
        return;
    ProviderPreferences preferences;
    if (m_store.providerPreferences(&preferences))
        m_providerOrder = preferences.values;
    refreshDisplayCatalogue();
    emit catalogueChanged();
    changed(true);
}
void AddonManager::refreshDisplayCatalogue()
{if(auto* display=displayCatalogue())display->refresh();}
QVariantList AddonManager::displayProvidersForFeature(const QString& feature) const
{const auto* display=displayCatalogue();return display?display->providers(feature):QVariantList{};}
QString AddonManager::prepareDisplayFeature(const QString& feature)
{ return prepareDisplayRequest(feature,false); }
QString AddonManager::prepareDisplaySettings(const QString& package)
{ return prepareDisplayRequest(package,true); }
QString AddonManager::prepareDisplayRequest(const QString& subject,bool settings)
{
    if(QThread::currentThread()!=thread() || m_retiring)return {};
    const QPointer<AddonManager> alive(this);
    const QPointer<AddonCatalogueView> captured(displayCatalogue());
    // A successor cannot inherit stale reservations or their inner IDs.
    QList<DisplayRequest> obsolete;
    for(auto it=m_displayRequests.begin();it!=m_displayRequests.end();) {
        if(!it->view || it->view!=captured) {obsolete.append(*it);it=m_displayRequests.erase(it);}else ++it;
    }
    for(const auto& request:obsolete) {
        disconnect(request.ownerDestroyed);
        if(request.view && !request.inner.isEmpty())request.view->release(request.inner);
    }
    if(!alive || m_retiring || !captured || captured!=displayCatalogue() || m_displayRequests.size()>=32)return {};
    static std::atomic<quint64> sequence{0};
    auto previous=sequence.load(std::memory_order_relaxed);
    for(;;) {
        if(previous==std::numeric_limits<quint64>::max())return {};
        if(sequence.compare_exchange_weak(previous,previous+1,std::memory_order_relaxed))break;
    }
    const QString outer=QStringLiteral("display:")+QString::number(previous+1,16);
    m_displayRequests.insert(outer,DisplayRequest{captured,{},{}}); // pending before callback-prone acquisition
    const auto inner=settings?captured->acquireSettings(subject):captured->acquireFeature(subject);
    if(!alive) {if(captured && !inner.isEmpty())captured->release(inner);return {};}
    auto found=m_displayRequests.find(outer);
    if(inner.isEmpty() || m_retiring || !captured || captured!=displayCatalogue() || !captured->requestCurrent(inner)
        || found==m_displayRequests.end() || found->view!=captured) {
        m_displayRequests.remove(outer);
        if(captured && !inner.isEmpty())captured->release(inner);
        return {};
    }
    found->inner=inner;
    // This detached cleanup survives the derived Manager's member teardown.
    // QObject emits destroyed before deleting children; the exact weak View is
    // still valid or already closed. The callback never accesses this Manager.
    found->ownerDestroyed=connect(this,&QObject::destroyed,captured,[captured,inner] {
        if(captured)captured->release(inner);
    });
    return outer;
}
void AddonManager::releaseDisplayRequest(const QString& outer)
{
    if(QThread::currentThread()!=thread())return;
    const auto found=m_displayRequests.find(outer);if(found==m_displayRequests.end())return;
    const auto request=*found;m_displayRequests.erase(found);
    disconnect(request.ownerDestroyed);
    if(request.view && !request.inner.isEmpty())request.view->release(request.inner);
}
bool AddonManager::displayRequestCurrent(const QString& outer) const
{
    if(QThread::currentThread()!=thread() || m_retiring)return false;
    const auto found=m_displayRequests.constFind(outer);
    return found!=m_displayRequests.cend() && found->view && !found->inner.isEmpty()
        && found->view==displayCatalogue() && found->view->requestCurrent(found->inner);
}
QString AddonManager::displaySettingsState(const QString& package) const
{const auto* display=displayCatalogue();return display?display->settingsState(package):QStringLiteral("unavailable");}
QString AddonManager::displaySettingsStamp(const QString& package) const
{const auto* display=displayCatalogue();return display?display->settingsStamp(package):QString{};}
bool AddonManager::displayCatalogueAttached() const
{return displayCatalogue()!=nullptr;}
QVariantMap AddonManager::displaySettingsSchema(const QString& package) const
{const auto* display=displayCatalogue();return display?display->settingsSchema(package):QVariantMap{};}
QVariantMap AddonManager::displaySettingsValues(const QString& package) const
{const auto* display=displayCatalogue();return display?display->settingsValues(package):QVariantMap{};}
QVariantMap AddonManager::displaySettingsMessages(const QString& package) const
{const auto* display=displayCatalogue();return display?display->settingsMessages(package):QVariantMap{};}
QVariantMap AddonManager::displaySettingsSecretStatus(const QString& package) const
{const auto* display=displayCatalogue();return display?display->settingsSecretStatus(package):QVariantMap{};}
QVariantMap AddonManager::displayCapabilities() const
{
    if(QThread::currentThread()!=thread())return {};
    const auto* display=displayCatalogue();
    auto result=display?display->capabilities():QVariantMap{{"safeMode",true},{"safeModeKnown",false},
        {"workerKnown",false},{"workerAvailable",false},{"networkHttpKnown",false},{"networkHttpAvailable",false},
        {"webSocketKnown",false},{"webSocketAvailable",false}};
    const auto operations=m_hostServices.availableOperations();
    const auto all=[&](const QStringList& required){for(const auto& value:required)if(!operations.contains(value))return false;return true;};
    const bool mediaKnown=m_mediaCapabilitiesSource && m_mediaCapabilitiesSource->thread()==QThread::currentThread()
        && m_mediaCapabilitiesSource->property("isStarted").isValid();
    QVariantMap media{{"known",mediaKnown}};
    const std::pair<const char*,const char*> fields[]={{"isStarted","loaded"},{"seekable","seekable"},
        {"pausable","pausable"},{"ratechangable","rateChange"},{"hasVideoOutput","videoOutput"}};
    for(const auto& field:fields)
        media.insert(QString::fromLatin1(field.second),mediaKnown && m_mediaCapabilitiesSource->property(field.first).toBool());
    result.insert(QStringLiteral("media"),media);
    result.insert(QStringLiteral("coreOperations"),operations);result.insert(QStringLiteral("workerProtocol"),QStringLiteral("wasm-message-v1"));
    const bool route=!m_retiring && property("hakoneNativeStreamAvailable").toBool();
    const int transport=property("hakoneNativeStreamTransport").toInt();
    result.insert(QStringLiteral("mediaStreamKnown"),route && transport!=0);
    result.insert(QStringLiteral("mediaStreamAvailable"),route && transport==2);
    result.insert(QStringLiteral("privateStorageAvailable"),all({QStringLiteral("storage.get"),QStringLiteral("storage.set"),QStringLiteral("storage.remove")}));
    result.insert(QStringLiteral("webSocketAvailable"),result.value(QStringLiteral("webSocketAvailable")).toBool() && all({QStringLiteral("network.websocket.open"),QStringLiteral("network.websocket.send"),QStringLiteral("network.websocket.receive"),QStringLiteral("network.websocket.close")}));
    return result;
}
std::optional<MetadataCatalogueSource> AddonManager::metadataCatalogueSource() const
{
    if (QThread::currentThread() != thread()) return std::nullopt;
    if (m_retiring || m_distributionBootstrapInProgress || !m_withdrawingPackages.isEmpty() || !m_removingPackages.isEmpty()) return std::nullopt;
    m_readerCaptureBegun=true;
    MetadataCatalogueSource source;
    source.queuedAuthority = m_store.authorityToken();
    if (!source.current()) return std::nullopt;
    source.catalogueReader = m_store.metadataCatalogueReader();
    const auto credentials = m_secrets.snapshotReader();
    source.credentialReader = [credentials, authority = source.queuedAuthority](const QString& package, const QString& reference) {
        if (!authority.current()) return QByteArray{};
        auto value = credentials(package, reference);
        if (!authority.current()) {
            value.fill('\0');
            value.clear();
        }
        return value;
    };
    source.credentialPresence = m_secrets.presenceSnapshot();
    source.workerExecutable = m_workerExecutable;
    // Reject a mutation between the independent memory captures.
    if (!source.current()) return std::nullopt;
    return source;
}
QVariantList AddonManager::contributionsOfType(const QString& type, bool includeDisabled) const
{
    QVariantList result;
    for (const auto& id : m_store.installedIds()) {
        InstalledPackage package; if (!m_store.installed(id, &package) || (!includeDisabled && !package.enabled)) continue;
        const auto account = accountReference(package);
        for (const auto& contribution : package.manifest.contributions) {
            if (contribution.type != type) continue;
            result.append(QVariantMap{{"id", QString(id + '/' + contribution.id)}, {"packageId", id},
                {"name", contribution.document.value("name").toString(package.manifest.name)}, {"enabled", package.enabled},
                {"document", contribution.document.toVariantMap()},
                {"accountReference", account}, {"configured", configured(package)},
                {"cachePartition", QString(QString::fromLatin1(package.digest) + ':' + QString::number(package.generation))}});
        }
    }
    return result;
}
QJsonObject AddonManager::contributionResource(const QString& global, const QString& resourceField) const
{
    static const QSet<QString> fields{QStringLiteral("resource"), QStringLiteral("view"), QStringLiteral("theme"), QStringLiteral("scene")};
    if (m_retiring || m_withdrawingPackages.contains(packagePart(global)) || !fields.contains(resourceField)) return {};
    InstalledPackage package; if (!m_store.installed(packagePart(global), &package) || !package.enabled) return {};
    const auto* contribution = package.manifest.contribution(localPart(global)); if (!contribution) return {};
    const auto path = contribution->document.value(resourceField).toString();
    if (!package.manifest.files.contains(path)) return {};
    return readObject(QDir(package.directory).filePath(path));
}
QJsonObject AddonManager::grantedScope(const QString& id, const QString& capability) const
{ if (m_retiring || m_withdrawingPackages.contains(id)) return {};
  InstalledPackage package; return m_store.installed(id, &package) && package.enabled ? package.grants.value(capability).toObject() : QJsonObject{}; }
QByteArray AddonManager::contributionFile(const QString& global, const QString& path, qint64 maximumBytes) const
{
    if (m_retiring || m_withdrawingPackages.contains(packagePart(global))
        || !portablePackagePath(path) || maximumBytes < 1 || maximumBytes > Manifest::MaximumFileBytes) return {};
    InstalledPackage package;
    if (!m_store.installed(packagePart(global), &package) || !package.enabled
        || !package.manifest.contribution(localPart(global)) || !package.manifest.files.contains(path)) return {};
    QFile file(QDir(package.directory).filePath(path));
    if (QFileInfo(file).isSymLink() || file.size() < 0 || file.size() > maximumBytes || !file.open(QIODevice::ReadOnly)) return {};
    const auto bytes = file.read(maximumBytes + 1);
    return bytes.size() <= maximumBytes ? bytes : QByteArray{};
}
std::function<UiAssetCapture()> AddonManager::uiAssetReader(const QString& global) const
{
    if(QThread::currentThread()!=thread() || m_distributionBootstrapInProgress)return {};
    m_readerCaptureBegun=true;
    if (m_retiring) return [] { UiAssetCapture result; result.error = QStringLiteral("The add-on manager is closing."); return result; };
    if (m_withdrawingPackages.contains(packagePart(global)))
        return [] { UiAssetCapture result; result.error = QStringLiteral("The add-on is not available."); return result; };
    if (m_removingPackages.contains(packagePart(global)) || m_store.packageRetiring(packagePart(global)))
        return [] { UiAssetCapture result; result.error = QStringLiteral("The add-on is being removed."); return result; };
    // This preparation reads memory only. The detached reader owns no QObject.
    const auto store = m_store;
    const auto presence = m_secrets.presenceSnapshot();
    const auto authority = m_store.authorityToken();
    return [store, presence, global, authority]() {
        UiAssetCapture result;
        const auto rejected = [&result] {
            result.error = QStringLiteral("The image package or its permissions changed.");
            result.registration.clear(); result.assets.clear(); result.current = {}; result.live = {};
            return result;
        };
        InstalledPackage package;
        if (!authority.current() || store.packageRetiring(packagePart(global)) || !store.installed(packagePart(global), &package) || !package.enabled) return rejected();
        const auto* contribution = package.manifest.contribution(localPart(global));
        if (!contribution || !QStringList{"ui.route", "ui.modal", "ui.section", "ui.replacement"}.contains(contribution->type)) return rejected();
        const auto scope = package.grants.value(QStringLiteral("ui.contribute")).toObject();
        const auto hook = contribution->document.value(QStringLiteral("hook")).toString();
        if (scope.isEmpty() || (!hook.isEmpty() && !scope.value(QStringLiteral("hooks")).toArray().contains(hook))) return rejected();
        QJsonObject settings; quint64 settingsRevision = 0;
        if (!store.networkConfiguration(package, &settings, &settingsRevision)) return rejected();
        Manifest verified; QMap<QString, QByteArray> contents;
        if (!Manifest::loadDirectory(package.directory, &verified, &contents, &result.error)
            || packageDigest(contents) != package.digest || verified.id != package.manifest.id) return rejected();
        const auto document = verified.settingsSchema.isEmpty()
            ? QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}, {"additionalProperties", false}}
            : QJsonDocument::fromJson(contents.value(verified.settingsSchema)).object();
        if (document.value("type").toString() != QLatin1String("object")) return rejected();
        const auto properties = document.value("properties").toObject();
        const auto references = settings.value(QStringLiteral("_credentials")).toObject();
        CredentialStore secrets;
        const auto present = [&presence, &secrets, &package](const QString& reference) {
            const auto known = presence.value(package.manifest.id);
            return known.contains(reference) ? known.value(reference) : secrets.present(package.manifest.id, reference);
        };
        for (const auto& required : document.value("required").toArray()) {
            const auto name = required.toString(); const auto field = properties.value(name).toObject();
            if (secretField(field)) { if (!present(references.value(name).toString())) return rejected(); }
            else if (!valueValid(settings.contains(name) ? settings.value(name) : field.value(QStringLiteral("default")), field)) return rejected();
        }
        QJsonObject accountFields, accountReferences; bool connected = true, protectedAccount = false;
        const auto approved = package.grants.value(QStringLiteral("credentials.use")).toObject().value(QStringLiteral("bindings")).toArray();
        for (const auto& binding : verified.permissionScope(QStringLiteral("credentials.use")).value(QStringLiteral("bindings")).toArray()) {
            if (!approved.contains(binding)) continue;
            protectedAccount = true;
            const auto name = binding.toObject().value(QStringLiteral("field")).toString(), reference = references.value(name).toString();
            accountReferences.insert(name, reference); connected &= present(reference);
        }
        for (const auto& name : verified.accountFields) {
            const auto field = properties.value(name).toObject();
            if (!field.isEmpty() && !secretField(field)) {
                const auto value = settings.contains(name) ? settings.value(name) : field.value(QStringLiteral("default"));
                accountFields.insert(name, value.isUndefined() ? QJsonValue(QJsonValue::Null) : value);
            }
        }
        const QJsonObject accountIdentity{{"package", verified.id}, {"source", package.identity.source},
            {"references", accountReferences}, {"account", accountFields}};
        const QByteArray accountBytes = QByteArray("vlc-addon-account-v1\n") + QJsonDocument(accountIdentity).toJson(QJsonDocument::Compact);
        const auto accountHash = QString::fromLatin1(QCryptographicHash::hash(accountBytes, QCryptographicHash::Sha256).toHex());
        const QString account = (!connected && protectedAccount ? QStringLiteral("disconnected:")
            : !protectedAccount && accountFields.isEmpty() ? QStringLiteral("public:") : QStringLiteral("account:")) + accountHash;
        const QJsonObject identity{{"contribution", global}, {"release", QString::fromLatin1(package.digest)},
            {"generation", QString::number(package.generation)}, {"account", account}, {"scope", scope}};
        const QByteArray identityBytes = QByteArray("vlc-ui-assets-v1\n") + QJsonDocument(identity).toJson(QJsonDocument::Compact);
        const auto stamp = QString::fromLatin1(QCryptographicHash::hash(identityBytes, QCryptographicHash::Sha256).toHex());
        const auto* verifiedContribution = verified.contribution(localPart(global));
        if (!verifiedContribution) return rejected();
        const auto declarations = verifiedContribution->document.value(QStringLiteral("assets")).toObject(); qint64 total = 0;
        for (auto it = declarations.begin(); it != declarations.end(); ++it) {
            const auto bytes = contents.value(it.value().toObject().value(QStringLiteral("path")).toString());
            if (bytes.isEmpty() || bytes.size() > 1024 * 1024 || (total += bytes.size()) > 8 * 1024 * 1024) return rejected();
            result.assets.insert(it.key(), bytes);
        }
        result.live = [authority] { return authority.current(); };
        result.current = [store, package, settings, settingsRevision, authority] { return authority.current() && store.networkConfigurationMatches(package, settings, settingsRevision); };
        if (!result.current()) return rejected();
        result.registration = {{"id", global}, {"packageId", verified.id}, {"enabled", true}, {"configured", true},
            {"accountReference", account}, {"assetStamp", stamp}, {"assets", declarations.toVariantMap()}};
        return result;
    };
}
AddonNativeAuthorityReader AddonManager::nativeAuthorityReader(const QString& global) const
{
    if(QThread::currentThread()!=thread() || m_distributionBootstrapInProgress)return {};
    m_readerCaptureBegun=true;
    if (m_retiring) return []() -> std::optional<AddonNativeAuthority> { return std::nullopt; };
    if (m_withdrawingPackages.contains(packagePart(global)) || m_removingPackages.contains(packagePart(global)) || m_store.packageRetiring(packagePart(global)))
        return []() -> std::optional<AddonNativeAuthority> { return std::nullopt; };
    const auto store = m_store;
    const auto authority = store.authorityToken();
    const auto readSecret = m_secrets.snapshotReader();
    return [store, authority, readSecret, global]() -> std::optional<AddonNativeAuthority> {
        AddonNativeAuthority result;
        if (!authority.current() || store.packageRetiring(packagePart(global)) || !store.installed(packagePart(global), &result.package)
            || !result.package.enabled) return std::nullopt;
        auto& package = result.package;
        if (!store.networkConfiguration(package, &result.settings, &result.settingsRevision)) return std::nullopt;
        Manifest verified; QMap<QString, QByteArray> contents;
        if (!Manifest::loadDirectory(package.directory, &verified, &contents)
            || packageDigest(contents) != package.digest || verified.id != package.manifest.id) return std::nullopt;
        package.manifest = std::move(verified);
        const auto* contribution = package.manifest.contribution(localPart(global));
        if (!contribution || global != package.manifest.id + QLatin1Char('/') + contribution->id) return std::nullopt;
        result.contribution = *contribution;
        result.globalId = global;
        const auto document = package.manifest.settingsSchema.isEmpty()
            ? QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}, {"additionalProperties", false}}
            : QJsonDocument::fromJson(contents.value(package.manifest.settingsSchema)).object();
        if (document.value(QStringLiteral("type")).toString() != QLatin1String("object")) return std::nullopt;
        result.schema = document;
        const auto properties = document.value(QStringLiteral("properties")).toObject();
        const auto references = result.settings.value(QStringLiteral("_credentials")).toObject();
        const auto present = [readSecret, id = package.manifest.id](const QString& reference) {
            return !readSecret(id, reference).isEmpty();
        };
        for (const auto& required : document.value(QStringLiteral("required")).toArray()) {
            const auto name = required.toString();
            const auto field = properties.value(name).toObject();
            if (secretField(field)) {
                if (!present(references.value(name).toString())) return std::nullopt;
            } else if (!valueValid(result.settings.contains(name) ? result.settings.value(name)
                : field.value(QStringLiteral("default")), field)) return std::nullopt;
        }
        result.accountReference = packageAccountReference(package, result.settings, document, present);
        if (result.accountReference.startsWith(QStringLiteral("disconnected:"))) return std::nullopt;
        result.live = [authority] { return authority.current(); };
        result.onInvalidated = [authority](std::function<void()> cancel) { return authority.onInvalidated(std::move(cancel)); };
        result.current = [store, package, settings = result.settings, revision = result.settingsRevision, authority] {
            return authority.current() && store.networkConfigurationMatches(package, settings, revision) && authority.current();
        };
        QSet<QString> fields;
        const auto approved = package.grants.value(QStringLiteral("credentials.use")).toObject().value(QStringLiteral("bindings")).toArray();
        for (const auto& binding : package.manifest.permissionScope(QStringLiteral("credentials.use")).value(QStringLiteral("bindings")).toArray())
            if (approved.contains(binding)) fields.insert(binding.toObject().value(QStringLiteral("field")).toString());
        result.credentials = [readSecret, references, fields, id = package.manifest.id, current = result.current](const QString& field) {
            if (!fields.contains(field) || !current()) return QByteArray{};
            auto value = readSecret(id, references.value(field).toString());
            if (!current()) { value.fill('\0'); return QByteArray{}; }
            return value;
        };
        if (!result.current()) return std::nullopt;
        return result;
    };
}
QVariantList AddonManager::providersForFeature(const QString& feature) const
{
    QVariantList result;
    for (const auto& id : m_store.installedIds()) {
        InstalledPackage package; if (!m_store.installed(id, &package)) continue;
        quint64 settingsRevision = 0; QJsonObject settings; m_store.settings(id, &settings, &settingsRevision);
        const auto account = accountReference(package);
        for (const auto& contribution : package.manifest.contributions) {
            if (contribution.type != QLatin1String("metadata.provider") || !contribution.features.contains(feature)) continue;
            result.append(QVariantMap{{"id", QString(id + '/' + contribution.id)}, {"packageId", id}, {"name", package.manifest.name},
                {"enabled", package.enabled}, {"configured", configured(package)}, {"features", contribution.features},
                {"operations", contribution.operations},
                {"accountReference", account},
                {"mediaKinds", contribution.mediaKinds}, {"status", !package.enabled ? tr("Disabled") : configured(package) ? tr("Ready") : tr("Setup required")},
                {"cachePartition", QString(QString::fromLatin1(package.digest) + ':' + QString::number(package.generation) + ':' + QString::number(settingsRevision))}});
        }
    }
    for (const auto& builtin : m_builtinProviders)
        if (builtin.value("features").toStringList().contains(feature)) result.append(builtin);
    const auto order = m_providerOrder.value(feature).toArray();
    auto rank = [&](const QVariant& value) { const auto id = value.toMap().value("id").toString(); for (qsizetype i = 0; i < order.size(); ++i) if (order.at(i).toString() == id) return i; return order.size(); };
    std::stable_sort(result.begin(), result.end(), [&](const QVariant& a, const QVariant& b) { return rank(a) < rank(b); });
    return result;
}
QStringList AddonManager::providerOrder(const QString& feature) const
{ QStringList result; for (const auto& value : m_providerOrder.value(feature).toArray()) result.append(value.toString()); return result; }
void AddonManager::registerBuiltinProvider(const QString& feature, const QVariantMap& input)
{
    const auto id = input.value("id").toString(), package = input.value("packageId").toString();
    if (!id.startsWith(QLatin1String("hakone.builtin.")) || packagePart(id) != package || feature.isEmpty()) return;
    auto descriptor = input; descriptor.insert("builtin", true);
    auto features = descriptor.value("features").toStringList(); if (!features.contains(feature)) features.append(feature);
    descriptor.insert("features", features);
    const auto enabled = m_providerOrder.value("_builtinEnabled").toObject();
    descriptor.insert("enabled", enabled.contains(package) ? enabled.value(package).toBool() : input.value("enabled", true).toBool());
    if (m_builtinProviders.value(id) == descriptor) return;
    m_builtinProviders.insert(id, descriptor); changed(true);
}
void AddonManager::unregisterBuiltinProvider(const QString& id)
{ if (m_builtinProviders.remove(id)) changed(true); }
bool AddonManager::setProviderOrder(const QString& feature, const QStringList& ids)
{
    if (feature.isEmpty() || feature.startsWith('_') || feature.size() > 128 || ids.size() > 128 || QSet<QString>(ids.begin(), ids.end()).size() != ids.size()) return changed(false, tr("The provider order is not correct."));
    QJsonObject saved; QString error;
    if (!m_store.change([&](QString* e) { return m_store.saveProviderOrder(feature, ids, &saved, e); },
            [&](const StoreProfileWriteContext& c, QString* e) { return m_store.saveProviderOrder(c, feature, ids, &saved, e); },
            &error)) return changed(false, error);
    m_providerOrder = saved; return changed(true);
}

QVariantMap AddonManager::inspectLocalPackage(const QString& directory) const
{
    Manifest manifest; QString error;
    if (!Manifest::loadDirectory(directory, &manifest, nullptr, &error)) return {{"valid", false}, {"error", error}};
    return {{"valid", true}, {"id", manifest.id}, {"name", manifest.name}, {"version", manifest.version},
        {"permissions", manifest.requiredPermissions.toVariantMap()}, {"source", manifest.source.toString()}};
}
QVariantMap AddonManager::pendingPackage() const
{
    if (!m_pendingPackage) return {};
    const auto& candidate = *m_pendingPackage;
    return {{"id", candidate.manifest.id}, {"name", candidate.manifest.name}, {"version", candidate.manifest.version},
        {"source", candidate.identity.source}, {"tag", candidate.identity.tag}, {"digest", QString::fromLatin1(candidate.digest)},
        {"permissions", candidate.manifest.requiredPermissions.toVariantMap()},
        {"developerSource", candidate.identity.developerSource}, {"distributionSource", candidate.identity.distributionSource}, {"publisherVerified", false},
        {"hostCapabilities", capabilities()}, {"workerRequired", !candidate.manifest.runtimeEntry.isEmpty()}};
}
bool AddonManager::stageLocalPackage(const QString& directory)
{
    if (busy()) return changed(false, tr("Wait for the repository request to end. You can cancel the request."));
    Manifest manifest; QString error;
    if (!Manifest::loadDirectory(directory, &manifest, nullptr, &error)) return changed(false, error);
    ReleaseIdentity identity; identity.source = manifest.source.toString(); identity.tag = QStringLiteral("v") + manifest.version;
    identity.developerSource = true;
    auto candidate = std::make_shared<PackageCandidate>();
    if (!PackageStore::inspectDirectory(directory, identity, candidate.get(), &error)) return changed(false, error);
    m_pendingPackage = std::move(candidate); m_updateResult.clear(); return changed(true);
}
bool AddonManager::ensureDistributionPackage(const QString& directory,const QString& expectedPackageId)
{
    if(m_distributionBootstrapInProgress)return changed(false,tr("The app is preparing the bundled add-ons. Try again after it finishes."));
    const auto result=distributionBootstrap::ensure(m_store,directory,expectedPackageId,
        m_distributionPackages.value(expectedPackageId).value(QStringLiteral("source")).toString());
    if(!result.offer.isEmpty()) {
        m_distributionDirectories.insert(expectedPackageId,result.directory);
        m_distributionPackages.insert(expectedPackageId,result.offer);
    }
    // Existing/suppressed packages never caused a revision publication in the
    // synchronous API. Keep that behavior for isolated tools and host tests.
    if(result.success && result.status!=QLatin1String("installed"))return true;
    return changed(result.success,result.error);
}
bool AddonManager::beginDistributionBootstrap(QList<DistributionBootstrapEntry> entries)
{
    if(QThread::currentThread()!=thread())return false;
    if(m_retiring || m_readerCaptureBegun || m_distributionBootstrapStarted || m_distributionBootstrapInProgress
        || busy() || !m_distributionPackages.isEmpty() || !m_workers.isEmpty() || m_pendingPackage
        || !m_removingPackages.isEmpty() || !m_withdrawingPackages.isEmpty()
        || findChild<QObject*>(QStringLiteral("hakoneDisplayCatalogue"),Qt::FindDirectChildrenOnly)) {
        m_error=tr("The startup package batch is not available.");return false;
    }
    try {if(!distributionBootstrap::validEntries(entries)){m_error=tr("The startup package list is not correct.");return false;}}
    catch(...){m_error=tr("The startup package list is not available.");return false;}
    std::shared_ptr<DistributionBootstrapBatch> result;
    std::shared_ptr<std::atomic_bool> cancellation;
    try {result=std::make_shared<DistributionBootstrapBatch>();cancellation=std::make_shared<std::atomic_bool>(false);}
    catch(...){m_error=tr("The startup package batch is not available.");return false;}
    m_distributionBootstrapCancel=cancellation;
    m_distributionBootstrapStarted=true;m_distributionBootstrapInProgress=true;
    ++m_distributionBootstrapGeneration;m_distributionBootstrapResults.clear();m_distributionBootstrapError.clear();
    const QPointer<AddonManager> alive(this);emit distributionBootstrapChanged();if(!alive)return false;
    const auto store=m_store;const auto ownedEntries=entries;
    bool accepted=false;
    try {accepted=m_repositoryJobs->submit([store,entries=std::move(entries),result,cancellation] {
        if(cancellation->load(std::memory_order_acquire)){result->canceled=true;return;}
        *result=distributionBootstrap::run(store,entries);
    },[this,ownedEntries,result](AddonRepositoryJobs::Result state) {
        finishDistributionBootstrap(ownedEntries,result.get(),state==AddonRepositoryJobs::Result::Canceled,
            state==AddonRepositoryJobs::Result::Failed?tr("The startup package batch did not finish."):QString{});
    });}catch(...){}
    if(!alive)return false;
    if(accepted && cancellation->load(std::memory_order_acquire))m_repositoryJobs->cancel();
    if(!accepted) {
        finishDistributionBootstrap(ownedEntries,nullptr,false,tr("The app cannot start the package batch. Start the app again to retry."));
        return false;
    }
    return true;
}
void AddonManager::cancelDistributionBootstrap()
{
    if(QThread::currentThread()!=thread() || !m_distributionBootstrapInProgress)return;
    // The flag does not claim that owned work has ended. Keep bootstrap active
    // until real completion, or app retirement prevents further delivery.
    if(m_distributionBootstrapCancel)m_distributionBootstrapCancel->store(true,std::memory_order_release);
    m_repositoryJobs->cancel();
}
void AddonManager::finishDistributionBootstrap(const QList<DistributionBootstrapEntry>& entries,
    const DistributionBootstrapBatch* batch,bool canceled,const QString& error)
{
    if(m_retiring || !m_distributionBootstrapInProgress)return;
    auto directories=m_distributionDirectories;auto offers=m_distributionPackages;
    QVariantList rows;QStringList errors;
    for(qsizetype i=0;i<entries.size();++i) {
        DistributionBootstrapItem item;item.id=entries[i].expectedId;
        if(batch && i<batch->items.size() && batch->items[i].id==item.id)item=batch->items[i];
        else {item.status=canceled?QStringLiteral("canceled"):QStringLiteral("error");item.error=!error.isEmpty()?error
            :canceled?tr("The app canceled the startup package check."):tr("The startup package check did not finish.");}
        if(!item.offer.isEmpty()) {directories.insert(item.id,item.directory);offers.insert(item.id,item.offer);}
        if(!item.success)errors.append(item.id+QStringLiteral(": ")+item.error.left(1024));
        rows.append(item.row());
    }
    m_distributionDirectories=std::move(directories);m_distributionPackages=std::move(offers);
    m_distributionBootstrapResults=std::move(rows);m_distributionBootstrapError=errors.join(QLatin1Char('\n'));
    if(m_distributionBootstrapError.isEmpty() && canceled)m_distributionBootstrapError=tr("The app canceled the startup package batch.");
    m_distributionBootstrapCancel.reset();
    m_error=m_distributionBootstrapError;m_distributionBootstrapInProgress=false;++m_distributionBootstrapGeneration;++m_revision;
    const QPointer<AddonManager> alive(this);emit distributionBootstrapChanged();if(alive)emit revisionChanged();
}
bool AddonManager::restoreDistributionPackage(const QString& id)
{ return stageDistributionPackage(id); }
bool AddonManager::stageDistributionPackage(const QString& id)
{
    if (busy()) return changed(false, tr("Wait for the current add-on request to end."));
    if (!m_distributionDirectories.contains(id) || m_store.packageRetiring(id))
        return changed(false, tr("The app has no available distribution package for this ID."));
    const auto expected = m_distributionPackages.value(id);
    const auto directory = m_distributionDirectories.value(id);
    const auto store = m_store;
    auto candidate = std::make_shared<PackageCandidate>();
    auto error = std::make_shared<QString>(); auto success = std::make_shared<bool>(false);
    m_pendingPackage.reset(); m_updateResult.clear();
    const bool started = m_repositoryJobs->submit([id, expected, directory, store, candidate, error, success] {
        InstalledPackage active;
        const bool installed = store.installed(id, &active);
        if ((installed && (!active.identity.distributionSource || active.identity.developerSource
                || active.identity.source != expected.value("source").toString()))
            || (!installed && !store.distributionSuppressed(id))) {
            *error = QStringLiteral("This package does not use the app distribution source."); return;
        }
        ReleaseIdentity identity; identity.source = expected.value("source").toString();
        identity.tag = QStringLiteral("v") + expected.value("version").toString(); identity.distributionSource = true;
        if (!PackageStore::inspectDirectory(directory, identity, candidate.get(), error.get())) return;
        if (candidate->manifest.id != id || candidate->manifest.source.toString() != identity.source
            || candidate->manifest.version != expected.value("version").toString()) {
            *error = QStringLiteral("The app package changed. Start the app again before a package review."); return;
        }
        if (installed) {
            SemanticVersion next, previous;
            if (!SemanticVersion::parse(candidate->manifest.version, &next)
                || !SemanticVersion::parse(active.manifest.version, &previous) || next.compare(previous) <= 0) {
                *error = QStringLiteral("A newer app package is not available."); return;
            }
        }
        *success = true;
    }, [this, candidate, error, success](AddonRepositoryJobs::Result result) {
        const bool finished = result == AddonRepositoryJobs::Result::Finished;
        if (*success && finished) m_pendingPackage = candidate;
        changed(*success && finished, !finished ? tr("The app package request did not finish.") : *error);
    });
    return changed(started, started ? QString{} : tr("The app cannot start the package request."));
}
bool AddonManager::stageRepository(const QString& source, const QString& tag)
{
    if (busy()) return changed(false, tr("A repository request is already active."));
    if (source.size() > 8192 || tag.size() > 128) return changed(false, tr("The repository address or tag is too long."));
    m_pendingPackage.reset();
    m_updateResult.clear();
    auto candidate = std::make_shared<PackageCandidate>();
    auto error = std::make_shared<QString>();
    auto success = std::make_shared<bool>(false);
    const bool started=m_repositoryJobs->submit([candidate, error, success, source, tag] {
        *success = PackageStore::fetchGitTag(source, tag, candidate.get(), error.get());
    }, [this, candidate, error, success](AddonRepositoryJobs::Result result) {
        const bool canceled=result==AddonRepositoryJobs::Result::Canceled;
        const bool finished=result==AddonRepositoryJobs::Result::Finished;
        if (*success && finished) m_pendingPackage = candidate;
        changed(*success && finished,canceled ? tr("The app canceled the repository request.")
            : !finished ? tr("The repository request did not finish.") : *error);
    });
    return changed(started,started ? QString{} : tr("The app cannot start the repository request."));
}
bool AddonManager::checkRepositoryUpdates(const QString& id, const QString& range, bool allowPrerelease)
{
    if (busy()) return changed(false, tr("A repository request is already active."));
    if (id.size() > 128 || range.size() > 256) return changed(false, tr("The package ID or version range is too long."));
    m_pendingPackage.reset(); m_updateResult = {{"packageId", id}, {"checking", true}};
    auto candidate = std::make_shared<PackageCandidate>();
    auto error = std::make_shared<QString>(); auto success = std::make_shared<bool>(false);
    const auto directory = m_store.storageDirectory();
    const bool started=m_repositoryJobs->submit([candidate, error, success, directory, id, range, allowPrerelease] {
        PackageStore store(directory);
        *success = store.fetchRepositoryUpdate(id, range, allowPrerelease, candidate.get(), error.get());
    }, [this, candidate, error, success, id](AddonRepositoryJobs::Result result) {
        const bool canceled=result==AddonRepositoryJobs::Result::Canceled;
        const bool finished=result==AddonRepositoryJobs::Result::Finished;
        const bool available = *success && finished && !candidate->manifest.id.isEmpty();
        if (available) m_pendingPackage = candidate;
        m_updateResult = {{"packageId", id}, {"checking", false}, {"available", available}, {"canceled", canceled},
            {"tag", available ? candidate->identity.tag : QString{}}, {"publisherVerified", false}};
        changed(*success && finished,canceled ? tr("The app canceled the repository request.")
            : !finished ? tr("The repository request did not finish.") : *error);
    });
    if (!started) m_updateResult={{"packageId",id},{"checking",false},{"available",false},{"canceled",false}};
    return changed(started,started ? QString{} : tr("The app cannot start the repository request."));
}

bool AddonManager::approvePendingPackage(bool enable)
{
    if (busy() || !m_pendingPackage) return changed(false, tr("Select an add-on package first."));
    const auto candidate = m_pendingPackage; QString error;
    const QPointer<AddonManager> alive(this);
    if (!withdraw(candidate->manifest.id)) return false;
    if (!alive) return false;
    if (candidate->identity.distributionSource) {
        const auto known = m_distributionPackages.value(candidate->manifest.id);
        InstalledPackage active;
        if (!m_distributionDirectories.contains(candidate->manifest.id)
            || known.value("source").toString() != candidate->identity.source
            || known.value("version").toString() != candidate->manifest.version
            || (m_store.installed(candidate->manifest.id, &active) && (!active.identity.distributionSource
                || active.identity.source != candidate->identity.source)))
            return changed(false, tr("The app distribution source changed. Do a new package review."));
    }
    const auto grants = candidate->manifest.requiredPermissions;
    const bool distribution = candidate->identity.distributionSource;
    if (!m_store.change([&](QString* e) { return m_store.install(*candidate, grants, e, distribution); },
            [&](const StoreProfileWriteContext& c, QString* e) { return m_store.install(c, *candidate, grants, e, distribution); },
            &error)) return changed(false, error);
    m_pendingPackage.reset();
    m_updateResult.clear();
    const auto installedId = candidate->manifest.id;
    if (enable && !m_store.change([&](QString* e) { return m_store.setEnabled(installedId, true, e); },
            [&](const StoreProfileWriteContext& c, QString* e) { return m_store.setEnabled(c, installedId, true, e); },
            &error)) return changed(false, error);
    return changed(true);
}
void AddonManager::discardPendingPackage()
{
    m_repositoryJobs->cancel();
    m_pendingPackage.reset(); m_updateResult.clear(); changed(true);
}
bool AddonManager::installLocalPackage(const QString& directory, bool approve)
{
    Manifest manifest; QString error;
    if (!Manifest::loadDirectory(directory, &manifest, nullptr, &error)) return changed(false, error);
    if (!approve && !manifest.requiredPermissions.isEmpty()) return changed(false, tr("Give permission before you install this add-on."));
    ReleaseIdentity identity; identity.source = manifest.source.toString(); identity.tag = QStringLiteral("v") + manifest.version;
    identity.developerSource = true;
    PackageCandidate candidate;
    if (!PackageStore::inspectDirectory(directory, identity, &candidate, &error)) return changed(false, error);
    if (!withdraw(manifest.id)) return false;
    m_crashes.remove(manifest.id);
    const auto grants = approve ? manifest.requiredPermissions : QJsonObject{};
    return changed(m_store.change([&](QString* e) { return m_store.install(candidate, grants, e); },
        [&](const StoreProfileWriteContext& c, QString* e) { return m_store.install(c, candidate, grants, e); }, &error), error);
}
bool AddonManager::setEnabled(const QString& id, bool enabled)
{
    if (m_removingPackages.contains(id) || m_store.packageRetiring(id)) return changed(false, tr("The add-on is being removed."));
    bool builtin = false;
    for (const auto& descriptor : m_builtinProviders) if (descriptor.value("packageId").toString() == id) builtin = true;
    if (builtin) {
        QJsonObject saved; QString error;
        if (!m_store.change([&](QString* e) { return m_store.saveBuiltinProviderEnabled(id, enabled, &saved, e); },
                [&](const StoreProfileWriteContext& c, QString* e) { return m_store.saveBuiltinProviderEnabled(c, id, enabled, &saved, e); },
                &error)) return changed(false, error);
        m_providerOrder = saved;
        for (auto& descriptor : m_builtinProviders) if (descriptor.value("packageId").toString() == id) descriptor.insert("enabled", enabled);
        return changed(true);
    }
    QString error; if (!withdraw(id)) return false;
    if (enabled) m_crashes.remove(id);
    return changed(m_store.change([&](QString* e) { return m_store.setEnabled(id, enabled, e); },
        [&](const StoreProfileWriteContext& c, QString* e) { return m_store.setEnabled(c, id, enabled, e); }, &error), error);
}
bool AddonManager::rollback(const QString& id)
{ if (m_store.packageRetiring(id)) return changed(false, tr("The add-on is being removed."));
  QString error; if (!withdraw(id)) return false;
  m_crashes.remove(id);
  return changed(m_store.change([&](QString* e) { return m_store.rollback(id, e); },
      [&](const StoreProfileWriteContext& c, QString* e) { return m_store.rollback(c, id, e); }, &error), error); }
bool AddonManager::setSafeMode(bool enabled)
{
    QString error;
    if (!m_store.change([&](QString* e) { return m_store.setSafeMode(enabled, e); },
            [&](const StoreProfileWriteContext& c, QString* e) { return m_store.setSafeMode(c, enabled, e); },
            &error)) return changed(false, error);
    if (enabled) for (const auto& id : m_store.installedIds()) if (!withdraw(id)) return false;
    return changed(true);
}
bool AddonManager::recoverPackage(const QString& id)
{ if (m_store.packageRetiring(id)) return changed(false, tr("The add-on is being removed."));
  QString error; if (!withdraw(id)) return false;
  m_crashes.remove(id);
  return changed(m_store.change([&](QString* e) { return m_store.recover(id, e); },
      [&](const StoreProfileWriteContext& c, QString* e) { return m_store.recover(c, id, e); }, &error), error); }
QVariantList AddonManager::retainedGenerations(const QString& id) const
{ return m_store.retainedGenerations(id).toVariantList(); }
bool AddonManager::uninstall(const QString& id, bool removeData)
{
    if (busy() || m_removingPackages.contains(id) || m_withdrawingPackages.contains(id)) return changed(false, tr("Wait for the current add-on request to end."));
    auto retirement = std::make_shared<StorePackageRetirement>(m_store.retirePackage(id));
    if (!*retirement) return changed(false, tr("The package ID is not correct."));
    auto store = m_store;
    auto error = std::make_shared<QString>(); auto success = std::make_shared<bool>(false);
    m_removingPackages.insert(id);
    const QPointer<AddonManager> alive(this);
    if (!withdraw(id)) {
        if (alive) m_removingPackages.remove(id);
        return false;
    }
    if (!alive) return false;
    const bool started = m_repositoryJobs->submit([store, id, removeData, retirement, error, success]() mutable {
        // The shared retirement hold refuses fresh readers before this task starts.
        if (!store.canUninstall(id, removeData, error.get())) return;
        if (removeData) {
            InstalledPackage active;
            if (store.installed(id, &active) && !store.change([&](QString* e) { return store.setEnabled(id, false, e); },
                    [&](const StoreProfileWriteContext& c, QString* e) { return store.setEnabled(c, id, false, e); }, error.get())) return;
            CredentialStore credentials;
            if (!credentials.removePackage(id)) {
                *error = QStringLiteral("The app cannot delete all saved keys. The add-on stays disabled. Try deletion again."); return;
            }
        }
        // One permitted write covers suppression and removal.
        *success = store.change([&](QString* e) { return store.setDistributionSuppressed(id, true, e) && store.uninstall(id, removeData, e); },
            [&](const StoreProfileWriteContext& c, QString* e) {
                const auto suppressed = store.setDistributionSuppressed(c, id, true, e);
                return suppressed == StoreProfileGeneration::Change::Applied ? store.uninstall(c, id, removeData, e) : suppressed;
            }, error.get());
    }, [this, id, retirement, error, success](AddonRepositoryJobs::Result result) mutable {
        m_removingPackages.remove(id);
        retirement.reset();
        changed(*success, *success ? QString{} : !error->isEmpty() ? *error
            : result == AddonRepositoryJobs::Result::Canceled ? tr("The app canceled the removal request.") : tr("The removal request did not finish."));
    });
    if (!started) {
        m_removingPackages.remove(id); retirement.reset();
        return changed(false, tr("The app cannot start the removal request."));
    }
    if (removeData) m_secrets.forgetPackage(id);
    retirement.reset();
    return changed(true);
}

bool AddonManager::crashBackoff(const QString& id) const
{
    const auto ledger = m_crashes.constFind(id);
    return ledger != m_crashes.cend() && m_crashClock.isValid() && m_crashClock.elapsed() < ledger->retryAfter;
}
void AddonManager::recordCrash(const QString& id, const QString& error)
{
    if (!m_crashClock.isValid())
        m_crashClock.start();
    const auto now = m_crashClock.elapsed();
    auto& ledger = m_crashes[id];
    while (!ledger.recent.isEmpty() && now - ledger.recent.first() > CrashWindowMs)
        ledger.recent.removeFirst();
    ledger.recent.append(now);
    if (ledger.recent.size() < CrashLimit) {
        // 2 s after the first crash, 4 s after the second.
        ledger.retryAfter = now + (qint64(1000) << ledger.recent.size());
        changed(false, error);
        return;
    }
    m_crashes.remove(id);
    QString name = id, refused;
    InstalledPackage package;
    if (m_store.installed(id, &package) && !package.manifest.name.isEmpty())
        name = package.manifest.name;
    // A quarantine is a saved decision, so every window applies it.
    m_store.change([&](QString* e) { return m_store.setEnabled(id, false, e); },
        [&](const StoreProfileWriteContext& c, QString* e) { return m_store.setEnabled(c, id, false, e); }, &refused);
    changed(false, tr("%1 stopped after it failed three times in five minutes. It is now off. Turn it on again to retry.").arg(name));
}
QVariantMap AddonManager::health(const QString& id) const
{
    const auto ledger = m_crashes.constFind(id);
    if (ledger == m_crashes.cend())
        return {{"crashes", 0}, {"retryInMs", 0}};
    const auto now = m_crashClock.isValid() ? m_crashClock.elapsed() : 0;
    return {{"crashes", ledger->recent.size()}, {"retryInMs", qMax<qint64>(0, ledger->retryAfter - now)}};
}
AddonWorker* AddonManager::worker(const InstalledPackage& package)
{
    // A crashed package waits before its worker starts again (finding F3).
    if (crashBackoff(package.manifest.id)) return nullptr;
    if (m_retiring || m_withdrawingPackages.contains(package.manifest.id)
        || m_removingPackages.contains(package.manifest.id) || m_store.packageRetiring(package.manifest.id)) return nullptr;
    if (auto* previous = m_workers.value(package.manifest.id)) {
        if (!previous->failed() && previous->configurationCurrent()) return previous;
        const QPointer<AddonManager> alive(this);
        if (!withdraw(package.manifest.id) || !alive || m_retiring || m_withdrawingPackages.contains(package.manifest.id)) return nullptr;
        InstalledPackage current;
        if (!m_store.installed(package.manifest.id, &current) || !current.enabled || current.digest != package.digest
            || current.generation != package.generation || current.grants != package.grants || !configured(current)) return nullptr;
        if (auto* replacement = m_workers.value(package.manifest.id))
            return !replacement->failed() && replacement->configurationCurrent() ? replacement : nullptr;
    }
    auto* instance = new AddonWorker(package, &m_network, this); instance->setExecutable(m_workerExecutable);
    instance->setPrecompiledCache(QDir(storageDirectory()).filePath(QStringLiteral("compiled")));
    instance->setHostServices(&m_hostServices);
    const auto id = package.manifest.id;
    NetworkBroker::RequestGuard current;
    auto resolver = credentialResolver(package, &current);
    instance->setConfiguration(QJsonObject::fromVariantMap(settingsValues(id)), QJsonObject::fromVariantMap(settingsSecretStatus(id)), std::move(resolver));
    instance->setRequestGuard(std::move(current));
    connect(instance, &AddonWorker::completed, this, [this](const QString& request, const QString& status, const QJsonObject& data, const QString& error) {
        const auto contribution = m_invocationContributions.take(request); m_invocationPackages.remove(request);
        if (!contribution.isEmpty()) emit operationFinished(request, contribution, status, data.toVariantMap(), error);
    });
    connect(instance, &AddonWorker::unavailable, this, [this, id, owner=QPointer<AddonWorker>(instance)](const QString& error) {
        if (!owner) return;
        const QString failure=error, package=id;
        if (m_workers.value(package) != owner.data()) { owner->deleteLater(); return; }
        const QPointer<AddonManager> alive(this);
        if (!withdraw(package) || !alive) return;
        recordCrash(package, failure);
    }, Qt::QueuedConnection);
    m_workers.insert(id, instance); return instance;
}
QString AddonManager::invoke(const QString& contributionId, const QString& operation, const QVariantMap& context)
{
    return invoke(contributionId, operation, context, {});
}
QString AddonManager::invoke(const QString& contributionId, const QString& operation, const QVariantMap& context,
                            NetworkBroker::RequestGuard current)
{
    return invoke(contributionId, operation, context, std::move(current), {});
}
QString AddonManager::invoke(const QString& contributionId, const QString& operation, const QVariantMap& context,
                            NetworkBroker::RequestGuard current, std::shared_ptr<AddonMetadataAttemptAdmission> attempt)
{
    const QPointer<AddonManager> alive(this);
    const bool permitted = NetworkBroker::guardCurrent(current);
    if (!alive) return {};
    if (!permitted) {
        const auto request = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QTimer::singleShot(0, this, [this, request, contributionId] {
            emit operationFinished(request, contributionId, QStringLiteral("denied"), {}, tr("The add-on request is no longer available."));
        });
        return request;
    }
    const auto packageId = packagePart(contributionId); InstalledPackage package;
    if (m_retiring || m_withdrawingPackages.contains(packageId)) return {};
    if (!m_store.installed(packageId, &package) || !package.enabled || !configured(package)) {
        const auto request = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QTimer::singleShot(0, this, [this, request, contributionId] { emit operationFinished(request, contributionId, QStringLiteral("auth_required"), {}, tr("Enable this add-on. Then enter its settings.")); });
        return request;
    }
    const auto projected = packageConfiguration::projectMetadataContext(package.manifest, package.grants,
        QJsonObject::fromVariantMap(context));
    if (attempt) {
        const auto contribution = package.manifest.contribution(localPart(contributionId));
        const auto feature = projected.value(QStringLiteral("feature")).toString();
        QStringList requested; bool fieldsValid = !projected.contains(QStringLiteral("fields")) || projected.value(QStringLiteral("fields")).isArray();
        for (const auto& field : projected.value(QStringLiteral("fields")).toArray()) {
            if (!field.isString() || field.toString().isEmpty() || field.toString().size() > 128 || requested.contains(field.toString())) fieldsValid = false;
            requested.append(field.toString());
        }
        quint64 settingsRevision = 0; QJsonObject settings;
        const bool configuration = m_store.networkConfiguration(package, &settings, &settingsRevision);
        const auto policy = contribution ? metadataCompletionAbsentStatuses(contribution->document, feature, operation) : std::optional<QSet<int>>{};
        const auto account = accountReference(package);
        const QString partition = QString::fromLatin1(package.digest) + QLatin1Char(':') + QString::number(package.generation)
            + QLatin1Char(':') + QString::number(settingsRevision);
        const bool declared = contribution && contribution->type == QLatin1String("metadata.provider")
            && contribution->contract == QLatin1String("metadata@1") && contribution->features.contains(feature)
            && contribution->operations.contains(operation) && fieldsValid && requested.size() <= 64 && configuration && policy.has_value();
        if (!declared || !attempt->bindManager({contributionId, contribution ? contribution->contract : QString{}, feature,
                operation, account, partition, package.digest, package.generation, settingsRevision, package.grants, projected, requested,
                policy.value_or(QSet<int>{})})) {
            attempt->blocked(); const auto request = QUuid::createUuid().toString(QUuid::WithoutBraces);
            QTimer::singleShot(0, this, [this, request, contributionId] {
                emit operationFinished(request, contributionId, QStringLiteral("denied"), {}, tr("The metadata attempt is not available."));
            });
            return request;
        }
        // Retain native authority independently of invocation.active and the Qt
        // receipt object. No QObject/Manager/file reader enters this guard.
        const auto original = std::move(current); const auto gate = attempt->m_capture.admission;
        const auto lifetime = attempt->m_capture.lifetime; const auto live = attempt->m_capture.live;
        current = [original, gate, lifetime, live] {
            return lifetime.current() && gate && gate->current() && NetworkBroker::guardCurrent(original)
                && NetworkBroker::guardCurrent(live);
        };
    }
    const QPointer<AddonWorker> instance = worker(package);
    if (!alive || !instance) return {};
    instance->setHostCapabilities(QJsonObject::fromVariantMap(capabilities()));
    const auto request = instance->invoke(localPart(contributionId), operation, projected, std::move(current), std::move(attempt));
    if (!alive || !instance) return {};
    m_invocationPackages.insert(request, packageId); m_invocationContributions.insert(request, contributionId);
    return request;
}
void AddonManager::cancel(const QString& request)
{
    const auto package = m_invocationPackages.take(request); m_invocationContributions.remove(request);
    if (cancelPreparedMetadata(request)) return;
    if (m_workers.contains(package)) m_workers.value(package)->cancel(request);
}
quint64 AddonManager::fetchAsset(const QString& contributionId, const QJsonObject& descriptor, NetworkBroker::Callback callback)
{
    const auto id = packagePart(contributionId); InstalledPackage package;
    if (m_retiring || m_withdrawingPackages.contains(id) || m_removingPackages.contains(id) || m_store.packageRetiring(id)
        || !m_store.installed(id, &package) || !package.enabled || !package.manifest.contribution(localPart(contributionId))) {
        QTimer::singleShot(0, this, [callback] { NetworkBroker::Response response; response.error = tr("The add-on is not available."); callback(response); }); return 0;
    }
    NetworkBroker::RequestGuard current; auto resolver = credentialResolver(package, &current);
    return m_network.fetch(package.manifest, package.grants, descriptor,
        std::move(resolver), std::move(callback), 16 * 1024 * 1024, NetworkBroker::RequestKind::BinaryAsset, std::move(current));
}
bool AddonManager::withdraw(const QString& packageId)
{
    const QString id = packageId;
    if (m_withdrawingPackages.contains(id)) return false;
    m_withdrawingPackages.insert(id);
    const QPointer<AddonManager> alive(this);
    auto release = qScopeGuard([alive, id] { if (alive) alive->m_withdrawingPackages.remove(id); });
    const QString error = tr("The add-on settings changed.");
    QList<QPair<QString, QString>> requests;
    const auto oldRequests = m_invocationPackages.keys(id);
    requests.reserve(oldRequests.size());
    for (const auto& request : oldRequests) {
        requests.append({request, m_invocationContributions.take(request)});
        m_invocationPackages.remove(request);
    }
    m_store.invalidateAuthority();
    if (!alive) return false;
    retirePreparedMetadata(id);
    if (!alive) return false;
    if (const QPointer<AddonWorker> instance = m_workers.take(id)) {
        instance->stop();
        if (!alive) return false;
        if (instance) instance->deleteLater();
    }
    m_network.cancelPackage(id);
    if (!alive) return false;
    m_hostServices.cancelPackage(id);
    if (!alive) return false;
    for (const auto& request : requests) {
        emit operationFinished(request.first, request.second, QStringLiteral("error"), {}, error);
        if (!alive) return false;
    }
    m_withdrawingPackages.remove(id);
    release.dismiss();
    emit packageWithdrawn(id);
    return bool(alive);
}
bool AddonManager::changed(bool success, const QString& error)
{ m_error = success ? QString{} : error; ++m_revision; emit revisionChanged(); return success; }
}
