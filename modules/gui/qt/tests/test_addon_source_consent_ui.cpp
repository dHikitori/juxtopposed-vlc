// SPDX-License-Identifier: GPL-2.0-or-later
#include "addons/addon_source_consent_ui.hpp"
#include <QTest>
#include <QScopeGuard>
#include <memory>
#include <utility>

using namespace vlc::addons;
namespace {
const QString source=QStringLiteral("org.hakone.test/source");
struct Fake {
    QObject lifetime;
    AddonSourceConsentUi* ui=nullptr;
    QVariantMap captured;
    QString last;
    QStringList discarded;
    unsigned requests=0,changes=0;
    bool active=true,inlineReady=false,inlineFinish=false,saved=false;
    std::function<void()> onRequest,onRead,onChange,onDiscard,onActive;
    Fake() {
        captured={{"id",source},{"name",QStringLiteral("Test source")},{"source",QStringLiteral("https://git.example/source")},
            {"kind",QStringLiteral("git")},{"accountLabel",QStringLiteral("Test account")},{"accountFields",QVariantMap{}},
            {"accountReference",QStringLiteral("account-a")},{"profile",QStringLiteral("public-https-media@1")},
            {"profiles",QStringList{QStringLiteral("public-https-media@1")}},{"publisherVerified",false},{"approved",false}};
    }
    AddonSourceConsentUi::Backend backend() {
        AddonSourceConsentUi::Backend result;
        result.lifetime=&lifetime;
        result.active=[this] {
            const bool value=active;
            if(onActive) { const auto call=onActive; call(); }
            return value;
        };
        result.request=[this](const QString& contribution) {
            last=QStringLiteral("review-")+QString::number(++requests);
            const auto request=last;
            captured.insert("id",contribution);
            if(onRequest) { const auto call=onRequest; call(); }
            if(inlineReady && ui) ui->approvalReady(request);
            return request;
        };
        result.read=[this](const QString&) {
            const auto snapshot=captured;
            if(onRead) { const auto call=onRead; call(); }
            return snapshot;
        };
        result.change=[this](const QString& request,bool approved) {
            ++changes; saved=approved;
            if(onChange) { const auto call=onChange; call(); }
            if(inlineFinish && ui) ui->actionFinished(request,true,{});
            return true;
        };
        result.discard=[this](const QString& request) {
            discarded.append(request);
            if(onDiscard) { const auto call=onDiscard; call(); }
        };
        return result;
    }
    void ready() { ui->approvalReady(last); }
};
}
class TestAddonSourceConsentUi : public QObject {
    Q_OBJECT
private slots:
    void reviewDoesNotGrantAndAnApprovalHasOneUse() {
        Fake fake; AddonSourceConsentUi ui(fake.backend()); fake.ui=&ui;
        QVERIFY(ui.available()); QVERIFY(ui.prepare(source)); QVERIFY(ui.preparing());
        QCOMPARE(fake.changes,0u); QVERIFY(!ui.approve());
        fake.ready(); QVERIFY(!ui.review().isEmpty()); QCOMPARE(fake.changes,0u);
        QVERIFY(ui.approve()); QVERIFY(ui.acting()); QCOMPARE(fake.changes,1u);
        QVERIFY(!ui.approve()); QVERIFY(!ui.prepare(source));
        ui.actionFinished(fake.last,true,{}); QVERIFY(!ui.acting()); QVERIFY(ui.review().isEmpty());
        QVERIFY(!ui.approve()); QVERIFY(fake.saved); ui.viewClosed(); QVERIFY(fake.saved);
    }
    void savedAccessCanBeRevokedThroughItsCapturedReview() {
        Fake fake; fake.saved=true; fake.captured.insert("approved",true);
        AddonSourceConsentUi ui(fake.backend()); fake.ui=&ui;
        QVERIFY(ui.prepare(source)); fake.ready(); QVERIFY(ui.revoke());
        ui.actionFinished(fake.last,true,{}); QVERIFY(!fake.saved); QCOMPARE(fake.changes,1u);
    }
    void changedAccountCannotUseTheDisplayedApproval() {
        Fake fake; AddonSourceConsentUi ui(fake.backend()); fake.ui=&ui;
        QVERIFY(ui.prepare(source)); fake.ready();
        fake.captured.insert("accountReference",QStringLiteral("account-b"));
        QVERIFY(!ui.approve()); QCOMPARE(fake.changes,0u); QVERIFY(ui.review().isEmpty());
        QCOMPARE(fake.discarded,QStringList{fake.last});
    }
    void aNestedApproveStartsOnlyOneSavedChange() {
        Fake fake; AddonSourceConsentUi ui(fake.backend()); fake.ui=&ui;
        QVERIFY(ui.prepare(source)); fake.ready();
        fake.onRead=[&] { fake.onRead={}; QVERIFY(ui.approve()); };
        QVERIFY(!ui.approve()); QCOMPARE(fake.changes,1u); QVERIFY(ui.acting());
        ui.actionFinished(fake.last,true,{}); QVERIFY(fake.saved); QVERIFY(!ui.acting());
    }
    void anOldAvailabilityReadCannotClearANewReview() {
        Fake fake; AddonSourceConsentUi ui(fake.backend()); fake.ui=&ui;
        fake.active=false;
        fake.onActive=[&] {
            fake.onActive={}; fake.active=true;
            QVERIFY(ui.prepare(QStringLiteral("org.hakone.other/source")));
        };
        ui.refresh(); QVERIFY(ui.preparing()); fake.ready();
        QCOMPARE(ui.review().value("id").toString(),QStringLiteral("org.hakone.other/source"));
    }
    void aCanceledCaptureDiscardsTheReturnedTicket() {
        Fake fake; AddonSourceConsentUi ui(fake.backend()); fake.ui=&ui;
        fake.onRequest=[&] { ui.viewClosed(); };
        QVERIFY(!ui.prepare(source)); QVERIFY(!ui.preparing()); QVERIFY(ui.review().isEmpty());
        QCOMPARE(fake.discarded,QStringList{fake.last}); QCOMPARE(fake.changes,0u);
    }
    void inlineReadyAndCompletionRequireTheHostAction() {
        Fake fake; fake.inlineReady=true; fake.inlineFinish=true;
        AddonSourceConsentUi ui(fake.backend()); fake.ui=&ui;
        QVERIFY(ui.prepare(source)); QVERIFY(!ui.preparing()); QCOMPARE(fake.changes,0u);
        QVERIFY(ui.approve()); QCOMPARE(fake.changes,1u); QVERIFY(!ui.acting());
        QVERIFY(ui.review().isEmpty()); QVERIFY(!ui.status().isEmpty());
    }
    void aDiscardCallbackCanReplaceTheCapture() {
        Fake fake; AddonSourceConsentUi ui(fake.backend()); fake.ui=&ui;
        QVERIFY(ui.prepare(source)); fake.ready();
        fake.onDiscard=[&] {
            fake.onDiscard={};
            QVERIFY(ui.prepare(QStringLiteral("org.hakone.other/source")));
        };
        QVERIFY(!ui.prepare(source)); QCOMPARE(fake.requests,2u); QVERIFY(ui.preparing());
        fake.ready(); QCOMPARE(ui.review().value("id").toString(),QStringLiteral("org.hakone.other/source"));
        QCOMPARE(fake.changes,0u);
    }
    void aReadCanDeleteTheFacade() {
        Fake fake; fake.ui=new AddonSourceConsentUi(fake.backend());
        const auto cleanup=qScopeGuard([&] { delete std::exchange(fake.ui,nullptr); });
        QVERIFY(fake.ui->prepare(source));
        fake.onRead=[&] { delete std::exchange(fake.ui,nullptr); };
        fake.ready(); QVERIFY(!fake.ui); QCOMPARE(fake.discarded.size(),1);
    }
    void anOldReadCannotPublishOverANewCapture() {
        Fake fake; AddonSourceConsentUi ui(fake.backend()); fake.ui=&ui;
        QVERIFY(ui.prepare(source)); const auto previous=fake.last;
        fake.onRead=[&] {
            fake.onRead={}; ui.viewClosed(); QVERIFY(ui.prepare(QStringLiteral("org.hakone.other/source")));
        };
        ui.approvalReady(previous); QVERIFY(ui.review().isEmpty()); QVERIFY(ui.preparing());
        fake.ready(); QCOMPARE(ui.review().value("id").toString(),QStringLiteral("org.hakone.other/source"));
        QCOMPARE(fake.changes,0u);
    }
    void aChangeCanDeleteTheFacade() {
        Fake fake; fake.ui=new AddonSourceConsentUi(fake.backend());
        const auto cleanup=qScopeGuard([&] { delete std::exchange(fake.ui,nullptr); });
        QVERIFY(fake.ui->prepare(source)); fake.ready();
        fake.onChange=[&] { delete std::exchange(fake.ui,nullptr); };
        QVERIFY(!fake.ui->approve()); QVERIFY(!fake.ui); QCOMPARE(fake.changes,1u);
    }
    void viewCloseDoesNotPublishALateSavedResult() {
        Fake fake; AddonSourceConsentUi ui(fake.backend()); fake.ui=&ui;
        QVERIFY(ui.prepare(source)); fake.ready(); QVERIFY(ui.approve());
        const auto request=fake.last; ui.viewClosed();
        ui.actionFinished(request,true,{}); QVERIFY(ui.status().isEmpty()); QVERIFY(fake.saved);
    }
    void inactiveAndClosedOwnersRefusePreparation() {
        Fake fake; AddonSourceConsentUi ui(fake.backend()); fake.ui=&ui;
        fake.active=false; ui.refresh(); QVERIFY(!ui.available()); QVERIFY(!ui.prepare(source));
        fake.active=true; ui.refresh(); QVERIFY(ui.available()); QVERIFY(ui.prepare(source));
        ui.close(); QVERIFY(!ui.available()); QVERIFY(!ui.prepare(source));
        QCOMPARE(fake.discarded,QStringList{fake.last});
    }
    void unsupportedReviewScopesAreRefused_data() {
        QTest::addColumn<QString>("field"); QTest::addColumn<QVariant>("value");
        QTest::newRow("profile") << QStringLiteral("profile") << QVariant(QStringLiteral("private-network@1"));
        QTest::newRow("wider-profile-list") << QStringLiteral("profiles") << QVariant(QStringList{QStringLiteral("public-https-media@1"),QStringLiteral("private-network@1")});
        QTest::newRow("extra-field") << QStringLiteral("extraAuthority") << QVariant(true);
        QTest::newRow("wrong-source") << QStringLiteral("id") << QVariant(QStringLiteral("org.hakone.other/source"));
        QTest::newRow("approved-type") << QStringLiteral("approved") << QVariant(QStringLiteral("false"));
    }
    void unsupportedReviewScopesAreRefused() {
        QFETCH(QString,field); QFETCH(QVariant,value);
        Fake fake; AddonSourceConsentUi ui(fake.backend()); fake.ui=&ui;
        QVERIFY(ui.prepare(source)); fake.captured.insert(field,value); fake.ready();
        QVERIFY(ui.review().isEmpty()); QCOMPARE(fake.changes,0u); QVERIFY(!ui.errorMessage().isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestAddonSourceConsentUi)
#include "test_addon_source_consent_ui.moc"
