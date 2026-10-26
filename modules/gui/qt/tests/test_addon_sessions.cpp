// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QUuid>
#include "addons/addon_sessions.hpp"
using namespace vlc::addons;

struct SessionPlayer {
    qint64 time = 1000000;
    QVariantMap state{{"mediaEpoch", "media-1"}, {"positionUs", 10000000}, {"durationUs", 120000000},
        {"transportRevision", 0}, {"rate", 1.0}, {"paused", false}, {"seekable", true},
        {"minimumRate", 0.5}, {"maximumRate", 2.0}, {"url", "file:///private/movie.mkv"}, {"secret", "not-for-addons"}};
    QStringList applied;
    AddonSessions::Backend backend() {
        return {[this] { return state; }, [this](const QString& operation, const QVariantMap& parameters, const QString& origin) {
            if (origin.isEmpty()) return false;
            applied.append(operation);
            if (operation == QLatin1String("play") || operation == QLatin1String("pause")) state.insert("paused", operation == QLatin1String("pause"));
            if (operation == QLatin1String("seek")) state.insert("positionUs", parameters.value("positionUs"));
            if (operation == QLatin1String("setRate")) state.insert("rate", parameters.value("rate"));
            state.insert("transportRevision", state.value("transportRevision").toInt() + 1); return true;
        }, [this] { return time; }};
    }
    QVariantMap command(const QString& operation, const QVariantMap& parameters = {}) const {
        return {{"operation", operation}, {"commandId", QUuid::createUuid().toString()},
            {"mediaEpoch", state.value("mediaEpoch")}, {"expectedRevision", state.value("transportRevision")}, {"parameters", parameters}};
    }
};

