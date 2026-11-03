// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_app_shutdown.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QPointer>
#include <QProcess>
#include <QScopeGuard>
#include <QTest>
#include <QThreadStorage>
#include <QTimer>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

using namespace vlc::addons;
namespace {
using Shutdown=AddonAppShutdown;
struct Wait {
    std::mutex mutex;
    std::condition_variable changed;
    bool released=false;
    std::atomic_bool entered{false},ended{false},tlsEnded{false};
    void wait() { std::unique_lock<std::mutex> lock(mutex); entered=true; changed.wait(lock,[&] { return released; }); ended=true; }
    void release() { { std::lock_guard<std::mutex> lock(mutex); released=true; } changed.notify_all(); }
};
struct QtTail {
    std::shared_ptr<Wait> wait;
    ~QtTail() { wait->tlsEnded=true; }
};
int child(int argc,char** argv,const std::string& mode)
{
    QCoreApplication app(argc,argv);
    const auto wait=std::make_shared<Wait>();
    const auto tls=std::make_shared<QThreadStorage<QtTail*>>();
    bool retired=false,joined=false,wrongOrder=false,timedOut=false;
    unsigned heartbeat=0,users=0,stops=0;
    Shutdown* owner=nullptr;
    Shutdown shutdown(app,{
        [&] { retired=true; if((mode=="user" || mode=="direct") && stops!=1) wrongOrder=true; },
        [&] { wrongOrder=wrongOrder || !retired || !wait->tlsEnded.load(); joined=true; },
        [&] { timedOut=true; },
        [&] { ++stops; wrongOrder=wrongOrder || !owner || owner->handle().current(); }
    },{20,10});
    owner=&shutdown;
    if(!shutdown.handle().startQtWork([wait,tls] { tls->setLocalData(new QtTail{wait}); wait->wait(); },[] {})) return 2;
    QTimer beat; beat.setInterval(5); QObject::connect(&beat,&QTimer::timeout,&app,[&] { ++heartbeat; }); beat.start();
    QTimer::singleShot(100,&app,[wait] { wait->release(); });
    if(mode=="startup") {
        shutdown.requestExit(7);
        if(shutdown.canDestroyApp()) return 3;
        const int code=app.exec();
        return code==7 && shutdown.canDestroyApp() && retired && joined && !wrongOrder && stops==0 && timedOut && heartbeat>0 ? 0 : 4;
    }
    if(mode=="direct") {
        shutdown.setUserQuit([&] { ++users; shutdown.requestExit(); });
        QTimer::singleShot(0,&app,[&] { app.exit(); });
        app.exec();
        if(shutdown.canDestroyApp()) return 5;
        shutdown.recoverEventLoopExit();
        app.exec();
        return shutdown.canDestroyApp() && users==1 && stops==1 && retired && joined && !wrongOrder && timedOut && heartbeat>0 ? 0 : 6;
    }
    if(mode=="user") {
        shutdown.setUserQuit([&] { ++users; shutdown.requestExit(); });
        shutdown.requestUserQuit();
        if(stops!=1 || users!=0 || joined) return 9;
        app.exec();
        return shutdown.canDestroyApp() && users==1 && stops==1 && retired && joined && !wrongOrder && timedOut && heartbeat>0 ? 0 : 10;
    }
    if(mode=="native") {
        const auto complete=std::make_shared<Wait>();
        std::atomic_bool cleaned{false},coreReturned{false};
        std::thread core([&] {
            QMetaObject::invokeMethod(&app,[&] {
                shutdown.requestNativeClose([&] { cleaned=true; complete->release(); });
            },Qt::QueuedConnection);
            complete->wait(); coreReturned=true;
            QMetaObject::invokeMethod(&app,[&] { shutdown.requestExit(); },Qt::QueuedConnection);
        });
        app.exec(); core.join();
        return shutdown.canDestroyApp() && coreReturned.load() && cleaned.load() && retired && joined && !wrongOrder && stops==0 && timedOut && heartbeat>0 ? 0 : 7;
    }
    return 8;
}
}
class TestAddonAppShutdown : public QObject
{
    Q_OBJECT
private slots:
    void cleanup() {
        QTRY_COMPARE_WITH_TIMEOUT(AddonStreamLifetime::processQtWorkCount(),0u,5000);
        QTRY_COMPARE_WITH_TIMEOUT(AddonStreamLifetime::processNetworkCount(),0u,5000);
    }
    void actualAppExit_data() {
        QTest::addColumn<QString>("mode");
        QTest::newRow("startup-failure")<<QStringLiteral("startup");
        QTest::newRow("direct-exit-recovery")<<QStringLiteral("direct");
        QTest::newRow("native-core-close")<<QStringLiteral("native");
        QTest::newRow("user-stop-before-drain")<<QStringLiteral("user");
    }
    void actualAppExit() {
        QFETCH(QString,mode); QProcess process;
        process.start(QCoreApplication::applicationFilePath(),{QStringLiteral("--shutdown-child"),mode});
        QVERIFY(process.waitForFinished(10000)); QCOMPARE(process.exitStatus(),QProcess::NormalExit);
        QCOMPARE(process.exitCode(),0);
    }
    void hooksAndReadyActionsHaveOneOrderAcrossTimeouts() {
        const auto wait=std::make_shared<Wait>(); const auto release=qScopeGuard([wait] { wait->release(); });
        QStringList order; unsigned timeouts=0; Shutdown shutdown(*QCoreApplication::instance(),{
            [&] { order.append(QStringLiteral("retire")); },[&] { order.append(QStringLiteral("joined")); },[&] { ++timeouts; }
        },{20,10});
        QVERIFY(shutdown.handle().startQtWork([wait] { wait->wait(); },[] {})); QTRY_VERIFY(wait->entered.load());
        QVERIFY(shutdown.requestRetirement([&] { order.append(QStringLiteral("ready")); }));
        QVERIFY(!shutdown.handle().current()); QTRY_COMPARE(timeouts,1u); QCOMPARE(order,QStringList{QStringLiteral("retire")});
        QTest::qWait(50); QCOMPARE(timeouts,1u); QVERIFY(!shutdown.joined());
        wait->release(); QTRY_VERIFY(shutdown.joined());
        QCOMPARE(order,(QStringList{QStringLiteral("retire"),QStringLiteral("joined"),QStringLiteral("ready")}));
    }
    void retiringHookReentryCannotJoinBeforeItsTail() {
        Shutdown* owner=nullptr; bool tail=false; QStringList order;
        Shutdown shutdown(*QCoreApplication::instance(),{
            [&] {
                order.append(QStringLiteral("retire"));
                QVERIFY(owner->requestRetirement([&] { QVERIFY(tail); order.append(QStringLiteral("nested")); }));
                owner->requestUserQuit();
                tail=true; order.append(QStringLiteral("tail"));
            },[&] { QVERIFY(tail); order.append(QStringLiteral("joined")); },[] {}
        },{20,10});
        owner=&shutdown;
        QVERIFY(shutdown.requestRetirement([&] { QVERIFY(tail); order.append(QStringLiteral("ready")); }));
        QCOMPARE(order,(QStringList{QStringLiteral("retire"),QStringLiteral("tail"),QStringLiteral("joined"),QStringLiteral("ready"),QStringLiteral("nested")}));
    }
    void applicationRegistrationEndsOnlyAfterTheJoinedBarrier() {
        const auto wait=std::make_shared<Wait>(); const auto release=qScopeGuard([wait] { wait->release(); });
        Shutdown shutdown(*QCoreApplication::instance(),{}, {20,10}); const auto handle=shutdown.handle();
        QVERIFY(AddonStreamLifetime::registerApplication(handle));
        const auto cleanup=qScopeGuard([handle] { AddonStreamLifetime::unregisterApplication(handle); });
        QVERIFY(shutdown.handle().startQtWork([wait] { wait->wait(); },[] {})); QTRY_VERIFY(wait->entered.load());
        QVERIFY(shutdown.requestRetirement([] {})); QVERIFY(!AddonStreamLifetime::unregisterApplication(handle));
        QVERIFY(!AddonStreamLifetime::applicationHandle().current());
        wait->release(); QTRY_VERIFY(shutdown.joined()); QVERIFY(AddonStreamLifetime::unregisterApplication(handle));
        QVERIFY(!AddonStreamLifetime::applicationHandle().current());
    }
    void closedOwnerAndQueuedPayloadsAreDestroyedAfterTheJoin() {
        const auto wait=std::make_shared<Wait>(); const auto release=qScopeGuard([wait] { wait->release(); });
        auto* receiver=new QObject; bool destroyed=false,ready=false;
        const auto payload=std::make_shared<std::atomic_bool>(false);
        QMetaObject::invokeMethod(receiver,[payload] { payload->store(true); },Qt::QueuedConnection);
        connect(receiver,&QObject::destroyed,this,[&] { destroyed=true; });
        Shutdown shutdown(*QCoreApplication::instance(),{
            [] {},[&] { QVERIFY(wait->ended.load()); receiver->deleteLater(); },[] {}
        },{20,10});
        QVERIFY(shutdown.handle().startQtWork([wait] { wait->wait(); },[] {})); QTRY_VERIFY(wait->entered.load());
        QVERIFY(shutdown.requestRetirement([&] { QVERIFY(destroyed); QVERIFY(payload->load()); ready=true; }));
        QTest::qWait(40); QVERIFY(!destroyed); QVERIFY(!ready);
        wait->release(); QTRY_VERIFY(ready); QVERIFY(destroyed);
    }
    void pendingCallbacksAreBoundedAndRunOnce() {
        const auto wait=std::make_shared<Wait>(); const auto release=qScopeGuard([wait] { wait->release(); });
        Shutdown shutdown(*QCoreApplication::instance(),{}, {20,10}); unsigned ready=0,native=0;
        QVERIFY(shutdown.handle().startQtWork([wait] { wait->wait(); },[] {})); QTRY_VERIFY(wait->entered.load());
        for(unsigned n=0;n<16;++n) QVERIFY(shutdown.requestRetirement([&] { ++ready; }));
        QVERIFY(!shutdown.requestRetirement([&] { ++ready; }));
        QVERIFY(shutdown.requestNativeClose([&] { ++native; }));
        wait->release(); QTRY_COMPARE(ready,16u); QVERIFY(shutdown.joined());
        QCOMPARE(native,1u);
        QVERIFY(shutdown.requestRetirement([&] { ++ready; })); QCOMPARE(ready,17u);
    }
    void userStopIsImmediateOnceWhileJoinedWorkAndCoreQuitWait() {
        const auto wait=std::make_shared<Wait>(); const auto release=qScopeGuard([wait] { wait->release(); });
        unsigned stops=0,retired=0,users=0,heartbeat=0; QStringList order; Shutdown* owner=nullptr;
        Shutdown shutdown(*QCoreApplication::instance(),{
            [&] { QCOMPARE(stops,1u); ++retired; order.append(QStringLiteral("retire")); },
            [&] { QVERIFY(wait->ended.load()); order.append(QStringLiteral("joined")); },[] {},
            [&] { ++stops; QVERIFY(!owner->handle().current()); order.append(QStringLiteral("stop")); }
        },{20,10}); owner=&shutdown;
        QVERIFY(shutdown.handle().startQtWork([wait] { wait->wait(); },[] {})); QTRY_VERIFY(wait->entered.load());
        QVERIFY(shutdown.setUserQuit([&] { QVERIFY(wait->ended.load()); QVERIFY(shutdown.joined()); ++users; order.append(QStringLiteral("core")); }));
        QTimer beat; beat.setInterval(5); connect(&beat,&QTimer::timeout,this,[&] { ++heartbeat; });beat.start();
        shutdown.requestUserQuit(); QCOMPARE(stops,1u); QCOMPARE(retired,1u); QCOMPARE(users,0u); QVERIFY(!shutdown.joined());
        shutdown.requestUserQuit(); QTest::qWait(60); QCOMPARE(stops,1u); QCOMPARE(users,0u); QVERIFY(heartbeat>0);
        QCOMPARE(order,(QStringList{QStringLiteral("stop"),QStringLiteral("retire")}));
        wait->release(); QTRY_COMPARE(users,1u); QCOMPARE(order,(QStringList{QStringLiteral("stop"),QStringLiteral("retire"),QStringLiteral("joined"),QStringLiteral("core")}));
        shutdown.requestUserQuit();QCoreApplication::processEvents();QCOMPARE(stops,1u);QCOMPARE(users,1u);
    }
    void interfaceRetirementAndNativeCloseDoNotRequestUserStop() {
        for(bool native:{false,true}) {
            unsigned stops=0,ready=0;
            Shutdown shutdown(*QCoreApplication::instance(),{[] {},[] {},[] {},[&] { ++stops; }},{20,10});
            if(native) QVERIFY(shutdown.requestNativeClose([&] { ++ready; }));
            else QVERIFY(shutdown.requestRetirement([&] { ++ready; }));
            QCOMPARE(ready,1u);QCOMPARE(stops,0u);QVERIFY(shutdown.joined());
        }
    }
    void earlyUserStopCanDeleteTheCoordinator() {
        auto* shutdown=new Shutdown(*QCoreApplication::instance());QPointer<Shutdown> owner(shutdown);unsigned retired=0;
        QVERIFY(shutdown->setHooks({[&] { ++retired; },[] {},[] {},[&] { delete shutdown; }}));
        shutdown->requestUserQuit();QVERIFY(owner.isNull());QCOMPARE(retired,0u);
    }
    void earlyUserStopReentryCannotPassItsTail() {
        Shutdown* owner=nullptr;bool tail=false;unsigned stops=0,users=0;QStringList order;
        Shutdown shutdown(*QCoreApplication::instance(),{
            [&] { QVERIFY(tail);order.append(QStringLiteral("retire")); },
            [&] { QVERIFY(tail);order.append(QStringLiteral("joined")); },[] {},
            [&] {
                ++stops;order.append(QStringLiteral("stop"));owner->requestUserQuit();
                QVERIFY(!owner->setHooks({}));
                QVERIFY(owner->requestRetirement([&] { QVERIFY(tail);order.append(QStringLiteral("ready")); }));
                QVERIFY(owner->requestNativeClose([&] { QVERIFY(tail);order.append(QStringLiteral("native")); }));
                QCoreApplication::processEvents();QVERIFY(!owner->joined());QCOMPARE(users,0u);
                tail=true;order.append(QStringLiteral("tail"));
            }
        },{20,10});owner=&shutdown;
        QVERIFY(shutdown.setUserQuit([&] { QVERIFY(tail);++users;order.append(QStringLiteral("core")); }));
        shutdown.requestUserQuit();QCOMPARE(stops,1u);QCOMPARE(users,0u);QTRY_COMPARE(users,1u);
        QCOMPARE(order,(QStringList{QStringLiteral("stop"),QStringLiteral("tail"),QStringLiteral("retire"),QStringLiteral("joined"),QStringLiteral("ready"),QStringLiteral("native"),QStringLiteral("core")}));
    }
    void alreadyJoinedCallbacksAlsoWaitForTheEarlyStopTail() {
        Shutdown* owner=nullptr;bool tail=false;unsigned users=0,ready=0,native=0,stops=0;
        Shutdown shutdown(*QCoreApplication::instance(),{[] {},[] {},[] {},[&] {
            ++stops;owner->requestUserQuit();
            QVERIFY(owner->requestRetirement([&] { QVERIFY(tail);++ready; }));
            QVERIFY(owner->requestNativeClose([&] { QVERIFY(tail);++native; }));
            QCoreApplication::processEvents();QCOMPARE(ready,0u);QCOMPARE(native,0u);QCOMPARE(users,0u);tail=true;
        }},{20,10});owner=&shutdown;
        QVERIFY(shutdown.requestRetirement([] {}));QVERIFY(shutdown.joined());QCOMPARE(stops,0u);
        QVERIFY(shutdown.setUserQuit([&] { QVERIFY(tail);++users; }));shutdown.requestUserQuit();
        QCOMPARE(stops,1u);QCOMPARE(ready,1u);QCOMPARE(native,1u);QTRY_COMPARE(users,1u);
    }
    void readyReentryCannotOvertakeItsBatchWithNativeCleanup() {
        const auto wait=std::make_shared<Wait>();const auto release=qScopeGuard([wait] { wait->release(); });
        Shutdown* owner=nullptr;QStringList order;unsigned users=0;
        Shutdown shutdown(*QCoreApplication::instance(),{[] {},[] {},[] {},[&] {
            order.append(QStringLiteral("stop"));
            QVERIFY(owner->requestRetirement([&] { order.append(QStringLiteral("nested-ready")); }));
        }},{20,10});owner=&shutdown;
        QVERIFY(shutdown.handle().startQtWork([wait] { wait->wait(); },[] {}));QTRY_VERIFY(wait->entered.load());
        QVERIFY(shutdown.setUserQuit([&] { ++users;order.append(QStringLiteral("core")); }));
        QVERIFY(shutdown.requestRetirement([&] { order.append(QStringLiteral("first"));shutdown.requestUserQuit();order.append(QStringLiteral("first-tail")); }));
        QVERIFY(shutdown.requestRetirement([&] { order.append(QStringLiteral("second")); }));
        QVERIFY(shutdown.requestNativeClose([&] { order.append(QStringLiteral("native")); }));
        wait->release();QTRY_COMPARE(users,1u);
        QCOMPARE(order,(QStringList{QStringLiteral("first"),QStringLiteral("stop"),QStringLiteral("first-tail"),QStringLiteral("second"),QStringLiteral("nested-ready"),QStringLiteral("native"),QStringLiteral("core")}));
    }
    void directReadyReentryCannotOvertakeItsTailWithNativeCleanup() {
        const auto wait=std::make_shared<Wait>();const auto release=qScopeGuard([wait] { wait->release(); });
        Shutdown shutdown(*QCoreApplication::instance(),{}, {20,10});QStringList order;bool native=false;
        QVERIFY(shutdown.handle().startQtWork([wait] { wait->wait(); },[] {}));QTRY_VERIFY(wait->entered.load());
        QVERIFY(shutdown.requestRetirement([&] {
            order.append(QStringLiteral("first"));
            QVERIFY(shutdown.requestNativeClose([&] { native=true;order.append(QStringLiteral("native")); }));
            QVERIFY(shutdown.requestRetirement([&] { order.append(QStringLiteral("nested-ready")); }));
            QCoreApplication::processEvents();QVERIFY(!native);
            QCOMPARE(order,(QStringList{QStringLiteral("first")}));order.append(QStringLiteral("first-tail"));
        }));
        QVERIFY(shutdown.requestRetirement([&] { order.append(QStringLiteral("second")); }));
        wait->release();QTRY_VERIFY(native);
        QCOMPARE(order,(QStringList{QStringLiteral("first"),QStringLiteral("first-tail"),QStringLiteral("second"),QStringLiteral("nested-ready"),QStringLiteral("native")}));
    }
    void readyCaptureDestructionCanDeleteTheCoordinator() {
        struct Drop { std::function<void()> action; ~Drop() { action(); } };
        const auto wait=std::make_shared<Wait>();const auto release=qScopeGuard([wait] { wait->release(); });
        auto* shutdown=new Shutdown(*QCoreApplication::instance());QPointer<Shutdown> owner(shutdown);
        QVERIFY(shutdown->handle().startQtWork([wait] { wait->wait(); },[] {}));QTRY_VERIFY(wait->entered.load());
        auto drop=std::make_shared<Drop>();drop->action=[shutdown] { delete shutdown; };
        QVERIFY(shutdown->requestRetirement([drop] {}));drop.reset();wait->release();QTRY_VERIFY(owner.isNull());
    }
    void earlyUserStopFailureStaysClosedAndCannotAuthorizeQuit() {
        unsigned stops=0,users=0;Shutdown shutdown(*QCoreApplication::instance(),{[] {},[] {},[] {},[&] { ++stops;throw 1; }},{20,10});
        QVERIFY(shutdown.setUserQuit([&] { ++users; }));shutdown.requestUserQuit();shutdown.requestUserQuit();
        QVERIFY(!shutdown.handle().current());QVERIFY(!shutdown.joined());QVERIFY(!shutdown.canDestroyApp());QCOMPARE(stops,1u);QCOMPARE(users,0u);
    }
    void readinessCanDestroyTheCoordinator() {
        auto* shutdown=new Shutdown(*QCoreApplication::instance()); QPointer<Shutdown> owner(shutdown);
        QVERIFY(shutdown->requestRetirement([&] { delete shutdown; })); QVERIFY(owner.isNull());
    }
    void retirementFailureCannotAuthorizeAppDestruction() {
        Shutdown shutdown(*QCoreApplication::instance(),{[] { throw 1; },[] {},[] {}},{20,10});
        shutdown.requestExit(); QVERIFY(shutdown.retiring()); QVERIFY(!shutdown.joined()); QVERIFY(!shutdown.canDestroyApp());
        bool ready=false; QVERIFY(!shutdown.requestRetirement([&] { ready=true; })); QVERIFY(!ready);
    }
};
int main(int argc,char** argv)
{
    if(argc==3 && std::string(argv[1])=="--shutdown-child") return child(argc,argv,argv[2]);
    QCoreApplication app(argc,argv); TestAddonAppShutdown test; return QTest::qExec(&test,argc,argv);
}
#include "test_addon_app_shutdown.moc"
