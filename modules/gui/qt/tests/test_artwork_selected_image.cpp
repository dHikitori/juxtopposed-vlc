// SPDX-License-Identifier: GPL-2.0-or-later
#include <QtTest/QtTest>
#include <QBuffer>
#include <QImageReader>
#include <QtEndian>
#include "addons/addon_artwork_image.hpp"
#include "addon_artwork_test_images.hpp"

using vlc::addons::AddonArtworkImage;
using namespace NativeArtworkFixture;
namespace {
QByteArray large(const char* codec,QSize size)
{
    QImage pixels(size,QImage::Format_RGB32); pixels.fill(QColor(170,30,70));
    QByteArray bytes; QBuffer output(&bytes); output.open(QIODevice::WriteOnly); pixels.save(&output,codec); return bytes;
}
QImage scaledNative(const QByteArray& bytes,int maximum)
{
    QBuffer source; source.setData(bytes); source.open(QIODevice::ReadOnly); QImageReader reader(&source);
    reader.setAutoTransform(true); const auto size=reader.size();
    if(size.width()>maximum || size.height()>maximum) reader.setScaledSize(size.scaled(QSize(maximum,maximum),Qt::KeepAspectRatio));
    return reader.read().convertToFormat(QImage::Format_ARGB32_Premultiplied);
}
}
class TestArtworkSelectedImage : public QObject {
    Q_OBJECT
private slots:
    void installedNativeFormatsRemainAvailable_data() {
        QTest::addColumn<QByteArray>("bytes"); const auto available=QImageReader::supportedImageFormats();
        for(const QByteArray codec:{"bmp","cur","gif","ico","jfif","jpeg","jpg","pbm","pgm","png","ppm","svg","svgz","webp","xbm","xpm"}) {
            if(!available.contains(codec)) continue;
            const auto bytes=codec=="svg" ? svgBytes() : codec=="svgz" ? compressedSvg()
                : codec=="gif" ? QByteArray::fromBase64("R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7") : patterned(codec);
            QVERIFY(!bytes.isEmpty()); QTest::newRow(codec.constData())<<bytes;
        }
    }
    void installedNativeFormatsRemainAvailable() {
        QFETCH(QByteArray,bytes); const auto expected=baseline(bytes); QVERIFY(!expected.isNull());
        QString error; const auto actual=AddonArtworkImage::decodeSelected(bytes,1024,&error);
        QVERIFY2(!actual.isNull(),qPrintable(error)); QCOMPARE(actual,expected);
    }
    void largeJpegUsesNativeScaledDecodeWithoutTheRemotePixelCap() {
        const auto bytes=large("JPEG",QSize(5000,4000)); QVERIFY(!bytes.isEmpty());
        const auto expected=scaledNative(bytes,1024); QVERIFY(!expected.isNull());
        QCOMPARE(AddonArtworkImage::decodeSelected(bytes,1024),expected);
        QVERIFY(AddonArtworkImage::decode(bytes,AddonArtworkImage::Profile::NativeArtwork,{32*1024*1024,128*1024*1024,16384}).isNull());
        QVERIFY(AddonArtworkImage::decode(bytes,AddonArtworkImage::Profile::StaticAddon,{}).isNull());
    }
    void largeNativePngKeepsTheExistingReaderFallback() {
        const auto bytes=large("PNG",QSize(7000,6000)); QVERIFY(!bytes.isEmpty());
        const auto expected=scaledNative(bytes,1024); QVERIFY(!expected.isNull());
        QCOMPARE(AddonArtworkImage::decodeSelected(bytes,1024),expected);
    }
    void largeUncompressedBitmapKeepsTheExistingNativeReader() {
        const auto bytes=large("BMP",QSize(5000,4000)); QVERIFY(bytes.size()>32*1024*1024);
        const auto expected=scaledNative(bytes,1024); QVERIFY(!expected.isNull());
        QCOMPARE(AddonArtworkImage::decodeSelected(bytes,1024),expected);
        QVERIFY(AddonArtworkImage::decode(bytes,AddonArtworkImage::Profile::NativeArtwork,{32*1024*1024,128*1024*1024,16384}).isNull());
        QVERIFY(AddonArtworkImage::decode(bytes,AddonArtworkImage::Profile::StaticAddon,{}).isNull());
    }
    void exifOrientationKeepsVisiblePixels() {
        for(unsigned value=1;value<=8;++value)
            for(bool big:{false,true}) { const auto bytes=exifJpeg(value,big,true);
                QCOMPARE(AddonArtworkImage::decodeSelected(bytes,1024),baseline(bytes)); }
    }
    void selectedHeaderAndExternalResourceLimitsStayClosed() {
        auto bytes=patterned("png"); qToBigEndian<quint32>(32768,bytes.data()+16); qToBigEndian<quint32>(4000,bytes.data()+20);
        QVERIFY(AddonArtworkImage::decodeSelected(bytes).isNull());
        bytes=patterned("png"); qToBigEndian<quint32>(32769,bytes.data()+16);
        QVERIFY(AddonArtworkImage::decodeSelected(bytes).isNull());
        QVERIFY(AddonArtworkImage::decodeSelected("<svg xmlns='http://www.w3.org/2000/svg' width='2' height='2'><image href='file:///private/art.png'/></svg>").isNull());
        QVERIFY(AddonArtworkImage::decodeSelected(QByteArray(32*1024*1024+1,'x')).isNull());
    }
};
QTEST_GUILESS_MAIN(TestArtworkSelectedImage)
#include "test_artwork_selected_image.moc"
