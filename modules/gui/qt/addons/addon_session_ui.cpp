// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_session_ui.hpp"
#include "addon_session_host.hpp"
#include "addon_manifest.hpp"
#include <QThread>
#include <QSet>
#include <chrono>
#include <limits>

namespace vlc::addons {
namespace {
qint64 nowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
bool bounded(const QString& text,int maximum,bool empty=false) {
    if((!empty && text.isEmpty()) || text.size()>maximum) return false;
    for(const auto c:text) if(c.category()==QChar::Other_Control) return false;
    return true;
}
QString label(QString text) {
    text=text.left(256); for(qsizetype i=0;i<text.size();++i) if(text.at(i).category()==QChar::Other_Control) text[i]=QLatin1Char(' '); return text;
}
const QStringList controls{QStringLiteral("play"),QStringLiteral("pause"),QStringLiteral("seek"),QStringLiteral("setRate")};
QStringList subset(const QVariant& value,const QStringList& allowed,int limit=4) {
    QStringList result;
    for(const auto& entry:value.toStringList()) if(result.size()<limit && allowed.contains(entry) && !result.contains(entry)) result.append(entry);
    return result;
}
AddonSessionUi::Backend hostBackend(AddonSessionHost* host,AddonSessionUi::Catalog catalog) {
    const QPointer<AddonSessionHost> owner(host); AddonSessionUi::Backend result; result.lifetime=host; result.catalog=std::move(catalog);
    result.prepare=[owner](const auto& id,const auto& relay,const auto& room,const auto& operations) { return owner ? owner->prepareApproval(id,relay,room,operations) : QString{}; };
    result.review=[owner](const auto& ticket) { return owner ? owner->approval(ticket) : QVariantMap{}; };
    result.accept=[owner](const auto& ticket,const auto& operations) { return owner ? owner->approve(ticket,operations) : QString{}; };
    result.cancel=[owner](const auto& ticket) { if(owner) owner->cancelApproval(ticket); };
    result.resume=[owner](const auto& id) { return owner && owner->resume(id); };
    result.end=[owner](const auto& id) { if(owner) owner->end(id); };
    result.sessions=[owner] { return owner ? owner->sessions() : QVariantList{}; }; return result;
}
}
AddonSessionUi::AddonSessionUi(Backend backend,QObject* parent)
    :QObject(parent),m_backend(std::move(backend)),m_owner(m_backend.lifetime),m_catalogOwner(m_backend.catalog.lifetime) {
    m_closed=!m_owner || !m_catalogOwner || !m_backend.catalog.read || !m_backend.prepare || !m_backend.review
        || !m_backend.accept || !m_backend.cancel || !m_backend.resume || !m_backend.end || !m_backend.sessions;
    if(m_owner) connect(m_owner,&QObject::destroyed,this,&AddonSessionUi::close);
    if(m_catalogOwner) connect(m_catalogOwner,&QObject::destroyed,this,&AddonSessionUi::close);
    m_timer.setInterval(250); connect(&m_timer,&QTimer::timeout,this,[this] {
        if(m_ticket.isEmpty()) return;
        if(nowMs()>=m_expires) { const QPointer<AddonSessionUi> alive(this); clearReview(); if(alive) fail(tr("The review expired. Open it again.")); }
        else if(!m_preparing) {
            const auto read=m_backend.review; const auto ticket=m_ticket; const auto generation=m_generation; const QPointer<AddonSessionUi> alive(this);
            const auto current=read(ticket); if(!alive || m_closed || !m_owner || !m_catalogOwner || ticket!=m_ticket || generation!=m_generation) return;
            if(current.isEmpty() || current!=m_review) { clearReview(); if(alive) fail(tr("The add-on or playback changed. Open the review again.")); }
        }
    });
    refresh();
}
AddonSessionUi::AddonSessionUi(AddonSessionHost* host,Catalog catalog,QObject* parent)
    :AddonSessionUi(hostBackend(host,std::move(catalog)),parent) {
    if(host) { connect(host,&AddonSessionHost::approvalReady,this,&AddonSessionUi::approvalReady);
        connect(host,&AddonSessionHost::sessionChanged,this,&AddonSessionUi::sessionChanged); }
}
AddonSessionUi::~AddonSessionUi() { m_destroying=true; close(); }
void AddonSessionUi::publish() { ++m_revision; emit changed(); }
bool AddonSessionUi::fail(const QString& error) { m_error=error; m_status.clear(); publish(); return false; }
bool AddonSessionUi::currentDisplay(const QString& controller) {
    if(!m_backend.catalog.attached) return true;
    auto read=m_backend.catalog.state; auto rowsRead=m_backend.catalog.read; const QPointer<AddonSessionUi> alive(this);
    const auto state=read ? read() : QVariantMap{}; read={};
    if(!alive || m_closed || !m_displayActive || !state.value("current").toBool() || !state.value("ready").toBool()) return false;
    const auto rows=rowsRead ? rowsRead() : QVariantList{}; rowsRead={};
    if(!alive || m_closed || !m_displayActive) return false;
    for(const auto& value:rows) { const auto row=value.toMap(); if(row.value("id").toString()==controller) return row.value("ready").toBool(); }
    return false;
}
void AddonSessionUi::clearReview() {
    m_timer.stop();
    ++m_generation;
    QString ticket; ticket.swap(m_ticket); m_reviewController.clear(); m_review.clear(); m_preparing=false; m_expires=0; m_earlyReady.clear();
    const auto cancel=m_backend.cancel; const auto owner=m_owner; if(owner && cancel && !ticket.isEmpty()) cancel(ticket);
}
void AddonSessionUi::refresh() {
    Q_ASSERT(QThread::currentThread()==thread());
    if(m_closed || !m_owner || !m_catalogOwner) return;
    if(m_refreshEpoch==std::numeric_limits<quint64>::max()) { close(); return; }
    const auto epoch=++m_refreshEpoch;
    auto read=m_backend.catalog.read; auto stateRead=m_backend.catalog.state; auto sessions=m_backend.sessions;
    const QPointer<AddonSessionUi> alive(this);
    const auto state=stateRead ? stateRead() : QVariantMap{}; stateRead={};
    if(!alive || m_closed || epoch!=m_refreshEpoch || !m_owner || !m_catalogOwner) return;
    const bool attached=m_backend.catalog.attached;
    const bool current=!attached || (m_displayActive && state.value("current").toBool());
    const auto rows=read(); read={};
    if(!alive || m_closed || epoch!=m_refreshEpoch || !m_owner || !m_catalogOwner) return;
    QVariantList controllers; QSet<QString> ids;
    for(const auto& value:rows) {
        const auto row=value.toMap(); const auto id=row.value("id").toString();
        if(controllers.size()>=32) break;
        if(!bounded(id,256) || ids.contains(id)) continue;
        if(!attached && (!row.value("enabled").toBool() || !row.value("configured").toBool())) continue;
        QStringList origins;
        for(const auto& origin:row.value("origins").toStringList()) if(origins.size()<32 && !exactWssOrigin(origin).isEmpty() && !origins.contains(origin)) origins.append(origin);
        if(origins.isEmpty() && !attached) continue;
        ids.insert(id); controllers.append(QVariantMap{{"id",id},{"name",label(row.value("name").toString())},
            {"origins",origins},{"operations",subset(row.value("operations"),controls)},
            {"ready",!attached || (current && state.value("ready").toBool() && row.value("ready").toBool() && !origins.isEmpty())},
            {"state",label(row.value("state").toString())}});
    }
    const auto active=sessions(); sessions={};
    if(!alive || m_closed || epoch!=m_refreshEpoch || !m_owner || !m_catalogOwner) return;
    QVariantList visible;
    for(const auto& value:active) {
        if(visible.size()>=8) break; auto row=value.toMap();
        if(!bounded(row.value("id").toString(),128)) continue;
        for(const auto& key:QStringList{QStringLiteral("name"),QStringLiteral("room"),QStringLiteral("relay")}) row.insert(key,label(row.value(key).toString()));
        visible.append(row);
    }
    m_controllers=std::move(controllers); m_sessions=std::move(visible);
    m_displayCurrent=current; m_displayLoading=attached && state.value("loading").toBool();
    m_displayError=attached ? label(state.value("error").toString()) : QString{};
    if(!current && !m_ticket.isEmpty()) { clearReview(); if(!alive || m_closed || epoch!=m_refreshEpoch) return; }
    if(!m_ticket.isEmpty() && !m_preparing) {
        const auto read=m_backend.review; const auto ticket=m_ticket; const auto generation=m_generation;
        const auto current=read(ticket);
        if(!alive || m_closed || epoch!=m_refreshEpoch || !m_owner || !m_catalogOwner || ticket!=m_ticket || generation!=m_generation) return;
        if(current.isEmpty() || current!=m_review) { clearReview(); if(!alive) return; fail(tr("The add-on or playback changed. Open the review again.")); return; }
    }
    if(!m_starting.isEmpty()) {
        for(const auto& value:m_sessions) if(value.toMap().value("id").toString()==m_starting && !value.toMap().value("starting").toBool()) {
            m_starting.clear(); m_status=tr("The session is active."); break;
        }
    }
    publish();
}
bool AddonSessionUi::prepare(const QString& id,const QString& relay,const QString& room) {
    if(m_closed || !m_owner || !m_catalogOwner || !bounded(relay,2048) || !bounded(room,256)) return fail(tr("Select a controller. Enter a relay address and room name."));
    const QPointer<AddonSessionUi> alive(this); clearReview(); if(!alive) return false;
    const auto generation=m_generation;
    refresh(); if(!alive) return false;
    if(m_closed || m_generation!=generation) return false;
    const bool display=currentDisplay(id); if(!alive || m_closed || m_generation!=generation) return false;
    if(!display) return fail(tr("The controller list changed. Wait for it to finish or try again."));
    QVariantMap selected; for(const auto& value:m_controllers) if(value.toMap().value("id").toString()==id) selected=value.toMap();
    const auto url=QUrl(relay,QUrl::StrictMode); auto origin=url; origin.setPath(QString{}); origin.setQuery(QString{}); origin.setFragment(QString{}); if(origin.port()==443) origin.setPort(-1);
    if(selected.isEmpty() || !selected.value("ready").toBool() || url.scheme()!=QLatin1String("wss") || !selected.value("origins").toStringList().contains(origin.toString(QUrl::FullyEncoded))) return fail(tr("This controller does not permit that relay."));
    m_error.clear(); m_status.clear(); m_reviewController=id; m_preparing=true; m_creating=true; m_expires=nowMs()+30000;
    const auto prepare=m_backend.prepare; const auto cancel=m_backend.cancel; const auto owner=m_owner;
    const auto ticket=prepare(id,url,room,selected.value("operations").toStringList());
    if(!alive) { if(owner && cancel && !ticket.isEmpty()) cancel(ticket); return false; }
    m_creating=false; if(m_closed || !m_owner || m_generation!=generation) { if(owner && cancel && !ticket.isEmpty()) cancel(ticket); return false; }
    if(ticket.isEmpty() || ticket.size()>128) { m_preparing=false; return fail(tr("The app cannot prepare this session.")); }
    m_ticket=ticket; m_timer.start(); const auto early=m_earlyReady; m_earlyReady.clear();
    if(early==ticket) approvalReady(ticket); else publish(); return true;
}
void AddonSessionUi::approvalReady(const QString& ticket) {
    if(m_closed) return;
    if(m_creating && m_ticket.isEmpty()) { if(m_earlyReady.isEmpty()) m_earlyReady=ticket.left(128); return; }
    if(ticket!=m_ticket || ticket.isEmpty()) return;
    const auto read=m_backend.review; const auto generation=m_generation; const QPointer<AddonSessionUi> alive(this); const auto review=read(ticket);
    if(!alive || m_closed || !m_owner || !m_catalogOwner || ticket!=m_ticket || generation!=m_generation) return;
    m_preparing=false;
    if(review.isEmpty() || nowMs()>=m_expires) { clearReview(); if(alive) fail(tr("The app refused this review. Open it again.")); return; }
    m_review=review; publish();
}
bool AddonSessionUi::accept(const QStringList& operations) {
    if(m_closed || m_preparing || m_ticket.isEmpty() || m_review.isEmpty() || nowMs()>=m_expires || operations.size()>4) return fail(tr("Open a current session review first."));
    QSet<QString> unique; const auto allowed=m_review.value("operations").toStringList();
    for(const auto& operation:operations) if(!controls.contains(operation) || !allowed.contains(operation) || unique.contains(operation)) return fail(tr("Select only the playback controls in this review.")); else unique.insert(operation);
    const QPointer<AddonSessionUi> alive(this); const auto read=m_backend.review; const auto ticket=m_ticket; const auto generation=m_generation;
    const auto displayed=m_review; const auto controller=m_reviewController;
    const auto current=read(ticket);
    if(!alive || m_closed || !m_owner || !m_catalogOwner || ticket!=m_ticket || generation!=m_generation || displayed!=m_review) return false;
    if(current.isEmpty() || current!=displayed) { clearReview(); if(alive) fail(tr("The review changed. Open it again.")); return false; }
    const bool display=currentDisplay(controller);
    if(!alive || m_closed || ticket!=m_ticket || generation!=m_generation || displayed!=m_review) return false;
    if(!display) { clearReview(); if(alive) fail(tr("The controller list changed. Open a new review.")); return false; }
    m_timer.stop(); m_ticket.clear(); m_reviewController.clear(); m_review.clear(); m_preparing=false; m_expires=0; m_error.clear();
    m_accepting=true; m_earlySessions.clear();
    const auto accept=m_backend.accept; const auto result=accept(ticket,operations); if(!alive) return false;
    m_accepting=false;
    if(m_closed) return !result.isEmpty();
    if(result.isEmpty()) return fail(tr("The app refused this session. Open a new review."));
    if(m_earlySessions.value(result)==QLatin1String("ended")) { m_earlySessions.clear(); return fail(tr("Playback or the add-on changed. Review this session again.")); }
    m_earlySessions.clear(); m_starting=result; m_status=tr("The app checks playback before the session starts."); refresh(); return true;
}
void AddonSessionUi::cancelReview() { const QPointer<AddonSessionUi> alive(this); clearReview(); if(alive) { m_error.clear(); publish(); } }
void AddonSessionUi::viewClosed() { setDisplayActive(false); }
void AddonSessionUi::setDisplayActive(bool active) {
    if(m_closed) return;
    if(active && m_displayActive) return;
    if(m_displayEpoch==std::numeric_limits<quint64>::max()) { close(); return; }
    const auto epoch=++m_displayEpoch;
    const QPointer<AddonSessionUi> alive(this);
    m_displayActive=active;
    if(!active) {
        clearReview();
        if(!alive || m_closed || epoch!=m_displayEpoch || m_displayActive!=active) return;
    }
    auto setActive=m_backend.catalog.setActive;
    if(setActive) setActive(active); setActive={};
    if(!alive || m_closed || epoch!=m_displayEpoch || m_displayActive!=active) return;
    m_error.clear(); refresh();
}
void AddonSessionUi::retryDisplay() {
    if(m_closed || !m_displayActive || !m_backend.catalog.attached) return;
    const auto epoch=m_displayEpoch;
    const QPointer<AddonSessionUi> alive(this); clearReview();
    if(!alive || m_closed || epoch!=m_displayEpoch || !m_displayActive) return;
    auto retry=m_backend.catalog.retry; if(retry) retry(); retry={};
    if(alive && !m_closed && epoch==m_displayEpoch && m_displayActive) refresh();
}
bool AddonSessionUi::resume(const QString& id) {
    if(m_closed || !m_owner) return false;
    bool allowed=false; for(const auto& value:m_sessions) { const auto row=value.toMap(); if(row.value("id").toString()==id && row.value("suspended").toBool() && row.value("connected").toBool()) allowed=true; }
    if(!allowed) return fail(tr("This session cannot resume playback control."));
    const auto resume=m_backend.resume; const QPointer<AddonSessionUi> alive(this); const bool accepted=resume(id); if(!alive) return false;
    if(!accepted) return fail(tr("The app refused to resume this session.")); m_error.clear(); m_status=tr("The app checks playback before control resumes."); publish(); return true;
}
bool AddonSessionUi::end(const QString& id) {
    if(m_closed || !m_owner) return false;
    bool found=false; for(const auto& value:m_sessions) if(value.toMap().value("id").toString()==id) found=true;
    if(!found) return false;
    const auto end=m_backend.end; const QPointer<AddonSessionUi> alive(this); end(id); if(!alive) return false;
    m_error.clear(); m_status=tr("The session ended."); refresh(); return true;
}
void AddonSessionUi::sessionChanged(const QString& id,const QString& state) {
    if(m_closed) return;
    if(m_accepting && m_earlySessions.size()<8) m_earlySessions.insert(id,state);
    if(id==m_starting && state==QLatin1String("ended")) { m_starting.clear(); m_error=tr("Playback or the add-on changed. Review this session again."); m_status.clear(); }
    refresh();
}
void AddonSessionUi::close() {
    if(m_closed) return;
    m_closed=true; m_displayActive=false; m_displayCurrent=false; m_displayLoading=false; m_timer.stop();
    ++m_generation;
    QString ticket; ticket.swap(m_ticket); m_reviewController.clear(); m_review.clear(); m_preparing=false; m_expires=0; m_earlyReady.clear();
    m_controllers.clear(); m_sessions.clear(); m_starting.clear(); m_status.clear();
    auto setActive=m_backend.catalog.setActive; auto cancel=m_backend.cancel; const auto owner=m_owner;
    const QPointer<AddonSessionUi> alive(this);
    if(setActive) setActive(false); setActive={};
    // Both detached obligations must finish if either callback deletes us.
    if(owner && cancel && !ticket.isEmpty()) cancel(ticket); cancel={};
    if(alive && !m_destroying) publish();
}
}
