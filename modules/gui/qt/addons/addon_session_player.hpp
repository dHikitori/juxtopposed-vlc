// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QList>
#include <QtGlobal>
#include <functional>

namespace vlc::addons {
// These values contain no player pointer, media path, or room credential.
struct SessionPlayerId {
    quint64 high=0,low=0;
    bool operator==(const SessionPlayerId& v) const noexcept { return high==v.high && low==v.low; }
    bool operator!=(const SessionPlayerId& v) const noexcept { return !(*this==v); }
    explicit operator bool() const noexcept { return high || low; }
};
struct SessionPlayerStamp {
    SessionPlayerId owner;
    quint64 media=0,input=0,revision=0,localRevision=0;
    bool operator==(const SessionPlayerStamp& v) const noexcept {
        return owner==v.owner && media==v.media && input==v.input && revision==v.revision && localRevision==v.localRevision;
    }
    bool operator!=(const SessionPlayerStamp& v) const noexcept { return !(*this==v); }
};
enum class SessionPlayerOperation { Play,Pause,Seek,TemporaryRate };
enum class SessionPlayerOrigin { Local,Remote,Core };
enum class SessionPlayerResult { Accepted,Invalid,Stale,NoInput,Denied,Busy,Limit,Exhausted,Closed,NoMemory };
struct SessionPlayerCommand {
    SessionPlayerStamp expected;
    SessionPlayerId lease;
    quint64 sequence=0;
    SessionPlayerOperation operation=SessionPlayerOperation::Pause;
    qint64 positionUs=0,durationUs=0;
    double rate=1;
};
struct SessionPlayerCompletion {
    SessionPlayerStamp expected;
    SessionPlayerId lease;
    quint64 sequence=0,leaseEpoch=0;
    SessionPlayerOperation operation=SessionPlayerOperation::Pause;
    bool success=false;
};
struct SessionPlayerSnapshot {
    SessionPlayerStamp stamp;
    SessionPlayerId lease;
    quint64 leaseEpoch=0,sequence=0;
    SessionPlayerOrigin origin=SessionPlayerOrigin::Core;
    int operation=-1;
    bool media=false,input=false,active=false,suspended=false,closed=true,exhausted=false;
    bool paused=false,pausable=false,seekable=false,rateChange=false,rateOwned=false;
    qint64 positionUs=-1,durationUs=-1,leaseDeadlineUs=0,rateDeadlineUs=0;
    double rate=1,baseRate=1,correctedRate=1;
};
// Call this backend on its owner's Qt thread. Native admission is authoritative.
// Accepted means FIFO admission. A completion proves actual execution.
struct SessionPlayerBackend {
    QObject* lifetime=nullptr;
    std::function<SessionPlayerSnapshot()> snapshot;
    std::function<qint64()> monotonicUs;
    std::function<SessionPlayerResult(const SessionPlayerStamp&,SessionPlayerId,quint32,qint64)> grant;
    std::function<SessionPlayerResult(const SessionPlayerStamp&,SessionPlayerId,qint64)> renew,resume;
    std::function<bool(SessionPlayerId)> revoke;
    // The returned closure uses native state only. It needs no Qt delivery.
    std::function<std::function<void()>(SessionPlayerId)> cancellation;
    std::function<SessionPlayerResult(const SessionPlayerCommand&)> apply;
    std::function<QList<SessionPlayerCompletion>()> completions;
};
}
