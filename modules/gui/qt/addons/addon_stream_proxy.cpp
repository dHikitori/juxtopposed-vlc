// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_stream_proxy.hpp"
#include "addon_stream_http.hpp"
#include "addon_stream_manifest.hpp"
#include "addon_stream_sockets.hpp"
#include "addon_stream_dns.hpp"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHash>
#include <QNetworkProxy>
#include <QSslConfiguration>
#include <QSslKey>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

namespace vlc::addons {
namespace {
using Http = AddonStreamHttp;
using Resource = AddonStreamManifest::Resource;
using Kind = AddonStreamManifest::Kind;
constexpr quint64 MaximumBody = 64ULL*1024*1024*1024;
constexpr qint64 MaximumInputMs = 24LL*60*60*1000;
std::atomic_uint sessions{0};
struct SessionLease { ~SessionLease() { sessions.fetch_sub(1,std::memory_order_release); } };
QString opaque() { return QUuid::createUuid().toString(QUuid::Id128); }
QString unavailable() { return QStringLiteral("The app cannot open this media proxy."); }
bool evaluate(const AddonStreamProxy::Current& value) noexcept
{ try { return value && value(); } catch (...) { return false; } }
bool playlistType(const QByteArray& value)
{ return value == "application/vnd.apple.mpegurl" || value == "application/x-mpegurl" || value == "audio/mpegurl" || value == "audio/x-mpegurl"; }
bool mediaType(const QByteArray& value)
{ return value.isEmpty() || value.startsWith("audio/") || value.startsWith("video/") || value == "application/octet-stream" || value == "application/ogg" || value == "application/mp4"; }
}
struct AddonStreamProxy::State : std::enable_shared_from_this<State> {
    struct Authority { Current live,authorize; };
    struct Activity {
        std::mutex mutex;
        std::condition_variable changed;
        unsigned tasks=0;
        std::weak_ptr<SessionLease> slot;
    };
    struct Task;
    struct DnsResult {
        quint64 connection=0,phase=0;
        std::vector<AddonStreamDns::Address> addresses;
        std::shared_ptr<Task> task;
    };
    // Native DNS writes a bounded native mailbox. It never posts a Qt event.
    struct EventGate {
        std::mutex mutex;
        Worker* owner=nullptr;
        AddonStreamLifetime::Handle lifetime;
        std::vector<DnsResult> dns;
    };
    struct Task {
        std::shared_ptr<Activity> activity;
        std::shared_ptr<SessionLease> slot;
        ~Task() {
            if (!activity) return;
            { std::lock_guard<std::mutex> lock(activity->mutex); --activity->tasks; } activity->changed.notify_all();
        }
    };
    Spec spec;
    std::shared_ptr<Testing> testing;
    std::shared_ptr<std::atomic_bool> active = std::make_shared<std::atomic_bool>(true);
    AddonStreamSockets sockets;
    std::atomic_bool requested{false}, started{false};
    std::atomic<quint64> bytes{0}, maximumBuffered{0};
    std::atomic_uint connections{0}, resourceCount{0}, originCount{0};
    mutable std::mutex mutex;
    std::shared_ptr<Activity> activity=std::make_shared<Activity>();
    std::shared_ptr<EventGate> events=std::make_shared<EventGate>();
    AddonStreamLifetime::NetworkTicket network;
    QString uri;
    QByteArray prefix;
    unsigned port = 0;
    Started completion;
    Cancel nativeCancel;
    bool cancelHookSet = false;
    bool authorityReleased=false;
    bool current(const Current& live) const noexcept { return spec.lifetime.current() && active->load(std::memory_order_acquire) && sockets.active() && evaluate(live); }
    // Only the network thread reads the stored predicate without this mutex.
    bool current() const noexcept { return current(spec.live); }
    Authority authority() { std::lock_guard<std::mutex> lock(mutex); return {spec.live,spec.authorize}; }
    void releaseAuthority() {
        Authority old; Started ready;
        { std::lock_guard<std::mutex> lock(mutex); authorityReleased=true;
            old.live.swap(spec.live); old.authorize.swap(spec.authorize); ready.swap(completion); }
        if(ready) try { ready(false); } catch(...) { cancel(); }
    }
    bool post(std::function<void(Worker*)>);
    void cancel() noexcept;
    void notify(bool) noexcept;
    std::shared_ptr<Task> task() {
        auto lease=activity->slot.lock(); if (!lease) return {};
        auto value=std::make_shared<Task>();
        std::lock_guard<std::mutex> lock(activity->mutex); if (activity->tasks >= 4) return {};
        ++activity->tasks; value->activity=activity; value->slot=std::move(lease); return value;
    }
};
struct AddonStreamProxy::Worker : QObject {
    struct Route {
        Resource resource;
        QByteArray parent, key;
        unsigned depth = 0, window = 0;
        qint64 used = 0;
        QByteArray publicTag, remoteTag;
    };
    struct Connection;
    std::shared_ptr<State> state;
    QEventLoop* loop;
    QTcpServer server;
    QTimer timer;
    QElapsedTimer elapsed;
    QHash<QByteArray,Route> routes;
    QSet<QByteArray> origins;
    QSet<Connection*> clients;
    quint64 totalBytes = 0;
    quint64 nextConnection = 0;
    Worker(std::shared_ptr<State> value,QEventLoop* eventLoop) : state(std::move(value)),loop(eventLoop) {
        elapsed.start(); server.setMaxPendingConnections(4);
        connect(&server,&QTcpServer::newConnection,this,[this] { accept(); });
        timer.setInterval(25); connect(&timer,&QTimer::timeout,this,[this] { tick(); }); timer.start();
    }
    ~Worker() override;
    bool init();
    void close();
    void accept();
    void tick();
    void authorized(quint64,quint64,bool,std::shared_ptr<State::Task>);
    void resolved(quint64,quint64,std::vector<AddonStreamDns::Address>);
    void dnsResults();
    bool origin(const QUrl& url) {
        if (!AddonStreamPolicy::url(url)) return false;
        const auto key = AddonStreamPolicy::origin(url);
        if (!origins.contains(key) && origins.size() >= 16) return false;
        origins.insert(key); state->originCount.store(unsigned(origins.size())); return true;
    }
    QByteArray descriptor(const Resource& value,const QByteArray& parent) const {
        QByteArray key = parent + '\0' + value.url.toEncoded() + '\0' + QByteArray::number(int(value.kind));
        if (value.range) key += ':' + QByteArray::number(value.range->offset) + ':' + QByteArray::number(value.range->length);
        return QCryptographicHash::hash(key,QCryptographicHash::Sha256).toHex();
    }
    void prune();
    AddonStreamManifest::Result rewrite(const QByteArray&,const QUrl&,const QByteArray&,Format);
};
struct AddonStreamProxy::Worker::Connection : QObject {
    Worker* worker;
    QTcpSocket* client;
    QSslSocket* remote = nullptr;
    quint64 clientWatch = 0,remoteWatch = 0;
    quint64 id = 0,hop = 0;
    int redirects = 0, interim = 0;
    QByteArray clientHeader, remoteHeader, routeId, sniff, manifest, wire, tail;
    Http::Request request;
    Http::Response response;
    Http::Chunks chunks;
    Route route;
    QUrl url;
    std::optional<Http::Range> requestedRange;
    std::optional<quint64> expected;
    quint64 received = 0;
    qint64 begun = 0,progress = 0;
    bool parsed = false, headers = false, sent = false, chunkOutput = false, hls = false, dash = false, classified = false;
    bool ended = false, finished = false, tlsClosed = false;
    Connection(Worker* owner,QTcpSocket* socket) : QObject(owner),worker(owner),client(socket) {
        id=++worker->nextConnection;
        client->setParent(this); client->setReadBufferSize(8192); begun = progress = worker->elapsed.elapsed();
        clientWatch = worker->state->sockets.watch(client->socketDescriptor());
        connect(client,&QTcpSocket::readyRead,this,[this] { readRequest(); });
        connect(client,&QTcpSocket::bytesWritten,this,[this] { progress = worker->elapsed.elapsed(); pump(); });
        connect(client,&QTcpSocket::disconnected,this,[this] { dispose(); });
        if (!clientWatch) reject(503);
    }
    ~Connection() override {
        disconnect(client,nullptr,this,nullptr); client->abort();
        dropRemote(); worker->state->sockets.release(clientWatch); worker->clients.remove(this); worker->state->connections.store(unsigned(worker->clients.size()));
    }
    bool current() const { return worker->state->current() && worker->state->started.load(std::memory_order_acquire); }
    void dispose() { if (!finished) finished = true; disconnect(client,nullptr,this,nullptr); deleteLater(); }
    void reject(int status) {
        if (finished) return;
        finished = true; dropRemote();
        if (!sent) { client->write(QByteArray("HTTP/1.1 ") + QByteArray::number(status) + " Media unavailable\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"); client->disconnectFromHost(); }
        else client->abort();
        if (client->state() == QAbstractSocket::UnconnectedState) dispose();
    }
    void dropRemote() {
        if (remote) { disconnect(remote,nullptr,this,nullptr); remote->abort(); remote->deleteLater(); remote = nullptr; }
        worker->state->sockets.release(remoteWatch); remoteWatch = 0;
    }
    void readRequest() {
        if (finished || parsed) { if (parsed && client->bytesAvailable()) reject(400); return; }
        clientHeader += client->read(8193-clientHeader.size());
        if (clientHeader.size() > 8192) { reject(431); return; }
        const auto end = clientHeader.indexOf("\r\n\r\n"); if (end < 0) return;
        const QByteArray host = QByteArray("127.0.0.1:") + QByteArray::number(worker->state->port);
        if (end + 4 != clientHeader.size() || !Http::request(clientHeader,host,&request)) { reject(400); return; }
        parsed = true; clientHeader.clear();
        if (!current() || !request.path.startsWith(worker->state->prefix)) { reject(403); return; }
        routeId = request.path.mid(worker->state->prefix.size());
        if (routeId.isEmpty() || routeId.contains('/') || !worker->routes.contains(routeId)) { reject(404); return; }
        auto found = worker->routes.find(routeId); found->used = worker->elapsed.elapsed(); route = *found; url = route.resource.url;
        for (auto* field : {&request.ifRange,&request.ifMatch}) if (!field->isEmpty()) {
            if (*field != route.publicTag || route.remoteTag.isEmpty()) { reject(412); return; }
            *field = route.remoteTag;
        }
        requestedRange = request.range;
        if (route.resource.range) {
            const auto extent = *route.resource.range; const auto last = extent.offset + extent.length - 1;
            if (!requestedRange) requestedRange = Http::Range{extent.offset,last};
            if (requestedRange->first < extent.offset || requestedRange->first > last || (requestedRange->last && *requestedRange->last > last)) { reject(416); return; }
            if (!requestedRange->last) requestedRange->last = last;
        }
        if ((route.resource.kind == Kind::Playlist || route.resource.kind == Kind::Key || route.resource.kind == Kind::SessionData) && requestedRange) {
            if (requestedRange->first || requestedRange->last || !request.ifRange.isEmpty()) { reject(416); return; }
            requestedRange.reset();
        }
        begin();
    }
    void begin() {
        if (!current() || !worker->origin(url)) { reject(502); return; }
        headers = false; remoteHeader.clear(); tlsClosed = false; ended = false; begun = progress = worker->elapsed.elapsed();
        const auto state=worker->state; const auto connection=id,phase=++hop;
        try {
            const auto task=state->task(); if (!task) { reject(503); return; }
            const auto authority=state->authority();
            const bool accepted=state->spec.lifetime.startQtWork([state,task,connection,phase,authority] {
                const bool valid=state->current(authority.live) && evaluate(authority.authorize) && state->current(authority.live);
                if (!valid) { state->cancel(); return; }
                try { state->post([connection,phase,valid,task](Worker* owner) { owner->authorized(connection,phase,valid,task); }); } catch (...) { state->cancel(); }
            },[state] { state->cancel(); },
            [task](auto ticket) { if(!ticket.retain(task)) throw std::bad_alloc(); });
            if(!accepted) reject(503);
        } catch (...) { reject(503); }
    }
    void authorized(bool valid,std::shared_ptr<State::Task> task) {
        if (finished) return;
        if (!valid) { worker->state->cancel(); return; }
        if (!current()) { reject(403); return; }
        const QHostAddress literal(url.host());
        if (!literal.isNull()) { resolved({literal}); return; }
        if (worker->state->testing && worker->state->testing->resolve) { resolved(worker->state->testing->resolve(url.host())); return; }
        const auto gate=worker->state->events;
        const auto host=QUrl::toAce(url.host()).toStdString(); const auto connection=id,phase=hop;
        const auto resolve=worker->state->testing ? worker->state->testing->nativeResolve : decltype(Testing::nativeResolve){};
        // This callback and its retained values use only native/std APIs.
        const bool accepted=worker->state->spec.lifetime.startPlain([gate,host,connection,phase,task,resolve] {
            auto addresses=resolve ? resolve(host) : AddonStreamDns::resolve(host);
            std::lock_guard<std::mutex> lock(gate->mutex);
            if(!gate->owner || !gate->lifetime.current() || gate->dns.size()>=4) return;
            gate->dns.push_back({connection,phase,std::move(addresses),task});
        },task);
        if(!accepted) reject(503);
    }
    void resolved(const QList<QHostAddress>& addresses) {
        if (finished || !current()) { reject(502); return; }
        const auto plan = AddonStreamPolicy::plan(url,addresses); if (!plan) { reject(502); return; }
        remote = new QSslSocket(this); remote->setProxy(QNetworkProxy::NoProxy); remote->setReadBufferSize(65536);
        QSslConfiguration config; config.setProtocol(QSsl::TlsV1_2OrLater); config.setPeerVerifyMode(QSslSocket::VerifyPeer);
        config.setPeerVerifyDepth(8); config.setAllowedNextProtocols({QByteArray("http/1.1")});
        config.setCaCertificates(worker->state->testing ? worker->state->testing->authorities : QSslConfiguration::systemCaCertificates());
        config.setLocalCertificateChain({}); config.setPrivateKey(QSslKey{}); remote->setSslConfiguration(config);
        remote->setPeerVerifyName(plan->peerName);
        connect(remote,&QSslSocket::connected,this,[this] {
            remoteWatch = worker->state->sockets.watch(remote->socketDescriptor()); if (!remoteWatch || !current()) reject(502);
        });
        connect(remote,&QSslSocket::sslErrors,this,[this](const QList<QSslError>&) { reject(502); });
        connect(remote,&QSslSocket::encrypted,this,[this,plan] {
            if (!current() || !remoteWatch) { reject(502); return; }
            QByteArray bytes = request.method + ' ' + plan->target + " HTTP/1.1\r\nHost: " + plan->host
                + "\r\nUser-Agent: Hakone-Contained-Media/1\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n";
            if (requestedRange) bytes += "Range: " + Http::rangeHeader(*requestedRange) + "\r\n";
            if (!request.ifRange.isEmpty()) bytes += "If-Range: " + request.ifRange + "\r\n";
            if (!request.ifMatch.isEmpty()) bytes += "If-Match: " + request.ifMatch + "\r\n";
            bytes += "\r\n"; remote->write(bytes);
        });
        connect(remote,&QSslSocket::readyRead,this,[this] { pump(); });
        connect(remote,&QSslSocket::alertReceived,this,[this](QSsl::AlertLevel,QSsl::AlertType type,const QString&) { if (type == QSsl::AlertType::CloseNotify) tlsClosed = true; });
        connect(remote,&QSslSocket::errorOccurred,this,[this](QAbstractSocket::SocketError error) { if (error != QAbstractSocket::RemoteHostClosedError) reject(502); });
        connect(remote,&QSslSocket::disconnected,this,[this] { ended = true; pump(); });
        if (worker->state->testing && worker->state->testing->connect) worker->state->testing->connect(remote,*plan);
        else remote->connectToHostEncrypted(plan->address.toString(),443,plan->peerName);
    }
    bool parseHeaders() {
        remoteHeader += remote->read(std::max<qint64>(0,32769-remoteHeader.size()));
        const auto end = remoteHeader.indexOf("\r\n\r\n");
        if (end < 0) { if (remoteHeader.size() > 32768 || ended) reject(502); return false; }
        if (end + 4 > 32768 || !Http::response(remoteHeader.left(end+4),&response)) { reject(502); return false; }
        wire = remoteHeader.mid(end+4); remoteHeader.clear();
        if (response.status >= 100 && response.status < 200) {
            if (++interim > 4 || response.length || response.chunked) { reject(502); return false; }
            remoteHeader = std::move(wire); return parseHeaders();
        }
        if (response.status == 301 || response.status == 302 || response.status == 303 || response.status == 307 || response.status == 308) {
            if (++redirects > 5 || response.location.isEmpty()) { reject(502); return false; }
            const auto next = url.resolved(QUrl::fromEncoded(response.location,QUrl::StrictMode));
            if (!AddonStreamPolicy::url(next)) { reject(502); return false; }
            dropRemote(); url = next; wire.clear(); begin(); return false;
        }
        if (response.status != 200 && response.status != 206) { reject(502); return false; }
        if ((!request.ifMatch.isEmpty() && response.etag != request.ifMatch)
            || (!request.ifRange.isEmpty() && response.status == 206 && response.etag != request.ifRange)) { reject(502); return false; }
        if (requestedRange) {
            if (response.status == 206) {
                if (response.range->first != requestedRange->first || (requestedRange->last && *response.range->last != *requestedRange->last)) { reject(502); return false; }
            } else if (requestedRange->first || requestedRange->last || route.resource.range || !request.ifRange.isEmpty()) { reject(502); return false; }
        } else if (response.status == 206) { reject(502); return false; }
        expected = response.length;
        if (response.range) expected = *response.range->last-response.range->first+1;
        if (expected && *expected > MaximumBody) { reject(502); return false; }
        dash = routeId == "main" && worker->state->spec.format == Format::Dash;
        hls = route.resource.kind == Kind::Playlist || dash;
        if (routeId == "main" && worker->state->spec.format == Format::Progressive && (playlistType(response.type) || response.type == "application/dash+xml")) { reject(502); return false; }
        if (routeId == "main" && worker->state->spec.format == Format::Auto && playlistType(response.type)) hls = true;
        if (routeId == "main" && worker->state->spec.format == Format::Auto && response.type == "application/dash+xml") hls = dash = true;
        if (routeId != "main" && hls && response.type == "application/dash+xml") { reject(502); return false; }
        if (hls && !wholePlaylist()) { reject(416); return false; }
        if (route.resource.kind == Kind::Key && expected && *expected != 16) { reject(502); return false; }
        if ((hls || route.resource.kind == Kind::SessionData) && expected && *expected > (hls ? 1024*1024 : 65536)) { reject(502); return false; }
        if (!hls && route.resource.kind != Kind::Key && route.resource.kind != Kind::SessionData && !mediaType(response.type)
            && !(routeId == "main" && worker->state->spec.format == Format::Auto && (response.type == "text/plain" || response.type == "application/xml" || response.type == "text/xml"))) { reject(502); return false; }
        headers = true;
        if (!response.etag.isEmpty()) {
            const QByteArray input = worker->state->prefix + '\0' + routeId + '\0' + url.toEncoded() + '\0' + response.etag;
            const QByteArray opaqueTag = '"' + QCryptographicHash::hash(input,QCryptographicHash::Sha256).toHex() + '"';
            worker->routes[routeId].remoteTag = response.etag; worker->routes[routeId].publicTag = opaqueTag; response.etag = opaqueTag;
        } else { worker->routes[routeId].remoteTag.clear(); worker->routes[routeId].publicTag.clear(); }
        if (request.method == "HEAD") {
            if (hls) { expected.reset(); response.range.reset(); response.status = 200; response.etag.clear(); }
            sendHeaders(); complete(); return false;
        }
        return true;
    }
    bool wholePlaylist() const {
        if (requestedRange && (requestedRange->first || requestedRange->last || !request.ifRange.isEmpty())) return false;
        return !response.range || (response.range->first == 0 && response.total && *response.range->last + 1 == *response.total);
    }
    void sendHeaders() {
        if (sent || finished) return;
        sent = true; chunkOutput = !expected;
        QByteArray bytes = QByteArray("HTTP/1.1 ") + QByteArray::number(response.status) + " OK\r\nConnection: close\r\n";
        bytes += response.acceptsRanges && !hls ? "Accept-Ranges: bytes\r\n" : "Accept-Ranges: none\r\n";
        const auto type = hls ? (dash ? QByteArray("application/dash+xml") : QByteArray("application/vnd.apple.mpegurl")) : route.resource.kind == Kind::Key ? QByteArray("application/octet-stream") : response.type;
        if (!type.isEmpty()) bytes += "Content-Type: " + type + "\r\n";
        if (expected) bytes += "Content-Length: " + QByteArray::number(*expected) + "\r\n";
        else if (request.method != "HEAD") bytes += "Transfer-Encoding: chunked\r\n";
        if (response.range) bytes += "Content-Range: bytes " + QByteArray::number(response.range->first) + '-' + QByteArray::number(*response.range->last)
            + '/' + (response.total ? QByteArray::number(*response.total) : QByteArray("*")) + "\r\n";
        if (!response.etag.isEmpty()) bytes += "ETag: " + response.etag + "\r\n";
        bytes += "Cache-Control: no-store\r\n\r\n"; client->write(bytes);
    }
    void output(const QByteArray& bytes) {
        if (bytes.isEmpty() || finished) return;
        if (!sent) sendHeaders();
        if (chunkOutput) client->write(QByteArray::number(bytes.size(),16) + "\r\n" + bytes + "\r\n"); else client->write(bytes);
        if (worker->state->testing && worker->state->testing->afterWrite) worker->state->testing->afterWrite();
    }
    bool body(QByteArray bytes) {
        if (bytes.isEmpty()) return true;
        if (received > MaximumBody-quint64(bytes.size()) || worker->totalBytes > MaximumBody-quint64(bytes.size())) { reject(502); return false; }
        received += quint64(bytes.size()); worker->totalBytes += quint64(bytes.size());
        worker->state->bytes.store(worker->totalBytes,std::memory_order_relaxed);
        if ((expected && received > *expected) || (route.resource.kind == Kind::Key && received > 16)) { reject(502); return false; }
        // Keep the last bounded block until the upstream framing is complete.
        if (expected && received == *expected) { tail=std::move(bytes); return true; }
        return deliver(std::move(bytes));
    }
    bool deliver(QByteArray bytes,bool final = false) {
        if (!classified) {
            sniff += bytes; if (sniff.size() < 8 && !final && !(expected && received == *expected) && !ended) return true;
            const auto front = sniff.trimmed();
            if (front.startsWith("#EXTM3U")) {
                if (routeId != "main" && !hls) { reject(502); return false; }
                if (routeId == "main" && worker->state->spec.format == Format::Progressive) { reject(502); return false; }
                hls = true;
            } else if (routeId == "main" && worker->state->spec.format == Format::Auto && (front.startsWith("<?xml") || front.startsWith("<MPD"))) {
                hls = dash = true;
            } else if (!hls && route.resource.kind != Kind::Key && route.resource.kind != Kind::SessionData
                && (front.startsWith('<') || front.startsWith('{') || front.startsWith('[') || front.startsWith("v=0"))) { reject(502); return false; }
            if (!hls && route.resource.kind != Kind::Key && route.resource.kind != Kind::SessionData && !mediaType(response.type)) { reject(502); return false; }
            if (hls && !wholePlaylist()) { reject(416); return false; }
            classified = true; bytes = std::move(sniff);
        }
        if (hls || route.resource.kind == Kind::Key || route.resource.kind == Kind::SessionData) {
            const qsizetype maximum = hls ? 1024*1024 : route.resource.kind == Kind::Key ? 16 : 65536;
            if (manifest.size() > maximum-bytes.size()) { reject(502); return false; }
            manifest += bytes;
        } else output(bytes);
        return true;
    }
    void complete() {
        if (finished) return;
        if (request.method != "HEAD" && ((expected && received != *expected) || (response.chunked && !chunks.done())
            || (!expected && !response.chunked && !tlsClosed) || (route.resource.kind == Kind::Key && received != 16))) { reject(502); return; }
        if (request.method != "HEAD") {
            if (!tail.isEmpty()) { if (!deliver(std::exchange(tail,{}),true)) return; }
            else if (!classified && !deliver({},true)) return;
        }
        if (request.method != "HEAD" && (hls || route.resource.kind == Kind::Key || route.resource.kind == Kind::SessionData)) {
            if (hls) {
                const auto converted = worker->rewrite(manifest,url,routeId,dash ? Format::Dash : Format::Hls); if (!converted.valid()) { reject(502); return; }
                manifest = converted.bytes;
                response.etag.clear();
            }
            expected = quint64(manifest.size()); response.range.reset(); response.status = 200; sendHeaders(); output(manifest);
        } else if (!sent) sendHeaders();
        if (request.method != "HEAD" && chunkOutput) client->write("0\r\n\r\n");
        finished = true; dropRemote(); client->disconnectFromHost(); if (client->state() == QAbstractSocket::UnconnectedState) dispose();
    }
    void pump() {
        if (finished || !remote) return;
        if (!current()) { reject(502); return; }
        if (!headers && !parseHeaders()) return;
        const auto buffered = quint64(client->bytesToWrite() + remote->bytesAvailable() + wire.size() + sniff.size() + manifest.size() + remoteHeader.size() + tail.size());
        auto previous = worker->state->maximumBuffered.load(std::memory_order_relaxed);
        while (previous < buffered && !worker->state->maximumBuffered.compare_exchange_weak(previous,buffered,std::memory_order_relaxed)) {}
        while (!finished && client->bytesToWrite() < 65536) {
            QByteArray part;
            if (response.chunked) {
                const auto input = !wire.isEmpty() ? std::exchange(wire,{}) : remote->read(16384);
                if (!chunks.feed(input,&part)) { reject(502); return; }
            } else part = !wire.isEmpty() ? std::exchange(wire,{}) : remote->read(16384);
            if (!part.isEmpty()) { progress = worker->elapsed.elapsed(); if (!body(std::move(part))) return; }
            if ((response.chunked && chunks.done()) || (!response.chunked && expected && received == *expected)) {
                if (!wire.isEmpty() || remote->bytesAvailable()) { reject(502); return; }
                if (ended) { complete(); return; }
                break;
            }
            if (remote->bytesAvailable() == 0 && (!response.chunked || !chunks.buffered())) break;
            if (part.isEmpty() && remote->bytesAvailable() == 0) break;
        }
        if (ended && remote->bytesAvailable() == 0 && client->bytesToWrite() < 65536) {
            complete();
        }
    }
    void tick(qint64 now) {
        const int idle = worker->state->testing ? std::clamp(worker->state->testing->idleMs,100,15000) : 15000;
        if ((!parsed || !headers) && now-begun > std::min(idle,10000)) reject(504);
        else if (!finished && client->bytesToWrite() < 65536 && now-progress > idle) reject(504);
    }
};
AddonStreamProxy::Worker::~Worker()
{
    { std::lock_guard<std::mutex> lock(state->events->mutex); state->events->owner = nullptr; state->events->dns.clear(); }
    const auto pending = clients.values(); for (auto* client : pending) delete client;
}
bool AddonStreamProxy::Worker::init()
{
    if (!state->current() || !origin(state->spec.upstream) || !server.listen(QHostAddress::LocalHost,0)) return false;
    state->port = server.serverPort(); state->prefix = '/' + opaque().toLatin1() + '/';
    state->uri = QStringLiteral("http://127.0.0.1:") + QString::number(state->port) + QString::fromLatin1(state->prefix) + QStringLiteral("main");
    routes.insert("main",Route{Resource{state->spec.upstream,(state->spec.format == Format::Hls || state->spec.format == Format::Dash) ? Kind::Playlist : Kind::Media,{}},{},{},0,0,0,{},{}}); state->resourceCount.store(1); return true;
}
void AddonStreamProxy::Worker::close()
{ state->cancel(); state->notify(false); server.close(); timer.stop(); loop->quit(); }
void AddonStreamProxy::Worker::accept()
{
    while (server.hasPendingConnections()) {
        auto* socket = server.nextPendingConnection(); if (!socket) break;
        if (!state->current() || clients.size() >= 4) { socket->abort(); socket->deleteLater(); continue; }
        auto* connection = new Connection(this,socket); clients.insert(connection); state->connections.store(unsigned(clients.size()));
        if (socket->bytesAvailable()) connection->readRequest();
    }
}
void AddonStreamProxy::Worker::tick()
{
    if (!state->current() || elapsed.elapsed() > MaximumInputMs) { close(); return; }
    dnsResults();
    const auto pending = clients.values(); for (auto* connection : pending) connection->tick(elapsed.elapsed());
}
void AddonStreamProxy::Worker::authorized(quint64 id,quint64 hop,bool valid,std::shared_ptr<State::Task> task)
{ for (auto* client : clients) if (client->id == id && client->hop == hop) { client->authorized(valid,std::move(task)); return; } }
void AddonStreamProxy::Worker::resolved(quint64 id,quint64 hop,std::vector<AddonStreamDns::Address> results)
{
    QList<QHostAddress> addresses;
    for(const auto& value:results) {
        if(value.family==4) addresses.append(QHostAddress((quint32(value.bytes[0])<<24)|(quint32(value.bytes[1])<<16)|(quint32(value.bytes[2])<<8)|quint32(value.bytes[3])));
        else if(value.family==6) { Q_IPV6ADDR bytes; std::copy(value.bytes.begin(),value.bytes.end(),bytes.c); QHostAddress address(bytes);
            if(value.scope) address.setScopeId(QString::number(value.scope));
            addresses.append(address); }
        else { addresses.clear(); break; }
    }
    for(auto* client:clients) if(client->id==id && client->hop==hop) { client->resolved(addresses); return; }
}
void AddonStreamProxy::Worker::dnsResults()
{
    std::vector<State::DnsResult> results;
    { std::lock_guard<std::mutex> lock(state->events->mutex); results.swap(state->events->dns); }
    for(auto& value:results) resolved(value.connection,value.phase,std::move(value.addresses));
}
void AddonStreamProxy::Worker::prune()
{
    QSet<QByteArray> used; for (const auto* client : clients) if (!client->finished) used.insert(client->routeId);
    for (auto it = routes.begin(); it != routes.end();) {
        if (it.key() != "main" && !used.contains(it.key()) && elapsed.elapsed()-it->used > 120000) it = routes.erase(it); else ++it;
    }
    state->resourceCount.store(unsigned(routes.size()));
}
AddonStreamManifest::Result AddonStreamProxy::Worker::rewrite(const QByteArray& bytes,const QUrl& upstream,const QByteArray& parent,Format format)
{
    prune();
    const auto found = routes.constFind(parent); if (found == routes.cend()) return {};
    const auto depth = found->depth; const auto window = found->window + 1;
    QHash<QByteArray,Route> staged; QSet<QByteArray> touched; auto nextOrigins = origins;
    const auto convert = format == Format::Dash ? AddonStreamManifest::dash : AddonStreamManifest::hls;
    const auto result = convert(bytes,upstream,QUrl(QStringLiteral("http://127.0.0.1:") + QString::number(state->port) + QString::fromLatin1(state->prefix)),
        [&](const QList<Resource>& entries) {
            QStringList urls;
            for (const auto& entry : entries) {
                if (!state->current() || !AddonStreamPolicy::url(entry.url) || depth >= 8 || (entry.kind == Kind::Playlist && depth >= 4)) return QStringList{};
                nextOrigins.insert(AddonStreamPolicy::origin(entry.url)); if (nextOrigins.size() > 16) return QStringList{};
                const auto key = descriptor(entry,parent); QByteArray id;
                for (auto it = routes.cbegin(); it != routes.cend(); ++it) if (it->key == key) { id = it.key(); break; }
                if (id.isEmpty()) for (auto it = staged.cbegin(); it != staged.cend(); ++it) if (it->key == key) { id = it.key(); break; }
                if (id.isEmpty()) { id = opaque().toLatin1(); staged.insert(id,Route{entry,parent,key,depth+1,window,elapsed.elapsed(),{},{}}); }
                touched.insert(id); urls.append(QStringLiteral("http://127.0.0.1:") + QString::number(state->port) + QString::fromLatin1(state->prefix+id));
            }
            qint64 size = 0; for (const auto& route : routes) size += route.resource.url.toEncoded().size(); for (const auto& route : staged) size += route.resource.url.toEncoded().size();
            if (routes.size()+staged.size() > 2048 || size > 4*1024*1024) return QStringList{};
            return urls;
        });
    if (!result.valid() || !state->current()) return {};
    origins = std::move(nextOrigins); for (auto it = staged.begin(); it != staged.end(); ++it) routes.insert(it.key(),it.value());
    routes[parent].window = window;
    for (const auto& id : touched) { routes[id].window = window; routes[id].used = elapsed.elapsed(); }
    QSet<QByteArray> active; for (const auto* client : clients) if (!client->finished) active.insert(client->routeId);
    for (auto it = routes.begin(); it != routes.end();) {
        if (it->parent == parent && it->window+2 < window && !active.contains(it.key())) it = routes.erase(it); else ++it;
    }
    state->resourceCount.store(unsigned(routes.size())); state->originCount.store(unsigned(origins.size()));
    return result;
}
bool AddonStreamProxy::State::post(std::function<void(Worker*)> call)
{
    std::lock_guard<std::mutex> lock(events->mutex); auto* target = events->owner;
    return target && QMetaObject::invokeMethod(target,[target,call=std::move(call)] { call(target); },Qt::QueuedConnection);
}
void AddonStreamProxy::State::cancel() noexcept
{
    active->store(false,std::memory_order_release); sockets.cancel();
    Cancel cleanup;
    { std::lock_guard<std::mutex> lock(mutex); cleanup.swap(nativeCancel); }
    if (cleanup) try { cleanup(); } catch (...) {}
}
void AddonStreamProxy::State::notify(bool value) noexcept
{
    Started ready;
    { std::lock_guard<std::mutex> lock(mutex); ready.swap(completion); }
    if (ready) try { ready(value); } catch (...) { active->store(false,std::memory_order_release); sockets.cancel(); }
}
AddonStreamProxy::AddonStreamProxy(std::shared_ptr<State> state) : m_state(std::move(state)) {}
AddonStreamProxy::~AddonStreamProxy() { cancel(); }
AddonStreamProxy::Cancel AddonStreamProxy::prepare(Spec spec,Ready ready)
{ return create(std::move(spec),{},std::move(ready)); }
AddonStreamProxy::Cancel AddonStreamProxy::prepareForTesting(Spec spec,Testing testing,Ready ready)
{ return create(std::move(spec),std::make_shared<Testing>(std::move(testing)),std::move(ready)); }
AddonStreamProxy::Cancel AddonStreamProxy::create(Spec spec,std::shared_ptr<Testing> testing,Ready ready)
{
    if (!ready) return {};
    const auto callback = std::make_shared<Ready>(std::move(ready));
    unsigned count = sessions.load(std::memory_order_relaxed);
    do { if (count >= 8 || !spec.lifetime.current() || !QCoreApplication::instance() || !AddonStreamPolicy::url(spec.upstream) || !spec.authorize || !evaluate(spec.live)) { (*callback)({},unavailable()); return {}; } }
    while (!sessions.compare_exchange_weak(count,count+1,std::memory_order_acq_rel,std::memory_order_relaxed));
    std::shared_ptr<State> state;
    std::shared_ptr<SessionLease> slot;
    try {
        slot=std::make_shared<SessionLease>();
        state = std::make_shared<State>(); state->spec = std::move(spec); state->testing = std::move(testing); state->activity->slot=slot;
        state->events->lifetime=state->spec.lifetime;
        const bool accepted=state->spec.lifetime.startNetwork([state,callback] {
            bool delivered = false;
            try {
                QEventLoop loop; Worker worker(state,&loop);
                { std::lock_guard<std::mutex> lock(state->events->mutex); state->events->owner = &worker; }
                bool valid = false; try { valid = worker.init(); } catch (...) {}
                delivered = true;
                try { (*callback)(valid ? std::shared_ptr<AddonStreamProxy>(new AddonStreamProxy(state)) : nullptr,valid ? QString{} : unavailable()); } catch (...) { state->cancel(); }
                *callback = {};
                if (valid && state->testing && state->testing->afterReady) state->testing->afterReady();
                if (valid && state->current()) loop.exec();
                state->cancel();
            } catch (...) { state->cancel(); if (!delivered) try { (*callback)({},unavailable()); } catch (...) {} }
            state->notify(false); *callback={}; state->testing.reset(); state->releaseAuthority(); state->spec.upstream=QUrl{};
        },[state] { state->cancel(); },
        [state,slot](auto ticket) {
            if(!ticket.retain(slot)) throw std::bad_alloc();
            state->network=std::move(ticket);
        });
        if(!accepted) { (*callback)({},unavailable()); return {}; }
    } catch (...) { if (!slot) sessions.fetch_sub(1,std::memory_order_release); (*callback)({},unavailable()); return {}; }
    return [weak=std::weak_ptr<State>(state)] { if (const auto value = weak.lock()) value->cancel(); };
}
QString AddonStreamProxy::uri() const { return m_state->uri; }
unsigned AddonStreamProxy::port() const { return m_state->port; }
QByteArray AddonStreamProxy::pathPrefix() const { return m_state->prefix; }
std::shared_ptr<std::atomic_bool> AddonStreamProxy::activeFlag() const { return m_state->active; }
void AddonStreamProxy::start(Current admission,Started ready)
{
    const auto state = m_state;
    if(!ready) return;
    try {
        bool rejected=false; State::Authority authority;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            rejected=state->authorityReleased || !state->active->load(std::memory_order_acquire)
                || !state->spec.lifetime.current() || state->requested.exchange(true,std::memory_order_acq_rel);
            if(!rejected) { authority={state->spec.live,state->spec.authorize}; state->completion.swap(ready); }
        }
        if(rejected) { Started refused; refused.swap(ready); refused(false); return; }
        const auto task=state->task(); if (!task) { state->cancel(); return; }
        const bool accepted=state->spec.lifetime.startQtWork([state,task,authority,admission=std::move(admission)] {
            if (!state->current(authority.live) || !evaluate(admission) || !evaluate(authority.authorize)) { state->cancel(); return; }
            try {
                if (!state->post([state](Worker*) { const bool valid = state->current(); state->started.store(valid,std::memory_order_release); state->notify(valid); if (!valid) state->cancel(); })) state->cancel();
            } catch (...) { state->cancel(); }
        },[state] { state->cancel(); },
        [task](auto ticket) { if(!ticket.retain(task)) throw std::bad_alloc(); });
        if(!accepted) state->cancel();
    } catch (...) { state->cancel(); if(ready) try { ready(false); } catch(...) {} }
}
void AddonStreamProxy::cancel() noexcept { m_state->cancel(); }
bool AddonStreamProxy::onCancelled(Cancel cleanup)
{
    if (!cleanup) return false;
    bool immediate=false;
    {
        std::lock_guard<std::mutex> lock(m_state->mutex); if (m_state->cancelHookSet) return false;
        m_state->cancelHookSet=true;
        if (m_state->active->load(std::memory_order_acquire)) m_state->nativeCancel=std::move(cleanup); else immediate=true;
    }
    if (immediate) cleanup();
    return true;
}
bool AddonStreamProxy::waitStopped(int milliseconds) const
{
    const auto timeout=std::chrono::milliseconds(std::clamp(milliseconds,0,30000));
    const auto deadline=std::chrono::steady_clock::now()+timeout;
    if(!m_state->network.waitJoined(timeout)) return false;
    const auto activity=m_state->activity; std::unique_lock<std::mutex> lock(activity->mutex);
    return activity->changed.wait_until(lock,deadline,[&] { return activity->tasks==0; });
}
unsigned AddonStreamProxy::liveSessions() { return sessions.load(std::memory_order_acquire); }
QVariantMap AddonStreamProxy::statistics() const
{
    return {{"bytes",QVariant::fromValue<qulonglong>(m_state->bytes.load())},{"maximumBuffered",QVariant::fromValue<qulonglong>(m_state->maximumBuffered.load())},
        {"connections",m_state->connections.load()},{"resources",m_state->resourceCount.load()},{"origins",m_state->originCount.load()}};
}
}
