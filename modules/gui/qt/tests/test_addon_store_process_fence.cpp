// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include "addons/addon_store_process_fence.hpp"
#include "addons/addon_store_authority.hpp"
#ifdef Q_OS_UNIX
# include <sys/stat.h>
# include <unistd.h>
#endif

using namespace vlc::addons;
using Fence = StoreProcessFence;
namespace {
struct Child {
    QProcess process;
    ~Child() {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            if (!process.waitForFinished(3000)) qFatal("The fence test child did not stop.");
        }
    }
    QByteArray start(const QString& file, Fence::Mode mode) {
        return start(QStringList{QStringLiteral("--hakone-fence-child"), file,
            mode == Fence::Mode::Peer ? QStringLiteral("peer") : QStringLiteral("replacement")});
    }
    QByteArray start(const QStringList& arguments) {
        process.start(QCoreApplication::applicationFilePath(), arguments);
        if (!process.waitForStarted(3000)) return {};
        QElapsedTimer elapsed; elapsed.start();
        while (!process.canReadLine() && elapsed.elapsed() < 3000) {
            process.waitForReadyRead(100);
            if (process.state() == QProcess::NotRunning && !process.canReadLine()) return {};
        }
        return process.readLine().trimmed();
    }
    bool release() {
        if (process.state() != QProcess::NotRunning) {
            if (process.write("release\n") != 8 || !process.waitForFinished(3000)) return false;
        }
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0
            && process.readAllStandardError().isEmpty();
    }
};
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
int child(const QString& path, Fence::Mode mode)
{
    Fence::Status status; QString error;
    const auto lease = Fence::acquire(path, mode, &status, &error);
    std::puts(status == Fence::Status::Acquired ? "acquired" : status == Fence::Status::Busy ? "busy" : "error");
    std::fflush(stdout);
    if (lease && std::getc(stdin) == EOF) return 2;
    return status == Fence::Status::Error ? 1 : 0;
}
}
class TestAddonStoreProcessFence : public QObject {
    Q_OBJECT
private slots:
    void replacementExcludesOtherProcesses_data() {
        QTest::addColumn<bool>("peer"); QTest::newRow("peer") << true; QTest::newRow("replacement") << false;
    }
    void replacementExcludesOtherProcesses() {
        QFETCH(bool, peer); QTemporaryDir root; const auto file = root.filePath(QStringLiteral("authority.fence"));
        Fence::Status status; QString error;
        auto owner = Fence::acquire(file, Fence::Mode::Replacement, &status, &error);
        QVERIFY2(owner, qPrintable(error)); QCOMPARE(status, Fence::Status::Acquired);
        const auto mode = peer ? Fence::Mode::Peer : Fence::Mode::Replacement;
        Child blocked; QCOMPARE(blocked.start(file, mode), QByteArray("busy")); QVERIFY(blocked.release());
        owner.reset();
        Child admitted; QCOMPARE(admitted.start(file, mode), QByteArray("acquired")); QVERIFY(admitted.release());
    }
    void peerProcessesShareTheFenceUntilTheLastPeerLeaves() {
        QTemporaryDir root; const auto file = root.filePath(QStringLiteral("authority.fence"));
        Child first, second; QCOMPARE(first.start(file, Fence::Mode::Peer), QByteArray("acquired"));
        QCOMPARE(second.start(file, Fence::Mode::Peer), QByteArray("acquired"));
        Fence::Status status; QString error = QStringLiteral("old error");
        QElapsedTimer elapsed; elapsed.start();
        QVERIFY(!Fence::acquire(file, Fence::Mode::Replacement, &status, &error));
        QCOMPARE(status, Fence::Status::Busy); QVERIFY(error.isEmpty()); QVERIFY(elapsed.elapsed() < 100);
        QVERIFY(first.release());
        QVERIFY(!Fence::acquire(file, Fence::Mode::Replacement, &status)); QCOMPARE(status, Fence::Status::Busy);
        QVERIFY(second.release());
        const auto owner = Fence::acquire(file, Fence::Mode::Replacement, &status, &error);
        QVERIFY2(owner, qPrintable(error)); QCOMPARE(status, Fence::Status::Acquired);
    }
    void aRetainedNativeCaptureKeepsThePeerFence() {
        QTemporaryDir root; const auto file = root.filePath(QStringLiteral("authority.fence"));
        Fence::Status status;
        auto lease = Fence::acquire(file, Fence::Mode::Peer, &status); QVERIFY(lease);
        std::function<void()> native = [lease] {}; lease.reset();
        Child blocked; QCOMPARE(blocked.start(file, Fence::Mode::Replacement), QByteArray("busy")); QVERIFY(blocked.release());
        std::thread release([capture = std::move(native)]() mutable { capture = {}; }); release.join();
        Child admitted; QCOMPARE(admitted.start(file, Fence::Mode::Replacement), QByteArray("acquired")); QVERIFY(admitted.release());
    }
    void childHelpersDoNotInheritThePeerFence() {
        QTemporaryDir root; const auto file = root.filePath(QStringLiteral("authority.fence"));
        Fence::Status status; auto peer = Fence::acquire(file, Fence::Mode::Peer, &status); QVERIFY(peer);
        Child helper;
        QCOMPARE(helper.start(QStringList{QStringLiteral("--hakone-fence-inheritance-child")}), QByteArray("holding"));
        peer.reset();
        Child replacement; QCOMPARE(replacement.start(file, Fence::Mode::Replacement), QByteArray("acquired"));
        QVERIFY(replacement.release()); QVERIFY(helper.release());
    }
    void aKilledProcessCannotLeaveALockedFence_data() {
        QTest::addColumn<bool>("peer"); QTest::newRow("peer crash") << true; QTest::newRow("replacement crash") << false;
    }
    void aKilledProcessCannotLeaveALockedFence() {
        QFETCH(bool, peer); QTemporaryDir root; const auto file = root.filePath(QStringLiteral("authority.fence"));
        Child crashed; QCOMPARE(crashed.start(file, peer ? Fence::Mode::Peer : Fence::Mode::Replacement), QByteArray("acquired"));
        Fence::Status status;
        QVERIFY(!Fence::acquire(file, Fence::Mode::Replacement, &status)); QCOMPARE(status, Fence::Status::Busy);
        crashed.process.kill(); QVERIFY(crashed.process.waitForFinished(3000));
        std::shared_ptr<const Fence> next;
        QTRY_VERIFY(next = Fence::acquire(file, Fence::Mode::Replacement, &status));
        QCOMPARE(status, Fence::Status::Acquired);
    }
    void replacementWaitsForNativeCancellation() {
        QTemporaryDir root; const auto file = root.filePath(QStringLiteral("authority.fence"));
        Fence::Status status; auto peer = Fence::acquire(file, Fence::Mode::Peer, &status); QVERIFY(peer);
        StoreAuthority authority(root.path()); const auto token = authority.capture();
        const auto gate = std::make_shared<NativeGate>();
        const auto watch = token.onInvalidated([gate] { gate->pause(); }); QVERIFY(bool(watch));
        std::atomic_bool ended{false};
        std::thread retire([&] { authority.invalidate(); peer.reset(); ended.store(true); });
        const auto joined = qScopeGuard([&] { gate->release(); retire.join(); });
        QVERIFY(gate->wait()); QVERIFY(!token.current()); QVERIFY(!ended.load());
        Child blocked; QCOMPARE(blocked.start(file, Fence::Mode::Replacement), QByteArray("busy")); QVERIFY(blocked.release());
        gate->release(); QTRY_VERIFY(ended.load());
        Child admitted; QCOMPARE(admitted.start(file, Fence::Mode::Replacement), QByteArray("acquired")); QVERIFY(admitted.release());
    }
    void differentProfilesHaveIndependentFences() {
        QTemporaryDir root; Fence::Status status;
        const auto first = Fence::acquire(root.filePath(QStringLiteral("first.fence")), Fence::Mode::Replacement, &status); QVERIFY(first);
        Child other; QCOMPARE(other.start(root.filePath(QStringLiteral("second.fence")), Fence::Mode::Replacement), QByteArray("acquired"));
        QVERIFY(other.release());
    }
    void profilePathsCanContainUnicode() {
        QTemporaryDir root; const auto file = root.filePath(QString::fromUtf8("profile-\xce\xbb.fence"));
        Fence::Status status; const auto owner = Fence::acquire(file, Fence::Mode::Replacement, &status); QVERIFY(owner);
        Child peer; QCOMPARE(peer.start(file, Fence::Mode::Peer), QByteArray("busy")); QVERIFY(peer.release());
    }
    void invalidFilesCannotGrantALease_data() {
        QTest::addColumn<QString>("kind");
        for (const auto& name : {"relative", "nul", "too long", "directory", "not empty", "missing parent", "wrong mode"}) QTest::newRow(name) << QString::fromLatin1(name);
    }
    void invalidFilesCannotGrantALease() {
        QFETCH(QString, kind); QTemporaryDir root; QString path = root.filePath(QStringLiteral("authority.fence"));
        auto mode = Fence::Mode::Peer;
        if (kind == QLatin1String("relative")) path = QStringLiteral("authority.fence");
        if (kind == QLatin1String("nul")) path.append(QChar::Null);
        if (kind == QLatin1String("too long")) path.append(QString(32761, QLatin1Char('x')));
        if (kind == QLatin1String("directory")) path = root.path();
        if (kind == QLatin1String("not empty")) { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("x"), qint64(1)); }
        if (kind == QLatin1String("missing parent")) path = root.filePath(QStringLiteral("missing/authority.fence"));
        if (kind == QLatin1String("wrong mode")) mode = static_cast<Fence::Mode>(99);
        Fence::Status status = Fence::Status::Acquired; QString error;
        QVERIFY(!Fence::acquire(path, mode, &status, &error)); QCOMPARE(status, Fence::Status::Error); QVERIFY(!error.isEmpty());
    }
