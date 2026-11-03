// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "addon_native_session_player.hpp"
#include <QPointer>
#include <QThread>
#include <vlc_common.h>
#include <vlc_addon_player.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <limits>

namespace vlc::addons {
namespace {
struct Lock { vlc_player_t* player; explicit Lock(vlc_player_t* p):player(p) { vlc_player_Lock(p); } ~Lock() { vlc_player_Unlock(player); } };
vlc_addon_session_id native(SessionPlayerId v) { return {v.high,v.low}; }
SessionPlayerId copied(vlc_addon_session_id v) { return {v.high,v.low}; }
vlc_addon_session_stamp native(const SessionPlayerStamp& s) { return {native(s.owner),s.media,s.input,s.revision,s.localRevision}; }
SessionPlayerStamp copied(const vlc_addon_session_stamp& s) { return {copied(s.owner),s.media_generation,s.input_generation,s.transport_revision,s.local_revision}; }
SessionPlayerResult copied(vlc_addon_session_status result) {
    switch(result) {
        case VLC_ADDON_SESSION_OK:return SessionPlayerResult::Accepted;
        case VLC_ADDON_SESSION_INVALID:return SessionPlayerResult::Invalid;
        case VLC_ADDON_SESSION_STALE:return SessionPlayerResult::Stale;
        case VLC_ADDON_SESSION_NO_INPUT:return SessionPlayerResult::NoInput;
        case VLC_ADDON_SESSION_DENIED:return SessionPlayerResult::Denied;
        case VLC_ADDON_SESSION_BUSY:return SessionPlayerResult::Busy;
        case VLC_ADDON_SESSION_LIMIT:return SessionPlayerResult::Limit;
        case VLC_ADDON_SESSION_EXHAUSTED:return SessionPlayerResult::Exhausted;
        case VLC_ADDON_SESSION_CLOSED:return SessionPlayerResult::Closed;
        case VLC_ADDON_SESSION_NO_MEMORY:return SessionPlayerResult::NoMemory;
    }
    return SessionPlayerResult::Invalid;
}
}
struct AddonNativeSessionPlayer::Mailbox {
    std::mutex mutex;
    std::array<vlc_addon_session_snapshot,64> values{};
    unsigned size=0;
    bool overflow=false;
    static void completed(const vlc_addon_session_snapshot* value,void* opaque) {
        auto* box=static_cast<Mailbox*>(opaque);
        std::lock_guard<std::mutex> lock(box->mutex);
        if (box->size==box->values.size()) { box->overflow=true; return; }
        box->values[box->size++]=*value;
    }
};
struct AddonNativeSessionPlayer::Native {
    std::mutex mutex;
    vlc_player_t* player=nullptr;
    SessionPlayerId lease;
};
AddonNativeSessionPlayer::AddonNativeSessionPlayer(vlc_player_t* player,QObject* parent)
    :QObject(parent),m_native(std::make_shared<Native>()),m_mailbox(std::make_unique<Mailbox>())
{
    if (!player) return;
    Lock lock(player);
    m_listener=vlc_addon_player_AddListener(player,&Mailbox::completed,m_mailbox.get());
    if (m_listener) m_native->player=player;
}
AddonNativeSessionPlayer::~AddonNativeSessionPlayer() { close(); }
bool AddonNativeSessionPlayer::available() const noexcept
{ std::lock_guard<std::mutex> lock(m_native->mutex); return m_native->player!=nullptr; }
void AddonNativeSessionPlayer::close()
{
    Q_ASSERT(QThread::currentThread()==thread());
    std::lock_guard<std::mutex> gate(m_native->mutex);
    auto* player=m_native->player; const auto lease=m_native->lease;
    if (!player) return;
    Lock lock(player);
    if (lease) vlc_addon_player_Revoke(player,native(lease));
    vlc_addon_player_RemoveListener(player,m_listener);
    m_listener=nullptr; m_native->player=nullptr; m_native->lease={};
}
SessionPlayerSnapshot AddonNativeSessionPlayer::snapshot()
{
    Q_ASSERT(QThread::currentThread()==thread());
    SessionPlayerSnapshot result; std::lock_guard<std::mutex> gate(m_native->mutex);
    auto* player=m_native->player; if (!player) return result;
    Lock lock(player); vlc_addon_session_snapshot s{}; vlc_addon_player_Snapshot(player,&s);
    result.stamp=copied(s.stamp); result.lease=copied(s.lease); result.leaseEpoch=s.lease_epoch; result.sequence=s.last_sequence;
    result.origin=static_cast<SessionPlayerOrigin>(s.origin); result.operation=int(s.operation);
    result.media=s.media; result.input=s.input; result.active=s.active; result.suspended=s.suspended;
    result.closed=s.closed; result.exhausted=s.exhausted; result.rateOwned=s.rate_owned;
    result.baseRate=s.base_rate; result.correctedRate=s.corrected_rate;
    result.leaseDeadlineUs=s.lease_deadline; result.rateDeadlineUs=s.rate_deadline;
    result.paused=vlc_player_IsPaused(player); result.pausable=vlc_player_CanPause(player);
    result.seekable=vlc_player_CanSeek(player); result.rateChange=vlc_player_CanChangeRate(player);
    const auto time=vlc_player_GetTime(player),length=vlc_player_GetLength(player);
    result.positionUs=time==VLC_TICK_INVALID ? -1 : time; result.durationUs=length==VLC_TICK_INVALID ? -1 : length;
    result.rate=vlc_player_GetRate(player); return result;
}
QList<SessionPlayerCompletion> AddonNativeSessionPlayer::completions()
{
    Q_ASSERT(QThread::currentThread()==thread());
    std::array<vlc_addon_session_snapshot,64> values; unsigned size=0; bool overflow=false;
    { std::lock_guard<std::mutex> lock(m_mailbox->mutex); size=m_mailbox->size; overflow=m_mailbox->overflow;
      std::copy_n(m_mailbox->values.begin(),size,values.begin()); m_mailbox->size=0; m_mailbox->overflow=false; }
    if (overflow) { close(); return {}; }
    QList<SessionPlayerCompletion> result; result.reserve(size);
    for (unsigned i=0;i<size;++i) {
        const auto& s=values[i]; if (s.completed_origin!=VLC_ADDON_SESSION_REMOTE || !s.completed_sequence
            || s.completed_operation<VLC_ADDON_SESSION_PLAY || s.completed_operation>VLC_ADDON_SESSION_TEMPORARY_RATE) continue;
        result.append({copied(s.completed_stamp),copied(s.completed_lease),s.completed_sequence,s.completed_lease_epoch,
            static_cast<SessionPlayerOperation>(s.completed_operation),s.completed_success});
    }
    return result;
}
SessionPlayerBackend AddonNativeSessionPlayer::backend()
{
    const QPointer<AddonNativeSessionPlayer> owner(this); const auto gate=m_native;
    SessionPlayerBackend result; result.lifetime=this;
    result.monotonicUs=[] { return vlc_tick_now(); };
    result.snapshot=[owner] { return owner ? owner->snapshot() : SessionPlayerSnapshot{}; };
    result.grant=[gate](const auto& stamp,auto lease,quint32 operations,qint64 duration) {
        std::lock_guard<std::mutex> guard(gate->mutex); auto* player=gate->player;
        if (!player) return SessionPlayerResult::Closed;
        Lock lock(player); const auto expected=native(stamp); vlc_addon_session_snapshot before{};
        vlc_addon_player_Snapshot(player,&before);
        if (before.active && copied(before.lease)!=gate->lease) return SessionPlayerResult::Busy;
        const auto result=copied(vlc_addon_player_Grant(player,&expected,native(lease),operations,duration));
        if (result==SessionPlayerResult::Accepted) gate->lease=lease;
        return result;
    };
    result.renew=[gate](const auto& stamp,auto lease,qint64 duration) {
        std::lock_guard<std::mutex> guard(gate->mutex); auto* player=gate->player;
        if (!player) return SessionPlayerResult::Closed;
        if (lease!=gate->lease) return SessionPlayerResult::Denied;
        Lock lock(player); const auto expected=native(stamp);
        return copied(vlc_addon_player_Renew(player,&expected,native(lease),duration));
    };
    result.resume=[gate](const auto& stamp,auto lease,qint64 duration) {
        std::lock_guard<std::mutex> guard(gate->mutex); auto* player=gate->player;
        if (!player) return SessionPlayerResult::Closed;
        if (lease!=gate->lease) return SessionPlayerResult::Denied;
        Lock lock(player); const auto expected=native(stamp);
        return copied(vlc_addon_player_Resume(player,&expected,native(lease),duration));
    };
    result.revoke=[gate](auto lease) {
        std::lock_guard<std::mutex> guard(gate->mutex); auto* player=gate->player;
        if (!player || lease!=gate->lease) return false;
        Lock lock(player); const bool revoked=vlc_addon_player_Revoke(player,native(lease));
        gate->lease={}; return revoked;
    };
    result.cancellation=[gate](auto lease) {
        return [gate,lease] {
            std::lock_guard<std::mutex> guard(gate->mutex); auto* player=gate->player;
            if (!player || lease!=gate->lease) return;
            Lock lock(player); vlc_addon_player_Revoke(player,native(lease)); gate->lease={};
        };
    };
    result.apply=[gate](const SessionPlayerCommand& value) {
        std::lock_guard<std::mutex> guard(gate->mutex); auto* player=gate->player;
        if (!player) return SessionPlayerResult::Closed;
        if (value.lease!=gate->lease) return SessionPlayerResult::Denied;
        if (value.operation<SessionPlayerOperation::Play || value.operation>SessionPlayerOperation::TemporaryRate
            || !std::isfinite(value.rate) || value.rate>std::numeric_limits<float>::max()) return SessionPlayerResult::Invalid;
        vlc_addon_session_command command{}; command.expected=native(value.expected); command.lease=native(value.lease);
        command.sequence=value.sequence; command.action.operation=static_cast<vlc_addon_session_operation>(value.operation);
        if (value.operation==SessionPlayerOperation::Seek) command.action.parameters.seek_time=value.positionUs;
        else if (value.operation==SessionPlayerOperation::TemporaryRate) command.action.parameters.rate={float(value.rate),value.durationUs};
        Lock lock(player); return copied(vlc_addon_player_Apply(player,&command));
    };
    result.completions=[owner] { return owner ? owner->completions() : QList<SessionPlayerCompletion>{}; };
    return result;
}
}
