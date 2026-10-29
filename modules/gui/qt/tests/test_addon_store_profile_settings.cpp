// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_package_store.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <cstdio>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

using namespace vlc::addons;
using Generation = StoreProfileGeneration;
using Change = Generation::Change;
using Status = Generation::Status;
namespace {
enum Operation { Order, Builtin, SafeMode, Distribution };
const auto provider = QStringLiteral("org.hakone.example/search");
const auto builtin = QStringLiteral("hakone.builtin.example");
const auto package = QStringLiteral("org.hakone.example");
Generation::Control control() {
    return {[] { return true; }, Generation::now, Generation::now() + 20 * 1000 * 1000};
}
QString name(int operation) {
    if (operation == SafeMode) return QStringLiteral("recovery.json");
    if (operation == Distribution) return QStringLiteral("distribution-policy.json");
    return QStringLiteral("provider-order.json");
}
QByteArray read(const QString& path) {
    QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
bool save(const QString& path, const QByteArray& bytes) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
Change apply(PackageStore& store, const StoreProfileWriteContext& context, int operation, QString* error) {
    switch (operation) {
        case Order: return store.saveProviderOrder(context, QStringLiteral("subtitles"), {provider}, nullptr, error);
        case Builtin: return store.saveBuiltinProviderEnabled(context, builtin, false, nullptr, error);
        case SafeMode: return store.setSafeMode(context, true, error);
        default: return store.setDistributionSuppressed(context, package, true, error);
    }
}
struct Fixture {
    QTemporaryDir root;
    std::unique_ptr<PackageStore> store;
    std::unique_ptr<Generation> owner;
    Generation::Epoch epoch;
    bool valid = false;
    Fixture() : store(std::make_unique<PackageStore>(root.path())) {
        if (!store->saveProviderOrder(QStringLiteral("artwork"), {QStringLiteral("org.hakone.artwork/image")})
            || !store->saveBuiltinProviderEnabled(builtin, true) || !store->setSafeMode(false)
            || !store->setDistributionSuppressed(package, false)) return;
        Status status; owner = Generation::acquireCoordinator(root.path(), &status);
        if (!owner) return;
        // This empty fixture has only these three documents. This is not the
        // production recovery validator for installed package state.
        const auto recovered = owner->recover(control(), [&](const auto& context, auto*) {
            if (!context.current()) return false;
            for (const auto& file : {name(Order), name(SafeMode), name(Distribution)})
                if (!QJsonDocument::fromJson(read(root.filePath(file))).isObject()) return false;
            return true;
        });
        epoch = recovered.epoch; valid = recovered.status == Status::Ready;
    }
    Generation::Ticket ticket() const { return {epoch, QByteArray(16, 't'), control()}; }
    Generation::Result write(int operation) {
        return Generation::write(root.path(), ticket(), [&](const auto& context, auto* error) {
            return apply(*store, context, operation, error);
        });
    }
};
void operations() {
    QTest::addColumn<int>("operation");
    QTest::newRow("order") << int(Order); QTest::newRow("builtin") << int(Builtin);
    QTest::newRow("safe-mode") << int(SafeMode); QTest::newRow("distribution") << int(Distribution);
}
QJsonObject remoteWrite(const Fixture& fixture) {
    QProcess process;
    process.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--hakone-settings-child"), fixture.root.path(),
        QString::fromLatin1(fixture.epoch.generation.toHex()), QString::number(fixture.epoch.revision)});
    if (!process.waitForStarted(3000) || !process.waitForFinished(3000)) {
        process.kill(); process.waitForFinished(3000); return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || !process.readAllStandardError().isEmpty()) return {};
    return QJsonDocument::fromJson(process.readAllStandardOutput()).object();
}
}
class TestAddonStoreProfileSettings : public QObject {
    Q_OBJECT
private slots:
    void guardedWritesCommitAndKeepOtherSettings_data() { operations(); }
    void guardedWritesCommitAndKeepOtherSettings() {
        QFETCH(int, operation); Fixture fixture; QVERIFY(fixture.valid);
        const auto oldToken = fixture.store->authorityToken(); std::atomic_uint cancelled{0};
        const auto watch = oldToken.onInvalidated([&] { ++cancelled; }); QVERIFY(watch);
        const auto result = fixture.write(operation);
        QCOMPARE(result.status, Status::Ready); QVERIFY(result.applied);
        QCOMPARE(result.epoch.generation, fixture.epoch.generation); QCOMPARE(result.epoch.revision, quint64(1));
        QCOMPARE(cancelled.load(), 1u); QVERIFY(!oldToken.current()); QVERIFY(!watch);
        ProviderPreferences preferences; QVERIFY(fixture.store->providerPreferences(&preferences));
        QVERIFY(preferences.authority.current());
        QCOMPARE(preferences.values.value(QStringLiteral("artwork")).toArray(), QJsonArray{QStringLiteral("org.hakone.artwork/image")});
        if (operation == Order) QCOMPARE(preferences.values.value(QStringLiteral("subtitles")).toArray(), QJsonArray{provider});
        QCOMPARE(preferences.values.value(QStringLiteral("_builtinEnabled")).toObject().value(builtin).toBool(), operation != Builtin);
        QCOMPARE(fixture.store->safeMode(), operation == SafeMode);
        const auto policy = QJsonDocument::fromJson(read(fixture.root.filePath(name(Distribution)))).object();
        QCOMPARE(policy.value(QStringLiteral("formatVersion")).toInt(), 1);
        QCOMPARE(policy.value(QStringLiteral("removed")).toObject().value(package).toBool(), operation == Distribution);
        Status status; QVERIFY(Generation::prepareJoin(fixture.root.path(), result.epoch, control(), &status).fence);
    }
    void aContextCannotWriteAnotherProfile_data() { operations(); }
    void aContextCannotWriteAnotherProfile() {
        QFETCH(int, operation); Fixture fixture, other; QVERIFY(fixture.valid && other.valid);
        const auto before = read(other.root.filePath(name(operation))); const auto token = other.store->authorityToken();
        const auto result = Generation::write(fixture.root.path(), fixture.ticket(), [&](const auto& context, auto* error) {
            return apply(*other.store, context, operation, error);
        });
        QCOMPARE(result.status, Status::Ready); QVERIFY(!result.applied); QVERIFY(!result.error.isEmpty());
        QCOMPARE(read(other.root.filePath(name(operation))), before); QVERIFY(token.current());
    }
    void invalidInputDoesNotInvalidateOrPublish() {
        Fixture fixture; QVERIFY(fixture.valid); const auto before = read(fixture.root.filePath(name(Order)));
        const auto token = fixture.store->authorityToken(); QJsonObject saved{{QStringLiteral("old"), true}};
        const auto result = Generation::write(fixture.root.path(), fixture.ticket(), [&](const auto& context, auto* error) {
            if (fixture.store->saveProviderOrder(context, QStringLiteral("_reserved"), {}, &saved, error) != Change::RolledBack
                || !saved.isEmpty()) return Change::NeedsRecovery;
            if (fixture.store->saveProviderOrder(context, QStringLiteral("subtitles"), {provider, provider}, &saved, error) != Change::RolledBack)
                return Change::NeedsRecovery;
            if (fixture.store->saveBuiltinProviderEnabled(context, package, false, &saved, error) != Change::RolledBack)
                return Change::NeedsRecovery;
            return fixture.store->setDistributionSuppressed(context, QStringLiteral("bad"), true, error);
        });
        QCOMPARE(result.status, Status::Ready); QVERIFY(!result.applied); QVERIFY(!result.error.isEmpty());
        QVERIFY(saved.isEmpty()); QVERIFY(token.current()); QCOMPARE(read(fixture.root.filePath(name(Order))), before);
    }
    void nativeCancellationEndsBeforePublication_data() { operations(); }
    void nativeCancellationEndsBeforePublication() {
        QFETCH(int, operation); Fixture fixture; QVERIFY(fixture.valid);
        const auto before = read(fixture.root.filePath(name(operation)));
        const auto token = fixture.store->authorityToken();
        std::mutex mutex; std::condition_variable changed; bool entered = false, released = false;
        const auto watch = token.onInvalidated([&] {
            std::unique_lock<std::mutex> lock(mutex); entered = true; changed.notify_all();
            changed.wait(lock, [&] { return released; });
        }); QVERIFY(watch);
        Generation::Result result; std::atomic_bool finished{false};
        std::thread writer([&] { result = fixture.write(operation); finished.store(true); });
        const auto cleanup = qScopeGuard([&] {
            { std::lock_guard<std::mutex> lock(mutex); released = true; changed.notify_all(); }
            if (writer.joinable()) writer.join();
        });
        { std::unique_lock<std::mutex> lock(mutex); QVERIFY(changed.wait_for(lock, std::chrono::seconds(3), [&] { return entered; })); }
        QVERIFY(!finished.load()); QVERIFY(!token.current()); QVERIFY(!fixture.store->authorityToken().current());
        QCOMPARE(read(fixture.root.filePath(name(operation))), before);
        QLockFile storeLock(fixture.root.filePath(QStringLiteral("store.lock"))); QVERIFY(!storeLock.tryLock(0));
        StoreProcessFence::Status status;
        QVERIFY(!StoreProcessFence::acquire(fixture.root.filePath(QStringLiteral("hakone-authority.fence")), StoreProcessFence::Mode::Peer, &status));
        QCOMPARE(status, StoreProcessFence::Status::Busy);
        { std::lock_guard<std::mutex> lock(mutex); released = true; changed.notify_all(); }
        writer.join(); QCOMPARE(result.status, Status::Ready); QVERIFY(result.applied);
        QVERIFY(read(fixture.root.filePath(name(operation))) != before);
    }
    void cancellationDuringDrainCannotCommit_data() { operations(); }
    void cancellationDuringDrainCannotCommit() {
        QFETCH(int, operation); Fixture fixture; QVERIFY(fixture.valid);
        const auto before = read(fixture.root.filePath(name(operation))); std::atomic_bool current{true};
        auto ticket = fixture.ticket(); ticket.control.current = [&] { return current.load(); };
        const auto token = fixture.store->authorityToken(); const auto watch = token.onInvalidated([&] { current.store(false); }); QVERIFY(watch);
        const auto result = Generation::write(fixture.root.path(), ticket, [&](const auto& context, auto* error) {
            return apply(*fixture.store, context, operation, error);
        });
        QCOMPARE(result.status, Status::NeedsRecovery); QVERIFY(!result.applied);
        QCOMPARE(read(fixture.root.filePath(name(operation))), before);
        Status status; QVERIFY(!Generation::prepareJoin(fixture.root.path(), fixture.epoch, control(), &status).fence);
        QCOMPARE(status, Status::NeedsRecovery);
    }
    void corruptSettingsLeaveAdmissionClosed_data() {
        QTest::addColumn<int>("operation"); QTest::newRow("provider") << int(Order);
        QTest::newRow("distribution") << int(Distribution);
    }
    void corruptSettingsLeaveAdmissionClosed() {
        QFETCH(int, operation); Fixture fixture; QVERIFY(fixture.valid);
        QVERIFY(save(fixture.root.filePath(name(operation)), "invalid"));
        const auto result = fixture.write(operation); QCOMPARE(result.status, Status::NeedsRecovery); QVERIFY(!result.applied);
        QCOMPARE(read(fixture.root.filePath(name(operation))), QByteArray("invalid"));
        Status status; QVERIFY(!Generation::prepareJoin(fixture.root.path(), fixture.epoch, control(), &status).fence);
        QCOMPARE(status, Status::NeedsRecovery);
    }
    void aFailedPublicationNeedsRecovery_data() { operations(); }
    void aFailedPublicationNeedsRecovery() {
        QFETCH(int, operation); Fixture fixture; QVERIFY(fixture.valid);
        const auto path = fixture.root.filePath(name(operation)); QVERIFY(QFile::remove(path)); QVERIFY(QDir().mkdir(path));
        const auto result = fixture.write(operation); QCOMPARE(result.status, Status::NeedsRecovery); QVERIFY(!result.applied);
        QVERIFY(!result.error.isEmpty()); QVERIFY(QFileInfo(path).isDir());
    }
#ifdef Q_OS_UNIX
    void linkedFilesCannotRedirectAGuardedWrite_data() { operations(); }
    void linkedFilesCannotRedirectAGuardedWrite() {
        QFETCH(int, operation); Fixture fixture; QVERIFY(fixture.valid); QTemporaryDir external;
        const auto target = external.filePath(QStringLiteral("outside.json")); QVERIFY(save(target, "{\"outside\":true}"));
        const auto before = read(target); const auto path = fixture.root.filePath(name(operation));
        QVERIFY(QFile::remove(path)); QVERIFY(QFile::link(target, path));
        const auto result = fixture.write(operation); QCOMPARE(result.status, Status::NeedsRecovery); QVERIFY(!result.applied);
        QVERIFY(QFileInfo(path).isSymLink()); QCOMPARE(read(target), before);
    }
    void distributionPolicyRefusesAFifoBeforeOpeningIt() {
        Fixture fixture; QVERIFY(fixture.valid); const auto path = fixture.root.filePath(name(Distribution));
        QVERIFY(QFile::remove(path)); QCOMPARE(::mkfifo(QFile::encodeName(path).constData(), 0600), 0);
        QElapsedTimer elapsed; elapsed.start(); const auto result = fixture.write(Distribution);
        QCOMPARE(result.status, Status::NeedsRecovery); QVERIFY(!result.applied); QVERIFY(elapsed.elapsed() < 1000);
    }
#endif
    void canonicalAliasUsesTheHeldLock() {
        Fixture fixture; QVERIFY(fixture.valid); PackageStore alias(fixture.root.path() + QStringLiteral("/."));
        const auto token = alias.authorityToken();
        const auto result = Generation::write(fixture.root.path(), fixture.ticket(), [&](const auto& context, auto* error) {
            return alias.saveProviderOrder(context, QStringLiteral("subtitles"), {provider}, nullptr, error);
        });
        QCOMPARE(result.status, Status::Ready); QVERIFY(result.applied); QVERIFY(!token.current());
    }
    void aRemoteStoreWriterRequiresTheExactEpochAndFence() {
        Fixture fixture; QVERIFY(fixture.valid); const auto before = read(fixture.root.filePath(name(Order)));
        Status status; auto peer = Generation::prepareJoin(fixture.root.path(), fixture.epoch, control(), &status); QVERIFY(peer.fence);
        const auto blocked = remoteWrite(fixture); QCOMPARE(blocked.value(QStringLiteral("status")).toInt(), int(Status::Busy));
        QVERIFY(!blocked.value(QStringLiteral("called")).toBool()); QCOMPARE(read(fixture.root.filePath(name(Order))), before);
        peer.fence.reset(); const auto committed = remoteWrite(fixture);
        QCOMPARE(committed.value(QStringLiteral("status")).toInt(), int(Status::Ready)); QVERIFY(committed.value(QStringLiteral("applied")).toBool());
        QCOMPARE(QJsonDocument::fromJson(read(fixture.root.filePath(name(Order)))).object().value(QStringLiteral("subtitles")).toArray(), QJsonArray{provider});
        const auto stale = remoteWrite(fixture); QCOMPARE(stale.value(QStringLiteral("status")).toInt(), int(Status::Stale));
        QVERIFY(!stale.value(QStringLiteral("called")).toBool());
    }
    void refreshRunsAfterStoreUnlockAndBeforeFenceRelease() {
        Fixture fixture; QVERIFY(fixture.valid); std::atomic_bool armed{false};
        std::mutex mutex; std::condition_variable changed; bool entered = false, released = false;
        const auto notices = fixture.store->onDisplayChanged([&] {
            if (!armed.load()) return;
            std::unique_lock<std::mutex> lock(mutex); entered = true; changed.notify_all();
            changed.wait(lock, [&] { return released; });
        }); QVERIFY(notices);
        Generation::Result result;
        std::thread writer([&] {
            result = Generation::write(fixture.root.path(), fixture.ticket(), [&](const auto& context, auto* error) {
                const auto change = apply(*fixture.store, context, Order, error); armed.store(true); return change;
            });
        });
        const auto cleanup = qScopeGuard([&] {
            { std::lock_guard<std::mutex> lock(mutex); released = true; changed.notify_all(); }
            if (writer.joinable()) writer.join();
        });
        { std::unique_lock<std::mutex> lock(mutex); QVERIFY(changed.wait_for(lock, std::chrono::seconds(3), [&] { return entered; })); }
        QLockFile storeLock(fixture.root.filePath(QStringLiteral("store.lock"))); QVERIFY(storeLock.tryLock(0)); storeLock.unlock();
        StoreProcessFence::Status status;
        QVERIFY(!StoreProcessFence::acquire(fixture.root.filePath(QStringLiteral("hakone-authority.fence")), StoreProcessFence::Mode::Peer, &status));
        QCOMPARE(status, StoreProcessFence::Status::Busy);
        { std::lock_guard<std::mutex> lock(mutex); released = true; changed.notify_all(); }
        writer.join(); QCOMPARE(result.status, Status::Ready); QVERIFY(result.applied);
        QVERIFY(StoreProcessFence::acquire(fixture.root.filePath(QStringLiteral("hakone-authority.fence")), StoreProcessFence::Mode::Peer, &status));
    }
};
int main(int argc, char** argv) {
    QCoreApplication application(argc, argv); const auto arguments = application.arguments();
    if (arguments.size() == 5 && arguments.at(1) == QLatin1String("--hakone-settings-child")) {
        PackageStore store(arguments.at(2)); bool called = false;
        Generation::Ticket ticket{{QByteArray::fromHex(arguments.at(3).toLatin1()), arguments.at(4).toULongLong()}, QByteArray(16, 'c'), control()};
        const auto result = Generation::write(arguments.at(2), ticket, [&](const auto& context, auto* error) {
            called = true; return apply(store, context, Order, error);
        });
        const auto bytes = QJsonDocument(QJsonObject{{QStringLiteral("status"), int(result.status)},
            {QStringLiteral("applied"), result.applied}, {QStringLiteral("called"), called}}).toJson(QJsonDocument::Compact);
        std::puts(bytes.constData()); return 0;
    }
    TestAddonStoreProfileSettings test; return QTest::qExec(&test, argc, argv);
}
#include "test_addon_store_profile_settings.moc"
