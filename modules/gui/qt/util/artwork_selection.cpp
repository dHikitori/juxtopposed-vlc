// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include "artwork_selection.hpp"
#include "addons/addon_artwork_image.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <optional>
#include <vlc_artwork_policy.h>
#include <vlc_input_item.h>
#include <vlc_input_policy.h>
#include <vlc_metadata_backend.h>
#include <vlc_metadata_revision.h>
extern "C" {
#include "../../../../src/preparser/metadata_local.h"
}

namespace {
using Lifetime = vlc::addons::AddonStreamLifetime;
using Item = std::shared_ptr<input_item_t>;
using Job = std::shared_ptr<vlc_metadata_job_t>;
struct Budget {
    std::atomic_uint retained{0},active{0};
    std::mutex mutex;
    std::condition_variable changed;
};
Budget& budget() { static Budget value; return value; }
struct Permit {
    ~Permit() { budget().retained.fetch_sub(1,std::memory_order_release); }
};
std::shared_ptr<Permit> retain()
{
    auto count=budget().retained.load(std::memory_order_relaxed);
    do { if(count>=16) return {}; }
    while(!budget().retained.compare_exchange_weak(count,count+1,std::memory_order_acq_rel,std::memory_order_relaxed));
    try { return std::make_shared<Permit>(); }
    catch(...) { budget().retained.fetch_sub(1,std::memory_order_release); throw; }
}
void releaseItem(input_item_t* item) { if(item) input_item_Release(item); }
void releaseJob(vlc_metadata_job_t* job) { if(job) vlc_metadata_job_Release(job); }
Item held(input_item_t* item) { return {item ? input_item_Hold(item) : nullptr,releaseItem}; }
Item adopted(input_item_t* item) { return {item,releaseItem}; }
int dispatch(void*,vlc_metadata_job_t*) { return VLC_EGENERIC; }
void canceled(void*,vlc_metadata_job_t*) {}
void ended(vlc_metadata_job_t*,int,void*) {}
bool revisionsEqual(const vlc_metadata_revisions& left,const vlc_metadata_revisions& right)
{
    if(left.extra!=right.extra) return false;
    for(unsigned field=0;field<VLC_META_TYPE_COUNT;++field) if(left.fields[field]!=right.fields[field]) return false;
    return true;
}
QString failure() { return QStringLiteral("The app cannot prepare this cover image."); }
class BoundedOutput final : public QIODevice {
public:
    QByteArray bytes;
    explicit BoundedOutput(qint64 maximum):limit(maximum) { bytes.reserve(maximum); open(QIODevice::WriteOnly); }
private:
    qint64 limit;
    qint64 readData(char*,qint64) override { return -1; }
    qint64 writeData(const char* data,qint64 count) override {
        if(count<0 || count>limit-bytes.size()) return -1;
        bytes.append(data,count); return count;
    }
};
QByteArray readFile(const QString& path,qint64 maximum,const ArtworkSelection::Current& current,QString* error)
{
    if(!current()) return {};
    const QFileInfo info(path);
    if(!info.isFile() || info.size()<1 || info.size()>maximum || !current()) { *error=failure(); return {}; }
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly) || !current()) { *error=failure(); return {}; }
    const auto expected=file.size();
    if(expected<1 || expected>maximum) { *error=failure(); return {}; }
    QByteArray bytes(expected,Qt::Uninitialized); qint64 copied=0;
    while(current() && copied<expected) {
        const auto count=file.read(bytes.data()+copied,std::min<qint64>(65536,expected-copied));
        if(count<1) { *error=failure(); return {}; }
        copied+=count;
    }
    if(copied!=expected || !file.read(1).isEmpty() || file.error()!=QFileDevice::NoError) { *error=failure(); return {}; }
    return current() && error->isEmpty() ? bytes : QByteArray{};
}
QString cache(const QString& directory,const QByteArray& bytes,const ArtworkSelection::Options& options,const ArtworkSelection::Current& current)
{
    if(!current() || directory.isEmpty() || !QDir::isAbsolutePath(directory) || QFileInfo(directory).isSymLink()
        || !QDir().mkpath(directory)) return {};
    QLockFile lock(QDir(directory).filePath(QStringLiteral(".writer.lock")));
    if(!lock.tryLock(0) || !current()) return {};
    const auto name=QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())+QStringLiteral(".png");
    const auto path=QDir(directory).filePath(name); QFile existing(path);
    if(existing.exists()) {
        const QFileInfo info(path);
        if(info.isSymLink() || !info.isFile() || info.size()!=bytes.size() || !existing.open(QIODevice::ReadOnly)
            || existing.read(bytes.size()+1)!=bytes || !current()) return {};
        return path;
    }
    qint64 size=0; int count=0;
    QDirIterator files(directory,QDir::Files|QDir::NoSymLinks);
    while(files.hasNext()) {
        files.next(); const auto file=files.fileInfo();
        if(++count>options.maximumCacheEntries || file.size()>options.maximumCacheBytes-size) return {};
        size+=file.size();
    }
    if(count>=options.maximumCacheEntries || bytes.size()>options.maximumCacheBytes-size || !current()) return {};
    QSaveFile output(path);
    if(!output.open(QIODevice::WriteOnly) || !output.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner)
        || output.write(bytes)!=bytes.size() || !current() || !output.commit()) return {};
    return path;
}
}
struct ArtworkSelection::Work {
    Item target;
    Job job;
    std::shared_ptr<Permit> permit;
    std::shared_ptr<std::atomic_bool> owner;
    Lifetime::Handle lifetime;
    vlc_metadata_revisions baseline{};
    QByteArray source;
    std::shared_ptr<vlc_input_policy_t> policy;
    QUrl file;
    quint64 generation=0;
    std::atomic_bool canceled{false},used{false};
    bool live() const noexcept { return !canceled.load(std::memory_order_acquire) && owner->load(std::memory_order_acquire) && lifetime.current(); }
    void cancel() noexcept {
        { std::lock_guard<std::mutex> lock(budget().mutex); canceled.store(true,std::memory_order_release); }
        budget().changed.notify_all();
        if(job) vlc_metadata_job_Cancel(job.get());
    }
    ~Work() { cancel(); }
    bool current(input_item_t* item,quint64 expected) const {
        if(!item || item!=target.get() || expected!=generation || used.load(std::memory_order_acquire) || !live()) return false;
        auto* raw=vlc_input_policy_HoldItem(item);
        const std::shared_ptr<vlc_input_policy_t> scope(raw,vlc_input_policy_Release);
        if(scope!=policy || (scope && vlc_input_policy_IsRevoked(scope.get()))) return false;
        {
            vlc_mutex_locker lock(&item->lock);
            if(!item->psz_uri || source!=item->psz_uri || vlc_meta_ExtraRevision(item->p_meta)!=baseline.extra) return false;
            for(unsigned field=0;field<VLC_META_TYPE_COUNT;++field)
                if((item->p_meta ? vlc_meta_FieldRevision(item->p_meta,vlc_meta_type_t(field)) : 0)!=baseline.fields[field]) return false;
        }
        raw=vlc_input_policy_HoldItem(item);
        const std::shared_ptr<vlc_input_policy_t> after(raw,vlc_input_policy_Release);
        return after==policy && (!after || !vlc_input_policy_IsRevoked(after.get()))
            && vlc_metadata_job_IsCurrentFor(job.get(),item) && live();
    }
};
struct ArtworkSelection::Mailbox {
    std::mutex mutex;
    std::shared_ptr<std::atomic_bool> open=std::make_shared<std::atomic_bool>(true);
    std::optional<Result> result;
};
ArtworkSelection::Receipt::Receipt(std::shared_ptr<Work> work):m_work(std::move(work)) {}
ArtworkSelection::Receipt::~Receipt() { cancel(); }
QUrl ArtworkSelection::Receipt::file() const { return m_work->file; }
quint64 ArtworkSelection::Receipt::generation() const noexcept { return m_work->generation; }
bool ArtworkSelection::Receipt::current(input_item_t* target,quint64 generation) const { return m_work->current(target,generation); }
bool ArtworkSelection::Receipt::stampCopy(input_item_t* copy) const
{
    if(!copy || !m_work->current(m_work->target.get(),m_work->generation)) return false;
    const auto source=std::unique_ptr<char,decltype(&free)>(input_item_GetURI(copy),&free);
    auto* raw=vlc_input_policy_HoldItem(copy); const std::shared_ptr<vlc_input_policy_t> policy(raw,vlc_input_policy_Release);
    if(!source || m_work->source!=source.get() || policy!=m_work->policy || (policy && vlc_input_policy_IsRevoked(policy.get()))) return false;
    const auto uri=m_work->file.toEncoded(); return vlc_artwork_item_SetApprovedURI(copy,uri.constData())==VLC_SUCCESS;
}
int ArtworkSelection::Receipt::apply(input_item_t* target,quint64 generation)
{
    if(!current(target,generation) || m_work->used.exchange(true,std::memory_order_acq_rel)) return -EAGAIN;
    return vlc_metadata_job_Apply(m_work->job.get(),target,uint32_t(1)<<vlc_meta_ArtworkURL,nullptr);
}
void ArtworkSelection::Receipt::cancel() noexcept { if(m_work) m_work->cancel(); }
ArtworkSelection::ArtworkSelection(Lifetime::Handle lifetime,QString directory,QObject* parent)
    : ArtworkSelection(std::move(lifetime),std::move(directory),Options{},Backend{},parent) {}
