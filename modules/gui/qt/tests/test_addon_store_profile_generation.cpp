// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QtEndian>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include "addons/addon_store_profile_generation.hpp"
#include "addons/addon_store_authority.hpp"

using namespace vlc::addons;
using Generation = StoreProfileGeneration;
using Status = Generation::Status;
namespace {
Generation::Control control() { return {[] { return true; }, Generation::now, Generation::now() + 20 * 1000 * 1000}; }
bool save(const QString& path, const QByteArray& data) {
    QSaveFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}
QByteArray read(const QString& path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
QString stampPath(const QString& root) { return root + QStringLiteral("/hakone-authority.stamp"); }
QString dataPath(const QString& root) { return root + QStringLiteral("/state.data"); }
QByteArray statusName(Status value) {
    switch (value) {
        case Status::Ready: return "ready"; case Status::Busy: return "busy"; case Status::Stale: return "stale";
        case Status::NeedsRecovery: return "recovery"; default: return "error";
    }
}
struct Fixture {
    QTemporaryDir root;
    std::unique_ptr<Generation> owner;
    Generation::Epoch epoch;
    bool valid = false;
    Fixture() {
        Status status; owner = Generation::acquireCoordinator(root.path(), &status);
        if (!owner) return;
        const auto result = owner->recover(control(), [](const auto& context, auto*) { return context.current() && save(dataPath(context.directory()), "old"); });
        epoch = result.epoch; valid = result.status == Status::Ready;
    }
    Generation::Ticket ticket() const { return {epoch, QByteArray(16, 't'), control()}; }
};
struct Child {
    QProcess process;
    ~Child() { if (process.state() != QProcess::NotRunning) { process.kill(); if (!process.waitForFinished(3000)) qFatal("The generation test child did not stop."); } }
    QByteArray start(const QString& root, const QString& operation, const Generation::Epoch& epoch = {}) {
        process.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--hakone-generation-child"), root, operation,
            QString::fromLatin1(epoch.generation.toHex()), QString::number(epoch.revision)});
        if (!process.waitForStarted(3000)) return {};
        QElapsedTimer elapsed; elapsed.start();
        while (!process.canReadLine() && elapsed.elapsed() < 3000) {
            process.waitForReadyRead(100);
            if (process.state() == QProcess::NotRunning && !process.canReadLine()) return {};
        }
        return process.readLine().trimmed();
    }
    bool release() {
        if (process.state() != QProcess::NotRunning && (process.write("release\n") != 8 || !process.waitForFinished(3000))) return false;
        return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 && process.readAllStandardError().isEmpty();
    }
};
Generation::Epoch epochFrom(const QByteArray& line) {
    const auto value = QJsonDocument::fromJson(line).object();
    return {QByteArray::fromHex(value.value(QStringLiteral("generation")).toString().toLatin1()),
        value.value(QStringLiteral("revision")).toString().toULongLong()};
}
int child(const QStringList& arguments) {
    const auto root = arguments.at(2), operation = arguments.at(3);
    const Generation::Epoch expected{QByteArray::fromHex(arguments.at(4).toLatin1()), arguments.at(5).toULongLong()};
    Generation::Result value; std::unique_ptr<Generation> owner;
    if (operation == QLatin1String("prepare")) {
        Status status; auto writer = Generation::prepareWriter(root, control(), &status);
        std::puts(statusName(status).constData()); std::fflush(stdout);
        return writer && std::getc(stdin) == EOF ? 2 : 0;
    } else if (operation == QLatin1String("recover")) {
        Status status; owner = Generation::acquireCoordinator(root, &status);
        value.status = status;
        if (owner) value = owner->recover(control(), [](const auto& context, auto*) { return context.current() && save(dataPath(context.directory()), "recovered"); });
    } else {
        Generation::Ticket ticket{expected, QByteArray(16, 'c'), control()};
        value = Generation::write(root, ticket, [&](const auto& context, auto*) {
            if (!context.current() || !save(dataPath(context.directory()), operation == QLatin1String("pause") ? "partial" : "new")) return Generation::Change::NeedsRecovery;
            if (operation == QLatin1String("pause")) { std::puts("writing"); std::fflush(stdout); if (std::getc(stdin) == EOF) return Generation::Change::NeedsRecovery; }
            return Generation::Change::Applied;
        });
    }
    const auto json = QJsonDocument(QJsonObject{{QStringLiteral("status"), QString::fromLatin1(statusName(value.status))},
        {QStringLiteral("generation"), QString::fromLatin1(value.epoch.generation.toHex())}, {QStringLiteral("revision"), QString::number(value.epoch.revision)}}).toJson(QJsonDocument::Compact);
    std::puts(json.constData()); std::fflush(stdout);
    if (owner && value.status == Status::Ready && std::getc(stdin) == EOF) return 2;
    return 0;
}
}
class TestAddonStoreProfileGeneration : public QObject {
    Q_OBJECT
private slots:
    void invalidPreparationDoesNotKeepALock_data() {
        QTest::addColumn<QString>("kind");
        for (const auto* kind : {"relative path", "missing path", "no current", "no clock", "expired", "too long", "clock error"}) QTest::newRow(kind) << QString::fromLatin1(kind);
    }
    void invalidPreparationDoesNotKeepALock() {
        QFETCH(QString, kind); Fixture fixture; QVERIFY(fixture.valid); Status status; auto request = control(); auto root = fixture.root.path();
        if (kind == QLatin1String("relative path")) root = QStringLiteral("relative");
        if (kind == QLatin1String("missing path")) root += QStringLiteral("/missing");
        if (kind == QLatin1String("no current")) request.current = {};
        if (kind == QLatin1String("no clock")) request.clock = {};
        if (kind == QLatin1String("expired")) request.deadline = Generation::now() - 1;
        if (kind == QLatin1String("too long")) request.deadline = Generation::now() + Generation::MaximumDuration * 2;
        if (kind == QLatin1String("clock error")) request.clock = []() -> qint64 { throw std::runtime_error("unit"); };
        const auto before = read(stampPath(fixture.root.path()));
        QVERIFY(!Generation::prepareWriter(root, request, &status)); QCOMPARE(status, kind == QLatin1String("expired") ? Status::Stale : Status::Error);
        QLockFile lock(fixture.root.filePath(QStringLiteral("store.lock"))); QVERIFY(lock.tryLock(0)); lock.unlock();
        QCOMPARE(read(stampPath(fixture.root.path())), before);
    }
    void preparationHoldsTheStoreWhilePeersCanJoin() {
        Fixture fixture; QVERIFY(fixture.valid); Status status;
        const auto before = read(stampPath(fixture.root.path()));
        auto oldPeer = Generation::prepareJoin(fixture.root.path(), fixture.epoch, control(), &status); QVERIFY(oldPeer.fence);
        auto writer = Generation::prepareWriter(fixture.root.path(), control(), &status); QVERIFY(writer); QCOMPARE(status, Status::Ready); QVERIFY(writer->current());
        QLockFile lock(fixture.root.filePath(QStringLiteral("store.lock"))); QVERIFY(!lock.tryLock(0));
        auto newPeer = Generation::prepareJoin(fixture.root.path(), fixture.epoch, control(), &status); QVERIFY(newPeer.fence); QCOMPARE(status, Status::Ready);
        QVERIFY(!Generation::prepareWriter(fixture.root.path(), control(), &status)); QCOMPARE(status, Status::Busy);
        bool called = false;
        QCOMPARE(fixture.owner->recover(control(), [&](const auto&, auto*) { called = true; return true; }).status, Status::Busy); QVERIFY(!called);
        Child remote; QCOMPARE(remote.start(fixture.root.path(), QStringLiteral("prepare")), QByteArray("busy")); QVERIFY(remote.release());
        QCOMPARE(read(stampPath(fixture.root.path())), before);
        writer.reset(); QVERIFY(lock.tryLock(0)); lock.unlock();
        QCOMPARE(read(stampPath(fixture.root.path())), before); QCOMPARE(read(dataPath(fixture.root.path())), QByteArray("old"));
    }
    void preparedCommitReleasesLocksBeforeItReturns() {
        Fixture fixture; QVERIFY(fixture.valid); Status status; const auto ticket = fixture.ticket();
        auto writer = Generation::prepareWriter(fixture.root.path(), ticket.control, &status); QVERIFY(writer);
        bool guarded = false, nestedRefused = false;
        const auto result = writer->write(ticket, [&](const auto& context, auto*) {
            QLockFile lock(fixture.root.filePath(QStringLiteral("store.lock")));
            StoreProcessFence::Status fenceStatus;
            const auto peer = StoreProcessFence::acquire(fixture.root.filePath(QStringLiteral("hakone-authority.fence")), StoreProcessFence::Mode::Peer, &fenceStatus);
            const auto nested = writer->write(ticket, [](const auto&, auto*) { return Generation::Change::Applied; });
            nestedRefused = nested.status == Status::Error && context.current();
            guarded = context.current() && !lock.tryLock(0) && !peer && fenceStatus == StoreProcessFence::Status::Busy;
            return save(dataPath(context.directory()), "prepared") ? Generation::Change::Applied : Generation::Change::NeedsRecovery;
        });
        QCOMPARE(result.status, Status::Ready); QVERIFY(result.applied && guarded && nestedRefused); QVERIFY(!writer->current());
        QCOMPARE(result.epoch.revision, fixture.epoch.revision + 1); QCOMPARE(read(dataPath(fixture.root.path())), QByteArray("prepared"));
        QLockFile lock(fixture.root.filePath(QStringLiteral("store.lock"))); QVERIFY(lock.tryLock(0)); lock.unlock();
        QVERIFY(Generation::prepareJoin(fixture.root.path(), result.epoch, control(), &status).fence);
        bool called = false;
        QCOMPARE(writer->write(ticket, [&](const auto&, auto*) { called = true; return Generation::Change::Applied; }).status, Status::Error); QVERIFY(!called);
    }
    void refusedPreparedWritesConsumeTheirScope_data() {
        QTest::addColumn<QString>("kind");
        for (const auto* kind : {"busy fence", "closed preparation", "expired preparation", "clock reversed", "extended ticket", "closed ticket", "expired ticket", "bad request", "old epoch", "no work"})
            QTest::newRow(kind) << QString::fromLatin1(kind);
    }
    void refusedPreparedWritesConsumeTheirScope() {
        QFETCH(QString, kind); Fixture fixture; QVERIFY(fixture.valid); Status status;
        qint64 tick = 100; bool live = true;
        Generation::Control prepared{[&] { return live; }, [&] { return tick; }, 1000};
        auto writer = Generation::prepareWriter(fixture.root.path(), prepared, &status); QVERIFY(writer);
        Generation::Ticket ticket{fixture.epoch, QByteArray(16, 'p'), {[] { return true; }, [&] { return tick; }, 1000}};
        Generation::PreparedPeer peer;
        if (kind == QLatin1String("busy fence")) { peer = Generation::prepareJoin(fixture.root.path(), fixture.epoch, control(), &status); QVERIFY(peer.fence); }
        if (kind == QLatin1String("closed preparation")) live = false;
        if (kind == QLatin1String("expired preparation")) tick = 1000;
        if (kind == QLatin1String("clock reversed")) tick = 99;
        if (kind == QLatin1String("extended ticket")) ticket.control.deadline = 1001;
        if (kind == QLatin1String("closed ticket")) ticket.control.current = [] { return false; };
        if (kind == QLatin1String("expired ticket")) ticket.control.deadline = 100;
        if (kind == QLatin1String("bad request")) ticket.request = {};
        if (kind == QLatin1String("old epoch")) ++ticket.expected.revision;
        const auto before = read(stampPath(fixture.root.path())); bool called = false;
        Generation::Work work = [&](const auto&, auto*) { called = true; return Generation::Change::Applied; };
        if (kind == QLatin1String("no work")) work = {};
        const auto result = writer->write(ticket, std::move(work));
        const auto expected = kind == QLatin1String("busy fence") ? Status::Busy
            : kind == QLatin1String("extended ticket") || kind == QLatin1String("bad request") || kind == QLatin1String("no work") ? Status::Error : Status::Stale;
        QCOMPARE(result.status, expected); QVERIFY(!called && !writer->current());
        QCOMPARE(read(stampPath(fixture.root.path())), before); QCOMPARE(read(dataPath(fixture.root.path())), QByteArray("old"));
        QLockFile lock(fixture.root.filePath(QStringLiteral("store.lock"))); QVERIFY(lock.tryLock(0)); lock.unlock();
        QVERIFY(Generation::prepareJoin(fixture.root.path(), fixture.epoch, control(), &status).fence);
    }
    void aTicketCannotRestartThePreparationBudget() {
        Fixture fixture; QVERIFY(fixture.valid); Status status; qint64 tick = 100;
        Generation::Control prepared{[] { return true; }, [&] { return tick; }, 1000};
        auto writer = Generation::prepareWriter(fixture.root.path(), prepared, &status); QVERIFY(writer);
        tick = 900;
        Generation::Ticket ticket{fixture.epoch, QByteArray(16, 'p'), {[] { return true; }, [&] { return tick; }, 1000}};
        bool expired = false;
        const auto result = writer->write(ticket, [&](const auto& context, auto*) { tick = 1000; expired = !context.current(); return Generation::Change::Applied; });
        QCOMPARE(result.status, Status::NeedsRecovery); QVERIFY(expired && !writer->current());
        QLockFile lock(fixture.root.filePath(QStringLiteral("store.lock"))); QVERIFY(lock.tryLock(0)); lock.unlock();
        QCOMPARE(fixture.owner->recover(control(), [](const auto&, auto*) { return true; }).status, Status::Ready);
    }
    void callbackCanRetireItsPreparedWriter() {
        Fixture fixture; QVERIFY(fixture.valid); Status status; const auto ticket = fixture.ticket();
        auto writer = Generation::prepareWriter(fixture.root.path(), ticket.control, &status); QVERIFY(writer);
        bool closed = false, held = false;
        const auto result = writer->write(ticket, [&](const auto& context, auto*) {
            writer.reset(); closed = !context.current();
            QLockFile lock(fixture.root.filePath(QStringLiteral("store.lock")));
            held = !lock.tryLock(0) && fixture.owner->recover(control(), [](const auto&, auto*) { return true; }).status == Status::Busy;
            return Generation::Change::Applied;
        });
        QCOMPARE(result.status, Status::NeedsRecovery); QVERIFY(closed && held && !writer);
        QLockFile lock(fixture.root.filePath(QStringLiteral("store.lock"))); QVERIFY(lock.tryLock(0)); lock.unlock();
        QCOMPARE(fixture.owner->recover(control(), [](const auto&, auto*) { return true; }).status, Status::Ready);
    }
    void releaseNoticeCanCloseTheWriter_data() {
        QTest::addColumn<bool>("retire"); QTest::newRow("retire writer") << true; QTest::newRow("cancel control") << false;
    }
    void releaseNoticeCanCloseTheWriter() {
        QFETCH(bool, retire); Fixture fixture; QVERIFY(fixture.valid); Status status;
        bool live = true, noticed = false; auto ticket = fixture.ticket(); ticket.control.current = [&] { return live; };
        auto writer = Generation::prepareWriter(fixture.root.path(), ticket.control, &status); QVERIFY(writer);
        StoreAuthority authority(fixture.root.path());
        const auto watch = authority.onChanged([&] { noticed = true; if (retire) writer.reset(); else live = false; }); QVERIFY(watch);
        const auto result = writer->write(ticket, [](const auto&, auto*) { return Generation::Change::Applied; });
        QCOMPARE(result.status, Status::NeedsRecovery); QVERIFY(noticed); QVERIFY(retire ? !writer : !writer->current());
        QLockFile lock(fixture.root.filePath(QStringLiteral("store.lock"))); QVERIFY(lock.tryLock(0)); lock.unlock();
        const Generation::Epoch next{fixture.epoch.generation, fixture.epoch.revision + 1};
        QVERIFY(Generation::prepareJoin(fixture.root.path(), next, control(), &status).fence);
    }
    void controlCheckCanRetireItsWriter_data() {
        QTest::addColumn<bool>("afterRelease"); QTest::newRow("callback") << false; QTest::newRow("after release") << true;
    }
    void controlCheckCanRetireItsWriter() {
        QFETCH(bool, afterRelease); Fixture fixture; QVERIFY(fixture.valid); Status status;
        bool armed = false, revoked = false; std::unique_ptr<Generation::Writer> writer;
        auto ticket = fixture.ticket(); ticket.control.current = [&] { if (armed) writer.reset(); return true; };
        writer = Generation::prepareWriter(fixture.root.path(), ticket.control, &status); QVERIFY(writer);
        StoreAuthority authority(fixture.root.path());
        const auto watch = authority.onChanged([&] { if (afterRelease) armed = true; }); QVERIFY(watch);
        const auto result = writer->write(ticket, [&](const auto& context, auto*) {
            if (!afterRelease) { armed = true; revoked = !context.current(); }
            return Generation::Change::Applied;
        });
        QCOMPARE(result.status, Status::NeedsRecovery); QVERIFY(!writer); QVERIFY(afterRelease || revoked);
        QLockFile lock(fixture.root.filePath(QStringLiteral("store.lock"))); QVERIFY(lock.tryLock(0)); lock.unlock();
        const Generation::Epoch next{fixture.epoch.generation, fixture.epoch.revision + 1};
        const auto peer = Generation::prepareJoin(fixture.root.path(), next, control(), &status);
        QCOMPARE(bool(peer.fence), afterRelease); QCOMPARE(status, afterRelease ? Status::Ready : Status::NeedsRecovery);
    }
    void aCrashedPreparationCannotStrandTheStore() {
        Fixture fixture; QVERIFY(fixture.valid); const auto before = read(stampPath(fixture.root.path()));
        Child remote; QCOMPARE(remote.start(fixture.root.path(), QStringLiteral("prepare")), QByteArray("ready"));
        fixture.owner.reset(); Status status; fixture.owner = Generation::acquireCoordinator(fixture.root.path(), &status); QVERIFY(fixture.owner);
        bool called = false;
        QCOMPARE(fixture.owner->recover(control(), [&](const auto&, auto*) { called = true; return true; }).status, Status::Busy); QVERIFY(!called);
        remote.process.kill(); QVERIFY(remote.process.waitForFinished(3000));
        QCOMPARE(read(stampPath(fixture.root.path())), before); QCOMPARE(read(dataPath(fixture.root.path())), QByteArray("old"));
        const auto recovered = fixture.owner->recover(control(), [](const auto&, auto*) { return true; });
        QCOMPARE(recovered.status, Status::Ready); QVERIFY(recovered.epoch.generation != fixture.epoch.generation);
    }
    void recoveryOwnsTheStoreAndExclusiveFence() {
        QTemporaryDir root; Status status; auto owner = Generation::acquireCoordinator(root.path(), &status); QVERIFY(owner);
        bool guarded = false, closed = false;
        const auto result = owner->recover(control(), [&](const auto& context, auto*) {
            QLockFile probe(root.filePath(QStringLiteral("store.lock")));
            StoreProcessFence::Status fenceStatus;
            const auto peer = StoreProcessFence::acquire(root.filePath(QStringLiteral("hakone-authority.fence")), StoreProcessFence::Mode::Peer, &fenceStatus);
            guarded = context.current() && context.directory() == root.path() && !probe.tryLock(0) && !peer && fenceStatus == StoreProcessFence::Status::Busy;
            const auto bytes = read(stampPath(root.path())); closed = bytes.size() == 88 && quint8(bytes.at(8)) == 2;
            return guarded && closed;
        });
        QCOMPARE(result.status, Status::Ready); QVERIFY(guarded && closed); QCOMPARE(result.epoch.generation.size(), 16); QCOMPARE(result.epoch.revision, quint64(0));
        const auto peer = Generation::prepareJoin(root.path(), result.epoch, control(), &status); QVERIFY(peer.fence); QCOMPARE(status, Status::Ready);
        QCOMPARE(read(stampPath(root.path())).size(), qsizetype(88));
    }
    void commitAndRollbackAdvanceTheRevision_data() { QTest::addColumn<bool>("apply"); QTest::newRow("commit") << true; QTest::newRow("rollback") << false; }
    void commitAndRollbackAdvanceTheRevision() {
        QFETCH(bool, apply); Fixture fixture; QVERIFY(fixture.valid); bool writing = false;
        const auto result = Generation::write(fixture.root.path(), fixture.ticket(), [&](const auto& context, auto* error) {
            const auto bytes = read(stampPath(context.directory())); writing = context.current() && bytes.size() == 88 && quint8(bytes.at(8)) == 1;
            if (apply && !save(dataPath(context.directory()), "new")) return Generation::Change::NeedsRecovery;
            if (!apply) *error = QStringLiteral("The change did not complete.");
            return apply ? Generation::Change::Applied : Generation::Change::RolledBack;
        });
        QCOMPARE(result.status, Status::Ready); QCOMPARE(result.applied, apply); QVERIFY(writing);
        QCOMPARE(result.error, apply ? QString{} : QStringLiteral("The change did not complete."));
        QCOMPARE(result.epoch.generation, fixture.epoch.generation); QCOMPARE(result.epoch.revision, quint64(1));
        QCOMPARE(read(dataPath(fixture.root.path())), apply ? QByteArray("new") : QByteArray("old"));
        bool called = false; QCOMPARE(Generation::write(fixture.root.path(), fixture.ticket(), [&](const auto&, auto*) { called = true; return Generation::Change::Applied; }).status, Status::Stale); QVERIFY(!called);
    }
    void aPeerFenceBlocksPublicationAndReplacement() {
        Fixture fixture; QVERIFY(fixture.valid); Status status;
        auto peer = Generation::prepareJoin(fixture.root.path(), fixture.epoch, control(), &status); QVERIFY(peer.fence);
        bool called = false;
        const auto write = [&](const auto&, auto*) { called = true; return Generation::Change::Applied; };
        QCOMPARE(Generation::write(fixture.root.path(), fixture.ticket(), write).status, Status::Busy); QVERIFY(!called);
        const auto recovery = [&](const auto&, auto*) { called = true; return true; };
        QCOMPARE(fixture.owner->recover(control(), recovery).status, Status::Busy); QVERIFY(!called);
        Child remote; QCOMPARE(QJsonDocument::fromJson(remote.start(fixture.root.path(), QStringLiteral("write"), fixture.epoch)).object().value(QStringLiteral("status")).toString(), QStringLiteral("busy")); QVERIFY(remote.release());
        peer.fence.reset(); QCOMPARE(Generation::write(fixture.root.path(), fixture.ticket(), write).status, Status::Ready); QVERIFY(called);
    }
    void aTicketCannotCrossCoordinatorReplacement() {
        Fixture fixture; QVERIFY(fixture.valid); const auto old = fixture.ticket(); fixture.owner.reset();
        Child replacement; const auto line = replacement.start(fixture.root.path(), QStringLiteral("recover")); const auto current = epochFrom(line);
        QVERIFY(current.generation.size() == 16 && current.generation != old.expected.generation);
        bool called = false; QCOMPARE(Generation::write(fixture.root.path(), old, [&](const auto&, auto*) { called = true; return Generation::Change::Applied; }).status, Status::Stale); QVERIFY(!called);
        Status status; QVERIFY(!Generation::prepareJoin(fixture.root.path(), old.expected, control(), &status).fence); QCOMPARE(status, Status::Stale);
        auto peer = Generation::prepareJoin(fixture.root.path(), current, control(), &status); QVERIFY(peer.fence);
        QVERIFY(replacement.release()); auto owner = Generation::acquireCoordinator(fixture.root.path(), &status); QVERIFY(owner);
        QCOMPARE(owner->recover(control(), [](const auto&, auto*) { return true; }).status, Status::Busy);
        peer.fence.reset(); const auto recovered = owner->recover(control(), [](const auto&, auto*) { return true; }); QCOMPARE(recovered.status, Status::Ready); QVERIFY(recovered.epoch.generation != current.generation);
    }
    void aCrashedWriterNeedsRecoveryBeforeAJoin() {
        Fixture fixture; QVERIFY(fixture.valid); Child writer; QCOMPARE(writer.start(fixture.root.path(), QStringLiteral("pause"), fixture.epoch), QByteArray("writing"));
        QCOMPARE(read(dataPath(fixture.root.path())), QByteArray("partial"));
        fixture.owner.reset(); Status status; fixture.owner = Generation::acquireCoordinator(fixture.root.path(), &status); QVERIFY(fixture.owner);
        bool called = false; QCOMPARE(fixture.owner->recover(control(), [&](const auto&, auto*) { called = true; return true; }).status, Status::Busy); QVERIFY(!called);
        writer.process.kill(); QVERIFY(writer.process.waitForFinished(3000));
        QTRY_COMPARE((Generation::prepareJoin(fixture.root.path(), fixture.epoch, control(), &status), status), Status::NeedsRecovery);
        const auto recovered = fixture.owner->recover(control(), [&](const auto& context, auto*) { called = true; return context.current() && save(dataPath(context.directory()), "old"); });
        QCOMPARE(recovered.status, Status::Ready); QVERIFY(called); QVERIFY(recovered.epoch.generation != fixture.epoch.generation);
        QCOMPARE(read(dataPath(fixture.root.path())), QByteArray("old")); QVERIFY(Generation::prepareJoin(fixture.root.path(), recovered.epoch, control(), &status).fence);
    }
    void aSecondCoordinatorCannotTakeTheOwnerLock() {
        Fixture fixture; QVERIFY(fixture.valid); Status status;
        QVERIFY(!Generation::acquireCoordinator(fixture.root.path() + QStringLiteral("/."), &status)); QCOMPARE(status, Status::Busy);
        Child remote; const auto reply = QJsonDocument::fromJson(remote.start(fixture.root.path(), QStringLiteral("recover"))).object(); QCOMPARE(reply.value(QStringLiteral("status")).toString(), QStringLiteral("busy")); QVERIFY(remote.release());
    }
    void recoveryCallbackCanRetireTheCoordinator() {
        Fixture fixture; QVERIFY(fixture.valid); bool closed = false, retained = false;
        const auto result = fixture.owner->recover(control(), [&](const auto& context, auto*) {
            fixture.owner.reset(); closed = !context.current(); Status status;
            retained = !Generation::acquireCoordinator(fixture.root.path(), &status) && status == Status::Busy;
            return true;
        });
        QCOMPARE(result.status, Status::NeedsRecovery); QVERIFY(closed && retained); QVERIFY(!fixture.owner);
        Status status; auto owner = Generation::acquireCoordinator(fixture.root.path(), &status); QVERIFY(owner);
        QCOMPARE(owner->recover(control(), [](const auto&, auto*) { return true; }).status, Status::Ready);
    }
    void aRemoteWriterKeepsAllRevisionBits() {
        Fixture fixture; QVERIFY(fixture.valid);
        auto expected = fixture.epoch; expected.revision = (quint64(1) << 63) + 17;
        auto bytes = read(stampPath(fixture.root.path())); QCOMPARE(bytes.size(), qsizetype(88));
        qToBigEndian<quint64>(expected.revision, bytes.data() + 32);
        bytes.replace(56, 32, QCryptographicHash::hash(bytes.first(56), QCryptographicHash::Sha256));
        QVERIFY(save(stampPath(fixture.root.path()), bytes));
        Child writer; const auto reply = writer.start(fixture.root.path(), QStringLiteral("write"), expected);
        const auto saved = epochFrom(reply); QCOMPARE(saved.generation, expected.generation); QCOMPARE(saved.revision, expected.revision + 1);
        QCOMPARE(QJsonDocument::fromJson(reply).object().value(QStringLiteral("status")).toString(), QStringLiteral("ready")); QVERIFY(writer.release());
        bytes = read(stampPath(fixture.root.path())); QCOMPARE(bytes.size(), qsizetype(88));
        QCOMPARE(qFromBigEndian<quint64>(bytes.constData() + 32), expected.revision + 1);
    }
    void invalidTicketsDoNotCallTheStore_data() {
        QTest::addColumn<QString>("kind");
        for (const auto* kind : {"generation", "revision", "zero generation", "zero request", "expired", "too long", "no current", "closed", "no clock", "clock error", "limit"}) QTest::newRow(kind) << QString::fromLatin1(kind);
    }
    void invalidTicketsDoNotCallTheStore() {
        QFETCH(QString, kind); Fixture fixture; QVERIFY(fixture.valid); auto ticket = fixture.ticket();
        if (kind == QLatin1String("generation")) ticket.expected.generation[0] = char(ticket.expected.generation.at(0) ^ 1);
        if (kind == QLatin1String("revision")) ++ticket.expected.revision;
        if (kind == QLatin1String("zero generation")) ticket.expected.generation = QByteArray(16, '\0');
        if (kind == QLatin1String("zero request")) ticket.request = QByteArray(16, '\0');
        if (kind == QLatin1String("expired")) ticket.control.deadline = Generation::now() - 1;
        if (kind == QLatin1String("too long")) ticket.control.deadline = Generation::now() + Generation::MaximumDuration * 2;
        if (kind == QLatin1String("no current")) ticket.control.current = {};
        if (kind == QLatin1String("closed")) ticket.control.current = [] { return false; };
        if (kind == QLatin1String("no clock")) ticket.control.clock = {};
        if (kind == QLatin1String("clock error")) ticket.control.clock = []() -> qint64 { throw std::runtime_error("unit"); };
        if (kind == QLatin1String("limit")) ticket.expected.revision = std::numeric_limits<quint64>::max();
        const auto before = read(stampPath(fixture.root.path())); bool called = false;
        const auto result = Generation::write(fixture.root.path(), ticket, [&](const auto&, auto*) { called = true; return Generation::Change::Applied; });
        const auto expected = kind == QLatin1String("generation") || kind == QLatin1String("revision")
            || kind == QLatin1String("expired") || kind == QLatin1String("closed") ? Status::Stale : Status::Error;
        QCOMPARE(result.status, expected); QVERIFY(!called); QCOMPARE(read(stampPath(fixture.root.path())), before);
    }
    void unfinishedWorkLeavesAdmissionClosed_data() {
        QTest::addColumn<int>("kind"); QTest::newRow("recovery needed") << 0; QTest::newRow("exception") << 1; QTest::newRow("cancel after write") << 2; QTest::newRow("unknown outcome") << 3;
    }
    void unfinishedWorkLeavesAdmissionClosed() {
        QFETCH(int, kind); Fixture fixture; QVERIFY(fixture.valid); auto ticket = fixture.ticket(); bool live = true; ticket.control.current = [&] { return live; };
        const auto result = Generation::write(fixture.root.path(), ticket, [&](const auto& context, auto*) {
            if (!context.current()) return Generation::Change::NeedsRecovery;
            if (kind == 1) throw std::runtime_error("unit secret");
            if (kind == 2) { save(dataPath(context.directory()), "new"); live = false; return Generation::Change::Applied; }
            return kind == 3 ? static_cast<Generation::Change>(99) : Generation::Change::NeedsRecovery;
        });
        QCOMPARE(result.status, Status::NeedsRecovery); QVERIFY(!result.error.contains(QStringLiteral("secret")));
        Status status; const Generation::Epoch next{fixture.epoch.generation, fixture.epoch.revision + 1};
        QVERIFY(!Generation::prepareJoin(fixture.root.path(), next, control(), &status).fence); QCOMPARE(status, Status::NeedsRecovery);
        const auto recovered = fixture.owner->recover(control(), [](const auto& context, auto*) { return context.current() && save(dataPath(context.directory()), "old"); });
        QCOMPARE(recovered.status, Status::Ready);
    }
    void corruptRecordsCannotGrantAJoin_data() {
        QTest::addColumn<QString>("kind");
        for (const auto* kind : {"magic", "phase", "reserved", "zero generation", "ready request", "checksum", "short", "extra"}) QTest::newRow(kind) << QString::fromLatin1(kind);
    }
    void corruptRecordsCannotGrantAJoin() {
        QFETCH(QString, kind); Fixture fixture; QVERIFY(fixture.valid); auto bytes = read(stampPath(fixture.root.path())); QCOMPARE(bytes.size(), qsizetype(88));
        if (kind == QLatin1String("magic")) bytes[0] = 'x';
        if (kind == QLatin1String("phase")) bytes[8] = 9;
        if (kind == QLatin1String("reserved")) bytes[9] = 1;
        if (kind == QLatin1String("zero generation")) bytes.replace(16, 16, QByteArray(16, '\0'));
        if (kind == QLatin1String("ready request")) bytes[40] = 1;
        if (kind == QLatin1String("checksum")) bytes[56] = char(bytes.at(56) ^ 1);
        else bytes.replace(56, 32, QCryptographicHash::hash(bytes.first(56), QCryptographicHash::Sha256));
        if (kind == QLatin1String("short")) bytes.chop(1);
        if (kind == QLatin1String("extra")) bytes.append('x');
        QVERIFY(save(stampPath(fixture.root.path()), bytes)); Status status;
        QVERIFY(!Generation::prepareJoin(fixture.root.path(), fixture.epoch, control(), &status).fence); QCOMPARE(status, Status::NeedsRecovery);
        bool called = false; QCOMPARE(fixture.owner->recover(control(), [&](const auto&, auto*) { called = true; return true; }).status, Status::NeedsRecovery); QVERIFY(!called);
        QCOMPARE(read(stampPath(fixture.root.path())), bytes);
    }
};
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv); const auto arguments = app.arguments();
    if (arguments.size() == 6 && arguments.at(1) == QLatin1String("--hakone-generation-child")) return child(arguments);
    TestAddonStoreProfileGeneration test; return QTest::qExec(&test, argc, argv);
}
#include "test_addon_store_profile_generation.moc"
