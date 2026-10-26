// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_manifest.hpp"
#include <QObject>
#include <QNetworkAccessManager>
#include <QHash>
#include <QHostAddress>
#include <QIODevice>
#include <functional>
#include <memory>
#include <mutex>

namespace vlc::addons {

// One origin policy applies to all packages and broker instances in the app.
class OriginRequestPacer
{
public:
    struct Admission { bool valid = false; qint64 delayMs = 0; };
    bool enqueue(quint64 ticket, const QString& origin, int minimumIntervalMs, qint64 now);
    Admission admit(quint64 ticket, qint64 now);
    void release(quint64 ticket, qint64 now);
private:
    struct Origin { qint64 lastStart = -1; int lastInterval = 0; QHash<quint64, int> active; };
    std::mutex m_lock;
    QHash<QString, Origin> m_origins;
    QHash<quint64, QString> m_tickets;
};

class NetworkBroker : public QObject
{
    Q_OBJECT
public:
    enum class RequestKind { Document, BinaryAsset, ImageAsset };
    struct Authorization { bool permitted = false; bool stripCredentials = false; };
    enum class BodyRead { More, PrefixComplete, TooLarge };
    struct Response { int status = 0; QByteArray body; QJsonObject headers; QString error; bool truncated = false; };
    using Callback = std::function<void(const Response&)>;
    using CredentialResolver = std::function<QByteArray(const QString& field)>;
    using RequestGuard = std::function<bool()>;
    explicit NetworkBroker(QObject* parent = nullptr);
    ~NetworkBroker() override;
    quint64 fetch(const Manifest&, const QJsonObject& grants, const QJsonObject& request,
                  CredentialResolver, Callback, qint64 maximumBytes = 512 * 1024);
    quint64 fetch(const Manifest&, const QJsonObject& grants, const QJsonObject& request,
                  CredentialResolver, Callback, qint64 maximumBytes, RequestKind kind);
    quint64 fetch(const Manifest&, const QJsonObject& grants, const QJsonObject& request,
                  CredentialResolver, Callback, qint64 maximumBytes, RequestKind kind, RequestGuard);
    void cancel(quint64 job);
    void cancelPackage(const QString& packageId);
    static bool publicAddress(const QHostAddress&);
    static bool permittedUrl(const Manifest&, const QJsonObject& grants, const QUrl&);
    static bool permittedImageRedirect(const Manifest&, const QJsonObject& grants, const QUrl&);
    static Authorization authorize(const Manifest&, const QJsonObject& grants, const QUrl&, RequestKind, int redirects);
    static bool textPrefixContentType(const QString&);
    static BodyRead readBody(QIODevice*, QByteArray&, qint64 limit, bool prefix);
private:
    struct Job;
    void begin(const std::shared_ptr<Job>&);
    void resolved(const std::shared_ptr<Job>&, const QList<QHostAddress>&);
    bool readResponse(const std::shared_ptr<Job>&);
    void responseHeaders(const std::shared_ptr<Job>&);
    void finish(const std::shared_ptr<Job>&, const QString& error = {});
    QNetworkAccessManager m_network;
    QHash<quint64, std::shared_ptr<Job>> m_jobs;
    quint64 m_sequence = 0;
};
}
