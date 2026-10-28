// SPDX-License-Identifier: GPL-2.0-or-later
#include <QtTest>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <atomic>
#include "addons/addon_artwork_attempt_plan.hpp"
#include "addons/addon_metadata_family_plan.hpp"
#include "addons/addon_package_configuration.hpp"
using namespace vlc::addons;
using Plan = ArtworkAttemptPlan;
namespace {
const QString secretReference = QStringLiteral("f6d7de84-9c62-4531-b28b-978f7e4ef956");
MetadataCataloguePackage portable(QString id, bool enabled = true)
{
    MetadataCataloguePackage row;
    row.package.manifest.id = id;
    row.package.manifest.version = QStringLiteral("1.0.0");
    row.package.identity.source = QStringLiteral("https://github.com/hakone-org/test-addon.git");
    row.package.digest = QByteArray(64, 'a'); row.package.generation = 2;
    row.package.enabled = enabled; row.savedEnabled = enabled;
    row.package.manifest.requiredPermissions.insert(QStringLiteral("metadata.context"),
        QJsonObject{{QStringLiteral("fields"), QJsonArray{QStringLiteral("kind"), QStringLiteral("title"), QStringLiteral("artist")}}});
    row.package.grants = row.package.manifest.requiredPermissions;
    row.settingsRevision = 3;
    row.schema = {{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), QJsonObject{}}};
    Contribution declaration;
    declaration.id = QStringLiteral("artwork"); declaration.type = QStringLiteral("metadata.provider");
    declaration.contract = QStringLiteral("metadata@1"); declaration.features = {QStringLiteral("artwork")};
    declaration.operations = {QStringLiteral("enrich")};
    declaration.document = {{QStringLiteral("id"), declaration.id}, {QStringLiteral("type"), declaration.type},
        {QStringLiteral("contract"), declaration.contract}, {QStringLiteral("features"), QJsonArray{QStringLiteral("artwork")}},
        {QStringLiteral("operations"), QJsonArray{QStringLiteral("enrich")}}};
    row.contributions = {declaration}; row.package.manifest.contributions = row.contributions;
    row.package.manifest.document = {{QStringLiteral("id"), id}, {QStringLiteral("version"), QStringLiteral("1.0.0")},
        {QStringLiteral("contributions"), QJsonArray{declaration.document}}};
    return row;
}
void protect(MetadataCataloguePackage& row, bool reference = true)
{
    const QJsonObject binding{{QStringLiteral("field"), QStringLiteral("apiKey")},
        {QStringLiteral("origin"), QStringLiteral("https://example.org")}, {QStringLiteral("placement"), QStringLiteral("header")},
        {QStringLiteral("name"), QStringLiteral("X-Api-Key")}};
    row.package.manifest.requiredPermissions.insert(QStringLiteral("credentials.use"), QJsonObject{{QStringLiteral("bindings"), QJsonArray{binding}}});
    row.package.grants = row.package.manifest.requiredPermissions;
    row.schema.insert(QStringLiteral("properties"), QJsonObject{{QStringLiteral("apiKey"), QJsonObject{
        {QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("x-hakone-secret"), true}}}});
    row.schema.insert(QStringLiteral("required"), QJsonArray{QStringLiteral("apiKey")});
    if (reference) row.settings.insert(QStringLiteral("_credentials"), QJsonObject{{QStringLiteral("apiKey"), secretReference}});
}
ArtworkBuiltinRegistration native(const AddonMetadataFamilyPlan::Binding& binding)
{
    ArtworkBuiltinRegistration result;
    result.descriptor = {{QStringLiteral("id"), binding.nativeContribution}, {QStringLiteral("packageId"), binding.nativeContribution.section(QLatin1Char('/'), 0, 0)},
        {QStringLiteral("features"), QJsonArray{QStringLiteral("artwork")}}, {QStringLiteral("operations"), QJsonArray{QStringLiteral("enrich")}},
        {QStringLiteral("enabled"), true}, {QStringLiteral("configured"), true},
        {QStringLiteral("cachePartition"), QString(QStringLiteral("native:test:") + binding.family)}};
    result.binding = QStringLiteral("registration:") + binding.family;
    result.available = true;
    return result;
}
bool distributionCandidate(const QString& directory, const QString& localId, const QString& version,
                           PackageCandidate* output, QString* error)
{
    const QJsonObject declaration{{QStringLiteral("id"), localId}, {QStringLiteral("type"), QStringLiteral("metadata.provider")},
        {QStringLiteral("contract"), QStringLiteral("metadata@1")}, {QStringLiteral("features"), QJsonArray{QStringLiteral("artwork")}},
        {QStringLiteral("operations"), QJsonArray{QStringLiteral("enrich")}}};
    const auto source = QStringLiteral("https://github.com/hakone-org/musicbrainz");
    const QJsonObject manifest{{QStringLiteral("formatVersion"), 1}, {QStringLiteral("id"), QStringLiteral("org.videolan.musicbrainz")},
        {QStringLiteral("name"), QStringLiteral("Artwork fixture")}, {QStringLiteral("version"), version},
        {QStringLiteral("publisher"), QStringLiteral("Tests")}, {QStringLiteral("license"), QStringLiteral("GPL-2.0-or-later")},
        {QStringLiteral("source"), source}, {QStringLiteral("api"), QJsonObject{
            {QStringLiteral("addon"), QStringLiteral(">=1.0.0 <2.0.0")},
            {QStringLiteral("contracts"), QJsonObject{{QStringLiteral("metadata"), QStringLiteral("^1.0.0")}}}}},
        {QStringLiteral("runtime"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("wasm-message-v1")}, {QStringLiteral("entry"), QStringLiteral("worker.wasm")}}},
        {QStringLiteral("files"), QJsonArray{QStringLiteral("addon.json"), QStringLiteral("LICENSE"), QStringLiteral("worker.wasm")}},
        {QStringLiteral("contributions"), QJsonArray{declaration}}};
    const auto write = [&](const QString& name, const QByteArray& data) {
        QFile file(QDir(directory).filePath(name));
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    };
    if (!write(QStringLiteral("addon.json"), QJsonDocument(manifest).toJson(QJsonDocument::Compact))
        || !write(QStringLiteral("LICENSE"), QByteArray("fixture license\n"))
        || !write(QStringLiteral("worker.wasm"), QByteArray::fromHex("0061736d01000000"))) return false;
    ReleaseIdentity identity; identity.source = source; identity.tag = QStringLiteral("v") + version; identity.distributionSource = true;
    return PackageStore::inspectDirectory(directory, identity, output, error);
}
struct Fixture {
    QTemporaryDir directory;
    StoreAuthority authority{directory.path()};
    MetadataCatalogueSnapshot catalogue;
    QList<ArtworkBuiltinRegistration> builtins;
    std::shared_ptr<std::atomic<qint64>> clock = std::make_shared<std::atomic<qint64>>(1000000);
    std::shared_ptr<std::atomic<bool>> live = std::make_shared<std::atomic<bool>>(true);
    Fixture() {
        catalogue.queuedAuthority = authority.capture(); catalogue.authority = authority.capture();
        catalogue.digest = QByteArray(32, 'd'); catalogue.retainedBytes = 256;
    }
    void allFamilies(bool portableToo = false) {
        for (const auto& binding : AddonMetadataFamilyPlan::bindings()) {
            builtins.append(native(binding));
            if (portableToo) catalogue.packages.append(portable(binding.portableContribution.section(QLatin1Char('/'), 0, 0)));
        }
    }
    Plan::Capture input() {
        catalogue.candidateCount = catalogue.packages.size(); catalogue.rawContributionCount = 0;
        for (const auto& row : catalogue.packages) catalogue.rawContributionCount += row.contributions.size();
        Plan::Capture value;
        value.catalogue = [snapshot = catalogue](MetadataCatalogueSnapshot* output, QString*) { *output = snapshot; return true; };
        value.builtins = builtins; value.registryIdentity = QByteArray(32, 'r'); value.subject = QByteArray(32, 's');
        value.privateScope = QStringLiteral("private-child-1"); value.deadline = 16000000;
        value.clock = [clock = clock] { return clock->load(); }; value.live = [live = live] { return live->load(); };
        value.workerAvailable = true;
        value.requestContext = {{QStringLiteral("identity"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("musicTrack")},
            {QStringLiteral("title"), QStringLiteral("Title")}, {QStringLiteral("artist"), QStringLiteral("Artist")}}},
            {QStringLiteral("media"), QStringLiteral("private-media-1")}, {QStringLiteral("language"), QStringLiteral("en")}};
        return value;
    }
    std::shared_ptr<const Plan> capture(QString* error = nullptr) { return Plan::capture(input(), error); }
};
QStringList sources(const std::shared_ptr<const Plan>& plan)
{ QStringList result; if (plan) for (const auto& slot : plan->attempts) result.append(slot.contribution); return result; }
const Plan::Decision* decision(const std::shared_ptr<const Plan>& plan, const QString& id)
{ if (plan) for (const auto& row : plan->rawDecisions) if (row.contribution == id) return &row; return nullptr; }
}
class TestArtworkAttemptPlan : public QObject {
    Q_OBJECT
private slots:
    void nativeOrderAndFamilyPeers() {
        Fixture f; f.allFamilies(true); auto plan = f.capture(); QVERIFY(plan); QVERIFY(plan->current()); QVERIFY(!plan->hasBlockedSlot());
        QCOMPARE(plan->attempts.size(), 4); QCOMPARE(plan->rawDecisions.size(), 8);
        for (qsizetype i = 0; i < 4; ++i) {
            const auto& binding = AddonMetadataFamilyPlan::bindings().at(i);
            QCOMPARE(plan->attempts.at(i).contribution, binding.nativeContribution); QCOMPARE(plan->attempts.at(i).ordinal, unsigned(i));
            QCOMPARE(decision(plan, binding.portableContribution)->disposition, Plan::Disposition::FamilyPeer);
        }
    }
    void unconfiguredPreferenceBlocksItsFamily() {
        Fixture f; f.allFamilies(true); protect(f.catalogue.packages[0], false);
        const auto id = AddonMetadataFamilyPlan::bindings().first().portableContribution;
        f.catalogue.providerPreferences = {{QStringLiteral("artwork"), QJsonArray{id}}};
        auto plan = f.capture(); QVERIFY(plan); QCOMPARE(plan->attempts.first().contribution, id); QVERIFY(plan->attempts.first().blocked);
        QVERIFY(!sources(plan).contains(AddonMetadataFamilyPlan::bindings().first().nativeContribution));
    }
    void unavailableNativeDoesNotRetryPeer() {
        Fixture f; f.allFamilies(true); f.builtins[0].available = false;
        auto plan = f.capture(); QVERIFY(plan); QVERIFY(plan->attempts.first().blocked);
        QCOMPARE(plan->attempts.first().implementation, Plan::Implementation::Native);
        QCOMPARE(plan->attempts.size(), 4);
    }
    void unsupportedPreferenceIsBlocking() {
        Fixture f; f.allFamilies(true); auto& declaration = f.catalogue.packages[0].contributions[0];
        declaration.operations = {QStringLiteral("identify")};
        f.catalogue.providerPreferences = {{QStringLiteral("artwork"), QJsonArray{AddonMetadataFamilyPlan::bindings().first().portableContribution}}};
        auto plan = f.capture(); QVERIFY(plan); QVERIFY(plan->attempts.first().blocked);
        QCOMPARE(plan->attempts.first().implementation, Plan::Implementation::Portable);
    }
    void missingPreferenceBlocksNativePeer() {
        Fixture f; f.allFamilies();
        const auto id = AddonMetadataFamilyPlan::bindings().first().portableContribution;
        f.catalogue.providerPreferences = {{QStringLiteral("artwork"), QJsonArray{id}}};
        auto plan = f.capture(); QVERIFY(plan); QCOMPARE(plan->attempts.first().contribution, id);
        QCOMPARE(plan->attempts.first().implementation, Plan::Implementation::Missing); QVERIFY(plan->attempts.first().blocked);
        QCOMPARE(plan->attempts.size(), 4);
    }
    void disabledPreferenceIsRetainedAndExcluded() {
        Fixture f; f.allFamilies(true); auto& row = f.catalogue.packages[0]; row.package.enabled = false; row.savedEnabled = false;
        const auto& binding = AddonMetadataFamilyPlan::bindings().first();
        f.catalogue.providerPreferences = {{QStringLiteral("artwork"), QJsonArray{binding.portableContribution}}};
        auto plan = f.capture(); QVERIFY(plan); QCOMPARE(plan->attempts.first().contribution, binding.nativeContribution);
        QCOMPARE(decision(plan, binding.portableContribution)->disposition, Plan::Disposition::Disabled);
    }
    void disabledNativeUsesPortableAtFamilyPosition() {
        Fixture f; f.allFamilies(true); const auto& binding = AddonMetadataFamilyPlan::bindings().first();
        f.catalogue.providerPreferences = {{QStringLiteral("_builtinEnabled"), QJsonObject{{binding.nativeContribution.section(QLatin1Char('/'), 0, 0), false}}}};
        auto plan = f.capture(); QVERIFY(plan); QCOMPARE(plan->attempts.first().contribution, binding.portableContribution);
        QCOMPARE(plan->attempts.at(1).family, QStringLiteral("google-images"));
    }
    void absentNativeUsesPortableAtFamilyPosition() {
        Fixture f; f.allFamilies(true); f.builtins.removeFirst();
        auto plan = f.capture(); QVERIFY(plan); QCOMPARE(plan->attempts.first().family, QStringLiteral("musicbrainz"));
        QCOMPARE(plan->attempts.first().implementation, Plan::Implementation::Portable);
    }
    void scopedSavedOrderWins() {
        Fixture f; f.allFamilies(true); const auto& bindings = AddonMetadataFamilyPlan::bindings();
        f.catalogue.providerPreferences = {{QStringLiteral("artwork"), QJsonArray{bindings.at(3).nativeContribution}},
            {QStringLiteral("artwork/musicTrack"), QJsonArray{bindings.at(2).nativeContribution}},
            {QStringLiteral("artwork/musicTrack/en"), QJsonArray{bindings.at(1).portableContribution}}};
        auto plan = f.capture(); QVERIFY(plan); QCOMPARE(plan->attempts.first().contribution, bindings.at(1).portableContribution);
        QCOMPARE(plan->attempts.at(1).family, QStringLiteral("musicbrainz"));
    }
    void sameFamilyPreferencesChooseFirstOnce() {
        Fixture f; f.allFamilies(true); const auto& binding = AddonMetadataFamilyPlan::bindings().first();
        f.catalogue.providerPreferences = {{QStringLiteral("artwork"), QJsonArray{binding.portableContribution, binding.nativeContribution}}};
        auto plan = f.capture(); QVERIFY(plan); QCOMPARE(plan->attempts.size(), 4); QCOMPARE(plan->attempts.first().contribution, binding.portableContribution);
        QVERIFY(!sources(plan).contains(binding.nativeContribution));
    }
    void omittedDescriptorChangesBothDigests() {
        Fixture f; auto row = portable(QStringLiteral("org.hakone.other"));
        row.contributions[0].features = {QStringLiteral("lyrics")};
        f.catalogue.packages.append(row); f.allFamilies(); auto first = f.capture(); QVERIFY(first);
        QCOMPARE(decision(first, QStringLiteral("org.hakone.other/artwork"))->disposition, Plan::Disposition::OtherFeature);
        f.catalogue.packages[0].contributions[0].document.insert(QStringLiteral("label"), QStringLiteral("Changed"));
        auto second = f.capture(); QVERIFY(second); QVERIFY(first->rawCatalogueDigest != second->rawCatalogueDigest);
        QVERIFY(first->planDigest != second->planDigest);
    }
    void duplicateNativeRefuses() {
        Fixture f; f.allFamilies(); f.builtins.append(f.builtins.first()); QString error;
        QVERIFY(!f.capture(&error)); QVERIFY(!error.isEmpty());
    }
    void selectedLimit_data() {
        QTest::addColumn<int>("count"); QTest::addColumn<bool>("accepted");
        QTest::newRow("zero") << 0 << true; QTest::newRow("sixteen") << 16 << true; QTest::newRow("seventeen") << 17 << false;
    }
    void selectedLimit() {
        QFETCH(int, count); QFETCH(bool, accepted); Fixture f;
        for (int i = 0; i < count; ++i) f.catalogue.packages.append(portable(QStringLiteral("org.hakone.provider%1").arg(i)));
        auto plan = f.capture(); QCOMPARE(bool(plan), accepted); if (plan) QCOMPARE(plan->attempts.size(), count);
    }
    void rawLimit_data() {
        QTest::addColumn<int>("count"); QTest::addColumn<bool>("accepted");
        QTest::newRow("exact") << 256 << true; QTest::newRow("overflow") << 257 << false;
    }
    void rawLimit() {
        QFETCH(int, count); QFETCH(bool, accepted); Fixture f; auto row = portable(QStringLiteral("org.hakone.theme")); row.contributions.clear();
        for (int i = 0; i < count; ++i) {
            Contribution declaration; declaration.id = QStringLiteral("section%1").arg(i); declaration.type = QStringLiteral("ui.section");
            declaration.document = {{QStringLiteral("id"), declaration.id}, {QStringLiteral("type"), declaration.type}};
            row.contributions.append(declaration);
        }
        f.catalogue.packages.append(row); auto plan = f.capture(); QCOMPARE(bool(plan), accepted);
        if (plan) { QCOMPARE(plan->rawDecisions.size(), count); QVERIFY(plan->attempts.isEmpty()); }
    }
    void combinedRawOverflowRefuses() {
        Fixture f; auto row = portable(QStringLiteral("org.hakone.sections")); row.contributions.clear();
        for (int i = 0; i < 253; ++i) { Contribution c; c.id = QStringLiteral("section%1").arg(i); c.type = QStringLiteral("ui.section"); row.contributions.append(c); }
        f.catalogue.packages.append(row); f.allFamilies(); QVERIFY(!f.capture());
    }
    void projectionsUseExactSharedIntersection() {
        Fixture f; f.catalogue.packages = {portable(QStringLiteral("org.hakone.first")), portable(QStringLiteral("org.hakone.second"))};
        f.catalogue.packages[1].package.grants.insert(QStringLiteral("metadata.context"), QJsonObject{{QStringLiteral("fields"), QJsonArray{QStringLiteral("title")}}});
        auto input = f.input(); input.requestContext.insert(QStringLiteral("url"), QStringLiteral("file:///private/media"));
        input.requestContext.insert(QStringLiteral("existingFields"), QJsonObject{{QStringLiteral("artwork"), QStringLiteral("private")}});
        auto plan = Plan::capture(std::move(input)); QVERIFY(plan); QCOMPARE(plan->attempts.size(), 2);
        const auto& first = plan->attempts.first(); const auto& second = plan->attempts.last();
        QVERIFY(first.projectedContextDigest != second.projectedContextDigest);
        QVERIFY(!first.projectedContext.contains(QStringLiteral("url"))); QVERIFY(!first.projectedContext.contains(QStringLiteral("existingFields")));
        QCOMPARE(second.projectedContext.value(QStringLiteral("identity")).toObject().keys(), QStringList{QStringLiteral("title")});
        QCOMPARE(first.projectedContextDigest, QCryptographicHash::hash(QJsonDocument(first.projectedContext).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256));
        QCOMPARE(first.partition, QString::fromLatin1(QByteArray(64, 'a')) + QStringLiteral(":2:3"));
    }
    void unknownPersistentPresenceUsesOwnedReader() {
        Fixture f; auto row = portable(QStringLiteral("org.hakone.keys")); protect(row); f.catalogue.packages.append(row);
        auto input = f.input(); int calls = 0;
        input.credentialReader = [&](const QString& package, const QString& reference) {
            ++calls; if (package != row.package.manifest.id || reference != secretReference) return QByteArray{};
            return QByteArray("test-secret-bytes");
        };
        auto plan = Plan::capture(std::move(input)); QVERIFY(plan); QCOMPARE(calls, 1); QVERIFY(!plan->hasBlockedSlot());
        const auto& slot = plan->attempts.first(); QVERIFY(slot.accountReference.startsWith(QStringLiteral("account:")));
        QCOMPARE(slot.credentialPresence.value(secretReference), QJsonValue(true));
        const auto expected = packageConfiguration::packageAccountReference(row.package, row.settings, row.schema, [](const QString&) { return true; });
        QCOMPARE(slot.accountReference, expected);
        QVERIFY(!QJsonDocument(slot.settings).toJson().contains("test-secret-bytes"));
    }
    void unknownPresenceWithoutReaderBlocks() {
        Fixture f; auto row = portable(QStringLiteral("org.hakone.keys")); protect(row); f.catalogue.packages.append(row);
        auto plan = f.capture(); QVERIFY(plan); QVERIFY(plan->hasBlockedSlot());
        QVERIFY(plan->attempts.first().credentialPresence.isEmpty());
    }
    void readerIsReleasedAtCaptureEnd() {
        Fixture f; f.catalogue.packages.append(portable(QStringLiteral("org.hakone.public")));
        auto input = f.input(); auto sentinel = std::make_shared<int>(1); std::weak_ptr<int> weak = sentinel;
        input.credentialReader = [sentinel](const QString&, const QString&) { return QByteArray{}; }; sentinel.reset();
        auto plan = Plan::capture(std::move(input)); QVERIFY(plan); QVERIFY(weak.expired());
    }
    void configuredDefaultAndPublicAccountMatchSharedRules() {
        Fixture f; auto row = portable(QStringLiteral("org.hakone.public"));
        row.schema.insert(QStringLiteral("properties"), QJsonObject{{QStringLiteral("language"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("default"), QStringLiteral("en")}}}});
        row.schema.insert(QStringLiteral("required"), QJsonArray{QStringLiteral("language")});
        f.catalogue.packages.append(row); auto plan = f.capture(); QVERIFY(plan); QVERIFY(!plan->hasBlockedSlot());
        QVERIFY(plan->attempts.first().accountReference.startsWith(QStringLiteral("public:")));
        QVERIFY(packageConfiguration::configured(row.settings, row.schema, [](const QString&) { return false; }));
    }
    void storeChangeDuringCredentialReadRefuses() {
        Fixture f; auto row = portable(QStringLiteral("org.hakone.keys")); protect(row); f.catalogue.packages.append(row);
        auto input = f.input(); input.credentialReader = [&](const QString&, const QString&) { f.authority.invalidate(); return QByteArray("secret"); };
        QVERIFY(!Plan::capture(std::move(input)));
    }
    void registryChangeDuringCredentialReadRefuses() {
        Fixture f; auto row = portable(QStringLiteral("org.hakone.keys")); protect(row); f.catalogue.packages.append(row);
        auto input = f.input(); input.credentialReader = [&](const QString&, const QString&) { f.live->store(false); return QByteArray("secret"); };
        QVERIFY(!Plan::capture(std::move(input)));
    }
    void planCurrentDetectsStoreRegistryAndDeadline() {
        Fixture clockFixture; clockFixture.allFamilies(); auto deadlinePlan = clockFixture.capture(); QVERIFY(deadlinePlan); QVERIFY(deadlinePlan->current());
        clockFixture.clock->store(16000000); QVERIFY(!deadlinePlan->current());
        Fixture registryFixture; registryFixture.allFamilies(); auto registryPlan = registryFixture.capture(); QVERIFY(registryPlan);
        registryFixture.live->store(false); QVERIFY(!registryPlan->current());
        Fixture storeFixture; storeFixture.allFamilies(); auto storePlan = storeFixture.capture(); QVERIFY(storePlan);
        storeFixture.authority.invalidate(); QVERIFY(!storePlan->current());
        storeFixture.catalogue.queuedAuthority = storeFixture.authority.capture();
        storeFixture.catalogue.authority = storeFixture.authority.capture();
        auto newPlan = storeFixture.capture(); QVERIFY(newPlan); QVERIFY(newPlan->current()); QVERIFY(!storePlan->current());
    }
    void captureDeadlineIncludesCredentialRead() {
        Fixture f; auto row = portable(QStringLiteral("org.hakone.keys")); protect(row); f.catalogue.packages.append(row);
        auto input = f.input(); input.credentialReader = [&](const QString&, const QString&) { f.clock->store(16000000); return QByteArray("secret"); };
        QVERIFY(!Plan::capture(std::move(input)));
    }
    void invalidIdentityRefuses() {
        Fixture f; auto input = f.input(); input.requestContext.insert(QStringLiteral("identity"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("other")}});
        QVERIFY(!Plan::capture(std::move(input)));
    }
    void malformedNativeDescriptorRefuses() {
        Fixture f; f.allFamilies(); f.builtins[0].descriptor.insert(QStringLiteral("enabled"), QStringLiteral("yes")); QVERIFY(!f.capture());
    }
    void removedPreferenceIsRetainedWithoutAttempt() {
        Fixture f; f.allFamilies(); const auto id = QStringLiteral("org.hakone.removed/artwork");
        f.catalogue.providerPreferences.insert(QStringLiteral("artwork"), QJsonArray{id});
        f.catalogue.distributionPolicy = {{QStringLiteral("removed"), QJsonObject{{QStringLiteral("org.hakone.removed"), true}}}};
        auto plan = f.capture(); QVERIFY(plan); QCOMPARE(plan->attempts.size(), 4); QVERIFY(!plan->hasBlockedSlot());
        QCOMPARE(decision(plan, id)->disposition, Plan::Disposition::RemovedPreference);
    }
    void restoredEnabledPackageWithMissingPreferenceBlocks() {
        Fixture f; f.allFamilies(); PackageStore store(f.directory.path()); QTemporaryDir originalSource, nextSource;
        PackageCandidate original, next; QString error;
        QVERIFY2(distributionCandidate(originalSource.path(), QStringLiteral("artwork"), QStringLiteral("1.0.0"), &original, &error), qPrintable(error));
        QVERIFY2(store.install(original, {}, &error), qPrintable(error));
        const auto id = QStringLiteral("org.videolan.musicbrainz"); const auto preferred = id + QStringLiteral("/artwork");
        QVERIFY2(store.setEnabled(id, true, &error), qPrintable(error));
        QVERIFY2(store.saveProviderOrder(QStringLiteral("artwork"), {preferred}, nullptr, &error), qPrintable(error));
        QVERIFY2(store.setDistributionSuppressed(id, true, &error), qPrintable(error));
        InstalledPackage installed; QVERIFY2(store.installed(id, &installed, &error), qPrintable(error)); QVERIFY(!installed.enabled);
        QVERIFY2(distributionCandidate(nextSource.path(), QStringLiteral("alternate"), QStringLiteral("1.1.0"), &next, &error), qPrintable(error));
        QVERIFY2(store.install(next, {}, &error, true), qPrintable(error));
        QVERIFY2(store.installed(id, &installed, &error), qPrintable(error)); QVERIFY(installed.enabled);
        QVERIFY(!store.distributionSuppressed(id));
        MetadataCatalogueSnapshot snapshot;
        QVERIFY2(store.metadataCatalogueReader()(&snapshot, &error), qPrintable(error));
        QCOMPARE(snapshot.packages.size(), 1); QVERIFY(snapshot.packages.first().package.enabled);
        QVERIFY(snapshot.distributionPolicy.value(QStringLiteral("removed")).toObject().value(id).toBool());
        auto input = f.input(); input.catalogue = store.metadataCatalogueReader();
        auto plan = Plan::capture(std::move(input), &error); QVERIFY2(plan, qPrintable(error));
        QCOMPARE(plan->attempts.first().contribution, preferred); QVERIFY(plan->attempts.first().blocked);
        QCOMPARE(plan->attempts.first().implementation, Plan::Implementation::Missing);
        QVERIFY(!sources(plan).contains(AddonMetadataFamilyPlan::bindings().first().nativeContribution));
        QCOMPARE(decision(plan, preferred)->disposition, Plan::Disposition::SelectedBlocked);
    }
    void distributionMarkerCannotDisableMissingNativePreference() {
        Fixture f; f.allFamilies(true); const auto binding = AddonMetadataFamilyPlan::bindings().first(); f.builtins.removeFirst();
        f.catalogue.providerPreferences.insert(QStringLiteral("artwork"), QJsonArray{binding.nativeContribution});
        f.catalogue.distributionPolicy = {{QStringLiteral("removed"), QJsonObject{{binding.nativeContribution.section(QLatin1Char('/'), 0, 0), true}}}};
        auto plan = f.capture(); QVERIFY(plan); QCOMPARE(plan->attempts.first().contribution, binding.nativeContribution);
        QVERIFY(plan->attempts.first().blocked); QCOMPARE(plan->attempts.first().implementation, Plan::Implementation::Missing);
        QVERIFY(!sources(plan).contains(binding.portableContribution));
    }
    void requiredPermissionRefusalBlocks() {
        Fixture f; auto row = portable(QStringLiteral("org.hakone.permission")); row.contributions[0].requires = {QStringLiteral("network.http")};
        f.catalogue.packages.append(row); auto plan = f.capture(); QVERIFY(plan); QVERIFY(plan->hasBlockedSlot());
    }
    void partitionFactsChangePlanDigest() {
        Fixture f; f.catalogue.packages.append(portable(QStringLiteral("org.hakone.public"))); auto first = f.capture(); QVERIFY(first);
        f.catalogue.packages[0].settingsRevision++; auto second = f.capture(); QVERIFY(second);
        QVERIFY(first->planDigest != second->planDigest); QVERIFY(first->attempts.first().partition != second->attempts.first().partition);
    }
    void safeModeKeepsNativeAndRetainsPortableExclusion() {
        Fixture f; f.allFamilies(true); f.catalogue.safeMode = true;
        for (auto& row : f.catalogue.packages) row.package.enabled = false;
        auto plan = f.capture(); QVERIFY(plan); QCOMPARE(plan->attempts.size(), 4); QVERIFY(!plan->hasBlockedSlot());
        QCOMPARE(decision(plan, AddonMetadataFamilyPlan::bindings().first().portableContribution)->disposition, Plan::Disposition::SafeMode);
    }
    void knownPresenceSeedDoesNotNeedDiskReader() {
        Fixture f; auto row = portable(QStringLiteral("org.hakone.keys")); protect(row); f.catalogue.packages.append(row);
        auto input = f.input(); input.knownPresence[row.package.manifest.id].insert(secretReference, true);
        auto plan = Plan::capture(std::move(input)); QVERIFY(plan); QVERIFY(!plan->hasBlockedSlot());
    }
    void retainedPayloadLimitRefuses() {
        Fixture f; f.catalogue.retainedBytes = MetadataCatalogueSnapshot::MaximumBytes;
        for (int i = 0; i < 16; ++i) f.catalogue.packages.append(portable(QStringLiteral("org.hakone.provider%1").arg(i)));
        auto input = f.input(); input.requestContext.insert(QStringLiteral("query"), QString(100000, QLatin1Char('x')));
        QVERIFY(!Plan::capture(std::move(input)));
    }
    void canonicalNativeCaptureOrder() {
        Fixture f; f.allFamilies(); auto first = f.capture(); QVERIFY(first);
        std::reverse(f.builtins.begin(), f.builtins.end()); auto second = f.capture(); QVERIFY(second);
        QCOMPARE(first->rawCatalogueDigest, second->rawCatalogueDigest); QCOMPARE(first->planDigest, second->planDigest);
    }
    void invalidCapture_data() {
        QTest::addColumn<int>("mode"); for (int i = 0; i < 9; ++i) QTest::newRow(qPrintable(QStringLiteral("case%1").arg(i))) << i;
    }
    void invalidCapture() {
        QFETCH(int, mode); Fixture f; auto input = f.input();
        switch (mode) {
        case 0: input.subject.clear(); break;
        case 1: input.subject = QByteArray(32, '\0'); break;
        case 2: input.privateScope.clear(); break;
        case 3: input.registryIdentity.clear(); break;
        case 4: input.deadline = 32000000; break;
        case 5: input.deadline = 1000000; break;
        case 6: input.requestContext.insert(QStringLiteral("query"), QString(140000, QLatin1Char('x'))); break;
        case 7: input.clock = []() -> qint64 { throw 1; }; break;
        case 8: input.live = []() -> bool { throw 1; }; break;
        }
        QVERIFY(!Plan::capture(std::move(input)));
    }
};
QTEST_GUILESS_MAIN(TestArtworkAttemptPlan)
#include "test_addon_artwork_attempt_plan.moc"
