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
            QJsonObject{{"id", "browser"}, {"type", "ui.route"}, {"contract", "ui@1"}, {"hook", "library.navigation@1"}, {"view", "view.json"},
                {"parameters", QJsonObject{{"type", "object"}, {"additionalProperties", false}, {"properties", QJsonObject{{"query", QJsonObject{{"type", "string"}}}}}}},
                {"stateSchema", QJsonObject{{"type", "object"}, {"additionalProperties", false}, {"properties", QJsonObject{{"result", QJsonObject{{"type", "string"}}}}}}}},
            QJsonObject{{"id", "open"}, {"type", "ui.command"}, {"contract", "ui@1"}, {"hook", "player.actions@1"}, {"route", "browser"}},
            QJsonObject{{"id", "update"}, {"type", "ui.command"}, {"contract", "ui@1"}, {"hook", "player.actions@1"}, {"command", "update"}},
            QJsonObject{{"id", "section"}, {"type", "ui.section"}, {"contract", "ui@1"}, {"hook", "home.sections@1"}, {"view", "view.json"}},
            QJsonObject{{"id", "modal"}, {"type", "ui.modal"}, {"contract", "ui@1"}, {"view", "view.json"},
                {"resultSchema", QJsonObject{{"type", "object"}, {"additionalProperties", false}, {"properties", QJsonObject{{"code", QJsonObject{{"type", "string"}, {"maxLength", 8}}}}}, {"required", QJsonArray{"code"}}}}},
            QJsonObject{{"id", "subtitle-action"}, {"type", "ui.replacement"}, {"contract", "ui@1"}, {"hook", "actions.subtitles.open@1"}, {"command", "org.example.ui/update"}},
            QJsonObject{{"id", "home"}, {"type", "ui.replacement"}, {"contract", "ui@1"}, {"hook", "home.content@1"}, {"view", "view.json"}}
        };
        const QJsonObject manifest{{"formatVersion", 1}, {"id", "org.example.ui"}, {"name", "Example UI"}, {"version", "1.0.0"},
            {"publisher", "Tests"}, {"license", "GPL-2.0-or-later"}, {"source", "https://example.org/ui.git"},
            {"api", QJsonObject{{"addon", "^1.0.0"}, {"contracts", QJsonObject{{"ui", "^1.0.0"}}}}},
            {"files", QJsonArray{"addon.json", "LICENSE", "view.json"}}, {"contributions", contributions},
            {"permissions", QJsonObject{{"required", QJsonObject{{"ui.contribute", QJsonObject{{"hooks", QJsonArray{
                "home.sections@1", "home.content@1", "library.navigation@1", "player.actions@1", "actions.subtitles.open@1"}}}}}}, {"optional", QJsonObject{}}}}};
        write(path + QStringLiteral("/addon.json"), QJsonDocument(manifest).toJson());
        write(path + QStringLiteral("/LICENSE"), "Test fixture\n");
        write(path + QStringLiteral("/view.json"), QJsonDocument(view).toJson());
    }
    static void selectionPackage(const QString& path, bool libraryAccess, bool mediaTitles = false) {
        package(path);
        QFile file(path + QStringLiteral("/addon.json")); QVERIFY(file.open(QIODevice::ReadOnly));
        auto manifest = QJsonDocument::fromJson(file.readAll()).object(); file.close();
        auto permissions = manifest.value("permissions").toObject(), required = permissions.value("required").toObject();
        auto ui = required.value("ui.contribute").toObject(); auto hooks = ui.value("hooks").toArray();
        hooks.append("dialog.add-to-playlist.content@1"); hooks.append("subtitles.search.content@1"); ui.insert("hooks", hooks);
        required.insert("ui.contribute", ui);
        if (libraryAccess) required.insert("library.read", QJsonObject{{"collections", QJsonArray{"playlists"}}, {"fields", QJsonArray{"title"}}});
        if (mediaTitles) required.insert("metadata.context", QJsonObject{{"fields", QJsonArray{"title"}}});
        permissions.insert("required", required); manifest.insert("permissions", permissions);
        const QJsonObject choice{{"type", "object"}, {"additionalProperties", false}, {"properties", QJsonObject{
            {"ref", QJsonObject{{"type", "string"}}}, {"title", QJsonObject{{"type", "string"}}}, {"enabled", QJsonObject{{"type", "boolean"}}}}}};
        const QJsonObject parameters{{"type", "object"}, {"additionalProperties", false}, {"properties", QJsonObject{
            {"choiceSet", QJsonObject{{"type", "string"}}}, {"targets", QJsonObject{{"type", "array"}, {"items", choice}, {"maxItems", 200}}},
            {"assets", QJsonObject{{"type", "array"}, {"items", choice}, {"maxItems", 200}}}}}, {"required", QJsonArray{"choiceSet", "targets", "assets"}}};
        const QJsonObject result{{"type", "object"}, {"additionalProperties", false}, {"properties", QJsonObject{
            {"choiceSet", QJsonObject{{"type", "string"}}}, {"selected", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}, {"maxItems", 1}}},
            {"newName", QJsonObject{{"type", "string"}, {"maxLength", 256}}}}}, {"required", QJsonArray{"choiceSet", "selected"}}};
        auto contributions = manifest.value("contributions").toArray();
        for (const auto& pair : {qMakePair(QStringLiteral("playlist"), QStringLiteral("dialog.add-to-playlist.content@1")),
                                qMakePair(QStringLiteral("subtitles"), QStringLiteral("subtitles.search.content@1"))})
            contributions.append(QJsonObject{{"id", pair.first}, {"type", "ui.replacement"}, {"contract", "ui@1"}, {"hook", pair.second},
                {"view", "view.json"}, {"parameters", parameters}, {"resultSchema", result}});
        manifest.insert("contributions", contributions); write(path + QStringLiteral("/addon.json"), QJsonDocument(manifest).toJson());
    }
