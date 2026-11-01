// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QSemaphore>
#include <QScopeGuard>
#include <atomic>
#include <chrono>
#include <thread>
#include <stdexcept>
#include "addons/addon_manager.hpp"

using namespace vlc::addons;

namespace {
QString fixture(const QString& directory)
{
#ifdef Q_OS_UNIX
    const auto path = QDir(directory).filePath(QStringLiteral("guard-worker"));
    QFile file(path);
    const QByteArray bytes = R"PY(#!/usr/bin/python3
import json, pathlib, sys
root = pathlib.Path(__file__)
print(json.dumps({"type":"ready","protocol":"wasm-message-v1"}), flush=True)
for line in sys.stdin:
    packet = json.loads(line)
    if packet.get("type") == "invoke":
        invocation = packet["id"]
        context = packet.get("context", {})
        if context.get("host"):
            messages = [{"type":"host-call","id":"host-1","invocation":invocation,
                "operation":"playback.observe","request":{}}]
        elif context.get("http"):
            messages = [{"type":"host-call","id":"http-1","invocation":invocation,
                "operation":"network.http","request":{"url":"https://8.8.8.8/guard"}}]
        else:
            messages = [{"type":"result","id":invocation,"status":"match","data":{"ok":True}}]
        print(json.dumps({"type":"messages","messages":messages}), flush=True)
        pathlib.Path(str(root) + "." + invocation + ".ready").touch()
    elif packet.get("type") == "host-result":
        print(json.dumps({"type":"messages","messages":[{"type":"result","id":packet["invocation"],
            "status":"match","data":{"response":packet.get("response", {})}}]}), flush=True)
    elif packet.get("type") == "cancel":
        print(json.dumps({"type":"messages","messages":[{"type":"host-call","id":"late-1",
            "invocation":packet["id"],"operation":"playback.observe","request":{}}]}), flush=True)
)PY";
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) return {};
    file.close();
    return file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner) ? path : QString{};
#else
    Q_UNUSED(directory);
    return {};
#endif
}
InstalledPackage package(const QString& directory)
{
    InstalledPackage result;
    result.directory = directory; result.enabled = true;
    result.manifest.id = QStringLiteral("org.example.invocation-guard");
    result.manifest.runtimeEntry = QStringLiteral("fixture.wasm");
    result.manifest.requiredPermissions = {{"network.http", QJsonObject{
        {"origins", QJsonArray{"https://8.8.8.8"}}, {"methods", QJsonArray{"GET"}}}},
        {"playback.observe", QJsonObject{}}};
    result.grants = result.manifest.requiredPermissions;
    Contribution contribution; contribution.id = QStringLiteral("search");
    contribution.operations = {QStringLiteral("searchAssets")};
    result.manifest.contributions.append(contribution);
    return result;
}
}
class TestAddonInvocationGuard : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
#ifndef Q_OS_UNIX
        QSKIP("The child fixture needs the Unix test interpreter.");
