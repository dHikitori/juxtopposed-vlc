// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addons/addon_session_websocket.hpp"
#include <QCryptographicHash>
#include <QEventLoop>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>
#include <QTimer>
#include <condition_variable>
#include <thread>

namespace session_test {
inline std::vector<vlc::addons::AddonStreamDns::Address> publicAddress() {
    vlc::addons::AddonStreamDns::Address value; value.family=4;
    value.bytes[0]=93; value.bytes[1]=184; value.bytes[2]=216; value.bytes[3]=34; return {value};
}
class Relay final : public QTcpServer {
public:
    QSslCertificate certificate; QSslKey key;
    std::function<QByteArray(const QByteArray&)> upgrade;
    std::function<void(const QByteArray&)> record;
    QByteArray first;
    void incomingConnection(qintptr descriptor) override {
        auto* socket=new QSslSocket(this);
        if(!socket->setSocketDescriptor(descriptor)) { delete socket; return; }
        socket->setLocalCertificate(certificate); socket->setPrivateKey(key);
        socket->setPeerVerifyMode(QSslSocket::VerifyNone);
        const auto data=std::make_shared<QByteArray>(); const auto ready=std::make_shared<bool>(false);
        connect(socket,&QSslSocket::readyRead,socket,[this,socket,data,ready] {
            if(*ready) { const auto value=socket->readAll(); if(!value.isEmpty()) record(value); return; }
            *data+=socket->read(16385-data->size()); if(data->size()>16384) { socket->abort(); return; }
            if(!data->contains("\r\n\r\n")) return;
            *ready=true; record(*data); socket->write(upgrade(*data)+first);
        });
        connect(socket,&QSslSocket::disconnected,socket,&QObject::deleteLater); socket->startServerEncryption();
    }
};
class Server {
    struct Shared {
        std::mutex mutex; std::condition_variable changed; unsigned port=0;
        QList<QByteArray> received; std::atomic_bool stop{false};
    };
    std::shared_ptr<Shared> shared=std::make_shared<Shared>(); std::thread thread;
public:
    Server(const QSslCertificate& certificate,const QSslKey& key,QByteArray first={},int mode=0) {
        thread=std::thread([value=shared,certificate,key,first,mode] {
            QEventLoop loop; Relay relay; relay.certificate=certificate; relay.key=key; relay.first=first;
            relay.record=[value](const auto& bytes) { std::lock_guard<std::mutex> lock(value->mutex); value->received.append(bytes); };
            relay.upgrade=[mode](const QByteArray& request)->QByteArray {
                if(mode==1) return QByteArray("HTTP/1.1 302 Found\r\nLocation: wss://other.example/room\r\nContent-Length: 0\r\n\r\n");
                QByteArray key;
                for(const auto& line:request.split('\n')) if(line.toLower().startsWith("sec-websocket-key:")) key=line.mid(line.indexOf(':')+1).trimmed();
                const QByteArray input=key+"258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
                const QByteArray accept=QCryptographicHash::hash(input,QCryptographicHash::Sha1).toBase64();
                return QByteArray("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ")+accept+"\r\n\r\n";
            };
            relay.listen(QHostAddress::LocalHost,0);
            { std::lock_guard<std::mutex> lock(value->mutex); value->port=relay.serverPort(); } value->changed.notify_all();
            QTimer timer; timer.setInterval(10);
            QObject::connect(&timer,&QTimer::timeout,&loop,[&] { if(value->stop.load()) loop.quit(); });
            timer.start(); loop.exec();
        });
        std::unique_lock<std::mutex> lock(shared->mutex);
        shared->changed.wait_for(lock,std::chrono::seconds(3),[&] { return shared->port; });
    }
    ~Server() { shared->stop=true; thread.join(); }
    unsigned port() const { return shared->port; }
    QList<QByteArray> received() { std::lock_guard<std::mutex> lock(shared->mutex); return shared->received; }
    vlc::addons::AddonSessionWebSocket::Testing fixture(const QSslCertificate& certificate) const {
        vlc::addons::AddonSessionWebSocket::Testing value; value.authorities={certificate};
        value.resolve=[](const std::string&) { return publicAddress(); };
        const auto port=this->port();
        value.connect=[port](QSslSocket* socket,const auto& plan) {
            socket->connectToHostEncrypted(QStringLiteral("127.0.0.1"),quint16(port),plan.peerName);
        };
        return value;
    }
};
}
