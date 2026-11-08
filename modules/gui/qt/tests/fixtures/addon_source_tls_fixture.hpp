// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QEventLoop>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTimer>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "addons/addon_stream_proxy.hpp"

// This fixture has no production endpoint override. Only the private test
// friend installs its synthetic DNS, TLS authority, and loopback connection.
struct AddonSourceTlsFixture {
    struct Server:QTcpServer {
        QSslCertificate certificate; QSslKey key; QByteArray body;
        std::atomic_uint* requests=nullptr;
        void incomingConnection(qintptr descriptor) override {
            auto* socket=new QSslSocket(this);
            if(!socket->setSocketDescriptor(descriptor)) { delete socket; return; }
            socket->setLocalCertificate(certificate); socket->setPrivateKey(key); socket->setPeerVerifyMode(QSslSocket::VerifyNone);
            const auto bytes=std::make_shared<QByteArray>(); const auto replied=std::make_shared<bool>(false);
            connect(socket,&QSslSocket::readyRead,socket,[this,socket,bytes,replied] {
                if(*replied) return; *bytes+=socket->read(8193-bytes->size());
                if(bytes->size()>8192) { socket->abort(); return; }
                if(!bytes->contains("\r\n\r\n")) return; *replied=true; ++*requests;
                socket->write(QByteArray("HTTP/1.1 200 OK\r\nContent-Type: audio/wav\r\nContent-Length: ")+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);
                socket->disconnectFromHost();
            });
            connect(socket,&QSslSocket::disconnected,socket,&QObject::deleteLater); socket->startServerEncryption();
        }
    };
    QTemporaryDir files;
    QSslCertificate certificate; QSslKey key;
    std::thread server;
    std::mutex mutex; std::condition_variable changed;
    std::atomic_bool stopped{false}; std::atomic_uint requests{0};
    unsigned port=0;
    ~AddonSourceTlsFixture() { stopped=true; if(server.joinable()) server.join(); }
    static QByteArray wave() {
        QByteArray pcm(4*8000*2,'\0');
        for(qsizetype i=0;i<pcm.size();i+=2) { const qint16 sample=((i/2)%40<20)?4096:-4096; pcm[i]=char(sample&255); pcm[i+1]=char((sample>>8)&255); }
        QByteArray result("RIFF"); const auto number=[&](quint32 n,unsigned size) { for(unsigned i=0;i<size;++i) result.append(char((n>>(8*i))&255)); };
        number(quint32(pcm.size()+36),4); result+="WAVEfmt "; number(16,4); number(1,2); number(1,2); number(8000,4); number(16000,4); number(2,2); number(16,2);
        result+="data"; number(quint32(pcm.size()),4); result+=pcm; return result;
    }
    bool start() {
        const auto executable=QStandardPaths::findExecutable(QStringLiteral("openssl")); if(executable.isEmpty()) return false;
        const auto cert=files.filePath(QStringLiteral("certificate.pem")),secret=files.filePath(QStringLiteral("fixture-key.pem"));
        QProcess process; process.start(executable,{QStringLiteral("req"),QStringLiteral("-x509"),QStringLiteral("-newkey"),QStringLiteral("rsa:2048"),
            QStringLiteral("-nodes"),QStringLiteral("-days"),QStringLiteral("2"),QStringLiteral("-subj"),QStringLiteral("/CN=media.example.test"),
            QStringLiteral("-addext"),QStringLiteral("subjectAltName=DNS:media.example.test"),
            // A server certificate, not a CA: macOS trust evaluation refuses
            // a self-signed CA certificate as the TLS server's own.
            QStringLiteral("-addext"),QStringLiteral("basicConstraints=critical,CA:FALSE"),
            QStringLiteral("-addext"),QStringLiteral("keyUsage=critical,digitalSignature,keyEncipherment"),
            QStringLiteral("-addext"),QStringLiteral("extendedKeyUsage=serverAuth"),
            QStringLiteral("-keyout"),secret,QStringLiteral("-out"),cert});
        if(!process.waitForFinished(10000) || process.exitCode()!=0) return false;
        QFile certFile(cert),keyFile(secret); if(!certFile.open(QIODevice::ReadOnly) || !keyFile.open(QIODevice::ReadOnly)) return false;
        certificate=QSslCertificate(certFile.readAll()); key=QSslKey(keyFile.readAll(),QSsl::Rsa);
        if(certificate.isNull() || key.isNull()) return false;
        server=std::thread([this] {
            QEventLoop loop; Server value; value.certificate=certificate; value.key=key; value.body=wave(); value.requests=&requests;
            value.listen(QHostAddress::LocalHost,0);
            { std::lock_guard<std::mutex> lock(mutex); port=value.serverPort(); } changed.notify_all();
            QTimer timer; timer.setInterval(10); QObject::connect(&timer,&QTimer::timeout,&loop,[&] { if(stopped) loop.quit(); }); timer.start(); loop.exec();
        });
        std::unique_lock<std::mutex> lock(mutex); return changed.wait_for(lock,std::chrono::seconds(3),[&]{return port!=0;});
    }
    vlc::addons::AddonStreamProxy::Testing testing() const {
        vlc::addons::AddonStreamProxy::Testing value; value.authorities={certificate}; value.idleMs=1500;
        value.resolve=[](const QString&) { return QList<QHostAddress>{QHostAddress(QStringLiteral("93.184.216.34"))}; };
        value.connect=[port=port](QSslSocket* socket,const auto& plan) { socket->connectToHostEncrypted(QStringLiteral("127.0.0.1"),quint16(port),plan.peerName); };
        return value;
    }
};
