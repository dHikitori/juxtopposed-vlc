// SPDX-License-Identifier: GPL-2.0-or-later
#include <QtTest/QtTest>
#include <QJsonDocument>
#include <QScopeGuard>
#include <memory>
#include <utility>
#include "addons/addon_metadata_choices.hpp"

namespace {
struct Fake {
    QVariantMap original{{"kind", "movie"}, {"title", "Original title"}, {"year", 2020}, {"ids", QVariantMap{{"imdb", "tt1234567"}}},
        {"nativeSubject", QVariantMap{{"uri", "file:///private/original.mkv"}, {"policy", "policy-one"}, {"owner", "owner-one"},
            {"uriRevision", "18446744073709551610"}, {"metadataRevision", "9007199254740993"}}}};
    QHash<QString, QVariantMap> media{{QStringLiteral("media-one"), original}, {QStringLiteral("media-two"), original}};
    QHash<QString, int> references{{QStringLiteral("media-one"), 1}, {QStringLiteral("media-two"), 1}};
    QString partition = QStringLiteral("release-1:account-1:settings-1");
    bool enabled = true;
    AddonMetadataChoices::Backend backend()
    {
        return {
            [this](const QString& id) -> std::optional<QVariantMap> { const auto found = media.constFind(id); return found == media.cend() ? std::nullopt : std::optional<QVariantMap>(*found); },
            [this](const QString& id) { if (!media.contains(id)) return false; ++references[id]; return true; },
            [this](const QString& id) { --references[id]; },
            [this](const QString&, const QString& id) { return QVariantMap{{"id", id}, {"enabled", enabled}, {"configured", true}, {"cachePartition", partition}}; }};
    }
    QVariantMap context() const
    { return {{"media", "media-one"}, {"generation", 7}, {"targetRevision", 11}, {"fieldRevisions", QVariantMap{{"title", qulonglong(9007199254740993ULL)}}}}; }
    QVariantMap candidate(const QString& title = QStringLiteral("Selected title")) const
    {
        return {{"id", "guest-id"}, {"provider", "example/identity"},
            {"identity", QVariantMap{{"kind", "movie"}, {"title", title}, {"year", 2021}, {"ids", QVariantMap{{"imdb", "tt7654321"}}}}},
            {"provenance", QVariantMap{{"provider", "example/identity"}, {"cachePartition", partition}}},
            {"attribution", QVariantMap{{"name", "Example provider"}, {"url", "https://provider.example.org/movie/1"}}}};
    }
};
QString choice(const QVariantMap& result, int index = 0)
{ return result.value("candidates").toList().value(index).toMap().value("reference").toString(); }
}
class TestAddonMetadataChoices : public QObject
{
    Q_OBJECT
private slots:
    void mediaInvalidationKeepsOtherSubjectsAndBalancesDetachedHolds()
    {
        Fake fake; AddonMetadataChoices choices(fake.backend());
        const auto first = choices.issue("video.identity", fake.context(), {fake.candidate()});
        const auto accepted = choices.select(choice(first), fake.context()); QVERIFY(!accepted.isEmpty());
        auto otherContext = fake.context(); otherContext.insert("media", "media-two");
        const auto second = choices.issue("video.identity", otherContext, {fake.candidate()});
        choices.invalidateMedia("media-one");
        QVERIFY(choices.selected(accepted, fake.context()).isEmpty());
        QVERIFY(choices.select(choice(first), fake.context()).isEmpty());
        QVERIFY(!choices.select(choice(second), otherContext).isEmpty());
        QCOMPARE(fake.references.value("media-one"), 1);
    }
    void mediaInvalidationReleasesAllDetachedHoldsIfTheFirstReleaseDeletesStore()
    {
        Fake fake; AddonMetadataChoices* store = nullptr; bool armed = false;
        auto backend = fake.backend();
        backend.releaseMedia = [&, release=backend.releaseMedia](const QString& media) {
            release(media); if (armed) delete std::exchange(store, nullptr);
        };
        store = new AddonMetadataChoices(std::move(backend));
        const auto cleanup = qScopeGuard([&] { delete std::exchange(store, nullptr); });
        const auto offered = store->issue("video.identity", fake.context(), {fake.candidate()});
        QVERIFY(!store->select(choice(offered), fake.context()).isEmpty());
        auto otherContext = fake.context(); otherContext.insert("media", "media-two");
        const auto other = store->issue("video.identity", otherContext, {fake.candidate()});
        QVERIFY(!store->select(choice(other), otherContext).isEmpty());
        armed = true; store->invalidateMedia("media-one"); QVERIFY(!store);
        QCOMPARE(fake.references.value("media-one"), 1); QCOMPARE(fake.references.value("media-two"), 1);
    }
    void callbackDeletionBalancesEveryHold_data()
    {
        QTest::addColumn<QString>("operation"); QTest::addColumn<QString>("callback"); QTest::addColumn<int>("ordinal");
        for (const QString operation : {QStringLiteral("issue"), QStringLiteral("select"), QStringLiteral("selected")}) {
            const auto add = [&](const char* callback, int ordinal) {
                const QString name = operation + QLatin1Char('-') + QLatin1String(callback) + QString::number(ordinal);
                QTest::newRow(name.toUtf8().constData()) << operation << QString::fromLatin1(callback) << ordinal;
            };
            add("media", 1); add("provider", 1);
            if (operation != QLatin1String("selected")) { add("hold", 1); add("media", 2); add("provider", 2); }
        }
    }
    void callbackDeletionBalancesEveryHold()
    {
        QFETCH(QString, operation); QFETCH(QString, callback); QFETCH(int, ordinal);
        Fake fake; AddonMetadataChoices* store = nullptr; bool armed = false, captureAlive = false;
        unsigned mediaCalls = 0, providerCalls = 0, holdCalls = 0;
        auto token = std::make_shared<int>(1); const std::weak_ptr<int> weak = token;
        auto backend = fake.backend();
        backend.mediaIdentity = [&, invoke=backend.mediaIdentity, held=token](const QString& id) {
            auto result = invoke(id);
            if (armed && callback == QLatin1String("media") && ++mediaCalls == unsigned(ordinal)) {
                delete std::exchange(store, nullptr); captureAlive = !weak.expired();
            }
            return result;
        };
        backend.registration = [&, invoke=backend.registration, held=token](const QString& feature, const QString& id) {
            auto result = invoke(feature, id);
            if (armed && callback == QLatin1String("provider") && ++providerCalls == unsigned(ordinal)) {
                delete std::exchange(store, nullptr); captureAlive = !weak.expired();
            }
            return result;
        };
        backend.holdMedia = [&, invoke=backend.holdMedia, held=token](const QString& id) {
            const auto result = invoke(id);
            if (armed && callback == QLatin1String("hold") && ++holdCalls == unsigned(ordinal)) {
                delete std::exchange(store, nullptr); captureAlive = !weak.expired();
            }
            return result;
        };
        store = new AddonMetadataChoices(std::move(backend)); backend = {}; token.reset();
        const auto cleanup = qScopeGuard([&] { delete std::exchange(store, nullptr); });
        QString offered, accepted;
        if (operation != QLatin1String("issue")) {
            offered = choice(store->issue("video.details", fake.context(), {fake.candidate()})); QVERIFY(!offered.isEmpty());
        }
        if (operation == QLatin1String("selected")) { accepted = store->select(offered, fake.context()); QVERIFY(!accepted.isEmpty()); }
        armed = true;
        if (operation == QLatin1String("issue")) QVERIFY(store->issue("video.details", fake.context(), {fake.candidate()}).contains("error"));
        else if (operation == QLatin1String("select")) QVERIFY(store->select(offered, fake.context()).isEmpty());
        else QVERIFY(store->selected(accepted, fake.context()).isEmpty());
        QVERIFY(!store); QVERIFY(captureAlive); QVERIFY(weak.expired()); QCOMPARE(fake.references.value("media-one"), 1);
    }

