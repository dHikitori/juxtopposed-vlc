// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_store_profile_protocol.hpp"
#include <QtEndian>
#include <limits>
#include <utility>

namespace vlc::addons {
namespace {
using Protocol = StoreProfileProtocol;
using Message = Protocol::Message;
using Kind = Protocol::Kind;
bool nonce(const QByteArray& value) noexcept { return value.size() == 16 && value != QByteArray(16, '\0'); }
bool emptyEpoch(const Protocol::Epoch& epoch) noexcept { return epoch.generation.isEmpty() && epoch.revision == 0; }
Message simple(Kind kind, const QByteArray& connection) { return {kind, connection, {}, {}, 0}; }
}
bool StoreProfileProtocol::valid(const Message& value) noexcept
{
    if (!nonce(value.connection)) return false;
    switch (value.kind) {
        case Kind::Welcome: case Kind::Recover: case Kind::Available:
            return emptyEpoch(value.epoch) && value.request.isEmpty() && value.deadline == 0;
        case Kind::Join: case Kind::Hold:
            return emptyEpoch(value.epoch) && nonce(value.request) && value.deadline == 0;
        case Kind::Candidate: case Kind::Confirm: case Kind::Ready:
            return nonce(value.epoch.generation) && nonce(value.request) && value.deadline == 0;
        case Kind::Begin: case Kind::Revoke: case Kind::Revoked: case Kind::Grant: case Kind::Complete: case Kind::Abort:
            return nonce(value.epoch.generation) && nonce(value.request) && value.deadline > 0;
    }
    return false;
}
QByteArray StoreProfileProtocol::encode(const Message& value)
{
    if (!valid(value)) return {};
    QByteArray bytes(FrameBytes, '\0'); bytes.replace(0, 8, QByteArrayLiteral("HAKONEP1")); bytes[8] = char(value.kind);
    bytes.replace(16, 16, value.connection);
    if (!value.epoch.generation.isEmpty()) bytes.replace(32, 16, value.epoch.generation);
    qToBigEndian<quint64>(value.epoch.revision, bytes.data() + 48);
    if (!value.request.isEmpty()) bytes.replace(56, 16, value.request);
    qToBigEndian<quint64>(quint64(value.deadline), bytes.data() + 72);
    return bytes;
}
std::optional<StoreProfileProtocol::Message> StoreProfileProtocol::decode(QByteArrayView bytes)
{
    if (bytes.size() != FrameBytes || bytes.first(8) != QByteArrayView("HAKONEP1", 8)
        || bytes.sliced(9, 7) != QByteArrayView(QByteArray(7, '\0'))
        || bytes.sliced(80, 16) != QByteArrayView(QByteArray(16, '\0'))) return {};
    const auto deadline = qFromBigEndian<quint64>(bytes.data() + 72);
    if (deadline > quint64(std::numeric_limits<qint64>::max())) return {};
    Message value{Kind(quint8(bytes.at(8))), bytes.sliced(16, 16).toByteArray(),
        {bytes.sliced(32, 16).toByteArray(), qFromBigEndian<quint64>(bytes.data() + 48)},
        bytes.sliced(56, 16).toByteArray(), qint64(deadline)};
    if (value.epoch.generation == QByteArray(16, '\0')) value.epoch.generation.clear();
    if (value.request == QByteArray(16, '\0')) value.request.clear();
    return valid(value) ? std::optional<Message>{std::move(value)} : std::nullopt;
}
StoreProfileProtocol::Decoder::Batch StoreProfileProtocol::Decoder::feed(QByteArrayView bytes)
{
    Batch batch;
    const auto fail = [&] { m_failed = true; m_pending.clear(); batch.ok = false; batch.messages.clear(); };
    if (m_failed || bytes.size() > MaximumInput) { fail(); return batch; }
    m_pending.append(bytes.data(), bytes.size());
    qsizetype used = 0;
    while (m_pending.size() - used >= FrameBytes) {
        auto message = decode(QByteArrayView(m_pending).sliced(used, FrameBytes));
        if (!message) { fail(); return batch; }
        batch.messages.push_back(std::move(*message)); used += FrameBytes;
    }
    m_pending.remove(0, used);
    return batch;
}
StoreProfileArbiter::StoreProfileArbiter(QByteArray instance) : m_instance(std::move(instance)) {}
StoreProfileArbiter::Effects StoreProfileArbiter::accept()
{
    Effects effect;
    if (!nonce(m_instance) || m_peers.size() >= MaximumPeers || m_next == std::numeric_limits<quint64>::max()) {
        effect.accepted = false; return effect;
    }
    auto id = m_instance.first(8); id.resize(16); qToBigEndian<quint64>(m_next++, id.data() + 8);
    m_peers.insert(id, Peer{}); effect.send.push_back(simple(Kind::Welcome, id)); return effect;
}
StoreProfileArbiter::Effects StoreProfileArbiter::recover()
{
    Effects effect;
    if (m_phase == Phase::Recovering) return effect;
    m_phase = Phase::Recovering; m_pending.reset(); effect.recoveryRequired = true;
    for (auto peer = m_peers.begin(); peer != m_peers.end(); ++peer) {
        peer.value() = Peer{}; effect.send.push_back(simple(Kind::Recover, peer.key()));
    }
    return effect;
}
StoreProfileArbiter::Effects StoreProfileArbiter::advance(qint64 now)
{
    if (now < 0 || now < m_lastTime) { auto effect = recover(); effect.accepted = false; return effect; }
    m_lastTime = now;
    return m_pending && now >= m_pending->begin.deadline ? recover() : Effects{};
}
StoreProfileArbiter::Effects StoreProfileArbiter::disconnected(const QByteArray& connection)
{
    auto peer = m_peers.find(connection);
    if (peer == m_peers.end()) { Effects effect; effect.accepted = false; return effect; }
    const bool uncertain = m_pending && (m_pending->begin.connection == connection || peer->phase == PeerPhase::Revoking);
    m_peers.erase(peer);
    return uncertain ? recover() : Effects{};
}
StoreProfileArbiter::Effects StoreProfileArbiter::reject(const QByteArray& connection)
{
    auto effect = disconnected(connection); effect.accepted = false; effect.close.push_back(connection); return effect;
}
bool StoreProfileArbiter::pendingMatch(const Message& value, bool completed) const noexcept
{
    if (!m_pending) return false;
    const auto& begin = m_pending->begin;
    if (value.request != begin.request || value.deadline != begin.deadline || value.epoch.generation != begin.epoch.generation) return false;
    return completed ? begin.epoch.revision != std::numeric_limits<quint64>::max() && value.epoch.revision == begin.epoch.revision + 1
        : value.epoch.revision == begin.epoch.revision;
}
StoreProfileArbiter::Effects StoreProfileArbiter::grantIfDrained()
{
    if (m_phase != Phase::Revoking || !m_pending) return {};
    for (const auto& peer : std::as_const(m_peers)) if (peer.phase == PeerPhase::Revoking) return {};
    m_phase = Phase::Granted;
    Effects effect; auto grant = m_pending->begin; grant.kind = Kind::Grant; effect.send.push_back(std::move(grant)); return effect;
}
StoreProfileArbiter::Effects StoreProfileArbiter::receive(const QByteArray& connection, const Message& value, qint64 now)
{
    auto peer = m_peers.find(connection);
    if (peer == m_peers.end() || value.connection != connection || !Protocol::valid(value)) return reject(connection);
    auto timed = advance(now);
    if (!timed.accepted || timed.recoveryRequired) return timed;
    if (value.kind == Kind::Join && (peer->phase == PeerPhase::Connected || peer->phase == PeerPhase::Drained)) {
        Effects effect;
        if (m_phase != Phase::Ready) { auto hold = value; hold.kind = Kind::Hold; effect.send.push_back(std::move(hold)); return effect; }
        peer->phase = PeerPhase::Candidate; peer->join = value.request; peer->offered = m_epoch;
        auto candidate = value; candidate.kind = Kind::Candidate; candidate.epoch = m_epoch;
        effect.send.push_back(std::move(candidate)); return effect;
    }
    if (value.kind == Kind::Confirm && value.request == peer->join && value.epoch == peer->offered) {
        if (m_phase != Phase::Ready) return {};
        if (peer->phase == PeerPhase::Candidate || peer->phase == PeerPhase::Ready) {
            peer->phase = PeerPhase::Ready; Effects effect; auto ready = value; ready.kind = Kind::Ready;
            effect.send.push_back(std::move(ready)); return effect;
        }
    }
    if (value.kind == Kind::Begin && m_phase == Phase::Ready && peer->phase == PeerPhase::Ready && value.epoch == m_epoch
        && value.deadline > now && value.deadline - now <= StoreProfileGeneration::MaximumDuration) {
        if (m_epoch.revision == std::numeric_limits<quint64>::max()) return recover();
        m_pending = Pending{value}; m_phase = Phase::Revoking; Effects effect;
        for (auto item = m_peers.begin(); item != m_peers.end(); ++item) {
            if (item->phase != PeerPhase::Ready && item->phase != PeerPhase::Candidate) continue;
            item->phase = PeerPhase::Revoking; auto revoke = value; revoke.kind = Kind::Revoke; revoke.connection = item.key();
            effect.send.push_back(std::move(revoke));
        }
        return effect;
    }
    if (value.kind == Kind::Revoked && pendingMatch(value, false) && (peer->phase == PeerPhase::Revoking || peer->phase == PeerPhase::Drained)) {
        peer->phase = PeerPhase::Drained; return grantIfDrained();
    }
    if (value.kind == Kind::Abort && m_pending && value.connection == m_pending->begin.connection && pendingMatch(value, false)) return recover();
    if (value.kind == Kind::Complete && m_pending && value.connection == m_pending->begin.connection && pendingMatch(value, true)) {
        if (m_phase == Phase::Checking) return {};
        if (m_phase == Phase::Granted) { m_phase = Phase::Checking; Effects effect; effect.check = value; return effect; }
    }
    return reject(connection);
}
StoreProfileArbiter::Effects StoreProfileArbiter::checked(const Message& receipt, bool coherent, qint64 now)
{
    Effects effect;
    if (m_phase != Phase::Checking || !m_pending || receipt.kind != Kind::Complete
        || receipt.connection != m_pending->begin.connection || !pendingMatch(receipt, true) || !Protocol::valid(receipt)) {
        effect.accepted = false; return effect;
    }
    auto timed = advance(now);
    if (!timed.accepted || timed.recoveryRequired) return timed;
    if (!coherent) return recover();
    m_epoch = receipt.epoch; m_phase = Phase::Ready; m_pending.reset();
    for (auto peer = m_peers.begin(); peer != m_peers.end(); ++peer) {
        peer.value() = Peer{}; effect.send.push_back(simple(Kind::Available, peer.key()));
    }
    return effect;
}
StoreProfileArbiter::Effects StoreProfileArbiter::recovered(const Epoch& epoch)
{
    Effects effect;
    if (!nonce(m_instance) || m_phase != Phase::Recovering || !nonce(epoch.generation)
        || epoch.revision != 0 || epoch.generation == m_epoch.generation) {
        effect.accepted = false; return effect;
    }
    m_epoch = epoch; m_phase = Phase::Ready; m_pending.reset();
    for (auto peer = m_peers.begin(); peer != m_peers.end(); ++peer) {
        peer.value() = Peer{}; effect.send.push_back(simple(Kind::Available, peer.key()));
    }
    return effect;
}
}
