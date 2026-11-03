// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_visualizer_resources.hpp"
#include "addon_manager.hpp"
#include "addon_visual_scene.hpp"
#include "addon_store_authority.hpp"
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSet>
#include <QSignalBlocker>
#include <QUuid>
#include <atomic>
#include <cmath>

namespace vlc::addons {
struct AddonVisualizerResources::Shared {
    std::atomic<quint64> epoch{0};
    QMutex mutex;
    std::shared_ptr<const AddonVisualizerActivation> prepared;
};
namespace {
bool secret(const QJsonObject& field)
{ return field.value("x-hakone-secret").toBool() || field.value("writeOnly").toBool() || field.value("format").toString() == QLatin1String("password"); }
bool valueValid(const QJsonValue& value, const QJsonObject& field)
{
    const auto type = field.value("type").toString();
    if (field.contains("enum") && !field.value("enum").toArray().contains(value)) return false;
    if (type == QLatin1String("boolean")) return value.isBool();
    if (type == QLatin1String("number") || type == QLatin1String("integer")) {
        if (!value.isDouble() || !std::isfinite(value.toDouble())) return false;
        const auto number = value.toDouble();
        return (type != QLatin1String("integer") || std::floor(number) == number)
            && number >= field.value("minimum").toDouble(-1e12) && number <= field.value("maximum").toDouble(1e12);
    }
    if (type == QLatin1String("array")) {
        if (!value.isArray() || value.toArray().size() > qMin(64, field.value("maxItems").toInt(64)) || value.toArray().size() < field.value("minItems").toInt(0)) return false;
        QSet<QByteArray> seen;
        for (const auto& item : value.toArray()) {
            if (!valueValid(item, field.value("items").toObject())) return false;
            const auto key = QJsonDocument(QJsonArray{item}).toJson(QJsonDocument::Compact);
            if (field.value("uniqueItems").toBool() && seen.contains(key)) return false;
            seen.insert(key);
        }
        return true;
    }
    if (type != QLatin1String("string") || !value.isString()) return false;
    const auto text = value.toString(), pattern = field.value("pattern").toString();
    if (text.size() < field.value("minLength").toInt(0) || text.size() > qMin(4096, field.value("maxLength").toInt(4096))) return false;
    if (pattern.isEmpty()) return true;
    if (pattern != QLatin1String("^[A-Za-z]{2,3}(-[A-Za-z0-9]{2,8})*$") || text.size() > 64) return false;
    static const QRegularExpression language(QStringLiteral("^[A-Za-z]{2,3}(-[A-Za-z0-9]{2,8})*\\z"));
    return language.match(text).hasMatch();
}
AddonUiAssets::Options profile()
{
    AddonUiAssets::Options value; value.maximumOutputDimension = 1024; value.maximumAssets = 16;
    value.maximumTickets = 16; value.maximumCacheBytes = 16 * 1024 * 1024; value.maximumImages = value.maximumSessionImages = 1;
    return value;
}
}
AddonUiAssets::Backend AddonVisualizerResources::backend(AddonManager* manager, const std::shared_ptr<Shared>& shared)
{
    const QPointer<AddonManager> owner(manager);
    return {[owner, shared](const QString& contribution) -> UiAssetReader {
        if (!owner) return {};
        const auto directory = owner->storageDirectory(); const auto presence = owner->credentialPresenceSnapshot();
        const auto executable = owner->workerExecutable(); const auto epoch = shared->epoch.load(std::memory_order_acquire);
        const auto authority = owner->storeAuthorityToken();
        return [directory, presence, executable, shared, epoch, contribution, authority] {
            UiAssetCapture capture;
            const PackageStore store(directory);
            const auto current = [shared, epoch, authority] { return authority.current() && shared->epoch.load(std::memory_order_acquire) == epoch; };
            const auto reject = [&capture] {
                capture.registration.clear(); capture.assets.clear(); capture.current = {}; capture.live = {};
                capture.error = QStringLiteral("The app cannot load this visualizer. The app shows the artwork."); return capture;
            };
            auto prepared = std::make_shared<AddonVisualizerActivation>();
            if (!current() || !store.installed(contribution.section('/', 0, 0), &prepared->package) || !prepared->package.enabled) return reject();
            auto& package = prepared->package; const auto* declared = package.manifest.contribution(contribution.section('/', 1));
            if (!declared || declared->type != QLatin1String("visualizer") || declared->contract != QLatin1String("visualizer@1")) return reject();
            const auto grant = package.grants.value(QStringLiteral("audio.analysis")).toObject().value(QStringLiteral("channels")).toArray();
            const auto channels = declared->document.value(QStringLiteral("analysisChannels")).toArray();
            const auto tiers = declared->document.value(QStringLiteral("qualityTiers")).toArray();
            if (channels.isEmpty() || channels.size() > 3 || tiers.isEmpty() || tiers.size() > 3) return reject();
            QSet<QString> seenChannels, seenTiers;
            for (const auto& channel : channels) {
                if (!QStringList{"bands", "waveform", "levels"}.contains(channel.toString()) || seenChannels.contains(channel.toString()) || !grant.contains(channel)) return reject();
                seenChannels.insert(channel.toString());
            }
            for (const auto& tier : tiers) {
                if (!QStringList{"low", "normal", "high"}.contains(tier.toString()) || seenTiers.contains(tier.toString())) return reject();
                seenTiers.insert(tier.toString());
            }
            for (const auto& operation : declared->operations) if (operation != QLatin1String("updateParameters")) return reject();
            if (declared->operations.contains(QStringLiteral("updateParameters")) && package.manifest.runtimeEntry.isEmpty()) return reject();
            QJsonObject settings; quint64 revision = 0;
            if (!current() || !store.networkConfiguration(package, &settings, &revision)) return reject();
            Manifest verified; QMap<QString, QByteArray> contents;
            if (!Manifest::loadDirectory(package.directory, &verified, &contents) || packageDigest(contents) != package.digest || !current()) return reject();
            const auto* entry = verified.contribution(contribution.section('/', 1)); if (!entry) return reject();
            const auto scenePath = entry->document.value(QStringLiteral("scene")).toString();
            if (!verified.files.contains(scenePath) || contents.value(scenePath).size() > 256 * 1024) return reject();
            const auto scene = QJsonDocument::fromJson(contents.value(scenePath));
            if (!scene.isObject() || !AddonVisualScene::validate(scene.object())) return reject();
            prepared->scene = scene.object();
            const auto schema = verified.settingsSchema.isEmpty() ? QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
                : QJsonDocument::fromJson(contents.value(verified.settingsSchema)).object();
            if (schema.value("type").toString() != QLatin1String("object")) return reject();
            const auto properties = schema.value("properties").toObject(), references = settings.value(QStringLiteral("_credentials")).toObject();
            if (properties.size() > 64) return reject();
            CredentialStore secrets;
            const auto present = [&presence, &secrets, &package](const QString& reference) {
                const auto known = presence.value(package.manifest.id);
                return known.contains(reference) ? known.value(reference) : secrets.present(package.manifest.id, reference);
            };
            for (auto it = properties.begin(); it != properties.end(); ++it) {
                const auto field = it.value().toObject();
                if (secret(field)) prepared->secretStatus.insert(it.key(), QJsonObject{{"present", present(references.value(it.key()).toString())}});
                else if (settings.contains(it.key())) prepared->settings.insert(it.key(), settings.value(it.key()));
                else if (field.contains("default")) prepared->settings.insert(it.key(), field.value("default"));
            }
            for (const auto& required : schema.value("required").toArray()) {
                const auto name = required.toString(); const auto field = properties.value(name).toObject();
                if (secret(field) ? !prepared->secretStatus.value(name).toObject().value("present").toBool() : !valueValid(prepared->settings.value(name), field)) return reject();
            }
            if (QJsonDocument(prepared->settings).toJson(QJsonDocument::Compact).size() > 256 * 1024) return reject();
            QJsonObject accountValues, accountReferences; bool connected = true, protectedAccount = false;
            const auto approved = package.grants.value(QStringLiteral("credentials.use")).toObject().value(QStringLiteral("bindings")).toArray();
            for (const auto& binding : verified.permissionScope(QStringLiteral("credentials.use")).value(QStringLiteral("bindings")).toArray()) {
                if (!approved.contains(binding)) continue;
                protectedAccount = true;
                const auto name = binding.toObject().value(QStringLiteral("field")).toString(), reference = references.value(name).toString();
                accountReferences.insert(name, reference); connected &= present(reference);
            }
            for (const auto& name : verified.accountFields) {
                const auto field = properties.value(name).toObject(); if (field.isEmpty() || secret(field)) continue;
                const auto value = settings.contains(name) ? settings.value(name) : field.value(QStringLiteral("default"));
                accountValues.insert(name, value.isUndefined() ? QJsonValue(QJsonValue::Null) : value);
            }
            const QJsonObject identity{{"package", verified.id}, {"source", package.identity.source}, {"references", accountReferences}, {"account", accountValues}};
            const QByteArray accountBytes = QByteArray("vlc-addon-account-v1\n") + QJsonDocument(identity).toJson(QJsonDocument::Compact);
            const auto digest = QString::fromLatin1(QCryptographicHash::hash(accountBytes, QCryptographicHash::Sha256).toHex());
            prepared->account = (!connected && protectedAccount ? QStringLiteral("disconnected:")
                : !protectedAccount && accountValues.isEmpty() ? QStringLiteral("public:") : QStringLiteral("account:")) + digest;
            prepared->key = QUuid::createUuid().toString(QUuid::WithoutBraces); prepared->executable = executable;
            prepared->settingsRevision = revision;
            prepared->live = current;
            prepared->current = [store, package, settings, revision, current] { return current() && store.networkConfigurationMatches(package, settings, revision); };
            const auto paths = prepared->scene.value(QStringLiteral("assets")).toObject(); QVariantMap declarations; qint64 total = 0;
            for (auto it = paths.begin(); it != paths.end(); ++it) {
                const auto path = it.value().toString(); const auto bytes = contents.value(path);
                if (!verified.files.contains(path) || bytes.isEmpty() || bytes.size() > 1024 * 1024 || (total += bytes.size()) > 8 * 1024 * 1024
                    || !QStringList{"png", "jpg", "jpeg", "webp"}.contains(QFileInfo(path).suffix().toLower())) return reject();
                declarations.insert(it.key(), QVariantMap{{"path", path}}); capture.assets.insert(it.key(), bytes);
            }
            if (!verified.runtimeEntry.isEmpty()) prepared->runtime = contents.value(verified.runtimeEntry);
            if (!prepared->current()) return reject();
            capture.current = prepared->current;
            capture.live = current;
            capture.registration = {{"packageId", verified.id}, {"enabled", true}, {"configured", true}, {"accountReference", prepared->account},
                {"assetStamp", prepared->key}, {"captureKey", prepared->key}, {"assets", declarations}};
            { QMutexLocker lock(&shared->mutex); if (!current()) return reject(); shared->prepared = std::move(prepared); }
            return capture;
        };
    },{},[owner](std::function<void()> wake) { return owner?owner->watchDisplayCatalogue(std::move(wake)):StoreAuthorityChanges{}; }};
}
AddonVisualizerResources::AddonVisualizerResources(AddonManager* manager, QObject* parent)
    : QObject(parent), m_manager(manager), m_shared(std::make_shared<Shared>()), m_assets(new AddonUiAssets(backend(manager, m_shared), profile(), this))
{
    connect(m_assets, &AddonUiAssets::sessionAssetsChanged, this, [this](const QString& session) { if (session == m_session) refresh(); });
    connect(m_assets, &AddonUiAssets::imageBudgetChanged, this, [this] { if (!m_session.isEmpty() && m_claim.isEmpty()) refresh(); });
    if (manager) {
        connect(manager, &AddonManager::revisionChanged, this, &AddonVisualizerResources::close);
        connect(manager, &QObject::destroyed, this, &AddonVisualizerResources::close);
        connect(manager, &AddonManager::packageWithdrawn, this, [this](const QString& id) {
            if (m_prepared && m_prepared->package.manifest.id == id) close();
        });
    }
}
AddonVisualizerResources::~AddonVisualizerResources()
{ disconnect(m_assets, nullptr, this, nullptr); clear(false); }
bool AddonVisualizerResources::load(const QString& contribution, QObject* surface)
{
    clear(false); if (!m_manager || contribution.isEmpty()) return false;
    setSurface(surface);
    m_session = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!m_assets->openSession(m_session, contribution)) { clear(false); return false; }
    return true;
}
bool AddonVisualizerResources::setSurface(QObject* surface)
{
    if (surface == m_surface) return ready();
    const QSignalBlocker blocker(m_assets);
    disconnect(m_surfaceDestroyed); m_surface = surface;
    const auto claim = m_claim; m_claim.clear();
    if (!claim.isEmpty()) m_assets->releaseImage(claim);
    if (surface) {
        m_surfaceDestroyed = connect(surface, &QObject::destroyed, this, &AddonVisualizerResources::close);
        if (valid()) m_claim = m_assets->claimImage(m_session, surface);
    }
    return ready();
}
void AddonVisualizerResources::close() { clear(true); }
void AddonVisualizerResources::clear(bool notify)
{
    const auto session = m_session, claim = m_claim; m_session.clear(); m_claim.clear(); m_shared->epoch.fetch_add(1, std::memory_order_acq_rel);
    disconnect(m_surfaceDestroyed); m_surface = nullptr; m_prepared.reset(); m_images.clear(); m_error.clear();
    { QMutexLocker lock(&m_shared->mutex); m_shared->prepared.reset(); }
    if (!claim.isEmpty()) m_assets->releaseImage(claim);
    if (!session.isEmpty()) m_assets->closeSession(session);
    if (notify) emit changed();
}
bool AddonVisualizerResources::loading() const
{ return !m_session.isEmpty() && m_assets->sessionLoading(m_session); }
void AddonVisualizerResources::refresh()
{
    if (m_session.isEmpty() || loading()) return;
    const auto registration = m_assets->sessionRegistration(m_session); std::shared_ptr<const AddonVisualizerActivation> prepared;
    { QMutexLocker lock(&m_shared->mutex); prepared = m_shared->prepared; }
    const auto images = m_assets->sessionPixels(m_session);
    if (!prepared || !prepared->live || !prepared->live() || registration.value("captureKey").toString() != prepared->key
        || images.size() != prepared->scene.value("assets").toObject().size()) {
        m_error = tr("The app cannot load this visualizer. The app shows the artwork."); m_prepared.reset(); m_images.clear(); emit changed(); return;
    }
    m_prepared = std::move(prepared); m_images = images;
    if (m_claim.isEmpty() && m_surface) m_claim = m_assets->claimImage(m_session, m_surface);
    if (m_surface && m_claim.isEmpty()) m_error = tr("The image limit is in use. The app shows the artwork."); else m_error.clear();
    emit changed();
}
}
