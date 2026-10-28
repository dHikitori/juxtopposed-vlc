// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_source_consent_ui.hpp"
#include <QSet>
#include <QThread>
#include <chrono>

namespace vlc::addons {
namespace {
qint64 nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool validReview(const QVariantMap& review,const QString& source) {
    const QSet<QString> allowed{QStringLiteral("id"),QStringLiteral("name"),QStringLiteral("source"),
        QStringLiteral("kind"),QStringLiteral("accountLabel"),QStringLiteral("accountFields"),
        QStringLiteral("accountReference"),QStringLiteral("profile"),QStringLiteral("profiles"),
        QStringLiteral("publisherVerified"),QStringLiteral("approved")};
    for(auto it=review.cbegin();it!=review.cend();++it)
        if(!allowed.contains(it.key())) return false;
    const auto name=review.value(QStringLiteral("name")).toString();
    const auto profile=QStringLiteral("public-https-media@1");
    return review.value(QStringLiteral("id")).toString()==source && !name.isEmpty() && name.size()<=512
        && review.value(QStringLiteral("profile")).toString()==profile
        && review.value(QStringLiteral("profiles")).toStringList()==QStringList{profile}
        && review.value(QStringLiteral("approved")).metaType().id()==QMetaType::Bool;
}
}
AddonSourceConsentUi::AddonSourceConsentUi(Backend backend,QObject* parent)
    :QObject(parent),m_backend(std::move(backend)),m_owner(m_backend.lifetime) {
    m_closed=!m_owner || !m_backend.active || !m_backend.request || !m_backend.read
        || !m_backend.change || !m_backend.discard;
    if(m_owner) connect(m_owner,&QObject::destroyed,this,&AddonSourceConsentUi::close);
    m_poll.setSingleShot(true);
    connect(&m_poll,&QTimer::timeout,this,&AddonSourceConsentUi::refresh);
    if(!m_closed) refresh();
}
AddonSourceConsentUi::~AddonSourceConsentUi() {
    m_destroying=true;
    close();
}
void AddonSourceConsentUi::publish() { if(!m_destroying) emit changed(); }
bool AddonSourceConsentUi::fail(const QString& message) {
    m_error=message.left(4096);
    m_status.clear();
    publish();
    return false;
}
quint64 AddonSourceConsentUi::clearReview() {
    const auto generation=++m_generation;
    m_poll.stop();
    QString request;
    request.swap(m_request);
    m_source.clear(); m_review.clear(); m_preparing=false; m_acting=false;
    m_expires=0; m_earlyReady.clear(); m_earlyAction={};
    const auto discard=m_backend.discard;
    const auto owner=m_owner;
    if(owner && discard && !request.isEmpty()) discard(request);
    return generation;
}
void AddonSourceConsentUi::refresh() {
    Q_ASSERT(QThread::currentThread()==thread());
    if(m_closed || !m_owner) return;
    const auto active=m_backend.active;
    const auto generation=m_generation;
    const QPointer<AddonSourceConsentUi> alive(this);
    const bool available=active();
    if(!alive || m_closed || !m_owner || generation!=m_generation) return;
    const bool changedAvailability=m_available!=available;
    m_available=available;
    if(!available && !m_request.isEmpty()) {
        const auto generation=clearReview();
        if(alive && !m_closed && m_generation==generation) fail(tr("Source playback is not available."));
        return;
    }
    if(!m_request.isEmpty() && !m_acting) {
        if(nowMs()>=m_expires) {
            const auto generation=clearReview();
            if(alive && !m_closed && m_generation==generation) fail(tr("The review expired. Open it again."));
            return;
        }
        if(!m_preparing) {
            const auto read=m_backend.read;
            const auto request=m_request;
            const auto generation=m_generation;
            const auto review=read(request);
            if(!alive || m_closed || !m_owner || request!=m_request || generation!=m_generation) return;
            if(review!=m_review) {
                const auto cleared=clearReview();
                if(alive && !m_closed && m_generation==cleared) fail(tr("The source or account changed. Open the review again."));
                return;
            }
        }
    }
    if(!m_request.isEmpty() && !m_acting) {
        const auto next=int(qMax<qint64>(1,m_expires-nowMs()));
        const auto remaining=m_poll.remainingTime();
        if(!m_poll.isActive() || remaining<0 || next<remaining) m_poll.start(next);
    }
    if(changedAvailability) publish();
}
bool AddonSourceConsentUi::prepare(const QString& contribution) {
    if(m_closed || !m_owner || m_acting || contribution.isEmpty() || contribution.size()>256) return false;
    const QPointer<AddonSourceConsentUi> alive(this);
    const auto generation=clearReview();
    if(!alive || m_closed || generation!=m_generation) return false;
    refresh();
    if(!alive || m_closed || generation!=m_generation) return false;
    if(!m_available) return fail(tr("Source playback is not available."));
    m_source=contribution; m_error.clear(); m_status.clear();
    m_preparing=true; m_creating=true; m_expires=nowMs()+30000;
    const auto request=m_backend.request;
    const auto discard=m_backend.discard;
    const auto owner=m_owner;
    const auto ticket=request(contribution);
    if(!alive) { if(owner && !ticket.isEmpty()) discard(ticket); return false; }
    m_creating=false;
    if(m_closed || !m_owner || generation!=m_generation) {
        if(owner && !ticket.isEmpty()) discard(ticket);
        return false;
    }
    if(ticket.isEmpty() || ticket.size()>128) {
        m_request=ticket;
        const auto cleared=clearReview();
        return alive && !m_closed && m_generation==cleared && fail(tr("The app cannot prepare this source review."));
    }
    m_request=ticket;
    m_poll.start(int(qMax<qint64>(1,m_expires-nowMs())));
    const auto ready=m_earlyReady;
    const auto action=m_earlyAction;
    m_earlyReady.clear(); m_earlyAction={};
    if(action.request==ticket) actionFinished(ticket,action.success,action.error);
    else if(ready==ticket) approvalReady(ticket);
    else publish();
    return bool(alive);
}
void AddonSourceConsentUi::approvalReady(const QString& input) {
    const QString request=input;
    if(m_closed || !m_owner) return;
    if(m_creating) { if(m_earlyReady.isEmpty()) m_earlyReady=request; return; }
    if(request!=m_request || !m_preparing || m_acting) return;
    const auto read=m_backend.read;
    const auto generation=m_generation;
    const QPointer<AddonSourceConsentUi> alive(this);
    const auto review=read(request);
    if(!alive || m_closed || !m_owner || request!=m_request || generation!=m_generation) return;
    if(nowMs()>=m_expires || !validReview(review,m_source)) {
        const auto cleared=clearReview();
        if(alive && !m_closed && m_generation==cleared) fail(tr("The source review is no longer available."));
        return;
    }
    m_preparing=false; m_review=review;
    publish();
}
bool AddonSourceConsentUi::change(bool approve) {
    if(m_closed || !m_owner || m_request.isEmpty() || m_preparing || m_acting || m_review.isEmpty()) return false;
    const auto read=m_backend.read;
    const auto request=m_request;
    const auto generation=m_generation;
    const auto change=m_backend.change;
    const QPointer<AddonSourceConsentUi> alive(this);
    const auto current=read(request);
    if(!alive || m_closed || !m_owner || request!=m_request || generation!=m_generation || m_acting || m_preparing) return false;
    if(nowMs()>=m_expires || current!=m_review || !validReview(current,m_source)
        || current.value(QStringLiteral("approved")).toBool()==approve) {
        const auto cleared=clearReview();
        return alive && !m_closed && m_generation==cleared && fail(tr("The source or account changed. Open the review again."));
    }
    m_poll.stop();
    m_acting=true; m_changing=true; m_approving=approve; m_error.clear(); m_earlyAction={};
    const bool accepted=change(request,approve);
    if(!alive) return false;
    m_changing=false;
    if(m_closed || !m_owner || request!=m_request || generation!=m_generation) return accepted;
    const auto early=m_earlyAction;
    m_earlyAction={};
    if(!accepted) {
        const auto cleared=clearReview();
        return alive && !m_closed && m_generation==cleared && fail(tr("The app refused this source change. Open a new review."));
    }
    if(early.request==request) actionFinished(request,early.success,early.error);
    else {
        m_status=approve ? tr("The app saves the source approval.") : tr("The app removes the source approval.");
        publish();
    }
    return true;
}
bool AddonSourceConsentUi::approve() { return change(true); }
bool AddonSourceConsentUi::revoke() { return change(false); }
void AddonSourceConsentUi::actionFinished(const QString& input,bool success,const QString& message) {
    const QString request=input,error=message;
    if(m_closed || !m_owner) return;
    if(m_creating || m_changing) {
        if(m_earlyAction.request.isEmpty()) m_earlyAction={request,error.left(4096),success};
        return;
    }
    if(request!=m_request) return;
    if(!m_acting && success) return;
    const bool approved=m_approving;
    const QPointer<AddonSourceConsentUi> alive(this);
    const auto generation=clearReview();
    if(!alive || m_closed || generation!=m_generation) return;
    if(!success) { fail(error.isEmpty() ? tr("The app cannot change this source approval.") : error); return; }
    m_error.clear();
    m_status=approved ? tr("Source playback is permitted.") : tr("Source playback approval was removed.");
    publish();
}
void AddonSourceConsentUi::viewClosed() {
    if(m_closed) return;
    const QPointer<AddonSourceConsentUi> alive(this);
    const auto generation=clearReview();
    if(!alive || m_closed || generation!=m_generation) return;
    m_error.clear(); m_status.clear();
    publish();
}
void AddonSourceConsentUi::close() {
    if(m_closed) return;
    m_closed=true; m_available=false; m_poll.stop();
    const QPointer<AddonSourceConsentUi> alive(this);
    clearReview();
    if(alive) { m_error.clear(); m_status.clear(); publish(); }
}
}