    void aHoldCallbackCannotPublishAfterInvalidation_data()
    {
        QTest::addColumn<bool>("selecting");
        QTest::newRow("issue") << false; QTest::newRow("select") << true;
    }
    void aHoldCallbackCannotPublishAfterInvalidation()
    {
        QFETCH(bool, selecting); Fake fake; AddonMetadataChoices* store = nullptr; bool armed = false;
        auto backend = fake.backend();
        backend.holdMedia = [&, hold=backend.holdMedia](const QString& id) {
            const auto result = hold(id); if (armed) store->invalidate(); return result;
        };
        AddonMetadataChoices choices(std::move(backend)); store = &choices;
        const auto result = choices.issue("video.details", fake.context(), {fake.candidate()});
        const auto offered = choice(result); QVERIFY(!offered.isEmpty()); armed = true;
        if (selecting) QVERIFY(choices.select(offered, fake.context()).isEmpty());
        else QVERIFY(choices.issue("video.details", fake.context(), {fake.candidate()}).contains("error"));
        QCOMPARE(fake.references.value("media-one"), 1); QVERIFY(choices.select(offered, fake.context()).isEmpty());
    }

    void releaseReentryRevokesCopiedDataBeforeItCanReturn()
    {
        Fake fake; AddonMetadataChoices* store = nullptr; bool armed = false; QString accepted;
        auto backend = fake.backend();
        backend.registration = [&, query=backend.registration](const QString& feature, const QString& id) {
            auto result = query(feature, id); if (armed) store->releaseSelection(accepted); return result;
        };
        AddonMetadataChoices choices(std::move(backend)); store = &choices;
        const auto set = choices.issue("video.details", fake.context(), {fake.candidate()});
        accepted = choices.select(choice(set), fake.context()); QVERIFY(!accepted.isEmpty()); armed = true;
        QVERIFY(choices.selected(accepted, fake.context()).isEmpty()); QCOMPARE(fake.references.value("media-one"), 2);
    }

