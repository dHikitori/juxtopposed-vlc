// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_session_websocket.hpp"
#include "addon_websocket_codec.hpp"
#include "addon_stream_sockets.hpp"
#include <QElapsedTimer>
#include <QEventLoop>
#include <QNetworkProxy>
#include <QScopeGuard>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTimer>
#include <algorithm>
#include <chrono>
#include <deque>
#include <mutex>
#include <optional>

namespace vlc::addons {
namespace {
std::atomic_uint connections{0};
struct Slot { ~Slot() { --connections; } };
std::shared_ptr<Slot> acquire() {
    auto value=connections.load(std::memory_order_acquire);
    while (value<AddonSessionWebSocket::ProcessLimit)
        if (connections.compare_exchange_weak(value,value+1,std::memory_order_acq_rel)) {
            try { return std::make_shared<Slot>(); } catch (...) { --connections; return {}; }
        }
    return {};
}
bool check(const AddonSessionWebSocket::Current& fn) noexcept { try { return fn && fn(); } catch (...) { return false; } }
QString origin(QUrl value) {
    if (value.scheme()!=QLatin1String("wss")) return {};
    value.setPath(QString{}); value.setQuery(QString{}); value.setFragment(QString{}); if (value.port()==443) value.setPort(-1);
    return value.toString(QUrl::FullyEncoded);
}
std::optional<QByteArray> headers(const QList<AddonSessionWebSocket::Header>& values) {
    if (values.size()>8) return {}; QByteArray result; QSet<QByteArray> seen;
    static const QSet<QByteArray> reserved{"host","connection","upgrade","sec-websocket-key","sec-websocket-version",
        "sec-websocket-protocol","sec-websocket-extensions","content-length","transfer-encoding","cookie","origin","proxy-authorization"};
    for (const auto& value:values) {
        const auto name=value.first.toLower();
        if (name.isEmpty() || name.size()>128 || value.second.size()>4096 || reserved.contains(name)
            || name.startsWith("sec-websocket-") || seen.contains(name)) return {};
        for (const unsigned char c:name)
            if (!((c>='a' && c<='z') || (c>='0' && c<='9') || QByteArrayView("!#$%&'*+-.^_`|~").contains(char(c)))) return {};
        for (const unsigned char c:value.second) if (c<32 || c>=127) return {};
        seen.insert(name); result+=value.first+": "+value.second+"\r\n";
        if (result.size()>8192) return {};
    }
    return result;
}
}
struct AddonSessionWebSocket::State {
    struct Message { bool text=false; std::string bytes; };
    struct DnsMailbox { std::mutex mutex; bool closed=false,ready=false; std::vector<AddonStreamDns::Address> addresses; };
    struct Gate { std::mutex mutex; Worker* owner=nullptr; };
    Spec spec;
    std::shared_ptr<Testing> testing;
    Events events;
    std::atomic_bool active{true};
    AddonStreamSockets sockets;
    AddonStreamLifetime::NetworkTicket network;
    std::mutex mutex;
    std::deque<Message> outgoing;
    qsizetype queuedBytes=0;
    std::shared_ptr<Slot> slot;
    const std::shared_ptr<DnsMailbox> dns=std::make_shared<DnsMailbox>();
    const std::shared_ptr<Gate> gate=std::make_shared<Gate>();
    bool current() const noexcept { return active.load(std::memory_order_acquire) && spec.lifetime.current() && check(spec.live); }
    void cancel() noexcept {
        active.store(false,std::memory_order_release); sockets.cancel();
        std::function<void()> revoke;
        { std::lock_guard<std::mutex> lock(mutex); revoke.swap(spec.nativeCancel); }
        if (revoke) try { revoke(); } catch (...) {}
    }
    void releaseAuthority() {
        Current live,authorize; std::function<QList<Header>()> credentials; Events notify;
        std::shared_ptr<Testing> fixture;
        { std::lock_guard<std::mutex> lock(mutex); live.swap(spec.live); authorize.swap(spec.authorize);
          credentials.swap(spec.credentials); notify.swap(events); fixture.swap(testing); outgoing.clear(); queuedBytes=0; }
    }
};
struct AddonSessionWebSocket::Worker final : QObject {
    std::shared_ptr<State> state;
    std::shared_ptr<Slot> slot;
    QEventLoop& loop;
    QSslSocket socket;
    QTimer timer;
    QElapsedTimer elapsed;
    std::unique_ptr<AddonWebSocketCodec> codec;
    AddonWebSocketCodec::Handshake handshake;
    QList<AddonWebSocketCodec::Event> incoming;
    QList<QByteArray> replies;
    qint64 progress=0,lastPing=0,budgetStart=0;
    qsizetype receivedBytes=0,sentBytes=0;
    unsigned receivedMessages=0,sentFrames=0;
    quint64 watch=0;
    bool authPending=false,connecting=false,open=false,terminal=false,handshakePending=false,codecMore=false;
    explicit Worker(std::shared_ptr<State> value,QEventLoop& eventLoop)
        :state(std::move(value)),slot(state->slot),loop(eventLoop) {
        elapsed.start(); timer.setInterval(25); connect(&timer,&QTimer::timeout,this,[this] { tick(); }); timer.start();
        { std::lock_guard<std::mutex> lock(state->gate->mutex); state->gate->owner=this; }
    }
    ~Worker() override {
        { std::lock_guard<std::mutex> lock(state->gate->mutex); state->gate->owner=nullptr; }
        { std::lock_guard<std::mutex> lock(state->dns->mutex); state->dns->closed=true; state->dns->addresses.clear(); }
        disconnect(&socket,nullptr,this,nullptr); socket.abort(); state->sockets.release(watch);
    }
    void notify(Event event) {
        if (state->events) try { state->events(std::move(event)); } catch (...) { state->cancel(); }
    }
    void stop(bool error,const QString& reason={}) {
        if (terminal) return; terminal=true; state->cancel();
        notify({error ? Kind::Error : Kind::Closed,{},0,reason}); loop.quit();
    }
    bool budget(bool output,qsizetype bytes,unsigned messages) {
        const auto now=elapsed.elapsed();
        if(now-budgetStart>=1000) { budgetStart=now; receivedBytes=sentBytes=0; receivedMessages=sentFrames=0; }
        auto& usedBytes=output ? sentBytes : receivedBytes; auto& usedMessages=output ? sentFrames : receivedMessages;
        const unsigned limit=output ? 64 : 128;
        if(bytes>1024*1024-usedBytes || messages>limit-usedMessages) {
            stop(true,QStringLiteral("The session message rate limit was reached.")); return false;
        }
        usedBytes+=bytes; usedMessages+=messages; return true;
    }
    void authorize(bool initial) {
        if (authPending || !state->current()) { if (!state->current()) stop(false); return; }
        authPending=true; const auto gate=state->gate; const auto held=slot;
        const auto current=state->spec.live,checkSaved=state->spec.authorize;
        const auto credentials=initial ? state->spec.credentials : std::function<QList<Header>()>{};
        const auto lifetime=state->spec.lifetime;
        const bool started=lifetime.startQtWork([gate,lifetime,current,checkSaved,credentials,initial] {
            bool permitted=lifetime.current() && check(current) && check(checkSaved); std::optional<QByteArray> secret;
            if (permitted) try { secret=headers(credentials ? credentials() : QList<Header>{}); } catch (...) { permitted=false; }
            permitted &= bool(secret);
            std::lock_guard<std::mutex> lock(gate->mutex);
            if (gate->owner && lifetime.current()) QMetaObject::invokeMethod(gate->owner,
                [owner=gate->owner,permitted,initial,secret=std::move(secret)] { owner->authorized(permitted,initial,secret.value_or(QByteArray{})); },Qt::QueuedConnection);
        },[] {},[held](AddonStreamLifetime::NetworkTicket ticket) { if (!ticket.retain(held)) throw 0; });
        if (!started) { authPending=false; stop(true,QStringLiteral("The app cannot check the session permission.")); }
    }
    void authorized(bool permitted,bool initial,const QByteArray& secret) {
        authPending=false;
        if (!permitted || !state->current()) { stop(true,QStringLiteral("The session permission changed.")); return; }
        if (initial) {
            auto request=AddonWebSocketCodec::handshake(state->spec.url,state->spec.protocols);
            if (!request || request->bytes.size()+secret.size()>AddonWebSocketCodec::MaximumHeader) { stop(true,QStringLiteral("The session request is not correct.")); return; }
            handshake=std::move(*request); handshake.bytes.chop(2); handshake.bytes+=secret+"\r\n";
            codec=std::make_unique<AddonWebSocketCodec>(handshake.key,handshake.protocols); lookup(); return;
        }
        if (handshakePending) {
            handshakePending=false; write(handshake.bytes); handshake.bytes.fill(0); handshake.bytes.clear(); return;
        }
        for (const auto& event:incoming) {
            if (!state->current()) { stop(false); return; }
            if (event.kind==AddonWebSocketCodec::Kind::Text || event.kind==AddonWebSocketCodec::Kind::Binary)
                notify({event.kind==AddonWebSocketCodec::Kind::Text ? Kind::Text : Kind::Binary,event.bytes});
            else if (event.kind==AddonWebSocketCodec::Kind::Closed) { stop(false); return; }
        }
        incoming.clear();
        for (const auto& frame:replies) if (!write(frame)) return;
        replies.clear(); sendQueued(); read();
    }
    void lookup() {
        const QHostAddress literal(state->spec.url.host());
        if (!literal.isNull()) {
            AddonStreamDns::Address value;
            if (literal.protocol()==QAbstractSocket::IPv4Protocol) {
                value.family=4; const auto v=literal.toIPv4Address();
                for (unsigned i=0;i<4;++i) value.bytes[i]=quint8(v>>(24-i*8));
            } else {
                value.family=6; const auto v=literal.toIPv6Address(); std::copy(std::begin(v.c),std::end(v.c),value.bytes.begin());
                if (!literal.scopeId().isEmpty()) { stop(true,QStringLiteral("The session relay address is not public.")); return; }
            }
            resolved({value}); return;
        }
        const auto box=state->dns; const std::string host=QUrl::toAce(state->spec.url.host()).toStdString();
        const auto resolve=state->testing ? state->testing->resolve : decltype(Testing::resolve){};
        // The fixture resolver must also use native/std data only.
        if (!state->spec.lifetime.startPlain([box,host,resolve] {
            auto addresses=resolve ? resolve(host) : AddonStreamDns::resolve(host);
            std::lock_guard<std::mutex> lock(box->mutex);
            if (!box->closed) { box->addresses=std::move(addresses); box->ready=true; }
        },slot)) stop(true,QStringLiteral("The app cannot resolve the session relay."));
    }
    void resolved(std::vector<AddonStreamDns::Address> addresses) {
        QList<QHostAddress> values;
        for (const auto& value:addresses) {
            if (value.scope) { stop(true,QStringLiteral("The session relay address is not public.")); return; }
            if (value.family==4) { quint32 v=0; for (unsigned i=0;i<4;++i) v=(v<<8)|value.bytes[i]; values.append(QHostAddress(v)); }
            else if (value.family==6) { Q_IPV6ADDR v{}; std::copy(value.bytes.begin(),value.bytes.end(),v.c); values.append(QHostAddress(v)); }
            else { stop(true,QStringLiteral("The session relay address is not public.")); return; }
        }
        auto url=state->spec.url; url.setScheme(QStringLiteral("https"));
        const auto plan=AddonStreamPolicy::plan(url,values);
        if (!plan) { stop(true,QStringLiteral("The session relay address is not public.")); return; }
        connecting=true; socket.setProxy(QNetworkProxy::NoProxy); socket.setReadBufferSize(AddonWebSocketCodec::MaximumWire);
        QSslConfiguration config; config.setProtocol(QSsl::TlsV1_2OrLater); config.setPeerVerifyMode(QSslSocket::VerifyPeer);
        config.setPeerVerifyDepth(8); config.setAllowedNextProtocols({QByteArray("http/1.1")});
        config.setCaCertificates(state->testing ? state->testing->authorities : QSslConfiguration::systemCaCertificates());
        socket.setSslConfiguration(config); socket.setPeerVerifyName(plan->peerName);
        connect(&socket,&QSslSocket::connected,this,[this] { watch=state->sockets.watch(socket.socketDescriptor()); if (!watch) stop(false); });
        connect(&socket,&QSslSocket::encrypted,this,[this] {
            if (!state->current()) { stop(false); return; } handshakePending=true; authorize(false);
        });
        connect(&socket,&QSslSocket::sslErrors,this,[this](const QList<QSslError>&) { stop(true,QStringLiteral("The app cannot verify the session relay.")); });
        connect(&socket,&QSslSocket::readyRead,this,[this] { read(); });
        connect(&socket,&QSslSocket::disconnected,this,[this] { if (codec) codec->eof(); stop(false); });
        connect(&socket,&QSslSocket::errorOccurred,this,[this](QAbstractSocket::SocketError) { stop(true,QStringLiteral("The session relay connection ended.")); });
        if (state->testing && state->testing->connect) state->testing->connect(&socket,*plan);
        else socket.connectToHostEncrypted(plan->address.toString(),443,plan->peerName);
    }
    bool write(const QByteArray& bytes) {
        if (!state->current()) { stop(false); return false; }
        if (!budget(true,bytes.size(),1)) return false;
        if (socket.bytesToWrite()+bytes.size()>MaximumQueuedBytes || socket.write(bytes)!=bytes.size()) {
            stop(true,QStringLiteral("The session output limit was reached.")); return false;
        }
        return true;
    }
    void sendQueued() {
        while (state->current() && socket.bytesToWrite()<65536) {
            std::optional<State::Message> value;
            { std::lock_guard<std::mutex> lock(state->mutex); if (state->outgoing.empty()) return;
              value=std::move(state->outgoing.front()); state->queuedBytes-=qsizetype(value->bytes.size()); state->outgoing.pop_front(); }
            const auto frame=codec->send(value->text,QByteArray(value->bytes.data(),qsizetype(value->bytes.size())));
            if (!frame) { stop(true,QStringLiteral("The session message is not correct.")); return; }
            if (!write(*frame)) return;
        }
    }
    void read() {
        if (terminal || authPending || !codec || !state->current()) return;
        const auto bytes=socket.read(AddonWebSocketCodec::MaximumWire); if (!bytes.isEmpty()) progress=elapsed.elapsed();
        auto batch=codec->receive(bytes); codecMore=batch.more;
        if (!batch.error.isEmpty()) { stop(true,QStringLiteral("The session relay protocol is not correct.")); return; }
        if (!budget(false,bytes.size(),unsigned(batch.events.size()+batch.replies.size()))) return;
        if (!open && codec->state()==AddonWebSocketCodec::State::Open) { open=true; progress=elapsed.elapsed(); notify({Kind::Ready,{}}); }
        if (!batch.events.isEmpty() || !batch.replies.isEmpty()) {
            incoming=std::move(batch.events); replies=std::move(batch.replies); authorize(false);
        }
    }
    void tick() {
        if (!state->current()) { stop(false); return; }
        std::vector<AddonStreamDns::Address> addresses; bool ready=false;
        { std::lock_guard<std::mutex> lock(state->dns->mutex); if (state->dns->ready) {
            ready=true; addresses.swap(state->dns->addresses); state->dns->ready=false; } }
        if (ready) resolved(std::move(addresses));
        const int idle=state->testing ? std::clamp(state->testing->idleMs,100,30000) : 30000;
        if ((!open && elapsed.elapsed()>10000) || (open && elapsed.elapsed()-progress>idle)) {
            stop(true,QStringLiteral("The session relay time limit was reached.")); return;
        }
        if (open && !authPending) {
            bool outgoing; { std::lock_guard<std::mutex> lock(state->mutex); outgoing=!state->outgoing.empty(); }
            if (outgoing || socket.bytesAvailable() || codecMore) authorize(false);
            else if (elapsed.elapsed()-lastPing>10000) {
                const auto frame=codec->ping({}); lastPing=elapsed.elapsed(); if (frame) { replies.append(*frame); authorize(false); }
            }
        }
    }
};
AddonSessionWebSocket::AddonSessionWebSocket(std::shared_ptr<State> value):m_state(std::move(value)) {}
AddonSessionWebSocket::~AddonSessionWebSocket() { cancel(); }
unsigned AddonSessionWebSocket::processCount() noexcept { return connections.load(std::memory_order_acquire); }
bool AddonSessionWebSocket::permitted(const Spec& spec) {
    auto url=spec.url; if (url.scheme()!=QLatin1String("wss")) return false; url.setScheme(QStringLiteral("https"));
    return AddonStreamPolicy::url(url) && spec.declaredOrigins.size()<=32 && spec.grantedOrigins.size()<=32
        && spec.declaredOrigins.contains(origin(spec.url)) && spec.grantedOrigins.contains(origin(spec.url))
        && bool(AddonWebSocketCodec::handshake(spec.url,spec.protocols));
}
std::shared_ptr<AddonSessionWebSocket> AddonSessionWebSocket::open(Spec spec,Events events)
{ return create(std::move(spec),{},std::move(events)); }
std::shared_ptr<AddonSessionWebSocket> AddonSessionWebSocket::openForTesting(Spec spec,Testing testing,Events events)
{ return create(std::move(spec),std::make_shared<Testing>(std::move(testing)),std::move(events)); }
std::shared_ptr<AddonSessionWebSocket> AddonSessionWebSocket::create(Spec spec,std::shared_ptr<Testing> testing,Events events) {
    if (!permitted(spec) || !spec.lifetime.current() || !check(spec.live) || !spec.authorize || !spec.nativeCancel || !events) return {};
    const auto slot=acquire(); if (!slot) return {};
    const auto state=std::make_shared<State>(); state->spec=std::move(spec); state->testing=std::move(testing); state->events=std::move(events); state->slot=slot;
    const auto lifetime=state->spec.lifetime;
    const bool started=lifetime.startNetwork([state] {
        const auto finish=qScopeGuard([state] { state->cancel(); state->releaseAuthority(); state->slot.reset(); });
        { QEventLoop loop; Worker worker(state,loop); worker.authorize(true); if (state->current()) loop.exec(); }
    },[state] { state->cancel(); },[state,slot](AddonStreamLifetime::NetworkTicket ticket) {
        state->network=ticket; if (!ticket.retain(slot)) throw 0;
    });
    if (!started) { state->cancel(); return {}; }
    return std::shared_ptr<AddonSessionWebSocket>(new AddonSessionWebSocket(state));
}
bool AddonSessionWebSocket::send(bool text,const QByteArray& bytes) {
    if (bytes.size()>AddonWebSocketCodec::MaximumMessage || !active()) return false;
    const auto state=m_state; std::lock_guard<std::mutex> lock(state->mutex);
    if (!state->active.load(std::memory_order_acquire) || state->outgoing.size()>=MaximumQueuedMessages
        || state->queuedBytes+bytes.size()>MaximumQueuedBytes) return false;
    state->outgoing.push_back({text,std::string(bytes.constData(),size_t(bytes.size()))}); state->queuedBytes+=bytes.size(); return true;
}
void AddonSessionWebSocket::cancel() noexcept { m_state->cancel(); }
bool AddonSessionWebSocket::active() const noexcept { return m_state->active.load(std::memory_order_acquire); }
bool AddonSessionWebSocket::waitStopped(int ms) const
{ return m_state->network.waitJoined(std::chrono::milliseconds(std::clamp(ms,0,5000))); }
}
