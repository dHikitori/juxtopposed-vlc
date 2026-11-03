// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSslSocket>
#include <QtPlugin>
#include <QFile>
#include <QDir>
#include <QUrlQuery>
#include <QTranslator>
#include <QEvent>
#include "util/subtitle_source_service.hpp"
#include "addons/addon_manager.hpp"
#include "addons/addon_metadata_service.hpp"
#include "addons/addon_catalogue_view.hpp"
#include "fixtures/metadata_preparation_probe.hpp"
#include <QScopeGuard>
#include <atomic>
#include <condition_variable>
#include <mutex>

#if defined(Q_OS_WIN) && defined(QT_STATIC)
Q_IMPORT_PLUGIN(QSchannelBackend)
#endif

struct HttpRequest { QByteArray method; QUrl url; QMap<QByteArray,QByteArray> headers; QByteArray body; };
class Server : public QTcpServer {
public:
    QList<HttpRequest> requests;
    std::function<void(const HttpRequest&,QTcpSocket*)> respond;
    Server() {
        if (!listen(QHostAddress::LocalHost)) qFatal("Cannot start local subtitle test server");
        connect(this,&QTcpServer::newConnection,this,[this] {
            while (hasPendingConnections()) {
                auto* socket=nextPendingConnection(); auto buffer=std::make_shared<QByteArray>();
                connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
                connect(socket,&QTcpSocket::readyRead,this,[this,socket,buffer] {
                    buffer->append(socket->readAll()); const auto end=buffer->indexOf("\r\n\r\n"); if(end<0)return;
                    const auto lines=buffer->left(end).split('\n'); const auto first=lines.first().trimmed().split(' ');
                    HttpRequest request;request.method=first[0];request.url=QUrl::fromEncoded(first[1]);
                    for(const auto& line:lines.mid(1)) {const auto colon=line.indexOf(':');if(colon>0)request.headers.insert(line.left(colon).toLower(),line.mid(colon+1).trimmed());}
                    const auto length=request.headers.value("content-length").toInt(); if(buffer->size()<end+4+length)return;
                    request.body=buffer->mid(end+4,length);disconnect(socket,&QTcpSocket::readyRead,this,nullptr);
                    requests.append(request);if(respond)respond(request,socket);
                });
            }
        });
    }
    QUrl base() const { return QUrl(QString("http://127.0.0.1:%1/api/").arg(serverPort())); }
    static void send(QTcpSocket* socket,QByteArray body,int status=200,QByteArray extra={}) {
        socket->write("HTTP/1.1 "+QByteArray::number(status)+" Result\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n"+extra+"\r\n"+body);socket->disconnectFromHost();
    }
};
static QByteArray results() {
    return R"({"data":[{"attributes":{"language":"en","release":"The Wand S01E04","hearing_impaired":true,"files":[{"file_id":42,"file_name":"The_Wand_S01E04.en.srt"},{"file_id":43,"file_name":"../../other.srt"}]}}]})";
}
static QByteArray captions() { return "1\n00:00:01,000 --> 00:00:03,000\nTest captions\n"; }
struct SubtitleDisplayWait {
    std::mutex mutex; std::condition_variable changed; bool released=false; std::atomic_bool entered{false};
    void wait() { std::unique_lock<std::mutex> lock(mutex); entered=true; changed.wait(lock,[&] { return released; }); }
    void release() { { std::lock_guard<std::mutex> lock(mutex); released=true; } changed.notify_all(); }
};
struct SubtitleDisplayFixture {
    QTemporaryDir directory;
    vlc::addons::AddonStreamLifetime lifetime;
    std::unique_ptr<vlc::addons::AddonManager> manager;
    QPointer<vlc::addons::AddonCatalogueView> view;
    vlc::addons::DisplayCatalogueSnapshot snapshot;
    std::shared_ptr<SubtitleDisplayWait> block;
    std::atomic_uint reads{0},keys{0}; std::atomic_bool onOwner{false}; QThread* ownerThread=QThread::currentThread();
    SubtitleDisplayFixture() {
        manager=std::make_unique<vlc::addons::AddonManager>(directory.filePath("addons"));
        vlc::addons::DisplayCataloguePackage row; auto& package=row.package;
        package.manifest.id=QStringLiteral("org.example.display-subtitles"); package.manifest.name=QStringLiteral("Subtitle fixture");
        package.manifest.version=QStringLiteral("1.0.0"); package.digest=QByteArray(64,'a'); package.generation=1; package.enabled=true;
        vlc::addons::Contribution contribution; contribution.id=QStringLiteral("search"); contribution.type=QStringLiteral("metadata.provider");
        contribution.contract=QStringLiteral("metadata@1"); contribution.features={QStringLiteral("subtitles")}; contribution.operations={QStringLiteral("searchAssets")};
        contribution.document={{"id",contribution.id},{"type",contribution.type},{"contract",contribution.contract},
            {"features",QJsonArray{"subtitles"}},{"operations",QJsonArray{"searchAssets"}}}; package.manifest.contributions={contribution};
        row.schema={{"type","object"},{"required",QJsonArray{"key"}},
            {"properties",QJsonObject{{"key",QJsonObject{{"type","string"},{"writeOnly",true}}}}}};
        row.settings={{"_credentials",QJsonObject{{"key","00000000-0000-4000-8000-000000000002"}}}}; snapshot.packages={row};
    }
    void attach() {
        vlc::addons::AddonCatalogueView::Backend backend;
        backend.capture=[this]()->std::optional<vlc::addons::DisplayCatalogueSource> {
            auto source=manager->displayCatalogueSource(); if(!source) return {};
            source->workerExecutable=QCoreApplication::applicationFilePath(); source->credentialPresence.clear();
            source->credentials=[this](const auto&,const auto&) { ++keys; onOwner=onOwner || QThread::currentThread()==ownerThread; return QByteArray("fixture-only-key"); };
            auto state=snapshot; state.authority=state.queuedAuthority=source->queuedAuthority; const auto waiting=block;
            source->reader=[this,state=std::move(state),waiting](const vlc::addons::DisplayCatalogueRequest&,vlc::addons::DisplayCatalogueSnapshot* out,QString*) {
                ++reads; onOwner=onOwner || QThread::currentThread()==ownerThread; if(waiting) waiting->wait(); *out=state; return true;
            }; return source;
        };
        backend.subscribe=[this](auto wake) { return manager->watchDisplayCatalogue(std::move(wake)); };
        view=new vlc::addons::AddonCatalogueView(std::move(backend),lifetime.handle(),manager.get());
        view->setObjectName(QStringLiteral("hakoneDisplayCatalogue"));
        QObject::connect(manager.get(),&vlc::addons::AddonManager::revisionChanged,view,&vlc::addons::AddonCatalogueView::inputsChanged);
    }
    ~SubtitleDisplayFixture() { if(view) view->close(); lifetime.close(); lifetime.handle().waitQt(std::chrono::seconds(5)); manager.reset(); }
};
class TestSubtitleSources : public QObject {
    Q_OBJECT
    vlc::addons::AddonStreamLifetime m_applicationLifetime;
    static SubtitleSourceService::Options options(Server& server) { SubtitleSourceService::Options o;o.apiBase=server.base();o.timeoutMs=1000;return o; }
    static QVariantMap result(QSignalSpy& spy) { if(!spy.wait(5000))qFatal("Subtitle client did not finish");return spy.last()[1].toMap(); }
    static QVariantList search(SubtitleSourceService& client) { QSignalSpy spy(&client,&SubtitleSourceService::searchFinished);client.search("The Wand S01E04","en");return result(spy).value("sources").toList(); }
    static QVariantList row(const QStringList& tracks) { return {QVariantMap{{"url","file:///movie.mkv"},{"baseline",QStringList{}},{"initialTracks",QStringList{}},{"tracks",tracks}}}; }
private slots:
    void initTestCase() { QVERIFY(vlc::addons::AddonStreamLifetime::registerApplication(m_applicationLifetime.handle())); }
    void cleanupTestCase() {
        m_applicationLifetime.close();
        QElapsedTimer timer; timer.start();
        while (!m_applicationLifetime.handle().qtDrained() && timer.elapsed() < 10000) {
            QCoreApplication::processEvents(); QTest::qWait(1);
        }
        QVERIFY(m_applicationLifetime.handle().qtDrained());
        QVERIFY(vlc::addons::AddonStreamLifetime::unregisterApplication(m_applicationLifetime.handle()));
    }
#ifdef HAKONE_METADATA_PREPARATION_WRAP
    void onlineEntryAndCancelRemainResponsiveDuringASlowProviderRead() {
        for (const bool cancel : {false, true}) {
            QTemporaryDir directory; Server server;
            server.respond = [](const HttpRequest&, QTcpSocket* socket) { Server::send(socket, results()); };
            vlc::addons::AddonManager manager(directory.filePath("addons")); AddonMetadataService owner(&manager);
            SubtitleSourceService client(options(server)); client.setAddonManager(&manager); client.setMetadataService(&owner);
            client.setStorageDirectory(directory.filePath("subtitles")); QVERIFY(client.setApiKey("native-test-key"));
            const auto probe = std::make_shared<metadataPreparationTest::Probe>(); metadataPreparationTest::catalogue = probe;
            const auto release = qScopeGuard([&] { metadataPreparationTest::catalogue.reset(); probe->release(); });
            QSignalSpy finished(&client, &SubtitleSourceService::searchFinished);
            QElapsedTimer elapsed; elapsed.start(); client.fetchLanguages(); client.search("The Wand S01E04", "en");
            QVERIFY(elapsed.elapsed() < 100); QTRY_VERIFY(probe->entered.load()); QVERIFY(!probe->onOwner.load());
            bool heartbeat = false; QTimer::singleShot(0, [&] { heartbeat = true; }); QTRY_VERIFY(heartbeat);
            QVERIFY(server.requests.isEmpty());
            if (cancel) { elapsed.restart(); client.cancelSearch(); QVERIFY(elapsed.elapsed() < 100); }
            probe->release();
            if (cancel) { QTRY_VERIFY(m_applicationLifetime.handle().qtDrained()); QTest::qWait(20); QVERIFY(server.requests.isEmpty()); }
            else { QTRY_COMPARE(finished.size(), 1); QCOMPARE(finished.first()[1].toMap().value("sources").toList().size(), 2); }
        }
    }
#endif
    void incomingDeletionAfterUnregisterCannotReviveTheOldNativePath() {
        QTemporaryDir directory; Server server; SubtitleSourceService client(options(server));
        auto original=std::make_unique<vlc::addons::AddonManager>(directory.filePath("original"));
        auto incoming=std::make_unique<vlc::addons::AddonManager>(directory.filePath("incoming"));
        client.setStorageDirectory(directory.filePath("subtitles")); QVERIFY(client.setApiKey("native-test-key"));
        client.setAddonManager(original.get()); bool armed=true; QObject externalObserver;
        connect(original.get(),&vlc::addons::AddonManager::revisionChanged,&externalObserver,[&] { if(armed) { armed=false; incoming.reset(); } });
        client.setAddonManager(incoming.get()); QVERIFY(!armed); QVERIFY(!incoming); QVERIFY(!client.configured());
        original.reset(); QVERIFY(!client.configured()); QVERIFY(!client.configurationKnown()); client.fetchLanguages();
        QSignalSpy finished(&client,&SubtitleSourceService::searchFinished); client.search("The Wand S01E04","en");
        QVERIFY(!result(finished).value("success").toBool()); QVERIFY(server.requests.isEmpty());
    }
    void managerBindingSurvivesIncomingDeletionAndNewerReentry() {
        for(const bool replace:{false,true}) {
            QTemporaryDir directory; Server server; SubtitleSourceService client(options(server));
            vlc::addons::AddonManager original(directory.filePath("original")),newer(directory.filePath("newer"));
            auto incoming=std::make_unique<vlc::addons::AddonManager>(directory.filePath("incoming"));
            client.setStorageDirectory(directory.filePath("subtitles")); QVERIFY(client.setApiKey("native-test-key"));
            client.setAddonManager(&original); bool armed=true;
            connect(&client,&SubtitleSourceService::searchProgressChanged,&client,[&] {
                if(!armed) return; armed=false;
                if(replace) client.setAddonManager(&newer); else incoming.reset();
            });
            client.setAddonManager(incoming.get()); QVERIFY(!armed); QVERIFY(client.configured());
            if(replace) {
                QVERIFY(original.providersForFeature("subtitles").isEmpty());
                QCOMPARE(newer.providersForFeature("subtitles").size(),1);
                QVERIFY(incoming->providersForFeature("subtitles").isEmpty());
            } else { QVERIFY(!incoming); QCOMPARE(original.providersForFeature("subtitles").size(),1); }
        }
    }
    void attachedStatusReadsNeverReadKeysAndExplicitFeatureChecksAreJoined() {
        SubtitleDisplayFixture display; display.attach(); Server server; SubtitleSourceService client(options(server));
        client.setAddonManager(display.manager.get());
        QTRY_VERIFY(display.view->isCurrent()); QTRY_COMPARE(client.displayProviders().size(),2);
        QVERIFY(client.providerDisplayAttached()); QVERIFY(!client.configured()); QVERIFY(!client.configurationKnown());
        const auto reads=display.reads.load();
        for(int i=0;i<100;++i) { client.configured(); client.configurationKnown(); client.displayProviders(); client.providerDisplayIsCurrent(); }
        QTest::qWait(30); QCOMPARE(display.reads.load(),reads); QCOMPARE(display.keys.load(),0u); QVERIFY(server.requests.isEmpty());
        const auto ticket=display.manager->prepareDisplayFeature(QStringLiteral("subtitles")); QVERIFY(!ticket.isEmpty());
        QTRY_VERIFY(client.configured()); QVERIFY(client.configurationKnown()); QCOMPARE(display.keys.load(),1u); QVERIFY(!display.onOwner.load());
        display.manager->releaseDisplayRequest(ticket);
    }
    void attachedDisplayRefreshDoesNotReplaceActualSearchAuthority() {
        SubtitleDisplayFixture display; display.attach(); Server server;
        server.respond=[](const HttpRequest&,QTcpSocket* socket) { Server::send(socket,results()); };
        SubtitleSourceService client(options(server)); client.setAddonManager(display.manager.get());
        client.setStorageDirectory(display.directory.filePath("subtitles")); QVERIFY(client.setApiKey("native-test-key"));
        QTRY_VERIFY(display.view->isCurrent()); QTRY_VERIFY(client.configured());
        const auto blocked=std::make_shared<SubtitleDisplayWait>(); display.block=blocked;
        const auto release=qScopeGuard([&] { blocked->release(); }); display.view->refresh(); QTRY_VERIFY(blocked->entered.load());
        QVERIFY(!client.providerDisplayCurrent()); QVERIFY(!client.configured()); QVERIFY(!client.configurationKnown());
        QVERIFY(!client.displayProviders().isEmpty());
        const auto sources=search(client); QCOMPARE(sources.size(),2); QCOMPARE(server.requests.size(),1);
        QCOMPARE(server.requests[0].headers.value("api-key"),QByteArray("native-test-key")); QCOMPARE(display.keys.load(),0u);
        blocked->release(); QTRY_VERIFY(display.view->isCurrent());
    }
    void destroyedDisplayAndRetiringManagerNeverFallBackToTheNativeKey() {
        SubtitleDisplayFixture display; display.attach(); Server server; SubtitleSourceService client(options(server));
        client.setAddonManager(display.manager.get()); client.setStorageDirectory(display.directory.filePath("subtitles"));
        QVERIFY(client.setApiKey("native-test-key")); QTRY_VERIFY(display.view->isCurrent()); QTRY_VERIFY(client.configured());
        delete display.view.data(); QVERIFY(!client.configured()); QVERIFY(!client.configurationKnown()); QVERIFY(client.displayProviders().isEmpty());
        display.attach(); QTRY_VERIFY(display.view->isCurrent()); QVERIFY(!client.configured()); QVERIFY(!client.providerDisplayIsCurrent());
        display.manager.reset(); QVERIFY(!client.configured()); client.fetchLanguages();
        QSignalSpy finished(&client,&SubtitleSourceService::searchFinished); client.search("The Wand S01E04","en");
        QVERIFY(!result(finished).value("success").toBool()); QVERIFY(server.requests.isEmpty());
    }
    void sharedOwnerInvalidationEndsTheUiSearch()
    {
        QTemporaryDir directory; Server server;
        server.respond = [](const HttpRequest&, QTcpSocket*) {};
        vlc::addons::AddonManager manager(directory.filePath("addons")); AddonMetadataService owner(&manager);
        SubtitleSourceService client(options(server)); client.setAddonManager(&manager); client.setMetadataService(&owner);
        client.setStorageDirectory(directory.filePath("subtitles")); QVERIFY(client.setApiKey("unit-only-native-key"));
        QSignalSpy finished(&client, &SubtitleSourceService::searchFinished);
        client.search("The Wand S01E04", "en"); QTRY_COMPARE(server.requests.size(), 1);
        owner.invalidate(); QCOMPARE(finished.size(), 1); QVERIFY(!finished[0][1].toMap().value("success").toBool());
        QCOMPARE(owner.mediaCount(), 0);
        client.cancelSearch(); QCoreApplication::processEvents(); QCOMPARE(finished.size(), 1);
    }

