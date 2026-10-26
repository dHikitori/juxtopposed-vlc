// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QJsonArray>
#include "addons/addon_host_services.hpp"
#include <thread>
using namespace vlc::addons;

class TestAddonHostServices : public QObject
{
    Q_OBJECT
    static AddonHostServices::Context context() {
        return {QStringLiteral("org.example.test"), QStringLiteral("org.example.test/session"), QStringLiteral("invoke-1"),
            {{"playback.control", QJsonObject{{"operations", QJsonArray{"pause"}}}}, {"playback.observe", QJsonObject{}}}};
    }
private slots:
    void closedOperationMapAndScopesPrecedeHandlers() {
        AddonHostServices services; QObject owner; int calls = 0, completions = 0;
        const auto handler = [&](const auto&, const auto&, auto done) -> AddonHostServices::Cancel { ++calls; done({{{"ok", true}}, {}}); return {}; };
        QVERIFY(!services.registerOperation(QStringLiteral("shell.execute"), &owner, handler));
        QVERIFY(services.registerOperation(QStringLiteral("playback.command"), &owner, handler));
        QVERIFY(!services.registerOperation(QStringLiteral("playback.command"), &owner, handler));
        QString error;
        auto denied = context(); denied.grants = {};
        services.dispatch(denied, QStringLiteral("playback.command"), {{"operation", "pause"}}, [&](auto result) { ++completions; error = result.error; });
        QTRY_COMPARE(completions, 1); QVERIFY(!error.isEmpty()); QCOMPARE(calls, 0);
        services.dispatch(context(), QStringLiteral("playback.command"), {{"operation", "seek"}}, [&](auto result) { ++completions; error = result.error; });
        QTRY_COMPARE(completions, 2); QVERIFY(!error.isEmpty()); QCOMPARE(calls, 0);
        auto foreign = context(); foreign.contribution = QStringLiteral("org.other.test/session");
        services.dispatch(foreign, QStringLiteral("playback.command"), {{"operation", "pause"}}, [&](auto result) { ++completions; error = result.error; });
        QTRY_COMPARE(completions, 3); QVERIFY(!error.isEmpty()); QCOMPARE(calls, 0);
        services.dispatch(context(), QStringLiteral("playback.command"), {{"operation", "pause"}}, [&](auto result) { ++completions; error = result.error; });
        QTRY_COMPARE(completions, 4); QVERIFY(error.isEmpty()); QCOMPARE(calls, 1);
    }
    void responseCompletionHasOneTerminalResult() {
        AddonHostServices services; QObject owner; AddonHostServices::Completion complete; int calls = 0;
        QVERIFY(services.registerOperation(QStringLiteral("playback.observe"), &owner,
            [&](const auto& host, const auto&, auto done) -> AddonHostServices::Cancel {
                if (host.package == context().package) complete = done; return {};
            }));
        services.dispatch(context(), QStringLiteral("playback.observe"), {}, [&](auto result) { ++calls; QVERIFY(result.error.isEmpty()); });
        QVERIFY(bool(complete)); std::thread worker([complete] { complete({{{"positionUs", 123}}, {}}); complete({{{"positionUs", 999}}, {}}); }); worker.join();
        QTRY_COMPARE(calls, 1); QTest::qWait(20); QCOMPARE(calls, 1);
    }
    void cancellationAndTimeoutAbortOnlyTheirOwnJobs() {
        AddonHostServices services; QObject owner; AddonHostServices::Completion complete; int aborts = 0, calls = 0;
        QVERIFY(services.registerOperation(QStringLiteral("playback.observe"), &owner,
            [&](const auto&, const auto&, auto done) -> AddonHostServices::Cancel { complete = done; return [&] { ++aborts; }; }));
        const auto first = services.dispatch(context(), QStringLiteral("playback.observe"), {}, [&](auto result) { ++calls; QVERIFY(!result.error.isEmpty()); });
        services.cancel(first); QCOMPARE(aborts, 1); QCOMPARE(calls, 1); complete({{}, {}}); QTest::qWait(20); QCOMPARE(calls, 1);
        services.dispatch(context(), QStringLiteral("playback.observe"), {}, [&](auto result) { ++calls; QVERIFY(!result.error.isEmpty()); }, 1);
        QTRY_COMPARE(calls, 2); QCOMPARE(aborts, 2);
    }
    void pendingQuotaAndPackageWithdrawalAreBounded() {
        AddonHostServices services; QObject owner; int aborts = 0, calls = 0;
        QVERIFY(services.registerOperation(QStringLiteral("playback.observe"), &owner,
            [&](const auto&, const auto&, auto) -> AddonHostServices::Cancel { return [&] { ++aborts; }; }));
        for (int i = 0; i < 32; ++i) QVERIFY(services.dispatch(context(), QStringLiteral("playback.observe"), {}, [&](auto) { ++calls; }));
        QCOMPARE(services.dispatch(context(), QStringLiteral("playback.observe"), {}, [&](auto) { QFAIL("A rejected request must have no callback."); }), quint64(0));
        auto other = context(); other.package = QStringLiteral("org.example.other"); other.contribution = QStringLiteral("org.example.other/session");
        const auto foreign = services.dispatch(other, QStringLiteral("playback.observe"), {}, [&](auto) { ++calls; }); QVERIFY(foreign);
        services.cancelPackage(context().package); QCOMPARE(calls, 32); QCOMPARE(aborts, 32);
        services.cancel(foreign); QCOMPARE(calls, 33); QCOMPARE(aborts, 33);
    }
    void ownerDestructionRemovesHandlersAndPendingWork() {
        AddonHostServices services; auto* owner = new QObject; int calls = 0, aborts = 0;
        QVERIFY(services.registerOperation(QStringLiteral("playback.observe"), owner,
            [&](const auto&, const auto&, auto) -> AddonHostServices::Cancel { return [&] { ++aborts; }; }));
        services.dispatch(context(), QStringLiteral("playback.observe"), {}, [&](auto result) { ++calls; QVERIFY(!result.error.isEmpty()); });
        delete owner; QCOMPARE(calls, 1); QCOMPARE(aborts, 0);
        services.dispatch(context(), QStringLiteral("playback.observe"), {}, [&](auto result) { ++calls; QVERIFY(!result.error.isEmpty()); });
        QTRY_COMPARE(calls, 2);
    }
    void oversizedInputAndOutputDoNotReachTheGuest() {
        AddonHostServices services; QObject owner; int handlers = 0, calls = 0; QString error;
        QVERIFY(services.registerOperation(QStringLiteral("playback.observe"), &owner,
            [&](const auto&, const auto&, auto done) -> AddonHostServices::Cancel { ++handlers; done({{{"data", QString(70000, 'x')}}, {}}); return {}; }));
        services.dispatch(context(), QStringLiteral("playback.observe"), {{"data", QString(70000, 'x')}}, [&](auto result) { ++calls; error = result.error; });
        QTRY_COMPARE(calls, 1); QCOMPARE(handlers, 0); QVERIFY(!error.isEmpty());
        services.dispatch(context(), QStringLiteral("playback.observe"), {}, [&](auto result) { ++calls; error = result.error; QVERIFY(result.response.isEmpty()); });
        QTRY_COMPARE(calls, 2); QCOMPARE(handlers, 1); QVERIFY(!error.isEmpty());
    }
    void completionAfterDestructionHasNoReceiver() {
        QObject owner; AddonHostServices::Completion late; int calls = 0;
        auto* services = new AddonHostServices;
        QVERIFY(services->registerOperation(QStringLiteral("playback.observe"), &owner,
            [&](const auto&, const auto&, auto done) -> AddonHostServices::Cancel { late = done; return {}; }));
        services->dispatch(context(), QStringLiteral("playback.observe"), {}, [&](auto) { ++calls; });
        delete services; QCOMPARE(calls, 1); QVERIFY(bool(late));
        std::thread worker([late] { late({{}, {}}); }); worker.join(); QTest::qWait(20); QCOMPARE(calls, 1);
    }
};
QTEST_GUILESS_MAIN(TestAddonHostServices)
#include "test_addon_host_services.moc"
