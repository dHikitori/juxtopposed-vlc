// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_repository_jobs.hpp"
#include "addons/addon_repository_cancellation.hpp"
#include <QCoreApplication>
#include <QPointer>
#include <QScopeGuard>
#include <QTest>
#include <QThreadStorage>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace vlc::addons;
namespace {
struct Wait {
    std::mutex mutex; std::condition_variable changed; bool released=false;
    std::atomic_bool entered{false},ended{false};
    void wait() { std::unique_lock<std::mutex> lock(mutex); entered=true; changed.wait(lock,[&] { return released; }); ended=true; }
    void release() { { std::lock_guard<std::mutex> lock(mutex); released=true; } changed.notify_all(); }
};
struct Tail { std::shared_ptr<Wait> wait; ~Tail() { wait->wait(); } };
}
class TestAddonRepositoryJobs : public QObject {
    Q_OBJECT
private slots:
    void cleanup() {
        QTRY_COMPARE_WITH_TIMEOUT(AddonRepositoryJobs::processCount(),0u,5000);
        QTRY_COMPARE_WITH_TIMEOUT(AddonStreamLifetime::processQtWorkCount(),0u,5000);
    }
    void typedRefusalSeparatesProcessQuotaFromBusyAndClosed() {
        using Refusal=AddonRepositoryJobs::Refusal;Refusal why=Refusal::None;
        AddonStreamLifetime owner;AddonRepositoryJobs first(owner.handle()),second(owner.handle()),third(owner.handle());
        const auto one=std::make_shared<Wait>(),two=std::make_shared<Wait>();
        const auto release=qScopeGuard([&]{one->release();two->release();owner.close();});
        QVERIFY(!third.submit({},[](auto){},&why));QCOMPARE(why,Refusal::Invalid);
        QVERIFY(first.submit([one]{one->wait();},[](auto){},&why));QCOMPARE(why,Refusal::None);QTRY_VERIFY(one->entered.load());
        QVERIFY(!first.submit([]{},[](auto){},&why));QCOMPARE(why,Refusal::Busy);
        QVERIFY(second.submit([two]{two->wait();},[](auto){},&why));QTRY_VERIFY(two->entered.load());
        QVERIFY(!third.submit([]{},[](auto){},&why));QCOMPARE(why,Refusal::ProcessQuota);
        third.close();QVERIFY(!third.submit([]{},[](auto){},&why));QCOMPARE(why,Refusal::Closed);
    }
    void nativeAvailabilityFollowsActualTlsCleanupAndUpdatedCount() {
        AddonStreamLifetime owner;AddonRepositoryJobs jobs(owner.handle());
        const auto wait=std::make_shared<Wait>();const auto storage=std::make_shared<QThreadStorage<Tail*>>();
        const auto release=qScopeGuard([&]{wait->release();owner.close();});
        std::atomic_uint notices{0},observed{99};unsigned calls=0;
        auto token=AddonRepositoryJobs::onProcessAvailable([&]{observed=AddonRepositoryJobs::processCount();++notices;});QVERIFY(token);
        QVERIFY(jobs.submit([storage,wait]{storage->setLocalData(new Tail{wait});},[&](auto result){QCOMPARE(result,AddonRepositoryJobs::Result::Finished);++calls;}));
        QTRY_VERIFY(wait->entered.load());QTRY_COMPARE(calls,1u);QCOMPARE(AddonRepositoryJobs::processCount(),1u);QCOMPARE(notices.load(),0u);
        wait->release();QTRY_COMPARE(notices.load(),1u);QCOMPARE(observed.load(),0u);
        QVERIFY(owner.handle().waitQt(std::chrono::seconds(5)));
    }
    void missingAndClosedAppHandlesRefuseWork() {
        AddonRepositoryJobs absent({}); QVERIFY(!absent.submit([] {},[](auto) {}));
        AddonStreamLifetime owner; AddonRepositoryJobs closed(owner.handle()); owner.close();
        QVERIFY(!closed.submit([] {},[](auto) {})); QVERIFY(!closed.busy());
    }
    void resultPublishesOnceAndReentryCanSubmitAgain() {
        AddonStreamLifetime owner; AddonRepositoryJobs jobs(owner.handle()); unsigned count=0;
        QVERIFY(jobs.submit([] {},[&](auto result) {
            QCOMPARE(result,AddonRepositoryJobs::Result::Finished); QVERIFY(!jobs.busy()); ++count;
            QVERIFY(jobs.submit([] {},[&](auto next) { QCOMPARE(next,AddonRepositoryJobs::Result::Finished); ++count; }));
        }));
        QVERIFY(jobs.busy()); QVERIFY(!jobs.submit([] {},[](auto) {})); QTRY_COMPARE(count,2u);
        owner.close(); QVERIFY(owner.handle().waitQt(std::chrono::seconds(5)));
    }
    void cancellationScopeReachesTheWorkerAndRestoresTheCaller() {
        AddonStreamLifetime owner; AddonRepositoryJobs jobs(owner.handle());
        const auto wait=std::make_shared<Wait>(); const auto release=qScopeGuard([&] { wait->release(); owner.close(); });
        std::atomic_bool observed{false}; unsigned calls=0;
        QVERIFY(!RepositoryCancellation::requested());
        QVERIFY(jobs.submit([&] { wait->wait(); observed=RepositoryCancellation::requested(); },[&](auto result) {
            QCOMPARE(result,AddonRepositoryJobs::Result::Canceled); ++calls;
        }));
        QTRY_VERIFY(wait->entered.load()); jobs.cancel(); wait->release(); QTRY_COMPARE(calls,1u);
        QVERIFY(observed.load()); QVERIFY(!RepositoryCancellation::requested()); owner.close();
        QVERIFY(owner.handle().waitQt(std::chrono::seconds(5)));
    }
    void nestedCancellationScopesRestoreTheirPreviousState() {
        auto stopped=std::make_shared<std::atomic_bool>(true),open=std::make_shared<std::atomic_bool>(false);
        QVERIFY(!RepositoryCancellation::requested());
        { RepositoryCancellation outer(stopped); QVERIFY(RepositoryCancellation::requested());
          { RepositoryCancellation inner(open); QVERIFY(!RepositoryCancellation::requested()); }
          QVERIFY(RepositoryCancellation::requested()); }
        QVERIFY(!RepositoryCancellation::requested());
    }
    void ownerDestructionDoesNotWaitOrPublishLateResults() {
        AddonStreamLifetime owner; auto jobs=std::make_unique<AddonRepositoryJobs>(owner.handle());
        const auto wait=std::make_shared<Wait>(); const auto release=qScopeGuard([&] { wait->release(); owner.close(); }); unsigned calls=0;
        QVERIFY(jobs->submit([wait] { wait->wait(); },[&](auto) { ++calls; })); QTRY_VERIFY(wait->entered.load());
        jobs.reset(); QVERIFY(!wait->ended.load()); wait->release(); QVERIFY(owner.handle().waitQt(std::chrono::seconds(5)));
        QCoreApplication::processEvents(); QCOMPARE(calls,0u);
    }
    void appCloseCancelsWithoutAnyGuiDelivery() {
        AddonStreamLifetime owner; AddonRepositoryJobs jobs(owner.handle());
        const auto wait=std::make_shared<Wait>(); const auto release=qScopeGuard([&] { wait->release(); owner.close(); });
        std::atomic_bool canceled{false}; unsigned calls=0;
        QVERIFY(jobs.submit([&] { wait->entered=true; while (!RepositoryCancellation::requested()) std::this_thread::yield(); canceled=true; },
            [&](auto) { ++calls; }));
        QTRY_VERIFY(wait->entered.load()); owner.close(); QVERIFY(owner.handle().waitQt(std::chrono::seconds(5)));
        QVERIFY(canceled.load()); QCoreApplication::processEvents(); QCOMPARE(calls,0u);
    }
    void quotaSurvivesActualQtThreadStorageDestruction() {
        AddonStreamLifetime owner; AddonRepositoryJobs first(owner.handle()),second(owner.handle()),third(owner.handle());
        const auto wait=std::make_shared<Wait>(); const auto storage=std::make_shared<QThreadStorage<Tail*>>();
        const auto release=qScopeGuard([&] { wait->release(); owner.close(); });
        QVERIFY(first.submit([storage,wait] { storage->setLocalData(new Tail{wait}); },[](auto) {})); QTRY_VERIFY(wait->entered.load());
        const auto other=std::make_shared<Wait>(); const auto releaseOther=qScopeGuard([&] { other->release(); });
        QVERIFY(second.submit([other] { other->wait(); },[](auto) {})); QTRY_VERIFY(other->entered.load());
        QCOMPARE(AddonRepositoryJobs::processCount(),AddonRepositoryJobs::ProcessLimit);
        QVERIFY(!third.submit([] {},[](auto) {})); first.close(); second.close(); owner.close();
        other->release(); QVERIFY(!owner.handle().waitQt(std::chrono::milliseconds(20)));
        QVERIFY(AddonRepositoryJobs::processCount()>0); wait->release(); QVERIFY(owner.handle().waitQt(std::chrono::seconds(5)));
        QVERIFY(wait->ended.load()); QCOMPARE(AddonRepositoryJobs::processCount(),0u);
    }
    void callbackCanDeleteTheOwner() {
        AddonStreamLifetime owner; auto* jobs=new AddonRepositoryJobs(owner.handle()); QPointer<AddonRepositoryJobs> guard(jobs);
        QVERIFY(jobs->submit([] {},[jobs](auto) { delete jobs; })); QTRY_VERIFY(guard.isNull());
        owner.close(); QVERIFY(owner.handle().waitQt(std::chrono::seconds(5)));
    }
    void workerExceptionIsARealFailure() {
        AddonStreamLifetime owner; AddonRepositoryJobs jobs(owner.handle()); unsigned calls=0;
        QVERIFY(jobs.submit([] { throw 1; },[&](auto result) { QCOMPARE(result,AddonRepositoryJobs::Result::Failed); ++calls; }));
        QTRY_COMPARE(calls,1u); owner.close(); QVERIFY(owner.handle().waitQt(std::chrono::seconds(5)));
    }
};
QTEST_GUILESS_MAIN(TestAddonRepositoryJobs)
#include "test_addon_repository_jobs.moc"