private slots:
    void typedSelectionNeedsExplicitChoiceAndDataScope() {
        const QString playlist = QStringLiteral("org.example.ui/playlist"), subtitles = QStringLiteral("org.example.ui/subtitles");
        const QString playlistHook = QStringLiteral("dialog.add-to-playlist.content@1"), subtitleHook = QStringLiteral("subtitles.search.content@1");
        for (const bool libraryAccess : {false, true}) {
            QTemporaryDir source, storage; selectionPackage(source.path(), libraryAccess); AddonManager manager(storage.path());
            QVERIFY2(manager.installLocalPackage(source.path(), true), qPrintable(manager.errorMessage())); QVERIFY(manager.setEnabled("org.example.ui", true));
            AddonUiRegistry registry(&manager); QVERIFY(!registry.canUseSelection(playlist, playlistHook));
            QVERIFY(registry.selectReplacement(playlistHook, playlist)); QCOMPARE(registry.canUseSelection(playlist, playlistHook), libraryAccess);
            QVERIFY(!registry.canUseSelection(playlist, subtitleHook));
            QVERIFY(registry.selectReplacement(subtitleHook, subtitles)); QVERIFY(registry.canUseSelection(subtitles, subtitleHook));
            QVERIFY(!registry.replacement(subtitleHook).value("selectionTitlesAllowed").toBool());
            registry.reportViewFailure(subtitles); QVERIFY(!registry.canUseSelection(subtitles, subtitleHook));
        }
        QTemporaryDir source, storage; selectionPackage(source.path(), true, true); AddonManager manager(storage.path());
        QVERIFY(manager.installLocalPackage(source.path(), true)); QVERIFY(manager.setEnabled("org.example.ui", true));
        AddonUiRegistry registry(&manager); QVERIFY(registry.selectReplacement(subtitleHook, subtitles));
        QVERIFY(registry.replacement(subtitleHook).value("selectionTitlesAllowed").toBool());
    }
    void typedResultsNeedACurrentActiveSessionAndDeclaredSchema() {
        QTemporaryDir source, storage; selectionPackage(source.path(), true); AddonManager manager(storage.path());
        QVERIFY(manager.installLocalPackage(source.path(), true)); QVERIFY(manager.setEnabled("org.example.ui", true)); AddonUiRegistry registry(&manager);
        const QString hook = QStringLiteral("dialog.add-to-playlist.content@1"), id = QStringLiteral("org.example.ui/playlist");
        QVERIFY(registry.selectReplacement(hook, id));
        const QVariantMap parameters{{"choiceSet", "one"}, {"targets", QVariantList{QVariantMap{{"ref", "target:0"}, {"title", "Films"}, {"enabled", true}}}}, {"assets", QVariantList{}}};
        const auto session = registry.createSession(id, parameters); QVERIFY(!session.isEmpty());
        const QVariantMap result{{"choiceSet", "one"}, {"selected", QVariantList{QStringLiteral("target:0")}}};
        QVERIFY(registry.validateSessionResult(session, result));
        QVERIFY(!registry.validateSessionResult(session, {{"choiceSet", "old"}, {"selected", QVariantList{QStringLiteral("target:0")}}}));
        QVERIFY(!registry.validateSessionResult(session, {{"choiceSet", "one"}, {"selected", QVariantList{1}}}));
        QVERIFY(!registry.validateSessionResult(session, {{"choiceSet", "one"}, {"selected", QVariantList{QStringLiteral("target:0"), QStringLiteral("target:1")}}}));
        registry.suspendSession(session, true); QVERIFY(!registry.validateSessionResult(session, result));
        registry.suspendSession(session, false); QVERIFY(registry.validateSessionResult(session, result));
        QVERIFY(registry.selectReplacement(hook, {})); QVERIFY(!registry.validateSessionResult(session, result));
        QVERIFY(registry.selectReplacement(hook, id)); registry.closeSession(session); QVERIFY(!registry.validateSessionResult(session, result));
        const auto active = registry.createSession(id, parameters); QVERIFY(manager.setEnabled("org.example.ui", false));
        QVERIFY(!registry.validateSessionResult(active, result));
    }
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
        QCOMPARE(registry.routes().size(), 1); QCOMPARE(registry.commands().size(), 2);
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
    void scopedSessionsKeepStatePrivateAndCancelOldWork() {
        QTemporaryDir source, storage; package(source.path()); AddonManager manager(storage.path());
        QVERIFY(manager.installLocalPackage(source.path(), true)); QVERIFY(manager.setEnabled("org.example.ui", true));
        QString request; QVariantMap invocation; QStringList canceled;
        AddonUiRegistry registry(&manager, {
            [&](const QString&, const QString&, const QVariantMap& context) { invocation = context; request = QString::number(request.toInt() + 1); return request; },
            [&](const QString& id) { canceled.append(id); }});
        const auto first = registry.createSession("org.example.ui/browser", {{"query", "radio"}},
            {{"surface", "library.navigation@1"}, {"url", "file:///private/movie.mkv"}, {"identity", QVariantMap{{"title", "Private title"}}}});
        const auto second = registry.createSession("org.example.ui/browser");
        QVERIFY(!first.isEmpty() && !second.isEmpty());
        const auto initial = registry.session(first).value("state").toMap();
        QCOMPARE(initial.value("parameters").toMap().value("query").toString(), QStringLiteral("radio"));
        QVERIFY(!initial.value("context").toMap().contains("url"));
        QVERIFY(!initial.value("context").toMap().contains("identity"));
        QVERIFY(registry.dispatchSession(first, "org.example.ui/update"));
        QVERIFY(!invocation.value("ui").toMap().contains("url"));
        registry.operationFinished(request, "org.example.ui/update", "match", {{"patch", QVariantMap{{"result", "Ready"}}}}, {});
        QCOMPARE(registry.session(first).value("state").toMap().value("result").toString(), QStringLiteral("Ready"));
        QVERIFY(!registry.session(second).value("state").toMap().contains("result"));
        QVERIFY(!registry.dispatchSession(first, "other.package/action"));
        QVERIFY(registry.dispatchSession(first, "org.example.ui/update")); const auto stale = request;
        registry.suspendSession(first, true); QVERIFY(canceled.contains(stale));
        QVERIFY(!registry.dispatchSession(first, "org.example.ui/update"));
        registry.operationFinished(stale, "org.example.ui/update", "match", {{"patch", QVariantMap{{"result", "Stale"}}}}, {});
        QCOMPARE(registry.session(first).value("state").toMap().value("result").toString(), QStringLiteral("Ready"));
        registry.suspendSession(first, false); QVERIFY(registry.dispatchSession(first, "org.example.ui/update"));
        const auto closed = request; registry.closeSession(first); QVERIFY(canceled.contains(closed));
        QVERIFY(registry.session(first).isEmpty());
        QSignalSpy failed(&registry, &AddonUiRegistry::sessionFailed);
        QVERIFY(registry.dispatchSession(second, "org.example.ui/update"));
        registry.operationFinished(request, "org.example.ui/update", "match", {{"patch", QVariantMap{{"__proto__", true}}}}, {});
        QCOMPARE(failed.size(), 1);
        QVERIFY(!registry.session(second).value("state").toMap().contains("__proto__"));
        QVERIFY(manager.setEnabled("org.example.ui", false)); QVERIFY(registry.session(second).isEmpty());
    }
    void dataSchemasRejectCodeAndMalformedResults() {
        const QJsonObject schema{{"type", "object"}, {"additionalProperties", false},
            {"properties", QJsonObject{{"code", QJsonObject{{"type", "string"}, {"maxLength", 8}}}}}, {"required", QJsonArray{"code"}}};
        QVERIFY(validateUiSchema(schema)); QVERIFY(validateUiValue(schema, QJsonObject{{"code", "Ready"}}));
        QVERIFY(!validateUiValue(schema, QJsonObject{{"code", 42}}));
        QVERIFY(!validateUiValue(schema, QJsonObject{{"code", "123456789"}}));
        QVERIFY(!validateUiValue(schema, QJsonObject{{"code", "Ready"}, {"extra", true}}));
        auto malformed = schema; malformed.insert("script", "readFile()"); QVERIFY(!validateUiSchema(malformed));
        malformed = schema; malformed.insert("required", QJsonArray{"missing"}); QVERIFY(!validateUiSchema(malformed));
        malformed = schema; malformed.insert("additionalProperties", true); QVERIFY(!validateUiSchema(malformed));
        {
            QTemporaryDir malformedSource; package(malformedSource.path());
            QFile file(malformedSource.path() + QStringLiteral("/addon.json")); QVERIFY(file.open(QIODevice::ReadOnly));
            auto manifest = QJsonDocument::fromJson(file.readAll()).object(); file.close();
            auto contributions = manifest.value("contributions").toArray(); auto route = contributions[0].toObject();
            route.insert(QStringLiteral("stateSchema"), malformed); contributions[0] = route; manifest.insert(QStringLiteral("contributions"), contributions);
            Manifest parsed; QString error;
            QVERIFY(!Manifest::parse(QJsonDocument(manifest).toJson(), &parsed, &error));
        }
        QTemporaryDir source, storage; package(source.path()); AddonManager manager(storage.path());
        QVERIFY(manager.installLocalPackage(source.path(), true)); QVERIFY(manager.setEnabled("org.example.ui", true));
        AddonUiRegistry registry(&manager);
        QSignalSpy requested(&registry, &AddonUiRegistry::modalSessionRequested), completed(&registry, &AddonUiRegistry::modalFinished);
        QVERIFY(registry.openModal("org.example.ui/modal")); QCOMPARE(requested.size(), 1);
        const auto ticket = requested.last()[0].toString();
        QVERIFY(!registry.completeModal(ticket, {{"code", 42}})); QCOMPARE(completed.size(), 0);
        QVERIFY(registry.completeModal(ticket, {{"code", "Ready"}})); QCOMPARE(completed.size(), 1);
        QVERIFY(completed.last()[1].toBool()); QVERIFY(!registry.completeModal(ticket, {{"code", "Again"}}));
        QVERIFY(registry.openModal("org.example.ui/modal")); registry.cancelModal(requested.last()[0].toString());
        QCOMPARE(completed.size(), 2); QVERIFY(!completed.last()[1].toBool());
    }
    void subtitleActionReportsCompletionForFallback() {
        QTemporaryDir source, storage; package(source.path()); AddonManager manager(storage.path());
        QVERIFY(manager.installLocalPackage(source.path(), true)); QVERIFY(manager.setEnabled("org.example.ui", true));
        AddonUiRegistry registry(&manager, {[](const QString&, const QString&, const QVariantMap&) { return QStringLiteral("request"); }, [](const QString&) {}});
        QVERIFY(registry.selectReplacement("actions.subtitles.open@1", "org.example.ui/subtitle-action"));
        QSignalSpy finished(&registry, &AddonUiRegistry::replacementFinished);
        const auto ticket = registry.executeReplacement("actions.subtitles.open@1"); QVERIFY(!ticket.isEmpty());
        registry.operationFinished("request", "org.example.ui/update", "error", {}, {});
        QCOMPARE(finished.size(), 1); QCOMPARE(finished.last()[0].toString(), ticket); QVERIFY(!finished.last()[1].toBool());
    }
};
QTEST_GUILESS_MAIN(TestAddonUiRegistry)
#include "test_addon_ui_registry.moc"
