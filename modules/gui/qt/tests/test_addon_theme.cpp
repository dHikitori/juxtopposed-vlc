// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <cmath>
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
    static double contrast(const QString& first, const QString& second)
    {
        const auto luminance = [](const QString& color) {
            const auto channel = [&](int offset) { const auto value = color.mid(offset, 2).toInt(nullptr, 16) / 255.; return value <= .04045 ? value / 12.92 : std::pow((value + .055) / 1.055, 2.4); };
            return .2126 * channel(1) + .7152 * channel(3) + .0722 * channel(5);
        };
        const auto a = luminance(first), b = luminance(second); return (std::max(a, b) + .05) / (std::min(a, b) + .05);
    }
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
        QVERIFY(!AddonThemeService::validate(variant(document, "colors", "status.danger", "#171923"), &error));
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
        QCOMPARE(high.value("baseAccent").toString(), QStringLiteral("#12bc89"));
        QVERIFY(contrast(color(high, "accent.primary"), color(high, "surface.selected")) >= 4.5);
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
        const auto id = QStringLiteral("org.hakone.example.midnight/midnight");
        QVERIFY(!themes.selectTheme(id));
        QVERIFY(manager.setEnabled("org.hakone.example.midnight", true));
        QCOMPARE(themes.availableThemes().size(), 1);
        QVERIFY(themes.selectedTheme().isEmpty());
        QSignalSpy changed(&themes, &AddonThemeService::revisionChanged);
        QVERIFY(themes.selectTheme(id));
        QCOMPARE(color(themes.tokens(false), "surface.base"), QStringLiteral("#171923"));
        const auto themeAccent = themes.tokens(false).value("baseAccent");
        themes.setUserAccentOverride("#12bc89");
        QCOMPARE(themes.tokens(false).value("baseAccent").toString(), QStringLiteral("#12bc89"));
        themes.setUserAccentOverride("default");
        QCOMPARE(themes.tokens(false).value("baseAccent"), themeAccent);
        QVERIFY(!themes.usingFallback());
        QVERIFY(manager.setEnabled("org.hakone.example.midnight", false));
        QVERIFY(themes.usingFallback()); QVERIFY(!themes.errorMessage().isEmpty());
        QCOMPARE(color(themes.tokens(false), "surface.base"), QStringLiteral("#1b1b1b"));
        QVERIFY(manager.setEnabled("org.hakone.example.midnight", true));
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
    void highContrastRetainsTheSelectedAccent()
    {
        for (const auto light : {false, true})
            for (const QString accent : {"#d37918", "#e15032", "#c49e23", "#19a234", "#18aad3", "#185dd3", "#8e42ff", "#f052b3", "#afafaf", "#12bc89", "#000000", "#ffffff"}) {
                const auto high = AddonThemeService::resolve(reference(), light, accent, true);
                QCOMPARE(high.value("baseAccent").toString(), accent);
                const auto rendered = color(high, "accent.primary");
                bool alreadyReadable = true;
                for (const auto* surface : {"surface.base", "surface.panel", "surface.field", "surface.hover", "surface.selected"}) {
                    alreadyReadable &= contrast(accent, color(high, surface)) >= 4.5;
                    QVERIFY2(contrast(rendered, color(high, surface)) >= 4.5, qPrintable(accent));
                }
                if (alreadyReadable) QCOMPARE(rendered, accent);
                // The contrast shade preserves the ordering of the chosen RGB channels.
                for (int a : {1, 3, 5}) for (int b : {1, 3, 5})
                    if (accent.mid(a, 2).toInt(nullptr, 16) > accent.mid(b, 2).toInt(nullptr, 16))
                        QVERIFY(rendered.mid(a, 2).toInt(nullptr, 16) >= rendered.mid(b, 2).toInt(nullptr, 16));
                QVERIFY(contrast(color(high, "text.onAccent"), rendered) >= 4.5);
                QCOMPARE(color(AddonThemeService::resolve(reference(), light, accent, false), "accent.primary"), accent);
            }
    }
    void tokenCacheOwnsItsKeyAndSeparatesEveryOption()
    {
        AddonThemeService themes(nullptr);
        for (int iteration = 0; iteration < 64; ++iteration)
            for (const auto light : {false, true}) for (const auto high : {false, true}) for (const auto reduced : {false, true}) {
                const QString accent = iteration % 2 ? QStringLiteral("#12bc89") : QStringLiteral("#185dd3");
                const auto expected = AddonThemeService::resolve({}, light, accent, high, reduced);
                QCOMPARE(themes.tokens(light, accent, high, reduced), expected);
                // Disturb stack and heap storage between repeated cache reads.
                const QByteArray noise(8192, char(iteration));
                QVERIFY(!noise.isEmpty());
                QCOMPARE(themes.tokens(light, accent, high, reduced), expected);
            }
    }
    void textualStatusRolesHaveContrastAcrossCoreAndAddonSurfaces()
    {
        for (const auto light : {false, true}) for (const auto highContrast : {false, true}) {
            for (const auto& document : {QJsonObject{}, reference()}) {
                const auto snapshot = AddonThemeService::resolve(document, light, QString(), highContrast);
                for (const auto& surface : {"surface.base", "surface.panel", "surface.field", "surface.hover", "surface.selected", "surface.menu", "status.dangerBackground"})
                    QVERIFY2(contrast(color(snapshot, "status.danger"), color(snapshot, surface)) >= 4.5, surface);
                QVERIFY(contrast(color(snapshot, "text.onAccent"), color(snapshot, "accent.primary")) >= 4.5);
                if (!highContrast) continue;
                for (const auto& text : {"text.primary", "text.secondary", "text.muted", "text.disabled", "text.heading", "text.playerHeading", "accent.primary", "status.danger"})
                    for (const auto& surface : {"surface.base", "surface.panel", "surface.field", "surface.hover", "surface.selected", "surface.menu"})
                        QVERIFY2(contrast(color(snapshot, text), color(snapshot, surface)) >= 4.5, text);
            }
        }
    }
};
QTEST_GUILESS_MAIN(TestAddonTheme)
#include "test_addon_theme.moc"
