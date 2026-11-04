// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>
#include "util/github_release_service.hpp"

using namespace vlc::updates;

static Version version(const char* value)
{
    return Version::parse(QString::fromLatin1(value)).value();
}

// A legacy release without a metadata block, named like the pre-rebrand packages.
static QJsonObject release(const QString& tag, const QString& suffix = QStringLiteral("win64.zip"))
{
    const QString name = QStringLiteral("vlc-juxtopposed-") + tag + QLatin1Char('-') + suffix;
    const auto base = QStringLiteral("https://github.com/hakone-org/hakone/releases/");
    return {{QStringLiteral("tag_name"), tag}, {QStringLiteral("draft"), false},
            {QStringLiteral("prerelease"), Version::parse(tag)->isPrerelease()},
            {QStringLiteral("html_url"), QString(base + QStringLiteral("tag/") + tag)},
            {QStringLiteral("body"), QStringLiteral("Release notes <b>remain plain text</b>")},
            {QStringLiteral("assets"), QJsonArray{QJsonObject{
                {QStringLiteral("name"), name}, {QStringLiteral("state"), QStringLiteral("uploaded")},
                {QStringLiteral("size"), 1234},
                {QStringLiteral("digest"), QStringLiteral("sha256:example")},
                {QStringLiteral("browser_download_url"), QString(base + QStringLiteral("download/") + tag + QLatin1Char('/') + name)}}}}};
}

static QJsonObject metadata(const QString& tag, int generation = 1)
{
    return {{QStringLiteral("schema"), 1}, {QStringLiteral("version"), tag},
            {QStringLiteral("generation"), generation},
            {QStringLiteral("product_name"), QStringLiteral("Sample Player")},
            {QStringLiteral("package_prefix"), QStringLiteral("sample-player")},
            {QStringLiteral("subtitle"), QStringLiteral("A release announcement <b>in plain text</b>")}};
}

static QString metadataBlock(const QJsonObject& object)
{
    return QStringLiteral("<!-- vlc-update-metadata\n") +
        QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)) + QStringLiteral("\n-->");
}

static QJsonObject describedRelease(const QString& tag, int generation = 1,
                                    const QString& suffix = QStringLiteral("win64.zip"))
{
    auto result = release(tag, suffix);
    result[QStringLiteral("body")] = QString(QStringLiteral("Notes\n\n") + metadataBlock(metadata(tag, generation)));
    auto asset = result.value(QStringLiteral("assets")).toArray().first().toObject();
    for (const auto* key : {"name", "browser_download_url"})
        asset[QLatin1String(key)] = asset.value(QLatin1String(key)).toString().replace(
            QStringLiteral("vlc-juxtopposed-"), QStringLiteral("sample-player-"));
    result[QStringLiteral("assets")] = QJsonArray{asset};
    return result;
}

class HttpFixture : public QTcpServer
{
public:
    std::function<QByteArray(int)> body;
    int status = 200;
    bool stall = false;
    int requests = 0;
    HttpFixture()
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            auto* socket = nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer = QByteArray()]() mutable {
                buffer += socket->readAll();
                if (!buffer.contains("\r\n\r\n")) return;
                ++requests;
                disconnect(socket, &QTcpSocket::readyRead, socket, nullptr);
                if (stall) return;
                const auto url = QUrl::fromEncoded(buffer.split(' ').value(1));
                const int page = QUrlQuery(url).queryItemValue(QStringLiteral("page")).toInt();
                const auto payload = body ? body(page) : QByteArray("[]");
                socket->write(QByteArray("HTTP/1.1 ") + QByteArray::number(status) +
                              " Result\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " +
                              QByteArray::number(payload.size()) + "\r\n\r\n" + payload);
                socket->disconnectFromHost();
            });
        });
        if (!listen(QHostAddress::LocalHost)) qFatal("Cannot listen for release tests");
    }
    GithubReleaseService::Options options() const
    {
        GithubReleaseService::Options result;
        result.endpoint = QUrl(QStringLiteral("http://127.0.0.1:%1/releases").arg(serverPort()));
        return result;
    }
};

