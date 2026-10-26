// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_manager.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSslSocket>
#include <QMetaProperty>
#include <QTimer>
#include <QUuid>
#include <algorithm>

namespace vlc::addons {
namespace {
bool secretField(const QJsonObject& field)
{ return field.value("x-vlc-secret").toBool() || field.value("writeOnly").toBool() || field.value("format").toString() == QLatin1String("password"); }
QJsonObject readObject(const QString& path)
{
    QFile file(path); if (!file.open(QIODevice::ReadOnly) || file.size() > 256 * 1024) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
bool valueValid(const QJsonValue& value, const QJsonObject& field)
{
    const auto type = field.value("type").toString();
    if (field.contains("enum") && !field.value("enum").toArray().contains(value)) return false;
    if (type == QLatin1String("boolean")) return value.isBool();
    if (type == QLatin1String("number") || type == QLatin1String("integer")) {
        if (!value.isDouble()) return false;
        const auto n = value.toDouble();
        if (type == QLatin1String("integer") && n != qint64(n)) return false;
        return n >= field.value("minimum").toDouble(-1e12) && n <= field.value("maximum").toDouble(1e12);
    }
    if (type == QLatin1String("array")) {
        if (!value.isArray() || value.toArray().size() > qMin(64, field.value("maxItems").toInt(64))
            || value.toArray().size() < field.value("minItems").toInt(0)) return false;
        QSet<QByteArray> seen;
        for (const auto& entry : value.toArray()) {
            if (!valueValid(entry, field.value("items").toObject())) return false;
            const auto key = QJsonDocument(QJsonArray{entry}).toJson(QJsonDocument::Compact);
            if (field.value("uniqueItems").toBool() && seen.contains(key)) return false;
            seen.insert(key);
        }
        return true;
    }
    if (type != QLatin1String("string") || !value.isString()) return false;
    const auto text = value.toString();
    if (text.size() < field.value("minLength").toInt(0) || text.size() > qMin(4096, field.value("maxLength").toInt(4096))) return false;
    const auto pattern = field.value("pattern").toString();
    if (!pattern.isEmpty()) {
        // The first SDK supports this bounded language-code grammar. Never
        // compile arbitrary package-supplied PCRE into a UI-thread validator.
        if (pattern != QLatin1String("^[A-Za-z]{2,3}(-[A-Za-z0-9]{2,8})*$")) return false;
        static const QRegularExpression language(QStringLiteral("^[A-Za-z]{2,3}(-[A-Za-z0-9]{2,8})*$"));
        if (text.size() > 64 || !language.match(text).hasMatch()) return false;
    }
    return true;
}
QString localPart(const QString& global) { return global.section('/', 1); }
QString packagePart(const QString& global) { return global.section('/', 0, 0); }
}

AddonManager::AddonManager(const QString& directory, QObject* parent)
    : QObject(parent), m_store(directory), m_network(this), m_hostServices(this)
{
    connect(&m_hostServices, &AddonHostServices::availabilityChanged, this, &AddonManager::capabilitiesChanged);
    connect(this, &AddonManager::revisionChanged, this, &AddonManager::capabilitiesChanged);
    m_providerOrder = readObject(QDir(directory).filePath(QStringLiteral("provider-order.json")));
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
    disconnect(&m_hostServices, nullptr, this, nullptr);
    for (const auto& connection : m_mediaCapabilityConnections) disconnect(connection);
    if (m_repositoryThread) {
        disconnect(m_repositoryThread, nullptr, this, nullptr);
        m_repositoryThread->requestInterruption(); m_repositoryThread->wait(); delete m_repositoryThread;
    }
    const auto workers = m_workers; for (auto* worker : workers) worker->stop(); m_secrets.clearSession();
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
        {"privateStorageAvailable", all({QStringLiteral("storage.get"), QStringLiteral("storage.set"), QStringLiteral("storage.remove")})},
        {"webSocketAvailable", all({QStringLiteral("network.websocket.open"), QStringLiteral("network.websocket.send"), QStringLiteral("network.websocket.close")})},
        {"media", media}};
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
QVariantMap AddonManager::settingsSecretStatus(const QString& id) const
{
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
    const auto document = schema(package); if (document.value("type").toString() != QLatin1String("object")) return false;
    const auto values = QJsonObject::fromVariantMap(settingsValues(package.manifest.id));
    const auto properties = document.value("properties").toObject();
    for (const auto& required : document.value("required").toArray()) {
        const auto name = required.toString();
        if (secretField(properties.value(name).toObject())) { if (credential(package.manifest.id, name).isEmpty()) return false; }
        else if (!values.contains(name) || !valueValid(values.value(name), properties.value(name).toObject())) return false;
    }
    return true;
}

bool AddonManager::saveAddonSettings(const QString& id, const QVariantMap& input, const QVariantMap& secretOperations)
{
    InstalledPackage package; QString error;
    if (!m_store.installed(id, &package, &error)) return changed(false, error);
    const auto document = schema(package), properties = document.value("properties").toObject();
    if (document.value("type").toString() != QLatin1String("object") || properties.size() > 64) return changed(false, tr("VLC cannot use this settings schema."));
    QJsonObject saved; quint64 expectedRevision = 0;
    if (!m_store.settings(id, &saved, &expectedRevision, &error)) return changed(false, error);
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
            return reject(tr("VLC cannot use this credential operation."));
        if (action == QLatin1String("delete")) { references.remove(it.key()); deletedFields.append(it.key()); }
        if (action == QLatin1String("replace")) {
            if (!valueValid(QJsonValue::fromVariant(operation.value("value")), field)) return reject(tr("Make sure that the credential is correct."));
            const auto result = m_secrets.create(id, it.key(), operation.value("value").toString().toUtf8(), operation.value("sessionOnly").toBool());
            if (result.reference.isEmpty()) return reject(tr("The secure credential store is not available. Select temporary storage to keep the credential until VLC closes."));
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
    if (!m_store.saveSettings(id, values, expectedRevision, &error)) return reject(error);
    // Keep old credentials for rollback. Explicit account deletion revokes all generations.
    withdraw(id);
    for (const auto& field : deletedFields)
        for (const auto& reference : m_store.credentialReferences(id, field)) m_secrets.remove(id, reference);
    return changed(true);
}

QVariantList AddonManager::packages() const
{
    QVariantList result;
    const auto installedIds = m_store.installedIds();
    for (const auto& id : installedIds) {
        InstalledPackage package; QString error;
        if (!m_store.installed(id, &package, &error)) {
            result.append(QVariantMap{{"id", id}, {"name", id}, {"installed", true}, {"enabled", false},
                {"configured", false}, {"recoveryRequired", true}, {"error", error}});
            continue;
        }
        result.append(QVariantMap{{"id", id}, {"name", package.manifest.name}, {"version", package.manifest.version},
            {"source", package.identity.source}, {"tag", package.identity.tag},
            {"commit", package.identity.distributionSource ? QString{} : package.identity.commitId},
            {"distributionSource", package.identity.distributionSource}, {"publisherVerified", false},
            {"digest", QString::fromLatin1(package.digest)}, {"enabled", package.enabled},
            {"configured", configured(package)}, {"hasPrevious", !package.previousDigest.isEmpty()},
            {"installed", true}, {"recoveryRequired", package.recoveryRequired}});
    }
    for (auto it = m_distributionDirectories.begin(); it != m_distributionDirectories.end(); ++it)
        if (!installedIds.contains(it.key()) && m_store.distributionSuppressed(it.key())) {
            const auto manifest = readObject(QDir(it.value()).filePath(QStringLiteral("addon.json")));
            result.append(QVariantMap{{"id", it.key()}, {"name", manifest.value("name").toString(it.key())},
                {"distributionSource", true}, {"publisherVerified", false}, {"installed", false},
                {"suppressed", true}, {"enabled", false}, {"configured", false}});
        }
    return result;
}
QVariantList AddonManager::contributionsOfType(const QString& type, bool includeDisabled) const
{
    QVariantList result;
    for (const auto& id : m_store.installedIds()) {
        InstalledPackage package; if (!m_store.installed(id, &package) || (!includeDisabled && !package.enabled)) continue;
        for (const auto& contribution : package.manifest.contributions) {
            if (contribution.type != type) continue;
            result.append(QVariantMap{{"id", QString(id + '/' + contribution.id)}, {"packageId", id},
                {"name", contribution.document.value("name").toString(package.manifest.name)}, {"enabled", package.enabled},
                {"document", contribution.document.toVariantMap()},
                {"cachePartition", QString(QString::fromLatin1(package.digest) + ':' + QString::number(package.generation))}});
        }
    }
    return result;
}
QJsonObject AddonManager::contributionResource(const QString& global, const QString& resourceField) const
{
    static const QSet<QString> fields{QStringLiteral("resource"), QStringLiteral("view"), QStringLiteral("theme"), QStringLiteral("scene")};
    if (!fields.contains(resourceField)) return {};
    InstalledPackage package; if (!m_store.installed(packagePart(global), &package) || !package.enabled) return {};
    const auto* contribution = package.manifest.contribution(localPart(global)); if (!contribution) return {};
    const auto path = contribution->document.value(resourceField).toString();
    if (!package.manifest.files.contains(path)) return {};
    return readObject(QDir(package.directory).filePath(path));
}
QJsonObject AddonManager::grantedScope(const QString& id, const QString& capability) const
{ InstalledPackage package; return m_store.installed(id, &package) && package.enabled ? package.grants.value(capability).toObject() : QJsonObject{}; }
QByteArray AddonManager::contributionFile(const QString& global, const QString& path, qint64 maximumBytes) const
{
    if (!portablePackagePath(path) || maximumBytes < 1 || maximumBytes > Manifest::MaximumFileBytes) return {};
    InstalledPackage package;
    if (!m_store.installed(packagePart(global), &package) || !package.enabled
        || !package.manifest.contribution(localPart(global)) || !package.manifest.files.contains(path)) return {};
    QFile file(QDir(package.directory).filePath(path));
    if (QFileInfo(file).isSymLink() || file.size() < 0 || file.size() > maximumBytes || !file.open(QIODevice::ReadOnly)) return {};
    const auto bytes = file.read(maximumBytes + 1);
    return bytes.size() <= maximumBytes ? bytes : QByteArray{};
}
QVariantList AddonManager::providersForFeature(const QString& feature) const
{
    QVariantList result;
    for (const auto& id : m_store.installedIds()) {
        InstalledPackage package; if (!m_store.installed(id, &package)) continue;
        quint64 settingsRevision = 0; QJsonObject settings; m_store.settings(id, &settings, &settingsRevision);
        for (const auto& contribution : package.manifest.contributions) {
            if (contribution.type != QLatin1String("metadata.provider") || !contribution.features.contains(feature)) continue;
            result.append(QVariantMap{{"id", QString(id + '/' + contribution.id)}, {"packageId", id}, {"name", package.manifest.name},
                {"enabled", package.enabled}, {"configured", configured(package)}, {"features", contribution.features},
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
    if (!id.startsWith(QLatin1String("vlc.builtin.")) || packagePart(id) != package || feature.isEmpty()) return;
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
    auto order = m_providerOrder; order.insert(feature, QJsonArray::fromStringList(ids));
    QSaveFile file(QDir(m_store.storageDirectory()).filePath(QStringLiteral("provider-order.json")));
    if (!QDir().mkpath(m_store.storageDirectory()) || !file.open(QIODevice::WriteOnly)) return changed(false, tr("VLC cannot save the provider settings."));
    const auto bytes = QJsonDocument(order).toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size() || !file.commit()) return changed(false, tr("VLC cannot save the provider settings."));
    m_providerOrder = order; return changed(true);
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
        {"developerSource", candidate.identity.developerSource}, {"publisherVerified", false},
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
    m_pendingPackage = std::move(candidate); return changed(true);
}
bool AddonManager::ensureDistributionPackage(const QString& directory, const QString& expectedPackageId)
{
    if (expectedPackageId.isEmpty()) return changed(false, tr("The distribution package ID is invalid."));
    m_distributionDirectories.insert(expectedPackageId, directory);
    if (m_store.distributionSuppressed(expectedPackageId)) return true;
    InstalledPackage existing;
    if (m_store.installed(expectedPackageId, &existing)) return true;
    if (QFileInfo::exists(QDir(m_store.storageDirectory()).filePath(QStringLiteral("packages/") + expectedPackageId + QStringLiteral("/state.json"))))
        return changed(false, tr("The installed package state is invalid. Restore the package before the next import."));
    Manifest manifest; QString error;
    if (!Manifest::loadDirectory(directory, &manifest, nullptr, &error)) return changed(false, error);
    if (manifest.id != expectedPackageId)
        return changed(false, tr("The distribution package ID is invalid."));
    ReleaseIdentity identity;
    identity.source = manifest.source.toString(); identity.tag = QStringLiteral("v") + manifest.version;
    identity.distributionSource = true;
    PackageCandidate candidate;
    if (!PackageStore::inspectDirectory(directory, identity, &candidate, &error)
        || !m_store.install(candidate, manifest.requiredPermissions, &error)
        || !m_store.setEnabled(manifest.id, true, &error)) return changed(false, error);
    return changed(true);
}
bool AddonManager::restoreDistributionPackage(const QString& id)
{
    if (!m_distributionDirectories.contains(id))
        return changed(false, tr("VLC has no trusted distribution path for this package."));
    QString error;
    if (!m_store.setDistributionSuppressed(id, false, &error)) return changed(false, error);
    if (ensureDistributionPackage(m_distributionDirectories.value(id), id)) return true;
    m_store.setDistributionSuppressed(id, true);
    return false;
}
bool AddonManager::stageRepository(const QString& source, const QString& tag)
{
    if (busy()) return changed(false, tr("A repository request is already active."));
    if (source.size() > 8192 || tag.size() > 128) return changed(false, tr("The repository address or tag is too long."));
    m_pendingPackage.reset();
    auto candidate = std::make_shared<PackageCandidate>();
    auto error = std::make_shared<QString>();
    auto success = std::make_shared<bool>(false);
    m_repositoryThread = QThread::create([candidate, error, success, source, tag] {
        *success = PackageStore::fetchGitTag(source, tag, candidate.get(), error.get());
    });
    auto* thread = m_repositoryThread;
    connect(thread, &QThread::finished, this, [this, thread, candidate, error, success] {
        if (m_repositoryThread == thread) m_repositoryThread = nullptr;
        if (*success && !thread->isInterruptionRequested()) m_pendingPackage = candidate;
        changed(*success && !thread->isInterruptionRequested(), thread->isInterruptionRequested() ? tr("VLC canceled the repository request.") : *error);
        thread->deleteLater();
    });
    thread->start(); return changed(true);
}
bool AddonManager::checkRepositoryUpdates(const QString& id, const QString& range, bool allowPrerelease)
{
    if (busy()) return changed(false, tr("A repository request is already active."));
    if (id.size() > 128 || range.size() > 256) return changed(false, tr("The package ID or version range is too long."));
    m_pendingPackage.reset(); m_updateResult = {{"packageId", id}, {"checking", true}};
    auto candidate = std::make_shared<PackageCandidate>();
    auto error = std::make_shared<QString>(); auto success = std::make_shared<bool>(false);
    const auto directory = m_store.storageDirectory();
    m_repositoryThread = QThread::create([candidate, error, success, directory, id, range, allowPrerelease] {
        PackageStore store(directory);
        *success = store.fetchRepositoryUpdate(id, range, allowPrerelease, candidate.get(), error.get());
    });
    auto* thread = m_repositoryThread;
    connect(thread, &QThread::finished, this, [this, thread, candidate, error, success, id] {
        if (m_repositoryThread == thread) m_repositoryThread = nullptr;
        const bool canceled = thread->isInterruptionRequested();
        const bool available = *success && !canceled && !candidate->manifest.id.isEmpty();
        if (available) m_pendingPackage = candidate;
        m_updateResult = {{"packageId", id}, {"checking", false}, {"available", available}, {"canceled", canceled},
            {"tag", available ? candidate->identity.tag : QString{}}, {"publisherVerified", false}};
        changed(*success && !canceled, canceled ? tr("VLC canceled the repository request.") : *error);
        thread->deleteLater();
    });
    thread->start(); return changed(true);
}

bool AddonManager::approvePendingPackage(bool enable)
{
    if (busy() || !m_pendingPackage) return changed(false, tr("Select an add-on package first."));
    const auto candidate = m_pendingPackage; QString error;
    withdraw(candidate->manifest.id);
    if (!m_store.install(*candidate, candidate->manifest.requiredPermissions, &error)) return changed(false, error);
    m_pendingPackage.reset();
    if (enable && !m_store.setEnabled(candidate->manifest.id, true, &error)) return changed(false, error);
    return changed(true);
}
void AddonManager::discardPendingPackage()
{
    if (m_repositoryThread) m_repositoryThread->requestInterruption();
    m_pendingPackage.reset(); changed(true);
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
    withdraw(manifest.id);
    return changed(m_store.install(candidate, approve ? manifest.requiredPermissions : QJsonObject{}, &error), error);
}
bool AddonManager::setEnabled(const QString& id, bool enabled)
{
    bool builtin = false;
    for (const auto& descriptor : m_builtinProviders) if (descriptor.value("packageId").toString() == id) builtin = true;
    if (builtin) {
        auto document = m_providerOrder, preferences = document.value("_builtinEnabled").toObject();
        preferences.insert(id, enabled); document.insert("_builtinEnabled", preferences);
        QSaveFile file(QDir(m_store.storageDirectory()).filePath(QStringLiteral("provider-order.json")));
        if (!QDir().mkpath(m_store.storageDirectory()) || !file.open(QIODevice::WriteOnly)) return changed(false, tr("VLC cannot save the provider settings."));
        const auto bytes = QJsonDocument(document).toJson(QJsonDocument::Compact);
        if (file.write(bytes) != bytes.size() || !file.commit()) return changed(false, tr("VLC cannot save the provider settings."));
        m_providerOrder = document;
        for (auto& descriptor : m_builtinProviders) if (descriptor.value("packageId").toString() == id) descriptor.insert("enabled", enabled);
        return changed(true);
    }
    QString error; withdraw(id); return changed(m_store.setEnabled(id, enabled, &error), error);
}
bool AddonManager::rollback(const QString& id)
{ QString error; withdraw(id); return changed(m_store.rollback(id, &error), error); }
bool AddonManager::setSafeMode(bool enabled)
{
    QString error;
    if (!m_store.setSafeMode(enabled, &error)) return changed(false, error);
    if (enabled) for (const auto& id : m_store.installedIds()) withdraw(id);
    return changed(true);
}
bool AddonManager::recoverPackage(const QString& id)
{ QString error; withdraw(id); return changed(m_store.recover(id, &error), error); }
QVariantList AddonManager::retainedGenerations(const QString& id) const
{ return m_store.retainedGenerations(id).toVariantList(); }
bool AddonManager::uninstall(const QString& id, bool removeData)
{
    QString error; withdraw(id);
    const auto references = m_store.credentialReferences(id);
    if (!m_store.setDistributionSuppressed(id, true, &error)) return changed(false, error);
    const bool success = m_store.uninstall(id, removeData, &error);
    if (success && removeData) for (const auto& reference : references) m_secrets.remove(id, reference);
    return changed(success, error);
}

AddonWorker* AddonManager::worker(const InstalledPackage& package)
{
    if (m_workers.contains(package.manifest.id)) return m_workers.value(package.manifest.id);
    auto* instance = new AddonWorker(package, &m_network, this); instance->setExecutable(m_workerExecutable);
    instance->setHostServices(&m_hostServices);
    const auto id = package.manifest.id;
    instance->setConfiguration(QJsonObject::fromVariantMap(settingsValues(id)), QJsonObject::fromVariantMap(settingsSecretStatus(id)),
        [this, id](const QString& field) { return credential(id, field); });
    connect(instance, &AddonWorker::completed, this, [this](const QString& request, const QString& status, const QJsonObject& data, const QString& error) {
        const auto contribution = m_invocationContributions.take(request); m_invocationPackages.remove(request);
        if (!contribution.isEmpty()) emit operationFinished(request, contribution, status, data.toVariantMap(), error);
    });
    connect(instance, &AddonWorker::unavailable, this, [this, id, instance](const QString& error) {
        if (m_workers.value(id) == instance) m_workers.remove(id);
        instance->deleteLater(); changed(false, error);
    });
    m_workers.insert(id, instance); return instance;
}
QString AddonManager::invoke(const QString& contributionId, const QString& operation, const QVariantMap& context)
{
    const auto packageId = packagePart(contributionId); InstalledPackage package;
    if (!m_store.installed(packageId, &package) || !package.enabled || !configured(package)) {
        const auto request = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QTimer::singleShot(0, this, [this, request, contributionId] { emit operationFinished(request, contributionId, QStringLiteral("auth_required"), {}, tr("Enable this add-on. Then enter its settings.")); });
        return request;
    }
    QJsonObject projected = QJsonObject::fromVariantMap(context);
    auto identity = projected.value("identity").toObject(); QJsonObject permittedIdentity;
    const auto fields = package.manifest.permissionScope(QStringLiteral("metadata.context")).value("fields").toArray();
    for (const auto& field : fields) if (identity.contains(field.toString())) permittedIdentity.insert(field.toString(), identity.value(field.toString()));
    projected.insert("identity", permittedIdentity);
    // Local paths and consumer state never enter the portable provider projection.
    for (const auto& name : {QStringLiteral("url"), QStringLiteral("targets"), QStringLiteral("existingFields"), QStringLiteral("protectedFields"), QStringLiteral("fieldRevisions")}) projected.remove(name);
    auto* instance = worker(package);
    instance->setHostCapabilities(QJsonObject::fromVariantMap(capabilities()));
    const auto request = instance->invoke(localPart(contributionId), operation, projected);
    m_invocationPackages.insert(request, packageId); m_invocationContributions.insert(request, contributionId);
    return request;
}
void AddonManager::cancel(const QString& request)
{
    const auto package = m_invocationPackages.take(request); m_invocationContributions.remove(request);
    if (m_workers.contains(package)) m_workers.value(package)->cancel(request);
}
quint64 AddonManager::fetchAsset(const QString& contributionId, const QJsonObject& descriptor, NetworkBroker::Callback callback)
{
    const auto id = packagePart(contributionId); InstalledPackage package;
    if (!m_store.installed(id, &package) || !package.enabled || !package.manifest.contribution(localPart(contributionId))) {
        QTimer::singleShot(0, this, [callback] { NetworkBroker::Response response; response.error = tr("The add-on is not available."); callback(response); }); return 0;
    }
    return m_network.fetch(package.manifest, package.grants, descriptor,
        [this, id](const QString& field) { return credential(id, field); }, std::move(callback), 16 * 1024 * 1024);
}
quint64 AddonManager::fetchImageAsset(const QString& contributionId, const QJsonObject& descriptor, NetworkBroker::Callback callback)
{
    const auto id = packagePart(contributionId); InstalledPackage package;
    if (!m_store.installed(id, &package) || !package.enabled || !package.manifest.contribution(localPart(contributionId))) {
        QTimer::singleShot(0, this, [callback] { NetworkBroker::Response response; response.error = tr("The add-on is not available."); callback(response); }); return 0;
    }
    auto request = descriptor; request.insert("method", "GET"); request.remove("body");
    return m_network.fetch(package.manifest, package.grants, request,
        [this, id](const QString& field) { return credential(id, field); }, std::move(callback),
        1024 * 1024, NetworkBroker::RequestKind::ImageAsset);
}
void AddonManager::withdraw(const QString& id)
{
    if (auto* instance = m_workers.take(id)) { instance->stop(); instance->deleteLater(); }
    m_network.cancelPackage(id);
    m_hostServices.cancelPackage(id);
    emit packageWithdrawn(id);
    const auto requests = m_invocationPackages.keys(id);
    for (const auto& request : requests) {
        const auto contribution = m_invocationContributions.take(request); m_invocationPackages.remove(request);
        emit operationFinished(request, contribution, QStringLiteral("error"), {}, tr("The add-on settings changed."));
    }
}
bool AddonManager::changed(bool success, const QString& error)
{ m_error = success ? QString{} : error; ++m_revision; emit revisionChanged(); return success; }
}
