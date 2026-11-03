// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_artwork_image.hpp"
#include <QRegularExpression>
#include <QHash>
#include <QtEndian>
#include <limits>

namespace vlc::addons::native_image {
using Limits = AddonArtworkImage::Limits;
inline bool dimensions(quint64 width, quint64 height, const Limits& limits)
{
    return width && height && width <= quint64(limits.dimension)
        && height <= quint64(limits.dimension) && width <= quint64(limits.pixelBytes) / 8 / height;
}
inline quint16 u16(const QByteArray& bytes, qsizetype at)
{ return qFromLittleEndian<quint16>(bytes.constData() + at); }
inline quint32 u32(const QByteArray& bytes, qsizetype at)
{ return qFromLittleEndian<quint32>(bytes.constData() + at); }
struct Dib {
    quint64 width = 0, height = 0, palette = 0, stride = 0;
    quint32 header = 0, compression = 0;
    quint16 bits = 0;
};
inline bool dib(const QByteArray& bytes, qsizetype at, const Limits& limits, bool icon, Dib* result)
{
    if (at < 0 || bytes.size() - at < 12) return false;
    Dib value; value.header = u32(bytes, at);
    if (value.header != 12 && value.header != 40 && value.header != 64
     && value.header != 108 && value.header != 124) return false;
    if (value.header > quint64(bytes.size() - at)) return false;
    quint16 planes;
    if (value.header == 12) {
        value.width = u16(bytes, at + 4); value.height = u16(bytes, at + 6);
        planes = u16(bytes, at + 8); value.bits = u16(bytes, at + 10);
    } else {
        const qint64 width = qint32(u32(bytes, at + 4)), height = qint32(u32(bytes, at + 8));
        if (width <= 0 || !height || height == std::numeric_limits<qint32>::min() || (icon && height < 0)) return false;
        value.width = quint64(width); value.height = quint64(height < 0 ? -height : height);
        planes = u16(bytes, at + 12); value.bits = u16(bytes, at + 14);
        value.compression = u32(bytes, at + 16); value.palette = u32(bytes, at + 32);
        if (value.palette > 65536 || value.compression > 4 || (height < 0 && value.compression == 1)
         || (height < 0 && value.compression == 2)) return false;
    }
    if (planes != 1 || (value.bits != 1 && value.bits != 4 && value.bits != 8
     && value.bits != 16 && value.bits != 24 && value.bits != 32)) return false;
    if ((value.compression == 1 && value.bits != 8) || (value.compression == 2 && value.bits != 4)
     || (value.compression >= 3 && value.bits != 16 && value.bits != 32)) return false;
    if (icon) { if (value.height & 1) return false; value.height /= 2; }
    if (!dimensions(value.width, value.height, limits)) return false;
    if (value.bits <= 8) {
        const quint64 maximum = quint64(1) << value.bits;
        if (value.palette > maximum) return false;
        if (!value.palette) value.palette = maximum;
    }
    value.stride = ((value.width * value.bits + 31) / 32) * 4;
    *result = value; return true;
}
inline bool bitmap(const QByteArray& bytes, const Limits& limits, QByteArray* clean)
{
    if (bytes.size() < 26 || !bytes.startsWith("BM")) return false;
    Dib value; if (!dib(bytes, 14, limits, false, &value)) return false;
    const quint64 offset = u32(bytes, 10), claimed = u32(bytes, 2);
    quint64 minimum = 14 + value.header + value.palette * (value.header == 12 ? 3 : 4);
    if (value.header == 40 && value.compression >= 3) minimum += value.compression == 4 ? 16 : 12;
    if (offset < minimum || offset >= quint64(bytes.size()) || (claimed && (claimed > quint64(bytes.size()) || claimed < offset))) return false;
    const quint64 end = claimed ? claimed : quint64(bytes.size());
    if (value.compression != 1 && value.compression != 2
     && value.stride * value.height > end - offset) return false;
    *clean = bytes;
    if (value.header == 124) {
        // A bitmap must not name an external color profile. Native output
        // contains pixels only; bit masks and alpha remain intact.
        qToLittleEndian<quint32>(0x73524742, clean->data() + 14 + 56);
        qToLittleEndian<quint32>(0, clean->data() + 14 + 112);
        qToLittleEndian<quint32>(0, clean->data() + 14 + 116);
    }
    return true;
}
template<class Png>
bool icon(const QByteArray& bytes, const Limits& limits, QByteArray* clean, Png sanitizePng)
{
    if (bytes.size() < 22 || u16(bytes, 0) || (u16(bytes, 2) != 1 && u16(bytes, 2) != 2)) return false;
    const quint16 count = u16(bytes, 4);
    if (!count || count > 256 || 6 + quint64(count) * 16 > quint64(bytes.size())) return false;
    const qsizetype directory = 6 + qsizetype(count) * 16;
    struct Cached { QByteArray bytes; quint64 width, height; };
    QHash<quint64, Cached> cached;
    QList<QPair<quint32, quint32>> regions;
    *clean = bytes.left(directory);
    for (unsigned index = 0; index < count; ++index) {
        const auto at = 6 + qsizetype(index) * 16;
        const quint32 length = u32(bytes, at + 8), offset = u32(bytes, at + 12);
        const quint64 width = quint8(bytes[at]) ? quint8(bytes[at]) : 256;
        const quint64 height = quint8(bytes[at + 1]) ? quint8(bytes[at + 1]) : 256;
        if (bytes[at + 3] != 0 || offset < quint64(directory) || length < 8
         || quint64(offset) + length > quint64(bytes.size()) || !dimensions(width, height, limits)) return false;
        const quint64 key = quint64(offset) << 32 | length;
        QByteArray output;
        const auto found = cached.constFind(key);
        if (found != cached.cend()) {
            if (found->width != width || found->height != height) return false;
            output = found->bytes;
        } else {
        // Shared entries can reuse one validated frame. Distinct overlapping
        // frames cannot multiply the work on the same encoded bytes.
        for (const auto& region : regions)
            if (offset < quint64(region.first) + region.second && region.first < quint64(offset) + length) return false;
        regions.append({offset, length});
        auto frame = bytes.mid(offset, length);
        if (frame.startsWith(QByteArray::fromHex("89504e470d0a1a0a"))) {
            if (frame.size() < 24 || qFromBigEndian<quint32>(frame.constData() + 16) != width
             || qFromBigEndian<quint32>(frame.constData() + 20) != height || !sanitizePng(frame, &output)) return false;
        } else {
            Dib value; if (!dib(frame, 0, limits, true, &value) || value.width != width || value.height != height
             || value.compression != 0 || value.header != 40) return false;
            const quint64 colorBytes = value.palette * 4;
            const quint64 maskBytes = ((width + 31) / 32) * 4 * height;
            const quint64 required = value.header + colorBytes + value.stride * height + maskBytes;
            if (required > quint64(frame.size())) return false;
            output = frame;
        }
        cached.insert(key, {output, width, height});
        }
        if (output.size() > limits.encodedBytes - clean->size()) return false;
        qToLittleEndian<quint32>(quint32(output.size()), clean->data() + at + 8);
        qToLittleEndian<quint32>(quint32(clean->size()), clean->data() + at + 12);
        clean->append(output);
    }
    return true;
}
inline bool whitespace(char byte)
{ return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n' || byte == '\v' || byte == '\f'; }
inline bool number(const QByteArray& bytes, qsizetype* at, quint32* value)
{
    for (;;) {
        while (*at < bytes.size() && whitespace(bytes[*at])) ++*at;
        if (*at == bytes.size() || bytes[*at] != '#') break;
        while (*at < bytes.size() && bytes[*at] != '\n') ++*at;
    }
    const auto start = *at; quint64 result = 0;
    while (*at < bytes.size() && bytes[*at] >= '0' && bytes[*at] <= '9') {
        if (*at - start >= 10) return false;
        result = result * 10 + unsigned(bytes[(*at)++] - '0');
        if (result > UINT32_MAX) return false;
    }
    if (*at == start || (*at < bytes.size() && !whitespace(bytes[*at]) && bytes[*at] != '#')) return false;
    *value = quint32(result); return true;
}
inline bool pnm(const QByteArray& bytes, const Limits& limits, QByteArray* clean)
{
    if (bytes.size() < 3 || bytes[0] != 'P' || bytes[1] < '1' || bytes[1] > '6' || !whitespace(bytes[2])) return false;
    const int type = bytes[1] - '0'; qsizetype at = 2; quint32 width, height, maximum = 1;
    if (!number(bytes, &at, &width) || !number(bytes, &at, &height)
     || !dimensions(width, height, limits)
     || ((type != 1 && type != 4) && (!number(bytes, &at, &maximum) || !maximum || maximum > 65535))) return false;
    if (type >= 4) {
        if (at >= bytes.size()) return false;
        if (bytes[at] == '#') {
            while (at < bytes.size() && bytes[at++] != '\n') {}
        } else {
            if (!whitespace(bytes[at])) return false;
            if (bytes[at++] == '\r' && at < bytes.size() && bytes[at] == '\n') ++at;
        }
        const quint64 stride = type == 4 ? (quint64(width) + 7) / 8
            : quint64(width) * (type == 6 ? 3 : 1) * (maximum > 255 ? 2 : 1);
        if (stride * height > quint64(bytes.size() - at)) return false;
    } else {
        const quint64 samples = quint64(width) * height * (type == 3 ? 3 : 1);
        for (quint64 sample = 0; sample < samples; ++sample) {
            quint32 value; if (!number(bytes, &at, &value) || value > maximum) return false;
        }
    }
    *clean = bytes; return true;
}
inline bool xbm(const QByteArray& bytes, const Limits& limits, QByteArray* clean)
{
    const auto brace = bytes.indexOf('{');
    if (brace < 0 || brace > 65536) return false;
    const auto header = QString::fromLatin1(bytes.constData(), brace);
    static const QRegularExpression defines(QStringLiteral("(?:^|[\\r\\n])\\s*#define\\s+(?:[A-Za-z_][A-Za-z_0-9]{0,255})?_(width|height)\\s+([0-9]{1,10})(?=[\\s]|$)"));
    quint32 width = 0, height = 0; auto matches = defines.globalMatch(header);
    while (matches.hasNext()) {
        const auto match = matches.next(); bool ok = false; const auto value = match.captured(2).toUInt(&ok);
        if (!ok) return false;
        auto& target = match.captured(1) == QLatin1String("width") ? width : height;
        if (target && target != value) return false;
        target = value;
    }
    if (!dimensions(width, height, limits)) return false;
    const bool words = header.contains(QRegularExpression(QStringLiteral("\\bshort\\b")));
    const quint64 stride = words ? ((quint64(width) + 15) / 16) * 2 : (quint64(width) + 7) / 8;
    const quint64 count = stride * height / (words ? 2 : 1);
    QByteArray pixels; pixels.reserve(qsizetype(stride * height));
    qsizetype at = brace + 1;
    for (quint64 index = 0; index < count; ++index) {
        while (at < bytes.size() && (whitespace(bytes[at]) || bytes[at] == ',')) ++at;
        if (at + 2 > bytes.size() || bytes.mid(at, 2).toLower() != "0x") return false;
        at += 2; const auto start = at; quint32 value = 0;
        while (at < bytes.size()) {
            const char byte = bytes[at]; int digit = byte >= '0' && byte <= '9' ? byte - '0'
                : byte >= 'a' && byte <= 'f' ? byte - 'a' + 10 : byte >= 'A' && byte <= 'F' ? byte - 'A' + 10 : -1;
            if (digit < 0) break;
            if (at - start >= (words ? 4 : 2)) return false;
            value = value * 16 + unsigned(digit); ++at;
        }
        if (at == start || (at < bytes.size() && !whitespace(bytes[at]) && bytes[at] != ',' && bytes[at] != '}')) return false;
        pixels.append(char(value & 255)); if (words) pixels.append(char(value >> 8));
    }
    *clean = "#define image_width " + QByteArray::number(width) + "\n#define image_height " + QByteArray::number(height)
        + "\nstatic unsigned char image_bits[] = {\n";
    const quint64 row = (quint64(width) + 7) / 8;
    for (quint64 y = 0; y < height; ++y)
        for (quint64 x = 0; x < row; ++x) {
            const auto byte = quint8(pixels[qsizetype(y * stride + x)]);
            clean->append("0x"); clean->append(QByteArray::number(byte, 16).rightJustified(2, '0')); clean->append(',');
            if (clean->size() > limits.encodedBytes) return false;
        }
    clean->append("\n};\n"); return clean->size() <= limits.encodedBytes;
}
inline bool xpm(const QByteArray& bytes, const Limits& limits, QByteArray* clean)
{
    const auto marker = bytes.indexOf("/* XPM */");
    if (marker < 0 || marker > 65536) return false;
    QList<QByteArray> strings; qsizetype at = marker + 9;
    while (at < bytes.size()) {
        if (bytes.mid(at, 2) == "/*") { const auto end = bytes.indexOf("*/", at + 2); if (end < 0) return false; at = end + 2; continue; }
        if (bytes[at++] != '"') continue;
        QByteArray value; bool ended = false;
        while (at < bytes.size()) {
            const char byte = bytes[at++];
            if (byte == '"') { ended = true; break; }
            value.append(byte);
            if (byte == '\\' && at < bytes.size()) value.append(bytes[at++]);
            if (value.size() > limits.encodedBytes) return false;
        }
        if (!ended || strings.size() >= 65536 + 16384 + 257) return false;
        strings.append(value);
    }
    if (strings.isEmpty()) return false;
    qsizetype position = 0; quint32 width, height, colors, chars;
    const auto& header = strings.first();
    if (!number(header, &position, &width) || !number(header, &position, &height)
     || !number(header, &position, &colors) || !number(header, &position, &chars)
     || !dimensions(width, height, limits) || !colors || colors > 65536 || !chars || chars > 16
     || quint64(strings.size()) < 1 + quint64(colors) + height) return false;
    for (quint32 index = 0; index < colors; ++index) if (strings[1 + index].size() <= chars) return false;
    for (quint32 y = 0; y < height; ++y) if (quint64(strings[1 + colors + y].size()) < quint64(width) * chars) return false;
    *clean = bytes; return true;
}
inline int exifOrientation(const QByteArray& payload)
{
    if (!payload.startsWith(QByteArray("Exif\0\0", 6)) || payload.size() > 65536) return 1;
    const auto bytes = payload.mid(6);
    if (bytes.size() < 8) return 1;
    const bool little = bytes.startsWith("II"), big = bytes.startsWith("MM");
    if (!little && !big) return 1;
    const auto shortAt = [&](qsizetype at) { return little ? qFromLittleEndian<quint16>(bytes.constData() + at) : qFromBigEndian<quint16>(bytes.constData() + at); };
    const auto longAt = [&](qsizetype at) { return little ? qFromLittleEndian<quint32>(bytes.constData() + at) : qFromBigEndian<quint32>(bytes.constData() + at); };
    if (shortAt(2) != 42) return 1;
    const quint64 offset = longAt(4);
    if (offset < 8 || offset + 2 > quint64(bytes.size())) return 1;
    const quint16 count = shortAt(qsizetype(offset));
    if (count > 512 || offset + 2 + quint64(count) * 12 > quint64(bytes.size())) return 1;
    for (unsigned index = 0; index < count; ++index) {
        const auto at = qsizetype(offset + 2 + index * 12);
        if (shortAt(at) == 0x112 && shortAt(at + 2) == 3 && longAt(at + 4) == 1) {
            const auto value = shortAt(at + 8); return value >= 1 && value <= 8 ? value : 1;
        }
    }
    return 1;
}
}
