// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include "addons/addon_ui_registry.hpp"

using namespace vlc::addons;
class TestAddonUiRegistry : public QObject
{
    Q_OBJECT
    static void write(const QString& path, const QByteArray& bytes) {
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), bytes.size());
    }
    static void package(const QString& path) {
        const QJsonObject view{{"contract", "ui@1"}, {"id", "browser"},
            {"root", QJsonObject{{"type", "Page"}, {"children", QJsonArray{QJsonObject{{"type", "Text"}, {"text", "Example content"}}}}}}};
        const QJsonArray contributions{
            QJsonObject{{"id", "browser"}, {"type", "ui.route"}, {"contract", "ui@1"}, {"hook", "library.navigation@1"}, {"view", "view.json"}},
            QJsonObject{{"id", "open"}, {"type", "ui.command"}, {"contract", "ui@1"}, {"hook", "player.actions@1"}, {"route", "browser"}},
            QJsonObject{{"id", "section"}, {"type", "ui.section"}, {"contract", "ui@1"}, {"hook", "home.sections@1"}, {"view", "view.json"}},
            QJsonObject{{"id", "home"}, {"type", "ui.replacement"}, {"contract", "ui@1"}, {"hook", "home.content@1"}, {"view", "view.json"}}
        };
        const QJsonObject manifest{{"formatVersion", 1}, {"id", "org.example.ui"}, {"name", "Example UI"}, {"version", "1.0.0"},
            {"publisher", "Tests"}, {"license", "GPL-2.0-or-later"}, {"source", "https://example.org/ui.git"},
            {"api", QJsonObject{{"addon", "^1.0.0"}, {"contracts", QJsonObject{{"ui", "^1.0.0"}}}}},
            {"files", QJsonArray{"addon.json", "LICENSE", "view.json"}}, {"contributions", contributions},
            {"permissions", QJsonObject{{"required", QJsonObject{{"ui.contribute", QJsonObject{{"hooks", QJsonArray{
                "home.sections@1", "home.content@1", "library.navigation@1", "player.actions@1"}}}}}}, {"optional", QJsonObject{}}}}};
        write(path + QStringLiteral("/addon.json"), QJsonDocument(manifest).toJson());
        write(path + QStringLiteral("/LICENSE"), "Test fixture\n");
        write(path + QStringLiteral("/view.json"), QJsonDocument(view).toJson());
    }
private slots:
    void boundedVocabularyRejectsCodeAndExternalSources() {
        QVERIFY(AddonUiRegistry::validViewDocument({{"contract", "ui@1"}, {"root", QJsonObject{{"type", "Text"}, {"text", "Hello"}}}}));
        QVERIFY(!AddonUiRegistry::validViewDocument({{"contract", "ui@1"}, {"root", QJsonObject{{"type", "Loader"}}}}));
        QVERIFY(!AddonUiRegistry::validViewDocument({{"contract", "ui@1"}, {"root", QJsonObject{{"type", "Image"}, {"source", "https://example.org/x.svg"}}}}));
        QVERIFY(!AddonUiRegistry::validViewDocument({{"contract", "ui@1"}, {"root", QJsonObject{{"type", "Text"}, {"__proto__", QJsonObject{}}}}}));
    }
    void explicitReplacementAndSafeRouteCommand() {
        QTemporaryDir source, storage; package(source.path()); AddonManager manager(storage.path());
        QVERIFY(manager.installLocalPackage(source.path(), true)); QVERIFY(manager.setEnabled(QStringLiteral("org.example.ui"), true));
        AddonUiRegistry registry(&manager);
        QCOMPARE(registry.routes().size(), 1); QCOMPARE(registry.commands().size(), 1);
        QCOMPARE(registry.sections(QStringLiteral("home.sections@1")).size(), 1);
        QVERIFY(registry.replacement(QStringLiteral("home.content@1")).isEmpty());
        QVERIFY(!registry.selectReplacement(QStringLiteral("permissions.content@1"), QStringLiteral("org.example.ui/home")));
        QVERIFY(!registry.selectReplacement(QStringLiteral("home.content@1"), QStringLiteral("org.example.ui/browser")));
        QVERIFY(registry.selectReplacement(QStringLiteral("home.content@1"), QStringLiteral("org.example.ui/home")));
        QVERIFY(!registry.replacement(QStringLiteral("home.content@1")).isEmpty());
        QSignalSpy opened(&registry, &AddonUiRegistry::routeRequested);
        QVERIFY(registry.triggerCommand(QStringLiteral("org.example.ui/open")));
        QCOMPARE(opened.size(), 1); QCOMPARE(opened.last()[0].toString(), QStringLiteral("addon/org.example.ui/browser"));
        QVERIFY(!registry.openRoute(QStringLiteral("home")));
        QVERIFY(!registry.triggerCommand(QStringLiteral("core.pause")));
        QVERIFY(!registry.openRoute(QStringLiteral("addon/org.example.ui/browser"), {{"unapproved", true}}));
        registry.reportViewFailure(QStringLiteral("org.example.ui/home"));
        QVERIFY(registry.replacement(QStringLiteral("home.content@1")).isEmpty());
        QVERIFY(!registry.errorMessage().isEmpty());
        QVERIFY(manager.setEnabled(QStringLiteral("org.example.ui"), false));
        QVERIFY(registry.routes().isEmpty()); QVERIFY(registry.commands().isEmpty());
        QVERIFY(!registry.triggerCommand(QStringLiteral("org.example.ui/open")));
    }
};
QTEST_GUILESS_MAIN(TestAddonUiRegistry)
#include "test_addon_ui_registry.moc"