    void invalidationDetachesEveryHoldBeforeTheFirstCallback()
    {
        Fake fake; AddonMetadataChoices* store = nullptr; bool armed = false; unsigned released = 0;
        auto backend = fake.backend();
        backend.releaseMedia = [&, release=backend.releaseMedia](const QString& id) {
            release(id);
            if (armed && ++released == 1) {
                QVERIFY(store->issue("video.details", fake.context(), {fake.candidate()}).contains("error"));
                store->invalidate(); delete std::exchange(store, nullptr);
            }
        };
        store = new AddonMetadataChoices(std::move(backend));
        const auto cleanup = qScopeGuard([&] { delete std::exchange(store, nullptr); });
        for (int index = 0; index < 2; ++index) {
            const auto set = store->issue("video.details", fake.context(), {fake.candidate()});
            QVERIFY(!store->select(choice(set), fake.context()).isEmpty());
        }
        QCOMPARE(fake.references.value("media-one"), 5); armed = true; store->invalidate();
        QVERIFY(!store); QCOMPARE(released, 4u); QCOMPARE(fake.references.value("media-one"), 1);
    }

    void theDestructorRefusesNewAdmissions()
    {
        Fake fake; AddonMetadataChoices* store = nullptr; bool destroying = false; unsigned released = 0;
        auto backend = fake.backend();
        backend.releaseMedia = [&, release=backend.releaseMedia](const QString& id) {
            if (destroying) {
                ++released; QVERIFY(store->issue("video.details", fake.context(), {fake.candidate()}).contains("error"));
                QVERIFY(store->select("metadata-choice:old", fake.context()).isEmpty());
            }
            release(id);
        };
        store = new AddonMetadataChoices(std::move(backend));
        const auto cleanup = qScopeGuard([&] { if (store) { destroying = true; delete store; store = nullptr; } });
        const auto set = store->issue("video.details", fake.context(), {fake.candidate()});
        QVERIFY(!store->select(choice(set), fake.context()).isEmpty());
        destroying = true; delete store; store = nullptr;
        QCOMPARE(released, 2u); QCOMPARE(fake.references.value("media-one"), 1);
    }

