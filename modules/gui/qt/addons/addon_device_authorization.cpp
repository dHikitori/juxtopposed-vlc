// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_device_authorization.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrlQuery>
#include <memory>

namespace vlc::addons {
namespace {
const QString DeviceGrant = QStringLiteral("urn:ietf:params:oauth:grant-type:device_code");

bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}
// An exact HTTPS endpoint without user data or a fragment.
bool endpoint(const QJsonValue& value, QUrl* url)
{
    if (!value.isString() || value.toString().size() > 2048) return false;
    const QUrl parsed(value.toString(), QUrl::StrictMode);
    if (!parsed.isValid() || parsed.scheme() != QLatin1String("https") || parsed.host().isEmpty()
        || (!parsed.userInfo().isEmpty() || parsed.authority().contains(QLatin1Char('@'))) || parsed.hasFragment()) return false;
    *url = parsed;
    return true;
}
QString origin(const QUrl& url)
{
    return url.scheme() + QStringLiteral("://") + url.host(QUrl::FullyEncoded)
        + (url.port() > 0 && url.port() != 443 ? QLatin1Char(':') + QString::number(url.port()) : QString{});
}
// A page the user opens in the browser: HTTPS, no user data.
QUrl page(const QJsonValue& value)
{
    if (!value.isString() || value.toString().size() > 2048) return {};
    const QUrl url(value.toString(), QUrl::StrictMode);
    return url.isValid() && url.scheme() == QLatin1String("https") && !url.host().isEmpty() && (url.userInfo().isEmpty() && !url.authority().contains(QLatin1Char('@')))
        ? url : QUrl{};
}
QByteArray form(const QList<std::pair<QString, QString>>& fields)
{
    QUrlQuery query;
    for (const auto& [name, value] : fields) query.addQueryItem(name, QString::fromLatin1(QUrl::toPercentEncoding(value)));
    return query.toString(QUrl::FullyEncoded).toLatin1();
}
QJsonObject object(const QByteArray& body)
{
    if (body.size() > DeviceAuthorization::MaxReplyBytes) return {};
    return QJsonDocument::fromJson(body).object();
}
bool token(const QJsonValue& value, QByteArray* output)
{
    // A bearer token is visible ASCII; a header cannot carry anything else.
    static const QRegularExpression visible(QStringLiteral("^[\\x21-\\x7e]{1,8192}\\z"));
    if (!value.isString() || !visible.match(value.toString()).hasMatch()) return false;
    *output = value.toString().toLatin1();
    return true;
}
}

std::optional<DeviceRecipe> DeviceRecipe::from(const QJsonObject& annotation, const QStringList& networkOrigins, QString* error)
{
    static const QStringList keys{QStringLiteral("recipe"), QStringLiteral("deviceAuthorizationUrl"),
        QStringLiteral("tokenUrl"), QStringLiteral("clientId"), QStringLiteral("scopes")};
    for (auto it = annotation.begin(); it != annotation.end(); ++it)
        if (!keys.contains(it.key())) { fail(error, QStringLiteral("The account recipe has a field that is not supported: ") + it.key()); return std::nullopt; }
    if (annotation.value(QStringLiteral("recipe")).toString() != QLatin1String("oauth-device")) {
        fail(error, QStringLiteral("The account recipe is not supported."));
        return std::nullopt;
    }
    DeviceRecipe recipe;
    if (!endpoint(annotation.value(QStringLiteral("deviceAuthorizationUrl")), &recipe.deviceAuthorizationUrl)
        || !endpoint(annotation.value(QStringLiteral("tokenUrl")), &recipe.tokenUrl)) {
        fail(error, QStringLiteral("The account recipe needs HTTPS endpoints."));
        return std::nullopt;
    }
    if (!networkOrigins.contains(origin(recipe.deviceAuthorizationUrl)) || !networkOrigins.contains(origin(recipe.tokenUrl))) {
        fail(error, QStringLiteral("The account endpoints must be declared network origins."));
        return std::nullopt;
    }
    static const QRegularExpression client(QStringLiteral("^[\\x21-\\x7e]{1,256}\\z"));
    static const QRegularExpression scope(QStringLiteral("^[\\x21\\x23-\\x5b\\x5d-\\x7e]{1,128}\\z"));
    const auto clientId = annotation.value(QStringLiteral("clientId"));
    if (!clientId.isString() || !client.match(clientId.toString()).hasMatch()) {
        fail(error, QStringLiteral("The account client ID is not correct."));
        return std::nullopt;
    }
    recipe.clientId = clientId.toString();
    const auto scopes = annotation.value(QStringLiteral("scopes"));
    if (!scopes.isUndefined()) {
        if (!scopes.isArray() || scopes.toArray().size() > 16) { fail(error, QStringLiteral("The account scopes are not correct.")); return std::nullopt; }
        for (const auto& item : scopes.toArray()) {
            if (!item.isString() || !scope.match(item.toString()).hasMatch() || recipe.scopes.contains(item.toString())) {
                fail(error, QStringLiteral("The account scopes are not correct."));
                return std::nullopt;
            }
            recipe.scopes.append(item.toString());
        }
    }
    return recipe;
}

