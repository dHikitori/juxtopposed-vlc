// SPDX-License-Identifier: GPL-2.0-or-later
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QDir>
#include "addons/addon_metadata_service.hpp"
#include "player/episode_metadata.hpp"
#include "util/episode_title_cache.hpp"

namespace {
struct Fake {
    struct Call { QString id; QVariantMap context; };
    QList<Call> calls; QStringList canceled;
    AddonMetadataService::Backend backend()
    {
        AddonMetadataService::Backend result;
        result.providers = {
            [](const QString&) { return QVariantList{QVariantMap{{"id", "test/episodes"}, {"enabled", true}, {"configured", true},
                {"mediaKinds", QStringList{"episode"}}, {"cachePartition", "release-1:account-1:settings-1"}}}; },
            [this](const QString&, const QString&, const QVariantMap& context) {
                const auto id = QStringLiteral("request-%1").arg(calls.size() + 1); calls.append({id, context}); return id;
            }, [this](const QString& id) { canceled.append(id); }};
        return result;
    }
    void complete(AddonMetadataService& owner, int index, const QString& title, const QString& source = QStringLiteral("https://www.tvmaze.com/episodes/1"))
    {
        const auto call = calls[index];
        owner.operationFinished(call.id, QStringLiteral("test/episodes"), QStringLiteral("match"), {
            {QStringLiteral("identity"), call.context.value(QStringLiteral("identity"))},
            {QStringLiteral("fields"), QVariantMap{{QStringLiteral("episodeTitle"), title}}},
            {QStringLiteral("attribution"), QVariantMap{{QStringLiteral("name"), QStringLiteral("TVmaze")},
                {QStringLiteral("url"), source}, {QStringLiteral("license"), QStringLiteral("CC-BY-SA-4.0")}}}}, {});
    }
};
QUrl url(const QTemporaryDir& directory, const QString& relative)
{ return QUrl::fromLocalFile(directory.filePath(relative)); }
}

