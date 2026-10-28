// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QTemporaryDir>
#include "util/media_fingerprint.hpp"
#include "util/subtitle_matching.hpp"
#include "util/media_identity.hpp"
#include "util/track_matching.hpp"
using namespace MediaIdentity;

class TestMediaIdentity : public QObject {
    Q_OBJECT
private slots:
    void sourceLocatorsDoNotSupplyFilenameEvidence() {
        for (const QString scheme : {QStringLiteral("hakone-addon-source"), QStringLiteral("VLC-ADDON-SOURCE")}) {
            const QUrl locator(scheme + QStringLiteral(":PRIVATE_LOCATOR_ONLY_987654321"));
            const auto empty = identify(locator);
            QVERIFY(empty.title.isEmpty()); QVERIFY(empty.release.isEmpty()); QCOMPARE(empty.kind, Kind::Unknown);
            QVERIFY(identify(locator, {{"title", locator.toString()}}).title.isEmpty());
            QVERIFY(identify(locator, {{"title", locator.path()}}).title.isEmpty());
            const auto display = identify(locator, {{"title", "Admitted display title"}, {"type", "movie"}});
            QCOMPARE(display.title, "Admitted display title"); QCOMPARE(display.kind, Kind::Movie);
            QVERIFY(display.release.isEmpty());
        }
        QCOMPARE(identify(QUrl("file:///Example.Movie.2020.mkv")).title, "Example Movie");
    }
    void unknownScopedIdsKeepTransportFactsWithoutStrongAuthority() {
        Identity value; value.kind = Kind::Movie; value.title = "An explicit title";
        value.ids.insert("external:example.catalog:edition", "281");
        value.ids.insert("external:tmdb:season", "281");
        const auto transport = toMap(value).value("externalIds").toList();
        QVERIFY(transport.contains(QVariantMap{{"namespace", "example.catalog"}, {"scope", "edition"}, {"value", "281"}}));
        QVERIFY(transport.contains(QVariantMap{{"namespace", "tmdb"}, {"scope", "season"}, {"value", "281"}}));
        const auto roundtrip = fromMap(toMap(value)); QCOMPARE(roundtrip.ids, value.ids);
        QVERIFY(!compare(value, roundtrip).strong);
    }
    void seriesIdentityKeepsItsScope() {
        Identity series; series.kind = Kind::Series; series.title = "Example";
        series.ids.insert("seriesTmdb", "42");
        const auto roundtrip = fromMap(toMap(series));
        QCOMPARE(roundtrip.kind, Kind::Series); QCOMPARE(roundtrip.ids, series.ids);
        QVERIFY(compare(series, roundtrip).strong);
        auto episode = series; episode.kind = Kind::Episode; episode.season = 1; episode.episodes = {2};
        QVERIFY(compare(series, episode).conflict);
        auto movie = series; movie.kind = Kind::Movie; movie.ids.clear(); movie.ids.insert("tmdbMovie", "42");
        QVERIFY(compare(series, movie).conflict);
        auto unknown = episode; unknown.ids.clear(); unknown.ids.insert("external:example:series", "42");
        auto other = unknown; other.title = "Different";
        QVERIFY(!compare(unknown, other).strong);
        QCOMPARE(parse("Example.2020.mkv").kind, Kind::Movie);
    }
    void episodeNamesExcludeFilenameReaderTitles() {
        const QUrl url("file://server/share/The%20Walking%20Dead%20-%20S01E03%20-%201080p.mkv");
        for (const auto& title : {QString("The Walking Dead - S01E03 - 1080p.mkv"),
                QString("The Walking Dead - S01E03 - 1080p"), QString("The Walking Dead S01E03"),
                QString("The Walking Dead - S01E03"), QString("The Walking Dead"), QString("Episode 3")})
            QVERIFY2(episodeTitle(url, {{"title", title}}).isEmpty(), qPrintable(title));
        QCOMPARE(episodeTitle(url, {{"title", "Tell It to the Frogs"}}), "Tell It to the Frogs");
        QCOMPARE(episodeTitle(url, {{"title", "  Days Gone By  "}, {"showName", "The Walking Dead"}}), "Days Gone By");
        QCOMPARE(episodeTitle(url, {{"episodeTitle", "A <literal> & name"}}), "A <literal> & name");
    }
    void numericEpisodeNamesAndGeneratedTitles() {
        const QUrl plain("file://server/share/The%20Walking%20Dead%20-%20S02E10.mkv");
        for (const auto& title : {QString("The Walking Dead 10"), QString("The Walking Dead - 010"),
                QString("The Walking Dead Episode 10"), QString("Episode 010"), QString("10")})
            QVERIFY2(episodeTitle(plain, {{"title", title}}).isEmpty(), qPrintable(title));
        QCOMPARE(episodeTitle(plain, {{"title", "18 Days Out"}}), "18 Days Out");
        QCOMPARE(episodeTitle(plain, {{"title", "1984"}}), "1984");
        const QUrl named("file://server/share/The%20Walking%20Dead%20-%20S02E10%20-%2018%20Days%20Out.mkv");
        QCOMPARE(episodeTitle(named, {}), "18 Days Out");
        QCOMPARE(episodeTitle(named, {{"title", named.fileName()}}), "18 Days Out");
        QCOMPARE(episodeTitle(named, {{"episodeTitle", ""}, {"title", "Embedded title"}}), "Embedded title");
        QCOMPARE(episodeTitle(named, {{"episodeTitle", "Better title"}}), "Better title");
        QVERIFY(episodeTitle(named, {{"episodeTitle", "Episode 10"}}, false).isEmpty());
        QCOMPARE(episodeTitle(QUrl("file:///Show.S02E10.18.Miles.Out.1080p.WEB-DL.x264.mkv"), {}), "18 Miles Out");
        QVERIFY(episodeTitle(QUrl("file:///Show.S02E10.1080p.WEB-DL.x264.mkv"), {}).isEmpty());
    }
    void filenames_data() {
        QTest::addColumn<QString>("filename"); QTest::addColumn<QString>("title");
        QTest::addColumn<int>("year"); QTest::addColumn<int>("season");
        QTest::addColumn<QList<int>>("episodes");
        QTest::newRow("movie") << "Dune.2021.1080p.BluRay.x264-GROUP.mkv" << "Dune" << 2021 << -1 << QList<int>{};
        QTest::newRow("remake") << "Dune (1984).mkv" << "Dune" << 1984 << -1 << QList<int>{};
        QTest::newRow("numeric-title") << "1917.2019.1080p.WEB-DL.mkv" << "1917" << 2019 << -1 << QList<int>{};
        QTest::newRow("numeric-prefix") << "2001.A.Space.Odyssey.1968.mkv" << "2001 A Space Odyssey" << 1968 << -1 << QList<int>{};
        QTest::newRow("year-as-title") << "1984.mkv" << "1984" << -1 << -1 << QList<int>{};
        QTest::newRow("punctuation") << "Spider-Man.No.Way.Home.2021.mkv" << "Spider-Man No Way Home" << 2021 << -1 << QList<int>{};
        QTest::newRow("part-of-title") << "Dune.Part.Two.2024.mkv" << "Dune Part Two" << 2024 << -1 << QList<int>{};
        QTest::newRow("part-one") << "Movie.Part.1.2024.mkv" << "Movie Part 1" << 2024 << -1 << QList<int>{};
        QTest::newRow("bracket-title") << "[REC].2007.mkv" << "[REC]" << 2007 << -1 << QList<int>{};
        QTest::newRow("unicode") << QString::fromUtf8("Le.Fabuleux.Destin.d’Amélie.Poulain.2001.mkv") << QString::fromUtf8("Le Fabuleux Destin d’Amélie Poulain") << 2001 << -1 << QList<int>{};
        QTest::newRow("fansub-seasonal") << "[Fansub] The.Show.S02E03.1080p.mkv" << "The Show" << -1 << 2 << QList<int>{3};
        QTest::newRow("episode") << "The.Show.S02E03.1080p.WEB-DL-GROUP.mkv" << "The Show" << -1 << 2 << QList<int>{3};
        QTest::newRow("series-year") << "The.Show.2005.S02E03.mkv" << "The Show" << 2005 << 2 << QList<int>{3};
        QTest::newRow("x-episode") << "The_Show.2x03.en.srt" << "The Show" << -1 << 2 << QList<int>{3};
        QTest::newRow("long-episode") << "The Show Season 2 Episode 3.mkv" << "The Show" << -1 << 2 << QList<int>{3};
        QTest::newRow("multi") << "The.Show.S02E03E04.mkv" << "The Show" << -1 << 2 << QList<int>{3,4};
        QTest::newRow("numbered-title") << "The Show S02E10 - 18 Days Out.mkv" << "The Show" << -1 << 2 << QList<int>{10};
        QTest::newRow("numbered-title-compact-dash") << "The Show S02E10 -18 Days Out.mkv" << "The Show" << -1 << 2 << QList<int>{10};
        QTest::newRow("bare-range") << "The.Show.S02E10-12.mkv" << "The Show" << -1 << 2 << QList<int>{10,11,12};
        QTest::newRow("spaced-markers") << "The Show S02E10 - E 12.mkv" << "The Show" << -1 << 2 << QList<int>{10,11,12};
        QTest::newRow("explicit-spaced-range") << "The Show S02E10 - E12.mkv" << "The Show" << -1 << 2 << QList<int>{10,11,12};
        QTest::newRow("range") << "The.Show.S02E03-E05.mkv" << "The Show" << -1 << 2 << QList<int>{3,4,5};
        QTest::newRow("multi-x") << "The.Show.2x03x04.mkv" << "The Show" << -1 << 2 << QList<int>{3,4};
        QTest::newRow("special") << "The.Show.S00E01.mkv" << "The Show" << -1 << 0 << QList<int>{1};
        QTest::newRow("season-pack") << "The.Show.S02.1080p.mkv" << "The Show" << -1 << 2 << QList<int>{};
        QTest::newRow("episode-only") << "S02E03.mkv" << "" << -1 << 2 << QList<int>{3};
        QTest::newRow("unknown") << "video.mp4" << "" << -1 << -1 << QList<int>{};
        QTest::newRow("release-name-without-extension") << "The.Matrix.1999" << "The Matrix" << 1999 << -1 << QList<int>{};
    }
    void filenames() {
        QFETCH(QString,filename); QFETCH(QString,title); QFETCH(int,year); QFETCH(int,season); QFETCH(QList<int>,episodes);
        const auto value = parse(filename);
        QCOMPARE(value.title,title); QCOMPARE(value.year,year); QCOMPARE(value.season,season); QCOMPARE(value.episodes,episodes);
        const auto roundtrip = fromMap(toMap(value));
        QCOMPARE(roundtrip.title,value.title); QCOMPARE(roundtrip.episodes,value.episodes); QCOMPARE(roundtrip.kind,value.kind);
    }
    void releaseEvidence() {
        const auto value = parse("Dune.2021.Extended.1080p.WEB-DL.x265.FLAC-GROUP.mkv");
        QCOMPARE(value.title,"Dune"); QCOMPARE(value.release.value("source"),"webdl");
        QCOMPARE(value.release.value("resolution"),"1080p"); QCOMPARE(value.release.value("videoCodec"),"x265");
        QCOMPARE(value.release.value("audioCodec"),"flac"); QCOMPARE(value.release.value("group"),"group");
        QCOMPARE(value.release.value("edition"),"extended");
    }
    void datesAndAbsoluteNumbers() {
        const auto daily = parse("The.Daily.Show.2024.03.15.1080p.mkv");
        QCOMPARE(daily.title,"The Daily Show"); QCOMPARE(daily.airDate,QDate(2024,3,15)); QCOMPARE(daily.year,-1);
        const auto anime = parse("[Fansub] Anime Title - 452v2 [1080p][ABC12345].mkv");
        QCOMPARE(anime.title,"Anime Title"); QCOMPARE(anime.absoluteEpisode,"452"); QCOMPARE(anime.season,-1);
        QCOMPARE(anime.release.value("group"),"Fansub"); QVERIFY(anime.episodes.isEmpty());
        const auto fractional = parse("[Fansub] Anime Title - 12.5 [720p].mkv");
        QCOMPARE(fractional.absoluteEpisode,"12.5");
        auto other = anime; other.absoluteEpisode="453"; QVERIFY(compare(anime,other).conflict);
        auto seasonal = parse("Anime Title S01E452.mkv"); QVERIFY(!compare(anime,seasonal).strong);
    }
    void explicitIds() {
        const auto movie = parse("Dune (2021) [imdbid-tt1160419] [tmdbid-438631].mkv");
        QCOMPARE(movie.ids.value("imdb"),"tt1160419"); QCOMPARE(movie.ids.value("tmdbMovie"),"438631");
        const auto series = parse("Show.S01E01.[tmdb-438631].mkv");
        const auto taggedSeries = parse("Show.[imdbid-tt1234567].S01E01.mkv");
        QCOMPARE(taggedSeries.ids.value("seriesImdb"),"tt1234567"); QVERIFY(!taggedSeries.ids.contains("imdb"));
        QCOMPARE(series.ids.value("seriesTmdb"),"438631"); QVERIFY(!series.ids.contains("tmdbMovie"));
    }
    void foldersAndMetadata() {
        const auto movie = identify(QUrl("file:///movies/Dune%20(2021)/video.mp4"));
        QCOMPARE(movie.title,"Dune"); QCOMPARE(movie.year,2021); QCOMPARE(movie.provenance.value("title"),"folder");
        const auto episode = identify(QUrl("file:///shows/Show/Season%202/S02E03.mkv"));
        QCOMPARE(episode.title,"Show"); QCOMPARE(episode.season,2); QCOMPARE(episode.episodes,QList<int>{3});
        const auto corrected = identify(QUrl("file:///Dune.1984.mkv"),{{"title","Dune"},{"year",2021},{"imdbId","tt1160419"}});
        QCOMPARE(corrected.year,2021); QCOMPARE(corrected.provenance.value("year"),"metadata");
        QVERIFY(corrected.conflicts.contains("year")); QCOMPARE(corrected.ids.value("imdb"),"tt1160419");
    }
    void comparisonProtectsIdentity() {
        QVERIFY(compare(parse("Dune.2021.mkv"),parse("Dune.2021.720p.srt")).strong);
        QVERIFY(compare(parse("Dune.2021.mkv"),parse("Dune.1984.srt")).conflict);
        QVERIFY(!compare(parse("Up.mkv"),parse("Up.in.the.Air.srt")).strong);
        QVERIFY(!compare(parse("Movie.Part.1.2024.mkv"),parse("Movie.Part.2.2024.srt")).strong);
        QVERIFY(compare(parse("Show.S01E03.mkv"),parse("Show.S01E04.srt")).conflict);
        QVERIFY(compare(parse("Show.S01E03E04.mkv"),parse("Show.S01E03.srt")).conflict);
        QVERIFY(!compare(parse("Show.S01E03.mkv"),parse("Show.S01.srt")).strong);
        QVERIFY(!compare(parse("Show.S01E03.mkv"),parse("Other.Show.S01E03.srt")).strong);
        QVERIFY(!compare(parse("Show.2005.S01E03.mkv"),parse("Show.S01E03.srt")).strong);
        const auto tagged = identify(QUrl("file:///Show.S01E03.mkv"),{{"title","Episode Title"},{"episode",3}});
        QCOMPARE(tagged.title,"Show");
        QVERIFY(similarity("The Matrx","The Matrix")>.86);
        QVERIFY(!compare(parse("The.Matrx.1999.mkv"),parse("The.Matrix.1999.srt")).strong);
    }
    void subtitleRankingAndAmbiguity() {
        const auto target=parse("Dune.2021.1080p.WEB-DL-GROUP.mkv");
        const QVariantMap web{{"url","opensubtitles:/1"},{"release","Dune.2021.720p.WEB-DL-GROUP"},{"language","en"}};
        const QVariantMap bluray{{"url","opensubtitles:/2"},{"release","Dune.2021.1080p.BluRay-OTHER"},{"language","en"}};
        const QVariantMap wrong{{"url","opensubtitles:/3"},{"release","Dune.1984.1080p.WEB-DL-GROUP"},{"language","en"}};
        const auto ranked=SubtitleMatching::rank(target,{wrong,bluray,web},"file:///Dune.mkv","en");
        QCOMPARE(ranked[0].toMap().value("url").toString(),"opensubtitles:/1");
        QCOMPARE(ranked.last().toMap().value("matchScore").toInt(),-1);
        const QVariantList media{QVariantMap{{"url","file:///Dune.mkv"},{"identity",toMap(target)}}};
        QCOMPARE(SubtitleMatching::match(media,{bluray,web},"en").value("file:///Dune.mkv").toStringList(),QStringList{"opensubtitles:/1"});
        auto duplicate=web;duplicate.insert("url","opensubtitles:/4");
        QVERIFY(SubtitleMatching::match(media,{web,duplicate},"en").isEmpty());
        const QVariantList ambiguous{media[0],QVariantMap{{"url","file:///other/Dune.mkv"},{"identity",toMap(target)}}};
        QVERIFY(SubtitleMatching::match(ambiguous,{web},"en").isEmpty());
        auto forced=web;forced.insert("forced",true);
        QVERIFY(SubtitleMatching::match(media,{forced},"en").isEmpty());
        QVERIFY(SubtitleMatching::match(media,{web},"fr").isEmpty());
        auto cut=web;cut.insert("release","Dune.2021.Extended.720p.WEB-DL-GROUP");
        auto theatrical=target;theatrical.release.insert("edition","theatrical");
        QVERIFY(!SubtitleMatching::rank(theatrical,{cut}).first().toMap().value("automaticMatch").toBool());
    }
    void providerIdentityAndScopedHash() {
        const QVariantMap source{{"url","opensubtitles:/1"},{"release","unhelpful"},{"language","en"},
            {"feature",QVariantMap{{"feature_type","Episode"},{"parent_title","Show"},{"season_number",1},
                {"episode_number",3},{"year",2024},{"parent_imdb_id",1234567},{"imdb_id",7654321}}}};
        const auto candidate=SubtitleMatching::identity(source);
        QCOMPARE(candidate.title,"Show");QCOMPARE(candidate.year,-1);QCOMPARE(candidate.ids.value("seriesImdb"),"tt1234567");
        auto target=parse("Show.S01E03.mkv");
        QVERIFY(SubtitleMatching::rank(target,{source}).first().toMap().value("automaticMatch").toBool());
        target.episodes={4};QCOMPARE(SubtitleMatching::rank(target,{source}).first().toMap().value("matchScore").toInt(),-1);
        auto hash=source;hash.insert("hashFor",QStringList{"file:///one.mkv"});
        const Identity unknown;
        QVERIFY(SubtitleMatching::rank(unknown,{hash},"file:///one.mkv").first().toMap().value("automaticMatch").toBool());
        QVERIFY(!SubtitleMatching::rank(unknown,{hash},"file:///two.mkv").first().toMap().value("automaticMatch").toBool());
    }
    void fileFingerprint() {
        QTemporaryDir directory;
        QFile file(directory.filePath("video.mkv")); QVERIFY(file.open(QIODevice::WriteOnly));
        QByteArray bytes(3 * 65536, '\0'); bytes[0] = 1; bytes[bytes.size() - 1] = char(0x80);
        QCOMPARE(file.write(bytes),qint64(bytes.size())); file.close();
        const auto url = QUrl::fromLocalFile(file.fileName());
        const auto hash = MediaFingerprint::openSubtitles(url);
        QCOMPARE(hash.hash,"8000000000030001"); QCOMPARE(hash.size,qint64(bytes.size()));
        QVERIFY(MediaFingerprint::openSubtitles(url,[] { return true; }).hash.isEmpty());
        QVERIFY(MediaFingerprint::openSubtitles(QUrl("https://example.com/video.mkv")).hash.isEmpty());
        QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.resize(1024)); file.close();
        QVERIFY(MediaFingerprint::openSubtitles(url).hash.isEmpty());
    }
    void audioIdentityAndConflicts() {
        const auto song = identify(QUrl("file:///music/Album/Disc%202/03%20-%20Artist%20-%20Song.flac"));
        QCOMPARE(song.kind, Kind::MusicTrack); QCOMPARE(song.title, "Song"); QCOMPARE(song.artist, "Artist");
        QCOMPARE(song.album, "Album"); QCOMPARE(song.disc, 2); QCOMPARE(song.track, 3);
        QCOMPARE(parse("1984.flac").title, "1984");
        const auto tags = identify(QUrl("file:///music/song.flac"), {{"title", "Correct title"}, {"artist", "Singer"},
            {"album", "Record"}, {"albumArtist", "Various Artists"}, {"track", "2/12"}, {"disc", 1}});
        QCOMPARE(tags.title, "Correct title"); QCOMPARE(tags.track, 2);
        QCOMPARE(fromMap(toMap(tags)).albumArtist, "Various Artists");
        QVERIFY(compare(tags, fromMap(toMap(tags))).strong);
        auto other = tags; other.artist = "Someone else";
        QVERIFY(compare(tags, other).conflict); QVERIFY(!compare(tags, other).strong);
        other = tags; other.album = "Different album"; QVERIFY(compare(tags, other).conflict);
        QVERIFY(!compare(parse("Song.flac"), parse("Song.flac")).strong);
        QCOMPARE(parse("Book.m4b").kind, Kind::Audiobook);
        for (auto kind : {Kind::Album, Kind::Artist, Kind::Podcast, Kind::PodcastEpisode, Kind::Audiobook}) {
            Identity value; value.kind = kind; value.title = "Title";
            QCOMPARE(fromMap(toMap(value)).kind, kind);
        }
    }
    void podcastIdentity() {
        const auto episode = identify(QUrl("file:///podcasts/Science.2026.10.23.mp3"),
            {{"type", "podcastEpisode"}, {"title", "A conversation"}, {"series", "Science"},
             {"guid", "episode-42"}, {"feedUrl", "https://example.com/feed"}, {"airDate", "2026-10-23"}});
        QCOMPARE(episode.kind, Kind::PodcastEpisode); QCOMPARE(episode.series, "Science");
        QCOMPARE(episode.title, "A conversation"); QCOMPARE(episode.airDate, QDate(2026,10,23));
        auto other = fromMap(toMap(episode));
        QVERIFY(compare(episode, other).strong);
        other.guid = "episode-43"; QVERIFY(compare(episode, other).conflict);
        other = episode; other.series = "Other podcast"; QVERIFY(compare(episode, other).conflict);
        auto track = episode; track.kind = Kind::MusicTrack; QVERIFY(compare(episode, track).conflict);
    }
    void localMatchingKeepsAmbiguityGuards() {
        const QUrl film("file:///Dune.2021.1080p.WEB-DL.mkv"), subtitle("file:///Dune.2021.720p.BluRay.srt");
        QCOMPARE(TrackMatching::match({film},{subtitle}).value(film),QList<QUrl>{subtitle});
        QVERIFY(TrackMatching::match({film,QUrl("file:///other/Dune.2021.mkv")},{subtitle}).isEmpty());
        QVERIFY(TrackMatching::match({film},{QUrl("file:///Dune.1984.srt")}).isEmpty());
        QVERIFY(TrackMatching::match({QUrl("file:///Dune.2021.Theatrical.mkv")},
            {QUrl("file:///Dune.2021.Extended.srt")}).isEmpty());
        const QUrl show("file:///Show.S01E01E02.mkv");
        QVERIFY(TrackMatching::match({show},{QUrl("file:///Show.S01E01.srt")}).isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestMediaIdentity)
#include "test_media_identity.moc"
