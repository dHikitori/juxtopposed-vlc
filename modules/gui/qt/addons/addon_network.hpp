// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_manifest.hpp"
#include <QObject>
#include <QNetworkAccessManager>
#include <QHash>
#include <QHostAddress>
#include <functional>
#include <memory>

namespace vlc::addons {

class NetworkBroker : public QObject
{
    Q_OBJECT
public:
    struct Response { int status = 0; QByteArray body; QJsonObject headers; QString error; };
    using Callback = std::function<void(const Response&)>;
    using CredentialResolver = std::function<QByteArray(const QString& field)>;
    explicit NetworkBroker(QObject* parent = nullptr);
    quint64 fetch(const Manifest&, const QJsonObject& grants, const QJsonObject& request,
                  CredentialResolver, Callback, qint64 maximumBytes = 512 * 1024);
    void cancel(quint64 job);
    void cancelPackage(const QString& packageId);
    static bool publicAddress(const QHostAddress&);
    static bool permittedUrl(const Manifest&, const QJsonObject& grants, const QUrl&);
private:
    struct Job;
    void begin(const std::shared_ptr<Job>&);
    void resolved(const std::shared_ptr<Job>&, const QList<QHostAddress>&);
    void finish(const std::shared_ptr<Job>&, const QString& error = {});
    QNetworkAccessManager m_network;
    QHash<quint64, std::shared_ptr<Job>> m_jobs;
    quint64 m_sequence = 0;
};
}
