// SPDX-License-Identifier: GPL-2.0-or-later
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QScopeGuard>
#include <QSslSocket>
#include <QTest>
#include <QTimer>
#include <QTemporaryDir>
#include <QThreadStorage>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include "addons/addon_stream_dns.hpp"
#include "addons/addon_stream_shutdown.hpp"
#include "addons/addon_source_admissions.hpp"
#include "addons/addon_source_consent.hpp"
#ifdef Q_OS_LINUX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace vlc::addons;
namespace {
using Lifetime=AddonStreamLifetime;
using Shutdown=AddonStreamShutdown;
class Semaphore {
    std::mutex mutex;
    std::condition_variable changed;
    unsigned count=0;
public:
    void release(unsigned value=1) { { std::lock_guard<std::mutex> lock(mutex); count+=value; } changed.notify_all(); }
    void acquire() { std::unique_lock<std::mutex> lock(mutex); changed.wait(lock,[&] { return count>0; }); --count; }
};
struct Wait {
    Semaphore release,tailRelease,cancelRelease;
    std::atomic_uint entered{0},objects{0};
    std::atomic_bool tailEntered{false},tailEnded{false};
    std::atomic_bool published{false},wrongOrder{false},canceled{false},denied{false},ran{false};
    std::atomic_bool retainedReleased{false};
};
struct Tail {
    std::shared_ptr<Wait> wait;
    explicit Tail(std::shared_ptr<Wait> value):wait(std::move(value)) {}
    Tail(const Tail&)=delete;
    ~Tail() { wait->tailEntered=true; wait->tailRelease.acquire(); wait->tailEnded=true; }
};
struct Retained {
    std::shared_ptr<Wait> wait;
    explicit Retained(std::shared_ptr<Wait> value):wait(std::move(value)) {}
    ~Retained() { wait->retainedReleased=true; }
};
struct ConsentFixture {
    QTemporaryDir directory;
    std::shared_ptr<Wait> wait=std::make_shared<Wait>();
    std::shared_ptr<std::atomic_bool> block=std::make_shared<std::atomic_bool>(false);
    AddonSourceConsent::Binding binding{AddonSourceConsent::SourceKind::Git,QStringLiteral("https://example.org/source.git"),
        QStringLiteral("org.example.source/catalog"),QStringLiteral("public")};
    std::shared_ptr<AddonSourceAdmissions> admissions;
    std::shared_ptr<AddonSourceConsent> consent;
    std::optional<AddonSourceConsent::Authorization> authorization;
    QString uri;
    bool setup() {
        admissions=std::make_shared<AddonSourceAdmissions>(directory.filePath(QStringLiteral("admissions")),[](const QVariantMap&) { return true; });
        const auto records=admissions; const auto gate=wait; const auto blocked=block;
        consent=std::make_shared<AddonSourceConsent>(directory.path(),AddonSourceConsent::Checks{
            [gate,blocked](const auto&) { if(blocked->load()) { ++gate->entered; gate->release.acquire(); } return true; },
            [records](const auto&,const QString& value) { return !records->currentReference(value).isEmpty(); }});
        QStringList values;
        if(!admissions->admit({QVariantMap{{"format","hakone-addon-source-v1"},{"source",binding.contribution},{"accountReference",binding.accountReference},
            {"itemId","station"},{"snapshot",QVariantMap{{"title","Station"},{"mediaKind","audio"}}}}},&values) || values.size()!=1) return false;
        uri=values.first(); const auto ticket=consent->capture(binding); if(!ticket || !consent->grant(ticket->id)) return false;
        const auto intent=consent->mintIntent(binding,uri,1,AddonSourceConsent::IntentReason::PrimaryPlayback);
        authorization=consent->consumeIntent(intent,binding,uri,1,AddonSourceConsent::IntentReason::PrimaryPlayback);
        return bool(authorization);
    }
};
int appLifetimeChild(int argc,char** argv)
{
    struct Plain {
        std::mutex mutex; std::condition_variable changed;
        bool entered=false,release=false,ended=false;
    };
    const auto plain=std::make_shared<Plain>(); const auto objects=std::make_shared<std::atomic_uint>(0);
    Lifetime::Handle handle; bool drained=false,guard=false;
    {
        QCoreApplication app(argc,argv); Shutdown shutdown(app); handle=shutdown.handle();
        if(!handle.startPlain([plain] {
            std::unique_lock<std::mutex> lock(plain->mutex); plain->entered=true; plain->changed.notify_all();
            plain->changed.wait(lock,[&] { return plain->release; }); plain->ended=true; plain->changed.notify_all();
        })) return 1;
        {
            std::unique_lock<std::mutex> lock(plain->mutex);
            if(!plain->changed.wait_for(lock,std::chrono::seconds(2),[&] { return plain->entered; })) return 2;
        }
        const auto network=std::make_shared<std::atomic_bool>(true);
        if(!handle.startNetwork([objects,network] {
            ++*objects; { QSslSocket socket; while(network->load()) std::this_thread::sleep_for(std::chrono::milliseconds(1)); } --*objects;
        },[network] { *network=false; })) return 3;
        QTimer::singleShot(0,&shutdown,[&] { shutdown.request([&](Shutdown::Result result) { drained=result==Shutdown::Result::Drained; app.quit(); }); });
        QTimer::singleShot(3000,&app,&QCoreApplication::quit);
        app.exec(); guard=shutdown.finalGuard(1000);
        if(!drained || !guard || objects->load()!=0 || handle.plainCount()!=1) return 4;
    }
    if(objects->load()!=0 || !handle.networkDrained() || handle.current()) return 5;
    {
        std::unique_lock<std::mutex> lock(plain->mutex); plain->release=true; plain->changed.notify_all();
        if(!plain->changed.wait_for(lock,std::chrono::seconds(2),[&] { return plain->ended; })) return 6;
    }
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(Lifetime::processPlainCount() && std::chrono::steady_clock::now()<deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return Lifetime::processPlainCount() ? 7 : 0;
}
}
class TestAddonStreamShutdown : public QObject
{
    Q_OBJECT
private slots:
    void cleanup() {
        QTRY_COMPARE_WITH_TIMEOUT(Lifetime::processNetworkCount(),0u,5000);
        QTRY_COMPARE_WITH_TIMEOUT(Lifetime::processPlainCount(),0u,5000);
        QTRY_COMPARE_WITH_TIMEOUT(Lifetime::processQtWorkCount(),0u,5000);
    }
    void drainIncludesSocketAndThreadLocalDestruction() {
        Shutdown shutdown(*QCoreApplication::instance()); const auto handle=shutdown.handle(); const auto wait=std::make_shared<Wait>();
        const auto release=qScopeGuard([wait] { wait->release.release(); wait->tailRelease.release(); });
        Lifetime::NetworkTicket ticket;
        QVERIFY(handle.startNetwork([wait] {
            if(!wait->published.load()) wait->wrongOrder=true;
            thread_local std::unique_ptr<Tail> tail; tail=std::make_unique<Tail>(wait);
            ++wait->objects; { QSslSocket socket; ++wait->entered; wait->release.acquire(); } --wait->objects;
        },[wait] { wait->release.release(); },[&](auto value) {
            ticket=std::move(value); QVERIFY(ticket.retain(std::make_shared<Retained>(wait))); wait->published=true;
        }));
        QTRY_COMPARE(wait->entered.load(),1u); QVERIFY(ticket); QVERIFY(!wait->wrongOrder.load());
        int result=-1; QVERIFY(shutdown.request([&](auto value) { result=int(value); },50));
        QTRY_VERIFY(wait->tailEntered.load()); QCOMPARE(wait->objects.load(),0u);
        QTRY_COMPARE(result,int(Shutdown::Result::TimedOut)); QVERIFY(!ticket.joined()); QVERIFY(!shutdown.finalGuard(0)); QVERIFY(!wait->retainedReleased.load());
        wait->tailRelease.release(); QTRY_VERIFY(ticket.joined()); QVERIFY(wait->tailEnded.load()); QVERIFY(wait->retainedReleased.load());
        result=-1; QVERIFY(shutdown.request([&](auto value) { result=int(value); }));
        QCOMPARE(result,int(Shutdown::Result::Drained)); QVERIFY(shutdown.finalGuard(0));
    }
    void aBlockedCancelCallbackDoesNotBlockTheGui() {
        Shutdown shutdown(*QCoreApplication::instance()); const auto handle=shutdown.handle(); const auto wait=std::make_shared<Wait>();
        const auto release=qScopeGuard([wait] { wait->cancelRelease.release(); wait->release.release(); });
        QVERIFY(handle.startNetwork([wait] { ++wait->entered; wait->release.acquire(); },[wait] {
            wait->canceled=true; wait->cancelRelease.acquire(); wait->release.release();
        }));
        QTRY_COMPARE(wait->entered.load(),1u); int result=-1; unsigned heartbeats=0;
        QTimer heartbeat; heartbeat.setInterval(5); connect(&heartbeat,&QTimer::timeout,this,[&] { ++heartbeats; }); heartbeat.start();
        QVERIFY(shutdown.request([&](auto value) { result=int(value); },50)); QTRY_VERIFY(wait->canceled.load());
        QTRY_COMPARE(result,int(Shutdown::Result::TimedOut)); QVERIFY(heartbeats>0); QVERIFY(!shutdown.finalGuard(0));
        wait->cancelRelease.release(); QTRY_VERIFY(handle.networkDrained()); QVERIFY(shutdown.finalGuard(0));
    }
    void drainIncludesCleanupQtThreadStorageDestruction() {
        struct Cleanup {
            std::shared_ptr<Wait> wait;
            std::shared_ptr<QThreadStorage<Tail*>> storage;
            Cleanup(std::shared_ptr<Wait> value,std::shared_ptr<QThreadStorage<Tail*>> tls):wait(std::move(value)),storage(std::move(tls)) {}
            ~Cleanup() {
                QSslSocket socket;
                storage->setLocalData(new Tail(wait));
            }
        };
        Shutdown shutdown(*QCoreApplication::instance()); const auto handle=shutdown.handle(); const auto wait=std::make_shared<Wait>();
        const auto release=qScopeGuard([wait] { wait->release.release(); wait->tailRelease.release(); });
        const auto storage=std::make_shared<QThreadStorage<Tail*>>();
        auto cleanup=std::make_shared<Cleanup>(wait,storage); Lifetime::NetworkTicket ticket;
        QVERIFY(handle.startNetwork([wait] { ++wait->entered; wait->release.acquire(); },
            [wait,cleanup] { wait->canceled=true; wait->release.release(); },[&](auto value) { ticket=std::move(value); }));
        cleanup.reset(); QTRY_COMPARE(wait->entered.load(),1u);
        int result=-1; QVERIFY(shutdown.request([&](auto value) { result=int(value); },50));
        QTRY_VERIFY(wait->tailEntered.load()); QVERIFY(wait->canceled.load());
        QTRY_COMPARE(result,int(Shutdown::Result::TimedOut)); QVERIFY(!ticket.joined()); QVERIFY(!handle.qtDrained());
        QVERIFY(!shutdown.finalGuard(0)); wait->tailRelease.release();
        QTRY_VERIFY(ticket.joined()); QVERIFY(wait->tailEnded.load()); QVERIFY(shutdown.finalGuard(0));
    }
    void blockedQtTlsDoesNotDelayOtherNetworkCancellation() {
        Shutdown shutdown(*QCoreApplication::instance()); const auto handle=shutdown.handle(); const auto wait=std::make_shared<Wait>();
        const auto release=qScopeGuard([wait] { wait->release.release(); wait->tailRelease.release(); });
        QVERIFY(handle.startQtWork([wait] {
            thread_local std::unique_ptr<Tail> tail; tail=std::make_unique<Tail>(wait);
        },[] {}));
        QTRY_VERIFY(wait->tailEntered.load());
        QVERIFY(handle.startNetwork([wait] { ++wait->entered; wait->release.acquire(); },[wait] { wait->canceled=true; wait->release.release(); }));
        QTRY_COMPARE(wait->entered.load(),1u); int result=-1; QVERIFY(shutdown.request([&](auto value) { result=int(value); },50));
        QTRY_VERIFY(wait->canceled.load()); QTRY_VERIFY(handle.networkDrained());
        QTRY_COMPARE(result,int(Shutdown::Result::TimedOut)); QVERIFY(!handle.qtDrained()); QVERIFY(!shutdown.finalGuard(0));
        wait->tailRelease.release(); QTRY_VERIFY(handle.qtDrained()); QVERIFY(shutdown.finalGuard(0));
    }
    void actualConsentCurrentWorkMustJoinBeforeAppTeardown() {
        ConsentFixture fixture; QVERIFY(fixture.setup()); Shutdown shutdown(*QCoreApplication::instance()); const auto handle=shutdown.handle();
        const auto wait=fixture.wait; const auto release=qScopeGuard([wait] { wait->release.release(); }); fixture.block->store(true);
        const auto consent=fixture.consent; const auto authorization=*fixture.authorization; const auto uri=fixture.uri;
        QVERIFY(handle.startQtWork([consent,authorization,uri,wait] { wait->ran=consent->current(authorization,uri,1); },[] {}));
        QTRY_COMPARE(wait->entered.load(),1u); int result=-1; QVERIFY(shutdown.request([&](auto value) { result=int(value); },50));
        QTRY_COMPARE(result,int(Shutdown::Result::TimedOut)); QVERIFY(handle.networkDrained()); QVERIFY(!handle.qtDrained());
        QVERIFY(!shutdown.finalGuard(0)); wait->release.release(); QTRY_VERIFY(handle.qtDrained());
        QVERIFY(wait->ran.load()); QVERIFY(shutdown.finalGuard(0));
    }
    void anActualBlockedQtFileReadKeepsTheFinalGuardClosed() {
#ifdef Q_OS_LINUX
        ConsentFixture fixture; QVERIFY(fixture.setup()); Shutdown shutdown(*QCoreApplication::instance()); const auto handle=shutdown.handle();
        const auto path=fixture.directory.filePath(QStringLiteral("source-consent/approvals.json")); QFile saved(path); QVERIFY(saved.open(QIODevice::ReadOnly));
        const auto bytes=saved.readAll(); saved.close(); QVERIFY(!bytes.isEmpty()); QVERIFY(QFile::remove(path));
        const auto native=QFile::encodeName(path); QCOMPARE(::mkfifo(native.constData(),0600),0);
        int writer=::open(native.constData(),O_RDWR|O_NONBLOCK|O_CLOEXEC); QVERIFY(writer>=0);
        const auto closeWriter=qScopeGuard([&] { if(writer>=0) ::close(writer); });
        const auto consent=fixture.consent; const auto authorization=*fixture.authorization; const auto uri=fixture.uri; const auto wait=fixture.wait;
        QVERIFY(handle.startQtWork([consent,authorization,uri,wait] { wait->ran=consent->current(authorization,uri,1); },[] {}));
        const auto readerOpen=[&] {
            for(const auto& name:QDir(QStringLiteral("/proc/self/fd")).entryList(QDir::Files|QDir::System|QDir::NoDotAndDotDot)) {
                bool valid=false; const int fd=name.toInt(&valid); if(!valid || fd==writer) continue;
                if(QFileInfo(QStringLiteral("/proc/self/fd/")+name).symLinkTarget()==path && (::fcntl(fd,F_GETFL)&O_ACCMODE)==O_RDONLY) return true;
            }
            return false;
        };
        QTRY_VERIFY(readerOpen()); int result=-1; QVERIFY(shutdown.request([&](auto value) { result=int(value); },50));
        QTRY_COMPARE(result,int(Shutdown::Result::TimedOut)); QVERIFY(!shutdown.finalGuard(0));
        QCOMPARE(::write(writer,bytes.constData(),size_t(bytes.size())),ssize_t(bytes.size())); ::close(writer); writer=-1;
        QTRY_VERIFY(handle.qtDrained()); QVERIFY(wait->ran.load()); QVERIFY(shutdown.finalGuard(0));
#else
        QSKIP("This file-read test needs Linux FIFO descriptors.");
#endif
    }
    void cancellationCanReenterTheLifetime() {
        Lifetime owner; const auto handle=owner.handle(); const auto wait=std::make_shared<Wait>();
        const auto release=qScopeGuard([wait] { wait->release.release(); });
        QVERIFY(handle.startNetwork([wait] { ++wait->entered; wait->release.acquire(); },[handle,wait] {
            handle.close(); wait->denied=!handle.startPlain([] {}); wait->release.release();
        }));
        QTRY_COMPARE(wait->entered.load(),1u); handle.close(); QVERIFY(!handle.current());
        QTRY_VERIFY(handle.networkDrained()); QVERIFY(wait->denied.load());
    }
    void retiredPlainWorkKeepsTheProcessQuota() {
        auto owner=std::make_unique<Lifetime>(); const auto handle=owner->handle(); const auto wait=std::make_shared<Wait>();
        const auto release=qScopeGuard([wait] { wait->release.release(int(Lifetime::PlainLimit)); });
        for(unsigned n=0;n<Lifetime::PlainLimit;++n) QVERIFY(handle.startPlain([wait] { ++wait->entered; wait->release.acquire(); }));
        QTRY_COMPARE(wait->entered.load(),Lifetime::PlainLimit); owner.reset();
        QVERIFY(handle.networkDrained()); QVERIFY(!handle.current()); QCOMPARE(handle.plainCount(),Lifetime::PlainLimit);
        Lifetime replacement; const auto next=replacement.handle(); QVERIFY(!next.startPlain([] {}));
        wait->release.release(int(Lifetime::PlainLimit)); QTRY_COMPARE(Lifetime::processPlainCount(),0u);
        QVERIFY(next.startPlain([wait] { wait->ran=true; })); QTRY_VERIFY(wait->ran.load());
    }
    void retiringQtWorkKeepsItsQuotaAndBlocksFinalTeardown() {
        auto owner=std::make_unique<Shutdown>(*QCoreApplication::instance()); const auto handle=owner->handle(); const auto wait=std::make_shared<Wait>();
        const auto release=qScopeGuard([wait] { wait->release.release(Lifetime::QtWorkLimit); });
        for(unsigned n=0;n<Lifetime::QtWorkLimit;++n) QVERIFY(handle.startQtWork([wait] { ++wait->entered; wait->release.acquire(); },[] {}));
        QTRY_COMPARE(wait->entered.load(),Lifetime::QtWorkLimit); QVERIFY(!owner->finalGuard(0)); owner.reset();
        Lifetime replacement; const auto next=replacement.handle(); QVERIFY(!next.startQtWork([] {},[] {}));
        QVERIFY(handle.networkDrained()); QVERIFY(!handle.qtDrained());
        wait->release.release(Lifetime::QtWorkLimit); QTRY_VERIFY(handle.qtDrained()); QTRY_COMPARE(Lifetime::processQtWorkCount(),0u);
        QVERIFY(next.startQtWork([] {},[] {})); QTRY_VERIFY(next.qtDrained());
    }
    void plainWorkRetainsItsPermitThroughCallbackTls() {
        Lifetime owner; const auto handle=owner.handle(); const auto wait=std::make_shared<Wait>();
        const auto release=qScopeGuard([wait] { wait->tailRelease.release(); });
        QVERIFY(handle.startPlain([wait] {
            thread_local std::unique_ptr<Tail> tail; tail=std::make_unique<Tail>(wait); wait->ran=true;
        },std::make_shared<Retained>(wait)));
        QTRY_VERIFY(wait->tailEntered.load()); QVERIFY(wait->ran.load()); owner.close();
        QVERIFY(handle.networkDrained()); QCOMPARE(handle.plainCount(),1u); QCOMPARE(Lifetime::processPlainCount(),1u);
        QVERIFY(!wait->retainedReleased.load());
        wait->tailRelease.release(); QTRY_COMPARE(handle.plainCount(),0u); QTRY_COMPARE(Lifetime::processPlainCount(),0u);
        QVERIFY(wait->retainedReleased.load());
    }
    void retiringNetworkThreadsKeepTheProcessQuota() {
        auto owner=std::make_unique<Lifetime>(); const auto handle=owner->handle(); const auto wait=std::make_shared<Wait>();
        const auto release=qScopeGuard([wait] { wait->release.release(int(Lifetime::NetworkLimit)); });
        for(unsigned n=0;n<Lifetime::NetworkLimit;++n)
            QVERIFY(handle.startNetwork([wait] { ++wait->entered; wait->release.acquire(); },[] {}));
        QTRY_COMPARE(wait->entered.load(),Lifetime::NetworkLimit); owner.reset(); QVERIFY(!handle.networkDrained());
        Lifetime replacement; const auto next=replacement.handle(); QVERIFY(!next.startNetwork([] {},[] {}));
        wait->release.release(int(Lifetime::NetworkLimit)); QTRY_VERIFY(handle.networkDrained());
        QTRY_COMPARE(Lifetime::processNetworkCount(),0u); QVERIFY(next.startNetwork([] {},[] {})); QTRY_VERIFY(next.networkDrained());
    }
    void registrationCanRetireBeforeTheThreadStarts() {
        Lifetime owner; const auto handle=owner.handle(); Lifetime::NetworkTicket ticket; const auto wait=std::make_shared<Wait>();
        QVERIFY(!handle.startNetwork([wait] { wait->ran=true; },[] {},[&](auto value) { ticket=std::move(value); handle.close(); }));
        QTRY_VERIFY(ticket.joined()); QVERIFY(!wait->ran.load()); QVERIFY(handle.networkDrained());
    }
    void failedRegistrationReleasesThePermit() {
        Lifetime owner; const auto handle=owner.handle(); const auto wait=std::make_shared<Wait>();
        QVERIFY(!handle.startNetwork([wait] { wait->ran=true; },[] {},[](auto) { throw 1; }));
        QTRY_VERIFY(handle.networkDrained()); QVERIFY(!wait->ran.load());
    }
    void theFinalGuardIsBoundedAndReturnsFailure() {
        Shutdown shutdown(*QCoreApplication::instance()); const auto handle=shutdown.handle(); const auto wait=std::make_shared<Wait>();
        const auto release=qScopeGuard([wait] { wait->release.release(); });
        QVERIFY(handle.startNetwork([wait] { ++wait->entered; wait->release.acquire(); },[] {})); QTRY_COMPARE(wait->entered.load(),1u);
        QElapsedTimer elapsed; elapsed.start(); QVERIFY(!shutdown.finalGuard(20)); QVERIFY(elapsed.elapsed()<1000);
        wait->release.release(); QTRY_VERIFY(handle.networkDrained()); QVERIFY(shutdown.finalGuard(0));
    }
    void aCompletionCallbackCanDestroyTheController() {
        auto owner=std::make_unique<Shutdown>(*QCoreApplication::instance()); const auto handle=owner->handle(); bool delivered=false;
        QVERIFY(owner->request([&](auto result) { delivered=result==Shutdown::Result::Drained; owner.reset(); }));
        QVERIFY(delivered); QVERIFY(!owner); QVERIFY(!handle.current()); QVERIFY(handle.networkDrained());
    }
    void nativeDnsUsesOnlyBoundedAsciiHostNames() {
        for(const auto& value:std::vector<std::string>{"",std::string(254,'x'),"http://example.org","example.org/","../",std::string("x\0y",3)})
            QVERIFY(AddonStreamDns::resolve(value).empty());
        const auto values=AddonStreamDns::resolve("localhost"); QVERIFY(!values.empty()); QVERIFY(values.size()<=32);
        for(const auto& value:values) QVERIFY(value.family==4 || value.family==6);
    }
    void theAppCanEndWithOnlyPlainRetiringWork() {
        QProcess process; process.start(QCoreApplication::applicationFilePath(),{QStringLiteral("--app-lifetime-child")});
        QVERIFY(process.waitForFinished(10000)); QCOMPARE(process.exitStatus(),QProcess::NormalExit); QCOMPARE(process.exitCode(),0);
    }
};
int main(int argc,char** argv)
{
    if(argc>1 && std::strcmp(argv[1],"--app-lifetime-child")==0) return appLifetimeChild(argc,argv);
    QCoreApplication app(argc,argv); TestAddonStreamShutdown test; return QTest::qExec(&test,argc,argv);
}
#include "test_addon_stream_shutdown.moc"
