// SPDX-License-Identifier: GPL-2.0-or-later
#include <QtTest/QtTest>
#include "addons/addon_stream_manifest.hpp"
using namespace vlc::addons;
class TestAddonStreamManifest : public QObject
{
    Q_OBJECT
    using Parser = AddonStreamManifest;
    const QUrl upstream{QStringLiteral("https://media.example.org/live/master.m3u8?private=token")};
    const QUrl prefix{QStringLiteral("http://127.0.0.1:9000/session_token/")};
    QList<Parser::Resource> resources;
    unsigned batches = 0;
    Parser::Result parse(const QByteArray& bytes)
    {
        resources.clear(); batches = 0;
        return Parser::hls(bytes, upstream, prefix, [this](const QList<Parser::Resource>& values) {
            ++batches; resources = values; QStringList result;
            for (qsizetype i = 0; i < values.size(); ++i)
                result.append(prefix.toString() + QStringLiteral("resource_") + QString::number(i));
            return result;
        });
    }
private slots:
    void relativeMediaAndPrivateQueriesStayBehindOpaqueUrls()
    {
        const auto result = parse("#EXTM3U\r\n#EXT-X-TARGETDURATION:10\r\n#EXTINF:10,Title\r\n../video/one.ts?key=private\r\n#EXT-X-ENDLIST\r\n");
        QVERIFY2(result.valid(), qPrintable(result.error)); QCOMPARE(batches, 1u); QCOMPARE(resources.size(), 1);
        QCOMPARE(resources[0].url, QUrl(QStringLiteral("https://media.example.org/video/one.ts?key=private")));
        QCOMPARE(resources[0].kind, Parser::Kind::Media);
        QVERIFY(!result.bytes.contains("private")); QVERIFY(!result.bytes.contains("media.example.org"));
        QVERIFY(result.bytes.contains("http://127.0.0.1:9000/session_token/resource_0"));
    }
    void masterRenditionsAndSessionDataUseTheBatch()
    {
        const auto result = parse("#EXTM3U\n#EXT-X-VERSION:7\n"
            "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"audio\",NAME=\"A, B\",URI=\"audio/index.m3u8\"\n"
            "#EXT-X-I-FRAME-STREAM-INF:BANDWIDTH=8000,URI=\"iframe/index.m3u8\"\n"
            "#EXT-X-SESSION-DATA:DATA-ID=\"name\",URI=\"session.json\"\n"
            "#EXT-X-STREAM-INF:BANDWIDTH=400000,AUDIO=\"audio\"\nvideo/index.m3u8\n");
        QVERIFY2(result.valid(), qPrintable(result.error)); QCOMPARE(resources.size(), 4);
        QCOMPARE(resources[0].kind, Parser::Kind::Playlist); QCOMPARE(resources[2].kind, Parser::Kind::SessionData);
        QCOMPARE(resources[3].url, QUrl(QStringLiteral("https://media.example.org/live/video/index.m3u8")));
        QVERIFY(result.bytes.contains("NAME=\"A, B\"")); QVERIFY(!result.bytes.contains("index.m3u8"));
    }
    void encryptionKeysRemainExplicitResources()
    {
        const auto result = parse("#EXTM3U\n#EXT-X-TARGETDURATION:10\n"
            "#EXT-X-KEY:METHOD=AES-128,URI=\"keys/key\",KEYFORMAT=\"identity\",KEYFORMATVERSIONS=\"1\"\n"
            "#EXTINF:10,\nsegment.ts\n#EXT-X-KEY:METHOD=NONE\n#EXTINF:10,\nplain.ts\n");
        QVERIFY2(result.valid(), qPrintable(result.error)); QCOMPARE(resources.size(), 3);
        QCOMPARE(resources[0].kind, Parser::Kind::Key); QVERIFY(result.bytes.contains("METHOD=NONE"));
        QVERIFY(!parse("#EXTM3U\n#EXT-X-TARGETDURATION:10\n#EXT-X-KEY:METHOD=NONE,URI=\"key\"\n#EXTINF:10,\nsegment.ts\n").valid());
        QCOMPARE(batches, 0u);
    }
    void implicitSegmentOffsetsBecomeExplicit()
    {
        const auto result = parse("#EXTM3U\n#EXT-X-TARGETDURATION:10\n#EXT-X-VERSION:4\n"
            "#EXTINF:10,\n#EXT-X-BYTERANGE:100@20\nfile.ts\n"
            "#EXTINF:10,\n#EXT-X-BYTERANGE:50\nfile.ts\n");
        QVERIFY2(result.valid(), qPrintable(result.error)); QCOMPARE(resources.size(), 2);
        QVERIFY(resources[0].range && resources[1].range);
        QCOMPARE(resources[1].range->offset, quint64(120)); QCOMPARE(resources[1].range->length, quint64(50));
        QVERIFY(result.bytes.contains("#EXT-X-BYTERANGE:50@120"));
        QVERIFY(!parse("#EXTM3U\n#EXT-X-TARGETDURATION:10\n#EXTINF:10,\n#EXT-X-BYTERANGE:50\nfile.ts\n").valid());
        QCOMPARE(batches, 0u);
    }
    void initializationRangeAttributeOrderDoesNotChangeTheUri()
    {
        for (const auto& attributes : {QByteArray("URI=\"file.mp4\",BYTERANGE=\"50@0\""),
                QByteArray("BYTERANGE=\"50@0\",URI=\"file.mp4\"")}) {
            const auto result = parse(QByteArray("#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-TARGETDURATION:10\n#EXT-X-MAP:")
                + attributes + "\n#EXTINF:10,\nsegment.m4s\n");
            QVERIFY2(result.valid(), qPrintable(result.error)); QCOMPARE(resources.size(), 2);
            QCOMPARE(resources[0].kind, Parser::Kind::Initialization);
            QVERIFY(resources[0].range); QCOMPARE(resources[0].range->offset, quint64(0));
            QVERIFY(result.bytes.contains("BYTERANGE=\"50@0\"")); QVERIFY(!result.bytes.contains("file.mp4"));
        }
    }
    void unsupportedFeaturesAndUnknownUriTagsDoNotRegisterResources()
    {
        for (const auto& tag : {QByteArray("#EXT-X-DEFINE:NAME=\"x\",VALUE=\"path\""),
                QByteArray("#EXT-X-PART:DURATION=1,URI=\"part\""),
                QByteArray("#EXT-X-SERVER-CONTROL:CAN-BLOCK-RELOAD=YES"),
                QByteArray("#EXT-X-UNKNOWN:URI =\"secret\""),
                QByteArray("#EXT-X-KEY:METHOD=SAMPLE-AES,URI=\"key\"")}) {
            QVERIFY(!parse(QByteArray("#EXTM3U\n#EXT-X-TARGETDURATION:10\n") + tag + "\n#EXTINF:10,\nsegment.ts\n").valid());
            QCOMPARE(batches, 0u);
        }
    }
    void invalidUrlsNeverReachTheRegistry()
    {
        for (const auto& uri : {QByteArray("file:///private.ts"), QByteArray("http://media.example.org/segment.ts"),
                QByteArray("https://user:password@media.example.org/segment.ts"), QByteArray("https://media.example.org/segment.ts#x"),
                QByteArray("data:video/mp2t;base64,AA"), QByteArray("https://media.example.org/a b"), QByteArray("{$variable}/segment.ts")}) {
            QVERIFY(!parse(QByteArray("#EXTM3U\n#EXT-X-TARGETDURATION:10\n#EXTINF:10,\n") + uri + "\n").valid());
            QCOMPARE(batches, 0u);
        }
    }
    void malformedInputAndRangeOverflowHaveNoPartialBatch()
    {
        const QList<QByteArray> inputs{"#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\n",
            "#EXTM3U\n#EXT-X-TARGETDURATION:10\n#EXTINF:10,\n#EXT-X-BYTERANGE:2@9223372036854775807\nfile.ts\n",
            "#EXTM3U\n#EXT-X-TARGETDURATION:10\n#EXT-X-KEY:METHOD=AES-128,URI=\"a\",URI=\"b\"\n#EXTINF:10,\nsegment.ts\n",
            "#EXTM3U\n#EXT-X-TARGETDURATION:10\n#EXTINF:10,\nsegment.ts\n#EXT-X-STREAM-INF:BANDWIDTH=1\nnext.m3u8\n",
            QByteArray::fromHex("efbbbf") + "#EXTM3U\n", QByteArray("#EXTM3U\n") + char(0xff)};
        for (const auto& input : inputs) { QVERIFY(!parse(input).valid()); QCOMPARE(batches, 0u); }
    }
    void resourceCountAndByteLimitsApplyBeforeRegistration()
    {
        QByteArray body("#EXTM3U\n#EXT-X-TARGETDURATION:10\n");
        for (unsigned i = 0; i < 512; ++i) body += "#EXTINF:10,\nsegment.ts\n";
        QVERIFY(parse(body).valid()); QCOMPARE(resources.size(), 512);
        QVERIFY(!parse(body + "#EXTINF:10,\nlast.ts\n").valid()); QCOMPARE(batches, 0u);
        QVERIFY(!parse(QByteArray(1024 * 1024 + 1, 'x')).valid()); QCOMPARE(batches, 0u);
    }
    void controlCharactersCannotHideUnregisteredUriLines()
    {
        for (const char control : {'\r', '\t', char(0x1f), char(0x7f)}) {
            const QByteArray body = QByteArray("#EXTM3U\n#EXT-X-TARGETDURATION:10\n#comment") + control
                + "https://media.example.org/hidden.ts\n#EXTINF:10,\nsegment.ts\n";
            QVERIFY(!parse(body).valid()); QCOMPARE(batches, 0u);
        }
    }
    void registryOutputMustStayWithinTheExactProxyScope()
    {
        const QByteArray body("#EXTM3U\n#EXT-X-TARGETDURATION:10\n#EXTINF:10,\nsegment.ts\n");
        for (const auto& uri : {QStringLiteral("http://127.0.0.1:9001/session_token/resource"),
                QStringLiteral("http://127.0.0.1:9000/other/resource"),
                QStringLiteral("http://127.0.0.1:9000/session_token/../private"),
                QStringLiteral("http://127.0.0.1:9000/session_token/resource?x=1")}) {
            const auto result = Parser::hls(body, upstream, prefix, [uri](const auto&) { return QStringList{uri}; });
            QVERIFY(!result.valid()); QVERIFY(result.bytes.isEmpty());
        }
        QVERIFY(!Parser::hls(body, upstream, prefix, [](const auto&) { return QStringList{}; }).valid());
    }
    void anEmptyLiveMediaPlaylistDoesNotNeedAResource()
    {
        const auto result = parse("#EXTM3U\n#EXT-X-TARGETDURATION:10\n#EXT-X-MEDIA-SEQUENCE:0\n");
        QVERIFY2(result.valid(), qPrintable(result.error)); QVERIFY(resources.isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestAddonStreamManifest)
#include "test_addon_stream_manifest.moc"
