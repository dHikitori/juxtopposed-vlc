// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include "addons/addon_ui_registry.hpp"
#include "addons/addon_panel_icon.hpp"
#include "addons/addon_catalogue_view.hpp"
#include "addons/addon_stream_lifetime.hpp"
#include <memory>

using namespace vlc::addons;
class TestAddonUiRegistry : public QObject
{
    Q_OBJECT
    std::unique_ptr<AddonStreamLifetime> m_lifetime;
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
    static void panelPackage(const QString& path, const QByteArray& icon) {
        const QJsonObject view{{"contract", "ui@1"}, {"id", "panel"},
            {"root", QJsonObject{{"type", "Page"}, {"children", QJsonArray{QJsonObject{{"type", "Text"}, {"text", QJsonObject{{"bind", "state.status"}}}}}}}}};
        const QJsonObject state{{"type", "object"}, {"additionalProperties", false}, {"properties", QJsonObject{{"status", QJsonObject{{"type", "string"}}}}}};
        const QJsonArray contributions{
            QJsonObject{{"id", "episodes"}, {"type", "ui.panel"}, {"contract", "ui@1"}, {"hook", "player.panels@1"}, {"name", "Episodes"},
                {"label", "Show the episodes"}, {"icon", "icon.svg"}, {"view", "view.json"}, {"command", "context"},
                {"placement", "footerRight"}, {"stateSchema", state}, {"state", QJsonObject{{"status", "loading"}}}},
            QJsonObject{{"id", "context"}, {"type", "ui.command"}, {"contract", "ui@1"}, {"label", "Context"}, {"command", "context"}},
            QJsonObject{{"id", "play"}, {"type", "ui.command"}, {"contract", "ui@1"}, {"label", "Play"}, {"command", "play"}}};
        const QJsonObject manifest{{"formatVersion", 1}, {"id", "org.example.panel"}, {"name", "Example panel"}, {"version", "1.0.0"},
            {"publisher", "Tests"}, {"license", "GPL-2.0-or-later"}, {"source", "https://example.org/panel.git"},
            {"api", QJsonObject{{"addon", "^1.0.0"}, {"contracts", QJsonObject{{"ui", "^1.0.0"}}}}},
            {"files", QJsonArray{"addon.json", "LICENSE", "view.json", "icon.svg"}}, {"contributions", contributions},
            {"permissions", QJsonObject{{"required", QJsonObject{{"ui.contribute", QJsonObject{{"hooks", QJsonArray{"player.panels@1"}}}},
                {"media.siblings", QJsonObject{{"sources", QJsonArray{"folder", "feed"}}}}}}, {"optional", QJsonObject{}}}}};
        write(path + QStringLiteral("/addon.json"), QJsonDocument(manifest).toJson());
        write(path + QStringLiteral("/LICENSE"), "Test fixture\n");
        write(path + QStringLiteral("/view.json"), QJsonDocument(view).toJson());
        write(path + QStringLiteral("/icon.svg"), icon);
    }
private slots:
    void init() {
        m_lifetime=std::make_unique<AddonStreamLifetime>();
        QVERIFY(AddonStreamLifetime::registerApplication(m_lifetime->handle()));
    }
    void cleanup() {
        const auto handle=m_lifetime->handle();handle.close();QVERIFY(handle.waitQt(std::chrono::seconds(5)));
        QVERIFY(AddonStreamLifetime::unregisterApplication(handle));m_lifetime.reset();
    }
    void attachedCatalogueLoadsPendingDocumentsIntoTheSameSession() {
        QTemporaryDir source,storage;package(source.path());AddonManager manager(storage.path());QVERIFY(manager.installLocalPackage(source.path(),true));QVERIFY(manager.setEnabled("org.example.ui",true));
        auto catalogue=std::make_unique<AddonCatalogueView>(&manager,m_lifetime->handle());int invokes=0;
        AddonUiRegistry registry(&manager,{[&](const QString&,const QString&,const QVariantMap&){++invokes;return QStringLiteral("request");},[](const QString&){}});
        QVERIFY(!registry.available());QVERIFY(registry.loading());QVERIFY(registry.routes().isEmpty());QVERIFY(!registry.triggerCommand("org.example.ui/update"));
        QTRY_VERIFY(catalogue->isCurrent());QTRY_VERIFY(registry.available());QCOMPARE(registry.routes().size(),1);
        QSignalSpy base(&registry,&AddonUiRegistry::changed),failed(&registry,&AddonUiRegistry::sessionFailed);
        const auto id=registry.createSession("org.example.ui/browser",{{"query","radio"}});QVERIFY(!id.isEmpty());
        QVERIFY(registry.session(id).value("pending").toBool());QVERIFY(!registry.dispatchSession(id,"org.example.ui/update"));QCOMPARE(invokes,0);
        QTRY_VERIFY(!registry.session(id).value("document").toMap().isEmpty());QVERIFY(!registry.session(id).value("pending").toBool());QCOMPARE(failed.size(),0);QCOMPARE(base.size(),0);
        QVERIFY(registry.dispatchSession(id,"org.example.ui/update"));QCOMPARE(invokes,1);
        registry.operationFinished("request","org.example.ui/update","match",{{"patch",QVariantMap{{"result","Kept"}}}},{});
        QCOMPARE(registry.session(id).value("state").toMap().value("result").toString(),QStringLiteral("Kept"));
        const auto other=registry.createSession("org.example.ui/modal");QVERIFY(!other.isEmpty());
        QTRY_VERIFY(!registry.session(other).value("document").toMap().isEmpty());
        QCOMPARE(registry.session(id).value("state").toMap().value("result").toString(),QStringLiteral("Kept"));QCOMPARE(base.size(),0);
        registry.closeSession(other);registry.closeSession(id);
    }
    void declaredLocaleMustBeReadyBeforeSessionActions_data() {
        QTest::addColumn<bool>("valid");QTest::newRow("valid")<<true;QTest::newRow("malformed")<<false;
    }
    void declaredLocaleMustBeReadyBeforeSessionActions() {
        QFETCH(bool,valid);QTemporaryDir source,storage;package(source.path());
        QFile file(source.filePath("addon.json"));QVERIFY(file.open(QIODevice::ReadOnly));auto manifest=QJsonDocument::fromJson(file.readAll()).object();file.close();
        auto files=manifest.value("files").toArray();files.append("locale.json");manifest.insert("files",files);
        manifest.insert("locales",QJsonObject{{"default","en"},{"files",QJsonObject{{"en","locale.json"}}}});
        write(source.filePath("addon.json"),QJsonDocument(manifest).toJson());write(source.filePath("locale.json"),valid?QByteArray("{\"label\":\"Ready\"}"):QByteArray("[]"));
        AddonManager manager(storage.path());QVERIFY(manager.installLocalPackage(source.path(),true));QVERIFY(manager.setEnabled("org.example.ui",true));
        auto catalogue=std::make_unique<AddonCatalogueView>(&manager,m_lifetime->handle());int invokes=0;
        AddonUiRegistry registry(&manager,{[&](const QString&,const QString&,const QVariantMap&){++invokes;return QStringLiteral("request");},[](const QString&){}});QTRY_VERIFY(registry.available());
        const auto id=registry.createSession("org.example.ui/browser");QVERIFY(!id.isEmpty());QVERIFY(!registry.dispatchSession(id,"org.example.ui/update"));
        if(valid) {
            QTRY_COMPARE(registry.session(id).value("messages").toMap().value("label").toString(),QStringLiteral("Ready"));
            QVERIFY(registry.dispatchSession(id,"org.example.ui/update"));QCOMPARE(invokes,1);
        } else {
            QTRY_VERIFY(registry.session(id).value("unavailable").toBool());QVERIFY(registry.session(id).value("document").toMap().isEmpty());
            QVERIFY(!registry.dispatchSession(id,"org.example.ui/update"));QCOMPARE(invokes,0);QVERIFY(registry.errorMessage().isEmpty());
        }
        registry.closeSession(id);
    }
    void attachedRefreshRetainsSafeStateAndWithdrawalRetiresIt() {
        QTemporaryDir source,storage;package(source.path());AddonManager manager(storage.path());QVERIFY(manager.installLocalPackage(source.path(),true));QVERIFY(manager.setEnabled("org.example.ui",true));
        auto catalogue=std::make_unique<AddonCatalogueView>(&manager,m_lifetime->handle());int invokes=0;
        AddonUiRegistry registry(&manager,{[&](const QString&,const QString&,const QVariantMap&){++invokes;return QStringLiteral("request");},[](const QString&){}});
        QTRY_VERIFY(registry.available());const auto id=registry.createSession("org.example.ui/browser");QVERIFY(!id.isEmpty());QTRY_VERIFY(!registry.session(id).value("document").toMap().isEmpty());
        QVERIFY(registry.dispatchSession(id,"org.example.ui/update"));registry.operationFinished("request","org.example.ui/update","match",{{"patch",QVariantMap{{"result","Private state"}}}},{});
        QSignalSpy failed(&registry,&AddonUiRegistry::sessionFailed);catalogue->refresh();QVERIFY(!registry.available());QVERIFY(catalogue->retainedCatalogueCurrent());
        QVERIFY(!registry.session(id).value("document").toMap().isEmpty());QVERIFY(!registry.dispatchSession(id,"org.example.ui/update"));QVERIFY(!registry.openRoute("addon/org.example.ui/browser"));QVERIFY(registry.createSession("org.example.ui/browser").isEmpty());
        QTRY_VERIFY(registry.available());QCOMPARE(failed.size(),0);QCOMPARE(registry.session(id).value("state").toMap().value("result").toString(),QStringLiteral("Private state"));
        QVERIFY(manager.setEnabled("org.example.ui",false));QVERIFY(!registry.available());QVERIFY(!registry.triggerCommand("org.example.ui/update"));
        QTRY_VERIFY(registry.session(id).isEmpty());QVERIFY(failed.size()>0);QTRY_VERIFY(registry.available());QVERIFY(registry.routes().isEmpty());QCOMPARE(invokes,1);
    }
    void attachedViewDestructionCannotFallBackToManager() {
        QTemporaryDir source,storage;package(source.path());AddonManager manager(storage.path());QVERIFY(manager.installLocalPackage(source.path(),true));QVERIFY(manager.setEnabled("org.example.ui",true));
        auto catalogue=std::make_unique<AddonCatalogueView>(&manager,m_lifetime->handle());int invokes=0;
        AddonUiRegistry registry(&manager,{[&](const QString&,const QString&,const QVariantMap&){++invokes;return QStringLiteral("request");},[](const QString&){}});QTRY_VERIFY(registry.available());QCOMPARE(registry.routes().size(),1);
        const auto id=registry.createSession("org.example.ui/browser");QVERIFY(!id.isEmpty());QTRY_VERIFY(!registry.session(id).value("document").toMap().isEmpty());catalogue.reset();
        QVERIFY(!registry.available());QVERIFY(registry.session(id).isEmpty());QVERIFY(!registry.openRoute("addon/org.example.ui/browser"));QVERIFY(!registry.triggerCommand("org.example.ui/update"));QVERIFY(registry.createSession("org.example.ui/browser").isEmpty());
        QVERIFY(manager.setEnabled("org.example.ui",false));QCOMPARE(registry.routes().size(),1);QCOMPARE(invokes,0);
    }
    void activeDocumentTicketsAreReleasedOnSessionClose() {
        QTemporaryDir source,storage;package(source.path());AddonManager manager(storage.path());QVERIFY(manager.installLocalPackage(source.path(),true));QVERIFY(manager.setEnabled("org.example.ui",true));
        auto catalogue=std::make_unique<AddonCatalogueView>(&manager,m_lifetime->handle());AddonUiRegistry registry(&manager);QTRY_VERIFY(registry.available());
        QStringList sessions;for(int i=0;i<32;++i){const auto id=registry.createSession("org.example.ui/browser");QVERIFY(!id.isEmpty());sessions.append(id);}
        QVERIFY(registry.createSession("org.example.ui/browser").isEmpty());QVERIFY(catalogue->acquireDocument("org.example.ui/browser","view").isEmpty());
        for(const auto& id:sessions)registry.closeSession(id);
        QStringList tickets;for(int i=0;i<32;++i){const auto ticket=catalogue->acquireDocument("org.example.ui/browser","view");QVERIFY(!ticket.isEmpty());tickets.append(ticket);}QVERIFY(catalogue->acquireDocument("org.example.ui/browser","view").isEmpty());
        for(const auto& ticket:tickets)catalogue->release(ticket);
    }
    void attachedTypedSelectionsNeedReadyCurrentSessions() {
        QTemporaryDir source,storage;selectionPackage(source.path(),true,true);AddonManager manager(storage.path());QVERIFY(manager.installLocalPackage(source.path(),true));QVERIFY(manager.setEnabled("org.example.ui",true));
        auto catalogue=std::make_unique<AddonCatalogueView>(&manager,m_lifetime->handle());AddonUiRegistry registry(&manager);QTRY_VERIFY(registry.available());
        const QString hook=QStringLiteral("dialog.add-to-playlist.content@1"),contribution=QStringLiteral("org.example.ui/playlist");QVERIFY(registry.selectReplacement(hook,contribution));QTRY_COMPARE(registry.replacement(hook).value("id").toString(),contribution);QTRY_VERIFY(registry.available());
        const QVariantMap parameters{{"choiceSet","one"},{"targets",QVariantList{QVariantMap{{"ref","target:0"},{"title","Films"},{"enabled",true}}}},{"assets",QVariantList{}}};
        QVERIFY(registry.canPrepareSelection(contribution,hook));QVERIFY(!registry.canUseSelection(contribution,hook));
        const auto session=registry.createSession(contribution,parameters);QVERIFY(!session.isEmpty());QVERIFY(!registry.canUseSelection(contribution,hook));const QVariantMap result{{"choiceSet","one"},{"selected",QVariantList{QStringLiteral("target:0")}}};QVERIFY(!registry.validateSessionResult(session,result));
        QTRY_VERIFY(!registry.session(session).value("document").toMap().isEmpty());QVERIFY(registry.canUseSelection(contribution,hook));QVERIFY(registry.validateSessionResult(session,result));QVERIFY(!registry.validateSessionResult(session,{{"choiceSet","old"},{"selected",QVariantList{QStringLiteral("target:0")}}}));
        catalogue->refresh();QVERIFY(registry.canPrepareSelection(contribution,hook));QVERIFY(!registry.canUseSelection(contribution,hook));QVERIFY(!registry.validateSessionResult(session,result));QTRY_VERIFY(registry.available());QVERIFY(registry.canUseSelection(contribution,hook));QVERIFY(registry.validateSessionResult(session,result));registry.closeSession(session);
    }
    void malformedLocaleCannotGrantTypedSelectionReadiness() {
        QTemporaryDir source,storage;selectionPackage(source.path(),true);QFile file(source.filePath("addon.json"));QVERIFY(file.open(QIODevice::ReadOnly));
        auto manifest=QJsonDocument::fromJson(file.readAll()).object();file.close();auto files=manifest.value("files").toArray();files.append("locale.json");manifest.insert("files",files);
        manifest.insert("locales",QJsonObject{{"default","en"},{"files",QJsonObject{{"en","locale.json"}}}});write(source.filePath("addon.json"),QJsonDocument(manifest).toJson());write(source.filePath("locale.json"),"[]");
        AddonManager manager(storage.path());QVERIFY(manager.installLocalPackage(source.path(),true));QVERIFY(manager.setEnabled("org.example.ui",true));
        auto catalogue=std::make_unique<AddonCatalogueView>(&manager,m_lifetime->handle());AddonUiRegistry registry(&manager);QTRY_VERIFY(registry.available());
        const QString hook=QStringLiteral("dialog.add-to-playlist.content@1"),contribution=QStringLiteral("org.example.ui/playlist");QVERIFY(registry.selectReplacement(hook,contribution));QTRY_VERIFY(registry.available());
        QVERIFY(registry.canPrepareSelection(contribution,hook));QVERIFY(!registry.canUseSelection(contribution,hook));
        const auto session=registry.createSession(contribution,{{"choiceSet","one"},{"targets",QVariantList{}},{"assets",QVariantList{}}});QVERIFY(!session.isEmpty());
        QTRY_VERIFY(registry.session(session).value("unavailable").toBool());QVERIFY(!registry.canUseSelection(contribution,hook));
        QVERIFY(!registry.validateSessionResult(session,{{"choiceSet","one"},{"selected",QVariantList{}}}));registry.closeSession(session);
    }
    void invokeReentryCannotPublishAnOldRequest_data() {
        QTest::addColumn<int>("mode");QTest::newRow("closed-session")<<0;QTest::newRow("suspended-session")<<1;QTest::newRow("withdrawn-package")<<2;
    }
    void invokeReentryCannotPublishAnOldRequest() {
        QFETCH(int,mode);QTemporaryDir source,storage;package(source.path());AddonManager manager(storage.path());QVERIFY(manager.installLocalPackage(source.path(),true));QVERIFY(manager.setEnabled("org.example.ui",true));
        QString session;QStringList cancelled;AddonUiRegistry* active=nullptr;
        AddonUiRegistry registry(&manager,AddonUiRegistry::Backend{
            [&](const QString&,const QString&,const QVariantMap&){
                if(mode==0)active->closeSession(session);else if(mode==1)active->suspendSession(session,true);else manager.setEnabled("org.example.ui",false);
                return QStringLiteral("old-request");},[&](const QString& request){cancelled.append(request);}});active=&registry;
        session=registry.createSession("org.example.ui/browser");QVERIFY(!session.isEmpty());QSignalSpy completed(&registry,&AddonUiRegistry::commandCompleted);
        QVERIFY(!registry.dispatchSession(session,"org.example.ui/update"));QCOMPARE(cancelled,QStringList{QStringLiteral("old-request")});
        registry.operationFinished("old-request","org.example.ui/update","match",{{"patch",QVariantMap{{"result","Late"}}}},{});QCOMPARE(completed.size(),0);
        if(mode==1)QVERIFY(registry.session(session).value("state").toMap().value("result").isNull());else QVERIFY(registry.session(session).isEmpty());
    }
    void backendRetirementCannotLeaveANewRequestUnowned() {
        QTemporaryDir source,storage;package(source.path());AddonManager manager(storage.path());QVERIFY(manager.installLocalPackage(source.path(),true));QVERIFY(manager.setEnabled("org.example.ui",true));
        std::unique_ptr<AddonUiRegistry> registry;QStringList cancelled;
        registry=std::make_unique<AddonUiRegistry>(&manager,AddonUiRegistry::Backend{
            [&](const QString&,const QString&,const QVariantMap&){registry.reset();return QStringLiteral("unpublished");},
            [&](const QString& request){cancelled.append(request);}});
        QVERIFY(!registry->triggerCommand("org.example.ui/update"));QVERIFY(!registry);QCOMPARE(cancelled,QStringList{QStringLiteral("unpublished")});
    }
    void cancellationCallbackCanRetireTheRegistry() {
        QTemporaryDir source,storage;package(source.path());AddonManager manager(storage.path());QVERIFY(manager.installLocalPackage(source.path(),true));QVERIFY(manager.setEnabled("org.example.ui",true));
        std::unique_ptr<AddonUiRegistry> registry;QStringList cancelled;
        registry=std::make_unique<AddonUiRegistry>(&manager,AddonUiRegistry::Backend{
            [](const QString&,const QString&,const QVariantMap&){return QStringLiteral("request");},
            [&](const QString& request){cancelled.append(request);registry.reset();}});
        QVERIFY(registry->triggerCommand("org.example.ui/update"));QVERIFY(manager.setEnabled("org.example.ui",false));
        QVERIFY(!registry);QCOMPARE(cancelled,QStringList{QStringLiteral("request")});
    }
    void sessionPublicationCanRetireTheRegistry() {
        QTemporaryDir source,storage;package(source.path());AddonManager manager(storage.path());QVERIFY(manager.installLocalPackage(source.path(),true));QVERIFY(manager.setEnabled("org.example.ui",true));
        auto registry=std::make_unique<AddonUiRegistry>(&manager,AddonUiRegistry::Backend{
            [](const QString&,const QString&,const QVariantMap&){return QStringLiteral("request");},[](const QString&){}});
        const auto id=registry->createSession("org.example.ui/browser");QVERIFY(!id.isEmpty());QVERIFY(registry->dispatchSession(id,"org.example.ui/update"));
        QObject::connect(registry.get(),&AddonUiRegistry::sessionChanged,&manager,[&]{registry.reset();});
        registry->operationFinished("request","org.example.ui/update","match",{{"patch",QVariantMap{{"result","Done"}}}},{});QVERIFY(!registry);
    }
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
    void panelIconsAreSingleColorShapesOnly() {
        const auto markup = panelIcon::markup("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#123456\" stroke-width=\"1.5\">\n            <!-- a comment --><rect x=\"3\" y=\"4\" width=\"18\" height=\"12\" rx=\"2\"/><path d=\"M8 20h8\" fill=\"red\"/></svg>");
        QVERIFY(markup.contains(QStringLiteral("stroke=\"COLOR\"")));
        QVERIFY(markup.contains(QStringLiteral("fill=\"COLOR\"")) && markup.contains(QStringLiteral("fill=\"none\"")));
        QVERIFY(!markup.contains(QStringLiteral("123456")) && !markup.contains(QStringLiteral("red")) && !markup.contains(QStringLiteral("comment")));
        for (const QByteArray bad : {
                 QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\"><script>alert(1)</script><path d=\"M1 1\"/></svg>"),
                 QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\"><image href=\"https://example.org/a.png\"/></svg>"),
                 QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\"><path d=\"M1 1\" onclick=\"x()\"/></svg>"),
                 QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\"><path d=\"M1 1\" style=\"fill:url(#a)\"/></svg>"),
                 QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 48 48\"><path d=\"M1 1\"/></svg>"),
                 QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\"><text>A</text></svg>"),
                 QByteArray("<!DOCTYPE svg [<!ENTITY a \"b\">]><svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\"><path d=\"&a;\"/></svg>"),
                 QByteArray("<svg viewBox=\"0 0 24 24\"><path d=\"M1 1\"/></svg>"),
                 QByteArray(16 * 1024 + 1, ' ')})
            QVERIFY2(panelIcon::markup(bad).isEmpty(), bad.constData());
    }
    void panelsAskForContextAndTakeEffectsOnlyFromClicks() {
        QTemporaryDir source, storage;
        panelPackage(source.path(), "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\"><path d=\"M4 6h16M4 12h16\" stroke=\"#000\" stroke-width=\"1.5\"/></svg>");
        AddonManager manager(storage.path());
        QVERIFY(manager.installLocalPackage(source.path(), true)); QVERIFY(manager.setEnabled("org.example.panel", true));
        QStringList requests, operations, canceled; QList<QVariantMap> contexts;
        AddonUiRegistry registry(&manager, {
            [&](const QString&, const QString& operation, const QVariantMap& context) {
                requests.append(QString::number(requests.size() + 1)); operations.append(operation); contexts.append(context); return requests.last(); },
            [&](const QString& id) { canceled.append(id); }});
        const auto panels = registry.panels();
        QCOMPARE(panels.size(), 1);
        const auto panel = panels.first().toMap();
        QCOMPARE(panel.value("id").toString(), QStringLiteral("org.example.panel/episodes"));
        QCOMPARE(panel.value("placement").toString(), QStringLiteral("footerRight"));
        QVERIFY(panel.value("iconMarkup").toString().contains(QStringLiteral("COLOR")));
        QVERIFY(!panel.value("available").toBool());
        QVERIFY(registry.siblingsWanted());
        QVERIFY(requests.isEmpty()); // no media yet: no package work

        QSignalSpy changed(&registry, &AddonUiRegistry::panelsChanged);
        registry.setPanelMedia({{"kind", "video"}, {"durationMs", 1200000}, {"url", "file:///private/show.mkv"}, {"title", "Private"}});
        QCOMPARE(requests.size(), 1); QCOMPARE(operations.last(), QStringLiteral("context"));
        const auto media = contexts.last().value("media").toMap();
        QCOMPARE(media.value("kind").toString(), QStringLiteral("video"));
        QCOMPARE(media.value("durationMs").toDouble(), 1200000.0);
        QVERIFY(!media.contains("url") && !media.contains("title"));
        QCOMPARE(contexts.last().value("panel").toString(), QStringLiteral("episodes"));

        // A newer media change cancels the old call, and its late reply is ignored.
        const auto old = requests.last();
        registry.setPanelMedia({{"kind", "video"}});
        QVERIFY(canceled.contains(old)); QCOMPARE(requests.size(), 2);
        registry.operationFinished(old, "org.example.panel/context", "match", {{"available", true}}, {});
        QVERIFY(!registry.panels().first().toMap().value("available").toBool());
        registry.operationFinished(requests.last(), "org.example.panel/context", "match", {{"available", true}}, {});
        QVERIFY(registry.panels().first().toMap().value("available").toBool());
        QVERIFY(changed.size() >= 1);

        // An effect in a reply to the app's own context call is ignored.
        QSignalSpy effects(&registry, &AddonUiRegistry::panelEffect);
        const auto session = registry.createSession("org.example.panel/episodes");
        QVERIFY(!session.isEmpty());
        QVERIFY(registry.attachPanelSession(session));
        QCOMPARE(requests.size(), 3); QCOMPARE(operations.last(), QStringLiteral("context"));
        QCOMPARE(contexts.last().value("state").toMap().value("status").toString(), QStringLiteral("loading"));
        registry.operationFinished(requests.last(), "org.example.panel/context", "match",
            {{"available", true}, {"patch", QVariantMap{{"status", "Ready"}}}, {"effect", QVariantMap{{"play", QVariantMap{{"id", "file:1"}}}}}}, {});
        QCOMPARE(registry.session(session).value("state").toMap().value("status").toString(), QStringLiteral("Ready"));
        QCOMPARE(effects.size(), 0);

        // A click in the view can ask for one known effect.
        QVERIFY(registry.dispatchSession(session, "org.example.panel/play", {}));
        registry.operationFinished(requests.last(), "org.example.panel/play", "match",
            {{"effect", QVariantMap{{"play", QVariantMap{{"id", "file:1"}, {"fromStart", true}}}}}}, {});
        QCOMPARE(effects.size(), 1);
        QCOMPARE(effects.last()[1].toString(), QStringLiteral("org.example.panel"));
        QCOMPARE(effects.last()[2].toString(), QStringLiteral("play"));
        QCOMPARE(effects.last()[3].toMap().value("id").toString(), QStringLiteral("file:1"));
        QVERIFY(registry.dispatchSession(session, "org.example.panel/play", {}));
        registry.operationFinished(requests.last(), "org.example.panel/play", "match", {{"effect", QVariantMap{{"delete", QVariantMap{}}}}}, {});
        QCOMPARE(effects.size(), 1);

        // Host data changes reach an open panel at most once a second.
        const int before = requests.size();
        registry.refreshPanels(); registry.refreshPanels(); registry.refreshPanels();
        QCOMPARE(requests.size(), before);
        QTRY_COMPARE_WITH_TIMEOUT(requests.size(), before + 1, 3000);
        QTest::qWait(1200); QCOMPARE(requests.size(), before + 1);
        registry.closeSession(session);
        registry.refreshPanels(); QTest::qWait(1200); QCOMPARE(requests.size(), before + 1);

        // A failed view keeps the panel and its button.
        registry.reportViewFailure("org.example.panel/episodes");
        QCOMPARE(registry.panels().size(), 1);
        QVERIFY(manager.setEnabled("org.example.panel", false));
        QVERIFY(registry.panels().isEmpty()); QVERIFY(!registry.siblingsWanted());
    }
    void panelDeclarationsAreChecked() {
        QTemporaryDir source; panelPackage(source.path(), "<svg/>");
        QFile file(source.path() + QStringLiteral("/addon.json")); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto manifest = QJsonDocument::fromJson(file.readAll()).object(); file.close();
        const auto mutate = [&](auto change) {
            auto copy = manifest; auto contributions = copy.value("contributions").toArray();
            auto panel = contributions.first().toObject(); change(panel, contributions); contributions.replace(0, panel);
            copy.insert("contributions", contributions); Manifest parsed; QString error;
            return Manifest::parse(QJsonDocument(copy).toJson(), &parsed, &error);
        };
        QVERIFY(mutate([](QJsonObject&, QJsonArray&) {}));
        QVERIFY(!mutate([](QJsonObject& panel, QJsonArray&) { panel.insert("command", "missing"); }));
        QVERIFY(!mutate([](QJsonObject& panel, QJsonArray&) { panel.insert("command", "episodes"); }));
        QVERIFY(!mutate([](QJsonObject& panel, QJsonArray&) { panel.insert("hook", "home.sections@1"); }));
        QVERIFY(!mutate([](QJsonObject& panel, QJsonArray&) { panel.insert("placement", "headerCenter"); }));
        QVERIFY(!mutate([](QJsonObject& panel, QJsonArray&) { panel.insert("icon", "view.json"); }));
        QVERIFY(!mutate([](QJsonObject& panel, QJsonArray&) { panel.remove("stateSchema"); panel.remove("state"); }));
        QVERIFY(!mutate([](QJsonObject& panel, QJsonArray&) { panel.insert("label", ""); }));
        QVERIFY(!mutate([](QJsonObject&, QJsonArray& list) { auto command = list[1].toObject(); command.insert("placement", "above"); list.replace(1, command); }));
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
