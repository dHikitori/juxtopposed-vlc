// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "addon_artwork_lookup_runner.hpp"
#include "addon_manager.hpp"
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QThread>
#include <QUuid>
#include <vlc_artwork_policy.h>
#include <vlc_input_item.h>
#include <vlc_metadata_backend.h>
#include <vlc_tick.h>
#include <utility>

namespace vlc::addons {
namespace {
using Aggregate = ArtworkAttemptAggregate;
using Plan = ArtworkAttemptPlan;
meta_fetcher_provider_t family(const QString& name)
{
    if(name==QLatin1String("musicbrainz")) return META_FETCHER_PROVIDER_MUSICBRAINZ;
    if(name==QLatin1String("google-images")) return META_FETCHER_PROVIDER_GOOGLE_IMAGES;
    if(name==QLatin1String("french-tv")) return META_FETCHER_PROVIDER_FRENCH_TV;
    if(name==QLatin1String("lastfm")) return META_FETCHER_PROVIDER_LASTFM;
    return META_FETCHER_PROVIDER_NONE;
}
bool live(const std::function<bool()>& check) noexcept
{ try { return check && check(); } catch(...) { return false; } }
}
struct ArtworkLookupRunner::Control {
    std::atomic_bool canceled{false};
    std::shared_ptr<CoreMetadataAdmission> gate;
    std::mutex mutex;
    Cancel active;
    void set(Cancel next) {
        Cancel retired; bool stop;
        { std::lock_guard<std::mutex> lock(mutex); stop=canceled.load(); retired=std::move(active); if(!stop) active=std::move(next); }
        if(stop && next) next();
    }
    bool install(std::shared_ptr<CoreMetadataAdmission> next) {
        bool stop;
        { std::lock_guard<std::mutex> lock(mutex); gate=next; stop=canceled.load(); }
        if(stop && next) next->revoke();
        return !stop;
    }
    void cancel() {
        const bool first=!canceled.exchange(true);
        Cancel call; std::shared_ptr<CoreMetadataAdmission> admission;
        { std::lock_guard<std::mutex> lock(mutex); admission=gate; if(first) call=std::move(active); }
        if(admission) admission->revoke();
        if(call) call();
    }
};
ArtworkLookupRunner::ArtworkLookupRunner(AddonManager* manager,Options options,Complete complete,QObject* parent)
    : QObject(parent),m_manager(manager),m_nativeContext(options.nativeContext),m_nativeImages(options.nativeImages),
      m_options(std::move(options)),m_complete(std::move(complete)),m_control(std::make_shared<Control>())
{
    m_deadline.setSingleShot(true);
    connect(&m_deadline,&QTimer::timeout,this,[this] { finish({-ETIMEDOUT,{},{}}); });
    if(manager) connect(manager,&AddonManager::operationFinished,this,
        [this](const auto& id,const auto&,const auto& status,const auto& data,const auto& error) {
            portableResult(id,status,data,error);
        });
    if(manager) connect(manager,&AddonManager::retiring,this,&ArtworkLookupRunner::cancel,Qt::DirectConnection);
}
ArtworkLookupRunner::~ArtworkLookupRunner()
{
    if(!m_terminal) { m_complete={}; cancel(); }
}
ArtworkLookupRunner::Cancel ArtworkLookupRunner::nativeCancellation() const
{ return [control=m_control] { control->cancel(); }; }
bool ArtworkLookupRunner::current() const
{
    return !m_terminal && m_manager && m_options.child && !m_control->canceled.load()
        && m_control->gate && m_control->gate->current() && m_options.lifetime.current()
        && m_options.registryToken.current() && live(m_options.live)
        && vlc_metadata_job_IsPending(m_options.child.get()) && (!m_plan || m_plan->current());
}
bool ArtworkLookupRunner::start()
{
    if(m_started || QThread::currentThread()!=thread() || !m_manager || !m_complete
        || !m_options.child || !m_options.registry || !m_options.registryToken.current()
        || !m_options.lifetime.current() || !live(m_options.live)) return false;
    m_started=true;
    const auto deadline=vlc_tick_now()+VLC_TICK_FROM_SEC(25);
    if(!m_control->install(CoreMetadataAdmission::create(deadline))) return false;
    if(!m_control->gate || !m_options.registry->arm(m_options.registryToken,m_control->gate)) return false;
    m_source=m_manager->metadataCatalogueSource();
    if(!m_source || !m_source->current()) return false;
    m_deadline.start(25000); capturePlan(); return true;
}
ArtworkAttemptPlan::Capture ArtworkLookupRunner::capture() const
{
    Plan::Capture value;
    value.catalogue=m_source->catalogueReader; value.credentialReader=m_source->credentialReader;
    value.knownPresence=m_source->credentialPresence; value.builtins=m_options.builtins;
    const auto identity=m_options.registryToken.identity();
    value.registryIdentity=QByteArray(reinterpret_cast<const char*>(identity.data()),identity.size());
    value.subject=m_plan ? m_plan->subject : QCryptographicHash::hash(QUuid::createUuid().toByteArray(),QCryptographicHash::Sha256);
    value.privateScope=m_options.privateScope; value.requestContext=m_options.context;
    value.deadline=m_plan ? m_plan->deadline : vlc_tick_now()+VLC_TICK_FROM_MS(m_deadline.remainingTime());
    value.clock=[] { return vlc_tick_now(); };
    value.live=[token=m_options.registryToken,life=m_options.lifetime,control=m_control,check=m_options.live,job=m_options.child] {
        return token.current() && life.current() && !control->canceled.load() && control->gate->current()
            && live(check) && vlc_metadata_job_IsPending(job.get());
    };
    return value;
}
void ArtworkLookupRunner::capturePlan()
{
    if(!current()) { finish({-EINTR,{},{}}); return; }
    struct Prepared { std::shared_ptr<const Plan> plan; QString error; };
    auto result=std::make_shared<Prepared>(); auto input=capture(); const auto executable=m_source->workerExecutable;
    const QPointer<ArtworkLookupRunner> owner(this);
    m_preparation=m_manager->queueMetadataPreparation({},[result,input=std::move(input),executable]() mutable {
        const QFileInfo worker(executable); input.workerAvailable=worker.isFile() && worker.isExecutable();
        result->plan=Plan::capture(std::move(input),&result->error);
    },[owner,result](bool ok) {
        if(!owner) return;
        owner->m_preparation.clear();
        if(!ok || !owner->current() || !result->plan) { owner->finish({VLC_EGENERIC,{},{}}); return; }
        owner->m_plan=result->plan;
        owner->m_aggregate=Aggregate::create(owner->m_plan,owner->m_options.child.get(),owner->m_control->gate);
        if(!owner->m_aggregate) { owner->finish({VLC_ENOTSUP,{},{}}); return; }
        owner->advance();
    });
    if(m_preparation.isEmpty()) finish({VLC_EGENERIC,{},{}});
}
void ArtworkLookupRunner::advance()
{
    if(!current() || !m_aggregate) { finish({-EINTR,{},{}}); return; }
    const auto next=m_aggregate->nextOrdinal();
    if(!next) {
        if(m_aggregate->exhausted() && !m_aggregate->negativeForbidden()) recheck(0,{},{},{},true);
        else finish({VLC_EGENERIC,{},{}});
        return;
    }
    m_ordinal=*next;
    if(!m_aggregate->reserve(*next)) { finish({VLC_EGENERIC,{},{}}); return; }
    const auto& slot=m_plan->attempts.at(*next);
    if(slot.blocked) { block(*next,Aggregate::BlockReason::Denied); return; }
    if(slot.implementation==Plan::Implementation::Portable) portable(*next);
    else if(slot.implementation==Plan::Implementation::Native) native(*next);
    else block(*next,Aggregate::BlockReason::Unsupported);
}
void ArtworkLookupRunner::portable(unsigned ordinal)
{
    const auto& slot=m_plan->attempts.at(ordinal);
    const auto prepare=m_manager->metadataInvocationPreparer(m_plan,ordinal);
    if(!prepare) { block(ordinal,Aggregate::BlockReason::Denied); return; }
    struct Prepared { std::unique_ptr<PreparedMetadataInvocation> recipe; QString error; };
    auto result=std::make_shared<Prepared>(); const QPointer<ArtworkLookupRunner> owner(this);
    m_preparation=m_manager->queueMetadataPreparation(slot.package.manifest.id,[prepare,result] { result->recipe=prepare(&result->error); },[owner,result,ordinal](bool ok) {
        if(!owner) return;
        owner->m_preparation.clear();
        if(!owner->current()) { owner->finish({-EINTR,{},{}}); return; }
        if(!ok || !result->recipe) { owner->block(ordinal,Aggregate::BlockReason::Denied); return; }
        const auto& slot=owner->m_plan->attempts.at(ordinal);
        AddonMetadataAttemptAdmission::Capture capture;
        capture.nativeSubject=owner->m_plan->subject; capture.nativeChildHold=owner->m_options.child;
        capture.ordinal=ordinal; capture.contribution=slot.contribution; capture.feature=slot.feature; capture.operation=slot.operation;
        capture.releaseDigest=slot.releaseDigest; capture.releaseGeneration=slot.releaseGeneration;
        capture.settingsRevision=slot.settingsRevision; capture.accountReference=slot.accountReference; capture.partition=slot.partition;
        capture.approvedGrants=slot.approvedGrants; capture.requestedFields=slot.requestedFields;
        capture.lifetime=owner->m_options.lifetime; capture.admission=owner->m_control->gate;
        capture.live=[queued=owner->m_plan->queuedAuthority,current=owner->m_plan->authority,control=owner->m_control] {
            return queued.current() && current.current() && !control->canceled.load();
        };
        owner->m_attempt=AddonMetadataAttemptAdmission::create(std::move(capture));
        if(!owner->m_attempt) { owner->block(ordinal,Aggregate::BlockReason::Denied); return; }
        owner->m_invocation=owner->m_manager->invokePreparedMetadata(std::move(result->recipe),{},owner->m_attempt);
        if(owner->m_invocation.isEmpty()) owner->block(ordinal,Aggregate::BlockReason::Error);
    });
    if(m_preparation.isEmpty()) block(ordinal,Aggregate::BlockReason::Error);
}
void ArtworkLookupRunner::portableResult(const QString& invocation,const QString& status,const QVariantMap& data,const QString& error)
{
    if(invocation!=m_invocation || m_invocation.isEmpty() || m_terminal) return;
    m_invocation.clear();
    if(!current() || !m_attempt) { finish({-EINTR,{},{}}); return; }
    auto receipt=m_attempt->take(); m_attempt.reset(); const auto ordinal=m_ordinal;
    if(!error.isEmpty()) { block(ordinal,Aggregate::BlockReason::Error); return; }
    if(status==QLatin1String("no_match")) {
        if(!m_aggregate->acceptPortable(ordinal,std::move(receipt))) { block(ordinal,Aggregate::BlockReason::Binding); return; }
        advance(); return;
    }
    if(!m_aggregate->acceptPortablePositive(ordinal,std::move(receipt),QJsonObject::fromVariantMap(data))) {
        block(ordinal,Aggregate::BlockReason::Binding); return;
    }
    // Reuse shared identity and label acceptance with exactly one captured
    // provider. Its empty partition disables cache and shared completions.
    const auto contribution=m_plan->attempts.at(ordinal).contribution;
    const QPointer<ArtworkLookupRunner> owner(this); AddonMetadataCoordinator::Backend backend;
    backend.providersForFeature=[contribution](const auto&) { return QVariantList{QVariantMap{
        {QStringLiteral("id"),contribution},{QStringLiteral("enabled"),true},{QStringLiteral("configured"),true},
        {QStringLiteral("features"),QStringList{QStringLiteral("artwork")}},{QStringLiteral("operations"),QStringList{QStringLiteral("enrich")}}}}; };
    backend.invoke=[owner,contribution,status,data](const auto&,const auto&,const auto&) {
        const auto id=QStringLiteral("accepted-artwork:")+QUuid::createUuid().toString(QUuid::WithoutBraces);
        if(owner && owner->m_acceptance) QTimer::singleShot(0,owner->m_acceptance,[owner,id,contribution,status,data] {
            if(owner && owner->m_acceptance) owner->m_acceptance->operationFinished(id,contribution,status,data,{});
        });
        return id;
    };
    m_acceptance=new AddonMetadataCoordinator(std::move(backend),this);
    connect(m_acceptance,&AddonMetadataCoordinator::finished,this,[this,ordinal](const auto&,const auto& status,const auto& result,const auto&) {
        acceptFields(ordinal,status,result);
    });
    m_acceptance->enrichNative(QStringLiteral("artwork"),m_options.context.toVariantMap(),{QStringLiteral("artwork")},m_options.privateScope);
}
void ArtworkLookupRunner::acceptFields(unsigned ordinal,const QString& status,const QVariantMap& data)
{
    if(m_acceptance) { m_acceptance->deleteLater(); m_acceptance.clear(); }
    if(!current()) { finish({-EINTR,{},{}}); return; }
    // Preserve the shared rule that a later provider cannot select an entity
    // from an earlier ambiguous result during automatic work.
    if(status==QLatin1String("ambiguous")) { finish({VLC_EGENERIC,{},{}}); return; }
    const auto fields=data.value(QStringLiteral("fields")).toMap();
    const auto value=fields.value(QStringLiteral("artwork"));
    if((status!=QLatin1String("match") && status!=QLatin1String("partial")) || !value.isValid() || value.isNull()) {
        block(ordinal,Aggregate::BlockReason::Denied); return;
    }
    resolvePortable(ordinal,value);
}
void ArtworkLookupRunner::resolvePortable(unsigned ordinal,const QVariant& descriptor)
{
    if(!m_options.resolvePortable) { block(ordinal,Aggregate::BlockReason::Unsupported); return; }
    const QPointer<ArtworkLookupRunner> owner(this); const auto contribution=m_plan->attempts.at(ordinal).contribution;
    const auto stage=++m_stage; auto delivered=std::make_shared<bool>(false);
    const auto resolver=m_options.resolvePortable;
    const auto id=resolver(contribution,descriptor,[owner,ordinal,stage,delivered](const QUrl& file,const QString& error) {
        if(std::exchange(*delivered,true)) return;
        if(!owner || owner->m_terminal || owner->m_stage!=stage) return;
        owner->m_asset.clear();
        if(!owner->current()) { owner->finish({-EINTR,{},{}}); return; }
        if(!error.isEmpty() || !file.isLocalFile()) { owner->block(ordinal,Aggregate::BlockReason::ImageFailure); return; }
        const auto uri=file.toEncoded(); auto item=Item(input_item_Copy(vlc_metadata_job_GetRequest(owner->m_options.child.get())->item),input_item_Release);
        if(!item || vlc_artwork_item_SetApprovedURI(item.get(),uri.constData())!=VLC_SUCCESS) { owner->block(ordinal,Aggregate::BlockReason::ImageFailure); return; }
        owner->recheck(ordinal,std::move(item),uri,{},false);
    });
    if(!owner || owner->m_terminal || *delivered || owner->m_stage!=stage) return;
    if(id.isEmpty()) { block(ordinal,Aggregate::BlockReason::ImageFailure); return; }
    m_asset=id;
}
void ArtworkLookupRunner::native(unsigned ordinal)
{
    if(!m_nativeContext) { block(ordinal,Aggregate::BlockReason::Absent); return; }
    const auto value=family(m_plan->attempts.at(ordinal).family);
    if(value==META_FETCHER_PROVIDER_NONE) { block(ordinal,Aggregate::BlockReason::Unsupported); return; }
    const QPointer<ArtworkLookupRunner> owner(this);
    const auto stage=++m_stage; auto delivered=std::make_shared<bool>(false); const auto control=m_control;
    auto cancel=m_nativeContext->probeOutcome(m_options.child.get(),value,[owner,ordinal,value,stage,delivered](const auto& result) {
        if(std::exchange(*delivered,true)) return;
        if(!owner || owner->m_terminal || owner->m_stage!=stage) return;
        owner->m_control->set({});
        if(!owner->current()) { owner->finish({-EINTR,{},{}}); return; }
        if(result.status!=VLC_SUCCESS || result.claim.isEmpty()) { owner->block(ordinal,Aggregate::BlockReason::Error); return; }
        if(owner->m_nativeContext && owner->m_nativeContext->validates(result.claim,owner->m_options.child.get(),value)) {
            if(!owner->m_aggregate->acceptNativePositive(ordinal,*owner->m_nativeContext,result.claim)) { owner->finish({VLC_EGENERIC,{},{}}); return; }
            owner->resolveNative(ordinal,result.claim,value);
        } else {
            if(!owner->m_nativeContext || !owner->m_aggregate->acceptNativeClaim(ordinal,*owner->m_nativeContext,result.claim)) {
                owner->block(ordinal,Aggregate::BlockReason::Unknown); return;
            }
            owner->advance();
        }
    });
    if(!owner || owner->m_terminal || *delivered || owner->m_stage!=stage) return;
    if(!cancel) { block(ordinal,Aggregate::BlockReason::Error); return; }
    control->set(std::move(cancel));
}
void ArtworkLookupRunner::resolveNative(unsigned ordinal,const QString& claim,meta_fetcher_provider_t family)
{
    if(!m_nativeImages) { block(ordinal,Aggregate::BlockReason::Absent); return; }
    const QPointer<ArtworkLookupRunner> owner(this);
    const auto stage=++m_stage; auto delivered=std::make_shared<bool>(false); const auto control=m_control;
    auto cancel=m_nativeImages->resolve(claim,m_options.child.get(),family,[owner,ordinal,stage,delivered](const auto& result) {
        if(std::exchange(*delivered,true)) return;
        if(!owner || owner->m_terminal || owner->m_stage!=stage) return;
        owner->m_control->set({});
        if(!owner->current()) { owner->finish({-EINTR,{},{}}); return; }
        if(!result.permit.current() || !result.error.isEmpty() || !result.file.isLocalFile()) {
            owner->block(ordinal,Aggregate::BlockReason::ImageFailure); return;
        }
        const auto uri=result.file.toEncoded(); auto item=Item(input_item_Copy(vlc_metadata_job_GetRequest(owner->m_options.child.get())->item),input_item_Release);
        if(!item || vlc_artwork_item_SetApprovedURI(item.get(),uri.constData())!=VLC_SUCCESS) { owner->block(ordinal,Aggregate::BlockReason::ImageFailure); return; }
        owner->recheck(ordinal,std::move(item),uri,result.permit,false);
    });
    if(!owner || owner->m_terminal || *delivered || owner->m_stage!=stage) return;
    if(!cancel) { block(ordinal,Aggregate::BlockReason::ImageFailure); return; }
    control->set(std::move(cancel));
}
void ArtworkLookupRunner::recheck(unsigned ordinal,Item image,const QByteArray& uri,AddonNativeArtworkAssets::Permit permit,bool negative)
{
    if(!current()) { finish({-EINTR,{},{}}); return; }
    struct Checked { std::shared_ptr<const Plan> plan; bool recipe=false; QString error; };
    auto checked=std::make_shared<Checked>(); auto input=capture(); const auto executable=m_source->workerExecutable;
    const auto old=m_plan;
    const auto prepare=!negative && old->attempts.at(ordinal).implementation==Plan::Implementation::Portable
        ? m_manager->metadataInvocationPreparer(old,ordinal) : MetadataInvocationPreparer{};
    const QPointer<ArtworkLookupRunner> owner(this);
    m_preparation=m_manager->queueMetadataPreparation({},[checked,input=std::move(input),executable,prepare,old]() mutable {
        const QFileInfo worker(executable); input.workerAvailable=worker.isFile() && worker.isExecutable();
        checked->plan=Plan::capture(std::move(input),&checked->error);
        if(!checked->plan || checked->plan->planDigest!=old->planDigest) return;
        if(prepare) { auto scratch=prepare(&checked->error); checked->recipe=scratch && scratch->current(); }
        else checked->recipe=true;
    },[owner,checked,ordinal,image=std::move(image),uri,permit=std::move(permit),negative](bool ok) mutable {
        if(!owner) return;
        owner->m_preparation.clear();
        if(!ok || !owner->current() || !checked->plan || !checked->recipe || checked->plan->planDigest!=owner->m_plan->planDigest) {
            owner->finish({-EINTR,{},{}}); return;
        }
        int status;
        if(negative) status=owner->m_aggregate->trySeal(owner->m_options.child.get());
        else if(owner->m_plan->attempts.at(ordinal).implementation==Plan::Implementation::Native)
            status=owner->m_aggregate->trySealNativePositive(ordinal,owner->m_options.child.get(),image.get(),uri.constData(),permit);
        else status=owner->m_aggregate->trySealPositive(ordinal,owner->m_options.child.get(),image.get(),uri.constData());
        if(status!=VLC_SUCCESS) { owner->finish({status,{},{}}); return; }
        if(negative) image={input_item_Copy(vlc_metadata_job_GetRequest(owner->m_options.child.get())->item),input_item_Release};
        if(!image) { owner->m_control->gate->revoke(); owner->finish({VLC_ENOMEM,{},{}}); return; }
        const QJsonObject provenance{{QStringLiteral("source"),QStringLiteral("hakone.artwork")},
            {QStringLiteral("outcome"),negative ? QStringLiteral("not-found") : QStringLiteral("match")}};
        owner->finish({VLC_SUCCESS,std::move(image),QJsonDocument(provenance).toJson(QJsonDocument::Compact)},true);
    });
    if(m_preparation.isEmpty()) finish({VLC_EGENERIC,{},{}});
}
void ArtworkLookupRunner::block(unsigned ordinal,Aggregate::BlockReason reason)
{
    if(m_terminal) return;
    ++m_stage; m_attempt.reset(); m_control->set({});
    if(!current() || !m_aggregate) { finish({-EINTR,{},{}}); return; }
    m_aggregate->block(ordinal,reason);
    QTimer::singleShot(0,this,&ArtworkLookupRunner::advance);
}
void ArtworkLookupRunner::cancel() { finish({-EINTR,{},{}}); }
void ArtworkLookupRunner::finish(Result result,bool sealed)
{
    if(m_terminal) return;
    m_terminal=true; ++m_stage; m_deadline.stop();
    if(m_manager) disconnect(m_manager,nullptr,this,nullptr);
    if(m_acceptance) { m_acceptance->deleteLater(); m_acceptance.clear(); }
    const auto manager=m_manager;
    const auto preparation=std::exchange(m_preparation,{}),invocation=std::exchange(m_invocation,{}),asset=std::exchange(m_asset,{});
    auto cancelAsset=std::move(m_options.cancelPortable); const auto control=m_control;
    auto complete=std::move(m_complete); auto held=m_options.child;
    m_attempt.reset(); m_aggregate.reset(); m_plan.reset(); m_source.reset(); m_options={};
    if(sealed) control->set({});
    else control->cancel();
    // Any external cancellation or completion below can destroy this owner.
    if(manager && !preparation.isEmpty()) manager->cancel(preparation);
    if(manager && !invocation.isEmpty()) manager->cancel(invocation);
    if(cancelAsset && !asset.isEmpty()) cancelAsset(asset);
    if(complete) complete(std::move(result));
}
}
