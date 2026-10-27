// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QSemaphore>
#include <QScopeGuard>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include "addons/addon_store_authority.hpp"

using namespace vlc::addons;
namespace {
struct NativeGate {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, released = false;
    void pause() {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true; changed.notify_all(); changed.wait(lock, [&] { return released; });
    }
    bool wait() {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(3), [&] { return entered; });
    }
    void release() { std::lock_guard<std::mutex> lock(mutex); released = true; changed.notify_all(); }
};
}
class TestAddonStoreAuthority : public QObject
{
    Q_OBJECT
    static bool write(const QString& path, const QByteArray& bytes) {
        QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
private slots:
    void nativeCancellationEndsBeforeTheWriterCanPublish()
    {
        QTemporaryDir directory; StoreAuthority owner(directory.path());
        const auto token = owner.capture(); const auto gate = std::make_shared<NativeGate>();
        std::atomic_bool canceled{false}, tokenClosed{false}, published{false};
        const auto watch = token.onInvalidated([gate, token, &canceled, &tokenClosed] {
            tokenClosed.store(!token.current()); gate->pause(); canceled.store(true);
        });
        QVERIFY(bool(watch));
        const auto path = directory.filePath(QStringLiteral("native-authority.json"));
        std::thread writer([&] {
            const auto mutation = owner.mutation();
            if (canceled.load()) published.store(write(path, "{\"enabled\":false}"));
        });
        const auto joined = qScopeGuard([&] { gate->release(); writer.join(); });
        QVERIFY(gate->wait()); QVERIFY(!token.current()); QVERIFY(tokenClosed.load());
        QVERIFY(!bool(watch)); QVERIFY(!published.load()); QVERIFY(!QFile::exists(path));
        gate->release(); QTRY_VERIFY(published.load());
    }
    void concurrentInvalidatorsWaitForEarlierNativeCancellation()
    {
        QTemporaryDir directory; StoreAuthority owner(directory.path());
        const auto gate = std::make_shared<NativeGate>();
        auto watch = owner.capture().onInvalidated([gate] { gate->pause(); }); QVERIFY(bool(watch));
        std::atomic_bool firstDone{false}, secondDone{false};
        std::thread first([&] { owner.invalidate(); firstDone.store(true); });
        std::thread second;
        const auto joined = qScopeGuard([&] { gate->release(); first.join(); if (second.joinable()) second.join(); });
        QVERIFY(gate->wait());
        const auto middle = owner.capture(); QVERIFY(middle.current());
        second = std::thread([&] { owner.invalidate(); secondDone.store(true); });
        QTRY_VERIFY(!middle.current()); QVERIFY(!firstDone.load()); QVERIFY(!secondDone.load());
        gate->release(); QTRY_VERIFY(firstDone.load() && secondDone.load());
    }
    void refusedRegistrationAlsoBlocksAConcurrentPublication()
    {
        QTemporaryDir directory; StoreAuthority owner(directory.path());
        const auto old = owner.capture(); owner.invalidate();
        const auto gate = std::make_shared<NativeGate>();
        std::atomic_bool registrationDone{false}, invalidated{false}, armed{true};
        std::thread registration([&] {
            const auto watch = old.onInvalidated([gate] { gate->pause(); });
            armed.store(bool(watch)); registrationDone.store(true);
        });
        std::thread writer;
        const auto joined = qScopeGuard([&] { gate->release(); registration.join(); if (writer.joinable()) writer.join(); });
        QVERIFY(gate->wait()); const auto middle = owner.capture(); QVERIFY(middle.current());
        writer = std::thread([&] { owner.invalidate(); invalidated.store(true); });
        QTRY_VERIFY(!middle.current()); QVERIFY(!invalidated.load()); QVERIFY(!registrationDone.load());
        gate->release(); QTRY_VERIFY(registrationDone.load() && invalidated.load()); QVERIFY(!armed.load());
    }
    void nativeWatchesHaveABoundedRetainedQuotaAndOneCancellation()
    {
        QTemporaryDir directory; StoreAuthority owner(directory.path());
        const auto calls = std::make_shared<std::atomic_uint>(0);
        std::vector<StoreAuthorityWatch> watches;
        for (unsigned index = 0; index < StoreAuthorityToken::WatchLimit; ++index) {
            auto watch = owner.capture().onInvalidated([calls] { ++*calls; }); QVERIFY(bool(watch));
            watches.push_back(std::move(watch));
        }
        QVERIFY(!bool(owner.capture().onInvalidated([calls] { ++*calls; }))); QCOMPARE(calls->load(), 1u);
        auto copy = watches.front(); watches.clear();
        owner.invalidate(); QCOMPARE(calls->load(), 2u); QVERIFY(!bool(copy));
        owner.invalidate(); QCOMPARE(calls->load(), 2u); copy = {};
        auto discarded = owner.capture().onInvalidated([calls] { ++*calls; }); QVERIFY(bool(discarded)); discarded = {};
        owner.invalidate(); QCOMPARE(calls->load(), 2u);
        auto fresh = owner.capture().onInvalidated([calls] { ++*calls; }); QVERIFY(bool(fresh));
        owner.invalidate(); QCOMPARE(calls->load(), 3u); QVERIFY(!bool(fresh));
    }
    void aNativeWatchRetainsTheSharedStoreEpoch()
    {
        QTemporaryDir directory; std::atomic_uint calls{0}; StoreAuthorityWatch watch;
        {
            StoreAuthority original(directory.path());
            watch = original.capture().onInvalidated([&calls] { ++calls; }); QVERIFY(bool(watch));
        }
        StoreAuthority replacement(directory.path()); replacement.invalidate();
        QCOMPARE(calls.load(), 1u); QVERIFY(!bool(watch));
    }
    void retiringWatchQuotaCoversItsNativeCaptureDestructor()
    {
        struct Capture {
            std::shared_ptr<NativeGate> gate;
            bool paused = false;
            ~Capture() { if (paused) gate->pause(); }
        };
        QTemporaryDir directory; StoreAuthority owner(directory.path());
        const auto token = owner.capture();
        const auto gate = std::make_shared<NativeGate>();
        auto capture = std::make_shared<Capture>(); capture->gate = gate;
        auto retiring = token.onInvalidated([capture] {}); QVERIFY(bool(retiring));
        const auto release = qScopeGuard([gate] { gate->release(); });
        capture->paused = true; capture.reset();
        std::vector<StoreAuthorityWatch> active;
        for (unsigned index = 1; index < StoreAuthorityToken::WatchLimit; ++index) {
            auto watch = token.onInvalidated([] {}); QVERIFY(bool(watch)); active.push_back(std::move(watch));
        }
        std::thread destructor([watch = std::move(retiring)]() mutable { watch = {}; });
        const auto joined = qScopeGuard([&] { gate->release(); if (destructor.joinable()) destructor.join(); });
        QVERIFY(gate->wait());
        std::atomic_uint refusals{0};
        QVERIFY(!bool(token.onInvalidated([&refusals] { ++refusals; }))); QCOMPARE(refusals.load(), 1u);
        gate->release(); destructor.join();
        auto fresh = token.onInvalidated([] {}); QVERIFY(bool(fresh));
    }
    void racingNativeRegistrationCannotMissInvalidation()
    {
        QTemporaryDir directory; StoreAuthority owner(directory.path());
        for (unsigned repeat = 0; repeat < 200; ++repeat) {
            const auto token = owner.capture(); std::atomic_bool start{false}; std::atomic_uint calls{0}; StoreAuthorityWatch watch;
            std::thread registrar([&] {
                while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
                watch = token.onInvalidated([&calls] { ++calls; });
            });
            std::thread writer([&] {
                while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
                owner.invalidate();
            });
            start.store(true, std::memory_order_release); registrar.join(); writer.join();
            QCOMPARE(calls.load(), 1u); QVERIFY(!bool(watch));
        }
    }
    void packageRetirementIsSharedAndKeepsOtherPackagesAvailable()
    {
        QTemporaryDir directory; StoreAuthority owner(directory.path()), peer(directory.path());
        const auto old = owner.capture();
        {
            auto first = owner.retirePackage(QStringLiteral("org.example.first"));
            QVERIFY(bool(first)); QVERIFY(!old.current());
            QVERIFY(peer.packageRetiring(QStringLiteral("org.example.first")));
            QVERIFY(!peer.packageRetiring(QStringLiteral("org.example.other")));
            const auto during = peer.capture(); QVERIFY(during.current());
            {
                auto nested = peer.retirePackage(QStringLiteral("org.example.first"));
                auto moved = std::move(nested); QVERIFY(bool(moved));
                QVERIFY(!during.current());
            }
            QVERIFY(peer.packageRetiring(QStringLiteral("org.example.first")));
        }
        QVERIFY(!peer.packageRetiring(QStringLiteral("org.example.first")));
        QVERIFY(!old.current()); QVERIFY(peer.capture().current());
        QVERIFY(!bool(owner.retirePackage(QStringLiteral("../other"))));
        QVERIFY(owner.packageRetiring(QStringLiteral("../other")));
    }
    void storeAuthorityTokensAreAtomicAndKeepTheirEpochAlive()
    {
        QTemporaryDir storage;
        auto token = captureStoreAuthority(storage.filePath(QStringLiteral("new/store")));
        StoreAuthority owner(storage.filePath(QStringLiteral("new/./store")));
        QVERIFY(token.current()); QVERIFY(owner.capture().current());
        std::atomic_bool started{false}, stale{false};
        std::thread native([&] {
            started.store(true, std::memory_order_release);
            while (token.current()) std::this_thread::yield();
            stale.store(true, std::memory_order_release);
        });
        while (!started.load(std::memory_order_acquire)) std::this_thread::yield();
        owner.invalidate(); native.join(); QVERIFY(stale.load()); QVERIFY(!token.current());
        auto fresh = owner.capture(); QVERIFY(fresh.current());
        {
            StoreAuthority peer(storage.filePath(QStringLiteral("new/store"))); peer.invalidate();
        }
        QVERIFY(!fresh.current());
        fresh = captureStoreAuthority(storage.filePath(QStringLiteral("new/store"))); QVERIFY(fresh.current());
        owner.invalidate(); QVERIFY(!fresh.current());
    }
    void storeAuthorityBlocksDeliveryDuringAndAfterAPausedWrite()
    {
        QTemporaryDir directory; StoreAuthority owner(directory.path());
        const auto old = owner.capture(); QSemaphore written, finish;
        std::atomic_bool saved{false};
        const auto path = directory.filePath(QStringLiteral("authority.json"));
        std::thread writer([&] {
            const auto mutation = owner.mutation();
            saved.store(write(path, "{\"enabled\":false}"), std::memory_order_release);
            written.release(); finish.acquire();
        });
        written.acquire();
        const auto during = owner.capture();
        bool delivered = true;
        QMetaObject::invokeMethod(this, [&] { delivered = old.current(); }, Qt::QueuedConnection);
        QCoreApplication::sendPostedEvents(this);
        const bool blocked = !delivered && !during.current();
        finish.release(); writer.join();
        QVERIFY(saved.load(std::memory_order_acquire)); QVERIFY(blocked);
        QVERIFY(!old.current()); QVERIFY(!during.current()); QVERIFY(owner.capture().current());
    }
    void failedWriteAndNestedGatesCannotReviveACapture()
    {
        QTemporaryDir directory; StoreAuthority owner(directory.path());
        const auto old = owner.capture(); StoreAuthorityToken during, nested;
        {
            const auto outer = owner.mutation(); during = owner.capture();
            {
                const auto inner = owner.mutation(); nested = owner.capture();
                QVERIFY(!write(directory.path(), "cannot replace a directory"));
            }
            QVERIFY(!old.current()); QVERIFY(!during.current()); QVERIFY(!nested.current());
        }
        QVERIFY(!old.current()); QVERIFY(!during.current()); QVERIFY(!nested.current());
        QVERIFY(owner.capture().current());
    }
};
QTEST_GUILESS_MAIN(TestAddonStoreAuthority)
#include "test_addon_store_authority.moc"
