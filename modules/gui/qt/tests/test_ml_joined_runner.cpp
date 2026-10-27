// SPDX-License-Identifier: GPL-2.0-or-later
#include "medialibrary/mljoinedthreadrunner.hpp"
#include "medialibrary/mlthreadpool.hpp"
#include <QCoreApplication>
#include <QPointer>
#include <QScopeGuard>
#include <QTest>
#include <QThreadStorage>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

using Lifetime=vlc::addons::AddonStreamLifetime;
namespace {
struct Wait {
    std::mutex mutex; std::condition_variable changed;
    bool released=false;
    std::atomic_bool entered{false},ended{false};
    void wait() { std::unique_lock<std::mutex> lock(mutex); entered=true; changed.wait(lock,[&] { return released; }); ended=true; }
    void release() { { std::lock_guard<std::mutex> lock(mutex); released=true; } changed.notify_all(); }
};
struct Tail {
    std::shared_ptr<Wait> wait;
    ~Tail() { wait->wait(); }
};
struct Context { unsigned value=0; };
struct Drop { std::function<void()> run; ~Drop() { if (run) run(); } };
}
class TestMLJoinedRunner : public QObject {
    Q_OBJECT
private slots:
    void cleanup() {
        QTRY_COMPARE_WITH_TIMEOUT(MLJoinedThreadRunner::processTaskCount(),0u,5000);
        QTRY_COMPARE_WITH_TIMEOUT(Lifetime::processQtWorkCount(),0u,5000);
    }
    void missingOrClosedLifetimeRefusesWork() {
        QObject receiver; MLJoinedThreadRunner absent({});
        QCOMPARE(absent.runOnThread<Context>(&receiver,[](auto&) {},[](auto,auto&) {}),quint64(0));
        Lifetime owner; MLJoinedThreadRunner closed(owner.handle()); owner.close();
        QCOMPARE(closed.runOnThread<Context>(&receiver,[](auto&) {},[](auto,auto&) {}),quint64(0));
        Lifetime active; MLJoinedThreadRunner local(active.handle()); quint64 foreign=1;
        std::thread caller([&] { foreign=local.runOnThread<Context>(&receiver,[](auto&) {},[](auto,auto&) {}); });
        caller.join(); QCOMPARE(foreign,quint64(0)); active.close();
    }
    void strictFifoPublishesContextOnTheOwnerThread() {
        Lifetime owner; MLJoinedThreadRunner runner(owner.handle()); QObject receiver;
        const auto first=std::make_shared<Wait>(); const auto release=qScopeGuard([&] { first->release(); owner.close(); });
        std::atomic_uint workOrder{0}; QList<unsigned> delivered; bool wrongThread=false;
        QVERIFY(runner.runOnThread<Context>(&receiver,[&](auto& ctx) { first->wait(); ctx.value=++workOrder; },
            [&](auto,auto& ctx) { wrongThread|=QThread::currentThread()!=thread(); delivered.append(ctx.value); })>0);
        QTRY_VERIFY(first->entered.load());
        QVERIFY(runner.runOnThread<Context>(&receiver,[&](auto& ctx) { ctx.value=++workOrder; },
            [&](auto,auto& ctx) { wrongThread|=QThread::currentThread()!=thread(); delivered.append(ctx.value); })>0);
        QCOMPARE(workOrder.load(),0u); first->release();
        QTRY_COMPARE(delivered,(QList<unsigned>{1,2})); QVERIFY(!wrongThread);
    }
    void cancelQueuedWorkNeverEntersItsNativeCallback() {
        Lifetime owner; MLJoinedThreadRunner runner(owner.handle()); QObject receiver;
        const auto wait=std::make_shared<Wait>(); const auto release=qScopeGuard([&] { wait->release(); owner.close(); });
        std::atomic_uint second{0}; unsigned callbacks=0;
        QVERIFY(runner.runOnThread<Context>(&receiver,[wait](auto&) { wait->wait(); },[&](auto,auto&) { ++callbacks; })>0);
        QTRY_VERIFY(wait->entered.load());
        const auto id=runner.runOnThread<Context>(&receiver,[&](auto&) { ++second; },[&](auto,auto&) { ++callbacks; }); QVERIFY(id);
        QVERIFY(runner.cancelTask(&receiver,id)); wait->release(); QTRY_COMPARE(callbacks,1u);
        QVERIFY(owner.handle().waitQt(std::chrono::seconds(5))); QCOMPARE(second.load(),0u);
    }
    void cancelActiveWorkSuppressesDeliveryWithoutClaimingRollback() {
        Lifetime owner; MLJoinedThreadRunner runner(owner.handle()); QObject receiver;
        const auto wait=std::make_shared<Wait>(); const auto release=qScopeGuard([&] { wait->release(); owner.close(); });
        unsigned callbacks=0; std::atomic_bool committed{false};
        const auto id=runner.runOnThread<Context>(&receiver,[&](auto&) { wait->wait(); committed=true; },[&](auto,auto&) { ++callbacks; }); QVERIFY(id);
        QTRY_VERIFY(wait->entered.load()); QObject other; QVERIFY(!runner.cancelTask(&other,id)); QVERIFY(runner.cancelTask(&receiver,id));
        wait->release(); QVERIFY(owner.handle().waitQt(std::chrono::seconds(5))); QCoreApplication::processEvents();
        QVERIFY(committed.load()); QCOMPARE(callbacks,0u);
    }
    void receiverDestructionCancelsQueuedAndActiveDelivery() {
        Lifetime owner; MLJoinedThreadRunner runner(owner.handle()); auto receiver=std::make_unique<QObject>();
        const auto wait=std::make_shared<Wait>(); const auto release=qScopeGuard([&] { wait->release(); owner.close(); });
        unsigned callbacks=0; std::atomic_uint queued{0};
        QVERIFY(runner.runOnThread<Context>(receiver.get(),[wait](auto&) { wait->wait(); },[&](auto,auto&) { ++callbacks; }));
        QTRY_VERIFY(wait->entered.load());
        QVERIFY(runner.runOnThread<Context>(receiver.get(),[&](auto&) { ++queued; },[&](auto,auto&) { ++callbacks; }));
        receiver.reset(); wait->release(); QVERIFY(owner.handle().waitQt(std::chrono::seconds(5)));
        QCoreApplication::processEvents(); QCOMPARE(queued.load(),0u); QCOMPARE(callbacks,0u);
    }
    void ownerDestructionDoesNotWaitForTheWorker() {
        Lifetime owner; auto runner=std::make_unique<MLJoinedThreadRunner>(owner.handle()); QObject receiver;
        const auto wait=std::make_shared<Wait>(); const auto release=qScopeGuard([&] { wait->release(); owner.close(); });
        unsigned callbacks=0;
        QVERIFY(runner->runOnThread<Context>(&receiver,[wait](auto&) { wait->wait(); },[&](auto,auto&) { ++callbacks; }));
        QTRY_VERIFY(wait->entered.load()); runner.reset(); QVERIFY(!wait->ended.load()); owner.close();
        QVERIFY(!owner.handle().qtDrained()); wait->release(); QVERIFY(owner.handle().waitQt(std::chrono::seconds(5)));
        QCoreApplication::processEvents(); QCOMPARE(callbacks,0u);
    }
    void qtThreadStorageKeepsTheWorkAndQuotaUntilActualThreadJoin() {
        Lifetime owner; MLJoinedThreadRunner runner(owner.handle()); QObject receiver;
        const auto tail=std::make_shared<Wait>(); const auto storage=std::make_shared<QThreadStorage<Tail*>>();
        const auto release=qScopeGuard([&] { tail->release(); owner.close(); });
        unsigned callbacks=0;
        QVERIFY(runner.runOnThread<Context>(&receiver,[storage,tail](auto&) { storage->setLocalData(new Tail{tail}); },
            [&](auto,auto&) { ++callbacks; }));
        QTRY_VERIFY(tail->entered.load()); QTRY_COMPARE(callbacks,1u);
        owner.close(); QVERIFY(!owner.handle().qtDrained()); QCOMPARE(MLJoinedThreadRunner::processTaskCount(),1u);
        QVERIFY(!owner.handle().waitQt(std::chrono::milliseconds(10)));
        tail->release(); QVERIFY(owner.handle().waitQt(std::chrono::seconds(5))); QVERIFY(tail->ended.load());
        QCOMPARE(MLJoinedThreadRunner::processTaskCount(),0u);
    }
    void appCloseCancelsQueuedWorkersWithoutGuiDelivery() {
        Lifetime owner; MLJoinedThreadRunner runner(owner.handle()); QObject receiver;
        const auto wait=std::make_shared<Wait>(); const auto release=qScopeGuard([&] { wait->release(); owner.close(); });
        std::atomic_uint queued{0}; unsigned callbacks=0;
        QVERIFY(runner.runOnThread<Context>(&receiver,[wait](auto&) { wait->wait(); },[&](auto,auto&) { ++callbacks; }));
        QTRY_VERIFY(wait->entered.load());
        QVERIFY(runner.runOnThread<Context>(&receiver,[&](auto&) { ++queued; },[&](auto,auto&) { ++callbacks; }));
        owner.close(); wait->release(); QVERIFY(owner.handle().waitQt(std::chrono::seconds(5)));
        QCOMPARE(queued.load(),0u); QCoreApplication::processEvents(); QCOMPARE(callbacks,0u);
    }
    void retiringTasksKeepTheProcessCapAcrossNewOwners() {
        Lifetime owner; auto runner=std::make_unique<MLJoinedThreadRunner>(owner.handle()); QObject receiver;
        const auto wait=std::make_shared<Wait>(); const auto release=qScopeGuard([&] { wait->release(); owner.close(); });
        for (unsigned index=0;index<MLJoinedThreadRunner::ProcessLimit;++index)
            QVERIFY(runner->runOnThread<Context>(&receiver,[wait](auto&) { wait->wait(); },[](auto,auto&) {}));
        QTRY_VERIFY(wait->entered.load()); QCOMPARE(MLJoinedThreadRunner::processTaskCount(),MLJoinedThreadRunner::ProcessLimit);
        MLJoinedThreadRunner second(owner.handle());
        QCOMPARE(second.runOnThread<Context>(&receiver,[](auto&) {},[](auto,auto&) {}),quint64(0));
        runner.reset(); wait->release(); QVERIFY(owner.handle().waitQt(std::chrono::seconds(5)));
        QVERIFY(second.runOnThread<Context>(&receiver,[](auto&) {},[](auto,auto&) {}));
        owner.close(); QVERIFY(owner.handle().waitQt(std::chrono::seconds(5)));
    }
    void callbackCanDeleteTheRunner() {
        Lifetime owner; auto* runner=new MLJoinedThreadRunner(owner.handle()); QPointer<MLJoinedThreadRunner> guard(runner); QObject receiver;
        QVERIFY(runner->runOnThread<Context>(&receiver,[](auto&) {},[runner](auto,auto&) { delete runner; }));
        QTRY_VERIFY(guard.isNull()); owner.close(); QVERIFY(owner.handle().waitQt(std::chrono::seconds(5)));
    }
    void cancelCanDeleteTheRunnerThroughTheLastCallbackCapture() {
        Lifetime owner; auto* runner=new MLJoinedThreadRunner(owner.handle()); QPointer<MLJoinedThreadRunner> guard(runner); QObject receiver;
        const auto wait=std::make_shared<Wait>(); const auto release=qScopeGuard([&] { wait->release(); owner.close(); });
        auto drop=std::make_shared<Drop>(); drop->run=[runner] { delete runner; };
        const auto id=runner->runOnThread<Context>(&receiver,[wait](auto&) { wait->wait(); },[drop](auto,auto&) {});
        QVERIFY(id); QTRY_VERIFY(wait->entered.load()); drop.reset();
        QVERIFY(runner->cancelTask(&receiver,id)); QVERIFY(guard.isNull());
        wait->release(); owner.close(); QVERIFY(owner.handle().waitQt(std::chrono::seconds(5)));
    }
    void receiverRemovalCanDeleteTheRunnerThroughCallbackCaptureDestruction() {
        Lifetime owner; auto* runner=new MLJoinedThreadRunner(owner.handle()); QPointer<MLJoinedThreadRunner> guard(runner);
        auto receiver=std::make_unique<QObject>();
        const auto wait=std::make_shared<Wait>(); const auto release=qScopeGuard([&] { wait->release(); owner.close(); });
        auto drop=std::make_shared<Drop>(); drop->run=[runner] { delete runner; };
        QVERIFY(runner->runOnThread<Context>(receiver.get(),[wait](auto&) { wait->wait(); },[drop](auto,auto&) {}));
        QTRY_VERIFY(wait->entered.load());
        QVERIFY(runner->runOnThread<Context>(receiver.get(),[](auto&) {},[drop](auto,auto&) {}));
        drop.reset(); receiver.reset(); QVERIFY(guard.isNull());
        wait->release(); owner.close(); QVERIFY(owner.handle().waitQt(std::chrono::seconds(5)));
    }
    void completedPayloadsStayBoundedWithoutGuiDelivery() {
        Lifetime owner; MLJoinedThreadRunner runner(owner.handle()); QObject receiver;
        unsigned calls=0;
        for (unsigned index=0;index<MLJoinedThreadRunner::OwnerLimit;++index) {
            QVERIFY(runner.runOnThread<Context>(&receiver,[](auto&) {},[&](auto,auto&) { ++calls; }));
            QVERIFY(owner.handle().waitQt(std::chrono::seconds(5)));
        }
        QCOMPARE(calls,0u);
        QCOMPARE(runner.runOnThread<Context>(&receiver,[](auto&) {},[](auto,auto&) {}),quint64(0));
        QCoreApplication::processEvents(); QCOMPARE(calls,MLJoinedThreadRunner::OwnerLimit);
        QVERIFY(runner.runOnThread<Context>(&receiver,[](auto&) {},[](auto,auto&) {}));
        owner.close(); QVERIFY(owner.handle().waitQt(std::chrono::seconds(5)));
    }
    void exceptionDoesNotPublishAnIncompleteContext() {
        Lifetime owner; MLJoinedThreadRunner runner(owner.handle()); QObject receiver; unsigned calls=0;
        QVERIFY(runner.runOnThread<Context>(&receiver,[](auto&) { throw 1; },[&](auto,auto&) { ++calls; }));
        QVERIFY(owner.handle().waitQt(std::chrono::seconds(5))); QCoreApplication::processEvents(); QCOMPARE(calls,0u); owner.close();
    }
    void legacyAndJoinedTasksHaveDistinctIds() {
        Lifetime owner; MLJoinedThreadRunner joined(owner.handle()); auto* legacy=new ThreadRunner; QObject receiver;
        unsigned calls=0;
        const auto first=legacy->runOnThread<Context>(&receiver,[](auto&) {},[&](auto,auto&) { ++calls; },"test-shared-ids");
        const auto second=joined.runOnThread<Context>(&receiver,[](auto&) {},[&](auto,auto&) { ++calls; });
        QVERIFY(first); QVERIFY(second); QVERIFY(first!=second); QTRY_COMPARE(calls,2u);
        legacy->destroy(); owner.close(); QVERIFY(owner.handle().waitQt(std::chrono::seconds(5)));
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    }
};
QTEST_GUILESS_MAIN(TestMLJoinedRunner)
#include "test_ml_joined_runner.moc"
