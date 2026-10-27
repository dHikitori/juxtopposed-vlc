// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QLockFile>
#include <QUuid>
#include <atomic>
#include <thread>
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
    void installEnableAndTicketCaptureDoNotApprovePlayback() {
        Fixture f; AddonSourceConsent store(f.profile.path(),f.checks(),f.options());
        QVERIFY(!store.approved(f.binding)); QVERIFY(!QFile::exists(f.file()));
        f.enabled=false; QVERIFY(!store.capture(f.binding)); f.enabled=true;
        QVERIFY(!store.approved(f.binding)); const auto ticket=store.capture(f.binding); QVERIFY(ticket);
        QVERIFY(!store.approved(f.binding)); QVERIFY(!store.grant(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        QVERIFY(store.grant(ticket->id)); QVERIFY(store.approved(f.binding)); QVERIFY(!store.grant(ticket->id));
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
};
QTEST_GUILESS_MAIN(TestAddonSourceConsent)
#include "test_addon_source_consent.moc"
