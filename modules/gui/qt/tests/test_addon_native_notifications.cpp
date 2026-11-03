// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_native_notifications.hpp"
#include <QScopeGuard>
#include <QTest>
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
using namespace vlc::addons;
namespace {
struct Wait {
    std::mutex mutex;std::condition_variable changed;bool released=false;
    std::atomic_bool entered{false};
    void wait(){std::unique_lock<std::mutex> lock(mutex);entered=true;changed.wait(lock,[&]{return released;});}
    void release(){{std::lock_guard<std::mutex> lock(mutex);released=true;}changed.notify_all();}
};
struct CaptureTail { std::shared_ptr<Wait> wait;~CaptureTail(){wait->wait();} };
}
class NativeNotificationsTest final:public QObject {
    Q_OBJECT
private slots:
    void emptyWakeRefusesAndQuotaHasNoImplicitPolling(){
        NativeNotifications bus;QVERIFY(!bus.subscribe({}));
        std::atomic_uint calls{0};std::array<NativeNotification,NativeNotifications::Limit> tokens;
        for(auto& token:tokens){token=bus.subscribe([&]{++calls;});QVERIFY(token);}
        QVERIFY(!bus.subscribe([]{}));QCOMPARE(calls.load(),0u);
        bus.notify();QCOMPARE(calls.load(),NativeNotifications::Limit);
        tokens[0]={};auto replacement=bus.subscribe([&]{++calls;});QVERIFY(replacement);
        bus.notify();QCOMPARE(calls.load(),2*NativeNotifications::Limit);
    }
    void captureDestructionRetainsQuotaAndRunsOutsideRegistryLock(){
        NativeNotifications bus;const auto wait=std::make_shared<Wait>();
        auto tail=std::make_shared<CaptureTail>();tail->wait=wait;
        std::array<NativeNotification,NativeNotifications::Limit> tokens;
        tokens[0]=bus.subscribe([tail]{});QVERIFY(tokens[0]);tail.reset();
        for(unsigned i=1;i<tokens.size();++i){tokens[i]=bus.subscribe([]{});QVERIFY(tokens[i]);}
        std::atomic_bool dropped{false};
        std::thread drop([&]{tokens[0]={};dropped=true;});
        auto join=qScopeGuard([&]{wait->release();drop.join();});
        QTRY_VERIFY(wait->entered.load());QVERIFY(!dropped.load());
        QVERIFY(!bus.subscribe([]{})); // callback destruction still owns quota
        bus.notify(); // proves no registry lock crosses that destruction
        wait->release();drop.join();join.dismiss();QVERIFY(dropped.load());
        QVERIFY(bus.subscribe([]{}));
    }
    void notificationRetainsReleasedSubscriptionUntilCallbackReturns(){
        NativeNotifications bus;const auto wait=std::make_shared<Wait>();
        auto token=bus.subscribe([wait]{wait->wait();});QVERIFY(token);
        std::thread notify([&]{bus.notify();});
        auto join=qScopeGuard([&]{wait->release();notify.join();});
        QTRY_VERIFY(wait->entered.load());token={};
        std::array<NativeNotification,NativeNotifications::Limit-1> rest;
        for(auto& item:rest){item=bus.subscribe([]{});QVERIFY(item);}
        QVERIFY(!bus.subscribe([]{}));wait->release();notify.join();join.dismiss();
        QVERIFY(bus.subscribe([]{}));
    }
    void callbackExceptionDoesNotSuppressOtherNativeWakes(){
        NativeNotifications bus;std::atomic_uint calls{0};
        auto throwing=bus.subscribe([]{throw 1;});auto next=bus.subscribe([&]{++calls;});
        QVERIFY(throwing);QVERIFY(next);bus.notify();QCOMPARE(calls.load(),1u);
    }
    void retainedTokenDoesNotKeepNotificationPublisherAlive(){
        NativeNotification token;std::atomic_uint calls{0};
        {NativeNotifications bus;token=bus.subscribe([&]{++calls;});QVERIFY(token);bus.notify();}
        QVERIFY(token);QCOMPARE(calls.load(),1u);token={};QCOMPARE(calls.load(),1u);
    }
};
QTEST_GUILESS_MAIN(NativeNotificationsTest)
#include "test_addon_native_notifications.moc"
