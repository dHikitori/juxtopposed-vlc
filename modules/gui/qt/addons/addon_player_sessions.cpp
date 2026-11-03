// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_player_sessions.hpp"
#include "addon_manager.hpp"
#include "addon_native_session_player.hpp"
#include "addon_session_host.hpp"
#include "addon_session_controller.hpp"
#include "addon_session_ui.hpp"
#include <QScopeGuard>
#include <QThread>

namespace vlc::addons {
AddonPlayerSessions::AddonPlayerSessions(AddonManager* manager,vlc_player_t* player,
    QObject* playerLifetime,AddonStreamLifetime::Handle lifetime,QObject* parent)
    :QObject(parent),m_manager(manager),m_playerLifetime(playerLifetime),m_lifetime(std::move(lifetime)) {
    if(!manager || !player || !playerLifetime || !m_lifetime.current()) { m_closed=true; return; }
    connect(manager,&AddonManager::retiring,this,&AddonPlayerSessions::close,Qt::DirectConnection);
    connect(manager,&QObject::destroyed,this,&AddonPlayerSessions::close);
    connect(playerLifetime,&QObject::destroyed,this,&AddonPlayerSessions::close);
    m_player=new AddonNativeSessionPlayer(player,this);
    const QPointer<AddonManager> owner(manager);
    m_host=new AddonSessionHost(m_player->backend(),{manager,[owner](const QString& contribution) {
        return owner ? owner->nativeAuthorityReader(contribution) : AddonNativeAuthorityReader{};
    }},m_lifetime,this);
    m_controller=new AddonSessionController(m_host,{manager,
        [owner](const QString& contribution,const QString& operation,const QVariantMap& context) {
            return owner ? owner->invoke(contribution,operation,context) : QString{};
        },[owner](const QString& request) { if(owner) owner->cancel(request); }},this);
    m_ui=new AddonSessionUi(m_host,manager,this);
    connect(manager,&AddonManager::operationFinished,m_controller,&AddonSessionController::completed);
    connect(manager,&AddonManager::packageWithdrawn,m_host,&AddonSessionHost::revokePackage,Qt::DirectConnection);
}
AddonPlayerSessions::~AddonPlayerSessions() {
    if(m_ui) m_ui->blockSignals(true);
    close();
}
AddonSessionUi* AddonPlayerSessions::ui() const { return m_ui.data(); }
bool AddonPlayerSessions::activate() {
    Q_ASSERT(QThread::currentThread()==thread());
    if(m_closed || m_active || !m_manager || !m_playerLifetime || !m_lifetime.current()
        || !m_player || !m_player->available() || !m_host || !m_controller || !m_ui) return false;
    const QPointer<AddonPlayerSessions> alive(this);
    const bool mounted=m_host->mount(m_manager->hostServices());
    if(!alive) return false;
    if(!mounted || m_closed || !m_manager || !m_playerLifetime || !m_lifetime.current()) { close(); return false; }
    m_active=true; m_ui->refresh(); return bool(alive) && alive->active();
}
void AddonPlayerSessions::close() {
    Q_ASSERT(QThread::currentThread()==thread());
    if(m_closed) return;
    m_closed=true; m_active=false;
    const QPointer<AddonPlayerSessions> alive(this);
    {
        const auto ui=m_ui;
        const bool blocked=ui ? ui->blockSignals(true) : false;
        const auto unblock=qScopeGuard([ui,blocked] { if(ui) ui->blockSignals(blocked); });
        if(m_controller) m_controller->close();
        if(!alive) return;
        if(m_host) m_host->close();
        if(!alive) return;
        if(m_player) m_player->close();
        if(!alive) return;
    }
    if(m_ui) m_ui->close();
}
}
