// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <memory>
#include "lib/libvlc_internal.h"
#include "addons/addon_discovery_source_owner.hpp"
#include "addons/addon_manager.hpp"
#include "addons/addon_source_admissions.hpp"
#include "addons/addon_source_consent_ui.hpp"

using namespace vlc::addons;
namespace {
const QString missingSource=QStringLiteral("org.hakone.missing/source");
const QString configuredSource=QStringLiteral("org.hakone.discovery-fixture/catalogue");
struct Fixture {
    QTemporaryDir directory,package;
    AddonStreamLifetime lifetime;
    libvlc_int_t* instance=libvlc_InternalCreate();
    std::unique_ptr<AddonManager> manager=std::make_unique<AddonManager>(directory.path());
    std::shared_ptr<AddonSourceAdmissions> admissions=std::make_shared<AddonSourceAdmissions>(
        directory.filePath(QStringLiteral("admissions")),[](const QVariantMap&) { return false; });
    std::unique_ptr<AddonDiscoverySourceOwner> owner=std::make_unique<AddonDiscoverySourceOwner>(
        manager.get(),VLC_OBJECT(instance),admissions,lifetime.handle());
    std::unique_ptr<AddonSourceConsentUi> ui=std::make_unique<AddonSourceConsentUi>(owner.get());
    bool installSource() {
        const QString source=QStringLiteral(VLC_ADDON_SOURCE_ROOT "/contrib-addons/examples/discovery-fixture");
        if(!QDir().mkpath(package.filePath(QStringLiteral("dist")))
            || !QFile::copy(source+QStringLiteral("/LICENSE"),package.filePath(QStringLiteral("LICENSE")))
            || !QFile::copy(source+QStringLiteral("/dist/addon.wasm"),package.filePath(QStringLiteral("dist/addon.wasm")))) return false;
        QFile input(source+QStringLiteral("/addon.json")); if(!input.open(QIODevice::ReadOnly)) return false;
        auto manifest=QJsonDocument::fromJson(input.readAll()).object();
        manifest.insert(QStringLiteral("permissions"),QJsonObject{{"required",QJsonObject{{"media.stream",
            QJsonObject{{"profiles",QJsonArray{"public-https-media@1"}}}}}},{"optional",QJsonObject{}}});
        QFile output(package.filePath(QStringLiteral("addon.json"))); if(!output.open(QIODevice::WriteOnly)) return false;
        const auto bytes=QJsonDocument(manifest).toJson(); if(output.write(bytes)!=bytes.size()) return false; output.close();
        return manager->installLocalPackage(package.path(),true)
            && manager->setEnabled(QStringLiteral("org.hakone.discovery-fixture"),true);
    }
    ~Fixture() {
        ui.reset(); owner.reset(); manager.reset(); lifetime.close();
        if(!lifetime.handle().waitQt(std::chrono::seconds(5))) qFatal("The source UI work did not end.");
        if(instance) libvlc_InternalDestroy(instance);
    }
};
}
class TestAddonSourceConsentUiOwner : public QObject {
    Q_OBJECT
private slots:
    void theNativeReviewNeedsAnExplicitAllowAndRemoveAction() {
        Fixture fixture; QVERIFY(fixture.instance); QVERIFY(fixture.installSource());
        QVERIFY(fixture.owner->activate()); QTRY_VERIFY(fixture.ui->available());
        QVERIFY(fixture.ui->prepare(configuredSource)); QTRY_VERIFY(!fixture.ui->review().isEmpty());
        QVERIFY(!fixture.ui->review().value("approved").toBool()); QVERIFY(!fixture.owner->canUseSource(configuredSource));
        QVERIFY(fixture.ui->approve()); QTRY_VERIFY(!fixture.ui->acting()); QVERIFY(fixture.ui->errorMessage().isEmpty());
        QTRY_VERIFY(fixture.owner->canUseSource(configuredSource)); fixture.ui->viewClosed();
        QVERIFY(fixture.owner->canUseSource(configuredSource));
        QVERIFY(fixture.ui->prepare(configuredSource)); QTRY_VERIFY(!fixture.ui->review().isEmpty());
        QVERIFY(fixture.ui->review().value("approved").toBool()); QVERIFY(fixture.ui->revoke());
        QVERIFY(!fixture.owner->canUseSource(configuredSource)); QTRY_VERIFY(!fixture.ui->acting());
        QVERIFY(fixture.ui->errorMessage().isEmpty()); QVERIFY(!fixture.owner->canUseSource(configuredSource));
    }
    void closingTheActualReviewDoesNotGrantPlayback() {
        Fixture fixture; QVERIFY(fixture.instance); QVERIFY(fixture.installSource());
        QVERIFY(fixture.owner->activate()); QTRY_VERIFY(fixture.ui->available());
        QVERIFY(fixture.ui->prepare(configuredSource)); QTRY_VERIFY(!fixture.ui->review().isEmpty());
        fixture.ui->viewClosed(); QVERIFY(fixture.ui->review().isEmpty());
        QVERIFY(!fixture.ui->approve()); QVERIFY(!fixture.owner->canUseSource(configuredSource));
        QVERIFY(fixture.ui->prepare(configuredSource)); QTRY_VERIFY(!fixture.ui->review().isEmpty());
        QVERIFY(!fixture.ui->review().value("approved").toBool());
    }
    void nativeActivationAndCloseReachThePanel() {
        Fixture fixture; QVERIFY(fixture.instance); QVERIFY(!fixture.ui->available());
        QVERIFY(fixture.owner->activate()); QTRY_VERIFY(fixture.owner->active()); QTRY_VERIFY(fixture.ui->available());
        fixture.owner->close(); QTRY_VERIFY(!fixture.ui->available());
        QVERIFY(fixture.ui->review().isEmpty()); QVERIFY(!fixture.ui->approve()); QVERIFY(!fixture.ui->revoke());
    }
    void aMissingSourceEndsTheActualAsynchronousCapture() {
        Fixture fixture; QVERIFY(fixture.instance); QVERIFY(fixture.owner->activate()); QTRY_VERIFY(fixture.ui->available());
        QSignalSpy finished(fixture.owner.get(),&AddonDiscoverySourceOwner::actionFinished);
        QVERIFY(fixture.ui->prepare(missingSource)); QVERIFY(fixture.ui->preparing());
        QTRY_COMPARE(finished.size(),1); QTRY_VERIFY(!fixture.ui->preparing());
        QVERIFY(!finished.first().at(1).toBool()); QVERIFY(!fixture.ui->errorMessage().isEmpty());
        QVERIFY(fixture.ui->review().isEmpty()); QVERIFY(!fixture.ui->approve());
    }
    void managerRetirementClosesTheOwnerAndPendingReview() {
        Fixture fixture; QVERIFY(fixture.instance); QVERIFY(fixture.owner->activate()); QTRY_VERIFY(fixture.ui->available());
        QVERIFY(fixture.ui->prepare(missingSource)); fixture.manager.reset();
        QVERIFY(!fixture.owner->active()); QTRY_VERIFY(!fixture.ui->available());
        QVERIFY(!fixture.ui->preparing()); QVERIFY(fixture.ui->review().isEmpty());
        QVERIFY(!fixture.ui->prepare(missingSource));
    }
    void ownerDestructionClosesThePanelDuringCapture() {
        Fixture fixture; QVERIFY(fixture.instance); QVERIFY(fixture.owner->activate()); QTRY_VERIFY(fixture.ui->available());
        QVERIFY(fixture.ui->prepare(missingSource)); fixture.owner.reset();
        QVERIFY(!fixture.ui->available()); QVERIFY(!fixture.ui->preparing()); QVERIFY(fixture.ui->review().isEmpty());
        QVERIFY(!fixture.ui->approve()); QVERIFY(!fixture.ui->revoke());
        QCoreApplication::processEvents(); QVERIFY(!fixture.ui->available()); QVERIFY(fixture.ui->review().isEmpty());
    }
    void panelDestructionDoesNotStopTheSourceOwner() {
        Fixture fixture; QVERIFY(fixture.instance); QVERIFY(fixture.owner->activate()); QTRY_VERIFY(fixture.ui->available());
        QVERIFY(fixture.ui->prepare(missingSource)); fixture.ui.reset();
        QCoreApplication::processEvents(); QVERIFY(fixture.owner->active());
        fixture.ui=std::make_unique<AddonSourceConsentUi>(fixture.owner.get());
        QVERIFY(fixture.ui->available()); QVERIFY(fixture.ui->review().isEmpty()); QVERIFY(!fixture.ui->approve());
    }
};
QTEST_GUILESS_MAIN(TestAddonSourceConsentUiOwner)
#include "test_addon_source_consent_ui_owner.moc"
