// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_session_websocket.hpp"
#include <QCryptographicHash>
#include <QEventLoop>
#include <QFile>
#include <QProcess>
#include <QScopeGuard>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace vlc::addons;
namespace {
using Wss=AddonSessionWebSocket;
struct Wait {
    std::mutex mutex; std::condition_variable changed; bool released=false;
    std::atomic_bool entered{false};
    void wait() { std::unique_lock<std::mutex> lock(mutex); entered=true; changed.wait(lock,[&] { return released; }); }
    void release() { { std::lock_guard<std::mutex> lock(mutex); released=true; } changed.notify_all(); }
};
std::vector<AddonStreamDns::Address> publicAddress() {
    AddonStreamDns::Address value; value.family=4; value.bytes[0]=93; value.bytes[1]=184; value.bytes[2]=216; value.bytes[3]=34; return {value};
}
struct Seen {
    std::mutex mutex; QList<Wss::Event> events; bool revokedBeforeTerminal=true;
    unsigned count(Wss::Kind kind) { std::lock_guard<std::mutex> lock(mutex); unsigned n=0; for (const auto& e:events) n+=e.kind==kind; return n; }
    QByteArray text() { std::lock_guard<std::mutex> lock(mutex); for (const auto& e:events) if(e.kind==Wss::Kind::Text) return e.bytes; return {}; }
};
struct Relay : QTcpServer {
    QSslCertificate certificate; QSslKey key;
    std::function<QByteArray(const QByteArray&)> upgrade;
    std::function<void(const QByteArray&)> record;
    QByteArray first;
    void incomingConnection(qintptr descriptor) override {
        auto* socket=new QSslSocket(this); if (!socket->setSocketDescriptor(descriptor)) { delete socket; return; }
        socket->setLocalCertificate(certificate); socket->setPrivateKey(key); socket->setPeerVerifyMode(QSslSocket::VerifyNone);
        const auto data=std::make_shared<QByteArray>(); const auto ready=std::make_shared<bool>(false);
        connect(socket,&QSslSocket::readyRead,socket,[this,socket,data,ready] {
            if (*ready) { const auto value=socket->readAll(); if (!value.isEmpty()) record(value); return; }
            *data+=socket->read(16385-data->size()); if (data->size()>16384) { socket->abort(); return; }
            if (!data->contains("\r\n\r\n")) return;
            *ready=true; record(*data); socket->write(upgrade(*data)+first);
        });
        connect(socket,&QSslSocket::disconnected,socket,&QObject::deleteLater); socket->startServerEncryption();
    }
};
struct Server {
    struct Shared {
        std::mutex mutex; std::condition_variable changed; unsigned port=0; QList<QByteArray> received;
        std::atomic_bool stop{false};
    };
    std::shared_ptr<Shared> shared=std::make_shared<Shared>(); std::thread thread;
    Server(const QSslCertificate& certificate,const QSslKey& key,QByteArray first={},int mode=0) {
        thread=std::thread([value=shared,certificate,key,first,mode] {
            QEventLoop loop; Relay relay; relay.certificate=certificate; relay.key=key; relay.first=first;
            relay.record=[value](const auto& bytes) { std::lock_guard<std::mutex> lock(value->mutex); value->received.append(bytes); };
            relay.upgrade=[mode](const QByteArray& request)->QByteArray {
                if (mode==1) return QByteArray("HTTP/1.1 302 Found\r\nLocation: wss://other.example/room\r\nContent-Length: 0\r\n\r\n");
                QByteArray key;
                for (const auto& line:request.split('\n')) if (line.toLower().startsWith("sec-websocket-key:")) key=line.mid(line.indexOf(':')+1).trimmed();
                const QByteArray input=key+"258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
                const QByteArray accept=QCryptographicHash::hash(input,QCryptographicHash::Sha1).toBase64();
                return QByteArray("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ")+accept+"\r\n\r\n";
            };
            relay.listen(QHostAddress::LocalHost,0);
            { std::lock_guard<std::mutex> lock(value->mutex); value->port=relay.serverPort(); } value->changed.notify_all();
            QTimer timer; timer.setInterval(10); QObject::connect(&timer,&QTimer::timeout,&loop,[&] { if (value->stop.load()) loop.quit(); }); timer.start(); loop.exec();
        });
        std::unique_lock<std::mutex> lock(shared->mutex); shared->changed.wait_for(lock,std::chrono::seconds(3),[&] { return shared->port; });
    }
    ~Server() { shared->stop=true; thread.join(); }
    unsigned port() const { return shared->port; }
    QList<QByteArray> received() { std::lock_guard<std::mutex> lock(shared->mutex); return shared->received; }
};
}
class TestAddonSessionWebSocket : public QObject {
    Q_OBJECT
    QTemporaryDir files;
    QSslCertificate certificate; QSslKey key;
    Wss::Spec spec(AddonStreamLifetime& lifetime,std::shared_ptr<std::atomic_uint> revoked) {
        Wss::Spec value; value.url=QUrl(QStringLiteral("wss://relay.example/room"));
        value.declaredOrigins={QStringLiteral("wss://relay.example")}; value.grantedOrigins=value.declaredOrigins;
        value.live=[] { return true; }; value.authorize=[] { return true; }; value.nativeCancel=[revoked] { ++*revoked; }; value.lifetime=lifetime.handle(); return value;
    }
    Wss::Testing fixture(Server& server) {
        Wss::Testing value; value.authorities={certificate}; value.resolve=[](const std::string&) { return publicAddress(); };
        const auto port=server.port(); value.connect=[port](QSslSocket* socket,const auto& plan) { socket->connectToHostEncrypted(QStringLiteral("127.0.0.1"),quint16(port),plan.peerName); }; return value;
    }
    Wss::Events events(std::shared_ptr<Seen> seen,std::shared_ptr<std::atomic_uint> revoked) {
        return [seen,revoked](Wss::Event value) { std::lock_guard<std::mutex> lock(seen->mutex);
            if (value.kind==Wss::Kind::Closed || value.kind==Wss::Kind::Error) seen->revokedBeforeTerminal&=revoked->load()>0;
            seen->events.append(std::move(value)); };
    }
private slots:
    void initTestCase() {
        const auto cert=files.filePath(QStringLiteral("certificate.pem")),secret=files.filePath(QStringLiteral("private.pem"));
        QProcess openssl; openssl.start(QStringLiteral("openssl"),{QStringLiteral("req"),QStringLiteral("-x509"),QStringLiteral("-newkey"),QStringLiteral("rsa:2048"),QStringLiteral("-nodes"),
            QStringLiteral("-keyout"),secret,QStringLiteral("-out"),cert,QStringLiteral("-days"),QStringLiteral("2"),QStringLiteral("-subj"),QStringLiteral("/CN=relay.example"),
            QStringLiteral("-addext"),QStringLiteral("subjectAltName=DNS:relay.example")});
        QVERIFY(openssl.waitForFinished(10000)); QCOMPARE(openssl.exitCode(),0);
        QFile certFile(cert),keyFile(secret); QVERIFY(certFile.open(QIODevice::ReadOnly)); QVERIFY(keyFile.open(QIODevice::ReadOnly));
        certificate=QSslCertificate(certFile.readAll()); key=QSslKey(keyFile.readAll(),QSsl::Rsa); QVERIFY(!certificate.isNull()); QVERIFY(!key.isNull());
    }
    void cleanup() {
        QTRY_COMPARE_WITH_TIMEOUT(Wss::processCount(),0u,5000);
        QTRY_COMPARE_WITH_TIMEOUT(AddonStreamLifetime::processQtWorkCount(),0u,5000);
        QTRY_COMPARE_WITH_TIMEOUT(AddonStreamLifetime::processNetworkCount(),0u,5000);
        QTRY_COMPARE_WITH_TIMEOUT(AddonStreamLifetime::processPlainCount(),0u,5000);
    }
    void exactOriginsAndSecurePortAreRequired() {
        AddonStreamLifetime owner; auto source=spec(owner,std::make_shared<std::atomic_uint>(0)); QVERIFY(Wss::permitted(source));
        source.grantedOrigins.clear(); QVERIFY(!Wss::permitted(source)); source.grantedOrigins=source.declaredOrigins;
        source.url=QUrl(QStringLiteral("wss://relay.example:8443/room")); QVERIFY(!Wss::permitted(source));
        source.url=QUrl(QStringLiteral("ws://relay.example/room")); QVERIFY(!Wss::permitted(source)); owner.close();
    }
    void tlsHandshakeReceivesTextAndClientFramesAreMasked() {
        AddonStreamLifetime owner; const auto revoked=std::make_shared<std::atomic_uint>(0); const auto seen=std::make_shared<Seen>();
        Server relay(certificate,key,QByteArray::fromHex("810568656c6c6f"));
        auto transport=Wss::openForTesting(spec(owner,revoked),fixture(relay),events(seen,revoked)); QVERIFY(transport);
        const auto close=qScopeGuard([&] { transport->cancel(); owner.close(); owner.handle().waitQt(std::chrono::seconds(5)); });
        QTRY_COMPARE(seen->count(Wss::Kind::Ready),1u); QTRY_COMPARE(seen->text(),QByteArray("hello")); QVERIFY(transport->send(true,"chat"));
        QTRY_VERIFY(relay.received().size()>=2); const auto frame=relay.received().last(); QVERIFY(frame.size()>=10);
        QCOMPARE(quint8(frame[0]),quint8(0x81)); QVERIFY(quint8(frame[1])&0x80); QByteArray decoded;
        for (int i=0;i<4;++i) decoded.append(char(quint8(frame[6+i])^quint8(frame[2+i%4]))); QCOMPARE(decoded,QByteArray("chat"));
        transport->cancel(); QVERIFY(transport->waitStopped(3000)); QCOMPARE(revoked->load(),1u);
    }
    void redirectsAndMaskedServerFramesFailClosed_data() {
        QTest::addColumn<int>("mode"); QTest::addColumn<QByteArray>("first");
        QTest::newRow("redirect")<<1<<QByteArray();
        QTest::newRow("masked-server")<<0<<QByteArray::fromHex("81810000000061");
    }
    void redirectsAndMaskedServerFramesFailClosed() {
        QFETCH(int,mode); QFETCH(QByteArray,first); AddonStreamLifetime owner;
        const auto revoked=std::make_shared<std::atomic_uint>(0); const auto seen=std::make_shared<Seen>(); Server relay(certificate,key,first,mode);
        auto transport=Wss::openForTesting(spec(owner,revoked),fixture(relay),events(seen,revoked)); QVERIFY(transport);
        const auto close=qScopeGuard([&] { transport->cancel(); owner.close(); owner.handle().waitQt(std::chrono::seconds(5)); });
        QTRY_COMPARE(seen->count(Wss::Kind::Error),1u); QVERIFY(!transport->active()); QCOMPARE(revoked->load(),1u); QVERIFY(seen->revokedBeforeTerminal);
    }
    void fullAuthorizationFailureRevokesBeforeOutputWithoutAQtEvent() {
        AddonStreamLifetime owner; const auto saved=std::make_shared<std::atomic_bool>(true);
        const auto revoked=std::make_shared<std::atomic_uint>(0); const auto seen=std::make_shared<Seen>(); Server relay(certificate,key);
        auto source=spec(owner,revoked); source.authorize=[saved] { return saved->load(); };
        auto transport=Wss::openForTesting(std::move(source),fixture(relay),events(seen,revoked)); QVERIFY(transport);
        const auto close=qScopeGuard([&] { transport->cancel(); owner.close(); owner.handle().waitQt(std::chrono::seconds(5)); });
        QTRY_COMPARE(seen->count(Wss::Kind::Ready),1u); *saved=false; QVERIFY(transport->send(true,"refused"));
        QVERIFY(transport->waitStopped(3000)); QCOMPARE(revoked->load(),1u); QVERIFY(seen->revokedBeforeTerminal); QCOMPARE(relay.received().size(),1);
    }
    void reservedCredentialHeadersNeverReachTheRelay() {
        AddonStreamLifetime owner; const auto revoked=std::make_shared<std::atomic_uint>(0); const auto seen=std::make_shared<Seen>(); Server relay(certificate,key);
        auto source=spec(owner,revoked); source.credentials=[] { return QList<Wss::Header>{{"Host","private.example"}}; };
        auto transport=Wss::openForTesting(std::move(source),fixture(relay),events(seen,revoked)); QVERIFY(transport);
        const auto close=qScopeGuard([&] { transport->cancel(); owner.close(); owner.handle().waitQt(std::chrono::seconds(5)); });
        QTRY_COMPARE(seen->count(Wss::Kind::Error),1u); QVERIFY(relay.received().isEmpty()); QCOMPARE(revoked->load(),1u);
    }
    void privateOrMixedDnsAnswersFailBeforeTheConnectCallback() {
        AddonStreamLifetime owner; const auto revoked=std::make_shared<std::atomic_uint>(0); const auto seen=std::make_shared<Seen>();
        std::atomic_uint connects{0}; Wss::Testing testing; testing.authorities={certificate};
        testing.resolve=[](const std::string&) { auto values=publicAddress(); values[0].bytes[0]=127; return values; };
        testing.connect=[&](auto*,const auto&) { ++connects; };
        auto transport=Wss::openForTesting(spec(owner,revoked),std::move(testing),events(seen,revoked)); QVERIFY(transport);
        const auto close=qScopeGuard([&] { transport->cancel(); owner.close(); owner.handle().waitQt(std::chrono::seconds(5)); });
        QTRY_COMPARE(seen->count(Wss::Kind::Error),1u); QCOMPARE(connects.load(),0u);
    }
    void blockedNativeDnsRetainsTheSlotAfterNetworkJoin() {
        AddonStreamLifetime owner; const auto wait=std::make_shared<Wait>(); const auto revoked=std::make_shared<std::atomic_uint>(0); const auto seen=std::make_shared<Seen>();
        Wss::Testing testing; testing.resolve=[wait](const std::string&) { wait->wait(); return publicAddress(); };
        auto transport=Wss::openForTesting(spec(owner,revoked),std::move(testing),events(seen,revoked)); QVERIFY(transport);
        const auto close=qScopeGuard([&] { wait->release(); transport->cancel(); owner.close(); owner.handle().waitQt(std::chrono::seconds(5)); });
        QTRY_VERIFY(wait->entered.load()); transport->cancel(); QVERIFY(transport->waitStopped(3000));
        QCOMPARE(Wss::processCount(),1u); QCOMPARE(AddonStreamLifetime::processPlainCount(),1u); QCOMPARE(revoked->load(),1u);
        wait->release(); QTRY_COMPARE(Wss::processCount(),0u);
    }
    void outputQueuesStayBoundedBeforeConnection() {
        AddonStreamLifetime owner; const auto wait=std::make_shared<Wait>(); const auto revoked=std::make_shared<std::atomic_uint>(0); const auto seen=std::make_shared<Seen>();
        Wss::Testing testing; testing.resolve=[wait](const std::string&) { wait->wait(); return publicAddress(); };
        auto transport=Wss::openForTesting(spec(owner,revoked),std::move(testing),events(seen,revoked)); QVERIFY(transport);
        const auto close=qScopeGuard([&] { wait->release(); transport->cancel(); owner.close(); owner.handle().waitQt(std::chrono::seconds(5)); });
        QTRY_VERIFY(wait->entered.load()); const QByteArray bytes(65536,'x');
        for (unsigned i=0;i<4;++i) QVERIFY(transport->send(false,bytes)); QVERIFY(!transport->send(false,"x"));
        QVERIFY(!transport->send(false,QByteArray(65537,'x')));
    }
    void tlsTrustAndOriginalHostnameAreRequired_data() {
        QTest::addColumn<bool>("wrongHost");
        QTest::newRow("untrusted-certificate")<<false;
        QTest::newRow("different-original-hostname")<<true;
    }
    void tlsTrustAndOriginalHostnameAreRequired() {
        QFETCH(bool,wrongHost); AddonStreamLifetime owner; const auto revoked=std::make_shared<std::atomic_uint>(0);
        const auto seen=std::make_shared<Seen>(); Server relay(certificate,key); auto source=spec(owner,revoked); auto testing=fixture(relay);
        if(wrongHost) {
            source.url=QUrl(QStringLiteral("wss://different.example/room"));
            source.declaredOrigins=source.grantedOrigins={QStringLiteral("wss://different.example")};
        } else testing.authorities.clear();
        auto transport=Wss::openForTesting(std::move(source),std::move(testing),events(seen,revoked)); QVERIFY(transport);
        const auto close=qScopeGuard([&] { transport->cancel(); owner.close(); owner.handle().waitQt(std::chrono::seconds(5)); });
        QTRY_COMPARE(seen->count(Wss::Kind::Error),1u); QCOMPARE(seen->count(Wss::Kind::Ready),0u);
        QCOMPARE(revoked->load(),1u); QVERIFY(relay.received().isEmpty());
    }
    void inputEventRateHasABoundedBudget() {
        AddonStreamLifetime owner; const auto revoked=std::make_shared<std::atomic_uint>(0); const auto seen=std::make_shared<Seen>();
        QByteArray frames; for(unsigned i=0;i<129;++i) frames+=QByteArray::fromHex("8a00"); Server relay(certificate,key,frames);
        auto transport=Wss::openForTesting(spec(owner,revoked),fixture(relay),events(seen,revoked)); QVERIFY(transport);
        const auto close=qScopeGuard([&] { transport->cancel(); owner.close(); owner.handle().waitQt(std::chrono::seconds(5)); });
        QTRY_COMPARE(seen->count(Wss::Kind::Error),1u); QCOMPARE(revoked->load(),1u); QVERIFY(!transport->active());
    }
    void heldStoppedTransportReleasesQtUsingCapturesAfterTheJoin() {
        AddonStreamLifetime owner; const auto revoked=std::make_shared<std::atomic_uint>(0); Server relay(certificate,key);
        const auto seen=std::make_shared<Seen>(); auto held=std::make_shared<int>(1); std::weak_ptr<int> weak=held;
        auto source=spec(owner,revoked); source.authorize=[held] { return *held==1; };
        auto receive=events(seen,revoked);
        auto transport=Wss::openForTesting(std::move(source),fixture(relay),[held,receive](auto event) { receive(std::move(event)); });
        QVERIFY(transport); held.reset(); QTRY_COMPARE(seen->count(Wss::Kind::Ready),1u); transport->cancel();
        QVERIFY(transport->waitStopped(3000)); QTRY_VERIFY(weak.expired()); QVERIFY(!transport->active());
        owner.close(); QVERIFY(owner.handle().waitQt(std::chrono::seconds(5)));
    }
};
QTEST_GUILESS_MAIN(TestAddonSessionWebSocket)
#include "test_addon_session_websocket.moc"
