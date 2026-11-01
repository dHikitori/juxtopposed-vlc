// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QSignalSpy>
#include <QDir>
#include <QJsonDocument>
#include <QThread>
#include <vlc/vlc.h>
#include "lib/libvlc_internal.h"
#include <vlc_playlist.h>
#include <vlc_input_item.h>
#include <vlc_input_policy.h>
#include "addons/addon_native_queue_store.hpp"
#include "addons/addon_manager.hpp"
#include "addons/addon_player_discovery.hpp"

using namespace vlc::addons;
class TestAddonNativeQueueStore : public QObject
{
    Q_OBJECT
    libvlc_instance_t* instance = nullptr;
    vlc_playlist_t* playlist = nullptr;
    struct Locked { vlc_playlist_t* playlist; explicit Locked(vlc_playlist_t* value) : playlist(value) { vlc_playlist_Lock(playlist); } ~Locked() { vlc_playlist_Unlock(playlist); } };
    static QVariantMap reference(const QString& id = QStringLiteral("station-42")) {
        return {{"format", "hakone-addon-source-v1"}, {"source", "org.example.radio/source"}, {"itemId", id},
            {"accountReference", "default"}, {"snapshot", QVariantMap{{"title", "Station\nhttps://invalid.example/injected"}, {"mediaKind", "audio"}}}};
    }
    // The store saves the admission on a worker, then completes on this thread.
    struct Outcome { int calls = 0; bool success = false; QString error; AddonQueueTransactions::Cancel cancel; };
    static std::shared_ptr<Outcome> start(AddonNativeQueueStore& store, const AddonQueueTransactions::Transaction& transaction, bool play = false) {
        auto outcome = std::make_shared<Outcome>();
        const auto done = [outcome](bool success, const QString& error) { ++outcome->calls; outcome->success = success; outcome->error = error; };
        outcome->cancel = play ? store.playReference(transaction, done) : store.append(transaction, done);
        return outcome;
    }
    static bool finished(const std::shared_ptr<Outcome>& outcome) { return QTest::qWaitFor([&] { return outcome->calls > 0; }, 5000) && outcome->calls == 1; }
    static bool succeeds(AddonNativeQueueStore& store, const AddonQueueTransactions::Transaction& transaction, bool play = false) {
        const auto outcome = start(store, transaction, play); return finished(outcome) && outcome->success;
    }
    static bool refreshed(AddonNativeQueueStore& store, const QVariantMap& reference) {
        auto outcome = std::make_shared<Outcome>();
        store.refreshDisplay(reference, [outcome](bool success) { ++outcome->calls; outcome->success = success; });
        return finished(outcome) && outcome->success;
    }
    size_t count() { Locked lock(playlist); return vlc_playlist_Count(playlist); }
private slots:
    void initTestCase() {
        const auto selected=qgetenv("VLC_TEST_PLUGIN_PATH");
        qputenv("VLC_PLUGIN_PATH",selected.isEmpty()?QByteArray(VLC_ADDON_NATIVE_BUILD_ROOT "/modules"):selected);
        const char* options[] = {"--ignore-config", "--no-plugins-cache", "--no-media-library"};
        instance = libvlc_new(3, options); QVERIFY(instance);
        playlist = vlc_playlist_New(VLC_OBJECT(instance->p_libvlc_int), VLC_PLAYLIST_PREPARSING_DISABLED, 1, 0); QVERIFY(playlist);
    }
    void cleanupTestCase() { if (playlist) vlc_playlist_Delete(playlist); if (instance) libvlc_release(instance); }
    void init() { vlc_playlist_Lock(playlist); vlc_playlist_Clear(playlist); vlc_playlist_Unlock(playlist); }
    void realQueueKeepsOnlyAdmittedStableReferences() {
        QTemporaryDir profile;
        auto admissions = std::make_shared<AddonSourceAdmissions>(profile.path(), [](const auto&) { return true; });
        AddonNativeQueueStore store(playlist, admissions);
        const auto before = store.snapshot(); QVERIFY(before.available);
        AddonQueueTransactions::Transaction transaction; transaction.view = "view"; transaction.target = "queue";
        transaction.targetIdentity = before.identity; transaction.expectedRevision = before.revision;
        transaction.references = {reference(), reference(QStringLiteral("station-2"))};
        QVERIFY(succeeds(store, transaction));
        quint64 firstId = 0; QString uri, title;
        { Locked lock(playlist); QCOMPARE(vlc_playlist_Count(playlist), size_t(2));
        auto* first = vlc_playlist_Get(playlist, 0); firstId = vlc_playlist_item_GetId(first);
        auto* item = vlc_playlist_item_GetMedia(first);
        char* rawUri = input_item_GetURI(item); uri = QString::fromUtf8(rawUri); free(rawUri);
        char* rawName = input_item_GetName(item); title = QString::fromUtf8(rawName); free(rawName);
        }
        QVERIFY(uri.startsWith(QStringLiteral("hakone-addon-source:v1:")));
        QVERIFY(!title.contains(QLatin1Char('\n'))); QVERIFY(!title.contains(QLatin1Char('\r')));
        QCOMPARE(store.queuedReference(firstId), reference());
        QVERIFY(store.snapshot().revision != before.revision);
        AddonSourceAdmissions restored(profile.path(), [](const auto&) { return true; }); QCOMPARE(restored.admittedReference(uri), reference());
        QTemporaryDir exportDirectory; const auto file = exportDirectory.filePath("queue.m3u8");
        vlc_playlist_Lock(playlist); const auto result = vlc_playlist_Export(playlist, file.toUtf8().constData(), "export-m3u8"); vlc_playlist_Unlock(playlist);
        QCOMPARE(result, VLC_SUCCESS);
        QFile output(file); QVERIFY(output.open(QIODevice::ReadOnly)); const auto text = output.readAll();
        QVERIFY(text.contains(uri.toUtf8()));
        QVERIFY(!text.contains("\nhttps://invalid.example/injected\n"));
    }
    void playKeepsTheStableQueueItemAndRejectsAStaleTarget() {
        QTemporaryDir profile; auto admissions=std::make_shared<AddonSourceAdmissions>(profile.path(),[](const auto&){return true;});
        AddonNativeQueueStore store(playlist,admissions); auto before=store.snapshot();
        AddonQueueTransactions::Transaction transaction; transaction.target="queue"; transaction.targetIdentity=before.identity;
        transaction.expectedRevision=before.revision; transaction.references={reference()};
        QVERIFY(succeeds(store,transaction));
        QString uri; quint64 itemId;
        { Locked lock(playlist); auto* entry=vlc_playlist_Get(playlist,0); itemId=vlc_playlist_item_GetId(entry);
            auto* item=vlc_playlist_item_GetMedia(entry); char* raw=input_item_GetURI(item); uri=QString::fromUtf8(raw); free(raw);
            auto* scope=vlc_input_policy_HoldItem(item); QVERIFY(scope); const bool exact=vlc_input_policy_IsSource(scope,uri.toUtf8().constData()); vlc_input_policy_Release(scope); QVERIFY(exact); }
        QVERIFY(!succeeds(store,transaction,true));
        before=store.snapshot(); transaction.expectedRevision=before.revision;
        QVERIFY(succeeds(store,transaction,true));
        { Locked lock(playlist); QCOMPARE(vlc_playlist_Count(playlist),size_t(1)); QCOMPARE(vlc_playlist_item_GetId(vlc_playlist_Get(playlist,0)),itemId); vlc_playlist_Stop(playlist); }
        QCOMPARE(store.queuedReference(itemId),reference());
    }
    void sourceRevocationDuringAdmissionCannotCommitAQueueAction_data() {
        QTest::addColumn<bool>("playing"); QTest::newRow("enqueue")<<false; QTest::newRow("play")<<true;
    }
    void sourceRevocationDuringAdmissionCannotCommitAQueueAction() {
        QFETCH(bool,playing); QTemporaryDir profile; bool authorized=true;
        auto admissions=std::make_shared<AddonSourceAdmissions>(profile.path(),[&](const auto&) { authorized=false; return true; });
        AddonNativeQueueStore store(playlist,admissions); const auto before=store.snapshot();
        AddonQueueTransactions::Transaction transaction; transaction.target="queue"; transaction.targetIdentity=before.identity;
        transaction.expectedRevision=before.revision; transaction.references={reference()}; transaction.authorityCurrent=[&] { return authorized; };
        const auto outcome=start(store,transaction,playing); QVERIFY(finished(outcome));
        QVERIFY(!outcome->success); QVERIFY(!authorized); QCOMPARE(store.snapshot().revision,before.revision);
        { Locked lock(playlist); QCOMPARE(vlc_playlist_Count(playlist),size_t(0)); QCOMPARE(vlc_playlist_GetCurrentIndex(playlist),ssize_t(-1)); }
    }
    void staleSnapshotAndMixedInvalidBatchDoNotChangeQueue() {
        QTemporaryDir profile;
        auto admissions = std::make_shared<AddonSourceAdmissions>(profile.path(), [](const auto&) { return true; });
        AddonNativeQueueStore store(playlist, admissions);
        const auto before = store.snapshot();
        auto* existing = input_item_New("file:///existing.mp3", "Existing"); QVERIFY(existing);
        { Locked lock(playlist); QCOMPARE(vlc_playlist_AppendOne(playlist, existing), VLC_SUCCESS); } input_item_Release(existing);
        AddonQueueTransactions::Transaction transaction; transaction.target = "queue"; transaction.targetIdentity = before.identity; transaction.expectedRevision = before.revision; transaction.references = {reference()};
        // Both fail before any admission is saved, so they complete at once.
        auto outcome = start(store, transaction); QCOMPARE(outcome->calls, 1); QVERIFY(!outcome->success);
        const auto current = store.snapshot(); transaction.targetIdentity = current.identity; transaction.expectedRevision = current.revision;
        auto bad = reference(); bad.insert("url", "https://example.org/stream"); transaction.references = {reference(), bad};
        outcome = start(store, transaction); QCOMPARE(outcome->calls, 1); QVERIFY(!outcome->success);
        QVERIFY(!QFile::exists(QDir(profile.path()).filePath("source-admissions.json")));
        { Locked lock(playlist); QCOMPARE(vlc_playlist_Count(playlist), size_t(1)); }
    }
    void revisionObserversRunAfterTheNativeLockIsReleased() {
        QTemporaryDir profile;
        auto admissions = std::make_shared<AddonSourceAdmissions>(profile.path(), [](const auto&) { return true; });
        AddonNativeQueueStore store(playlist, admissions);
        const auto before = store.snapshot();
        bool completed = false, synchronous = false; quint64 observed = 0;
        connect(&store, &AddonNativeQueueStore::revisionChanged, &store, [&] {
            if (!completed) { synchronous = true; return; }
            observed = store.snapshot().revision;
        }, Qt::DirectConnection);
        AddonQueueTransactions::Transaction transaction; transaction.target = "queue";
        transaction.targetIdentity = before.identity; transaction.expectedRevision = before.revision; transaction.references = {reference()};
        bool success = false; store.append(transaction, [&](bool result, const QString&) { completed = true; success = result; });
        QTRY_VERIFY(completed);
        QVERIFY(success); QVERIFY(!synchronous);
        QTRY_VERIFY(observed > before.revision);
    }
    void displayRefreshKeepsTheLocatorAndManualTitle() {
        QTemporaryDir profile;
        auto admissions = std::make_shared<AddonSourceAdmissions>(profile.path(), [](const auto&) { return true; });
        AddonNativeQueueStore store(playlist, admissions);
        const auto before = store.snapshot();
        AddonQueueTransactions::Transaction transaction; transaction.target = "queue"; transaction.targetIdentity = before.identity;
        transaction.expectedRevision = before.revision; transaction.references = {reference()};
        QVERIFY(succeeds(store, transaction));
        QString uri; quint64 itemId = 0;
        { Locked lock(playlist); auto* entry = vlc_playlist_Get(playlist, 0); itemId = vlc_playlist_item_GetId(entry);
            char* value = input_item_GetURI(vlc_playlist_item_GetMedia(entry)); uri = QString::fromUtf8(value); free(value); }
        const auto stable = store.snapshot();
        auto updated = reference(); updated.insert("snapshot", QVariantMap{{"title", "Updated title"}, {"mediaKind", "audio"}});
        QSignalSpy metadata(AddonNativeQueueStore::notifications(), &AddonNativeQueueNotifications::metadataChanged);
        QVERIFY(refreshed(store, updated)); QCOMPARE(store.snapshot().revision, stable.revision); QCOMPARE(metadata.size(), 1);
        { Locked lock(playlist); auto* entry = vlc_playlist_Get(playlist, 0); QCOMPARE(vlc_playlist_item_GetId(entry), itemId);
            char* value = input_item_GetURI(vlc_playlist_item_GetMedia(entry)); QCOMPARE(QString::fromUtf8(value), uri); free(value);
            value = input_item_GetName(vlc_playlist_item_GetMedia(entry)); QCOMPARE(QString::fromUtf8(value), QStringLiteral("Updated title")); free(value); }
        transaction.expectedRevision = stable.revision; transaction.references = {updated};
        QVERIFY(succeeds(store, transaction));
        { Locked lock(playlist); QCOMPARE(vlc_playlist_Count(playlist), size_t(2));
            char* value = input_item_GetURI(vlc_playlist_item_GetMedia(vlc_playlist_Get(playlist, 1))); QCOMPARE(QString::fromUtf8(value), uri); free(value);
            input_item_SetName(vlc_playlist_item_GetMedia(vlc_playlist_Get(playlist, 0)), "My title");
            input_item_SetTitle(vlc_playlist_item_GetMedia(vlc_playlist_Get(playlist, 0)), "My title"); }
        updated.insert("snapshot", QVariantMap{{"title", "Later source title"}, {"mediaKind", "audio"}}); QVERIFY(refreshed(store, updated));
        { Locked lock(playlist); char* value = input_item_GetName(vlc_playlist_item_GetMedia(vlc_playlist_Get(playlist, 0))); QCOMPARE(QString::fromUtf8(value), QStringLiteral("My title")); free(value); }
        QCOMPARE(store.queuedReference(itemId).value("snapshot").toMap().value("title").toString(), QStringLiteral("Later source title"));
    }
    void admissionIsSavedOffThisThreadBeforeTheCommit() {
        QTemporaryDir profile; QList<QThread*> threads;
        auto admissions = std::make_shared<AddonSourceAdmissions>(profile.path(), [&](const auto&) { threads.append(QThread::currentThread()); return true; });
        AddonNativeQueueStore store(playlist, admissions); const auto before = store.snapshot();
        AddonQueueTransactions::Transaction transaction; transaction.target = "queue"; transaction.targetIdentity = before.identity;
        transaction.expectedRevision = before.revision; transaction.references = {reference()};
        const auto outcome = start(store, transaction);
        // append() returns before the admission is saved and the queue changes.
        QCOMPARE(outcome->calls, 0); QCOMPARE(count(), size_t(0)); QCOMPARE(store.snapshot().revision, before.revision);
        QVERIFY(finished(outcome)); QVERIFY(outcome->success); QCOMPARE(count(), size_t(1));
        QVERIFY(QFile::exists(QDir(profile.path()).filePath("source-admissions.json")));
        // The validator runs before the save and again before the commit, both on this thread.
        QCOMPARE(threads.size(), 2); for (auto* thread : threads) QCOMPARE(thread, QThread::currentThread());
    }
    void actionsCommitInTheOrderTheyArrive() {
        QTemporaryDir profile;
        auto admissions = std::make_shared<AddonSourceAdmissions>(profile.path(), [](const auto&) { return true; });
        AddonNativeQueueStore store(playlist, admissions); const auto before = store.snapshot();
        AddonQueueTransactions::Transaction first; first.target = "queue"; first.targetIdentity = before.identity;
        first.expectedRevision = before.revision; first.references = {reference(QStringLiteral("first"))};
        auto second = first; second.references = {reference(QStringLiteral("second"))};
        auto third = first; third.references = {reference(QStringLiteral("third"))};
        QStringList order;
        const auto watch = [&](const QString& name) { return [&order, name](bool success, const QString&) { order.append(name + (success ? "+" : "-")); }; };
        store.append(first, watch("first")); store.append(second, watch("second")); store.playReference(third, watch("third"));
        QTRY_COMPARE(order.size(), 3);
        // The first commit changes the revision, so the later actions see a changed queue.
        QCOMPARE(order, (QStringList{"first+", "second-", "third-"}));
        QCOMPARE(count(), size_t(1));
        Locked lock(playlist); auto* item = vlc_playlist_item_GetMedia(vlc_playlist_Get(playlist, 0));
        char* uri = input_item_GetURI(item); const auto admitted = AddonSourceAdmissions::decodeUntrusted(QString::fromUtf8(uri)); free(uri);
        QCOMPARE(admitted.value("itemId").toString(), QStringLiteral("first"));
    }
    void cancelOrCloseBeforeTheCommitChangesNothing_data() {
        QTest::addColumn<bool>("closing"); QTest::addColumn<bool>("playing");
        QTest::newRow("cancel enqueue") << false << false; QTest::newRow("cancel play") << false << true;
        QTest::newRow("close enqueue") << true << false; QTest::newRow("close play") << true << true;
    }
    void cancelOrCloseBeforeTheCommitChangesNothing() {
        QFETCH(bool, closing); QFETCH(bool, playing); QTemporaryDir profile;
        auto admissions = std::make_shared<AddonSourceAdmissions>(profile.path(), [](const auto&) { return true; });
        auto store = std::make_unique<AddonNativeQueueStore>(playlist, admissions); const auto before = store->snapshot();
        AddonQueueTransactions::Transaction transaction; transaction.target = "queue"; transaction.targetIdentity = before.identity;
        transaction.expectedRevision = before.revision; transaction.references = {reference()};
        const auto outcome = start(*store, transaction, playing); QCOMPARE(outcome->calls, 0);
        auto next = start(*store, transaction); QCOMPARE(next->calls, 0);
        if (closing) { store->close(); QCOMPARE(outcome->calls, 1); QCOMPARE(next->calls, 1); }
        else { QVERIFY(outcome->cancel); outcome->cancel(); }
        QVERIFY(finished(outcome)); QVERIFY(!outcome->success); QVERIFY(finished(next));
        if (closing) QVERIFY(!next->success);
        else QVERIFY(next->success); // A canceled action does not block the one after it.
        // A late worker result does not complete an action again.
        QTest::qWait(100); QCOMPARE(outcome->calls, 1); QCOMPARE(next->calls, 1);
        QCOMPARE(count(), size_t(closing ? 0 : 1));
        { Locked lock(playlist); QCOMPARE(vlc_playlist_GetCurrentIndex(playlist), ssize_t(-1)); }
    }
    void destroyingTheStoreFailsItsPendingActions() {
        QTemporaryDir profile;
        auto admissions = std::make_shared<AddonSourceAdmissions>(profile.path(), [](const auto&) { return true; });
        auto store = std::make_unique<AddonNativeQueueStore>(playlist, admissions); const auto before = store->snapshot();
        AddonQueueTransactions::Transaction transaction; transaction.target = "queue"; transaction.targetIdentity = before.identity;
        transaction.expectedRevision = before.revision; transaction.references = {reference()};
        const auto outcome = start(*store, transaction); store.reset();
        QCOMPARE(outcome->calls, 1); QVERIFY(!outcome->success);
        QTest::qWait(100); QCOMPARE(outcome->calls, 1); QCOMPARE(count(), size_t(0));
    }
    void revocationWhileTheAdmissionIsSavedCannotCommit_data() {
        QTest::addColumn<bool>("playing"); QTest::newRow("enqueue") << false; QTest::newRow("play") << true;
    }
    void revocationWhileTheAdmissionIsSavedCannotCommit() {
        // No authorityCurrent: the store's own check after the save must stop the commit.
        QFETCH(bool, playing); QTemporaryDir profile; int checks = 0;
        auto admissions = std::make_shared<AddonSourceAdmissions>(profile.path(), [&](const auto&) { return ++checks == 1; });
        AddonNativeQueueStore store(playlist, admissions); const auto before = store.snapshot();
        AddonQueueTransactions::Transaction transaction; transaction.target = "queue"; transaction.targetIdentity = before.identity;
        transaction.expectedRevision = before.revision; transaction.references = {reference()};
        const auto outcome = start(store, transaction, playing); QVERIFY(finished(outcome));
        QVERIFY(!outcome->success); QCOMPARE(checks, 2); QCOMPARE(count(), size_t(0)); QCOMPARE(store.snapshot().revision, before.revision);
    }
    void realManagerAccountChangesRevokeQueuedReferences() {
        QTemporaryDir profile, package;
        auto manifest = QJsonDocument::fromJson("{\"formatVersion\":1,\"id\":\"org.example.radio\",\"name\":\"Radio\",\"version\":\"1.0.0\","
            "\"publisher\":\"Tests\",\"license\":\"GPL-2.0-or-later\",\"source\":\"https://git.example.org/radio.git\","
            "\"api\":{\"addon\":\">=1.0.0 <2.0.0\",\"contracts\":{\"discovery\":\"^1.0.0\",\"settings\":\"^1.0.0\"}},"
            "\"runtime\":{\"kind\":\"wasm-message-v1\",\"entry\":\"worker.wasm\"},\"files\":[\"addon.json\",\"LICENSE\",\"worker.wasm\",\"schema.json\"],"
            "\"contributions\":[{\"id\":\"source\",\"type\":\"discovery.source\",\"contract\":\"discovery@1\",\"operations\":[\"roots\",\"resolve\"],\"requires\":[\"network.http\",\"credentials.use\"]}],"
            "\"permissions\":{\"required\":{\"network.http\":{\"origins\":[\"https://radio.example.org\"],\"methods\":[\"GET\"]},"
            "\"credentials.use\":{\"bindings\":[{\"field\":\"apiKey\",\"origin\":\"https://radio.example.org\",\"header\":\"Authorization\",\"prefix\":\"Bearer \"}]}},\"optional\":{}},"
            "\"settings\":{\"version\":1,\"schema\":\"schema.json\",\"accountFields\":[\"username\"]}}").object();
        const auto write = [&](const QString& path, const QByteArray& data) {
            QFile file(package.filePath(path)); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        QVERIFY(write("addon.json", QJsonDocument(manifest).toJson())); QVERIFY(write("LICENSE", "test license\n"));
        QVERIFY(write("worker.wasm", QByteArray::fromHex("0061736d01000000")));
        QVERIFY(write("schema.json", "{\"type\":\"object\",\"properties\":{\"apiKey\":{\"type\":\"string\",\"x-hakone-secret\":true,\"minLength\":1},"
            "\"language\":{\"type\":\"string\",\"default\":\"en\"},\"username\":{\"type\":\"string\",\"default\":\"guest\"}},\"required\":[\"apiKey\"],\"additionalProperties\":false}"));
        AddonManager manager(profile.filePath("addons")); const QString packageId = QStringLiteral("org.example.radio");
        QVERIFY2(manager.installLocalPackage(package.path(), true), qPrintable(manager.errorMessage()));
        const auto account = [&] { return manager.contributionsOfType(QStringLiteral("discovery.source"), true).first().toMap().value("accountReference").toString(); };
        const auto replace = [](const QString& value) { return QVariantMap{{"apiKey", QVariantMap{{"operation", "replace"}, {"value", value}, {"sessionOnly", true}}}}; };
        QVERIFY(manager.saveAddonSettings(packageId, {{"language", "en"}, {"username", "alice"}}, replace(QStringLiteral("first-unit-key"))));
        QVERIFY(manager.setEnabled(packageId, true));
        const auto alice = account(); QVERIFY(!alice.isEmpty());
        auto admissions = std::make_shared<AddonSourceAdmissions>(profile.filePath("references"), [&](const auto& value) { return discoveryReferenceAvailable(&manager, value); });
        AddonNativeQueueStore store(playlist, admissions);
        auto selected = reference(); selected.insert("accountReference", alice);
        const auto before = store.snapshot(); AddonQueueTransactions::Transaction transaction;
        transaction.target = "queue"; transaction.targetIdentity = before.identity; transaction.expectedRevision = before.revision; transaction.references = {selected};
        QVERIFY(succeeds(store, transaction));
        quint64 itemId; QString uri;
        { Locked lock(playlist); auto* item = vlc_playlist_Get(playlist, 0); itemId = vlc_playlist_item_GetId(item);
            char* value = input_item_GetURI(vlc_playlist_item_GetMedia(item)); uri = QString::fromUtf8(value); free(value); }
        QCOMPARE(store.queuedReference(itemId), selected);
        QVERIFY(manager.saveAddonSettings(packageId, {{"language", "fr"}, {"username", "alice"}}, {})); QCOMPARE(account(), alice);
        QCOMPARE(store.queuedReference(itemId), selected);
        manifest.insert("version", "1.0.1"); QVERIFY(write("addon.json", QJsonDocument(manifest).toJson()));
        QVERIFY2(manager.installLocalPackage(package.path(), true), qPrintable(manager.errorMessage())); QCOMPARE(account(), alice);
        QCOMPARE(store.queuedReference(itemId), selected); QCOMPARE(admissions->uriForReference(selected), uri);
        QVERIFY(manager.saveAddonSettings(packageId, {{"language", "fr"}, {"username", "alice"}}, replace(QStringLiteral("second-unit-key"))));
        const auto rotated = account(); QVERIFY(rotated != alice); QVERIFY(store.queuedReference(itemId).isEmpty());
        QVERIFY(!admissions->admittedReference(uri).isEmpty());
        QStringList newUris; QVERIFY(!admissions->admit({selected}, &newUris));
        selected.insert("accountReference", rotated); QVERIFY(admissions->admit({selected}, &newUris)); QVERIFY(newUris.first() != uri);
        QVERIFY(manager.saveAddonSettings(packageId, {{"language", "fr"}, {"username", "bob"}}, {})); QVERIFY(account() != rotated);
        QVERIFY(admissions->currentReference(newUris.first()).isEmpty());
        QVERIFY(manager.saveAddonSettings(packageId, {{"language", "fr"}, {"username", "bob"}}, {{"apiKey", QVariantMap{{"operation", "delete"}}}}));
        QVERIFY(!manager.packages().first().toMap().value("configured").toBool()); QVERIFY(store.queuedReference(itemId).isEmpty());
        auto emptyAccount = reference(); emptyAccount.insert("accountReference", QString()); QVERIFY(!discoveryReferenceAvailable(&manager, emptyAccount));
    }
};
QTEST_GUILESS_MAIN(TestAddonNativeQueueStore)
#include "test_addon_native_queue_store.moc"
