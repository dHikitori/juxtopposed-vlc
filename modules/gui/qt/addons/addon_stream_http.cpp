// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_stream_http.hpp"
#include <algorithm>
#include <limits>

namespace vlc::addons {
namespace {
constexpr quint64 Maximum = quint64(std::numeric_limits<qint64>::max());
bool number(const QByteArray& value, quint64* output)
{
    if (value.isEmpty() || value.size() > 19) return false;
    for (const auto byte : value) if (byte < '0' || byte > '9') return false;
    bool ok; *output = value.toULongLong(&ok); return ok && *output <= Maximum;
}
bool printable(const QByteArray& value)
{ for (const unsigned char byte : value) if (byte < 32 || byte > 126) return false; return true; }
bool name(const QByteArray& value)
{
    if (value.isEmpty() || value.size() > 64) return false;
    for (const auto byte : value) if (!(byte >= 'a' && byte <= 'z') && !(byte >= '0' && byte <= '9') && byte != '-') return false;
    return true;
}
bool fields(const QByteArray& input, qsizetype maximum, QByteArray* first, QMap<QByteArray,QByteArray>* output)
{
    if (input.size() > maximum || !input.endsWith("\r\n\r\n")) return false;
    const auto lines = input.left(input.size() - 2).split('\n');
    if (lines.size() > 130 || lines.size() < 2 || !lines.first().endsWith('\r')) return false;
    *first = lines.first().chopped(1); if (!printable(*first) || first->size() > 2048) return false;
    for (int index = 1; index < lines.size() - 1; ++index) {
        const auto line = lines[index]; if (!line.endsWith('\r') || line.size() > 4096) return false;
        const auto colon = line.indexOf(':'); if (colon <= 0) return false;
        const auto key = line.left(colon).toLower();
        const auto value = line.mid(colon + 1, line.size() - colon - 2).trimmed();
        if (!name(key) || !printable(value)) return false;
        // Repeated framing and conditional fields are ambiguous. Cookies are ignored.
        if (output->contains(key)) { if (key == "set-cookie" || key == "warning") continue; return false; }
        output->insert(key, value);
    }
    return true;
}
}
bool AddonStreamHttp::range(const QByteArray& value, Range* output)
{
    if (!value.startsWith("bytes=") || value.size() > 64) return false;
    const auto parts = value.mid(6).split('-'); quint64 first, last = 0;
    if (parts.size() != 2 || !number(parts[0], &first) || (!parts[1].isEmpty() && (!number(parts[1], &last) || last < first))) return false;
    *output = {first, parts[1].isEmpty() ? std::nullopt : std::optional<quint64>(last)}; return true;
}
QByteArray AddonStreamHttp::rangeHeader(const Range& value)
{ return QByteArray("bytes=") + QByteArray::number(value.first) + '-' + (value.last ? QByteArray::number(*value.last) : QByteArray{}); }
bool AddonStreamHttp::strongTag(const QByteArray& value)
{
    if (value.size() < 2 || value.size() > 256 || value.front() != '"' || value.back() != '"') return false;
    for (int index = 1; index < value.size() - 1; ++index) {
        const unsigned char byte = value[index]; if (byte < 33 || byte > 126 || byte == '"') return false;
    }
    return true;
}
bool AddonStreamHttp::request(const QByteArray& input, const QByteArray& host, Request* output)
{
    *output = {}; QByteArray first; QMap<QByteArray,QByteArray> values;
    if (!fields(input, 8192, &first, &values)) return false;
    const auto parts = first.split(' ');
    if (parts.size() != 3 || (parts[0] != "GET" && parts[0] != "HEAD") || parts[2] != "HTTP/1.1"
        || values.value("host") != host || parts[1].isEmpty() || parts[1].size() > 1024 || parts[1][0] != '/') return false;
    for (const auto byte : parts[1]) if (!(byte >= 'A' && byte <= 'Z') && !(byte >= 'a' && byte <= 'z')
        && !(byte >= '0' && byte <= '9') && byte != '/' && byte != '_' && byte != '-') return false;
    static const QList<QByteArray> allowed{"host","range","if-range","if-match","accept","accept-encoding","accept-language","cache-control","pragma","user-agent","connection","icy-metadata","content-length"};
    for (auto it = values.cbegin(); it != values.cend(); ++it) if (!allowed.contains(it.key())) return false;
    if (values.contains("content-length") && values.value("content-length") != "0") return false;
    if (values.contains("range")) { Range value; if (!range(values.value("range"), &value)) return false; output->range = value; }
    if (values.contains("if-range")) {
        if (!output->range || !strongTag(values.value("if-range"))) return false;
        output->ifRange = values.value("if-range");
    }
    if (values.contains("if-match")) {
        if (!strongTag(values.value("if-match"))) return false;
        output->ifMatch = values.value("if-match");
    }
    output->method = parts[0]; output->path = parts[1]; return true;
}
bool AddonStreamHttp::response(const QByteArray& input, Response* output)
{
    *output = {}; QByteArray first; QMap<QByteArray,QByteArray> values;
    if (!fields(input, 32768, &first, &values) || first.size() < 12 || (first.left(9) != "HTTP/1.1 " && first.left(9) != "HTTP/1.0 ")) return false;
    const auto code = first.mid(9,3); quint64 status;
    if (!number(code, &status) || status < 100 || status > 599 || (first.size() > 12 && first[12] != ' ')) return false;
    output->status = int(status);
    if (values.contains("content-length")) { quint64 size; if (!number(values.value("content-length"), &size)) return false; output->length = size; }
    if (values.contains("transfer-encoding")) {
        if (values.value("transfer-encoding").toLower() != "chunked" || output->length) return false;
        output->chunked = true;
    }
    if (values.contains("content-encoding") && values.value("content-encoding").toLower() != "identity") return false;
    if (values.contains("icy-metaint")) return false;
    if (values.contains("content-range")) {
        const auto value = values.value("content-range"); if (!value.startsWith("bytes ")) return false;
        const auto parts = value.mid(6).split('/'); if (parts.size() != 2) return false;
        Range range; if (!AddonStreamHttp::range(QByteArray("bytes=") + parts[0], &range) || !range.last) return false;
        if (parts[1] != "*") { quint64 total; if (!number(parts[1], &total) || total <= *range.last) return false; output->total = total; }
        output->range = range;
        if (output->length && *output->length != *range.last - range.first + 1) return false;
    }
    if ((output->status == 206) != output->range.has_value()) return false;
    output->type = values.value("content-type").split(';').first().trimmed().toLower();
    output->acceptsRanges = values.value("accept-ranges").toLower() == "bytes" || output->status == 206;
    if (output->type.size() > 128) return false;
    if (!output->type.isEmpty()) {
        if (output->type.count('/') != 1 || output->type.startsWith('/') || output->type.endsWith('/')) return false;
        for (const auto c : output->type) if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9')
            && !QByteArray("/!#$&^_.+-").contains(c)) return false;
    }
    output->location = values.value("location"); if (output->location.size() > 8192) return false;
    if (strongTag(values.value("etag"))) output->etag = values.value("etag");
    return true;
}
bool AddonStreamHttp::Chunks::feed(const QByteArray& bytes, QByteArray* output, qsizetype limit)
{
    output->clear();
    if (m_failed || bytes.size() > 65536 || m_buffer.size() > 65536 - bytes.size() || limit < 1 || limit > 65536
        || (m_done && !bytes.isEmpty())) return m_failed = true, false;
    m_buffer += bytes;
    const auto bad = [this] { m_failed = true; return false; };
    while (!m_done && output->size() < limit) {
        if (m_trailers) {
            const auto end = m_buffer.indexOf("\r\n");
            if (end < 0) return m_buffer.size() + m_trailerBytes <= 8192 || bad();
            const auto line = m_buffer.left(end); m_buffer.remove(0,end+2); m_trailerBytes += end + 2;
            if (m_trailerBytes > 8192 || !printable(line)) return bad();
            if (line.isEmpty()) { m_done = true; if (!m_buffer.isEmpty()) return bad(); break; }
            const auto colon = line.indexOf(':'); if (colon <= 0 || !name(line.left(colon).toLower())) return bad();
            continue;
        }
        if (m_crlf) {
            if (m_buffer.size() < 2) break;
            if (!m_buffer.startsWith("\r\n")) return bad();
            m_buffer.remove(0,2); m_crlf = false; m_size = true;
        }
        if (m_size) {
            const auto end = m_buffer.indexOf("\r\n"); if (end < 0) return m_buffer.size() <= 128 || bad();
            const auto line = m_buffer.left(end); if (line.isEmpty() || line.size() > 128 || !printable(line)) return bad();
            const auto count = line.split(';').first(); if (count.isEmpty() || count.size() > 16) return bad();
            for (const auto c : count) if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f') && !(c >= 'A' && c <= 'F')) return bad();
            bool ok; m_left = count.toULongLong(&ok,16); if (!ok || m_left > Maximum) return bad();
            m_buffer.remove(0,end+2); m_size = false; if (!m_left) { m_trailers = true; continue; }
        }
        const auto take = qsizetype(std::min<quint64>({m_left, quint64(m_buffer.size()), quint64(limit-output->size())}));
        if (!take) break;
        output->append(m_buffer.constData(),take); m_buffer.remove(0,take); m_left -= quint64(take);
        if (!m_left) m_crlf = true;
    }
    return true;
}
}
