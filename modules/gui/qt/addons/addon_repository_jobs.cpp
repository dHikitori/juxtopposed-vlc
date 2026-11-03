// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_repository_jobs.hpp"
#include "addon_repository_cancellation.hpp"
#include <QThread>
#include <atomic>
#include <mutex>

namespace vlc::addons {
thread_local std::shared_ptr<const std::atomic_bool> RepositoryCancellation::s_current;
namespace {
std::atomic_uint processJobs{0};
NativeNotifications& processAvailability()
{static NativeNotifications notices;return notices;}
void releaseProcess() noexcept
{processJobs.fetch_sub(1,std::memory_order_acq_rel);processAvailability().notify();}
struct Permit { ~Permit() { releaseProcess(); } };
std::shared_ptr<Permit> acquire(AddonRepositoryJobs::Refusal* refusal)
{
    auto count=processJobs.load(std::memory_order_acquire);
    while (count<AddonRepositoryJobs::ProcessLimit) {
        if (!processJobs.compare_exchange_weak(count,count+1,std::memory_order_acq_rel)) continue;
        try { return std::make_shared<Permit>(); }
        catch (...) { releaseProcess();if(refusal)*refusal=AddonRepositoryJobs::Refusal::ResourceFailure;return {}; }
    }
    if(refusal)*refusal=AddonRepositoryJobs::Refusal::ProcessQuota;
    return {};
}
}
struct AddonRepositoryJobs::Gate {
    std::mutex mutex;
    AddonRepositoryJobs* owner=nullptr;
    bool closed=false;
};
struct AddonRepositoryJobs::Job {
    const std::shared_ptr<std::atomic_bool> canceled=std::make_shared<std::atomic_bool>(false);
};
AddonRepositoryJobs::AddonRepositoryJobs(AddonStreamLifetime::Handle lifetime,QObject* parent)
    :QObject(parent),m_lifetime(std::move(lifetime)),m_gate(std::make_shared<Gate>()) { m_gate->owner=this; }
AddonRepositoryJobs::~AddonRepositoryJobs() { close(); }
unsigned AddonRepositoryJobs::processCount() noexcept { return processJobs.load(std::memory_order_acquire); }
NativeNotification AddonRepositoryJobs::onProcessAvailable(std::function<void()> nativeWake) noexcept
{return processAvailability().subscribe(std::move(nativeWake));}
void AddonRepositoryJobs::cancel() noexcept
{ if (m_job) m_job->canceled->store(true,std::memory_order_release); }
void AddonRepositoryJobs::close()
{
    Q_ASSERT(QThread::currentThread()==thread());
    { std::lock_guard<std::mutex> lock(m_gate->mutex); m_gate->closed=true; m_gate->owner=nullptr; }
    cancel(); m_job.reset(); Complete empty; m_complete.swap(empty);
}
bool AddonRepositoryJobs::submit(Work work,Complete complete)
{return submit(std::move(work),std::move(complete),nullptr);}
bool AddonRepositoryJobs::submit(Work work,Complete complete,Refusal* refusal)
{
    Q_ASSERT(QThread::currentThread()==thread());
    const auto refuse=[&](Refusal reason){if(refusal)*refusal=reason;return false;};
    if(refusal)*refusal=Refusal::None;
    if(!work || !complete)return refuse(Refusal::Invalid);
    if(m_job)return refuse(Refusal::Busy);
    if(!m_lifetime.current())return refuse(Refusal::Closed);
    const auto gate=m_gate;
    {std::lock_guard<std::mutex> lock(gate->mutex);if(gate->closed)return refuse(Refusal::Closed);}
    const auto permit=acquire(refusal);if(!permit)return false;
    std::shared_ptr<Job> job;
    try {job=std::make_shared<Job>();}catch(...){return refuse(Refusal::ResourceFailure);}
    const auto lifetime=m_lifetime;
    m_job=job; m_complete.swap(complete);
    bool started=false;
    try { started=lifetime.startQtWork([gate,job,lifetime,work=std::move(work)]() mutable {
        RepositoryCancellation cancellation(job->canceled);
        bool failed=false;
        if (!job->canceled->load(std::memory_order_acquire) && lifetime.current())
            try { work(); } catch (...) { failed=true; }
        Work empty; work.swap(empty); empty={};
        std::lock_guard<std::mutex> lock(gate->mutex);
        if (!gate->closed && gate->owner && lifetime.current())
            QMetaObject::invokeMethod(gate->owner,[gate,job,failed] {
                AddonRepositoryJobs* owner;
                { std::lock_guard<std::mutex> lock(gate->mutex); owner=gate->owner; }
                if (owner) owner->completed(job,failed);
            },Qt::QueuedConnection);
    },[job] { job->canceled->store(true,std::memory_order_release); },
    [permit](AddonStreamLifetime::NetworkTicket ticket) { if (!ticket.retain(permit)) throw 0; }); }
    catch (...) {}
    if (started) return true;
    job->canceled->store(true,std::memory_order_release); m_job.reset();
    Complete empty;m_complete.swap(empty);return refuse(Refusal::QtAdmissionFailure);
}
void AddonRepositoryJobs::completed(const std::shared_ptr<Job>& job,bool failed)
{
    if (m_job!=job) return;
    m_job.reset(); Complete complete; m_complete.swap(complete);
    if (complete && m_lifetime.current())
        complete(job->canceled->load(std::memory_order_acquire) ? Result::Canceled : failed ? Result::Failed : Result::Finished);
}
}
