// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrlQuery>
#include "addons/addon_package_store.hpp"
#include "addons/addon_registry_client.hpp"
#include "addons/addon_registry_install.hpp"
using namespace vlc::addons;

namespace {
const QString Source = QStringLiteral("https://github.com/example/addon-midnight.git");

QJsonObject indexRelease(const QString& version, char object = 'a', const QString& addonApi = QStringLiteral(">=1.0.0 <2.0.0"))
{
    return {{"version", version}, {"tag", "v" + version}, {"tagObjectId", QString(40, QLatin1Char(object))},
            {"commitId", QString(40, QLatin1Char(object == 'a' ? 'b' : 'c'))}, {"objectFormat", "sha1"},
            {"digest", QString(64, QLatin1Char('d'))}, {"prerelease", version.contains(QLatin1Char('-'))},
            {"addonApi", addonApi}, {"contracts", QJsonObject{{"theme", "^1.0.0"}}}};
}
QByteArray index(const QJsonArray& releases, const QString& source = Source, const QJsonArray& yanked = {})
{
    return QJsonDocument(QJsonObject{{"revision", 7}, {"generatedAt", "2026-11-03T04:12:00Z"},
        {"addons", QJsonArray{QJsonObject{{"id", "org.example.midnight"}, {"source", source},
                                          {"releases", releases}, {"yanked", yanked}}}}}).toJson();
}
QJsonObject fullRelease(const QString& version = QStringLiteral("1.2.0"))
{
    return {{"addonId", "org.example.midnight"}, {"version", version}, {"prerelease", false}, {"state", "published"},
            {"yankReason", QJsonValue::Null}, {"publishedAt", "2026-10-28T09:00:00Z"},
            {"install", QJsonObject{{"source", Source}, {"tag", "v" + version}, {"tagObjectId", QString(40, QLatin1Char('a'))},
                                    {"commitId", QString(40, QLatin1Char('b'))}, {"objectFormat", "sha1"}, {"digest", QString(64, QLatin1Char('d'))}}},
            {"addonApi", ">=1.0.0 <2.0.0"}, {"contracts", QJsonObject{{"theme", "^1.0.0"}}}, {"optionalContracts", QJsonObject{}},
            {"runtime", false}, {"permissions", QJsonObject{}},
            {"contributions", QJsonArray{QJsonObject{{"id", "midnight"}, {"type", "theme"}, {"name", "Midnight"}}}},
            {"locales", QJsonArray{"en"}}, {"packageBytes", 1234}, {"changelog", QJsonValue::Null},
            {"sourceUrl", "https://github.com/example/addon-midnight/tree/v" + version},
            {"webUrl", "https://addons.hakone.org/addons/org.example.midnight/releases#" + version}};
}
QJsonObject summary(const QString& id = QStringLiteral("org.example.midnight"))
{
    return {{"id", id}, {"name", "Midnight"}, {"summary", "A dark theme."},
            {"publisher", QJsonObject{{"name", "Example"}, {"slug", "example"}, {"verified", false}}},
            {"iconUrl", QJsonValue::Null}, {"iconUrl2x", QJsonValue::Null}, {"categories", QJsonArray{"themes"}},
            {"contributionTypes", QJsonArray{"theme"}}, {"license", "GPL-2.0-or-later"},
            {"latest", QJsonObject{{"version", "1.2.0"}, {"publishedAt", "2026-10-28T09:00:00Z"}, {"addonApi", ">=1.0.0 <2.0.0"}, {"prerelease", false}}},
            {"featured", false}, {"updatedAt", "2026-10-28T09:00:00Z"},
            {"url", "https://api.hakone.org/v1/addons/" + id}, {"webUrl", "https://addons.hakone.org/addons/" + id}};
}
QByteArray json(const QJsonObject& object) { return QJsonDocument(object).toJson(QJsonDocument::Compact); }

PackageCandidate candidateFor(const RegistryRelease& release)
{
    PackageCandidate candidate;
    candidate.manifest.id = release.addonId;
    candidate.manifest.version = release.version;
    candidate.identity.source = release.install.source;
    candidate.identity.tag = release.install.tag;
    candidate.identity.tagObjectId = release.install.tagObjectId;
    candidate.identity.commitId = release.install.commitId;
    candidate.identity.objectFormat = release.install.objectFormat;
    candidate.digest = release.install.digest;
    return candidate;
}

// A local registry: answers each request from `answer` and keeps what it was asked.
class RegistryFixture : public QTcpServer
{
public:
    struct Answer { int status = 200; QByteArray body; QByteArray headers; };
    std::function<Answer(const QUrl&, const QByteArray& request)> answer;
    QList<QUrl> urls;
    QList<QByteArray> requests;
    RegistryFixture()
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            auto* socket = nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer = QByteArray()]() mutable {
                buffer += socket->readAll();
                if (!buffer.contains("\r\n\r\n")) return;
                disconnect(socket, &QTcpSocket::readyRead, socket, nullptr);
                const auto url = QUrl::fromEncoded(buffer.split(' ').value(1));
                urls.append(url); requests.append(buffer);
                const auto result = answer ? answer(url, buffer) : Answer{404, "{}", {}};
                socket->write("HTTP/1.1 " + QByteArray::number(result.status) + " Result\r\nContent-Type: application/json\r\n"
                              + result.headers + "Connection: close\r\nContent-Length: " + QByteArray::number(result.body.size())
                              + "\r\n\r\n" + result.body);
                socket->disconnectFromHost();
            });
        });
        if (!listen(QHostAddress::LocalHost)) qFatal("Cannot listen for registry tests");
    }
    AddonRegistryClient::Settings settings(bool enabled = true) const
    { return {enabled, QStringLiteral("http://127.0.0.1:%1/v1").arg(serverPort())}; }
};
}