class TestAddonEpisodeMetadata : public QObject
{
    Q_OBJECT
private slots:
    void allRequestedEpisodesKeepPriorityVersionsSeasonsAndMemoryCache()
    {
        QTemporaryDir directory; Fake fake; AddonMetadataService owner(fake.backend()); owner.setAutomaticNetworkAllowed(true);
        EpisodeMetadata episode; episode.setMetadataService(&owner); QSignalSpy titles(&episode, &EpisodeMetadata::titleReady);
        const auto first = url(directory, "Example.Series.2020/Season 1/Example.Series.S01E01.mkv");
        const auto duplicate = url(directory, "Example.Series.2020/Season 1/Example.Series.S01E01.1080p.mkv");
        const auto current = url(directory, "Example.Series.2020/Season 1/Example.Series.S01E02.mkv");
        const auto nextSeason = url(directory, "Example.Series.2020/Season 2/Example.Series.S02E01.mkv");
        episode.request(current, {}); episode.readCached({first, duplicate, current, nextSeason});
        QTRY_COMPARE(fake.calls.size(), 1);
        QCOMPARE(fake.calls[0].context.value("identity").toMap().value("episodes").toList(), QVariantList{2});
        fake.complete(owner, 0, "Second episode"); QTRY_COMPARE(fake.calls.size(), 2);
        QCOMPARE(fake.calls[1].context.value("identity").toMap().value("season").toInt(), 1);
        QCOMPARE(fake.calls[1].context.value("identity").toMap().value("episodes").toList(), QVariantList{1});
        fake.complete(owner, 1, "First episode"); QTRY_COMPARE(fake.calls.size(), 3);
        QCOMPARE(fake.calls[2].context.value("identity").toMap().value("season").toInt(), 2);
        fake.complete(owner, 2, "New season"); QTRY_COMPARE(titles.size(), 4);
        QHash<QUrl, QString> actual;
        for (const auto& title : titles) { actual.insert(title[0].toUrl(), title[1].toString()); QCOMPARE(title[2].toUrl().host(), "www.tvmaze.com"); }
        QCOMPARE(actual.value(first), "First episode"); QCOMPARE(actual.value(duplicate), "First episode");
        QCOMPARE(actual.value(current), "Second episode"); QCOMPARE(actual.value(nextSeason), "New season");
        const auto newVersion = url(directory, "Example.Series.2020/Season 1/Example.Series.S01E02.720p.mkv");
        episode.readCached({newVersion}); QTRY_COMPARE(titles.size(), 5); QCOMPARE(fake.calls.size(), 3);
        QCOMPARE(titles.last()[1].toString(), "Second episode"); QCOMPARE(owner.mediaCount(), 0);
    }
    void unsupportedEpisodeFormsDoNotStartRemoteRequests()
    {
        QTemporaryDir directory;
        for (const auto* name : {"Example.Series.S01E01E02.mkv", "Example.Series.S00E01.mkv", "Example.Series - 12.mkv", "Example.Series.2020.01.01.mkv"}) {
            Fake fake; AddonMetadataService owner(fake.backend()); owner.setAutomaticNetworkAllowed(true);
            EpisodeMetadata episode; episode.setMetadataService(&owner); QSignalSpy titles(&episode, &EpisodeMetadata::titleReady);
            episode.request(url(directory, QString::fromLatin1(name)), {}); QTest::qWait(50);
            QVERIFY(fake.calls.isEmpty()); QVERIFY(titles.isEmpty());
        }
    }
    void numberedFoldersAndScopedIdsReachTheProviderWithoutPrivatePaths()
    {
        QTemporaryDir directory; Fake fake; AddonMetadataService owner(fake.backend()); owner.setAutomaticNetworkAllowed(true);
        EpisodeMetadata episode; episode.setMetadataService(&owner); QSignalSpy titles(&episode, &EpisodeMetadata::titleReady);
        const auto target = url(directory, "Example.Series.2020/Season 2/03.mkv");
        episode.request(target, {{"seriesImdbId", "tt1234567"}}); QTRY_COMPARE(fake.calls.size(), 1);
        const auto context = fake.calls.first().context; const auto identity = context.value("identity").toMap();
        QCOMPARE(identity.value("season").toInt(), 2); QCOMPARE(identity.value("episodes").toList(), QVariantList{3});
        QCOMPARE(identity.value("year").toInt(), 2020); QCOMPARE(identity.value("ids").toMap().value("seriesImdb").toString(), "tt1234567");
        QVERIFY(!QJsonDocument::fromVariant(context).toJson().contains(directory.path().toUtf8()));
        fake.complete(owner, 0, "Third episode", "https://www.tvmaze.com/episodes/33"); QTRY_COMPARE(titles.size(), 1);
        QCOMPARE(titles[0][2].toUrl(), QUrl("https://www.tvmaze.com/episodes/33"));
    }
    void localTagsAndFilenameNamesDoNotNeedAProvider()
    {
        QTemporaryDir directory; Fake fake; AddonMetadataService owner(fake.backend()); owner.setAutomaticNetworkAllowed(true);
        EpisodeMetadata episode; episode.setMetadataService(&owner); QSignalSpy titles(&episode, &EpisodeMetadata::titleReady);
        episode.request(url(directory, "Example.Series.S01E01.mkv"), {{"title", "Embedded episode name"}});
        QTRY_COMPARE(titles.size(), 1); QCOMPARE(titles[0][1].toString(), "Embedded episode name");
        episode.request(url(directory, "Example.Series.S01E02.Filename.Episode.Name.mkv"), {});
        QTRY_COMPARE(titles.size(), 2); QCOMPARE(titles[1][1].toString(), "Filename Episode Name"); QVERIFY(fake.calls.isEmpty());
    }
    void userCorrectionSurvivesAResultAlreadyInFlight()
    {
        QTemporaryDir directory; Fake fake; AddonMetadataService owner(fake.backend()); owner.setAutomaticNetworkAllowed(true);
        EpisodeMetadata episode; episode.setMetadataService(&owner); QSignalSpy titles(&episode, &EpisodeMetadata::titleReady);
        const auto target = url(directory, "Example.Series.S01E01.mkv");
        episode.request(target, {}); QTRY_COMPARE(fake.calls.size(), 1);
        episode.request(target, {{"episodeTitle", "My title"}}); QTRY_COMPARE(titles.size(), 1);
        fake.complete(owner, 0, "Remote title"); QTest::qWait(50);
        QCOMPARE(titles.size(), 1); QCOMPARE(titles[0][1].toString(), "My title");
    }
    void seriesChangeCancelsOldRequestsAndRejectsLateResults()
    {
        QTemporaryDir directory; Fake fake; AddonMetadataService owner(fake.backend()); owner.setAutomaticNetworkAllowed(true);
        EpisodeMetadata episode; episode.setMetadataService(&owner); QSignalSpy titles(&episode, &EpisodeMetadata::titleReady);
        episode.request(url(directory, "Example.Series.S01E01.mkv"), {}); QTRY_COMPARE(fake.calls.size(), 1);
        episode.request(url(directory, "Other.Series.S01E01.mkv"), {}); QTRY_COMPARE(fake.calls.size(), 2);
        QVERIFY(fake.canceled.contains(fake.calls[0].id));
        fake.complete(owner, 0, "Old title"); QTest::qWait(50); QVERIFY(titles.isEmpty());
        fake.complete(owner, 1, "New title"); QTRY_COMPARE(titles.size(), 1); QCOMPARE(titles[0][1].toString(), "New title");
    }
    void anotherProviderSuppliesAnEpisodeNameAndItsOwnSourceCredit()
    {
        QTemporaryDir directory; Fake fake; AddonMetadataService owner(fake.backend()); owner.setAutomaticNetworkAllowed(true);
        EpisodeMetadata episode; episode.setMetadataService(&owner); QSignalSpy titles(&episode, &EpisodeMetadata::titleReady);
        const auto target = url(directory, "Example.Series.S01E01.mkv");
        episode.request(target, {}); QTRY_COMPARE(fake.calls.size(), 1);
        const auto call = fake.calls.first();
        owner.operationFinished(call.id, QStringLiteral("test/episodes"), QStringLiteral("match"), {
            {"identity", call.context.value("identity")}, {"fields", QVariantMap{{"episodeTitle", "Another provider title"}}},
            {"attribution", QVariantMap{{"name", "Example provider"}, {"url", "https://provider.example.org/episodes/1"}}}}, {});
        QTRY_COMPARE(titles.size(), 1); QCOMPARE(titles[0][1].toString(), "Another provider title");
        QCOMPARE(titles[0][2].toUrl(), QUrl("https://provider.example.org/episodes/1"));
    }
    void hostCacheUsesItsOwnNamespaceAndPerFieldProviderCredit()
    {
        const QUrl target("file:///Example.Series.2020/Example.Series.S01E01.mkv");
        const auto identity = SeriesDiscovery::identify(target);
        const QVariantMap provenance{{"provider", "org.example.provider/episodes"}, {"feature", "video.details"},
            {"cachePartition", "release:account:revision"}, {"attribution", QVariantMap{{"name", "Example provider"},
                {"url", "https://provider.example.org/episodes/1"}, {"license", "Example license"}}}};
        // The provider permits saving; the record itself is not stored.
        auto permitted = provenance; permitted.insert("retention", QVariantMap{{"keep", "library"}});
        const auto bytes = HostMetadataCache::encode(identity, "Provider title", permitted);
        QVERIFY(bytes.startsWith("vlc-host-metadata-v1\n")); QVERIFY(!bytes.startsWith(EpisodeTitleCache::prefix));
        const auto entry = HostMetadataCache::decode(bytes, target, identity);
        QCOMPARE(entry.title, "Provider title"); QCOMPARE(entry.provenance, provenance);
        QCOMPARE(entry.source, QUrl("https://provider.example.org/episodes/1"));
        QVERIFY(EpisodeTitleCache::decode(bytes, target, identity).title.isEmpty());
        auto other = identity; other.season = 2; QVERIFY(HostMetadataCache::decode(bytes, target, other).title.isEmpty());
        QVERIFY(HostMetadataCache::encode(identity, "Missing provider", {}).isEmpty());
    }
    void anAddOnTitleIsSavedOnlyWhenItsRetentionPermits()
    {
        const auto identity = SeriesDiscovery::identify(QUrl("file:///Example.Series.2020/Example.Series.S01E01.mkv"));
        QVariantMap provenance{{"provider", "org.example.provider/episodes"}, {"feature", "video.details"}};
        QVERIFY(HostMetadataCache::encode(identity, "Provider title", provenance).isEmpty());
        provenance.insert("retention", QVariantMap{{"keep", "memory"}});
        QVERIFY(HostMetadataCache::encode(identity, "Provider title", provenance).isEmpty());
        provenance.insert("retention", QVariantMap{{"keep", "library"}});
        QVERIFY(!HostMetadataCache::encode(identity, "Provider title", provenance).isEmpty());
        // Local tags and the host's own sources have no package part.
        QVERIFY(!HostMetadataCache::encode(identity, "Local title", {{"provider", "vlc.core.local"}, {"feature", "video.details"}}).isEmpty());
    }
    void cacheCodecRetainsTheOldFormatAndRejectsAnotherIdentity()
    {
        const QUrl target("file:///Example.Series.2020/Example.Series.S01E01.mkv");
        const auto identity = SeriesDiscovery::identify(target);
        const auto bytes = EpisodeTitleCache::encode(identity, "Cached title", QUrl("https://www.tvmaze.com/episodes/1"));
        QVERIFY(bytes.startsWith("vlc-episode-v1\n"));
        const auto decoded = EpisodeTitleCache::decode(bytes, target, identity);
        QCOMPARE(decoded.title, "Cached title"); QCOMPARE(decoded.source, QUrl("https://www.tvmaze.com/episodes/1"));
        for (int field = 0; field < 4; ++field) {
            auto other = identity;
            if (field == 0) other.title = "Other Series";
            if (field == 1) other.year = 2010;
            if (field == 2) other.season = 2;
            if (field == 3) other.episodes = {2};
            QVERIFY(EpisodeTitleCache::decode(bytes, target, other).title.isEmpty());
        }
        QVERIFY(EpisodeTitleCache::decode("podcast-state", target, identity).title.isEmpty());
        QVERIFY(EpisodeTitleCache::encode(identity, QString(20000, QLatin1Char('x')), {}).isEmpty());
        const auto invalid = EpisodeTitleCache::encode(identity, "Cached title", QUrl("file:///private/image"));
        QCOMPARE(EpisodeTitleCache::decode(invalid, target, identity).title, "Cached title");
        QVERIFY(EpisodeTitleCache::decode(invalid, target, identity).source.isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestAddonEpisodeMetadata)
#include "test_addon_episode_metadata.moc"
