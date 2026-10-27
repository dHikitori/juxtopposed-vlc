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
    static QByteArray mpd(const QByteArray& body)
    {
        return QByteArray("<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\" type=\"static\" profiles=\"urn:mpeg:dash:profile:isoff-main:2011\" minBufferTime=\"PT1S\" mediaPresentationDuration=\"PT2S\"><Period><AdaptationSet>")
            + body + "</AdaptationSet></Period></MPD>";
    }
    Parser::Result dash(const QByteArray& bytes)
    {
        resources.clear(); batches = 0;
        return Parser::dash(bytes, upstream, prefix, [this](const QList<Parser::Resource>& values) {
            ++batches; resources = values; QStringList result;
            for (qsizetype i = 0; i < values.size(); ++i) result.append(prefix.toString() + QStringLiteral("resource_") + QString::number(i));
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
    void dashListsResolveEachRepresentationBeforePublication()
    {
        const auto result = dash(mpd("<BaseURL>../video/</BaseURL><Representation id=\"one\" bandwidth=\"5000\"><BaseURL>first/</BaseURL>"
            "<SegmentList timescale=\"1\" duration=\"1\"><Initialization sourceURL=\"init.mp4?key=secret\"/>"
            "<SegmentURL media=\"one.m4s\"/><SegmentURL media=\"https://other.example.org/two.m4s\"/></SegmentList></Representation>"
            "<Representation id=\"two\" bandwidth=\"9000\"><BaseURL>second/</BaseURL><SegmentList duration=\"2\"><SegmentURL media=\"all.m4s\"/></SegmentList></Representation>"));
        QVERIFY2(result.valid(), qPrintable(result.error)); QCOMPARE(batches, 1u); QCOMPARE(resources.size(), 4);
        QCOMPARE(resources[0].url, QUrl(QStringLiteral("https://media.example.org/video/first/init.mp4?key=secret")));
        QCOMPARE(resources[3].url, QUrl(QStringLiteral("https://media.example.org/video/second/all.m4s")));
        QCOMPARE(resources[0].kind, Parser::Kind::Initialization);
        QVERIFY(!result.bytes.contains("secret")); QVERIFY(!result.bytes.contains("example.org")); QVERIFY(!result.bytes.contains("<BaseURL>"));
        QVERIFY(result.bytes.contains("resource_3")); QVERIFY(result.bytes.contains("timescale=\"1\""));
    }
    void dashFileRangesKeepTheOriginalByteCoordinates()
    {
        const auto result = dash(mpd("<Representation id=\"one\" bandwidth=\"5000\"><BaseURL>movie.mp4?key=secret</BaseURL>"
            "<SegmentBase indexRange=\"100-199\" indexRangeExact=\"true\"><Initialization range=\"0-99\"/></SegmentBase></Representation>"));
        QVERIFY2(result.valid(), qPrintable(result.error)); QCOMPARE(resources.size(), 2); QVERIFY(!resources[0].range); QVERIFY(resources[1].range);
        QCOMPARE(resources[1].range->offset, quint64(0)); QCOMPARE(resources[1].range->length, quint64(100));
        QCOMPARE(resources[0].url, resources[1].url); QVERIFY(!result.bytes.contains("secret"));
        QVERIFY(result.bytes.contains("<BaseURL>http://127.0.0.1:9000/session_token/resource_0</BaseURL>"));
        QVERIFY(result.bytes.contains("sourceURL=\"http://127.0.0.1:9000/session_token/resource_1\""));
        QVERIFY(result.bytes.contains("indexRange=\"100-199\""));
    }
    void dashTimelineAndSegmentRangesHaveFiniteBounds()
    {
        const auto valid = "<Representation><SegmentList timescale=\"1000\"><SegmentTimeline><S t=\"0\" d=\"1000\" r=\"1\"/></SegmentTimeline>"
            "<SegmentURL media=\"file.mp4\" mediaRange=\"100-199\"/><SegmentURL media=\"file.mp4\" mediaRange=\"200-299\"/></SegmentList></Representation>";
        QVERIFY(dash(mpd(valid)).valid()); QCOMPARE(resources[1].range->offset, quint64(200)); QCOMPARE(resources[1].range->length, quint64(100));
        for (const auto* repeat : {"-1", "512", "9223372036854775807"}) {
            QByteArray body(valid); const QByteArray replacement = QByteArray("r=\"") + repeat + "\"";
            body.replace("r=\"1\"", replacement); QVERIFY(!dash(mpd(body)).valid()); QCOMPARE(batches, 0u);
        }
        QByteArray overflow(valid); overflow.replace("d=\"1000\"", "d=\"9223372036854775807\""); QVERIFY(!dash(mpd(overflow)).valid()); QCOMPARE(batches, 0u);
        QByteArray reversed(valid); reversed.replace("200-299", "299-200"); QVERIFY(!dash(mpd(reversed)).valid()); QCOMPARE(batches, 0u);
    }
    void dashExternalFeaturesFailBeforeTheRegistryCall()
    {
        const QByteArray valid = "<Representation><SegmentList duration=\"1\"><SegmentURL media=\"one.m4s\"/></SegmentList></Representation>";
        for (const auto* extra : {"<Location>https://other.example.org/new.mpd</Location>", "<UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:http-xsdate:2014\" value=\"https://other.example.org/time\"/>",
                "<SegmentTemplate media=\"$Number$.m4s\"/>", "<ContentProtection/>", "<EventStream/>", "<Unknown href=\"https://other.example.org/\"/>"}) {
            QVERIFY(!dash(mpd(QByteArray(extra) + valid)).valid()); QCOMPARE(batches, 0u);
        }
        auto dynamic = mpd(valid); dynamic.replace("type=\"static\"", "type=\"dynamic\""); QVERIFY(!dash(dynamic).valid()); QCOMPARE(batches, 0u);
        auto linked = mpd(valid); linked.replace("<Period>", "<Period xmlns:xlink=\"http://www.w3.org/1999/xlink\" xlink:href=\"https://other.example.org/period\">");
        QVERIFY(!dash(linked).valid()); QCOMPARE(batches, 0u);
        auto dtd = QByteArray("<!DOCTYPE MPD [<!ENTITY leak SYSTEM 'file:///private/key'>]>") + mpd(valid); QVERIFY(!dash(dtd).valid()); QCOMPARE(batches, 0u);
        auto processing = QByteArray("<?load href='https://other.example.org/file'?>") + mpd(valid); QVERIFY(!dash(processing).valid()); QCOMPARE(batches, 0u);
    }
    void dashUnknownAttributesAndAmbiguousInheritanceAreRejected()
    {
        for (const auto* body : {"<SegmentList duration=\"1\"><SegmentURL media=\"one.m4s\"/></SegmentList><Representation/>",
                "<Representation><BaseURL>a/</BaseURL><BaseURL>b/</BaseURL><SegmentList duration=\"1\"><SegmentURL media=\"one.m4s\"/></SegmentList></Representation>",
                "<Representation><SegmentList duration=\"1\"><SegmentURL media=\"one.m4s\" index=\"https://other.example.org/index\"/></SegmentList></Representation>",
                "<Representation><SegmentBase/></Representation>", "<Representation><SegmentList><SegmentURL media=\"one.m4s\"/></SegmentList></Representation>",
                "<Representation><SegmentList duration=\"1\"><SegmentURL mediaRange=\"0-99\"/></SegmentList></Representation>",
                "<Representation><BaseURL>directory/</BaseURL><SegmentBase/></Representation>",
                "<Representation><SegmentList duration=\"1\" timescale=\"0\"><SegmentURL media=\"one.m4s\"/></SegmentList></Representation>"}) {
            QVERIFY(!dash(mpd(body)).valid()); QCOMPARE(batches, 0u);
        }
    }
    void dashResourceAndDocumentLimitsPrecedePublication()
    {
        QByteArray list = "<Representation><SegmentList duration=\"1\">";
        for (int i = 0; i < 513; ++i) list += "<SegmentURL media=\"one.m4s\"/>";
        list += "</SegmentList></Representation>"; QVERIFY(!dash(mpd(list)).valid()); QCOMPARE(batches, 0u);
        QVERIFY(!dash(QByteArray(1024 * 1024 + 1, 'x')).valid()); QCOMPARE(batches, 0u);
        auto foreign = mpd("<Representation/>"); foreign.replace("urn:mpeg:dash:schema:mpd:2011", "https://other.example.org/schema");
        QVERIFY(!dash(foreign).valid()); QCOMPARE(batches, 0u);
        auto bad = mpd("<Representation/>"); bad.insert(10, char(0)); QVERIFY(!dash(bad).valid()); QCOMPARE(batches, 0u);
    }
    void dashRegistryResultsCannotEscapeTheCapturedProxyScope()
    {
        const auto body = mpd("<Representation><SegmentList duration=\"1\"><SegmentURL media=\"one.m4s\"/></SegmentList></Representation>");
        for (const auto* route : {"https://other.example.org/media", "http://127.0.0.1:9000/other/file", "http://127.0.0.1:9000/session_token/../escape", "http://127.0.0.1:9000/session_token/file?url=remote"}) {
            QVERIFY(!Parser::dash(body, upstream, prefix, [route](const auto&) { return QStringList{QString::fromLatin1(route)}; }).valid());
        }
        QVERIFY(!Parser::dash(body, upstream, prefix, [](const auto&) { return QStringList{}; }).valid());
    }
    void anEmptyLiveMediaPlaylistDoesNotNeedAResource()
    {
        const auto result = parse("#EXTM3U\n#EXT-X-TARGETDURATION:10\n#EXT-X-MEDIA-SEQUENCE:0\n");
        QVERIFY2(result.valid(), qPrintable(result.error)); QVERIFY(resources.isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestAddonStreamManifest)
#include "test_addon_stream_manifest.moc"
