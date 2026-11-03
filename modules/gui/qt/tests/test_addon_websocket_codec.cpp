// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include "addons/addon_websocket_codec.hpp"
using namespace vlc::addons;
namespace {
using Codec = AddonWebSocketCodec;
const QByteArray key("dGhlIHNhbXBsZSBub25jZQ==");
const QByteArray response("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n\r\n");
QByteArray server(quint8 opcode, const QByteArray& payload, bool final = true)
{
    QByteArray bytes; bytes.append(char(opcode | (final ? 0x80 : 0)));
    if (payload.size() < 126) bytes.append(char(payload.size()));
    else if (payload.size() <= 65535) { bytes.append(char(126)); bytes.append(char(payload.size() >> 8)); bytes.append(char(payload.size())); }
    else { bytes.append(char(127)); for (int i = 7; i >= 0; --i) bytes.append(char(quint64(payload.size()) >> (i * 8))); }
    bytes += payload; return bytes;
}
QByteArray unpack(const QByteArray& frame)
{
    if (frame.size() < 6 || !(quint8(frame[1]) & 0x80)) return {};
    qsizetype header = 2; quint64 length = quint8(frame[1]) & 0x7f;
    if (length == 126) { if (frame.size() < 8) return {}; length = (quint64(quint8(frame[2])) << 8) | quint8(frame[3]); header = 4; }
    else if (length == 127) { if (frame.size() < 14) return {}; length = 0; for (int i = 2; i < 10; ++i) length = (length << 8) | quint8(frame[i]); header = 10; }
    if (length > quint64(Codec::MaximumMessage) || frame.size() != header + 4 + qsizetype(length)) return {};
    QByteArray result; for (qsizetype i = 0; i < qsizetype(length); ++i) result.append(char(quint8(frame[header + 4 + i]) ^ quint8(frame[header + i % 4])));
    return result;
}
}
class TestAddonWebSocketCodec : public QObject
{
    Q_OBJECT
    static void open(Codec& codec) { QVERIFY(codec.receive(response).error.isEmpty()); QCOMPARE(codec.state(), Codec::State::Open); }
private slots:
    void handshakeUsesOnlyWssAndCapturedRequestFields()
    {
        const auto request = Codec::handshake(QUrl(QStringLiteral("wss://relay.example.org/room?token=test")), {QStringLiteral("sync.v1")}); QVERIFY(request);
        QVERIFY(request->bytes.startsWith("GET /room?token=test HTTP/1.1\r\nHost: relay.example.org\r\n"));
        QCOMPARE(QByteArray::fromBase64(request->key).size(), 16); QVERIFY(request->bytes.contains("Sec-WebSocket-Protocol: sync.v1\r\n"));
        QVERIFY(!request->bytes.contains("Cookie:")); QVERIFY(!request->bytes.contains("Authorization:"));
        for (const auto* url : {"ws://relay.example.org/", "https://relay.example.org/", "wss://user:pass@relay.example.org/", "wss://@relay.example.org/", "wss://relay.example.org/#fragment", "wss://relay.example.org:8443/"})
            QVERIFY(!Codec::handshake(QUrl(QString::fromLatin1(url))));
        QVERIFY(!Codec::handshake(QUrl(QStringLiteral("wss://relay.example.org/")), {QStringLiteral("bad\r\nheader")}));
        QVERIFY(!Codec::handshake(QUrl(QStringLiteral("wss://relay.example.org/")), {QStringLiteral("same"), QStringLiteral("same")}));
    }
    void partialUpgradeAndCoalescedFrameUseTheKnownAcceptVector()
    {
        Codec codec(key); QCOMPARE(codec.state(), Codec::State::Handshake);
        for (qsizetype i = 0; i + 1 < response.size(); ++i) { const auto batch = codec.receive(response.mid(i, 1)); QVERIFY(batch.error.isEmpty()); QVERIFY(batch.events.isEmpty()); }
        const QByteArray last = response.right(1) + server(1, "hello"); const auto batch = codec.receive(last);
        QCOMPARE(codec.state(), Codec::State::Open); QCOMPARE(batch.events.size(), 1); QCOMPARE(batch.events[0].kind, Codec::Kind::Text); QCOMPARE(batch.events[0].bytes, QByteArray("hello"));
    }
    void invalidOrAmbiguousUpgradeNeverOpens()
    {
        QList<QByteArray> bad;
        for (const auto& pair : QList<QPair<QByteArray,QByteArray>>{{"101 Switching Protocols", "200 OK"}, {"HTTP/1.1", "HTTP/1.0"},
                {"s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "wrong"}, {"Connection: Upgrade", "Connection: keep-alive"}, {"Connection: Upgrade", "Connection: Upgrade,"}, {"Upgrade: websocket", "Upgrade: h2c"}}) {
            auto bytes = response; bytes.replace(pair.first, pair.second); bad.append(bytes);
        }
        for (const auto* header : {"Sec-WebSocket-Accept: duplicate\r\n", "Transfer-Encoding: chunked\r\n", "Content-Length: 2\r\n",
                "Sec-WebSocket-Extensions: permessage-deflate\r\n", "Sec-WebSocket-Protocol: other\r\n", " folded: yes\r\n"}) {
            auto bytes = response; bytes.insert(bytes.size()-2, header); bad.append(bytes);
        }
        for (const auto& bytes : bad) { Codec codec(key); QVERIFY(!codec.receive(bytes).error.isEmpty()); QCOMPARE(codec.state(), Codec::State::Failed); QVERIFY(!codec.send(true, "x")); }
        Codec malformed("not a nonce"); QCOMPARE(malformed.state(), Codec::State::Failed);
    }
    void subprotocolMustHaveBeenOfferedExactly()
    {
        auto bytes = response; bytes.insert(bytes.size()-2, "Sec-WebSocket-Protocol: sync.v1\r\n");
        Codec allowed(key, {QStringLiteral("sync.v1")}); QVERIFY(allowed.receive(bytes).error.isEmpty()); QCOMPARE(allowed.protocol(), QStringLiteral("sync.v1"));
        Codec absent(key); QVERIFY(!absent.receive(bytes).error.isEmpty());
        Codec other(key, {QStringLiteral("SYNC.v1")}); QVERIFY(!other.receive(bytes).error.isEmpty());
    }
    void clientMessagesAreMaskedWithFreshHostMasks()
    {
        quint8 generation = 0; Codec codec(key, {}, [&] { return Codec::Mask{++generation, 2, 3, 4}; }); open(codec);
        for (const auto count : {0, 5, 125, 126, 65535, 65536}) {
            const QByteArray value(count, 'a'); const auto bytes = codec.send(false, value); QVERIFY(bytes); QCOMPARE(quint8((*bytes)[0]), quint8(0x82));
            QVERIFY(quint8((*bytes)[1]) & 0x80); QCOMPARE(unpack(*bytes), value);
        }
        QVERIFY(!codec.send(false, QByteArray(Codec::MaximumMessage+1, 'a'))); QCOMPARE(generation, quint8(6));
    }
    void textValidationRunsOnCompleteMessages()
    {
        Codec codec(key); open(codec); QVERIFY(!codec.send(true, QByteArray::fromHex("c0af"))); QVERIFY(codec.send(false, QByteArray::fromHex("c0af")));
        const auto first = codec.receive(server(1, QByteArray::fromHex("e282"), false)); QVERIFY(first.error.isEmpty()); QVERIFY(first.events.isEmpty());
        const auto last = codec.receive(server(0, QByteArray::fromHex("ac"))); QVERIFY(last.error.isEmpty()); QCOMPARE(last.events.size(), 1); QCOMPARE(last.events[0].bytes, QByteArray::fromHex("e282ac"));
        Codec invalid(key); open(invalid); QVERIFY(!invalid.receive(server(1, QByteArray::fromHex("eda080"))).error.isEmpty());
    }
    void controlFramesCanInterruptAFragmentedMessage()
    {
        Codec codec(key); open(codec); QVERIFY(codec.receive(server(1, "one", false)).events.isEmpty());
        const auto ping = codec.receive(server(9, "check")); QVERIFY(ping.error.isEmpty()); QCOMPARE(ping.replies.size(), 1); QCOMPARE(unpack(ping.replies[0]), QByteArray("check")); QCOMPARE(quint8(ping.replies[0][0]), quint8(0x8a));
        const auto done = codec.receive(server(0, "two")); QCOMPARE(done.events.size(), 1); QCOMPARE(done.events[0].bytes, QByteArray("onetwo"));
        const auto pong = codec.receive(server(10, "check")); QCOMPARE(pong.events[0].kind, Codec::Kind::Pong);
    }
    void completeTextMustNotEndInsideACodePoint()
    {
        for (const auto* hex : {"c2", "e282", "f09f92"}) {
            const auto bytes = QByteArray::fromHex(hex); Codec codec(key); open(codec);
            QVERIFY(!codec.send(true, bytes));
            QVERIFY(!codec.receive(server(1, bytes)).error.isEmpty());
        }
    }
    void serverMaskReservedBitsOpcodesAndNonminimalLengthsFail()
    {
        QList<QByteArray> frames{QByteArray::fromHex("818000000000"), QByteArray::fromHex("c100"), QByteArray::fromHex("817e0000"), QByteArray::fromHex("817f0000000000000001"), QByteArray::fromHex("827f8000000000000000")};
        for (quint8 opcode : {3,4,5,6,7,11,12,13,14,15}) frames.append(QByteArray(1,char(0x80|opcode))+char(0));
        for (const auto& frame : frames) { Codec codec(key); open(codec); QVERIFY(!codec.receive(frame).error.isEmpty()); }
    }
    void oversizedAndFragmentedControlsFail()
    {
        for (const auto& bytes : {server(9, "x", false), server(8, QByteArray(126,'x')), server(10,QByteArray(126,'x'))}) {
            Codec codec(key); open(codec); QVERIFY(!codec.receive(bytes).error.isEmpty());
        }
    }
    void continuationRulesAndAssemblyLimitsAreEnforced()
    {
        Codec missing(key); open(missing); QVERIFY(!missing.receive(server(0,"x")).error.isEmpty());
        Codec interleave(key); open(interleave); QVERIFY(interleave.receive(server(1,"x",false)).error.isEmpty()); QVERIFY(!interleave.receive(server(2,"y")).error.isEmpty());
        Codec count(key); open(count); QVERIFY(count.receive(server(1,"",false)).error.isEmpty());
        for (unsigned i=0;i<15;++i) QVERIFY(count.receive(server(0,"",false)).error.isEmpty());
        QVERIFY(!count.receive(server(0,"")).error.isEmpty());
        Codec limit(key); open(limit); QVERIFY(limit.receive(server(2,QByteArray(32768,'x'),false)).error.isEmpty());
        QVERIFY(!limit.receive(server(0,QByteArray(40000,'y'))).error.isEmpty());
    }
    void extendedServerFramesAndPartialPayloadsRemainBounded()
    {
        for (const auto size : {126,65536}) {
            Codec codec(key); open(codec); const QByteArray value(size,'z'); const auto wire=server(2,value);
            const auto first=codec.receive(wire.left(7)); QVERIFY(first.error.isEmpty()); QVERIFY(first.events.isEmpty());
            const auto last=codec.receive(wire.mid(7)); QVERIFY(last.error.isEmpty()); QCOMPARE(last.events.size(),1); QCOMPARE(last.events[0].bytes,value);
        }
        Codec declared(key); open(declared); QVERIFY(!declared.receive(QByteArray::fromHex("827f0000000000010001")).error.isEmpty());
        Codec wire(key); open(wire); QVERIFY(!wire.receive(QByteArray(Codec::MaximumWire+1,'x')).error.isEmpty());
        Codec header(key); QVERIFY(header.receive(QByteArray(Codec::MaximumHeader-1,'x')).error.isEmpty()); QVERIFY(!header.receive("x").error.isEmpty());
    }
    void onePumpCannotProcessAnUnboundedFrameBurst()
    {
        Codec codec(key); open(codec); QByteArray wire; for (int i=0;i<100;++i) wire+=server(1,"");
        auto batch=codec.receive(wire); QCOMPARE(batch.events.size(),Codec::MaximumBatch); QVERIFY(batch.more); qsizetype total=batch.events.size();
        while(batch.more) { batch=codec.receive({}); QVERIFY(batch.events.size()<=Codec::MaximumBatch); total+=batch.events.size(); }
        QCOMPARE(total,qsizetype(100));
    }
    void peerCloseIsEchoedOnceAndCannotReopenTheChannel()
    {
        Codec codec(key); open(codec); const QByteArray payload=QByteArray::fromHex("03e8")+"done"; const auto batch=codec.receive(server(8,payload));
        QCOMPARE(batch.events.size(),1); QCOMPARE(batch.events[0].kind,Codec::Kind::Closed); QCOMPARE(batch.events[0].code,quint16(1000)); QCOMPARE(batch.events[0].bytes,QByteArray("done"));
        QCOMPARE(batch.replies.size(),1); QCOMPARE(unpack(batch.replies[0]),payload); QCOMPARE(codec.state(),Codec::State::Closed);
        QVERIFY(!codec.send(true,"x")); QVERIFY(codec.receive(server(1,"later")).events.isEmpty()); QVERIFY(codec.eof().events.isEmpty());
    }
    void aCompletedMessageCanWaitForTheNextBatch()
    {
        for (bool fragmented : {false, true}) {
            Codec codec(key); open(codec); QByteArray wire = server(1, "x");
            if (fragmented) { wire += server(2, QByteArray(65535, 'b'), false); wire += server(0, "b"); }
            else wire += server(2, QByteArray(65536, 'b'));
            const auto first = codec.receive(wire); QVERIFY(first.error.isEmpty()); QVERIFY(first.more);
            QCOMPARE(first.events.size(), 1); QCOMPARE(first.events[0].bytes, QByteArray("x"));
            const auto next = codec.receive({}); QVERIFY(next.error.isEmpty()); QVERIFY(!next.more);
            QCOMPARE(next.events.size(), 1); QCOMPARE(next.events[0].bytes, QByteArray(65536, 'b'));
        }
    }
    void invalidCloseCodesReasonsAndPayloadSizesFail()
    {
        for (const auto code : {999,1004,1005,1006,1015,2000,5000}) {
            Codec codec(key); open(codec); QByteArray payload;payload.append(char(code>>8));payload.append(char(code));
            QVERIFY(!codec.receive(server(8,payload)).error.isEmpty());
        }
        for (const auto& payload : {QByteArray("x"),QByteArray::fromHex("03e8c0af")}) { Codec codec(key);open(codec);QVERIFY(!codec.receive(server(8,payload)).error.isEmpty()); }
    }
    void localCloseAndAbruptEofHaveDistinctTerminalResults()
    {
        Codec codec(key); open(codec); const auto close=codec.close(1000,"done");QVERIFY(close);QCOMPARE(codec.state(),Codec::State::Closing);
        QVERIFY(!codec.send(true,"later"));QVERIFY(!codec.close());const auto peer=codec.receive(server(8,QByteArray::fromHex("03e8")));QVERIFY(peer.replies.isEmpty());QCOMPARE(codec.state(),Codec::State::Closed);
        Codec eof(key);open(eof);eof.receive(server(1,"partial",false));const auto ended=eof.eof();QCOMPARE(ended.events.size(),1);QCOMPARE(ended.events[0].code,quint16(1006));QVERIFY(!ended.error.isEmpty());QCOMPARE(eof.state(),Codec::State::Failed);
        Codec pending(key);QVERIFY(!pending.eof().error.isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestAddonWebSocketCodec)
#include "test_addon_websocket_codec.moc"
