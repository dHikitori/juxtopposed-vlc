// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QSignalSpy>
#include "addons/addon_discovery_service.hpp"
#include "addons/addon_player_discovery.hpp"
#include "addons/addon_catalogue_view.hpp"
#include "addons/addon_manager.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <atomic>

using namespace vlc::addons;
#ifdef HAKONE_DISCOVERY_INVOKE_WRAP
namespace {
std::function<QString(const QString&, const QString&, const QVariantMap&)> discoveryInvoke;
struct InvokeScope {
    explicit InvokeScope(decltype(discoveryInvoke) invoke) { discoveryInvoke = std::move(invoke); }
    ~InvokeScope() { discoveryInvoke = {}; }
};
}
extern "C" QString __real__ZN3vlc6addons12AddonManager6invokeERK7QStringS4_RK4QMapIS2_8QVariantE(AddonManager*,const QString&,const QString&,const QVariantMap&);
extern "C" QString __wrap__ZN3vlc6addons12AddonManager6invokeERK7QStringS4_RK4QMapIS2_8QVariantE(AddonManager* owner,const QString& source,const QString& operation,const QVariantMap& context)
{
    const auto invoke = discoveryInvoke;
    return invoke ? invoke(source, operation, context)
        : __real__ZN3vlc6addons12AddonManager6invokeERK7QStringS4_RK4QMapIS2_8QVariantE(owner,source,operation,context);
}
#endif
namespace {
struct DisplayFixture {
    QTemporaryDir profile;
    AddonStreamLifetime lifetime;
    AddonManager manager{profile.path()};
    StoreAuthority authority{profile.path() + QStringLiteral("/display-fixture")};
    DisplayCatalogueSnapshot snapshot;
    QPointer<AddonCatalogueView> display;
    std::atomic_uint reads{0}, credentials{0};
    std::atomic_bool readOnGui{false}, credentialOnGui{false};
    QThread* gui = QThread::currentThread();
    explicit DisplayFixture(bool protectedSettings = false) {
        DisplayCataloguePackage row; auto& package = row.package;
        package.manifest.id = QStringLiteral("org.example.radio"); package.manifest.name = QStringLiteral("Radio");
        package.manifest.version = QStringLiteral("1.0.0"); package.identity.source = QStringLiteral("https://github.com/hakone-org/radio-fixture");
        package.digest = QByteArray(64, 'a'); package.generation = 1; package.enabled = true; row.savedEnabled = true;
        row.schema = {{"type", "object"}, {"properties", QJsonObject{}}};
        if (protectedSettings) {
            row.schema = {{"type", "object"}, {"required", QJsonArray{"apiKey"}},
                {"properties", QJsonObject{{"apiKey", QJsonObject{{"type", "string"}, {"writeOnly", true}}}}}};
            row.settings = {{"_credentials", QJsonObject{{"apiKey", "00000000-0000-4000-8000-000000000001"}}}};
        }
        Contribution source; source.id = QStringLiteral("source"); source.type = QStringLiteral("discovery.source");
        source.operations = {QStringLiteral("roots"), QStringLiteral("search")};
        source.document = {{"id", source.id}, {"type", source.type}, {"name", "Radio"}};
        package.manifest.contributions.append(source); snapshot.packages.append(row);
        AddonCatalogueView::Backend backend;
        backend.capture = [this]() -> std::optional<DisplayCatalogueSource> {
            DisplayCatalogueSource source; source.queuedAuthority = authority.capture(); source.workerExecutable = QStringLiteral("/bin/true");
            auto state = snapshot; state.authority = source.queuedAuthority; state.queuedAuthority = source.queuedAuthority;
            source.reader = [this, state](const DisplayCatalogueRequest&, DisplayCatalogueSnapshot* out, QString*) {
                ++reads; readOnGui = QThread::currentThread() == gui; *out = state; return true;
            };
            source.credentials = [this](const QString&, const QString&) {
                ++credentials; credentialOnGui = QThread::currentThread() == gui; return QByteArray("temporary-fixture-key");
            };
            return source;
        };
        backend.subscribe = [this](std::function<void()> wake) { return authority.onChanged(std::move(wake)); };
        display = new AddonCatalogueView(std::move(backend), lifetime.handle(), &manager);
        display->setObjectName(QStringLiteral("hakoneDisplayCatalogue"));
    }
    ~DisplayFixture() { delete display.data(); lifetime.close(); lifetime.handle().waitQt(std::chrono::seconds(5)); }
};
}
class TestAddonDiscoveryService : public QObject
{
    Q_OBJECT
    struct Fixture {
        QVariantList calls;
        QStringList canceled, released;
        QVariantList sources{{QVariantMap{{"id", "org.example.radio/source"}, {"name", "Radio"}, {"enabled", true}, {"configured", true},
            {"cachePartition", "release-1"}, {"operations", QStringList{"roots", "browse", "search", "resolve"}}}}};
        AddonDiscoveryCoordinator::Backend backend(bool playable = false) {
            AddonDiscoveryCoordinator::Backend result;
            result.sources = [this] { return sources; };
            result.invoke = [this](const QString& source, const QString& operation, const QVariantMap& context) {
                const auto id = QString::number(calls.size() + 1);
                calls.append(QVariantMap{{"id", id}, {"source", source}, {"operation", operation}, {"context", context}}); return id;
            };
            result.cancel = [this](const QString& id) { canceled.append(id); };
            if (playable) result.authorizePlayable = [](const QString&, const QVariantMap&, auto done) { done(QStringLiteral("playable:42"), {}); return QStringLiteral("gate-job"); };
            result.cancelPlayable = [](const QString&) {};
            return result;
        }
        void complete(AddonDiscoveryService& service, int index, const QVariantMap& data) {
            const auto call = calls[index].toMap();
            service.operationFinished(call.value("id").toString(), call.value("source").toString(), QStringLiteral("match"), data, {});
        }
        static QVariantMap item(const QString& id, const QString& kind = QStringLiteral("media")) {
            return {{"id", id}, {"kind", kind}, {"title", QStringLiteral("<b>Station</b>")}, {"mediaKind", "audio"},
                {"url", "https://example.org/private-stream"}, {"options", QStringList{"not-permitted"}}};
        }
    };
    static QVariantList items(AddonDiscoveryService& service, const QString& id) { return service.view(id).value("items").toList(); }
private slots:
    void attachedRowsAndConstructorsAreMemoryOnlyAndRetainViewsOnUnrelatedChanges() {
#ifndef HAKONE_DISCOVERY_INVOKE_WRAP
        QSKIP("This real Manager adapter fixture needs the test-only GNU invocation wrapper.");
#else
        DisplayFixture display; QTRY_VERIFY(display.display->isCurrent());
        Fixture fixture; InvokeScope invoke(fixture.backend().invoke); AddonDiscoveryService service(&display.manager, fixture.backend(), {});
        QVERIFY(service.available()); QCOMPARE(service.sources().size(), 1); QCOMPARE(display.credentials.load(), 0u);
        const auto reads = display.reads.load();
        for (int i = 0; i < 20; ++i) { QCOMPARE(service.sources().size(), 1); QVERIFY(service.available()); }
        QCOMPARE(display.reads.load(), reads); QVERIFY(!display.readOnGui);
        const auto id = service.openSource("org.example.radio/source"); QVERIFY(!id.isEmpty());
        QTRY_COMPARE(fixture.calls.size(), 1); fixture.complete(service, 0, {{"items", QVariantList{Fixture::item("station")}}});
        QTRY_COMPARE(items(service, id).size(), 1); const auto retained = items(service, id);
        QSignalSpy closed(&service, &AddonDiscoveryService::viewClosed);
        emit display.manager.revisionChanged(); QCOMPARE(closed.size(), 0); QCOMPARE(items(service, id), retained);
        display.display->refresh(); QCOMPARE(items(service, id), retained); QVERIFY(!service.available());
        QTRY_VERIFY(service.available()); QCOMPARE(closed.size(), 0); QCOMPARE(items(service, id), retained);
        // A different package changes the global token, not this source identity.
        auto other = display.snapshot.packages.first(); other.package.manifest.id = QStringLiteral("org.example.other");
        other.package.manifest.contributions.clear(); display.snapshot.packages.append(other); display.authority.invalidate();
        QVERIFY(!service.available()); QCOMPARE(items(service, id), retained);
        QTRY_VERIFY(service.available()); QCOMPARE(closed.size(), 0); QCOMPARE(items(service, id), retained);
        ++display.snapshot.packages[0].settingsRevision; display.authority.invalidate();
        QTRY_COMPARE(closed.size(), 1); QVERIFY(service.view(id).isEmpty());
#endif
    }
    void visibleRequestsSharePresenceAndHiddenRowsNeverOpenWallet() {
        DisplayFixture display(true); QTRY_VERIFY(display.display->isCurrent());
        Fixture fixture; AddonDiscoveryService service(&display.manager, fixture.backend(), {});
        QCOMPARE(service.sources().size(), 1); QVERIFY(!service.sources().first().toMap().value("configurationKnown").toBool());
        QVERIFY(service.openSource("org.example.radio/source").isEmpty()); QCOMPARE(display.credentials.load(), 0u);
        QStringList requests;
        for (int i = 0; i < 16; ++i) { const auto request = service.acquireDisplay(); QVERIFY(!request.isEmpty()); requests.append(request); }
        QVERIFY(service.acquireDisplay().isEmpty());
        QTRY_VERIFY(service.sources().first().toMap().value("configured").toBool()); QCOMPARE(display.credentials.load(), 1u); QVERIFY(!display.credentialOnGui);
        const auto count = display.reads.load(); QTest::qWait(250); QCOMPARE(display.reads.load(), count); QVERIFY(fixture.calls.isEmpty());
        for (const auto& request : requests) service.releaseDisplay(request);
        display.authority.invalidate(); QTRY_VERIFY(display.display->isCurrent()); QCOMPARE(display.credentials.load(), 1u);
        QVERIFY(!service.sources().first().toMap().value("configurationKnown").toBool());
        const auto next = service.acquireDisplay(); QVERIFY(!next.isEmpty());
        QTRY_VERIFY(service.sources().first().toMap().value("configured").toBool()); QCOMPARE(display.credentials.load(), 2u);
        service.close(); QVERIFY(!service.available()); QVERIFY(service.acquireDisplay().isEmpty());
        display.authority.invalidate(); QTRY_VERIFY(display.display->isCurrent()); QCOMPARE(display.credentials.load(), 2u);
    }
    void destroyedAttachedViewNeverFallsBackToManagerScans() {
        DisplayFixture display; QTRY_VERIFY(display.display->isCurrent()); Fixture fixture;
        AddonDiscoveryService service(&display.manager, fixture.backend(), {}); QCOMPARE(service.sources().size(), 1);
        const auto request = service.acquireDisplay(); QVERIFY(!request.isEmpty());
        delete display.display.data(); QVERIFY(!service.available()); QVERIFY(service.sources().isEmpty());
        service.releaseDisplay(request); QVERIFY(service.openSource("org.example.radio/source").isEmpty()); QVERIFY(fixture.calls.isEmpty());
    }
    void fullSharedQuotaHasAnExplicitReopenPathWithoutRetries() {
        DisplayFixture display(true); QTRY_VERIFY(display.display->isCurrent());
        QStringList held; for (int i = 0; i < 32; ++i) { const auto ticket = display.display->acquireFeature(QStringLiteral("metadata.provider")); QVERIFY(!ticket.isEmpty()); held.append(ticket); }
        Fixture fixture; AddonDiscoveryService service(&display.manager, fixture.backend(), {});
        const auto request = service.acquireDisplay(); QVERIFY(!request.isEmpty()); QVERIFY(!service.available());
        QVERIFY(!service.preparationError().isEmpty()); const auto reads = display.reads.load();
        QTest::qWait(250); QCOMPARE(display.credentials.load(), 0u);
        // The already-queued unrelated feature capture may finish once; the
        // refused Discovery demand must not create a recurring retry loop.
        QTRY_VERIFY(display.display->isCurrent()); const auto quiet = display.reads.load();
        QTest::qWait(250); QCOMPARE(display.reads.load(), quiet); QVERIFY(quiet >= reads);
        for (const auto& ticket : held) display.display->release(ticket);
        QVERIFY(!service.available()); service.releaseDisplay(request);
        const auto reopened = service.acquireDisplay(); QVERIFY(!reopened.isEmpty()); QVERIFY(service.preparationError().isEmpty());
        QTRY_VERIFY(service.sources().first().toMap().value("configured").toBool()); QCOMPARE(display.credentials.load(), 1u);
        service.releaseDisplay(reopened);
    }
    void deniedGuardKeepsStaticRowsAndDoesNotRefreshNativeSource() {
        Fixture fixture; auto backend = fixture.backend(); bool fresh = true; backend.current = [&] { return fresh; };
        AddonDiscoveryService service(std::move(backend), {}); int refreshes = 0;
        service.setSourcePlayback({{}, {}, [&](const QString&) { ++refreshes; }});
        fresh = false; QVERIFY(service.openSource("org.example.radio/source").isEmpty()); QCOMPARE(refreshes, 0);
        fresh = true; const auto id = service.openSource("org.example.radio/source"); QTRY_COMPARE(fixture.calls.size(), 1); QCOMPARE(refreshes, 1);
        fixture.complete(service, 0, {{"items", QVariantList{Fixture::item("station")}}}); QTRY_COMPARE(items(service, id).size(), 1);
        const auto old = items(service, id); fresh = false; QVERIFY(!service.search(id, "query")); QVERIFY(!service.canEnqueue(id));
        QCOMPARE(items(service, id), old); fresh = true; QVERIFY(service.search(id, "query"));
    }
    void stablePlayAndEnqueueDoNotResolveBeforeTheNativeOpen() {
        Fixture fixture; fixture.sources[0]=QVariantMap{{"id","org.example.radio/source"},{"name","Radio"},{"enabled",true},{"configured",true},
            {"accountReference","public:fixture"},{"cachePartition","release-1"},{"operations",QStringList{"roots","resolve"}}};
        int played=0,appended=0,refreshed=0; bool approved=false,emptyHandle=false; QVariantList saved;
        AddonQueueTransactions::Backend queues;
        queues.snapshot=[](const auto&) { return AddonQueueTransactions::Snapshot{true,"queue",1}; };
        queues.append=[&](const auto& value,auto done) { ++appended; saved=value.references; done(true,{}); return []{}; };
        queues.playReference=[&](const auto& value,auto done) { ++played; saved=value.references; emptyHandle=value.playbackHandle.isEmpty(); done(true,{}); return AddonQueueTransactions::Cancel{}; };
        AddonDiscoveryService service(fixture.backend(),queues);
        service.setSourcePlayback({[&](const auto&) { return approved; },[&](const auto& ref) { return approved && ref.value("accountReference")==QLatin1String("public:fixture"); },[&](const auto&) { ++refreshed; }});
        const auto view=service.openSource("org.example.radio/source"); QTRY_COMPARE(fixture.calls.size(),1); QCOMPARE(refreshed,1);
        fixture.complete(service,0,{{"items",QVariantList{Fixture::item("station")}}}); QTRY_COMPARE(items(service,view).size(),1);
        const auto item=items(service,view).first().toMap().value("ref").toString();
        QVERIFY(!service.canPlay(view)); QVERIFY(!service.canEnqueue(view)); QVERIFY(service.play(view,item).isEmpty());
        approved=true; service.refreshSourcePlayback(); QVERIFY(service.canPlay(view)); QVERIFY(service.canEnqueue(view));
        QVERIFY(!service.enqueue(view,{item}).isEmpty()); QVERIFY(!service.play(view,item).isEmpty());
        QCOMPARE(appended,1); QCOMPARE(played,1); QVERIFY(emptyHandle); QCOMPARE(fixture.calls.size(),1); QCOMPARE(saved.size(),1);
        QCOMPARE(saved.first().toMap().value("itemId").toString(),QStringLiteral("station")); QVERIFY(!saved.first().toMap().contains("url"));
        approved=false; QVERIFY(service.play(view,item).isEmpty()); QVERIFY(service.enqueue(view,{item}).isEmpty());
        QCOMPARE(appended,1); QCOMPARE(played,1);
    }
    void playlistSelectionKeepsNativeAuthorityOutsideProviderCalls() {
        Fixture fixture; QString appended; QStringList released;
        AddonQueueTransactions::Backend queues;
        queues.snapshot=[](const QString& target) { return AddonQueueTransactions::Snapshot{target==QLatin1String("library:one"),target,1}; };
        queues.append=[&](const auto& transaction,auto done) { appended=transaction.target; done(true,{}); return []{}; };
        AddonDiscoveryService service(fixture.backend(),queues);
        AddonDiscoveryService::PlaylistBackend playlists;
        playlists.list=[](const QString&,auto done) { done(true,{QVariantMap{{"ref","choice:one"},{"name","Saved playlist"}}},false,{}); return []{}; };
        playlists.prepare=[](const QString& choice,auto done) { if (choice==QLatin1String("choice:one")) done("library:one",{}); return []{}; };
        playlists.release=[&](const QString& target) { released.append(target); }; service.setPlaylistBackend(playlists);
        const auto view=service.openSource("org.example.radio/source"); QTRY_COMPARE(fixture.calls.size(),1);
        fixture.complete(service,0,{{"items",QVariantList{Fixture::item("station")}}}); QTRY_COMPARE(items(service,view).size(),1);
        QTRY_VERIFY(service.view(view).value("playlistAvailable").toBool()); QVERIFY(!service.canEnqueue(view));
        QVERIFY(!service.choosePlaylist(view,"native-id:42")); QVERIFY(service.choosePlaylist(view,"choice:one"));
        QCOMPARE(service.view(view).value("playlistTarget").toString(),QStringLiteral("library:one"));
        const auto item=items(service,view).first().toMap().value("ref").toString();
        QVERIFY(service.enqueue(view,{item},"library:foreign").isEmpty());
        QVERIFY(!service.enqueue(view,{item},"library:one").isEmpty());
        QCOMPARE(appended,QStringLiteral("library:one")); QTRY_COMPARE(released,QStringList{QStringLiteral("library:one")});
        QVERIFY(service.view(view).value("playlistTarget").toString().isEmpty());
        QVERIFY(service.enqueue(view,{item},"library:one").isEmpty()); QCOMPARE(fixture.calls.size(),1);
        QVERIFY(!fixture.calls.first().toMap().value("context").toMap().contains("playlist"));
    }
    void latePlaylistPreparationIsReleasedAfterViewCloseOrReselection() {
        Fixture fixture; AddonDiscoveryService service(fixture.backend(),{}); QStringList released,releasedChoices; int canceled=0;
        QList<AddonDiscoveryService::PlaylistBackend::Prepared> pending;
        AddonDiscoveryService::PlaylistBackend playlists;
        playlists.list=[](const QString&,auto done) { done(true,{QVariantMap{{"ref","choice:one"},{"name","Playlist"}}},false,{}); return []{}; };
        playlists.prepare=[&](const QString&,auto done) { pending.append(std::move(done)); return [&] { ++canceled; }; };
        playlists.release=[&](const QString& target) { released.append(target); };
        playlists.releaseChoices=[&](const QStringList& choices) { releasedChoices.append(choices); }; service.setPlaylistBackend(playlists);
        const auto view=service.openSource("org.example.radio/source"); QTRY_VERIFY(service.view(view).value("playlistAvailable").toBool());
        QVERIFY(service.choosePlaylist(view,"choice:one")); QVERIFY(service.choosePlaylist(view,"choice:one")); QCOMPARE(canceled,1);
        pending[0]("library:old",{}); QCOMPARE(released,QStringList{QStringLiteral("library:old")});
        pending[1]("library:current",{}); QCOMPARE(service.view(view).value("playlistTarget").toString(),QStringLiteral("library:current"));
        service.closeView(view); QVERIFY(released.contains("library:current"));
        QVERIFY(releasedChoices.contains("choice:one"));
        const auto next=service.openSource("org.example.radio/source"); QTRY_VERIFY(service.view(next).value("playlistAvailable").toBool());
        QVERIFY(service.choosePlaylist(next,"choice:one")); service.invalidate(); pending.last()("library:late",{});
        QVERIFY(released.contains("library:late")); QVERIFY(service.view(next).isEmpty());
    }
    void nativeBackendMultiplexerHasNoFallbackForUnknownTargets() {
        QString appended; AddonDiscoveryNativeBackend queue,library;
        queue.snapshot=[](const QString& target) { return AddonQueueTransactions::Snapshot{true,"queue-identity",1}; };
        library.snapshot=[](const QString& target) { return AddonQueueTransactions::Snapshot{target==QLatin1String("library:one"),target,2}; };
        queue.append=[&](const auto&,auto done) { appended="queue"; done(true,{}); return []{}; };
        library.append=[&](const auto&,auto done) { appended="library"; done(true,{}); return []{}; };
        const auto combined=combineDiscoveryBackends(queue,library); QVERIFY(combined.libraryAppendAvailable); QVERIFY(!combined.stableReferencePlaybackAvailable);
        QVERIFY(combined.snapshot("queue").available); QVERIFY(combined.snapshot("library:one").available); QVERIFY(!combined.snapshot("other:one").available);
        AddonQueueTransactions::Transaction transaction; transaction.target="other:one"; bool success=true;
        combined.append(transaction,[&](bool result,const QString&) { success=result; }); QVERIFY(!success); QVERIFY(appended.isEmpty());
        transaction.target="library:one"; combined.append(transaction,[](bool,const QString&) {}); QCOMPARE(appended,QStringLiteral("library"));
        transaction.target="queue"; combined.append(transaction,[](bool,const QString&) {}); QCOMPARE(appended,QStringLiteral("queue"));
    }
    void browseAndSearchUseOnlyHostReferences() {
        Fixture fixture; AddonDiscoveryService service(fixture.backend(), {});
        const auto view = service.openSource("org.example.radio/source"); QVERIFY(!view.isEmpty());
        QTRY_COMPARE(fixture.calls.size(), 1);
        fixture.complete(service, 0, {{"items", QVariantList{Fixture::item("root", "node")}}});
        QTRY_COMPARE(items(service, view).size(), 1);
        const auto node = items(service, view)[0].toMap(); QVERIFY(node.value("ref").toString() != QLatin1String("root"));
        QVERIFY(!node.contains("url")); QVERIFY(!node.contains("options"));
        QVERIFY(service.browse(view, node.value("ref").toString())); QTRY_COMPARE(fixture.calls.size(), 2);
        QCOMPARE(fixture.calls[1].toMap().value("context").toMap().value("nodeRef").toString(), QStringLiteral("root"));
        fixture.complete(service, 1, {{"items", QVariantList{Fixture::item("station")}}});
        QTRY_COMPARE(items(service, view).size(), 1); QVERIFY(service.view(view).value("canGoBack").toBool());
        QVERIFY(service.search(view, "new query")); QTRY_COMPARE(fixture.calls.size(), 3);
        QVERIFY(service.search(view, "latest query")); QTRY_COMPARE(fixture.calls.size(), 4);
        QVERIFY(fixture.canceled.contains("3"));
        fixture.complete(service, 2, {{"items", QVariantList{Fixture::item("old-result")}}});
        fixture.complete(service, 3, {{"items", QVariantList{Fixture::item("new-result")}}});
        QTRY_COMPARE(items(service, view).size(), 1);
        QCOMPARE(service.view(view).value("query").toString(), QStringLiteral("latest query"));
        service.closeView(view); QVERIFY(service.view(view).isEmpty());
        QVERIFY(!service.browse(view, node.value("ref").toString()));
    }
    void missingPlayableAdapterDeniesBeforeResolve() {
        Fixture fixture; AddonDiscoveryService service(fixture.backend(), {});
        const auto view = service.openSource("org.example.radio/source"); QTRY_COMPARE(fixture.calls.size(), 1);
        fixture.complete(service, 0, {{"items", QVariantList{Fixture::item("station")}}}); QTRY_COMPARE(items(service, view).size(), 1);
        QVERIFY(!service.canPlay(view)); QVERIFY(!service.canEnqueue(view));
        QVERIFY(service.play(view, items(service, view)[0].toMap().value("ref").toString()).isEmpty());
        QCOMPARE(fixture.calls.size(), 1);
    }
    void queueReceivesOneReferenceBatchWithoutUrls() {
        Fixture fixture; QVariantList applied;
        AddonQueueTransactions::Backend queues;
        queues.snapshot = [](const QString& target) { return AddonQueueTransactions::Snapshot{target == QLatin1String("queue"), "queue-1", 1}; };
        queues.append = [&](const auto& transaction, auto done) { applied = transaction.references; done(true, {}); return []{}; };
        AddonDiscoveryService service(fixture.backend(), queues);
        const auto view = service.openSource("org.example.radio/source"); QTRY_COMPARE(fixture.calls.size(), 1);
        fixture.complete(service, 0, {{"items", QVariantList{Fixture::item("station")}}}); QTRY_COMPARE(items(service, view).size(), 1);
        const auto ref = items(service, view)[0].toMap().value("ref").toString();
        QSignalSpy finished(&service, &AddonDiscoveryService::actionFinished);
        QVERIFY(!service.enqueue(view, {ref}).isEmpty()); QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(applied.size(), 1); QVERIFY(!applied[0].toMap().contains("url")); QVERIFY(!applied[0].toMap().contains("options"));
        QCOMPARE(applied[0].toMap().value("itemId").toString(), QStringLiteral("station"));
        QVERIFY(service.enqueue(view, {ref, ref}).isEmpty());
        QVERIFY(service.enqueue(view, {QStringLiteral("station")}).isEmpty());
    }
    void queueChangeDuringResolveReleasesHandle() {
        Fixture fixture; quint64 revision = 1; int played = 0;
        AddonQueueTransactions::Backend queues;
        queues.snapshot = [&](const QString&) { return AddonQueueTransactions::Snapshot{true, "queue-1", revision}; };
        queues.play = [&](const auto&, auto done) { ++played; done(true, {}); return []{}; };
        queues.releasePlayable = [&](const QString& handle) { fixture.released.append(handle); };
        AddonDiscoveryService service(fixture.backend(true), queues);
        const auto view = service.openSource("org.example.radio/source"); QTRY_COMPARE(fixture.calls.size(), 1);
        fixture.complete(service, 0, {{"items", QVariantList{Fixture::item("station")}}}); QTRY_COMPARE(items(service, view).size(), 1);
        QSignalSpy finished(&service, &AddonDiscoveryService::actionFinished);
        QVERIFY(service.canPlay(view));
        QVERIFY(!service.play(view, items(service, view)[0].toMap().value("ref").toString()).isEmpty());
        QTRY_COMPARE(fixture.calls.size(), 2); ++revision;
        fixture.complete(service, 1, {{"url", "https://example.org/stream"}});
        QTRY_COMPARE(finished.size(), 1); QVERIFY(!finished.last()[1].toBool()); QCOMPARE(played, 0);
        QCOMPARE(fixture.released, QStringList{QStringLiteral("playable:42")});
    }
    void latestPlayCancelsAnEarlierResolve() {
        Fixture fixture; QString played;
        AddonQueueTransactions::Backend queues;
        queues.snapshot = [](const QString&) { return AddonQueueTransactions::Snapshot{true, "queue-1", 1}; };
        queues.play = [&](const auto& transaction, auto done) { played = transaction.references[0].toMap().value("itemId").toString(); done(true, {}); return []{}; };
        AddonDiscoveryService service(fixture.backend(true), queues);
        const auto view = service.openSource("org.example.radio/source"); QTRY_COMPARE(fixture.calls.size(), 1);
        fixture.complete(service, 0, {{"items", QVariantList{Fixture::item("first"), Fixture::item("second")}}}); QTRY_COMPARE(items(service, view).size(), 2);
        QVERIFY(!service.play(view, items(service, view)[0].toMap().value("ref").toString()).isEmpty()); QTRY_COMPARE(fixture.calls.size(), 2);
        QVERIFY(!service.play(view, items(service, view)[1].toMap().value("ref").toString()).isEmpty()); QTRY_COMPARE(fixture.calls.size(), 3);
        QVERIFY(fixture.canceled.contains("2"));
        fixture.complete(service, 1, {{"url", "https://example.org/first"}});
        fixture.complete(service, 2, {{"url", "https://example.org/second"}});
        QTRY_COMPARE(played, QStringLiteral("second"));
    }
    void rootPagesAppendWithoutExposingTheProviderCursor() {
        Fixture fixture; AddonDiscoveryService service(fixture.backend(), {});
        const auto view = service.openSource("org.example.radio/source"); QTRY_COMPARE(fixture.calls.size(), 1);
        fixture.complete(service, 0, {{"items", QVariantList{Fixture::item("first")}}, {"cursor", QVariantMap{{"offset", 2}}}});
        QTRY_COMPARE(items(service, view).size(), 1); QVERIFY(service.view(view).value("hasMore").toBool());
        QVERIFY(!service.view(view).contains("cursor")); QVERIFY(service.loadMore(view)); QTRY_COMPARE(fixture.calls.size(), 2);
        QCOMPARE(fixture.calls.last().toMap().value("operation").toString(), QStringLiteral("roots"));
        QCOMPARE(fixture.calls.last().toMap().value("context").toMap().value("cursor").toMap().value("offset").toInt(), 2);
        fixture.complete(service, 1, {{"items", QVariantList{Fixture::item("second")}}});
        QTRY_COMPARE(items(service, view).size(), 2); QVERIFY(!service.view(view).value("hasMore").toBool());
    }
};
QTEST_GUILESS_MAIN(TestAddonDiscoveryService)
#include "test_addon_discovery_service.moc"