class TestGithubReleases : public QObject
{
    Q_OBJECT
private slots:
    void precedence_data()
    {
        QTest::addColumn<QString>("installed");
        QTest::addColumn<QString>("candidate");
        QTest::addColumn<int>("expected");
        const char* rows[][3] = {
            {"4.0.0", "3.5.994", "-1"}, {"4.0.0", "4.0.3", "1"},
            {"4.0.0-beta.5", "4.0.0-beta.6", "1"},
            {"4.0.0-beta.9", "4.0.0-beta.10", "1"},
            {"4.0.0-beta.5", "4.0.0", "1"},
            {"4.1.0-beta.5", "4.0.9", "-1"},
            {"4.0.0-rc.1", "4.0.0-beta.99", "-1"},
            {"4.0.0-beta.1", "4.0.0-beta.1.1", "1"},
            {"4.0.0+build.1", "4.0.0+build.2", "0"},
            {"4.0.0-beta.999999999999999999999999", "4.0.0-beta.1000000000000000000000000", "1"},
            {"4.0.0-1", "4.0.0-alpha", "1"},
            {"beta-3", "4.0.0-beta.4", "1"}
        };
        for (const auto& row : rows)
            QTest::newRow(row[1]) << QString::fromLatin1(row[0]) << QString::fromLatin1(row[1]) << QByteArray(row[2]).toInt();
    }
    void precedence()
    {
        QFETCH(QString, installed); QFETCH(QString, candidate); QFETCH(int, expected);
        QCOMPARE(Version::parse(candidate)->compare(*Version::parse(installed)), expected);
    }
    void parsing()
    {
        for (const auto* invalid : {"", "4.0", "04.0.0", "4.0.0-beta.01", "4.0.0-", "4.0.0+", "4.0.0.1",
                                    "4.0.0-beta/5", "999999999999999999999.0.0", "beta-0"})
            QVERIFY2(!Version::parse(QString::fromLatin1(invalid)), invalid);
        QCOMPARE(version("v4.0.0-beta.5").text, QStringLiteral("4.0.0-beta.5"));
        QCOMPARE(version("beta-3").text, QStringLiteral("4.0.0-beta.3"));
    }
    void platform_data()
    {
        QTest::addColumn<QString>("os"); QTest::addColumn<QString>("arch"); QTest::addColumn<QString>("suffix");
        QTest::newRow("win64") << QStringLiteral("windows") << QStringLiteral("x86_64") << QStringLiteral("win64.zip");
        QTest::newRow("win32") << QStringLiteral("windows") << QStringLiteral("x86") << QStringLiteral("win32.zip");
        QTest::newRow("winarm") << QStringLiteral("windows") << QStringLiteral("arm64") << QStringLiteral("winarm64.zip");
        QTest::newRow("macarm") << QStringLiteral("macos") << QStringLiteral("arm64") << QStringLiteral("macos-arm64.zip");
        QTest::newRow("macintel") << QStringLiteral("macos") << QStringLiteral("x86_64") << QStringLiteral("macos-x86_64.zip");
        QTest::newRow("universal") << QStringLiteral("macos") << QStringLiteral("arm64") << QStringLiteral("macos-universal.zip");
        QTest::newRow("linux") << QStringLiteral("linux") << QStringLiteral("x86_64") << QStringLiteral("linux-x86_64.tar.gz");
        QTest::newRow("linuxarm") << QStringLiteral("linux") << QStringLiteral("arm64") << QStringLiteral("linux-arm64.tar.gz");
    }
    void platform()
    {
        QFETCH(QString, os); QFETCH(QString, arch); QFETCH(QString, suffix);
        const auto result = selectRelease({release(QStringLiteral("4.0.1"), suffix)}, version("4.0.0"), {os, arch});
        QVERIFY(result);
        QVERIFY(result->asset.name.endsWith(suffix));
        QCOMPARE(result->asset.size, 1234);
        QCOMPARE(result->asset.digest, QStringLiteral("sha256:example"));
    }
    void selection()
    {
        const Target linuxTarget{QStringLiteral("linux"), QStringLiteral("x86_64")};
        const auto linuxSuffix = QStringLiteral("linux-x86_64.tar.gz");
        // Intentionally unsorted: an older branch can be published most recently.
        const QJsonArray releases{release(QStringLiteral("3.5.994"), linuxSuffix),
            release(QStringLiteral("4.0.3")), release(QStringLiteral("4.0.2"), linuxSuffix),
            release(QStringLiteral("4.0.1"), linuxSuffix)};
        const auto result = selectRelease(releases, version("4.0.0"), linuxTarget);
        QVERIFY(result);
        QCOMPARE(result->version.text, QStringLiteral("4.0.2"));
        QVERIFY(!selectRelease(releases, version("4.1.0-beta.1"), linuxTarget));
        QVERIFY(!selectRelease(releases, version("4.0.0"), {QStringLiteral("linux"), QStringLiteral("riscv64")}));
        const Target win{QStringLiteral("windows"), QStringLiteral("x86_64")};
        QVERIFY(!selectRelease({release(QStringLiteral("4.1.0-beta.1"))}, version("4.0.0"), win));
        auto stable = selectRelease({release(QStringLiteral("4.0.0"))}, version("4.0.0-beta.5"), win);
        QVERIFY(stable);
        auto legacy = selectRelease({release(QStringLiteral("beta-3"))}, version("4.0.0-beta.2"), win);
        QVERIFY(legacy);
        QCOMPARE(legacy->tag, QStringLiteral("beta-3"));
    }
    void rejectUnavailableOrInvalid()
    {
        const Target target{QStringLiteral("windows"), QStringLiteral("x86_64")};
        const auto original = release(QStringLiteral("4.0.1"));
        auto object = original;
        object[QStringLiteral("draft")] = true;
        QVERIFY(!selectRelease({object}, version("4.0.0"), target));
        object = original; object[QStringLiteral("prerelease")] = true;
        QVERIFY(!selectRelease({object}, version("4.0.0"), target));
        object = original; object[QStringLiteral("html_url")] = QStringLiteral("https://example.com/release");
        QVERIFY(!selectRelease({object}, version("4.0.0"), target));
        const QPair<QString, QJsonValue> changes[] = {
            {QStringLiteral("state"), QStringLiteral("starter")}, {QStringLiteral("size"), 0},
            {QStringLiteral("name"), QStringLiteral("vlc-juxtopposed-4.0.1-win64.zip.sha256")},
            {QStringLiteral("name"), QStringLiteral("vlc-juxtopposed-4.0.1-source.zip")},
            {QStringLiteral("browser_download_url"), QStringLiteral("https://example.com/vlc.zip")}
        };
        for (const auto& change : changes)
        {
            object = original;
            auto asset = object.value(QStringLiteral("assets")).toArray().first().toObject();
            asset[change.first] = change.second;
            object[QStringLiteral("assets")] = QJsonArray{asset};
            QVERIFY(!selectRelease({object}, version("4.0.0"), target));
        }
    }
    void generationsAndMetadata()
    {
        const Target win{QStringLiteral("windows"), QStringLiteral("x86_64")};
        const auto candidate = describedRelease(QStringLiteral("1.0.0"));
        // Explicit generations take precedence; ordinary tags retain SemVer ordering.
        QVERIFY(!selectRelease({release(QStringLiteral("1.0.0"))}, version("4.0.0-beta.5"), win));
        const auto chosen = selectRelease({release(QStringLiteral("9.0.0")), candidate}, version("4.0.0-beta.5"), win);
        QVERIFY(chosen);
        QCOMPARE(chosen->version.generation, 1u);
        QCOMPARE(chosen->version.text, QStringLiteral("1.0.0"));
        QCOMPARE(chosen->productName, QStringLiteral("Sample Player"));
        QCOMPARE(chosen->subtitle, QStringLiteral("A release announcement <b>in plain text</b>"));
        QCOMPARE(chosen->notes, QStringLiteral("Notes"));
        QVERIFY(chosen->asset.name.startsWith(QStringLiteral("sample-player-")));
        auto installed = version("1.0.0");
        installed.generation = 1;
        QVERIFY(!selectRelease({release(QStringLiteral("9.0.0")), candidate}, installed, win));
        const auto next = selectRelease({release(QStringLiteral("9.0.0")),
            describedRelease(QStringLiteral("1.0.1")), describedRelease(QStringLiteral("1.0.2"))}, installed, win);
        QVERIFY(next);
        QCOMPARE(next->version.text, QStringLiteral("1.0.2"));
        QVERIFY(!selectRelease({describedRelease(QStringLiteral("2.0.0-beta.1"), 2)}, installed, win));
        auto beta = version("4.0.0-beta.5");
        QVERIFY(selectRelease({describedRelease(QStringLiteral("1.0.0-beta.1"))}, beta, win));
        for (const auto& suffix : {QStringLiteral("win64.zip"), QStringLiteral("macos-arm64.zip"),
                                  QStringLiteral("macos-x86_64.zip"), QStringLiteral("linux-x86_64.tar.gz")})
        {
            const Target target = suffix.startsWith(QLatin1String("macos"))
                ? Target{QStringLiteral("macos"), suffix.contains(QLatin1String("arm64")) ? QStringLiteral("arm64") : QStringLiteral("x86_64")}
                : Target{suffix.startsWith(QLatin1String("linux")) ? QStringLiteral("linux") : QStringLiteral("windows"), QStringLiteral("x86_64")};
            QVERIFY(selectRelease({describedRelease(QStringLiteral("1.0.0"), 1, suffix)}, beta, target));
        }
        auto draft = candidate;
        draft[QStringLiteral("draft")] = true;
        QVERIFY(!selectRelease({draft}, beta, win));
        const auto legacy = selectRelease({release(QStringLiteral("4.0.0-beta.5"))}, version("4.0.0-beta.4"), win);
        QVERIFY(legacy);
        QCOMPARE(legacy->productName, QStringLiteral("Hakone"));
        QVERIFY(legacy->subtitle.isEmpty());
    }
    void macDiskImagesAreNotUpdatePackages()
    {
        // Mac releases also publish a drag-to-install .dmg (and checksums) beside
        // each .zip. Exact names keep the .zip selected, whatever the asset order.
        const auto tag = QStringLiteral("1.0.0");
        const auto base = QStringLiteral("https://github.com/hakone-org/hakone/releases/download/1.0.0/");
        const auto asset = [&](const QString& suffix) {
            const QString name = QStringLiteral("sample-player-1.0.0-") + suffix;
            return QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("state"), QStringLiteral("uploaded")},
                               {QStringLiteral("size"), 1234}, {QStringLiteral("digest"), QStringLiteral("sha256:example")},
                               {QStringLiteral("browser_download_url"), QString(base + name)}};
        };
        auto object = describedRelease(tag);
        const auto beta = version("0.9.0-beta.1");
        for (const auto& arch : {QStringLiteral("arm64"), QStringLiteral("x86_64")})
        {
            const Target mac{QStringLiteral("macos"), arch};
            const auto prefix = QStringLiteral("macos-") + arch;
            QJsonArray assets{asset(prefix + QStringLiteral(".dmg")), asset(prefix + QStringLiteral(".dmg.sha256")),
                              asset(prefix + QStringLiteral(".zip")), asset(prefix + QStringLiteral(".zip.sha256"))};
            for (int order = 0; order < 2; ++order)
            {
                object[QStringLiteral("assets")] = assets;
                const auto chosen = selectRelease({object}, beta, mac);
                QVERIFY(chosen);
                QCOMPARE(chosen->asset.name, QStringLiteral("sample-player-1.0.0-") + prefix + QStringLiteral(".zip"));
                QCOMPARE(chosen->asset.url.toString(), base + chosen->asset.name);
                QJsonArray reversed;
                for (auto i = assets.size(); i > 0; --i)
                    reversed.append(assets.at(i - 1));
                assets = reversed;
            }
            // A disk image alone is not an update package for the in-app checker.
            object[QStringLiteral("assets")] = QJsonArray{asset(prefix + QStringLiteral(".dmg")),
                                                         asset(QStringLiteral("macos-universal.dmg"))};
            QVERIFY(!selectRelease({object}, beta, mac));
        }
    }
    void invalidMetadata()
    {
        const Target win{QStringLiteral("windows"), QStringLiteral("x86_64")};
        const auto original = describedRelease(QStringLiteral("1.0.0"));
        const QPair<QString, QJsonValue> changes[] = {
            {QStringLiteral("schema"), 2}, {QStringLiteral("schema"), true},
            {QStringLiteral("version"), QStringLiteral("1.0.1")},
            {QStringLiteral("generation"), -1}, {QStringLiteral("generation"), 1.5},
            {QStringLiteral("generation"), 2147483648.0}, {QStringLiteral("generation"), true},
            {QStringLiteral("product_name"), QString()}, {QStringLiteral("product_name"), QStringLiteral("Name\nInjected")},
            {QStringLiteral("product_name"), QString(129, QLatin1Char('x'))},
            {QStringLiteral("package_prefix"), QStringLiteral("../player")},
            {QStringLiteral("package_prefix"), QStringLiteral("other-player")},
            {QStringLiteral("subtitle"), true}, {QStringLiteral("subtitle"), QStringLiteral("Line\nBreak")},
            {QStringLiteral("subtitle"), QString(513, QLatin1Char('x'))}
        };
        for (const auto& change : changes)
        {
            auto object = original;
            auto data = metadata(QStringLiteral("1.0.0"));
            data[change.first] = change.second;
            object[QStringLiteral("body")] = metadataBlock(data);
            QVERIFY2(!selectRelease({object}, version("4.0.0-beta.5"), win), qPrintable(change.first));
        }
        for (const auto& body : {QStringLiteral("<!-- vlc-update-metadata {"),
                                QStringLiteral("<!-- vlc-update-metadata [] -->"),
                                QStringLiteral("<!-- vlc-update-metadata {bad} -->"),
                                metadataBlock(metadata(QStringLiteral("1.0.0"))).repeated(2)})
        {
            auto object = original;
            object[QStringLiteral("body")] = body;
            QVERIFY(!selectRelease({object}, version("4.0.0-beta.5"), win));
        }
        auto data = metadata(QStringLiteral("1.0.0"));
        data.remove(QStringLiteral("subtitle"));
        auto object = original;
        object[QStringLiteral("body")] = metadataBlock(data);
        const auto chosen = selectRelease({object}, version("4.0.0-beta.5"), win);
        QVERIFY(chosen);
        QVERIFY(chosen->subtitle.isEmpty());
        // Metadata never relaxes the repository URL restriction.
        object[QStringLiteral("html_url")] = QStringLiteral("https://github.com/other/project/releases/tag/1.0.0");
        QVERIFY(!selectRelease({object}, version("4.0.0-beta.5"), win));
    }
    void unicodeAnnouncements()
    {
        auto object = describedRelease(QStringLiteral("1.0.0"));
        auto data = metadata(QStringLiteral("1.0.0"));
        const auto subtitle = QString::fromUtf8("日本語のリリース 🎉");
        data[QStringLiteral("subtitle")] = subtitle;
        object[QStringLiteral("body")] = metadataBlock(data);
        const Target win{QStringLiteral("windows"), QStringLiteral("x86_64")};
        const auto chosen = selectRelease({object}, version("4.0.0-beta.5"), win);
        QVERIFY(chosen);
        QCOMPARE(chosen->subtitle, subtitle);
        data[QStringLiteral("subtitle")] = QStringLiteral("Hidden\u202econtrol");
        object[QStringLiteral("body")] = metadataBlock(data);
        QVERIFY(!selectRelease({object}, version("4.0.0-beta.5"), win));
    }
    void generationPagination_data()
    {
        QTest::addColumn<bool>("reverse");
        QTest::newRow("generation-first") << false;
        QTest::newRow("generation-last") << true;
    }
    void generationPagination()
    {
        QFETCH(bool, reverse);
        HttpFixture server;
        server.body = [reverse](int page) {
            QJsonArray entries;
            entries.append((page == 1) != reverse ? describedRelease(QStringLiteral("1.0.0"))
                                                               : release(QStringLiteral("9.0.0")));
            if (page == 1) while (entries.size() < 100) entries.append(QJsonObject());
            return QJsonDocument(entries).toJson();
        };
        GithubReleaseService service(server.options());
        QSignalSpy done(&service, &GithubReleaseService::finished);
        service.check(version("4.0.0-beta.5"), {QStringLiteral("windows"), QStringLiteral("x86_64")});
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(done.takeFirst().at(0).toBool());
        QVERIFY(service.release());
        QCOMPARE(service.release()->version.text, QStringLiteral("1.0.0"));
        QCOMPARE(service.release()->version.generation, 1u);
        QCOMPARE(server.requests, 2);
    }
    void paginationAndLateAssets()
    {
        HttpFixture server;
        bool available = false;
        server.body = [&available](int page) {
            QJsonArray entries;
            if (page == 1) for (int i = 0; i < 100; ++i) entries.append(release(QStringLiteral("3.5.994")));
            else if (available) entries.append(release(QStringLiteral("4.0.1")));
            return QJsonDocument(entries).toJson();
        };
        GithubReleaseService service(server.options());
        QSignalSpy done(&service, &GithubReleaseService::finished);
        service.check(version("4.0.0"), {QStringLiteral("windows"), QStringLiteral("x86_64")});
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(done.takeFirst().at(0).toBool());
        QVERIFY(!service.release());
        available = true;
        service.check(version("4.0.0"), {QStringLiteral("windows"), QStringLiteral("x86_64")});
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(done.takeFirst().at(0).toBool());
        QVERIFY(service.release());
        QCOMPARE(server.requests, 4);
        QCOMPARE(service.release()->version.text, QStringLiteral("4.0.1"));
        server.status = 403;
        service.check(version("4.0.0"), {QStringLiteral("windows"), QStringLiteral("x86_64")});
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(!done.takeFirst().at(0).toBool());
        QVERIFY(!service.release());
    }
    void networkFailures_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("rate-limit") << 0;
        QTest::newRow("malformed") << 1;
        QTest::newRow("not-array") << 2;
        QTest::newRow("oversize") << 3;
        QTest::newRow("timeout") << 4;
        QTest::newRow("pagination-limit") << 5;
    }
    void networkFailures()
    {
        QFETCH(int, mode);
        HttpFixture server;
        auto options = server.options();
        if (mode == 0) server.status = 429;
        if (mode == 1) server.body = [](int) { return QByteArray("["); };
        if (mode == 2) server.body = [](int) { return QByteArray("{}"); };
        if (mode == 3) { options.maxJsonBytes = 2; server.body = [](int) { return QByteArray("[{},{}]"); }; }
        if (mode == 4) { server.stall = true; options.timeoutMs = 20; }
        if (mode == 5)
        {
            options.maxPages = 1;
            server.body = [](int) { QJsonArray entries; for (int i = 0; i < 100; ++i) entries.append(QJsonObject()); return QJsonDocument(entries).toJson(); };
        }
        GithubReleaseService service(options);
        QSignalSpy done(&service, &GithubReleaseService::finished);
        service.check(version("4.0.0"), {QStringLiteral("windows"), QStringLiteral("x86_64")});
        QTRY_COMPARE(done.size(), 1);
        QVERIFY(!done.takeFirst().at(0).toBool());
        QVERIFY(!service.release());
    }
};

QTEST_GUILESS_MAIN(TestGithubReleases)
#include "test_github_releases.moc"
