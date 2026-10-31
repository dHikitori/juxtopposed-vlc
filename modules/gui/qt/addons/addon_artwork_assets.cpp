// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_artwork_assets.hpp"
#include "addon_manager.hpp"
#include "addon_artwork_image.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include "addon_repository_cancellation.hpp"
#include <mutex>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>
#include <algorithm>

AddonArtworkAssets::Backend AddonArtworkAssets::managerBackend(vlc::addons::AddonManager* manager)
{
    QPointer<vlc::addons::AddonManager> target(manager);
    Backend backend;
    backend.prepare = [target](const QString& contribution,vlc::addons::ArtworkPreparationComplete complete) {
        return target ? target->prepareArtworkAsset(contribution,std::move(complete)) : QString{};
    };
    backend.cancelPreparation = [target](const QString& id) { if(target) target->cancel(id); };
    backend.fetchPrepared = [target](const vlc::addons::PreparedArtwork& prepared,const QJsonObject& request,
                                    vlc::addons::NetworkBroker::Callback complete) {
        return target ? target->fetchPreparedArtwork(prepared,request,std::move(complete)) : 0;
    };
    backend.work = [target](std::function<void()> work,std::function<void(bool)> complete) {
        return target ? target->queueMetadataPreparation({},std::move(work),std::move(complete)) : QString{};
    };
    backend.cancelWork = [target](const QString& id) { if(target) target->cancel(id); };
    backend.cancel = [target](quint64 request) { if(target) target->cancelAsset(request); };
    return backend;
}
struct AddonArtworkAssets::CacheResult { QUrl file; QString error; };
struct AddonArtworkAssets::Cache {
    std::mutex mutex;
    int entries = -1;
    qint64 bytes = 0;
    CacheResult run(const QString& directory,const Options& options,const QString& key,
                    const std::function<bool()>& live,const vlc::addons::NetworkBroker::Response* response) {
        std::lock_guard<std::mutex> lock(mutex);
        const auto current=[&] { return !vlc::addons::RepositoryCancellation::requested() && (!live || live()); };
        const auto canceled=[] { return CacheResult{{},tr("The artwork request is no longer current.")}; };
        if(!current()) return canceled();
        if(directory.isEmpty() || QFileInfo(directory).isSymLink())
            return {{},tr("The artwork cache directory is not correct.")};
        if(!response) {
            for(const auto& extension : {QStringLiteral("png"),QStringLiteral("jpg"),QStringLiteral("webp")}) {
                if(!current()) return canceled();
                const auto path=QDir(directory).filePath(key+QLatin1Char('.')+extension);
                QFile file(path);const QFileInfo info(file);
                if(info.isSymLink() || !info.isFile() || info.size()>options.maxImageBytes || !file.open(QIODevice::ReadOnly)) continue;
                if(imageFormat(file.read(options.maxImageBytes+1),options,nullptr)) {
                    if(!current()) return canceled();
                    return {QUrl::fromLocalFile(path),{}};
                }
            }
            if(entries<0) {
                entries=0;bytes=0;
                QDirIterator files(directory,{QStringLiteral("*.png"),QStringLiteral("*.jpg"),QStringLiteral("*.webp")},QDir::Files|QDir::NoSymLinks);
                while(files.hasNext()) {
                    if(!current()) { entries=-1;return canceled(); }
                    files.next();++entries;bytes+=files.fileInfo().size();
                    if(entries>=options.maxEntries || bytes>=options.maxCacheBytes) break;
                }
            }
            if(!current()) return canceled();
            if(entries>=options.maxEntries || bytes>=options.maxCacheBytes)
                return {{},tr("The artwork cache is full.")};
            return {};
        }
        QString extension;
        if(!response->error.isEmpty() || response->status<200 || response->status>=300 || response->truncated
            || !imageFormat(response->body,options,&extension))
            return {{},tr("The artwork response is not a complete PNG, JPEG, or WebP image within the size limit.")};
        if(!current()) return canceled();
        if(entries<0 || entries>=options.maxEntries || bytes+response->body.size()>options.maxCacheBytes)
            return {{},tr("The artwork cache is full.")};
        if(!QDir().mkpath(directory)) return {{},tr("The app cannot create the artwork cache.")};
        const auto path=QDir(directory).filePath(key+QLatin1Char('.')+extension);
        if(QFileInfo(path).isSymLink()) return {{},tr("The artwork cache file is not correct.")};
        QSaveFile file(path);
        if(!current()) return canceled();
        if(!file.open(QIODevice::WriteOnly)) return {{},tr("The app cannot save the artwork image.")};
        file.setPermissions(QFile::ReadOwner|QFile::WriteOwner);
        if(file.write(response->body)!=response->body.size() || !current()) return canceled();
        if(!file.commit()) return {{},tr("The app cannot save the artwork image.")};
        ++entries;bytes+=response->body.size();
        if(!current()) return canceled();
        return {QUrl::fromLocalFile(path),{}};
    }
};
AddonArtworkAssets::AddonArtworkAssets(vlc::addons::AddonManager* manager, const QString& directory, QObject* parent)
    : AddonArtworkAssets(managerBackend(manager), directory, Options{}, parent)
{
    if (manager) {
        connect(manager, &vlc::addons::AddonManager::revisionChanged, this, &AddonArtworkAssets::invalidate);
        // Release pending images and keys while the Manager members are live.
        connect(manager, &vlc::addons::AddonManager::retiring, this, &AddonArtworkAssets::invalidate, Qt::DirectConnection);
    }
}
AddonArtworkAssets::AddonArtworkAssets(Backend backend, const QString& directory, const Options& options, QObject* parent)
    : QObject(parent), m_backend(std::move(backend)),
      m_directory(QDir::isAbsolutePath(directory) ? QDir::cleanPath(directory) : QString{}), m_options(options), m_cache(std::make_shared<Cache>())
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
bool AddonArtworkAssets::current(const Transfer& transfer) const
{
    if(transfer.prepared) return transfer.prepared->current();
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
    QJsonObject request{{QStringLiteral("url"), url.toString(QUrl::FullyEncoded)}, {QStringLiteral("method"), QStringLiteral("GET")}};
    const auto credential = descriptor.value(QStringLiteral("credential")).toString();
    if (!credential.isEmpty()) request.insert(QStringLiteral("credential"), credential);
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_waiters.insert(id, {{}, std::move(completion), {}});
    const QPointer<AddonArtworkAssets> owner(this);
    if(m_backend.prepare) {
        const auto prepare=m_backend.prepare;
        const auto preparation=prepare(contribution,[owner,id,contribution,request](vlc::addons::PreparedArtwork value,const QString& error) {
            if(!owner || !owner->m_waiters.contains(id)) return;
            owner->m_waiters[id].preparation.clear();
            if(!error.isEmpty() || !value || !value->current()) {
                owner->failWaiter(id,error.isEmpty() ? tr("The artwork provider changed.") : error); return;
            }
            const auto registration=value->registration();
            owner->begin(id,contribution,request,registration,std::move(value));
        });
        if(!owner) return id;
        const auto waiter=m_waiters.find(id);
        if(waiter!=m_waiters.end() && waiter->key.isEmpty()) {
            waiter->preparation=preparation;
            if(preparation.isEmpty()) QTimer::singleShot(0,this,[this,id] { failWaiter(id,tr("The artwork provider could not start.")); });
        }
    } else {
        const auto registration = m_backend.registration ? m_backend.registration(contribution) : QVariantMap{};
        if(owner) begin(id,contribution,request,registration);
    }
    return id;
}
void AddonArtworkAssets::failWaiter(const QString& id,const QString& error)
{
    const auto found=m_waiters.find(id);
    if(found==m_waiters.end()) return;
    auto callback=std::move(found->completion);m_waiters.erase(found);
    if(callback) callback({},error);
}
void AddonArtworkAssets::begin(const QString& id,const QString& contribution,const QJsonObject& request,
    const QVariantMap& registration,vlc::addons::PreparedArtwork prepared)
{
    const auto waiter=m_waiters.find(id);if(waiter==m_waiters.end()) return;
    const auto partition=registration.value(QStringLiteral("cachePartition")).toString();
    if(!registration.value(QStringLiteral("enabled")).toBool() || !registration.value(QStringLiteral("configured")).toBool() || partition.isEmpty()) {
        QTimer::singleShot(0,this,[this,id] { failWaiter(id,tr("The artwork provider is not available.")); }); return;
    }
    const QUrl url(request.value(QStringLiteral("url")).toString(),QUrl::StrictMode);
    const auto identity = QJsonDocument(QJsonObject{{QStringLiteral("provider"), contribution}, {QStringLiteral("partition"), partition},
        {QStringLiteral("request"), request}}).toJson(QJsonDocument::Compact);
    const auto key = QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
    waiter->key=key;
    auto existing = m_transfers.find(key);
    if (existing != m_transfers.end()) { existing->waiters.insert(id); return; }
    Transfer transfer{contribution, partition, request, {id}, 0, std::move(prepared), {}, 0}; m_transfers.insert(key, transfer);
    QTimer::singleShot(0,this,[this,key,url] {
        if(!m_transfers.contains(key)) return;
        if(!url.isValid() || url.scheme()!=QStringLiteral("https") || url.host().isEmpty() || (!url.userInfo().isEmpty() || url.authority().contains(QLatin1Char('@')))
            || url.hasFragment() || url.toEncoded().size()>8192) {
            finish(key,{},tr("The artwork request is not correct."));return;
        }
        cacheWork(key);
    });
}
void AddonArtworkAssets::cacheWork(const QString& key,const vlc::addons::NetworkBroker::Response* response)
{
    const auto found=m_transfers.find(key);if(found==m_transfers.end()) return;
    if(!current(*found)) { finish(key,{},tr("The artwork provider or account changed."));return; }
    const auto stage=++m_workSequence;found->stage=stage;
    const auto prepared=found->prepared;
    const auto live=prepared ? std::function<bool()>([prepared] { return prepared->current(); }) : std::function<bool()>{};
    const auto cache=m_cache;const auto directory=m_directory;const auto options=m_options;
    const auto result=std::make_shared<CacheResult>();
    const auto data=response ? std::make_shared<vlc::addons::NetworkBroker::Response>(*response) : nullptr;
    const auto work=[cache,directory,options,key,live,result,data] { *result=cache->run(directory,options,key,live,data.get()); };
    const QPointer<AddonArtworkAssets> owner(this);const auto delivered=std::make_shared<bool>(false);
    const auto complete=[owner,key,stage,result,data,delivered](bool ok) {
        *delivered=true;
        if(!owner) return;
        const auto found=owner->m_transfers.find(key);
        if(found==owner->m_transfers.end() || found->stage!=stage) return;
        found->work.clear();
        if(!ok || !owner->current(*found)) { owner->finish(key,{},tr("The artwork provider or account changed."));return; }
        if(!result->file.isEmpty() || !result->error.isEmpty()) { owner->finish(key,result->file,result->error);return; }
        if(data) { owner->finish(key,{},tr("The artwork image is not available."));return; }
        owner->startFetch(key);
    };
    if(!m_backend.work) { work();complete(true);return; }
    const auto schedule=m_backend.work;
    const auto id=schedule(work,complete);
    if(!owner || *delivered) return;
    const auto pending=m_transfers.find(key);
    if(pending==m_transfers.end() || pending->stage!=stage) return;
    if(id.isEmpty()) { finish(key,{},tr("The app cannot prepare the artwork now. Try again."));return; }
    pending->work=id;
}
void AddonArtworkAssets::startFetch(const QString& key)
{
    auto found=m_transfers.find(key);if(found==m_transfers.end()) return;
    if(!current(*found)) { finish(key,{},tr("The artwork provider or account changed."));return; }
    const auto contribution=found->contribution;const auto request=found->request;
    const auto prepared=found->prepared;const auto fetch=m_backend.fetch;const auto fetchPrepared=m_backend.fetchPrepared;
    const auto cancel=m_backend.cancel;const QPointer<AddonArtworkAssets> owner(this);
    const vlc::addons::NetworkBroker::Callback complete=[owner,key](const auto& response) { if(owner) owner->complete(key,response); };
    const auto network=prepared && fetchPrepared ? fetchPrepared(prepared,request,complete) : fetch ? fetch(contribution,request,complete) : 0;
    if(!owner) { if(network && cancel) cancel(network);return; }
    found=m_transfers.find(key);
    if(found!=m_transfers.end()) {
        found->network=network;
        if(!network) finish(key,{},tr("The artwork download did not start."));
    } else if(network && cancel) cancel(network);
}
void AddonArtworkAssets::complete(const QString& key,const vlc::addons::NetworkBroker::Response& response)
{
    const auto found=m_transfers.find(key);if(found==m_transfers.end()) return;
    found->network=0;cacheWork(key,&response);
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
    const auto key = waiter->key, preparation=waiter->preparation; m_waiters.erase(waiter);
    if(!preparation.isEmpty()) {
        const auto cancel=m_backend.cancelPreparation;
        if(cancel) cancel(preparation);
        return;
    }
    auto transfer = m_transfers.find(key);
    if (transfer == m_transfers.end()) return;
    transfer->waiters.remove(id);
    if (!transfer->waiters.isEmpty()) return;
    const auto network = transfer->network; const auto work=transfer->work;m_transfers.erase(transfer);
    const auto cancel=m_backend.cancel;const auto cancelWork=m_backend.cancelWork;
    if(!work.isEmpty() && cancelWork) cancelWork(work);
    if(network && cancel) cancel(network);
}
void AddonArtworkAssets::invalidate()
{
    const auto ids = m_waiters.keys();
    QPointer<AddonArtworkAssets> guard(this);
    for (const auto& id : ids) { cancel(id); if (!guard) return; }
}
