// SPDX-License-Identifier: GPL-2.0-or-later
#include "mljoinedthreadrunner.hpp"
#include <QMap>
#include <QPointer>
#include <QThread>
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>

namespace {
std::atomic_uint processTasks{0};
struct Control { std::atomic_bool canceled{false}; };
bool reserve(std::atomic_uint& count,unsigned limit)
{
    auto value=count.load(std::memory_order_acquire);
    while (value<limit)
        if (count.compare_exchange_weak(value,value+1,std::memory_order_acq_rel)) return true;
    return false;
}
struct Quota { std::atomic_uint count{0}; };
struct Permit {
    std::shared_ptr<Quota> owner;
    explicit Permit(std::shared_ptr<Quota> value):owner(std::move(value)) {}
    ~Permit() { owner->count.fetch_sub(1,std::memory_order_acq_rel); processTasks.fetch_sub(1,std::memory_order_acq_rel); }
};
std::shared_ptr<Permit> acquire(const std::shared_ptr<Quota>& owner)
{
    if (!reserve(owner->count,MLJoinedThreadRunner::OwnerLimit)) return {};
    if (!reserve(processTasks,MLJoinedThreadRunner::ProcessLimit)) { --owner->count; return {}; }
    try { return std::make_shared<Permit>(owner); }
    catch (...) { --owner->count; --processTasks; return {}; }
}
}
struct MLJoinedThreadRunner::Shared {
    std::mutex mutex;
    std::condition_variable changed;
    MLJoinedThreadRunner* owner=nullptr;
    bool closed=false;
    std::deque<std::shared_ptr<Control>> queue;
    const std::shared_ptr<Quota> quota=std::make_shared<Quota>();
};
struct MLJoinedThreadRunner::Entry {
    const QObject* receiver=nullptr;
    std::shared_ptr<Control> control;
    std::function<void(quint64)> complete;
    QMetaObject::Connection destroyed;
};
struct MLJoinedThreadRunner::Entries { QMap<quint64,Entry> values; };
MLJoinedThreadRunner::MLJoinedThreadRunner(Handle lifetime,QObject* parent)
    :QObject(parent),m_lifetime(std::move(lifetime)),m_shared(std::make_shared<Shared>()),m_entries(std::make_unique<Entries>())
{ m_shared->owner=this; }
MLJoinedThreadRunner::~MLJoinedThreadRunner() { close(); }
unsigned MLJoinedThreadRunner::processTaskCount() noexcept { return processTasks.load(std::memory_order_acquire); }
void MLJoinedThreadRunner::close()
{
    Q_ASSERT(QThread::currentThread()==thread());
    const auto shared=m_shared;
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        shared->closed=true; shared->owner=nullptr;
        for (const auto& control:shared->queue) control->canceled.store(true,std::memory_order_release);
    }
    shared->changed.notify_all();
    auto entries=std::move(m_entries->values); m_entries->values.clear();
    for (const auto& entry:entries) { entry.control->canceled=true; disconnect(entry.destroyed); }
}
bool MLJoinedThreadRunner::cancelTask(const QObject* receiver,quint64 id)
{
    Q_ASSERT(QThread::currentThread()==thread());
    auto it=m_entries->values.find(id);
    if (it==m_entries->values.end() || it->receiver!=receiver) return false;
    const auto shared=m_shared;
    std::function<void(quint64)> retired;
    retired.swap(it->complete);
    it->control->canceled.store(true,std::memory_order_release);
    const auto connection=it->destroyed;
    m_entries->values.erase(it); disconnect(connection); shared->changed.notify_all();
    return true;
}
void MLJoinedThreadRunner::receiverDestroyed(const QObject* receiver)
{
    const auto shared=m_shared;
    std::array<std::function<void(quint64)>,OwnerLimit> retired;
    unsigned count=0;
    for (auto it=m_entries->values.begin();it!=m_entries->values.end();) {
        if (it->receiver!=receiver) { ++it; continue; }
        Q_ASSERT(count<retired.size()); retired[count++].swap(it->complete);
        it->control->canceled.store(true,std::memory_order_release);
        disconnect(it->destroyed); it=m_entries->values.erase(it);
    }
    shared->changed.notify_all();
}
quint64 MLJoinedThreadRunner::submit(const QObject* receiver,std::function<void()> work,std::function<void(quint64)> complete)
{
    if (QThread::currentThread()!=thread()) return 0;
    if (!receiver || receiver->thread()!=thread() || !work || !complete || !m_lifetime.current()
        || m_entries->values.size()>=OwnerLimit) return 0;
    const auto shared=m_shared;
    const auto permit=acquire(shared->quota); if (!permit) return 0;
    const auto id=nextMLTaskId(); if (!id) return 0;
    const auto control=std::make_shared<Control>();
    {
        std::lock_guard<std::mutex> lock(shared->mutex);
        if (shared->closed) return 0;
        shared->queue.push_back(control);
    }
    Entry entry; entry.receiver=receiver; entry.control=control; entry.complete=std::move(complete);
    entry.destroyed=connect(receiver,&QObject::destroyed,this,[this,receiver] { receiverDestroyed(receiver); });
    m_entries->values.insert(id,std::move(entry));
    const auto lifetime=m_lifetime;
    bool started=false;
    try { started=lifetime.startQtWork([shared,control,lifetime,id,work=std::move(work)]() mutable {
        bool execute=false;
        {
            std::unique_lock<std::mutex> lock(shared->mutex);
            shared->changed.wait(lock,[&] { return shared->closed || control->canceled.load(std::memory_order_acquire) ||
                (!shared->queue.empty() && shared->queue.front()==control); });
            execute=!shared->closed && !control->canceled.load(std::memory_order_acquire) && lifetime.current();
        }
        bool success=false;
        if (execute) try { work(); success=true; } catch (...) {}
        // Release native leases and work captures on this counted worker.
        std::function<void()> empty; work.swap(empty); empty={};
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            const auto it=std::find(shared->queue.begin(),shared->queue.end(),control);
            if (it!=shared->queue.end()) shared->queue.erase(it);
            if (shared->owner && !shared->closed && lifetime.current() && !control->canceled.load(std::memory_order_acquire))
                QMetaObject::invokeMethod(shared->owner,[shared,id,success] {
                    MLJoinedThreadRunner* owner;
                    { std::lock_guard<std::mutex> lock(shared->mutex); owner=shared->owner; }
                    if (owner) { if (success) owner->completed(id); else {
                        const auto it=owner->m_entries->values.constFind(id);
                        if (it!=owner->m_entries->values.cend()) owner->cancelTask(it->receiver,id);
                    } }
                },Qt::QueuedConnection);
        }
        shared->changed.notify_all();
    },[shared,control] {
        control->canceled.store(true,std::memory_order_release); shared->changed.notify_all();
    },[permit](vlc::addons::AddonStreamLifetime::NetworkTicket ticket) {
        if (!ticket.retain(permit)) throw 0;
    }); } catch (...) {}
    if (!started) {
        cancelTask(receiver,id);
        { std::lock_guard<std::mutex> lock(shared->mutex);
          const auto it=std::find(shared->queue.begin(),shared->queue.end(),control);
          if (it!=shared->queue.end()) shared->queue.erase(it); }
        shared->changed.notify_all(); return 0;
    }
    return id;
}
void MLJoinedThreadRunner::completed(quint64 id)
{
    auto it=m_entries->values.find(id); if (it==m_entries->values.end()) return;
    Entry entry; entry.receiver=it->receiver; entry.control=it->control; entry.destroyed=it->destroyed;
    entry.complete.swap(it->complete); m_entries->values.erase(it); disconnect(entry.destroyed);
    if (!entry.control->canceled.load(std::memory_order_acquire) && m_lifetime.current()) entry.complete(id);
}
