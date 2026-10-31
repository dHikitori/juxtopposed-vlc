// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_media_siblings.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTest>
using namespace vlc::addons;

namespace {
using Siblings = AddonMediaSiblings;
AddonHostServices::Context context(const QString& package, const QJsonArray& sources)
{
    return {package, package + QStringLiteral("/episodes"), QStringLiteral("invocation-1"),
            QJsonObject{{"media.siblings", QJsonObject{{"sources", sources}}}}, {}};
}
Siblings::Item file(int season, int episode, int version = 0, const QString& folder = QStringLiteral("Season 1"))
{
    Siblings::Item item;
    item.kind = QStringLiteral("folder");
    item.name = QStringLiteral("Show.S%1E%2%3.mkv").arg(season, 2, 10, QLatin1Char('0')).arg(episode, 2, 10, QLatin1Char('0'))
        .arg(version ? QStringLiteral(".1080p") : QString());
    item.url = QUrl::fromLocalFile(QStringLiteral("/private/shows/Show/") + folder + QLatin1Char('/') + item.name);
    item.folder = folder; item.season = season; item.episodes = {episode}; item.version = version;
    item.title = QStringLiteral("Episode %1").arg(episode);
    item.artwork = QStringLiteral("image://thumbnail/") + QString::number(season * 100 + episode);
    return item;
}
struct Call { bool done = false; AddonHostServices::Result result; };
}

