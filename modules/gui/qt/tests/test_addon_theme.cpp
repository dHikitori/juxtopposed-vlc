// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <cmath>
#include "addons/addon_manager.hpp"
#include "addons/addon_theme_service.hpp"
#include "addons/addon_catalogue_view.hpp"

using namespace vlc::addons;

namespace {
struct ThemeReadGate {
    std::mutex mutex; std::condition_variable changed; bool released=false;
    std::atomic_bool entered{false};
    void wait() { std::unique_lock<std::mutex> lock(mutex); entered=true; changed.wait(lock,[this] { return released; }); }
    void release() { std::lock_guard<std::mutex> lock(mutex); released=true; changed.notify_all(); }
};
struct ThemeDisplay {
    QTemporaryDir directory;
    StoreAuthority authority{directory.path()};
    AddonStreamLifetime lifetime;
    AddonManager manager{directory.filePath(QStringLiteral("profile"))};
    std::unique_ptr<AddonCatalogueView> view;
    DisplayCatalogueSnapshot snapshot;
    std::shared_ptr<ThemeReadGate> gate;
    std::atomic_uint reads{0}; std::atomic_bool readOnOwner{false};
    QThread* owner=QThread::currentThread();
    const QString id=QStringLiteral("org.example.display/midnight");
    explicit ThemeDisplay(QJsonObject document,bool blocked=false) {
        DisplayCataloguePackage row;
        row.package.manifest.id=QStringLiteral("org.example.display");
        row.package.manifest.name=QStringLiteral("Display theme"); row.package.manifest.version=QStringLiteral("1.0.0");
        row.package.manifest.files={QStringLiteral("theme.json")};
        row.package.digest=QByteArray(64,'a'); row.package.generation=1; row.package.enabled=true; row.savedEnabled=true;
        row.schema={{"type","object"},{"properties",QJsonObject{}}};
        Contribution contribution; contribution.id=QStringLiteral("midnight"); contribution.type=QStringLiteral("theme");
        contribution.document={{"id",contribution.id},{"type",contribution.type},{"theme",QStringLiteral("theme.json")}};
        row.package.manifest.contributions={contribution}; row.resources.insert(QStringLiteral("midnight/theme"),document);
        snapshot.packages={row}; snapshot.selectedTheme=id;
        snapshot.themes={QVariantMap{{"id",id},{"packageId",row.package.manifest.id},{"name",row.package.manifest.name},
            {"field",QStringLiteral("theme")},{"enabled",true},{"light",true},{"dark",true}}};
        snapshot.displayPreferences={{"appearance",QJsonObject{{"selectedTheme",id},{"highContrast",false},{"reducedMotion",false}}}};
        if(blocked) gate=std::make_shared<ThemeReadGate>();
        AddonCatalogueView::Backend backend;
        backend.capture=[this]() -> std::optional<DisplayCatalogueSource> {
            DisplayCatalogueSource source; source.queuedAuthority=authority.capture();
            auto state=snapshot; state.queuedAuthority=state.authority=source.queuedAuthority;
            const auto wait=gate;
            source.reader=[this,state=std::move(state),wait](const auto&,DisplayCatalogueSnapshot* out,QString*) {
                ++reads; readOnOwner=QThread::currentThread()==owner;
                if(wait) wait->wait();
                *out=state; return true;
            };
            return source;
        };
        backend.subscribe=[this](std::function<void()> wake) { return authority.onChanged(std::move(wake)); };
        view=std::make_unique<AddonCatalogueView>(std::move(backend),lifetime.handle(),&manager);
        view->setObjectName(QStringLiteral("hakoneDisplayCatalogue"));
    }
    ~ThemeDisplay() {
        if(gate) gate->release();
        if(view) view->close();
        lifetime.close(); lifetime.handle().waitQt(std::chrono::seconds(5));
        view.reset();
    }
};
}

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
        QFile file(QStringLiteral(VLC_ADDON_SOURCE_ROOT "/modules/gui/qt/tests/fixtures/addons/midnight/themes/midnight.json"));
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
        const QDir source(QStringLiteral(VLC_ADDON_SOURCE_ROOT "/modules/gui/qt/tests/fixtures/addons/midnight"));
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
    void attachedLoadingNeverUsesTheLegacyPreferenceFile() {
        ThemeDisplay display(reference(),true);
        QVERIFY(QDir().mkpath(display.manager.storageDirectory()));
        QFile poison(QDir(display.manager.storageDirectory()).filePath(QStringLiteral("theme-selection.json")));
        QVERIFY(poison.open(QIODevice::WriteOnly));
        QVERIFY(poison.write("{\"selectedTheme\":\"org.example.poison/theme\"}")>0); poison.close();
        AddonThemeService service(&display.manager);
        QVERIFY(service.loading()); QVERIFY(service.selectedTheme().isEmpty());
        QVERIFY(service.availableThemes().isEmpty());
        QTRY_VERIFY(display.gate->entered.load());
        display.gate->release(); QTRY_VERIFY(display.view->isCurrent());
        QCOMPARE(service.selectedTheme(),display.id); QCOMPARE(service.availableThemes().size(),1);
        QVERIFY(!service.loading()); QVERIFY(!service.usingFallback()); QVERIFY(!display.readOnOwner.load());
    }
    void attachedRepeatedReadsAndCorePreferencesDoNotRecapturePackages() {
        ThemeDisplay display(reference()); QTRY_VERIFY(display.view->isCurrent());
        AddonThemeService service(&display.manager);
        QCOMPARE(service.availableThemes().size(),1); QVERIFY(!service.usingFallback());
        const auto reads=display.reads.load(); const auto revision=service.revision();
        for(int i=0;i<100;++i) {
            service.refresh(); service.availableThemes(); service.tokens(false); service.tokens(true);
        }
        QCOMPARE(service.revision(),revision); QCOMPARE(display.reads.load(),reads);
        QVERIFY(service.setHighContrast(true)); QVERIFY(service.setReducedMotion(true));
        QTest::qWait(40);
        QVERIFY(service.highContrast()); QVERIFY(service.reducedMotion());
        QCOMPARE(display.reads.load(),reads); QVERIFY(!display.readOnOwner.load());
    }
    void attachedRefreshKeepsThePaletteAndRejectsStaleChanges() {
        ThemeDisplay display(reference()); QTRY_VERIFY(display.view->isCurrent());
        AddonThemeService service(&display.manager); const auto before=service.tokens(false);
        const auto themes=service.availableThemes();
        display.gate=std::make_shared<ThemeReadGate>(); display.view->refresh();
        QTRY_VERIFY(display.gate->entered.load());
        QCOMPARE(service.availableThemes(),themes); QCOMPARE(service.tokens(false),before);
        QVERIFY(!service.selectTheme(QString())); QVERIFY(!service.setHighContrast(true));
        QCOMPARE(service.selectedTheme(),display.id); QVERIFY(!service.highContrast());
        display.gate->release(); QTRY_VERIFY(display.view->isCurrent());
        QCOMPARE(service.tokens(false),before); QVERIFY(!service.usingFallback());
        display.view.reset(); service.refresh();
        QCOMPARE(service.availableThemes(),themes); QCOMPARE(service.tokens(false),before);
        QVERIFY(!service.selectTheme(QString()));
    }
    void attachedDisableAndRestoreUseAcceptedSnapshots() {
        ThemeDisplay display(reference()); QTRY_VERIFY(display.view->isCurrent());
        AddonThemeService service(&display.manager); const auto before=service.tokens(false);
        const auto themes=display.snapshot.themes;
        display.snapshot.packages[0].package.enabled=false; display.snapshot.themes.clear();
        display.authority.invalidate(); QTRY_VERIFY(service.usingFallback());
        QVERIFY(service.availableThemes().isEmpty()); QCOMPARE(service.selectedTheme(),display.id);
        display.snapshot.packages[0].package.enabled=true; display.snapshot.themes=themes;
        display.authority.invalidate();
        QTRY_VERIFY(display.view->isCurrent() && !service.loading() && !service.usingFallback());
        QCOMPARE(service.tokens(false),before); QCOMPARE(service.availableThemes().size(),1);
    }
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
