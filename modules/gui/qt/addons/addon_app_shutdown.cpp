// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_app_shutdown.hpp"
#include <QCoreApplication>
#include <QEvent>
#include <QPointer>
#include <QThread>
#include <algorithm>
#include <utility>

namespace vlc::addons {
AddonAppShutdown::AddonAppShutdown(QCoreApplication& app)
    :AddonAppShutdown(app,Hooks{},Options{}) {}
AddonAppShutdown::AddonAppShutdown(QCoreApplication& app,Hooks hooks,Options options)
    :QObject(nullptr),m_app(app),m_stream(app),m_hooks(std::move(hooks)),m_options(options)
{
    m_valid=QCoreApplication::instance()==&app && QThread::currentThread()==app.thread();
    m_options.timeoutMs=std::clamp(m_options.timeoutMs,1,5000);
    m_options.retryMs=std::clamp(m_options.retryMs,10,1000);
    m_retry.setSingleShot(true);
    connect(&m_retry,&QTimer::timeout,this,[this] { waitForWork(); });
    connect(&app,&QCoreApplication::aboutToQuit,this,[this] { begin(); },Qt::DirectConnection);
}
AddonAppShutdown::~AddonAppShutdown()
{ m_destroying=true; m_retry.stop(); m_stream.handle().close(); }
bool AddonAppShutdown::onAppThread() const
{ return m_valid && !m_destroying && QThread::currentThread()==thread(); }
bool AddonAppShutdown::setHooks(Hooks hooks)
{
    if(!onAppThread() || m_retiring) return false;
    m_hooks=std::move(hooks); return true;
}
bool AddonAppShutdown::setUserQuit(Action action)
{
    if(!onAppThread() || !action || m_userQuit || m_userDelivered) return false;
    m_userQuit=std::move(action); scheduleActions(); return true;
}
bool AddonAppShutdown::requestRetirement(Action ready)
{
    if(!onAppThread() || !ready || m_failed) return false;
    if(m_joined) { ready(); return true; }
    if(m_ready.size()>=16) return false;
    m_ready.push_back(std::move(ready)); begin(); return true;
}
bool AddonAppShutdown::requestNativeClose(Action ready)
{
    if(!onAppThread() || !ready || m_failed) return false;
    if(m_joined) { ready(); return true; }
    if(m_nativeClose) return false;
    m_nativeClose.swap(ready); begin(); return true;
}
void AddonAppShutdown::requestUserQuit()
{
    if(!onAppThread()) return;
    const QPointer<AddonAppShutdown> owner(this);
    m_userRequested=true; begin(); if(owner) scheduleActions();
}
void AddonAppShutdown::requestExit(int code)
{
    if(!onAppThread()) return;
    const QPointer<AddonAppShutdown> owner(this);
    m_exitRequested=true; m_exitCode=code; begin(); if(owner) scheduleActions();
}
void AddonAppShutdown::recoverEventLoopExit()
{
    if(!onAppThread()) return;
    const QPointer<AddonAppShutdown> owner(this); begin(); if(!owner) return;
    if(!m_exitRequested) requestUserQuit();
    else scheduleActions();
}
bool AddonAppShutdown::canDestroyApp() noexcept
{ return onAppThread() && m_exitRequested && m_joined && !m_failed && m_stream.finalGuard(0); }
void AddonAppShutdown::begin()
{
    if(!onAppThread()) return;
    const QPointer<AddonAppShutdown> owner(this);
    if(!m_retiring) {
        m_retiring=true; m_retiringHook=true; m_stream.handle().close();
        const auto action=m_hooks.retiring;
        try { if(action) action(); } catch(...) { if(owner) m_failed=true; }
        if(!owner) return;
        m_retiringHook=false;
    }
    waitForWork();
}
void AddonAppShutdown::waitForWork()
{
    if(!onAppThread() || !m_retiring || m_joined || m_waiting || m_finishing || m_retiringHook || m_failed) return;
    m_waiting=true;
    const QPointer<AddonAppShutdown> owner(this);
    const bool accepted=m_stream.request([owner](auto result) {
        if(!owner) return;
        owner->m_waiting=false;
        if(result==AddonStreamShutdown::Result::Drained) { owner->finish(); return; }
        if(!owner->m_reported) {
            owner->m_reported=true; const auto action=owner->m_hooks.timedOut;
            try { if(action) action(); } catch(...) {}
            if(!owner) return;
        }
        owner->m_retry.start(owner->m_options.retryMs);
    },m_options.timeoutMs);
    if(!accepted && owner) { m_waiting=false; m_retry.start(m_options.retryMs); }
}
void AddonAppShutdown::finish()
{
    if(m_joined || m_finishing || m_failed || !m_stream.finalGuard(0)) return;
    m_finishing=true;
    const QPointer<AddonAppShutdown> owner(this); const auto action=m_hooks.joined;
    try { if(action) action(); } catch(...) { if(owner) { m_failed=true; m_finishing=false; } return; }
    if(!owner) return;
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    if(!owner) return;
    if(!m_stream.finalGuard(0)) { m_finishing=false; waitForWork(); return; }
    m_joined=true; m_finishing=false;
    std::vector<Action> ready; ready.swap(m_ready);
    for(auto& callback:ready) {
        try { callback(); } catch(...) {}
        if(!owner) return;
    }
    Action native; native.swap(m_nativeClose);
    if(native) try { native(); } catch(...) {}
    if(!owner) return;
    scheduleActions();
}
void AddonAppShutdown::scheduleActions()
{
    if(!onAppThread() || !m_joined || m_failed) return;
    if(m_exitRequested && !m_exitQueued) {
        m_exitQueued=true;
        QTimer::singleShot(0,this,[this] {
            m_exitQueued=false;
            if(canDestroyApp()) m_app.exit(m_exitCode);
        });
    } else if(!m_exitRequested && m_userRequested && !m_userDelivered && !m_userQueued && m_userQuit) {
        m_userQueued=true;
        QTimer::singleShot(0,this,[this] {
            m_userQueued=false;
            if(m_exitRequested || m_userDelivered || !m_joined || m_failed) return;
            m_userDelivered=true; Action action; action.swap(m_userQuit);
            try { action(); } catch(...) {}
        });
    }
}
}
