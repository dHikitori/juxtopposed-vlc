// SPDX-License-Identifier: GPL-2.0-or-later
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QJsonDocument>
#include "addons/addon_discovery_coordinator.hpp"

namespace {
const QString sourceId = QStringLiteral("org.example.discovery/catalogue");
QVariantMap source() {
    return {{"id", sourceId}, {"packageId", "org.example.discovery"}, {"name", "Fixture catalogue"},
        {"enabled", true}, {"configured", true}, {"cachePartition", "release-1:account-1:config-1"},
        {"accountReference", "account-1"}, {"operations", QStringList{"roots", "browse", "search", "suggest", "resolve", "subscribe"}}};
}
QVariantMap media(const QString& id = QStringLiteral("station-1")) {
    return {{"id", id}, {"kind", "media"}, {"title", "Example radio"}, {"mediaKind", "musicTrack"},
        {"url", "https://invalid.example/never-exposed?token=private"}, {"options", "sout=forbidden"}};
}
QVariantMap node() { return {{"id", "radio-root"}, {"kind", "node"}, {"title", "Radio"}}; }
struct Fake {
    struct Call { QString id, source, operation; QVariantMap context; };
    QVariantList registrations{source()};
    QList<Call> calls;
    QStringList canceled, canceledGates;
    QVariantList proposals;
    QHash<QString, AddonDiscoveryCoordinator::PlayableCompletion> completions;
    bool playable = false;
    AddonDiscoveryCoordinator::Backend backend() {
        AddonDiscoveryCoordinator::Backend value;
        value.sources = [this] { return registrations; };
        value.invoke = [this](const QString& source, const QString& operation, const QVariantMap& context) {
            const QString id = QStringLiteral("discovery-%1").arg(calls.size());
            calls.append({id, source, operation, context}); return id;
        };
        value.cancel = [this](const QString& id) { canceled.append(id); };
        if (playable) {
            value.authorizePlayable = [this](const QString&, const QVariantMap& proposal, AddonDiscoveryCoordinator::PlayableCompletion completion) {
                const QString id = QStringLiteral("gate-%1").arg(proposals.size());
                proposals.append(proposal); completions.insert(id, std::move(completion)); return id;
            };
            value.cancelPlayable = [this](const QString& id) { canceledGates.append(id); };
        }
        return value;
    }
};
void complete(AddonDiscoveryCoordinator& discovery, const Fake::Call& call, const QVariantMap& data,
              const QString& status = QStringLiteral("match")) {
    discovery.operationFinished(call.id, call.source, status, data, {});
}
QVariantMap page(const QVariantList& items) { return {{"items", items}}; }
QString firstRef(const QSignalSpy& results) {
    return results.last()[2].toMap().value("items").toList().first().toMap().value("ref").toString();
}
}

