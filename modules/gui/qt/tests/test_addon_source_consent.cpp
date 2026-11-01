// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QUuid>
#include <atomic>
#include <thread>
#include <vector>
#include <QScopeGuard>
#include <QElapsedTimer>
#include <condition_variable>
#include <mutex>
#include "addons/addon_package_store.hpp"
#ifdef Q_OS_UNIX
# include <fcntl.h>
# include <sys/stat.h>
# include <unistd.h>
#endif
#include "addons/addon_source_consent.hpp"
#include "addons/addon_source_admissions.hpp"

using namespace vlc::addons;
class TestAddonSourceConsent : public QObject
{
    Q_OBJECT
    struct Fixture {
        QTemporaryDir profile;
        AddonSourceConsent::Binding binding{AddonSourceConsent::SourceKind::Git,QStringLiteral("https://example.org/addons/radio.git"),
            QStringLiteral("org.example.radio/source"),QStringLiteral("account-1")};
        qint64 now=0; bool enabled=true,granted=true; quint64 release=1;
        AddonSourceAdmissions admissions{profile.filePath("admissions"),[this](const QVariantMap& ref) {
            return enabled && ref.value("source").toString()==binding.contribution && ref.value("accountReference").toString()==binding.accountReference;
        }};
        AddonSourceConsent::Checks checks() {
            return {[this](const auto& value) { const auto expected=AddonSourceConsent::canonical(binding); return enabled && granted && expected && value==*expected; },
                [this](const auto&,const QString& uri) { return !admissions.currentReference(uri).isEmpty(); }};
        }
        AddonSourceConsent::Options options() {
            AddonSourceConsent::Options result; result.now=[this] { return now; }; result.ticketLifetimeMs=10; result.intentLifetimeMs=10; return result;
        }
        QString uri(const QString& item=QStringLiteral("station")) {
            const QVariantMap ref{{"format","hakone-addon-source-v1"},{"source",binding.contribution},{"accountReference",binding.accountReference},
                {"itemId",item},{"snapshot",QVariantMap{{"title","Station"},{"mediaKind","audio"}}}};
            QStringList values; return admissions.admit({ref},&values)?values.first():QString();
        }
        QString file() const { return profile.filePath("source-consent/approvals.json"); }
        bool approve(AddonSourceConsent& store) { const auto ticket=store.capture(binding); return ticket && store.grant(ticket->id); }
    };
    static QJsonObject read(const QString& path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return QJsonDocument::fromJson(file.readAll()).object(); }
    static bool write(const QString& path,const QByteArray& bytes) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size(); }
    using Reason=AddonSourceConsent::IntentReason;
