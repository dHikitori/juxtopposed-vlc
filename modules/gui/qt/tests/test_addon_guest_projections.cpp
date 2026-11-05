// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_guest_projections.hpp"
#include "addons/addon_manifest.hpp"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
#ifndef Q_OS_WIN
#include <unistd.h>
#endif
using namespace vlc::addons;

namespace {
using Projections = AddonGuestProjections;
AddonHostServices::Context context(const QString& package, const QJsonObject& grants)
{ return {package, package + QStringLiteral("/main"), QStringLiteral("invocation-1"), grants, {}}; }
const QJsonObject AllGrants{
    {"queue.read", QJsonObject{}},
    {"queue.modify", QJsonObject{{"operations", QJsonArray{"remove"}}}},
    {"library.read", QJsonObject{{"collections", QJsonArray{"albums", "tracks"}}, {"fields", QJsonArray{"title", "year"}}}},
    {"media.fingerprint", QJsonObject{{"algorithms", QJsonArray{"opensubtitles-hash"}}}}};
Projections::Queue queue()
{
    Projections::Queue result;
    result.total = 3; result.current = 1;
    for (int i = 0; i < 3; ++i) {
        Projections::QueueItem item;
        item.key = QByteArray::number(i + 10); item.title = QStringLiteral("Song %1").arg(i);
        item.artist = QStringLiteral("Artist"); item.album = QStringLiteral("Album"); item.kind = QStringLiteral("audio");
        item.durationMs = 1000 * (i + 1);
        result.items.append(item);
    }
    return result;
}
struct Call { bool done = false; AddonHostServices::Result result; };
}

class TestAddonGuestProjections : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;
    QUrl m_media;
    QString m_libraryCollection;
    int m_libraryLimit = 0;
    Call run(AddonHostServices& services, const QString& operation, const QJsonObject& request,
             const QString& package = QStringLiteral("org.example.one"), const QJsonObject& grants = AllGrants)
    {
        auto call = std::make_shared<Call>();
        services.dispatch(context(package, grants), operation, request, [call](AddonHostServices::Result result) {
            call->done = true; call->result = std::move(result);
        });
        if (!QTest::qWaitFor([call] { return call->done; }, 5000)) return {};
        return *call;
    }
    // The queue for the location, check and removal tests: when set, the
    // projection reads and changes this list instead of queue().
    std::optional<QList<Projections::QueueItem>> m_queueItems;
    std::unique_ptr<Projections> mount(AddonHostServices& services, bool library = true)
    {
        auto projections = std::make_unique<Projections>();
        Projections::LibraryReader reader;
        if (library) reader = [this](const QString& collection, const QString&, int offset, int limit, Projections::LibraryDone done) {
            m_libraryCollection = collection; m_libraryLimit = limit;
            Projections::LibraryPage page;
            for (int i = 0; i < limit + 1; ++i)
                page.items.append({QByteArray::number(offset + i), {{"title", QStringLiteral("Album %1").arg(offset + i)},
                    {"artist", "Hidden"}, {"year", 2000 + i}, {"mrl", "file:///private/album"}}});
            page.more = true; page.items.resize(limit);
            QTimer::singleShot(0, [done, page] { done(page); });
            return std::function<void()>{};
        };
        projections->mount(&services, [this](int offset, int limit) -> std::optional<Projections::Queue> {
            auto value = queue();
            if (m_queueItems) { value.items = *m_queueItems; value.total = value.items.size(); value.current = 0; }
            value.items = value.items.mid(offset, limit); return value;
        }, std::move(reader), [this] { return m_media; },
        [this](const std::function<bool(const QByteArray&)>& selected) -> std::optional<Projections::QueueRemoval> {
            if (!m_queueItems) return std::nullopt;
            Projections::QueueRemoval removal;
            for (qsizetype i = m_queueItems->size(); i-- > 0;)
                if (selected(m_queueItems->at(i).key)) { m_queueItems->removeAt(i); ++removal.removed; }
            removal.total = m_queueItems->size();
            return removal;
        });
        return projections;
    }

