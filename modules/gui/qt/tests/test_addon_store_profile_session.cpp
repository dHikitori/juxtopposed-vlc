// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_store_profile_session.hpp"
#include <QTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <atomic>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>
using namespace vlc::addons;
using Session = StoreProfileSession;
using Client = StoreProfileClient;
using G = StoreProfileGeneration;
namespace {
void say(const QByteArray& line)
{
    std::fputs(QByteArray(line + '\n').constData(), stdout);
    std::fflush(stdout);
}

// One app process: a session, a native watch on its ready token, and a writer.
int child(const QStringList& args)
{
    const auto profile = args.at(2);
    const auto runtime = args.at(3);
    AddonStreamLifetime lifetime;
    StoreAuthority authority(profile);
    auto peer = authority.attachPeer();
    if (!peer)
        return 2;
    StoreAuthorityWatch watch;
    std::unique_ptr<Session> session;
    Session::Options options;
    options.directory = profile;
    options.unixRuntime = runtime;
    options.work = lifetime.handle();
    options.recovery = [](const StoreProfileWriteContext& context, QString*) { return context.current(); };
    options.coordinating = [](bool coordinating) { say(coordinating ? "coordinating 1" : "coordinating 0"); };
    options.changed = [&](Client::State state) {
        if (state != Client::State::Ready || !session)
            return;
        const auto epoch = *session->epoch();
        const auto token = authority.capture();
        // Native only: report when another process's change cancels this lease.
        watch = token.onInvalidated([] { say("cancelled"); });
        say("ready " + epoch.generation.toHex() + ' ' + QByteArray::number(epoch.revision));
    };
    session = std::make_unique<Session>(std::move(options), std::move(peer));
    std::vector<std::thread> writers;
    // Commands arrive on stdin. A reader thread posts them to the event loop.
    std::thread([&] {
        for (int command; (command = std::getchar()) != EOF;) {
            QMetaObject::invokeMethod(qApp, [&, command] {
                if (command == 'q') {
                    session->close();
                    qApp->quit();
                } else if (command == 'h') {
                    // A native cancellation that never returns.
                    watch = authority.capture().onInvalidated([] {
                        say("hanging");
                        for (;;) std::this_thread::sleep_for(std::chrono::seconds(1));
                    });
                    say("armed");
                } else if (command == 'w' || (command >= '1' && command <= '5')) {
                    // 'w' writes. '1' to '5' exit at one writer boundary:
                    // 1 holding store.lock, 2 after the permit request,
                    // 3 after the grant, 4 inside the write, 5 before Complete.
                    const int crashAt = command == 'w' ? 0 : command - '0';
                    auto ready = session->readyWait();
                    auto port = session->writePort();
                    writers.emplace_back([ready, port, profile, crashAt] {
                        const auto crash = [crashAt](int point) {
                            if (point != crashAt) return;
                            say("crash " + QByteArray::number(point));
                            std::_Exit(7);
                        };
                        const auto deadline = G::now() + 10 * 1000 * 1000;
                        ready(deadline);
                        G::Status status;
                        auto scope = G::prepareWriter(profile, {[] { return true; }, G::now, deadline}, &status);
                        crash(1);
                        auto permit = port(deadline);
                        crash(2);
                        G::Result result;
                        if (scope && permit->wait()) {
                            crash(3);
                            result = scope->write(permit->ticket(), [crash](const StoreProfileWriteContext& context, QString*) {
                                crash(4);
                                return context.current() ? G::Change::Applied : G::Change::NeedsRecovery;
                            });
                        }
                        scope.reset();
                        crash(5);
                        permit->finish(result);
                        say("wrote " + QByteArray::number(int(result.status)) + ' ' + QByteArray::number(result.epoch.revision));
                    });
                }
            }, Qt::QueuedConnection);
        }
    }).detach();
    if (!session->start())
        return 3;
    const int code = qApp->exec();
    for (auto& writer : writers)
        if (writer.joinable())
            writer.join();
    session.reset();
    lifetime.close();
    lifetime.handle().waitQt(std::chrono::seconds(10));
    return code;
}

struct Child {
    QProcess process;
    QList<QByteArray> lines;
    explicit Child(const QString& profile, const QString& runtime) {
        process.start(QCoreApplication::applicationFilePath(),
            {QStringLiteral("--hakone-session-child"), profile, runtime});
    }
    ~Child() {
        if (process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(5000);
        }
    }
    // The next line that starts with the prefix. Earlier lines stay recorded.
    std::optional<QByteArray> expect(const QByteArray& prefix, int timeout = 20000) {
        QElapsedTimer timer; timer.start();
        qsizetype seen = 0;
        while (timer.elapsed() < timeout) {
            while (process.canReadLine()) lines.append(process.readLine().trimmed());
            for (; seen < lines.size(); ++seen)
                if (lines[seen].startsWith(prefix)) { auto line = lines.takeAt(seen); return line; }
            if (process.state() == QProcess::NotRunning && !process.canReadLine()) return std::nullopt;
            process.waitForReadyRead(50);
        }
        return std::nullopt;
    }
    bool saw(const QByteArray& prefix) const {
        for (const auto& line : lines) if (line.startsWith(prefix)) return true;
        return false;
    }
    bool send(char command) { return process.write(&command, 1) == 1 && process.waitForBytesWritten(1000); }
};
// "ready <generation> <revision>" as a pair.
std::pair<QByteArray, QByteArray> readyEpoch(const QByteArray& line) {
    const auto parts = line.split(' ');
    return parts.size() == 3 ? std::pair{parts[1], parts[2]} : std::pair<QByteArray, QByteArray>{};
}
}
class TestAddonStoreProfileSession : public QObject
{
    Q_OBJECT
    QTemporaryDir profile, runtime;
private slots:
    void init() {
        QVERIFY(profile.isValid() && runtime.isValid());
    }
    void twoProcessesShareOneGeneration() {
        QTemporaryDir root, run;
        Child first(root.path(), run.path());
        QVERIFY(first.expect("coordinating 1"));
        const auto a = first.expect("ready"); QVERIFY(a);
        Child second(root.path(), run.path());
        const auto b = second.expect("ready"); QVERIFY(b);
        QCOMPARE(readyEpoch(*a), readyEpoch(*b));
        QCOMPARE(readyEpoch(*a).second, QByteArray("0"));
        QVERIFY(!second.saw("coordinating 1"));
    }
    void aWriteInOneProcessCancelsNativeWorkInTheOther() {
        QTemporaryDir root, run;
        Child first(root.path(), run.path()); QVERIFY(first.expect("ready"));
        Child second(root.path(), run.path()); const auto before = second.expect("ready"); QVERIFY(before);
        QVERIFY(second.send('w'));
        // The other process's native lease is cancelled before the write.
        QVERIFY(first.expect("cancelled"));
        const auto wrote = second.expect("wrote"); QVERIFY(wrote);
        QCOMPARE(*wrote, QByteArray("wrote 0 1"));
        const auto a = first.expect("ready"); QVERIFY(a);
        const auto b = second.expect("ready"); QVERIFY(b);
        QCOMPARE(readyEpoch(*a), readyEpoch(*b));
        QCOMPARE(readyEpoch(*a).first, readyEpoch(*before).first);
        QCOMPARE(readyEpoch(*a).second, QByteArray("1"));
    }
    void theNextProcessTakesOverWhenTheCoordinatorIsKilled() {
        QTemporaryDir root, run;
        Child first(root.path(), run.path()); QVERIFY(first.expect("coordinating 1"));
        const auto old = first.expect("ready"); QVERIFY(old);
        Child second(root.path(), run.path()); QVERIFY(second.expect("ready"));
        first.process.kill(); QVERIFY(first.process.waitForFinished(5000));
        QVERIFY(second.expect("cancelled"));
        QVERIFY(second.expect("coordinating 1"));
        const auto next = second.expect("ready"); QVERIFY(next);
        QVERIFY(readyEpoch(*next).first != readyEpoch(*old).first);
        QCOMPARE(readyEpoch(*next).second, QByteArray("0"));
    }
    void aQuitHandsOverToo() {
        QTemporaryDir root, run;
        Child first(root.path(), run.path()); QVERIFY(first.expect("coordinating 1")); QVERIFY(first.expect("ready"));
        Child second(root.path(), run.path()); QVERIFY(second.expect("ready"));
        QVERIFY(first.send('q'));
        QVERIFY(first.process.waitForFinished(10000));
        QCOMPARE(first.process.exitStatus(), QProcess::NormalExit); QCOMPARE(first.process.exitCode(), 0);
        QVERIFY(second.expect("coordinating 1"));
        QVERIFY(second.expect("ready"));
    }
    void oneOfTheRemainingProcessesTakesOver() {
        QTemporaryDir root, run;
        Child first(root.path(), run.path()); QVERIFY(first.expect("coordinating 1")); QVERIFY(first.expect("ready"));
        Child second(root.path(), run.path()); QVERIFY(second.expect("ready"));
        Child third(root.path(), run.path()); QVERIFY(third.expect("ready"));
        first.process.kill(); QVERIFY(first.process.waitForFinished(5000));
        const auto b = second.expect("ready"); QVERIFY(b);
        const auto c = third.expect("ready"); QVERIFY(c);
        QCOMPARE(readyEpoch(*b), readyEpoch(*c));
        QTest::qWait(300);
        while (second.process.canReadLine()) second.lines.append(second.process.readLine().trimmed());
        while (third.process.canReadLine()) third.lines.append(third.process.readLine().trimmed());
        // Exactly one process coordinates the new generation.
        QVERIFY(second.saw("coordinating 1") != third.saw("coordinating 1"));
    }
    // Step 2.7: a process exits at each writer boundary. The survivor must
    // reach a coherent ready record and write again.
    void aWriterThatExitsAtABoundaryLeavesAWritableProfile_data() {
        QTest::addColumn<char>("point");
        QTest::newRow("holding store.lock") << '1';
        QTest::newRow("after the permit request") << '2';
        QTest::newRow("after the grant") << '3';
        QTest::newRow("inside the write") << '4';
        QTest::newRow("before Complete") << '5';
    }
    void aWriterThatExitsAtABoundaryLeavesAWritableProfile() {
        QFETCH(char, point);
        QTemporaryDir root, run;
        Child survivor(root.path(), run.path()); QVERIFY(survivor.expect("coordinating 1")); QVERIFY(survivor.expect("ready"));
        Child victim(root.path(), run.path()); QVERIFY(victim.expect("ready"));
        QVERIFY(victim.send(point));
        QVERIFY(victim.expect("crash"));
        QVERIFY(victim.process.waitForFinished(10000));
        QCOMPARE(victim.process.exitCode(), 7);
        // A later write from the survivor passes, after any recovery.
        QVERIFY(survivor.send('w'));
        const auto wrote = survivor.expect("wrote", 30000); QVERIFY(wrote);
        QVERIFY2(wrote->startsWith("wrote 0 "), wrote->constData());
        QVERIFY(survivor.expect("ready"));
    }
    void aPeerKilledDuringItsCancellationStartsRecovery() {
        QTemporaryDir root, run;
        Child writer(root.path(), run.path()); QVERIFY(writer.expect("coordinating 1")); const auto old = writer.expect("ready"); QVERIFY(old);
        Child hung(root.path(), run.path()); QVERIFY(hung.expect("ready"));
        QVERIFY(hung.send('h')); QVERIFY(hung.expect("armed"));
        QVERIFY(writer.send('w'));
        QVERIFY(hung.expect("hanging"));
        // The hung peer never acknowledges. It still holds its shared fence,
        // so no write can pass it. Its exit ends the write and releases it.
        QVERIFY(!writer.expect("wrote", 1000));
        hung.process.kill(); QVERIFY(hung.process.waitForFinished(5000));
        const auto first = writer.expect("wrote", 30000); QVERIFY(first);
        QVERIFY2(!first->startsWith("wrote 0 "), first->constData());
        const auto next = writer.expect("ready", 30000); QVERIFY(next);
        QVERIFY(readyEpoch(*next).first != readyEpoch(*old).first);
        QVERIFY(writer.send('w'));
        const auto second = writer.expect("wrote", 30000); QVERIFY(second);
        QVERIFY2(second->startsWith("wrote 0 "), second->constData());
    }
    void aCoordinatorKilledDuringAWriteHandsOver() {
        QTemporaryDir root, run;
        Child coordinator(root.path(), run.path()); QVERIFY(coordinator.expect("coordinating 1")); QVERIFY(coordinator.expect("ready"));
        Child hung(root.path(), run.path()); QVERIFY(hung.expect("ready"));
        Child writer(root.path(), run.path()); QVERIFY(writer.expect("ready"));
        QVERIFY(hung.send('h')); QVERIFY(hung.expect("armed"));
        QVERIFY(writer.send('w'));
        QVERIFY(hung.expect("hanging"));
        coordinator.process.kill(); QVERIFY(coordinator.process.waitForFinished(5000));
        // The pending write fails. The hung peer still blocks recovery.
        const auto failed = writer.expect("wrote", 30000); QVERIFY(failed);
        QVERIFY2(!failed->startsWith("wrote 0 "), failed->constData());
        hung.process.kill(); QVERIFY(hung.process.waitForFinished(5000));
        QVERIFY(writer.expect("coordinating 1", 30000));
        QVERIFY(writer.expect("ready", 30000));
        QVERIFY(writer.send('w'));
        const auto wrote = writer.expect("wrote", 30000); QVERIFY(wrote);
        QVERIFY2(wrote->startsWith("wrote 0 "), wrote->constData());
    }
};
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() == 4 && args.at(1) == QLatin1String("--hakone-session-child"))
        return child(args);
    TestAddonStoreProfileSession test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_addon_store_profile_session.moc"
