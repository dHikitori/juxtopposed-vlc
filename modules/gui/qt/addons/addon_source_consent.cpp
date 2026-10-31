// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_source_consent.hpp"
#include "addon_source_admissions.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <atomic>
#include <array>
#include <condition_variable>

namespace vlc::addons {
namespace {
constexpr qint64 MaximumBytes=1024*1024;
constexpr quint64 MaximumRevision=9007199254740991ULL, GenerationWindow=4096;
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool validUuid(const QString& value)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\\z"));
    return pattern.match(value).hasMatch() && !QUuid(value).isNull();
}
bool text(const QString& value,qsizetype maximum)
{
    if (value.isEmpty() || value.size()>maximum) return false;
    for (qsizetype i=0;i<value.size();++i) {
        const auto c=value[i];
        if (c.category()==QChar::Other_Control || c.isLowSurrogate()) return false;
        if (c.isHighSurrogate() && (++i>=value.size() || !value[i].isLowSurrogate())) return false;
    }
    return true;
}
QString kind(AddonSourceConsent::SourceKind value)
{
    switch (value) {
    case AddonSourceConsent::SourceKind::Git: return QStringLiteral("git");
    case AddonSourceConsent::SourceKind::Developer: return QStringLiteral("developer");
    case AddonSourceConsent::SourceKind::Distribution: return QStringLiteral("distribution");
    }
    return {};
}
QJsonObject bindingJson(const AddonSourceConsent::Binding& value)
{ return {{"kind",kind(value.kind)},{"identity",value.identity},{"contribution",value.contribution},{"account",value.accountReference},{"profile",value.profile}}; }
bool sameKeys(const QJsonObject& value,const QStringList& names)
{ if (value.size()!=names.size()) return false; for (const auto& name:names) if (!value.contains(name)) return false; return true; }
bool fail(QString* error,const QString& value) { if (error) *error=value; return false; }
QString savedError() { return QObject::tr("The saved source approvals are not correct."); }
qint64 monotonic()
{
    static const auto start=std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
}
bool uriMatches(const AddonSourceConsent::Binding& binding,const QString& uri)
{
    const auto ref=AddonSourceAdmissions::decodeUntrusted(uri);
    return !ref.isEmpty() && ref.value("source").toString()==binding.contribution
        && ref.value("accountReference").toString()==binding.accountReference;
}
QString profilePath(const QString& directory)
{
    QString path=QDir::cleanPath(QFileInfo(directory).absoluteFilePath()); QStringList suffix;
    while (true) {
        const QFileInfo info(path); const auto canonical=info.canonicalFilePath();
        if (!canonical.isEmpty()) { path=canonical; for (const auto& part:suffix) path=QDir(path).filePath(part); break; }
        const auto parent=info.absolutePath(); if (parent==path) break;
        suffix.prepend(info.fileName()); path=parent;
    }
#ifdef Q_OS_WIN
    path=path.toCaseFolded();
#endif
    return QDir::cleanPath(path);
}
template<class Epoch> void advance(const std::shared_ptr<Epoch>& value) noexcept
{
    auto revision=value->revision.load(std::memory_order_relaxed);
    while (revision!=std::numeric_limits<quint64>::max())
        if (value->revision.compare_exchange_weak(revision,revision+1,std::memory_order_release,std::memory_order_relaxed)) return;
    value->retired.store(true,std::memory_order_release);
}
template<class Epoch> bool live(const Epoch& value,quint64 revision) noexcept
{
    return value.allowed.load(std::memory_order_acquire) && !value.retired.load(std::memory_order_acquire)
        && !value.writers.load(std::memory_order_acquire) && value.revision.load(std::memory_order_acquire)==revision;
}
template<class Epoch> struct LiveMutation {
    std::shared_ptr<Epoch> value;
    std::optional<StoreAuthorityMutation> authority;
    explicit LiveMutation(std::shared_ptr<Epoch> epoch):value(std::move(epoch)) {
        value->writers.fetch_add(1,std::memory_order_acq_rel); advance(value);
        authority.emplace(value->authority.mutation());
    }
    ~LiveMutation() {
        advance(value); authority.reset();
        value->writers.fetch_sub(1,std::memory_order_release);
        // Store mutation completion can wake before this outer writer ends.
        // Send a final display wake after every live flag is observable.
        value->authority.displayUnlocked();
    }
};
}
struct AddonSourceConsent::LiveEpoch {
    explicit LiveEpoch(const QString& directory):authority(directory) {}
    StoreAuthority authority;
    std::atomic<quint64> revision{0};
    std::atomic_uint writers{0};
    std::atomic_bool allowed{true},retired{false};
};
struct AddonSourceConsent::LiveOwner {
    explicit LiveOwner(const QString& directory):authority(directory) {}
    StoreAuthority authority;
    std::atomic_bool open{true},allowed{true};
};
struct AddonSourceConsent::LiveProfile {
    explicit LiveProfile(QString path):directory(std::move(path)),
        reset(std::make_shared<LiveEpoch>(QDir(directory).filePath(QStringLiteral("authority/reset")))) {}
    const QString directory;
    std::shared_ptr<LiveEpoch> reset;
    std::mutex mutex;
    QHash<QString,std::shared_ptr<LiveEpoch>> entries;
};
namespace {
struct ConsentNativeCancel {
    explicit ConsentNativeCancel(std::function<void()> value):cancel(std::move(value)) {}
    const std::function<void()> cancel;
    std::atomic_uint phase{0};
    std::mutex mutex;
    std::condition_variable ended;
    void fire() noexcept {
        unsigned expected=0;
        if (phase.compare_exchange_strong(expected,1,std::memory_order_acq_rel)) {
            try { if(cancel) cancel(); } catch(...) {}
            { std::lock_guard<std::mutex> lock(mutex); phase.store(2,std::memory_order_release); }
            ended.notify_all();
        } else if(expected==1) {
            std::unique_lock<std::mutex> lock(mutex);
            ended.wait(lock,[this] { return phase.load(std::memory_order_acquire)==2; });
        }
    }
};
}
AddonSourceConsent::DisplayChanges::operator bool() const noexcept {
    if(!m_count) return false;
    for(unsigned index=0;index<m_count;++index) if(!m_changes[index]) return false;
    return true;
}
AddonSourceConsent::DisplayChanges AddonSourceConsent::displayChanges(std::function<void()> wake) const noexcept {
    try {
        DisplayChanges changes;
        if(!wake || !m_liveOwner || !m_liveProfile || !m_liveProfile->reset) return changes;
        changes.m_changes[0]=m_packageAuthority.onChanged(wake);
        changes.m_changes[1]=m_liveOwner->authority.onChanged(wake);
        changes.m_changes[2]=m_liveProfile->reset->authority.onChanged(std::move(wake));
        changes.m_count=3;
        if(!changes) return {};
        return changes;
    } catch(...) { return {}; }
}
AddonSourceConsent::DisplayChanges AddonSourceConsent::LiveToken::entryChanges(std::function<void()> wake) const noexcept {
    try {
        DisplayChanges changes;
        if(!wake || !m_entry || !m_receipt) return changes;
        changes.m_changes[0]=m_entry->authority.onChanged(wake);
        changes.m_changes[1]=m_receipt->authority.onChanged(std::move(wake));
        changes.m_count=2;
        if(!changes) return {};
        return changes;
    } catch(...) { return {}; }
}
struct AddonSourceConsent::LiveWatch::State {
    // Release the shared call before its retained native watch quotas.
    std::array<StoreAuthorityWatch,5> watches;
    std::shared_ptr<ConsentNativeCancel> call;
};
AddonSourceConsent::LiveWatch::operator bool() const noexcept {
    if(!m_state || m_state->call->phase.load(std::memory_order_acquire)!=0) return false;
    for(const auto& watch:m_state->watches) if(!watch) return false;
    return true;
}
AddonSourceConsent::LiveWatch AddonSourceConsent::LiveToken::onInvalidated(std::function<void()> nativeCancel) const noexcept {
    std::shared_ptr<ConsentNativeCancel> call;
    std::shared_ptr<LiveWatch::State> state;
    std::function<void()> cancel;
    try {
        call=std::make_shared<ConsentNativeCancel>(std::move(nativeCancel));
        if(!current()) { call->fire(); return {}; }
        state=std::make_shared<LiveWatch::State>(); state->call=call;
        cancel=[call] { call->fire(); };
        state->watches[0]=m_package.onInvalidated(cancel);
        state->watches[1]=m_ownerAuthority.onInvalidated(cancel);
        state->watches[2]=m_receiptAuthority.onInvalidated(cancel);
        state->watches[3]=m_profileAuthority.onInvalidated(cancel);
        state->watches[4]=m_entryAuthority.onInvalidated(cancel);
        LiveWatch watch; watch.m_state=state;
        if(!current() || !watch) {
            call->fire(); cancel={}; call.reset(); return {};
        }
        return watch;
    } catch(...) {
        if(call) call->fire(); else try { if(nativeCancel) nativeCancel(); } catch(...) {}
        cancel={}; call.reset(); state.reset(); return {};
    }
}
std::shared_ptr<AddonSourceConsent::LiveProfile> AddonSourceConsent::liveProfile(const QString& directory)
{
    struct Registry { std::mutex mutex; QHash<QString,std::shared_ptr<LiveProfile>> profiles; };
    static Registry registry;
    const auto key=profilePath(directory); std::lock_guard<std::mutex> lock(registry.mutex);
    for (auto it=registry.profiles.begin();it!=registry.profiles.end();) {
        bool denied=false; const bool idle=it.value().use_count()==1;
        if (idle) {
            auto& profile=*it.value(); std::lock_guard<std::mutex> entries(profile.mutex);
            denied=!profile.reset->allowed.load(std::memory_order_acquire);
            for (const auto& entry:profile.entries) denied|=!entry->allowed.load(std::memory_order_acquire);
        }
        if (idle && (!denied || !QFileInfo::exists(it.key()))) it=registry.profiles.erase(it); else ++it;
    }
    auto profile=registry.profiles.value(key);
    if (!profile && registry.profiles.size()>=64) {
        static const auto unavailable=[] { auto value=std::make_shared<LiveProfile>(QStringLiteral("/hakone-consent-unavailable")); value->reset->allowed.store(false); value->reset->retired.store(true); return value; }();
        return unavailable;
    }
    if (!profile) { profile=std::make_shared<LiveProfile>(key); registry.profiles.insert(key,profile); }
    return profile;
}
std::shared_ptr<AddonSourceConsent::LiveEpoch> AddonSourceConsent::liveEntry(const QString& key) const
{
    std::lock_guard<std::mutex> lock(m_liveProfile->mutex); auto entry=m_liveProfile->entries.value(key);
    if (!entry && m_liveProfile->entries.size()>=512) {
        m_liveProfile->reset->allowed.store(false,std::memory_order_release); advance(m_liveProfile->reset);
        m_liveProfile->reset->authority.invalidate(); return {};
    }
    if (!entry) { entry=std::make_shared<LiveEpoch>(QDir(m_liveProfile->directory).filePath(QStringLiteral("authority/entries/")+key)); m_liveProfile->entries.insert(key,entry); }
    return entry;
}
bool AddonSourceConsent::liveAllowed(const QString& key) const
{
    if (!m_liveOwner->open.load(std::memory_order_acquire)) return false;
    const auto reset=m_liveProfile->reset;
    if (!live(*reset,reset->revision.load(std::memory_order_acquire))) return false;
    std::lock_guard<std::mutex> lock(m_liveProfile->mutex); const auto entry=m_liveProfile->entries.value(key);
    return !entry || live(*entry,entry->revision.load(std::memory_order_acquire));
}
AddonSourceConsent::LiveToken AddonSourceConsent::liveToken(const QString& key,const StoreAuthorityToken& package) const
{
    LiveToken token; token.m_owner=m_liveOwner; token.m_receipt=std::make_shared<LiveOwner>(QDir(m_liveProfile->directory).filePath(QStringLiteral("authority/receipts/")+uuid())); token.m_profile=m_liveProfile;
    token.m_entry=liveEntry(key); token.m_package=package;
    token.m_profileRevision=m_liveProfile->reset->revision.load(std::memory_order_acquire);
    if (token.m_entry) token.m_entryRevision=token.m_entry->revision.load(std::memory_order_acquire);
    token.m_ownerAuthority=m_liveOwner->authority.capture(); token.m_receiptAuthority=token.m_receipt->authority.capture();
    token.m_profileAuthority=m_liveProfile->reset->authority.capture();
    if(token.m_entry) token.m_entryAuthority=token.m_entry->authority.capture();
    return token;
}
bool AddonSourceConsent::LiveToken::current() const noexcept
{
    return m_owner && m_receipt && m_profile && m_entry && m_owner->open.load(std::memory_order_acquire) && m_owner->allowed.load(std::memory_order_acquire)
        && m_receipt->open.load(std::memory_order_acquire) && m_package.current()
        && live(*m_profile->reset,m_profileRevision) && live(*m_entry,m_entryRevision)
        && m_owner->open.load(std::memory_order_acquire) && m_owner->allowed.load(std::memory_order_acquire) && m_receipt->open.load(std::memory_order_acquire)
        && live(*m_profile->reset,m_profileRevision) && live(*m_entry,m_entryRevision) && m_package.current()
        && m_ownerAuthority.current() && m_receiptAuthority.current() && m_profileAuthority.current() && m_entryAuthority.current();
}
struct AddonSourceConsent::State {
    QString epoch;
    quint64 revision=0;
    QJsonObject approvals;
};
bool AddonSourceConsent::Binding::operator==(const Binding& other) const
{ return kind==other.kind && identity==other.identity && contribution==other.contribution && accountReference==other.accountReference && profile==other.profile; }
AddonSourceConsent::AddonSourceConsent(QString path,Checks checks)
    : AddonSourceConsent(std::move(path),std::move(checks),Options{}) {}
