// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QSignalSpy>
#include "addons/addon_discovery_service.hpp"

using namespace vlc::addons;
class TestAddonDiscoveryService : public QObject
{
    Q_OBJECT
    struct Fixture {
        QVariantList calls;
        QStringList canceled, released;
        QVariantList sources{{QVariantMap{{"id", "org.example.radio/source"}, {"name", "Radio"}, {"enabled", true}, {"configured", true},
            {"cachePartition", "release-1"}, {"operations", QStringList{"roots", "browse", "search", "resolve"}}}}};
        AddonDiscoveryCoordinator::Backend backend(bool playable = false) {
            AddonDiscoveryCoordinator::Backend result;
            result.sources = [this] { return sources; };
            result.invoke = [this](const QString& source, const QString& operation, const QVariantMap& context) {
                const auto id = QString::number(calls.size() + 1);
                calls.append(QVariantMap{{"id", id}, {"source", source}, {"operation", operation}, {"context", context}}); return id;
            };
            result.cancel = [this](const QString& id) { canceled.append(id); };
            if (playable) result.authorizePlayable = [](const QString&, const QVariantMap&, auto done) { done(QStringLiteral("playable:42"), {}); return QStringLiteral("gate-job"); };
            result.cancelPlayable = [](const QString&) {};
            return result;
        }
        void complete(AddonDiscoveryService& service, int index, const QVariantMap& data) {
            const auto call = calls[index].toMap();
            service.operationFinished(call.value("id").toString(), call.value("source").toString(), QStringLiteral("match"), data, {});
        }
        static QVariantMap item(const QString& id, const QString& kind = QStringLiteral("media")) {
            return {{"id", id}, {"kind", kind}, {"title", QStringLiteral("<b>Station</b>")}, {"mediaKind", "audio"},
                {"url", "https://example.org/private-stream"}, {"options", QStringList{"not-permitted"}}};
        }
    };
    static QVariantList items(AddonDiscoveryService& service, const QString& id) { return service.view(id).value("items").toList(); }
private slots:
    void browseAndSearchUseOnlyHostReferences() {
        Fixture fixture; AddonDiscoveryService service(fixture.backend(), {});
        const auto view = service.openSource("org.example.radio/source"); QVERIFY(!view.isEmpty());
        QTRY_COMPARE(fixture.calls.size(), 1);
        fixture.complete(service, 0, {{"items", QVariantList{Fixture::item("root", "node")}}});
        QTRY_COMPARE(items(service, view).size(), 1);
        const auto node = items(service, view)[0].toMap(); QVERIFY(node.value("ref").toString() != QLatin1String("root"));
        QVERIFY(!node.contains("url")); QVERIFY(!node.contains("options"));
        QVERIFY(service.browse(view, node.value("ref").toString())); QTRY_COMPARE(fixture.calls.size(), 2);
        QCOMPARE(fixture.calls[1].toMap().value("context").toMap().value("nodeRef").toString(), QStringLiteral("root"));
        fixture.complete(service, 1, {{"items", QVariantList{Fixture::item("station")}}});
        QTRY_COMPARE(items(service, view).size(), 1); QVERIFY(service.view(view).value("canGoBack").toBool());
        QVERIFY(service.search(view, "new query")); QTRY_COMPARE(fixture.calls.size(), 3);
        QVERIFY(service.search(view, "latest query")); QTRY_COMPARE(fixture.calls.size(), 4);
        QVERIFY(fixture.canceled.contains("3"));
        fixture.complete(service, 2, {{"items", QVariantList{Fixture::item("old-result")}}});
        fixture.complete(service, 3, {{"items", QVariantList{Fixture::item("new-result")}}});
        QTRY_COMPARE(items(service, view).size(), 1);
        QCOMPARE(service.view(view).value("query").toString(), QStringLiteral("latest query"));
        service.closeView(view); QVERIFY(service.view(view).isEmpty());
        QVERIFY(!service.browse(view, node.value("ref").toString()));
    }
    void missingPlayableAdapterDeniesBeforeResolve() {
        Fixture fixture; AddonDiscoveryService service(fixture.backend(), {});
        const auto view = service.openSource("org.example.radio/source"); QTRY_COMPARE(fixture.calls.size(), 1);
        fixture.complete(service, 0, {{"items", QVariantList{Fixture::item("station")}}}); QTRY_COMPARE(items(service, view).size(), 1);
        QVERIFY(!service.canPlay(view)); QVERIFY(!service.canEnqueue(view));
        QVERIFY(service.play(view, items(service, view)[0].toMap().value("ref").toString()).isEmpty());
        QCOMPARE(fixture.calls.size(), 1);
    }
    void queueReceivesOneReferenceBatchWithoutUrls() {
        Fixture fixture; QVariantList applied;
        AddonQueueTransactions::Backend queues;
        queues.snapshot = [](const QString& target) { return AddonQueueTransactions::Snapshot{target == QLatin1String("queue"), "queue-1", 1}; };
        queues.append = [&](const auto& transaction, auto done) { applied = transaction.references; done(true, {}); return []{}; };
        AddonDiscoveryService service(fixture.backend(), queues);
        const auto view = service.openSource("org.example.radio/source"); QTRY_COMPARE(fixture.calls.size(), 1);
        fixture.complete(service, 0, {{"items", QVariantList{Fixture::item("station")}}}); QTRY_COMPARE(items(service, view).size(), 1);
        const auto ref = items(service, view)[0].toMap().value("ref").toString();
        QSignalSpy finished(&service, &AddonDiscoveryService::actionFinished);
        QVERIFY(!service.enqueue(view, {ref}).isEmpty()); QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(applied.size(), 1); QVERIFY(!applied[0].toMap().contains("url")); QVERIFY(!applied[0].toMap().contains("options"));
        QCOMPARE(applied[0].toMap().value("itemId").toString(), QStringLiteral("station"));
        QVERIFY(service.enqueue(view, {ref, ref}).isEmpty());
        QVERIFY(service.enqueue(view, {QStringLiteral("station")}).isEmpty());
    }
    void queueChangeDuringResolveReleasesHandle() {
        Fixture fixture; quint64 revision = 1; int played = 0;
        AddonQueueTransactions::Backend queues;
        queues.snapshot = [&](const QString&) { return AddonQueueTransactions::Snapshot{true, "queue-1", revision}; };
        queues.play = [&](const auto&, auto done) { ++played; done(true, {}); return []{}; };
        queues.releasePlayable = [&](const QString& handle) { fixture.released.append(handle); };
        AddonDiscoveryService service(fixture.backend(true), queues);
        const auto view = service.openSource("org.example.radio/source"); QTRY_COMPARE(fixture.calls.size(), 1);
        fixture.complete(service, 0, {{"items", QVariantList{Fixture::item("station")}}}); QTRY_COMPARE(items(service, view).size(), 1);
        QSignalSpy finished(&service, &AddonDiscoveryService::actionFinished);
        QVERIFY(service.canPlay(view));
        QVERIFY(!service.play(view, items(service, view)[0].toMap().value("ref").toString()).isEmpty());
        QTRY_COMPARE(fixture.calls.size(), 2); ++revision;
        fixture.complete(service, 1, {{"url", "https://example.org/stream"}});
        QTRY_COMPARE(finished.size(), 1); QVERIFY(!finished.last()[1].toBool()); QCOMPARE(played, 0);
        QCOMPARE(fixture.released, QStringList{QStringLiteral("playable:42")});
    }
    void latestPlayCancelsAnEarlierResolve() {
        Fixture fixture; QString played;
        AddonQueueTransactions::Backend queues;
        queues.snapshot = [](const QString&) { return AddonQueueTransactions::Snapshot{true, "queue-1", 1}; };
        queues.play = [&](const auto& transaction, auto done) { played = transaction.references[0].toMap().value("itemId").toString(); done(true, {}); return []{}; };
        AddonDiscoveryService service(fixture.backend(true), queues);
        const auto view = service.openSource("org.example.radio/source"); QTRY_COMPARE(fixture.calls.size(), 1);
        fixture.complete(service, 0, {{"items", QVariantList{Fixture::item("first"), Fixture::item("second")}}}); QTRY_COMPARE(items(service, view).size(), 2);
        QVERIFY(!service.play(view, items(service, view)[0].toMap().value("ref").toString()).isEmpty()); QTRY_COMPARE(fixture.calls.size(), 2);
        QVERIFY(!service.play(view, items(service, view)[1].toMap().value("ref").toString()).isEmpty()); QTRY_COMPARE(fixture.calls.size(), 3);
        QVERIFY(fixture.canceled.contains("2"));
        fixture.complete(service, 1, {{"url", "https://example.org/first"}});
        fixture.complete(service, 2, {{"url", "https://example.org/second"}});
        QTRY_COMPARE(played, QStringLiteral("second"));
    }
    void rootPagesAppendWithoutExposingTheProviderCursor() {
        Fixture fixture; AddonDiscoveryService service(fixture.backend(), {});
        const auto view = service.openSource("org.example.radio/source"); QTRY_COMPARE(fixture.calls.size(), 1);
        fixture.complete(service, 0, {{"items", QVariantList{Fixture::item("first")}}, {"cursor", QVariantMap{{"offset", 2}}}});
        QTRY_COMPARE(items(service, view).size(), 1); QVERIFY(service.view(view).value("hasMore").toBool());
        QVERIFY(!service.view(view).contains("cursor")); QVERIFY(service.loadMore(view)); QTRY_COMPARE(fixture.calls.size(), 2);
        QCOMPARE(fixture.calls.last().toMap().value("operation").toString(), QStringLiteral("roots"));
        QCOMPARE(fixture.calls.last().toMap().value("context").toMap().value("cursor").toMap().value("offset").toInt(), 2);
        fixture.complete(service, 1, {{"items", QVariantList{Fixture::item("second")}}});
        QTRY_COMPARE(items(service, view).size(), 2); QVERIFY(!service.view(view).value("hasMore").toBool());
    }
};
QTEST_GUILESS_MAIN(TestAddonDiscoveryService)
#include "test_addon_discovery_service.moc"