    void sharedMetadataOwnerKeepsNativeSearchAndPreparation()
    {
        QTemporaryDir directory; Server server;
        server.respond = [&](const HttpRequest& request, QTcpSocket* socket) {
            if (request.url.path().endsWith("/subtitles")) Server::send(socket, results());
            else if (request.url.path().endsWith("/download"))
                Server::send(socket, QJsonDocument(QJsonObject{{"link", server.base().resolved(QUrl("file.srt")).toString()}}).toJson());
            else Server::send(socket, captions());
        };
        vlc::addons::AddonManager manager(directory.filePath("addons"));
        AddonMetadataService owner(&manager);
        SubtitleSourceService client(options(server)); client.setAddonManager(&manager);
        client.setMetadataService(&owner); client.setStorageDirectory(directory.filePath("subtitles"));
        QVERIFY(client.setApiKey("unit-only-native-key"));
        const auto sources = search(client); QCOMPARE(sources.size(), 2); QCOMPARE(server.requests.size(), 1);
        QCOMPARE(owner.mediaCount(), 0);
        const auto selected = sources[0].toMap().value("url").toString();
        QCOMPARE(QUrl(selected).scheme(), "addon-subtitle");
        QSignalSpy prepared(&client, &SubtitleSourceService::preparationFinished);
        client.prepareRows(row({selected})); auto answer = result(prepared);
        QVERIFY2(answer.value("success").toBool(), qPrintable(answer.value("error").toString()));
        const auto track = answer.value("rows").toList()[0].toMap().value("tracks").toStringList()[0];
        QFile file(QUrl(track).toLocalFile()); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), captions());
        QCOMPARE(server.requests.size(), 3);
        const auto count = server.requests.size();
        client.setMetadataService(nullptr);
        QSignalSpy searched(&client, &SubtitleSourceService::searchFinished);
        client.search("The Wand S01E04", "en"); answer = result(searched);
        QVERIFY(!answer.value("success").toBool()); QCOMPARE(server.requests.size(), count);
    }

    void providerCoordinatorNativeAdapterUsesOpaqueSourcesAndNormalPreparation() {
        QTemporaryDir directory;
        Server server;
        server.respond = [&](const HttpRequest& request, QTcpSocket* socket) {
            if (request.url.path().endsWith("/subtitles")) Server::send(socket, results());
            else if (request.url.path().endsWith("/download"))
                Server::send(socket, QJsonDocument(QJsonObject{{"link", server.base().resolved(QUrl("file.srt")).toString()}}).toJson());
            else Server::send(socket, captions());
        };
        vlc::addons::AddonManager manager(directory.filePath("addons"));
        SubtitleSourceService client(options(server));
        client.setAddonManager(&manager);
        client.setStorageDirectory(directory.filePath("subtitles"));
        QVERIFY(client.setApiKey("unit-only-native-key"));
        QVERIFY(client.configured());
        const auto providers = manager.providersForFeature("subtitles");
        QCOMPARE(providers.size(), 1);
        QCOMPARE(providers[0].toMap().value("id").toString(), "hakone.builtin.opensubtitles/search");
        const auto sources = search(client);
        QCOMPARE(sources.size(), 2);
        const auto selected = sources[0].toMap().value("url").toString();
        QCOMPARE(QUrl(selected).scheme(), "addon-subtitle");
        QVERIFY(!selected.contains("unit-only-native-key"));
        QCOMPARE(server.requests.size(), 1); // One native request through the coordinator, no duplicate old path.
        QSignalSpy prepared(&client, &SubtitleSourceService::preparationFinished);
        client.prepareRows(row({selected}));
        const auto answer = result(prepared);
        QVERIFY2(answer.value("success").toBool(), qPrintable(answer.value("error").toString()));
        const auto track = answer.value("rows").toList()[0].toMap().value("tracks").toStringList()[0];
        QVERIFY(QUrl(track).isLocalFile());
        QFile file(QUrl(track).toLocalFile()); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), captions());
        QCOMPARE(server.requests.size(), 3);
        QCOMPARE(server.requests[0].headers.value("api-key"), QByteArray("unit-only-native-key"));
        QVERIFY(server.requests[2].headers.value("api-key").isEmpty());
    }

    void providerCoordinatorRejectsForgedReferencesAndHonorsDisable() {
        QTemporaryDir directory;
        Server server;
        vlc::addons::AddonManager manager(directory.filePath("addons"));
        SubtitleSourceService client(options(server));
        client.setAddonManager(&manager);
        client.setStorageDirectory(directory.filePath("subtitles"));
        QVERIFY(client.setApiKey("unit-only-native-key"));
        QSignalSpy prepared(&client, &SubtitleSourceService::preparationFinished);
        client.prepareRows(row({"addon-subtitle:/forged/not-issued.srt"}));
        QVERIFY(!result(prepared).value("success").toBool());
        QVERIFY(server.requests.isEmpty());
        QVERIFY(manager.setEnabled("hakone.builtin.opensubtitles", false));
        QVERIFY(!client.configured());
        // Refreshing the existing key/configuration must not re-enable it.
        QVERIFY(client.setApiKey("unit-only-native-key"));
        QVERIFY(!client.configured());
        QVERIFY(!manager.providersForFeature("subtitles")[0].toMap().value("enabled").toBool());
    }

    void providerCoordinatorFallbackDoesNotDuplicateNativeSearch() {
        QTemporaryDir directory;
        Server server;
        server.respond = [](const HttpRequest&, QTcpSocket* socket) { Server::send(socket, results()); };
        vlc::addons::AddonManager manager(directory.filePath("addons"));
        SubtitleSourceService client(options(server));
        client.setAddonManager(&manager);
        client.setStorageDirectory(directory.filePath("subtitles"));
        QVERIFY(client.setApiKey("unit-only-native-key"));
        manager.registerBuiltinProvider("subtitles", {{"id", "hakone.builtin.unavailable/search"},
            {"packageId", "hakone.builtin.unavailable"}, {"name", "Unavailable fixture"}, {"enabled", true},
            {"configured", true}, {"mediaKinds", QStringList{"episode"}}, {"cachePartition", "fixture-1"}});
        QVERIFY(manager.setProviderOrder("subtitles", {"hakone.builtin.unavailable/search", "hakone.builtin.opensubtitles/search"}));
        const auto sources = search(client);
        QCOMPARE(sources.size(), 2);
        QCOMPARE(server.requests.size(), 1);
    }
    void retainedProviderLimitPreservesAlreadySelectedHandles() {
        QTemporaryDir directory;
        Server server;
        server.respond = [&](const HttpRequest& request, QTcpSocket* socket) {
            if (request.url.path().endsWith("/subtitles")) Server::send(socket, results());
            else if (request.url.path().endsWith("/download"))
                Server::send(socket, QJsonDocument(QJsonObject{{"link", server.base().resolved(QUrl("file.srt")).toString()}}).toJson());
            else Server::send(socket, captions());
        };
        vlc::addons::AddonManager manager(directory.filePath("addons"));
        auto limited = options(server); limited.maxAddonRecords = 1;
        SubtitleSourceService client(limited); client.setAddonManager(&manager);
        client.setStorageDirectory(directory.filePath("subtitles")); QVERIFY(client.setApiKey("unit-only-native-key"));
        QSignalSpy searched(&client, &SubtitleSourceService::searchFinished);
        client.search("The Wand S01E04", "en");
        auto answer = result(searched); const auto sources = answer.value("sources").toList();
        QCOMPARE(sources.size(), 1); QVERIFY(!answer.value("warning").toString().isEmpty());
        const auto selected = sources[0].toMap().value("url").toString();
        searched.clear(); client.search("The Wand S01E04", "en");
        answer = result(searched); QVERIFY(answer.value("sources").toList().isEmpty());
        QVERIFY(!answer.value("warning").toString().isEmpty());
        QSignalSpy prepared(&client, &SubtitleSourceService::preparationFinished);
        client.prepareRows(row({selected})); answer = result(prepared);
        QVERIFY2(answer.value("success").toBool(), qPrintable(answer.value("error").toString()));
        QVERIFY(QUrl(answer.value("rows").toList()[0].toMap().value("tracks").toStringList()[0]).isLocalFile());
    }
    void languageLabelsChangeWithoutReloadingSources() {
        Server server;SubtitleSourceService client(options(server));
        QCOMPARE(client.languages().first().toMap().value("text").toString(),QStringLiteral("English"));
        class Translator : public QTranslator {
        public:
            bool isEmpty() const override { return false; }
            QString translate(const char*, const char* source, const char*, int) const override {
                if (qstrcmp(source,"English")==0) return QStringLiteral("Anglais");
                return {};
            }
        } translator;
        QSignalSpy changed(&client,&SubtitleSourceService::languagesChanged);
        QVERIFY(QCoreApplication::installTranslator(&translator));
        QEvent language(QEvent::LanguageChange);QCoreApplication::sendEvent(QCoreApplication::instance(),&language);
        QVERIFY(!changed.isEmpty());
        QCOMPARE(client.languages().first().toMap().value("text").toString(),QStringLiteral("Anglais"));
        QCOMPARE(client.languages().first().toMap().value("code").toString(),QStringLiteral("en"));
        QVERIFY(QCoreApplication::removeTranslator(&translator));
        QCoreApplication::sendEvent(QCoreApplication::instance(),&language);
        QCOMPARE(client.languages().first().toMap().value("text").toString(),QStringLiteral("English"));
        QVERIFY(server.requests.isEmpty());
    }
    void configurationAndMissingKey() {
        QTemporaryDir directory;Server server;SubtitleSourceService client(options(server));client.setStorageDirectory(directory.path());
        QSignalSpy spy(&client,&SubtitleSourceService::searchFinished);client.search("Film","en");
        QVERIFY(!result(spy).value("success").toBool());QVERIFY(server.requests.isEmpty());
        QVERIFY(!client.setApiKey("bad\r\nkey"));QVERIFY(!client.setApiKey(QString::fromUtf8("秘密")));
        QVERIFY(client.setApiKey("unit-secret"));QVERIFY(client.configured());
        SubtitleSourceService next(options(server));next.setStorageDirectory(directory.path());QVERIFY(next.configured());
#ifndef Q_OS_WIN
        QVERIFY(!(QFile::permissions(directory.filePath("source-settings.ini"))&QFile::WriteOther));
#endif
    }
    void searchParsesFilesAndQuery() {
        QTemporaryDir directory;Server server;server.respond=[](const HttpRequest&,QTcpSocket* socket){Server::send(socket,results());};
        SubtitleSourceService client(options(server));client.setStorageDirectory(directory.path());QVERIFY(client.setApiKey("unit-secret"));
        const auto sources=search(client);QCOMPARE(sources.size(),2);
        QCOMPARE(sources[0].toMap().value("name").toString(),"The_Wand_S01E04.en.srt");
        QCOMPARE(sources[1].toMap().value("name").toString(),"other.srt");
        QVERIFY(sources[0].toMap().value("hearingImpaired").toBool());
        QCOMPARE(server.requests.size(),1);QCOMPARE(server.requests[0].url.path(),"/api/subtitles");
        QCOMPARE(QUrlQuery(server.requests[0].url).queryItemValue("query"),"the wand");
        QCOMPARE(QUrlQuery(server.requests[0].url).queryItemValue("languages"),"en");
        QCOMPARE(QUrlQuery(server.requests[0].url).queryItemValue("season_number"),"1");
        QCOMPARE(QUrlQuery(server.requests[0].url).queryItemValue("episode_number"),"4");
        QCOMPARE(server.requests[0].headers.value("api-key"),QByteArray("unit-secret"));QVERIFY(!server.requests[0].headers.value("user-agent").isEmpty());
    }
    void allLanguagesDoesNotSendALanguageFilter() {
        QTemporaryDir directory;Server server;server.respond=[](const HttpRequest&,QTcpSocket* socket){Server::send(socket,results());};
        SubtitleSourceService client(options(server));client.setStorageDirectory(directory.path());QVERIFY(client.setApiKey("unit-secret"));
        QSignalSpy spy(&client,&SubtitleSourceService::searchFinished);client.search("The Wand","");
        QVERIFY(result(spy).value("success").toBool());QCOMPARE(server.requests.size(),1);
        const QUrlQuery query(server.requests[0].url);
        QVERIFY(!query.hasQueryItem("languages"));QCOMPARE(query.queryItemValue("query"),"the wand");
    }
    void structuredIdentitySearchAndManualCorrection() {
        QTemporaryDir directory; Server server;
        server.respond=[](const HttpRequest&,QTcpSocket* socket){Server::send(socket,results());};
        SubtitleSourceService client(options(server)); client.setStorageDirectory(directory.path()); QVERIFY(client.setApiKey("unit-secret"));
        QSignalSpy spy(&client,&SubtitleSourceService::searchFinished);
        client.searchMedia({{"url","https://example.com/Dune.2021.1080p.WEB-DL.mkv"}},"en");
        QVERIFY(result(spy).value("success").toBool());
        QUrlQuery query(server.requests.first().url);
        QCOMPARE(query.queryItemValue("query"),"dune"); QCOMPARE(query.queryItemValue("year"),"2021");
        QCOMPARE(query.queryItemValue("type"),"movie");
        spy.clear();
        client.searchMedia({{"url","https://example.com/Show.S01E04.mkv"},
            {"metadata",QVariantMap{{"seriesImdbId","tt1234567"}}}},"en");
        QVERIFY(result(spy).value("success").toBool()); query=QUrlQuery(server.requests.last().url);
        QCOMPARE(query.queryItemValue("parent_imdb_id"),"1234567"); QVERIFY(!query.hasQueryItem("query"));
        QCOMPARE(query.queryItemValue("season_number"),"1"); QCOMPARE(query.queryItemValue("episode_number"),"4");
        spy.clear();
        client.searchMedia({{"url","https://example.com/Show.S01E04.mkv"},{"manual",true},{"query","Other Show S02E05"},
            {"metadata",QVariantMap{{"seriesImdbId","tt1234567"}}}},"en");
        QVERIFY(result(spy).value("success").toBool()); query=QUrlQuery(server.requests.last().url);
        QCOMPARE(query.queryItemValue("query"),"other show"); QVERIFY(!query.hasQueryItem("parent_imdb_id"));
        QCOMPARE(query.queryItemValue("season_number"),"2"); QCOMPARE(query.queryItemValue("episode_number"),"5");
    }
    void manualBatchCorrectionBelongsToTheAnchorFile() {
        QTemporaryDir directory;Server server;
        server.respond=[](const HttpRequest&,QTcpSocket* socket){Server::send(socket,results());};
        SubtitleSourceService client(options(server));client.setStorageDirectory(directory.path());QVERIFY(client.setApiKey("unit-secret"));
        QSignalSpy spy(&client,&SubtitleSourceService::searchFinished);
        const QString first="https://example.com/Show.S01E01.mkv", anchor="https://example.com/Show.S01E04.mkv";
        const QVariantList targets{QVariantMap{{"url",first}},QVariantMap{{"url",anchor}}};
        client.searchMedia({{"targets",targets},{"anchor",anchor},{"manual",true},{"query","Corrected Show S02E05"}},"en");
        const auto answer=result(spy);QVERIFY(answer.value("success").toBool());QCOMPARE(server.requests.size(),1);
        const auto identities=answer.value("identities").toMap();QCOMPARE(identities.size(),1);
        QVERIFY(identities.contains(anchor));QVERIFY(!identities.contains(first));
        QCOMPARE(identities.value(anchor).toMap().value("title").toString(),"Corrected Show");
    }
    void hashLookupIsScopedAndCanFallBack() {
        QTemporaryDir directory; QFile video(directory.filePath("Show.S01E04.mkv"));
        QVERIFY(video.open(QIODevice::WriteOnly)); QVERIFY(video.resize(131072)); video.close();
        const auto url=QUrl::fromLocalFile(video.fileName()).toString(QUrl::FullyEncoded);
        Server server; bool hashMatch=true;
        server.respond=[&](const HttpRequest& request,QTcpSocket* socket) {
            auto body=results();
            if(hashMatch && QUrlQuery(request.url).hasQueryItem("moviehash")) body.replace("\"language\":\"en\"","\"moviehash_match\":true,\"language\":\"en\"");
            Server::send(socket,body);
        };
        SubtitleSourceService client(options(server));client.setStorageDirectory(directory.path());QVERIFY(client.setApiKey("unit-secret"));
        QSignalSpy spy(&client,&SubtitleSourceService::searchFinished);
        client.searchMedia({{"url",url}},"en"); auto answer=result(spy);
        QCOMPARE(server.requests.size(),1); const QUrlQuery query(server.requests[0].url);
        QCOMPARE(query.queryItemValue("moviehash"),"0000000000020000"); QCOMPARE(query.queryItemValue("moviebytesize"),"131072");
        const auto source=answer.value("sources").toList()[0].toMap(); QCOMPARE(source.value("hashFor").toStringList(),QStringList{url});
        QCOMPARE(source.value("matchReason").toString(),"Exact file match");
        hashMatch=false;spy.clear();server.requests.clear();client.searchMedia({{"url",url}},"en");
        answer=result(spy);QCOMPARE(server.requests.size(),2);
        QVERIFY(QUrlQuery(server.requests[0].url).hasQueryItem("moviehash"));
        QCOMPARE(QUrlQuery(server.requests[1].url).queryItemValue("episode_number"),"4");
        QVERIFY(answer.value("sources").toList()[0].toMap().value("hashFor").toStringList().isEmpty());
        spy.clear();server.requests.clear();client.searchMedia({{"url",url},{"manual",true},{"query","Corrected Title 2020"}},"en");
        QVERIFY(result(spy).value("success").toBool());QCOMPARE(server.requests.size(),1);
        QVERIFY(!QUrlQuery(server.requests[0].url).hasQueryItem("moviehash"));
    }
    void uncertainYearFallbackPreservesEpisodeConstraints() {
        QTemporaryDir directory;Server server;
        server.respond=[](const HttpRequest&,QTcpSocket* socket){Server::send(socket,R"({"data":[]})");};
        SubtitleSourceService client(options(server));client.setStorageDirectory(directory.path());QVERIFY(client.setApiKey("unit-secret"));
        QSignalSpy spy(&client,&SubtitleSourceService::searchFinished);
        client.searchMedia({{"url","https://example.com/Show.2005.S02E03.mkv"}},"en");
        QVERIFY(result(spy).value("success").toBool());QCOMPARE(server.requests.size(),2);
        QVERIFY(QUrlQuery(server.requests.first().url).hasQueryItem("year"));
        const QUrlQuery last(server.requests.last().url);QVERIFY(!last.hasQueryItem("year"));
        QCOMPARE(last.queryItemValue("season_number"),"2");QCOMPARE(last.queryItemValue("episode_number"),"3");
        spy.clear();server.requests.clear();client.search("Show 2005 S02E03","en");
        QVERIFY(result(spy).value("success").toBool());QCOMPARE(server.requests.size(),1);
        spy.clear();server.requests.clear();
        client.searchMedia({{"url","https://example.com/Show.S02E03.mkv"},{"metadata",QVariantMap{{"year",2005}}}},"en");
        QVERIFY(result(spy).value("success").toBool());QCOMPARE(server.requests.size(),1);
    }
    void cancellationStopsHashAndRemainingBatchTargets() {
        QTemporaryDir directory;QFile video(directory.filePath("Show.S01E04.mkv"));
        QVERIFY(video.open(QIODevice::WriteOnly));QVERIFY(video.resize(131072));video.close();
        Server server;server.respond=[](const HttpRequest&,QTcpSocket*){};
        SubtitleSourceService client(options(server));client.setStorageDirectory(directory.path());QVERIFY(client.setApiKey("unit-secret"));
        QSignalSpy spy(&client,&SubtitleSourceService::searchFinished);
        client.searchMedia({{"url",QUrl::fromLocalFile(video.fileName())}},"en");client.cancelSearch();
        QTest::qWait(50);QCOMPARE(spy.count(),0);QVERIFY(server.requests.isEmpty());
        const QVariantList targets{QVariantMap{{"url","https://example.com/Show.S01E04.mkv"}},
                                   QVariantMap{{"url","https://example.com/Show.S01E05.mkv"}}};
        client.searchMedia({{"targets",targets}},"en");
        QVERIFY(QTest::qWaitFor([&]{return server.requests.size()==1;}));
        client.cancelSearch();QTest::qWait(50);QCOMPARE(server.requests.size(),1);QCOMPARE(spy.count(),0);
        QCOMPARE(client.searchTotal(),0);
    }
    void paginationAndBatchKeepProviderEvidence() {
        QTemporaryDir directory;Server server;
        server.respond=[](const HttpRequest&,QTcpSocket* socket){auto body=results();body.replace("\"data\":","\"total_pages\":5,\"data\":");Server::send(socket,body);};
        SubtitleSourceService client(options(server));client.setStorageDirectory(directory.path());QVERIFY(client.setApiKey("unit-secret"));
        QSignalSpy spy(&client,&SubtitleSourceService::searchFinished);
        const QVariantList targets{QVariantMap{{"url","https://example.com/The.Wand.S01E04.mkv"}},
                                   QVariantMap{{"url","https://example.com/The.Wand.S01E05.mkv"}}};
        client.searchMedia({{"targets",targets}},"en");const auto answer=result(spy);
        QVERIFY(answer.value("success").toBool());QCOMPARE(server.requests.size(),6);
        QCOMPARE(answer.value("sources").toList().size(),2);QCOMPARE(answer.value("identities").toMap().size(),2);
        QVERIFY(!answer.value("warning").toString().isEmpty());
        QCOMPARE(QUrlQuery(server.requests.last().url).queryItemValue("episode_number"),"5");
        QCOMPARE(QUrlQuery(server.requests.last().url).queryItemValue("page"),"3");
    }
    void consumerKeyAndCustomOverride() {
        QTemporaryDir directory;Server server;server.respond=[](const HttpRequest&,QTcpSocket* socket){Server::send(socket,results());};
        auto o=options(server);o.consumerApiKey="app-consumer";SubtitleSourceService client(o);client.setStorageDirectory(directory.path());
        QVERIFY(client.configured());QVERIFY(client.hasConsumerKey());QVERIFY(!client.hasCustomKey());
        search(client);QCOMPARE(server.requests.last().headers.value("api-key"),QByteArray("app-consumer"));
        QVERIFY(!QFileInfo::exists(directory.filePath("source-settings.ini")));
        QVERIFY(client.setApiKey("user-key"));QVERIFY(client.hasCustomKey());
        search(client);QCOMPARE(server.requests.last().headers.value("api-key"),QByteArray("user-key"));
        SubtitleSourceService restored(o);restored.setStorageDirectory(directory.path());QVERIFY(restored.hasCustomKey());
        QVERIFY(client.clearApiKey());QVERIFY(client.configured());QVERIFY(!client.hasCustomKey());
        search(client);QCOMPARE(server.requests.last().headers.value("api-key"),QByteArray("app-consumer"));
        SubtitleSourceService reset(o);reset.setStorageDirectory(directory.path());QVERIFY(!reset.hasCustomKey());QVERIFY(reset.configured());
        SubtitleSourceService noConsumer(options(server));noConsumer.setStorageDirectory(directory.path());
        QVERIFY(noConsumer.setApiKey("temporary"));QVERIFY(noConsumer.clearApiKey());QVERIFY(!noConsumer.configured());
    }
    void apiRedirectsStayOnOrigin() {
        QTemporaryDir directory;Server server,other;int otherRequests=0;
        other.respond=[&](const HttpRequest&,QTcpSocket* socket){++otherRequests;Server::send(socket,results());};
        server.respond=[&](const HttpRequest& request,QTcpSocket* socket){
            if(request.url.path()=="/api/subtitles")Server::send(socket,{},301,"Location: /api/canonical\r\n");
            else Server::send(socket,results());
        };
        SubtitleSourceService client(options(server));client.setStorageDirectory(directory.path());QVERIFY(client.setApiKey("unit-secret"));
        QCOMPARE(search(client).size(),2);QCOMPARE(server.requests.size(),2);
        QCOMPARE(server.requests.last().headers.value("api-key"),QByteArray("unit-secret"));
        server.respond=[&](const HttpRequest&,QTcpSocket* socket){Server::send(socket,{},302,"Location: "+other.base().toEncoded()+"subtitles\r\n");};
        QSignalSpy spy(&client,&SubtitleSourceService::searchFinished);client.search("Film","en");
        QVERIFY(!result(spy).value("success").toBool());QCOMPARE(otherRequests,0);
    }
    void serverErrors_data() {
        QTest::addColumn<int>("status");QTest::addColumn<QByteArray>("body");
        QTest::newRow("key")<<403<<QByteArray("{}");QTest::newRow("quota")<<429<<QByteArray("{}");
        QTest::newRow("server")<<503<<QByteArray("{}");QTest::newRow("bad-json")<<200<<QByteArray("<html>error</html>");
    }
    void serverErrors() {
        QFETCH(int,status);QFETCH(QByteArray,body);QTemporaryDir directory;Server server;
        server.respond=[&](const HttpRequest&,QTcpSocket* socket){Server::send(socket,body,status);};
        SubtitleSourceService client(options(server));client.setStorageDirectory(directory.path());QVERIFY(client.setApiKey("unit-secret"));
        QSignalSpy spy(&client,&SubtitleSourceService::searchFinished);client.search("Film","en");const auto answer=result(spy);
        QVERIFY(!answer.value("success").toBool());QVERIFY(!answer.value("error").toString().isEmpty());QVERIFY(!answer.value("error").toString().contains("unit-secret"));
    }
    void timeoutAndCancellation() {
        QTemporaryDir directory;Server server;server.respond=[](const HttpRequest&,QTcpSocket*){};
        auto o=options(server);o.timeoutMs=80;SubtitleSourceService client(o);client.setStorageDirectory(directory.path());QVERIFY(client.setApiKey("unit-secret"));
        QSignalSpy spy(&client,&SubtitleSourceService::searchFinished);client.search("Film","en");QVERIFY(!result(spy).value("success").toBool());
        spy.clear();client.search("Old film","en");client.cancelSearch();QTest::qWait(100);QCOMPARE(spy.count(),0);
        server.respond=[](const HttpRequest&,QTcpSocket* socket){Server::send(socket,results());};
        client.search("New film","en");QVERIFY(result(spy).value("success").toBool());QCOMPARE(spy.count(),1);
    }
    void selectedFilesDownloadAndCache() {
        QTemporaryDir directory;Server server;
        server.respond=[&](const HttpRequest& request,QTcpSocket* socket){
            if(request.url.path()=="/api/subtitles")Server::send(socket,results());
            else if(request.url.path()=="/api/download")Server::send(socket,QJsonDocument(QJsonObject{{"link",server.base().resolved(QUrl("../file.srt")).toString()}}).toJson());
            else Server::send(socket,captions());
        };
        SubtitleSourceService client(options(server));client.setStorageDirectory(directory.path());QVERIFY(client.setApiKey("unit-secret"));
        const auto sources=search(client);const auto selected=sources[0].toMap().value("url").toString();
        QSignalSpy spy(&client,&SubtitleSourceService::preparationFinished);client.prepareRows(row({selected,selected}));auto answer=result(spy);
        QVERIFY(answer.value("success").toBool());QCOMPARE(server.requests.size(),3);
        const auto body=QJsonDocument::fromJson(server.requests[1].body).object();QCOMPARE(body.value("file_id").toInt(),42);QCOMPARE(body.value("sub_format").toString(),"srt");
        QVERIFY(!server.requests[2].headers.contains("api-key"));QVERIFY(!server.requests[2].headers.contains("authorization"));
        const auto tracks=answer.value("rows").toList()[0].toMap().value("tracks").toStringList();QCOMPARE(tracks.size(),2);QCOMPARE(tracks[0],tracks[1]);
        QFile file(QUrl(tracks[0]).toLocalFile());QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),captions());
        spy.clear();client.prepareRows(row({selected}));QVERIFY(result(spy).value("success").toBool());QCOMPARE(server.requests.size(),3);
        const auto cached=search(client)[0].toMap().value("url").toString();QCOMPARE(cached,tracks[0]);
    }
    void languageListIsValidatedAndCached() {
        QTemporaryDir directory;Server server;
        server.respond=[](const HttpRequest&,QTcpSocket* socket){Server::send(socket,R"json({"data":[{"language_code":"en","language_name":"English"},{"language_code":"pt-br","language_name":"Portuguese (Brazil)"},{"language_code":"en","language_name":"Duplicate"},{"language_code":"../../bad","language_name":"Bad"}]})json");};
        SubtitleSourceService client(options(server));client.setStorageDirectory(directory.path());QVERIFY(client.setApiKey("unit-secret"));
        QSignalSpy spy(&client,&SubtitleSourceService::languagesChanged);client.fetchLanguages();QVERIFY(spy.wait());
        QCOMPARE(client.languages().size(),2);QCOMPARE(server.requests.size(),1);QCOMPARE(server.requests[0].url.path(),"/api/infos/languages");
        client.fetchLanguages();QTest::qWait(40);QCOMPARE(server.requests.size(),1);
    }
    void partialDownloadCanBeRetriedWithoutSpendingQuotaAgain() {
        QTemporaryDir directory;Server server;bool fail=true;
        server.respond=[&](const HttpRequest& request,QTcpSocket* socket){
            if(request.url.path()=="/api/subtitles")Server::send(socket,results());
            else if(request.url.path()=="/api/download") {
                const auto id=QJsonDocument::fromJson(request.body).object().value("file_id").toInt();
                if(id==43 && fail)Server::send(socket,"{}",429);
                else Server::send(socket,QJsonDocument(QJsonObject{{"link",server.base().resolved(QUrl("../file.srt")).toString()}}).toJson());
            } else Server::send(socket,captions());
        };
        SubtitleSourceService client(options(server));client.setStorageDirectory(directory.path());QVERIFY(client.setApiKey("unit-secret"));
        const auto sources=search(client);const auto a=sources[0].toMap().value("url").toString(),b=sources[1].toMap().value("url").toString();
        QSignalSpy spy(&client,&SubtitleSourceService::preparationFinished);client.prepareRows(row({a,b}));auto answer=result(spy);
        QVERIFY(!answer.value("success").toBool());QCOMPARE(answer.value("resolved").toMap().size(),1);QVERIFY(answer.value("rows").toList().isEmpty());
        QCOMPARE(server.requests.size(),4);fail=false;spy.clear();client.prepareRows(row({a,b}));answer=result(spy);
        QVERIFY(answer.value("success").toBool());QCOMPARE(server.requests.size(),6);QCOMPARE(answer.value("resolved").toMap().size(),2);
    }
    void cancelDownloadDoesNotEmitOrWriteAFile() {
        QTemporaryDir directory;Server server;
        server.respond=[](const HttpRequest& request,QTcpSocket* socket){if(request.url.path()=="/api/subtitles")Server::send(socket,results());};
        SubtitleSourceService client(options(server));client.setStorageDirectory(directory.path());QVERIFY(client.setApiKey("unit-secret"));
        const auto selected=search(client)[0].toMap().value("url").toString();QSignalSpy spy(&client,&SubtitleSourceService::preparationFinished);client.prepareRows(row({selected}));
        QTRY_COMPARE(server.requests.size(),2);client.cancelPreparation();QTest::qWait(40);QVERIFY(!client.preparing());QCOMPARE(spy.count(),0);
        QVERIFY(!QFileInfo::exists(directory.filePath("opensubtitles/42/The_Wand_S01E04.en.srt")));
    }
    void tlsBackendIsAvailable() {
        QVERIFY2(QSslSocket::supportsSsl(),"The subtitle source needs a working TLS backend");
    }
    void rejectsBadFilesAndLimits_data() {
        QTest::addColumn<QByteArray>("body");QTest::addColumn<qint64>("limit");
        QTest::newRow("html")<<QByteArray("<html>not subtitles</html>")<<qint64(1024);
        QTest::newRow("size")<<(captions()+QByteArray(2048,'x'))<<qint64(1024);
    }
    void rejectsBadFilesAndLimits() {
        QFETCH(QByteArray,body);QFETCH(qint64,limit);QTemporaryDir directory;Server server;
        server.respond=[&](const HttpRequest& request,QTcpSocket* socket){
            if(request.url.path()=="/api/subtitles")Server::send(socket,results());
            else if(request.url.path()=="/api/download")Server::send(socket,QJsonDocument(QJsonObject{{"link",server.base().resolved(QUrl("../file.srt")).toString()}}).toJson());
            else Server::send(socket,body);
        };
        auto o=options(server);o.maxFileBytes=limit;SubtitleSourceService client(o);client.setStorageDirectory(directory.path());QVERIFY(client.setApiKey("unit-secret"));
        const auto selected=search(client)[0].toMap().value("url").toString();QSignalSpy spy(&client,&SubtitleSourceService::preparationFinished);client.prepareRows(row({selected}));
        QVERIFY(!result(spy).value("success").toBool());QVERIFY(!QFileInfo::exists(directory.filePath("opensubtitles/42/The_Wand_S01E04.en.srt")));
    }
    void apiRedirectDoesNotSendKey() {
        QTemporaryDir directory;Server server;server.respond=[](const HttpRequest&,QTcpSocket* socket){Server::send(socket,{},302,"Location: http://127.0.0.1:1/leak\r\n");};
        SubtitleSourceService client(options(server));client.setStorageDirectory(directory.path());QVERIFY(client.setApiKey("unit-secret"));
        QSignalSpy spy(&client,&SubtitleSourceService::searchFinished);client.search("Film","en");QVERIFY(!result(spy).value("success").toBool());QCOMPARE(server.requests.size(),1);
    }
    void linksStayInTheDraftUntilSave() {
        QTemporaryDir directory;Server server;SubtitleSourceService client(options(server));client.setStorageDirectory(directory.path());
        const auto url=server.base().resolved(QUrl("../Episode.en.srt?token=private-value")).toString();
        const auto answer=client.addLinks({url,url+"#duplicate","file:///etc/passwd","https://user:password@example.com/a.srt","javascript:alert(1)","ftp://example.com/a.srt"});
        QVERIFY(answer.value("success").toBool());const auto sources=answer.value("sources").toList();QCOMPARE(sources.size(),1);
        QVERIFY(!answer.value("error").toString().isEmpty());const auto item=sources.first().toMap();
        QCOMPARE(item.value("name").toString(),"Episode.en.srt");QVERIFY(item.value("url").toString().startsWith("subtitlelink:"));
        QVERIFY(!item.value("url").toString().contains("private-value"));QCOMPARE(item.value("kind").toString(),"link");
        QVERIFY(server.requests.isEmpty());QVERIFY(!QFileInfo::exists(directory.filePath("links")));
        client.resetSession();QVERIFY(server.requests.isEmpty());
    }
    void directLinkFormats_data() {
        QTest::addColumn<QByteArray>("bytes");QTest::addColumn<QString>("format");
        QTest::newRow("srt")<<captions()<<QString("srt");
        QTest::newRow("vtt")<<QByteArray("WEBVTT\n\n00:01.000 --> 00:03.000\nCaption\n")<<QString("vtt");
        QTest::newRow("ass")<<QByteArray("[Script Info]\nScriptType: v4.00+\n[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\nDialogue: 0,0:00:01.00,0:00:03.00,Default,,0,0,0,,Caption\n")<<QString("ass");
        QTest::newRow("ssa")<<QByteArray("[Script Info]\n[V4 Styles]\n[Events]\nDialogue: Marked=0,0:00:01.00,0:00:03.00,Default,,0,0,0,,Caption\n")<<QString("ssa");
        QTest::newRow("microdvd")<<QByteArray("{25}{75}Caption\n")<<QString("sub");
        QTest::newRow("subviewer")<<QByteArray("[SUBTITLE]\n00:00:01.00,00:00:03.00\nCaption\n")<<QString("sub");
        QTest::newRow("sami")<<QByteArray("<SAMI><BODY><SYNC Start=1000><P>Caption<SYNC Start=3000><P>&nbsp;</BODY></SAMI>")<<QString("smi");
        QTest::newRow("ttml")<<QByteArray("<tt xmlns=\"http://www.w3.org/ns/ttml\"><body><div><p begin=\"1s\" end=\"3s\">Caption</p></div></body></tt>")<<QString("ttml");
        QTest::newRow("usf")<<QByteArray("<USFSubtitles version=\"1.0\"><subtitles><subtitle start=\"00:00:01.000\" stop=\"00:00:03.000\"><text>Caption</text></subtitle></subtitles></USFSubtitles>")<<QString("usf");
        QTest::newRow("legacy")<<QByteArray("1\n00:00:01,000 --> 00:00:03,000\nCaf\xe9\n")<<QString("srt");
        QByteArray utf16("\xff\xfe",2);for(const auto ch:QString::fromUtf8(captions())) {utf16.append(char(ch.unicode()&255));utf16.append(char(ch.unicode()>>8));}
        QTest::newRow("utf16")<<utf16<<QString("srt");
    }
    void directLinkFormats() {
        QFETCH(QByteArray,bytes);QFETCH(QString,format);QTemporaryDir directory;Server server;
        server.respond=[&](const HttpRequest&,QTcpSocket* socket){Server::send(socket,bytes);};
        SubtitleSourceService client(options(server));client.setStorageDirectory(directory.path());
        const auto url=server.base().resolved(QUrl("../caption.download?token=private-value")).toString();
        const auto source=client.addLinks({url}).value("sources").toList().first().toMap().value("url").toString();
        QSignalSpy spy(&client,&SubtitleSourceService::preparationFinished);client.prepareRows(row({source}));const auto answer=result(spy);
        QVERIFY2(answer.value("success").toBool(),qPrintable(answer.value("error").toString()));QCOMPARE(server.requests.size(),1);
        QVERIFY(!server.requests.first().headers.contains("api-key"));QVERIFY(!server.requests.first().headers.contains("authorization"));
        const auto local=answer.value("rows").toList().first().toMap().value("tracks").toStringList().first();
        QCOMPARE(QFileInfo(QUrl(local).toLocalFile()).suffix(),format);QFile file(QUrl(local).toLocalFile());QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),bytes);
        spy.clear();client.prepareRows(row({source}));QVERIFY(result(spy).value("success").toBool());QCOMPARE(server.requests.size(),1);
        QCOMPARE(client.addLinks({url}).value("sources").toList().first().toMap().value("url").toString(),local);
    }
    void linkRejectsOtherContent_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("page")<<QByteArray("<!DOCTYPE html><html><body>Download</body></html>");
        QTest::newRow("html-with-cues")<<(QByteArray("<?xml version=\"1.0\"?><html><body>\n")+captions()+"</body></html>");
        QTest::newRow("archive")<<QByteArray("PK\003\004archive");
        QTest::newRow("binary")<<QByteArray("binary\0data",11);
        QTest::newRow("invalid")<<QByteArray("not a subtitle file");
    }
    void linkRejectsOtherContent() {
        QFETCH(QByteArray,bytes);QTemporaryDir directory;Server server;
        server.respond=[&](const HttpRequest&,QTcpSocket* socket){Server::send(socket,bytes);};
        SubtitleSourceService client(options(server));client.setStorageDirectory(directory.path());
        const auto source=client.addLinks({server.base().toString()}).value("sources").toList().first().toMap().value("url").toString();
        QSignalSpy spy(&client,&SubtitleSourceService::preparationFinished);client.prepareRows(row({source}));const auto answer=result(spy);
        QVERIFY(!answer.value("success").toBool());QVERIFY(!answer.value("error").toString().isEmpty());QVERIFY(answer.value("rows").toList().isEmpty());
        QVERIFY(!QFileInfo::exists(directory.filePath("links")));
    }
    void linkRedirectKeepsCredentialsOffTheRequest() {
        QTemporaryDir directory;Server server;
        server.respond=[](const HttpRequest& request,QTcpSocket* socket){
            if(request.url.path()=="/api/")Server::send(socket,{},302,"Location: /caption.srt\r\n");else Server::send(socket,captions());
        };
        SubtitleSourceService client(options(server));client.setStorageDirectory(directory.path());QVERIFY(client.setApiKey("unit-secret"));
        const auto source=client.addLinks({server.base().toString()}).value("sources").toList().first().toMap().value("url").toString();
        QSignalSpy spy(&client,&SubtitleSourceService::preparationFinished);client.prepareRows(row({source}));QVERIFY(result(spy).value("success").toBool());
        QCOMPARE(server.requests.size(),2);for(const auto& request:server.requests) {QVERIFY(!request.headers.contains("api-key"));QVERIFY(!request.headers.contains("authorization"));}
    }
    void linkCancellationAndTimeout() {
        QTemporaryDir directory;Server server;server.respond=[](const HttpRequest&,QTcpSocket*){};
        auto o=options(server);o.timeoutMs=80;SubtitleSourceService client(o);client.setStorageDirectory(directory.path());
        const auto source=client.addLinks({server.base().toString()}).value("sources").toList().first().toMap().value("url").toString();
        QSignalSpy spy(&client,&SubtitleSourceService::preparationFinished);client.prepareRows(row({source}));QVERIFY(!result(spy).value("success").toBool());
        spy.clear();client.prepareRows(row({source}));QTRY_COMPARE(server.requests.size(),2);client.cancelPreparation();QTest::qWait(100);QCOMPARE(spy.count(),0);QVERIFY(!client.preparing());
        QVERIFY(!QFileInfo::exists(directory.filePath("links")));
    }
    void linkHttpErrorsDoNotAskForAnApiKey() {
        QTemporaryDir directory;Server server;server.respond=[](const HttpRequest&,QTcpSocket* socket){Server::send(socket,"{}",403);};
        SubtitleSourceService client(options(server));client.setStorageDirectory(directory.path());
        const auto source=client.addLinks({server.base().toString()}).value("sources").toList().first().toMap().value("url").toString();
        QSignalSpy spy(&client,&SubtitleSourceService::preparationFinished);client.prepareRows(row({source}));const auto answer=result(spy);
        QVERIFY(!answer.value("success").toBool());QVERIFY(!answer.value("error").toString().contains("OpenSubtitles"));QVERIFY(!answer.value("error").toString().contains("API"));
    }
    void linkDownloadSizeIsBounded() {
        QTemporaryDir directory;Server server;server.respond=[](const HttpRequest&,QTcpSocket* socket){Server::send(socket,captions()+QByteArray(2048,'x'));};
        auto o=options(server);o.maxFileBytes=1024;SubtitleSourceService client(o);client.setStorageDirectory(directory.path());
        const auto source=client.addLinks({server.base().toString()}).value("sources").toList().first().toMap().value("url").toString();
        QSignalSpy spy(&client,&SubtitleSourceService::preparationFinished);client.prepareRows(row({source}));const auto answer=result(spy);
        QVERIFY(!answer.value("success").toBool());QVERIFY(!QFileInfo::exists(directory.filePath("links")));
    }
};
QTEST_GUILESS_MAIN(TestSubtitleSources)
#include "test_subtitle_sources.moc"
