// SPDX-License-Identifier: GPL-2.0-or-later
#include <QtTest>
#include <QPointer>
#include <QTemporaryDir>
#include <QSocketNotifier>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>
#include "addons/addon_store_authority.hpp"
#include "addons/addon_catalogue_store_lock.hpp"
#include "addons/addon_native_wake.hpp"
#ifdef Q_OS_WIN
# include <windows.h>
# include <QWinEventNotifier>
#else
# include <unistd.h>
# include <fcntl.h>
#endif
using namespace vlc::addons;
namespace {
struct IoGate {
    std::mutex mutex;std::condition_variable condition;
    bool entered=false,release=false;
    std::atomic_bool pause{false};std::atomic_int result{-9};
    void arm(){std::lock_guard<std::mutex> lock(mutex);entered=false;release=false;result=-9;pause=true;}
    bool before(){
        if(!pause.exchange(false))return false;
        std::unique_lock<std::mutex> lock(mutex);entered=true;condition.notify_all();
        condition.wait(lock,[&]{return release;});return true;
    }
    bool await(){std::unique_lock<std::mutex> lock(mutex);return condition.wait_for(lock,std::chrono::seconds(3),[&]{return entered;});}
    void unblock(){std::lock_guard<std::mutex> lock(mutex);release=true;condition.notify_all();}
} io;
#ifdef Q_OS_WIN
BOOL fixtureSignal(HANDLE event){const bool paused=io.before();const BOOL result=SetEvent(event);if(paused)io.result=result?1:-1;return result;}
#else
ssize_t fixtureSignal(int fd,const void* data,size_t length){const bool paused=io.before();const auto result=::write(fd,data,length);if(paused)io.result=int(result);return result;}
#endif
}
// Interpose only the actual native signal syscall, to hold a scheduler point
// across close or unlock. No fault/test branch exists in production sources.
#ifdef Q_OS_WIN
# define SetEvent fixtureSignal
#else
# define write fixtureSignal
#endif
#include "../addons/addon_native_wake.cpp"
#ifdef Q_OS_WIN
# undef SetEvent
#else
# undef write
#endif
class CatalogueNativeTest final : public QObject {
    Q_OBJECT
private slots:
    void persistentBeginEndSignals(){
        QTemporaryDir directory;QVERIFY(directory.isValid());StoreAuthority authority(directory.path());
        auto calls=std::make_shared<std::atomic_uint>(0);auto watch=authority.onChanged([calls]{++*calls;});QVERIFY(watch);
        const auto before=authority.capture();
        {auto mutation=authority.mutation();QVERIFY(!before.current());QVERIFY(!authority.capture().current());QCOMPARE(calls->load(),1u);}
        QVERIFY(authority.capture().current());QCOMPARE(calls->load(),3u);
        authority.invalidate();QCOMPARE(calls->load(),4u);
    }
    void nestedWriterFinalQuiescence(){
        QTemporaryDir directory;StoreAuthority authority(directory.path());auto calls=std::make_shared<std::atomic_uint>(0);
        auto watch=authority.onChanged([calls]{++*calls;});
        {auto outer=authority.mutation();{auto inner=authority.mutation();QVERIFY(!authority.capture().current());}
         QVERIFY(!authority.capture().current());QCOMPARE(calls->load(),4u);}
        QVERIFY(authority.capture().current());QCOMPARE(calls->load(),6u);
    }
    void boundedSubscriptionQuota(){
        QTemporaryDir directory;StoreAuthority authority(directory.path());std::vector<StoreAuthorityChanges> values;
        for(unsigned i=0;i<StoreAuthority::ChangeLimit;++i){auto value=authority.onChanged([]{});QVERIFY(value);values.push_back(value);}
        QVERIFY(!authority.onChanged([]{}));values.pop_back();QVERIFY(authority.onChanged([]{}));
    }
    void lastCaptureDropsOutsideLocksAndBeforeQuota(){
        QTemporaryDir directory;StoreAuthority authority(directory.path());
        struct Control {std::mutex mutex;std::condition_variable cv;bool entered=false,release=false;};
        struct Drop {std::shared_ptr<Control> control;
            explicit Drop(std::shared_ptr<Control> value):control(std::move(value)){}
            ~Drop(){std::unique_lock<std::mutex> lock(control->mutex);control->entered=true;control->cv.notify_all();control->cv.wait(lock,[&]{return control->release;});}};
        const auto control=std::make_shared<Control>();
        auto marker=std::make_shared<Drop>(control);
        std::vector<StoreAuthorityChanges> values;
        for(unsigned i=1;i<StoreAuthority::ChangeLimit;++i)values.push_back(authority.onChanged([]{}));
        auto held=authority.onChanged([marker]{});marker.reset();QVERIFY(held);
        std::thread releasing([value=std::move(held)]()mutable{value={};});
        bool entered;
        {std::unique_lock<std::mutex> lock(control->mutex);entered=control->cv.wait_for(lock,std::chrono::seconds(3),[&]{return control->entered;});}
        bool refused=false;unsigned callbacks=0;
        if(entered){refused=!authority.onChanged([]{});authority.invalidate();callbacks=1;}
        {std::lock_guard<std::mutex> lock(control->mutex);control->release=true;control->cv.notify_all();}
        releasing.join();QVERIFY(entered);QVERIFY(refused);QCOMPARE(callbacks,1u);QVERIFY(authority.onChanged([]{}));
    }
    void nativeBurstCoalesces(){
        unsigned calls=0;AddonNativeWake wake([&]{++calls;});QVERIFY(wake.valid());const auto signal=wake.nativeCallback();
        std::thread producer([signal]{for(unsigned i=0;i<5000;++i)signal();});producer.join();
        QTRY_COMPARE(calls,1u);QTest::qWait(30);QCOMPARE(calls,1u);
    }
    void unchangedOwnerHasNoWake(){
        unsigned calls=0;AddonNativeWake wake([&]{++calls;});QVERIFY(wake.valid());
        QTest::qWait(80);QCOMPARE(calls,0u);
    }
    void closeRetainsHandleAcrossPausedNativeSignal(){
        unsigned calls=0;auto wake=std::make_unique<AddonNativeWake>([&]{++calls;});QVERIFY(wake->valid());
        auto signal=wake->nativeCallback();io.arm();std::atomic_bool returned{false};
        std::thread producer([signal,&returned]{signal();returned=true;});
        const bool entered=io.await();wake.reset();
        unsigned heartbeat=0;QTimer timer;connect(&timer,&QTimer::timeout,this,[&]{++heartbeat;});timer.start(1);QTest::qWait(15);
        const bool stillPending=!returned.load();io.unblock();producer.join();timer.stop();
        QVERIFY(entered);QVERIFY(stillPending);QVERIFY(heartbeat>0);QCOMPARE(io.result.load(),1);QCOMPARE(calls,0u);
        signal();QTest::qWait(10);QCOMPARE(calls,0u);
    }
    void deliveryCanDeleteOwner(){
        QPointer<AddonNativeWake> wake;wake=new AddonNativeWake([&]{delete wake.data();});QVERIFY(wake->valid());
        const auto signal=wake->nativeCallback();signal();QTRY_VERIFY(!wake);signal();
    }
    void deliveryCaptureDropCanDeleteOwner(){
        QPointer<AddonNativeWake> wake;
        auto marker=std::shared_ptr<int>(new int(1),[&](int* p){delete p;delete wake.data();});
        wake=new AddonNativeWake([marker,&wake]{wake->close();});marker.reset();
        const auto signal=wake->nativeCallback();signal();QTRY_VERIFY(!wake);signal();
    }
    void postUnlockNotificationSeesReleasedLock(){
        QTemporaryDir directory;StoreAuthority authority(directory.path());const auto path=directory.filePath(QStringLiteral("store.lock"));
        AddonNativeWake wake([]{});QVERIFY(wake.valid());const auto signal=wake.nativeCallback();auto phase=std::make_shared<std::atomic_int>(0);
        auto subscription=authority.onChanged([phase,signal]{if(phase->load()==2)signal();});QVERIFY(subscription);
        io.arm();std::atomic_bool acquired{false};
        std::thread writer([&]{CatalogueStoreLock lock(path,authority);acquired=lock.tryLock(0);if(!acquired)return;
            {auto mutation=authority.mutation();}phase->store(2);});
        const bool entered=io.await();QLockFile other(path);const bool unlocked=entered && other.tryLock(0);if(unlocked)other.unlock();
        io.unblock();writer.join();QVERIFY(acquired.load());QVERIFY(entered);QVERIFY(unlocked);
    }
    void mutationEndDoesNotReplaceOuterUnlock(){
        QTemporaryDir directory;StoreAuthority authority(directory.path());const auto path=directory.filePath(QStringLiteral("store.lock"));
        unsigned deliveries=0;AddonNativeWake wake([&]{++deliveries;});auto subscription=authority.onChanged(wake.nativeCallback());
        {
            CatalogueStoreLock lock(path,authority);QVERIFY(lock.tryLock(0));{auto mutation=authority.mutation();}
            QTRY_VERIFY(deliveries>0);QVERIFY(authority.capture().current());QLockFile other(path);QVERIFY(!other.tryLock(0));
        }
        const auto before=deliveries;QTRY_VERIFY(deliveries>before);QLockFile other(path);QVERIFY(other.tryLock(0));
    }
    void droppingSubscriptionStopsFutureSignals(){
        QTemporaryDir directory;StoreAuthority authority(directory.path());auto calls=std::make_shared<std::atomic_uint>(0);
        {auto watch=authority.onChanged([calls]{++*calls;});authority.invalidate();QCOMPARE(calls->load(),1u);}
        authority.invalidate();QCOMPARE(calls->load(),1u);
    }
};
QTEST_GUILESS_MAIN(CatalogueNativeTest)
#include "test_addon_catalogue_native.moc"
