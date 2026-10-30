// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QByteArray>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <functional>
#include <optional>

class QNetworkAccessManager;

namespace vlc::addons {

// The host-owned account recipe for the OAuth 2.0 device authorization grant
// (RFC 8628). A settings field declares it:
//
//   "x-hakone-secret": true, "x-hakone-control": "account",
//   "x-hakone-account": {"recipe": "oauth-device",
//     "deviceAuthorizationUrl": "https://...", "tokenUrl": "https://...",
//     "clientId": "...", "scopes": ["..."]}
//
// The access token becomes the field's credential, so the ordinary
// credentials.use binding injects it. The add-on never sees a token, a code
// or a server reply.
struct DeviceRecipe
{
    QUrl deviceAuthorizationUrl;
    QUrl tokenUrl;
    QString clientId;
    QStringList scopes;
    // networkOrigins are the package's declared network.http origins. Both
    // endpoints must be among them, so the review shows them.
    static std::optional<DeviceRecipe> from(const QJsonObject& annotation, const QStringList& networkOrigins,
                                            QString* error = nullptr);
};

struct DeviceTokens
{
    QByteArray accessToken;
    QByteArray refreshToken;
    // Seconds from the reply; 0 when the server did not say.
    qint64 expiresIn = 0;
};

// One HTTPS POST with a form body. The reply callback receives the HTTP
// status (0 for a transport failure) and at most 64 KiB of body. The
// returned function cancels the request; after it, reply is not called.
using DevicePost = std::function<std::function<void()>(const QUrl&, const QByteArray& form,
                                                      std::function<void(int status, const QByteArray& body)> reply)>;
// The production transport: no redirects, no cookies, 15 s limit.
DevicePost devicePost(QNetworkAccessManager*);

class DeviceAuthorization final : public QObject
{
    Q_OBJECT
public:
    enum class State { Idle, Requesting, Waiting, Connected, Failed, Canceled };
    Q_ENUM(State)
    static constexpr int MaxReplyBytes = 64 * 1024;
    static constexpr int MaxLifetimeSeconds = 1800;

    DeviceAuthorization(DeviceRecipe, DevicePost, QObject* parent = nullptr);
    ~DeviceAuthorization() override;

    void start();
    void cancel();
    State state() const { return m_state; }
    QString userCode() const { return m_userCode; }
    QUrl verificationUrl() const { return m_verification; }
    // The page with the code filled in, when the server supplies one.
    QUrl completeVerificationUrl() const { return m_complete.isEmpty() ? m_verification : m_complete; }
    int remainingSeconds() const;
    QString message() const { return m_message; }
    int pollIntervalMs() const { return m_intervalMs; }

    // Exchanges a refresh token once. done runs on this thread, or never
    // when the returned cancel function runs first.
    static std::function<void()> refresh(const DeviceRecipe&, const QByteArray& refreshToken, const DevicePost&,
                                         std::function<void(std::optional<DeviceTokens>)> done);
    // A token reply: 200 with a bearer access token. Messages from the
    // server are never shown.
    static std::optional<DeviceTokens> tokens(int status, const QByteArray& body);

signals:
    void changed();
    void connected(const vlc::addons::DeviceTokens& tokens);

private:
    void authorized(int status, const QByteArray& body);
    void poll();
    void polled(int status, const QByteArray& body);
    void fail(const QString& message);
    void setState(State);

    const DeviceRecipe m_recipe;
    const DevicePost m_post;
    State m_state = State::Idle;
    QByteArray m_deviceCode;
    QString m_userCode, m_message;
    QUrl m_verification, m_complete;
    int m_intervalMs = 5000;
    qint64 m_lifetimeMs = 0;
    QElapsedTimer m_clock;
    QTimer m_timer;
    std::function<void()> m_cancelRequest;
};
}
Q_DECLARE_METATYPE(vlc::addons::DeviceTokens)
