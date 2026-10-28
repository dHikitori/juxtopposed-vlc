// SPDX-License-Identifier: GPL-2.0-or-later
// Test TLS transport only. Production address admission runs before this route.
#pragma once
#include <QTcpServer>
#include <QSslSocket>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFile>
#include <QPointer>
#include <memory>
struct ReceiptTlsServer final : QTcpServer {
    QSslCertificate certificate;
    QSslKey key;
    QByteArray response;
    unsigned requests=0,handshakes=0;
    bool keepOpen=false;
    ReceiptTlsServer() {
        QFile cert(QStringLiteral(HAKONE_ATTEMPT_FIXTURE_DIR "/receipt-tls-cert.pem"));
        QFile privateKey(QStringLiteral(HAKONE_ATTEMPT_FIXTURE_DIR "/receipt-tls-key.pem"));
        if(cert.open(QIODevice::ReadOnly)) certificate=QSslCertificate(cert.readAll());
        if(privateKey.open(QIODevice::ReadOnly)) key=QSslKey(privateKey.readAll(),QSsl::Rsa);
    }
    bool start() { return !certificate.isNull() && !key.isNull() && listen(QHostAddress::LocalHost,0); }
    void incomingConnection(qintptr descriptor) override {
        auto* socket=new QSslSocket(this);
        socket->setLocalCertificate(certificate); socket->setPrivateKey(key); socket->setPeerVerifyMode(QSslSocket::VerifyNone);
        if(!socket->setSocketDescriptor(descriptor)) { delete socket; return; }
        auto request=std::make_shared<QByteArray>(); auto sent=std::make_shared<bool>(false);
        QObject::connect(socket,&QSslSocket::encrypted,this,[this] { ++handshakes; });
        QObject::connect(socket,&QSslSocket::readyRead,this,[this,socket,request,sent] {
            if(*sent) return;
            request->append(socket->readAll());
            if(request->size()>16384) { socket->abort(); return; }
            if(!request->contains("\r\n\r\n")) return;
            if(!request->contains("\r\nHost: 8.8.8.8\r\n") && !request->contains("\r\nhost: 8.8.8.8\r\n"))
                qFatal("The test Host field changed.");
            *sent=true; ++requests;
            if(!response.isEmpty()) socket->write(response);
            if(!keepOpen) socket->disconnectFromHost();
        });
        QObject::connect(socket,&QSslSocket::disconnected,socket,&QObject::deleteLater);
        socket->startServerEncryption();
    }
};
struct ReceiptTransportState { unsigned admitted=0,destroyed=0; };
class ReceiptRoutedNetwork final : public QNetworkAccessManager {
    QPointer<ReceiptTlsServer> server;
    std::shared_ptr<ReceiptTransportState> state;
public:
    ReceiptRoutedNetwork(ReceiptTlsServer& value,std::shared_ptr<ReceiptTransportState> observed)
        : server(&value),state(std::move(observed)) {}
    ~ReceiptRoutedNetwork() override { ++state->destroyed; }
protected:
    QNetworkReply* createRequest(Operation operation,const QNetworkRequest& request,QIODevice* body) override {
        if(!server || operation!=GetOperation || request.url().scheme()!=QLatin1String("https")
            || request.url().host()!=QLatin1String("8.8.8.8") || request.peerVerifyName()!=QLatin1String("8.8.8.8")
            || request.rawHeader("Host")!=QByteArray("8.8.8.8") || proxy().type()!=QNetworkProxy::NoProxy)
            qFatal("The test transport did not receive an admitted request.");
        ++state->admitted;
        auto routed=request; auto url=routed.url(); url.setHost(QStringLiteral("127.0.0.1")); url.setPort(server->serverPort()); routed.setUrl(url);
        auto tls=routed.sslConfiguration(); auto certificates=tls.caCertificates(); certificates.append(server->certificate);
        tls.setCaCertificates(certificates); routed.setSslConfiguration(tls);
        // Keep the original peer name and Host. Qt verifies the test IP SAN.
        return QNetworkAccessManager::createRequest(operation,routed,body);
    }
};