DevicePost devicePost(QNetworkAccessManager* network)
{
    const QPointer<QNetworkAccessManager> owner(network);
    return [owner](const QUrl& url, const QByteArray& body, std::function<void(int, const QByteArray&)> reply)
        -> std::function<void()> {
        if (!owner || url.scheme() != QLatin1String("https")) {
            QTimer::singleShot(0, [reply] { reply(0, {}); });
            return [] {};
        }
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/x-www-form-urlencoded"));
        request.setRawHeader("Accept", "application/json");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
        request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
        request.setTransferTimeout(15000);
        auto* pending = owner->post(request, body);
        pending->setReadBufferSize(DeviceAuthorization::MaxReplyBytes + 1);
        auto received = std::make_shared<QByteArray>();
        auto done = std::make_shared<bool>(false);
        QObject::connect(pending, &QNetworkReply::readyRead, pending, [pending, received] {
            received->append(pending->read(DeviceAuthorization::MaxReplyBytes + 1 - received->size()));
            if (received->size() > DeviceAuthorization::MaxReplyBytes) pending->abort();
        });
        QObject::connect(pending, &QNetworkReply::finished, pending, [pending, received, done, reply] {
            pending->deleteLater();
            if (*done) return;
            *done = true;
            if (received->size() <= DeviceAuthorization::MaxReplyBytes)
                received->append(pending->read(DeviceAuthorization::MaxReplyBytes + 1 - received->size()));
            const int status = pending->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool transport = pending->error() != QNetworkReply::NoError && status == 0;
            reply(transport || received->size() > DeviceAuthorization::MaxReplyBytes ? 0 : status, *received);
        });
        const QPointer<QNetworkReply> alive(pending);
        return [alive, done] { *done = true; if (alive) alive->abort(); };
    };
}

DeviceAuthorization::DeviceAuthorization(DeviceRecipe recipe, DevicePost post, QObject* parent)
    : QObject(parent), m_recipe(std::move(recipe)), m_post(std::move(post))
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &DeviceAuthorization::poll);
}

DeviceAuthorization::~DeviceAuthorization()
{
    if (m_cancelRequest) m_cancelRequest();
}

void DeviceAuthorization::setState(State state)
{
    m_state = state;
    emit changed();
}

void DeviceAuthorization::fail(const QString& message)
{
    m_timer.stop();
    m_deviceCode.clear();
    m_message = message;
    setState(State::Failed);
}

void DeviceAuthorization::start()
{
    if (m_state == State::Requesting || m_state == State::Waiting) return;
    m_message.clear(); m_userCode.clear(); m_verification.clear(); m_complete.clear();
    setState(State::Requesting);
    const QPointer<DeviceAuthorization> self(this);
    m_cancelRequest = m_post(m_recipe.deviceAuthorizationUrl,
        form({{QStringLiteral("client_id"), m_recipe.clientId}, {QStringLiteral("scope"), m_recipe.scopes.join(' ')}}),
        [self](int status, const QByteArray& body) { if (self) self->authorized(status, body); });
}

void DeviceAuthorization::authorized(int status, const QByteArray& body)
{
    m_cancelRequest = {};
    if (m_state != State::Requesting) return;
    if (status == 0) return fail(tr("The app could not reach the sign-in service. Check the network connection and try again."));
    const auto reply = object(body);
    const auto lifetime = reply.value(QStringLiteral("expires_in"));
    const auto interval = reply.value(QStringLiteral("interval"));
    static const QRegularExpression code(QStringLiteral("^[\\x21-\\x7e]{1,4096}\\z"));
    static const QRegularExpression userCode(QStringLiteral("^[\\x20-\\x7e]{1,64}\\z"));
    const auto device = reply.value(QStringLiteral("device_code")).toString();
    const auto user = reply.value(QStringLiteral("user_code")).toString().trimmed();
    const auto verification = page(reply.value(QStringLiteral("verification_uri")));
    const double seconds = lifetime.toDouble();
    if (status != 200 || !code.match(device).hasMatch() || !userCode.match(user).hasMatch() || verification.isEmpty()
        || !lifetime.isDouble() || seconds < 1
        || (!interval.isUndefined() && (!interval.isDouble() || interval.toDouble() < 1 || interval.toDouble() > 60)))
        return fail(tr("The sign-in service sent a reply that the app cannot use."));
    m_deviceCode = device.toLatin1();
    m_userCode = user;
    m_verification = verification;
    m_complete = page(reply.value(QStringLiteral("verification_uri_complete")));
    m_intervalMs = interval.isUndefined() ? 5000 : int(interval.toDouble()) * 1000;
    m_lifetimeMs = qint64(std::min(seconds, double(MaxLifetimeSeconds))) * 1000;
    m_clock.start();
    setState(State::Waiting);
    m_timer.start(m_intervalMs);
}