#ifdef Q_OS_UNIX
    void linksAndSpecialFilesCannotGrantALease_data() {
        QTest::addColumn<QString>("kind");
        for (const auto& name : {"symlink", "hard link", "fifo", "public mode"}) QTest::newRow(name) << QString::fromLatin1(name);
    }
    void linksAndSpecialFilesCannotGrantALease() {
        QFETCH(QString, kind); QTemporaryDir root; const auto path = root.filePath(QStringLiteral("authority.fence"));
        const auto native = QFile::encodeName(path); Fence::Status status;
        if (kind == QLatin1String("fifo")) QCOMPARE(mkfifo(native.constData(), 0600), 0);
        else {
            auto lease = Fence::acquire(path, Fence::Mode::Peer, &status); QVERIFY(lease); lease.reset();
            const auto alias = QFile::encodeName(root.filePath(QStringLiteral("alias.fence")));
            if (kind == QLatin1String("symlink")) { QCOMPARE(symlink(native.constData(), alias.constData()), 0); }
            if (kind == QLatin1String("hard link")) QCOMPARE(::link(native.constData(), alias.constData()), 0);
            if (kind == QLatin1String("public mode")) QCOMPARE(chmod(native.constData(), 0644), 0);
            if (kind == QLatin1String("symlink")) {
                QVERIFY(!Fence::acquire(QString::fromLocal8Bit(alias), Fence::Mode::Peer, &status));
                QCOMPARE(status, Fence::Status::Error); return;
            }
        }
        QVERIFY(!Fence::acquire(path, Fence::Mode::Peer, &status)); QCOMPARE(status, Fence::Status::Error);
    }
#endif
};
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    if (arguments.size() == 2 && arguments.at(1) == QLatin1String("--hakone-fence-inheritance-child")) {
        std::puts("holding"); std::fflush(stdout);
        return std::getc(stdin) == EOF ? 2 : 0;
    }
    if (arguments.size() == 4 && arguments.at(1) == QLatin1String("--hakone-fence-child"))
        return child(arguments.at(2), arguments.at(3) == QLatin1String("peer")
            ? Fence::Mode::Peer : Fence::Mode::Replacement);
    TestAddonStoreProcessFence test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_addon_store_process_fence.moc"
