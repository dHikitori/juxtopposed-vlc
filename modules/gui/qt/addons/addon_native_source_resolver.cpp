// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "addon_native_source_resolver.hpp"
#include "addon_source_admissions.hpp"
#include <QMetaObject>
#include <QPointer>
#include <QRegularExpression>
#include <algorithm>
#include <cerrno>
#include <mutex>
#include <new>
#include <vlc_common.h>
#include <vlc_input_policy.h>
#include <vlc_source_backend.h>
#include <vlc_source_owner.h>

namespace vlc::addons {
namespace {
using Job = std::shared_ptr<vlc_source_job_t>;
constexpr unsigned MaximumAdmissionWork=8;
std::atomic_uint admissionWork{0};
struct AdmissionWork {
    ~AdmissionWork() { admissionWork.fetch_sub(1,std::memory_order_release); }
};
std::shared_ptr<AdmissionWork> acquireAdmissionWork()
{
    auto count=admissionWork.load(std::memory_order_relaxed);
    do { if(count>=MaximumAdmissionWork) return {}; }
    while(!admissionWork.compare_exchange_weak(count,count+1,std::memory_order_acq_rel,std::memory_order_relaxed));
    try { return std::make_shared<AdmissionWork>(); }
    catch(...) { admissionWork.fetch_sub(1,std::memory_order_release); throw; }
}
Job hold(vlc_source_job_t* value) { return {vlc_source_job_Hold(value), vlc_source_job_Release}; }
void cancel(const AddonNativeSourceResolver::Cancel& value) noexcept
{ if (value) try { value(); } catch (...) {} }
bool proxy(const QByteArray& uri, vlc_input_policy_t* policy)
{
    if (uri.isEmpty() || uri.size() > 1024 || uri.contains('\0') || !policy) return false;
    static const QRegularExpression syntax(QStringLiteral("^http://127\\.0\\.0\\.1:([1-9][0-9]{0,4})/([A-Za-z0-9_-]{1,128})/([A-Za-z0-9_-]+(?:/[A-Za-z0-9_-]+){0,3})$"));
    const auto match = syntax.match(QString::fromLatin1(uri));
    return match.hasMatch() && match.captured(1).toUInt() <= 65535 && match.captured(3).size() <= 512
        && vlc_input_policy_IsProxy(policy, uri.constData());
}
}
struct AddonNativeSourceResolver::Gate {
    std::mutex mutex;
    AddonStreamLifetime::Handle lifetime;
    AddonNativeSourceResolver* owner = nullptr;
    std::atomic_bool closed{false};
    std::atomic_uint queued{0};
};
struct AddonNativeSourceResolver::Guard {
    Job job;
    std::shared_ptr<vlc_source_backend_t> native;
    std::shared_ptr<Gate> gate;
    AddonStreamLifetime::Handle lifetime;
    Admission admission;
    QString uri;
    QString sourceHint,accountHint;
    quint64 generation = 0;
    std::atomic_int phase{0}; // Pending, issued, or revoked.
    std::shared_ptr<std::atomic_bool> active;
    std::shared_ptr<vlc_input_policy_t> policy;
    bool admissionReady=false;
    bool pending() const {
        return lifetime.current() && phase.load(std::memory_order_acquire)==0 && !gate->closed.load(std::memory_order_acquire)
            && !vlc_source_backend_IsClosed(native.get()) && vlc_source_job_IsPending(job.get());
    }
    bool current() const {
        const auto valid = [this] {
            const auto state = phase.load(std::memory_order_acquire);
            const auto transport = std::atomic_load_explicit(&active, std::memory_order_acquire);
            const auto scope = std::atomic_load_explicit(&policy, std::memory_order_acquire);
            return lifetime.current() && state < 2 && !gate->closed.load(std::memory_order_acquire)
                && !vlc_source_backend_IsClosed(native.get())
                && (!transport || transport->load(std::memory_order_acquire))
                && (!scope || !vlc_input_policy_IsRevoked(scope.get()))
                && (state == 1 || vlc_source_job_IsPending(job.get()));
        };
        return valid() && admission.current(uri, generation) && valid();
    }
};
struct AddonNativeSourceResolver::Pending {
    std::shared_ptr<Guard> guard;
    std::shared_ptr<Prepared> transport;
    Cancel preparation, startup;
    bool starting = false;
    std::atomic_bool preparedDelivered{false}, startDelivered{false};
};
struct AddonNativeSourceResolver::Issued {
    std::shared_ptr<Guard> guard;
    std::shared_ptr<Prepared> transport;
};
bool AddonNativeSourceResolver::Admission::current(const QString& uri, quint64 generation) const
{
    try { return lifetime.current() && consent && authorization.uri() == uri && authorization.generation() == generation
        && authorization.liveToken().current() && configurationLive && configurationLive()
        && authorization.liveToken().current(); } catch (...) { return false; }
}
bool AddonNativeSourceResolver::Admission::authorize(const QString& uri, quint64 generation) const
{
    try { return current(uri, generation) && configurationCurrent && configurationCurrent()
        && consent->current(authorization, uri, generation) && current(uri, generation); }
    catch (...) { return false; }
}
std::unique_ptr<AddonNativeSourceResolver::Prepared> AddonNativeSourceResolver::Prepared::adopt(
    QByteArray uri, vlc_input_policy_t* policy, vlc_source_lease_t* lease,
    std::shared_ptr<std::atomic_bool> active, StartIo start)
{
    auto value = std::unique_ptr<Prepared>(new (std::nothrow) Prepared);
    if (!value) {
        if (active) active->store(false, std::memory_order_release);
        vlc_input_policy_Revoke(policy); vlc_input_policy_Release(policy);
        if (lease) { vlc_source_lease_Cancel(lease); vlc_source_lease_Release(lease); }
        return {};
    }
    value->m_uri = std::move(uri); value->m_policy = policy; value->m_lease = lease;
    value->m_active = std::move(active); value->m_start = std::move(start); return value;
}
void AddonNativeSourceResolver::Prepared::revoke() noexcept
{
    if (m_revoked.exchange(true, std::memory_order_acq_rel)) return;
    if (m_active) m_active->store(false, std::memory_order_release);
    vlc_input_policy_Revoke(m_policy);
    if (m_lease) vlc_source_lease_Cancel(m_lease);
}
AddonNativeSourceResolver::Prepared::~Prepared()
{ revoke(); vlc_input_policy_Release(m_policy); if (m_lease) vlc_source_lease_Release(m_lease); }
bool AddonNativeSourceResolver::post(const std::shared_ptr<Gate>& gate, std::function<void(AddonNativeSourceResolver*)> call) noexcept
{
    try {
    struct Permit {
        std::shared_ptr<Gate> gate;
        ~Permit() { if (gate) gate->queued.fetch_sub(1, std::memory_order_release); }
    };
    auto permit = std::make_shared<Permit>();
    auto queued = gate->queued.load(std::memory_order_relaxed);
    do { if (queued >= 256) return false; }
    while (!gate->queued.compare_exchange_weak(queued, queued + 1, std::memory_order_acq_rel, std::memory_order_relaxed));
    permit->gate = gate;
    std::lock_guard<std::mutex> lock(gate->mutex); auto* owner = gate->owner;
    return owner && gate->lifetime.current() && !gate->closed.load(std::memory_order_acquire)
        && QMetaObject::invokeMethod(owner, [owner, call = std::move(call), permit = std::move(permit)] { call(owner); }, Qt::QueuedConnection);
    } catch (...) { return false; }
}
int AddonNativeSourceResolver::nativeDispatch(void* opaque, vlc_source_job_t* job)
{
    try {
        const auto gate = *static_cast<std::shared_ptr<Gate>*>(opaque);
        if (!vlc_source_job_IsPending(job)) return -EINTR;
        return post(gate, [job = hold(job)](auto* owner) { owner->dispatch(job); }) ? VLC_SUCCESS : -EINTR;
    } catch (...) { return VLC_ENOMEM; }
}
void AddonNativeSourceResolver::nativeCancel(void* opaque, vlc_source_job_t* job)
{
    try {
        const auto gate = *static_cast<std::shared_ptr<Gate>*>(opaque);
        post(gate, [job = hold(job)](auto* owner) { owner->canceled(job.get()); });
    } catch (...) {}
}
void AddonNativeSourceResolver::nativeDestroy(void* opaque)
{ delete static_cast<std::shared_ptr<Gate>*>(opaque); }
AddonNativeSourceResolver::AddonNativeSourceResolver(vlc_object_t* instance, Backend backend, QObject* parent)
    : AddonNativeSourceResolver(instance, std::move(backend), Limits{}, parent) {}
AddonNativeSourceResolver::AddonNativeSourceResolver(vlc_object_t* instance, Backend backend, Limits limits, QObject* parent)
    : QObject(parent), m_backend(std::move(backend)), m_limits(limits), m_gate(std::make_shared<Gate>())
{
    m_limits.pending = std::clamp(m_limits.pending, 1u, 64u); m_limits.issued = std::clamp(m_limits.issued, 1u, 64u);
    m_gate->owner = this; m_gate->lifetime=m_backend.lifetime;
    if (!instance || (!m_backend.admit && !m_backend.captureAdmission) || !m_backend.prepare
        || !m_backend.lifetime.current()) { close(); return; }
    auto* opaque = new std::shared_ptr<Gate>(m_gate);
    static const vlc_source_backend_operations operations{nativeDispatch, nativeCancel, nativeDestroy};
    auto* native = vlc_source_backend_New(&operations, opaque);
    if (!native) { delete opaque; close(); return; }
    m_native.reset(native, vlc_source_backend_Release);
    m_bound = vlc_source_owner_Set(instance, m_native.get()) == VLC_SUCCESS;
    if (!m_bound) close();
}
AddonNativeSourceResolver::~AddonNativeSourceResolver() { close(); }
bool AddonNativeSourceResolver::bound() const
{ return m_bound && m_backend.lifetime.current() && !m_gate->closed.load(std::memory_order_acquire) && m_native && !vlc_source_backend_IsClosed(m_native.get()); }
bool AddonNativeSourceResolver::prune()
{
    std::vector<Issued> discarded;
    std::vector<std::shared_ptr<Pending>> canceledJobs;
    for (auto it = m_jobs.begin(); it != m_jobs.end();) {
        if (!vlc_source_job_IsPending(it->first)) {
            it->second->guard->phase.store(2, std::memory_order_release); canceledJobs.push_back(it->second); it = m_jobs.erase(it);
        } else ++it;
    }
    for (auto it = m_issued.begin(); it != m_issued.end();) {
        if (it->guard->phase.load(std::memory_order_acquire) == 2 || !it->transport->m_active->load(std::memory_order_acquire)
            || vlc_input_policy_IsRevoked(it->transport->m_policy)) {
            it->guard->phase.store(2, std::memory_order_release); discarded.push_back(std::move(*it)); it = m_issued.erase(it);
        } else ++it;
    }
    const QPointer<AddonNativeSourceResolver> owner(this);
    for (const auto& value : canceledJobs) {
        cancel(value->preparation); cancel(value->startup); if (value->transport) value->transport->revoke();
    }
    for (const auto& value : discarded) value.transport->revoke();
    return owner && !m_gate->closed.load(std::memory_order_acquire);
}
unsigned AddonNativeSourceResolver::issuedCount()
{ return prune() ? unsigned(m_issued.size()) : 0; }
unsigned AddonNativeSourceResolver::admissionWorkCount() noexcept
{ return admissionWork.load(std::memory_order_acquire); }
unsigned AddonNativeSourceResolver::admissionWorkLimit() noexcept
{ return MaximumAdmissionWork; }
void AddonNativeSourceResolver::dispatch(Job job)
{
    if (!prune() || !vlc_source_job_IsPending(job.get())) return;
    if (m_jobs.size() >= m_limits.pending || m_issued.size() >= m_limits.issued) {
        vlc_source_job_Complete(job.get(), VLC_ENOMEM, nullptr); return;
    }
    const auto* request = vlc_source_job_GetRequest(job.get());
    const auto uri = QString::fromUtf8(request->source_uri); const auto generation = request->input_generation;
    auto pending = std::make_shared<Pending>(); auto guard = std::make_shared<Guard>(); pending->guard = guard;
    guard->job = job; guard->native = m_native; guard->gate = m_gate; guard->lifetime=m_backend.lifetime;
    // Use these untrusted fields only to cancel pending work.
    const auto hint=AddonSourceAdmissions::decodeUntrusted(uri);
    guard->sourceHint=hint.value(QStringLiteral("source")).toString(); guard->accountHint=hint.value(QStringLiteral("accountReference")).toString();
    guard->uri = uri; guard->generation = generation; m_jobs.emplace(job.get(), pending);
    const auto gate=m_gate; const auto admit=m_backend.admit;
    const auto capture=m_backend.captureAdmission; const auto lifetime=m_backend.lifetime;
    const QPointer<AddonNativeSourceResolver> owner(this);
    try {
        AdmissionReader reader;
        if (capture) reader=capture(uri,generation);
        else if (admit) reader=[admit,uri,generation] { return admit(uri,generation); };
        if (!owner || !guard->pending()) return;
        if (!reader) { fail(pending,-EACCES); return; }
        const auto work=acquireAdmissionWork(); if(!work) { fail(pending,VLC_ENOMEM); return; }
        const bool accepted=lifetime.startQtWork([pending,guard,gate,reader=std::move(reader)] {
            if(!guard->pending()) return;
            std::optional<Admission> admission;
            try { admission=reader(); } catch(...) {}
            if(!guard->pending()) return;
            try {
                if(!post(gate,[pending,admission=std::move(admission)](auto* target) mutable { target->admitted(pending,std::move(admission)); }))
                    vlc_source_job_Cancel(guard->job.get());
            } catch(...) { vlc_source_job_Cancel(guard->job.get()); }
        },[job=guard->job] {
            // Keep Qt values out of the native cancellation and joiner threads.
            vlc_source_job_Cancel(job.get());
        },[work](auto ticket) {
            if(!ticket.retain(work)) throw std::bad_alloc();
        });
        if(!accepted && owner) owner->fail(pending,VLC_ENOMEM);
    } catch(...) { if (owner) owner->fail(pending,VLC_ENOMEM); }
}
void AddonNativeSourceResolver::admitted(const std::shared_ptr<Pending>& pending,std::optional<Admission> admission)
{
    const auto guard=pending->guard; const auto job=guard->job;
    const auto found=m_jobs.find(job.get()); if(found==m_jobs.end() || found->second!=pending) return;
    if(admission) admission->lifetime=m_backend.lifetime;
    if(!bound() || !guard->pending() || !admission || !admission->current(guard->uri,guard->generation)) {
        fail(pending,-EACCES); return;
    }
    guard->admission=std::move(*admission); guard->admissionReady=true;
    const QPointer<AddonNativeSourceResolver> owner(this);
    const auto prepare = m_backend.prepare; const auto gate = m_gate;
    const std::weak_ptr<Pending> weakPending = pending; const std::weak_ptr<Guard> weakGuard = guard;
    Cancel cancellation;
    try {
        cancellation = prepare(guard->admission, [weakGuard] { const auto guard = weakGuard.lock(); return guard && guard->current(); }, [gate, weakPending](std::unique_ptr<Prepared> value) {
            const auto pending = weakPending.lock(); if (!pending) return;
            try {
                if (pending->preparedDelivered.exchange(true, std::memory_order_acq_rel)) return;
                auto payload = std::make_shared<std::unique_ptr<Prepared>>(std::move(value));
                if (!post(gate, [pending, payload](auto* target) { target->prepared(pending, std::move(*payload)); }))
                    vlc_source_job_Cancel(pending->guard->job.get());
            } catch (...) { vlc_source_job_Cancel(pending->guard->job.get()); }
        });
    } catch (...) {
        if (owner) fail(pending, VLC_EGENERIC);
    }
    if (!owner || m_gate->closed.load(std::memory_order_acquire) || m_jobs.find(job.get()) == m_jobs.end()) cancel(cancellation);
    else pending->preparation = std::move(cancellation);
}
void AddonNativeSourceResolver::prepared(const std::shared_ptr<Pending>& pending, std::unique_ptr<Prepared> value)
{
    const auto job = pending->guard->job;
    if (m_jobs.find(job.get()) == m_jobs.end() || pending->starting) return;
    if (!pending->guard->current() || !value || !value->m_lease || !value->m_active || !value->m_start
        || !value->m_active->load(std::memory_order_acquire) || !proxy(value->m_uri, value->m_policy)) {
        fail(pending, -EACCES); return;
    }
    const QPointer<AddonNativeSourceResolver> owner(this);
    const bool attached = vlc_source_job_AttachTransport(job.get(), value->m_policy, value->m_lease);
    if (!owner) return;
    if (!attached || !pending->guard->current()) { fail(pending, -EINTR); return; }
    pending->starting = true; pending->transport = std::move(value);
    std::atomic_store_explicit(&pending->guard->active, pending->transport->m_active, std::memory_order_release);
    std::atomic_store_explicit(&pending->guard->policy,
        std::shared_ptr<vlc_input_policy_t>(vlc_input_policy_Hold(pending->transport->m_policy), vlc_input_policy_Release), std::memory_order_release);
    const auto start = pending->transport->m_start; const auto guard = pending->guard; const auto gate = m_gate;
    const std::weak_ptr<Pending> weakPending = pending; const std::weak_ptr<Guard> weakGuard = guard;
    Cancel cancellation;
    try {
        cancellation = start([weakGuard] { const auto guard = weakGuard.lock(); return guard && guard->current(); }, [gate, weakPending](int status) {
            const auto pending = weakPending.lock(); if (!pending) return;
            try {
                if (pending->startDelivered.exchange(true, std::memory_order_acq_rel)) return;
                if (!post(gate, [pending, status](auto* target) { target->started(pending, status); }))
                    vlc_source_job_Cancel(pending->guard->job.get());
            } catch (...) { vlc_source_job_Cancel(pending->guard->job.get()); }
        });
    } catch (...) {
        if (owner) fail(pending, VLC_EGENERIC);
    }
    if (!owner || m_gate->closed.load(std::memory_order_acquire) || m_jobs.find(job.get()) == m_jobs.end()) cancel(cancellation);
    else pending->startup = std::move(cancellation);
}
void AddonNativeSourceResolver::started(const std::shared_ptr<Pending>& pending, int status)
{
    const auto guard = pending->guard; const auto job = guard->job;
    if (m_jobs.find(job.get()) == m_jobs.end() || !pending->starting) return;
    if (status != VLC_SUCCESS || !guard->current()) { fail(pending, status == VLC_SUCCESS ? -EACCES : status); return; }
    if (!prune()) return;
    if (m_jobs.find(job.get()) == m_jobs.end()) return;
    if (!guard->current()) { fail(pending, -EACCES); return; }
    if (m_issued.size() >= m_limits.issued) { fail(pending, VLC_ENOMEM); return; }
    const auto transport = pending->transport;
    m_jobs.erase(job.get()); m_issued.push_back({guard, transport}); guard->phase.store(1, std::memory_order_release);
    const vlc_source_result result{transport->m_uri.constData(), transport->m_policy, transport->m_lease};
    if (!vlc_source_job_Complete(job.get(), VLC_SUCCESS, &result)) { guard->phase.store(2, std::memory_order_release); transport->revoke(); }
}
void AddonNativeSourceResolver::fail(const std::shared_ptr<Pending>& pending, int status)
{
    const auto job = pending->guard->job;
    if (!m_jobs.erase(job.get())) return;
    pending->guard->phase.store(2, std::memory_order_release);
    const auto preparation = std::move(pending->preparation), startup = std::move(pending->startup);
    const auto transport = pending->transport;
    vlc_source_job_Complete(job.get(), status, nullptr);
    cancel(preparation); cancel(startup); if (transport) transport->revoke();
}
void AddonNativeSourceResolver::canceled(vlc_source_job_t* native)
{
    const auto found = m_jobs.find(native); if (found == m_jobs.end()) return;
    const auto pending = found->second; m_jobs.erase(found); pending->guard->phase.store(2, std::memory_order_release);
    const auto preparation = std::move(pending->preparation), startup = std::move(pending->startup);
    const auto transport = pending->transport;
    cancel(preparation); cancel(startup); if (transport) transport->revoke();
}
void AddonNativeSourceResolver::withdraw(const AddonSourceConsent::Binding& binding)
{
    const auto canonical = AddonSourceConsent::canonical(binding); if (!canonical) return;
    std::vector<std::shared_ptr<Pending>> pending; std::vector<Issued> issued;
    for (auto it = m_jobs.begin(); it != m_jobs.end();) {
        const auto& guard=*it->second->guard;
        const bool match=guard.admissionReady ? guard.admission.authorization.binding()==*canonical
            : guard.sourceHint==canonical->contribution && guard.accountHint==canonical->accountReference;
        if (match) {
            it->second->guard->phase.store(2, std::memory_order_release); pending.push_back(it->second); it = m_jobs.erase(it);
        } else ++it;
    }
    for (auto it = m_issued.begin(); it != m_issued.end();) {
        if (it->guard->admission.authorization.binding() == *canonical) {
            it->guard->phase.store(2, std::memory_order_release); issued.push_back(std::move(*it)); it = m_issued.erase(it);
        } else ++it;
    }
    for (const auto& value : pending) {
        vlc_source_job_Cancel(value->guard->job.get()); cancel(value->preparation); cancel(value->startup);
        if (value->transport) value->transport->revoke();
    }
    for (const auto& value : issued) value.transport->revoke();
}
void AddonNativeSourceResolver::close()
{
    const auto gate = m_gate;
    { std::lock_guard<std::mutex> lock(gate->mutex); gate->owner = nullptr; gate->closed.store(true, std::memory_order_release); }
    auto jobs = std::move(m_jobs); auto issued = std::move(m_issued); const auto native = m_native;
    m_jobs.clear(); m_issued.clear(); m_bound = false;
    for (const auto& value : jobs) value.second->guard->phase.store(2, std::memory_order_release);
    for (const auto& value : issued) value.guard->phase.store(2, std::memory_order_release);
    if (native) vlc_source_backend_Close(native.get());
    for (const auto& value : jobs) { cancel(value.second->preparation); cancel(value.second->startup); if (value.second->transport) value.second->transport->revoke(); }
    for (const auto& value : issued) value.transport->revoke();
}
}
