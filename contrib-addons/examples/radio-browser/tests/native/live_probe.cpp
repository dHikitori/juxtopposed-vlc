// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <QScopeGuard>
#include <atomic>
#include <cstdio>
#include <memory>
#include <vlc/vlc.h>
#include <vlc_playlist.h>
#include <vlc_player.h>
#include <vlc_input_item.h>
#include "lib/libvlc_internal.h"
#include "addons/addon_manager.hpp"
#include "addons/addon_discovery_source_owner.hpp"
#include "addons/addon_discovery_service.hpp"
#include "addons/addon_native_queue_store.hpp"
#include "addons/addon_player_discovery.hpp"
#include "addons/addon_source_admissions.hpp"
using namespace vlc::addons;
namespace {
void phase(const char* value) { std::fprintf(stderr,"probe-phase: %s\n",value); std::fflush(stderr); }
bool wait(const std::function<bool()>& test,int milliseconds) {
    QElapsedTimer elapsed; elapsed.start();
    while(!test() && elapsed.elapsed()<milliseconds) { QCoreApplication::processEvents(QEventLoop::AllEvents,10); QThread::msleep(1); }
    return test();
}
struct Lock { vlc_playlist_t* value; explicit Lock(vlc_playlist_t* p):value(p) { vlc_playlist_Lock(value); } ~Lock() { vlc_playlist_Unlock(value); } };
bool run(libvlc_instance_t* core,const QString& package,const QString& worker,QJsonObject& proof) {
    QTemporaryDir storage; AddonStreamLifetime lifetime;
    AddonManager manager(storage.path()); manager.setWorkerExecutable(worker);
    const QString packageId=QStringLiteral("org.hakone.radio-browser"),source=packageId+QStringLiteral("/stations");
    if(!manager.installLocalPackage(package,true) || !manager.setEnabled(packageId,true)) { proof["error"]=manager.errorMessage(); return false; }
    const QPointer<AddonManager> alive(&manager);
    auto admissions=std::make_shared<AddonSourceAdmissions>(storage.path(),[alive](const auto& reference) { return discoveryReferenceAvailable(alive,reference); });
    const auto playlist=std::unique_ptr<vlc_playlist_t,decltype(&vlc_playlist_Delete)>(vlc_playlist_New(VLC_OBJECT(core->p_libvlc_int),VLC_PLAYLIST_PREPARSING_DISABLED,1,0),vlc_playlist_Delete);
    if(!playlist) { proof["error"]="The native queue did not start."; return false; }
    AddonNativeQueueStore queue(playlist.get(),admissions);
    AddonDiscoverySourceOwner owner(&manager,VLC_OBJECT(core->p_libvlc_int),admissions,lifetime.handle());
    const auto close=qScopeGuard([&] {
        phase("owner-retirement");
        manager.setNativeStreamAvailable(false); owner.close(); queue.close();
        { Lock lock(playlist.get()); vlc_playlist_Stop(playlist.get()); }
        lifetime.close(); if(!lifetime.handle().waitQt(std::chrono::seconds(15))) qFatal("The native source work did not end.");
        phase("owner-work-joined");
    });
    if(!owner.activate() || !wait([&] { return owner.active(); },5000)) { proof["error"]=owner.errorMessage(); return false; }
    manager.setNativeStreamAvailable(true);
    const auto review=owner.requestApproval(source);
    if(review.isEmpty() || !wait([&] { return !owner.approval(review).isEmpty(); },5000) || !owner.approve(review)
        || !wait([&] { return owner.canUseSource(source); },5000)) { proof["error"]="The temporary source approval did not start."; return false; }
    QJsonArray operations; QObject operationObserver;
    QObject::connect(&manager,&AddonManager::operationFinished,&operationObserver,[&](const auto&,const auto& contribution,const auto& status,const QVariantMap& data,const auto& error) {
        if(operations.size()<32) operations.append(QJsonObject{{"source",contribution},{"status",status},{"error",error},
            {"items",data.value("items").toList().size()},{"keys",QJsonArray::fromStringList(data.keys())}});
    });
    const auto exportOperations=qScopeGuard([&] { proof["operations"]=operations; });
    const auto service=std::unique_ptr<AddonDiscoveryService>(createPlayerDiscoveryWithSources(&manager,&owner,queue.backend(),nullptr));
    proof["sources"]=QJsonArray::fromVariantList(service->sources());
    const auto view=service->openSource(source);
    if(view.isEmpty() || !wait([&] { const auto current=service->view(view);
        return !current.value("busy").toBool() && (!current.value("items").toList().isEmpty() || !current.value("error").toString().isEmpty()); },15000)) { proof["error"]="The source roots did not arrive."; return false; }
    proof["rootView"]=QJsonObject::fromVariantMap(service->view(view));
    auto items=service->view(view).value("items").toList(); proof["rootCount"]=items.size();
    if(items.isEmpty() || !service->browse(view,items.first().toMap().value("ref").toString())
        || !wait([&] { return !service->view(view).value("busy").toBool(); },15000)) { proof["error"]="The station page did not arrive."; return false; }
    proof["stationView"]=QJsonObject::fromVariantMap(service->view(view));
    items=service->view(view).value("items").toList(); proof["stationCount"]=items.size();
    if(items.isEmpty()) { proof["error"]=service->view(view).value("error").toString(); return false; }
    struct Stats { std::atomic<quint64> decoded{0}; std::atomic_uint stopped{0}; } stats;
    static const vlc_player_cbs callbacks=[] { vlc_player_cbs c{};
        c.on_statistics_changed=[](vlc_player_t*,const input_stats_t* s,void* raw) { static_cast<Stats*>(raw)->decoded=s->i_decoded_audio; };
        c.on_state_changed=[](vlc_player_t*,vlc_player_state s,void* raw) { if(s==VLC_PLAYER_STATE_STOPPED) ++static_cast<Stats*>(raw)->stopped; }; return c; }();
    auto* player=vlc_playlist_GetPlayer(playlist.get()); vlc_player_Lock(player);
    auto* listener=vlc_player_AddListener(player,&callbacks,&stats); vlc_player_Unlock(player);
    if(!listener) { proof["error"]="The native observer did not start."; return false; }
    const auto stop=qScopeGuard([&] { vlc_player_Lock(player); vlc_player_Stop(player); vlc_player_RemoveListener(player,listener); vlc_player_Unlock(player); });
    unsigned resolves=0; QObject receiver;
    QObject::connect(&manager,&AddonManager::operationFinished,&receiver,[&](const auto&,const auto&,const auto&,const QVariantMap& data,const auto&) {
        if(data.contains("url")) ++resolves;
    });
    QJsonArray attempts;
    for(int index=0;index<items.size() && index<3;++index) {
        const auto item=items.at(index).toMap(); const auto ref=item.value("ref").toString();
        if(!service->canEnqueue(view) || service->enqueue(view,{ref}).isEmpty()) continue;
        quint64 id=0; QString uri; size_t count=0;
        { Lock lock(playlist.get()); count=vlc_playlist_Count(playlist.get());
            if(!count) continue; auto* entry=vlc_playlist_Get(playlist.get(),count-1); id=vlc_playlist_item_GetId(entry);
            char* raw=input_item_GetURI(vlc_playlist_item_GetMedia(entry)); uri=QString::fromUtf8(raw); free(raw); }
        if(!uri.startsWith(QStringLiteral("hakone-addon-source:v1:"))) { proof["error"]="The queue did not store a stable reference."; return false; }
        if(index==0) { proof["resolvedBeforePlay"]=int(resolves); if(resolves) return false; }
        stats.decoded=0;
        phase("native-play-start");
        const auto action=service->play(view,ref); const bool decoded=!action.isEmpty() && wait([&] { return stats.decoded.load()>0; },15000);
        phase(decoded ? "native-audio-decoded" : "native-play-timeout");
        const auto saved=queue.queuedReference(id); QJsonObject attempt{{"stationId",saved.value("itemId").toString()},
            {"title",item.value("title").toString()},{"decodedAudio",qint64(stats.decoded.load())},{"decoded",decoded}};
        { Lock lock(playlist.get()); attempt["queueCountPreserved"]=vlc_playlist_Count(playlist.get())==count; vlc_playlist_Stop(playlist.get()); }
        phase("native-stop-sent");
        attempts.append(attempt); wait([&] { return stats.stopped.load()>0; },5000);
        phase("native-stop-wait-ended");
        if(decoded) { proof["attempts"]=attempts; proof["resolveResults"]=int(resolves); proof["passed"]=true; return true; }
    }
    proof["attempts"]=attempts; proof["resolveResults"]=int(resolves); proof["error"]=owner.errorMessage(); return false;
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    if(argc!=4) return 2;
    const char* options[]={"--ignore-config","--quiet","--no-plugins-cache","--no-media-library","--no-metadata-network-access","--aout=adummy","--vout=vdummy","--text-renderer=tdummy"};
    auto* core=libvlc_new(int(std::size(options)),options); QJsonObject proof;
    const bool passed=core && run(core,QString::fromLocal8Bit(argv[1]),QString::fromLocal8Bit(argv[2]),proof);
    phase("native-probe-returned");
    if(core) libvlc_release(core);
    phase("native-core-released");
    proof["passed"]=passed; proof["profile"]="public-https-media@1"; proof["readOnly"]=true;
    QFile file(QString::fromLocal8Bit(argv[3])); if(!file.open(QIODevice::WriteOnly)) return 3;
    file.write(QJsonDocument(proof).toJson()); return passed?0:1;
}
