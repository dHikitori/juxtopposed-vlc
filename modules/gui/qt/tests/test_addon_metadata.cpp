// SPDX-License-Identifier: GPL-2.0-or-later
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QScopeGuard>
#include <utility>
#include "addons/addon_metadata_coordinator.hpp"

namespace {
struct FakeBackend {
    struct Call { QString id; QString provider; QString operation; QVariantMap context; };
    QVariantList providers;
    QList<Call> calls;
    QStringList canceled;
    std::function<void(const Call&)> duringInvoke;
    AddonMetadataCoordinator::Backend backend() {
        return {
            [this](const QString&) { return providers; },
            [this](const QString& provider, const QString& operation, const QVariantMap& context) {
                const auto id = QStringLiteral("call-%1").arg(calls.size() + 1);
                calls.append({id, provider, operation, context});
                if (duringInvoke) duringInvoke(calls.last());
                return id;
            },
            [this](const QString& invocation) { canceled.append(invocation); }
        };
    }
};

QVariantMap registration(const QString& id) {
    const QString cachePartition = id + QStringLiteral(":release-1:account-1:settings-1");
    return {{"id", id}, {"packageId", id.section('/', 0, 0)}, {"name", id}, {"enabled", true},
        {"configured", true}, {"cachePartition", cachePartition},
        {"mediaKinds", QStringList{"movie", "episode"}}, {"features", QStringList{"subtitles", "video.details"}}};
}
QVariantMap identity(const QString& imdb = QStringLiteral("tt1234567")) {
    return {{"kind", "movie"}, {"title", "Example Movie"}, {"year", 2020}, {"ids", QVariantMap{{"imdb", imdb}}}};
}
QVariantMap context(int generation = 1) {
    return {{"identity", identity()}, {"language", "en"}, {"generation", generation}};
}
QVariantMap asset(const QString& id = QStringLiteral("subtitle-1"), const QVariantMap& entity = identity()) {
    return {{"id", id}, {"name", "Example.Movie.2020"}, {"language", "en"}, {"format", "srt"}, {"identity", entity}};
}
QVariantMap assets(const QVariantMap& candidate = asset()) {
    return {{"candidates", QVariantList{candidate}}};
}
QVariantMap claims(const QVariantMap& fields, const QVariantMap& entity = identity()) {
    return {{"identity", entity}, {"fields", fields}};
}
void complete(AddonMetadataCoordinator& coordinator, const FakeBackend::Call& call,
              const QString& status, const QVariantMap& data = {}, const QString& error = {}) {
    coordinator.operationFinished(call.id, call.provider, status, data, error);
}
}

class TestAddonMetadata : public QObject
{
    Q_OBJECT
private slots:
    void anInvokeCanDeleteTheOwnerAndItsReturnedWorkIsCanceled()
    {
        for (const bool native : {false, true}) {
            FakeBackend fake; fake.providers = {registration("provider/details")};
            AddonMetadataCoordinator* owner = nullptr;
            const auto cleanup = qScopeGuard([&] { delete std::exchange(owner, nullptr); });
            auto token = std::make_shared<int>(1); const std::weak_ptr<int> weak = token;
            bool aliveDuringReturn = false;
            const auto body = [&](const QString& provider, const QString& operation, const QVariantMap& projected) {
                const auto id = QStringLiteral("returned-request"); fake.calls.append({id, provider, operation, projected});
                delete std::exchange(owner, nullptr); aliveDuringReturn = !weak.expired(); return id;
            };
            auto backend = fake.backend();
            backend.requiresNativeScope = [native](const QString&) { return native; };
            backend.invoke = [&, held=token](const QString& provider, const QString& operation, const QVariantMap& projected) {
                return body(provider, operation, projected);
            };
            backend.invokeNative = [&, held=token](const QString& provider, const QString& operation,
                                                   const QVariantMap& projected, const QString&) {
                return body(provider, operation, projected);
            };
            owner = new AddonMetadataCoordinator(std::move(backend)); token.reset();
            if (native) owner->enrichNative("video.details", context(), {"description"}, "native-scope");
            else owner->enrich("video.details", context(), {"description"});
            QTRY_VERIFY(!owner); QCOMPARE(fake.calls.size(), 1);
            QCOMPARE(fake.canceled, QStringList{fake.calls.first().id});
            QVERIFY(aliveDuringReturn); QVERIFY(weak.expired());
        }
    }

    void aTimeoutCancelCanDeleteTheCoordinator()
    {
        FakeBackend fake; fake.providers = {registration("provider/details")};
        AddonMetadataCoordinator* owner = nullptr; auto backend = fake.backend();
        backend.cancel = [&](const QString& id) { fake.canceled.append(id); delete std::exchange(owner, nullptr); };
        owner = new AddonMetadataCoordinator(std::move(backend));
        const auto cleanup = qScopeGuard([&] { delete std::exchange(owner, nullptr); });
        QSignalSpy finished(owner, &AddonMetadataCoordinator::finished);
        auto request = context(); request.insert("timeoutMs", 100);
        owner->enrich("video.details", request, {"description"});
        QTRY_VERIFY(!owner); QCOMPARE(fake.calls.size(), 1);
        QCOMPARE(fake.canceled, QStringList{fake.calls.first().id}); QCOMPARE(finished.size(), 0);
    }

