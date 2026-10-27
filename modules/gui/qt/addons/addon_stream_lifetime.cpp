// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_stream_lifetime.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>

namespace vlc::addons {
namespace {
std::atomic_uint networkCount{0},plainCount{0},qtWorkCount{0};
struct ApplicationRegistry {
    std::mutex mutex;
    AddonStreamLifetime::Handle handle;
};
ApplicationRegistry& applicationRegistry()
{ static ApplicationRegistry registry; return registry; }
constexpr unsigned JoinedLimit=AddonStreamLifetime::NetworkLimit+AddonStreamLifetime::QtWorkLimit;
struct Permit {
    std::atomic_uint* count;
    explicit Permit(std::atomic_uint* value):count(value) {}
    Permit(const Permit&)=delete;
    ~Permit() { count->fetch_sub(1,std::memory_order_release); }
};
std::shared_ptr<Permit> acquire(std::atomic_uint& counter,unsigned limit)
{
    auto count=counter.load(std::memory_order_relaxed);
    do { if(count>=limit) return {}; }
    while(!counter.compare_exchange_weak(count,count+1,std::memory_order_acq_rel,std::memory_order_relaxed));
    try { return std::make_shared<Permit>(&counter); }
    catch(...) { counter.fetch_sub(1,std::memory_order_release); throw; }
}
std::chrono::milliseconds bounded(std::chrono::milliseconds value)
{ return std::clamp(value,std::chrono::milliseconds(0),std::chrono::milliseconds(30000)); }
}
struct AddonStreamLifetime::Network {
    std::thread thread;
    Cancel cancel;
    Cancel cleanup;
    std::shared_ptr<Permit> permit;
    bool started=false,done=false,cancelTaken=false,reaping=false;
    bool networking=true;
    std::atomic_bool joined{false};
    bool retainClosed=false;
    std::shared_ptr<void> retained;
    std::mutex mutex;
    std::condition_variable changed;
};
struct AddonStreamLifetime::State : std::enable_shared_from_this<State> {
    std::atomic_bool open{true};
    std::atomic_uint networks{0},qtWork{0},joinedWork{0},plain{0};
    std::mutex mutex;
    std::condition_variable changed;
    std::array<std::shared_ptr<Network>,JoinedLimit> entries{};
    bool reaperStarted=false;
    void completed(const std::shared_ptr<Network>& entry,unsigned slot) noexcept {
        entry->permit.reset();
        { std::lock_guard<std::mutex> lock(entry->mutex); entry->joined.store(true,std::memory_order_release); }
        {
            std::lock_guard<std::mutex> lock(mutex);
            entries[slot].reset();
            (entry->networking ? networks : qtWork).fetch_sub(1,std::memory_order_release);
            joinedWork.fetch_sub(1,std::memory_order_release);
        }
        entry->changed.notify_all(); changed.notify_all();
    }
    void finish(const std::shared_ptr<Network>& entry,unsigned slot) noexcept {
        // This collector uses native/std APIs only. Qt capture cleanup can
        // create Qt/pthread TLS, so join that separate thread too.
        while(true) {
            try {
                std::thread cleanup([entry] {
                    if(entry->thread.joinable()) entry->thread.join();
                    std::shared_ptr<void> retained;
                    { std::lock_guard<std::mutex> lock(entry->mutex); entry->retainClosed=true; retained=std::move(entry->retained); }
                    entry->cleanup={}; retained.reset();
                });
                cleanup.join(); break;
            } catch(...) {
                // Keep this entry and its quotas closed until cleanup can run.
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        completed(entry,slot);
    }
    void reap() noexcept {
        while(true) {
            std::shared_ptr<Network> entry;
            Cancel cancel;
            unsigned slot=JoinedLimit;
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock,[&] {
                    if(!open.load(std::memory_order_acquire) && !joinedWork.load(std::memory_order_acquire)) return true;
                    for(const auto& value:entries) if(value &&
                        ((!open.load(std::memory_order_acquire) && !value->cancelTaken) || (value->started && value->done && !value->reaping))) return true;
                    return false;
                });
                if(!open.load(std::memory_order_acquire) && !joinedWork.load(std::memory_order_acquire)) return;
                for(unsigned index=0;index<JoinedLimit;++index) {
                    auto& value=entries[index];
                    if(value && !open.load(std::memory_order_acquire) && !value->cancelTaken) {
                        value->cancelTaken=true; cancel.swap(value->cancel); entry=value; break;
                    }
                }
                if(!entry) for(unsigned index=0;index<JoinedLimit;++index) {
                    auto& value=entries[index];
                    if(value && value->started && value->done && !value->reaping) {
                        entry=value; slot=index; value->reaping=true;
                        if(!value->cancelTaken) value->cleanup.swap(value->cancel);
                        value->cancelTaken=true; break;
                    }
                }
            }
            if(slot==JoinedLimit) {
                if(cancel) try { cancel(); } catch(...) {}
                // Destruct captures on the counted cleanup thread, not here.
                { std::lock_guard<std::mutex> lock(mutex); entry->cleanup.swap(cancel); }
                continue;
            }
            const auto state=shared_from_this();
            try { std::thread([state,entry,slot] { state->finish(entry,slot); }).detach(); }
            catch(...) {
                { std::lock_guard<std::mutex> lock(mutex); entry->reaping=false; }
                // No cleanup occurs on this native-only reaper.
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }
};
AddonStreamLifetime::AddonStreamLifetime():m_state(std::make_shared<State>()) {}
AddonStreamLifetime::~AddonStreamLifetime() { close(); }
AddonStreamLifetime::Handle::Handle(std::shared_ptr<State> state):m_state(std::move(state)) {}
AddonStreamLifetime::NetworkTicket::NetworkTicket(std::shared_ptr<Network> entry):m_network(std::move(entry)) {}
bool AddonStreamLifetime::NetworkTicket::joined() const noexcept
{ return !m_network || m_network->joined.load(std::memory_order_acquire); }
bool AddonStreamLifetime::NetworkTicket::waitJoined(std::chrono::milliseconds timeout) const noexcept
{
    if(!m_network) return true;
    std::unique_lock<std::mutex> lock(m_network->mutex);
    return m_network->changed.wait_for(lock,bounded(timeout),[this] { return joined(); });
}
bool AddonStreamLifetime::NetworkTicket::retain(std::shared_ptr<void> value) const noexcept
{
    if(!m_network || !value) return false;
    std::lock_guard<std::mutex> lock(m_network->mutex);
    if(m_network->retainClosed || m_network->retained) return false;
    m_network->retained=std::move(value); return true;
}
bool AddonStreamLifetime::Handle::current() const noexcept
{ return m_state && m_state->open.load(std::memory_order_acquire); }
unsigned AddonStreamLifetime::Handle::networkCount() const noexcept
{ return m_state ? m_state->networks.load(std::memory_order_acquire) : 0; }
unsigned AddonStreamLifetime::Handle::plainCount() const noexcept
{ return m_state ? m_state->plain.load(std::memory_order_acquire) : 0; }
unsigned AddonStreamLifetime::Handle::qtWorkCount() const noexcept
{ return m_state ? m_state->qtWork.load(std::memory_order_acquire) : 0; }
bool AddonStreamLifetime::Handle::networkDrained() const noexcept { return networkCount()==0; }
bool AddonStreamLifetime::Handle::qtDrained() const noexcept
{ return !m_state || m_state->joinedWork.load(std::memory_order_acquire)==0; }
void AddonStreamLifetime::Handle::close() const noexcept
{
    if(m_state) {
        { std::lock_guard<std::mutex> lock(m_state->mutex); m_state->open.store(false,std::memory_order_release); }
        m_state->changed.notify_all();
    }
}
bool AddonStreamLifetime::Handle::waitNetwork(std::chrono::milliseconds timeout) const noexcept
{
    if(!m_state) return true;
    std::unique_lock<std::mutex> lock(m_state->mutex);
    return m_state->changed.wait_for(lock,bounded(timeout),[this] { return networkDrained(); });
}
bool AddonStreamLifetime::Handle::waitQt(std::chrono::milliseconds timeout) const noexcept
{
    if(!m_state) return true;
    std::unique_lock<std::mutex> lock(m_state->mutex);
    return m_state->changed.wait_for(lock,bounded(timeout),[this] { return qtDrained(); });
}
bool AddonStreamLifetime::Handle::startNetwork(Work run,Cancel cancel,std::function<void(NetworkTicket)> registered) const
{ return start(std::move(run),std::move(cancel),std::move(registered),true); }
bool AddonStreamLifetime::Handle::startQtWork(Work run,Cancel cancel,std::function<void(NetworkTicket)> registered) const
{ return start(std::move(run),std::move(cancel),std::move(registered),false); }
bool AddonStreamLifetime::Handle::start(Work run,Cancel cancel,std::function<void(NetworkTicket)> registered,bool networking) const
{
    if(!m_state || !run || !cancel || !current()) return false;
    const auto state=m_state;
    auto entry=std::make_shared<Network>(); entry->cancel=std::move(cancel); entry->networking=networking;
    entry->permit=acquire(networking ? ::vlc::addons::networkCount : ::vlc::addons::qtWorkCount,networking ? NetworkLimit : QtWorkLimit);
    if(!entry->permit) return false;
    unsigned slot=JoinedLimit;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if(!state->open.load(std::memory_order_acquire)) return false;
        for(unsigned index=0;index<JoinedLimit;++index) if(!state->entries[index]) { slot=index; break; }
        if(slot==JoinedLimit) return false;
        if(!state->reaperStarted) {
            std::thread([state] { state->reap(); }).detach(); state->reaperStarted=true;
        }
        state->entries[slot]=entry;
        (networking ? state->networks : state->qtWork).fetch_add(1,std::memory_order_release);
        state->joinedWork.fetch_add(1,std::memory_order_release);
    }
    try { if(registered) registered(NetworkTicket(entry)); }
    catch(...) {
        std::lock_guard<std::mutex> lock(state->mutex); entry->started=true; entry->done=true; state->changed.notify_all(); return false;
    }
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if(!state->open.load(std::memory_order_acquire)) {
            entry->started=true; entry->done=true; state->changed.notify_all(); return false;
        }
        try {
            entry->thread=std::thread([state,entry,run=std::move(run)]() mutable {
                try { run(); } catch(...) {}
                run={};
                { std::lock_guard<std::mutex> lock(state->mutex); entry->done=true; }
                state->changed.notify_all();
            });
            entry->started=true;
        } catch(...) { entry->started=true; entry->done=true; state->changed.notify_all(); return false; }
    }
    state->changed.notify_all(); return true;
}
bool AddonStreamLifetime::Handle::startPlain(Work run,std::shared_ptr<void> retained) const
{
    if(!m_state || !run || !current()) return false;
    const auto state=m_state; const auto permit=acquire(::vlc::addons::plainCount,PlainLimit); if(!permit) return false;
    const auto callback=std::make_shared<Work>(std::move(run));
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if(!state->open.load(std::memory_order_acquire)) return false;
        state->plain.fetch_add(1,std::memory_order_release);
    }
    try {
        std::thread([state,permit,callback,retained=std::move(retained)]() mutable {
            // The callback and its resource use only native/std APIs.
            while(state->open.load(std::memory_order_acquire)) {
                try {
                    std::thread worker([callback] {
                        try { (*callback)(); } catch(...) {}
                        *callback={};
                    });
                    worker.join(); break;
                } catch(...) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
            *callback={}; retained.reset();
            state->plain.fetch_sub(1,std::memory_order_release); state->changed.notify_all();
        }).detach();
    } catch(...) { state->plain.fetch_sub(1,std::memory_order_release); state->changed.notify_all(); return false; }
    return true;
}
unsigned AddonStreamLifetime::processNetworkCount() noexcept { return networkCount.load(std::memory_order_acquire); }
unsigned AddonStreamLifetime::processPlainCount() noexcept { return plainCount.load(std::memory_order_acquire); }
unsigned AddonStreamLifetime::processQtWorkCount() noexcept { return qtWorkCount.load(std::memory_order_acquire); }
bool AddonStreamLifetime::registerApplication(const Handle& handle) noexcept
{
    auto& registry = applicationRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    if (!handle.current()) return false;
    if (registry.handle.m_state) return registry.handle.m_state == handle.m_state;
    registry.handle = handle;
    return true;
}
bool AddonStreamLifetime::unregisterApplication(const Handle& handle) noexcept
{
    auto& registry = applicationRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    if (!handle.m_state || registry.handle.m_state != handle.m_state
        || handle.current() || !handle.qtDrained()) return false;
    registry.handle = {};
    return true;
}
AddonStreamLifetime::Handle AddonStreamLifetime::applicationHandle() noexcept
{
    auto& registry = applicationRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);
    return registry.handle;
}
}
