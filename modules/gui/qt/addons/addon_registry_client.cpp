// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_registry_client.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QNetworkReply>
#include <QSaveFile>
#include <QThreadPool>
#include <utility>

namespace vlc::addons {
namespace {
const char* const CanceledProperty = "addonRegistryCanceled";
const char* const OffHostProperty = "addonRegistryOffHost";

QString normalized(const QString& address)
{
    QString result = address.trimmed();
    while (result.endsWith(QLatin1Char('/'))) result.chop(1);
    return result;
}
// Runs on the GUI thread later, unless the owner is gone by then.
template <typename Owner, typename Function>
void later(const QPointer<Owner>& owner, Function function)
{
    QMetaObject::invokeMethod(QCoreApplication::instance(), [owner, function = std::move(function)]() mutable {
        if (owner) function();
    }, Qt::QueuedConnection);
}
}

AddonRegistryClient::AddonRegistryClient(QString cacheDirectory, QObject* parent)
    : QObject(parent), m_cacheDirectory(std::move(cacheDirectory))
{
}

AddonRegistryClient::~AddonRegistryClient()
{
    for (const auto& reply : std::as_const(m_replies))
        if (reply) { reply->setProperty(CanceledProperty, true); reply->abort(); }
}

void AddonRegistryClient::setSettingsSource(std::function<Settings()> source) { m_settings = std::move(source); }

AddonRegistryClient::Settings AddonRegistryClient::settings() const
{
    Settings result = m_settings ? m_settings() : Settings{};
    result.address = normalized(result.address);
    if (result.address.isEmpty()) result.address = QString::fromLatin1(DefaultAddress);
    return result;
}

bool AddonRegistryClient::loopbackAddress(const QUrl& url)
{
    const auto host = url.host().toLower();
    return host == QLatin1String("localhost") || host.endsWith(QLatin1String(".localhost"))
        || host == QLatin1String("127.0.0.1") || host == QLatin1String("::1");
}

QString AddonRegistryClient::addressProblem(const QString& address)
{
    const auto value = normalized(address);
    if (value.isEmpty()) return tr("Enter the registry address.");
    const QUrl url(value, QUrl::StrictMode);
    if (value.size() > 2048 || !url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty()
        || url.hasQuery() || url.hasFragment())
        return tr("The registry address is not a correct web address.");
    if (url.scheme() == QLatin1String("https")) return {};
    if (url.scheme() == QLatin1String("http") && loopbackAddress(url)) return {};
    return tr("The registry address must use HTTPS. Only a registry on this computer may use HTTP.");
}

AddonRegistryClient::Failure AddonRegistryClient::failure(const QString& code) const
{
    if (code == QLatin1String("off")) return {code, tr("The add-on registry is turned off.")};
    if (code == QLatin1String("timeout")) return {code, tr("The add-on registry did not answer in time.")};
    if (code == QLatin1String("too_large")) return {code, tr("The add-on registry sent more data than the app allows.")};
    if (code == QLatin1String("not_found")) return {code, tr("The add-on registry does not have this add-on or version.")};
    if (code == QLatin1String("rate_limited")) return {code, tr("The add-on registry is busy. Try again in a minute.")};
    if (code == QLatin1String("invalid")) return {code, tr("The add-on registry sent an answer the app cannot use.")};
    if (code == QLatin1String("canceled")) return {code, {}};
    if (code == QLatin1String("http")) return {code, tr("The add-on registry could not answer. Try again later.")};
    return {QStringLiteral("offline"), tr("The add-on registry is not available. Installed add-ons still work.")};
}

QString AddonRegistryClient::cachePath(const QString& address) const
{
    const auto key = QCryptographicHash::hash(address.toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
    return QDir(m_cacheDirectory).filePath(QStringLiteral("addon-registry/") + QString::fromLatin1(key));
}

quint64 AddonRegistryClient::get(const QString& path, const QUrlQuery& query, qsizetype maximumBytes,
                                 const QByteArray& etag, Raw done)
{
    const QPointer<AddonRegistryClient> owner(this);
    const auto config = settings();
    const QString code = !config.enabled ? QStringLiteral("off") : !addressProblem(config.address).isEmpty() ? QStringLiteral("address") : QString{};
    if (!code.isEmpty()) {
        const Failure refused = code == QLatin1String("address") ? Failure{code, addressProblem(config.address)} : failure(code);
        later(owner, [done, refused] { done(std::nullopt, refused); });
        return 0;
    }
    QUrl url(config.address, QUrl::StrictMode);
    url.setPath(url.path() + path);
    if (!query.isEmpty()) url.setQuery(query);
    QNetworkRequest request(url);
    request.setTransferTimeout(TimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(3);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    request.setRawHeader("Accept", "application/json");
    const auto version = QCoreApplication::applicationVersion();
    request.setHeader(QNetworkRequest::UserAgentHeader, version.isEmpty() ? QStringLiteral("Hakone") : QStringLiteral("Hakone/") + version);
    if (!etag.isEmpty()) request.setRawHeader("If-None-Match", etag);

    auto* reply = m_network.get(request);
    const quint64 id = ++m_requests;
    m_replies.insert(id, reply);
    auto body = std::make_shared<QByteArray>();
    auto tooLarge = std::make_shared<bool>(false);
    const QString host = url.host();
    connect(reply, &QNetworkReply::redirected, reply, [reply, host](const QUrl& target) {
        if (target.host() != host) { reply->setProperty(OffHostProperty, true); reply->abort(); }
    });
    connect(reply, &QNetworkReply::readyRead, reply, [reply, body, tooLarge, maximumBytes] {
        body->append(reply->readAll());
        if (body->size() > maximumBytes) { *tooLarge = true; reply->abort(); }
    });
    connect(reply, &QNetworkReply::finished, this, [this, id, reply, body, tooLarge, maximumBytes, done] {
        m_replies.remove(id);
        reply->deleteLater();
        if (!*tooLarge) body->append(reply->readAll());
        if (*tooLarge || body->size() > maximumBytes) return done(std::nullopt, failure(QStringLiteral("too_large")));
        if (reply->property(CanceledProperty).toBool()) return done(std::nullopt, failure(QStringLiteral("canceled")));
        if (reply->property(OffHostProperty).toBool()) return done(std::nullopt, failure(QStringLiteral("invalid")));
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 0) {
            const bool timedOut = reply->error() == QNetworkReply::OperationCanceledError || reply->error() == QNetworkReply::TimeoutError;
            qWarning("Add-on registry: %s", qUtf8Printable(reply->errorString()));
            return done(std::nullopt, failure(timedOut ? QStringLiteral("timeout") : QStringLiteral("offline")));
        }
        if (status == 304 || status == 200)
            return done(Reply{status == 200 ? *body : QByteArray{}, reply->rawHeader("ETag"), status}, {});
        const auto code = registry::errorCode(*body);
        if (status == 404) return done(std::nullopt, failure(QStringLiteral("not_found")));
        if (status == 429) return done(std::nullopt, failure(QStringLiteral("rate_limited")));
        qWarning("Add-on registry: HTTP %d %s", status, qUtf8Printable(code));
        done(std::nullopt, failure(QStringLiteral("http")));
    });
    return id;
}

template <typename T>
quint64 AddonRegistryClient::getParsed(const QString& path, const QUrlQuery& query,
    std::function<bool(const QByteArray&, T*, bool, QString*)> parser, Done<T> done)
{
    const QPointer<AddonRegistryClient> owner(this);
    const bool httpLinks = loopbackAddress(QUrl(settings().address));
    return get(path, query, registry::MaximumResponseBytes, {}, [this, owner, parser, done, httpLinks](const std::optional<Reply>& reply, const Failure& refused) {
        if (!reply) return done(std::nullopt, refused);
        const Failure invalid = failure(QStringLiteral("invalid"));
        QThreadPool::globalInstance()->start([owner, parser, done, httpLinks, invalid, body = reply->body] {
            auto value = std::make_shared<T>();
            QString error;
            const bool parsed = parser(body, value.get(), httpLinks, &error);
            if (!parsed) qWarning("Add-on registry: %s", qUtf8Printable(error));
            later(owner, [done, value, parsed, invalid] {
                if (parsed) done(*value, {}); else done(std::nullopt, invalid);
            });
        });
    });
}

quint64 AddonRegistryClient::search(const QUrlQuery& query, Done<RegistryPage> done)
{
    return getParsed<RegistryPage>(QStringLiteral("/addons"), query, registry::parsePage, std::move(done));
}

quint64 AddonRegistryClient::addon(const QString& id, Done<RegistryAddon> done)
{
    return getParsed<RegistryAddon>(QStringLiteral("/addons/") + id, {}, registry::parseAddon, std::move(done));
}

quint64 AddonRegistryClient::release(const QString& id, const QString& version, Done<RegistryRelease> done)
{
    return getParsed<RegistryRelease>(QStringLiteral("/addons/") + id + QStringLiteral("/releases/") + version, {},
                                      registry::parseRelease, std::move(done));
}

void AddonRegistryClient::cancel(quint64 request)
{
    const auto reply = m_replies.value(request);
    if (!reply) return;
    reply->setProperty(CanceledProperty, true);
    reply->abort();
}

void AddonRegistryClient::loadIndex(std::function<void(bool, const Failure&)> done)
{
    m_indexWaiting.append(std::move(done));
    if (m_indexLoading) return;
    m_indexLoading = true;
    const QPointer<AddonRegistryClient> owner(this);
    const auto address = settings().address;
    const auto finish = [this](bool changed, const Failure& result) {
        m_indexLoading = false;
        const auto waiting = std::exchange(m_indexWaiting, {});
        for (const auto& callback : waiting) callback(changed, result);
    };
    const auto fetch = [this, owner, address, finish] {
        const auto directory = cachePath(address);
        get(QStringLiteral("/index"), {}, registry::MaximumIndexBytes, m_indexEtag,
            [this, owner, address, directory, finish](const std::optional<Reply>& reply, const Failure& refused) {
            if (!reply) return finish(false, refused);
            if (reply->status == 304) return finish(false, {});
            const Failure invalid = failure(QStringLiteral("invalid"));
            QThreadPool::globalInstance()->start([owner, address, directory, finish, invalid, body = reply->body, etag = reply->etag] {
                auto index = std::make_shared<RegistryIndex>();
                QString error;
                const bool parsed = registry::parseIndex(body, index.get(), &error);
                if (parsed && QDir().mkpath(directory)) {
                    // The ETag file names the cached bytes it belongs to.
                    QSaveFile bytes(QDir(directory).filePath(QStringLiteral("index.json")));
                    QSaveFile tag(QDir(directory).filePath(QStringLiteral("index.etag")));
                    if (bytes.open(QIODevice::WriteOnly) && bytes.write(body) == body.size() && bytes.commit()
                        && tag.open(QIODevice::WriteOnly)) { tag.write(etag); tag.commit(); }
                }
                if (!parsed) qWarning("Add-on registry: %s", qUtf8Printable(error));
                later(owner, [owner, address, finish, invalid, index, parsed, etag] {
                    if (!parsed) return finish(false, invalid);
                    owner->m_index = *index; owner->m_indexEtag = etag;
                    owner->m_indexAddress = address; owner->m_indexKnown = true;
                    finish(true, {});
                });
            });
        });
    };
    if (m_indexKnown && m_indexAddress == address) { fetch(); return; }
    // A new address: show its cached index, if any, then ask the registry.
    m_index = {}; m_indexEtag.clear(); m_indexKnown = false; m_indexAddress = address;
    const auto directory = cachePath(address);
    QThreadPool::globalInstance()->start([owner, address, directory, fetch] {
        QFile bytes(QDir(directory).filePath(QStringLiteral("index.json")));
        QFile tag(QDir(directory).filePath(QStringLiteral("index.etag")));
        auto index = std::make_shared<RegistryIndex>();
        QByteArray etag;
        bool cached = bytes.size() <= registry::MaximumIndexBytes && bytes.open(QIODevice::ReadOnly)
            && registry::parseIndex(bytes.read(registry::MaximumIndexBytes), index.get());
        if (cached && tag.open(QIODevice::ReadOnly)) etag = tag.read(1024).trimmed();
        later(owner, [owner, address, fetch, index, cached, etag] {
            if (cached && owner->m_indexAddress == address) {
                owner->m_index = *index; owner->m_indexEtag = etag; owner->m_indexKnown = true;
            }
            fetch();
        });
    });
}
}
