// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <vlc_common.h>
#include <vlc_input_item.h>
#include "addons/addon_accepted_identities.hpp"
#include "addons/addon_identity_choice.hpp"
#include "addons/addon_metadata_service.hpp"

using namespace vlc::addons;
namespace {
const QString Provider = QStringLiteral("org.videolan.tvmaze/episodes");
QVariantMap registration()
{
    return {{"id", Provider}, {"packageId", "org.videolan.tvmaze"}, {"name", "TVmaze"}, {"enabled", true},
        {"configured", true}, {"features", QStringList{"video.details", "video.identity"}},
        {"mediaKinds", QStringList{"episode"}}, {"operations", QStringList{"identify", "enrich"}},
        {"cachePartition", "release-1:account-1:settings-1"}};
}
QVariantMap show(const QString& id, int year)
{
    return {{"kind", "episode"}, {"title", "Example Series"}, {"year", year}, {"season", 1},
        {"episodes", QVariantList{2}}, {"ids", QVariantMap{{"tvmazeShow", id}}}};
}
struct Fake {
    struct Call { QString id, provider, operation; QVariantMap context; };
    QList<Call> calls;
    QStringList canceled;
    AddonMetadataService::Backend backend()
    {
        AddonMetadataService::Backend result;
        result.providers = {
            [](const QString&) { return QVariantList{registration()}; },
            [this](const QString& provider, const QString& operation, const QVariantMap& context) {
                const auto id = QStringLiteral("portable-%1").arg(calls.size() + 1);
                calls.append({id, provider, operation, context});
                return id;
            },
            [this](const QString& id) { canceled.append(id); }};
        return result;
    }
};
struct Fixture {
    QTemporaryDir profile;
    Fake fake;
    AddonMetadataService service{fake.backend()};
    std::shared_ptr<AddonAcceptedIdentities> accepted = std::make_shared<AddonAcceptedIdentities>(profile.path());
    input_item_t* item = input_item_New("file:///shows/Example%20Series/Season%201/Example.Series.S01E02.mkv",
                                        "Example.Series.S01E02.mkv");
    AddonIdentityChoice choice{&service, accepted, [this] { return input_item_Hold(item); }};
    ~Fixture() { choice.close(); input_item_Release(item); }
    void offer(const QVariantList& identities) {
        const auto call = fake.calls.last();
        QVariantList candidates;
        for (const auto& identity : identities) candidates.append(QVariantMap{{"identity", identity}});
        service.operationFinished(call.id, call.provider, "match", {{"candidates", candidates}}, {});
    }
    void draft(const QVariantMap& identity, const QString& title) {
        const auto call = fake.calls.last();
        service.operationFinished(call.id, call.provider, "match",
            {{"identity", identity}, {"fields", QVariantMap{{"episodeTitle", title}}}}, {});
    }
    QString title() const {
        char* value = input_item_GetMeta(item, vlc_meta_Title);
        const auto result = QString::fromUtf8(value ? value : "");
        free(value);
        return result;
    }
};
}

