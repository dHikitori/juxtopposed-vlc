// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_session_host.hpp"
#include "addon_session_websocket.hpp"
#include "addon_websocket_codec.hpp"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QRandomGenerator>
#include <QThread>
#include <QUuid>
#include <QtEndian>
#include <array>
#include <atomic>
#include <cmath>
#include <chrono>
#include <deque>
#include <mutex>

namespace vlc::addons {
namespace {
std::atomic_uint readers{0},checks{0};
struct Permit { std::atomic_uint& count; explicit Permit(std::atomic_uint& value):count(value) {} ~Permit() { --count; } };
std::shared_ptr<Permit> acquire(std::atomic_uint& count,unsigned limit) {
    auto value=count.load(std::memory_order_acquire);
    while(value<limit) if(count.compare_exchange_weak(value,value+1,std::memory_order_acq_rel)) {
        try { return std::make_shared<Permit>(count); } catch(...) { --count; return {}; }
    }
    return {};
}
QString token(const char* prefix) { return QString::fromLatin1(prefix)+QUuid::createUuid().toString(QUuid::Id128); }
bool check(const std::function<bool()>& fn) noexcept { try { return fn && fn(); } catch (...) { return false; } }
QString epoch(const SessionPlayerStamp& stamp) {
    const QByteArray value=QByteArray::number(stamp.owner.high)+':'+QByteArray::number(stamp.owner.low)+':'+QByteArray::number(stamp.media)+':'+QByteArray::number(stamp.input);
    return QString::fromLatin1(QCryptographicHash::hash(value,QCryptographicHash::Sha256).toHex());
}
QString relayOrigin(QUrl value) { value.setPath(QString{}); value.setQuery(QString{}); value.setFragment(QString{}); if(value.port()==443) value.setPort(-1); return value.toString(QUrl::FullyEncoded); }
SessionPlayerId nativeId() {
    const auto bytes=QUuid::createUuid().toRfc4122(); const auto* data=reinterpret_cast<const uchar*>(bytes.constData());
    return {qFromBigEndian<quint64>(data),qFromBigEndian<quint64>(data+8)};
}
std::optional<SessionPlayerOperation> operation(const QString& name) {
    if(name==QLatin1String("play")) return SessionPlayerOperation::Play;
    if(name==QLatin1String("pause")) return SessionPlayerOperation::Pause;
    if(name==QLatin1String("seek")) return SessionPlayerOperation::Seek;
    if(name==QLatin1String("setRate")) return SessionPlayerOperation::TemporaryRate;
    return {};
}
bool integer(const QJsonValue& value,quint64* output,bool positive=false) {
    if(value.isString()) {
        const auto text=value.toString(); if(text.isEmpty() || text.size()>20 || (text.size()>1 && text.startsWith(QLatin1Char('0')))) return false;
        for(const auto c:text) if(c<QLatin1Char('0') || c>QLatin1Char('9')) return false;
        bool ok=false; const auto n=text.toULongLong(&ok); if(!ok || (positive && n==0)) return false; *output=n; return true;
    }
    if(!value.isDouble()) return false; const auto n=value.toDouble();
    if(!std::isfinite(n) || n<0 || n>9007199254740991.0 || std::floor(n)!=n || (positive && n==0)) return false;
    *output=quint64(n); return true;
}
bool keys(const QJsonObject& value,const QSet<QString>& permitted) { for(auto it=value.begin();it!=value.end();++it) if(!permitted.contains(it.key())) return false; return true; }
bool scope(const AddonNativeAuthority& value,const QString& name) {
    return value.package.manifest.permissionDeclared(name) && value.package.grants.value(name).isObject();
}
bool allowedOperation(const AddonNativeAuthority& value,const QString& name) {
    return operation(name) && value.package.manifest.permissionScope(QStringLiteral("playback.control")).value("operations").toArray().contains(name)
        && value.package.grants.value("playback.control").toObject().value("operations").toArray().contains(name);
}
QString error() { return QStringLiteral("The session is not available or its permission changed."); }
qint64 steadyUs() { return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
bool identifier(const QString& text) {
    if(text.isEmpty() || text.size()>128) return false;
    for(const auto c:text) if(c.category()==QChar::Other_Control) return false;
    return true;
}
AddonHostServices::Result refused() { return {{},error()}; }
}
struct AddonSessionHost::Gate {
    std::mutex mutex; AddonSessionHost* owner=nullptr;
    void post(std::function<void(AddonSessionHost*)> action) {
        std::lock_guard<std::mutex> lock(mutex);
        if(owner) QMetaObject::invokeMethod(owner,[owner=owner,action=std::move(action)] { action(owner); },Qt::QueuedConnection);
    }
};
struct AddonSessionHost::Control {
    struct Message { bool text=false; std::string bytes; size_t jsonBytes=0,encodedChars=0; };
    std::atomic_bool open{true};
    std::mutex mutex;
    std::function<void()> revoke;
    std::deque<Message> messages;
    size_t bytes=0;
    bool ready=false,ended=false,posted=false;
    void cancel() noexcept {
        open.store(false,std::memory_order_release); std::function<void()> action;
        { std::lock_guard<std::mutex> lock(mutex); action.swap(revoke); }
        if(action) try { action(); } catch(...) {}
    }
};
struct AddonSessionHost::Approval {
    QString contribution,room; QUrl relay; QStringList operations,protocols;
    qint64 expires=0; std::optional<AddonNativeAuthority> authority; SessionPlayerStamp stamp;
};
struct AddonSessionHost::Session {
    AddonNativeAuthority authority; SessionPlayerStamp stamp; SessionPlayerId native;
    QString id,room; QUrl relay; QStringList protocols,operations; QSet<QString> seen;
    QString invocation; qint64 invocationDeadline=0; QSet<QString> invocationIds;
    quint64 sequence=0,leaseEpoch=0; qint64 expires=0;
    bool starting=true,connected=false,suspended=false;
    StoreAuthorityWatch watch;
    std::shared_ptr<Control> control=std::make_shared<Control>();
    std::shared_ptr<AddonSessionWebSocket> transport;
    AddonHostServices::Completion opened,receive;
    QPointer<QTimer> receiveTimer;
};
struct AddonSessionHost::Command {
    QString session,id,invocation; SessionPlayerCommand native; qint64 at=0,expires=0;
    bool checking=false,admitted=false; QPointer<QTimer> timer; AddonHostServices::Completion reply;
};
struct AddonSessionHost::State {
    QHash<QString,Approval> approvals;
    QHash<QString,std::shared_ptr<Session>> sessions;
    QHash<QString,Command> commands;
    struct Check { QString session,invocation; std::function<void(bool)> done; };
    QHash<quint64,Check> checks;
    quint64 nextCheck=0;
    QString controller;
    QPointer<QObject> registration;
    TransportFactory transport=[](auto spec,auto events) { return AddonSessionWebSocket::open(std::move(spec),std::move(events)); };
    bool closed=false,mounted=false;
};
AddonSessionHost::AddonSessionHost(SessionPlayerBackend player,AuthoritySource authorities,AddonStreamLifetime::Handle lifetime,QObject* parent)
    :QObject(parent),m_player(std::move(player)),m_authorities(std::move(authorities)),m_authorityLifetime(m_authorities.lifetime),
    m_playerLifetime(m_player.lifetime),m_lifetime(std::move(lifetime)),m_gate(std::make_shared<Gate>()),m_state(std::make_unique<State>()) {
    m_gate->owner=this;
    if(!m_playerLifetime || !m_authorityLifetime || !m_authorities.capture || !m_player.snapshot || !m_player.monotonicUs || !m_player.grant
        || !m_player.renew || !m_player.resume || !m_player.revoke || !m_player.apply || !m_player.cancellation || !m_player.completions || !m_lifetime.current()) m_state->closed=true;
    if(!m_state->closed && m_player.snapshot().closed) m_state->closed=true;
    if(m_authorityLifetime) connect(m_authorityLifetime,&QObject::destroyed,this,&AddonSessionHost::close);
    if(m_playerLifetime) connect(m_playerLifetime,&QObject::destroyed,this,&AddonSessionHost::close);
    m_timer.setInterval(50); connect(&m_timer,&QTimer::timeout,this,&AddonSessionHost::tick); if(!m_state->closed) m_timer.start();
}
AddonSessionHost::~AddonSessionHost() { close(); }
bool AddonSessionHost::setTransportFactoryForTesting(TransportFactory factory) {
    if(m_state->closed || m_state->mounted || !m_state->sessions.isEmpty() || !m_state->approvals.isEmpty() || !factory) return false;
    m_state->transport=std::move(factory); return true;
}
QString AddonSessionHost::prepareApproval(const QString& contribution,const QUrl& relay,const QString& room,const QStringList& operations,const QStringList& protocols) {
    Q_ASSERT(QThread::currentThread()==thread());
    if(m_state->closed || !m_lifetime.current() || m_state->approvals.size()>=8 || contribution.size()>256 || room.isEmpty() || room.size()>256
        || operations.size()>4 || !AddonWebSocketCodec::handshake(relay,protocols)) return {};
    for(const auto c:room) if(c.isNull() || c.category()==QChar::Other_Control) return {};
    QSet<QString> unique; for(const auto& name:operations) if(!operation(name) || unique.contains(name)) return {}; else unique.insert(name);
    const auto permit=acquire(readers,4); if(!permit) return {};
    const auto reader=m_authorities.capture(contribution); if(!reader) return {};
    const auto id=token("hakone-session-approval:");
    m_state->approvals.insert(id,{contribution,room,relay,operations,protocols,m_player.monotonicUs()+30000000,{}, {}});
    const auto gate=m_gate; const auto lifetime=m_lifetime;
    if(!lifetime.startQtWork([reader,gate,lifetime,id] {
        std::optional<AddonNativeAuthority> result; if(lifetime.current()) try { result=reader(); } catch(...) {}
        gate->post([id,result=std::move(result)](AddonSessionHost* owner) mutable { owner->prepared(id,std::move(result)); });
    },[] {},[permit](AddonStreamLifetime::NetworkTicket ticket) { if(!ticket.retain(permit)) throw 0; })) { m_state->approvals.remove(id); return {}; }
    return id;
}
void AddonSessionHost::prepared(const QString& id,std::optional<AddonNativeAuthority> authority) {
    auto it=m_state->approvals.find(id); if(it==m_state->approvals.end() || m_state->closed || !m_lifetime.current()) return;
    if(!authority || !authority->package.enabled || authority->package.recoveryRequired || authority->accountReference.isEmpty()
        || !authority->onInvalidated || !check(authority->live) || authority->globalId!=it->contribution || authority->contribution.type!=QLatin1String("session.controller")
        || authority->contribution.contract!=QLatin1String("session@1") || !authority->contribution.operations.contains(QStringLiteral("run")) || !scope(*authority,QStringLiteral("playback.observe"))
        || !scope(*authority,QStringLiteral("session.background"))) { m_state->approvals.erase(it); emit approvalReady(id); return; }
    QStringList permitted; for(const auto& op:it->operations) if(allowedOperation(*authority,op)) permitted.append(op); it->operations=std::move(permitted);
    const auto origin=relayOrigin(it->relay);
    if(!authority->package.manifest.permissionScope(QStringLiteral("network.websocket")).value("origins").toArray().contains(origin)
        || !authority->package.grants.value("network.websocket").toObject().value("origins").toArray().contains(origin)) {
        m_state->approvals.erase(it); emit approvalReady(id); return;
    }
    it->authority=std::move(authority); it->stamp=m_player.snapshot().stamp; emit approvalReady(id);
}
QVariantMap AddonSessionHost::approval(const QString& id) const {
    const auto it=m_state->approvals.constFind(id);
    if(m_state->closed || it==m_state->approvals.cend() || !it->authority || it->expires<=m_player.monotonicUs() || !check(it->authority->live)) return {};
    QStringList fields{QStringLiteral("mediaEpoch"),QStringLiteral("transportRevision"),QStringLiteral("observedAtUs"),QStringLiteral("origin"),QStringLiteral("originSequence"),QStringLiteral("originOperation"),QStringLiteral("suspended")};
    const auto declared=it->authority->package.manifest.permissionScope(QStringLiteral("playback.observe")).value("fields").toArray();
    const auto granted=it->authority->package.grants.value("playback.observe").toObject().value("fields").toArray();
    for(const auto& name:QStringList{QStringLiteral("positionUs"),QStringLiteral("durationUs"),QStringLiteral("rate"),QStringLiteral("paused"),QStringLiteral("pausable"),QStringLiteral("seekable"),QStringLiteral("rateChange"),
            QStringLiteral("buffering"),QStringLiteral("live"),QStringLiteral("liveStartUs"),QStringLiteral("liveEndUs"),QStringLiteral("outputLatencyUs"),QStringLiteral("outputLatencyUncertaintyUs")})
        if(declared.contains(name) && granted.contains(name)) fields.append(name);
    return {{"name",it->authority->package.manifest.name},{"relay",relayOrigin(it->relay)},{"room",it->room},{"observationFields",fields},
        {"operations",it->operations},{"observeOnlyAllowed",true},{"continuesAfterViewClose",true},{"sharesMediaPaths",false},{"sharesMediaBytes",false}};
}
QString AddonSessionHost::approve(const QString& ticket,const QStringList& operations) {
    const auto found=m_state->approvals.find(ticket);
    if(m_state->closed || !m_lifetime.current() || found==m_state->approvals.end() || !found->authority || !check(found->authority->live) || found->expires<=m_player.monotonicUs()
        || m_state->sessions.size()>=8 || (!operations.isEmpty() && !m_state->controller.isEmpty())) return {};
    QSet<QString> unique; for(const auto& name:operations)
        if(!found->operations.contains(name) || unique.contains(name) || !allowedOperation(*found->authority,name)) return {}; else unique.insert(name);
    const auto approved=*found; m_state->approvals.erase(found);
    auto session=std::make_shared<Session>(); session->id=token("hakone-session:"); session->authority=*approved.authority;
    session->stamp=approved.stamp; session->native=nativeId(); session->relay=approved.relay; session->room=approved.room;
    session->protocols=approved.protocols; session->operations=operations; session->expires=m_player.monotonicUs()+30000000;
    m_state->sessions.insert(session->id,session); if(!operations.isEmpty()) m_state->controller=session->id;
    const auto id=session->id;
    if(!authorize(session,[this,id](bool allowed) { activated(id,allowed); })) { end(id); return {}; }
    return id;
}
void AddonSessionHost::cancelApproval(const QString& id) { m_state->approvals.remove(id); }
bool AddonSessionHost::authorize(const std::shared_ptr<Session>& session,std::function<void(bool)> complete,bool bound) {
    if(m_state->closed || !m_lifetime.current() || !session->control->open.load(std::memory_order_acquire) || m_state->checks.size()>=32 || m_state->nextCheck==UINT64_MAX) return false;
    if(bound && !invocationCurrent(session,session->invocation)) return false;
    const auto permit=acquire(checks,16); if(!permit) return false;
    const auto id=++m_state->nextCheck; m_state->checks.insert(id,{session->id,bound ? session->invocation : QString{},std::move(complete)});
    const auto gate=m_gate; const auto source=session->authority; const auto control=session->control; const auto lifetime=m_lifetime;
    if(lifetime.startQtWork([gate,source,control,lifetime,id] {
        const bool allowed=lifetime.current() && control->open.load(std::memory_order_acquire) && check(source.live) && check(source.current) && check(source.live);
        if(!allowed) control->cancel(); gate->post([id,allowed](AddonSessionHost* owner) { owner->checked(id,allowed); });
    },[control] { control->cancel(); },[permit](AddonStreamLifetime::NetworkTicket ticket) { if(!ticket.retain(permit)) throw 0; })) return true;
    const auto found=m_state->checks.find(id); std::function<void(bool)> retired;
    if(found!=m_state->checks.end()) { retired.swap(found->done); m_state->checks.erase(found); }
    return false;
}
void AddonSessionHost::checked(quint64 id,bool allowed) {
    const auto it=m_state->checks.find(id); if(it==m_state->checks.end()) return;
    const auto session=m_state->sessions.value(it->session); const auto invocation=it->invocation;
    std::function<void(bool)> done; done.swap(it->done); m_state->checks.erase(it);
    if(!invocation.isEmpty() && !invocationCurrent(session,invocation)) allowed=false;
    if(done) done(allowed && !m_state->closed && m_lifetime.current());
}
void AddonSessionHost::activated(const QString& id,bool allowed) {
    const auto session=m_state->sessions.value(id); if(!session) return;
    if(!allowed || !m_lifetime.current() || !check(session->authority.live) || m_player.snapshot().stamp!=session->stamp) { end(id); return; }
    const auto control=session->control;
    const auto revoke=session->operations.isEmpty() ? std::function<void()>{} : m_player.cancellation(session->native);
    if(!session->authority.onInvalidated || (!session->operations.isEmpty() && !revoke)) { end(id); return; }
    {
        std::lock_guard<std::mutex> lock(control->mutex); control->revoke=revoke;
    }
    // Every watch callback repeats the exact native revoke. A swap-once cancel
    // could otherwise return while another caller still waits for the player.
    try {
        session->watch=session->authority.onInvalidated([control,revoke] {
            control->open.store(false,std::memory_order_release);
            if(revoke) revoke();
            control->cancel();
        });
    } catch(...) { if(revoke) revoke(); end(id); return; }
    if(!session->watch || !m_lifetime.current() || !check(session->authority.live) || !control->open.load(std::memory_order_acquire)) {
        if(revoke) revoke(); end(id); return;
    }
    if(!session->operations.isEmpty()) {
        quint32 mask=0; for(const auto& name:session->operations) mask|=quint32(1)<<unsigned(*operation(name));
        if(m_player.grant(session->stamp,session->native,mask,30000000)!=SessionPlayerResult::Accepted) { end(id); return; }
        const auto current=m_player.snapshot(); session->stamp=current.stamp; session->leaseEpoch=current.leaseEpoch;
    }
    if(!session->watch || !m_lifetime.current() || !check(session->authority.live) || !control->open.load(std::memory_order_acquire)) {
        if(revoke) revoke(); end(id); return;
    }
    session->starting=false; emit sessionChanged(id,QStringLiteral("active"));
}
bool AddonSessionHost::resume(const QString& id) {
    const auto session=m_state->sessions.value(id);
    if(m_state->closed || !session || session->starting || !session->suspended || !session->connected || !check(session->authority.live)) return false;
    return authorize(session,[this,id](bool allowed) {
        const auto session=m_state->sessions.value(id); if(!session) return;
        const auto snapshot=m_player.snapshot();
        if(!allowed || !check(session->authority.live) || !session->control->open.load(std::memory_order_acquire) || epoch(snapshot.stamp)!=epoch(session->stamp)
            || m_player.resume(snapshot.stamp,session->native,30000000)!=SessionPlayerResult::Accepted) { end(id); return; }
        const auto current=m_player.snapshot(); session->stamp=current.stamp; session->leaseEpoch=current.leaseEpoch;
        session->expires=m_player.monotonicUs()+30000000; session->suspended=false; emit sessionChanged(id,QStringLiteral("active"));
    });
}
void AddonSessionHost::end(const QString& id) {
    const auto session=m_state->sessions.take(id); if(!session) return;
    session->control->cancel(); if(session->transport) session->transport->cancel();
    if(session->receiveTimer) { session->receiveTimer->stop(); session->receiveTimer->deleteLater(); }
    QList<AddonHostServices::Completion> replies;
    QList<std::function<void(bool)>> retiredChecks;
    const auto pending=m_state->checks.keys();
    for(const auto key:pending) {
        auto it=m_state->checks.find(key); if(it==m_state->checks.end() || it->session!=id) continue;
        std::function<void(bool)> done; done.swap(it->done); m_state->checks.erase(it); retiredChecks.append(std::move(done));
    }
    if(session->opened) { AddonHostServices::Completion reply; reply.swap(session->opened); replies.append(std::move(reply)); }
    if(session->receive) { AddonHostServices::Completion reply; reply.swap(session->receive); replies.append(std::move(reply)); }
    const auto commands=m_state->commands.keys();
    for(const auto& command:commands) {
        auto it=m_state->commands.find(command); if(it==m_state->commands.end() || it->session!=id) continue;
        if(it->timer) { it->timer->stop(); it->timer->deleteLater(); }
        AddonHostServices::Completion reply; reply.swap(it->reply); m_state->commands.erase(it); if(reply) replies.append(std::move(reply));
    }
    if(m_state->controller==id) m_state->controller.clear();
    const QPointer<AddonSessionHost> alive(this);
    for(const auto& reply:replies) { reply(refused()); if(!alive) return; }
    for(const auto& done:retiredChecks) { if(done) done(false); if(!alive) return; }
    emit sessionChanged(id,QStringLiteral("ended"));
}
void AddonSessionHost::revokePackage(const QString& package) {
    const QPointer<AddonSessionHost> alive(this); const auto ids=m_state->sessions.keys();
    for(const auto& id:ids) { const auto session=m_state->sessions.value(id); if(session && session->authority.package.manifest.id==package) end(id); if(!alive) return; }
    const auto tickets=m_state->approvals.keys(); for(const auto& ticket:tickets) {
        const auto& value=m_state->approvals[ticket]; if(value.contribution.section(QLatin1Char('/'),0,0)==package) m_state->approvals.remove(ticket);
    }
}
QVariantList AddonSessionHost::sessions() const {
    QVariantList result;
    for(const auto& value:m_state->sessions) result.append(QVariantMap{{"id",value->id},{"name",value->authority.package.manifest.name},
        {"relay",relayOrigin(value->relay)},{"room",value->room},{"contribution",value->authority.globalId},
        {"connected",value->connected},{"starting",value->starting},{"suspended",value->suspended},{"operations",value->operations}});
    return result;
}
bool AddonSessionHost::current(const QString& id) const {
    const auto session=m_state->sessions.value(id);
    if(m_state->closed || !m_lifetime.current() || !session || session->starting || !m_playerLifetime || !m_authorityLifetime
        || session->expires<=m_player.monotonicUs() || !session->watch || !session->control->open.load(std::memory_order_acquire) || !check(session->authority.live)) return false;
    const auto snapshot=m_player.snapshot();
    return epoch(snapshot.stamp)==epoch(session->stamp) && (session->operations.isEmpty() || (snapshot.active && snapshot.lease==session->native));
}
QVariantMap AddonSessionHost::invocationContext(const QString& id) const {
    const auto session=m_state->sessions.value(id); if(!current(id) || !session) return {};
    return {{"contribution",session->authority.globalId},{"context",QVariantMap{{"session",id},{"room",session->room},{"sliceMs",25000}}}};
}
bool AddonSessionHost::checkForInvocation(const QString& id,std::function<void(bool)> complete) {
    const auto session=m_state->sessions.value(id); if(!current(id) || !session || !complete) return false;
    const QPointer<AddonSessionHost> owner(this);
    return authorize(session,[owner,id,complete=std::move(complete)](bool allowed) { complete(allowed && owner && owner->current(id)); });
}
bool AddonSessionHost::invocationCurrent(const std::shared_ptr<Session>& session,const QString& invocation) const {
    return session && !invocation.isEmpty() && session->invocation==invocation && steadyUs()<session->invocationDeadline
        && !m_state->closed && m_lifetime.current() && session->watch && session->control->open.load(std::memory_order_acquire) && check(session->authority.live);
}
bool AddonSessionHost::bindInvocation(const QString& id,const QString& invocation,qint64 deadline) {
    const auto session=m_state->sessions.value(id); const auto now=steadyUs();
    if(!current(id) || !session || !identifier(invocation) || !session->invocation.isEmpty() || session->invocationIds.contains(invocation)
        || session->invocationIds.size()>=4096 || deadline<=now || deadline>now+25000000) return false;
    session->invocation=invocation; session->invocationDeadline=deadline; session->invocationIds.insert(invocation); return true;
}
bool AddonSessionHost::releaseInvocation(const QString& id,const QString& invocation) {
    const auto session=m_state->sessions.value(id); if(!session || session->invocation!=invocation || invocation.isEmpty()) return false;
    session->invocation.clear(); session->invocationDeadline=0;
    // An unfinished open or admitted native command cannot pass into a new slice.
    bool mustEnd=bool(session->opened);
    for(const auto& command:m_state->commands) if(command.session==id && command.invocation==invocation && command.admitted) mustEnd=true;
    if(mustEnd) { end(id); return false; }
    QList<AddonHostServices::Completion> replies;
    if(session->receiveTimer) { session->receiveTimer->stop(); session->receiveTimer->deleteLater(); session->receiveTimer=nullptr; }
    if(session->receive) { AddonHostServices::Completion reply; reply.swap(session->receive); replies.append(std::move(reply)); }
    const auto commands=m_state->commands.keys();
    for(const auto& key:commands) {
        auto it=m_state->commands.find(key); if(it==m_state->commands.end() || it->session!=id || it->invocation!=invocation) continue;
        if(it->timer) { it->timer->stop(); it->timer->deleteLater(); }
        AddonHostServices::Completion reply; reply.swap(it->reply); m_state->commands.erase(it); if(reply) replies.append(std::move(reply));
    }
    QList<std::function<void(bool)>> checks;
    const auto pending=m_state->checks.keys();
    for(const auto key:pending) {
        auto it=m_state->checks.find(key); if(it==m_state->checks.end() || it->session!=id || it->invocation!=invocation) continue;
        std::function<void(bool)> done; done.swap(it->done); m_state->checks.erase(it); checks.append(std::move(done));
    }
    const QPointer<AddonSessionHost> alive(this);
    for(const auto& reply:replies) { reply(refused()); if(!alive) return false; }
    for(const auto& done:checks) { if(done) done(false); if(!alive) return false; }
    return m_state->sessions.contains(id) && current(id);
}
void AddonSessionHost::close() {
    if(m_state->closed && !m_gate->owner) return;
    m_state->closed=true; m_timer.stop();
    { std::lock_guard<std::mutex> lock(m_gate->mutex); m_gate->owner=nullptr; }
    m_state->approvals.clear();
    const QPointer<AddonSessionHost> alive(this); const auto ids=m_state->sessions.keys();
    for(const auto& id:ids) { end(id); if(!alive) return; }
    QHash<quint64,State::Check> retired; retired.swap(m_state->checks);
    if(m_state->registration) { auto* registration=m_state->registration.data(); m_state->registration=nullptr; delete registration; }
}
QVariantMap AddonSessionHost::observation(const QString& id) const {
    const auto session=m_state->sessions.value(id);
    if(m_state->closed || !session || session->starting || !session->control->open.load(std::memory_order_acquire)
        || !check(session->authority.live)) return {};
    const auto state=m_player.snapshot(); if(epoch(state.stamp)!=epoch(session->stamp)) return {};
    const auto declared=session->authority.package.manifest.permissionScope(QStringLiteral("playback.observe")).value("fields").toArray();
    const auto granted=session->authority.package.grants.value("playback.observe").toObject().value("fields").toArray();
    const QVariantMap fields{{"positionUs",state.positionUs<0 ? QVariant{} : QVariant(state.positionUs)},
        {"durationUs",state.durationUs<0 ? QVariant{} : QVariant(state.durationUs)},{"rate",state.rate},{"paused",state.paused},
        {"seekable",state.seekable},{"pausable",state.pausable},{"rateChange",state.rateChange},
        {"buffering",QVariant{}},{"live",QVariant{}},{"liveStartUs",QVariant{}},{"liveEndUs",QVariant{}},
        {"outputLatencyUs",QVariant{}},{"outputLatencyUncertaintyUs",QVariant{}}};
    QVariantMap result{{"mediaEpoch",epoch(state.stamp)},{"transportRevision",QString::number(state.stamp.revision)},
        {"observedAtUs",QString::number(m_player.monotonicUs())},{"suspended",session->suspended || state.suspended}};
    result.insert("origin",state.origin==SessionPlayerOrigin::Local ? QStringLiteral("local")
        : state.origin==SessionPlayerOrigin::Remote ? QStringLiteral("session") : QStringLiteral("core"));
    result.insert("originSequence",state.origin==SessionPlayerOrigin::Remote && state.lease==session->native ? QVariant(QString::number(state.sequence)) : QVariant{});
    const std::array<QString,7> names{QStringLiteral("play"),QStringLiteral("pause"),QStringLiteral("seek"),QStringLiteral("setRate"),QStringLiteral("setRate"),QStringLiteral("other"),QStringLiteral("rateRestore")};
    result.insert("originOperation",state.operation>=0 && state.operation<int(names.size()) ? names[size_t(state.operation)] : QStringLiteral("other"));
    for(auto it=fields.begin();it!=fields.end();++it) if(declared.contains(it.key()) && granted.contains(it.key())) result.insert(it.key(),it.value());
    return result;
}
void AddonSessionHost::tick() {
    if(m_state->closed) return;
    if(!m_lifetime.current() || !m_authorityLifetime || !m_playerLifetime) { close(); return; }
    const QPointer<AddonSessionHost> alive(this); const auto now=m_player.monotonicUs();
    const auto completion=m_player.completions();
    for(const auto& done:completion) {
        const auto ids=m_state->commands.keys();
        for(const auto& id:ids) {
            const auto value=m_state->commands.value(id); const auto session=m_state->sessions.value(value.session);
            if(session && value.admitted && value.native.lease==done.lease && value.native.expected==done.expected
                && value.native.sequence==done.sequence && value.native.operation==done.operation && session->leaseEpoch==done.leaseEpoch)
                finishCommand(id,done.success ? QStringLiteral("applied") : QStringLiteral("error"),done.success ? QString{} : QStringLiteral("The player did not execute this command."));
            if(!alive) return;
        }
    }
    const auto snapshot=m_player.snapshot(); const auto ids=m_state->sessions.keys();
    for(const auto& id:ids) {
        const auto session=m_state->sessions.value(id); if(!session) continue;
        if(!session->control->open.load(std::memory_order_acquire) || !check(session->authority.live) || session->expires<=now
            || epoch(snapshot.stamp)!=epoch(session->stamp)) { end(id); if(!alive) return; continue; }
        if(!session->starting && !session->operations.isEmpty() && (!snapshot.active || snapshot.lease!=session->native || snapshot.closed || snapshot.exhausted)) { end(id); if(!alive) return; continue; }
        if(!session->starting && !session->operations.isEmpty() && snapshot.suspended && !session->suspended) {
            session->suspended=true; emit sessionChanged(id,QStringLiteral("suspended")); if(!alive) return;
        }
    }
    const auto commands=m_state->commands.keys();
    for(const auto& id:commands) {
        const auto value=m_state->commands.value(id); const auto session=m_state->sessions.value(value.session);
        if(value.expires<=now && session && value.admitted) session->control->cancel();
        if(!session || value.expires<=now || snapshot.stamp.localRevision!=value.native.expected.localRevision
            || epoch(snapshot.stamp)!=epoch(value.native.expected)) finishCommand(id,QStringLiteral("cancelled"));
        if(!alive) return;
    }
    for(auto it=m_state->approvals.begin();it!=m_state->approvals.end();) {
        if(it->expires<=now) it=m_state->approvals.erase(it); else ++it;
    }
}
AddonHostServices::Cancel AddonSessionHost::submit(const QString& sessionId,const QJsonObject& request,AddonHostServices::Completion reply) {
    const auto session=m_state->sessions.value(sessionId); const auto name=request.value("operation").toString(); const auto op=operation(name);
    quint64 revision=0,sequence=0,at=0;
    const auto now=m_player.monotonicUs(); const auto snapshot=m_player.snapshot();
    const auto commandId=request.value("commandId").toString();
    if(!session || session->starting || !session->connected || session->suspended || session->operations.isEmpty()
        || !session->operations.contains(name) || !op || !session->control->open.load(std::memory_order_acquire) || !check(session->authority.live)
        || !keys(request,{"session","operation","commandId","sequence","mediaEpoch","expectedRevision","atUs","parameters"})
        || !identifier(commandId) || session->seen.contains(commandId) || session->seen.size()>=4096 || m_state->commands.size()>=64
        || !integer(request.value("expectedRevision"),&revision) || !integer(request.value("sequence"),&sequence,true) || sequence<=session->sequence
        || revision!=snapshot.stamp.revision || request.value("mediaEpoch").toString()!=epoch(snapshot.stamp)
        || epoch(session->stamp)!=epoch(snapshot.stamp) || (request.contains("atUs") && !integer(request.value("atUs"),&at))) { reply(refused()); return {}; }
    if(!request.contains("atUs")) at=quint64(now);
    if(at>quint64(now+5000000) || at+200000<quint64(now)) { reply({{},QStringLiteral("The command time is outside the permitted range.")}); return {}; }
    const auto parameters=request.value("parameters"); if(!parameters.isObject()) { reply(refused()); return {}; }
    Command command; command.session=sessionId; command.invocation=session->invocation; command.id=token("hakone-session-command:"); command.at=qint64(at); command.expires=qint64(at)+3000000;
    command.native.expected=snapshot.stamp; command.native.lease=session->native; command.native.sequence=sequence; command.native.operation=*op;
    const auto data=parameters.toObject(); quint64 position=0,duration=0;
    if(*op==SessionPlayerOperation::Seek) {
        if(!keys(data,{"positionUs"}) || !integer(data.value("positionUs"),&position) || position>quint64(INT64_MAX)) { reply(refused()); return {}; }
        command.native.positionUs=qint64(position);
    } else if(*op==SessionPlayerOperation::TemporaryRate) {
        const auto rate=data.value("rate");
        if(!keys(data,{"rate","durationMs"}) || !rate.isDouble() || !std::isfinite(rate.toDouble()) || rate.toDouble()<=0
            || !data.value("durationMs").isDouble() || !integer(data.value("durationMs"),&duration,true) || duration>5000) { reply(refused()); return {}; }
        command.native.rate=rate.toDouble(); command.native.durationUs=qint64(duration)*1000;
    } else if(!data.isEmpty()) { reply(refused()); return {}; }
    const auto id=command.id; session->sequence=sequence; session->seen.insert(commandId); command.reply=std::move(reply);
    auto* timer=new QTimer(this); timer->setSingleShot(true); timer->setTimerType(Qt::PreciseTimer); command.timer=timer;
    m_state->commands.insert(id,std::move(command)); connect(timer,&QTimer::timeout,this,[this,id] { execute(id); });
    timer->start(int(std::max<qint64>(0,(qint64(at)-now+999)/1000)));
    const QPointer<AddonSessionHost> owner(this); return [owner,id] { if(owner) owner->cancelCommand(id); };
}
void AddonSessionHost::execute(const QString& id) {
    auto it=m_state->commands.find(id); if(it==m_state->commands.end()) return;
    const auto session=m_state->sessions.value(it->session);
    if(!invocationCurrent(session,it->invocation) || m_player.monotonicUs()>it->at+200000) { finishCommand(id,QStringLiteral("cancelled")); return; }
    it->checking=true;
    if(!authorize(session,[this,id](bool allowed) { admitted(id,allowed); },true)) finishCommand(id,QStringLiteral("error"),error());
}
void AddonSessionHost::admitted(const QString& id,bool allowed) {
    auto it=m_state->commands.find(id); if(it==m_state->commands.end()) return;
    const auto session=m_state->sessions.value(it->session);
    if(!allowed || !invocationCurrent(session,it->invocation) || session->suspended || !session->connected || !check(session->authority.live)
        || !session->control->open.load(std::memory_order_acquire) || m_player.monotonicUs()>it->at+200000) { finishCommand(id,QStringLiteral("rejected"),error()); return; }
    const auto result=m_player.apply(it->native);
    if(result!=SessionPlayerResult::Accepted) { finishCommand(id,QStringLiteral("rejected"),QStringLiteral("The native playback state changed or refused this command.")); return; }
    it->admitted=true; it->checking=false;
    if(!invocationCurrent(session,it->invocation)) { session->control->cancel(); finishCommand(id,QStringLiteral("cancelled")); }
}
void AddonSessionHost::finishCommand(const QString& id,const QString& status,const QString& errorText) {
    auto it=m_state->commands.find(id); if(it==m_state->commands.end()) return;
    if(it->timer) { it->timer->stop(); it->timer->deleteLater(); }
    AddonHostServices::Completion reply; reply.swap(it->reply); m_state->commands.erase(it);
    if(reply) reply({{{"status",status}},errorText});
}
void AddonSessionHost::cancelCommand(const QString& id) {
    const auto command=m_state->commands.constFind(id);
    if(command!=m_state->commands.cend() && command->admitted) {
        const auto session=m_state->sessions.value(command->session); if(session) session->control->cancel();
    }
    finishCommand(id,QStringLiteral("cancelled"));
}
bool AddonSessionHost::mount(AddonHostServices* services) {
    if(m_state->closed || m_state->mounted || !services) return false;
    const QStringList names{QStringLiteral("network.websocket.receive"),QStringLiteral("network.websocket.open"),QStringLiteral("network.websocket.send"),
        QStringLiteral("network.websocket.close"),QStringLiteral("playback.observe"),QStringLiteral("playback.command"),QStringLiteral("session.renew"),QStringLiteral("session.end")};
    const QPointer<QObject> registration(new QObject(this));
    const QPointer<AddonSessionHost> owner(this); const QPointer<AddonHostServices> dispatcher(services);
    for(const auto& name:names) {
        const bool registered=dispatcher && dispatcher->registerOperation(name,registration,[owner,name](const auto& context,const auto& request,auto complete) {
            if(!owner) { complete(refused()); return AddonHostServices::Cancel{}; }
            return owner->handle(context,name,request,std::move(complete));
        });
        if(!owner) return false;
        if(!registered || !dispatcher || !registration || m_state->closed) {
            if(registration) delete registration.data(); return false;
        }
    }
    m_state->registration=registration; m_state->mounted=true; return true;
}
AddonHostServices::Cancel AddonSessionHost::handle(const AddonHostServices::Context& context,const QString& name,const QJsonObject& request,AddonHostServices::Completion complete) {
    const auto id=request.value("session").toString(); const auto session=m_state->sessions.value(id);
    if(m_state->closed || !m_lifetime.current() || !session || session->starting || context.package!=session->authority.package.manifest.id
        || context.contribution!=session->authority.globalId || !session->control->open.load(std::memory_order_acquire)
        || context.grants!=session->authority.package.grants || !invocationCurrent(session,context.invocation)) { complete(refused()); return {}; }
    if(name==QLatin1String("playback.command")) return submit(id,request,std::move(complete));
    if(name==QLatin1String("session.end") || name==QLatin1String("network.websocket.close")) {
        if(!keys(request,{"session"})) { complete(refused()); return {}; } end(id); complete({{{"closed",true}}, {}}); return {};
    }
    if(name==QLatin1String("network.websocket.receive")) {
        if(!keys(request,{"session"}) || !session->connected || session->receive) { complete(refused()); return {}; }
        session->receive=std::move(complete); auto* timer=new QTimer(this); timer->setSingleShot(true); session->receiveTimer=timer;
        const QPointer<AddonSessionHost> owner(this);
        const auto invocation=context.invocation;
        connect(timer,&QTimer::timeout,this,[this,id,invocation] { if(invocationCurrent(m_state->sessions.value(id),invocation)) finishReceive(id); });
        timer->start(9000); finishReceive(id);
        if(!owner) return {};
        return [owner,id,invocation] { if(!owner) return; const auto session=owner->m_state->sessions.value(id); if(!session || session->invocation!=invocation) return;
            if(session->receiveTimer) { session->receiveTimer->stop(); session->receiveTimer->deleteLater(); session->receiveTimer=nullptr; }
            AddonHostServices::Completion retired; retired.swap(session->receive);
        };
    }
    if(name==QLatin1String("network.websocket.open")) {
        if(!keys(request,{"session"}) || session->transport || session->opened) { complete(refused()); return {}; }
        session->opened=std::move(complete);
        const QPointer<AddonSessionHost> owner(this);
        if(!authorize(session,[this,id](bool allowed) {
            const auto session=m_state->sessions.value(id); if(!session) return;
            if(!allowed || !session->control->open.load(std::memory_order_acquire)) { end(id); return; }
            AddonSessionWebSocket::Spec spec; spec.url=session->relay; spec.protocols=session->protocols; spec.lifetime=m_lifetime;
            for(const auto& value:session->authority.package.manifest.permissionScope(QStringLiteral("network.websocket")).value("origins").toArray()) spec.declaredOrigins.insert(value.toString());
            for(const auto& value:session->authority.package.grants.value("network.websocket").toObject().value("origins").toArray()) spec.grantedOrigins.insert(value.toString());
            const auto control=session->control; const auto source=session->authority; const auto gate=m_gate;
            spec.live=[control,live=source.live] { return control->open.load(std::memory_order_acquire) && check(live); };
            spec.authorize=source.current; spec.nativeCancel=[control] { control->cancel(); };
            const auto origin=relayOrigin(session->relay);
            spec.credentials=[source,origin] {
                QList<AddonSessionWebSocket::Header> result;
                const auto approved=source.package.grants.value("credentials.use").toObject().value("bindings").toArray();
                for(const auto& value:source.package.manifest.permissionScope(QStringLiteral("credentials.use")).value("bindings").toArray()) {
                    const auto binding=value.toObject();
                    if(binding.value("origin").toString()!=origin || !approved.contains(value)) continue;
                    const auto secret=source.credentials ? source.credentials(binding.value("field").toString()) : QByteArray{};
                    if(secret.isEmpty()) throw 0;
                    const QByteArray header=binding.value("header").toString().toLatin1();
                    const QByteArray data=binding.value("prefix").toString().toLatin1()+secret; result.append({header,data});
                }
                return result;
            };
            session->transport=m_state->transport(std::move(spec),[control,gate,id](AddonSessionWebSocket::Event event) {
                bool notify=false,overflow=false;
                const bool text=event.kind==AddonSessionWebSocket::Kind::Text;
                size_t jsonBytes=0,encodedChars=0;
                if(text || event.kind==AddonSessionWebSocket::Kind::Binary) {
                    const auto data=text ? QString::fromUtf8(event.bytes) : QString::fromLatin1(event.bytes.toBase64());
                    encodedChars=size_t(data.size());
                    jsonBytes=size_t(QJsonDocument(QJsonObject{{"kind",text ? "text" : "binary"},{"data",data}}).toJson(QJsonDocument::Compact).size());
                }
                {
                    std::lock_guard<std::mutex> lock(control->mutex);
                    if(event.kind==AddonSessionWebSocket::Kind::Ready) control->ready=true;
                    else if(event.kind==AddonSessionWebSocket::Kind::Error || event.kind==AddonSessionWebSocket::Kind::Closed) control->ended=true;
                    else {
                        overflow=control->messages.size()>=32 || control->bytes+size_t(event.bytes.size())>65536
                            || (event.kind==AddonSessionWebSocket::Kind::Binary && event.bytes.size()>49152) || jsonBytes+15>256*1024;
                        if(!overflow) { control->bytes+=size_t(event.bytes.size()); control->messages.push_back({text,event.bytes.toStdString(),jsonBytes,encodedChars}); }
                    }
                    if(!control->posted) { control->posted=true; notify=true; }
                }
                if(overflow) control->cancel(); if(notify) gate->post([id](AddonSessionHost* owner) { owner->transportEvents(id); });
            });
            if(!session->transport) end(id);
        },true)) end(id);
        const auto invocation=context.invocation;
        return [owner,id,invocation] { if(owner) { const auto session=owner->m_state->sessions.value(id); if(session && session->invocation==invocation) owner->end(id); } };
    }
    if(name==QLatin1String("network.websocket.send")) {
        if(!keys(request,{"session","kind","data"}) || !session->transport || !session->connected || !request.value("data").isString()) { complete(refused()); return {}; }
        const auto kind=request.value("kind").toString(); QByteArray bytes;
        if(kind==QLatin1String("text")) bytes=request.value("data").toString().toUtf8();
        else if(kind==QLatin1String("binary")) {
            const auto encoded=request.value("data").toString().toLatin1(); if(encoded.size()>65536) { complete(refused()); return {}; }
            bytes=QByteArray::fromBase64(encoded); if(bytes.toBase64()!=encoded) { complete(refused()); return {}; }
        } else { complete(refused()); return {}; }
        if(bytes.size()>65536) { complete(refused()); return {}; }
        const auto pending=std::make_shared<std::atomic_bool>(true); const auto cancelled=[pending] { pending->store(false,std::memory_order_release); };
        if(!authorize(session,[this,id,bytes,text=kind==QLatin1String("text"),pending,complete](bool allowed) {
            if(!pending->exchange(false)) return;
            const auto session=m_state->sessions.value(id);
            if(!allowed || !session || !check(session->authority.live) || !session->control->open.load(std::memory_order_acquire)
                || !session->transport || !session->transport->send(text,bytes)) { complete(refused()); return; }
            complete({{{"queued",true}}, {}});
        },true)) { cancelled(); complete(refused()); }
        return cancelled;
    }
    if(name!=QLatin1String("playback.observe") && name!=QLatin1String("session.renew")) { complete(refused()); return {}; }
    if(!keys(request,{"session"})) { complete(refused()); return {}; }
    const auto pending=std::make_shared<std::atomic_bool>(true); const auto cancel=[pending] { pending->store(false,std::memory_order_release); };
    if(!authorize(session,[this,id,name,pending,complete](bool allowed) {
        if(!pending->exchange(false)) return;
        const auto session=m_state->sessions.value(id); if(!allowed || !session || !check(session->authority.live)
            || !session->control->open.load(std::memory_order_acquire)) { complete(refused()); return; }
        if(name==QLatin1String("playback.observe")) {
            const auto value=observation(id); if(value.isEmpty()) { complete(refused()); return; } complete({QJsonObject::fromVariantMap(value),{}}); return;
        }
        const auto snapshot=m_player.snapshot();
        if(session->suspended || (!session->operations.isEmpty() && m_player.renew(snapshot.stamp,session->native,30000000)!=SessionPlayerResult::Accepted)) { complete(refused()); return; }
        session->expires=m_player.monotonicUs()+30000000; complete({{{"renewed",true}}, {}});
    },true)) { cancel(); complete(refused()); }
    return cancel;
}
void AddonSessionHost::transportEvents(const QString& id) {
    const auto session=m_state->sessions.value(id); if(!session) return; bool ready,ended;
    { std::lock_guard<std::mutex> lock(session->control->mutex); ready=session->control->ready; ended=session->control->ended; session->control->posted=false; }
    if(ended || !session->control->open.load(std::memory_order_acquire)) { end(id); return; }
    if(session->opened && !invocationCurrent(session,session->invocation)) { end(id); return; }
    if(ready && !session->connected) {
        session->connected=true; AddonHostServices::Completion complete; complete.swap(session->opened);
        const QPointer<AddonSessionHost> alive(this); if(complete) complete({{{"session",id},{"connected",true}}, {}});
        if(!alive) return; emit sessionChanged(id,QStringLiteral("connected")); if(!alive) return;
    }
    finishReceive(id);
}
void AddonSessionHost::finishReceive(const QString& id) {
    const auto session=m_state->sessions.value(id); if(!session || !session->receive) return;
    if(!check(session->authority.live) || !session->control->open.load(std::memory_order_acquire)) { end(id); return; }
    if(!invocationCurrent(session,session->invocation)) {
        if(session->receiveTimer) { session->receiveTimer->stop(); session->receiveTimer->deleteLater(); session->receiveTimer=nullptr; }
        AddonHostServices::Completion complete; complete.swap(session->receive); if(complete) complete(refused()); return;
    }
    std::deque<Control::Message> messages;
    { std::lock_guard<std::mutex> lock(session->control->mutex);
      if(session->control->messages.empty() && session->receiveTimer && session->receiveTimer->isActive()) return;
      size_t encoded=0,jsonBytes=15;
      while(!session->control->messages.empty() && messages.size()<32) {
          const auto& value=session->control->messages.front();
          const auto extra=value.jsonBytes+(messages.empty() ? 0 : 1);
          if(encoded+value.encodedChars>65536 || jsonBytes+extra>256*1024) break;
          encoded+=value.encodedChars; jsonBytes+=extra; session->control->bytes-=value.bytes.size();
          messages.push_back(std::move(session->control->messages.front())); session->control->messages.pop_front();
      } }
    if(session->receiveTimer) { session->receiveTimer->stop(); session->receiveTimer->deleteLater(); session->receiveTimer=nullptr; }
    QJsonArray values;
    for(const auto& value:messages) {
        const QByteArray bytes(value.bytes.data(),qsizetype(value.bytes.size()));
        values.append(QJsonObject{{"kind",value.text ? "text" : "binary"},{"data",value.text ? QString::fromUtf8(bytes) : QString::fromLatin1(bytes.toBase64())}});
    }
    AddonHostServices::Completion complete; complete.swap(session->receive);
    if(complete) complete({{{"messages",values}}, {}});
}
}
