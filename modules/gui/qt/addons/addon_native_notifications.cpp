// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_native_notifications.hpp"
#include <array>
#include <atomic>
#include <mutex>
#include <utility>
namespace vlc::addons {
struct NativeNotificationState {
    std::mutex mutex;
    std::array<std::weak_ptr<NativeNotificationCallback>,NativeNotifications::Limit> callbacks;
    std::atomic_uint count{0};
};
struct NativeNotificationCallback {
    struct Quota {
        std::shared_ptr<NativeNotificationState> owner;
        bool counted=false;
        ~Quota(){if(counted)owner->count.fetch_sub(1,std::memory_order_release);}
    };
    NativeNotificationCallback(std::shared_ptr<NativeNotificationState> owner,std::function<void()> call)
        :quota{std::move(owner),false},wake(std::move(call)){}
    // First declared: release the bound only after the native wake capture.
    Quota quota;
    const std::function<void()> wake;
};
NativeNotifications::NativeNotifications() noexcept
{try{m_state=std::make_shared<NativeNotificationState>();}catch(...) {}}
NativeNotification NativeNotifications::subscribe(std::function<void()> wake) const noexcept
{
    NativeNotification result;
    try {
        if(!m_state || !wake)return result;
        const auto callback=std::make_shared<NativeNotificationCallback>(m_state,std::move(wake));
        auto count=m_state->count.load(std::memory_order_acquire);
        do {if(count>=Limit)return result;}
        while(!m_state->count.compare_exchange_weak(count,count+1,std::memory_order_acq_rel,std::memory_order_acquire));
        callback->quota.counted=true;
        {
            std::lock_guard<std::mutex> lock(m_state->mutex);
            for(auto& weak:m_state->callbacks)if(weak.expired()){
                weak=callback;result.m_callback=callback;break;
            }
        }
        // Local captures and any last references retire outside the registry.
    } catch(...) {}
    return result;
}
void NativeNotifications::notify() const noexcept
{
    if(!m_state)return;
    std::array<std::shared_ptr<NativeNotificationCallback>,Limit> pending;
    unsigned count=0;
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        for(auto& weak:m_state->callbacks){
            if(auto callback=weak.lock())pending[count++]=std::move(callback);
            else weak.reset();
        }
    }
    for(unsigned i=0;i<count;++i)try{pending[i]->wake();}catch(...){}
    // Do not release a last callback/capture under the registry lock.
}
}