private slots:
    void displaySubscriptionsKeepAllNativeCancellationSlotsAvailable() {
        Fixture f; std::atomic_uint display{0},canceled{0};
        AddonSourceConsent store(f.profile.path(),f.checks(),f.options());
        QVERIFY(f.approve(store)); const auto token=store.approvalToken(f.binding); QVERIFY(token);
        const auto shared=store.displayChanges([&] { ++display; }); QVERIFY(bool(shared));
        const auto entry=token->entryChanges([&] { ++display; }); QVERIFY(bool(entry));
        StoreAuthority authority(f.profile.path()); const auto baseline=authority.capture();
        std::vector<StoreAuthorityWatch> native; native.reserve(StoreAuthorityToken::WatchLimit);
        for(unsigned index=0;index<StoreAuthorityToken::WatchLimit;++index) {
            auto watch=baseline.onInvalidated([&] { ++canceled; }); QVERIFY(bool(watch)); native.push_back(std::move(watch));
        }
        QCOMPARE(canceled.load(),0u);
        std::thread writer([&] { authority.invalidate(); }); writer.join();
        QCOMPARE(canceled.load(),StoreAuthorityToken::WatchLimit); QVERIFY(display.load()>0);
    }
    void bindingAndProfileChangesWakeTheDisplayWithoutQtDelivery() {
        Fixture f; std::atomic_uint sharedCalls{0},entryCalls{0};
        AddonSourceConsent store(f.profile.path(),f.checks(),f.options());
        QVERIFY(f.approve(store)); const auto token=store.approvalToken(f.binding); QVERIFY(token);
        const auto shared=store.displayChanges([&] { ++sharedCalls; }); QVERIFY(bool(shared));
        const auto entry=token->entryChanges([&] { ++entryCalls; }); QVERIFY(bool(entry));
        const auto revoke=store.nativeBindingRevoker(f.binding); QVERIFY(revoke);
        std::thread writer(revoke); writer.join();
        QVERIFY(!token->current()); QVERIFY(entryCalls.load()>0); QCOMPARE(sharedCalls.load(),0u);
        const auto reset=store.nativeProfileRevoker(); QVERIFY(reset);
        std::thread profile(reset); profile.join(); QVERIFY(sharedCalls.load()>0);
    }
    void refusedDisplayQuotaDoesNotConsumeNativeCancellationQuota() {
        Fixture f; std::atomic_uint wake{0},cancel{0};
        AddonSourceConsent store(f.profile.path(),f.checks(),f.options());
        StoreAuthority authority(f.profile.path());
        std::vector<StoreAuthorityChanges> displays;
        for(unsigned index=0;index<StoreAuthority::ChangeLimit;++index) {
            auto changes=authority.onChanged([&] { ++wake; }); QVERIFY(bool(changes)); displays.push_back(std::move(changes));
        }
        QVERIFY(!store.displayChanges([&] { ++wake; }));
        const auto native=authority.capture().onInvalidated([&] { ++cancel; }); QVERIFY(bool(native));
        QCOMPARE(cancel.load(),0u); displays.clear();
        QVERIFY(bool(store.displayChanges([&] { ++wake; })));
    }
    void distributionIdentityNamesUseHakone_data() {
        QTest::addColumn<QString>("identity"); QTest::addColumn<int>("kind"); QTest::addColumn<bool>("accepted");
        const int distribution=int(AddonSourceConsent::SourceKind::Distribution);
        QTest::newRow("hakone") << QStringLiteral("hakone-distribution:org.example.radio") << distribution << true;
        QTest::newRow("https") << QStringLiteral("https://example.org/addons/radio.git") << distribution << true;
        QTest::newRow("legacy-refused") << QStringLiteral("vlc-distribution:org.example.radio") << distribution << false;
        QTest::newRow("empty") << QStringLiteral("hakone-distribution:") << distribution << false;
        QTest::newRow("path") << QStringLiteral("hakone-distribution:../radio") << distribution << false;
        QTest::newRow("query") << QStringLiteral("hakone-distribution:org.example.radio?key=value") << distribution << false;
        QTest::newRow("fragment") << QStringLiteral("hakone-distribution:org.example.radio#other") << distribution << false;
        QTest::newRow("git-refused") << QStringLiteral("hakone-distribution:org.example.radio") << int(AddonSourceConsent::SourceKind::Git) << false;
        QTest::newRow("developer-refused") << QStringLiteral("hakone-distribution:org.example.radio") << int(AddonSourceConsent::SourceKind::Developer) << false;
    }
    void distributionIdentityNamesUseHakone() {
        QFETCH(QString,identity); QFETCH(int,kind); QFETCH(bool,accepted);
        Fixture f; auto value=f.binding; value.identity=identity; value.kind=static_cast<AddonSourceConsent::SourceKind>(kind);
        const auto canonical=AddonSourceConsent::canonical(value);
        QCOMPARE(bool(canonical),accepted);
        if(accepted) QCOMPARE(canonical->identity,identity);
    }
    void repeatedOwnerRetirementWaitsForTheFirstNativeRevocation() {
        Fixture f; AddonSourceConsent store(f.profile.path(),f.checks(),f.options()); QVERIFY(f.approve(store));
        const auto token=store.approvalToken(f.binding); QVERIFY(token);
        std::mutex mutex; std::condition_variable changed; bool entered=false,released=false;
        std::atomic_bool firstDone{false},secondDone{false};
        const auto watch=token->onInvalidated([&] { std::unique_lock<std::mutex> lock(mutex); entered=true; changed.notify_all(); changed.wait(lock,[&]{return released;}); });
        QVERIFY(watch); const auto retire=store.nativeOwnerRevoker();
        std::thread first([&] { retire(); firstDone=true; }); std::thread second;
        const auto cleanup=qScopeGuard([&] {
            { std::lock_guard<std::mutex> lock(mutex); released=true; } changed.notify_all();
            if(first.joinable()) first.join(); if(second.joinable()) second.join();
        });
        { std::unique_lock<std::mutex> lock(mutex); QVERIFY(changed.wait_for(lock,std::chrono::seconds(3),[&]{return entered;})); }
        second=std::thread([&] { store.retire(); secondDone=true; });
        QVERIFY(!token->current()); QVERIFY(!firstDone.load()); QVERIFY(!secondDone.load());
        { std::lock_guard<std::mutex> lock(mutex); released=true; } changed.notify_all(); first.join(); second.join();
        QVERIFY(firstDone.load()); QVERIFY(secondDone.load()); QVERIFY(!watch);
    }
    void nativeWatchesRevokeBeforeSavedStateCanChange() {
        Fixture f; AddonSourceConsent store(f.profile.path(),f.checks(),f.options());
        QVERIFY(f.approve(store)); const auto uri=f.uri();
        const auto intent=store.mintIntent(f.binding,uri,1,Reason::PrimaryPlayback);
        const auto receipt=store.consumeIntent(intent,f.binding,uri,1,Reason::PrimaryPlayback); QVERIFY(receipt);
        const auto original=f.binding; f.binding.contribution=QStringLiteral("org.example.radio/other");
        QVERIFY(f.approve(store)); const auto otherUri=f.uri();
        const auto otherIntent=store.mintIntent(f.binding,otherUri,2,Reason::PrimaryPlayback);
        const auto other=store.consumeIntent(otherIntent,f.binding,otherUri,2,Reason::PrimaryPlayback); QVERIFY(other);
        f.binding=original;
        std::mutex mutex; std::condition_variable changed; bool entered=false,released=false;
        std::atomic_uint calls{0}; std::atomic_bool revoked{false},reset{false};
        const auto watch=receipt->liveToken().onInvalidated([&] {
            ++calls; std::unique_lock<std::mutex> lock(mutex); entered=true; changed.notify_all();
            changed.wait(lock,[&] { return released; });
        });
        QVERIFY(bool(watch));
        std::thread revoke([&] { store.revoke(f.binding); revoked=true; });
        std::thread resetter;
        auto cleanup=qScopeGuard([&] {
            { std::lock_guard<std::mutex> lock(mutex); released=true; } changed.notify_all();
            if(revoke.joinable()) revoke.join(); if(resetter.joinable()) resetter.join();
        });
        { std::unique_lock<std::mutex> lock(mutex); QVERIFY(changed.wait_for(lock,std::chrono::seconds(3),[&] { return entered; })); }
        QVERIFY(!receipt->liveToken().current()); QVERIFY(!revoked.load());
        resetter=std::thread([&] { store.reset(); reset=true; });
        QTRY_VERIFY_WITH_TIMEOUT(!other->liveToken().current(),1000);
        // The saved record must remain until the native cancellation returns.
        QVERIFY(!read(f.file()).value("approvals").toObject().isEmpty()); QVERIFY(!reset.load());
        { std::lock_guard<std::mutex> lock(mutex); released=true; } changed.notify_all();
        revoke.join(); resetter.join();
        QVERIFY(revoked.load()); QVERIFY(reset.load()); QCOMPARE(calls.load(),1u); QVERIFY(!watch);
    }
    void nativeWatchTracksOwnerReceiptPackageAndBindingSeparately() {
        Fixture f; AddonSourceConsent store(f.profile.path(),f.checks(),f.options()); QVERIFY(f.approve(store));
        const auto uri=f.uri(); auto intent=store.mintIntent(f.binding,uri,1,Reason::PrimaryPlayback);
        const auto receipt=store.consumeIntent(intent,f.binding,uri,1,Reason::PrimaryPlayback); QVERIFY(receipt);
        std::atomic_uint calls{0}; const auto watch=receipt->liveToken().onInvalidated([&] { ++calls; }); QVERIFY(bool(watch));
        const auto package=PackageStore(f.profile.path()).authorityToken();
        const auto original=f.binding; f.binding.contribution=QStringLiteral("org.example.radio/other");
        QVERIFY(f.approve(store)); f.binding=original;
        QVERIFY(package.current()); QVERIFY(receipt->liveToken().current()); QVERIFY(bool(watch)); QCOMPARE(calls.load(),0u);
        AddonSourceConsent peer(f.profile.path(),f.checks(),f.options()); peer.close();
        QVERIFY(bool(watch)); QCOMPARE(calls.load(),0u);
        auto state=read(f.file()); state.insert("epoch",QUuid::createUuid().toString(QUuid::WithoutBraces));
        QVERIFY(write(f.file(),QJsonDocument(state).toJson()));
        QVERIFY(!store.current(*receipt,uri,1)); QCOMPARE(calls.load(),1u); QVERIFY(!watch);
        PackageStore(f.profile.path()).invalidateAuthority(); QCOMPARE(calls.load(),1u);
        bool refused=false; QVERIFY(!receipt->liveToken().onInvalidated([&] { refused=true; })); QVERIFY(refused);
    }
    void nativeWatchQuotaRetainsReleaseAndRetirement() {
        Fixture f; AddonSourceConsent store(f.profile.path(),f.checks(),f.options()); QVERIFY(f.approve(store)); const auto uri=f.uri();
        const auto intent=store.mintIntent(f.binding,uri,1,Reason::PrimaryPlayback);
        const auto receipt=store.consumeIntent(intent,f.binding,uri,1,Reason::PrimaryPlayback); QVERIFY(receipt);
        std::atomic_uint calls{0}; QList<AddonSourceConsent::LiveWatch> watches;
        for(unsigned i=0;i<StoreAuthorityToken::WatchLimit;++i) {
            const auto watch=receipt->liveToken().onInvalidated([&] { ++calls; }); QVERIFY(bool(watch)); watches.append(watch);
        }
        QVERIFY(!receipt->liveToken().onInvalidated([&] { ++calls; })); QCOMPARE(calls.load(),1u);
        watches.clear(); calls=0;
        const auto watch=receipt->liveToken().onInvalidated([&] { ++calls; }); QVERIFY(bool(watch));
        store.retire(); QCOMPARE(calls.load(),1u); QVERIFY(!watch); QVERIFY(!receipt->liveToken().current());
        store.retire(); store.close(); QCOMPARE(calls.load(),1u);
    }
    void approvalTokensAndNativeRevokersDoNotCreatePlaybackIntent() {
        Fixture f; AddonSourceConsent store(f.profile.path(),f.checks(),f.options());
        QVERIFY(!store.approvalToken(f.binding)); QVERIFY(f.approve(store));
        const auto approval=store.approvalToken(f.binding); QVERIFY(approval); QVERIFY(approval->current());
        const auto uri=f.uri(); const auto intent=store.mintIntent(f.binding,uri,1,Reason::PrimaryPlayback);
        const auto receipt=store.consumeIntent(intent,f.binding,uri,1,Reason::PrimaryPlayback); QVERIFY(receipt);
        unsigned canceled=0; const auto watch=receipt->liveToken().onInvalidated([&] { ++canceled; }); QVERIFY(bool(watch));
        const auto revoke=store.nativeBindingRevoker(f.binding); QVERIFY(bool(revoke)); revoke();
        QCOMPARE(canceled,1u); QVERIFY(!approval->current()); QVERIFY(!store.approvalToken(f.binding));
        QVERIFY(!store.mintIntent(f.binding,uri,2,Reason::PrimaryPlayback).size());
        QVERIFY(f.approve(store)); const auto fresh=store.approvalToken(f.binding); QVERIFY(fresh); QVERIFY(fresh->current());
        const auto reset=store.nativeProfileRevoker(); QVERIFY(bool(reset)); reset();
        QVERIFY(!fresh->current()); QVERIFY(!store.approvalToken(f.binding)); QVERIFY(store.reset());
        QVERIFY(!store.approvalToken(f.binding));
    }
    void installEnableAndTicketCaptureDoNotApprovePlayback() {
        Fixture f; AddonSourceConsent store(f.profile.path(),f.checks(),f.options());
        QVERIFY(!store.approved(f.binding)); QVERIFY(!QFile::exists(f.file()));
        f.enabled=false; QVERIFY(!store.capture(f.binding)); f.enabled=true;
        QVERIFY(!store.approved(f.binding)); const auto ticket=store.capture(f.binding); QVERIFY(ticket);
        QVERIFY(!store.approved(f.binding)); QVERIFY(!store.grant(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QVERIFY(store.grant(ticket->id)); QVERIFY(store.approved(f.binding)); QVERIFY(!store.grant(ticket->id));
        AddonSourceConsent restored(f.profile.path(),f.checks(),f.options()); QVERIFY(restored.approved(f.binding));
    }
    void acceptedPermissionApprovesOnlyACurrentSource() {
        Fixture f; AddonSourceConsent store(f.profile.path(),f.checks(),f.options());
        f.enabled=false; QVERIFY(!store.approveAccepted(f.binding)); QVERIFY(!QFile::exists(f.file())); f.enabled=true;
        f.granted=false; QVERIFY(!store.approveAccepted(f.binding)); QVERIFY(!store.approved(f.binding)); f.granted=true;
        QVERIFY(store.approveAccepted(f.binding)); QVERIFY(store.approved(f.binding));
        const auto revision=read(f.file()).value("revision").toInt();
        QVERIFY(store.approveAccepted(f.binding)); QCOMPARE(read(f.file()).value("revision").toInt(),revision);
        const auto uri=f.uri(); QVERIFY(!store.mintIntent(f.binding,uri,1,Reason::PrimaryPlayback).isEmpty());
        // A new account is a new binding; accepting it again approves it.
        f.binding.accountReference="account-2"; QVERIFY(!store.approved(f.binding));
        QVERIFY(store.approveAccepted(f.binding)); QVERIFY(store.approved(f.binding));
        AddonSourceConsent restored(f.profile.path(),f.checks(),f.options()); QVERIFY(restored.approved(f.binding));
    }
    void canonicalRepositoryAndSourceKindsHaveSeparateAuthority() {
        Fixture f; AddonSourceConsent store(f.profile.path(),f.checks(),f.options());
        auto alias=f.binding; alias.identity="https://EXAMPLE.ORG:443/addons/../addons/radio.git";
        const auto ticket=store.capture(alias); QVERIFY(ticket); QCOMPARE(ticket->binding.identity,f.binding.identity); QVERIFY(store.grant(ticket->id));
        const auto original=f.binding; f.binding.kind=AddonSourceConsent::SourceKind::Developer;
        QVERIFY(!store.approved(f.binding)); QVERIFY(f.approve(store)); f.binding.kind=AddonSourceConsent::SourceKind::Distribution;
        QVERIFY(!store.approved(f.binding)); f.binding=original; QVERIFY(store.approved(f.binding));
        for (const QString& identity:QStringList{"http://example.org/repo.git","https://user:password@example.org/repo.git","https://example.org/repo.git?token=bad","https://example.org/repo.git#other"}) {
            auto invalid=f.binding; invalid.identity=identity; QVERIFY(!AddonSourceConsent::canonical(invalid));
        }
        auto invalid=f.binding; invalid.profile="public-https-media@2"; QVERIFY(!AddonSourceConsent::canonical(invalid));
        invalid=f.binding; invalid.accountReference="disconnected:account-1"; QVERIFY(!AddonSourceConsent::canonical(invalid));
    }
    void expiredChangedAndResetTicketsCannotGrant() {
        Fixture f; AddonSourceConsent store(f.profile.path(),f.checks(),f.options()); const auto old=store.capture(f.binding); QVERIFY(old);
        f.now=10; QVERIFY(!store.grant(old->id)); const auto account=store.capture(f.binding); QVERIFY(account);
        f.binding.accountReference="account-2"; QVERIFY(!store.grant(account->id)); QVERIFY(!store.approved(f.binding));
        const auto reset=store.capture(f.binding); QVERIFY(reset); QVERIFY(store.reset()); QVERIFY(!store.grant(reset->id));
        AddonSourceConsent peer(f.profile.path(),f.checks(),f.options()); const auto pending=peer.capture(f.binding); QVERIFY(pending);
        QVERIFY(store.reset()); QVERIFY(!peer.grant(pending->id));
        const auto stale=store.capture(f.binding); QVERIFY(stale); QVERIFY(f.approve(peer)); QVERIFY(!store.grant(stale->id));
    }
    void malformedReboundAndOversizedFilesFailClosed() {
        Fixture f; AddonSourceConsent store(f.profile.path(),f.checks(),f.options()); QVERIFY(f.approve(store)); const auto uri=f.uri(); QVERIFY(!uri.isEmpty());
        const auto intent=store.mintIntent(f.binding,uri,1,Reason::PrimaryPlayback); const auto authorization=store.consumeIntent(intent,f.binding,uri,1,Reason::PrimaryPlayback); QVERIFY(authorization);
        auto state=read(f.file()); auto approvals=state.value("approvals").toObject(); auto it=approvals.begin(); auto record=it.value().toObject(); auto binding=record.value("binding").toObject();
        binding.insert("account","account-other"); record.insert("binding",binding); approvals.insert(it.key(),record); state.insert("approvals",approvals);
        const auto tampered=QJsonDocument(state).toJson(); QVERIFY(write(f.file(),tampered));
        QVERIFY(!store.approved(f.binding)); QVERIFY(!store.current(*authorization,uri,1)); QVERIFY(!store.capture(f.binding));
        QFile file(f.file()); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),tampered); file.close();
        QVERIFY(store.reset()); QVERIFY(!store.approved(f.binding)); QVERIFY(!store.current(*authorization,uri,1));
        QVERIFY(write(f.file(),QByteArray(1024*1024+1,'x'))); QVERIFY(!store.approved(f.binding)); QVERIFY(!store.capture(f.binding)); QVERIFY(store.reset());
    }
    void intentUsesExactUriGenerationAndReasonOnlyOnce() {
        Fixture f; AddonSourceConsent store(f.profile.path(),f.checks(),f.options()); QVERIFY(f.approve(store)); const auto uri=f.uri(), other=f.uri("other");
        auto token=store.mintIntent(f.binding,uri,1,Reason::PrimaryPlayback); QVERIFY(!token.isEmpty());
        QVERIFY(!store.consumeIntent(token,f.binding,other,1,Reason::PrimaryPlayback)); QVERIFY(!store.consumeIntent(token,f.binding,uri,1,Reason::PrimaryPlayback));
        token=store.mintIntent(f.binding,uri,2,Reason::Replay); QVERIFY(!token.isEmpty());
        QVERIFY(!store.consumeIntent(token,f.binding,uri,3,Reason::Replay)); QVERIFY(store.mintIntent(f.binding,uri,2,Reason::Replay).isEmpty());
        token=store.mintIntent(f.binding,uri,3,Reason::QueueAdvance); QVERIFY(!token.isEmpty());
        const auto authorization=store.consumeIntent(token,f.binding,uri,3,Reason::QueueAdvance); QVERIFY(authorization);
        QVERIFY(store.current(*authorization,uri,3)); QVERIFY(!store.current(*authorization,uri,4)); QVERIFY(!store.current(*authorization,other,3));
        QVERIFY(!store.consumeIntent(token,f.binding,uri,3,Reason::QueueAdvance));
        token=store.mintIntent(f.binding,uri,4,Reason::Replay); QVERIFY(!store.consumeIntent(token,f.binding,uri,4,Reason::PrimaryPlayback));
        QVERIFY(store.mintIntent(f.binding,uri,0,Reason::PrimaryPlayback).isEmpty());
        QVERIFY(store.mintIntent(f.binding,uri,5,static_cast<Reason>(99)).isEmpty());
        QVERIFY(store.mintIntent(f.binding,"https://example.org/media",5,Reason::PrimaryPlayback).isEmpty());
        auto forged=uri; forged.replace(forged.lastIndexOf('.')+1,36,QStringLiteral("00000000-0000-4000-8000-000000000001"));
        QVERIFY(store.mintIntent(f.binding,forged,5,Reason::PrimaryPlayback).isEmpty());
    }
    void expiryClockRollbackAndGenerationWindowRefuseReplay() {
        Fixture f; AddonSourceConsent store(f.profile.path(),f.checks(),f.options()); QVERIFY(f.approve(store)); const auto uri=f.uri();
        auto token=store.mintIntent(f.binding,uri,1,Reason::PrimaryPlayback); QVERIFY(!token.isEmpty()); f.now=10;
        QVERIFY(!store.consumeIntent(token,f.binding,uri,1,Reason::PrimaryPlayback)); QVERIFY(store.mintIntent(f.binding,uri,1,Reason::PrimaryPlayback).isEmpty());
        token=store.mintIntent(f.binding,uri,2,Reason::PrimaryPlayback); f.now=9; QVERIFY(!store.consumeIntent(token,f.binding,uri,2,Reason::PrimaryPlayback));
        f.now=11; QVERIFY(store.mintIntent(f.binding,uri,2,Reason::PrimaryPlayback).isEmpty());
        token=store.mintIntent(f.binding,uri,3,Reason::PrimaryPlayback); const auto authorization=store.consumeIntent(token,f.binding,uri,3,Reason::PrimaryPlayback); QVERIFY(authorization);
        f.now=1000000; QVERIFY(store.current(*authorization,uri,3));
        QVERIFY(!store.mintIntent(f.binding,uri,10000,Reason::Replay).isEmpty()); QVERIFY(store.mintIntent(f.binding,uri,3,Reason::PrimaryPlayback).isEmpty());
    }
    void revokeRegrantResetAndNewOwnersCannotReviveOldReceipts() {
        Fixture f; AddonSourceConsent store(f.profile.path(),f.checks(),f.options()); QVERIFY(f.approve(store)); const auto uri=f.uri();
        const auto token=store.mintIntent(f.binding,uri,1,Reason::PrimaryPlayback); const auto original=store.consumeIntent(token,f.binding,uri,1,Reason::PrimaryPlayback); QVERIFY(original);
        const auto pending=store.mintIntent(f.binding,uri,2,Reason::Replay); QVERIFY(store.revoke(f.binding));
        QVERIFY(!store.approved(f.binding)); QVERIFY(!store.current(*original,uri,1)); QVERIFY(!store.consumeIntent(pending,f.binding,uri,2,Reason::Replay));
        QVERIFY(f.approve(store)); QVERIFY(!store.current(*original,uri,1)); QVERIFY(store.mintIntent(f.binding,uri,1,Reason::PrimaryPlayback).isEmpty());
        const auto fresh=store.mintIntent(f.binding,uri,3,Reason::PrimaryPlayback); const auto authorization=store.consumeIntent(fresh,f.binding,uri,3,Reason::PrimaryPlayback); QVERIFY(authorization);
        QVERIFY(store.reset()); QVERIFY(!store.current(*authorization,uri,3)); QVERIFY(f.approve(store));
        store.close(); QVERIFY(!store.approved(f.binding)); QVERIFY(!store.current(*authorization,uri,3));
        AddonSourceConsent reopened(f.profile.path(),f.checks(),f.options()); QVERIFY(reopened.approved(f.binding)); QVERIFY(!reopened.current(*authorization,uri,3));
    }
    void releaseIdentityIsStableButAccountSourceAndGrantsMustBeCurrent() {
        Fixture f; AddonSourceConsent store(f.profile.path(),f.checks(),f.options()); QVERIFY(f.approve(store)); const auto original=f.binding; const auto uri=f.uri();
        const auto token=store.mintIntent(f.binding,uri,1,Reason::PrimaryPlayback); const auto authorization=store.consumeIntent(token,f.binding,uri,1,Reason::PrimaryPlayback); QVERIFY(authorization);
        ++f.release; QVERIFY(store.approved(f.binding)); QVERIFY(store.current(*authorization,uri,1));
        f.granted=false; QVERIFY(!store.approved(f.binding)); QVERIFY(!store.current(*authorization,uri,1)); f.granted=true;
        f.binding.accountReference="account-2"; QVERIFY(!store.approved(f.binding)); QVERIFY(!store.current(*authorization,uri,1));
        f.binding=original; f.binding.identity="https://example.org/other-owner/radio.git"; QVERIFY(!store.approved(f.binding));
        QVERIFY(!store.current(*authorization,uri,1));
    }
    void quotasAndWriteConflictsKeepAuthorityBounded() {
        Fixture f; auto options=f.options(); options.approvals=2; options.tickets=2; options.intents=2;
        AddonSourceConsent store(f.profile.path(),f.checks(),options);
        const auto one=store.capture(f.binding),two=store.capture(f.binding); QVERIFY(one && two); QVERIFY(!store.capture(f.binding)); QVERIFY(store.grant(one->id));
        const auto uri=f.uri(); QVERIFY(!store.mintIntent(f.binding,uri,1,Reason::PrimaryPlayback).isEmpty()); QVERIFY(!store.mintIntent(f.binding,uri,2,Reason::PrimaryPlayback).isEmpty());
        QVERIFY(store.mintIntent(f.binding,uri,3,Reason::PrimaryPlayback).isEmpty()); f.now=10;
        f.binding.accountReference="account-2"; QVERIFY(f.approve(store)); f.binding.accountReference="account-3";
        const auto limit=store.capture(f.binding); QVERIFY(limit); QVERIFY(!store.grant(limit->id));
        f.binding.accountReference="account-1"; QVERIFY(store.approved(f.binding));
        QLockFile lock(f.profile.filePath("source-consent/approvals.lock")); QVERIFY(lock.tryLock(0));
        QVERIFY(!store.revoke(f.binding)); QVERIFY(!store.approved(f.binding)); lock.unlock(); QVERIFY(store.revoke(f.binding));
        AddonSourceConsent peer(f.profile.path(),f.checks(),options); QVERIFY(!peer.approved(f.binding));
    }
    void concurrentNativeIntentChecksNeedNoQObject() {
        Fixture f; AddonSourceConsent store(f.profile.path(),f.checks(),f.options()); QVERIFY(f.approve(store)); const auto uri=f.uri(); std::atomic_int passed{0};
        const auto worker=[&](quint64 first) { for (quint64 i=first;i<first+4;++i) {
            const auto token=store.mintIntent(f.binding,uri,i,Reason::QueueAdvance);
            const auto authorization=store.consumeIntent(token,f.binding,uri,i,Reason::QueueAdvance);
            if (authorization && store.current(*authorization,uri,i)) ++passed;
        }};
        std::thread a(worker,1),b(worker,5); a.join(); b.join(); QCOMPARE(passed.load(),8);
    }
    void liveReceiptsCloseOnOwnerDestructionWithoutAStorePointer() {
        Fixture f; AddonSourceConsent::LiveToken empty; QVERIFY(!empty.current());
        auto store=std::make_unique<AddonSourceConsent>(f.profile.path(),f.checks(),f.options()); QVERIFY(f.approve(*store)); const auto uri=f.uri();
        const auto intent=store->mintIntent(f.binding,uri,1,Reason::PrimaryPlayback);
        const auto authorization=store->consumeIntent(intent,f.binding,uri,1,Reason::PrimaryPlayback); QVERIFY(authorization);
        const auto live=authorization->liveToken(); QVERIFY(live.current()); store.reset(); QVERIFY(!live.current());
    }
    void aPeerOwnerCloseDoesNotCloseOtherOwnersReceipts() {
        Fixture f; AddonSourceConsent one(f.profile.path(),f.checks(),f.options()),two(f.profile.path(),f.checks(),f.options());
        QVERIFY(f.approve(one)); const auto uri=f.uri();
        const auto first=one.mintIntent(f.binding,uri,1,Reason::PrimaryPlayback),second=two.mintIntent(f.binding,uri,1,Reason::PrimaryPlayback);
        const auto a=one.consumeIntent(first,f.binding,uri,1,Reason::PrimaryPlayback),b=two.consumeIntent(second,f.binding,uri,1,Reason::PrimaryPlayback);
        QVERIFY(a && b); two.close(); QVERIFY(a->liveToken().current()); QVERIFY(!b->liveToken().current());
        QVERIFY(one.current(*a,uri,1));
    }
    void peerRevokeRegrantAndResetCannotReviveLiveReceipts() {
        Fixture f; AddonSourceConsent one(f.profile.path(),f.checks(),f.options()),peer(f.profile.path()+"/./",f.checks(),f.options());
        QVERIFY(f.approve(one)); const auto uri=f.uri(); auto intent=one.mintIntent(f.binding,uri,1,Reason::PrimaryPlayback);
        const auto original=one.consumeIntent(intent,f.binding,uri,1,Reason::PrimaryPlayback); QVERIFY(original); QVERIFY(original->liveToken().current());
        QVERIFY(peer.revoke(f.binding)); QVERIFY(!original->liveToken().current()); QVERIFY(!one.approved(f.binding));
        QVERIFY(f.approve(peer)); QVERIFY(!original->liveToken().current());
        intent=one.mintIntent(f.binding,uri,2,Reason::Replay); const auto renewed=one.consumeIntent(intent,f.binding,uri,2,Reason::Replay); QVERIFY(renewed);
        QVERIFY(renewed->liveToken().current()); QVERIFY(f.approve(peer)); QVERIFY(!renewed->liveToken().current());
        intent=one.mintIntent(f.binding,uri,3,Reason::Replay); const auto fresh=one.consumeIntent(intent,f.binding,uri,3,Reason::Replay); QVERIFY(fresh);
        QVERIFY(peer.reset()); QVERIFY(!fresh->liveToken().current()); QVERIFY(f.approve(peer)); QVERIFY(!fresh->liveToken().current());
    }
    void approvingAnotherBindingKeepsTheExistingReceipt() {
        Fixture f; const auto uri=f.uri(); auto checks=f.checks();
        checks.current=[](const auto& value) { return AddonSourceConsent::canonical(value).has_value(); };
        checks.admitted=[uri](const auto&,const QString& value) { return value==uri; };
        AddonSourceConsent store(f.profile.path(),checks,f.options()),peer(f.profile.path(),checks,f.options()); QVERIFY(f.approve(store));
        const auto intent=store.mintIntent(f.binding,uri,1,Reason::PrimaryPlayback); const auto authorization=store.consumeIntent(intent,f.binding,uri,1,Reason::PrimaryPlayback); QVERIFY(authorization);
        const auto original=f.binding; f.binding.contribution=QStringLiteral("org.example.radio/other"); QVERIFY(f.approve(peer)); f.binding=original;
        QVERIFY(authorization->liveToken().current()); QVERIFY(store.current(*authorization,uri,1));
    }
    void failedPeerRevocationDeniesNewReceiptsUntilExplicitApproval() {
        Fixture f; AddonSourceConsent store(f.profile.path(),f.checks(),f.options()),peer(f.profile.path(),f.checks(),f.options()); QVERIFY(f.approve(store)); const auto uri=f.uri();
        const auto intent=store.mintIntent(f.binding,uri,1,Reason::PrimaryPlayback); const auto authorization=store.consumeIntent(intent,f.binding,uri,1,Reason::PrimaryPlayback); QVERIFY(authorization);
        QLockFile lock(f.profile.filePath("source-consent/approvals.lock")); QVERIFY(lock.tryLock(0));
        QVERIFY(!peer.revoke(f.binding)); QVERIFY(!authorization->liveToken().current()); QVERIFY(!store.approved(f.binding));
        QVERIFY(store.mintIntent(f.binding,uri,2,Reason::Replay).isEmpty()); lock.unlock(); QVERIFY(f.approve(peer));
        QVERIFY(store.approved(f.binding)); QVERIFY(!authorization->liveToken().current());
    }
    void failedRevocationSurvivesOwnerRecreationInTheSameProcess() {
        Fixture f;
        {
            AddonSourceConsent store(f.profile.path(),f.checks(),f.options()); QVERIFY(f.approve(store));
            QLockFile lock(f.profile.filePath("source-consent/approvals.lock")); QVERIFY(lock.tryLock(0)); QVERIFY(!store.revoke(f.binding));
        }
        AddonSourceConsent recreated(f.profile.path(),f.checks(),f.options()); QVERIFY(!recreated.approved(f.binding));
        QVERIFY(f.approve(recreated)); QVERIFY(recreated.approved(f.binding));
    }
    void packageStoreChangesInvalidateLiveReceiptsImmediately() {
        Fixture f; AddonSourceConsent store(f.profile.path(),f.checks(),f.options()); QVERIFY(f.approve(store)); const auto uri=f.uri();
        const auto intent=store.mintIntent(f.binding,uri,1,Reason::PrimaryPlayback); const auto authorization=store.consumeIntent(intent,f.binding,uri,1,Reason::PrimaryPlayback); QVERIFY(authorization);
        const auto live=authorization->liveToken(); QVERIFY(live.current()); PackageStore peer(f.profile.path()); QVERIFY(peer.setSafeMode(true));
        QVERIFY(!live.current()); QVERIFY(peer.setSafeMode(false)); QVERIFY(!live.current());
    }
    void externalFileChangesRequireTheFullCheckAndPermanentlyCloseThatReceipt() {
        Fixture f; AddonSourceConsent store(f.profile.path(),f.checks(),f.options()); QVERIFY(f.approve(store)); const auto uri=f.uri();
        const auto intent=store.mintIntent(f.binding,uri,1,Reason::PrimaryPlayback); const auto authorization=store.consumeIntent(intent,f.binding,uri,1,Reason::PrimaryPlayback); QVERIFY(authorization);
        const auto live=authorization->liveToken(); const auto saved=read(f.file()); QVERIFY(write(f.file(),"invalid JSON"));
        QVERIFY(live.current()); QVERIFY(!store.current(*authorization,uri,1)); QVERIFY(!live.current());
        QVERIFY(write(f.file(),QJsonDocument(saved).toJson())); QVERIFY(!live.current());
    }
