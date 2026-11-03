// SPDX-License-Identifier: GPL-2.0-or-later
// Test the real Manager/Worker path. The native gate uses no Core layout.
#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QDir>
#include <QThread>
#include <QNetworkProxy>
#include "fixtures/receipt_tls_fixture.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include "addons/addon_manager.hpp"
#include "addons/addon_metadata_attempt.hpp"
#include "addons/addon_metadata_completion_policy.hpp"
using namespace vlc::addons;
namespace {
const QString packageId=QStringLiteral("org.hakone.receipt-fixture");
const QString contributionId=packageId+QStringLiteral("/artwork");
QByteArray hash(const QJsonObject& value) {
    return QCryptographicHash::hash(QJsonDocument(value).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256);
}
struct NativeGate final : AddonMetadataNativeAdmission {
    std::atomic_bool valid{true};
    std::atomic_uint revoked{0};
    std::atomic<long long> expires{0};
    static long long now() noexcept {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    bool current() const noexcept override { const auto end=expires.load(); return valid.load() && (!end || now()<end); }
    void revoke() noexcept override { valid=false; ++revoked; }
};
bool write(const QString& path,const QByteArray& data) {
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data)==data.size();
}
struct Fixture {
    QTemporaryDir package,profile;
    AddonStreamLifetime lifetime;
    QObject hostOwner;
    std::unique_ptr<AddonManager> manager;
    std::optional<AddonNativeAuthority> authority;
    QList<StoreAuthorityWatch> watches;
    unsigned calls=0,cancels=0;
    QList<AddonHostServices::Completion> held;
    QString error;
    bool valid=false;
    explicit Fixture(QJsonObject policy={},std::unique_ptr<QNetworkAccessManager>* network=nullptr,
                     QStringList origins={QStringLiteral("https://8.8.8.8")},bool optionalIdentity=false,
                     const QString& module={},const QString& executable={}) {
        const QJsonArray fields{"kind","title","year","season","episodes","episode","episodeNumbering","ids","externalIds","artist","album","guid","feedUrl"};
        QJsonObject contribution{{"id","artwork"},{"type","metadata.provider"},{"contract","metadata@1"},
            {"features",QJsonArray{"artwork","video.details"}},{"operations",QJsonArray{"enrich","identify"}},
            {"mediaKinds",QJsonArray{"movie","episode","series"}},{"requires",QJsonArray{"metadata.context"}}};
        if(!policy.isEmpty()) contribution.insert("completionPolicy",policy);
        const QJsonObject requiredPermissions{
            {"metadata.context",QJsonObject{{"fields",fields}}},
            {"playback.observe",QJsonObject{{"fields",QJsonArray{"state"}}}},
            {"network.http",QJsonObject{{"origins",QJsonArray::fromStringList(origins)},{"methods",QJsonArray{"GET"}}}}
        };
        QJsonObject manifest{{"formatVersion",1},{"id",packageId},{"name","Receipt fixture"},{"version","1.0.0"},
            {"publisher","Host tests"},{"license","GPL-2.0-or-later"},{"source","https://github.com/hakone-org/addon-receipt-fixture.git"},
            {"api",QJsonObject{{"addon","^1.0.0"},{"contracts",QJsonObject{{"metadata","^1.0.0"},{"settings","^1.0.0"}}}}},
            {"runtime",QJsonObject{{"kind","wasm-message-v1"},{"entry","dist/addon.wasm"}}},
            {"files",QJsonArray{"addon.json","LICENSE","dist/addon.wasm","settings/schema.json"}},
            {"settings",QJsonObject{{"schema","settings/schema.json"},{"version",1}}},
            {"permissions",QJsonObject{{"required",requiredPermissions},{"optional",QJsonObject{}}}},
            {"contributions",QJsonArray{contribution}}};
        if(optionalIdentity) {
            auto permissions=manifest["permissions"].toObject(); auto required=permissions["required"].toObject();
            permissions["optional"]=QJsonObject{{"metadata.context",required.take("metadata.context")}};
            permissions["required"]=required; manifest["permissions"]=permissions;
            contribution["requires"]=QJsonArray{}; manifest["contributions"]=QJsonArray{contribution};
        }
        QDir().mkpath(package.filePath("dist")); QDir().mkpath(package.filePath("settings"));
        const QJsonObject schema{{"type","object"},{"additionalProperties",false},{"properties",QJsonObject{
            {"language",QJsonObject{{"type","string"},{"default","en"}}}}}};
        if(!write(package.filePath("addon.json"),QJsonDocument(manifest).toJson())
            || !write(package.filePath("LICENSE"),"Test fixture.\n")
            || !write(package.filePath("settings/schema.json"),QJsonDocument(schema).toJson())
            || !(module.isEmpty() ? write(package.filePath("dist/addon.wasm"),QByteArray("\0asm\1\0\0\0",8))
                                 : QFile::copy(module,package.filePath("dist/addon.wasm")))) return;
        manager=network ? AddonManager::withNetworkTransport(profile.path(),*network) : std::make_unique<AddonManager>(profile.path());
        if(!manager) return;
        manager->setWorkerExecutable(executable.isEmpty() ? QStringLiteral(HAKONE_ATTEMPT_PROCESS_FIXTURE) : executable);
        if(!manager->installLocalPackage(package.path(),true) || !manager->setEnabled(packageId,true)) {
            error=manager->errorMessage(); return;
        }
        auto reader=manager->nativeAuthorityReader(contributionId);
        std::thread read([&] { authority=reader(); }); read.join();
        valid=authority.has_value();
    }
    ~Fixture() { manager.reset(); held.clear(); lifetime.close(); }
    QVariantMap context(const QString& mode=QStringLiteral("zero"),QVariantMap identity={{"kind","movie"},{"title","Fixture"}}) const {
        return {{"feature","artwork"},{"fields",QVariantList{"artwork"}},{"identity",identity},{"fixture",mode}};
    }
    AddonMetadataAttemptAdmission::Capture capture(const std::shared_ptr<NativeGate>& gate,unsigned ordinal=0) const {
        AddonMetadataAttemptAdmission::Capture result;
        if(!authority) return result;
        const auto& value=*authority;
        result.nativeSubject=QByteArray(32,'n'); result.nativeChildHold=std::make_shared<int>(7); result.ordinal=ordinal;
        result.contribution=contributionId; result.feature="artwork"; result.operation="enrich";
        result.releaseDigest=value.package.digest; result.releaseGeneration=value.package.generation;
        result.settingsRevision=value.settingsRevision; result.accountReference=value.accountReference;
        result.partition=QString::fromLatin1(value.package.digest)+QLatin1Char(':')+QString::number(value.package.generation)
            +QLatin1Char(':')+QString::number(value.settingsRevision);
        result.approvedGrants=value.package.grants; result.requestedFields={QStringLiteral("artwork")};
        result.lifetime=lifetime.handle(); result.admission=gate; result.live=value.live;
        return result;
    }
    std::shared_ptr<AddonMetadataAttemptAdmission> attempt(const std::shared_ptr<NativeGate>& gate,unsigned ordinal=0) {
        if(!authority) return {};
        watches.append(authority->onInvalidated([gate] { gate->revoke(); }));
        return AddonMetadataAttemptAdmission::create(capture(gate,ordinal));
    }
    bool host(const QString& mode) {
        return manager->hostServices()->registerOperation("playback.observe",&hostOwner,
            [this,mode](const auto&,const auto&,auto done) -> AddonHostServices::Cancel {
                const auto number=++calls;
                if(mode=="pending") held.append(std::move(done));
                else if(mode=="fail" || (mode=="retry" && number==1)) done({{},QStringLiteral("The fixture lookup failed.")});
                else done({QJsonObject{{"state","stopped"}}, {}});
                return [this] { ++cancels; };
            });
    }
};
}
class TestAddonMetadataAttempt : public QObject {
    Q_OBJECT
private slots:
    void pureProviderMintsOnlyOneBoundReceipt() {
        Fixture fixture; QVERIFY2(fixture.valid,qPrintable(fixture.error));
        auto gate=std::make_shared<NativeGate>(); auto attempt=fixture.attempt(gate); QVERIFY(attempt);
        const auto context=fixture.context(); const auto capture=fixture.capture(gate);
        QSignalSpy result(fixture.manager.get(),&AddonManager::operationFinished);
        const auto id=fixture.manager->invoke(contributionId,"enrich",context,{},attempt); QVERIFY(!id.isEmpty());
        QTRY_COMPARE(result.size(),1); QCOMPARE(result.first()[2].toString(),QStringLiteral("no_match"));
        auto receipt=attempt->take(); QVERIFY(receipt.has_value()); QCOMPARE(receipt->outcome(),AddonMetadataCompletionReceipt::Outcome::NoMatch);
        QVERIFY(receipt->matches(capture.nativeSubject,0,contributionId,hash(QJsonObject::fromVariantMap(context)),capture.partition));
        QVERIFY(!receipt->matches(QByteArray(32,'x'),0,contributionId,hash(QJsonObject::fromVariantMap(context)),capture.partition));
        QVERIFY(!receipt->matches(capture.nativeSubject,1,contributionId,hash(QJsonObject::fromVariantMap(context)),capture.partition));
        QVERIFY(!receipt->matches(capture.nativeSubject,0,contributionId,hash(QJsonObject::fromVariantMap(context)),QStringLiteral("other")));
        QCOMPARE(receipt->resultDigest(),hash({})); QVERIFY(!attempt->take().has_value());
    }
    void positiveReceiptBindsTheExactResult_data() {
        QTest::addColumn<QString>("mode");
        QTest::newRow("match") << QStringLiteral("positive");
        QTest::newRow("partial") << QStringLiteral("positive-partial");
    }
    void positiveReceiptBindsTheExactResult() {
        QFETCH(QString,mode); Fixture fixture; QVERIFY(fixture.valid);
        auto gate=std::make_shared<NativeGate>(); auto attempt=fixture.attempt(gate); QVERIFY(attempt);
        const QJsonObject record{{"identity",QJsonObject{{"kind","movie"},{"title","Fixture"}}},
            {"fields",QJsonObject{{"artwork",QJsonObject{{"id","opaque-image"}}}}}};
        const QJsonObject data{{"records",QJsonArray{record}}};
        auto context=fixture.context(mode); context["fixtureData"]=data.toVariantMap();
        QSignalSpy result(fixture.manager.get(),&AddonManager::operationFinished);
        fixture.manager->invoke(contributionId,"enrich",context,{},attempt); QTRY_COMPARE(result.size(),1);
        auto receipt=attempt->take(); QVERIFY(receipt);
        QCOMPARE(receipt->outcome(),mode=="positive" ? AddonMetadataCompletionReceipt::Outcome::Match : AddonMetadataCompletionReceipt::Outcome::Partial);
        QVERIFY(receipt->matchesResult(data)); QCOMPARE(receipt->resultDigest(),hash(data));
        auto changed=data; changed["identity"]=QJsonObject{{"title","Other"}}; QVERIFY(!receipt->matchesResult(changed));
        const auto capture=fixture.capture(gate);
        QVERIFY(receipt->matches(capture.nativeSubject,0,contributionId,hash(QJsonObject::fromVariantMap(context)),capture.partition));
        QVERIFY(!attempt->take()); gate->revoke(); QVERIFY(!receipt->matchesResult(data));
    }
    void positiveReceiptRequiresCompleteHostWork_data() {
        QTest::addColumn<QString>("mode"); QTest::addColumn<QString>("host"); QTest::addColumn<bool>("expected");
        QTest::newRow("local result") << QStringLiteral("positive") << QString() << true;
        QTest::newRow("explicit error") << QStringLiteral("positive-error") << QString() << false;
        QTest::newRow("pending call") << QStringLiteral("pending-positive") << QStringLiteral("pending") << false;
        QTest::newRow("complete call") << QStringLiteral("host-positive") << QStringLiteral("success") << true;
        QTest::newRow("failed call") << QStringLiteral("host-positive") << QStringLiteral("fail") << false;
        QTest::newRow("retry does not erase failure") << QStringLiteral("host-positive-retry") << QStringLiteral("retry") << false;
    }
    void positiveReceiptRequiresCompleteHostWork() {
        QFETCH(QString,mode); QFETCH(QString,host); QFETCH(bool,expected);
        Fixture fixture; QVERIFY(fixture.valid); if(!host.isEmpty()) QVERIFY(fixture.host(host));
        auto gate=std::make_shared<NativeGate>(); auto attempt=fixture.attempt(gate); QVERIFY(attempt);
        auto context=fixture.context(mode); context["fixtureData"]=QVariantMap{{"fields",QVariantMap{{"artwork",QVariantMap{{"id","image"}}}}}};
        QSignalSpy result(fixture.manager.get(),&AddonManager::operationFinished);
        fixture.manager->invoke(contributionId,"enrich",context,{},attempt); QTRY_COMPARE(result.size(),1);
        QCOMPARE(attempt->take().has_value(),expected);
    }
    void positiveReceiptBoundsTheResult_data() {
        QTest::addColumn<QJsonObject>("data");
        QTest::newRow("empty") << QJsonObject{};
        QTest::newRow("oversized field") << QJsonObject{{"fields",QString(65537,QLatin1Char('x'))}};
        QJsonObject deep{{"field","value"}}; for(unsigned i=0;i<18;++i) deep=QJsonObject{{"nested",deep}};
        QTest::newRow("depth") << deep;
        QJsonArray many; for(unsigned i=0;i<1025;++i) many.append(int(i));
        QTest::newRow("array count") << QJsonObject{{"records",many}};
    }
    void positiveReceiptBoundsTheResult() {
        QFETCH(QJsonObject,data); Fixture fixture; QVERIFY(fixture.valid);
        auto gate=std::make_shared<NativeGate>(); auto attempt=fixture.attempt(gate); QVERIFY(attempt);
        auto context=fixture.context("positive"); context["fixtureData"]=data.toVariantMap();
        QSignalSpy result(fixture.manager.get(),&AddonManager::operationFinished);
        fixture.manager->invoke(contributionId,"enrich",context,{},attempt); QTRY_COMPARE(result.size(),1);
        QVERIFY(!attempt->take());
    }
    void actualRustWorkerAndCommittedWasmMintAPureNoMatch() {
        Fixture fixture({},nullptr,{QStringLiteral("https://8.8.8.8")},false,
            QStringLiteral(HAKONE_ATTEMPT_FRENCH_TV_WASM),
            QStringLiteral(HAKONE_ATTEMPT_RUST_WORKER));
        QVERIFY2(fixture.valid,qPrintable(fixture.error));
        auto gate=std::make_shared<NativeGate>(); auto attempt=fixture.attempt(gate); QVERIFY(attempt);
        QSignalSpy result(fixture.manager.get(),&AddonManager::operationFinished);
        fixture.manager->invoke(contributionId,"enrich",fixture.context(),{},attempt);
        QTRY_COMPARE_WITH_TIMEOUT(result.size(),1,5000); QCOMPARE(result.first()[2].toString(),QStringLiteral("no_match"));
        QVERIFY(result.first()[3].toMap().isEmpty()); QVERIFY(attempt->take().has_value());
    }
    void ordinaryProjectionUsesOnlyDeclaredAndApprovedIdentityFields_data() {
        QTest::addColumn<bool>("optional");
        QTest::newRow("required grant")<<false; QTest::newRow("optional field is not approved")<<true;
    }
    void ordinaryProjectionUsesOnlyDeclaredAndApprovedIdentityFields() {
        QFETCH(bool,optional); Fixture fixture({},nullptr,{QStringLiteral("https://8.8.8.8")},optional); QVERIFY(fixture.valid);
        auto context=fixture.context("echo"); auto identity=context["identity"].toMap(); identity["artist"]="Private artist";
        identity["url"]="file:///private/media.flac"; context["identity"]=identity;
        QSignalSpy result(fixture.manager.get(),&AddonManager::operationFinished);
        fixture.manager->invoke(contributionId,"enrich",context); QTRY_COMPARE(result.size(),1);
        const auto projected=result.first()[3].toMap().value("identity").toMap();
        QVERIFY(!projected.contains("url"));
        if(optional) QVERIFY(projected.isEmpty());
        else { QCOMPARE(projected.value("title").toString(),QStringLiteral("Fixture")); QCOMPARE(projected.value("artist").toString(),QStringLiteral("Private artist")); }
    }
    void anUnsentOrForgedCompletionCannotMint() {
        Fixture fixture; QVERIFY(fixture.valid);
        auto gate=std::make_shared<NativeGate>(); auto attempt=fixture.attempt(gate); QVERIFY(attempt);
        emit fixture.manager->operationFinished("cached",contributionId,"no_match",{},{});
        QVERIFY(!attempt->take().has_value());
        const auto id=fixture.manager->invoke(contributionId,"enrich",fixture.context("hold"),{},attempt);
        QVERIFY(!id.isEmpty()); QTest::qWait(50);
        emit fixture.manager->operationFinished(id,contributionId,"no_match",{},{});
        QVERIFY(!attempt->take().has_value()); fixture.manager->cancel(id); QVERIFY(!gate->current());
    }
    void strictResultValidation_data() {
        QTest::addColumn<QString>("mode"); QTest::addColumn<bool>("expected");
        QTest::newRow("same identity")<<QStringLiteral("echo")<<true;
        QTest::newRow("retention")<<QStringLiteral("retention")<<true;
        for(const auto* mode : {"retention-bad","wrong-identity","mixed-numbering","records-type","records-nonempty","claim","cursor","partial","error-type","error-text","unknown-key","duplicate","late-host"})
            QTest::newRow(mode)<<QString::fromLatin1(mode)<<false;
    }
    void strictResultValidation() {
        QFETCH(QString,mode); QFETCH(bool,expected);
        Fixture fixture; QVERIFY(fixture.valid); auto gate=std::make_shared<NativeGate>(); auto attempt=fixture.attempt(gate); QVERIFY(attempt);
        auto context=fixture.context(mode);
        if(mode=="mixed-numbering") context["identity"]=QVariantMap{{"kind","episode"},{"title","Fixture"},
            {"season",1},{"episodes",QVariantList{2}}};
        QSignalSpy result(fixture.manager.get(),&AddonManager::operationFinished);
        fixture.manager->invoke(contributionId,"enrich",context,{},attempt); QTRY_COMPARE(result.size(),1);
        QCOMPARE(attempt->take().has_value(),expected);
    }
    void completedAndFailedHostCalls_data() {
        QTest::addColumn<QString>("mode"); QTest::addColumn<QString>("hostMode");
        QTest::addColumn<unsigned>("calls"); QTest::addColumn<bool>("expected");
        QTest::newRow("complete")<<QStringLiteral("host")<<QStringLiteral("ok")<<1u<<true;
        QTest::newRow("failed")<<QStringLiteral("host")<<QStringLiteral("fail")<<1u<<false;
        QTest::newRow("retry does not erase failure")<<QStringLiteral("host-retry")<<QStringLiteral("retry")<<2u<<false;
        QTest::newRow("pending is not complete")<<QStringLiteral("pending")<<QStringLiteral("pending")<<1u<<false;
        QTest::newRow("unavailable handler")<<QStringLiteral("host")<<QString{}<<0u<<false;
    }
    void completedAndFailedHostCalls() {
        QFETCH(QString,mode); QFETCH(QString,hostMode); QFETCH(unsigned,calls); QFETCH(bool,expected);
        Fixture fixture; QVERIFY(fixture.valid); if(!hostMode.isEmpty()) QVERIFY(fixture.host(hostMode));
        auto gate=std::make_shared<NativeGate>(); auto attempt=fixture.attempt(gate); QVERIFY(attempt);
        QSignalSpy result(fixture.manager.get(),&AddonManager::operationFinished);
        fixture.manager->invoke(contributionId,"enrich",fixture.context(mode),{},attempt);
        QTRY_COMPARE(result.size(),1); QCOMPARE(fixture.calls,calls); QCOMPARE(attempt->take().has_value(),expected);
        if(mode=="pending") QCOMPARE(fixture.cancels,1u);
    }
    void scopedIdentityNormalization_data() {
        QTest::addColumn<QVariantMap>("requested"); QTest::addColumn<QVariantMap>("returned"); QTest::addColumn<bool>("expected");
        const QVariantMap movie{{"kind","movie"},{"title","Fixture"},{"ids",QVariantMap{{"imdb","tt123"}}}};
        QTest::newRow("exact movie scope")<<movie<<QVariantMap{{"kind","movie"},{"title","Fixture"},
            {"externalIds",QVariantList{QVariantMap{{"namespace","imdb"},{"scope","movie"},{"value","tt123"}}}}}<<true;
        QTest::newRow("series scope is distinct")<<movie<<QVariantMap{{"kind","movie"},{"title","Fixture"},
            {"externalIds",QVariantList{QVariantMap{{"namespace","imdb"},{"scope","series"},{"value","tt123"}}}}}<<false;
        const QVariantMap unknown{{"kind","series"},{"title","Fixture"},{"ids",QVariantMap{{"external:custom-db:series","17"}}}};
        QTest::newRow("unknown namespace retains scope")<<unknown<<QVariantMap{{"kind","series"},{"title","Fixture"},
            {"externalIds",QVariantList{QVariantMap{{"namespace","custom-db"},{"scope","series"},{"value","17"}}}}}<<true;
        QTest::newRow("unknown namespace cannot change scope")<<unknown<<QVariantMap{{"kind","series"},{"title","Fixture"},
            {"externalIds",QVariantList{QVariantMap{{"namespace","custom-db"},{"scope","episode"},{"value","17"}}}}}<<false;
        const QVariantMap episode{{"kind","episode"},{"title","Fixture"},{"season",1},{"episodes",QVariantList{2}}};
        QTest::newRow("seasonal SDK form")<<episode<<QVariantMap{{"kind","episode"},{"title","Fixture"},
            {"episodeNumbering",QVariantMap{{"scheme","seasonal"},{"season",1},{"episodes",QVariantList{2}}}}}<<true;
        QTest::newRow("absolute is distinct")<<episode<<QVariantMap{{"kind","episode"},{"title","Fixture"},
            {"episodeNumbering",QVariantMap{{"scheme","absolute"},{"number","2"}}}}<<false;
        QTest::newRow("direct TVmaze episode ID")<<QVariantMap{{"kind","episode"},{"title","Fixture"},{"ids",QVariantMap{{"tvmazeEpisode","7"}}}}
            <<QVariantMap{{"kind","episode"},{"title","Fixture"},{"externalIds",QVariantList{QVariantMap{{"namespace","tvmaze"},{"scope","episode"},{"value","7"}}}}}<<true;
        QTest::newRow("season scope stays distinct")<<QVariantMap{{"kind","series"},{"title","Fixture"},{"ids",QVariantMap{{"external:tvmaze:season","7"}}}}
            <<QVariantMap{{"kind","series"},{"title","Fixture"},{"externalIds",QVariantList{QVariantMap{{"namespace","tvmaze"},{"scope","season"},{"value","7"}}}}}<<true;
        QTest::newRow("additional title cannot disappear")<<QVariantMap{{"kind","movie"},{"title","Fixture"}}
            <<QVariantMap{{"kind","movie"},{"titles",QVariantList{"Fixture","Other title"}}}<<false;
        QTest::newRow("long title suffix cannot disappear")<<QVariantMap{{"kind","movie"},{"title",QString(512,QLatin1Char('t'))}}
            <<QVariantMap{{"kind","movie"},{"title",QString(QString(512,QLatin1Char('t'))+QLatin1Char('x'))}}<<false;
        QTest::newRow("nonempty conflict cannot disappear")<<QVariantMap{{"kind","movie"},{"title","Fixture"}}
            <<QVariantMap{{"kind","movie"},{"title","Fixture"},{"conflicts",QVariantList{"Two records differ."}}}<<false;
    }
    void scopedIdentityNormalization() {
        QFETCH(QVariantMap,requested); QFETCH(QVariantMap,returned); QFETCH(bool,expected);
        Fixture fixture; QVERIFY(fixture.valid); auto gate=std::make_shared<NativeGate>(); auto attempt=fixture.attempt(gate); QVERIFY(attempt);
        auto context=fixture.context("returned",requested); context["fixtureIdentity"]=returned;
        QSignalSpy result(fixture.manager.get(),&AddonManager::operationFinished);
        fixture.manager->invoke(contributionId,"enrich",context,{},attempt); QTRY_COMPARE(result.size(),1);
        QCOMPARE(attempt->take().has_value(),expected);
    }
    void aFailedSiblingDoesNotCertifyOrInvalidateAnotherInvocation() {
        Fixture fixture; QVERIFY(fixture.valid); QVERIFY(fixture.host("retry"));
        auto firstGate=std::make_shared<NativeGate>(),secondGate=std::make_shared<NativeGate>();
        auto first=fixture.attempt(firstGate),second=fixture.attempt(secondGate,1); QVERIFY(first); QVERIFY(second);
        QSignalSpy result(fixture.manager.get(),&AddonManager::operationFinished);
        fixture.manager->invoke(contributionId,"enrich",fixture.context("host"),{},first);
        fixture.manager->invoke(contributionId,"enrich",fixture.context("host"),{},second);
        QTRY_COMPARE(result.size(),2); QCOMPARE(fixture.calls,2u);
        QVERIFY(!first->take().has_value()); QVERIFY(second->take().has_value());
    }
    void aTimedOutHostCallCannotBecomeNoMatch() {
        Fixture fixture; QVERIFY(fixture.valid); QVERIFY(fixture.host("pending"));
        auto gate=std::make_shared<NativeGate>(); auto attempt=fixture.attempt(gate); QVERIFY(attempt);
        QSignalSpy result(fixture.manager.get(),&AddonManager::operationFinished);
        fixture.manager->invoke(contributionId,"enrich",fixture.context("host"),{},attempt);
        QTRY_COMPARE_WITH_TIMEOUT(result.size(),1,12500); QCOMPARE(fixture.cancels,1u);
        QVERIFY(!attempt->take().has_value());
    }
    void cancelAndExpiryNeedNoQtDelivery() {
        Fixture fixture; QVERIFY(fixture.valid); QVERIFY(fixture.host("pending"));
        auto gate=std::make_shared<NativeGate>(); auto attempt=fixture.attempt(gate); QVERIFY(attempt);
        const auto id=fixture.manager->invoke(contributionId,"enrich",fixture.context("host"),{},attempt);
        QTRY_COMPARE(fixture.calls,1u); fixture.manager->cancel(id); QCOMPARE(fixture.cancels,1u);
        QVERIFY(!attempt->take().has_value()); QVERIFY(!gate->current());
        auto timed=std::make_shared<NativeGate>();
        auto expired=fixture.attempt(timed); QVERIFY(expired);
        QSignalSpy result(fixture.manager.get(),&AddonManager::operationFinished);
        fixture.manager->invoke(contributionId,"enrich",fixture.context(),{},expired); QTRY_COMPARE(result.size(),1);
        timed->expires=NativeGate::now()-1; QVERIFY(!expired->current()); QVERIFY(!expired->take().has_value());
    }
    void configAndOwnerRetirementRevokeTakenReceipts_data() {
        QTest::addColumn<QString>("change");
        for(const auto* change : {"settings","disabled","manager","app","native"}) QTest::newRow(change)<<QString::fromLatin1(change);
    }
    void configAndOwnerRetirementRevokeTakenReceipts() {
        QFETCH(QString,change); Fixture fixture; QVERIFY(fixture.valid);
        auto gate=std::make_shared<NativeGate>(); auto attempt=fixture.attempt(gate); QVERIFY(attempt);
        QSignalSpy result(fixture.manager.get(),&AddonManager::operationFinished);
        fixture.manager->invoke(contributionId,"enrich",fixture.context(),{},attempt); QTRY_COMPARE(result.size(),1);
        auto receipt=attempt->take(); QVERIFY(receipt.has_value()); QVERIFY(receipt->current());
        if(change=="settings") QVERIFY(fixture.manager->saveAddonSettings(packageId,{{"language","fr"}},{}));
        else if(change=="disabled") QVERIFY(fixture.manager->setEnabled(packageId,false));
        else if(change=="manager") fixture.manager.reset();
        else if(change=="app") fixture.lifetime.close();
        else gate->revoke();
        QVERIFY(!receipt->current()); QVERIFY(!receipt->nativeAdmission());
    }
    void captureMismatchCannotUseInstalledProvider() {
        Fixture fixture; QVERIFY(fixture.valid); auto gate=std::make_shared<NativeGate>(); auto capture=fixture.capture(gate);
        capture.settingsRevision++; auto attempt=AddonMetadataAttemptAdmission::create(capture); QVERIFY(attempt);
        QSignalSpy result(fixture.manager.get(),&AddonManager::operationFinished);
        fixture.manager->invoke(contributionId,"enrich",fixture.context(),{},attempt); QTRY_COMPARE(result.size(),1);
        QCOMPARE(result.first()[2].toString(),QStringLiteral("denied")); QVERIFY(!attempt->take().has_value());
    }
    void channelQuotaStaysChargedThroughTheFinalCapture() {
        Fixture fixture; QVERIFY(fixture.valid); auto gate=std::make_shared<NativeGate>(); auto capture=fixture.capture(gate);
        std::vector<std::shared_ptr<AddonMetadataAttemptAdmission>> values; values.reserve(1024);
        for(unsigned i=0;i<1024;++i) { auto value=AddonMetadataAttemptAdmission::create(capture); QVERIFY(value); values.push_back(std::move(value)); }
        QVERIFY(!AddonMetadataAttemptAdmission::create(capture)); values.pop_back();
        QVERIFY(AddonMetadataAttemptAdmission::create(capture)); values.clear();
    }
    void aTakenReceiptRetainsItsChannelCharge() {
        Fixture fixture; QVERIFY(fixture.valid); auto gate=std::make_shared<NativeGate>(); auto capture=fixture.capture(gate);
        std::vector<std::shared_ptr<AddonMetadataAttemptAdmission>> values;
        for(unsigned i=0;i<1023;++i) { auto value=AddonMetadataAttemptAdmission::create(capture); QVERIFY(value); values.push_back(std::move(value)); }
        auto attempt=fixture.attempt(gate); QVERIFY(attempt);
        QSignalSpy result(fixture.manager.get(),&AddonManager::operationFinished);
        fixture.manager->invoke(contributionId,"enrich",fixture.context(),{},attempt); QTRY_COMPARE(result.size(),1);
        auto receipt=attempt->take(); QVERIFY(receipt.has_value()); attempt.reset();
        QVERIFY(!AddonMetadataAttemptAdmission::create(capture)); receipt.reset();
        QVERIFY(AddonMetadataAttemptAdmission::create(capture)); values.clear();
    }
    void captureByteQuotaRecoversAfterRelease() {
        Fixture fixture; QVERIFY(fixture.valid); auto gate=std::make_shared<NativeGate>(); auto capture=fixture.capture(gate);
        capture.approvedGrants=QJsonObject{{"test",QString(600000,QLatin1Char('x'))}};
        std::vector<std::shared_ptr<AddonMetadataAttemptAdmission>> values;
        for(unsigned i=0;i<64;++i) { auto value=AddonMetadataAttemptAdmission::create(capture); if(!value) break; values.push_back(std::move(value)); }
        QVERIFY(!values.empty()); QVERIFY(values.size()<64); QVERIFY(!AddonMetadataAttemptAdmission::create(capture));
        values.clear(); QVERIFY(AddonMetadataAttemptAdmission::create(capture));
    }
    void ownedTransportRefusesParentAndThreadMismatchBeforeTransfer() {
        QTemporaryDir profile;
        QObject parent;
        std::unique_ptr<QNetworkAccessManager> network(new QNetworkAccessManager(&parent));
        auto* original=network.get();
        QVERIFY(!AddonManager::withNetworkTransport(profile.path(),network)); QCOMPARE(network.get(),original);
        network->setParent(nullptr);
        QThread thread; thread.start(); network->moveToThread(&thread);
        QVERIFY(!NetworkBroker::withTransport(network)); QCOMPARE(network.get(),original);
        auto* owner=QThread::currentThread();
        QVERIFY(QMetaObject::invokeMethod(original,[original,owner] { original->moveToThread(owner); },Qt::BlockingQueuedConnection));
        thread.quit(); QVERIFY(thread.wait(2000));
        auto broker=NetworkBroker::withTransport(network); QVERIFY(broker); QVERIFY(!network); QCOMPARE(original->parent(),broker.get());
    }
    void completeHttpObservations_data() {
        QTest::addColumn<int>("status"); QTest::addColumn<int>("policyCode");
        QTest::addColumn<bool>("prefix"); QTest::addColumn<bool>("shortBody");
        QTest::addColumn<int>("bodyBytes"); QTest::addColumn<bool>("knownLength"); QTest::addColumn<bool>("expected");
        struct Case { const char* name; int status,policy; bool prefix,shortBody; int bytes; bool knownLength,expected; };
        const Case cases[]={
            {"complete 200",200,0,false,false,128,true,true},
            {"complete 204",204,0,false,false,0,true,true},
            {"undeclared 404",404,0,false,false,128,true,false},
            {"declared 404",404,404,false,false,128,true,true},
            {"declared 410",410,410,false,false,128,true,true},
            {"wrong declared status",404,410,false,false,128,true,false},
            {"wrong declared feature",404,-404,false,false,128,true,false},
            {"401",401,0,false,false,128,true,false},
            {"429",429,0,false,false,128,true,false},
            {"500",500,0,false,false,128,true,false},
            {"partial 206",206,0,false,false,128,true,false},
            {"long prefix 200",200,0,true,false,128,true,false},
            {"short 200",200,0,false,true,128,true,false},
            {"short declared 404",404,404,false,true,128,true,false},
            {"complete short prefix",200,0,true,false,5,true,true},
            {"complete empty prefix",204,0,true,false,0,true,true},
            {"exact cap prefix",200,0,true,false,16,true,false},
            {"exact cap declared 404",404,404,true,false,16,true,false},
            {"complete short declared 404 prefix",404,404,true,false,5,true,true},
            {"unknown length prefix with EOF",200,0,true,false,5,false,true},
            {"complete short body without prefix",200,0,false,false,5,true,true}
        };
        for(const auto& row : cases) QTest::newRow(row.name)<<row.status<<row.policy<<row.prefix<<row.shortBody<<row.bytes<<row.knownLength<<row.expected;
    }
    void completeHttpObservations() {
        QFETCH(int,status); QFETCH(int,policyCode); QFETCH(bool,prefix); QFETCH(bool,shortBody);
        QFETCH(int,bodyBytes); QFETCH(bool,knownLength); QFETCH(bool,expected);
        ReceiptTlsServer server; QVERIFY(server.start());
        const QByteArray body(bodyBytes,'x');
        server.response="HTTP/1.1 "+QByteArray::number(status)+" Fixture\r\nContent-Type: text/plain\r\n";
        if(knownLength) server.response+="Content-Length: "+QByteArray::number(body.size())+"\r\n";
        if(status==206) server.response+="Content-Range: bytes 0-127/256\r\n";
        server.response+="Connection: close\r\n\r\n"+(shortBody ? body.left(2) : body);
        auto state=std::make_shared<ReceiptTransportState>();
        std::unique_ptr<QNetworkAccessManager> network(new ReceiptRoutedNetwork(server,state));
        QJsonObject policy;
        if(policyCode) policy={{"version",1},{"httpAbsent",QJsonArray{QJsonObject{
            {"feature",policyCode<0 ? "video.details" : "artwork"},{"operation","enrich"},{"statuses",QJsonArray{policyCode<0 ? -policyCode : policyCode}}}}}};
        Fixture fixture(policy,&network); QVERIFY2(fixture.valid,qPrintable(fixture.error)); QVERIFY(!network);
        auto gate=std::make_shared<NativeGate>(); auto attempt=fixture.attempt(gate); QVERIFY(attempt);
        auto context=fixture.context("network");
        QVariantMap request{{"url","https://8.8.8.8/result"},{"method","GET"}};
        if(prefix) { request["responseBytes"]=16; request["allowTruncated"]=true; }
        context["fixtureRequest"]=request;
        QSignalSpy result(fixture.manager.get(),&AddonManager::operationFinished);
        fixture.manager->invoke(contributionId,"enrich",context,{},attempt);
        QTRY_COMPARE_WITH_TIMEOUT(result.size(),1,5000);
        QCOMPARE(state->admitted,1u); QCOMPARE(server.handshakes,1u); QCOMPARE(server.requests,1u);
        QCOMPARE(attempt->take().has_value(),expected);
        fixture.manager.reset(); QCOMPARE(state->destroyed,1u);
    }
    void aDeclaredPrivateAddressNeverReachesTheInjectedTransport() {
        ReceiptTlsServer server; QVERIFY(server.start());
        auto state=std::make_shared<ReceiptTransportState>();
        std::unique_ptr<QNetworkAccessManager> network(new ReceiptRoutedNetwork(server,state));
        const QString origin=QStringLiteral("https://127.0.0.1:%1").arg(server.serverPort());
        Fixture fixture({},&network,{origin}); QVERIFY(fixture.valid);
        auto gate=std::make_shared<NativeGate>(); auto attempt=fixture.attempt(gate); QVERIFY(attempt);
        auto context=fixture.context("network"); context["fixtureRequest"]=QVariantMap{{"url",QString(origin+QStringLiteral("/denied"))}};
        QSignalSpy result(fixture.manager.get(),&AddonManager::operationFinished);
        fixture.manager->invoke(contributionId,"enrich",context,{},attempt); QTRY_COMPARE(result.size(),1);
        QVERIFY(!attempt->take().has_value()); QCOMPARE(state->admitted,0u); QCOMPARE(server.requests,0u);
    }
    void aCanceledHttpBodyCannotBecomeComplete_data() {
        QTest::addColumn<bool>("unknownLength");
        QTest::newRow("declared body is pending")<<false;
        QTest::newRow("unknown length has no EOF")<<true;
    }
    void aCanceledHttpBodyCannotBecomeComplete() {
        QFETCH(bool,unknownLength);
        ReceiptTlsServer server; QVERIFY(server.start()); server.keepOpen=true;
        server.response="HTTP/1.1 200 Fixture\r\nContent-Type: text/plain\r\n";
        if(!unknownLength) server.response+="Content-Length: 10000\r\n";
        server.response+="Connection: close\r\n\r\nx";
        auto state=std::make_shared<ReceiptTransportState>();
        std::unique_ptr<QNetworkAccessManager> network(new ReceiptRoutedNetwork(server,state));
        Fixture fixture({},&network); QVERIFY(fixture.valid);
        auto gate=std::make_shared<NativeGate>(); auto attempt=fixture.attempt(gate); QVERIFY(attempt);
        auto context=fixture.context("network");
        QVariantMap request{{"url","https://8.8.8.8/result"}};
        if(unknownLength) { request["responseBytes"]=16; request["allowTruncated"]=true; }
        context["fixtureRequest"]=request;
        QSignalSpy result(fixture.manager.get(),&AddonManager::operationFinished);
        const auto id=fixture.manager->invoke(contributionId,"enrich",context,{},attempt);
        QTRY_COMPARE(server.requests,1u); QTest::qWait(30); QVERIFY(result.isEmpty());
        QVERIFY(!attempt->take().has_value()); fixture.manager->cancel(id);
        QVERIFY(!attempt->take().has_value()); QVERIFY(!gate->current());
    }
    void policyIsClosedAndBoundToTheDeclaredPair() {
        const QJsonObject declaration{{"type","metadata.provider"},{"contract","metadata@1"},
            {"features",QJsonArray{"artwork","video.details"}},{"operations",QJsonArray{"enrich","identify"}}};
        auto contribution=declaration;
        QVERIFY(metadataCompletionAbsentStatuses(contribution,"artwork","enrich").has_value());
        QVERIFY(metadataCompletionAbsentStatuses(contribution,"artwork","enrich")->isEmpty());
        contribution["completionPolicy"]=QJsonObject{{"version",1},{"httpAbsent",QJsonArray{
            QJsonObject{{"feature","artwork"},{"operation","enrich"},{"statuses",QJsonArray{404,410}}}}}};
        QCOMPARE(metadataCompletionAbsentStatuses(contribution,"artwork","enrich").value(),QSet<int>({404,410}));
        QVERIFY(metadataCompletionAbsentStatuses(contribution,"video.details","enrich")->isEmpty());
        auto policy=contribution["completionPolicy"].toObject(); policy["version"]=2; contribution["completionPolicy"]=policy;
        QVERIFY(!validateMetadataCompletionPolicy(contribution));
    }
};
QTEST_GUILESS_MAIN(TestAddonMetadataAttempt)
#include "test_addon_metadata_attempt.moc"