int DeviceAuthorization::remainingSeconds() const
{
    if (m_state != State::Waiting) return 0;
    return int(std::max<qint64>(0, m_lifetimeMs - m_clock.elapsed()) / 1000);
}

void DeviceAuthorization::poll()
{
    if (m_state != State::Waiting) return;
    if (m_clock.elapsed() >= m_lifetimeMs) return fail(tr("The code expired. Start again to get a new code."));
    const QPointer<DeviceAuthorization> self(this);
    m_cancelRequest = m_post(m_recipe.tokenUrl,
        form({{QStringLiteral("grant_type"), DeviceGrant}, {QStringLiteral("device_code"), QString::fromLatin1(m_deviceCode)},
              {QStringLiteral("client_id"), m_recipe.clientId}}),
        [self](int status, const QByteArray& body) { if (self) self->polled(status, body); });
}

void DeviceAuthorization::polled(int status, const QByteArray& body)
{
    m_cancelRequest = {};
    if (m_state != State::Waiting) return;
    if (const auto result = tokens(status, body)) {
        m_deviceCode.clear();
        m_timer.stop();
        setState(State::Connected);
        emit connected(*result);
        return;
    }
    const auto error = object(body).value(QStringLiteral("error")).toString();
    // A transport failure is retried: the user may still be signing in.
    if (status == 0 || error == QLatin1String("authorization_pending")) { m_timer.start(m_intervalMs); return; }
    if (error == QLatin1String("slow_down")) { m_intervalMs = std::min(m_intervalMs + 5000, 60000); m_timer.start(m_intervalMs); emit changed(); return; }
    if (error == QLatin1String("access_denied")) return fail(tr("The sign-in was declined."));
    if (error == QLatin1String("expired_token")) return fail(tr("The code expired. Start again to get a new code."));
    fail(tr("The sign-in service refused the request."));
}

void DeviceAuthorization::cancel()
{
    if (m_cancelRequest) { auto cancelRequest = std::move(m_cancelRequest); m_cancelRequest = {}; cancelRequest(); }
    m_timer.stop();
    m_deviceCode.clear();
    if (m_state == State::Requesting || m_state == State::Waiting) setState(State::Canceled);
}

std::optional<DeviceTokens> DeviceAuthorization::tokens(int status, const QByteArray& body)
{
    if (status != 200) return std::nullopt;
    const auto reply = object(body);
    DeviceTokens result;
    if (!token(reply.value(QStringLiteral("access_token")), &result.accessToken)) return std::nullopt;
    const auto type = reply.value(QStringLiteral("token_type"));
    if (!type.isString() || type.toString().compare(QLatin1String("bearer"), Qt::CaseInsensitive) != 0) return std::nullopt;
    const auto refresh = reply.value(QStringLiteral("refresh_token"));
    if (!refresh.isUndefined() && !token(refresh, &result.refreshToken)) return std::nullopt;
    const auto expires = reply.value(QStringLiteral("expires_in"));
    if (!expires.isUndefined()) {
        if (!expires.isDouble() || expires.toDouble() < 0 || expires.toDouble() > 10.0 * 365 * 86400) return std::nullopt;
        result.expiresIn = qint64(expires.toDouble());
    }
    return result;
}

std::function<void()> DeviceAuthorization::refresh(const DeviceRecipe& recipe, const QByteArray& refreshToken,
                                                    const DevicePost& post, std::function<void(std::optional<DeviceTokens>)> done)
{
    if (refreshToken.isEmpty()) { QTimer::singleShot(0, [done] { done(std::nullopt); }); return [] {}; }
    return post(recipe.tokenUrl,
        form({{QStringLiteral("grant_type"), QStringLiteral("refresh_token")},
              {QStringLiteral("refresh_token"), QString::fromLatin1(refreshToken)},
              {QStringLiteral("client_id"), recipe.clientId}}),
        [done, refreshToken](int status, const QByteArray& body) {
            auto result = tokens(status, body);
            // A server can keep the refresh token unchanged.
            if (result && result->refreshToken.isEmpty()) result->refreshToken = refreshToken;
            done(result);
        });
}
}
