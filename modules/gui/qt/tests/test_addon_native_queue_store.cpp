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
#include <vlc/vlc.h>
#include "lib/libvlc_internal.h"
#include <vlc_playlist.h>
#include <vlc_input_item.h>
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
private slots:
    void initTestCase() {
        qputenv("VLC_PLUGIN_PATH", VLC_ADDON_NATIVE_BUILD_ROOT "/modules");
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
        bool success = false; store.append(transaction, [&](bool result, const QString&) { success = result; }); QVERIFY(success);
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
    void staleSnapshotAndMixedInvalidBatchDoNotChangeQueue() {
        QTemporaryDir profile;
        auto admissions = std::make_shared<AddonSourceAdmissions>(profile.path(), [](const auto&) { return true; });
        AddonNativeQueueStore store(playlist, admissions);
        const auto before = store.snapshot();
        auto* existing = input_item_New("file:///existing.mp3", "Existing"); QVERIFY(existing);
        { Locked lock(playlist); QCOMPARE(vlc_playlist_AppendOne(playlist, existing), VLC_SUCCESS); } input_item_Release(existing);
        AddonQueueTransactions::Transaction transaction; transaction.target = "queue"; transaction.targetIdentity = before.identity; transaction.expectedRevision = before.revision; transaction.references = {reference()};
        bool success = true; store.append(transaction, [&](bool result, const QString&) { success = result; }); QVERIFY(!success);
        const auto current = store.snapshot(); transaction.targetIdentity = current.identity; transaction.expectedRevision = current.revision;
        auto bad = reference(); bad.insert("url", "https://example.org/stream"); transaction.references = {reference(), bad};
        store.append(transaction, [&](bool result, const QString&) { success = result; }); QVERIFY(!success);
        { Locked lock(playlist); QCOMPARE(vlc_playlist_Count(playlist), size_t(1)); }
    }
    void revisionObserversRunAfterTheNativeLockIsReleased() {
        QTemporaryDir profile;
        auto admissions = std::make_shared<AddonSourceAdmissions>(profile.path(), [](const auto&) { return true; });
        AddonNativeQueueStore store(playlist, admissions);
        const auto before = store.snapshot();
        bool returned = false, synchronous = false; quint64 observed = 0;
        connect(&store, &AddonNativeQueueStore::revisionChanged, &store, [&] {
            if (!returned) { synchronous = true; return; }
            observed = store.snapshot().revision;
        }, Qt::DirectConnection);
        AddonQueueTransactions::Transaction transaction; transaction.target = "queue";
        transaction.targetIdentity = before.identity; transaction.expectedRevision = before.revision; transaction.references = {reference()};
        bool success = false; store.append(transaction, [&](bool result, const QString&) { success = result; });
        returned = true;
        QVERIFY(success); QVERIFY(!synchronous); QCOMPARE(observed, quint64(0));
        QTRY_VERIFY(observed > before.revision);
    }
    void displayRefreshKeepsTheLocatorAndManualTitle() {
        QTemporaryDir profile;
        auto admissions = std::make_shared<AddonSourceAdmissions>(profile.path(), [](const auto&) { return true; });
        AddonNativeQueueStore store(playlist, admissions);
        const auto before = store.snapshot();
        AddonQueueTransactions::Transaction transaction; transaction.target = "queue"; transaction.targetIdentity = before.identity;
        transaction.expectedRevision = before.revision; transaction.references = {reference()};
        bool success = false; store.append(transaction, [&](bool result, const QString&) { success = result; }); QVERIFY(success);
        QString uri; quint64 itemId = 0;
        { Locked lock(playlist); auto* entry = vlc_playlist_Get(playlist, 0); itemId = vlc_playlist_item_GetId(entry);
            char* value = input_item_GetURI(vlc_playlist_item_GetMedia(entry)); uri = QString::fromUtf8(value); free(value); }
        const auto stable = store.snapshot();
        auto updated = reference(); updated.insert("snapshot", QVariantMap{{"title", "Updated title"}, {"mediaKind", "audio"}});
        QSignalSpy metadata(AddonNativeQueueStore::notifications(), &AddonNativeQueueNotifications::metadataChanged);
        QVERIFY(store.refreshDisplay(updated)); QCOMPARE(store.snapshot().revision, stable.revision); QCOMPARE(metadata.size(), 1);
        { Locked lock(playlist); auto* entry = vlc_playlist_Get(playlist, 0); QCOMPARE(vlc_playlist_item_GetId(entry), itemId);
            char* value = input_item_GetURI(vlc_playlist_item_GetMedia(entry)); QCOMPARE(QString::fromUtf8(value), uri); free(value);
            value = input_item_GetName(vlc_playlist_item_GetMedia(entry)); QCOMPARE(QString::fromUtf8(value), QStringLiteral("Updated title")); free(value); }
        transaction.expectedRevision = stable.revision; transaction.references = {updated};
        store.append(transaction, [&](bool result, const QString&) { success = result; }); QVERIFY(success);
        { Locked lock(playlist); QCOMPARE(vlc_playlist_Count(playlist), size_t(2));
            char* value = input_item_GetURI(vlc_playlist_item_GetMedia(vlc_playlist_Get(playlist, 1))); QCOMPARE(QString::fromUtf8(value), uri); free(value);
            input_item_SetName(vlc_playlist_item_GetMedia(vlc_playlist_Get(playlist, 0)), "My title");
            input_item_SetTitle(vlc_playlist_item_GetMedia(vlc_playlist_Get(playlist, 0)), "My title"); }
        updated.insert("snapshot", QVariantMap{{"title", "Later source title"}, {"mediaKind", "audio"}}); QVERIFY(store.refreshDisplay(updated));
        { Locked lock(playlist); char* value = input_item_GetName(vlc_playlist_item_GetMedia(vlc_playlist_Get(playlist, 0))); QCOMPARE(QString::fromUtf8(value), QStringLiteral("My title")); free(value); }
        QCOMPARE(store.queuedReference(itemId).value("snapshot").toMap().value("title").toString(), QStringLiteral("Later source title"));
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
        bool success = false; store.append(transaction, [&](bool result, const QString&) { success = result; }); QVERIFY(success);
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
