// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QBuffer>
#include <QImageReader>
#include <QImageWriter>
#include <QtEndian>
#include <zlib.h>

namespace NativeArtworkFixture {
inline QImage baseline(const QByteArray& bytes, bool orient = true)
{
    QBuffer input; input.setData(bytes); input.open(QIODevice::ReadOnly);
    QImageReader reader(&input); reader.setAutoTransform(orient);
    return reader.read().convertToFormat(QImage::Format_ARGB32_Premultiplied);
}
inline QByteArray patterned(const QByteArray& format)
{
    QImage pixels(3, 2, QImage::Format_ARGB32);
    pixels.setPixelColor(0, 0, Qt::red); pixels.setPixelColor(1, 0, Qt::green); pixels.setPixelColor(2, 0, Qt::blue);
    pixels.setPixelColor(0, 1, Qt::transparent); pixels.setPixelColor(1, 1, QColor(20, 40, 80, 128)); pixels.setPixelColor(2, 1, Qt::yellow);
    QByteArray bytes; QBuffer output(&bytes); output.open(QIODevice::WriteOnly);
    QImageWriter writer(&output, format); writer.setQuality(100);
    return writer.write(pixels) ? bytes : QByteArray{};
}
inline QByteArray svgBytes()
{ return "<svg xmlns='http://www.w3.org/2000/svg' width='3' height='2'><rect width='3' height='2' fill='red'/></svg>"; }
inline QByteArray compressedSvg()
{
    const auto svg = svgBytes(); QByteArray output(4096, '\0'); z_stream stream{};
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) return {};
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(svg.constData())); stream.avail_in = uInt(svg.size());
    stream.next_out = reinterpret_cast<Bytef*>(output.data()); stream.avail_out = uInt(output.size());
    const auto result = deflate(&stream, Z_FINISH); output.resize(qsizetype(stream.total_out)); deflateEnd(&stream);
    return result == Z_STREAM_END ? output : QByteArray{};
}
inline QByteArray exifJpeg(unsigned orientation, bool big = false, bool xmp = false)
{
    QByteArray payload("Exif\0\0", 6); QByteArray tiff(26, '\0');
    tiff[0] = tiff[1] = big ? 'M' : 'I';
    const auto shortAt = [&](int at, quint16 value) { if (big) qToBigEndian(value, tiff.data() + at); else qToLittleEndian(value, tiff.data() + at); };
    const auto longAt = [&](int at, quint32 value) { if (big) qToBigEndian(value, tiff.data() + at); else qToLittleEndian(value, tiff.data() + at); };
    shortAt(2, 42); longAt(4, 8); shortAt(8, 1); shortAt(10, 0x112); shortAt(12, 3); longAt(14, 1); shortAt(18, quint16(orientation));
    payload += tiff; QByteArray length(2, '\0'); qToBigEndian<quint16>(quint16(payload.size() + 2), length.data());
    auto bytes = patterned("jpeg"); bytes.insert(2, QByteArray::fromHex("ffe1") + length + payload);
    if (xmp) bytes.insert(2 + payload.size() + 4, QByteArray::fromHex("ffe10005786d70"));
    return bytes;
}
}
