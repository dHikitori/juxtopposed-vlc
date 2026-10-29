// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_store_profile_generation.hpp"
#include <QByteArrayView>
#include <QList>
#include <QMap>
#include <optional>

namespace vlc::addons {
// Host-only control messages. This is not an add-on contract. The socket owner
// must authenticate the profile user before it calls the coordinator.
class StoreProfileProtocol final {
public:
    using Epoch = StoreProfileGeneration::Epoch;
    enum class Kind : quint8 {
        Welcome = 1, Join, Candidate, Confirm, Ready, Begin, Revoke, Revoked,
        Grant, Complete, Hold, Recover, Abort, Available
    };
    struct Message {
        Kind kind = Kind::Welcome;
        QByteArray connection;
        Epoch epoch;
        QByteArray request;
        qint64 deadline = 0;
    };
    static constexpr qsizetype FrameBytes = 96;
    static constexpr qsizetype MaximumBatch = 32;
    static constexpr qsizetype MaximumInput = FrameBytes * MaximumBatch;
    static bool valid(const Message&) noexcept;
    static QByteArray encode(const Message&);
    static std::optional<Message> decode(QByteArrayView);
    class Decoder final {
    public:
        struct Batch { bool ok = true; QList<Message> messages; };
        // A malformed or oversized batch fails the decoder permanently. No
        // message from that batch escapes. The owner must close its socket.
        Batch feed(QByteArrayView);
        bool failed() const noexcept { return m_failed; }
        qsizetype bufferedBytes() const noexcept { return m_pending.size(); }
    private:
        QByteArray m_pending;
        bool m_failed = false;
    };
};

// Memory-only coordinator policy. Use one owner on independent joined work.
// Effects describe work; they do not execute callbacks or grant native tokens.
// No socket, timer, file access, thread, or polling loop lives in this object.
class StoreProfileArbiter final {
public:
    using Protocol = StoreProfileProtocol;
    using Message = Protocol::Message;
    using Kind = Protocol::Kind;
    using Epoch = Protocol::Epoch;
    enum class Phase { Recovering, Ready, Revoking, Granted, Checking };
    struct Effects {
        bool accepted = true;
        QList<Message> send;
        QList<QByteArray> close;
        bool recoveryRequired = false;
        // The owner must verify this exact ready epoch through the actual
        // native fence and saved record before it calls checked().
        std::optional<Message> check;
    };
    static constexpr qsizetype MaximumPeers = 64;
    // The owner supplies a fresh random 16-byte instance nonce for this owner
    // lock lifetime. Connection IDs are allocated here, never by clients.
    explicit StoreProfileArbiter(QByteArray instance);
    StoreProfileArbiter(const StoreProfileArbiter&) = delete;
    StoreProfileArbiter& operator=(const StoreProfileArbiter&) = delete;
    Phase phase() const noexcept { return m_phase; }
    Epoch epoch() const { return m_epoch; }
    qsizetype peers() const noexcept { return m_peers.size(); }
    Effects accept();
    Effects receive(const QByteArray& acceptedConnection, const Message&, qint64 now);
    Effects disconnected(const QByteArray& acceptedConnection);
    // Use a single deadline wake while a mutation is active. No idle poll is
    // required. Time is monotonic microseconds in the writer's clock domain.
    Effects advance(qint64 now);
    Effects checked(const Message& exactCheck, bool coherent, qint64 now);
    // Call only after full recovery under owner lock, store.lock and the
    // exclusive native fence. A different generation at revision zero is
    // required, including after an aborted or disconnected writer.
    Effects recovered(const Epoch&);
private:
    enum class PeerPhase { Connected, Candidate, Ready, Revoking, Drained };
    struct Peer { PeerPhase phase = PeerPhase::Connected; QByteArray join; Epoch offered; };
    struct Pending { Message begin; };
    Effects recover();
    Effects reject(const QByteArray&);
    Effects grantIfDrained();
    bool pendingMatch(const Message&, bool completed) const noexcept;
    const QByteArray m_instance;
    quint64 m_next = 1;
    qint64 m_lastTime = -1;
    Phase m_phase = Phase::Recovering;
    Epoch m_epoch;
    QMap<QByteArray, Peer> m_peers;
    std::optional<Pending> m_pending;
};
}