    void retainedSelectionsConsumeTheByteLimitAfterSetRelease()
    {
        Fake fake; AddonMetadataChoices::Options options; options.maxBytes = 8192;
        AddonMetadataChoices choices(fake.backend(), options); auto candidate = fake.candidate();
        const QString address = QStringLiteral("https://provider.example.org/") + QString(1800, QLatin1Char('x'));
        candidate["attribution"] = QVariantMap{{"name", "Example provider"}, {"url", address}};
        const auto set = choices.issue("video.details", fake.context(), {candidate}); QVERIFY(!choice(set).isEmpty());
        QStringList retained;
        for (int index = 0; index < options.maxSelections; ++index) {
            const auto selected = choices.select(choice(set), fake.context()); if (selected.isEmpty()) break; retained.append(selected);
        }
        QVERIFY(!retained.isEmpty()); QVERIFY(retained.size() < options.maxSelections);
        choices.releaseSet(set.value("choiceSet").toString());
        const auto next = choices.issue("video.details", fake.context(), {candidate}); QVERIFY(!choice(next).isEmpty());
        QVERIFY(choices.select(choice(next), fake.context()).isEmpty());
        choices.releaseSelection(retained.takeLast());
        QVERIFY(!choices.select(choice(next), fake.context()).isEmpty());
        for (const auto& selected : retained) QVERIFY(!choices.selected(selected, fake.context()).isEmpty());
    }

    void privateNativeSubjectChangesRevokeChoices()
    {
        Fake fake; AddonMetadataChoices choices(fake.backend());
        const auto set = choices.issue("video.details", fake.context(), {fake.candidate()});
        const auto accepted = choices.select(choice(set), fake.context()); QVERIFY(!accepted.isEmpty());
        for (const QString key : {QStringLiteral("uri"), QStringLiteral("policy"), QStringLiteral("owner"), QStringLiteral("uriRevision"), QStringLiteral("metadataRevision")}) {
            fake.media["media-one"] = fake.original;
            auto subject = fake.media["media-one"].value("nativeSubject").toMap();
            const QString changed = subject.value(key).toString() + QLatin1Char('x'); subject[key] = changed;
            fake.media["media-one"]["nativeSubject"] = subject;
            QVERIFY(choices.select(choice(set), fake.context()).isEmpty()); QVERIFY(choices.selected(accepted, fake.context()).isEmpty());
        }
    }

    void expiryRecoversQuotaWithoutTimerDelivery_data()
    {
        QTest::addColumn<bool>("deleteOnRelease"); QTest::newRow("recover") << false; QTest::newRow("delete") << true;
    }
    void expiryRecoversQuotaWithoutTimerDelivery()
    {
        QFETCH(bool, deleteOnRelease); Fake fake; AddonMetadataChoices* store = nullptr; bool armed = false;
        auto backend = fake.backend();
        backend.releaseMedia = [&, release=backend.releaseMedia](const QString& id) {
            release(id); if (armed && deleteOnRelease) delete std::exchange(store, nullptr);
        };
        AddonMetadataChoices::Options options; options.maxSets = 1; options.maxSelections = 1; options.lifetimeMs = 30;
        store = new AddonMetadataChoices(std::move(backend), options);
        const auto cleanup = qScopeGuard([&] { delete std::exchange(store, nullptr); });
        const auto first = store->issue("video.details", fake.context(), {fake.candidate()}); QVERIFY(!choice(first).isEmpty());
        QVERIFY(!store->select(choice(first), fake.context()).isEmpty()); QTest::qSleep(45); armed = true;
        const auto next = store->issue("video.details", fake.context(), {fake.candidate()});
        if (deleteOnRelease) { QVERIFY(!store); QVERIFY(next.contains("error")); QCOMPARE(fake.references.value("media-one"), 1); }
        else { QVERIFY(!choice(next).isEmpty()); QCOMPARE(fake.references.value("media-one"), 2); }
    }