class TestAddonRegistry : public QObject
{
    Q_OBJECT
private slots:
    void indexKeepsEachReleaseIdentity()
    {
        RegistryIndex parsed; QString error;
        QVERIFY2(registry::parseIndex(index({indexRelease("1.2.0"), indexRelease("1.1.0-beta.1")}, Source, {"1.0.1"}), &parsed, &error), qPrintable(error));
        QCOMPARE(parsed.revision, qint64(7));
        const auto* entry = parsed.find(QStringLiteral("org.example.midnight"));
        QVERIFY(entry);
        QCOMPARE(entry->releases.size(), 2);
        QCOMPARE(entry->releases.first().install.source, Source);
        QCOMPARE(entry->releases.first().install.tag, QStringLiteral("v1.2.0"));
        QCOMPARE(entry->releases.first().install.digest, QByteArray(64, 'd'));
        QVERIFY(entry->releases.last().prerelease);
        QCOMPARE(entry->yanked, QStringList{QStringLiteral("1.0.1")});
    }
    void indexRefusesAnyWrongIdentity_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        auto with = [](const char* key, const QJsonValue& value) { auto release = indexRelease("1.2.0"); release.insert(QLatin1String(key), value); return index({release}); };
        QTest::newRow("http source") << index({indexRelease("1.2.0")}, QStringLiteral("http://github.com/example/x.git"));
        QTest::newRow("user in source") << index({indexRelease("1.2.0")}, QStringLiteral("https://me@github.com/example/x.git"));
        QTest::newRow("short object") << with("tagObjectId", QString(39, QLatin1Char('a')));
        QTest::newRow("mixed widths") << with("commitId", QString(64, QLatin1Char('b')));
        QTest::newRow("upper hex") << with("digest", QString(64, QLatin1Char('D')));
        QTest::newRow("unknown format") << with("objectFormat", "md5");
        QTest::newRow("tag is not the version") << with("tag", "v1.2.1");
        QTest::newRow("tag with a path") << with("tag", "release/1.2.0");
        QTest::newRow("prerelease disagrees") << with("prerelease", true);
        QTest::newRow("version") << with("version", "1.2");
        QTest::newRow("range") << with("addonApi", "");
        QTest::newRow("too many releases") << index(QJsonArray{indexRelease("1.0.0"), indexRelease("1.1.0"), indexRelease("1.2.0"), indexRelease("1.3.0"),
            indexRelease("1.4.0"), indexRelease("1.5.0"), indexRelease("1.6.0"), indexRelease("1.7.0"), indexRelease("1.8.0"), indexRelease("1.9.0"), indexRelease("2.0.0")});
        QTest::newRow("not JSON") << QByteArray("<html>");
        QTest::newRow("too large") << QByteArray(registry::MaximumIndexBytes + 1, ' ');
        const auto entry = QJsonObject{{"id", "org.example.midnight"}, {"source", Source}, {"releases", QJsonArray{}}, {"yanked", QJsonArray{}}};
        QTest::newRow("duplicate add-on") << QJsonDocument(QJsonObject{{"revision", 1}, {"addons", QJsonArray{entry, entry}}}).toJson();
    }
    void indexRefusesAnyWrongIdentity()
    {
        QFETCH(QByteArray, bytes);
        RegistryIndex parsed; QString error;
        QVERIFY(!registry::parseIndex(bytes, &parsed, &error));
        QVERIFY(!error.isEmpty());
    }
    void detailAndPageParse()
    {
        auto detail = summary();
        detail.insert("source", Source); detail.insert("homepageUrl", QJsonValue::Null);
        detail.insert("description", "# Midnight\n\nA dark theme.\u0007"); detail.insert("state", "listed");
        detail.insert("latestRelease", fullRelease()); detail.insert("releasesUrl", "https://api.hakone.org/v1/addons/org.example.midnight/releases");
        RegistryAddon addon; QString error;
        QVERIFY2(registry::parseAddon(json(detail), &addon, false, &error), qPrintable(error));
        QCOMPARE(addon.latestRelease->install.commitId, QString(40, QLatin1Char('b')));
        QVERIFY2(!addon.description.contains(QChar(7)), "control characters are removed");
        QVERIFY(addon.description.contains(QLatin1Char('\n')));
        QVERIFY(addon.toVariant().value("latestRelease").toMap().value("compatible").toBool());

        // The release must belong to the add-on and its source.
        auto other = fullRelease(); other.insert("addonId", "org.example.other");
        detail.insert("latestRelease", other);
        QVERIFY(!registry::parseAddon(json(detail), &addon, false, &error));

        RegistryPage page;
        QVERIFY2(registry::parsePage(json({{"items", QJsonArray{summary(), summary("org.example.dawn")}}, {"next", "abc"}}), &page, false, &error), qPrintable(error));
        QCOMPARE(page.items.size(), 2);
        QCOMPARE(page.next, QStringLiteral("abc"));
        auto withoutLatest = summary(); withoutLatest.insert("latest", QJsonValue::Null);
        QVERIFY(registry::parsePage(json({{"items", QJsonArray{withoutLatest}}, {"next", QJsonValue::Null}}), &page, false, &error));
        QVERIFY(page.items.first().latestVersion.isEmpty());
    }
    void linksUseHttpsUnlessTheRegistryIsLocal()
    {
        auto item = summary(); item.insert("webUrl", "http://addons.localhost:3000/addons/org.example.midnight");
        RegistryPage page;
        QVERIFY(registry::parsePage(json({{"items", QJsonArray{item}}, {"next", QJsonValue::Null}}), &page, false));
        QVERIFY(page.items.first().webUrl.isEmpty());
        QVERIFY(registry::parsePage(json({{"items", QJsonArray{item}}, {"next", QJsonValue::Null}}), &page, true));
        QCOMPARE(page.items.first().webUrl, QStringLiteral("http://addons.localhost:3000/addons/org.example.midnight"));
        item.insert("webUrl", "javascript:alert(1)");
        QVERIFY(registry::parsePage(json({{"items", QJsonArray{item}}, {"next", QJsonValue::Null}}), &page, true));
        QVERIFY(page.items.first().webUrl.isEmpty());
    }
    void yankedReleaseKeepsItsReason()
    {
        auto value = fullRelease(); value.insert("state", "yanked"); value.insert("yankReason", "Breaks playback.");
        RegistryRelease release;
        QVERIFY(registry::parseRelease(json(value), &release, false));
        QVERIFY(release.yanked());
        QCOMPARE(release.yankReason, QStringLiteral("Breaks playback."));
        value.insert("state", "draft");
        QVERIFY(!registry::parseRelease(json(value), &release, false));
    }
    void fetchedReleaseMustMatchTheRegistry_data()
    {
        QTest::addColumn<QString>("field");
        for (const char* field : {"source", "tag", "objectFormat", "tagObjectId", "commitId", "digest", "id", "version", "developer"})
            QTest::newRow(field) << QString::fromLatin1(field);
    }
    void fetchedReleaseMustMatchTheRegistry()
    {
        QFETCH(QString, field);
        RegistryRelease release;
        QVERIFY(registry::parseRelease(json(fullRelease()), &release, false));
        auto candidate = candidateFor(release);
        QString error;
        QVERIFY2(verifyRegistryIdentity(candidate, release, &error), qPrintable(error));
        if (field == "source") candidate.identity.source = QStringLiteral("https://github.com/attacker/addon-midnight.git");
        if (field == "tag") candidate.identity.tag = QStringLiteral("1.2.0");
        if (field == "objectFormat") candidate.identity.objectFormat = QStringLiteral("sha256");
        if (field == "tagObjectId") candidate.identity.tagObjectId = QString(40, QLatin1Char('e'));
        if (field == "commitId") candidate.identity.commitId = QString(40, QLatin1Char('e'));
        if (field == "digest") candidate.digest = QByteArray(64, 'e');
        if (field == "id") candidate.manifest.id = QStringLiteral("org.example.other");
        if (field == "version") candidate.manifest.version = QStringLiteral("1.2.1");
        if (field == "developer") candidate.identity.developerSource = true;
        QVERIFY(!verifyRegistryIdentity(candidate, release, &error));
        QVERIFY(error.contains(QStringLiteral("does not match the registry")));
    }
    void updateSelectionFollowsTheGitRules()
    {
        RegistryIndex parsed;
        QVERIFY(registry::parseIndex(index({indexRelease("2.0.0"), indexRelease("1.10.0"), indexRelease("1.9.0"),
                                            indexRelease("1.11.0-beta.1"), indexRelease("1.12.0", 'a', "^2.0.0")}), &parsed));
        const auto& entry = parsed.addons.first();
        const auto update = selectRegistryUpdate(entry, QStringLiteral("1.0.0"), Source, QStringLiteral("^1.0.0"), false);
        QVERIFY(update);
        QCOMPARE(update->version, QStringLiteral("1.10.0"));
        // The prerelease needs the policy; 1.12.0 needs a newer host.
        QCOMPARE(selectRegistryUpdate(entry, QStringLiteral("1.0.0"), Source, QStringLiteral("^1.0.0"), true)->version, QStringLiteral("1.11.0-beta.1"));
        QVERIFY(!selectRegistryUpdate(entry, QStringLiteral("1.10.0"), Source, QStringLiteral("^1.10.0"), false));
        QVERIFY(!selectRegistryUpdate(entry, QStringLiteral("1.0.0"), QStringLiteral("https://github.com/other/fork.git"), QStringLiteral("^1.0.0"), false));
    }
    void addressRules()
    {
        QVERIFY(AddonRegistryClient::addressProblem(QStringLiteral("https://api.hakone.org/v1")).isEmpty());
        QVERIFY(AddonRegistryClient::addressProblem(QStringLiteral("http://api.localhost:3000/v1/")).isEmpty());
        QVERIFY(AddonRegistryClient::addressProblem(QStringLiteral("http://127.0.0.1:3000/v1")).isEmpty());
        QVERIFY(AddonRegistryClient::addressProblem(QStringLiteral("http://[::1]:3000/v1")).isEmpty());
        QVERIFY(!AddonRegistryClient::addressProblem(QStringLiteral("http://api.hakone.org/v1")).isEmpty());
        QVERIFY(!AddonRegistryClient::addressProblem(QStringLiteral("http://localhost.example.com/v1")).isEmpty());
        QVERIFY(!AddonRegistryClient::addressProblem(QStringLiteral("https://me:secret@api.hakone.org/v1")).isEmpty());
        QVERIFY(!AddonRegistryClient::addressProblem(QStringLiteral("https://api.hakone.org/v1?x=1")).isEmpty());
        QVERIFY(!AddonRegistryClient::addressProblem(QStringLiteral("ftp://api.hakone.org/v1")).isEmpty());
        QVERIFY(!AddonRegistryClient::addressProblem({}).isEmpty());
    }
    void indexIsCachedAndRevalidated()
    {
        RegistryFixture server;
        const QByteArray body = index({indexRelease("1.2.0")});
        server.answer = [&](const QUrl& url, const QByteArray& request) -> RegistryFixture::Answer {
            if (url.path() != QLatin1String("/v1/index")) return {404, "{}", {}};
            // Header names are case-insensitive; Qt sends them in lower case.
            if (request.toLower().contains("if-none-match: \"one\"")) return {304, {}, "ETag: \"one\"\r\n"};
            return {200, body, "ETag: \"one\"\r\n"};
        };
        QTemporaryDir cache;
        auto settings = server.settings();
        {
            AddonRegistryClient client(cache.path());
            client.setSettingsSource([&] { return settings; });
            bool done = false, changed = false;
            client.loadIndex([&](bool value, const AddonRegistryClient::Failure& failure) { done = true; changed = value; QVERIFY(failure.code.isEmpty()); });
            QTRY_VERIFY(done);
            QVERIFY(changed);
            QVERIFY(client.index().find(QStringLiteral("org.example.midnight")));
            QVERIFY(!server.requests.last().toLower().contains("if-none-match"));
            QVERIFY(!server.requests.last().toLower().contains("cookie:"));
            done = false;
            client.loadIndex([&](bool value, const AddonRegistryClient::Failure&) { done = true; changed = value; });
            QTRY_VERIFY(done);
            QVERIFY2(!changed, "a current ETag gets a 304");
        }
        // A new client starts from the disk cache and revalidates it.
        AddonRegistryClient client(cache.path());
        server.answer = [](const QUrl&, const QByteArray&) -> RegistryFixture::Answer { return {500, "{}", {}}; };
        client.setSettingsSource([&] { return settings; });
        bool done = false; QString code;
        client.loadIndex([&](bool, const AddonRegistryClient::Failure& failure) { done = true; code = failure.code; });
        QTRY_VERIFY(done);
        QCOMPARE(code, QStringLiteral("http"));
        QVERIFY2(client.index().find(QStringLiteral("org.example.midnight")), "the cached index stays when the registry fails");
        QVERIFY(server.requests.last().toLower().contains("if-none-match: \"one\""));
    }
    void searchAsksForWhatThisHostRuns()
    {
        RegistryFixture server;
        server.answer = [](const QUrl&, const QByteArray&) -> RegistryFixture::Answer {
            return {200, json({{"items", QJsonArray{summary()}}, {"next", QJsonValue::Null}}), {}};
        };
        AddonRegistryClient client(QDir::tempPath());
        const auto settings = server.settings();
        client.setSettingsSource([&] { return settings; });
        QUrlQuery query; query.addQueryItem(QStringLiteral("q"), QStringLiteral("dark theme")); query.addQueryItem(QStringLiteral("host"), QStringLiteral("1.0.0"));
        std::optional<RegistryPage> page;
        client.search(query, [&](const std::optional<RegistryPage>& value, const AddonRegistryClient::Failure&) { page = value; });
        QTRY_VERIFY(page.has_value());
        QCOMPARE(page->items.first().id, QStringLiteral("org.example.midnight"));
        QCOMPARE(server.urls.last().path(), QStringLiteral("/v1/addons"));
        QCOMPARE(QUrlQuery(server.urls.last()).queryItemValue(QStringLiteral("q"), QUrl::FullyDecoded), QStringLiteral("dark theme"));
    }
    void failuresHaveCodes()
    {
        RegistryFixture server;
        AddonRegistryClient client(QDir::tempPath());
        auto settings = server.settings();
        client.setSettingsSource([&] { return settings; });
        auto codeFor = [&](RegistryFixture::Answer answer) {
            server.answer = [answer](const QUrl&, const QByteArray&) { return answer; };
            QString code; bool done = false;
            client.addon(QStringLiteral("org.example.midnight"), [&](const std::optional<RegistryAddon>&, const AddonRegistryClient::Failure& failure) { code = failure.code; done = true; });
            [&] { QTRY_VERIFY(done); }();
            return code;
        };
        QCOMPARE(codeFor({404, R"({"error":{"code":"not_found","message":"x"}})", {}}), QStringLiteral("not_found"));
        QCOMPARE(codeFor({429, "{}", "Retry-After: 60\r\n"}), QStringLiteral("rate_limited"));
        QCOMPARE(codeFor({200, "not json", {}}), QStringLiteral("invalid"));
        QCOMPARE(codeFor({200, QByteArray(registry::MaximumResponseBytes + 10, ' '), {}}), QStringLiteral("too_large"));
        QCOMPARE(codeFor({302, {}, "Location: https://elsewhere.example/v1/addons/x\r\n"}), QStringLiteral("invalid"));
        settings.enabled = false;
        QCOMPARE(codeFor({200, "{}", {}}), QStringLiteral("off"));
        settings = {true, QStringLiteral("http://api.hakone.org/v1")};
        QCOMPARE(codeFor({200, "{}", {}}), QStringLiteral("address"));
    }
};
QTEST_GUILESS_MAIN(TestAddonRegistry)
#include "test_addon_registry.moc"
