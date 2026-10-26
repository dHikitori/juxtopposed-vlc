// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDateTime>
#include "addons/addon_playable_gate.hpp"
using namespace vlc::addons;

class TestAddonPlayableGate : public QObject
{
    Q_OBJECT
    static QString source() { return QStringLiteral("org.example.stream/catalogue"); }
    static bool package(const QString& directory) {
        QDir().mkpath(directory);
        const QJsonObject manifest{{"formatVersion", 1}, {"id", "org.example.stream"}, {"name", "Stream fixture"}, {"version", "1.0.0"},
            {"publisher", "Tests"}, {"license", "GPL-2.0-or-later"}, {"source", "https://example.org/stream.git"},
            {"api", QJsonObject{{"addon", "^1.0.0"}, {"contracts", QJsonObject{{"discovery", "^1.0.0"}}}}},
            {"runtime", QJsonObject{{"kind", "wasm-message-v1"}, {"entry", "addon.wasm"}}}, {"files", QJsonArray{"addon.json", "addon.wasm", "LICENSE"}},
            {"contributions", QJsonArray{QJsonObject{{"id", "catalogue"}, {"type", "discovery.source"}, {"contract", "discovery@1"},
                {"operations", QJsonArray{"roots", "resolve"}}}}},
            {"permissions", QJsonObject{{"required", QJsonObject{{"network.http", QJsonObject{{"origins", QJsonArray{"https://streams.example.org"}}, {"methods", QJsonArray{"GET"}}}}}}}}};
        const QMap<QString, QByteArray> files{{QStringLiteral("addon.json"), QJsonDocument(manifest).toJson()},
            {QStringLiteral("addon.wasm"), QByteArray("\0asm\1\0\0\0", 8)}, {QStringLiteral("LICENSE"), QByteArray("SPDX-License-Identifier: GPL-2.0-or-later\n")}};
        for (auto it = files.begin(); it != files.end(); ++it) { QFile file(QDir(directory).filePath(it.key())); if (!file.open(QIODevice::WriteOnly) || file.write(it.value()) != it.value().size()) return false; }
        return true;
    }
    static QVariantMap proposal() {
        return {{"url", "https://streams.example.org/radio"}, {"hostContext", QVariantMap{{"view", "view-1"}, {"generation", 1}, {"intent", "play"}}}};
    }
    struct Transport {
        bool permission = true; int starts = 0, cancellations = 0, releases = 0;
        AddonPlayableGate::Context context;
        std::function<void(AddonPlayableGate::Result)> ready;
        AddonPlayableGate::Backend backend() {
            return {[this](const auto&, const auto&) { return permission; },
                [this](const auto& host, const auto&, auto complete) { ++starts; context = host; ready = complete; return QStringLiteral("native-%1").arg(starts); },
                [this](const auto&) { ++cancellations; }};
        }
        AddonPlayableGate::Result result() {
            auto lease = std::make_shared<AddonPlayableGate::Lease>();
            // A synthetic native item token. The registry does not dereference it.
            const auto storage = std::make_shared<int>(7);
            lease->item = std::shared_ptr<input_item_t>(storage, reinterpret_cast<input_item_t*>(storage.get()));
            lease->cleanup = [this] { ++releases; }; return {lease, {}};
        }
    };
    static bool install(AddonManager& manager, const QString& directory) {
        return package(directory) && manager.installLocalPackage(directory, true) && manager.setEnabled(QStringLiteral("org.example.stream"), true);
    }
private slots:
    void defaultGateHasNoTransportAuthority() {
        QTemporaryDir files; AddonManager manager(files.path() + QStringLiteral("/profile"));
        QVERIFY2(install(manager, files.path() + QStringLiteral("/package")), qPrintable(manager.errorMessage()));
        AddonPlayableGate gate(&manager); QVERIFY(!gate.supported()); bool done = false;
        gate.authorize(source(), proposal(), [&](const auto& handle, const auto& error) { done = true; QVERIFY(handle.isEmpty()); QVERIFY(!error.isEmpty()); });
        QTRY_VERIFY(done);
    }
    void exactOriginAndUserAuthorityPrecedePreparation() {
        QTemporaryDir files; AddonManager manager(files.path() + QStringLiteral("/profile"));
        QVERIFY2(install(manager, files.path() + QStringLiteral("/package")), qPrintable(manager.errorMessage()));
        Transport transport; AddonPlayableGate gate(&manager, transport.backend()); int done = 0;
        auto bad = proposal(); bad.insert("url", "https://streams.example.org.attacker.invalid/radio");
        gate.authorize(source(), bad, [&](const auto& handle, const auto& error) { ++done; QVERIFY(handle.isEmpty()); QVERIFY(!error.isEmpty()); });
        QTRY_COMPARE(done, 1); QCOMPARE(transport.starts, 0);
        transport.permission = false;
        gate.authorize(source(), proposal(), [&](const auto& handle, const auto& error) { ++done; QVERIFY(handle.isEmpty()); QVERIFY(!error.isEmpty()); });
        QTRY_COMPARE(done, 2); QCOMPARE(transport.starts, 0);
    }
    void handlesAreOneUseAndBindTheHostIntent() {
        QTemporaryDir files; AddonManager manager(files.path() + QStringLiteral("/profile"));
        QVERIFY2(install(manager, files.path() + QStringLiteral("/package")), qPrintable(manager.errorMessage()));
        Transport transport; AddonPlayableGate gate(&manager, transport.backend()); QString handle;
        auto input = proposal(); input.insert("intent", "enqueue");
        gate.authorize(source(), input, [&](const auto& value, const auto& error) { handle = value; QVERIFY(error.isEmpty()); });
        QCOMPARE(transport.context.intent, QStringLiteral("play")); QCOMPARE(transport.context.view, QStringLiteral("view-1"));
        transport.ready(transport.result()); QTRY_VERIFY(!handle.isEmpty()); QVERIFY(!handle.contains("https"));
        int consumed = 0;
        QVERIFY(gate.consume(handle, QStringLiteral("play"), [&](input_item_t* item) { ++consumed; return item != nullptr; }));
        QVERIFY(!gate.consume(handle, QStringLiteral("play"), [&](auto*) { ++consumed; return true; })); QCOMPARE(consumed, 1);
        QCOMPARE(transport.releases, 0); gate.release(handle); QCOMPARE(transport.releases, 1);
    }
    void queuedAndDeliveredResultsRemainRevocableBeforeConsumption() {
        QTemporaryDir files; AddonManager manager(files.path() + QStringLiteral("/profile"));
        QVERIFY2(install(manager, files.path() + QStringLiteral("/package")), qPrintable(manager.errorMessage()));
        Transport transport; AddonPlayableGate gate(&manager, transport.backend()); int callbacks = 0; QString handle;
        auto job = gate.authorize(source(), proposal(), [&](const auto& value, const auto&) { ++callbacks; handle = value; });
        transport.ready(transport.result()); gate.cancel(job); QTest::qWait(20); QCOMPARE(callbacks, 0); QCOMPARE(transport.releases, 1);
        job = gate.authorize(source(), proposal(), [&](const auto& value, const auto&) { ++callbacks; handle = value; });
        transport.ready(transport.result()); QTRY_VERIFY(!handle.isEmpty()); gate.cancel(job);
        QVERIFY(!gate.consume(handle, QStringLiteral("play"), [](auto*) { return true; })); QCOMPARE(transport.releases, 2);
    }
    void duplicateCompletionAndSourceWithdrawalReleaseAllLeases() {
        QTemporaryDir files; AddonManager manager(files.path() + QStringLiteral("/profile"));
        QVERIFY2(install(manager, files.path() + QStringLiteral("/package")), qPrintable(manager.errorMessage()));
        Transport transport; AddonPlayableGate gate(&manager, transport.backend()); int callbacks = 0; QString handle;
        gate.authorize(source(), proposal(), [&](const auto& value, const auto&) { ++callbacks; handle = value; });
        transport.ready(transport.result()); transport.ready(transport.result()); QTRY_VERIFY(!handle.isEmpty());
        QTest::qWait(20); QCOMPARE(callbacks, 1); QCOMPARE(transport.releases, 1);
        QVERIFY(gate.consume(handle, QStringLiteral("play"), [](auto*) { return true; }));
        QVERIFY(manager.setEnabled(QStringLiteral("org.example.stream"), false)); QCOMPARE(transport.releases, 2);
    }
    void changedAuthorityAndExpiredPreparedStreamsCannotBeConsumed() {
        QTemporaryDir files; AddonManager manager(files.path() + QStringLiteral("/profile"));
        QVERIFY2(install(manager, files.path() + QStringLiteral("/package")), qPrintable(manager.errorMessage()));
        Transport transport; AddonPlayableGate gate(&manager, transport.backend()); QString handle;
        gate.authorize(source(), proposal(), [&](const auto& value, const auto&) { handle = value; });
        transport.ready(transport.result()); QTRY_VERIFY(!handle.isEmpty()); transport.permission = false;
        QVERIFY(!gate.consume(handle, QStringLiteral("play"), [](auto*) { return true; })); QCOMPARE(transport.releases, 1);
        transport.permission = true; handle.clear(); auto input = proposal(); input.insert("expiresAt", QDateTime::currentMSecsSinceEpoch() + 200);
        gate.authorize(source(), input, [&](const auto& value, const auto&) { handle = value; });
        transport.ready(transport.result()); QTRY_VERIFY(!handle.isEmpty()); QTest::qWait(250);
        QVERIFY(!gate.consume(handle, QStringLiteral("play"), [](auto*) { return true; })); QCOMPARE(transport.releases, 2);
    }
    void lateLeaseAfterCloseStillHasCleanup() {
        QTemporaryDir files; AddonManager manager(files.path() + QStringLiteral("/profile"));
        QVERIFY2(install(manager, files.path() + QStringLiteral("/package")), qPrintable(manager.errorMessage()));
        Transport transport; auto* gate = new AddonPlayableGate(&manager, transport.backend()); int callbacks = 0;
        gate->authorize(source(), proposal(), [&](const auto&, const auto&) { ++callbacks; }); delete gate;
        transport.ready(transport.result()); QTest::qWait(20); QCOMPARE(callbacks, 0); QCOMPARE(transport.releases, 1); QCOMPARE(transport.cancellations, 1);
    }
    void nativeCallbacksCanDestroyTheGate() {
        for (int mode = 0; mode < 4; ++mode) {
            QTemporaryDir files; AddonManager manager(files.path() + QStringLiteral("/profile"));
            QVERIFY2(install(manager, files.path() + QStringLiteral("/package")), qPrintable(manager.errorMessage()));
            Transport transport; auto backend = transport.backend(); AddonPlayableGate* gate = nullptr; int checks = 0;
            backend.authority = [&](const auto&, const auto&) {
                ++checks;
                if (mode == 0 || (mode == 2 && checks == 2)) { delete gate; gate = nullptr; }
                return true;
            };
            if (mode == 1) {
                const auto prepare = backend.prepare;
                backend.prepare = [&, prepare](const auto& context, const auto& value, auto completed) {
                    const auto native = prepare(context, value, completed); delete gate; gate = nullptr; return native;
                };
            }
            gate = new AddonPlayableGate(&manager, backend); QString handle;
            gate->authorize(source(), proposal(), [&](const auto& value, const auto&) { handle = value; });
            if (mode < 2) {
                QVERIFY(gate == nullptr); QVERIFY(handle.isEmpty());
                if (mode == 1) { QCOMPARE(transport.cancellations, 1); transport.ready(transport.result()); QCOMPARE(transport.releases, 1); }
            } else {
                transport.ready(transport.result()); QTRY_VERIFY(!handle.isEmpty());
                QVERIFY(!gate->consume(handle, QStringLiteral("play"), [&](auto*) { delete gate; gate = nullptr; return true; }));
                QVERIFY(gate == nullptr); QCOMPARE(transport.releases, 1);
            }
        }
    }
    void nativeCancellationCanDestroyTheGate() {
        for (const bool close : {false, true}) {
            QTemporaryDir files; AddonManager manager(files.path() + QStringLiteral("/profile"));
            QVERIFY2(install(manager, files.path() + QStringLiteral("/package")), qPrintable(manager.errorMessage()));
            Transport transport; auto backend = transport.backend(); AddonPlayableGate* gate = nullptr;
            backend.cancel = [&](const auto&) { ++transport.cancellations; delete gate; gate = nullptr; };
            gate = new AddonPlayableGate(&manager, backend);
            const auto job = gate->authorize(source(), proposal(), [](const auto&, const auto&) {});
            if (close) gate->close(); else gate->cancel(job);
            QVERIFY(gate == nullptr); QCOMPARE(transport.cancellations, 1);
            transport.ready(transport.result()); QCOMPARE(transport.releases, 1);
        }
    }
    void reentrantAuthorityCannotExceedTheActiveLeaseLimit() {
        QTemporaryDir files; AddonManager manager(files.path() + QStringLiteral("/profile"));
        QVERIFY2(install(manager, files.path() + QStringLiteral("/package")), qPrintable(manager.errorMessage()));
        Transport transport; auto backend = transport.backend(); AddonPlayableGate* target = nullptr;
        QStringList handles; bool fill = false, filled = false; int consumers = 0;
        backend.authority = [&](const auto&, const auto&) {
            if (fill && !filled) {
                filled = true;
                for (int i = 0; i < 4; ++i) target->consume(handles[i], QStringLiteral("play"), [&](auto*) { ++consumers; return true; });
            }
            return true;
        };
        AddonPlayableGate gate(&manager, backend); target = &gate;
        for (int i = 0; i < 5; ++i) {
            gate.authorize(source(), proposal(), [&](const auto& value, const auto&) { handles.append(value); });
            transport.ready(transport.result()); QTRY_COMPARE(handles.size(), i + 1);
        }
        fill = true;
        QVERIFY(!gate.consume(handles.last(), QStringLiteral("play"), [&](auto*) { ++consumers; return true; }));
        QCOMPARE(consumers, 4); QCOMPARE(transport.releases, 1); gate.close(); QCOMPARE(transport.releases, 5);
    }
};
QTEST_GUILESS_MAIN(TestAddonPlayableGate)
#include "test_addon_playable_gate.moc"