    void nativeScopesStayPrivateAndDoNotShareClaims()
    {
        FakeBackend fake; fake.providers = {registration("native/artwork")};
        auto backend = fake.backend(); QStringList scopes;
        backend.requiresNativeScope = [](const QString&) { return true; };
        backend.invokeNative = [&](const QString& provider, const QString& operation,
                                    const QVariantMap& projected, const QString& scope) {
            scopes.append(scope);
            const auto id = QStringLiteral("native-%1").arg(fake.calls.size() + 1);
            fake.calls.append({id, provider, operation, projected}); return id;
        };
        AddonMetadataCoordinator coordinator(std::move(backend));
        QSignalSpy results(&coordinator, &AddonMetadataCoordinator::finished);
        QSignalSpy canceled(&coordinator, &AddonMetadataCoordinator::canceled);
        const auto first = coordinator.enrichNative("video.details", context(), {"description"}, "anchor-a");
        coordinator.enrichNative("video.details", context(), {"description"}, "anchor-b");
        QTRY_COMPARE(fake.calls.size(), 2); QCOMPARE(scopes, QStringList({"anchor-a", "anchor-b"}));
        for (const auto& call : fake.calls) {
            QVERIFY(!call.context.contains("privateScope"));
            QVERIFY(!QJsonDocument::fromVariant(call.context).toJson().contains("anchor-"));
        }
        coordinator.cancel(first); QCOMPARE(canceled.size(), 1); QCOMPARE(fake.canceled, QStringList{fake.calls.first().id});
        complete(coordinator, fake.calls.last(), "match", claims({{"description", "Native description"}}));
        QTRY_COMPARE(results.size(), 1);
        coordinator.enrichNative("video.details", context(), {"description"}, "anchor-b");
        QTRY_COMPARE(fake.calls.size(), 3);
        coordinator.enrich("video.details", context(), {"description"});
        QTRY_COMPARE(results.size(), 2); QCOMPARE(fake.calls.size(), 3);
        QCOMPARE(results.last().at(1).toString(), QStringLiteral("no_match"));
    }

    void nativeScopesDoNotChangePortableCoalescing()
    {
        FakeBackend fake; fake.providers = {registration("portable/details")};
        auto backend = fake.backend(); backend.requiresNativeScope = [](const QString&) { return false; };
        backend.invokeNative = [](const QString&, const QString&, const QVariantMap&, const QString&) {
            QTest::qFail("A portable request used the native hook.", __FILE__, __LINE__); return QString{};
        };
        AddonMetadataCoordinator coordinator(std::move(backend));
        QSignalSpy results(&coordinator, &AddonMetadataCoordinator::finished);
        coordinator.enrichNative("video.details", context(), {"description"}, "anchor-a");
        coordinator.enrichNative("video.details", context(), {"description"}, "anchor-b");
        QTRY_COMPARE(fake.calls.size(), 1);
        QVERIFY(!QJsonDocument::fromVariant(fake.calls.first().context).toJson().contains("anchor-"));
        complete(coordinator, fake.calls.first(), "match", claims({{"description", "Portable description"}}));
        QTRY_COMPARE(results.size(), 2);
    }

    void sequentialFallbackUsesFeatureOrder() {
        FakeBackend fake;
        fake.providers = {registration("a/search"), registration("b/search")};
        AddonMetadataCoordinator coordinator(fake.backend());
        coordinator.setProviderOrder("subtitles", {"b/search", "a/search"});
        QSignalSpy results(&coordinator, &AddonMetadataCoordinator::finished);
        const auto consumer = coordinator.searchAssets("subtitles", context());
        QTRY_COMPARE(fake.calls.size(), 1);
        QCOMPARE(fake.calls[0].provider, "b/search");
        complete(coordinator, fake.calls[0], "no_match");
        QTRY_COMPARE(fake.calls.size(), 2);
        QCOMPARE(fake.calls[1].provider, "a/search");
        complete(coordinator, fake.calls[1], "match", assets());
        QTRY_COMPARE(results.size(), 1);
        QCOMPARE(results[0][0].toString(), consumer);
        QCOMPARE(results[0][1].toString(), "match");
        QCOMPARE(results[0][2].toMap().value("candidates").toList()[0].toMap().value("provider").toString(), "a/search");
    }

    void mediaAndLanguageProfileOverridesFeatureDefault() {
        FakeBackend fake;
        fake.providers = {registration("a/search"), registration("b/search")};
        AddonMetadataCoordinator coordinator(fake.backend());
        coordinator.setProviderOrder("subtitles", {"a/search", "b/search"});
        coordinator.setProviderOrder("subtitles/movie/en", {"b/search", "a/search"});
        coordinator.searchAssets("subtitles", context());
        QTRY_COMPARE(fake.calls.size(), 1);
        QCOMPARE(fake.calls[0].provider, "b/search");
    }

