// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QSemaphore>
#include <QScopeGuard>
#include <atomic>
#include <thread>
#include "addons/addon_stream_http.hpp"
#include "addons/addon_stream_policy.hpp"
#include "addons/addon_stream_sockets.hpp"
using namespace vlc::addons;
class TestAddonStreamHttp : public QObject
{
    Q_OBJECT
private slots:
    void exactHostMethodAndConditionalRange()
    {
        AddonStreamHttp::Request value;
        QVERIFY(AddonStreamHttp::request("GET /token/main HTTP/1.1\r\nHost: 127.0.0.1:1234\r\nRange: bytes=10-19\r\nIf-Range: \"version-one\"\r\n\r\n","127.0.0.1:1234",&value));
        QCOMPARE(value.path,QByteArray("/token/main")); QCOMPARE(value.range->first,quint64(10)); QCOMPARE(*value.range->last,quint64(19));
        QVERIFY(AddonStreamHttp::request("HEAD /token/main HTTP/1.1\r\nHost: 127.0.0.1:1234\r\nIf-Match: \"version-one\"\r\nContent-Length: 0\r\n\r\n","127.0.0.1:1234",&value));
        QCOMPARE(value.ifMatch,QByteArray("\"version-one\""));
    }
    void unsafeRequestsAreRefused_data()
    {
        QTest::addColumn<QByteArray>("input");
        QTest::newRow("connect") << QByteArray("CONNECT example.org:443 HTTP/1.1\r\nHost: 127.0.0.1:1234\r\n\r\n");
        QTest::newRow("foreign-host") << QByteArray("GET /token/main HTTP/1.1\r\nHost: example.org\r\n\r\n");
        for (const auto& name : QList<QByteArray>{"Origin","Cookie","Authorization","Proxy-Authorization","Referer","Transfer-Encoding"}) {
            const QByteArray value = QByteArray("GET /token/main HTTP/1.1\r\nHost: 127.0.0.1:1234\r\n") + name + ": bad\r\n\r\n";
            QTest::newRow(name.constData()) << value;
        }
        QTest::newRow("query") << QByteArray("GET /token/main?url=other HTTP/1.1\r\nHost: 127.0.0.1:1234\r\n\r\n");
        QTest::newRow("multiple-range") << QByteArray("GET /token/main HTTP/1.1\r\nHost: 127.0.0.1:1234\r\nRange: bytes=1-2,3-4\r\n\r\n");
        QTest::newRow("suffix-range") << QByteArray("GET /token/main HTTP/1.1\r\nHost: 127.0.0.1:1234\r\nRange: bytes=-5\r\n\r\n");
        QTest::newRow("weak-if-range") << QByteArray("GET /token/main HTTP/1.1\r\nHost: 127.0.0.1:1234\r\nRange: bytes=1-\r\nIf-Range: W/\"tag\"\r\n\r\n");
        QTest::newRow("body") << QByteArray("GET /token/main HTTP/1.1\r\nHost: 127.0.0.1:1234\r\nContent-Length: 1\r\n\r\n");
    }
    void unsafeRequestsAreRefused()
    { QFETCH(QByteArray,input); AddonStreamHttp::Request value; QVERIFY(!AddonStreamHttp::request(input,"127.0.0.1:1234",&value)); }
    void responseFramingAndRangesAreConsistent()
    {
        AddonStreamHttp::Response value;
        QVERIFY(AddonStreamHttp::response("HTTP/1.1 206 Partial\r\nContent-Length: 10\r\nContent-Range: bytes 10-19/100\r\nContent-Type: video/mp4\r\n\r\n",&value));
        QCOMPARE(*value.length,quint64(10)); QCOMPARE(*value.total,quint64(100));
        for (const auto& fields : QList<QByteArray>{"Content-Length: 10\r\nContent-Length: 10\r\n","Content-Length: 10\r\nTransfer-Encoding: chunked\r\n",
             "Transfer-Encoding: gzip, chunked\r\n","Content-Encoding: gzip\r\n","Content-Range: bytes 10-19/19\r\n","Content-Length: -1\r\n"})
            QVERIFY(!AddonStreamHttp::response("HTTP/1.1 200 OK\r\n"+fields+"\r\n",&value));
        QVERIFY(!AddonStreamHttp::response("HTTP/1.1 206 Partial\r\nContent-Length: 9\r\nContent-Range: bytes 10-19/100\r\n\r\n",&value));
    }
    void chunksAcceptFragmentationAndRefuseExtraBytes()
    {
        const QByteArray input("3\r\nabc\r\n2;part=yes\r\nde\r\n0\r\nX-Result: yes\r\n\r\n");
        AddonStreamHttp::Chunks reader; QByteArray all,part;
        for (const auto byte : input) { QVERIFY(reader.feed(QByteArray(1,byte),&part)); all += part; }
        QCOMPARE(all,QByteArray("abcde")); QVERIFY(reader.done()); QVERIFY(!reader.feed("extra",&part));
        AddonStreamHttp::Chunks large; QVERIFY(!large.feed(QByteArray(65537,'a'),&part));
        AddonStreamHttp::Chunks invalid; QVERIFY(!invalid.feed("xyz\r\n",&part));
        AddonStreamHttp::Chunks overflow; QVERIFY(!overflow.feed("ffffffffffffffff\r\n",&part));
    }
    void publicDnsAndOriginalTlsNameAreBoundToOnePlan()
    {
        const QUrl url(QStringLiteral("https://media.example/a%20file.mp4?token=synthetic"));
        const auto plan = AddonStreamPolicy::plan(url,{QHostAddress(QStringLiteral("93.184.216.34"))}); QVERIFY(plan);
        QCOMPARE(plan->peerName,QStringLiteral("media.example")); QCOMPARE(plan->address,QHostAddress(QStringLiteral("93.184.216.34")));
        QCOMPARE(plan->target,QByteArray("/a%20file.mp4?token=synthetic"));
        QVERIFY(!AddonStreamPolicy::plan(url,{plan->address,QHostAddress::LocalHost}));
        for (const QString& address : QStringList{"0.0.0.0","10.0.0.1","100.64.1.1","169.254.1.2","192.0.2.1","192.88.99.2","224.1.1.1","::1","::ffff:8.8.8.8","2001:db8::1","3fff::1","3fff:fff:ffff::1"})
            QVERIFY(!AddonStreamPolicy::publicAddress(QHostAddress(address)));
        QHostAddress scoped(QStringLiteral("2001:4860:4860::8888")); scoped.setScopeId(QStringLiteral("net0"));
        QVERIFY(!AddonStreamPolicy::publicAddress(scoped));
        for (const QString& address : QStringList{"http://media.example/file","https://media.example:8443/file","https://user@media.example/file","https://media.example/a%0afile","https://media.example/file#other"})
            QVERIFY(!AddonStreamPolicy::url(QUrl(address)));
    }
    void nativeSocketCancellationWakesAReadWithoutOwnerEvents()
    {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost,0)); const auto port=server.serverPort();
        QSemaphore reading,done; std::atomic_bool connected{false},woke{false};
        std::thread input([&] { QTcpSocket socket; socket.connectToHost(QHostAddress::LocalHost,port); connected=socket.waitForConnected(2000);
            reading.release(); if (connected) { socket.waitForReadyRead(5000); woke=socket.state()==QAbstractSocket::UnconnectedState; } done.release(); });
        const auto join=qScopeGuard([&] { if (input.joinable()) input.join(); });
        QVERIFY(reading.tryAcquire(1,3000)); QTRY_VERIFY(server.hasPendingConnections()); auto* peer=server.nextPendingConnection(); QVERIFY(peer);
        AddonStreamSockets sockets; const auto token=sockets.watch(peer->socketDescriptor()); QVERIFY(token); sockets.cancel();
        const bool completed=done.tryAcquire(1,2000); if (!completed) peer->abort(); input.join();
        QVERIFY(completed); QVERIFY(woke.load()); QVERIFY(!sockets.active()); peer->abort(); sockets.release(token); delete peer;
    }
};
QTEST_GUILESS_MAIN(TestAddonStreamHttp)
#include "test_addon_stream_http.moc"