AddonSourceConsent::AddonSourceConsent(QString path,Checks checks,Options options)
    : m_directory(QDir(path).filePath(QStringLiteral("source-consent"))),
      m_file(QDir(m_directory).filePath(QStringLiteral("approvals.json"))),m_owner(uuid()),m_packageAuthority(path),
      m_liveProfile(liveProfile(m_directory)),m_liveOwner(std::make_shared<LiveOwner>(QDir(m_directory).filePath(QStringLiteral("authority/owners/")+m_owner))),m_checks(std::move(checks)),m_options(std::move(options))
{
    m_options.approvals=std::clamp(m_options.approvals,1,256); m_options.tickets=std::clamp(m_options.tickets,1,64); m_options.intents=std::clamp(m_options.intents,1,128);
    m_options.ticketLifetimeMs=std::clamp<qint64>(m_options.ticketLifetimeMs,1,300000);
    m_options.intentLifetimeMs=std::clamp<qint64>(m_options.intentLifetimeMs,1,30000);
    if (!m_options.now) m_options.now=monotonic;
}
AddonSourceConsent::~AddonSourceConsent() { close(); }
std::optional<AddonSourceConsent::Binding> AddonSourceConsent::canonical(const Binding& input)
{
    static const QRegularExpression contribution(QStringLiteral("^[a-z][a-z0-9.-]{1,127}/[a-z][a-z0-9.-]{0,63}\\z"));
    if (kind(input.kind).isEmpty() || !text(input.identity,2048) || !contribution.match(input.contribution).hasMatch()
        || !text(input.accountReference,512) || input.accountReference.startsWith(QLatin1String("disconnected:"))
        || input.profile!=QLatin1String("public-https-media@1")) return {};
    Binding result=input; QUrl url(input.identity,QUrl::StrictMode);
    if (url.isValid() && url.scheme()==QLatin1String("https") && !url.host().isEmpty()
        && (url.userInfo().isEmpty() && !url.authority().contains(QLatin1Char('@'))) && !url.hasQuery() && !url.hasFragment() && url.port(-1)!=0) {
        const auto decoded=url.path(QUrl::FullyDecoded);
        for (auto c:decoded) if (c.category()==QChar::Other_Control) return {};
        url.setHost(url.host().toLower()); if (url.port(-1)==443) url.setPort(-1);
        result.identity=QString::fromLatin1(url.adjusted(QUrl::NormalizePathSegments).toEncoded(QUrl::FullyEncoded));
    } else if (input.kind==SourceKind::Developer && url.isLocalFile() && url.host().isEmpty() && (url.userInfo().isEmpty() && !url.authority().contains(QLatin1Char('@')))
               && !url.hasQuery() && !url.hasFragment() && QDir::isAbsolutePath(url.toLocalFile())) {
        for (auto c:url.toLocalFile()) if (c.category()==QChar::Other_Control) return {};
        result.identity=QString::fromLatin1(QUrl::fromLocalFile(QDir::cleanPath(url.toLocalFile())).toEncoded(QUrl::FullyEncoded));
    } else if (input.kind==SourceKind::Distribution
               && QRegularExpression(QStringLiteral("^hakone-distribution:[a-z][a-z0-9.-]{1,127}\\z")).match(input.identity).hasMatch()) {}
    else return {};
    return result.identity.size()<=2048?std::optional<Binding>(result):std::nullopt;
}
QString AddonSourceConsent::key(const Binding& binding)
{ return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(bindingJson(binding)).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex()); }
bool AddonSourceConsent::reasonValid(IntentReason value)
{ return value==IntentReason::PrimaryPlayback || value==IntentReason::QueueAdvance || value==IntentReason::Replay; }
bool AddonSourceConsent::sourceCurrent(const Binding& value) const
{ try { return m_checks.current && m_checks.current(value); } catch (...) { return false; } }
bool AddonSourceConsent::admissionCurrent(const Binding& value,const QString& uri) const
{ try { return uriMatches(value,uri) && m_checks.admitted && m_checks.admitted(value,uri); } catch (...) { return false; } }
qint64 AddonSourceConsent::prune()
{
    qint64 now=-1; try { now=m_options.now(); } catch (...) {}
    if (now<0 || now<m_lastNow) { m_tickets.clear(); m_intents.clear(); return -1; }
    m_lastNow=now;
    for (auto it=m_tickets.begin();it!=m_tickets.end();) if (it->expires<=now) it=m_tickets.erase(it); else ++it;
    for (auto it=m_intents.begin();it!=m_intents.end();) if (it->expires<=now) it=m_intents.erase(it); else ++it;
    return now;
}
bool AddonSourceConsent::directory(QString* error) const
{
    if (QFileInfo(m_directory).isSymLink() || QFileInfo(m_file).isSymLink()
        || QFileInfo(QDir(m_directory).filePath(QStringLiteral("approvals.lock"))).isSymLink()) return fail(error,savedError());
    return QDir().mkpath(m_directory) || fail(error,QObject::tr("The app cannot create the source approval store."));
}
bool AddonSourceConsent::load(State* output,QString* error) const
{
    *output={}; QFile file(m_file);
    if (QFileInfo(m_directory).isSymLink() || QFileInfo(m_file).isSymLink()) return fail(error,savedError());
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly) || file.size()>MaximumBytes) return fail(error,savedError());
    const auto bytes=file.read(MaximumBytes+1);
    if (bytes.size()>MaximumBytes || !file.atEnd()) return fail(error,savedError());
    QJsonParseError parse; const auto document=QJsonDocument::fromJson(bytes,&parse); const auto root=document.object();
    const auto revision=root.value("revision").toDouble(-1);
    if (parse.error!=QJsonParseError::NoError || !document.isObject() || !sameKeys(root,{"version","epoch","revision","approvals"})
        || root.value("version")!=QJsonValue(1) || !validUuid(root.value("epoch").toString()) || !root.value("revision").isDouble()
        || !std::isfinite(revision) || revision<0 || revision>double(MaximumRevision) || std::floor(revision)!=revision
        || !root.value("approvals").isObject() || root.value("approvals").toObject().size()>256) return fail(error,savedError());
    const auto records=root.value("approvals").toObject();
    for (auto it=records.begin();it!=records.end();++it) {
        const auto record=it.value().toObject(), saved=record.value("binding").toObject(); Binding binding;
        const auto type=saved.value("kind").toString();
        if (type==QLatin1String("git")) binding.kind=SourceKind::Git;
        else if (type==QLatin1String("developer")) binding.kind=SourceKind::Developer;
        else if (type==QLatin1String("distribution")) binding.kind=SourceKind::Distribution;
        else return fail(error,savedError());
        binding.identity=saved.value("identity").toString(); binding.contribution=saved.value("contribution").toString();
        binding.accountReference=saved.value("account").toString(); binding.profile=saved.value("profile").toString();
        const auto normalized=canonical(binding);
        if (!sameKeys(record,{"binding","id"}) || !validUuid(record.value("id").toString()) || !normalized
            || bindingJson(*normalized)!=saved || key(*normalized)!=it.key()) return fail(error,savedError());
    }
    output->epoch=root.value("epoch").toString(); output->revision=quint64(revision); output->approvals=records; return true;
}
bool AddonSourceConsent::save(const State& state,QString* error) const
{
    const auto bytes=QJsonDocument(QJsonObject{{"version",1},{"epoch",state.epoch},{"revision",double(state.revision)},{"approvals",state.approvals}}).toJson(QJsonDocument::Compact);
    if (bytes.size()>MaximumBytes) return fail(error,QObject::tr("The source approval store is larger than the limit."));
    QSaveFile file(m_file); file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner)
        || file.write(bytes)!=bytes.size() || !file.commit()) return fail(error,QObject::tr("The app cannot save source playback approval."));
    return true;
}
std::optional<AddonSourceConsent::Ticket> AddonSourceConsent::capture(const Binding& value,QString* error)
{
    const auto binding=canonical(value);
    if (!binding || !sourceCurrent(*binding)) { fail(error,QObject::tr("The source or account is not available.")); return {}; }
    std::lock_guard<std::mutex> guard(m_mutex); const auto now=prune();
    if (m_closed || m_denyAll || !m_liveProfile->reset->allowed.load(std::memory_order_acquire) || m_liveProfile->reset->retired.load(std::memory_order_acquire)
        || now<0 || m_tickets.size()>=m_options.tickets || now>std::numeric_limits<qint64>::max()-m_options.ticketLifetimeMs) return {};
    if (!directory(error)) return {};
    QLockFile lock(QDir(m_directory).filePath(QStringLiteral("approvals.lock"))); if (!lock.tryLock(0)) { fail(error,QObject::tr("The source approval store is busy. Try again.")); return {}; }
    State state; if (!load(&state,error)) return {};
    if (state.epoch.isEmpty()) { LiveMutation mutation(m_liveProfile->reset); state.epoch=uuid(); if (!save(state,error)) return {}; }
    Ticket ticket{uuid(),*binding}; m_tickets.insert(ticket.id,{ticket,state.epoch,state.revision,now+m_options.ticketLifetimeMs,m_ticketEpoch}); return ticket;
}
bool AddonSourceConsent::grant(const QString& token,QString* error)
{
    Dialog dialog;
    { std::lock_guard<std::mutex> guard(m_mutex); if (m_closed || prune()<0 || !m_tickets.contains(token)) return fail(error,QObject::tr("The approval request expired. Open it again.")); dialog=m_tickets.take(token); }
    if (!sourceCurrent(dialog.ticket.binding)) return fail(error,QObject::tr("The source or account changed. Open approval again."));
    std::lock_guard<std::mutex> guard(m_mutex);
    const auto now=prune();
    if (m_closed || m_denyAll || !m_liveProfile->reset->allowed.load(std::memory_order_acquire) || m_liveProfile->reset->retired.load(std::memory_order_acquire)
        || now<0 || now>=dialog.expires || dialog.localEpoch!=m_ticketEpoch || !directory(error)) return false;
    QLockFile lock(QDir(m_directory).filePath(QStringLiteral("approvals.lock"))); if (!lock.tryLock(0)) return fail(error,QObject::tr("The source approval store is busy. Try again."));
    State state; if (!load(&state,error)) return false;
    if (state.epoch!=dialog.epoch || state.revision!=dialog.revision || state.revision>=MaximumRevision) return fail(error,QObject::tr("The saved approval changed. Open approval again."));
    const auto id=key(dialog.ticket.binding);
    if (!state.approvals.contains(id) && state.approvals.size()>=m_options.approvals) return fail(error,QObject::tr("The app has the maximum number of source approvals."));
    const auto entry=liveEntry(id); if (!entry) return fail(error,savedError());
    LiveMutation mutation(entry);
    state.approvals.insert(id,QJsonObject{{"binding",bindingJson(dialog.ticket.binding)},{"id",uuid()}}); ++state.revision;
    if (!save(state,error)) return false;
    entry->allowed.store(true,std::memory_order_release); m_withdrawn.remove(id); return true;
}
bool AddonSourceConsent::approved(const Binding& value) const
{
    const auto binding=canonical(value); if (!binding || !sourceCurrent(*binding)) return false;
    std::lock_guard<std::mutex> guard(m_mutex); const auto id=key(*binding); State state;
    if (!liveAllowed(id)) return false;
    return !m_closed && !m_denyAll && !m_withdrawn.contains(id) && load(&state) && state.approvals.contains(id);
}
std::optional<AddonSourceConsent::LiveToken> AddonSourceConsent::approvalToken(const Binding& value) const
{
    const auto package=m_packageAuthority.capture(); const auto binding=canonical(value);
    if(!package.current() || !binding || !sourceCurrent(*binding)) return {};
    std::lock_guard<std::mutex> guard(m_mutex); const auto id=key(*binding);
    const auto token=liveToken(id,package); State state;
    if(m_closed || m_denyAll || m_withdrawn.contains(id) || !token.current()
        || !load(&state) || !state.approvals.contains(id) || !token.current()) return {};
    return token;
}
std::function<void()> AddonSourceConsent::nativeBindingRevoker(const Binding& value) const
{
    const auto binding=canonical(value); if(!binding) return {};
    const auto entry=liveEntry(key(*binding)); if(!entry) return {};
    return [entry] { LiveMutation mutation(entry); entry->allowed.store(false,std::memory_order_release); };
}
std::function<void()> AddonSourceConsent::nativeProfileRevoker() const
{
    const auto reset=m_liveProfile->reset;
    return [reset] { LiveMutation mutation(reset); reset->allowed.store(false,std::memory_order_release); };
}
std::function<void()> AddonSourceConsent::nativeOwnerRevoker() const
{
    const auto owner=m_liveOwner;
    return [owner] { owner->open.store(false,std::memory_order_release); owner->authority.invalidate(); };
}
bool AddonSourceConsent::revoke(const Binding& value,QString* error)
{
    const auto binding=canonical(value); if (!binding) return fail(error,savedError());
    if (!m_liveOwner->open.load(std::memory_order_acquire)) return false;
    const auto id=key(*binding); const auto entry=liveEntry(id); if (!entry) return fail(error,savedError());
    LiveMutation mutation(entry); entry->allowed.store(false,std::memory_order_release);
    std::lock_guard<std::mutex> guard(m_mutex); if (m_closed) return false;
    ++m_ticketEpoch;
    if (m_withdrawn.size()<256) m_withdrawn.insert(id);
    else { m_denyAll=true; m_liveOwner->allowed.store(false,std::memory_order_release); m_liveOwner->authority.invalidate(); }
    for (auto it=m_tickets.begin();it!=m_tickets.end();) if (key(it->ticket.binding)==id) it=m_tickets.erase(it); else ++it;
    for (auto it=m_intents.begin();it!=m_intents.end();) if (key(it->authorization.m_binding)==id) it=m_intents.erase(it); else ++it;
    if (!directory(error)) return false;
    QLockFile lock(QDir(m_directory).filePath(QStringLiteral("approvals.lock"))); if (!lock.tryLock(0)) return fail(error,QObject::tr("The source approval store is busy. Try again."));
    State state; if (!load(&state,error)) return false;
    if (state.epoch.isEmpty()) state.epoch=uuid();
    if (state.revision>=MaximumRevision) return fail(error,savedError());
    state.approvals.remove(id); ++state.revision; if (!save(state,error)) return false;
    { std::lock_guard<std::mutex> lock(m_liveProfile->mutex); if (m_liveProfile->entries.value(id)==entry) m_liveProfile->entries.remove(id); }
    m_withdrawn.remove(id); return true;
}
bool AddonSourceConsent::reset(QString* error)
{
    if (!m_liveOwner->open.load(std::memory_order_acquire)) return false;
    if (m_liveProfile->reset->retired.load(std::memory_order_acquire)) return fail(error,savedError());
    LiveMutation mutation(m_liveProfile->reset); m_liveProfile->reset->allowed.store(false,std::memory_order_release);
    std::lock_guard<std::mutex> guard(m_mutex); if (m_closed) return false;
    ++m_reset; ++m_ticketEpoch; m_tickets.clear(); m_intents.clear(); m_denyAll=true;
    if (!directory(error)) return false;
    QLockFile lock(QDir(m_directory).filePath(QStringLiteral("approvals.lock"))); if (!lock.tryLock(0)) return fail(error,QObject::tr("The source approval store is busy. Try again."));
    State state; state.epoch=uuid(); if (!save(state,error)) return false;
    { std::lock_guard<std::mutex> lock(m_liveProfile->mutex); m_liveProfile->entries.clear(); }
    m_liveProfile->reset->allowed.store(true,std::memory_order_release);
    m_liveOwner->allowed.store(true,std::memory_order_release); m_withdrawn.clear(); m_denyAll=false; return true;
}
QString AddonSourceConsent::mintIntent(const Binding& value,const QString& uri,quint64 generation,IntentReason reason)
{
    const auto package=m_packageAuthority.capture();
    const auto binding=canonical(value);
    if (!package.current() || !binding || !generation || !reasonValid(reason) || !sourceCurrent(*binding) || !admissionCurrent(*binding,uri)) return {};
    std::lock_guard<std::mutex> guard(m_mutex); const auto now=prune(); const auto id=key(*binding); State state;
    if (m_closed || m_denyAll || !liveAllowed(id) || m_withdrawn.contains(id) || now<0 || m_intents.size()>=m_options.intents
        || now>std::numeric_limits<qint64>::max()-m_options.intentLifetimeMs || generation<=m_floor || m_issued.contains(generation)
        || !load(&state) || !state.approvals.contains(id)) return {};
    m_highest=std::max(m_highest,generation); m_floor=m_highest>GenerationWindow?m_highest-GenerationWindow:0;
    for (auto it=m_issued.begin();it!=m_issued.end();) if (*it<=m_floor) it=m_issued.erase(it); else ++it;
    if (generation<=m_floor) return {};
    m_issued.insert(generation); Authorization authorization; authorization.m_binding=*binding; authorization.m_uri=uri;
    authorization.m_generation=generation; authorization.m_reason=reason; authorization.m_epoch=state.epoch;
    authorization.m_approval=state.approvals.value(id).toObject().value("id").toString(); authorization.m_owner=m_owner; authorization.m_reset=m_reset;
    authorization.m_live=liveToken(id,package); if (!authorization.m_live.current()) return {};
    const auto token=uuid(); m_intents.insert(token,{authorization,now+m_options.intentLifetimeMs}); return token;
}
std::optional<AddonSourceConsent::Authorization> AddonSourceConsent::consumeIntent(const QString& token,const Binding& value,
    const QString& uri,quint64 generation,IntentReason reason)
{
    Intent intent;
    { std::lock_guard<std::mutex> guard(m_mutex); if (m_closed || prune()<0 || !m_intents.contains(token)) return {}; intent=m_intents.take(token); }
    const auto binding=canonical(value);
    if (!binding || !(*binding==intent.authorization.m_binding) || intent.authorization.m_uri!=uri
        || intent.authorization.m_generation!=generation || intent.authorization.m_reason!=reason
        || !current(intent.authorization,uri,generation)) return {};
    return intent.authorization;
}
bool AddonSourceConsent::current(const Authorization& authorization,const QString& uri,quint64 generation) const
{
    if (authorization.m_owner!=m_owner || authorization.m_uri!=uri || authorization.m_generation!=generation) return false;
    if (!authorization.m_live.current()) return false;
    const auto reject=[&authorization] {
        if(authorization.m_live.m_receipt->open.exchange(false,std::memory_order_acq_rel)) authorization.m_live.m_receipt->authority.invalidate();
        return false;
    };
    if (!sourceCurrent(authorization.m_binding) || !admissionCurrent(authorization.m_binding,uri)) return reject();
    std::lock_guard<std::mutex> guard(m_mutex); const auto id=key(authorization.m_binding); State state;
    const bool valid=!m_closed && !m_denyAll && authorization.m_reset==m_reset && !m_withdrawn.contains(id) && load(&state)
        && state.epoch==authorization.m_epoch && state.approvals.value(id).toObject().value("id").toString()==authorization.m_approval
        && authorization.m_live.current();
    return valid || reject();
}
void AddonSourceConsent::retire() noexcept
{ m_liveOwner->open.store(false,std::memory_order_release); m_liveOwner->authority.invalidate(); }
void AddonSourceConsent::close()
{ retire(); std::lock_guard<std::mutex> guard(m_mutex); if (m_closed) return;
  m_closed=true; ++m_reset; ++m_ticketEpoch; m_tickets.clear(); m_intents.clear(); m_issued.clear(); }
}
