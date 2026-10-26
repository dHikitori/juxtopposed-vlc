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

namespace vlc::addons {
struct NetworkBroker::Job {
    quint64 id = 0;
    Manifest manifest;
    QJsonObject grants, request;
    CredentialResolver credential;
    Callback callback;
    QUrl url;
    QPointer<QNetworkReply> reply;
    QPointer<QTimer> deadline;
    int lookup = -1, redirects = 0;
    qint64 limit = 0;
    Response response;
};

NetworkBroker::NetworkBroker(QObject* parent) : QObject(parent), m_network(this)
{
    // DNS validation/pinning must not be bypassed by an implicit proxy resolver.
    m_network.setProxy(QNetworkProxy::NoProxy);
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

quint64 NetworkBroker::fetch(const Manifest& manifest, const QJsonObject& grants, const QJsonObject& request,
                            CredentialResolver credential, Callback callback, qint64 maximumBytes)
{
    auto job = std::make_shared<Job>();
    job->id = ++m_sequence; job->manifest = manifest; job->grants = grants;
    job->request = request; job->credential = std::move(credential); job->callback = std::move(callback);
    job->url = QUrl(request.value(QStringLiteral("url")).toString(), QUrl::StrictMode);
    job->limit = qBound(qint64(1), maximumBytes, qint64(16 * 1024 * 1024));
    int packageJobs = 0;
    for (const auto& active : m_jobs) if (active->manifest.id == manifest.id) ++packageJobs;
    m_jobs.insert(job->id, job);
    job->deadline = new QTimer(this); job->deadline->setSingleShot(true);
    connect(job->deadline, &QTimer::timeout, this, [this, job] { finish(job, tr("The add-on request timed out.")); });
    job->deadline->start(20000);
    QTimer::singleShot(0, this, [this, job, packageJobs] {
        if (packageJobs >= 4 || m_jobs.size() > 64) finish(job, tr("The add-on request limit was reached."));
        else begin(job);
    });
    return job->id;
}

void NetworkBroker::begin(const std::shared_ptr<Job>& job)
{
    if (!m_jobs.contains(job->id)) return;
    if (!permittedUrl(job->manifest, job->grants, job->url)) { finish(job, tr("This network destination is not approved.")); return; }
    const auto method = job->request.value(QStringLiteral("method")).toString(QStringLiteral("GET"));
    const auto scope = job->manifest.permissionScope(QStringLiteral("network.http"));
    const auto approved = job->grants.value(QStringLiteral("network.http")).toObject();
    auto methodAllowed = [&](const QJsonObject& object) {
        const auto methods = object.value(QStringLiteral("methods")).toArray();
        return methods.isEmpty() ? method == QLatin1String("GET") : methods.contains(method);
    };
    if (!methodAllowed(scope) || !methodAllowed(approved)
        || (method != QLatin1String("GET") && method != QLatin1String("POST"))) {
        finish(job, tr("This network operation is not approved.")); return;
    }
    const QHostAddress literal(job->url.host());
    if (!literal.isNull()) { resolved(job, {literal}); return; }
    job->lookup = QHostInfo::lookupHost(job->url.host(), this, [this, job](const QHostInfo& info) {
        job->lookup = -1;
        if (info.error() != QHostInfo::NoError) finish(job, tr("The add-on service could not be reached."));
        else resolved(job, info.addresses());
    });
}

void NetworkBroker::resolved(const std::shared_ptr<Job>& job, const QList<QHostAddress>& addresses)
{
    if (!m_jobs.contains(job->id)) return;
    if (addresses.isEmpty()) { finish(job, tr("The add-on service could not be reached.")); return; }
    for (const auto& address : addresses)
        if (!publicAddress(address)) { finish(job, tr("Private network destinations are not approved.")); return; }
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
    request.setRawHeader("User-Agent", "VLC-Addon/1.0");
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Accept-Encoding", "identity");
    const auto headers = job->request.value(QStringLiteral("headers")).toObject();
    for (auto it = headers.begin(); it != headers.end(); ++it) {
        const auto name = it.key().toLower();
        const auto value = it.value().toString().toUtf8();
        if ((name != QLatin1String("accept") && name != QLatin1String("content-type"))
            || value.size() > 512 || value.contains('\r') || value.contains('\n')) {
            finish(job, tr("The add-on supplied an unsupported request header.")); return;
        }
        request.setRawHeader(name.toUtf8(), value);
    }
    const auto credentialField = job->request.value(QStringLiteral("credential")).toString();
    if (!credentialField.isEmpty()) {
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
                finish(job, tr("Set up this add-on's account or API key first.")); return;
            }
            const auto prefix = binding.value(QStringLiteral("prefix")).toString().toUtf8();
            if (prefix.contains('\r') || prefix.contains('\n') || prefix.size() > 32) {
                finish(job, tr("The credential binding is invalid.")); return;
            }
            request.setRawHeader(header, prefix + secret);
        }
        // On cross-origin redirects no credential binding matches, so the
        // protected header is deliberately omitted rather than forwarded.
    }
    const auto body = job->request.value(QStringLiteral("body")).toString().toUtf8();
    if (body.size() > 128 * 1024) { finish(job, tr("The add-on request is too large.")); return; }
    const bool post = job->request.value(QStringLiteral("method")).toString(QStringLiteral("GET")) == QLatin1String("POST");
    job->reply = post ? m_network.post(request, body) : m_network.get(request);
    job->reply->setReadBufferSize(qMin<qint64>(job->limit + 1, 64 * 1024));
    connect(job->reply, &QNetworkReply::readyRead, this, [this, job] {
        if (!job->reply || !m_jobs.contains(job->id)) return;
        job->response.body.append(job->reply->readAll());
        if (job->response.body.size() > job->limit) finish(job, tr("The add-on response is too large."));
    });
    connect(job->reply, &QNetworkReply::finished, this, [this, job] {
        if (!job->reply || !m_jobs.contains(job->id)) return;
        const auto reply = job->reply;
        job->response.body.append(reply->readAll());
        if (job->response.body.size() > job->limit) { finish(job, tr("The add-on response is too large.")); return; }
        job->response.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (!redirect.isEmpty()) {
            if (++job->redirects > 5) { finish(job, tr("The add-on service redirected too many times.")); return; }
            job->url = job->url.resolved(redirect); job->response = {};
            disconnect(reply, nullptr, this, nullptr); reply->deleteLater(); job->reply = nullptr;
            begin(job); return;
        }
        for (const auto& header : {QByteArray("content-type"), QByteArray("retry-after")})
            job->response.headers.insert(QString::fromLatin1(header), QString::fromLatin1(reply->rawHeader(header).left(512)));
        if (job->response.status == 0 || (reply->error() != QNetworkReply::NoError && job->response.status < 400))
            finish(job, tr("The add-on service could not be reached securely."));
        else finish(job);
    });
}

void NetworkBroker::finish(const std::shared_ptr<Job>& job, const QString& error)
{
    if (!m_jobs.remove(job->id)) return;
    if (job->lookup >= 0) QHostInfo::abortHostLookup(job->lookup);
    if (job->deadline) { job->deadline->stop(); job->deadline->deleteLater(); }
    if (job->reply) { disconnect(job->reply, nullptr, this, nullptr); job->reply->abort(); job->reply->deleteLater(); }
    job->response.error = error;
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