    void partialEnrichmentProtectsCurrentFieldsAndPreservesProvenance() {
        FakeBackend fake;
        fake.providers = {registration("a/details"), registration("b/details")};
        AddonMetadataCoordinator coordinator(fake.backend());
        QSignalSpy results(&coordinator, &AddonMetadataCoordinator::finished);
        auto request = context(12);
        request["existingFields"] = QVariantMap{{"title", "My correction"}};
        request["protectedFields"] = QStringList{"title"};
        request["fieldRevisions"] = QVariantMap{{"title", 9}, {"description", 4}, {"genre", 2}};
        request["targetRevision"] = 22;
        coordinator.enrich("video.details", request, {"title", "description", "genre"});
        QTRY_COMPARE(fake.calls.size(), 1);
        QCOMPARE(fake.calls[0].context.value("fields").toStringList(), (QStringList{"description", "genre"}));
        complete(coordinator, fake.calls[0], "partial", claims({{"title", "Bad replacement"}, {"description", "From A"}}));
        QTRY_COMPARE(fake.calls.size(), 2);
        QCOMPARE(fake.calls[1].context.value("fields").toStringList(), QStringList{"genre"});
        complete(coordinator, fake.calls[1], "match", claims({{"description", "From B"}, {"genre", "Drama"}}));
        QTRY_COMPARE(results.size(), 1);
        const auto data = results[0][2].toMap();
        QCOMPARE(results[0][1].toString(), "match");
        QCOMPARE(data.value("contextGeneration").toInt(), 12);
        QCOMPARE(data.value("targetRevision").toInt(), 22);
        QCOMPARE(data.value("expectedFieldRevisions").toMap().value("description").toInt(), 4);
        const auto fields = data.value("fields").toMap();
        QVERIFY(!fields.contains("title"));
        QCOMPARE(fields.value("description").toString(), "From A");
        QCOMPARE(fields.value("genre").toString(), "Drama");
        QCOMPARE(data.value("provenance").toMap().value("description").toMap().value("provider").toString(), "a/details");
        QCOMPARE(data.value("provenance").toMap().value("genre").toMap().value("provider").toString(), "b/details");
    }

    void conflictingEntityCannotWinByProviderRank() {
        FakeBackend fake;
        fake.providers = {registration("a/search"), registration("b/search")};
        AddonMetadataCoordinator coordinator(fake.backend());
        QSignalSpy results(&coordinator, &AddonMetadataCoordinator::finished);
        coordinator.searchAssets("subtitles", context());
        QTRY_COMPARE(fake.calls.size(), 1);
        complete(coordinator, fake.calls[0], "match", assets(asset("bad", identity("tt9999999"))));
        QTRY_COMPARE(fake.calls.size(), 2);
        complete(coordinator, fake.calls[1], "match", assets(asset("good")));
        QTRY_COMPARE(results.size(), 1);
        const auto candidates = results[0][2].toMap().value("candidates").toList();
        QCOMPARE(candidates.size(), 1);
        QCOMPARE(candidates[0].toMap().value("id").toString(), "good");
    }

    void ambiguousRecordsNeverBecomeAnAutomaticPatch() {
        FakeBackend fake;
        fake.providers = {registration("a/details")};
        AddonMetadataCoordinator coordinator(fake.backend());
        QSignalSpy results(&coordinator, &AddonMetadataCoordinator::finished);
        coordinator.enrich("video.details", context(), {"description"});
        QTRY_COMPARE(fake.calls.size(), 1);
        const QVariantList records{claims({{"description", "First"}}), claims({{"description", "Second"}})};
        complete(coordinator, fake.calls[0], "match", {{"records", records}});
        QTRY_COMPARE(results.size(), 1);
        QCOMPARE(results[0][1].toString(), "ambiguous");
        QVERIFY(results[0][2].toMap().value("fields").toMap().isEmpty());
    }

    void explicitAggregationKeepsPreferenceOrderDespiteReplyOrder() {
        FakeBackend fake;
        fake.providers = {registration("a/search"), registration("b/search")};
        AddonMetadataCoordinator coordinator(fake.backend());
        QSignalSpy results(&coordinator, &AddonMetadataCoordinator::finished);
        coordinator.searchAssets("subtitles", context(), true);
        QTRY_COMPARE(fake.calls.size(), 2);
        complete(coordinator, fake.calls[1], "match", assets(asset("from-b")));
        complete(coordinator, fake.calls[0], "match", assets(asset("from-a")));
        QTRY_COMPARE(results.size(), 1);
        const auto candidates = results[0][2].toMap().value("candidates").toList();
        QCOMPARE(candidates.size(), 2);
        QCOMPARE(candidates[0].toMap().value("id").toString(), "from-a");
        QCOMPARE(candidates[1].toMap().value("id").toString(), "from-b");
    }