private slots:
    void anOperationIsOfferedOnlyWithItsSource()
    {
        AddonHostServices services;
        auto projections = mount(services, false);
        const auto available = services.availableOperations();
        QVERIFY(available.contains("queue.snapshot"));
        QVERIFY(available.contains("queue.apply"));
        QVERIFY(available.contains("media.fingerprint"));
        QVERIFY(!available.contains("library.query"));
        projections.reset();
        QVERIFY(services.availableOperations().isEmpty());
    }
    void theQueueHasOpaqueIdsAndNoAddresses()
    {
        AddonHostServices services; auto projections = mount(services);
        const auto first = run(services, "queue.snapshot", {{"limit", 2}});
        QVERIFY2(first.result.error.isEmpty(), qPrintable(first.result.error));
        const auto response = first.result.response;
        QCOMPARE(response.value("total").toInt(), 3); QCOMPARE(response.value("current").toInt(), 1);
        const auto items = response.value("items").toArray();
        QCOMPARE(items.size(), 2);
        const auto row = items.first().toObject();
        QCOMPARE(row.value("title").toString(), QStringLiteral("Song 0"));
        QCOMPARE(row.value("durationMs").toInt(), 1000);
        const auto id = row.value("id").toString();
        QVERIFY(id.startsWith("queue:"));
        // Neither the playlist item's own ID (10) nor its index (0), nor any raw number.
        bool numeric = false; id.mid(6).toLongLong(&numeric);
        QVERIFY(id != "queue:10"); QVERIFY(id != "queue:0"); QVERIFY(!numeric);
        QVERIFY(!QJsonDocument(response).toJson().contains("file:"));
        // The same item has another ID for another add-on, and the same ID again for this one.
        const auto other = run(services, "queue.snapshot", {{"limit", 1}}, "org.example.two");
        QVERIFY(other.result.response.value("items").toArray().first().toObject().value("id") != row.value("id"));
        const auto again = run(services, "queue.snapshot", {{"limit", 1}});
        QCOMPARE(again.result.response.value("items").toArray().first().toObject().value("id"), row.value("id"));
    }
    void sameAddressesHaveTheSameLocationKey()
    {
        m_queueItems = QList<Projections::QueueItem>{};
        const auto add = [this](int key, const QByteArray& location, const QString& file = {}) {
            Projections::QueueItem item; item.key = QByteArray::number(key); item.title = QStringLiteral("Item %1").arg(key);
            item.location = location; item.file = file; m_queueItems->append(item);
        };
        add(1, "file:///music/a.mp3", "/music/a.mp3");
        add(2, "https://example.org/live");
        add(3, "file:///music/a.mp3", "/music/a.mp3");
        add(4, {});
        AddonHostServices services; auto projections = mount(services);
        const auto call = run(services, "queue.snapshot", {});
        QVERIFY2(call.result.error.isEmpty(), qPrintable(call.result.error));
        const auto items = call.result.response.value("items").toArray();
        QCOMPARE(items.size(), 4);
        const auto location = [&items](int i) { return items[i].toObject().value("location").toString(); };
        QVERIFY(location(0).startsWith("location:"));
        QCOMPARE(location(0), location(2)); QVERIFY(location(0) != location(1));
        QVERIFY(!items[3].toObject().contains("location"));
        QVERIFY(items[0].toObject().value("file").toBool()); QVERIFY(!items[1].toObject().contains("file"));
        // Without "check", the host does not look for the files.
        QVERIFY(!items[0].toObject().contains("availability"));
        const auto json = QJsonDocument(call.result.response).toJson();
        QVERIFY(!json.contains("music")); QVERIFY(!json.contains("example.org"));
        // Another add-on cannot compare its keys with these.
        const auto other = run(services, "queue.snapshot", {}, "org.example.two");
        QVERIFY(other.result.response.value("items").toArray()[0].toObject().value("location").toString() != location(0));
        m_queueItems.reset();
    }
    void aCheckFindsMissingFilesAndFolders()
    {
        QFile present(m_dir.filePath("present.mp3"));
        QVERIFY(present.open(QIODevice::WriteOnly)); present.close();
        QVERIFY(QDir(m_dir.path()).mkpath("locked"));
        QFile locked(m_dir.filePath("locked/inside.mp3"));
        QVERIFY(locked.open(QIODevice::WriteOnly)); locked.close();
        m_queueItems = QList<Projections::QueueItem>{};
        const auto add = [this](int key, const QString& file) {
            Projections::QueueItem item; item.key = QByteArray::number(key); item.title = QStringLiteral("Item %1").arg(key);
            item.file = file; item.location = file.isEmpty() ? QByteArray("https://example.org/a") : QUrl::fromLocalFile(file).toEncoded();
            m_queueItems->append(item);
        };
        add(1, present.fileName());
        add(2, m_dir.filePath("gone.mp3"));
        add(3, m_dir.filePath("unplugged/gone.mp3"));
        add(4, {});
        add(5, m_dir.filePath("unplugged/also-gone.mp3"));
        add(6, present.fileName() + "/inside.mp3"); // a path part is a file
#ifndef Q_OS_WIN
        // A folder that the app cannot search: the file is not proven gone.
        add(7, m_dir.filePath("locked/inside.mp3"));
        add(8, m_dir.filePath("locked/gone.mp3"));
        QVERIFY(QFile::setPermissions(m_dir.filePath("locked"), QFileDevice::Permissions{}));
#endif
        AddonHostServices services; auto projections = mount(services);
        const auto call = run(services, "queue.snapshot", {{"check", true}});
#ifndef Q_OS_WIN
        QFile::setPermissions(m_dir.filePath("locked"), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
#endif
        QVERIFY2(call.result.error.isEmpty(), qPrintable(call.result.error));
        QVERIFY(!call.result.response.contains("checkIncomplete"));
        const auto items = call.result.response.value("items").toArray();
        const auto availability = [&items](int i) { return items[i].toObject().value("availability").toString(); };
        QCOMPARE(availability(0), QStringLiteral("present"));
        QCOMPARE(availability(1), QStringLiteral("missing"));
        QCOMPARE(availability(2), QStringLiteral("folderMissing"));
        // A stream is not a file: the host does not check it.
        QVERIFY(!items[3].toObject().contains("availability"));
        QCOMPARE(availability(4), QStringLiteral("folderMissing"));
        // The "folder" is a file that is there: only the file is missing.
        QCOMPARE(availability(5), QStringLiteral("missing"));
#ifndef Q_OS_WIN
        // Running as root ignores permissions; then the files are found.
        if (::geteuid() != 0) {
            QCOMPARE(availability(6), QStringLiteral("unknown"));
            QCOMPARE(availability(7), QStringLiteral("unknown"));
        }
#endif
        QVERIFY(!QJsonDocument(call.result.response).toJson().contains("gone"));
        m_queueItems.reset();
    }
    void aRemovalChangesOnlyTheNamedItemsOfThisAddon()
    {
        m_queueItems = QList<Projections::QueueItem>{};
        for (int key = 1; key <= 5; ++key) {
            Projections::QueueItem item; item.key = QByteArray::number(key); item.title = QStringLiteral("Item %1").arg(key);
            m_queueItems->append(item);
        }
        AddonHostServices services; auto projections = mount(services);
        const auto read = run(services, "queue.snapshot", {});
        const auto items = read.result.response.value("items").toArray();
        QCOMPARE(items.size(), 5);
        const auto id = [&items](int i) { return items[i].toObject().value("id").toString(); };
        // Another add-on's IDs for the same items do not select them.
        const auto foreign = run(services, "queue.snapshot", {}, "org.example.two").result.response.value("items").toArray();
        const auto ignored = run(services, "queue.apply", {{"operation", "remove"}, {"items", QJsonArray{foreign[0].toObject().value("id")}}});
        QVERIFY2(ignored.result.error.isEmpty(), qPrintable(ignored.result.error));
        QCOMPARE(ignored.result.response.value("removed").toInt(), 0);
        QCOMPARE(m_queueItems->size(), 5);
        const auto removed = run(services, "queue.apply", {{"operation", "remove"}, {"items", QJsonArray{id(1), id(3), id(3)}}});
        QVERIFY2(removed.result.error.isEmpty(), qPrintable(removed.result.error));
        QCOMPARE(removed.result.response.value("removed").toInt(), 2);
        QCOMPARE(removed.result.response.value("total").toInt(), 3);
        QCOMPARE(m_queueItems->at(0).key, QByteArray("1")); QCOMPARE(m_queueItems->at(1).key, QByteArray("3"));
        QCOMPARE(m_queueItems->at(2).key, QByteArray("5"));
        // The same IDs again: the items are gone, so nothing changes.
        QCOMPARE(run(services, "queue.apply", {{"operation", "remove"}, {"items", QJsonArray{id(1)}}}).result.response.value("removed").toInt(), 0);
        // Without the granted operation, the host refuses before the queue changes.
        const auto ungranted = run(services, "queue.apply", {{"operation", "remove"}, {"items", QJsonArray{id(0)}}}, "org.example.one",
            {{"queue.read", QJsonObject{}}, {"queue.modify", QJsonObject{{"operations", QJsonArray{"move"}}}}});
        QVERIFY(!ungranted.result.error.isEmpty());
        const auto unread = run(services, "queue.apply", {{"operation", "remove"}, {"items", QJsonArray{id(0)}}}, "org.example.one",
            {{"queue.read", QJsonObject{}}});
        QVERIFY(unread.result.error.contains("permission"));
        QCOMPARE(m_queueItems->size(), 3);
        m_queueItems.reset();
    }
    void requestsOutsideTheLimitsAreRefused_data()
    {
        QTest::addColumn<QString>("operation"); QTest::addColumn<QJsonObject>("request");
        QTest::newRow("queue limit") << "queue.snapshot" << QJsonObject{{"limit", 101}};
        QTest::newRow("queue fraction") << "queue.snapshot" << QJsonObject{{"offset", 1.5}};
        QTest::newRow("queue key") << "queue.snapshot" << QJsonObject{{"uri", true}};
        QTest::newRow("queue check") << "queue.snapshot" << QJsonObject{{"check", 1}};
        QTest::newRow("apply operation") << "queue.apply" << QJsonObject{{"operation", "clear"}, {"items", QJsonArray{}}};
        QTest::newRow("apply empty") << "queue.apply" << QJsonObject{{"operation", "remove"}, {"items", QJsonArray{}}};
        QTest::newRow("apply id") << "queue.apply" << QJsonObject{{"operation", "remove"}, {"items", QJsonArray{"queue:10"}}};
        QTest::newRow("apply key") << "queue.apply" << QJsonObject{{"operation", "remove"}, {"items", QJsonArray{QString("queue:") + QString(32, 'a')}}, {"index", 1}};
        QTest::newRow("library collection") << "library.query" << QJsonObject{{"collection", "folders"}};
        QTest::newRow("library limit") << "library.query" << QJsonObject{{"collection", "albums"}, {"limit", 51}};
        QTest::newRow("library text") << "library.query" << QJsonObject{{"collection", "albums"}, {"query", QString(129, 'a')}};
        QTest::newRow("fingerprint algorithm") << "media.fingerprint" << QJsonObject{{"algorithm", "sha256"}};
    }
    void requestsOutsideTheLimitsAreRefused()
    {
        QFETCH(QString, operation); QFETCH(QJsonObject, request);
        AddonHostServices services; auto projections = mount(services);
        const auto call = run(services, operation, request);
        QVERIFY(call.done); QVERIFY(!call.result.error.isEmpty()); QVERIFY(call.result.response.isEmpty());
    }
    void theLibraryReturnsOnlyGrantedCollectionsAndFields()
    {
        AddonHostServices services; auto projections = mount(services);
        const auto call = run(services, "library.query", {{"collection", "albums"}, {"limit", 3}});
        QVERIFY2(call.result.error.isEmpty(), qPrintable(call.result.error));
        QCOMPARE(m_libraryCollection, QStringLiteral("albums")); QCOMPARE(m_libraryLimit, 3);
        const auto items = call.result.response.value("items").toArray();
        QCOMPARE(items.size(), 3); QVERIFY(call.result.response.value("more").toBool());
        const auto row = items.first().toObject();
        QCOMPARE(row.value("title").toString(), QStringLiteral("Album 0")); QCOMPARE(row.value("year").toInt(), 2000);
        QVERIFY(row.value("id").toString().startsWith("albums:"));
        // Not granted, or not a projected field: never returned.
        QVERIFY(!row.contains("artist")); QVERIFY(!row.contains("mrl"));
        const auto refused = run(services, "library.query", {{"collection", "artists"}});
        QVERIFY(refused.result.error.contains("permission"));
        const auto ungranted = run(services, "library.query", {{"collection", "albums"}}, "org.example.one", {{"queue.read", QJsonObject{}}});
        QVERIFY(!ungranted.result.error.isEmpty());
    }
    void theFingerprintIsComputedOffTheUiThreadForTheLocalFile()
    {
        QFile file(m_dir.filePath("movie.mkv"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QByteArray bytes(256 * 1024, '\0');
        for (int i = 0; i < bytes.size(); ++i) bytes[i] = char(i * 7);
        QVERIFY(file.write(bytes) == bytes.size()); file.close();
        AddonHostServices services; auto projections = mount(services);
        m_media = QUrl::fromLocalFile(file.fileName());
        const auto call = run(services, "media.fingerprint", {{"algorithm", "opensubtitles-hash"}});
        QVERIFY2(call.result.error.isEmpty(), qPrintable(call.result.error));
        QCOMPARE(call.result.response.value("value").toString().size(), 16);
        QCOMPARE(call.result.response.value("size").toInt(), bytes.size());
        QVERIFY(!QJsonDocument(call.result.response).toJson().contains("movie"));
        const auto ungranted = run(services, "media.fingerprint", {{"algorithm", "opensubtitles-hash"}}, "org.example.one",
            {{"media.fingerprint", QJsonObject{{"algorithms", QJsonArray{}}}}});
        QVERIFY(ungranted.result.error.contains("permission"));
        m_media = QUrl("https://example.org/stream.mkv");
        const auto remote = run(services, "media.fingerprint", {{"algorithm", "opensubtitles-hash"}});
        QVERIFY(!remote.result.error.isEmpty());
    }
    void aManifestNamesOnlyProjectedCollectionsFieldsAndHashes_data()
    {
        QTest::addColumn<QJsonObject>("permissions"); QTest::addColumn<bool>("valid");
        QTest::newRow("library") << QJsonObject{{"library.read", QJsonObject{{"collections", QJsonArray{"albums", "playlists"}}, {"fields", QJsonArray{"title"}}}}} << true;
        QTest::newRow("folders") << QJsonObject{{"library.read", QJsonObject{{"collections", QJsonArray{"folders"}}, {"fields", QJsonArray{"title"}}}}} << false;
        QTest::newRow("mrl") << QJsonObject{{"library.read", QJsonObject{{"collections", QJsonArray{"albums"}}, {"fields", QJsonArray{"mrl"}}}}} << false;
        QTest::newRow("hash") << QJsonObject{{"media.fingerprint", QJsonObject{{"algorithms", QJsonArray{"opensubtitles-hash"}}}}} << true;
        QTest::newRow("sha") << QJsonObject{{"media.fingerprint", QJsonObject{{"algorithms", QJsonArray{"sha256"}}}}} << false;
    }
    void aManifestNamesOnlyProjectedCollectionsFieldsAndHashes()
    {
        QFETCH(QJsonObject, permissions); QFETCH(bool, valid);
        const QJsonObject manifest{{"formatVersion", 1}, {"id", "org.example.read"}, {"name", "Read"}, {"version", "1.0.0"},
            {"publisher", "Tests"}, {"license", "GPL-2.0-or-later"}, {"source", "https://git.example.org/read.git"},
            {"api", QJsonObject{{"addon", ">=1.0.0 <2.0.0"}, {"contracts", QJsonObject{}}}},
            {"files", QJsonArray{"addon.json", "LICENSE"}}, {"contributions", QJsonArray{}},
            {"permissions", QJsonObject{{"required", permissions}, {"optional", QJsonObject{}}}}};
        Manifest parsed; QString error;
        const bool parsedOk = Manifest::parse(QJsonDocument(manifest).toJson(), &parsed, &error);
        // A valid scope passes the permission check; the bare manifest then
        // stops at its empty contribution list.
        if (valid) QVERIFY2(parsedOk || error.contains("contribution list"), qPrintable(error));
        else QVERIFY2(!parsedOk && error.contains("not supported"), qPrintable(error));
    }
};
QTEST_GUILESS_MAIN(TestAddonGuestProjections)
#include "test_addon_guest_projections.moc"
