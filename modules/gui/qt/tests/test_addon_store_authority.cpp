// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QSemaphore>
#include <atomic>
#include <thread>
#include "addons/addon_store_authority.hpp"

using namespace vlc::addons;
class TestAddonStoreAuthority : public QObject
{
    Q_OBJECT
    static bool write(const QString& path, const QByteArray& bytes) {
        QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
private slots:
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
