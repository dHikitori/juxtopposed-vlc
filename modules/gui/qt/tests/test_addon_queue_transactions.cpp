// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QSignalSpy>
#include "addons/addon_queue_transactions.hpp"

using namespace vlc::addons;
class TestAddonQueueTransactions : public QObject
{
    Q_OBJECT
    static QVariantMap reference() { return {{"format", "hakone-addon-source-v1"}, {"source", "org.example.radio/source"},
        {"itemId", "station-42"}, {"accountReference", "default"}, {"snapshot", QVariantMap{{"title", "Station"}, {"mediaKind", "audio"}}}}; }
private slots:
    void validatesOnlySourceReferences() {
        const auto ref = reference(); QVERIFY(AddonQueueTransactions::validReference(ref));
        auto bad = ref; bad.insert("url", "https://private.example/stream"); QVERIFY(!AddonQueueTransactions::validReference(bad));
        bad = ref; bad.insert("format", "vlc-addon-source-v1"); QVERIFY(!AddonQueueTransactions::validReference(bad));
        bad = ref; bad.insert("options", QStringList{"sout"}); QVERIFY(!AddonQueueTransactions::validReference(bad));
        bad = ref; bad.insert("source", "../../core"); QVERIFY(!AddonQueueTransactions::validReference(bad));
    }
    void commitsOneImmutableBatchAndOneCompletion() {
        int commits = 0; AddonQueueTransactions::Completion completion; AddonQueueTransactions::Transaction staged;
        AddonQueueTransactions adapter({[](const QString&) { return AddonQueueTransactions::Snapshot{true, "queue-1", 4}; },
            [&](const auto& transaction, auto done) { ++commits; staged = transaction; completion = done; return []{}; }, {}, {}});
        QSignalSpy finished(&adapter, &AddonQueueTransactions::finished);
        auto ref = reference(); const auto id = adapter.append("view-1", {ref});
        ref.insert("itemId", "Changed");
        QCOMPARE(commits, 1); QCOMPARE(staged.expectedRevision, 4u); QCOMPARE(staged.targetIdentity, QStringLiteral("queue-1"));
        QCOMPARE(staged.references[0].toMap().value("itemId").toString(), QStringLiteral("station-42"));
        completion(true, {}); completion(false, QStringLiteral("Late failure"));
        QTRY_COMPARE(finished.size(), 1); QCOMPARE(finished.last()[0].toString(), id); QVERIFY(finished.last()[1].toBool());
    }
    void nativeAdapterDoesTheLastRevisionCheck() {
        quint64 revision = 4; int applied = 0;
        AddonQueueTransactions adapter({[&](const QString&) { return AddonQueueTransactions::Snapshot{true, "queue-1", revision}; },
            [&](const auto& transaction, auto done) {
                ++revision;
                if (transaction.expectedRevision == revision) { ++applied; done(true, {}); }
                else done(false, QStringLiteral("The queue changed."));
                return []{};
            }, {}, {}});
        QSignalSpy finished(&adapter, &AddonQueueTransactions::finished);
        adapter.append("view-1", {reference()}); QTRY_COMPARE(finished.size(), 1);
        QVERIFY(!finished.last()[1].toBool()); QCOMPARE(applied, 0);
    }
    void playbackNeedsMatchingTargetAndReleasesUnusedHandle() {
        int plays = 0; QStringList released;
        AddonQueueTransactions adapter({[](const QString&) { return AddonQueueTransactions::Snapshot{true, "queue-1", 9}; }, {},
            [&](const auto&, auto done) { ++plays; done(true, {}); return []{}; }, [&](const auto& handle) { released.append(handle); }});
        QSignalSpy finished(&adapter, &AddonQueueTransactions::finished);
        adapter.play("view-1", reference(), "playable:1", {true, "queue-1", 8});
        QTRY_COMPARE(finished.size(), 1); QVERIFY(!finished.last()[1].toBool()); QCOMPARE(plays, 0);
        QCOMPARE(released, QStringList{QStringLiteral("playable:1")});
        adapter.play("view-1", reference(), "playable:2", {true, "queue-1", 9});
        QTRY_COMPARE(finished.size(), 2); QVERIFY(finished.last()[1].toBool()); QCOMPARE(plays, 1);
    }
    void canceledAndDestroyedViewsIgnoreLateCompletions() {
        AddonQueueTransactions::Completion completion; int canceled = 0;
        auto adapter = new AddonQueueTransactions({[](const QString&) { return AddonQueueTransactions::Snapshot{true, "queue-1", 1}; },
            [&](const auto&, auto done) { completion = done; return [&]{ ++canceled; }; }, {}, {}});
        QSignalSpy finished(adapter, &AddonQueueTransactions::finished);
        adapter->append("view-1", {reference()}); adapter->cancelView("view-1");
        QCOMPARE(canceled, 1); QCOMPARE(finished.size(), 1);
        completion(true, {}); QTest::qWait(1); QCOMPARE(finished.size(), 1);
        adapter->append("view-2", {reference()}); delete adapter; QCOMPARE(canceled, 2);
        completion(true, {}); QTest::qWait(1);
    }
};
QTEST_GUILESS_MAIN(TestAddonQueueTransactions)
#include "test_addon_queue_transactions.moc"