    void coalescedConsumersRetainIndependentGenerationAndCancellation() {
        FakeBackend fake;
        fake.providers = {registration("a/search")};
        AddonMetadataCoordinator coordinator(fake.backend());
        QSignalSpy results(&coordinator, &AddonMetadataCoordinator::finished);
        const auto first = coordinator.searchAssets("subtitles", context(1));
        const auto second = coordinator.searchAssets("subtitles", context(2));
        QTRY_COMPARE(fake.calls.size(), 1);
        coordinator.cancel(first);
        QVERIFY(fake.canceled.isEmpty());
        complete(coordinator, fake.calls[0], "match", assets());
        QTRY_COMPARE(results.size(), 1);
        QCOMPARE(results[0][0].toString(), second);
        QCOMPARE(results[0][2].toMap().value("contextGeneration").toInt(), 2);
    }

    void lastConsumerCancellationStopsUnderlyingJobAndLateWrites() {
        FakeBackend fake;
        fake.providers = {registration("a/search")};
        AddonMetadataCoordinator coordinator(fake.backend());
        QSignalSpy results(&coordinator, &AddonMetadataCoordinator::finished);
        const auto consumer = coordinator.searchAssets("subtitles", context());
        QTRY_COMPARE(fake.calls.size(), 1);
        coordinator.cancel(consumer);
        QCOMPARE(fake.canceled, QStringList{fake.calls[0].id});
        complete(coordinator, fake.calls[0], "match", assets());
        QTest::qWait(1);
        QVERIFY(results.isEmpty());
    }

    void cacheIsPartitionedByReleaseAccountAndSettingsGeneration() {
        FakeBackend fake;
        fake.providers = {registration("a/search")};
        AddonMetadataCoordinator coordinator(fake.backend());
        QSignalSpy results(&coordinator, &AddonMetadataCoordinator::finished);
        coordinator.searchAssets("subtitles", context(1));
        QTRY_COMPARE(fake.calls.size(), 1);
        complete(coordinator, fake.calls[0], "match", assets());
        QTRY_COMPARE(results.size(), 1);
        coordinator.searchAssets("subtitles", context(2));
        QTRY_COMPARE(results.size(), 2);
        QCOMPARE(fake.calls.size(), 1);
        auto changed = fake.providers[0].toMap();
        changed["cachePartition"] = "release-2:account-2:settings-2";
        fake.providers[0] = changed;
        coordinator.searchAssets("subtitles", context(3));
        QTRY_COMPARE(fake.calls.size(), 2);
    }

    void privateCallerStateNeverEntersProviderContext() {
        FakeBackend fake;
        fake.providers = {registration("a/search")};
        AddonMetadataCoordinator coordinator(fake.backend());
        auto request = context();
        request["url"] = "file:///private/movie.mkv";
        request["settings"] = QVariantMap{{"sensitive", "private"}};
        request["protectedFields"] = QStringList{"title"};
        auto entity = request["identity"].toMap(); entity["url"] = "file:///private/movie.mkv";
        request["identity"] = entity;
        coordinator.searchAssets("subtitles", request);
        QTRY_COMPARE(fake.calls.size(), 1);
        QVERIFY(!fake.calls[0].context.contains("url"));
        QVERIFY(!fake.calls[0].context.contains("settings"));
        QVERIFY(!fake.calls[0].context.contains("protectedFields"));
        QVERIFY(!fake.calls[0].context.value("identity").toMap().contains("url"));
    }

    void retryAfterCreatesABackoffWithoutBecomingNoMatch() {
        FakeBackend fake;
        fake.providers = {registration("a/search")};
        AddonMetadataCoordinator coordinator(fake.backend());
        QSignalSpy results(&coordinator, &AddonMetadataCoordinator::finished);
        coordinator.searchAssets("subtitles", context(1));
        QTRY_COMPARE(fake.calls.size(), 1);
        complete(coordinator, fake.calls[0], "rate_limited", {{"retryAfterSeconds", 120}}, "Try later");
        QTRY_COMPARE(results.size(), 1);
        coordinator.searchAssets("subtitles", context(2));
        QTRY_COMPARE(results.size(), 2);
        QCOMPARE(fake.calls.size(), 1);
        QCOMPARE(results[1][1].toString(), "rate_limited");
        auto changed = fake.providers[0].toMap(); changed["cachePartition"] = "different-account-generation";
        fake.providers[0] = changed;
        coordinator.searchAssets("subtitles", context(3));
        QTRY_COMPARE(fake.calls.size(), 2);
    }

    void missingSetupIsExplicitAndDoesNotInvoke() {
        FakeBackend fake;
        auto unconfigured = registration("a/search"); unconfigured["configured"] = false;
        fake.providers = {unconfigured};
        AddonMetadataCoordinator coordinator(fake.backend());
        QSignalSpy results(&coordinator, &AddonMetadataCoordinator::finished);
        coordinator.searchAssets("subtitles", context());
        QTRY_COMPARE(results.size(), 1);
        QCOMPARE(results[0][1].toString(), "auth_required");
        QVERIFY(fake.calls.isEmpty());
    }

    void synchronousBackendReplyIsCorrelatedAfterInvokeReturns() {
        FakeBackend fake;
        fake.providers = {registration("a/search")};
        AddonMetadataCoordinator coordinator(fake.backend());
        fake.duringInvoke = [&](const FakeBackend::Call& call) { complete(coordinator, call, "match", assets()); };
        QSignalSpy results(&coordinator, &AddonMetadataCoordinator::finished);
        coordinator.searchAssets("subtitles", context());
        QTRY_COMPARE(results.size(), 1);
        QCOMPARE(results[0][1].toString(), "match");
    }

