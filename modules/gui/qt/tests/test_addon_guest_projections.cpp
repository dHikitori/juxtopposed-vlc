// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_guest_projections.hpp"
#include "addons/addon_manifest.hpp"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>
using namespace vlc::addons;

namespace {
using Projections = AddonGuestProjections;
AddonHostServices::Context context(const QString& package, const QJsonObject& grants)
{ return {package, package + QStringLiteral("/main"), QStringLiteral("invocation-1"), grants, {}}; }
const QJsonObject AllGrants{
    {"queue.read", QJsonObject{}},
    {"library.read", QJsonObject{{"collections", QJsonArray{"albums", "tracks"}}, {"fields", QJsonArray{"title", "year"}}}},
    {"media.fingerprint", QJsonObject{{"algorithms", QJsonArray{"opensubtitles-hash"}}}}};
Projections::Queue queue()
{
    Projections::Queue result;
    result.total = 3; result.current = 1;
    for (int i = 0; i < 3; ++i)
        result.items.append({QByteArray::number(i + 10), QStringLiteral("Song %1").arg(i), QStringLiteral("Artist"),
            QStringLiteral("Album"), QStringLiteral("audio"), 1000 * (i + 1)});
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
        projections->mount(&services, [](int offset, int limit) -> std::optional<Projections::Queue> {
            auto value = queue(); value.items = value.items.mid(offset, limit); return value;
        }, std::move(reader), [this] { return m_media; });
        return projections;
    }

private slots:
    void anOperationIsOfferedOnlyWithItsSource()
    {
        AddonHostServices services;
        auto projections = mount(services, false);
        const auto available = services.availableOperations();
        QVERIFY(available.contains("queue.snapshot"));
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
        QVERIFY(row.value("id").toString().startsWith("queue:"));
        QVERIFY(!row.value("id").toString().contains("10"));
        QVERIFY(!QJsonDocument(response).toJson().contains("file:"));
        // The same item has another ID for another add-on, and the same ID again for this one.
        const auto other = run(services, "queue.snapshot", {{"limit", 1}}, "org.example.two");
        QVERIFY(other.result.response.value("items").toArray().first().toObject().value("id") != row.value("id"));
        const auto again = run(services, "queue.snapshot", {{"limit", 1}});
        QCOMPARE(again.result.response.value("items").toArray().first().toObject().value("id"), row.value("id"));
    }
    void requestsOutsideTheLimitsAreRefused_data()
    {
        QTest::addColumn<QString>("operation"); QTest::addColumn<QJsonObject>("request");
        QTest::newRow("queue limit") << "queue.snapshot" << QJsonObject{{"limit", 101}};
        QTest::newRow("queue fraction") << "queue.snapshot" << QJsonObject{{"offset", 1.5}};
        QTest::newRow("queue key") << "queue.snapshot" << QJsonObject{{"uri", true}};
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
