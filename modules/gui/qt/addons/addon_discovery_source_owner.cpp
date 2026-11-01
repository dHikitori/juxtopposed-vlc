// SPDX-License-Identifier: GPL-2.0-or-later
#include <algorithm>
#include <climits>
#include "addon_discovery_source_owner.hpp"
#include "addon_native_wake.hpp"
#include "addon_manager.hpp"
#include "addon_source_admissions.hpp"
#include "addon_queue_transactions.hpp"
#include "addon_player_discovery.hpp"
#include "addon_discovery_service.hpp"
#include "addon_stream_proxy_native.hpp"
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSslSocket>
#include <QThread>
#include <QUuid>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>

namespace vlc::addons {
namespace {
std::atomic_uint captureWork{0},controlWork{0};
struct SourceWorkPermit {
    std::atomic_uint* counter=nullptr;
    ~SourceWorkPermit() { if(counter) counter->fetch_sub(1,std::memory_order_release); }
};
std::shared_ptr<SourceWorkPermit> workPermit(bool control) {
    const auto permit=std::make_shared<SourceWorkPermit>(); auto& counter=control?controlWork:captureWork;
    auto count=counter.load(std::memory_order_relaxed); const unsigned limit=control?4:8;
    do { if(count>=limit) return {}; }
    while(!counter.compare_exchange_weak(count,count+1,std::memory_order_acq_rel,std::memory_order_relaxed));
    permit->counter=&counter; return permit;
}
QString requestId() { return QStringLiteral("hakone-source:")+QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool current(const std::function<bool()>& check) noexcept { try { return check && check(); } catch(...) { return false; } }
QString sourceKind(AddonSourceConsent::SourceKind kind) {
    switch(kind) {
    case AddonSourceConsent::SourceKind::Git:return QStringLiteral("git");
    case AddonSourceConsent::SourceKind::Developer:return QStringLiteral("developer");
    case AddonSourceConsent::SourceKind::Distribution:return QStringLiteral("distribution");
    }
    return {};
}
bool sourceId(const QString& value) {
    static const QRegularExpression syntax(QStringLiteral("^[a-z][a-z0-9.-]{1,127}/[a-z][a-z0-9.-]{0,63}\\z"));
    return syntax.match(value).hasMatch();
}
}
struct AddonDiscoverySourceOwner::NativeStop {
    enum class Transport { Unknown, Ready, Unavailable };
    std::atomic_bool open{true};
    std::atomic<Transport> transport{Transport::Unknown};
    std::mutex mutex;
    std::condition_variable ended;
    std::shared_ptr<const std::function<void()>> retire;
    std::function<void()> wake;
    bool transportReady() const noexcept {
        return open.load(std::memory_order_acquire) && transport.load(std::memory_order_acquire)==Transport::Ready;
    }
    void close() noexcept {
        open.store(false,std::memory_order_release);
        std::shared_ptr<const std::function<void()>> cancel;
        { std::lock_guard<std::mutex> lock(mutex); cancel=retire; }
        try { if(cancel && *cancel) (*cancel)(); } catch(...) {}
        ended.notify_all();
        try { if(wake) wake(); } catch(...) {}
    }
    bool install(std::function<void()> cancel) {
        auto value=std::make_shared<const std::function<void()>>(std::move(cancel));
        std::lock_guard<std::mutex> lock(mutex);
        if(!open.load(std::memory_order_acquire)) return false;
        retire=std::move(value); return true;
    }
};
struct AddonDiscoverySourceOwner::Shared {
    struct Entry { AddonSourceConsent::Binding binding; AddonNativeAuthority authority; };
    QString directory;
    std::shared_ptr<NativeStop> stop;
    std::shared_ptr<AddonSourceAdmissions> records;
    std::shared_ptr<AddonSourceConsent> consent;
    AddonSourceConsent::DisplayChanges changes;
    std::function<void()> reset;
    std::mutex mutex;
    QHash<QString,Entry> entries;
    // Call only from explicit joined review/capture or primary-open admission.
    // No Store or native backend lock is held during the platform probe.
    bool probeTransport() {
        if(!stop->open.load(std::memory_order_acquire)) return false;
        bool ready=false;
        try { ready=QSslSocket::supportsSsl(); } catch(...) {}
        if(!stop->open.load(std::memory_order_acquire)) return false;
        const auto next=ready?NativeStop::Transport::Ready:NativeStop::Transport::Unavailable;
        const auto previous=stop->transport.exchange(next,std::memory_order_acq_rel);
        if(previous!=next && stop->wake) stop->wake();
        return ready;
    }
    bool put(const AddonSourceConsent::Binding& binding,const AddonNativeAuthority& authority) {
        std::optional<Entry> previous;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if(!stop->open.load(std::memory_order_acquire) || (entries.size()>=128 && !entries.contains(binding.contribution))) return false;
            if(entries.contains(binding.contribution)) previous=entries.take(binding.contribution);
            entries.insert(binding.contribution,{binding,authority});
        }
        return true;
    }
    bool current(const AddonSourceConsent::Binding& binding) {
        std::optional<Entry> entry;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if(!stop->open.load(std::memory_order_acquire)) return false;
            const auto found=entries.constFind(binding.contribution);
            if(found!=entries.cend()) entry=*found;
        }
        return entry && entry->binding==binding && vlc::addons::current(entry->authority.live)
            && vlc::addons::current(entry->authority.current) && stop->open.load(std::memory_order_acquire);
    }
    std::shared_ptr<AddonSourceConsent> approvals() {
        std::lock_guard<std::mutex> lock(mutex); return consent;
    }
};
struct AddonDiscoverySourceOwner::Gate {
    std::mutex mutex;
    AddonDiscoverySourceOwner* owner=nullptr;
    AddonStreamLifetime::Handle lifetime;
    std::atomic_uint queued{0};
};
struct AddonDiscoverySourceOwner::SourceCapture {
    AddonNativeAuthority authority;
    AddonSourceConsent::Binding binding;
    std::optional<AddonSourceConsent::Ticket> ticket;
    std::optional<AddonSourceConsent::LiveToken> approved;
    std::function<void()> revoke;
    QVariantMap descriptor;
};
struct AddonDiscoverySourceOwner::Review {
    std::shared_ptr<SourceCapture> capture;
    std::chrono::steady_clock::time_point expires=std::chrono::steady_clock::now()+std::chrono::seconds(30);
};
struct AddonDiscoverySourceOwner::Cache {
    std::shared_ptr<SourceCapture> capture;
    AddonSourceConsent::DisplayChanges approvalChanges;
    bool available=false;
};
struct AddonDiscoverySourceOwner::Resolve {
    Admission admission;
    Current current;
    Prepared done;
    QString invocation;
    Cancel transport;
    std::chrono::steady_clock::time_point deadline;
};
bool AddonDiscoverySourceOwner::post(const std::shared_ptr<Gate>& gate,std::function<void(AddonDiscoverySourceOwner*)> call) {
    try {
    struct Permit {
        std::shared_ptr<Gate> gate;
        ~Permit() { if(gate) gate->queued.fetch_sub(1,std::memory_order_release); }
    };
    const auto permit=std::make_shared<Permit>();
    auto count=gate->queued.load(std::memory_order_relaxed);
    do { if(count>=128) return false; }
    while(!gate->queued.compare_exchange_weak(count,count+1,std::memory_order_acq_rel,std::memory_order_relaxed));
    permit->gate=gate;
    std::lock_guard<std::mutex> lock(gate->mutex); auto* owner=gate->owner;
    return owner && gate->lifetime.current() && QMetaObject::invokeMethod(owner,[owner,permit,call=std::move(call)] { call(owner); },Qt::QueuedConnection);
    } catch(...) { return false; }
}
std::optional<AddonSourceConsent::Binding> AddonDiscoverySourceOwner::binding(const AddonNativeAuthority& authority) {
    const auto& package=authority.package; const auto& source=authority.contribution;
    const auto profile=QStringLiteral("public-https-media@1");
    if(!package.enabled || package.recoveryRequired || source.type!=QLatin1String("discovery.source")
        || source.contract!=QLatin1String("discovery@1") || !source.operations.contains(QStringLiteral("resolve"))
        || authority.globalId!=package.manifest.id+QLatin1Char('/')+source.id
        || !package.manifest.permissionScope(QStringLiteral("media.stream")).value("profiles").toArray().contains(profile)
        || !package.grants.value("media.stream").toObject().value("profiles").toArray().contains(profile)
        || !current(authority.live)) return {};
    AddonSourceConsent::Binding result;
    result.kind=package.identity.distributionSource?AddonSourceConsent::SourceKind::Distribution
        :package.identity.developerSource?AddonSourceConsent::SourceKind::Developer:AddonSourceConsent::SourceKind::Git;
    result.identity=package.identity.source;
    if(result.kind==AddonSourceConsent::SourceKind::Developer && QDir::isAbsolutePath(result.identity))
        result.identity=QString::fromLatin1(QUrl::fromLocalFile(result.identity).toEncoded(QUrl::FullyEncoded));
    result.contribution=authority.globalId; result.accountReference=authority.accountReference; result.profile=profile;
    return AddonSourceConsent::canonical(result);
}
bool AddonDiscoverySourceOwner::admitted(const std::shared_ptr<AddonSourceAdmissions>& records,const AddonSourceConsent::Binding& binding,const QString& uri) {
    const auto reference=records?records->admittedReference(uri):QVariantMap{};
    return !reference.isEmpty() && reference.value("source").toString()==binding.contribution
        && reference.value("accountReference").toString()==binding.accountReference;
}
AddonDiscoverySourceOwner::AddonDiscoverySourceOwner(AddonManager* manager,vlc_object_t* instance,
    std::shared_ptr<AddonSourceAdmissions> records,AddonStreamLifetime::Handle lifetime,QObject* parent)
    :QObject(parent),m_manager(manager),m_instance(instance),m_lifetime(std::move(lifetime)),
      m_stop(std::make_shared<NativeStop>()),m_shared(std::make_shared<Shared>()),m_gate(std::make_shared<Gate>())
{
    m_shared->directory=manager?manager->storageDirectory():QString{}; m_shared->records=std::move(records); m_shared->stop=m_stop;
    m_gate->owner=this; m_gate->lifetime=m_lifetime; m_prepareTransport=prepareContainedSource;
    m_wake=std::make_unique<AddonNativeWake>([this] {
        if(m_starting || m_closing) return;
        const QPointer<AddonDiscoverySourceOwner> alive(this);
        tick();
        // A changed native subject must invalidate an open review even if
        // its cached playback availability was already false.
        if(alive && !m_closing) notify();
    },this);
    m_stop->wake=m_wake->nativeCallback();
    m_poll.setSingleShot(true);
    connect(&m_poll,&QTimer::timeout,this,&AddonDiscoverySourceOwner::tick);
    if(manager) {
        connect(manager,&AddonManager::retiring,this,&AddonDiscoverySourceOwner::close,Qt::DirectConnection);
        connect(manager,&QObject::destroyed,this,&AddonDiscoverySourceOwner::close);
        connect(manager,&AddonManager::operationFinished,this,&AddonDiscoverySourceOwner::resolved);
    }
}
AddonDiscoverySourceOwner::~AddonDiscoverySourceOwner() { close(); }
bool AddonDiscoverySourceOwner::active() const {
    return !m_closing && m_lifetime.current() && m_stop->open.load(std::memory_order_acquire) && m_resolver && m_resolver->bound();
}
bool AddonDiscoverySourceOwner::transportKnown() const {
    return active() && m_stop->transport.load(std::memory_order_acquire)!=NativeStop::Transport::Unknown;
}
bool AddonDiscoverySourceOwner::transportAvailable() const {
    return active() && m_stop->transportReady();
}
void AddonDiscoverySourceOwner::notify() {
    scheduleMaintenance();
    if(m_noticeQueued) return; m_noticeQueued=true;
    QTimer::singleShot(0,this,[this] { m_noticeQueued=false; emit changed(); });
}
bool AddonDiscoverySourceOwner::activate() {
    Q_ASSERT(QThread::currentThread()==thread());
    if(m_closing || m_starting || active() || !m_manager || !m_instance || !m_shared->records || !m_lifetime.current()) return false;
    if(!m_wake->valid()) { m_error=tr("The app cannot receive source playback changes."); notify(); return false; }
    m_starting=true; const auto shared=m_shared; const auto gate=m_gate; const auto stop=m_stop;
    const bool accepted=m_lifetime.startQtWork([shared,gate,stop] {
        std::shared_ptr<AddonSourceConsent> consent;
        try {
            const std::weak_ptr<Shared> weak=shared;
            AddonSourceConsent::Options options; options.ticketLifetimeMs=30000;
            consent=std::make_shared<AddonSourceConsent>(shared->directory,AddonSourceConsent::Checks{
                [weak](const auto& binding) { const auto state=weak.lock(); return state && state->current(binding); },
                [records=shared->records](const auto& binding,const auto& uri) { return admitted(records,binding,uri); }},options);
            // Shared package/owner/profile display notifications are retained
            // once. Each cache entry adds only its own binding/receipt wake.
            const auto changes=consent->displayChanges(stop->wake);
            if(!changes) {
                post(gate,[](auto* owner) { owner->initialized(false,tr("The app cannot watch source playback changes.")); });
                stop->close(); return;
            }
            if(stop->install(consent->nativeOwnerRevoker())) {
                { std::lock_guard<std::mutex> lock(shared->mutex); shared->consent=consent; shared->changes=changes; shared->reset=consent->nativeProfileRevoker(); }
                if(!post(gate,[](auto* owner) { owner->initialized(true,{}); })) stop->close();
            } else consent->retire();
        } catch(...) {
            post(gate,[](auto* owner) { owner->initialized(false,tr("The app cannot prepare source playback.")); });
            stop->close();
        }
        {
            std::unique_lock<std::mutex> lock(stop->mutex);
            stop->ended.wait(lock,[&] { return !stop->open.load(std::memory_order_acquire); });
        }
        QHash<QString,Shared::Entry> entries; std::shared_ptr<AddonSourceConsent> retired; AddonSourceConsent::DisplayChanges changes; std::function<void()> reset;
        { std::lock_guard<std::mutex> lock(shared->mutex); entries.swap(shared->entries); retired.swap(shared->consent); changes=std::move(shared->changes); shared->changes={}; reset.swap(shared->reset); }
        if(consent) consent->close();
    },[stop] { stop->close(); });
    if(!accepted) { m_starting=false; m_error=tr("The app cannot start source playback."); notify(); }
    return accepted;
}
void AddonDiscoverySourceOwner::initialized(bool success,const QString& error) {
    m_starting=false;
    if(m_closing) return;
    if(!m_lifetime.current()) { close(); return; }
    if(!success) { m_error=error; close(); return; }
    if(!m_stop->open.load(std::memory_order_acquire)) {
        m_error=tr("Source playback stopped."); close(); return;
    }
    const QPointer<AddonDiscoverySourceOwner> owner(this);
    AddonNativeSourceResolver::Backend backend; backend.lifetime=m_lifetime;
    backend.captureAdmission=[owner](const auto& uri,quint64 generation) {
        return owner?owner->captureAdmission(uri,generation):AddonNativeSourceResolver::AdmissionReader{};
    };
    backend.watchAdmission=[owner](auto cancel) {
        if(!owner || !owner->m_manager || !owner->active()) { if(cancel) cancel(); return StoreAuthorityWatch{}; }
        return owner->m_manager->storeAuthorityToken().onInvalidated(std::move(cancel));
    };
    backend.prepare=[owner](const auto& admission,auto current,auto done) {
        if(!owner) { done({}); return Cancel{}; }
        return owner->resolve(admission,std::move(current),std::move(done));
    };
    m_resolver=std::make_unique<AddonNativeSourceResolver>(m_instance,std::move(backend));
    if(!m_resolver->bound()) { m_error=tr("Another source playback owner is active."); close(); return; }
    notify();
}
QString AddonDiscoverySourceOwner::refreshSource(const QString& contribution) {
    if(m_refreshes.contains(contribution)) return m_refreshes.value(contribution);
    return captureSource(contribution,false);
}
QString AddonDiscoverySourceOwner::requestApproval(const QString& contribution) { return captureSource(contribution,true); }
QString AddonDiscoverySourceOwner::captureSource(const QString& contribution,bool review) {
    Q_ASSERT(QThread::currentThread()==thread());
    if(!active() || !m_manager || !sourceId(contribution) || (review?m_reviews.size():m_refreshes.size())>=16) return {};
    const auto permit=workPermit(false); if(!permit) { m_error=tr("Wait for the current source request to end."); notify(); return {}; }
    const auto reader=m_manager->nativeAuthorityReader(contribution); if(!reader) return {};
    const auto id=requestId(); const auto shared=m_shared; const auto gate=m_gate;
    if(review) m_reviews.insert(id,std::make_shared<Review>()); else m_refreshes.insert(contribution,id);
    scheduleMaintenance();
    const bool accepted=m_lifetime.startQtWork([reader,shared,gate,id,contribution,review] {
        std::shared_ptr<SourceCapture> capture; QString error;
        try {
            if(shared->stop->open.load(std::memory_order_acquire)) {
                const auto authority=reader(); const auto consent=shared->approvals();
                const auto key=authority?binding(*authority):std::nullopt;
                if(authority && consent && key && current(authority->current)) shared->probeTransport();
                // A transport hint does not grant or revoke consent. Review
                // and stable-reference capture remain possible if TLS fails.
                if(authority && consent && key && current(authority->current) && shared->stop->open.load(std::memory_order_acquire)
                    && shared->put(*key,*authority)) {
                    // binding() admits only an enabled package whose media.stream
                    // permission the user accepted. That acceptance approves it.
                    if(!review) consent->approveAccepted(*key,&error);
                    capture=std::make_shared<SourceCapture>(); capture->authority=*authority; capture->binding=*key;
                    capture->revoke=consent->nativeBindingRevoker(*key); capture->approved=consent->approvalToken(*key);
                    if(review) capture->ticket=consent->capture(*key,&error);
                    QVariantMap accountFields; QStringList accountNames;
                    for(const auto& field:authority->package.manifest.accountFields) {
                        const auto value=authority->settings.value(field);
                        if(!value.isString() && !value.isDouble() && !value.isBool()) continue;
                        auto label=value.toVariant().toString();
                        if(!QUrl(label,QUrl::StrictMode).scheme().isEmpty() || label.contains(QStringLiteral("://"))) continue;
                        for(auto& character:label) if(character.category()==QChar::Other_Control || character.category()==QChar::Separator_Line
                            || character.category()==QChar::Separator_Paragraph) character=QLatin1Char(' ');
                        label=label.simplified().left(80); if(label.isEmpty()) continue;
                        accountFields.insert(field,label); if(accountNames.size()<2) accountNames.append(label);
                    }
                    const QString accountLabel=!accountNames.isEmpty()?accountNames.join(QStringLiteral(", "))
                        :key->accountReference.startsWith(QLatin1String("public:"))?tr("No account"):tr("Configured account");
                    capture->descriptor={{"id",contribution},{"name",authority->package.manifest.name},{"source",key->identity},
                        {"kind",sourceKind(key->kind)},{"accountReference",key->accountReference},{"accountLabel",accountLabel},{"accountFields",accountFields},
                        {"profile",key->profile},{"profiles",QStringList{key->profile}},{"publisherVerified",false}};
                    if((review && !capture->ticket) || !current(authority->current)) capture.reset();
                }
            }
        } catch(...) { capture.reset(); }
        if(!capture && error.isEmpty()) error=tr("The source or account is not available. Open its settings and try again.");
        if(!post(gate,[id,contribution,capture=std::move(capture),error](auto* owner) { owner->captured(id,contribution,capture,error); })) shared->stop->close();
    },[] {},[permit](auto ticket) { if(!ticket.retain(permit)) throw std::bad_alloc(); });
    if(!accepted) {
        m_reviews.remove(id); if(m_refreshes.value(contribution)==id) m_refreshes.remove(contribution);
        m_error=tr("The app cannot start the source request."); notify(); return {};
    }
    return id;
}
void AddonDiscoverySourceOwner::captured(const QString& id,const QString& source,std::shared_ptr<SourceCapture> capture,const QString& error) {
    const bool review=m_reviews.contains(id); const bool refresh=m_refreshes.value(source)==id;
    if(!active() || (!review && !refresh)) return;
    if(refresh) m_refreshes.remove(source);
    if(capture && !current(capture->authority.live)) capture.reset();
    if(!capture) {
        m_reviews.remove(id); m_cache.remove(source); m_error=error; notify();
        emit actionFinished(id,false,error); return;
    }
    if(review) {
        if(std::chrono::steady_clock::now()>=m_reviews[id]->expires) { m_reviews.remove(id); return; }
        m_reviews[id]->capture=capture;
    }
    if(m_cache.size()<128 || m_cache.contains(source)) {
        auto cache=std::make_shared<Cache>(); cache->capture=capture;
        if(capture->approved) cache->approvalChanges=capture->approved->entryChanges(m_wake->nativeCallback());
        // Cache entries use independent display subscriptions. They never take
        // slots from the native lease cancellation quota. The cache bound is128.
        cache->available=capture->approved && cache->approvalChanges
            && capture->approved->current() && current(capture->authority.live);
        if(capture->approved && !cache->approvalChanges) {
            const auto error=tr("The app cannot watch this source approval. Try again.");
            m_reviews.remove(id); m_error=error; notify();
            emit actionFinished(id,false,error); return;
        }
        m_cache.insert(source,std::move(cache));
    }
    m_error=m_stop->transport.load(std::memory_order_acquire)==NativeStop::Transport::Unavailable
        ?tr("The app cannot use secure media connections."):QString{};
    notify();
    if(review) emit approvalReady(id);
}
bool AddonDiscoverySourceOwner::canUseSource(const QString& contribution) const {
    if(!active()) return false;
    const auto found=m_cache.constFind(contribution);
    return found!=m_cache.cend() && (*found)->capture && (*found)->capture->approved
        && (*found)->capture->approved->current() && current((*found)->capture->authority.live);
}
bool AddonDiscoverySourceOwner::canUseReference(const QVariantMap& reference) const {
    const auto source=reference.value("source").toString();
    const auto found=m_cache.constFind(source);
    return AddonQueueTransactions::validReference(reference) && canUseSource(source) && found!=m_cache.cend()
        && (*found)->capture->binding.accountReference==reference.value("accountReference").toString();
}
std::function<bool()> AddonDiscoverySourceOwner::referenceGuard(const QVariantList& references) const {
    if(!active() || references.isEmpty() || references.size()>200) return {};
    std::vector<Current> checks; QSet<QString> sources;
    for(const auto& value:references) {
        if(value.metaType().id()!=QMetaType::QVariantMap || !canUseReference(value.toMap())) return {};
        const auto source=value.toMap().value("source").toString(); if(sources.contains(source)) continue;
        sources.insert(source); const auto capture=m_cache.value(source)->capture;
        checks.push_back([approval=*capture->approved,configuration=capture->authority.live] { return approval.current() && current(configuration); });
    }
    const auto native=m_resolver->liveGuard(); const auto stop=m_stop;
    return [native,stop,checks=std::move(checks)] {
        if(!stop->open.load(std::memory_order_acquire) || !current(native)) return false;
        for(const auto& check:checks) if(!current(check)) return false;
        return current(native) && stop->open.load(std::memory_order_acquire);
    };
}
QVariantMap AddonDiscoverySourceOwner::approval(const QString& id) const {
    const auto review=m_reviews.value(id);
    if(!active() || !review || !review->capture || std::chrono::steady_clock::now()>=review->expires
        || !current(review->capture->authority.live)) return {};
    auto descriptor=review->capture->descriptor;
    descriptor.insert(QStringLiteral("approved"),review->capture->approved && review->capture->approved->current());
    return descriptor;
}
void AddonDiscoverySourceOwner::discardApproval(const QString& id) { m_reviews.remove(id); scheduleMaintenance(); }
bool AddonDiscoverySourceOwner::approve(const QString& request) { return changeApproval(request,true); }
bool AddonDiscoverySourceOwner::revoke(const QString& request) { return changeApproval(request,false); }
bool AddonDiscoverySourceOwner::changeApproval(const QString& id,bool approve) {
    Q_ASSERT(QThread::currentThread()==thread());
    const auto review=m_reviews.value(id);
    if(!active() || !review || !review->capture || !review->capture->ticket || std::chrono::steady_clock::now()>=review->expires || m_controls>=4
        || (approve && !current(review->capture->authority.live))) return false;
    const auto permit=workPermit(true); if(!permit) return false;
    m_reviews.remove(id); ++m_controls;
    const auto capture=review->capture; const auto shared=m_shared; const auto gate=m_gate; const auto lifetime=m_lifetime;
    const QPointer<AddonDiscoverySourceOwner> owner(this);
    if(!approve && capture->revoke) capture->revoke();
    if(!owner) return false;
    const bool accepted=lifetime.startQtWork([shared,gate,capture,id,approve] {
        bool success=false; QString error;
        try {
            const auto consent=shared->approvals();
            if(consent && shared->stop->open.load(std::memory_order_acquire)) {
                if(approve) {
                    if(current(capture->authority.current)) success=consent->grant(capture->ticket->id,&error);
                    else error=tr("The source or account changed. Open approval again.");
                } else success=consent->revoke(capture->binding,&error);
            }
        } catch(...) {}
        if(!success && error.isEmpty()) error=tr("The app cannot save source approval. Try again.");
        if(!post(gate,[id,source=capture->binding.contribution,success,error](auto* owner) { owner->changedApproval(id,source,success,error); })) shared->stop->close();
    },[] {},[permit](auto ticket) { if(!ticket.retain(permit)) throw std::bad_alloc(); });
    if(!accepted) {
        --m_controls; m_reviews.insert(id,review); m_error=tr("The app cannot start the approval request."); notify();
    }
    return accepted;
}
void AddonDiscoverySourceOwner::changedApproval(const QString& id,const QString& source,bool success,const QString& error) {
    if(m_controls) --m_controls;
    if(m_closing) return;
    m_error=error; m_cache.remove(source);
    if(active() && !source.isEmpty()) refreshSource(source);
    notify(); emit actionFinished(id,success,error);
}
bool AddonDiscoverySourceOwner::resetApprovals() {
    Q_ASSERT(QThread::currentThread()==thread());
    if(!active() || m_controls>=4) return false;
    const auto permit=workPermit(true); if(!permit) return false;
    const auto shared=m_shared; const auto gate=m_gate; const auto id=requestId(); std::function<void()> reset;
    { std::lock_guard<std::mutex> lock(shared->mutex); reset=shared->reset; }
    if(!reset) return false;
    m_reviews.clear(); m_cache.clear(); ++m_controls; const auto lifetime=m_lifetime;
    const QPointer<AddonDiscoverySourceOwner> owner(this); reset(); if(!owner) return false; notify();
    const bool accepted=lifetime.startQtWork([shared,gate,id] {
        bool success=false; QString error;
        try { const auto consent=shared->approvals(); if(consent) success=consent->reset(&error); } catch(...) {}
        if(!success && error.isEmpty()) error=tr("The app cannot reset source approvals. Try again.");
        if(!post(gate,[id,success,error](auto* owner) { owner->changedApproval(id,{},success,error); })) shared->stop->close();
    },[] {},[permit](auto ticket) { if(!ticket.retain(permit)) throw std::bad_alloc(); });
    if(!accepted) { --m_controls; m_error=tr("The app cannot start the reset request."); notify(); }
    return accepted;
}
AddonNativeSourceResolver::AdmissionReader AddonDiscoverySourceOwner::captureAdmission(const QString& uri,quint64 generation) {
    Q_ASSERT(QThread::currentThread()==thread());
    if(!active() || !m_manager || !generation) return {};
    const auto reference=AddonSourceAdmissions::decodeUntrusted(uri);
    const auto source=reference.value("source").toString();
    if(reference.isEmpty() || !sourceId(source)) return {};
    const auto reader=m_manager->nativeAuthorityReader(source); const auto shared=m_shared; const auto lifetime=m_lifetime;
    return [reader,shared,lifetime,uri,generation]() -> std::optional<Admission> {
        if(!reader || !lifetime.current() || !shared->stop->open.load(std::memory_order_acquire)) return {};
        const auto authority=reader(); const auto key=authority?binding(*authority):std::nullopt;
        const auto consent=shared->approvals();
        if(!authority || !key || !consent || !current(authority->current) || !admitted(shared->records,*key,uri)
            || !shared->put(*key,*authority)) return {};
        if(!shared->probeTransport() || !current(authority->current) || !shared->stop->transportReady()) return {};
        // A playlist entry can play before its source was opened in this profile.
        if(!consent->approveAccepted(*key)) return {};
        const auto intent=consent->mintIntent(*key,uri,generation,AddonSourceConsent::IntentReason::PrimaryPlayback);
        if(intent.isEmpty()) return {};
        const auto authorization=consent->consumeIntent(intent,*key,uri,generation,AddonSourceConsent::IntentReason::PrimaryPlayback);
        if(!authorization || !current(authority->current) || !shared->stop->open.load(std::memory_order_acquire)) return {};
        const auto stop=shared->stop; const auto records=shared->records;
        const auto saved=authority->current,live=authority->live;
        Admission result{consent,*authorization,
            [stop,records,key=*key,uri,saved,live] {
                return stop->transportReady() && current(live) && current(saved)
                    && admitted(records,key,uri) && current(live) && stop->transportReady();
            },[stop,live] { return stop->transportReady() && current(live) && stop->transportReady(); },lifetime};
        return result.current(uri,generation)?std::optional<Admission>(std::move(result)):std::nullopt;
    };
}
AddonDiscoverySourceOwner::Cancel AddonDiscoverySourceOwner::resolve(const Admission& admission,Current check,Prepared done) {
    Q_ASSERT(QThread::currentThread()==thread());
    if(!active() || !m_stop->transportReady() || !m_manager || !done || !current(check) || m_resolves.size()>=16) { if(done) done({}); return {}; }
    const auto uri=admission.authorization.uri(); const auto generation=admission.authorization.generation();
    const auto reference=AddonSourceAdmissions::decodeUntrusted(uri); const auto& binding=admission.authorization.binding();
    if(reference.value("source").toString()!=binding.contribution || reference.value("accountReference").toString()!=binding.accountReference
        || !admission.current(uri,generation)) { done({}); return {}; }
    const auto id=requestId(); const auto job=std::make_shared<Resolve>(); job->admission=admission;
    job->current=std::move(check); job->done=std::move(done); job->deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    m_resolves.insert(id,job); scheduleMaintenance(); const QPointer<AddonDiscoverySourceOwner> owner(this);
    const auto invocation=m_manager->invoke(binding.contribution,QStringLiteral("resolve"),
        {{"itemRef",reference.value("itemId")},{"intent",QStringLiteral("play")}},
        [check=job->current] { return current(check); });
    if(!owner) return {};
    if(invocation.isEmpty() || !m_resolves.contains(id) || !current(job->current)) { cancelResolve(id); return {}; }
    job->invocation=invocation; m_invocations.insert(invocation,id);
    return [owner,id] { if(owner) owner->cancelResolve(id); };
}
void AddonDiscoverySourceOwner::resolved(const QString& invocation,const QString& contribution,const QString& status,
    const QVariantMap& data,const QString& error) {
    const auto id=m_invocations.value(invocation); if(id.isEmpty()) return;
    const auto job=m_resolves.value(id);
    if(!job || job->invocation!=invocation || contribution!=job->admission.authorization.binding().contribution) return;
    m_invocations.remove(invocation); job->invocation.clear();
    QString failure;
    AddonStreamProxy::Spec spec;
    if(!active() || !m_stop->transportReady() || !current(job->current)) failure=tr("The playback request is no longer available.");
    else if(status!=QLatin1String("match")) failure=tr("The source did not return a media stream.");
    else {
        for(auto it=data.cbegin();it!=data.cend();++it)
            if(it.key()!=QLatin1String("url") && it.key()!=QLatin1String("format") && it.key()!=QLatin1String("expiresAt"))
                failure=tr("The source returned media properties that are not permitted.");
        const auto url=data.value("url");
        spec.upstream=QUrl(url.toString(),QUrl::StrictMode);
        if(url.metaType().id()!=QMetaType::QString || url.toString().size()>8192 || !AddonStreamPolicy::url(spec.upstream))
            failure=tr("The source did not return a permitted HTTPS media address.");
        const auto format=data.value("format",QStringLiteral("auto"));
        if(format.metaType().id()!=QMetaType::QString) failure=tr("The app does not support the media format.");
        else if(format.toString()==QLatin1String("auto")) spec.format=AddonStreamProxy::Format::Auto;
        else if(format.toString()==QLatin1String("progressive")) spec.format=AddonStreamProxy::Format::Progressive;
        else if(format.toString()==QLatin1String("hls")) spec.format=AddonStreamProxy::Format::Hls;
        else if(format.toString()==QLatin1String("dash")) spec.format=AddonStreamProxy::Format::Dash;
        else failure=tr("The app does not support the media format.");
        if(data.contains("expiresAt")) {
            const auto value=QJsonValue::fromVariant(data.value("expiresAt")); const auto time=value.toDouble(-1);
            if(!value.isDouble() || !std::isfinite(time) || std::floor(time)!=time || time>9007199254740991.0
                || time<=double(QDateTime::currentMSecsSinceEpoch())) failure=tr("The media address expired. Start playback again.");
        }
    }
    if(!failure.isEmpty()) {
        m_error=failure; notify(); cancelResolve(id); return;
    }
    Q_UNUSED(error);
    const auto prepare=m_prepareTransport; const QPointer<AddonDiscoverySourceOwner> owner(this);
    Cancel cancellation;
    try {
        const auto gate=m_gate; const auto stop=m_stop; const std::weak_ptr<Resolve> weak=job;
        cancellation=prepare(job->admission,job->current,std::move(spec),[gate,stop,id,weak](std::unique_ptr<AddonNativeSourceResolver::Prepared> value) mutable {
            try {
                auto payload=std::make_shared<std::unique_ptr<AddonNativeSourceResolver::Prepared>>(std::move(value));
                if(!post(gate,[id,weak,payload](auto* owner) mutable {
                    const auto job=weak.lock(); if(!job || owner->m_resolves.value(id)!=job) return;
                    owner->m_resolves.remove(id); owner->scheduleMaintenance(); auto done=std::move(job->done);
                    if(!current(job->current)) payload->reset();
                    if(done) done(std::move(*payload));
                })) stop->close();
            } catch(...) { stop->close(); }
        });
    } catch(...) { if(owner) cancelResolve(id); }
    if(!owner || m_resolves.value(id)!=job || !current(job->current)) { if(cancellation) cancellation(); }
    else job->transport=std::move(cancellation);
}
void AddonDiscoverySourceOwner::cancelResolve(const QString& id) {
    const auto job=m_resolves.take(id); if(!job) return;
    scheduleMaintenance();
    m_invocations.remove(job->invocation); const auto manager=m_manager;
    const auto invocation=job->invocation; job->invocation.clear();
    const auto cancel=std::move(job->transport); const auto done=std::move(job->done);
    if(manager && !invocation.isEmpty()) manager->cancel(invocation);
    if(cancel) cancel(); if(done) done({});
}
void AddonDiscoverySourceOwner::scheduleMaintenance() {
    if(m_closing) { m_poll.stop(); return; }
    // Only active resolutions need frequent current checks. Reviews have exact
    // deadlines. Cache invalidation arrives through bounded native watches.
    auto delay=std::chrono::milliseconds::max();
    const auto now=std::chrono::steady_clock::now();
    if(!m_resolves.isEmpty()) delay=std::chrono::milliseconds(20);
    for(const auto& review:m_reviews) {
        const auto left=std::chrono::duration_cast<std::chrono::milliseconds>(review->expires-now);
        delay=std::min(delay,std::max(std::chrono::milliseconds(1),left));
    }
    if(delay==std::chrono::milliseconds::max()) { m_poll.stop(); return; }
    const auto next=int(std::min(delay,std::chrono::milliseconds(INT_MAX)).count());
    // Repeated notices must not postpone a pending current or deadline check.
    const auto remaining=m_poll.remainingTime();
    if(!m_poll.isActive() || remaining<0 || next<remaining) m_poll.start(next);
}
void AddonDiscoverySourceOwner::tick() {
    if(m_closing) return;
    if(!active()) { m_error=tr("Source playback stopped."); close(); return; }
    const QPointer<AddonDiscoverySourceOwner> owner(this);
    const auto resolves=m_resolves;
    for(auto it=resolves.cbegin();it!=resolves.cend();++it) {
        if(!current(it.value()->current) || std::chrono::steady_clock::now()>=it.value()->deadline) cancelResolve(it.key());
        if(!owner) return;
    }
    bool changed=false;
    for(auto it=m_reviews.begin();it!=m_reviews.end();) {
        if(std::chrono::steady_clock::now()>=it.value()->expires) { it=m_reviews.erase(it); changed=true; }
        else ++it;
    }
    for(auto it=m_cache.begin();it!=m_cache.end();++it) {
        const auto available=canUseSource(it.key());
        if(it.value()->available!=available) { it.value()->available=available; changed=true; }
    }
    if(changed) notify();
    scheduleMaintenance();
}
void AddonDiscoverySourceOwner::close() {
    Q_ASSERT(QThread::currentThread()==thread());
    if(m_closing) return;
    m_closing=true; m_starting=false; m_poll.stop(); m_instance=nullptr;
    if(m_wake) m_wake->close();
    { std::lock_guard<std::mutex> lock(m_gate->mutex); m_gate->owner=nullptr; }
    const auto stop=m_stop; auto resolver=std::move(m_resolver); const auto manager=m_manager;
    auto jobs=std::move(m_resolves); m_resolves.clear(); m_invocations.clear(); m_reviews.clear(); m_cache.clear(); m_refreshes.clear(); m_controls=0;
    notify(); stop->close();
    if(resolver) resolver->close();
    for(const auto& job:jobs) {
        const auto invocation=job->invocation; const auto cancel=std::move(job->transport); const auto done=std::move(job->done);
        if(manager && !invocation.isEmpty()) manager->cancel(invocation);
        if(cancel) cancel(); if(done) done({});
    }
}
AddonDiscoveryService* createPlayerDiscoveryWithSources(AddonManager* manager,AddonDiscoverySourceOwner* sources,AddonDiscoveryNativeBackend native,QObject* parent)
{
    const QPointer<AddonDiscoverySourceOwner> owner(sources);
    const bool supported=native.stableReferencePlaybackAvailable && bool(native.playReference);
    AddonQueueTransactions::Backend queues;
    queues.snapshot=[owner,supported,snapshot=std::move(native.snapshot)](const QString& target) {
        return snapshot && (target!=QLatin1String("queue") || (supported && owner && owner->active()))?snapshot(target):AddonQueueTransactions::Snapshot{};
    };
    if(native.append) queues.append=[owner,supported,append=std::move(native.append)](const auto& transaction,auto done) {
        if(transaction.target==QLatin1String("queue")) {
            if(!supported) { done(false,QObject::tr("Source playback is not available.")); return AddonQueueTransactions::Cancel{}; }
            auto guarded=transaction; guarded.authorityCurrent=owner?owner->referenceGuard(transaction.references):std::function<bool()>{};
            if(!guarded.authorityCurrent || !guarded.authorityCurrent()) { done(false,QObject::tr("Approve this source before you add it to the queue.")); return AddonQueueTransactions::Cancel{}; }
            return append(guarded,std::move(done));
        }
        return append(transaction,std::move(done));
    };
    if(native.stableReferencePlaybackAvailable && native.playReference)
        queues.playReference=[owner,play=std::move(native.playReference)](const auto& transaction,auto done) {
            if(transaction.references.size()!=1 || !owner || !owner->canUseReference(transaction.references.first().toMap())) {
                done(false,QObject::tr("Approve this source before you start playback.")); return AddonQueueTransactions::Cancel{};
            }
            auto guarded=transaction; guarded.authorityCurrent=owner->referenceGuard(transaction.references);
            if(!guarded.authorityCurrent || !guarded.authorityCurrent()) { done(false,QObject::tr("The source approval changed. Try again.")); return AddonQueueTransactions::Cancel{}; }
            return play(guarded,std::move(done));
        };
    if(supported) queues.pendingAhead=std::move(native.pendingAhead);
    auto* service=new AddonDiscoveryService(manager,{},std::move(queues),parent);
    service->setSourcePlayback({[owner,supported](const auto& source) { return supported && owner && owner->canUseSource(source); },
        [owner](const auto& reference) { return owner && owner->canUseReference(reference); },
        [owner](const auto& source) { if(owner) owner->refreshSource(source); }});
    if(sources) QObject::connect(sources,&AddonDiscoverySourceOwner::changed,service,&AddonDiscoveryService::refreshSourcePlayback);
    return service;
}

}