#ifdef Q_OS_UNIX
    void liveRevocationDoesNotWaitForABlockedDiskCheck_data() {
        QTest::addColumn<bool>("retire"); QTest::newRow("binding-revoke") << false; QTest::newRow("owner-retire") << true;
    }
    void liveRevocationDoesNotWaitForABlockedDiskCheck() {
        QFETCH(bool,retire);
        Fixture f; AddonSourceConsent store(f.profile.path(),f.checks(),f.options()); QVERIFY(f.approve(store)); const auto uri=f.uri();
        const auto intent=store.mintIntent(f.binding,uri,1,Reason::PrimaryPlayback); const auto authorization=store.consumeIntent(intent,f.binding,uri,1,Reason::PrimaryPlayback); QVERIFY(authorization);
        const auto live=authorization->liveToken(); std::atomic_uint canceled{0};
        const auto watch=live.onInvalidated([&] { ++canceled; }); QVERIFY(bool(watch));
        const QString saved=f.file()+QStringLiteral(".saved"); QVERIFY(QFile::rename(f.file(),saved));
        const auto path=QFile::encodeName(f.file()); QVERIFY(::mkfifo(path.constData(),0600)==0);
        int writer=-1; std::thread reader,revoker; std::atomic_bool readDone{false},revokeDone{false};
        const auto cleanup=qScopeGuard([&] {
            if (writer<0) writer=::open(path.constData(),O_RDWR|O_NONBLOCK);
            QFile::remove(f.file()); QFile::rename(saved,f.file()); if (writer>=0) ::close(writer);
            if (reader.joinable()) reader.join(); if (revoker.joinable()) revoker.join();
        });
        reader=std::thread([&] { store.current(*authorization,uri,1); readDone.store(true); });
        QTRY_VERIFY_WITH_TIMEOUT([&] { if (writer<0) writer=::open(path.constData(),O_WRONLY|O_NONBLOCK); return writer>=0; }(),5000);
        QVERIFY(!readDone.load()); QVERIFY(live.current());
        if(retire) { QElapsedTimer clock; clock.start(); store.retire(); QVERIFY(clock.elapsed()<1000); }
        else revoker=std::thread([&] { store.revoke(f.binding); revokeDone.store(true); });
        QTRY_VERIFY_WITH_TIMEOUT(!live.current(),1000); QCOMPARE(canceled.load(),1u);
        QVERIFY(!readDone.load()); QVERIFY(!revokeDone.load());
    }
#endif
};
QTEST_GUILESS_MAIN(TestAddonSourceConsent)
#include "test_addon_source_consent.moc"
