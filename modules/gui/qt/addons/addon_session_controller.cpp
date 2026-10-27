// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_session_controller.hpp"
#include <QThread>
#include <chrono>

namespace vlc::addons {
namespace { qint64 nowUs() { return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); } }
AddonSessionController::AddonSessionController(AddonSessionHost* host,Backend backend,QObject* parent)
    :QObject(parent),m_host(host),m_lifetime(backend.lifetime),m_backend(std::move(backend)) {
    m_closed=!host || !m_lifetime || !m_backend.invoke || !m_backend.cancel;
    if(host) {
        connect(host,&AddonSessionHost::sessionChanged,this,[this](const QString& id,const QString& status) { changed(id,status); });
        connect(host,&QObject::destroyed,this,&AddonSessionController::close);
    }
    if(m_lifetime) connect(m_lifetime,&QObject::destroyed,this,&AddonSessionController::close);
}
AddonSessionController::~AddonSessionController() { close(); }
void AddonSessionController::changed(const QString& id,const QString& status) {
    if(status==QLatin1String("ended")) { stop(id,false); return; }
    if(m_closed || !m_host || status!=QLatin1String("active") || m_loops.contains(id) || m_loops.size()>=8) return;
    const auto context=m_host->invocationContext(id); const auto contribution=context.value("contribution").toString();
    if(contribution.isEmpty()) { m_host->end(id); return; }
    Loop loop; loop.contribution=contribution;
    auto* timer=new QTimer(this); timer->setSingleShot(true); loop.timer=timer;
    connect(timer,&QTimer::timeout,this,[this,id] { begin(id); });
    auto* deadline=new QTimer(this); deadline->setSingleShot(true); deadline->setTimerType(Qt::PreciseTimer); loop.deadline=deadline;
    connect(deadline,&QTimer::timeout,this,[this,id] { stop(id,true); });
    m_loops.insert(id,std::move(loop)); timer->start(0);
}
void AddonSessionController::begin(const QString& id) {
    auto it=m_loops.find(id);
    if(m_closed || !m_host || !m_lifetime || it==m_loops.end() || it->checking || !it->request.isEmpty()) return;
    if(!m_host->current(id)) { stop(id,true); return; }
    it->checking=true; const QPointer<AddonSessionController> owner(this);
    if(!m_host->checkForInvocation(id,[owner,id](bool allowed) { if(owner) owner->checked(id,allowed); })) stop(id,true);
}
void AddonSessionController::checked(const QString& id,bool allowed) {
    auto it=m_loops.find(id); if(it==m_loops.end()) return;
    it->checking=false;
    if(!allowed || m_closed || !m_host || !m_lifetime || !m_host->current(id)) { stop(id,true); return; }
    const auto context=m_host->invocationContext(id);
    if(context.value("contribution").toString()!=it->contribution || !context.value("context").canConvert<QVariantMap>()) { stop(id,true); return; }
    const auto invoke=m_backend.invoke; const auto cancel=m_backend.cancel; const auto lifetime=m_lifetime;
    const auto contribution=it->contribution; const QPointer<AddonSessionController> alive(this);
    it->expires=nowUs()+25000000; it->deadline->start(25000);
    m_creating=id;
    QString request; try { request=invoke(contribution,QStringLiteral("run"),context.value("context").toMap()); } catch(...) {}
    if(!alive) { if(lifetime && cancel && !request.isEmpty()) cancel(request); return; }
    m_creating.clear();
    it=m_loops.find(id);
    if(it==m_loops.end() || m_closed || !m_host || !m_host->current(id) || nowUs()>=it->expires) {
        if(it!=m_loops.end()) stop(id,true);
        if(lifetime && cancel && !request.isEmpty()) cancel(request); return;
    }
    if(request.isEmpty() || request.size()>128 || m_requests.contains(request)) {
        stop(id,true);
        if(lifetime && cancel && !request.isEmpty()) cancel(request);
        return;
    }
    if(!m_host->bindInvocation(id,request,it->expires)) {
        stop(id,true); if(lifetime && cancel) cancel(request); return;
    }
    it->request=request; m_requests.insert(request,id);
    const auto early=m_early.find(request);
    if(early!=m_early.end()) {
        const auto result=early.value(); m_early.clear();
        completed(request,result.contribution,result.status,result.data,result.error); return;
    }
    m_early.clear();
}
void AddonSessionController::completed(const QString& request,const QString& contribution,const QString& status,const QVariantMap& data,const QString& error) {
    if(m_closed) return;
    const auto found=m_requests.find(request);
    if(found==m_requests.end()) {
        if(!m_creating.isEmpty() && request.size()<=128 && m_early.isEmpty()) m_early.insert(request,{contribution,status,error.left(512),data});
        return;
    }
    const auto id=found.value(); m_requests.erase(found); auto it=m_loops.find(id); if(it==m_loops.end()) return;
    it->request.clear(); if(it->deadline) it->deadline->stop();
    const QPointer<AddonSessionController> alive(this);
    const bool released=m_host && m_host->releaseInvocation(id,request);
    if(!alive) return;
    it=m_loops.find(id); if(it==m_loops.end()) return;
    const auto keep=data.value(QStringLiteral("continue"));
    if(contribution!=it->contribution || (status!=QLatin1String("match") && status!=QLatin1String("partial")) || !error.isEmpty()
        || !released || nowUs()>=it->expires || data.size()!=1 || keep.metaType().id()!=QMetaType::Bool || !keep.toBool() || !m_host || !m_host->current(id)) { stop(id,true); return; }
    it->timer->start(500);
}
void AddonSessionController::stop(const QString& id,bool end) {
    auto it=m_loops.find(id);
    QString request;
    if(it!=m_loops.end()) {
        request=it->request;
        if(it->timer) { it->timer->stop(); it->timer->deleteLater(); }
        if(it->deadline) { it->deadline->stop(); it->deadline->deleteLater(); }
        m_requests.remove(request); m_loops.erase(it);
    }
    const auto cancel=m_backend.cancel; const auto lifetime=m_lifetime;
    const auto host=m_host; const QPointer<AddonSessionController> alive(this);
    if(host && !request.isEmpty()) host->releaseInvocation(id,request);
    if(end && host) host->end(id);
    if(lifetime && cancel && !request.isEmpty()) cancel(request);
    if(!alive) return;
}
void AddonSessionController::close() {
    if(m_closed && m_loops.isEmpty()) return;
    m_closed=true; const auto ids=m_loops.keys(); const QPointer<AddonSessionController> alive(this);
    for(const auto& id:ids) { stop(id,true); if(!alive) return; }
    m_early.clear();
}
}
