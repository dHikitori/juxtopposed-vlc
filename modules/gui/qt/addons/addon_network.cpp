// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_network.hpp"
#include <QHostInfo>
#include <QJsonArray>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>
#include <QRegularExpression>
#include <atomic>
#include <chrono>
#include <cmath>

namespace vlc::addons {
namespace {
qint64 monotonicMs()
{ return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
OriginRequestPacer& appPacer()
{ static OriginRequestPacer pacer; return pacer; }
std::atomic<quint64> paceSequence{0};
QString originFor(const QUrl& url)
{
    QUrl origin; origin.setScheme(QStringLiteral("https")); origin.setHost(url.host().toLower());
    if (url.port(443) != 443) origin.setPort(url.port());
    return exactHttpsOrigin(origin.toString());
}
int minimumInterval(const QJsonObject& scope)
{
    const auto value = scope.value(QStringLiteral("minimumIntervalMs"));
    if (value.isUndefined()) return 0;
    const auto interval = value.toDouble(-1);
    return value.isDouble() && std::isfinite(interval) && interval >= 0 && interval <= 60000
        && std::floor(interval) == interval ? int(interval) : -1;
}
}
bool OriginRequestPacer::enqueue(quint64 ticket, const QString& address, int interval, qint64 now)
{
    const auto origin = exactHttpsOrigin(address);
    if (!ticket || origin.isEmpty() || interval < 0 || interval > 60000 || now < 0) return false;
    std::lock_guard<std::mutex> guard(m_lock);
    if (m_tickets.contains(ticket) || m_tickets.size() >= 4096) return false;
    for (auto it = m_origins.begin(); it != m_origins.end();)
        if (it->active.isEmpty() && now >= it->lastStart + it->lastInterval) it = m_origins.erase(it); else ++it;
    if (!m_origins.contains(origin) && m_origins.size() >= 1024) return false;
    auto& state = m_origins[origin];
    int effective = interval;
    for (const auto minimum : state.active) effective = std::max(effective, minimum);
    if (now < state.lastStart + state.lastInterval) effective = std::max(effective, state.lastInterval);
    // Each queued request retains the strongest interval in its active group.
    for (auto it = state.active.begin(); it != state.active.end(); ++it) it.value() = std::max(it.value(), effective);
    state.active.insert(ticket, effective); m_tickets.insert(ticket, origin);
    return true;
}
OriginRequestPacer::Admission OriginRequestPacer::admit(quint64 ticket, qint64 now)
{
    std::lock_guard<std::mutex> guard(m_lock);
    const auto found = m_tickets.constFind(ticket);
    if (found == m_tickets.cend() || now < 0) return {};
    auto& state = m_origins[*found];
    int interval = 0;
    for (const auto minimum : state.active) interval = std::max(interval, minimum);
    if (state.lastStart >= 0) {
        const auto delay = state.lastStart + std::max(interval, state.lastInterval) - now;
        if (delay > 0) return {true, delay};
    }
    state.lastStart = now; state.lastInterval = interval;
    return {true, 0};
}
void OriginRequestPacer::release(quint64 ticket, qint64 now)
{
    std::lock_guard<std::mutex> guard(m_lock);
    const auto origin = m_tickets.take(ticket);
    auto found = m_origins.find(origin);
    if (found == m_origins.end()) return;
    found->active.remove(ticket);
    if (found->active.isEmpty() && (found->lastStart < 0 || now >= found->lastStart + found->lastInterval)) m_origins.erase(found);
}
struct NetworkBroker::Job {
    quint64 id = 0;
    Manifest manifest;
    QJsonObject grants, request;
    CredentialResolver credential;
    RequestGuard current;
    Callback callback;
    QUrl url;
    QPointer<QNetworkReply> reply;
    QPointer<QTimer> deadline;
    QPointer<QTimer> paceTimer;
    quint64 paceTicket = 0;
    int minimumIntervalMs = 0;
    NetworkBroker::RequestKind kind = NetworkBroker::RequestKind::BinaryAsset;
    bool prefix = false, stripCredentials = false;
    QString optionError;
    int lookup = -1, redirects = 0;
    qint64 limit = 0;
    Response response;
};

NetworkBroker::NetworkBroker(QObject* parent) : QObject(parent), m_network(this)
{
    // DNS validation/pinning must not be bypassed by an implicit proxy resolver.
    m_network.setProxy(QNetworkProxy::NoProxy);
}
NetworkBroker::~NetworkBroker()
{
    const auto jobs = m_jobs;
    for (const auto& job : jobs) { job->callback = {}; finish(job); }
}

bool NetworkBroker::publicAddress(const QHostAddress& address)
{
    if (address.isNull() || address.isLoopback() || address.isMulticast()) return false;
    if (address.protocol() == QAbstractSocket::IPv4Protocol) {
        const quint32 ip = address.toIPv4Address();
        const QList<QPair<quint32, int>> excluded = {
            {0x00000000,8}, {0x0a000000,8}, {0x64400000,10}, {0x7f000000,8},
            {0xa9fe0000,16}, {0xac100000,12}, {0xc0000000,24}, {0xc0000200,24},
            {0xc0a80000,16}, {0xc6120000,15}, {0xc6336400,24}, {0xcb007100,24},
            {0xe0000000,4}, {0xf0000000,4}
        };
        for (const auto& range : excluded) {
            const quint32 mask = 0xffffffffu << (32 - range.second);
            if ((ip & mask) == range.first) return false;
        }
        return true;
    }
    if (address.protocol() != QAbstractSocket::IPv6Protocol) return false;
    // Only global unicast. This excludes mapped IPv4, link-local, ULA,
    // unspecified, multicast and special transition ranges.
    if (!address.isInSubnet(QHostAddress(QStringLiteral("2000::")), 3)) return false;
    if (address.isInSubnet(QHostAddress(QStringLiteral("2001::")), 23)
        || address.isInSubnet(QHostAddress(QStringLiteral("2001:db8::")), 32)
        || address.isInSubnet(QHostAddress(QStringLiteral("2002::")), 16)) return false;
    return true;
}

bool NetworkBroker::permittedUrl(const Manifest& manifest, const QJsonObject& grants, const QUrl& url)
{
    if (!url.isValid() || url.scheme() != QLatin1String("https") || url.host().isEmpty()
        || !url.userInfo().isEmpty() || url.hasFragment() || url.toEncoded().size() > 8192) return false;
    QUrl origin; origin.setScheme(QStringLiteral("https")); origin.setHost(url.host().toLower());
    if (url.port(443) != 443) origin.setPort(url.port());
    const auto key = exactHttpsOrigin(origin.toString());
    if (key.isEmpty()) return false;
    auto contains = [&](const QJsonObject& scope) {
        for (const auto& value : scope.value(QStringLiteral("origins")).toArray())
            if (exactHttpsOrigin(value.toString()) == key) return true;
        return false;
    };
    return contains(manifest.permissionScope(QStringLiteral("network.http")))
        && contains(grants.value(QStringLiteral("network.http")).toObject());
}

bool NetworkBroker::permittedImageRedirect(const Manifest& manifest, const QJsonObject& grants, const QUrl& url)
{
    if (!url.isValid() || url.scheme() != QStringLiteral("https") || url.port(443) != 443
        || url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasFragment() || url.toEncoded().size() > 8192) return false;
    const auto host = url.host().toLower();
    if (host != QStringLiteral("archive.org") && !host.endsWith(QStringLiteral(".archive.org"))) return false;
    const auto profile = QStringLiteral("internet-archive-images@1");
    return manifest.permissionScope(QStringLiteral("network.http")).value(QStringLiteral("assetRedirectProfiles")).toArray().contains(profile)
        && grants.value(QStringLiteral("network.http")).toObject().value(QStringLiteral("assetRedirectProfiles")).toArray().contains(profile);
}
bool NetworkBroker::textPrefixContentType(const QString& value)
{
    const auto type = value.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
    return type == QStringLiteral("text/html") || type == QStringLiteral("text/plain");
}
NetworkBroker::Authorization NetworkBroker::authorize(const Manifest& manifest, const QJsonObject& grants,
                                                       const QUrl& url, RequestKind kind, int redirects)
{
    const bool imageRedirect = kind == RequestKind::ImageAsset && redirects > 0
        && permittedImageRedirect(manifest, grants, url);
    return {permittedUrl(manifest, grants, url) || imageRedirect, imageRedirect};
}
NetworkBroker::BodyRead NetworkBroker::readBody(QIODevice* source, QByteArray& output, qint64 limit, bool prefix)
{
    if (!source || limit < 1 || output.size() > limit) return BodyRead::TooLarge;
    const qint64 target = prefix ? limit : limit + 1;
    while (source->bytesAvailable() > 0 && output.size() < target) {
        const auto bytes = source->read(std::min<qint64>(64 * 1024, target - output.size()));
        if (bytes.isEmpty()) break;
        output.append(bytes);
    }
    if (output.size() > limit) return BodyRead::TooLarge;
    return prefix && output.size() == limit ? BodyRead::PrefixComplete : BodyRead::More;
}
quint64 NetworkBroker::fetch(const Manifest& manifest, const QJsonObject& grants, const QJsonObject& request,
                            CredentialResolver credential, Callback callback, qint64 maximumBytes)
{
    return fetch(manifest, grants, request, std::move(credential), std::move(callback), maximumBytes, RequestKind::BinaryAsset);
}
quint64 NetworkBroker::fetch(const Manifest& manifest, const QJsonObject& grants, const QJsonObject& request,
                            CredentialResolver credential, Callback callback, qint64 maximumBytes, RequestKind kind)
{
    return fetch(manifest, grants, request, std::move(credential), std::move(callback), maximumBytes, kind, {});
}
quint64 NetworkBroker::fetch(const Manifest& manifest, const QJsonObject& grants, const QJsonObject& request,
                            CredentialResolver credential, Callback callback, qint64 maximumBytes, RequestKind kind, RequestGuard current)
{
    auto job = std::make_shared<Job>();
    job->id = ++m_sequence; job->manifest = manifest; job->grants = grants;
    job->request = request; job->credential = std::move(credential); job->callback = std::move(callback);
    job->current = std::move(current);
    job->url = QUrl(request.value(QStringLiteral("url")).toString(), QUrl::StrictMode);
    job->kind = kind;
    const auto declaredInterval = minimumInterval(manifest.permissionScope(QStringLiteral("network.http")));
    const auto approvedInterval = minimumInterval(grants.value(QStringLiteral("network.http")).toObject());
    job->minimumIntervalMs = declaredInterval < 0 || approvedInterval < 0 ? -1 : std::max(declaredInterval, approvedInterval);
    job->limit = qBound(qint64(1), maximumBytes, qint64(16 * 1024 * 1024));
    const auto requested = request.value(QStringLiteral("responseBytes"));
    if (!requested.isUndefined()) {
        const auto limit = requested.toDouble(-1);
        if (!requested.isDouble() || !std::isfinite(limit) || limit < 1 || limit > 512 * 1024 || std::floor(limit) != limit)
            job->optionError = tr("The response byte limit is not correct.");
        else job->limit = std::min(job->limit, qint64(limit));
    }
    const auto truncated = request.value(QStringLiteral("allowTruncated"));
    if (!truncated.isUndefined() && !truncated.isBool()) job->optionError = tr("The response prefix option is not correct.");
    job->prefix = truncated.toBool();
    if (job->prefix && (kind != RequestKind::Document || requested.isUndefined()
        || request.value(QStringLiteral("method")).toString(QStringLiteral("GET")) != QStringLiteral("GET")))
        job->optionError = tr("Only a GET document request can use a response prefix.");
    int packageJobs = 0;
    for (const auto& active : m_jobs) if (active->manifest.id == manifest.id) ++packageJobs;
    m_jobs.insert(job->id, job);
    job->deadline = new QTimer(this); job->deadline->setSingleShot(true);
    connect(job->deadline, &QTimer::timeout, this, [this, job] { finish(job, tr("The add-on service did not send a response in time.")); });
    job->deadline->start(20000);
    QTimer::singleShot(0, this, [this, job, packageJobs] {
        if (!m_jobs.contains(job->id)) return;
        if (!job->optionError.isEmpty()) finish(job, job->optionError);
        else if (packageJobs >= 4 || m_jobs.size() > 64) finish(job, tr("The add-on sent too many requests."));
        else begin(job);
    });
    return job->id;
}

void NetworkBroker::begin(const std::shared_ptr<Job>& job)
{
    if (!m_jobs.contains(job->id)) return;
    if (job->current && !job->current()) { finish(job, tr("The add-on release, account, or settings changed.")); return; }
    const auto authorization = authorize(job->manifest, job->grants, job->url, job->kind, job->redirects);
    if (!authorization.permitted) {
        finish(job, tr("VLC does not have permission to use this network address.")); return;
    }
    if (authorization.stripCredentials) job->stripCredentials = true;
    const auto method = job->request.value(QStringLiteral("method")).toString(QStringLiteral("GET"));
    const auto scope = job->manifest.permissionScope(QStringLiteral("network.http"));
    const auto approved = job->grants.value(QStringLiteral("network.http")).toObject();
    auto methodAllowed = [&](const QJsonObject& object) {
        const auto methods = object.value(QStringLiteral("methods")).toArray();
        return methods.isEmpty() ? method == QLatin1String("GET") : methods.contains(method);
    };
    if (!methodAllowed(scope) || !methodAllowed(approved)
        || (method != QLatin1String("GET") && method != QLatin1String("POST"))
        || (job->kind == RequestKind::ImageAsset && method != QLatin1String("GET"))) {
        finish(job, tr("VLC does not have permission to do this network operation.")); return;
    }
    if (!job->paceTicket) {
        const auto ticket = ++paceSequence;
        if (job->minimumIntervalMs < 0 || !appPacer().enqueue(ticket, originFor(job->url), job->minimumIntervalMs, monotonicMs())) {
            finish(job, tr("The network origin request limit or interval is not correct.")); return;
        }
        job->paceTicket = ticket;
    }
    const QHostAddress literal(job->url.host());
    if (!literal.isNull()) { resolved(job, {literal}); return; }
    job->lookup = QHostInfo::lookupHost(job->url.host(), this, [this, job](const QHostInfo& info) {
        job->lookup = -1;
        if (info.error() != QHostInfo::NoError) finish(job, tr("VLC cannot connect to the add-on service."));
        else resolved(job, info.addresses());
    });
}

void NetworkBroker::resolved(const std::shared_ptr<Job>& job, const QList<QHostAddress>& addresses)
{
    if (!m_jobs.contains(job->id)) return;
    if (job->current && !job->current()) { finish(job, tr("The add-on release, account, or settings changed.")); return; }
    if (addresses.isEmpty()) { finish(job, tr("VLC cannot connect to the add-on service.")); return; }
    for (const auto& address : addresses)
        if (!publicAddress(address)) { finish(job, tr("VLC does not have permission to use a private network address.")); return; }
    auto selected = addresses.first();
    for (const auto& address : addresses) if (address.protocol() == QAbstractSocket::IPv4Protocol) { selected = address; break; }
    auto pinned = job->url; pinned.setHost(selected.toString());
    QNetworkRequest request(pinned);
    request.setPeerVerifyName(job->url.host());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    const QByteArray host = job->url.host().toUtf8() + (job->url.port(443) == 443 ? QByteArray{} : ':' + QByteArray::number(job->url.port()));
    request.setRawHeader("Host", host);
    request.setRawHeader("User-Agent", "VLC-Addon/1.0 (+https://www.videolan.org/)");
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Accept-Encoding", "identity");
    const auto headers = job->request.value(QStringLiteral("headers")).toObject();
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        const auto name = it.key().toLower();
        const auto value = it.value().toString().toUtf8();
        if ((name != QLatin1String("accept") && name != QLatin1String("content-type"))
            || value.size() > 512 || value.contains('\r') || value.contains('\n')) {
            finish(job, tr("VLC cannot use the request header from this add-on.")); return;
        }
        request.setRawHeader(name.toUtf8(), value);
    }
    const auto credentialField = job->request.value(QStringLiteral("credential")).toString();
    if (!credentialField.isEmpty() && !job->stripCredentials) {
        const auto declared = job->manifest.permissionScope(QStringLiteral("credentials.use"));
        const auto approved = job->grants.value(QStringLiteral("credentials.use")).toObject();
        for (const auto& value : declared.value(QStringLiteral("bindings")).toArray()) {
            const auto binding = value.toObject();
            if (binding.value(QStringLiteral("field")).toString() != credentialField
                || !approved.value(QStringLiteral("bindings")).toArray().contains(binding)) continue;
            QUrl origin; origin.setScheme(QStringLiteral("https")); origin.setHost(job->url.host());
            if (job->url.port(443) != 443) origin.setPort(job->url.port());
            if (exactHttpsOrigin(binding.value(QStringLiteral("origin")).toString()) != exactHttpsOrigin(origin.toString())) continue;
            const auto secret = job->credential ? job->credential(credentialField) : QByteArray{};
            const auto header = binding.value(QStringLiteral("header")).toString().toUtf8();
            static const QRegularExpression headerPattern(QStringLiteral("^[A-Za-z][A-Za-z0-9-]{0,63}$"));
            if (secret.isEmpty() || secret.size() > 4096 || secret.contains('\r') || secret.contains('\n')
                || !headerPattern.match(QString::fromLatin1(header)).hasMatch()) {
                finish(job, tr("Enter the account data or API key for this add-on.")); return;
            }
            const auto prefix = binding.value(QStringLiteral("prefix")).toString().toUtf8();
            if (prefix.contains('\r') || prefix.contains('\n') || prefix.size() > 32) {
                finish(job, tr("The credential binding is not correct.")); return;
            }
            request.setRawHeader(header, prefix + secret);
        }
        // On cross-origin redirects no credential binding matches, so the
        // protected header is deliberately omitted rather than forwarded.
    }
    const auto body = job->request.value(QStringLiteral("body")).toString().toUtf8();
    if (body.size() > 128 * 1024) { finish(job, tr("The add-on request is too large.")); return; }
    const auto admission = appPacer().admit(job->paceTicket, monotonicMs());
    if (!admission.valid) { finish(job, tr("The network origin request is no longer available.")); return; }
    if (admission.delayMs > 0) {
        job->paceTimer = new QTimer(this); job->paceTimer->setSingleShot(true);
        connect(job->paceTimer, &QTimer::timeout, this, [this, job, addresses] {
            if (job->paceTimer) job->paceTimer->deleteLater();
            job->paceTimer = nullptr;
            if (m_jobs.contains(job->id)) resolved(job, addresses);
        });
        job->paceTimer->start(int(admission.delayMs));
        return;
    }
    const bool post = job->request.value(QStringLiteral("method")).toString(QStringLiteral("GET")) == QLatin1String("POST");
    job->reply = post ? m_network.post(request, body) : m_network.get(request);
    job->reply->setReadBufferSize(qMin<qint64>(job->limit + 1, 64 * 1024));
    connect(job->reply, &QNetworkReply::readyRead, this, [this, job] {
        if (!job->reply || !m_jobs.contains(job->id)) return;
        readResponse(job);
    });
    connect(job->reply, &QNetworkReply::finished, this, [this, job] {
        if (!job->reply || !m_jobs.contains(job->id)) return;
        const auto reply = job->reply;
        if (!readResponse(job)) return;
        job->response.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (!redirect.isEmpty()) {
            if (++job->redirects > 5) { finish(job, tr("The add-on service sent too many redirects.")); return; }
            job->url = job->url.resolved(redirect); job->response = {};
            disconnect(reply, nullptr, this, nullptr); reply->deleteLater(); job->reply = nullptr;
            appPacer().release(job->paceTicket, monotonicMs()); job->paceTicket = 0;
            begin(job); return;
        }
        responseHeaders(job);
        if (job->response.status == 0 || (reply->error() != QNetworkReply::NoError && job->response.status < 400))
            finish(job, tr("VLC cannot make a secure connection to this add-on service."));
        else finish(job);
    });
}

void NetworkBroker::responseHeaders(const std::shared_ptr<Job>& job)
{
    if (!job->reply) return;
    job->response.status = job->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    for (const auto& header : {QByteArray("content-type"), QByteArray("retry-after")})
        job->response.headers.insert(QString::fromLatin1(header), QString::fromLatin1(job->reply->rawHeader(header).left(512)));
}
bool NetworkBroker::readResponse(const std::shared_ptr<Job>& job)
{
    if (!job->reply || !m_jobs.contains(job->id)) return false;
    if (job->current && !job->current()) { finish(job, tr("The add-on release, account, or settings changed.")); return false; }
    const auto reply = job->reply;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    responseHeaders(job);
    if (job->prefix && status >= 200 && status < 300) {
        if (!textPrefixContentType(QString::fromLatin1(reply->rawHeader("content-type")))) {
            finish(job, tr("A response prefix requires HTML or plain text.")); return false;
        }
        const auto read = readBody(reply, job->response.body, job->limit, true);
        if (read == BodyRead::PrefixComplete) {
            responseHeaders(job);
            const auto length = reply->header(QNetworkRequest::ContentLengthHeader);
            job->response.truncated = !reply->isFinished() && (!length.isValid() || length.toLongLong() > job->response.body.size());
            finish(job); return false;
        }
        if (read == BodyRead::TooLarge) { finish(job, tr("The add-on response is too large.")); return false; }
        return true;
    }
    if (readBody(reply, job->response.body, job->limit, false) == BodyRead::TooLarge) {
        finish(job, tr("The add-on response is too large.")); return false;
    }
    return true;
}
void NetworkBroker::finish(const std::shared_ptr<Job>& job, const QString& error)
{
    if (!m_jobs.remove(job->id)) return;
    if (job->paceTicket) { appPacer().release(job->paceTicket, monotonicMs()); job->paceTicket = 0; }
    if (job->lookup >= 0) QHostInfo::abortHostLookup(job->lookup);
    if (job->deadline) { job->deadline->stop(); job->deadline->deleteLater(); }
    if (job->paceTimer) { job->paceTimer->stop(); job->paceTimer->deleteLater(); }
    if (job->reply) { disconnect(job->reply, nullptr, this, nullptr); job->reply->abort(); job->reply->deleteLater(); }
    job->response.error = error;
    if (job->callback && job->current && !job->current()) {
        job->response.body.clear();
        job->response.error = tr("The add-on release, account, or settings changed.");
    }
    const auto callback = std::move(job->callback);
    if (callback) callback(job->response);
}
void NetworkBroker::cancel(quint64 id)
{
    const auto job = m_jobs.value(id); if (!job) return;
    job->callback = {}; finish(job);
}
void NetworkBroker::cancelPackage(const QString& package)
{ const auto jobs = m_jobs; for (const auto& job : jobs) if (job->manifest.id == package) cancel(job->id); }
}
