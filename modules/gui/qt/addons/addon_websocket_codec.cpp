// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_websocket_codec.hpp"
#include <QCryptographicHash>
#include <QMap>
#include <QRandomGenerator>
#include <QSet>
#include <QStringDecoder>
#include <cstring>

namespace vlc::addons {
namespace {
AddonWebSocketCodec::Mask randomMask()
{
    AddonWebSocketCodec::Mask mask{}; const auto value = QRandomGenerator::system()->generate();
    std::memcpy(mask.data(), &value, mask.size()); return mask;
}
bool listToken(const QByteArray& value, const QByteArray& wanted)
{
    for (const auto& part : value.split(',')) if (part.trimmed().compare(wanted, Qt::CaseInsensitive) == 0) return true;
    return false;
}
}
bool AddonWebSocketCodec::utf8(const QByteArray& bytes)
{
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless); const QString decoded = decoder.decode(bytes);
    Q_UNUSED(decoded); return !decoder.hasError();
}
bool AddonWebSocketCodec::code(quint16 value)
{
    return (value >= 1000 && value <= 1014 && value != 1004 && value != 1005 && value != 1006)
        || (value >= 3000 && value <= 4999);
}
bool AddonWebSocketCodec::token(const QByteArray& bytes)
{
    if (bytes.isEmpty() || bytes.size() > 128) return false;
    for (const unsigned char c : bytes) if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
        || QByteArrayView("!#$%&'*+-.^_`|~").contains(char(c)))) return false;
    return true;
}
std::optional<AddonWebSocketCodec::Handshake> AddonWebSocketCodec::handshake(const QUrl& url, const QStringList& protocols)
{
    if (!url.isValid() || url.scheme() != QLatin1String("wss") || url.host().isEmpty() || (!url.userInfo().isEmpty() || url.authority().contains(QLatin1Char('@')))
        || url.authority(QUrl::FullyEncoded).contains(QLatin1Char('@')) || url.hasFragment()
        || url.port(443) != 443 || url.toEncoded().size() > 8192 || protocols.size() > 8) return {};
    QSet<QString> unique;
    for (const auto& protocol : protocols) {
        if (protocol.size() > 128 || protocol.toLatin1() != protocol.toUtf8() || !token(protocol.toLatin1()) || unique.contains(protocol)) return {};
        unique.insert(protocol);
    }
    QByteArray nonce; nonce.reserve(16);
    for (int i = 0; i < 4; ++i) { const auto mask = randomMask(); nonce.append(reinterpret_cast<const char*>(mask.data()), mask.size()); }
    const QByteArray key = nonce.toBase64();
    QByteArray path = url.path(QUrl::FullyEncoded).toUtf8(); if (path.isEmpty()) path = "/";
    if (url.hasQuery()) { path += '?'; path += url.query(QUrl::FullyEncoded).toUtf8(); }
    if (!path.startsWith('/')) return {};
    const QByteArray host = url.authority(QUrl::FullyEncoded).toUtf8();
    const QByteArray target = path + host;
    for (const unsigned char c : target) if (c <= 32 || c >= 127 || c == '\\') return {};
    QByteArray bytes = "GET " + path + " HTTP/1.1\r\nHost: " + host
        + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key + "\r\nSec-WebSocket-Version: 13\r\n";
    if (!protocols.isEmpty()) bytes += "Sec-WebSocket-Protocol: " + protocols.join(QStringLiteral(", ")).toLatin1() + "\r\n";
    bytes += "\r\n"; if (bytes.size() > MaximumHeader) return {};
    return Handshake{bytes, key, protocols};
}
AddonWebSocketCodec::AddonWebSocketCodec(QByteArray key, QStringList protocols, MaskSource mask)
    : m_protocols(std::move(protocols)), m_mask(mask ? std::move(mask) : MaskSource(randomMask))
{
    if (key.size() != 24 || m_protocols.size() > 8) { m_state = State::Failed; return; }
    const auto decoded = QByteArray::fromBase64(key);
    if (decoded.size() != 16 || decoded.toBase64() != key) { m_state = State::Failed; return; }
    QSet<QString> unique;
    for (const auto& protocol : m_protocols) {
        if (protocol.size() > 128 || protocol.toLatin1() != protocol.toUtf8() || !token(protocol.toLatin1()) || unique.contains(protocol)) { m_state = State::Failed; return; }
        unique.insert(protocol);
    }
    const QByteArray input = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    m_accept = QCryptographicHash::hash(input, QCryptographicHash::Sha1).toBase64();
}
bool AddonWebSocketCodec::upgrade(const QByteArray& bytes)
{
    if (bytes.size() > MaximumHeader || !bytes.endsWith("\r\n\r\n")) return false;
    const auto lines = bytes.left(bytes.size() - 4).split('\n'); if (lines.size() < 2 || lines.size() > 65) return false;
    auto status = lines[0]; if (!status.endsWith('\r')) return false; status.chop(1);
    for (const unsigned char c : status) if (c < 32 || c == 127) return false;
    if (!status.startsWith("HTTP/1.1 101") || (status.size() > 12 && status[12] != ' ')) return false;
    QMap<QByteArray, QByteArray> headers;
    for (qsizetype i = 1; i < lines.size(); ++i) {
        auto line = lines[i];
        // The final header has no CR after the closing delimiter was removed.
        if (i + 1 < lines.size()) { if (!line.endsWith('\r')) return false; line.chop(1); }
        if (line.isEmpty() || line.startsWith(' ') || line.startsWith('\t')) return false;
        for (const unsigned char c : line) if ((c < 32 && c != '\t') || c == 127) return false;
        const auto colon = line.indexOf(':'); if (colon <= 0) return false;
        const auto name = line.left(colon).toLower(); const auto value = line.mid(colon + 1).trimmed();
        if (!token(name) || headers.contains(name)) return false;
        headers.insert(name, value);
    }
    for (const auto& part : headers.value("connection").split(',')) if (!token(part.trimmed())) return false;
    if (headers.value("upgrade").compare("websocket", Qt::CaseInsensitive) != 0 || !listToken(headers.value("connection"), "upgrade")
        || headers.value("sec-websocket-accept") != m_accept || headers.contains("sec-websocket-extensions")
        || headers.contains("transfer-encoding") || (headers.contains("content-length") && headers.value("content-length") != "0")) return false;
    if (headers.contains("sec-websocket-protocol")) {
        const auto protocol = headers.value("sec-websocket-protocol");
        if (!token(protocol) || !m_protocols.contains(QString::fromLatin1(protocol))) return false;
        m_protocol = QString::fromLatin1(protocol);
    }
    return true;
}
QByteArray AddonWebSocketCodec::frame(quint8 opcode, const QByteArray& bytes)
{
    QByteArray result; result.reserve(bytes.size() + 8); result.append(char(0x80 | opcode));
    if (bytes.size() < 126) result.append(char(0x80 | bytes.size()));
    else if (bytes.size() <= 65535) { result.append(char(0xfe)); result.append(char(bytes.size() >> 8)); result.append(char(bytes.size())); }
    else {
        result.append(char(0xff)); const auto length = quint64(bytes.size());
        for (int i = 7; i >= 0; --i) result.append(char(length >> (i * 8)));
    }
    const auto mask = m_mask(); result.append(reinterpret_cast<const char*>(mask.data()), mask.size());
    for (qsizetype i = 0; i < bytes.size(); ++i) result.append(char(quint8(bytes[i]) ^ mask[i % 4]));
    return result;
}
AddonWebSocketCodec::Batch AddonWebSocketCodec::fail()
{
    m_state = State::Failed; m_wire.clear(); m_fragments.clear(); m_fragmentKind = 0;
    return {{}, {}, QStringLiteral("The session connection sent data that is not valid."), false};
}
AddonWebSocketCodec::Batch AddonWebSocketCodec::receive(const QByteArray& bytes)
{
    if (m_state == State::Failed) return fail();
    if (m_state == State::Closed) return {};
    if (bytes.size() > MaximumWire || m_wire.size() > MaximumWire - bytes.size()) return fail();
    m_wire += bytes; Batch batch;
    if (m_state == State::Handshake) {
        const auto end = m_wire.indexOf("\r\n\r\n");
        if (end < 0) return m_wire.size() >= MaximumHeader ? fail() : batch;
        if (end + 4 > MaximumHeader || !upgrade(m_wire.left(end + 4))) return fail();
        m_wire.remove(0, end + 4); m_state = State::Open;
    }
    qsizetype delivered = 0; unsigned count = 0;
    while (count < MaximumBatch && delivered < MaximumMessage) {
        if (m_wire.size() < 2) break;
        const auto first = quint8(m_wire[0]), second = quint8(m_wire[1]); const bool final = first & 0x80;
        const quint8 opcode = first & 0x0f; const bool control = opcode >= 8;
        if ((first & 0x70) || (second & 0x80) || (opcode != 0 && opcode != 1 && opcode != 2 && opcode != 8 && opcode != 9 && opcode != 10)) return fail();
        quint64 length = second & 0x7f; qsizetype header = 2;
        if (length == 126) {
            if (m_wire.size() < 4) break;
            length = (quint64(quint8(m_wire[2])) << 8) | quint8(m_wire[3]); header = 4;
            if (length < 126) return fail();
        } else if (length == 127) {
            if (m_wire.size() < 10) break;
            length = 0; for (int i = 2; i < 10; ++i) length = (length << 8) | quint8(m_wire[i]); header = 10;
            if (length <= 65535) return fail();
        }
        if (length > quint64(MaximumMessage) || (control && (!final || length > 125))) return fail();
        if (m_wire.size() - header < qsizetype(length)) break;
        if (!control && final && m_state == State::Open) {
            const auto assembled = qsizetype(length) + (opcode == 0 && m_fragmentKind ? m_fragments.size() : 0);
            if (assembled > MaximumMessage) return fail();
            if (delivered && assembled > MaximumMessage - delivered) { batch.more = true; return batch; }
        }
        const QByteArray payload = m_wire.mid(header, qsizetype(length)); m_wire.remove(0, header + qsizetype(length)); ++count;
        if (opcode == 8) {
            if (payload.size() == 1) return fail();
            quint16 status = 1005; QByteArray reason;
            if (payload.size() >= 2) {
                status = (quint16(quint8(payload[0])) << 8) | quint8(payload[1]); reason = payload.mid(2);
                if (!code(status) || !utf8(reason)) return fail();
            }
            if (m_state == State::Open) batch.replies.append(frame(8, payload));
            batch.events.append({Kind::Closed, reason, status}); m_state = State::Closed; m_wire.clear(); m_fragments.clear(); return batch;
        }
        if (opcode == 9) { batch.replies.append(frame(10, payload)); continue; }
        if (opcode == 10) { batch.events.append({Kind::Pong, payload, 0}); continue; }
        if (m_state == State::Closing) continue;
        if (opcode == 0) {
            if (!m_fragmentKind || ++m_fragmentCount > 16 || m_fragments.size() > MaximumMessage - payload.size()) return fail();
            m_fragments += payload;
            if (final) {
                if (m_fragmentKind == 1 && !utf8(m_fragments)) return fail();
                delivered += m_fragments.size(); batch.events.append({m_fragmentKind == 1 ? Kind::Text : Kind::Binary, m_fragments, 0});
                m_fragments.clear(); m_fragmentKind = 0; m_fragmentCount = 0;
            }
        } else {
            if (m_fragmentKind) return fail();
            if (!final) { m_fragmentKind = opcode; m_fragmentCount = 1; m_fragments = payload; }
            else {
                if (opcode == 1 && !utf8(payload)) return fail();
                delivered += payload.size(); batch.events.append({opcode == 1 ? Kind::Text : Kind::Binary, payload, 0});
            }
        }
    }
    batch.more = m_wire.size() >= 2 && (count >= MaximumBatch || delivered >= MaximumMessage); return batch;
}
AddonWebSocketCodec::Batch AddonWebSocketCodec::eof()
{
    if (m_state == State::Closed || m_state == State::Failed) return {};
    auto result = fail(); result.events.append({Kind::Closed, {}, 1006}); return result;
}
std::optional<QByteArray> AddonWebSocketCodec::send(bool text, const QByteArray& bytes)
{
    if (m_state != State::Open || bytes.size() > MaximumMessage || (text && !utf8(bytes))) return {};
    return frame(text ? 1 : 2, bytes);
}
std::optional<QByteArray> AddonWebSocketCodec::ping(const QByteArray& bytes)
{
    if (m_state != State::Open || bytes.size() > 125) return {};
    return frame(9, bytes);
}
std::optional<QByteArray> AddonWebSocketCodec::close(quint16 status, const QByteArray& reason)
{
    if (m_state != State::Open || !code(status) || reason.size() > 123 || !utf8(reason)) return {};
    QByteArray payload; payload.append(char(status >> 8)); payload.append(char(status)); payload += reason;
    m_state = State::Closing; m_fragments.clear(); m_fragmentKind = 0;
    return frame(8, payload);
}
}
