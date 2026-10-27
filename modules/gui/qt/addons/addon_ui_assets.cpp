// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_ui_assets.hpp"
#include "addon_manager.hpp"
#include "addon_artwork_image.hpp"
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonDocument>
#include <QMutex>
#include <QMutexLocker>
#include <QRunnable>
#include <QThreadPool>
#include <QRegularExpression>
#include <QUuid>
#include <algorithm>
#include <cstring>

namespace vlc::addons {
struct AddonUiAssets::SharedImages {
    struct Ticket { QString session, cacheKey; quint64 epoch; QImage pixels; std::function<bool()> live; };
    struct Claim { QString session; quint64 epoch; };
    QMutex mutex;
    QPointer<AddonUiAssets> owner;
    QHash<QString, Ticket> tickets;
    QHash<QString, Claim> claims;
    std::function<void()> wake;
};
namespace {
struct DecodeBudget {
    QMutex mutex;
    int jobs = 0, claims = 0, captures = 0;
    qint64 bytes = 0;
    QList<std::weak_ptr<AddonUiAssets::SharedImages>> services;
};
std::shared_ptr<DecodeBudget> decodeBudget()
{ static const auto value = std::make_shared<DecodeBudget>(); return value; }
void wakeDecoders(const std::shared_ptr<DecodeBudget>& budget)
{
    QList<std::shared_ptr<AddonUiAssets::SharedImages>> services;
    {
        QMutexLocker lock(&budget->mutex);
        for (auto it = budget->services.begin(); it != budget->services.end();) {
            if (auto service = it->lock()) { services.append(std::move(service)); ++it; }
            else it = budget->services.erase(it);
        }
    }
    for (const auto& service : services) service->wake();
}
struct BytesLease {
    std::shared_ptr<DecodeBudget> budget;
    qint64 bytes;
    ~BytesLease() {
        { QMutexLocker lock(&budget->mutex); budget->bytes -= bytes; }
        wakeDecoders(budget);
    }
};
struct JobLease {
    std::shared_ptr<DecodeBudget> budget;
    ~JobLease() {
        { QMutexLocker lock(&budget->mutex); --budget->jobs; }
        wakeDecoders(budget);
    }
};
struct CaptureLease {
    std::shared_ptr<DecodeBudget> budget;
    ~CaptureLease() {
        { QMutexLocker lock(&budget->mutex); --budget->captures; }
        wakeDecoders(budget);
    }
};
struct ImageLease {
    std::shared_ptr<DecodeBudget> budget;
    ~ImageLease() {
        { QMutexLocker lock(&budget->mutex); --budget->claims; }
        wakeDecoders(budget);
    }
};
QString nonce() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool identifier(const QString& value)
{
    static const QRegularExpression expression(QStringLiteral("^[A-Za-z_][A-Za-z0-9_.-]{0,95}$"));
    return expression.match(value).hasMatch() && value != QLatin1String("__proto__")
        && value != QLatin1String("prototype") && value != QLatin1String("constructor");
}
QImage fail(QString* error, const char* message) { if (error) *error = QString::fromLatin1(message); return {}; }

}
AddonUiAssets::Backend AddonUiAssets::managerBackend(AddonManager* manager)
{
    const QPointer<AddonManager> target(manager);
    return {[target](const QString& id) { return target ? target->uiAssetReader(id) : UiAssetReader{}; }};
}
AddonUiAssets::AddonUiAssets(AddonManager* manager, QObject* parent)
    : AddonUiAssets(managerBackend(manager), Options{}, parent)
{
    if (manager) {
        connect(manager, &AddonManager::revisionChanged, this, &AddonUiAssets::invalidate);
        connect(manager, &AddonManager::packageWithdrawn, this, &AddonUiAssets::invalidatePackage);
        connect(manager, &QObject::destroyed, this, &AddonUiAssets::invalidate);
    }
}
AddonUiAssets::AddonUiAssets(Backend backend, const Options& options, QObject* parent)
    : QObject(parent), m_backend(std::move(backend)), m_options(options), m_images(std::make_shared<SharedImages>())
{
    m_options.maximumImageBytes = std::clamp<qint64>(options.maximumImageBytes, 1, 1024 * 1024);
    m_options.maximumQueuedBytes = std::clamp<qint64>(options.maximumQueuedBytes, 1, 8 * 1024 * 1024);
    m_options.maximumCacheBytes = std::clamp<qint64>(options.maximumCacheBytes, 1, 32 * 1024 * 1024);
    m_options.maximumDimension = std::clamp(options.maximumDimension, 1, 1024);
    m_options.maximumOutputDimension = std::clamp(options.maximumOutputDimension, 1, 1024);
    m_options.maximumJobs = std::clamp(options.maximumJobs, 1, 2);
    m_options.maximumAssets = std::clamp(options.maximumAssets, 1, 64);
    m_options.maximumTickets = std::clamp(options.maximumTickets, 1, 256);
    m_options.maximumSessionImages = std::clamp(options.maximumSessionImages, 1, 32);
    m_options.maximumImages = std::clamp(options.maximumImages, 1, 64);
    m_images->owner = this;
    m_authorityTimer.setInterval(100);
    connect(&m_authorityTimer, &QTimer::timeout, this, &AddonUiAssets::expireScopes);
    const std::weak_ptr<SharedImages> weak = m_images;
    m_images->wake = [weak] {
        const auto shared = weak.lock(); if (!shared) return;
        QMutexLocker lock(&shared->mutex); const auto owner = shared->owner;
        if (owner) QMetaObject::invokeMethod(owner, [owner] {
            if (!owner) return;
            owner->pump();
            if (owner) emit owner->imageBudgetChanged();
        }, Qt::QueuedConnection);
    };
    const auto budget = decodeBudget(); QMutexLocker lock(&budget->mutex); budget->services.append(m_images);
}
AddonUiAssets::~AddonUiAssets()
{ shutdown(false); }
void AddonUiAssets::close() { shutdown(true); }
void AddonUiAssets::shutdown(bool notify)
{
    if (m_closing) return;
    m_closing = true;
    m_authorityTimer.stop();
    { QMutexLocker lock(&m_images->mutex); m_images->owner = nullptr; m_images->tickets.clear(); m_images->claims.clear(); }
    for (const auto& claim : m_claims) disconnect(claim.destroyed);
    const auto sessions = m_scopes.keys();
    m_claims.clear(); m_scopes.clear(); m_queue.clear(); m_jobs.clear(); m_captureQueue.clear(); m_captureJobs.clear(); m_cache.clear(); m_queuedBytes = 0; m_cacheBytes = 0;
    if (notify) {
        QPointer<AddonUiAssets> guard(this);
        for (const auto& session : sessions) { emit sessionAssetsChanged(session); if (!guard) return; }
        emit imageBudgetChanged();
    }
}
QString AddonUiAssets::stamp(const QVariantMap& registration)
{
    if (!registration.value("enabled").toBool() || !registration.value("configured").toBool()
        || registration.value("accountReference").toString().isEmpty() || registration.value("assetStamp").toString().isEmpty()) return {};
    return registration.value("assetStamp").toString();
}
QImage AddonUiAssets::decodeImage(const QByteArray& bytes, const QString& suffix, const Options& options, QString* error)
{
    if (error) error->clear();
    if (bytes.isEmpty() || bytes.size() > std::clamp<qint64>(options.maximumImageBytes, 1, 1024 * 1024))
        return fail(error, "The image exceeds the byte limit or is empty.");
    const auto format = suffix.toLower() == QLatin1String("jpg") ? QByteArray("jpeg") : suffix.toLower().toLatin1();
    if (AddonArtworkImage::format(bytes) != format) return fail(error, "The image format is not permitted by the static image profile.");
    const int limit = std::clamp(options.maximumDimension, 1, 1024);
    auto image = AddonArtworkImage::decode(bytes, AddonArtworkImage::Profile::StaticAddon,
        {options.maximumImageBytes, qint64(limit) * limit * 8, limit}, error);
    if (image.isNull()) return {};
    const int output = std::clamp(options.maximumOutputDimension, 1, 1024);
    if (image.width() > output || image.height() > output) image = image.scaled(output, output, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QImage pixels(image.size(), QImage::Format_ARGB32_Premultiplied);
    if (pixels.isNull()) return fail(error, "The app cannot allocate the image pixels.");
    for (int row = 0; row < image.height(); ++row)
        std::memcpy(pixels.scanLine(row), image.constScanLine(row), size_t(image.width()) * 4);
    return pixels;
}
bool AddonUiAssets::openSession(const QString& session, const QString& contribution)
{
    if (m_closing) return false;
    QPointer<AddonUiAssets> guard(this);
    closeSession(session, false); if (!guard || session.isEmpty() || session.size() > 128 || m_scopes.size() >= 32) return false;
    const auto epoch = ++m_epoch;
    const auto prepare = m_backend.prepare;
    const auto read = prepare ? prepare(contribution) : UiAssetReader{};
    if (!guard || !read || m_epoch != epoch) return false;
    m_scopes.insert(session, {contribution, contribution.section('/', 0, 0), {}, epoch, false, {}, {}, {}, {}});
    m_authorityTimer.start();
    m_captureQueue.append({nonce(), session, epoch, read}); m_error.clear();
    pump(); return true;
}
void AddonUiAssets::captured(const QString& id, UiAssetCapture capture)
{
    const auto active = m_captureJobs.find(id); if (active == m_captureJobs.end()) return;
    const auto work = *active; m_captureJobs.erase(active);
    auto found = m_scopes.find(work.session);
    if (m_closing || found == m_scopes.end() || found->epoch != work.epoch || found->suspended) { pump(); return; }
    const auto registration = capture.registration;
    const auto scopeStamp = stamp(registration);
    const auto declarations = registration.value("assets").toMap();
    const auto reject = [this, &work, &capture] {
        m_error = capture.error; closeSession(work.session);
    };
    if (!capture.error.isEmpty() || scopeStamp.isEmpty() || registration.value("assets").metaType().id() != QMetaType::QVariantMap
        || declarations.size() > m_options.maximumAssets || !capture.current || !capture.live || !capture.live()
        || capture.assets.size() != declarations.size()) { reject(); return; }
    const auto& bytes = capture.assets;
    qint64 total = 0;
    for (auto it = declarations.begin(); it != declarations.end(); ++it) {
        const auto entry = it.value().toMap(); const auto path = entry.value("path").toString();
        const auto suffix = QFileInfo(path).suffix().toLower();
        if (!identifier(it.key()) || entry.size() != 1 || !portablePackagePath(path)
            || !QStringList{"png", "jpg", "jpeg", "webp"}.contains(suffix) || !bytes.contains(it.key())
            || bytes.value(it.key()).isEmpty() || bytes.value(it.key()).size() > m_options.maximumImageBytes) { reject(); return; }
        total += bytes.value(it.key()).size();
    }
    bool ticketsFull;
    { QMutexLocker lock(&m_images->mutex);
      ticketsFull = m_images->tickets.size() + m_queue.size() + m_jobs.size() + declarations.size() > m_options.maximumTickets; }
    if (ticketsFull) { reject(); return; }
    if (total > m_options.maximumQueuedBytes - m_queuedBytes) { reject(); return; }
    const auto budget = decodeBudget();
    bool bytesAvailable;
    {
        QMutexLocker lock(&budget->mutex);
        bytesAvailable = total <= 8 * 1024 * 1024 - budget->bytes;
        if (bytesAvailable) budget->bytes += total;
    }
    if (!bytesAvailable) { reject(); return; }
    found->package = registration.value("packageId").toString(); found->stamp = scopeStamp; found->registration = registration; found->live = capture.live;
    const auto session = work.session; const auto epoch = work.epoch; m_error.clear();
    for (auto it = declarations.begin(); it != declarations.end(); ++it) {
        const auto suffix = QFileInfo(it.value().toMap().value("path").toString()).suffix().toLower();
        const auto encoded = bytes.value(it.key());
        auto bytesLease = std::make_shared<BytesLease>(); bytesLease->budget = budget; bytesLease->bytes = encoded.size();
        const auto key = capture.cacheKeys.value(it.key());
        if (m_cache.contains(key)) publish(session, it.key(), key);
        else { m_queue.append({nonce(), session, it.key(), suffix, key, epoch, encoded, std::move(bytesLease), capture.current, capture.live}); m_queuedBytes += encoded.size(); }
    }
    pump(); emit sessionAssetsChanged(session);
}
void AddonUiAssets::pump()
{
    if (!m_closing && m_captureJobs.isEmpty() && !m_captureQueue.isEmpty()) {
        const auto budget = decodeBudget(); bool admitted = false;
        { QMutexLocker lock(&budget->mutex); if (budget->captures < 1) { ++budget->captures; admitted = true; } }
        if (admitted) {
            auto lease = std::make_shared<CaptureLease>(); lease->budget = budget;
            const auto work = m_captureQueue.takeFirst(); m_captureJobs.insert(work.id, work);
            const auto shared = m_images;
            QThreadPool::globalInstance()->start(QRunnable::create([shared, work, lease = std::move(lease)]() mutable {
                auto capture = work.read();
                if (capture.current && !capture.current()) { capture.registration.clear(); capture.assets.clear(); }
                const auto declarations = capture.registration.value("assets").toMap();
                for (auto it = capture.assets.cbegin(); it != capture.assets.cend(); ++it) {
                    const auto suffix = QFileInfo(declarations.value(it.key()).toMap().value("path").toString()).suffix().toLower();
                    QCryptographicHash hash(QCryptographicHash::Sha256); hash.addData(it.value()); hash.addData(QByteArray(1, '\0')); hash.addData(suffix.toUtf8());
                    capture.cacheKeys.insert(it.key(), QString::fromLatin1(hash.result().toHex()));
                }
                QMutexLocker lock(&shared->mutex); const auto target = shared->owner;
                if (target) QMetaObject::invokeMethod(target, [target, id = work.id, capture = std::move(capture), lease = std::move(lease)]() mutable {
                    if (target) target->captured(id, std::move(capture));
                }, Qt::QueuedConnection);
            }));
        }
    }

    while (!m_closing && m_jobs.size() < m_options.maximumJobs && !m_queue.isEmpty()) {
        const auto budget = decodeBudget();
        {
            QMutexLocker lock(&budget->mutex); if (budget->jobs >= 2) return; ++budget->jobs;
        }
        auto jobLease = std::make_shared<JobLease>(); jobLease->budget = budget;
        auto work = m_queue.takeFirst(); m_jobs.insert(work.id, work);
        const auto shared = m_images; const auto options = m_options;
        const auto decode = m_backend.decode;
        QThreadPool::globalInstance()->start(QRunnable::create([shared, options, work, jobLease = std::move(jobLease), decode]() mutable {
            QString error; QImage pixels;
            if (work.current && work.current()) {
                pixels = decode ? decode(work.bytes, work.suffix, options, &error) : decodeImage(work.bytes, work.suffix, options, &error);
                if (!work.current()) pixels = {};
            }
            work.bytes.clear(); work.bytesLease.reset();
            QMutexLocker lock(&shared->mutex);
            if (!shared->owner) return;
            const auto target = shared->owner;
            QMetaObject::invokeMethod(target, [target, id = work.id, pixels = std::move(pixels), error, jobLease = std::move(jobLease)]() mutable {
                if (target) target->decoded(id, std::move(pixels), error);
            }, Qt::QueuedConnection);
        }));
    }
}
bool AddonUiAssets::makeRoom(qint64 bytes)
{
    if (bytes > m_options.maximumCacheBytes) return false;
    while (m_cacheBytes + bytes > m_options.maximumCacheBytes) {
        auto oldest = m_cache.end();
        for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
            if (!it->pins && (oldest == m_cache.end() || it->use < oldest->use)) oldest = it;
        if (oldest == m_cache.end()) return false;
        m_cacheBytes -= oldest->bytes; m_cache.erase(oldest);
    }
    return true;
}
bool AddonUiAssets::publish(const QString& session, const QString& alias, const QString& cacheKey)
{
    auto scope = m_scopes.find(session); auto cached = m_cache.find(cacheKey);
    if (scope == m_scopes.end() || scope->suspended || !scope->live || !scope->live() || cached == m_cache.end()) return false;
    const auto ticket = nonce(); ++cached->pins; cached->use = ++m_use;
    const QString url = QStringLiteral("image://addonassets/") + ticket;
    scope->tickets.insert(alias, ticket); scope->urls.insert(QStringLiteral("asset:") + alias, url);
    QMutexLocker lock(&m_images->mutex);
    m_images->tickets.insert(ticket, {session, cacheKey, scope->epoch, cached->image, scope->live}); return true;
}
void AddonUiAssets::decoded(const QString& id, QImage image, const QString& error)
{
    const auto found = m_jobs.find(id); if (found == m_jobs.end()) return;
    const auto work = *found; m_jobs.erase(found); m_queuedBytes -= work.bytes.size();
    if (!m_scopes.contains(work.session) || m_scopes.value(work.session).epoch != work.epoch || m_scopes.value(work.session).suspended) {
        pump(); return;
    }
    const auto scope = m_scopes.constFind(work.session);
    if (scope != m_scopes.cend() && !scope->suspended && scope->epoch == work.epoch && work.live && work.live()
        && !image.isNull() && error.isEmpty()) {
        if (!m_cache.contains(work.cacheKey) && makeRoom(image.sizeInBytes())) {
            m_cache.insert(work.cacheKey, {image, image.sizeInBytes(), 0, ++m_use}); m_cacheBytes += image.sizeInBytes();
        }
        publish(work.session, work.alias, work.cacheKey);
    }
    pump(); emit sessionAssetsChanged(work.session);
}
QVariantMap AddonUiAssets::sessionAssets(const QString& session) const
{
    const auto scope = m_scopes.constFind(session);
    return scope == m_scopes.cend() || scope->suspended || !scope->live || !scope->live() ? QVariantMap{} : scope->urls;
}
QVariantMap AddonUiAssets::sessionRegistration(const QString& session) const
{
    const auto scope = m_scopes.constFind(session);
    return scope == m_scopes.cend() || scope->suspended || !scope->live || !scope->live() ? QVariantMap{} : scope->registration;
}
QMap<QString, QImage> AddonUiAssets::sessionPixels(const QString& session) const
{
    const auto scope = m_scopes.constFind(session); QMap<QString, QImage> result;
    if (scope == m_scopes.cend() || scope->suspended || !scope->live || !scope->live()) return result;
    QMutexLocker lock(&m_images->mutex);
    for (auto it = scope->tickets.cbegin(); it != scope->tickets.cend(); ++it) {
        const auto ticket = m_images->tickets.constFind(it.value());
        if (ticket != m_images->tickets.cend() && ticket->epoch == scope->epoch && ticket->session == session)
            result.insert(it.key(), ticket->pixels);
    }
    return result;
}
bool AddonUiAssets::sessionLoading(const QString& session) const
{
    const auto scope = m_scopes.constFind(session);
    if (scope == m_scopes.cend() || scope->suspended) return false;
    const auto pending = [&session, epoch = scope->epoch](const auto& work) { return work.session == session && work.epoch == epoch; };
    return std::any_of(m_captureQueue.cbegin(), m_captureQueue.cend(), pending)
        || std::any_of(m_captureJobs.cbegin(), m_captureJobs.cend(), pending)
        || std::any_of(m_queue.cbegin(), m_queue.cend(), pending) || std::any_of(m_jobs.cbegin(), m_jobs.cend(), pending);
}
void AddonUiAssets::removeTicket(const QString& ticket)
{
    QMutexLocker lock(&m_images->mutex); const auto old = m_images->tickets.take(ticket);
    auto cache = m_cache.find(old.cacheKey); if (cache != m_cache.end() && cache->pins > 0) --cache->pins;
}
void AddonUiAssets::closeSession(const QString& session, bool notify)
{
    const auto found = m_scopes.find(session); if (found == m_scopes.end()) return;
    const auto scope = *found; m_scopes.erase(found);
    if (m_scopes.isEmpty()) m_authorityTimer.stop();
    for (const auto& ticket : scope.tickets) removeTicket(ticket);
    for (auto it = m_queue.begin(); it != m_queue.end();) {
        if (it->session == session) { m_queuedBytes -= it->bytes.size(); it = m_queue.erase(it); } else ++it;
    }
    for (auto it = m_captureQueue.begin(); it != m_captureQueue.end();) {
        if (it->session == session) it = m_captureQueue.erase(it); else ++it;
    }
    const auto claims = m_claims.keys();
    for (const auto& id : claims) if (m_claims.value(id).session == session) {
        disconnect(m_claims.take(id).destroyed); QMutexLocker lock(&m_images->mutex); m_images->claims.remove(id);
    }
    pump(); if (!m_closing && notify) {
        QPointer<AddonUiAssets> guard(this); emit sessionAssetsChanged(session);
        if (guard) emit imageBudgetChanged();
    }
}
void AddonUiAssets::suspendSession(const QString& session, bool suspended)
{
    const auto found = m_scopes.find(session); if (found == m_scopes.end() || found->suspended == suspended) return;
    const auto contribution = found->contribution;
    if (!suspended) { openSession(session, contribution); return; }
    QPointer<AddonUiAssets> guard(this); closeSession(session); if (!guard) return;
    m_scopes.insert(session, {contribution, {}, {}, ++m_epoch, true, {}, {}, {}, {}});
}
QString AddonUiAssets::claimImage(const QString& session, QObject* owner)
{
    if (!owner || !m_scopes.contains(session) || m_scopes.value(session).suspended) return {};
    int count = 0;
    for (auto it = m_claims.cbegin(); it != m_claims.cend(); ++it) {
        if (it->owner == owner) return it->session == session ? it.key() : QString{};
        count += it->session == session;
    }
    if (count >= m_options.maximumSessionImages || m_claims.size() >= m_options.maximumImages) return {};
    const auto budget = decodeBudget();
    { QMutexLocker lock(&budget->mutex); if (budget->claims >= 64) return {}; ++budget->claims; }
    auto lease = std::make_shared<ImageLease>(); lease->budget = budget;
    const auto id = nonce();
    const auto connection = connect(owner, &QObject::destroyed, this, [this, id] { releaseImage(id); });
    m_claims.insert(id, {session, owner, connection, std::move(lease)});
    QMutexLocker lock(&m_images->mutex); m_images->claims.insert(id, {session, m_scopes.value(session).epoch}); return id;
}
void AddonUiAssets::releaseImage(const QString& claim)
{
    const auto found = m_claims.find(claim); if (found == m_claims.end()) return;
    disconnect(found->destroyed); m_claims.erase(found);
    { QMutexLocker lock(&m_images->mutex); m_images->claims.remove(claim); }
    if (!m_closing) emit imageBudgetChanged();
}
QString AddonUiAssets::assetUrlForClaim(const QString& session, const QString& alias, const QString& claim) const
{
    const auto scope = m_scopes.constFind(session); const auto selected = m_claims.constFind(claim);
    if (scope == m_scopes.cend() || scope->suspended || !scope->live || !scope->live()
        || selected == m_claims.cend() || selected->session != session || !selected->owner) return {};
    const auto base = scope->urls.value(alias).toString();
    return base.isEmpty() ? QString{} : base + '/' + claim;
}
QImage AddonUiAssets::image(const std::shared_ptr<SharedImages>& shared, const QString& id, QSize* size)
{
    if (size) *size = {};
    const auto parts = id.split('/'); if (!shared || parts.size() != 2) return {};
    QMutexLocker lock(&shared->mutex);
    const auto ticket = shared->tickets.constFind(parts[0]); const auto claim = shared->claims.constFind(parts[1]);
    if (ticket == shared->tickets.cend() || claim == shared->claims.cend()
        || ticket->session != claim->session || ticket->epoch != claim->epoch || !ticket->live || !ticket->live()) return {};
    if (size) *size = ticket->pixels.size();
    return ticket->pixels;
}
void AddonUiAssets::invalidatePackage(const QString& package)
{
    const auto sessions = m_scopes.keys(); QPointer<AddonUiAssets> guard(this);
    for (const auto& session : sessions) if (m_scopes.value(session).package == package) { closeSession(session); if (!guard) return; }
}
void AddonUiAssets::invalidate()
{
    const auto sessions = m_scopes.keys(); QPointer<AddonUiAssets> guard(this);
    for (const auto& session : sessions) { closeSession(session); if (!guard) return; }
}
void AddonUiAssets::expireScopes()
{
    const auto sessions = m_scopes.keys(); QPointer<AddonUiAssets> guard(this);
    for (const auto& session : sessions) {
        const auto scope = m_scopes.constFind(session);
        if (scope != m_scopes.cend() && scope->live && !scope->live()) {
            closeSession(session); if (!guard) return;
        }
    }
}
QVariantMap AddonUiAssets::statistics() const
{
    const auto budget = decodeBudget(); QMutexLocker lock(&budget->mutex);
    return {{"queuedBytes", m_queuedBytes}, {"cacheBytes", m_cacheBytes}, {"jobs", m_jobs.size() + m_captureJobs.size()},
        {"queuedJobs", m_queue.size() + m_captureQueue.size()}, {"claims", m_claims.size()},
        {"globalJobs", budget->jobs}, {"globalIntegrityCaptures", budget->captures}, {"globalClaims", budget->claims}, {"globalEncodedBytes", budget->bytes},
        {"globalDecodePixelReservation", qint64(budget->jobs) * 1024 * 1024 * 16},
        {"decodePixelReservation", qint64(m_jobs.size()) * m_options.maximumDimension * m_options.maximumDimension * 16}};
}
}