class TestAddonMediaSiblings : public QObject
{
    Q_OBJECT
    Siblings::Snapshot m_snapshot;
    QList<QUrl> m_artwork;
    Call run(AddonHostServices& services, const QJsonObject& request, const QString& package = QStringLiteral("org.example.one"),
             const QJsonArray& sources = {"folder", "feed"}, int wait = 5000)
    {
        auto call = std::make_shared<Call>();
        services.dispatch(context(package, sources), QStringLiteral("media.siblings"), request,
                          [call](AddonHostServices::Result result) { call->done = true; call->result = std::move(result); }, 20000);
        QTest::qWaitFor([call] { return call->done; }, wait);
        return *call;
    }
    std::unique_ptr<Siblings> mount(AddonHostServices& services)
    {
        auto siblings = std::make_unique<Siblings>();
        Siblings::Source source;
        source.snapshot = [this] { return m_snapshot; };
        source.artwork = [this](const QUrl& url) { m_artwork.append(url); };
        if (!siblings->mount(&services, std::move(source))) return {};
        return siblings;
    }
private slots:
    void init()
    {
        m_artwork.clear();
        m_snapshot = {};
        m_snapshot.kind = QStringLiteral("folder"); m_snapshot.show = QStringLiteral("Show");
        for (int episode = 1; episode <= 3; ++episode) m_snapshot.items.append(file(1, episode));
        m_snapshot.items.append(file(1, 2, 1));
        m_snapshot.items.append(file(2, 1, 0, QStringLiteral("Season 2")));
        m_snapshot.items[0].current = true; m_snapshot.items[1].played = true; m_snapshot.items[1].progress = 0.5;
        m_snapshot.items[2].titleSource = QUrl(QStringLiteral("https://www.tvmaze.com/episodes/3"));
    }
    void readsCopiesWithoutPathsOrUrls()
    {
        AddonHostServices services; auto siblings = mount(services); QVERIFY(siblings);
        const auto call = run(services, {});
        QVERIFY2(call.done && call.result.error.isEmpty(), qPrintable(call.result.error));
        const auto response = call.result.response;
        QCOMPARE(response.value("kind").toString(), QStringLiteral("folder"));
        QCOMPARE(response.value("show").toString(), QStringLiteral("Show"));
        QCOMPARE(response.value("total").toInt(), 5);
        const auto seasons = response.value("seasons").toArray();
        QCOMPARE(seasons.size(), 2);
        QCOMPARE(seasons[0].toObject().value("season").toInt(), 1);
        QCOMPARE(seasons[0].toObject().value("count").toInt(), 3); // versions are not episodes
        QCOMPARE(response.value("currentSeason").toInt(), 1);
        const auto items = response.value("items").toArray();
        QCOMPARE(items.size(), 5);
        const auto first = items[0].toObject();
        QVERIFY(first.value("id").toString().startsWith(QStringLiteral("media:")));
        QCOMPARE(first.value("name").toString(), QStringLiteral("Show.S01E01.mkv"));
        QCOMPARE(first.value("folder").toString(), QStringLiteral("Season 1"));
        QVERIFY(first.value("current").toBool());
        QCOMPARE(items[1].toObject().value("progress").toDouble(), 0.5);
        QVERIFY(!items[2].toObject().contains("hasSource") && !first.contains("artwork") && !first.contains("description"));
        QCOMPARE(items[3].toObject().value("version").toInt(), 1);
        const auto text = QJsonDocument(response).toJson();
        QVERIFY(!text.contains("/private") && !text.contains("file:") && !text.contains("tvmaze") && !text.contains("image://"));
        // Each package gets its own IDs.
        const auto other = run(services, {}, QStringLiteral("org.example.two"));
        QVERIFY(other.result.response.value("items").toArray()[0].toObject().value("id") != first.value("id"));
        // Details are only for the items a panel shows in full.
        const auto third = items[2].toObject().value("id").toString();
        const auto detailed = run(services, {{"limit", 1}, {"details", QJsonArray{first.value("id"), third, "media:unknown"}}}).result.response;
        const auto details = detailed.value("details").toArray();
        QCOMPARE(details.size(), 2);
        QCOMPARE(details[0].toObject().value("artwork").toString(), first.value("id").toString());
        QVERIFY(details[1].toObject().value("hasSource").toBool());
        QVERIFY(!QJsonDocument(detailed).toJson().contains("tvmaze"));
        // Host artwork is kept for the app's image loading, by handle.
        const auto artwork = siblings->artwork(QStringLiteral("org.example.one"));
        QCOMPARE(artwork.value(first.value("id").toString()).toString(), QStringLiteral("image://thumbnail/101"));
        QVERIFY(siblings->artwork(QStringLiteral("org.example.three")).isEmpty());
    }
    void filtersPagesAndChecksTheRequest()
    {
        AddonHostServices services; auto siblings = mount(services); QVERIFY(siblings);
        auto call = run(services, {{"season", 1}, {"offset", 1}, {"limit", 2}});
        QCOMPARE(call.result.response.value("total").toInt(), 4);
        QCOMPARE(call.result.response.value("currentOffset").toInt(), 0);
        QCOMPARE(call.result.response.value("items").toArray().size(), 2);
        QCOMPARE(call.result.response.value("items").toArray()[0].toObject().value("title").toString(), QStringLiteral("Episode 2"));
        QCOMPARE(run(services, {{"season", 2}}).result.response.value("items").toArray().size(), 1);
        for (const auto& bad : {QJsonObject{{"limit", 0}}, QJsonObject{{"limit", 101}}, QJsonObject{{"offset", -1}},
                                QJsonObject{{"season", 1.5}}, QJsonObject{{"url", "file:///"}},
                                QJsonObject{{"details", QJsonArray{"a", "b", "c", "d", "e"}}}}) {
            const auto result = run(services, bad);
            QVERIFY2(result.done && !result.result.error.isEmpty(), QJsonDocument(bad).toJson().constData());
        }
        // A feed needs the feed source; the reply says nothing about it.
        m_snapshot.kind = QStringLiteral("feed");
        call = run(services, {}, QStringLiteral("org.example.one"), {"folder"});
        QCOMPARE(call.result.response.value("kind").toString(), QStringLiteral("none"));
        QVERIFY(call.result.response.value("items").toArray().isEmpty());
        QVERIFY(!call.result.response.contains("show"));
        // Media without a series has nothing.
        m_snapshot.kind.clear();
        QCOMPARE(run(services, {}).result.response.value("kind").toString(), QStringLiteral("none"));
    }
    void waitsForARunningScan()
    {
        AddonHostServices services; auto siblings = mount(services); QVERIFY(siblings);
        m_snapshot.loading = true;
        const auto call = run(services, {}, QStringLiteral("org.example.one"), {"folder"}, 300);
        QVERIFY(!call.done);
        auto waiting = std::make_shared<Call>();
        services.dispatch(context(QStringLiteral("org.example.one"), {"folder"}), QStringLiteral("media.siblings"), {},
                          [waiting](AddonHostServices::Result result) { waiting->done = true; waiting->result = std::move(result); }, 20000);
        QTest::qWait(100); QVERIFY(!waiting->done);
        siblings->sourceChanged(); QTest::qWait(50); QVERIFY(!waiting->done); // still loading
        m_snapshot.loading = false; siblings->sourceChanged();
        QTRY_VERIFY(waiting->done);
        QCOMPARE(waiting->result.response.value("total").toInt(), 5);
        QVERIFY(!waiting->result.response.value("loading").toBool());
    }
    void effectsNeedIdsFromARead()
    {
        AddonHostServices services; auto siblings = mount(services); QVERIFY(siblings);
        QSignalSpy play(siblings.get(), &Siblings::playRequested), rescan(siblings.get(), &Siblings::rescanRequested),
            identify(siblings.get(), &Siblings::identifyRequested), source(siblings.get(), &Siblings::sourceRequested);
        // A package that read nothing cannot act.
        QVERIFY(!siblings->runEffect(QStringLiteral("org.example.one"), QStringLiteral("rescan"), {}));
        const auto items = run(services, {{"details", QJsonArray{"media:unknown"}}}).result.response.value("items").toArray();
        QVERIFY(m_artwork.isEmpty());
        const auto second = items[1].toObject().value("id").toString(), third = items[2].toObject().value("id").toString();
        QVERIFY(siblings->runEffect(QStringLiteral("org.example.one"), QStringLiteral("play"), {{"id", second}, {"fromStart", true}}));
        QCOMPARE(play.size(), 1);
        QCOMPARE(play.last()[0].toUrl(), m_snapshot.items[1].url);
        QVERIFY(play.last()[3].toBool());
        QVERIFY(!siblings->runEffect(QStringLiteral("org.example.two"), QStringLiteral("play"), {{"id", second}}));
        QVERIFY(!siblings->runEffect(QStringLiteral("org.example.one"), QStringLiteral("play"), {{"id", "media:unknown"}}));
        QVERIFY(!siblings->runEffect(QStringLiteral("org.example.one"), QStringLiteral("play"), {{"id", second}, {"fromStart", "yes"}}));
        QVERIFY(!siblings->runEffect(QStringLiteral("org.example.one"), QStringLiteral("source"), {{"id", second}}));
        QVERIFY(siblings->runEffect(QStringLiteral("org.example.one"), QStringLiteral("source"), {{"id", third}}));
        QCOMPARE(source.last()[0].toUrl(), QUrl(QStringLiteral("https://www.tvmaze.com/episodes/3")));
        QVERIFY(siblings->runEffect(QStringLiteral("org.example.one"), QStringLiteral("rescan"), {{"chooseFolder", true}}));
        QVERIFY(rescan.last()[0].toBool());
        QVERIFY(siblings->runEffect(QStringLiteral("org.example.one"), QStringLiteral("identify"), {}));
        QCOMPARE(identify.size(), 1);
        QVERIFY(!siblings->runEffect(QStringLiteral("org.example.one"), QStringLiteral("delete"), {}));
        // An item that left the series cannot be played.
        m_snapshot.items.removeAt(1);
        QVERIFY(!siblings->runEffect(QStringLiteral("org.example.one"), QStringLiteral("play"), {{"id", second}}));
        // Artwork is made only for the few items asked for, without one.
        run(services, {{"details", QJsonArray{third}}});
        QVERIFY(m_artwork.isEmpty());
        m_snapshot.items[1].artwork.clear();
        run(services, {{"details", QJsonArray{third}}});
        QCOMPARE(m_artwork, QList<QUrl>{m_snapshot.items[1].url});
        // A feed has no folder to choose and no series to identify.
        m_snapshot.kind = QStringLiteral("feed");
        QVERIFY(!siblings->runEffect(QStringLiteral("org.example.one"), QStringLiteral("rescan"), {{"chooseFolder", true}}));
        QVERIFY(siblings->runEffect(QStringLiteral("org.example.one"), QStringLiteral("rescan"), {}));
        QVERIFY(!siblings->runEffect(QStringLiteral("org.example.one"), QStringLiteral("identify"), {}));
    }
};

QTEST_GUILESS_MAIN(TestAddonMediaSiblings)
#include "test_addon_media_siblings.moc"
