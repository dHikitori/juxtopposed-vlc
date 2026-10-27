// SPDX-License-Identifier: GPL-2.0-or-later
#include <QtTest/QtTest>
#include <QBuffer>
#include <QImageReader>
#include <QtEndian>
#include <zlib.h>
#include "addons/addon_artwork_image.hpp"

using vlc::addons::AddonArtworkImage;
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