class TestAddonDiscovery : public QObject
{
    Q_OBJECT
private slots:
    void hostProfileReportsUnimplementedPlaybackAndSubscriptions() {
        Fake fake; AddonDiscoveryCoordinator discovery(fake.backend());
        QVERIFY(discovery.supports(sourceId, "roots"));
        QVERIFY(!discovery.supports(sourceId, "resolve"));
        QVERIFY(!discovery.supports(sourceId, "subscribe"));
        const auto operations = discovery.sources()[0].toMap().value("operations").toStringList();
        QVERIFY(!operations.contains("resolve")); QVERIFY(!operations.contains("subscribe"));
    }
    void operationDeclarationMayComeFromTheValidatedContributionDocument() {
        Fake fake;
        auto descriptor = source(); descriptor.remove("operations");
        descriptor["document"] = QVariantMap{{"operations", QStringList{"roots"}}};
        fake.registrations[0] = descriptor;
        AddonDiscoveryCoordinator discovery(fake.backend());
        QVERIFY(discovery.supports(sourceId, "roots")); QVERIFY(!discovery.supports(sourceId, "browse"));
    }
    void rootsIssueOpaqueReferencesWithoutURLsOrPlayerOptions() {
        Fake fake; AddonDiscoveryCoordinator discovery(fake.backend());
        QSignalSpy done(&discovery, &AddonDiscoveryCoordinator::finished);
        discovery.roots(sourceId, "view", 1);
        QTRY_COMPARE(fake.calls.size(), 1);
        complete(discovery, fake.calls[0], page({node(), media()}));
        QTRY_COMPARE(done.size(), 1);
        const auto items = done[0][2].toMap().value("items").toList();
        QCOMPARE(items.size(), 2);
        QVERIFY(items[0].toMap().value("ref").toString() != "radio-root");
        QVERIFY(!items[1].toMap().contains("url")); QVERIFY(!items[1].toMap().contains("options"));
        const auto queue = discovery.queueReference(items[1].toMap().value("ref").toString());
        QCOMPARE(queue.value("itemId").toString(), "station-1");
        QVERIFY(!QJsonDocument::fromVariant(queue).toJson().contains("never-exposed"));
    }
    void browseRejectsForgedAndCrossViewNodeReferences() {
        Fake fake; AddonDiscoveryCoordinator discovery(fake.backend());
        QSignalSpy done(&discovery, &AddonDiscoveryCoordinator::finished);
        discovery.browse("forged", "view", 1); QTRY_COMPARE(done.size(), 1); QVERIFY(fake.calls.isEmpty());
        discovery.roots(sourceId, "view", 1); QTRY_COMPARE(fake.calls.size(), 1);
        complete(discovery, fake.calls[0], page({node()})); QTRY_COMPARE(done.size(), 2);
        const auto ref = firstRef(done);
        discovery.browse(ref, "other", 1); QTRY_COMPARE(done.size(), 3); QCOMPARE(fake.calls.size(), 1);
        discovery.browse(ref, "view", 1); QTRY_COMPARE(fake.calls.size(), 2);
        QCOMPARE(fake.calls[1].context.value("nodeRef").toString(), "radio-root");
    }
    void cursorsBindQueryViewAndAccountAndDeduplicateStableItems() {
        Fake fake; AddonDiscoveryCoordinator discovery(fake.backend());
        QSignalSpy done(&discovery, &AddonDiscoveryCoordinator::finished);
        discovery.search(sourceId, "jazz", {}, "view", 1); QTRY_COMPARE(fake.calls.size(), 1);
        complete(discovery, fake.calls[0], {{"items", QVariantList{media()}}, {"cursor", QVariantMap{{"offset", 2}}}});
        QTRY_COMPARE(done.size(), 1);
        const auto first = firstRef(done); const auto cursor = done[0][2].toMap().value("cursor").toString();
        QVERIFY(!cursor.isEmpty());
        discovery.search(sourceId, "different", {}, "view", 1, cursor); QTRY_COMPARE(done.size(), 2); QCOMPARE(fake.calls.size(), 1);
        discovery.search(sourceId, "jazz", {}, "view", 1, cursor); QTRY_COMPARE(fake.calls.size(), 2);
        QCOMPARE(fake.calls[1].context.value("cursor").toMap().value("offset").toInt(), 2);
        complete(discovery, fake.calls[1], page({media(), media("station-2"), media("station-2")}));
        QTRY_COMPARE(done.size(), 3);
        const auto data = done.last()[2].toMap();
        QCOMPARE(data.value("items").toList().size(), 1); QCOMPARE(data.value("updates").toList().size(), 1);
        QCOMPARE(data.value("updates").toList()[0].toMap().value("ref").toString(), first);
    }
    void rootsPaginationKeepsCursorAuthorityInTheHost() {
        Fake fake; AddonDiscoveryCoordinator discovery(fake.backend());
        QSignalSpy done(&discovery, &AddonDiscoveryCoordinator::finished);
        discovery.roots(sourceId, "view", 1); QTRY_COMPARE(fake.calls.size(), 1);
        complete(discovery, fake.calls[0], {{"items", QVariantList{media()}}, {"cursor", QVariantMap{{"offset", 2}}}});
        QTRY_COMPARE(done.size(), 1);
        const auto cursor = done.last()[2].toMap().value("cursor").toString(); QVERIFY(!cursor.isEmpty());
        discovery.roots(sourceId, "other-view", 1, cursor); QTRY_COMPARE(done.size(), 2); QCOMPARE(fake.calls.size(), 1);
        discovery.search(sourceId, "jazz", {}, "view", 1, cursor); QTRY_COMPARE(done.size(), 3); QCOMPARE(fake.calls.size(), 1);
        discovery.roots(sourceId, "view", 1, cursor); QTRY_COMPARE(fake.calls.size(), 2);
        QCOMPARE(fake.calls.last().operation, QStringLiteral("roots"));
        QCOMPARE(fake.calls.last().context.value("cursor").toMap().value("offset").toInt(), 2);
        complete(discovery, fake.calls.last(), page({media("station-2")})); QTRY_COMPARE(done.size(), 4);
        QCOMPARE(done.last()[2].toMap().value("items").toList().size(), 1);
    }
    void changingGenerationCancelsJobsAndRevokesHandles() {
        Fake fake; AddonDiscoveryCoordinator discovery(fake.backend());
        QSignalSpy done(&discovery, &AddonDiscoveryCoordinator::finished);
        discovery.roots(sourceId, "view", 1); QTRY_COMPARE(fake.calls.size(), 1);
        complete(discovery, fake.calls[0], page({media()})); QTRY_COMPARE(done.size(), 1);
        const auto old = firstRef(done);
        discovery.search(sourceId, "jazz", {}, "view", 1); QTRY_COMPARE(fake.calls.size(), 2);
        discovery.roots(sourceId, "view", 2); QTRY_COMPARE(fake.calls.size(), 3);
        QCOMPARE(fake.canceled, QStringList{fake.calls[1].id});
        QVERIFY(discovery.queueReference(old).isEmpty());
        complete(discovery, fake.calls[1], page({media("stale")}));
        complete(discovery, fake.calls[2], page({media("current")})); QTRY_COMPARE(done.size(), 2);
        QCOMPARE(done.last()[2].toMap().value("generation").toULongLong(), quint64(2));
    }
    void sourceOrAccountChangeRejectsInflightResults() {
        Fake fake; AddonDiscoveryCoordinator discovery(fake.backend());
        QSignalSpy done(&discovery, &AddonDiscoveryCoordinator::finished);
        discovery.roots(sourceId, "view", 1); QTRY_COMPARE(fake.calls.size(), 1);
        auto changed = fake.registrations[0].toMap(); changed["cachePartition"] = "release-2:account-2"; fake.registrations[0] = changed;
        complete(discovery, fake.calls[0], page({media()})); QTRY_COMPARE(done.size(), 1);
        QCOMPARE(done[0][1].toString(), "error"); QVERIFY(!done[0][2].toMap().contains("items"));
    }
    void closedViewCannotBeReopenedByAnOldGeneration() {
        Fake fake; AddonDiscoveryCoordinator discovery(fake.backend());
        QSignalSpy done(&discovery, &AddonDiscoveryCoordinator::finished);
        discovery.roots(sourceId, "view", 4); QTRY_COMPARE(fake.calls.size(), 1);
        complete(discovery, fake.calls[0], page({media()})); QTRY_COMPARE(done.size(), 1);
        discovery.cancelView("view");
        discovery.roots(sourceId, "view", 4); QTRY_COMPARE(done.size(), 2);
        QCOMPARE(fake.calls.size(), 1); QCOMPARE(done.last()[1].toString(), "error");
        discovery.roots(sourceId, "view", 5); QTRY_COMPARE(fake.calls.size(), 2);
    }
    void suggestionsAreBoundedDataAndExcludeCallerPrivateState() {
        Fake fake; AddonDiscoveryCoordinator discovery(fake.backend());
        QSignalSpy done(&discovery, &AddonDiscoveryCoordinator::finished);
        discovery.suggest(sourceId, {{"url", "file:///private/file.mkv"}, {"language", "en"}, {"queue", QVariantList{"private"}}}, 2, "view", 1);
        QTRY_COMPARE(fake.calls.size(), 1);
        QCOMPARE(fake.calls[0].operation, "suggest"); QVERIFY(!fake.calls[0].context.contains("url")); QVERIFY(!fake.calls[0].context.contains("queue"));
        complete(discovery, fake.calls[0], page({media(), media("two"), media("three")})); QTRY_COMPARE(done.size(), 1);
        QCOMPARE(done[0][1].toString(), "error");
    }
    void queueReferencesResolveLazilyAfterViewCloses() {
        Fake fake; fake.playable = true; AddonDiscoveryCoordinator discovery(fake.backend());
        QSignalSpy done(&discovery, &AddonDiscoveryCoordinator::finished);
        discovery.roots(sourceId, "browse", 1); QTRY_COMPARE(fake.calls.size(), 1);
        complete(discovery, fake.calls[0], page({media()})); QTRY_COMPARE(done.size(), 1);
        const auto queue = discovery.queueReference(firstRef(done));
        discovery.cancelView("browse");
        auto release = fake.registrations[0].toMap(); release["cachePartition"] = "release-2:account-1:config-2"; fake.registrations[0] = release;
        discovery.resolveQueued(queue, "play", "queue-player", 9); QTRY_COMPARE(fake.calls.size(), 2);
        QCOMPARE(fake.calls[1].operation, "resolve"); QCOMPARE(fake.calls[1].context.value("itemRef").toString(), "station-1");
        QVERIFY(fake.proposals.isEmpty());
    }
    void playbackIsDeniedBeforeProviderInvocationWithoutATrustedAdapter() {
        Fake fake; AddonDiscoveryCoordinator discovery(fake.backend());
        QSignalSpy done(&discovery, &AddonDiscoveryCoordinator::finished);
        discovery.roots(sourceId, "view", 1); QTRY_COMPARE(fake.calls.size(), 1);
        complete(discovery, fake.calls[0], page({media()})); QTRY_COMPARE(done.size(), 1);
        discovery.resolve(firstRef(done), "play", "view", 1); QTRY_COMPARE(done.size(), 2);
        QCOMPARE(done.last()[1].toString(), "unsupported"); QCOMPARE(fake.calls.size(), 1);
    }
    void actualPlaybackRequiresGateCompletionAndOnlyReturnsAnOpaqueHandle() {
        Fake fake; fake.playable = true; AddonDiscoveryCoordinator discovery(fake.backend());
        QSignalSpy done(&discovery, &AddonDiscoveryCoordinator::finished);
        discovery.roots(sourceId, "view", 1); QTRY_COMPARE(fake.calls.size(), 1);
        complete(discovery, fake.calls[0], page({media()})); QTRY_COMPARE(done.size(), 1);
        discovery.resolve(firstRef(done), "play", "view", 1); QTRY_COMPARE(fake.calls.size(), 2);
        complete(discovery, fake.calls[1], {{"url", "https://public.example/radio.m3u8"}, {"hostContext", QVariantMap{{"intent", "forged"}}}});
        QTRY_COMPARE(fake.proposals.size(), 1); QCOMPARE(done.size(), 1);
        QCOMPARE(fake.proposals[0].toMap().value("hostContext").toMap().value("intent").toString(), "play");
        fake.completions.value("gate-0")("playable:host-issued-1", {});
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(done.last()[2].toMap().value("playbackHandle").toString(), "playable:host-issued-1");
        QVERIFY(!done.last()[2].toMap().contains("url"));
    }
    void unsafeProtocolAndVlcOptionsNeverReachTheGate_data() {
        QTest::addColumn<QVariantMap>("proposal");
        QTest::newRow("file") << QVariantMap{{"url", "file:///private/movie.mkv"}};
        QTest::newRow("http") << QVariantMap{{"url", "http://public.example/radio"}};
        QTest::newRow("options") << QVariantMap{{"url", "https://public.example/radio"}, {"options", ":sout=bad"}};
        QTest::newRow("userinfo") << QVariantMap{{"url", "https://name:secret@public.example/radio"}};
    }
    void unsafeProtocolAndVlcOptionsNeverReachTheGate() {
        QFETCH(QVariantMap, proposal);
        Fake fake; fake.playable = true; AddonDiscoveryCoordinator discovery(fake.backend());
        QSignalSpy done(&discovery, &AddonDiscoveryCoordinator::finished);
        discovery.roots(sourceId, "view", 1); QTRY_COMPARE(fake.calls.size(), 1);
        complete(discovery, fake.calls[0], page({media()})); QTRY_COMPARE(done.size(), 1);
        discovery.resolve(firstRef(done), "play", "view", 1); QTRY_COMPARE(fake.calls.size(), 2);
        complete(discovery, fake.calls[1], proposal); QTRY_COMPARE(done.size(), 2);
        QCOMPARE(done.last()[1].toString(), "error"); QVERIFY(fake.proposals.isEmpty());
    }
    void cancelWhileAuthorizingRejectsLatePlaybackHandles() {
        Fake fake; fake.playable = true; AddonDiscoveryCoordinator discovery(fake.backend());
        QSignalSpy done(&discovery, &AddonDiscoveryCoordinator::finished);
        discovery.roots(sourceId, "view", 1); QTRY_COMPARE(fake.calls.size(), 1);
        complete(discovery, fake.calls[0], page({media()})); QTRY_COMPARE(done.size(), 1);
        const auto request = discovery.resolve(firstRef(done), "play", "view", 1); QTRY_COMPARE(fake.calls.size(), 2);
        complete(discovery, fake.calls[1], {{"url", "https://public.example/radio"}}); QTRY_COMPARE(fake.proposals.size(), 1);
        discovery.cancel(request); QCOMPARE(fake.canceledGates, QStringList{"gate-0"});
        fake.completions.value("gate-0")("late-handle", {}); QTest::qWait(1); QCOMPARE(done.size(), 1);
    }
    void operationsMustBeDeclaredAndSubscriptionsStayExplicitlyUnsupported() {
        Fake fake; fake.registrations[0] = QVariantMap{{"id", sourceId}, {"enabled", true}, {"configured", true}, {"cachePartition", "a"}};
        AddonDiscoveryCoordinator discovery(fake.backend()); QSignalSpy done(&discovery, &AddonDiscoveryCoordinator::finished);
        discovery.roots(sourceId, "view", 1); QTRY_COMPARE(done.size(), 1); QCOMPARE(done[0][1].toString(), "unsupported");
        discovery.subscribe("any", "view", 1); QTRY_COMPARE(done.size(), 2); QCOMPARE(done.last()[1].toString(), "unsupported");
        QVERIFY(fake.calls.isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestAddonDiscovery)
#include "test_addon_discovery.moc"