    void ambiguousIdentitiesNeedAnExplicitDistinctSelection()
    {
        Fake fake; AddonMetadataChoices choices(fake.backend());
        const auto result = choices.issue("video.details", fake.context(), {fake.candidate("First"), fake.candidate("Second")});
        QCOMPARE(result.value("candidates").toList().size(), 2); QVERIFY(result.value("requiresSelection").toBool());
        QVERIFY(!choice(result).isEmpty()); QVERIFY(choice(result) != choice(result, 1));
        QCOMPARE(fake.media.value("media-one"), fake.original);
        const auto selected = choices.select(choice(result, 1), fake.context());
        QVERIFY(selected.startsWith(QStringLiteral("metadata-selection:"))); QVERIFY(selected != choice(result, 1));
        const auto data = choices.selected(selected, fake.context());
        QCOMPARE(data.value("identity").toMap().value("title").toString(), "Second");
        QCOMPARE(data.value("identity").toMap().value("kind").toString(), "movie");
        QCOMPARE(data.value("provenance").toMap().value("provider").toString(), "example/identity");
        QCOMPARE(fake.media.value("media-one"), fake.original);
    }
    void choicesAndSelectionsBindThePrivateTargetAndEveryRevision()
    {
        Fake fake; AddonMetadataChoices choices(fake.backend());
        const auto result = choices.issue("video.details", fake.context(), {fake.candidate()});
        const auto accepted = choices.select(choice(result), fake.context()); QVERIFY(!accepted.isEmpty());
        for (int field = 0; field < 4; ++field) {
            auto changed = fake.context();
            if (field == 0) changed["media"] = "media-two";
            if (field == 1) changed["generation"] = 8;
            if (field == 2) changed["targetRevision"] = 12;
            if (field == 3) changed["fieldRevisions"] = QVariantMap{{"title", qulonglong(9007199254740992ULL)}};
            QVERIFY(choices.select(choice(result), changed).isEmpty()); QVERIFY(choices.selected(accepted, changed).isEmpty());
        }
        fake.media["media-one"]["title"] = "A new private identity";
        QVERIFY(choices.select(choice(result), fake.context()).isEmpty()); QVERIFY(choices.selected(accepted, fake.context()).isEmpty());
    }
    void providerReleaseAndAccountChangesRevokeUseImmediately()
    {
        Fake fake; AddonMetadataChoices choices(fake.backend());
        const auto result = choices.issue("video.details", fake.context(), {fake.candidate()});
        const auto accepted = choices.select(choice(result), fake.context()); QVERIFY(!accepted.isEmpty());
        fake.partition = "release-2:account-2:settings-2";
        QVERIFY(choices.select(choice(result), fake.context()).isEmpty()); QVERIFY(choices.selected(accepted, fake.context()).isEmpty());
        fake.partition = "release-1:account-1:settings-1"; fake.enabled = false;
        QVERIFY(choices.selected(accepted, fake.context()).isEmpty());
        choices.invalidate(); QCOMPARE(fake.references.value("media-one"), 1);
        fake.enabled = true; QVERIFY(choices.select(choice(result), fake.context()).isEmpty());
    }
    void forgedReferencesAndProviderClaimsHaveNoAuthority()
    {
        Fake fake; AddonMetadataChoices choices(fake.backend());
        QVERIFY(choices.select("metadata-choice:forged", fake.context()).isEmpty());
        auto candidate = fake.candidate(); candidate["provenance"] = QVariantMap{{"provider", "another/provider"}, {"cachePartition", fake.partition}};
        QVERIFY(choices.issue("video.details", fake.context(), {candidate}).contains("error"));
        auto unknown = fake.context(); unknown["media"] = "not-a-host-reference";
        QVERIFY(choices.issue("video.details", unknown, {fake.candidate()}).contains("error"));
        QVERIFY(choices.selected("metadata-selection:forged", fake.context()).isEmpty());
    }
    void publicRowsOmitGuestIdsAndUnknownIdentityProperties()
    {
        Fake fake; AddonMetadataChoices choices(fake.backend()); auto candidate = fake.candidate();
        auto entity = candidate.value("identity").toMap(); entity["url"] = "file:///private/media.mkv"; candidate["identity"] = entity;
        candidate["private"] = "file:///private/secret";
        const auto result = choices.issue("video.details", fake.context(), {candidate});
        const auto bytes = QJsonDocument::fromVariant(result).toJson();
        QVERIFY(!bytes.contains("guest-id")); QVERIFY(!bytes.contains("file:///private"));
        QVERIFY(result.value("candidates").toList().first().toMap().value("requiresConfirmation").toBool());
    }
    void explicitReleaseAndInvalidationKeepReferenceCountsCorrect()
    {
        Fake fake; AddonMetadataChoices choices(fake.backend());
        const auto result = choices.issue("video.details", fake.context(), {fake.candidate()});
        QCOMPARE(fake.references.value("media-one"), 2);
        const auto selected = choices.select(choice(result), fake.context()); QCOMPARE(fake.references.value("media-one"), 3);
        choices.releaseSet(result.value("choiceSet").toString()); QCOMPARE(fake.references.value("media-one"), 2);
        QVERIFY(choices.select(choice(result), fake.context()).isEmpty()); QVERIFY(!choices.selected(selected, fake.context()).isEmpty());
        choices.releaseSelection(selected); QCOMPARE(fake.references.value("media-one"), 1);
        QVERIFY(choices.selected(selected, fake.context()).isEmpty());
    }
    void limitsPreserveExistingHandlesAndReportOmittedRows()
    {
        Fake fake; AddonMetadataChoices::Options options; options.maxChoices = 1; options.maxSelections = 1;
        AddonMetadataChoices choices(fake.backend(), options);
        const auto result = choices.issue("video.details", fake.context(), {fake.candidate("First"), fake.candidate("Second")});
        QCOMPARE(result.value("candidates").toList().size(), 1); QVERIFY(result.value("limited").toBool()); QVERIFY(result.contains("warning"));
        const auto accepted = choices.select(choice(result), fake.context()); QVERIFY(!accepted.isEmpty());
        QVERIFY(choices.issue("video.details", fake.context(), {fake.candidate()}).contains("error"));
        QVERIFY(choices.select(choice(result), fake.context()).isEmpty()); QVERIFY(!choices.selected(accepted, fake.context()).isEmpty());
        auto large = fake.candidate(); large["extra"] = QString(8193, QLatin1Char('x'));
        QVERIFY(choices.issue("video.details", fake.context(), {large}).contains("error"));
    }
    void expiryDoesNotDependOnTimerDelivery()
    {
        Fake fake; AddonMetadataChoices::Options options; options.lifetimeMs = 80; AddonMetadataChoices choices(fake.backend(), options);
        const auto result = choices.issue("video.details", fake.context(), {fake.candidate()});
        const auto accepted = choices.select(choice(result), fake.context()); QVERIFY(!accepted.isEmpty());
        QTest::qSleep(100);
        QVERIFY(choices.select(choice(result), fake.context()).isEmpty()); QVERIFY(choices.selected(accepted, fake.context()).isEmpty());
        QCoreApplication::processEvents(); QCOMPARE(fake.references.value("media-one"), 1);
    }
};
QTEST_GUILESS_MAIN(TestAddonMetadataChoices)
#include "test_addon_metadata_choices.moc"
