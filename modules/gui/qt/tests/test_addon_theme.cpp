// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include "addons/addon_manager.hpp"
#include "addons/addon_theme_service.hpp"

using namespace vlc::addons;

class PlatformThemePreferences : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString contrastPreference MEMBER preference NOTIFY preferenceChanged)
public:
    QString preference = QStringLiteral("NoPreference");
    void set(const QString& value) { preference = value; emit preferenceChanged(); }
signals:
    void preferenceChanged();
};
class TestAddonTheme : public QObject
{
    Q_OBJECT
    static QJsonObject reference()
    {
        QFile file(QStringLiteral(VLC_ADDON_SOURCE_ROOT "/contrib-addons/examples/midnight/themes/midnight.json"));
        if (!file.open(QIODevice::ReadOnly)) return {};
        return QJsonDocument::fromJson(file.readAll()).object();
    }
    static QJsonObject variant(QJsonObject document, const QString& group, const QString& key, const QJsonValue& value)
    {
        auto variants = document.value("variants").toObject(); auto dark = variants.value("dark").toObject();
        auto values = dark.value(group).toObject(); values.insert(key, value); dark.insert(group, values);
        variants.insert("dark", dark); document.insert("variants", variants); return document;
    }
    static bool copyExample(const QString& destination)
    {
        const QDir source(QStringLiteral(VLC_ADDON_SOURCE_ROOT "/contrib-addons/examples/midnight"));
        for (const auto& path : {QStringLiteral("addon.json"), QStringLiteral("LICENSE"), QStringLiteral("README.md"), QStringLiteral("themes/midnight.json")}) {
            const auto target = QDir(destination).filePath(path);
            if (!QDir().mkpath(QFileInfo(target).absolutePath()) || !QFile::copy(source.filePath(path), target)) return false;
        }
        return true;
    }
    static QString color(const QVariantMap& tokens, const char* name)
    { return tokens.value("colors").toMap().value(QString::fromLatin1(name)).toString(); }
private slots:
    void referenceAndBounds()
    {
        QString error;
        const auto document = reference(); QVERIFY(!document.isEmpty());
        QVERIFY2(AddonThemeService::validate(document, &error), qPrintable(error));
        QVERIFY(!AddonThemeService::validate(variant(document, "colors", "qml.source", "#ffffff"), &error));
        QVERIFY(!AddonThemeService::validate(variant(document, "colors", "surface.base", "url(file:///etc/passwd)"), &error));
        QVERIFY(!AddonThemeService::validate(variant(document, "shape", "controlRadius", 13), &error));
        QVERIFY(!AddonThemeService::validate(variant(document, "shape", "panelRadius", -1), &error));
        QVERIFY(!AddonThemeService::validate(variant(document, "effects", "panelOpacity", .2), &error));
        QVERIFY(!AddonThemeService::validate(variant(document, "colors", "text.primary", "#171923"), &error));
        QVERIFY(!AddonThemeService::validate(variant(document, "colors", "focus.ring", "#171923"), &error));
        QVERIFY(!AddonThemeService::validate(variant(document, "colors", "surface.hover", "#ffffff"), &error));
        QVERIFY(!AddonThemeService::validate(variant(document, "colors", "surface.selected", "#ffffff"), &error));
    }
    void overridesAndMissingVariants()
    {
        const auto document = reference();
        QCOMPARE(color(AddonThemeService::resolve(document, false), "surface.base"), QStringLiteral("#171923"));
        QCOMPARE(color(AddonThemeService::resolve(document, true), "surface.base"), QStringLiteral("#e9edf5"));
        QCOMPARE(color(AddonThemeService::resolve(document, false, "#12bc89"), "accent.primary"), QStringLiteral("#12bc89"));
        const auto high = AddonThemeService::resolve(document, false, "#12bc89", true, false);
        QCOMPARE(color(high, "surface.base"), QStringLiteral("#000000"));
        QCOMPARE(color(high, "text.primary"), QStringLiteral("#ffffff"));
        QCOMPARE(color(high, "accent.primary"), QStringLiteral("#ffff00"));
        QCOMPARE(high.value("effects").toMap().value("panelOpacity").toDouble(), 1.);
        QCOMPARE(AddonThemeService::resolve(document, false, {}, false, true).value("effects").toMap().value("panelOpacity").toDouble(), 1.);
        auto onlyDark = document; auto variants = document.value("variants").toObject(); variants.remove("light"); onlyDark.insert("variants", variants);
        QCOMPARE(color(AddonThemeService::resolve(onlyDark, true), "surface.base"), QStringLiteral("#acacac"));
    }
    void explicitSelectionPersistenceAndLiveFallback()
    {
        QTemporaryDir source, profile; QVERIFY(source.isValid() && profile.isValid()); QVERIFY(copyExample(source.path()));
        AddonManager manager(profile.path());
        QVERIFY(manager.stageLocalPackage(source.path())); QVERIFY(manager.approvePendingPackage(false));
        AddonThemeService themes(&manager);
        QCOMPARE(themes.availableThemes().size(), 0);
        const auto id = QStringLiteral("org.videolan.example.midnight/midnight");
        QVERIFY(!themes.selectTheme(id));
        QVERIFY(manager.setEnabled("org.videolan.example.midnight", true));
        QCOMPARE(themes.availableThemes().size(), 1);
        QVERIFY(themes.selectedTheme().isEmpty());
        QSignalSpy changed(&themes, &AddonThemeService::revisionChanged);
        QVERIFY(themes.selectTheme(id));
        QCOMPARE(color(themes.tokens(false), "surface.base"), QStringLiteral("#171923"));
        QVERIFY(!themes.usingFallback());
        QVERIFY(manager.setEnabled("org.videolan.example.midnight", false));
        QVERIFY(themes.usingFallback()); QVERIFY(!themes.errorMessage().isEmpty());
        QCOMPARE(color(themes.tokens(false), "surface.base"), QStringLiteral("#1b1b1b"));
        QVERIFY(manager.setEnabled("org.videolan.example.midnight", true));
        QVERIFY(!themes.usingFallback()); QCOMPARE(themes.selectedTheme(), id);
        AddonThemeService restored(&manager);
        QCOMPARE(restored.selectedTheme(), id); QCOMPARE(color(restored.tokens(false), "surface.base"), QStringLiteral("#171923"));
        QVERIFY(restored.selectTheme(QString())); QVERIFY(restored.selectedTheme().isEmpty());
        QCOMPARE(color(restored.tokens(false), "surface.base"), QStringLiteral("#1b1b1b"));
        QVERIFY(changed.count() >= 3);
    }
    void sharedRolesPreserveBuiltInAndFollowSelectedPalette()
    {
        const auto builtin = AddonThemeService::resolve({}, false);
        QCOMPARE(color(builtin, "surface.menu"), QStringLiteral("#191919"));
        QCOMPARE(color(builtin, "surface.library"), QStringLiteral("#0e0e0e"));
        QCOMPARE(color(builtin, "text.muted"), QStringLiteral("#6b6b6b"));
        const auto selected = AddonThemeService::resolve(reference(), false);
        QCOMPARE(color(selected, "surface.menu"), QStringLiteral("#1e2230"));
        QCOMPARE(color(selected, "surface.library"), QStringLiteral("#1e2230"));
        QCOMPARE(color(selected, "text.heading"), QStringLiteral("#edf0f7"));
        QCOMPARE(selected.value("shape").toMap().value("menuRadius").toDouble(), 12.);
    }
    void globalSubscribersObserveServiceCreationAndRecovery()
    {
        QVERIFY(AddonThemeService::instance() == nullptr);
        QSignalSpy updates(AddonThemeService::notifications(), &AddonThemeNotifications::changed);
        QTemporaryDir profile; AddonManager manager(profile.path());
        {
            AddonThemeService themes(&manager);
            QCOMPARE(AddonThemeService::instance(), &themes);
            QVERIFY(updates.count() >= 1);
            updates.clear();
            themes.setUserAccentOverride("#12bc89");
            QCOMPARE(updates.count(), 1);
        }
        QVERIFY(AddonThemeService::instance() == nullptr);
        QCOMPARE(updates.count(), 2);
    }
    void userAndPlatformAccessibilityAreAuthoritative()
    {
        QTemporaryDir profile; AddonManager manager(profile.path()); AddonThemeService themes(&manager);
        themes.setUserAccentOverride("#12bc89");
        QCOMPARE(color(themes.tokens(false), "accent.primary"), QStringLiteral("#12bc89"));
        themes.setUserAccentOverride("default");
        QCOMPARE(color(themes.tokens(false), "accent.primary"), QStringLiteral("#d37918"));
        QVERIFY(themes.setHighContrast(true));
        QCOMPARE(color(themes.tokens(false), "text.primary"), QStringLiteral("#ffffff"));
        AddonThemeService restored(&manager); QVERIFY(restored.highContrast());
        QVERIFY(themes.setHighContrast(false));
        QVERIFY(themes.setReducedMotion(true));
        AddonThemeService motionRestored(&manager); QVERIFY(motionRestored.reducedMotion());
        PlatformThemePreferences platform; themes.observePlatformPreferences(&platform);
        platform.set("HighContrast"); QVERIFY(themes.highContrast()); QVERIFY(themes.platformHighContrast());
        QVERIFY(themes.setHighContrast(false)); QVERIFY(themes.highContrast());
        platform.set("NoPreference"); QVERIFY(!themes.highContrast());
    }
};
QTEST_GUILESS_MAIN(TestAddonTheme)
#include "test_addon_theme.moc"