#endif
    }
    void queuedNativeStopRetiresOnlyItsInvocation_data()
    {
        QTest::addColumn<bool>("host");
        QTest::newRow("queued-host-call") << true;
        QTest::newRow("queued-terminal-result") << false;
    }
    void queuedNativeStopRetiresOnlyItsInvocation()
    {
        QFETCH(bool, host);
        QTemporaryDir directory; const auto executable = fixture(directory.path()); QVERIFY(!executable.isEmpty());
        NetworkBroker broker; AddonHostServices services; QObject owner; int calls = 0;
        QVERIFY(services.registerOperation(QStringLiteral("playback.observe"), &owner, [&](const auto&, const auto&, auto done) {
            ++calls; done({{{"ok", true}}, {}}); return AddonHostServices::Cancel{};
        }));
        AddonWorker worker(package(directory.path()), &broker); worker.setExecutable(executable); worker.setHostServices(&services);
        QSignalSpy completed(&worker, &AddonWorker::completed), unavailable(&worker, &AddonWorker::unavailable);
        worker.invoke(QStringLiteral("search"), QStringLiteral("searchAssets"), {}); QTRY_COMPARE(completed.size(), 1); completed.clear();
        auto current = std::make_shared<std::atomic_bool>(true);
        QSemaphore parsed, stopped; std::atomic_bool armed{false};
        std::thread nativeStop([&] { parsed.acquire(); current->store(false); stopped.release(); });
        const auto join = qScopeGuard([&] { parsed.release(); nativeStop.join(); });
        worker.setRequestGuard([&] {
            if (armed.exchange(false)) { parsed.release(); stopped.acquire(); }
            return true;
        });
        const auto expired = worker.invoke(QStringLiteral("search"), QStringLiteral("searchAssets"), {{"host", host}}, [current] { return current->load(); });
        const auto sibling = worker.invoke(QStringLiteral("search"), QStringLiteral("searchAssets"), {});
        // The first parsed child packet pauses before invocation admission.
        // Native Stop then completes without a Qt cancellation event.
        armed.store(true);
        QTRY_COMPARE(completed.size(), 2); QCOMPARE(calls, 0); QVERIFY(unavailable.isEmpty()); QVERIFY(!worker.failed());
        QMap<QString, QString> statuses;
        for (const auto& row : completed) statuses.insert(row[0].toString(), row[1].toString());
        QCOMPARE(statuses.value(expired), QStringLiteral("denied")); QCOMPARE(statuses.value(sibling), QStringLiteral("match"));
    }
    void hostGuardSurvivesCancellationAndLateCompletion()
    {
        QTemporaryDir directory; NetworkBroker broker; AddonHostServices services; QObject owner;
        AddonHostServices::Context captured; AddonHostServices::Completion late; int cancels = 0;
        QVERIFY(services.registerOperation(QStringLiteral("playback.observe"), &owner, [&](const auto& context, const auto&, auto done) {
            captured = context; late = std::move(done); return AddonHostServices::Cancel{[&] { ++cancels; }};
        }));
        AddonWorker worker(package(directory.path()), &broker); worker.setExecutable(fixture(directory.path())); worker.setHostServices(&services);
        QSignalSpy completed(&worker, &AddonWorker::completed), unavailable(&worker, &AddonWorker::unavailable);
        auto current = std::make_shared<std::atomic_bool>(true);
        const auto id = worker.invoke(QStringLiteral("search"), QStringLiteral("searchAssets"), {{"host", true}}, [current] { return current->load(); });
        QTRY_VERIFY(bool(late)); QVERIFY(captured.current()); current->store(false); QVERIFY(!captured.current());
        late({{{"secretResult", 7}}, {}}); QTRY_COMPARE(completed.size(), 1);
        QCOMPARE(completed.first()[0].toString(), id); QCOMPARE(completed.first()[1].toString(), QStringLiteral("denied"));
        QVERIFY(completed.first()[2].toJsonObject().isEmpty()); QVERIFY(!captured.current());
        const auto sibling = worker.invoke(QStringLiteral("search"), QStringLiteral("searchAssets"), {});
        QTRY_COMPARE(completed.size(), 2); QCOMPARE(completed.last()[0].toString(), sibling); QVERIFY(unavailable.isEmpty());
        worker.stop(); QVERIFY(!captured.current()); Q_UNUSED(cancels);
    }
    void nativeChangeDuringDispatchCancelsTheReturnedJob()
    {
        QTemporaryDir directory; NetworkBroker broker; AddonHostServices services; QObject owner;
        auto current = std::make_shared<std::atomic_bool>(true); int calls = 0, cancels = 0;
        QVERIFY(services.registerOperation(QStringLiteral("playback.observe"), &owner, [&](const auto&, const auto&, auto) {
            ++calls; current->store(false); return AddonHostServices::Cancel{[&] { ++cancels; }};
        }));
        AddonWorker worker(package(directory.path()), &broker); worker.setExecutable(fixture(directory.path())); worker.setHostServices(&services);
        QSignalSpy completed(&worker, &AddonWorker::completed);
        worker.invoke(QStringLiteral("search"), QStringLiteral("searchAssets"), {{"host", true}}, [current] { return current->load(); });
        QTRY_COMPARE(completed.size(), 1); QCOMPARE(calls, 1); QCOMPARE(cancels, 1);
        QCOMPARE(completed.first()[1].toString(), QStringLiteral("denied"));
    }
    void exceptionAndReentryGuardsFailClosed_data()
    {
        QTest::addColumn<bool>("destroy");
        QTest::newRow("exception") << false; QTest::newRow("owner-destruction") << true;
    }
    void exceptionAndReentryGuardsFailClosed()
    {
        QFETCH(bool, destroy);
        QTemporaryDir directory; NetworkBroker broker;
        auto* worker = new AddonWorker(package(directory.path()), &broker); worker->setExecutable(fixture(directory.path()));
        const QPointer<AddonWorker> alive(worker);
        auto calls = std::make_shared<unsigned>(0);
        const auto id = worker->invoke(QStringLiteral("search"), QStringLiteral("searchAssets"), {}, [worker, destroy, calls] {
            ++*calls;
            if (destroy) { delete worker; return true; }
            throw std::runtime_error("Test guard refusal");
        });
        QVERIFY(!id.isEmpty()); QCOMPARE(*calls, 1u);
        if (destroy) QVERIFY(!alive);
        else { QSignalSpy completed(worker, &AddonWorker::completed); QTRY_COMPARE(completed.size(), 1); QCOMPARE(completed.first()[1].toString(), QStringLiteral("denied")); delete worker; }
    }
    void brokerRechecksBeforeDnsAndBeforeSocketAdmission_data()
    {
        QTest::addColumn<int>("refuseAt");
        QTest::newRow("before-dns") << 1; QTest::newRow("before-socket") << 2;
        QTest::newRow("last-socket-check") << 3;
    }
    void brokerRechecksBeforeDnsAndBeforeSocketAdmission()
    {
        QFETCH(int, refuseAt);
        QTemporaryDir directory; const auto installed = package(directory.path()); NetworkBroker broker;
        int checks = 0, done = 0;
        broker.fetch(installed.manifest, installed.grants, {{"url", "https://8.8.8.8/guard"}}, {}, [&](const auto& response) {
            ++done; QVERIFY(!response.error.isEmpty()); QVERIFY(response.body.isEmpty());
        }, 1024, NetworkBroker::RequestKind::Document, [&] { return ++checks < refuseAt; });
        QTRY_COMPARE(done, 1); QVERIFY(checks >= refuseAt);
    }
    void brokerGuardCanThrowOrDeleteTheBroker_data()
    {
        QTest::addColumn<bool>("destroy");
        QTest::newRow("exception") << false; QTest::newRow("owner-destruction") << true;
    }
    void brokerGuardCanThrowOrDeleteTheBroker()
    {
        QFETCH(bool, destroy);
        QTemporaryDir directory; const auto installed = package(directory.path());
        auto* broker = new NetworkBroker; QPointer<NetworkBroker> alive(broker); int callbacks = 0;
        broker->fetch(installed.manifest, installed.grants, {{"url", "https://8.8.8.8/guard"}}, {}, [&](const auto& response) {
            ++callbacks; QVERIFY(!response.error.isEmpty());
        }, 1024, NetworkBroker::RequestKind::Document, [broker, destroy]() -> bool {
            if (destroy) { delete broker; return true; }
            throw std::runtime_error("Test guard refusal");
        });
        if (destroy) { QTRY_VERIFY(!alive); QCOMPARE(callbacks, 0); }
        else { QTRY_COMPARE(callbacks, 1); delete broker; }
    }
    void managerPreservesTheOldEntryAndScopesGuardedInvocations()
    {
        QTemporaryDir directory, storage; AddonManager manager(storage.path());
        const QString id = QStringLiteral("org.hakone.thesubtitledb");
        QVERIFY2(manager.installLocalPackage(QStringLiteral(VLC_ADDON_SOURCE_ROOT "/contrib-addons/thesubtitledb"), true), qPrintable(manager.errorMessage()));
        QVERIFY(manager.saveAddonSettings(id, {{"language", "en"}}, {{"apiKey", QVariantMap{{"operation", "replace"}, {"value", "unit-guard-key"}, {"sessionOnly", true}}}}));
        QVERIFY(manager.setEnabled(id, true)); const auto executable = fixture(directory.path()); manager.setWorkerExecutable(executable);
        QSignalSpy completed(&manager, &AddonManager::operationFinished);
        const QString contribution = id + QStringLiteral("/search");
        const auto refused = manager.invoke(contribution, QStringLiteral("searchAssets"), {}, [] { return false; });
        const auto legacy = manager.invoke(contribution, QStringLiteral("searchAssets"), {});
        QTRY_COMPARE(completed.size(), 2); QMap<QString, QString> statuses;
        for (const auto& row : completed) statuses.insert(row[0].toString(), row[2].toString());
        QCOMPARE(statuses.value(refused), QStringLiteral("denied")); QCOMPARE(statuses.value(legacy), QStringLiteral("match"));
        QVERIFY(!QFileInfo::exists(executable + QLatin1Char('.') + refused + QStringLiteral(".ready")));
        completed.clear(); auto current = std::make_shared<std::atomic_bool>(true);
        const auto expired = manager.invoke(contribution, QStringLiteral("searchAssets"), {}, [current] { return current->load(); });
        current->store(false);
        QTRY_COMPARE(completed.size(), 1); QCOMPARE(completed.first()[0].toString(), expired);
        QCOMPARE(completed.first()[2].toString(), QStringLiteral("denied"));
    }
};
QTEST_GUILESS_MAIN(TestAddonInvocationGuard)
#include "test_addon_invocation_guard.moc"
