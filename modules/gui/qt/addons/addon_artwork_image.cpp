// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_artwork_image.hpp"
#include "addon_native_image_formats.hpp"
#include <QBuffer>
#include <QImageReader>
#include <QTransform>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QtEndian>
#include <algorithm>
#include <cstring>
#include <zlib.h>

namespace vlc::addons {
namespace {
using Profile = AddonArtworkImage::Profile;
using Limits = AddonArtworkImage::Limits;
constexpr qint64 metadataLimit = 16 * 1024;
bool dimensions(quint64 width, quint64 height, const Limits& limits)
{
    return width && height && width <= quint64(limits.dimension) && height <= quint64(limits.dimension)
        && width <= quint64(limits.pixelBytes) / 8 / height;
}
quint32 be32(const QByteArray& bytes, qsizetype offset)
{ return qFromBigEndian<quint32>(bytes.constData() + offset); }
quint32 le32(const QByteArray& bytes, qsizetype offset)
{ return qFromLittleEndian<quint32>(bytes.constData() + offset); }
quint32 le24(const QByteArray& bytes, qsizetype offset)
{ return quint8(bytes[offset]) | quint32(quint8(bytes[offset + 1])) << 8 | quint32(quint8(bytes[offset + 2])) << 16; }
bool png(const QByteArray& bytes, Profile profile, const Limits& limits, QByteArray* clean)
{
    qsizetype at = 8; bool header = false, data = false, ended = false; qint64 metadata = 0;
    clean->append(bytes.constData(), 8);
    while (at + 12 <= bytes.size()) {
        const quint32 length = be32(bytes, at);
        if (quint64(length) + 12 > quint64(bytes.size() - at)) return false;
        const auto type = bytes.mid(at + 4, 4);
        if (!header && (type != "IHDR" || length != 13 || !dimensions(be32(bytes, at + 8), be32(bytes, at + 12), limits))) return false;
        if (header && type == "IHDR") return false;
        const bool animation = type == "acTL" || type == "fcTL" || type == "fdAT";
        if (profile == Profile::StaticAddon) {
            if (animation || type == "zTXt" || type == "iCCP") return false;
            if (type == "iTXt") {
                const auto text = bytes.mid(at + 8, length); const auto keyword = text.indexOf('\0');
                if (keyword < 1 || keyword + 2 >= text.size() || text[keyword + 1] != 0) return false;
            }
            if (type != "IDAT" && (metadata += length) > metadataLimit) return false;
        }
        // Keep the native default frame and its palette. Remove all ancillary
        // metadata before the decoder can expand it.
        const bool needed = type == "IHDR" || type == "PLTE" || type == "IDAT" || type == "IEND" || type == "tRNS";
        if (profile == Profile::StaticAddon || needed) clean->append(bytes.constData() + at, length + 12);
        header = true; data |= type == "IDAT"; at += qsizetype(length) + 12;
        if (type == "IEND") { ended = length == 0 && data && at == bytes.size(); break; }
    }
    return ended;
}
bool jpeg(const QByteArray& bytes, Profile profile, const Limits& limits, QByteArray* clean, int* orientation)
{
    qsizetype at = 2; qint64 metadata = 0; bool entropy = false, scan = false, size = false, exif = false; int scans = 0;
    clean->append(bytes.constData(), 2);
    while (at < bytes.size()) {
        if (entropy) {
            const auto start = at;
            while (at < bytes.size()) {
                if (quint8(bytes[at++]) != 0xff) continue;
                if (at >= bytes.size()) return false;
                while (at < bytes.size() && quint8(bytes[at]) == 0xff) ++at;
                if (at >= bytes.size()) return false;
                const auto marker = quint8(bytes[at]);
                if (marker == 0 || (marker >= 0xd0 && marker <= 0xd7)) { ++at; continue; }
                --at; while (at > start && quint8(bytes[at - 1]) == 0xff) --at;
                break;
            }
            clean->append(bytes.constData() + start, at - start); entropy = false;
        }
        const auto start = at;
        if (at >= bytes.size() || quint8(bytes[at++]) != 0xff) return false;
        while (at < bytes.size() && quint8(bytes[at]) == 0xff) ++at;
        if (at >= bytes.size()) return false;
        const auto marker = quint8(bytes[at++]);
        if (marker == 0xd9) { clean->append(bytes.constData() + start, at - start); return size && scan && at == bytes.size(); }
        if (marker == 0xd8 || marker == 0 || marker == 1 || (marker >= 0xd0 && marker <= 0xd7)) return false;
        if (at + 2 > bytes.size()) return false;
        const auto length = qFromBigEndian<quint16>(bytes.constData() + at);
        if (length < 2 || length > bytes.size() - at) return false;
        const bool auxiliary = (marker >= 0xe0 && marker <= 0xef) || marker == 0xfe;
        if (profile == Profile::NativeArtwork && marker == 0xe1 && !exif
         && bytes.mid(at + 2, 6) == QByteArray("Exif\0\0", 6)) {
            *orientation = native_image::exifOrientation(bytes.mid(at + 2, length - 2)); exif = true;
        }
        const bool colorTransform = profile == Profile::NativeArtwork && marker == 0xee
            && length == 14 && bytes.mid(at + 2, 5) == "Adobe";
        if (auxiliary && profile == Profile::StaticAddon && (metadata += length - 2) > metadataLimit) return false;
        if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
            if (length < 8 || !dimensions(qFromBigEndian<quint16>(bytes.constData() + at + 5),
                    qFromBigEndian<quint16>(bytes.constData() + at + 3), limits)) return false;
            size = true;
        }
        at += length;
        if (!auxiliary || colorTransform || profile == Profile::StaticAddon) clean->append(bytes.constData() + start, at - start);
        if (marker == 0xda) { if (++scans > 1024) return false; entropy = true; scan = true; }
    }
    return false;
}
bool webp(const QByteArray& bytes, Profile profile, const Limits& limits, QByteArray* clean)
{
    if (bytes.size() < 12 || quint64(le32(bytes, 4)) + 8 != quint64(bytes.size())) return false;
    qsizetype at = 12; qint64 metadata = 0; int frames = 0; bool image = false, canvas = false;
    clean->append(bytes.constData(), 12);
    while (at + 8 <= bytes.size()) {
        const auto type = bytes.mid(at, 4); const quint32 length = le32(bytes, at + 4);
        if (quint64(length) + 8 + (length & 1) > quint64(bytes.size() - at)) return false;
        const auto body = at + 8;
        if (type == "VP8X") {
            if (canvas || length != 10 || !dimensions(le24(bytes, body + 4) + 1, le24(bytes, body + 7) + 1, limits)) return false;
            canvas = true;
            if (profile == Profile::StaticAddon && (quint8(bytes[body]) & 2)) return false;
        }
        if (type == "VP8 ") {
            if (length < 10 || bytes.mid(body + 3, 3) != QByteArray::fromHex("9d012a")
                || !dimensions(qFromLittleEndian<quint16>(bytes.constData() + body + 6) & 0x3fff,
                    qFromLittleEndian<quint16>(bytes.constData() + body + 8) & 0x3fff, limits)) return false;
            image = true;
        }
        if (type == "VP8L") {
            if (length < 5 || quint8(bytes[body]) != 0x2f) return false;
            const auto bits = le32(bytes, body + 1);
            if (!dimensions((bits & 0x3fff) + 1, ((bits >> 14) & 0x3fff) + 1, limits)) return false;
            image = true;
        }
        if (type == "ANIM" || type == "ANMF") {
            if (profile == Profile::StaticAddon) return false;
            if (type == "ANMF") {
                if (++frames > 256 || length < 16 || !canvas
                    || !dimensions(le24(bytes, body + 6) + 1, le24(bytes, body + 9) + 1, limits)) return false;
                image = true;
            } else if (length != 6) return false;
        }
        const bool auxiliary = type != "VP8 " && type != "VP8L" && type != "ALPH" && type != "VP8X" && type != "ANIM" && type != "ANMF";
        if (auxiliary && profile == Profile::StaticAddon && (metadata += length) > metadataLimit) return false;
        if (!auxiliary || profile == Profile::StaticAddon) {
            const auto output = clean->size(); clean->append(bytes.constData() + at, qsizetype(length) + 8 + (length & 1));
            if (type == "VP8X" && profile == Profile::NativeArtwork) (*clean)[output + 8] = char(quint8((*clean)[output + 8]) & ~quint8(0x2c));
        }
        at += qsizetype(length) + 8 + (length & 1);
    }
    if (at != bytes.size() || !image) return false;
    qToLittleEndian<quint32>(quint32(clean->size() - 8), clean->data() + 4); return true;
}
bool gif(const QByteArray& bytes, const Limits& limits, QByteArray* clean)
{
    if (bytes.size() < 13 || !dimensions(qFromLittleEndian<quint16>(bytes.constData() + 6),
            qFromLittleEndian<quint16>(bytes.constData() + 8), limits)) return false;
    qsizetype at = 13 + ((quint8(bytes[10]) & 0x80) ? 3 << ((quint8(bytes[10]) & 7) + 1) : 0);
    if (at > bytes.size()) return false;
    clean->append(bytes.constData(), at);
    const auto blocks = [&bytes](qsizetype* position) {
        while (*position < bytes.size()) {
            const auto count = quint8(bytes[(*position)++]);
            if (count > bytes.size() - *position) return false;
            *position += count; if (!count) return true;
        }
        return false;
    };
    while (at < bytes.size()) {
        const auto start = at; const auto tag = quint8(bytes[at++]);
        if (tag == 0x21) {
            if (at >= bytes.size()) return false;
            const auto label = quint8(bytes[at++]); if (!blocks(&at)) return false;
            if (label == 0xf9) { if (at - start != 8) return false; clean->append(bytes.constData() + start, at - start); }
        } else if (tag == 0x2c) {
            if (at + 9 > bytes.size()) return false;
            const quint32 width = qFromLittleEndian<quint16>(bytes.constData() + at + 4), height = qFromLittleEndian<quint16>(bytes.constData() + at + 6);
            if (!dimensions(width, height, limits)
                || qFromLittleEndian<quint16>(bytes.constData() + at) + width > quint32(qFromLittleEndian<quint16>(bytes.constData() + 6))
                || qFromLittleEndian<quint16>(bytes.constData() + at + 2) + height > quint32(qFromLittleEndian<quint16>(bytes.constData() + 8))) return false;
            const auto flags = quint8(bytes[at + 8]); at += 9 + ((flags & 0x80) ? 3 << ((flags & 7) + 1) : 0);
            if (at >= bytes.size()) return false;
            ++at; if (!blocks(&at)) return false;
            clean->append(bytes.constData() + start, at - start); clean->append(char(0x3b)); return true;
        } else return false;
    }
    return false;
}
bool css(const QString& value)
{
    if (value.contains(QLatin1Char('\\')) || value.contains(QLatin1Char('@'))) return false;
    static const QRegularExpression calls(QStringLiteral("url\\s*\\(([^)]*)\\)"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression fragment(QStringLiteral("^#[A-Za-z_][A-Za-z0-9_.:-]{0,255}$"));
    auto matches = calls.globalMatch(value);
    while (matches.hasNext()) {
        auto reference = matches.next().captured(1).trimmed();
        if ((reference.startsWith(QLatin1Char('\'')) && reference.endsWith(QLatin1Char('\'')))
            || (reference.startsWith(QLatin1Char('"')) && reference.endsWith(QLatin1Char('"')))) reference = reference.mid(1, reference.size() - 2);
        if (!fragment.match(reference).hasMatch()) return false;
    }
    // Refuse malformed URL tokens before the SVG engine sees CSS.
    QString withoutCalls = value; withoutCalls.remove(calls);
    return !withoutCalls.contains(QStringLiteral("url"), Qt::CaseInsensitive);
}
bool svg(const QByteArray& bytes, const Limits& limits, QByteArray* clean)
{
    QXmlStreamReader input(bytes); QXmlStreamWriter output(clean); int depth = 0, elements = 0, attributes = 0;
    qint64 inlineBytes = 0, inlinePixels = 0; bool root = false; QString current;
    static const QRegularExpression fragment(QStringLiteral("^#[A-Za-z_][A-Za-z0-9_.:-]{0,255}$"));
    while (!input.atEnd()) {
        const auto token = input.readNext();
        if (token == QXmlStreamReader::DTD || token == QXmlStreamReader::EntityReference || token == QXmlStreamReader::ProcessingInstruction) return false;
        if (token == QXmlStreamReader::StartElement) {
            if (++depth > 128 || ++elements > 8192 || input.attributes().size() > 128) return false;
            const auto name = input.name().toString(); current = name.toLower();
            if (input.namespaceUri() != QLatin1String("http://www.w3.org/2000/svg") || (!root && name != QLatin1String("svg"))
                || current == QLatin1String("script") || current == QLatin1String("foreignobject")
                || current.startsWith(QLatin1String("animate")) || current == QLatin1String("set") || current == QLatin1String("discard")) return false;
            root = true; output.writeStartElement(QStringLiteral("http://www.w3.org/2000/svg"), name);
            for (const auto& attribute : input.attributes()) {
                if (++attributes > 32768 || attribute.value().size() > 1024 * 1024) return false;
                auto value = attribute.value().toString(); const auto local = attribute.name().toString().toLower();
                const auto space = attribute.namespaceUri();
                if ((!space.isEmpty() && space != QLatin1String("http://www.w3.org/1999/xlink") && space != QLatin1String("http://www.w3.org/XML/1998/namespace"))
                    || local.startsWith(QLatin1String("on")) || local == QLatin1String("base") || !css(value)) return false;
                if (local == QLatin1String("href")) {
                    if (current == QLatin1String("image") && value.startsWith(QLatin1String("data:image/"))) {
                        const auto comma = value.indexOf(QLatin1Char(','));
                        if (comma < 0 || !value.left(comma).endsWith(QLatin1String(";base64"))) return false;
                        const auto encoded = value.mid(comma + 1).toLatin1();
                        if ((inlineBytes += encoded.size()) > 4 * 1024 * 1024) return false;
                        const auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
                        if (!decoded || AddonArtworkImage::format(decoded.decoded) == "svg" || AddonArtworkImage::format(decoded.decoded) == "svgz") return false;
                        auto pixels = AddonArtworkImage::decode(decoded.decoded, Profile::NativeArtwork, limits);
                        if (pixels.isNull() || (inlinePixels += pixels.sizeInBytes()) > limits.pixelBytes / 2) return false;
                        QByteArray raster; QBuffer buffer(&raster); buffer.open(QIODevice::WriteOnly);
                        if (!pixels.save(&buffer, "PNG") || (inlineBytes += raster.size()) > 4 * 1024 * 1024) return false;
                        value = QStringLiteral("data:image/png;base64,") + QString::fromLatin1(raster.toBase64());
                    } else if (!fragment.match(value).hasMatch()) return false;
                }
                if (space.isEmpty()) output.writeAttribute(attribute.name().toString(), value);
                else output.writeAttribute(space.toString(), attribute.name().toString(), value);
            }
        } else if (token == QXmlStreamReader::EndElement) { output.writeEndElement(); --depth; current.clear(); }
        else if (token == QXmlStreamReader::Characters) {
            const auto value = input.text().toString(); if (!css(value)) return false;
            output.writeCharacters(value);
        }
        if (clean->size() > 8 * 1024 * 1024) return false;
    }
    return root && !depth && !input.hasError() && !output.hasError();
}
bool gunzip(const QByteArray& bytes, QByteArray* output)
{
    z_stream stream{}; if (inflateInit2(&stream, MAX_WBITS + 16) != Z_OK) return false;
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(bytes.constData())); stream.avail_in = uInt(bytes.size());
    char buffer[16 * 1024]; int result;
    do {
        stream.next_out = reinterpret_cast<Bytef*>(buffer); stream.avail_out = sizeof(buffer);
        result = inflate(&stream, Z_NO_FLUSH);
        const auto count = qsizetype(sizeof(buffer) - stream.avail_out);
        if (output->size() + count > 8 * 1024 * 1024) { inflateEnd(&stream); return false; }
        output->append(buffer, count);
    } while (result == Z_OK);
    const bool valid = result == Z_STREAM_END && stream.avail_in == 0; inflateEnd(&stream); return valid;
}
}
QByteArray AddonArtworkImage::format(const QByteArray& bytes)
{
    if (bytes.startsWith(QByteArray::fromHex("89504e470d0a1a0a"))) return "png";
    if (bytes.startsWith(QByteArray::fromHex("ffd8"))) return "jpeg";
    if (bytes.size() >= 12 && bytes.startsWith("RIFF") && bytes.mid(8, 4) == "WEBP") return "webp";
    if (bytes.startsWith("GIF87a") || bytes.startsWith("GIF89a")) return "gif";
    if (bytes.startsWith(QByteArray::fromHex("1f8b"))) return "svgz";
    if (bytes.startsWith("BM")) return "bmp";
    if (bytes.size() >= 6 && bytes.startsWith(QByteArray::fromHex("00000100"))) return "ico";
    if (bytes.size() >= 6 && bytes.startsWith(QByteArray::fromHex("00000200"))) return "cur";
    if (bytes.size() >= 3 && bytes[0] == 'P' && native_image::whitespace(bytes[2])) {
        if (bytes[1] == '1' || bytes[1] == '4') return "pbm";
        if (bytes[1] == '2' || bytes[1] == '5') return "pgm";
        if (bytes[1] == '3' || bytes[1] == '6') return "ppm";
    }
    const auto textHeader = bytes.left(65536);
    if (textHeader.contains("/* XPM */")) return "xpm";
    if (textHeader.contains("#define") && textHeader.contains("_width") && textHeader.contains("_height")) return "xbm";
    const auto prefix = bytes.left(1024).trimmed();
    if (prefix.startsWith("<svg") || prefix.startsWith("<?xml") || prefix.startsWith("<!--")) return "svg";
    return {};
}
static QImage decodeProfile(const QByteArray& bytes, Profile profile, const Limits& requested, QString* error, int selectedDimension = 0)
{
    if (error) error->clear();
    const auto fail = [error] { if (error) *error = QStringLiteral("The artwork format or resource limit is not valid."); return QImage{}; };
    const bool native = profile == Profile::NativeArtwork;
    const bool selected = native && selectedDimension > 0;
    Limits limits{std::clamp<qint64>(requested.encodedBytes, 1, selected ? 512 * 1024 * 1024 : native ? 32 * 1024 * 1024 : 1024 * 1024),
        std::clamp<qint64>(requested.pixelBytes, 8, selected ? 800000000 : native ? 128 * 1024 * 1024 : 8 * 1024 * 1024),
        std::clamp(requested.dimension, 1, selected ? 32768 : native ? 16384 : 1024)};
    if (bytes.isEmpty() || bytes.size() > limits.encodedBytes) return fail();
    QByteArray clean; auto codec = AddonArtworkImage::format(bytes); bool valid = false; int orientation = 1;
    if (codec == "png") valid = png(bytes, profile, limits, &clean);
    else if (codec == "jpeg") valid = jpeg(bytes, profile, limits, &clean, &orientation);
    else if (codec == "webp") valid = webp(bytes, profile, limits, &clean);
    else if (profile == Profile::NativeArtwork && codec == "gif") valid = gif(bytes, limits, &clean);
    else if (native && codec == "bmp") valid = native_image::bitmap(bytes, limits, &clean);
    else if (native && (codec == "ico" || codec == "cur"))
        valid = native_image::icon(bytes, limits, &clean, [&](const QByteArray& frame, QByteArray* output) { return png(frame, profile, limits, output); });
    else if (native && (codec == "pbm" || codec == "pgm" || codec == "ppm")) valid = native_image::pnm(bytes, limits, &clean);
    else if (native && codec == "xbm") valid = native_image::xbm(bytes, limits, &clean);
    else if (native && codec == "xpm") valid = native_image::xpm(bytes, limits, &clean);
    else if (profile == Profile::NativeArtwork && (codec == "svg" || codec == "svgz")) {
        QByteArray document = bytes;
        if (codec == "svgz") { document.clear(); if (!gunzip(bytes, &document)) return fail(); }
        valid = svg(document, limits, &clean); codec = "svg";
    }
    if (!valid || !QImageReader::supportedImageFormats().contains(codec)) return fail();
    QBuffer input(&clean); input.open(QIODevice::ReadOnly);
    QImageReader reader(&input, codec); reader.setAutoDetectImageFormat(false); reader.setDecideFormatFromContent(false); reader.setAutoTransform(false);
    const auto size = reader.size();
    if (!size.isValid() || !dimensions(size.width(), size.height(), limits)) return fail();
    auto expected = size;
    if (selected && (size.width() > selectedDimension || size.height() > selectedDimension)) {
        expected = size.scaled(QSize(selectedDimension, selectedDimension), Qt::KeepAspectRatio);
        // This is the existing native reader's scaled-decode path. A codec
        // can use an internal full frame. The header caps that frame at
        // 100 million pixels; Qt's global allocation policy stays unchanged.
        reader.setScaledSize(expected);
    }
    auto image = reader.read();
    if (image.isNull() || image.size() != expected || image.sizeInBytes() > limits.pixelBytes) return fail();
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (image.isNull() || image.sizeInBytes() > limits.pixelBytes) return fail();
    if (native) {
        switch (orientation) {
            case 2: image = image.mirrored(true, false); break;
            case 3: image = image.transformed(QTransform().rotate(180)); break;
            case 4: image = image.mirrored(false, true); break;
            case 5: image = image.transformed(QTransform().rotate(90)).mirrored(true, false); break;
            case 6: image = image.transformed(QTransform().rotate(90)); break;
            case 7: image = image.transformed(QTransform().rotate(90)).mirrored(false, true); break;
            case 8: image = image.transformed(QTransform().rotate(270)); break;
        }
        if (image.isNull() || image.sizeInBytes() > limits.pixelBytes) return fail();
    }
    QImage pixels(image.size(), QImage::Format_ARGB32_Premultiplied);
    if (pixels.isNull()) return fail();
    for (int row = 0; row < image.height(); ++row) std::memcpy(pixels.scanLine(row), image.constScanLine(row), size_t(image.width()) * 4);
    return pixels;
}
QImage AddonArtworkImage::decode(const QByteArray& bytes, Profile profile, const Limits& limits, QString* error)
{ return decodeProfile(bytes, profile, limits, error); }
QImage AddonArtworkImage::decodeSelected(const QByteArray& bytes, int maximumDimension, QString* error)
{
    return decodeProfile(bytes, Profile::NativeArtwork, Limits{512 * 1024 * 1024, 800000000, 32768}, error,
        std::clamp(maximumDimension, 1, 4096));
}
}