ArtworkSelection::ArtworkSelection(Lifetime::Handle lifetime,QString directory,Options options,Backend backend,QObject* parent)
    : QObject(parent),m_lifetime(std::move(lifetime)),m_directory(std::move(directory)),m_options(options),m_backend(std::move(backend)),m_mailbox(std::make_shared<Mailbox>())
{
    m_options.maximumEncodedBytes=std::clamp<qint64>(options.maximumEncodedBytes,1,512*1024*1024);
    m_options.maximumCacheBytes=std::clamp<qint64>(options.maximumCacheBytes,1,512*1024*1024);
    m_options.maximumCacheEntries=std::clamp(options.maximumCacheEntries,1,8192);
    m_options.maximumOutputDimension=std::clamp(options.maximumOutputDimension,1,4096);
    m_options.previewDimension=std::clamp(options.previewDimension,1,512);
    if(!m_backend.read) m_backend.read=readFile;
    if(!m_backend.decode) m_backend.decode=[](const QByteArray& bytes,int maximum,QString* error) {
        return vlc::addons::AddonArtworkImage::decodeSelected(bytes,maximum,error);
    };
    const vlc_metadata_backend_operations operations{dispatch,canceled,nullptr};
    m_native={vlc_metadata_backend_New(&operations,nullptr),[](vlc_metadata_backend_t* value) { if(value) vlc_metadata_backend_Release(value); }};
    m_timer.setInterval(10); connect(&m_timer,&QTimer::timeout,this,[this] { poll(); });
}
ArtworkSelection::~ArtworkSelection() { close(); }
bool ArtworkSelection::localFile(const QUrl& url)
{
    const auto bytes=url.toEncoded(); return url.isLocalFile() && url.host().isEmpty() && !url.hasQuery() && !url.hasFragment()
        && vlc_artwork_uri_IsLocalFile(bytes.constData());
}
quint64 ArtworkSelection::prepare(input_item_t* target,const QUrl& selected,Complete complete)
{
    cancel();
    if(m_closed || !target || !complete || !m_native || !m_lifetime.current() || !localFile(selected)
        || m_generation==std::numeric_limits<quint64>::max()) return 0;
    ++m_generation;
    const auto native=m_native.get();
    std::shared_ptr<Work> work;
    try {
        const auto permit=retain(); if(!permit) return 0;
        for(unsigned attempt=0;attempt<4;++attempt) {
            vlc_metadata_revisions before{},after{};
            const auto snapshot=adopted(vlc_metadata_item_Snapshot(target,&before)); if(!snapshot) return 0;
            const char* fields[]{"artwork"}; vlc_metadata_request request{};
            request.feature="artwork"; request.operation="enrich"; request.fields=fields; request.field_count=1;
            request.item=target; request.generation=m_generation;
            Job job(vlc_metadata_job_New(native,&request,ended,nullptr),releaseJob); if(!job) return 0;
            const auto second=adopted(vlc_metadata_item_Snapshot(target,&after));
            const auto* captured=vlc_metadata_job_GetRequest(job.get())->item;
            const auto source=QByteArray(snapshot->psz_uri ? snapshot->psz_uri : "");
            auto* raw=vlc_input_policy_HoldItem(snapshot.get()); const std::shared_ptr<vlc_input_policy_t> policy(raw,vlc_input_policy_Release);
            raw=vlc_input_policy_HoldItem(const_cast<input_item_t*>(captured)); const std::shared_ptr<vlc_input_policy_t> capturedPolicy(raw,vlc_input_policy_Release);
            if(!second || !revisionsEqual(before,after) || source!=(captured->psz_uri ? captured->psz_uri : "") || policy!=capturedPolicy) continue;
            work=std::make_shared<Work>(); work->target=held(target); work->job=std::move(job); work->permit=permit;
            work->baseline=after; work->source=source; work->policy=policy; work->generation=m_generation;
            work->owner=m_mailbox->open; work->lifetime=m_lifetime;
            if(vlc_metadata_job_StartLocal(work->job.get())!=VLC_SUCCESS) return 0;
            break;
        }
        if(!work) return 0;
        const auto mailbox=m_mailbox; const auto path=selected.toLocalFile(); const auto directory=m_directory;
        const auto options=m_options; const auto backend=m_backend;
        m_work=work; m_complete=std::move(complete); m_timer.start();
        const bool accepted=m_lifetime.startQtWork([work,mailbox,path,directory,options,backend] {
            struct Active { bool held=false; ~Active() { if(held) {
                { std::lock_guard<std::mutex> lock(budget().mutex); budget().active.fetch_sub(1,std::memory_order_release); }
                budget().changed.notify_all();
            } } } active;
            {
                std::unique_lock<std::mutex> lock(budget().mutex);
                budget().changed.wait(lock,[&] { return !work->live() || budget().active.load(std::memory_order_acquire)==0; });
                if(!work->live()) return;
                budget().active.fetch_add(1,std::memory_order_acq_rel); active.held=true;
            }
            Result result; result.generation=work->generation;
            const Current current=[work] { return work->live(); };
            try {
                const auto bytes=backend.read(path,options.maximumEncodedBytes,current,&result.error);
                if(!current()) return;
                if(bytes.isEmpty() || bytes.size()>options.maximumEncodedBytes) result.error=failure();
                auto pixels=result.error.isEmpty() ? backend.decode(bytes,options.maximumOutputDimension,&result.error) : QImage{};
                if(!current()) return;
                if(!pixels.isNull()) {
                    QByteArray encoded;
                    for(unsigned attempt=0;attempt<2 && current();++attempt) {
                        BoundedOutput output(80*1024*1024);
                        if(pixels.save(&output,"PNG")) { encoded=std::move(output.bytes); break; }
                        if(pixels.width()<=2048 && pixels.height()<=2048) break;
                        pixels=pixels.scaled(QSize(2048,2048),Qt::KeepAspectRatio,Qt::SmoothTransformation);
                    }
                    if(encoded.isEmpty()) result.error=failure();
                    else {
                        const auto saved=cache(directory,encoded,options,current);
                        if(saved.isEmpty()) result.error=failure();
                        else {
                            work->file=QUrl::fromLocalFile(saved);
                            const auto copy=adopted(vlc_metadata_job_CopyLocalItem(work->job.get()));
                            const auto uri=work->file.toEncoded();
                            if(!copy || vlc_artwork_item_SetApprovedURI(copy.get(),uri.constData())!=VLC_SUCCESS
                                || vlc_metadata_job_PrepareLocal(work->job.get(),copy.get(),nullptr)!=VLC_SUCCESS
                                || !vlc_metadata_job_Complete(work->job.get(),VLC_SUCCESS,copy.get(),nullptr,0)) result.error=failure();
                            else {
                                result.preview=pixels.scaled(QSize(options.previewDimension,options.previewDimension),Qt::KeepAspectRatio,Qt::SmoothTransformation);
                                result.receipt=std::shared_ptr<Receipt>(new Receipt(work));
                            }
                        }
                    }
                } else if(result.error.isEmpty()) result.error=failure();
            } catch(...) { result.error=failure(); }
            if(!current()) return;
            std::lock_guard<std::mutex> lock(mailbox->mutex);
            if(mailbox->open->load(std::memory_order_acquire) && work->live()) mailbox->result=std::move(result);
        },[work] { work->cancel(); });
        if(!accepted) { cancel(); return 0; }
    } catch(...) { cancel(); return 0; }
    return m_generation;
}
void ArtworkSelection::poll()
{
    if(!m_lifetime.current()) { cancel(); return; }
    std::optional<Result> result;
    { std::lock_guard<std::mutex> lock(m_mailbox->mutex); result.swap(m_mailbox->result); }
    if(!result || result->generation!=m_generation || !m_work || m_closed) return;
    if(!result->receipt) m_work->cancel();
    m_work.reset(); m_timer.stop(); m_receipt=result->receipt;
    Complete complete; complete.swap(m_complete);
    if(complete) try { complete(std::move(*result)); } catch(...) {}
}
void ArtworkSelection::cancel()
{
    if(m_work) m_work->cancel();
    m_work.reset();
    if(const auto receipt=m_receipt.lock()) receipt->cancel();
    m_receipt.reset();
    m_complete={}; m_timer.stop();
    { std::lock_guard<std::mutex> lock(m_mailbox->mutex); m_mailbox->result.reset(); }
}
void ArtworkSelection::close()
{
    if(m_closed) return;
    m_closed=true; m_mailbox->open->store(false,std::memory_order_release); cancel();
    if(m_native) vlc_metadata_backend_Close(m_native.get());
}
unsigned ArtworkSelection::retainedCount() noexcept { return budget().retained.load(std::memory_order_acquire); }
unsigned ArtworkSelection::activeCount() noexcept { return budget().active.load(std::memory_order_acquire); }

