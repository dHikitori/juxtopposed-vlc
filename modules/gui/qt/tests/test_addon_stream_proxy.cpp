// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QTcpServer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QEventLoop>
#include <QTimer>
#include <QSemaphore>
#include <QScopeGuard>
#include <QRegularExpression>
#include <QSslKey>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <utility>
#include "addons/addon_stream_proxy.hpp"
#include "addons/addon_stream_shutdown.hpp"
#ifdef VLC_ADDON_NATIVE_STREAM_TESTS
#include <vlc_common.h>
#include <vlc_input_item.h>
#include <vlc_input_policy.h>
#include <vlc_player.h>
#include "lib/libvlc_internal.h"
#include "addons/addon_source_admissions.hpp"
#include "addons/addon_stream_proxy_native.hpp"
#endif
using namespace vlc::addons;
namespace {
struct NativeWait {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered=false,released=false;
    void block() { std::unique_lock<std::mutex> lock(mutex); entered=true; changed.notify_all(); changed.wait(lock,[&] { return released; }); }
    bool wait() { std::unique_lock<std::mutex> lock(mutex); return changed.wait_for(lock,std::chrono::seconds(3),[&] { return entered; }); }
    void release() { { std::lock_guard<std::mutex> lock(mutex); released=true; } changed.notify_all(); }
};
std::vector<AddonStreamDns::Address> publicFixtureAddress()
{ AddonStreamDns::Address value; value.family=4; value.bytes[0]=93; value.bytes[1]=184; value.bytes[2]=216; value.bytes[3]=34; return {value}; }
int dnsAfterAppChild(int argc,char** argv)
{
    const auto wait=std::make_shared<NativeWait>(); bool safe=false;
    {
        QCoreApplication app(argc,argv); AddonStreamShutdown lifetime(app);
        struct Outcome { std::mutex mutex; std::condition_variable changed; std::shared_ptr<AddonStreamProxy> proxy; bool ready=false,started=false; };
        const auto result=std::make_shared<Outcome>();
        AddonStreamProxy::Spec spec{QUrl(QStringLiteral("https://media.example/file")),AddonStreamProxy::Format::Progressive,
            [] { return true; },[] { return true; },lifetime.handle()};
        AddonStreamProxy::Testing testing;
        testing.nativeResolve=[wait](const std::string&) { wait->block(); return publicFixtureAddress(); };
        auto cancel=AddonStreamProxy::prepareForTesting(std::move(spec),std::move(testing),[result](auto value,const QString&) {
            { std::lock_guard<std::mutex> lock(result->mutex); result->proxy=std::move(value); result->ready=true; } result->changed.notify_all();
        });
        { std::unique_lock<std::mutex> lock(result->mutex); result->changed.wait_for(lock,std::chrono::seconds(3),[&] { return result->ready; }); }
        if(result->proxy) {
            result->proxy->start([] { return true; },[result](bool value) {
                { std::lock_guard<std::mutex> lock(result->mutex); result->started=value; } result->changed.notify_all();
            });
            { std::unique_lock<std::mutex> lock(result->mutex); result->changed.wait_for(lock,std::chrono::seconds(3),[&] { return result->started; }); }
            QTcpSocket client; client.connectToHost(QHostAddress::LocalHost,quint16(result->proxy->port()));
            if(client.waitForConnected(2000)) {
                client.write(QByteArray("GET ")+QUrl(result->proxy->uri()).path().toLatin1()+" HTTP/1.1\r\nHost: 127.0.0.1:"+
                    QByteArray::number(result->proxy->port())+"\r\n\r\n"); client.waitForBytesWritten(2000);
                safe=wait->wait() && lifetime.finalGuard(3000) && !result->proxy->waitStopped(0)
                    && AddonStreamLifetime::processPlainCount()==1;
            }
            client.abort(); result->proxy.reset();
        }
        if(cancel) cancel();
        safe=safe && lifetime.finalGuard(3000);
    }
    // Only native/std work remains after the actual app destructor.
    wait->release(); const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(AddonStreamLifetime::processPlainCount() && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return safe && !AddonStreamLifetime::processPlainCount() && !AddonStreamProxy::liveSessions() ? 0 : 7;
}
struct Reply { QByteArray bytes; bool hold = false; QByteArray delayed{}; };
struct TlsServer : QTcpServer {
    QSslCertificate certificate; QSslKey key;
    std::function<Reply(const QByteArray&)> handler;
    void incomingConnection(qintptr descriptor) override {
        auto* socket = new QSslSocket(this); if (!socket->setSocketDescriptor(descriptor)) { delete socket; return; }
        socket->setLocalCertificate(certificate); socket->setPrivateKey(key); socket->setPeerVerifyMode(QSslSocket::VerifyNone);
        auto request = std::make_shared<QByteArray>(); auto replied = std::make_shared<bool>(false);
        connect(socket,&QSslSocket::readyRead,socket,[this,socket,request,replied] {
            if (*replied) return;
            *request += socket->read(8193-request->size());
            if (request->size() > 8192) { socket->abort(); return; }
            if (!request->contains("\r\n\r\n")) return;
            *replied = true; const auto result = handler(*request); socket->write(result.bytes);
            if (!result.delayed.isEmpty()) QTimer::singleShot(50,socket,[socket,result] { socket->write(result.delayed); socket->disconnectFromHost(); });
            else if (!result.hold) socket->disconnectFromHost();
        });
        connect(socket,&QSslSocket::disconnected,socket,&QObject::deleteLater); socket->startServerEncryption();
    }
};
struct Server {
    struct State {
        std::mutex mutex; std::condition_variable ready; unsigned port = 0;
        std::atomic_bool stop{false}; std::atomic_uint requests{0}; QList<QByteArray> seen;
    };
    std::shared_ptr<State> state = std::make_shared<State>(); std::thread thread;
    Server(const QSslCertificate& certificate,const QSslKey& key,std::function<Reply(const QByteArray&)> handler) {
        thread = std::thread([value=state,certificate,key,handler=std::move(handler)] {
            QEventLoop loop; TlsServer server; server.certificate=certificate; server.key=key;
            server.handler=[value,handler](const QByteArray& request) {
                { std::lock_guard<std::mutex> lock(value->mutex); value->seen.append(request); } ++value->requests;
                return handler(request);
            };
            server.listen(QHostAddress::LocalHost,0);
            { std::lock_guard<std::mutex> lock(value->mutex); value->port=server.serverPort(); } value->ready.notify_all();
            QTimer timer; timer.setInterval(10); QObject::connect(&timer,&QTimer::timeout,&loop,[&] { if (value->stop.load()) loop.quit(); }); timer.start(); loop.exec();
        });
        std::unique_lock<std::mutex> lock(state->mutex); state->ready.wait_for(lock,std::chrono::seconds(3),[&] { return state->port; });
    }
    ~Server() { state->stop=true; if (thread.joinable()) thread.join(); }
    unsigned port() const { return state->port; }
};
Reply ok(const QByteArray& body,const QByteArray& type="audio/mpeg")
{ return {QByteArray("HTTP/1.1 200 OK\r\nContent-Type: ")+type+"\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body,false}; }
struct Result { int status=0; QByteArray body; QNetworkReply::NetworkError error=QNetworkReply::UnknownNetworkError; QList<QByteArray> headers; QByteArray etag; };
Result get(const QString& uri,const QMap<QByteArray,QByteArray>& headers={})
{
    QNetworkAccessManager network; network.setProxy(QNetworkProxy::NoProxy); QNetworkRequest request{QUrl(uri)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::ManualRedirectPolicy);
    for (auto it=headers.begin();it!=headers.end();++it) request.setRawHeader(it.key(),it.value());
    auto* reply=network.get(request); QEventLoop loop; QTimer timeout; timeout.setSingleShot(true); timeout.setInterval(5000);
    QObject::connect(reply,&QNetworkReply::finished,&loop,&QEventLoop::quit); QObject::connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);
    timeout.start(); loop.exec();
    const Result result{reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(),reply->readAll(),reply->error(),reply->rawHeaderList(),reply->rawHeader("ETag")};
    reply->abort(); delete reply; return result;
}
#ifdef VLC_ADDON_NATIVE_STREAM_TESTS
struct NativePlayback {
    using Resolver=AddonNativeSourceResolver;
    struct Source { std::atomic_bool open{true},saved{true}; std::atomic_uint admitted{0}; };
    std::shared_ptr<Source> source=std::make_shared<Source>();
    QTemporaryDir profile;
    std::shared_ptr<AddonSourceAdmissions> admissions;
    std::shared_ptr<AddonSourceConsent> consent;
    AddonSourceConsent::Binding binding{AddonSourceConsent::SourceKind::Git,
        QStringLiteral("https://example.org/radio.git"),QStringLiteral("org.example.radio/source"),QStringLiteral("public")};
    libvlc_instance_t* instance=nullptr;
    vlc_player_t* player=nullptr;
    vlc_player_listener_id* listener=nullptr;
    input_item_t* item=nullptr;
    std::unique_ptr<Resolver> resolver;
    QString uri;
    std::atomic<quint64> decoded{0};
    std::atomic_uint stopped{0},errors{0};
    static void statistics(vlc_player_t*,const input_stats_t* stats,void* opaque) {
        auto& value=*static_cast<NativePlayback*>(opaque); auto previous=value.decoded.load();
        while(previous<stats->i_decoded_audio && !value.decoded.compare_exchange_weak(previous,stats->i_decoded_audio)) {}
    }
    static void state(vlc_player_t*,vlc_player_state state,void* opaque) {
        if (state==VLC_PLAYER_STATE_STOPPED) ++static_cast<NativePlayback*>(opaque)->stopped;
    }
    static void error(vlc_player_t*,vlc_player_error,void* opaque) { ++static_cast<NativePlayback*>(opaque)->errors; }
    bool init(AddonStreamProxy::Spec spec,AddonStreamProxy::Testing testing) {
        static const char* const args[]={"--ignore-config","--quiet","--no-plugins-cache","--no-media-library",
            "--no-metadata-network-access","--aout=adummy","--vout=vdummy","--text-renderer=tdummy"};
        instance=libvlc_new(int(std::size(args)),args); if (!instance) return false;
        if (var_SetString(instance->p_libvlc_int,"window","wdummy")!=VLC_SUCCESS) return false;
        const auto state=source;
        admissions=std::make_shared<AddonSourceAdmissions>(profile.filePath(QStringLiteral("admissions")),
            [state](const QVariantMap&) { return state->open.load(); });
        const auto records=admissions; const auto key=binding;
        consent=std::make_shared<AddonSourceConsent>(profile.path(),AddonSourceConsent::Checks{
            [state,key](const auto& value) { return state->open.load() && value==key; },
            [records](const auto&,const auto& uri) { return !records->currentReference(uri).isEmpty(); }});
        const QVariantMap reference{{"format","hakone-addon-source-v1"},{"source",binding.contribution},{"accountReference",binding.accountReference},
            {"itemId","station"},{"snapshot",QVariantMap{{"title","Native TLS station"},{"mediaKind","audio"}}}};
        QStringList values; if (!admissions->admit({reference},&values) || values.size()!=1) return false; uri=values.first();
        const auto ticket=consent->capture(binding); if (!ticket || !consent->grant(ticket->id)) return false;
        const auto approvals=consent;
        Resolver::Backend backend{
            [state,records,approvals,key](const QString& uri,quint64 generation)->std::optional<Resolver::Admission> {
                if (!state->open.load() || records->currentReference(uri).isEmpty()) return {};
                const auto intent=approvals->mintIntent(key,uri,generation,AddonSourceConsent::IntentReason::PrimaryPlayback);
                const auto receipt=approvals->consumeIntent(intent,key,uri,generation,AddonSourceConsent::IntentReason::PrimaryPlayback);
                if (!receipt) return {};
                ++state->admitted;
                return Resolver::Admission{approvals,*receipt,[state] { return state->open.load() && state->saved.load(); },[state] { return state->open.load(); },{}};
            },
            [spec,testing](const Resolver::Admission& admission,auto current,auto ready) mutable->Resolver::Cancel {
                return prepareContainedSourceForTesting(admission,std::move(current),spec,testing,std::move(ready));
            },spec.lifetime};
        resolver=std::make_unique<Resolver>(VLC_OBJECT(instance->p_libvlc_int),std::move(backend)); if (!resolver->bound()) return false;
        player=vlc_player_New(VLC_OBJECT(instance->p_libvlc_int),VLC_PLAYER_LOCK_NORMAL); if (!player) return false;
        static const vlc_player_cbs callbacks=[] { vlc_player_cbs value{}; value.on_state_changed=NativePlayback::state;
            value.on_statistics_changed=NativePlayback::statistics; value.on_error_changed=NativePlayback::error; return value; }();
        vlc_player_Lock(player); listener=vlc_player_AddListener(player,&callbacks,this); vlc_player_Unlock(player);
        item=input_item_New(uri.toUtf8().constData(),"Native TLS station"); return listener && item;
    }
    bool play() {
        decoded=0; vlc_player_Lock(player); const int set=vlc_player_SetCurrentMedia(player,item);
        const int started=set==VLC_SUCCESS ? vlc_player_Start(player) : set; vlc_player_Unlock(player); return started==VLC_SUCCESS;
    }
    void stop() { if(player) { vlc_player_Lock(player); vlc_player_Stop(player); vlc_player_Unlock(player); } }
    ~NativePlayback() {
        source->open=false; if (resolver) resolver->close();
        if(player) { vlc_player_Lock(player); vlc_player_Stop(player); if(listener) vlc_player_RemoveListener(player,listener);
            vlc_player_Unlock(player); vlc_player_Delete(player); }
        resolver.reset(); if(item) input_item_Release(item); if(instance) libvlc_release(instance);
    }
};
QByteArray wave() {
    QByteArray pcm(4*8000*2,'\0');
    for(qsizetype i=0;i<pcm.size();i+=2) { const qint16 sample=((i/2)%40<20)?4096:-4096; pcm[i]=char(sample&255); pcm[i+1]=char((sample>>8)&255); }
    QByteArray result("RIFF"); const auto number=[&](quint32 n,unsigned size) { for(unsigned i=0;i<size;++i) result.append(char((n>>(8*i))&255)); };
    number(quint32(pcm.size()+36),4); result+="WAVEfmt "; number(16,4); number(1,2); number(1,2);
    number(8000,4); number(16000,4); number(2,2); number(16,2); result+="data"; number(quint32(pcm.size()),4); result+=pcm; return result;
}
#endif
}
class TestAddonStreamProxy : public QObject
{
    Q_OBJECT
    QTemporaryDir files{QDir::tempPath()+QStringLiteral("/vlc-stream-proxy-fixture-XXXXXX")};
    QSslCertificate certificate;
    QSslKey key;
    std::unique_ptr<AddonStreamShutdown> lifetime;
    AddonStreamProxy::Testing fixture(Server& server) {
        AddonStreamProxy::Testing value; value.authorities={certificate}; value.idleMs=1500;
        value.resolve=[](const QString&) { return QList<QHostAddress>{QHostAddress(QStringLiteral("93.184.216.34"))}; };
        const auto port=server.port(); value.connect=[port](QSslSocket* socket,const AddonStreamPolicy::Plan& plan) {
            socket->connectToHostEncrypted(QStringLiteral("127.0.0.1"),quint16(port),plan.peerName);
        }; return value;
    }
    std::shared_ptr<AddonStreamProxy> prepare(AddonStreamProxy::Spec spec,AddonStreamProxy::Testing testing) {
        struct Outcome { std::mutex mutex; std::condition_variable ready; bool done=false; std::shared_ptr<AddonStreamProxy> result; };
        const auto outcome=std::make_shared<Outcome>();
        const auto cancel=AddonStreamProxy::prepareForTesting(std::move(spec),std::move(testing),[outcome](auto value,const QString&) {
            { std::lock_guard<std::mutex> lock(outcome->mutex); outcome->result=std::move(value); outcome->done=true; } outcome->ready.notify_all();
        });
        std::unique_lock<std::mutex> lock(outcome->mutex);
        if (!outcome->ready.wait_for(lock,std::chrono::seconds(3),[&] { return outcome->done; })) { lock.unlock(); cancel(); return {}; }
        auto result=std::move(outcome->result); return result;
    }
    bool start(const std::shared_ptr<AddonStreamProxy>& proxy) {
        struct Outcome { std::mutex mutex; std::condition_variable ready; bool done=false,valid=false; };
        const auto outcome=std::make_shared<Outcome>();
        proxy->start([] { return true; },[outcome](bool value) { { std::lock_guard<std::mutex> lock(outcome->mutex); outcome->valid=value; outcome->done=true; } outcome->ready.notify_all(); });
        std::unique_lock<std::mutex> lock(outcome->mutex); return outcome->ready.wait_for(lock,std::chrono::seconds(3),[&] { return outcome->done; }) && outcome->valid;
    }
    AddonStreamProxy::Spec spec(const QString& path=QStringLiteral("/file"),AddonStreamProxy::Format format=AddonStreamProxy::Format::Auto)
    { return {QUrl(QStringLiteral("https://media.example")+path),format,[] { return true; },[] { return true; },lifetime->handle()}; }
private slots:
    void initTestCase() {
        const auto executable=QStandardPaths::findExecutable(QStringLiteral("openssl")); QVERIFY(!executable.isEmpty());
        const auto cert=files.filePath(QStringLiteral("certificate.pem")),secret=files.filePath(QStringLiteral("fixture-key.pem"));
        QProcess process; process.start(executable,{QStringLiteral("req"),QStringLiteral("-x509"),QStringLiteral("-newkey"),QStringLiteral("rsa:2048"),
            QStringLiteral("-nodes"),QStringLiteral("-days"),QStringLiteral("2"),QStringLiteral("-subj"),QStringLiteral("/CN=media.example"),
            QStringLiteral("-addext"),QStringLiteral("subjectAltName=DNS:media.example,DNS:cdn.example"),
            // A server certificate, not a CA: macOS trust evaluation refuses
            // a self-signed CA certificate as the TLS server's own.
            QStringLiteral("-addext"),QStringLiteral("basicConstraints=critical,CA:FALSE"),
            QStringLiteral("-addext"),QStringLiteral("keyUsage=critical,digitalSignature,keyEncipherment"),
            QStringLiteral("-addext"),QStringLiteral("extendedKeyUsage=serverAuth"),
            QStringLiteral("-keyout"),secret,QStringLiteral("-out"),cert});
        QVERIFY(process.waitForFinished(10000)); QCOMPARE(process.exitCode(),0);
        QFile certFile(cert),keyFile(secret); QVERIFY(certFile.open(QIODevice::ReadOnly)); QVERIFY(keyFile.open(QIODevice::ReadOnly));
        certificate=QSslCertificate(certFile.readAll()); key=QSslKey(keyFile.readAll(),QSsl::Rsa); QVERIFY(!certificate.isNull()); QVERIFY(!key.isNull());
    }
    void init() { lifetime=std::make_unique<AddonStreamShutdown>(*QCoreApplication::instance()); }
    void cleanup() {
        QVERIFY(lifetime->finalGuard(3000));
        QTRY_COMPARE_WITH_TIMEOUT(AddonStreamProxy::liveSessions(),0u,5000);
        lifetime.reset();
    }
    void preparationRequiresTheAppLifetime() {
        auto source=spec(); source.lifetime={}; QVERIFY(!prepare(source,{}));
        AddonStreamLifetime retired; retired.close(); source.lifetime=retired.handle(); QVERIFY(!prepare(source,{}));
        QCOMPARE(AddonStreamProxy::liveSessions(),0u);
    }
    void nativeDnsMailboxValidatesEveryAddressBeforeTls() {
        Server server(certificate,key,[](const auto&) { return ok("ID3native-dns"); }); auto testing=fixture(server); testing.resolve={};
        testing.nativeResolve=[](const std::string&) { return publicFixtureAddress(); };
        auto proxy=prepare(spec(),testing); QVERIFY(proxy); QVERIFY(start(proxy)); QCOMPARE(get(proxy->uri()).body,QByteArray("ID3native-dns"));
        proxy->cancel(); QVERIFY(proxy->waitStopped(3000));
        testing.nativeResolve=[](const std::string&) { auto values=publicFixtureAddress(); auto privateAddress=values.front(); privateAddress.bytes[0]=127;
            privateAddress.bytes[1]=0; privateAddress.bytes[2]=0; privateAddress.bytes[3]=1; values.push_back(privateAddress); return values; };
        proxy=prepare(spec(),testing); QVERIFY(proxy); const auto stop=qScopeGuard([&] { proxy->cancel(); proxy->waitStopped(3000); }); QVERIFY(start(proxy));
        QCOMPARE(get(proxy->uri()).status,502); QCOMPARE(server.state->requests.load(),1u);
    }
    void noOutboundIoBeforeStartAndProgressiveBytesUsePinnedTls() {
        const QByteArray body("ID3synthetic music bytes"); Server server(certificate,key,[&](const QByteArray&) { return ok(body); });
        auto testing=fixture(server); std::atomic_bool pinned{false}; const auto dial=testing.connect;
        testing.connect=[&](auto* socket,const auto& plan) { pinned=plan.address==QHostAddress(QStringLiteral("93.184.216.34")) && plan.peerName==QStringLiteral("media.example"); dial(socket,plan); };
        auto proxy=prepare(spec(QStringLiteral("/file?signature=synthetic-only")),std::move(testing)); QVERIFY(proxy);
        const auto stop=qScopeGuard([&] { proxy->cancel(); QVERIFY(proxy->waitStopped(3000)); });
        QCOMPARE(server.state->requests.load(),0u); QCOMPARE(get(proxy->uri()).status,403); QCOMPARE(server.state->requests.load(),0u);
        QVERIFY(start(proxy)); const auto result=get(proxy->uri(),{{"User-Agent","untrusted-client-name"}});
        QCOMPARE(result.status,200); QCOMPARE(result.body,body); QCOMPARE(result.error,QNetworkReply::NoError); QVERIFY(pinned.load());
        std::lock_guard<std::mutex> lock(server.state->mutex); QVERIFY(server.state->seen.first().contains("Host: media.example\r\n"));
        QVERIFY(server.state->seen.first().contains("User-Agent: Hakone-Contained-Media/1\r\n"));
        QVERIFY(!server.state->seen.first().contains("untrusted-client-name")); QVERIFY(!result.headers.contains("Location"));
    }
    void privateDnsAndWrongCertificateNeverSendAnHttpRequest() {
        Server server(certificate,key,[](const auto&) { return ok("ID3body"); });
        auto testing=fixture(server); testing.resolve=[](const auto&) { return QList<QHostAddress>{QHostAddress(QStringLiteral("93.184.216.34")),QHostAddress::LocalHost}; };
        auto proxy=prepare(spec(),testing); QVERIFY(proxy); QVERIFY(start(proxy)); QCOMPARE(get(proxy->uri()).status,502); QCOMPARE(server.state->requests.load(),0u);
        proxy->cancel(); QVERIFY(proxy->waitStopped(3000)); proxy.reset();
        auto wrong=spec(); wrong.upstream=QUrl(QStringLiteral("https://wrong.example/file")); proxy=prepare(wrong,fixture(server)); QVERIFY(proxy);
        const auto stop=qScopeGuard([&] { proxy->cancel(); proxy->waitStopped(3000); });
        QVERIFY(start(proxy)); QCOMPARE(get(proxy->uri()).status,502); QCOMPARE(server.state->requests.load(),0u);
    }
    void redirectsRepeatDnsAndPrivateTargetsAreDenied() {
        Server server(certificate,key,[](const QByteArray& request) {
            if (request.startsWith("GET /file ")) return Reply{"HTTP/1.1 302 Redirect\r\nLocation: https://cdn.example/final\r\nContent-Length: 0\r\n\r\n"};
            if (request.startsWith("GET /final ")) return ok("ID3redirect result");
            return Reply{"HTTP/1.1 302 Redirect\r\nLocation: https://127.0.0.1/private\r\nContent-Length: 0\r\n\r\n"};
        });
        auto testing=fixture(server); std::atomic_uint lookups{0}; const auto resolve=testing.resolve; testing.resolve=[&](const auto& host) { ++lookups; return resolve(host); };
        auto proxy=prepare(spec(),testing); QVERIFY(proxy); QVERIFY(start(proxy)); const auto stop=qScopeGuard([&] { proxy->cancel(); proxy->waitStopped(3000); });
        const auto result=get(proxy->uri()); QCOMPARE(result.body,QByteArray("ID3redirect result")); QCOMPARE(lookups.load(),2u); QCOMPARE(server.state->requests.load(),2u);
        proxy->cancel(); QVERIFY(proxy->waitStopped(3000)); proxy=prepare(spec(QStringLiteral("/private")),testing); QVERIFY(proxy); QVERIFY(start(proxy));
        QCOMPARE(get(proxy->uri()).status,502); QCOMPARE(server.state->requests.load(),3u);
    }
    void nativeSeekRangesAndConditionalTagsKeepExactBytes() {
        const QByteArray data("ID3abcdefghijklmno");
        Server server(certificate,key,[&](const QByteArray& request) {
            if (request.contains("Range: bytes=3-7\r\n")) return Reply{"HTTP/1.1 206 Partial\r\nContent-Type: audio/mpeg\r\nETag: \"one\"\r\nContent-Range: bytes 3-7/18\r\nContent-Length: 5\r\n\r\nabcde"};
            return Reply{QByteArray("HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nETag: \"one\"\r\nContent-Length: ")+QByteArray::number(data.size())+"\r\n\r\n"+data};
        });
        auto proxy=prepare(spec(),fixture(server)); QVERIFY(proxy); const auto stop=qScopeGuard([&] { proxy->cancel(); proxy->waitStopped(3000); }); QVERIFY(start(proxy));
        const auto initial=get(proxy->uri()); QVERIFY(!initial.etag.isEmpty()); QVERIFY(initial.etag!=QByteArray("\"one\""));
        const auto result=get(proxy->uri(),{{"Range","bytes=3-7"},{"If-Range",initial.etag}}); QCOMPARE(result.status,206); QCOMPARE(result.body,QByteArray("abcde"));
        const auto fail=get(proxy->uri(),{{"Range","bytes=4-8"}}); QCOMPARE(fail.status,502);
    }
    void conditionalResponsesMustKeepTheCapturedStrongTag() {
        std::atomic_int mode{0}; Server server(certificate,key,[&](const QByteArray&) {
            if(mode==1) return Reply{"HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nETag: \"two\"\r\nContent-Length: 6\r\n\r\nID3abc"};
            if(mode==2) return Reply{"HTTP/1.1 206 Partial\r\nContent-Type: audio/mpeg\r\nETag: \"two\"\r\nContent-Range: bytes 0-2/6\r\nContent-Length: 3\r\n\r\nID3"};
            if(mode==3) return Reply{"HTTP/1.1 206 Partial\r\nContent-Type: audio/mpeg\r\nContent-Range: bytes 0-2/6\r\nContent-Length: 3\r\n\r\nID3"};
            return Reply{"HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nETag: \"one\"\r\nContent-Length: 6\r\n\r\nID3abc"};
        });
        auto proxy=prepare(spec(),fixture(server)); QVERIFY(proxy); const auto stop=qScopeGuard([&] { proxy->cancel(); proxy->waitStopped(3000); }); QVERIFY(start(proxy));
        const auto initial=get(proxy->uri()); QCOMPARE(initial.status,200); QVERIFY(!initial.etag.isEmpty());
        mode=1; QCOMPARE(get(proxy->uri(),{{"If-Match",initial.etag}}).status,502);
        mode=2; QCOMPARE(get(proxy->uri(),{{"Range","bytes=0-2"},{"If-Range",initial.etag}}).status,502);
        mode=3; QCOMPARE(get(proxy->uri(),{{"Range","bytes=0-2"},{"If-Range",initial.etag}}).status,502);
        mode=4; QCOMPARE(get(proxy->uri(),{{"Range","bytes=0-2"},{"If-Range",initial.etag}}).status,502);
        QVERIFY(proxy->activeFlag()->load());
    }
    void shortBodiesAndAmbiguousFramingDoNotFinishAsValidMedia() {
        Server server(certificate,key,[](const QByteArray& request) {
            if (request.startsWith("GET /short ")) return Reply{"HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nContent-Length: 30\r\n\r\nID3short"};
            return Reply{"HTTP/1.1 200 OK\r\nContent-Length: 3\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n"};
        });
        auto proxy=prepare(spec(QStringLiteral("/short")),fixture(server)); QVERIFY(proxy); QVERIFY(start(proxy));
        const auto truncated=get(proxy->uri()); QVERIFY(truncated.error!=QNetworkReply::NoError); QVERIFY(truncated.body.size()<30);
        proxy->cancel(); QVERIFY(proxy->waitStopped(3000)); proxy=prepare(spec(),fixture(server)); QVERIFY(proxy); const auto stop=qScopeGuard([&] { proxy->cancel(); proxy->waitStopped(3000); });
        QVERIFY(start(proxy)); QCOMPARE(get(proxy->uri()).status,502);
    }
    void lateBytesAfterTheDeclaredBodyCannotCompleteTheLocalResponse() {
        Server server(certificate,key,[](const auto&) { return Reply{"HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nContent-Length: 3\r\n\r\nID3",true,"extra"}; });
        auto proxy=prepare(spec(),fixture(server)); QVERIFY(proxy); const auto stop=qScopeGuard([&] { proxy->cancel(); proxy->waitStopped(3000); }); QVERIFY(start(proxy));
        const auto result=get(proxy->uri()); QCOMPARE(result.status,502); QVERIFY(result.body.isEmpty());
    }
    void hlsKeysAndSegmentsUseOnlyOpaqueRoutes() {
        const QByteArray playlist("#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-KEY:METHOD=AES-128,URI=\"https://cdn.example/key?secret=synthetic\"\n#EXTINF:2,\nsegment.ts\n#EXT-X-ENDLIST\n");
        Server server(certificate,key,[&](const QByteArray& request) {
            if (request.startsWith("GET /file ")) return ok(playlist,"application/vnd.apple.mpegurl");
            if (request.startsWith("GET /key?secret=synthetic ")) return ok(QByteArray(16,'k'),"application/octet-stream");
            return ok(QByteArray(188,'G'),"video/mp2t");
        });
        auto proxy=prepare(spec(QStringLiteral("/file"),AddonStreamProxy::Format::Hls),fixture(server));
        QVERIFY(proxy); const auto stop=qScopeGuard([&] { proxy->cancel(); proxy->waitStopped(3000); }); QVERIFY(start(proxy));
        const auto result=get(proxy->uri(),{{"Range","bytes=0-"}}); QCOMPARE(result.status,200); QVERIFY(result.body.startsWith("#EXTM3U"));
        QVERIFY(!result.body.contains("cdn.example")); QVERIFY(!result.body.contains("secret=synthetic"));
        const QRegularExpression urls(QStringLiteral("http://127\\.0\\.0\\.1:[0-9]+/[A-Za-z0-9_-]+/[A-Za-z0-9_-]+"));
        const auto text=QString::fromUtf8(result.body); auto matches=urls.globalMatch(text); QStringList values; while(matches.hasNext()) values.append(matches.next().captured());
        QCOMPARE(values.size(),2); QCOMPARE(get(values[0],{{"Range","bytes=0-"}}).body,QByteArray(16,'k')); QCOMPARE(get(values[1]).body,QByteArray(188,'G'));
        QCOMPARE(proxy->statistics().value("resources").toUInt(),3u);
    }
    void failedManifestRegistrationDoesNotLeaveRoutes() {
        const QByteArray playlist("#EXTM3U\n#EXT-X-TARGETDURATION:1\n#EXTINF:1,\nhttps://media.example:8443/forbidden.ts\n");
        Server server(certificate,key,[&](const auto&) { return ok(playlist,"application/vnd.apple.mpegurl"); });
        auto proxy=prepare(spec(),fixture(server)); QVERIFY(proxy); const auto stop=qScopeGuard([&] { proxy->cancel(); proxy->waitStopped(3000); }); QVERIFY(start(proxy));
        QCOMPARE(get(proxy->uri()).status,502); QCOMPARE(proxy->statistics().value("resources").toUInt(),1u); QCOMPARE(proxy->statistics().value("origins").toUInt(),1u);
    }
    void savedAuthorityIsCheckedOffTheNetworkThreadForEveryRequest() {
        Server server(certificate,key,[](const auto&) { return ok("ID3authorized body"); });
        auto source=spec(); std::atomic_bool allowed{true},wrongThread{false}; std::atomic_uint calls{0}; const auto owner=std::this_thread::get_id();
        std::mutex mutex; std::thread::id networkThread;
        source.authorize=[&] { ++calls; std::lock_guard<std::mutex> lock(mutex);
            if (std::this_thread::get_id()==owner || std::this_thread::get_id()==networkThread) wrongThread=true;
            return allowed.load(); };
        auto testing=fixture(server); const auto dial=testing.connect; testing.connect=[&](auto* socket,const auto& plan) {
            { std::lock_guard<std::mutex> lock(mutex); networkThread=std::this_thread::get_id(); } dial(socket,plan);
        };
        auto proxy=prepare(source,testing); QVERIFY(proxy); const auto stop=qScopeGuard([&] { proxy->cancel(); proxy->waitStopped(3000); }); QVERIFY(start(proxy));
        QCOMPARE(get(proxy->uri()).status,200); const auto before=calls.load(); QVERIFY(before>=2); allowed=false;
        QVERIFY(get(proxy->uri()).error!=QNetworkReply::NoError); QCOMPARE(server.state->requests.load(),1u);
        QVERIFY(!proxy->activeFlag()->load()); QVERIFY(calls.load()>before); QVERIFY(!wrongThread.load());
    }
    void failedFullAdmissionRevokesOtherClientsWhileTheLiveTokenStaysOpen() {
        Server server(certificate,key,[](const auto&) { return Reply{"HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nContent-Length: 1000000\r\n\r\nID3begin",true}; });
        auto source=spec(); std::atomic_bool allowed{true}; source.authorize=[&] { return allowed.load(); };
        auto proxy=prepare(source,fixture(server)); QVERIFY(proxy); const auto cleanup=qScopeGuard([&] { proxy->cancel(); proxy->waitStopped(3000); });
        std::atomic_uint canceled{0}; QVERIFY(proxy->onCancelled([&] { ++canceled; })); QVERIFY(start(proxy));
        QTcpSocket first; first.connectToHost(QHostAddress::LocalHost,quint16(proxy->port())); QVERIFY(first.waitForConnected(2000));
        first.write(QByteArray("GET ")+QUrl(proxy->uri()).path().toLatin1()+" HTTP/1.1\r\nHost: 127.0.0.1:"+QByteArray::number(proxy->port())+"\r\n\r\n");
        QVERIFY(first.waitForBytesWritten(2000)); QVERIFY(first.waitForReadyRead(3000)); first.readAll();
        allowed=false; QVERIFY(get(proxy->uri()).error!=QNetworkReply::NoError);
        while(first.waitForReadyRead(1500)) first.readAll();
        QCOMPARE(first.state(),QAbstractSocket::UnconnectedState); QVERIFY(!proxy->activeFlag()->load());
        QCOMPARE(canceled.load(),1u); QCOMPARE(server.state->requests.load(),1u); QVERIFY(proxy->waitStopped(3000));
    }
    void backpressureKeepsApplicationBuffersBounded() {
        const QByteArray body=QByteArray("ID3")+QByteArray(16*1024*1024,'x');
        Server server(certificate,key,[&](const auto&) { return ok(body); });
        auto proxy=prepare(spec(),fixture(server)); QVERIFY(proxy); const auto stop=qScopeGuard([&] { proxy->cancel(); proxy->waitStopped(3000); }); QVERIFY(start(proxy));
        QTcpSocket socket; socket.setReadBufferSize(1); socket.connectToHost(QHostAddress::LocalHost,quint16(proxy->port())); QVERIFY(socket.waitForConnected(2000));
        socket.write(QByteArray("GET ")+QUrl(proxy->uri()).path().toLatin1()+" HTTP/1.1\r\nHost: 127.0.0.1:"+QByteArray::number(proxy->port())+"\r\n\r\n");
        QVERIFY(socket.waitForBytesWritten(2000)); QTest::qWait(200);
        const auto stats=proxy->statistics(); QVERIFY(stats.value("bytes").toULongLong()>0); QVERIFY(stats.value("bytes").toULongLong()<quint64(body.size()));
        QVERIFY(stats.value("maximumBuffered").toULongLong()<512*1024); proxy->cancel(); QVERIFY(proxy->waitStopped(3000)); socket.abort();
    }
    void retiringThreadsKeepTheirGlobalPreparationSlot() {
        Server server(certificate,key,[](const auto&) { return ok("ID3body"); });
        QSemaphore blocked,release; QList<std::shared_ptr<AddonStreamProxy>> values;
        const auto cleanup=qScopeGuard([&] { release.release(); for (const auto& value:values) { value->cancel(); value->waitStopped(3000); } });
        auto first=fixture(server); first.afterReady=[&] { blocked.release(); release.acquire(); };
        values.append(prepare(spec(),first)); QVERIFY(values.first()); QVERIFY(blocked.tryAcquire(1,2000));
        for (unsigned i=1;i<8;++i) { values.append(prepare(spec(),fixture(server))); QVERIFY(values.last()); }
        QCOMPARE(AddonStreamProxy::liveSessions(),8u); values.first()->cancel(); QVERIFY(!values.first()->waitStopped(10));
        QVERIFY(!prepare(spec(),fixture(server))); QCOMPARE(AddonStreamProxy::liveSessions(),8u);
        release.release(); QVERIFY(values.first()->waitStopped(3000)); QTRY_COMPARE(AddonStreamProxy::liveSessions(),7u);
        values.append(prepare(spec(),fixture(server))); QVERIFY(values.last());
    }
    void retiringAdmissionTasksKeepTheirGlobalPreparationSlot() {
        Server server(certificate,key,[](const auto&) { return ok("ID3body"); });
        QSemaphore blocked,release; std::atomic_uint calls{0}; auto source=spec();
        source.authorize=[&] { if (++calls==2) { blocked.release(); release.acquire(); } return true; };
        QList<std::shared_ptr<AddonStreamProxy>> values; values.append(prepare(source,fixture(server))); QVERIFY(values.first());
        const auto cleanup=qScopeGuard([&] { release.release(); for(const auto& value:values) { value->cancel(); value->waitStopped(3000); } });
        QVERIFY(start(values.first())); QTcpSocket client; client.connectToHost(QHostAddress::LocalHost,quint16(values.first()->port()));
        QVERIFY(client.waitForConnected(2000)); client.write(QByteArray("GET ")+QUrl(values.first()->uri()).path().toLatin1()+
            " HTTP/1.1\r\nHost: 127.0.0.1:"+QByteArray::number(values.first()->port())+"\r\n\r\n");
        QVERIFY(client.waitForBytesWritten(2000)); QVERIFY(blocked.tryAcquire(1,3000)); values.first()->cancel();
        QVERIFY(!values.first()->waitStopped(100));
        for(unsigned i=1;i<8;++i) { values.append(prepare(spec(),fixture(server))); QVERIFY(values.last()); }
        QCOMPARE(AddonStreamProxy::liveSessions(),8u); QVERIFY(!prepare(spec(),fixture(server)));
        release.release(); QVERIFY(values.first()->waitStopped(3000)); QTRY_COMPARE(AddonStreamProxy::liveSessions(),7u);
    }
    void appShutdownWaitsForTlsAndKeepsTheGuiResponsive() {
        Server server(certificate,key,[](const auto&) { return Reply{"HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nContent-Length: 1000000\r\n\r\nID3begin",true}; });
        QSemaphore blocked,release; auto testing=fixture(server); std::atomic_bool once{false};
        testing.afterWrite=[&] { if(!once.exchange(true)) { blocked.release(); release.acquire(); } };
        auto proxy=prepare(spec(),testing); QVERIFY(proxy); const auto stop=qScopeGuard([&] { release.release(); proxy->cancel(); proxy->waitStopped(3000); });
        QVERIFY(start(proxy)); QTcpSocket client; client.connectToHost(QHostAddress::LocalHost,quint16(proxy->port())); QVERIFY(client.waitForConnected(2000));
        client.write(QByteArray("GET ")+QUrl(proxy->uri()).path().toLatin1()+" HTTP/1.1\r\nHost: 127.0.0.1:"+QByteArray::number(proxy->port())+"\r\n\r\n");
        QVERIFY(client.waitForBytesWritten(2000)); QVERIFY(blocked.tryAcquire(1,3000));
        bool returned=false; auto result=AddonStreamShutdown::Result::Drained;
        QVERIFY(lifetime->request([&](auto value) { result=value; returned=true; },20));
        QTRY_VERIFY(returned); QCOMPARE(result,AddonStreamShutdown::Result::TimedOut); QVERIFY(!lifetime->finalGuard());
        QVERIFY(!proxy->activeFlag()->load()); QCOMPARE(lifetime->handle().networkCount(),1u);
        release.release(); returned=false; QVERIFY(lifetime->request([&](auto value) { result=value; returned=true; },3000));
        QTRY_VERIFY(returned); QCOMPARE(result,AddonStreamShutdown::Result::Drained); QVERIFY(proxy->waitStopped(0));
    }
    void appShutdownJoinsTheFullRequestAuthorization() {
        Server server(certificate,key,[](const auto&) { return ok("ID3body"); }); QSemaphore blocked,release;
        auto source=spec(); std::atomic_uint calls{0}; source.authorize=[&] { if(++calls==2) { blocked.release(); release.acquire(); } return true; };
        auto proxy=prepare(source,fixture(server)); QVERIFY(proxy); const auto stop=qScopeGuard([&] { release.release(); proxy->cancel(); proxy->waitStopped(3000); });
        QVERIFY(start(proxy)); QTcpSocket client; client.connectToHost(QHostAddress::LocalHost,quint16(proxy->port())); QVERIFY(client.waitForConnected(2000));
        client.write(QByteArray("GET ")+QUrl(proxy->uri()).path().toLatin1()+" HTTP/1.1\r\nHost: 127.0.0.1:"+QByteArray::number(proxy->port())+"\r\n\r\n");
        QVERIFY(client.waitForBytesWritten(2000)); QVERIFY(blocked.tryAcquire(1,3000));
        QVERIFY(!lifetime->finalGuard(20)); QVERIFY(!proxy->activeFlag()->load());
        QCOMPARE(lifetime->handle().qtWorkCount(),1u); QCOMPARE(AddonStreamProxy::liveSessions(),1u);
        release.release(); QVERIFY(lifetime->finalGuard(3000)); QVERIFY(proxy->waitStopped(3000)); QCOMPARE(server.state->requests.load(),0u);
    }
    void aHeldNativeProxyCannotRetainQtAuthorityAfterDrain() {
        struct Capture {
            std::shared_ptr<std::atomic_bool> destroyed;
            explicit Capture(std::shared_ptr<std::atomic_bool> value):destroyed(std::move(value)) {}
            ~Capture() { QSslSocket socket; destroyed->store(QCoreApplication::instance()!=nullptr); }
        };
        const auto destroyed=std::make_shared<std::atomic_bool>(false); auto capture=std::make_shared<Capture>(destroyed);
        Server server(certificate,key,[](const auto&) { return ok("ID3body"); }); auto source=spec();
        source.live=[capture] { return true; }; source.authorize=[capture] { return true; }; capture.reset();
        auto proxy=prepare(std::move(source),fixture(server)); QVERIFY(proxy);
        // A moved-from std::function can keep its callable (libc++).
        source.live={}; source.authorize={};
        QVERIFY(lifetime->finalGuard(3000)); QVERIFY(proxy->waitStopped(0)); QVERIFY(destroyed->load());
        // The external native lease can still hold this stopped proxy.
        QVERIFY(proxy); proxy->cancel();
        const auto lateDestroyed=std::make_shared<std::atomic_bool>(false);
        auto late=std::make_shared<Capture>(lateDestroyed); bool rejected=false;
        proxy->start([] { return true; },[late,&rejected](bool valid) { rejected=!valid; });
        late.reset(); QVERIFY(rejected); QVERIFY(lateDestroyed->load());
    }
    void nativeDnsRetiresAfterTheActualAppDestructor() {
        QProcess child; child.start(QCoreApplication::applicationFilePath(),{QStringLiteral("--native-dns-after-app")});
        QVERIFY(child.waitForFinished(10000)); QCOMPARE(child.exitStatus(),QProcess::NormalExit); QCOMPARE(child.exitCode(),0);
    }
    void staticDashMapsTheWholeFileAndInitializationRange() {
        const QByteArray xml("<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\" profiles=\"urn:mpeg:dash:profile:isoff-on-demand:2011\" minBufferTime=\"PT1S\" type=\"static\"><Period><AdaptationSet><Representation id=\"one\" bandwidth=\"1000\"><BaseURL>https://cdn.example/media.mp4?signature=synthetic</BaseURL><SegmentBase indexRange=\"0-7\"><Initialization range=\"8-15\"/></SegmentBase></Representation></AdaptationSet></Period></MPD>");
        Server server(certificate,key,[&](const QByteArray& request) {
            if (request.startsWith("GET /file ")) return ok(xml,"application/dash+xml");
            if (request.contains("Range: bytes=8-15\r\n")) return Reply{"HTTP/1.1 206 Partial\r\nContent-Type: video/mp4\r\nContent-Range: bytes 8-15/32\r\nContent-Length: 8\r\n\r\nINITDATA"};
            return Reply{"HTTP/1.1 206 Partial\r\nContent-Type: video/mp4\r\nContent-Range: bytes 0-7/32\r\nContent-Length: 8\r\n\r\nINDEXDAT"};
        });
        auto proxy=prepare(spec(QStringLiteral("/file"),AddonStreamProxy::Format::Dash),fixture(server)); QVERIFY(proxy);
        const auto stop=qScopeGuard([&] { proxy->cancel(); proxy->waitStopped(3000); }); QVERIFY(start(proxy));
        const auto result=get(proxy->uri(),{{"Range","bytes=0-"}}); QCOMPARE(result.status,200); QVERIFY(!result.body.contains("cdn.example")); QVERIFY(!result.body.contains("signature=synthetic"));
        const QRegularExpression pattern(QStringLiteral("http://127\\.0\\.0\\.1:[0-9]+/[A-Za-z0-9_-]+/[A-Za-z0-9_-]+"));
        auto matches=pattern.globalMatch(QString::fromUtf8(result.body)); QStringList paths; while(matches.hasNext()) paths.append(matches.next().captured()); QCOMPARE(paths.size(),2);
        QCOMPARE(get(paths[0],{{"Range","bytes=0-7"}}).body,QByteArray("INDEXDAT"));
        QCOMPARE(get(paths[1],{{"Range","bytes=8-15"}}).body,QByteArray("INITDATA"));
        const auto requests=server.state->requests.load(); QCOMPARE(get(paths[1],{{"Range","bytes=0-7"}}).status,416); QCOMPARE(server.state->requests.load(),requests);
    }
    void cancellationHookRunsOnceAndSupportsNativeReentry() {
        Server server(certificate,key,[](const auto&) { return ok("ID3body"); }); auto source=spec(); std::atomic_bool live{true}; source.live=[&] { return live.load(); };
        auto proxy=prepare(source,fixture(server)); QVERIFY(proxy); std::atomic_uint canceled{0};
        QVERIFY(proxy->onCancelled([weak=std::weak_ptr<AddonStreamProxy>(proxy),&canceled] { ++canceled; if (const auto owner=weak.lock()) owner->cancel(); }));
        QVERIFY(start(proxy)); live=false; QVERIFY(proxy->waitStopped(3000)); QCOMPARE(canceled.load(),1u); proxy->cancel(); QCOMPARE(canceled.load(),1u);
    }
    void nativeCancelWakesTheClientWhileTheNetworkLoopIsBlocked() {
        Server server(certificate,key,[](const auto&) { return Reply{"HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nContent-Length: 1000000\r\n\r\nID3begin",true}; });
        QSemaphore blocked,release,done; auto testing=fixture(server); std::atomic_bool once{false};
        testing.afterWrite=[&] { if (!once.exchange(true)) { blocked.release(); release.acquire(); } };
        auto proxy=prepare(spec(),testing); QVERIFY(proxy); const auto cleanup=qScopeGuard([&] { release.release(); proxy->cancel(); proxy->waitStopped(3000); }); QVERIFY(start(proxy));
        std::atomic_bool woke{false}; const auto port=proxy->port(); const auto uri=QUrl(proxy->uri()).path().toLatin1();
        std::thread input([&] { QTcpSocket socket; socket.connectToHost(QHostAddress::LocalHost,quint16(port)); if (socket.waitForConnected(2000)) {
            socket.write(QByteArray("GET ")+uri+" HTTP/1.1\r\nHost: 127.0.0.1:"+QByteArray::number(port)+"\r\n\r\n"); socket.waitForBytesWritten(2000);
            while(socket.waitForReadyRead(5000)) socket.readAll();
            woke=socket.state()==QAbstractSocket::UnconnectedState;
        } done.release(); });
        const auto join=qScopeGuard([&] { release.release(); if(input.joinable()) input.join(); });
        QVERIFY(blocked.tryAcquire(1,5000)); proxy->cancel(); QVERIFY(done.tryAcquire(1,1500)); QVERIFY(woke.load());
        release.release(); input.join(); QVERIFY(proxy->waitStopped(3000));
    }
#ifdef VLC_ADDON_NATIVE_STREAM_TESTS
    void nativeHelperReplacesCallerAuthorityWithTheCapturedAdmission() {
        Server server(certificate,key,[](const auto&) { return ok(wave(),"audio/wav"); });
        NativePlayback playback; QVERIFY(playback.init(spec(),fixture(server))); playback.source->saved=false;
        QVERIFY(playback.play()); QTRY_VERIFY_WITH_TIMEOUT(playback.errors.load()>0,5000);
        QCOMPARE(playback.source->admitted.load(),1u); QCOMPARE(playback.decoded.load(),0u); QCOMPARE(server.state->requests.load(),0u);
        QTRY_COMPARE_WITH_TIMEOUT(AddonStreamProxy::liveSessions(),0u,3000);
    }
    void corePlaybackDecodesPinnedTlsWaveAndReplaysTheSameStableItem() {
        const auto bytes=wave(); Server server(certificate,key,[bytes](const auto&) { return ok(bytes,"audio/wav"); });
        NativePlayback playback; QVERIFY(playback.init(spec(),fixture(server))); QVERIFY(playback.play());
        QTRY_VERIFY_WITH_TIMEOUT(playback.decoded.load()>0,8000); QCOMPARE(playback.errors.load(),0u);
        const auto stopped=playback.stopped.load(); playback.stop(); QTRY_VERIFY_WITH_TIMEOUT(playback.stopped.load()>stopped,5000);
        char* uri=input_item_GetURI(playback.item); QVERIFY(uri); QCOMPARE(QString::fromUtf8(uri),playback.uri); free(uri);
        auto* policy=vlc_input_policy_HoldItem(playback.item); QVERIFY(vlc_input_policy_IsSource(policy,playback.uri.toUtf8().constData()));
        QVERIFY(!vlc_input_policy_IsRevoked(policy)); vlc_input_policy_Release(policy);
        QVERIFY(playback.play()); QTRY_VERIFY_WITH_TIMEOUT(playback.decoded.load()>0,8000); QCOMPARE(playback.source->admitted.load(),2u);
        QVERIFY(server.state->requests.load()>=2u); QCOMPARE(playback.errors.load(),0u);
    }
    void corePlaybackDecodesAnHlsSegmentThroughOpaqueRoutes() {
        const auto tool=QStandardPaths::findExecutable(QStringLiteral("ffmpeg")); QVERIFY(!tool.isEmpty());
        const auto path=files.filePath(QStringLiteral("native-segment.ts")); QProcess process;
        process.start(tool,{QStringLiteral("-nostdin"),QStringLiteral("-v"),QStringLiteral("error"),QStringLiteral("-y"),
            QStringLiteral("-f"),QStringLiteral("lavfi"),QStringLiteral("-i"),QStringLiteral("anullsrc=r=8000:cl=mono"),
            QStringLiteral("-t"),QStringLiteral("4"),QStringLiteral("-c:a"),QStringLiteral("aac"),QStringLiteral("-b:a"),QStringLiteral("32k"),
            QStringLiteral("-f"),QStringLiteral("mpegts"),path});
        QVERIFY(process.waitForFinished(10000)); QCOMPARE(process.exitCode(),0); QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto segment=file.readAll(); QVERIFY(!segment.isEmpty());
        const QByteArray playlist("#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:4\n#EXT-X-MEDIA-SEQUENCE:0\n#EXTINF:4,\nsegment.ts\n#EXT-X-ENDLIST\n");
        Server server(certificate,key,[playlist,segment](const QByteArray& request) {
            return request.startsWith("GET /file ") ? ok(playlist,"application/vnd.apple.mpegurl") : ok(segment,"video/mp2t"); });
        NativePlayback playback; QVERIFY(playback.init(spec(QStringLiteral("/file"),AddonStreamProxy::Format::Hls),fixture(server)));
        QVERIFY(playback.play()); QTRY_VERIFY_WITH_TIMEOUT(playback.decoded.load()>0,10000); QCOMPARE(playback.errors.load(),0u);
        QVERIFY(server.state->requests.load()>=2u);
    }
    void coreTeardownCancelsAReadWithoutNetworkOrGuiDelivery() {
        const auto bytes=wave(); Server server(certificate,key,[bytes](const auto&) {
            return Reply{QByteArray("HTTP/1.1 200 OK\r\nContent-Type: audio/wav\r\nContent-Length: 1000000\r\n\r\n")+bytes,true}; });
        QSemaphore blocked,release,deleted; std::atomic_bool once{false},waiting{false}; auto testing=fixture(server);
        testing.afterWrite=[&] { if (!once.exchange(true)) { waiting=true; blocked.release(); release.acquire(); } };
        NativePlayback playback; QVERIFY(playback.init(spec(),testing));
        std::thread cleanup; const auto finish=qScopeGuard([&] { release.release(); if(cleanup.joinable()) cleanup.join(); });
        QVERIFY(playback.play()); QTRY_COMPARE_WITH_TIMEOUT(playback.source->admitted.load(),1u,3000);
        QTRY_VERIFY_WITH_TIMEOUT(waiting.load(),3000); QVERIFY(blocked.tryAcquire());
        auto* player=std::exchange(playback.player,nullptr); auto* listener=std::exchange(playback.listener,nullptr);
        cleanup=std::thread([player,listener,&deleted] {
            vlc_player_Lock(player); vlc_player_Stop(player); vlc_player_RemoveListener(player,listener); vlc_player_Unlock(player);
            vlc_player_Delete(player); deleted.release();
        });
        QVERIFY(deleted.tryAcquire(1,2000)); cleanup.join();
        QCOMPARE(AddonStreamProxy::liveSessions(),1u); release.release();
        QTRY_COMPARE_WITH_TIMEOUT(AddonStreamProxy::liveSessions(),0u,3000);
    }
#endif
};
int main(int argc,char** argv)
{
    if(argc==2 && std::string(argv[1])=="--native-dns-after-app") return dnsAfterAppChild(argc,argv);
    QCoreApplication app(argc,argv); TestAddonStreamProxy test; return QTest::qExec(&test,argc,argv);
}
#include "test_addon_stream_proxy.moc"