class TestAddonSessions : public QObject
{
    Q_OBJECT
    static QString status(QSignalSpy& spy) { if (spy.isEmpty() && !spy.wait(1000)) return QStringLiteral("missing"); return spy.takeFirst()[1].toString(); }
private slots:
    void authorityIsExclusiveAndObservationHasNoPrivateFields() {
        SessionPlayer player; AddonSessions sessions(player.backend());
        const auto lease = sessions.authorize(QStringLiteral("org.example.party"), {QStringLiteral("pause")});
        QVERIFY(!lease.isEmpty());
        QVERIFY(sessions.authorize(QStringLiteral("org.example.other"), {QStringLiteral("play")}).isEmpty());
        QVERIFY(sessions.observe(QStringLiteral("org.example.other"), lease).isEmpty());
        const auto observation = sessions.observe(QStringLiteral("org.example.party"), lease);
        QVERIFY(!observation.contains("url")); QVERIFY(!observation.contains("secret"));
        QVERIFY(observation.contains("observedAtUs"));
        QSignalSpy done(&sessions, &AddonSessions::commandFinished);
        sessions.submit(QStringLiteral("org.example.other"), lease, player.command(QStringLiteral("pause")));
        QCOMPARE(status(done), QStringLiteral("rejected")); QVERIFY(player.applied.isEmpty());
        player.state.insert("mediaEpoch", "another-media");
        QVERIFY(sessions.observe(QStringLiteral("org.example.party"), lease).isEmpty());
    }
    void commandsRecheckRevisionAndNeverUndoLocalInput() {
        SessionPlayer player; AddonSessions sessions(player.backend());
        const QString package = QStringLiteral("org.example.party");
        const auto lease = sessions.authorize(package, {QStringLiteral("play"), QStringLiteral("pause")});
        QSignalSpy done(&sessions, &AddonSessions::commandFinished);
        const auto pause = player.command(QStringLiteral("pause")); sessions.submit(package, lease, pause);
        QCOMPARE(status(done), QStringLiteral("applied")); QVERIFY(player.state.value("paused").toBool());
        auto duplicate = pause; duplicate.insert("expectedRevision", player.state.value("transportRevision"));
        sessions.submit(package, lease, duplicate); QCOMPARE(status(done), QStringLiteral("rejected"));
        sessions.localIntervention(QStringLiteral("pause"));
        sessions.submit(package, lease, player.command(QStringLiteral("play"))); QCOMPARE(status(done), QStringLiteral("rejected"));
        QVERIFY(sessions.observe(package, lease).value("suspended").toBool());
        QVERIFY(sessions.resume(package, lease)); sessions.submit(package, lease, player.command(QStringLiteral("play")));
        QCOMPARE(status(done), QStringLiteral("applied")); QVERIFY(!player.state.value("paused").toBool());
    }
    void rateCorrectionRestoresOnlyItsOwnState() {
        SessionPlayer player; AddonSessions sessions(player.backend());
        const QString package = QStringLiteral("org.example.party");
        const auto lease = sessions.authorize(package, {QStringLiteral("setRate"), QStringLiteral("seek")});
        QSignalSpy done(&sessions, &AddonSessions::commandFinished);
        sessions.submit(package, lease, player.command(QStringLiteral("seek"), {{"positionUs", -1}})); QCOMPARE(status(done), QStringLiteral("rejected"));
        sessions.submit(package, lease, player.command(QStringLiteral("setRate"), {{"rate", 1.03}, {"durationMs", 5000}}));
        QCOMPARE(status(done), QStringLiteral("applied")); QCOMPARE(player.state.value("rate").toDouble(), 1.03);
        sessions.end(package, lease); QCOMPARE(player.state.value("rate").toDouble(), 1.0);
        const auto next = sessions.authorize(package, {QStringLiteral("setRate")});
        sessions.submit(package, next, player.command(QStringLiteral("setRate"), {{"rate", 1.02}, {"durationMs", 5000}}));
        QCOMPARE(status(done), QStringLiteral("applied"));
        player.state.insert("rate", 1.25); sessions.localIntervention(QStringLiteral("setRate")); sessions.end(package, next);
        QCOMPARE(player.state.value("rate").toDouble(), 1.25);
    }
    void scheduledCommandsRejectNewEpochAndExpiredLease() {
        SessionPlayer player; AddonSessions sessions(player.backend()); const QString package = QStringLiteral("org.example.party");
        const auto lease = sessions.authorize(package, {QStringLiteral("seek")}, 1000);
        QSignalSpy done(&sessions, &AddonSessions::commandFinished);
        auto seek = player.command(QStringLiteral("seek"), {{"positionUs", 20000000}}); seek.insert("atUs", player.time + 30000);
        sessions.submit(package, lease, seek); player.state.insert("mediaEpoch", "media-2");
        QCOMPARE(status(done), QStringLiteral("rejected")); QVERIFY(player.applied.isEmpty());
        player.state.insert("mediaEpoch", "media-1"); player.time += 2000000;
        sessions.submit(package, lease, player.command(QStringLiteral("seek"), {{"positionUs", 20000000}}));
        QCOMPARE(status(done), QStringLiteral("rejected"));
        QVERIFY(!sessions.renew(package, lease));
    }
    void lateAndMalformedCommandsDoNotReachPlayer() {
        SessionPlayer player; AddonSessions sessions(player.backend()); const QString package = QStringLiteral("org.example.party");
        const auto lease = sessions.authorize(package, {QStringLiteral("pause"), QStringLiteral("setRate")});
        QSignalSpy done(&sessions, &AddonSessions::commandFinished);
        auto command = player.command(QStringLiteral("pause")); command.insert("atUs", QStringLiteral("1000000"));
        sessions.submit(package, lease, command); QCOMPARE(status(done), QStringLiteral("rejected"));
        command = player.command(QStringLiteral("setRate"), {{"rate", 1.02}, {"durationMs", 12.5}});
        sessions.submit(package, lease, command); QCOMPARE(status(done), QStringLiteral("rejected"));
        command = player.command(QStringLiteral("pause")); command.insert("atUs", player.time + 30000);
        sessions.submit(package, lease, command); player.time += 500000;
        QCOMPARE(status(done), QStringLiteral("rejected")); QVERIFY(player.applied.isEmpty());
    }
    void reentrantEndRestoresRateAndHasOneTerminalResult() {
        SessionPlayer player; auto backend = player.backend(); const QString package = QStringLiteral("org.example.party");
        AddonSessions* owner = nullptr; QString lease; bool first = true;
        const auto apply = backend.apply;
        backend.apply = [&](const QString& operation, const QVariantMap& parameters, const QString& origin) {
            const bool result = apply(operation, parameters, origin);
            if (first) { first = false; owner->end(package, lease); }
            return result;
        };
        AddonSessions sessions(std::move(backend)); owner = &sessions;
        lease = sessions.authorize(package, {QStringLiteral("setRate")});
        QSignalSpy done(&sessions, &AddonSessions::commandFinished);
        sessions.submit(package, lease, player.command(QStringLiteral("setRate"), {{"rate", 1.02}, {"durationMs", 5000}}));
        QCOMPARE(status(done), QStringLiteral("cancelled")); QTest::qWait(20); QVERIFY(done.isEmpty());
        QCOMPARE(player.state.value("rate").toDouble(), 1.0);
        QVERIFY(sessions.observe(package, lease).isEmpty());
    }
    void closeDoesNotPermitReentrantAuthorization() {
        SessionPlayer player; AddonSessions sessions(player.backend()); const QString package = QStringLiteral("org.example.party");
        const auto lease = sessions.authorize(package, {QStringLiteral("pause")}); QVERIFY(!lease.isEmpty());
        QString next;
        connect(&sessions, &AddonSessions::sessionChanged, &sessions, [&](const QString&, const QString& state) {
            if (state == QLatin1String("ended")) next = sessions.authorize(package, {});
        });
        sessions.close(); QVERIFY(next.isEmpty()); QVERIFY(sessions.observe(package, lease).isEmpty());
        QVERIFY(sessions.authorize(package, {}).isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestAddonSessions)
#include "test_addon_sessions.moc"