#include "artwork_store.hpp"
struct ArtworkNativePreview::State {
    std::atomic_bool open{true};
    std::atomic<quint64> generation{0};
    std::mutex mutex;
    struct Result { quint64 generation; QImage pixels; };
    std::optional<Result> result;
};
ArtworkNativePreview::ArtworkNativePreview(Lifetime::Handle lifetime,QObject* parent)
    : QObject(parent),m_lifetime(std::move(lifetime)),m_state(std::make_shared<State>())
{
    m_timer.setInterval(10); connect(&m_timer,&QTimer::timeout,this,[this] { poll(); });
}
ArtworkNativePreview::~ArtworkNativePreview() { close(); }
void ArtworkNativePreview::request(QString path,Complete complete)
{
    cancel();
    if(!complete || path.isEmpty() || !QDir::isAbsolutePath(path) || !m_lifetime.current() || !m_state->open.load()) return;
    const auto generation=m_state->generation.load(std::memory_order_acquire);
    const auto state=m_state; const auto lifetime=m_lifetime;
    try {
        const auto permit=retain(); if(!permit) return;
        m_complete=std::move(complete); m_timer.start();
        const bool accepted=m_lifetime.startQtWork([state,lifetime,generation,path] {
            const auto current=[&] { return state->open.load(std::memory_order_acquire)
                && state->generation.load(std::memory_order_acquire)==generation && lifetime.current(); };
            struct Active { bool held=false; ~Active() { if(held) {
                { std::lock_guard<std::mutex> lock(budget().mutex); budget().active.fetch_sub(1,std::memory_order_release); }
                budget().changed.notify_all();
            } } } active;
            {
                std::unique_lock<std::mutex> lock(budget().mutex);
                budget().changed.wait(lock,[&] { return !current() || budget().active.load(std::memory_order_acquire)==0; });
                if(!current()) return;
                budget().active.fetch_add(1,std::memory_order_acq_rel); active.held=true;
            }
            QImage pixels;
            try { pixels=ArtworkStore::read(path,512); } catch(...) {}
            std::lock_guard<std::mutex> lock(state->mutex);
            if(current()) state->result=State::Result{generation,std::move(pixels)};
        },[state] {
            { std::lock_guard<std::mutex> lock(budget().mutex); state->open.store(false,std::memory_order_release); }
            budget().changed.notify_all();
        },
        [permit](auto ticket) { if(!ticket.retain(permit)) throw std::bad_alloc(); });
        if(!accepted) cancel();
    } catch(...) { cancel(); }
}
void ArtworkNativePreview::poll()
{
    if(!m_lifetime.current()) { cancel(); return; }
    std::optional<State::Result> result;
    { std::lock_guard<std::mutex> lock(m_state->mutex); result.swap(m_state->result); }
    if(!result || result->generation!=m_state->generation.load(std::memory_order_acquire) || !m_state->open.load()) return;
    m_timer.stop(); Complete complete; complete.swap(m_complete);
    if(complete) try { complete(std::move(result->pixels)); } catch(...) {}
}
void ArtworkNativePreview::cancel()
{
    {
        std::lock_guard<std::mutex> lock(budget().mutex);
        const auto generation=m_state->generation.load(std::memory_order_relaxed);
        if(generation==std::numeric_limits<quint64>::max()) m_state->open.store(false,std::memory_order_release);
        else m_state->generation.store(generation+1,std::memory_order_release);
    }
    m_complete={}; m_timer.stop(); budget().changed.notify_all();
    { std::lock_guard<std::mutex> lock(m_state->mutex); m_state->result.reset(); }
}
void ArtworkNativePreview::close()
{ m_state->open.store(false,std::memory_order_release); cancel(); }
