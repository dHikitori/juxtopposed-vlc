// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QUuid>
#include "addons/addon_source_admissions.hpp"

using namespace vlc::addons;
class TestAddonSourceAdmissions : public QObject
{
    Q_OBJECT
    static QVariantMap reference() { return {{"format", "hakone-addon-source-v1"}, {"source", "org.example.radio/source"},
        {"itemId", "station-42"}, {"accountReference", "account-1"}, {"snapshot", QVariantMap{{"title", "Station"}, {"mediaKind", "audio"}}}}; }
    static QString forged(const QVariantMap& reference, const QString& id) {
        auto locator = reference; locator.remove("snapshot");
        return QStringLiteral("hakone-addon-source:v1:") + QString::fromLatin1(QJsonDocument(QJsonObject::fromVariantMap(locator)).toJson(QJsonDocument::Compact).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)) + '.' + id;
    }
private slots:
    void persistsExactSourceAccountAndDisplayAdmission() {
        QTemporaryDir profile; QVERIFY(profile.isValid()); int validations = 0;
        AddonSourceAdmissions store(profile.path(), [&](const auto&) { ++validations; return true; });
        QStringList uris; QString error;
        QVERIFY2(store.admit({reference()}, &uris, &error), qPrintable(error)); QCOMPARE(uris.size(), 1); QCOMPARE(validations, 1);
        QCOMPARE(store.admittedReference(uris[0]), reference());
        AddonSourceAdmissions restored(profile.path(), [](const auto&) { return true; });
        QCOMPARE(restored.admittedReference(uris[0]), reference());
        QStringList duplicate; QVERIFY(restored.admit({reference()}, &duplicate)); QCOMPARE(duplicate, uris);
        QString id; const auto locator = AddonSourceAdmissions::decodeUntrusted(uris[0], &id);
        QCOMPARE(locator.value("itemId"), reference().value("itemId")); QVERIFY(locator.value("snapshot").toMap().isEmpty());
        auto changed = reference(); changed.insert("accountReference", "account-2"); QVERIFY(restored.admittedReference(forged(changed, id)).isEmpty());
        changed = reference(); changed.insert("source", "org.example.other/source"); QVERIFY(restored.admittedReference(forged(changed, id)).isEmpty());
        changed = reference(); changed.insert("snapshot", QVariantMap{{"title", "Other title"}, {"mediaKind", "audio"}});
        QCOMPARE(restored.admittedReference(forged(changed, id)), reference());
        QStringList refreshed; QVERIFY(restored.admit({changed}, &refreshed)); QCOMPARE(refreshed, uris);
        QCOMPARE(restored.admittedReference(uris[0]), changed);
    }
    void checkCallsOnlyTheValidatorAndRecordOnlyTheFile() {
        QTemporaryDir profile; int validations = 0; bool sourceAvailable = true;
        const AddonSourceAdmissions store(profile.path(), [&](const auto&) { ++validations; return sourceAvailable; });
        QVERIFY(store.check({reference()})); QCOMPARE(validations, 1); QVERIFY(!QFile::exists(profile.filePath("source-admissions.json")));
        sourceAvailable = false; QString error; QVERIFY(!store.check({reference()}, &error)); QVERIFY(!error.isEmpty());
        QStringList uris; QVERIFY(store.record({reference()}, &uris)); QCOMPARE(validations, 2); QCOMPARE(uris.size(), 1);
        QCOMPARE(store.admittedReference(uris[0]), reference());
        auto invalid = reference(); invalid.remove("itemId"); QVERIFY(!store.record({invalid}, &uris)); QVERIFY(uris.isEmpty());
    }
    void unknownAndImportedUrisHaveNoAuthority() {
        QTemporaryDir first, second;
        AddonSourceAdmissions source(first.path(), [](const auto&) { return true; });
        AddonSourceAdmissions imported(second.path(), [](const auto&) { return true; });
        QStringList uris; QVERIFY(source.admit({reference()}, &uris));
        QVERIFY(!AddonSourceAdmissions::decodeUntrusted(uris[0]).isEmpty());
        QString oldUri = uris[0]; oldUri.replace(QStringLiteral("hakone-addon-source:"), QStringLiteral("vlc-addon-source:"));
        QVERIFY(AddonSourceAdmissions::decodeUntrusted(oldUri).isEmpty());
        QVERIFY(source.admittedReference(oldUri).isEmpty());
        QVERIFY(imported.admittedReference(uris[0]).isEmpty());
        QVERIFY(source.admittedReference(forged(reference(), QUuid::createUuid().toString(QUuid::WithoutBraces))).isEmpty());
        QVERIFY(AddonSourceAdmissions::decodeUntrusted(QString(12001, QLatin1Char('a'))).isEmpty());
        QVERIFY(AddonSourceAdmissions::decodeUntrusted(QStringLiteral("https://example.org/stream")).isEmpty());
    }
    void admissionDoesNotActivateASourceOrChangeItsAccount() {
        QTemporaryDir profile; bool sourceAvailable = false;
        AddonSourceAdmissions store(profile.path(), [&](const auto& ref) { return sourceAvailable && ref.value("accountReference").toString() == QLatin1String("account-1"); });
        QStringList uris; QVERIFY(!store.admit({reference()}, &uris)); QVERIFY(uris.isEmpty());
        sourceAvailable = true; QVERIFY(store.admit({reference()}, &uris));
        auto other = reference(); other.insert("accountReference", "account-2"); QVERIFY(!store.admit({other}, &uris));
        sourceAvailable = false; QVERIFY(!store.admit({reference()}, &uris));
    }
    void independentWritersKeepBothAdmissionRecords() {
        QTemporaryDir profile;
        AddonSourceAdmissions first(profile.path(), [](const auto&) { return true; }), second(profile.path(), [](const auto&) { return true; });
        QStringList one, two; QVERIFY(first.admit({reference()}, &one));
        auto other = reference(); other.insert("itemId", "station-2"); QVERIFY(second.admit({other}, &two));
        QCOMPARE(first.admittedReference(two[0]), other); QCOMPARE(second.admittedReference(one[0]), reference());
    }
    void malformedSavedDataFailsClosed() {
        QTemporaryDir profile;
        AddonSourceAdmissions store(profile.path(), [](const auto&) { return true; });
        QStringList uris; QVERIFY(store.admit({reference()}, &uris));
        QFile file(profile.filePath("source-admissions.json")); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("[]"), 2); file.close();
        QVERIFY(store.admittedReference(uris[0]).isEmpty());
        QVERIFY(!store.admit({reference()}, &uris)); QVERIFY(uris.isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("[]"));
    }
};
QTEST_GUILESS_MAIN(TestAddonSourceAdmissions)
#include "test_addon_source_admissions.moc"