    void manualConflictIsOnlyASuggestionRequiringConfirmation() {
        FakeBackend fake;
        fake.providers = {registration("a/search")};
        AddonMetadataCoordinator coordinator(fake.backend());
        QSignalSpy results(&coordinator, &AddonMetadataCoordinator::finished);
        auto request = context(); request["manual"] = true; request["query"] = "A different movie";
        coordinator.searchAssets("subtitles", request);
        QTRY_COMPARE(fake.calls.size(), 1);
        complete(coordinator, fake.calls[0], "match", assets(asset("other", identity("tt9999999"))));
        QTRY_COMPARE(results.size(), 1);
        const auto candidate = results[0][2].toMap().value("candidates").toList()[0].toMap();
        QVERIFY(candidate.value("hostMatch").toMap().value("requiresConfirmation").toBool());
    }

    void explicitAssetSearchRetainsAmbiguousAlternativesForAChoice() {
        FakeBackend fake;
        fake.providers = {registration("a/search")};
        AddonMetadataCoordinator coordinator(fake.backend());
        QSignalSpy results(&coordinator, &AddonMetadataCoordinator::finished);
        auto request = context(); request["interactive"] = true;
        coordinator.searchAssets("subtitles", request);
        QTRY_COMPARE(fake.calls.size(), 1);
        complete(coordinator, fake.calls[0], "ambiguous", assets());
        QTRY_COMPARE(results.size(), 1);
        const auto candidates = results[0][2].toMap().value("candidates").toList();
        QCOMPARE(candidates.size(), 1);
        QVERIFY(candidates[0].toMap().value("hostMatch").toMap().value("requiresConfirmation").toBool());
    }
    void oversizedCandidatesAreNotRetainedOrCached() {
        FakeBackend fake;
        fake.providers = {registration("a/search")};
        AddonMetadataCoordinator coordinator(fake.backend());
        QSignalSpy results(&coordinator, &AddonMetadataCoordinator::finished);
        coordinator.searchAssets("subtitles", context()); QTRY_COMPARE(fake.calls.size(), 1);
        auto oversized = asset(); oversized["unexpectedPayload"] = QString(9000, QLatin1Char('x'));
        complete(coordinator, fake.calls[0], "match", assets(oversized)); QTRY_COMPARE(results.size(), 1);
        QCOMPARE(results[0][1].toString(), "error");
        QVERIFY(results[0][2].toMap().value("candidates").toList().isEmpty());
        coordinator.searchAssets("subtitles", context()); QTRY_COMPARE(fake.calls.size(), 2);
    }

    void sealedNativeArtworkRetainsOnlyItsMissingArtworkFeature()
    {
        FakeBackend fake; fake.providers = {registration("hakone.builtin.lastfm/artwork")};
        auto backend = fake.backend(); int validations = 0;
        backend.requiresNativeScope = [](const QString&) { return true; };
        backend.invokeNative = [&](const QString& provider, const QString& operation,
                                   const QVariantMap& projected, const QString& scope) {
            if (scope != QLatin1String("exact-native-anchor")) {
                QTest::qFail("The native anchor does not match.", __FILE__, __LINE__); return QString{};
            }
            const auto id = QStringLiteral("native-call");
            fake.calls.append({id, provider, operation, projected}); return id;
        };
        backend.validateNativeArtwork = [&](const QString& provider, const QVariantMap& data,
                                             const QString& scope) -> std::optional<AddonMetadataCoordinator::NativeArtwork> {
            ++validations;
            if (provider != QLatin1String("hakone.builtin.lastfm/artwork")
                || scope != QLatin1String("exact-native-anchor") || data.value("claim") != QLatin1String("host-reference")) return {};
            return AddonMetadataCoordinator::NativeArtwork{QVariantMap{{"nativeClaim", "host-reference"}},
                "lastfm", {{"label", "Last.fm"}, {"url", "https://www.last.fm/"}}};
        };
        AddonMetadataCoordinator owner(std::move(backend)); QSignalSpy results(&owner, &AddonMetadataCoordinator::finished);
        owner.enrichNative("artwork", context(), {"artwork", "description"}, "exact-native-anchor");
        QTRY_COMPARE(fake.calls.size(), 1);
        complete(owner, fake.calls.first(), "match", {{"claim", "host-reference"}, {"fields", QVariantMap{{"description", "Untrusted field"}}}});
        QTRY_COMPARE(results.size(), 1); QCOMPARE(validations, 1);
        const auto data = results.first().at(2).toMap();
        QCOMPARE(data.value("fields").toMap(), QVariantMap({{"artwork", QVariantMap{{"nativeClaim", "host-reference"}}}}));
        const auto proof = data.value("provenance").toMap().value("artwork").toMap();
        QCOMPARE(proof.value("nativeFamily").toString(), QStringLiteral("lastfm"));
        QCOMPARE(proof.value("compatibilityPolicy").toString(), QStringLiteral("native-family-artwork@1"));
        QCOMPARE(proof.value("matchEvidence").toString(), QStringLiteral("retained-native-compatibility"));
        QVERIFY(!proof.value("strong").toBool()); QVERIFY(!data.contains("identity"));
    }

