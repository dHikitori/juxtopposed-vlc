// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_artwork_assets.hpp"
#include "addon_manager.hpp"
#include "addon_artwork_image.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>
#include <algorithm>

AddonArtworkAssets::Backend AddonArtworkAssets::managerBackend(vlc::addons::AddonManager* manager)
{
    QPointer<vlc::addons::AddonManager> target(manager);
    return {
        [target](const QString& contribution) {
            if (target) for (const auto& value : target->providersForFeature(QStringLiteral("artwork")))
                if (value.toMap().value(QStringLiteral("id")) == contribution) return value.toMap();
            return QVariantMap{};
        },
        [target](const QString& contribution, const QJsonObject& request, vlc::addons::NetworkBroker::Callback callback) {
            return target ? target->fetchImageAsset(contribution, request, std::move(callback)) : 0;
        },
        [target](quint64 request) { if (target) target->cancelAsset(request); }};
}
AddonArtworkAssets::AddonArtworkAssets(vlc::addons::AddonManager* manager, const QString& directory, QObject* parent)
    : AddonArtworkAssets(managerBackend(manager), directory, Options{}, parent)
{
    if (manager) connect(manager, &vlc::addons::AddonManager::revisionChanged, this, &AddonArtworkAssets::invalidate);
}
AddonArtworkAssets::AddonArtworkAssets(Backend backend, const QString& directory, const Options& options, QObject* parent)
    : QObject(parent), m_backend(std::move(backend)),
      m_directory(QFileInfo(directory).isAbsolute() ? QDir::cleanPath(directory) : QString{}), m_options(options)
{
    m_options.maxEntries = std::clamp(options.maxEntries, 1, 8192);
    m_options.maxCacheBytes = std::clamp<qint64>(options.maxCacheBytes, 1, 512 * 1024 * 1024);
    m_options.maxImageBytes = std::clamp<qint64>(options.maxImageBytes, 1, 1024 * 1024);
    m_options.maxDimension = std::clamp(options.maxDimension, 1, 1024);
}
AddonArtworkAssets::~AddonArtworkAssets() { invalidate(); }
bool AddonArtworkAssets::imageFormat(const QByteArray& bytes, const Options& options, QString* extension)
{
    if (bytes.isEmpty() || bytes.size() > options.maxImageBytes) return false;
    const vlc::addons::AddonArtworkImage::Limits limits{options.maxImageBytes,
        qint64(std::clamp(options.maxDimension, 1, 1024)) * std::clamp(options.maxDimension, 1, 1024) * 8, options.maxDimension};
    if (vlc::addons::AddonArtworkImage::decode(bytes, vlc::addons::AddonArtworkImage::Profile::StaticAddon, limits).isNull()) return false;
    const auto format = vlc::addons::AddonArtworkImage::format(bytes);
    if (extension) *extension = format == "jpeg" ? QStringLiteral("jpg") : QString::fromLatin1(format);
    return true;
}
void AddonArtworkAssets::measureCache()
{
    m_cacheEntries = 0; m_cacheBytes = 0;
    if (m_directory.isEmpty() || QFileInfo(m_directory).isSymLink()) return;
    const auto files = QDir(m_directory).entryInfoList({QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.webp")}, QDir::Files | QDir::NoSymLinks);
    for (const auto& file : files) { ++m_cacheEntries; m_cacheBytes += file.size(); }
}
QUrl AddonArtworkAssets::cached(const QString& key) const
{
    for (const auto& extension : {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("webp")}) {
        const auto path = QDir(m_directory).filePath(key + QLatin1Char('.') + extension);
        QFile file(path); const QFileInfo info(file);
        if (info.isSymLink() || !info.isFile() || info.size() > m_options.maxImageBytes || !file.open(QIODevice::ReadOnly)) continue;
        if (imageFormat(file.read(m_options.maxImageBytes + 1), m_options, nullptr)) return QUrl::fromLocalFile(path);
    }
    return {};
}
bool AddonArtworkAssets::current(const Transfer& transfer) const
{
    const auto registration = m_backend.registration ? m_backend.registration(transfer.contribution) : QVariantMap{};
    return registration.value(QStringLiteral("enabled")).toBool() && registration.value(QStringLiteral("configured")).toBool()
        && !transfer.partition.isEmpty() && registration.value(QStringLiteral("cachePartition")).toString() == transfer.partition;
}
QString AddonArtworkAssets::resolve(const QString& contribution, const QVariant& claim, Completion completion)
{
    if (!completion || m_waiters.size() >= 64) return {};
    auto descriptor = claim.toMap();
    if (claim.metaType().id() == QMetaType::QString) descriptor.insert(QStringLiteral("url"), claim.toString());
    const QUrl url(descriptor.value(QStringLiteral("url")).toString(), QUrl::StrictMode);
    const auto registration = m_backend.registration ? m_backend.registration(contribution) : QVariantMap{};
    const auto partition = registration.value(QStringLiteral("cachePartition")).toString();
    QJsonObject request{{QStringLiteral("url"), url.toString(QUrl::FullyEncoded)}, {QStringLiteral("method"), QStringLiteral("GET")}};
    const auto credential = descriptor.value(QStringLiteral("credential")).toString();
    if (!credential.isEmpty()) request.insert(QStringLiteral("credential"), credential);
    const auto identity = QJsonDocument(QJsonObject{{QStringLiteral("provider"), contribution}, {QStringLiteral("partition"), partition},
        {QStringLiteral("request"), request}}).toJson(QJsonDocument::Compact);
    const auto key = QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_waiters.insert(id, {key, std::move(completion)});
    auto existing = m_transfers.find(key);
    if (existing != m_transfers.end()) { existing->waiters.insert(id); return id; }
    Transfer transfer{contribution, partition, request, {id}, 0}; m_transfers.insert(key, transfer);
    QTimer::singleShot(0, this, [this, key, url] {
        auto found = m_transfers.find(key);
        if (found == m_transfers.end()) return;
        if (!current(*found)) { finish(key, {}, tr("The artwork provider or account changed.")); return; }
        if (!url.isValid() || url.scheme() != QStringLiteral("https") || url.host().isEmpty() || !url.userInfo().isEmpty()
            || url.hasFragment() || url.toEncoded().size() > 8192 || m_directory.isEmpty() || QFileInfo(m_directory).isSymLink()) {
            finish(key, {}, tr("The artwork request or cache directory is not correct.")); return;
        }
        const auto local = cached(key);
        if (!local.isEmpty()) { finish(key, local, {}); return; }
        if (m_cacheEntries < 0) measureCache();
        if (m_cacheEntries >= m_options.maxEntries || m_cacheBytes >= m_options.maxCacheBytes || !m_backend.fetch) {
            finish(key, {}, tr("The artwork cache limit has been reached.")); return;
        }
        const auto contribution = found->contribution; const auto request = found->request;
        QPointer<AddonArtworkAssets> owner(this);
        const auto fetch = m_backend.fetch; const auto cancel = m_backend.cancel;
        const auto network = fetch(contribution, request, [owner, key](const vlc::addons::NetworkBroker::Response& response) {
            if (owner) owner->complete(key, response);
        });
        if (!owner) { if (network && cancel) cancel(network); return; }
        found = m_transfers.find(key);
        if (found != m_transfers.end()) {
            found->network = network;
            if (!network) finish(key, {}, tr("The artwork download did not start."));
        } else if (network && cancel) cancel(network);
    });
    return id;
}
void AddonArtworkAssets::complete(const QString& key, const vlc::addons::NetworkBroker::Response& response)
{
    const auto found = m_transfers.constFind(key);
    if (found == m_transfers.cend()) return;
    if (!current(*found)) { finish(key, {}, tr("The artwork provider or account changed.")); return; }
    QString extension;
    if (!response.error.isEmpty() || response.status < 200 || response.status >= 300 || response.truncated
        || !imageFormat(response.body, m_options, &extension)) {
        finish(key, {}, tr("The artwork response is not a complete PNG, JPEG, or WebP image within the size limit.")); return;
    }
    if (m_cacheEntries >= m_options.maxEntries || m_cacheBytes + response.body.size() > m_options.maxCacheBytes) {
        finish(key, {}, tr("The artwork cache limit has been reached.")); return;
    }
    if (!QDir().mkpath(m_directory)) { finish(key, {}, tr("The app cannot create the artwork cache.")); return; }
    const auto path = QDir(m_directory).filePath(key + QLatin1Char('.') + extension);
    if (QFileInfo(path).isSymLink()) { finish(key, {}, tr("The artwork cache file is not correct.")); return; }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) { finish(key, {}, tr("The app cannot save the artwork image.")); return; }
    file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    if (file.write(response.body) != response.body.size() || !file.commit()) { finish(key, {}, tr("The app cannot save the artwork image.")); return; }
    ++m_cacheEntries; m_cacheBytes += response.body.size();
    finish(key, QUrl::fromLocalFile(path), {});
}
void AddonArtworkAssets::finish(const QString& key, const QUrl& file, const QString& error)
{
    const auto transfer = m_transfers.take(key);
    QList<Completion> callbacks;
    for (const auto& id : transfer.waiters) {
        const auto waiter = m_waiters.find(id);
        if (waiter != m_waiters.end()) { callbacks.append(std::move(waiter->completion)); m_waiters.erase(waiter); }
    }
    // A callback can destroy this object. Do not use its members below.
    for (const auto& callback : callbacks) callback(file, error);
}
void AddonArtworkAssets::cancel(const QString& id)
{
    const auto waiter = m_waiters.find(id);
    if (waiter == m_waiters.end()) return;
    const auto key = waiter->key; m_waiters.erase(waiter);
    auto transfer = m_transfers.find(key);
    if (transfer == m_transfers.end()) return;
    transfer->waiters.remove(id);
    if (!transfer->waiters.isEmpty()) return;
    const auto network = transfer->network; m_transfers.erase(transfer);
    const auto cancel = m_backend.cancel;
    if (network && cancel) cancel(network);
}
void AddonArtworkAssets::invalidate()
{
    const auto ids = m_waiters.keys();
    QPointer<AddonArtworkAssets> guard(this);
    for (const auto& id : ids) { cancel(id); if (!guard) return; }
}
