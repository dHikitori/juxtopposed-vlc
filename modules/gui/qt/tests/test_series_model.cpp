// SPDX-License-Identifier: GPL-2.0-or-later
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include "player/series_model.hpp"

class TestSeriesModel : public QObject
{
    Q_OBJECT
    static QVariantMap episode(int season, int number, const QString& guid, const QString& url, int sourceIndex) {
        return {{"title", "A title"}, {"mrl", url}, {"sourceIndex", sourceIndex}, {"progress", 0.25},
            {"podcast", QVariantMap{{"feed", "https://example.invalid/feed"}, {"guid", guid},
                {"season", season < 0 ? "" : QString::number(season)}, {"episode", number < 0 ? "" : QString::number(number)},
                {"description", "Episode notes"}, {"date", "2025-01-01T00:00:00Z"}}}};
    }
    static QVariantMap podcast() {
        return {{"type", "podcastEpisode"}, {"feedUrl", "https://example.invalid/feed"},
            {"series", "Podcast"}, {"guid", "playing"}, {"season", 3}, {"episode", 2}};
    }
    // The sibling for one file or feed entry; an empty url when there is none.
    static SeriesModel::Sibling sibling(const SeriesModel& model, const QUrl& url) {
        for (const auto& item : model.siblings()) if (item.url == url) return item;
        return {};
    }
    static QList<QUrl> urls(const SeriesModel& model) {
        QList<QUrl> result;
        for (const auto& item : model.siblings()) result.append(item.url);
        return result;
    }
private slots:
    void feedSeasonsGuidIdentityAndOrdering() {
        SeriesModel model;
        model.setCurrent(QUrl("https://example.invalid/old-token.mp3"), podcast());
        QVERIFY(model.available()); QCOMPARE(model.showTitle(), "Podcast");
        QVariantList episodes{episode(3, 10, "last", "https://example.invalid/10.mp3", 0),
            episode(3, 2, "playing", "https://example.invalid/new-token.mp3", 1),
            episode(0, 1, "special", "https://example.invalid/special.mp3", 2),
            episode(-1, -1, "unnumbered", "https://example.invalid/extra.mp3", 3),
            episode(3, 2, "playing", "https://example.invalid/duplicate.mp3", 4)};
        QSignalSpy changed(&model, &SeriesModel::siblingsChanged);
        model.setFeedEpisodes(model.feed(), episodes);
        QCOMPARE(changed.count(), 1);
        // Every season in one list, by season, then episode; the guid keeps one entry.
        QCOMPARE(urls(model), (QList<QUrl>{QUrl("https://example.invalid/extra.mp3"), QUrl("https://example.invalid/special.mp3"),
            QUrl("https://example.invalid/new-token.mp3"), QUrl("https://example.invalid/10.mp3")}));
        const auto items = model.siblings();
        QCOMPARE(items[0].identity.season, -1); QVERIFY(items[0].identity.episodes.isEmpty());
        QCOMPARE(items[1].identity.season, 0); QCOMPARE(items[3].identity.episodes, QList<int>{10});
        // The guid finds the playing entry although its address changed.
        const auto playing = items[2];
        QVERIFY(playing.current); QCOMPARE(playing.feedIndex, 1); QCOMPARE(playing.version, 0);
        QCOMPARE(playing.identity.season, 3); QCOMPARE(playing.identity.episodes, QList<int>{2});
        QCOMPARE(playing.title, "A title"); QCOMPARE(playing.description, "Episode notes");
        QCOMPARE(playing.published, QDateTime(QDate(2025, 1, 1), QTime(0, 0), QTimeZone::UTC));
        QCOMPARE(playing.progress, .25);
        for (const auto& item : items) if (item.url != playing.url) QVERIFY(!item.current);
        changed.clear();
        model.setProgress(QUrl("https://example.invalid/old-token.mp3"), .6);
        QCOMPARE(sibling(model, playing.url).progress, .6);
        QCOMPARE(changed.count(), 0); // The playing progress is not a sibling change.
        auto meta = podcast(); meta["description"] = "New notes";
        model.setCurrent(QUrl("https://example.invalid/old-token.mp3"), meta);
        // A metadata refresh keeps the feed entries and updates the playing one.
        QCOMPARE(model.siblings().size(), 4);
        QCOMPARE(sibling(model, playing.url).description, "New notes");
        QVERIFY(sibling(model, playing.url).current);
    }
    void unrelatedFeedAndOldScanCannotReplaceCurrentContext() {
        QTemporaryDir base;
        const auto local = QUrl::fromLocalFile(base.path() + "/Show.S01E01.mkv");
        QFile file(local.toLocalFile()); QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
        SeriesModel model; model.setCurrent(local, {});
        model.setCurrent(QUrl("https://example.invalid/current.mp3"), podcast());
        const QVariantList episodes{episode(3, 2, "playing", "https://example.invalid/current.mp3", 0)};
        model.setFeedEpisodes(QUrl("https://example.invalid/other-feed"), episodes);
        QVERIFY(model.siblings().isEmpty());
        model.setFeedEpisodes(model.feed(), episodes);
        QTest::qWait(100);
        QCOMPARE(urls(model), QList<QUrl>{QUrl("https://example.invalid/current.mp3")});
        QCOMPARE(model.showTitle(), "Podcast");
        model.setCurrent(QUrl("https://example.invalid/movie.mp4"), {});
        QVERIFY(!model.available()); QVERIFY(model.siblings().isEmpty());
    }
    void localProgressVersionsAndSnapshotReuse() {
        QTemporaryDir base;
        QList<QUrl> urls;
        for (const auto* name : {"Show.S01E01.mkv", "Show.S01E01.1080p.mkv", "Show.S02E01.mkv"}) {
            const auto path = base.path() + '/' + name;
            QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
            urls.append(QUrl::fromLocalFile(path));
        }
        SeriesModel model;
        model.setCurrent(urls[0], {});
        QTRY_VERIFY(!model.loading());
        // Both versions of the first episode and the next season.
        QCOMPARE(model.siblings().size(), 3);
        const auto first = sibling(model, urls[0]), version = sibling(model, urls[1]), next = sibling(model, urls[2]);
        QCOMPARE(first.identity.season, 1); QCOMPARE(version.identity.season, 1); QCOMPARE(next.identity.season, 2);
        QCOMPARE(first.identity.episodes, version.identity.episodes);
        QCOMPARE(QSet<int>({first.version, version.version}), QSet<int>({0, 1})); QCOMPARE(next.version, 0);
        QCOMPARE(first.feedIndex, -1);
        QVERIFY(first.current); QVERIFY(!version.current); QVERIFY(!next.current);
        QSignalSpy changed(&model, &SeriesModel::siblingsChanged);
        model.setProgress(urls[0], .4); QCOMPARE(sibling(model, urls[0]).progress, .4);
        QCOMPARE(changed.count(), 0);
        model.setPlayed(urls[0], true); QVERIFY(sibling(model, urls[0]).played);
        QCOMPARE(changed.count(), 1);
        model.setProgress(QUrl("file:///other.mkv"), .8);
        QCOMPARE(sibling(model, urls[0]).progress, .4);
        QVERIFY(sibling(model, QUrl("file:///other.mkv")).url.isEmpty());
        // Artwork is asked for once per file, and only for files of the series.
        QSignalSpy artwork(&model, &SeriesModel::artworkRequested);
        model.setArtwork(urls[1], "file:///unrequested.png");
        QVERIFY(sibling(model, urls[1]).artwork.isEmpty());
        model.requestArtwork(urls[1]); model.requestArtwork(urls[1]); model.requestArtwork(QUrl("file:///other.mkv"));
        QCOMPARE(artwork.count(), 1); QCOMPARE(artwork.first().first().toUrl(), urls[1]);
        model.setArtwork(urls[1], "file:///poster.png");
        QCOMPARE(sibling(model, urls[1]).artwork, "file:///poster.png");
        // Moving to a known sibling reuses the snapshot and its states.
        model.setCurrent(urls[2], {});
        QVERIFY(!model.loading()); QCOMPARE(model.siblings().size(), 3);
        QVERIFY(sibling(model, urls[2]).current); QVERIFY(!sibling(model, urls[0]).current);
        QCOMPARE(sibling(model, urls[0]).progress, .4); QVERIFY(sibling(model, urls[0]).played);
        QCOMPARE(sibling(model, urls[1]).artwork, "file:///poster.png");
    }
    void refreshRetriesPartialScansWithTheLargerScope() {
        QTemporaryDir base;
        const auto current = QUrl::fromLocalFile(base.path() + "/Show.S01E01.mkv");
        const auto deep = base.path() + "/a/b/c/d/e/Show.S01E02.mkv";
        QVERIFY(QDir().mkpath(QFileInfo(deep).absolutePath()));
        for (const auto& path : {current.toLocalFile(), deep}) {
            QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly));
        }
        SeriesModel model; model.setCurrent(current, {});
        QTRY_VERIFY(!model.loading()); QVERIFY(model.limited()); QCOMPARE(urls(model), QList<QUrl>{current});
        model.refresh();
        QTRY_VERIFY(!model.loading()); QVERIFY(!model.limited());
        QCOMPARE(urls(model), (QList<QUrl>{current, QUrl::fromLocalFile(deep)}));
        model.refresh();
        QTRY_VERIFY(!model.loading()); QCOMPARE(model.siblings().size(), 2);
    }
    void realTitlesSurviveNavigationAndScans() {
        QTemporaryDir base;
        const auto first = QUrl::fromLocalFile(base.path() + "/Show.S01E01.mkv");
        const auto second = QUrl::fromLocalFile(base.path() + "/Show.S01E02.mkv");
        for (const auto& url : {first, second}) { QFile file(url.toLocalFile()); QVERIFY(file.open(QIODevice::WriteOnly)); }
        SeriesModel model; model.setCurrent(first, {{"title", "Show S01E01"}});
        QTRY_VERIFY(!model.loading()); QCOMPARE(urls(model), (QList<QUrl>{first, second}));
        // A title that only repeats the file name is no title.
        QVERIFY(sibling(model, first).title.isEmpty());
        const QUrl source("https://www.tvmaze.com/episodes/1/show-1x01-pilot");
        model.setEpisodeTitle(first, "Pilot", source);
        QCOMPARE(sibling(model, first).title, "Pilot");
        QCOMPARE(sibling(model, first).titleSource, source);
        model.setEpisodeTitle(second, "Second <line> & title");
        QCOMPARE(sibling(model, second).title, "Second <line> & title");
        model.setCurrent(second, {{"episodeTitle", "Show S01E02"}});
        QCOMPARE(sibling(model, second).title, "Second <line> & title");
        model.refresh(); QTRY_VERIFY(!model.loading());
        QCOMPARE(sibling(model, first).title, "Pilot");
        QCOMPARE(sibling(model, second).title, "Second <line> & title");
        model.setCurrent(first, {{"episodeTitle", "File title"}});
        QCOMPARE(sibling(model, first).title, "File title");
        QVERIFY(sibling(model, first).titleSource.isEmpty());
        model.setEpisodeTitle(first, "Show S01E01");
        QCOMPARE(sibling(model, first).title, "File title");
        model.setEpisodeTitle(QUrl("file:///unrelated.mkv"), "Unrelated");
        QCOMPARE(model.siblings().size(), 2);
    }
    void siblingFilenameTitlesAndNumberedNames() {
        QTemporaryDir base;
        const auto first = QUrl::fromLocalFile(base.path() + "/Show S02E09.mkv");
        const auto second = QUrl::fromLocalFile(base.path() + "/Show S02E10 - 18 Days Out.mkv");
        for (const auto& url : {first, second}) { QFile file(url.toLocalFile()); QVERIFY(file.open(QIODevice::WriteOnly)); }
        SeriesModel model; model.setCurrent(first, {});
        QTRY_VERIFY(!model.loading()); QCOMPARE(urls(model), (QList<QUrl>{first, second}));
        QCOMPARE(sibling(model, second).identity.season, 2);
        QCOMPARE(sibling(model, second).identity.episodes, QList<int>{10});
        QCOMPARE(sibling(model, second).title, "18 Days Out");
        model.setEpisodeTitle(second, "Online name", QUrl("https://www.tvmaze.com/episodes/1"));
        QCOMPARE(sibling(model, second).title, "18 Days Out");
        model.setCurrent(second, {{"episodeTitle", "Embedded name"}});
        QCOMPARE(sibling(model, second).title, "Embedded name");
        model.refresh(); QTRY_VERIFY(!model.loading());
        QCOMPARE(sibling(model, second).title, "Embedded name");
    }
    // The player writes ( and ) percent-encoded; the folder scan does not.
    // Both forms are one file.
    void playerUrlsMatchScannedFilesWhateverTheirEncoding() {
        QTemporaryDir base;
        QVERIFY(QDir().mkpath(base.path() + "/Show (2006)/Season 01"));
        const auto first = QUrl::fromLocalFile(base.path() + "/Show (2006)/Season 01/Show (2006) S01E01.mkv");
        const auto second = QUrl::fromLocalFile(base.path() + "/Show (2006)/Season 01/Show (2006) S01E02.mkv");
        for (const auto& url : {first, second}) { QFile file(url.toLocalFile()); QVERIFY(file.open(QIODevice::WriteOnly)); }
        const QUrl player(first.toString(QUrl::FullyEncoded).replace("(", "%28").replace(")", "%29"), QUrl::StrictMode);
        QVERIFY(player.toString(QUrl::FullyEncoded).contains("%28"));
        QVERIFY(player != first); // QUrl compares the two forms as different addresses.
        SeriesModel model; model.setCurrent(player, {});
        QTRY_VERIFY(!model.loading());
        QCOMPARE(urls(model), (QList<QUrl>{first, second}));
        QVERIFY(sibling(model, first).current); QCOMPARE(sibling(model, first).version, 0);
        model.setProgress(player, .4); QCOMPARE(sibling(model, first).progress, .4);
        model.setEpisodeTitle(player, "Pilot"); QCOMPARE(sibling(model, first).title, "Pilot");
        // The same file again, in the scan's form, is the same media.
        model.setCurrent(first, {});
        QCOMPARE(urls(model), (QList<QUrl>{first, second})); QVERIFY(sibling(model, first).current);
    }
    void destructionDuringScanIsSafe() {
        QTemporaryDir base;
        const auto url = QUrl::fromLocalFile(base.path() + "/Show.S01E01.mkv");
        auto* model = new SeriesModel; model->setCurrent(url, {}); delete model;
        QTest::qWait(50);
    }
};
QTEST_GUILESS_MAIN(TestSeriesModel)
#include "test_series_model.moc"