    void nativeArtworkCannotReplaceProtectedFieldsOrInventPortableIdentity()
    {
        for (int row = 0; row != 5; ++row) {
            FakeBackend fake; fake.providers = {registration("provider/artwork")}; auto backend = fake.backend();
            const bool native = row != 4;
            backend.requiresNativeScope = [native](const QString&) { return native; };
            backend.invokeNative = [&](const QString& provider, const QString& operation, const QVariantMap& projected, const QString&) {
                const auto id = QStringLiteral("native-call"); fake.calls.append({id, provider, operation, projected}); return id;
            };
            backend.validateNativeArtwork = [row](const QString&, const QVariantMap&, const QString&)
                -> std::optional<AddonMetadataCoordinator::NativeArtwork> {
                if (row == 2) return {};
                return AddonMetadataCoordinator::NativeArtwork{QVariantMap{{"nativeClaim", "host-reference"}}, row == 3 ? "unknown-family" : "lastfm", {}};
            };
            AddonMetadataCoordinator owner(std::move(backend)); QSignalSpy results(&owner, &AddonMetadataCoordinator::finished);
            auto request = context();
            if (row == 0) request.insert("existingFields", QVariantMap{{"artwork", "file:///native/cover.png"}});
            if (row == 1) request.insert("protectedFields", QStringList{"artwork"});
            owner.enrichNative("artwork", request, {"artwork"}, "exact-native-anchor");
            if (row >= 2) { QTRY_COMPARE(fake.calls.size(), 1); complete(owner, fake.calls.first(), "match", {{"nativeClaim", "host-reference"}, {"fields", QVariantMap{{"artwork", "https://images.invalid/art.png"}}}}); }
            QTRY_COMPARE(results.size(), 1); QVERIFY(results.first().at(2).toMap().value("fields").toMap().isEmpty());
            if (row < 2) QCOMPARE(fake.calls.size(), 0);
        }
    }

    void aProviderPlanCannotAddAuthorityOrRetryAnExcludedFamily()
    {
        FakeBackend fake; fake.providers = {registration("portable/artwork"), registration("native/artwork")};
        auto backend = fake.backend(); backend.requiresNativeScope = [](const QString& id) { return id.startsWith("native/"); };
        backend.invokeNative = [&](const QString& provider, const QString& operation, const QVariantMap& projected, const QString&) {
            const auto id = QStringLiteral("native-call"); fake.calls.append({id, provider, operation, projected}); return id;
        };
        backend.planProviders = [](const QString&, const QString&, const QVariantList& rows, bool) {
            auto changed = rows.first().toMap(); changed.insert("cachePartition", "forged-partition");
            return QVariantList{registration("unknown/artwork"), changed, changed};
        };
        AddonMetadataCoordinator owner(std::move(backend)); QSignalSpy results(&owner, &AddonMetadataCoordinator::finished);
        owner.enrichNative("artwork", context(), {"artwork"}, "native-anchor"); QTRY_COMPARE(fake.calls.size(), 1);
        complete(owner, fake.calls.first(), "no_match"); QTRY_COMPARE(results.size(), 1);
        QCOMPARE(fake.calls.size(), 1); QCOMPARE(fake.calls.first().provider, QStringLiteral("portable/artwork"));
    }

    void aProviderPlanCanDeleteItsOwnerBeforeDispatch()
    {
        FakeBackend fake; fake.providers = {registration("portable/artwork")};
        auto backend = fake.backend(); AddonMetadataCoordinator* owner = nullptr;
        auto captured = std::make_shared<int>(1); const std::weak_ptr<int> weak(captured); bool heldDuringReturn = false;
        backend.planProviders = [&, captured](const QString&, const QString&, const QVariantList& rows, bool) {
            delete std::exchange(owner, nullptr); heldDuringReturn = !weak.expired(); return rows;
        };
        owner = new AddonMetadataCoordinator(std::move(backend)); captured.reset();
        const auto request = owner->enrich("artwork", context(), {"artwork"});
        QVERIFY(!owner); QVERIFY(!request.isEmpty()); QVERIFY(heldDuringReturn); QVERIFY(weak.expired());
        QTest::qWait(1); QCOMPARE(fake.calls.size(), 0);
    }

    void artworkAssetSearchRetainsLabelEvidenceForUserSelection() {
        FakeBackend fake; fake.providers = {registration("labels/art")};
        AddonMetadataCoordinator coordinator(fake.backend()); QSignalSpy results(&coordinator, &AddonMetadataCoordinator::finished);
        coordinator.searchAssets("artwork", context()); QTRY_COMPARE(fake.calls.size(), 1);
        auto candidate = asset(); candidate["evidence"] = QVariantMap{{"type", "label"}};
        complete(coordinator, fake.calls[0], "match", assets(candidate)); QTRY_COMPARE(results.size(), 1);
        const auto result = results[0][2].toMap().value("candidates").toList().first().toMap();
        QVERIFY(result.value("hostMatch").toMap().value("requiresConfirmation").toBool());
    }

