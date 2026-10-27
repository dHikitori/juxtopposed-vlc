// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_player_sessions.hpp"
#include "addons/addon_manager.hpp"
#include "addons/addon_native_session_player.hpp"
#include "addons/addon_session_host.hpp"
#include "addons/addon_session_ui.hpp"
#include "session-runtime/native_session_fixture.h"
#include <QTest>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <memory>
#include <utility>

using namespace vlc::addons;
namespace vlc::addons {
class AddonPlayerSessionsTestAccess {
public:
    static SessionPlayerBackend player(AddonPlayerSessions& owner) { return owner.m_player->backend(); }
};
}
namespace {
QStringList sessionOperations(AddonManager& manager) {
    auto operations=manager.hostServices()->availableOperations();
    for(const auto& name:QStringList{QStringLiteral("storage.get"),QStringLiteral("storage.set"),QStringLiteral("storage.remove")}) operations.removeAll(name);
    return operations;
}
struct Fixture {
    native_session_fixture* native=native_session_create();
    ~Fixture() { native_session_destroy(native); }
};
}
class TestAddonPlayerSessions : public QObject {
    Q_OBJECT
private slots:
    void activationPublishesTheCompleteHostAndCloseRemovesIt() {
        Fixture fixture; QVERIFY(fixture.native); QTemporaryDir directory; QObject playerLifetime;
        AddonStreamLifetime lifetime; AddonManager manager(directory.path());
        AddonPlayerSessions sessions(&manager,native_session_player(fixture.native),&playerLifetime,lifetime.handle());
        QVERIFY(sessions.ui()); QVERIFY(!sessions.active());
        QVERIFY(!manager.capabilities().value("webSocketAvailable").toBool());
        QVERIFY(sessions.activate()); QVERIFY(sessions.active()); QVERIFY(!sessions.activate());
        QVERIFY(manager.capabilities().value("webSocketAvailable").toBool());
        QCOMPARE(sessionOperations(manager).size(),8);
        QVERIFY(sessions.ui()->sessions().isEmpty()); QVERIFY(!native_session_active(fixture.native));
        sessions.close(); sessions.close(); QVERIFY(!sessions.active());
        QVERIFY(!manager.capabilities().value("webSocketAvailable").toBool());
        QVERIFY(sessionOperations(manager).isEmpty()); QVERIFY(!sessions.activate());
    }
    void closeRevokesTheNativeLeaseBeforeUiNotification() {
        Fixture fixture; QVERIFY(fixture.native); QTemporaryDir directory; QObject playerLifetime;
        AddonStreamLifetime lifetime; AddonManager manager(directory.path());
        AddonPlayerSessions sessions(&manager,native_session_player(fixture.native),&playerLifetime,lifetime.handle());
        QVERIFY(sessions.activate()); auto backend=AddonPlayerSessionsTestAccess::player(sessions);
        const SessionPlayerId lease{401,501};
        QCOMPARE(backend.grant(backend.snapshot().stamp,lease,15,30000000),SessionPlayerResult::Accepted);
        QVERIFY(native_session_active(fixture.native));
        unsigned notifications=0;
        connect(sessions.ui(),&AddonSessionUi::changed,this,[&] {
            ++notifications; QVERIFY(!native_session_active(fixture.native));
            QVERIFY(sessionOperations(manager).isEmpty());
        });
        sessions.close(); QVERIFY(notifications>0); QVERIFY(!native_session_active(fixture.native));
        QCOMPARE(backend.grant({},lease,15,30000000),SessionPlayerResult::Closed);
    }
    void nativePlayerLifetimeClosesTheAssembly() {
        Fixture fixture; QVERIFY(fixture.native); QTemporaryDir directory;
        AddonStreamLifetime lifetime; AddonManager manager(directory.path());
        auto playerLifetime=std::make_unique<QObject>();
        AddonPlayerSessions sessions(&manager,native_session_player(fixture.native),playerLifetime.get(),lifetime.handle());
        QVERIFY(sessions.activate()); playerLifetime.reset();
        QVERIFY(!sessions.active()); QVERIFY(sessionOperations(manager).isEmpty());
        QVERIFY(!sessions.ui()->prepare(QStringLiteral("org.hakone.test/controller"),QStringLiteral("wss://relay.example"),QStringLiteral("Room")));
    }
    void destructionDoesNotPublishAnIntermediateUiState() {
        Fixture fixture; QVERIFY(fixture.native); QTemporaryDir directory; QObject playerLifetime;
        AddonStreamLifetime lifetime; AddonManager manager(directory.path());
        auto sessions=std::make_unique<AddonPlayerSessions>(&manager,native_session_player(fixture.native),&playerLifetime,lifetime.handle());
        QVERIFY(sessions->activate()); unsigned notifications=0;
        connect(sessions->ui(),&AddonSessionUi::changed,this,[&] { ++notifications; });
        sessions.reset(); QCOMPARE(notifications,0u); QVERIFY(sessionOperations(manager).isEmpty());
    }
    void managerRetirementClosesBeforeItsMembersAreDestroyed() {
        Fixture fixture; QVERIFY(fixture.native); QTemporaryDir directory; QObject playerLifetime;
        AddonStreamLifetime lifetime; auto manager=std::make_unique<AddonManager>(directory.path());
        AddonPlayerSessions sessions(manager.get(),native_session_player(fixture.native),&playerLifetime,lifetime.handle());
        QVERIFY(sessions.activate()); auto backend=AddonPlayerSessionsTestAccess::player(sessions);
        const SessionPlayerId lease{402,502};
        QCOMPARE(backend.grant(backend.snapshot().stamp,lease,15,30000000),SessionPlayerResult::Accepted);
        manager.reset(); QVERIFY(!sessions.active()); QVERIFY(!native_session_active(fixture.native));
        QCOMPARE(backend.grant({},lease,15,30000000),SessionPlayerResult::Closed);
        sessions.close();
    }
    void aClosedAppCannotPublishSessionOperations() {
        Fixture fixture; QVERIFY(fixture.native); QTemporaryDir directory; QObject playerLifetime;
        AddonStreamLifetime lifetime; AddonManager manager(directory.path());
        AddonPlayerSessions sessions(&manager,native_session_player(fixture.native),&playerLifetime,lifetime.handle());
        lifetime.close(); QVERIFY(!sessions.activate()); QVERIFY(!sessions.active());
        QVERIFY(sessionOperations(manager).isEmpty()); sessions.close();
    }
    void aConflictingHandlerRollsBackOnlyTheSessionMount() {
        Fixture fixture; QVERIFY(fixture.native); QTemporaryDir directory; QObject playerLifetime,existing;
        AddonStreamLifetime lifetime; AddonManager manager(directory.path());
        QVERIFY(manager.hostServices()->registerOperation(QStringLiteral("playback.command"),&existing,
            [](const auto&,const auto&,auto complete) { complete({}); return AddonHostServices::Cancel{}; }));
        AddonPlayerSessions sessions(&manager,native_session_player(fixture.native),&playerLifetime,lifetime.handle());
        QVERIFY(!sessions.activate()); QVERIFY(!sessions.active());
        QCOMPARE(sessionOperations(manager),QStringList{QStringLiteral("playback.command")});
    }
    void anAvailabilityObserverCanDeleteTheAssemblyDuringMount() {
        Fixture fixture; QVERIFY(fixture.native); QTemporaryDir directory; QObject playerLifetime;
        AddonStreamLifetime lifetime; AddonManager manager(directory.path());
        auto* sessions=new AddonPlayerSessions(&manager,native_session_player(fixture.native),&playerLifetime,lifetime.handle());
        const auto cleanup=qScopeGuard([&] { delete std::exchange(sessions,nullptr); });
        connect(manager.hostServices(),&AddonHostServices::availabilityChanged,this,[&] { delete std::exchange(sessions,nullptr); });
        QVERIFY(!sessions->activate()); QVERIFY(!sessions);
        QVERIFY(sessionOperations(manager).isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestAddonPlayerSessions)
#include "test_addon_player_sessions.moc"