class TestAddonIdentityChoice : public QObject
{
    Q_OBJECT
private slots:
    void anExplicitChoiceAppliesItsTitleAndSavesARevision()
    {
        Fixture f;
        QSignalSpy accepted(&f.choice, &AddonIdentityChoice::accepted);
        QVERIFY(f.choice.start());
        QCOMPARE(f.choice.state(), QStringLiteral("searching"));
        QTRY_COMPARE(f.fake.calls.size(), 1);
        QCOMPARE(f.fake.calls.first().operation, QStringLiteral("identify"));
        QCOMPARE(f.fake.calls.first().context.value("identity").toMap().value("title").toString(), QStringLiteral("Example Series"));
        f.offer({show("111", 2020), show("112", 2021)});
        QTRY_COMPARE(f.choice.state(), QStringLiteral("choosing"));
        const auto rows = f.choice.candidates();
        QCOMPARE(rows.size(), 2);
        // QML gets display rows only, never a choice reference.
        for (const auto& row : rows) {
            QVERIFY(!row.toMap().contains("reference"));
            QVERIFY(!row.toMap().contains("identity"));
        }
        QVERIFY(rows.at(1).toMap().value("detail").toString().contains("2021"));
        QVERIFY(!f.choice.choose(2));
        QVERIFY(f.choice.choose(1));
        QCOMPARE(f.choice.state(), QStringLiteral("applying"));
        QTRY_COMPARE(f.fake.calls.size(), 2);
        QCOMPARE(f.fake.calls.last().operation, QStringLiteral("enrich"));
        QCOMPARE(f.fake.calls.last().context.value("identity").toMap().value("ids").toMap().value("tvmazeShow").toString(),
                 QStringLiteral("112"));
        f.draft(show("112", 2021), QStringLiteral("The Chosen Episode"));
        QTRY_COMPARE(f.choice.state(), QStringLiteral("done"));
        QCOMPARE(f.title(), QStringLiteral("The Chosen Episode"));
        QCOMPARE(accepted.size(), 1);
        QCOMPARE(accepted.first().first().toULongLong(), 1ull);
        const auto record = f.accepted->find(QStringLiteral("series:example series"));
        QVERIFY(record);
        QCOMPARE(record->revision, 1ull);
        QCOMPARE(record->provider, Provider);
        QCOMPARE(record->identity.value("ids").toMap().value("tvmazeShow").toString(), QStringLiteral("112"));
        // Numbering stays local, so the choice serves the whole series.
        QVERIFY(!record->identity.contains("season"));
        QCOMPARE(f.service.identityTargetCount(), 0);
    }
    void oneOfferStillWaitsForTheUser()
    {
        Fixture f;
        QVERIFY(f.choice.start());
        QTRY_COMPARE(f.fake.calls.size(), 1);
        f.offer({show("111", 2020)});
        QTRY_COMPARE(f.choice.state(), QStringLiteral("choosing"));
        QTest::qWait(50);
        QCOMPARE(f.fake.calls.size(), 1);
        QCOMPARE(f.title(), QString());
        QVERIFY(!f.accepted->find(QStringLiteral("series:example series")));
    }
    void aManualQueryReplacesTheSearchText()
    {
        Fixture f;
        QVERIFY(f.choice.start(QStringLiteral("Another Show")));
        QCOMPARE(f.choice.query(), QStringLiteral("Another Show"));
        QTRY_COMPARE(f.fake.calls.size(), 1);
        QCOMPARE(f.fake.calls.first().context.value("query").toString(), QStringLiteral("Another Show"));
    }
    void cancelReleasesTheSearch()
    {
        Fixture f;
        QVERIFY(f.choice.start());
        QTRY_COMPARE(f.fake.calls.size(), 1);
        QCOMPARE(f.service.identityTargetCount(), 1);
        f.choice.cancel();
        QCOMPARE(f.choice.state(), QStringLiteral("idle"));
        QCOMPARE(f.service.identityTargetCount(), 0);
        QVERIFY(f.fake.canceled.contains(f.fake.calls.first().id));
        // A late answer changes nothing.
        f.offer({show("111", 2020)});
        QTest::qWait(20);
        QCOMPARE(f.choice.state(), QStringLiteral("idle"));
    }
    void noOfferAndNoEpisodeFailWithAMessage()
    {
        Fixture f;
        QVERIFY(f.choice.start());
        QTRY_COMPARE(f.fake.calls.size(), 1);
        f.service.operationFinished(f.fake.calls.first().id, Provider, "no_match", {}, {});
        QTRY_COMPARE(f.choice.state(), QStringLiteral("failed"));
        QVERIFY(!f.choice.message().isEmpty());
        input_item_t* movie = input_item_New("file:///films/Example.Movie.2020.mkv", "Example.Movie.2020.mkv");
        AddonIdentityChoice other(&f.service, f.accepted, [movie] { return input_item_Hold(movie); });
        QVERIFY(!other.start());
        QCOMPARE(other.state(), QStringLiteral("failed"));
        input_item_Release(movie);
        AddonIdentityChoice nothing(&f.service, f.accepted, [] { return static_cast<input_item_t*>(nullptr); });
        QVERIFY(!nothing.start());
    }
    void revisionsRiseAcrossWindowsAndForget()
    {
        QTemporaryDir profile;
        AddonAcceptedIdentities first(profile.path()), second(profile.path());
        const QVariantMap local{{"type", "episode"}, {"title", "Example Series"}, {"year", 2020}};
        const auto key = AddonAcceptedIdentities::key(local);
        QCOMPARE(key, QStringLiteral("series:example series:2020"));
        QVERIFY(AddonAcceptedIdentities::key({{"type", "movie"}, {"title", "Film"}}).isEmpty());
        QCOMPARE(first.accept(key, show("111", 2020), Provider), 1ull);
        // Another window reads the saved choice and writes the next revision.
        QCOMPARE(second.find(key)->identity.value("ids").toMap().value("tvmazeShow").toString(), QStringLiteral("111"));
        QCOMPARE(second.accept(key, show("112", 2021), Provider), 2ull);
        QCOMPARE(first.find(key)->revision, 2ull);
        QVERIFY(first.forget(key));
        QVERIFY(!second.find(key));
        QCOMPARE(second.accept(key, show("111", 2020), Provider), 4ull);
    }
};
QTEST_GUILESS_MAIN(TestAddonIdentityChoice)
#include "test_addon_identity_choice.moc"