    void artworkLabelFallbackWaitsForStrongProviders() {
        FakeBackend fake; fake.providers = {registration("labels/art"), registration("strong/art")};
        AddonMetadataCoordinator coordinator(fake.backend()); QSignalSpy results(&coordinator, &AddonMetadataCoordinator::finished);
        coordinator.enrich("artwork", context(), {"artwork"}); QTRY_COMPARE(fake.calls.size(), 1);
        auto record = claims({{"artwork", QVariantMap{{"url", "https://images.invalid/label.jpg"}}}});
        record["evidence"] = QVariantMap{{"type", "label"}};
        complete(coordinator, fake.calls[0], "match", record); QTRY_COMPARE(fake.calls.size(), 2); QVERIFY(results.isEmpty());
        complete(coordinator, fake.calls[1], "match", claims({{"artwork", QVariantMap{{"url", "https://images.invalid/strong.jpg"}}}}));
        QTRY_COMPARE(results.size(), 1);
        QCOMPARE(results[0][2].toMap().value("fields").toMap().value("artwork").toMap().value("url").toString(), "https://images.invalid/strong.jpg");
    }

    void artworkLabelNeedsEveryHostArtistAndAlbumField() {
        for (const auto& variation : {QStringLiteral("correct"), QStringLiteral("artist"), QStringLiteral("album"), QStringLiteral("missingAlbum")}) {
            FakeBackend fake; fake.providers = {registration("labels/art")};
            AddonMetadataCoordinator coordinator(fake.backend()); QSignalSpy results(&coordinator, &AddonMetadataCoordinator::finished);
            const QVariantMap target{{"kind", "movie"}, {"title", "Example Movie"}, {"artist", "Artist A"}, {"album", "Album A"}};
            auto candidate = target;
            if (variation == QStringLiteral("artist")) candidate["artist"] = "Artist B";
            if (variation == QStringLiteral("album")) candidate["album"] = "Album B";
            if (variation == QStringLiteral("missingAlbum")) candidate.remove("album");
            coordinator.enrich("artwork", {{"identity", target}}, {"artwork", "description"}); QTRY_COMPARE(fake.calls.size(), 1);
            auto record = claims({{"artwork", QVariantMap{{"url", "https://images.invalid/label.jpg"}}}, {"description", "Do not apply"}}, candidate);
            record["evidence"] = QVariantMap{{"type", "label"}};
            complete(coordinator, fake.calls[0], "match", record); QTRY_COMPARE(results.size(), 1);
            const auto data = results[0][2].toMap();
            QCOMPARE(data.value("fields").toMap().contains("artwork"), variation == QStringLiteral("correct"));
            QVERIFY(!data.value("fields").toMap().contains("description"));
            if (variation == QStringLiteral("correct")) QCOMPARE(data.value("provenance").toMap().value("artwork").toMap().value("matchEvidence").toString(), "label");
        }
    }

    void artworkEnrichmentCannotReplaceExistingOrUserArtwork() {
        for (const bool user : {false, true}) {
            FakeBackend fake; fake.providers = {registration("labels/art")};
            AddonMetadataCoordinator coordinator(fake.backend()); QSignalSpy results(&coordinator, &AddonMetadataCoordinator::finished);
            auto request = context(); request["refresh"] = true;
            if (user) request["userFields"] = QVariantMap{{"artwork", "User image"}};
            else request["existingFields"] = QVariantMap{{"artwork", "file:///private/embedded-cover.jpg"}};
            coordinator.enrich("artwork", request, {"artwork"}); QTRY_COMPARE(results.size(), 1);
            QVERIFY(fake.calls.isEmpty()); QVERIFY(results[0][2].toMap().value("fields").toMap().isEmpty());
        }
    }

