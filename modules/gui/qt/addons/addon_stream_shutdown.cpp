// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_stream_shutdown.hpp"
#include <QCoreApplication>
#include <QPointer>
#include <QThread>
#include <algorithm>

namespace vlc::addons {
AddonStreamShutdown::AddonStreamShutdown(QCoreApplication& app):QObject(nullptr)
{
    m_valid=QCoreApplication::instance()==&app && QThread::currentThread()==app.thread();
    if(!m_valid) { m_lifetime.close(); return; }
    m_clock.start(); m_timer.setInterval(10);
    connect(&m_timer,&QTimer::timeout,this,[this] { poll(); });
    connect(&app,&QCoreApplication::aboutToQuit,this,[this] { m_lifetime.close(); },Qt::DirectConnection);
}
AddonStreamShutdown::~AddonStreamShutdown()
{ m_destroying=true; m_timer.stop(); m_lifetime.close(); }
AddonStreamLifetime::Handle AddonStreamShutdown::handle() const noexcept
{ return m_valid ? m_lifetime.handle() : AddonStreamLifetime::Handle{}; }
bool AddonStreamShutdown::request(Complete complete,int timeoutMs)
{
    if(!m_valid || m_destroying || !complete || QThread::currentThread()!=thread() || m_callbacks.size()>=8) return false;
    m_lifetime.close();
    const auto deadline=m_clock.elapsed()+std::clamp(timeoutMs,0,5000);
    if(!m_running) { m_deadline=deadline; m_running=true; }
    else m_deadline=std::min(m_deadline,deadline);
    m_callbacks.push_back(std::move(complete));
    m_timer.start(); poll(); return true;
}
void AddonStreamShutdown::poll()
{
    if(!m_running) return;
    const auto drained=m_lifetime.handle().qtDrained();
    if(!drained && m_clock.elapsed()<m_deadline) return;
    auto callbacks=std::move(m_callbacks); m_callbacks.clear(); m_running=false; m_timer.stop();
    const QPointer<AddonStreamShutdown> owner(this);
    for(auto& complete:callbacks) {
        if(!owner) return;
        try { complete(drained ? Result::Drained : Result::TimedOut); } catch(...) {}
    }
}
bool AddonStreamShutdown::finalGuard(int timeoutMs) noexcept
{
    m_lifetime.close();
    return m_lifetime.handle().waitQt(std::chrono::milliseconds(std::clamp(timeoutMs,0,5000)));
}
}
