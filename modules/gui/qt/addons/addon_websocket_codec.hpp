// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QByteArray>
#include <QList>
#include <QStringList>
#include <QUrl>
#include <array>
#include <functional>
#include <optional>

namespace vlc::addons {
// Wire parsing only. The host must provide approved, pinned, verified TLS and
// check package/session authority before each network write and publication.
class AddonWebSocketCodec final
{
public:
    static constexpr qsizetype MaximumMessage = 65536, MaximumWire = 131072;
    static constexpr qsizetype MaximumHeader = 16384, MaximumBatch = 32;
    enum class State { Handshake, Open, Closing, Closed, Failed };
    enum class Kind { Text, Binary, Pong, Closed };
    struct Event { Kind kind; QByteArray bytes; quint16 code = 0; };
    struct Batch { QList<Event> events; QList<QByteArray> replies; QString error; bool more = false; };
    struct Handshake { QByteArray bytes, key; QStringList protocols; };
    using Mask = std::array<quint8, 4>;
    using MaskSource = std::function<Mask()>;
    static std::optional<Handshake> handshake(const QUrl&, const QStringList& protocols = {});
    AddonWebSocketCodec(QByteArray key, QStringList protocols = {}, MaskSource = {});
    AddonWebSocketCodec(const AddonWebSocketCodec&) = delete;
    AddonWebSocketCodec& operator=(const AddonWebSocketCodec&) = delete;
    State state() const { return m_state; }
    QString protocol() const { return m_protocol; }
    Batch receive(const QByteArray&);
    Batch eof();
    std::optional<QByteArray> send(bool text, const QByteArray&);
    std::optional<QByteArray> ping(const QByteArray&);
    std::optional<QByteArray> close(quint16 code = 1000, const QByteArray& reason = {});
private:
    static bool utf8(const QByteArray&);
    static bool code(quint16);
    static bool token(const QByteArray&);
    QByteArray frame(quint8 opcode, const QByteArray&);
    bool upgrade(const QByteArray&);
    Batch fail();
    QByteArray m_accept, m_wire, m_fragments;
    QStringList m_protocols;
    QString m_protocol;
    MaskSource m_mask;
    State m_state = State::Handshake;
    quint8 m_fragmentKind = 0;
    unsigned m_fragmentCount = 0;
};
}