    void labelArtworkUsesThePreferredProviderAndPreservesItsAmbiguity() {
        for (const bool ambiguous : {false, true}) {
            FakeBackend fake; fake.providers = {registration("a/art"), registration("b/art")};
            for (auto& provider : fake.providers) {
                auto descriptor = provider.toMap(); descriptor["mediaKinds"] = QStringList{"unknown"}; provider = descriptor;
            }
            AddonMetadataCoordinator coordinator(fake.backend()); QSignalSpy results(&coordinator, &AddonMetadataCoordinator::finished);
            const QVariantMap target{{"kind", "unknown"}, {"title", "Channel One"}};
            coordinator.enrich("artwork", {{"identity", target}}, {"artwork"}); QTRY_COMPARE(fake.calls.size(), 1);
            for (int provider = 0; provider < 2; ++provider) {
                QVariantList records;
                const int count = provider == 0 && ambiguous ? 2 : 1;
                for (int i = 0; i < count; ++i) {
                    auto record = claims({{"artwork", QVariantMap{{"url", QStringLiteral("https://images.invalid/%1-%2.jpg").arg(provider).arg(i)}}}}, target);
                    record["evidence"] = QVariantMap{{"type", "label"}}; records.append(record);
                }
                complete(coordinator, fake.calls[provider], "match", {{"records", records}});
                if (!provider) QTRY_COMPARE(fake.calls.size(), 2);
            }
            QTRY_COMPARE(results.size(), 1);
            const auto data = results[0][2].toMap();
            if (ambiguous) {
                QCOMPARE(results[0][1].toString(), "ambiguous"); QVERIFY(data.value("fields").toMap().isEmpty());
                const auto choices = data.value("artworkCandidates").toList(); QCOMPARE(choices.size(), 2);
                for (const auto& choice : choices) QCOMPARE(choice.toMap().value("provenance").toMap().value("provider").toString(), "a/art");
            } else {
                QCOMPARE(results[0][1].toString(), "match");
                QCOMPARE(data.value("fields").toMap().value("artwork").toMap().value("url").toString(), "https://images.invalid/0-0.jpg");
                QCOMPARE(data.value("provenance").toMap().value("artwork").toMap().value("provider").toString(), "a/art");
            }
        }
    }

    void laterProvidersCannotSelectAnEarlierAmbiguousEntity() {
        for (const bool label : {false, true}) {
            FakeBackend fake; fake.providers = {registration("a/art"), registration("b/art")};
            AddonMetadataCoordinator coordinator(fake.backend()); QSignalSpy results(&coordinator, &AddonMetadataCoordinator::finished);
            coordinator.enrich("artwork", context(), {"artwork"}); QTRY_COMPARE(fake.calls.size(), 1);
            QVariantList first;
            for (int i = 0; i < 2; ++i) first.append(claims({{"artwork", QVariantMap{{"url", QStringLiteral("https://images.invalid/first-%1.jpg").arg(i)}}}}));
            complete(coordinator, fake.calls[0], "match", {{"records", first}}); QTRY_COMPARE(fake.calls.size(), 2);
            auto later = claims({{"artwork", QVariantMap{{"url", "https://images.invalid/later.jpg"}}}});
            if (label) later["evidence"] = QVariantMap{{"type", "label"}};
            complete(coordinator, fake.calls[1], "match", later); QTRY_COMPARE(results.size(), 1);
            QCOMPARE(results[0][1].toString(), "ambiguous");
            const auto data = results[0][2].toMap(); QVERIFY(data.value("fields").toMap().isEmpty());
            const auto choices = data.value("artworkCandidates").toList(); QCOMPARE(choices.size(), 2);
            for (const auto& choice : choices) QCOMPARE(choice.toMap().value("provenance").toMap().value("provider").toString(), "a/art");
        }
    }

    void fieldProvenanceRetainsOnlyBoundedHttpsCredit() {
        for (const auto& url : {QStringLiteral("https://www.tvmaze.com/episodes/1"), QStringLiteral("file:///private/key"),
                QStringLiteral("https://user:password@example.org/source"), QString(3000, QLatin1Char('x'))}) {
            FakeBackend fake; fake.providers = {registration("a/details")};
            AddonMetadataCoordinator coordinator(fake.backend()); QSignalSpy results(&coordinator, &AddonMetadataCoordinator::finished);
            coordinator.enrich("video.details", context(), {"description"}); QTRY_COMPARE(fake.calls.size(), 1);
            auto record = claims({{"description", "Provider text"}});
            record["attribution"] = QVariantMap{{"name", "TVmaze"}, {"url", url}, {"license", "CC-BY-SA-4.0"}, {"extra", "drop"}};
            complete(coordinator, fake.calls[0], "match", record); QTRY_COMPARE(results.size(), 1);
            const auto provenance = results[0][2].toMap().value("provenance").toMap().value("description").toMap();
            const auto credit = provenance.value("attribution").toMap();
            QCOMPARE(!credit.isEmpty(), url == QStringLiteral("https://www.tvmaze.com/episodes/1"));
            QVERIFY(!credit.contains("extra"));
        }
    }

    void timeoutPreservesPartialSuccessAndCancelsThePendingProvider() {
        FakeBackend fake;
        fake.providers = {registration("a/details"), registration("b/details")};
        AddonMetadataCoordinator coordinator(fake.backend());
        QSignalSpy results(&coordinator, &AddonMetadataCoordinator::finished);
        auto request = context(); request["timeoutMs"] = 100;
        coordinator.enrich("video.details", request, {"description", "genre"});
        QTRY_COMPARE(fake.calls.size(), 1);
        complete(coordinator, fake.calls[0], "partial", claims({{"description", "Keep this"}}));
        QTRY_COMPARE(fake.calls.size(), 2);
        QTRY_COMPARE(results.size(), 1);
        QCOMPARE(results[0][1].toString(), "partial");
        QCOMPARE(results[0][2].toMap().value("fields").toMap().value("description").toString(), "Keep this");
        QCOMPARE(fake.canceled, QStringList{fake.calls[1].id});
    }
};

QTEST_GUILESS_MAIN(TestAddonMetadata)
#include "test_addon_metadata.moc"
