// SPDX-License-Identifier: GPL-2.0-or-later
#include <QtTest/QtTest>
#include <QBuffer>
#include <QImageReader>
#include <QImageWriter>
#include <QtEndian>
#include <zlib.h>
#include <cstring>
#include "addons/addon_artwork_image.hpp"
#include "addon_artwork_test_images.hpp"

using vlc::addons::AddonArtworkImage;
using namespace NativeArtworkFixture;
namespace {
QByteArray image(const char* format = "PNG", int size = 2)
{
    QImage pixels(size, size, QImage::Format_RGB32); pixels.fill(Qt::red);
    QByteArray bytes; QBuffer output(&bytes); output.open(QIODevice::WriteOnly); pixels.save(&output, format); return bytes;
}
QByteArray chunk(const QByteArray& name, const QByteArray& data)
{
    QByteArray bytes(4, '\0'); qToBigEndian<quint32>(quint32(data.size()), bytes.data()); bytes += name; bytes += data;
    const auto crc = crc32(0, reinterpret_cast<const Bytef*>(bytes.constData() + 4), uInt(bytes.size() - 4));
    QByteArray checksum(4, '\0'); qToBigEndian<quint32>(quint32(crc), checksum.data()); return bytes + checksum;
}
QByteArray pngWith(const QByteArray& name, const QByteArray& payload)
{ auto bytes = image(); bytes.insert(33, chunk(name, payload)); return bytes; }
AddonArtworkImage::Limits nativeLimits() { return {32 * 1024 * 1024, 128 * 1024 * 1024, 16384}; }

}
class TestAddonArtworkImage : public QObject
{
    Q_OBJECT
private slots:
    void installedNativeFormatsMatchTheNativeReader_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        const auto readers = QImageReader::supportedImageFormats();
        const QList<QByteArray> baselineFormats{"bmp", "cur", "gif", "ico", "jfif", "jpeg", "jpg", "pbm", "pgm", "png", "ppm", "svg", "svgz", "webp", "xbm", "xpm"};
        for (const auto& codec : baselineFormats) {
            if (!readers.contains(codec)) continue;
            QByteArray bytes;
            if (codec == "svg") bytes = svgBytes();
            else if (codec == "svgz") bytes = compressedSvg();
            else if (codec == "gif") bytes = QByteArray::fromBase64("R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7");
            else bytes = patterned(codec);
            QVERIFY2(!bytes.isEmpty(), codec.constData());
            QTest::newRow(codec.constData()) << bytes;
        }
    }
    void installedNativeFormatsMatchTheNativeReader()
    {
        QFETCH(QByteArray, bytes);
        const auto expected = baseline(bytes); QVERIFY(!expected.isNull());
        QString error; const auto actual = AddonArtworkImage::decode(bytes, AddonArtworkImage::Profile::NativeArtwork, nativeLimits(), &error);
        QVERIFY2(!actual.isNull(), qPrintable(error + QStringLiteral(" Input: ") + QString::fromLatin1(bytes.left(256))));
        QCOMPARE(actual, expected); QVERIFY(actual.textKeys().isEmpty());
        const auto codec = AddonArtworkImage::format(bytes);
        if (codec != "png" && codec != "jpeg" && codec != "webp")
            QVERIFY(AddonArtworkImage::decode(bytes, AddonArtworkImage::Profile::StaticAddon, {}).isNull());
    }
    void nativeExifOrientationMatchesVisibleNativePixels()
    {
        for (unsigned orientation = 1; orientation <= 8; ++orientation)
            for (const bool big : {false, true}) {
                const auto bytes = exifJpeg(orientation, big, true);
                const auto expected = baseline(bytes); QVERIFY(!expected.isNull());
                const auto actual = AddonArtworkImage::decode(bytes, AddonArtworkImage::Profile::NativeArtwork, nativeLimits());
                QCOMPARE(actual, expected);
                QCOMPARE(AddonArtworkImage::decode(bytes, AddonArtworkImage::Profile::StaticAddon, {}), baseline(bytes, false));
            }
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    void nativeCmykJpegKeepsTheAdobeColorTransform()
    {
        QImage pixels(2, 1, QImage::Format_CMYK8888);
        const auto samples = QByteArray::fromHex("00ffff00ff000000");
        std::memcpy(pixels.scanLine(0), samples.constData(), size_t(samples.size()));
        QByteArray bytes; QBuffer output(&bytes); QVERIFY(output.open(QIODevice::WriteOnly));
        QImageWriter writer(&output, "jpeg"); writer.setQuality(100); QVERIFY(writer.write(pixels));
        QVERIFY(bytes.contains("Adobe")); const auto expected = baseline(bytes); QVERIFY(!expected.isNull());
        QCOMPARE(AddonArtworkImage::decode(bytes, AddonArtworkImage::Profile::NativeArtwork, nativeLimits()), expected);
    }
#endif
    void malformedExifCannotChangePixelOrientation()
    {
        for (const auto& change : {QPair<int, quint32>{16, 0xffffffff}, {16, 7}, {20, 513}}) {
            auto bytes = exifJpeg(6);
            if (change.first == 20) qToLittleEndian<quint16>(quint16(change.second), bytes.data() + change.first);
            else qToLittleEndian(change.second, bytes.data() + change.first);
            QCOMPARE(AddonArtworkImage::decode(bytes, AddonArtworkImage::Profile::NativeArtwork, nativeLimits()), baseline(bytes, false));
        }
    }
    void nativeRasterHeadersCannotExpandBeyondTheInputAndPixelLimits()
    {
        auto bmp = patterned("bmp"); QVERIFY(!bmp.isEmpty());
        for (const auto& invalid : {QPair<int, quint32>{18, 65535}, {22, 0x80000000}, {10, 0xffffffff}, {14, 0xffffffff}}) {
            auto bytes = bmp; qToLittleEndian(invalid.second, bytes.data() + invalid.first);
            QVERIFY(AddonArtworkImage::decode(bytes, AddonArtworkImage::Profile::NativeArtwork, nativeLimits()).isNull());
        }
        bmp.chop(1); QVERIFY(AddonArtworkImage::decode(bmp, AddonArtworkImage::Profile::NativeArtwork, nativeLimits()).isNull());
        for (const auto& bytes : {QByteArray("P6\n4294967295 2\n255\n"), QByteArray("P6\n2 2\n255\n\0", 13),
                QByteArray("/* XPM */\nstatic const char* x[]={\"2 2 4294967295 1\",\"a c red\",\"aa\",\"aa\"};"),
                QByteArray("#define x_width 4294967295\n#define x_height 2\nstatic unsigned char x_bits[]={0x00};")})
            QVERIFY(AddonArtworkImage::decode(bytes, AddonArtworkImage::Profile::NativeArtwork, nativeLimits()).isNull());
    }
    void portableAnymapTextAndBinaryPixelsUseTheSameNativeProfile()
    {
        for (const auto& bytes : QList<QByteArray>{QByteArray("P1\n# comment\n2 2\n0 1 1 0\n"), QByteArray("P2\n2 2\n15\n0 5 10 15\n"),
                QByteArray("P3\n2 1\n255\n255 0 0 0 255 0\n"),
                QByteArray("P5\n2 1\n65535\n") + QByteArray::fromHex("0000ffff"),
                QByteArray("P6\n1 1\n65535\n") + QByteArray::fromHex("ffff00008000")}) {
            const auto expected = baseline(bytes); QVERIFY(!expected.isNull());
            QCOMPARE(AddonArtworkImage::decode(bytes, AddonArtworkImage::Profile::NativeArtwork, nativeLimits()), expected);
        }
    }
    void iconEntriesAndNestedImagesRemainBounded()
    {
        const auto png = image(); QByteArray icon(22, '\0');
        qToLittleEndian<quint16>(1, icon.data() + 2); qToLittleEndian<quint16>(1, icon.data() + 4);
        icon[6] = icon[7] = 2; qToLittleEndian<quint16>(1, icon.data() + 10); qToLittleEndian<quint16>(32, icon.data() + 12);
        qToLittleEndian<quint32>(quint32(png.size()), icon.data() + 14); qToLittleEndian<quint32>(22, icon.data() + 18); icon += png;
        const auto expected = baseline(icon); QVERIFY(!expected.isNull());
        QCOMPARE(AddonArtworkImage::decode(icon, AddonArtworkImage::Profile::NativeArtwork, nativeLimits()), expected);
        for (const auto& invalid : {QPair<int, quint32>{14, 0xffffffff}, {18, 0xffffffff}}) {
            auto bytes = icon; qToLittleEndian(invalid.second, bytes.data() + invalid.first);
            QVERIFY(AddonArtworkImage::decode(bytes, AddonArtworkImage::Profile::NativeArtwork, nativeLimits()).isNull());
        }
        // Repeated directory entries may share one validated frame. A
        // different overlapping frame must not cause a repeated input scan.
        QByteArray shared = icon.left(22) + icon.mid(6, 16);
        qToLittleEndian<quint16>(2, shared.data() + 4);
        qToLittleEndian<quint32>(38, shared.data() + 18);
        qToLittleEndian<quint32>(38, shared.data() + 34);
        shared += png;
        QCOMPARE(AddonArtworkImage::decode(shared, AddonArtworkImage::Profile::NativeArtwork, nativeLimits()), baseline(shared));
        auto overlap = shared; qToLittleEndian<quint32>(39, overlap.data() + 34);
        qToLittleEndian<quint32>(quint32(png.size() - 1), overlap.data() + 30);
        QVERIFY(AddonArtworkImage::decode(overlap, AddonArtworkImage::Profile::NativeArtwork, nativeLimits()).isNull());
        auto many = icon; qToLittleEndian<quint16>(257, many.data() + 4);
        QVERIFY(AddonArtworkImage::decode(many, AddonArtworkImage::Profile::NativeArtwork, nativeLimits()).isNull());
        auto truncated = icon; truncated.chop(1);
        QVERIFY(AddonArtworkImage::decode(truncated, AddonArtworkImage::Profile::NativeArtwork, nativeLimits()).isNull());
    }
    void staticRasterAndNativeRasterUseSeparateProfiles()
    {
        for (const auto* codec : {"PNG", "JPEG", "WEBP"}) {
            const auto bytes = image(codec); QVERIFY(!bytes.isEmpty());
            QVERIFY(!AddonArtworkImage::decode(bytes, AddonArtworkImage::Profile::StaticAddon, {}).isNull());
            QVERIFY(!AddonArtworkImage::decode(bytes, AddonArtworkImage::Profile::NativeArtwork, nativeLimits()).isNull());
        }
        const auto large = image("PNG", 1025);
        QVERIFY(AddonArtworkImage::decode(large, AddonArtworkImage::Profile::StaticAddon, {}).isNull());
        QVERIFY(!AddonArtworkImage::decode(large, AddonArtworkImage::Profile::NativeArtwork, nativeLimits()).isNull());
    }
    void compressedPngMetadataNeverEntersTheDecoder()
    {
        const auto compressed = qCompress(QByteArray(8 * 1024 * 1024, 'x')).mid(4);
        for (const auto& field : {QPair<QByteArray, QByteArray>{"zTXt", QByteArray("label\0\0", 7) + compressed},
                {"iCCP", QByteArray("profile\0\0", 9) + compressed},
                {"iTXt", QByteArray("label\0\1\0\0\0", 10) + compressed}}) {
            const auto bytes = pngWith(field.first, field.second);
            QVERIFY(bytes.size() < 32 * 1024);
            QVERIFY(AddonArtworkImage::decode(bytes, AddonArtworkImage::Profile::StaticAddon, {}).isNull());
            const auto pixels = AddonArtworkImage::decode(bytes, AddonArtworkImage::Profile::NativeArtwork, nativeLimits());
            QVERIFY(!pixels.isNull()); QVERIFY(pixels.textKeys().isEmpty());
        }
        QVERIFY(AddonArtworkImage::decode(pngWith("tEXt", QByteArray(16385, 'x')), AddonArtworkImage::Profile::StaticAddon, {}).isNull());
    }
    void metadataAndAnimationHaveAClosedStaticProfile()
    {
        QByteArray animation(8, '\0'); qToBigEndian<quint32>(2, animation.data());
        const auto apng = pngWith("acTL", animation);
        QVERIFY(AddonArtworkImage::decode(apng, AddonArtworkImage::Profile::StaticAddon, {}).isNull());
        QVERIFY(!AddonArtworkImage::decode(apng, AddonArtworkImage::Profile::NativeArtwork, nativeLimits()).isNull());
        auto jpeg = image("JPEG"); QByteArray app(2, '\0'); qToBigEndian<quint16>(20002, app.data()); app += QByteArray(20000, 'x');
        jpeg.insert(2, QByteArray::fromHex("ffe2") + app);
        QVERIFY(AddonArtworkImage::decode(jpeg, AddonArtworkImage::Profile::StaticAddon, {}).isNull());
        QVERIFY(!AddonArtworkImage::decode(jpeg, AddonArtworkImage::Profile::NativeArtwork, nativeLimits()).isNull());
        const auto gif = QByteArray::fromBase64("R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7");
        QVERIFY(AddonArtworkImage::decode(gif, AddonArtworkImage::Profile::StaticAddon, {}).isNull());
        QVERIFY(!AddonArtworkImage::decode(gif, AddonArtworkImage::Profile::NativeArtwork, nativeLimits()).isNull());
    }
    void vectorResourcesStayInsideTheDocument()
    {
        const QByteArray prefix("<svg xmlns='http://www.w3.org/2000/svg' width='2' height='2'>"), suffix("</svg>");
        QVERIFY(!AddonArtworkImage::decode(prefix + "<rect width='2' height='2' fill='red'/>" + suffix,
            AddonArtworkImage::Profile::NativeArtwork, nativeLimits()).isNull());
        const auto embedded = prefix + "<image width='2' height='2' href='data:image/png;base64," + image().toBase64() + "'/>" + suffix;
        QVERIFY(!AddonArtworkImage::decode(embedded, AddonArtworkImage::Profile::NativeArtwork, nativeLimits()).isNull());
        for (const auto& node : {QByteArray("<image href='file:///tmp/private.png'/>"),
                QByteArray("<image href='https://127.0.0.1/private.png'/>"),
                QByteArray("<use href='other.svg#shape'/>"),
                QByteArray("<style>@import 'https://example.org/style';</style>"),
                QByteArray("<rect style='fill:u\\72l(https://example.org/image)'/>"),
                QByteArray("<rect fill='url(https://example.org/image)'/>"),
                QByteArray("<svg xml:base='file:///tmp/'/>"),
                QByteArray("<foreignObject/><script/>")})
            QVERIFY(AddonArtworkImage::decode(prefix + node + suffix, AddonArtworkImage::Profile::NativeArtwork, nativeLimits()).isNull());
        QVERIFY(AddonArtworkImage::decode("<!DOCTYPE svg [<!ENTITY test 'bad'>]>" + prefix + "<text>&test;</text>" + suffix,
            AddonArtworkImage::Profile::NativeArtwork, nativeLimits()).isNull());
        QVERIFY(AddonArtworkImage::decode(prefix + "<rect/>" + suffix, AddonArtworkImage::Profile::StaticAddon, {}).isNull());
    }
    void spoofedContainersAndPixelClaimsFailBeforeDecode()
    {
        auto png = image(); qToBigEndian<quint32>(65535, png.data() + 16);
        QVERIFY(AddonArtworkImage::decode(png, AddonArtworkImage::Profile::NativeArtwork, nativeLimits()).isNull());
        auto truncated = image(); truncated.chop(1);
        QVERIFY(AddonArtworkImage::decode(truncated, AddonArtworkImage::Profile::StaticAddon, {}).isNull());
        const auto spoof = QByteArray("RIFF") + QByteArray::fromHex("04000000") + "WEBP";
        QVERIFY(AddonArtworkImage::decode(spoof, AddonArtworkImage::Profile::NativeArtwork, nativeLimits()).isNull());
        AddonArtworkImage::Limits small{1024 * 1024, 8, 1024};
        QVERIFY(AddonArtworkImage::decode(image(), AddonArtworkImage::Profile::NativeArtwork, small).isNull());
    }
};
QTEST_GUILESS_MAIN(TestAddonArtworkImage)
#include "test_addon_artwork_image.moc"
