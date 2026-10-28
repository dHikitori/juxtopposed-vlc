// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_native_wake.hpp"
#include <QPointer>
#include <atomic>
#include <cerrno>
#include <utility>
#ifdef Q_OS_WIN
# include <windows.h>
# include <QWinEventNotifier>
#else
# include <unistd.h>
# include <fcntl.h>
# include <QSocketNotifier>
#endif
namespace vlc::addons {
struct AddonNativeWake::Native {
    std::atomic_bool closed{false};
#ifdef Q_OS_WIN
    HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    bool valid() const noexcept { return event!=nullptr; }
    ~Native() { const auto error=GetLastError();if(event)CloseHandle(event);SetLastError(error); }
    void signal() noexcept {
        const auto error=GetLastError();
        if(!closed.load(std::memory_order_acquire))SetEvent(event);
        SetLastError(error);
    }
    void drain() noexcept { ResetEvent(event); }
#else
    int descriptors[2]{-1,-1};
    Native() {
        if(pipe(descriptors))return;
        for(auto fd:descriptors) {
            const auto flags=fcntl(fd,F_GETFL);
            if(flags<0 || fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0 || fcntl(fd,F_SETFD,FD_CLOEXEC)<0) {
                ::close(descriptors[0]);::close(descriptors[1]);descriptors[0]=descriptors[1]=-1;return;
            }
        }
    }
    bool valid() const noexcept { return descriptors[0]>=0; }
    ~Native() { const int saved=errno;for(auto fd:descriptors)if(fd>=0)::close(fd);errno=saved; }
    void signal() noexcept {
        const int saved=errno;
        if(!closed.load(std::memory_order_acquire)) {
            const char byte=0;ssize_t result;
            do { result=write(descriptors[1],&byte,1); } while(result<0 && errno==EINTR);
            // EAGAIN means the existing readable wake already covers this one.
        }
        errno=saved;
    }
    void drain() noexcept {
        char bytes[1024];
        for(unsigned attempts=0;attempts<64;++attempts) {
            const auto count=read(descriptors[0],bytes,sizeof(bytes));
            if(count<0 && errno==EINTR)continue;
            if(count<=0)break;
        }
    }
#endif
};
AddonNativeWake::AddonNativeWake(std::function<void()> deliver,QObject* parent)
    :QObject(parent),m_native(std::make_shared<Native>()),m_deliver(std::move(deliver))
{
    if(!m_native->valid())return;
#ifdef Q_OS_WIN
    auto* notifier=new QWinEventNotifier(m_native->event,this);
    connect(notifier,&QWinEventNotifier::activated,this,[this] { activated(); });
#else
    auto* notifier=new QSocketNotifier(m_native->descriptors[0],QSocketNotifier::Read,this);
    connect(notifier,&QSocketNotifier::activated,this,[this] { activated(); });
#endif
    m_notifier=notifier;
}
AddonNativeWake::~AddonNativeWake() { close(); }
bool AddonNativeWake::valid() const noexcept { return !m_closed && m_native && m_native->valid() && m_notifier; }
std::function<void()> AddonNativeWake::nativeCallback() const
{
    const auto native=m_native;
    return [native] { if(native && native->valid())native->signal(); };
}
void AddonNativeWake::activated()
{
    if(m_closed)return;
#ifdef Q_OS_WIN
    auto* notifier=static_cast<QWinEventNotifier*>(m_notifier);
#else
    auto* notifier=static_cast<QSocketNotifier*>(m_notifier);
#endif
    notifier->setEnabled(false);m_native->drain();
    auto deliver=m_deliver;const QPointer<AddonNativeWake> alive(this);
    if(deliver)deliver();
    deliver={};
    if(alive && !m_closed)notifier->setEnabled(true);
}
void AddonNativeWake::close()
{
    if(m_closed)return;
    m_closed=true;
    if(m_native)m_native->closed.store(true,std::memory_order_release);
    // No callback owns Qt. Disable/destroy notifier before dropping OS owner.
    delete std::exchange(m_notifier,nullptr);
    m_native.reset();std::function<void()> deliver;deliver.swap(m_deliver);
}
}
