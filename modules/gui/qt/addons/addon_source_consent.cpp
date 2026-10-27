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

namespace vlc::addons {
namespace {
constexpr qint64 MaximumBytes=1024*1024;
constexpr quint64 MaximumRevision=9007199254740991ULL, GenerationWindow=4096;
QString uuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool validUuid(const QString& value)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$"));
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
      m_file(QDir(m_directory).filePath(QStringLiteral("approvals.json"))),m_owner(uuid()),m_checks(std::move(checks)),m_options(std::move(options))
{
    m_options.approvals=std::clamp(m_options.approvals,1,256); m_options.tickets=std::clamp(m_options.tickets,1,64); m_options.intents=std::clamp(m_options.intents,1,128);
    m_options.ticketLifetimeMs=std::clamp<qint64>(m_options.ticketLifetimeMs,1,300000);
    m_options.intentLifetimeMs=std::clamp<qint64>(m_options.intentLifetimeMs,1,30000);
    if (!m_options.now) m_options.now=monotonic;
}
std::optional<AddonSourceConsent::Binding> AddonSourceConsent::canonical(const Binding& input)
{
    static const QRegularExpression contribution(QStringLiteral("^[a-z][a-z0-9.-]{1,127}/[a-z][a-z0-9.-]{0,63}$"));
    if (kind(input.kind).isEmpty() || !text(input.identity,2048) || !contribution.match(input.contribution).hasMatch()
        || !text(input.accountReference,512) || input.accountReference.startsWith(QLatin1String("disconnected:"))
        || input.profile!=QLatin1String("public-https-media@1")) return {};
    Binding result=input; QUrl url(input.identity,QUrl::StrictMode);
    if (url.isValid() && url.scheme()==QLatin1String("https") && !url.host().isEmpty()
        && url.userInfo().isEmpty() && !url.hasQuery() && !url.hasFragment() && url.port(-1)!=0) {
        const auto decoded=url.path(QUrl::FullyDecoded);
        for (auto c:decoded) if (c.category()==QChar::Other_Control) return {};
        url.setHost(url.host().toLower()); if (url.port(-1)==443) url.setPort(-1);
        result.identity=QString::fromLatin1(url.adjusted(QUrl::NormalizePathSegments).toEncoded(QUrl::FullyEncoded));
    } else if (input.kind==SourceKind::Developer && url.isLocalFile() && url.host().isEmpty() && url.userInfo().isEmpty()
               && !url.hasQuery() && !url.hasFragment() && QDir::isAbsolutePath(url.toLocalFile())) {
        for (auto c:url.toLocalFile()) if (c.category()==QChar::Other_Control) return {};
        result.identity=QString::fromLatin1(QUrl::fromLocalFile(QDir::cleanPath(url.toLocalFile())).toEncoded(QUrl::FullyEncoded));
    } else if (input.kind==SourceKind::Distribution
               && QRegularExpression(QStringLiteral("^vlc-distribution:[a-z][a-z0-9.-]{1,127}$")).match(input.identity).hasMatch()) {}
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
    if (m_closed || m_denyAll || now<0 || m_tickets.size()>=m_options.tickets || now>std::numeric_limits<qint64>::max()-m_options.ticketLifetimeMs) return {};
    if (!directory(error)) return {};
    QLockFile lock(QDir(m_directory).filePath(QStringLiteral("approvals.lock"))); if (!lock.tryLock(0)) { fail(error,QObject::tr("The source approval store is busy. Try again.")); return {}; }
    State state; if (!load(&state,error)) return {};
    if (state.epoch.isEmpty()) { state.epoch=uuid(); if (!save(state,error)) return {}; }
    Ticket ticket{uuid(),*binding}; m_tickets.insert(ticket.id,{ticket,state.epoch,state.revision,now+m_options.ticketLifetimeMs,m_ticketEpoch}); return ticket;
}
bool AddonSourceConsent::grant(const QString& token,QString* error)
{
    Dialog dialog;
    { std::lock_guard<std::mutex> guard(m_mutex); if (m_closed || prune()<0 || !m_tickets.contains(token)) return fail(error,QObject::tr("The approval request expired. Open it again.")); dialog=m_tickets.take(token); }
    if (!sourceCurrent(dialog.ticket.binding)) return fail(error,QObject::tr("The source or account changed. Open approval again."));
    std::lock_guard<std::mutex> guard(m_mutex);
    const auto now=prune();
    if (m_closed || m_denyAll || now<0 || now>=dialog.expires || dialog.localEpoch!=m_ticketEpoch || !directory(error)) return false;
    QLockFile lock(QDir(m_directory).filePath(QStringLiteral("approvals.lock"))); if (!lock.tryLock(0)) return fail(error,QObject::tr("The source approval store is busy. Try again."));
    State state; if (!load(&state,error)) return false;
    if (state.epoch!=dialog.epoch || state.revision!=dialog.revision || state.revision>=MaximumRevision) return fail(error,QObject::tr("The saved approval changed. Open approval again."));
    const auto id=key(dialog.ticket.binding);
    if (!state.approvals.contains(id) && state.approvals.size()>=m_options.approvals) return fail(error,QObject::tr("The source approval limit was reached."));
    state.approvals.insert(id,QJsonObject{{"binding",bindingJson(dialog.ticket.binding)},{"id",uuid()}}); ++state.revision;
    if (!save(state,error)) return false;
    m_withdrawn.remove(id); return true;
}
bool AddonSourceConsent::approved(const Binding& value) const
{
    const auto binding=canonical(value); if (!binding || !sourceCurrent(*binding)) return false;
    std::lock_guard<std::mutex> guard(m_mutex); const auto id=key(*binding); State state;
    return !m_closed && !m_denyAll && !m_withdrawn.contains(id) && load(&state) && state.approvals.contains(id);
}
bool AddonSourceConsent::revoke(const Binding& value,QString* error)
{
    const auto binding=canonical(value); if (!binding) return fail(error,savedError());
    std::lock_guard<std::mutex> guard(m_mutex); if (m_closed) return false;
    ++m_ticketEpoch;
    const auto id=key(*binding);
    if (m_withdrawn.size()<256) m_withdrawn.insert(id); else m_denyAll=true;
    for (auto it=m_tickets.begin();it!=m_tickets.end();) if (key(it->ticket.binding)==id) it=m_tickets.erase(it); else ++it;
    for (auto it=m_intents.begin();it!=m_intents.end();) if (key(it->authorization.m_binding)==id) it=m_intents.erase(it); else ++it;
    if (!directory(error)) return false;
    QLockFile lock(QDir(m_directory).filePath(QStringLiteral("approvals.lock"))); if (!lock.tryLock(0)) return fail(error,QObject::tr("The source approval store is busy. Try again."));
    State state; if (!load(&state,error)) return false;
    if (state.epoch.isEmpty()) state.epoch=uuid();
    if (state.revision>=MaximumRevision) return fail(error,savedError());
    state.approvals.remove(id); ++state.revision; if (!save(state,error)) return false;
    m_withdrawn.remove(id); return true;
}
bool AddonSourceConsent::reset(QString* error)
{
    std::lock_guard<std::mutex> guard(m_mutex); if (m_closed) return false;
    ++m_reset; ++m_ticketEpoch; m_tickets.clear(); m_intents.clear(); m_denyAll=true;
    if (!directory(error)) return false;
    QLockFile lock(QDir(m_directory).filePath(QStringLiteral("approvals.lock"))); if (!lock.tryLock(0)) return fail(error,QObject::tr("The source approval store is busy. Try again."));
    State state; state.epoch=uuid(); if (!save(state,error)) return false;
    m_withdrawn.clear(); m_denyAll=false; return true;
}
QString AddonSourceConsent::mintIntent(const Binding& value,const QString& uri,quint64 generation,IntentReason reason)
{
    const auto binding=canonical(value);
    if (!binding || !generation || !reasonValid(reason) || !sourceCurrent(*binding) || !admissionCurrent(*binding,uri)) return {};
    std::lock_guard<std::mutex> guard(m_mutex); const auto now=prune(); const auto id=key(*binding); State state;
    if (m_closed || m_denyAll || m_withdrawn.contains(id) || now<0 || m_intents.size()>=m_options.intents
        || now>std::numeric_limits<qint64>::max()-m_options.intentLifetimeMs || generation<=m_floor || m_issued.contains(generation)
        || !load(&state) || !state.approvals.contains(id)) return {};
    m_highest=std::max(m_highest,generation); m_floor=m_highest>GenerationWindow?m_highest-GenerationWindow:0;
    for (auto it=m_issued.begin();it!=m_issued.end();) if (*it<=m_floor) it=m_issued.erase(it); else ++it;
    if (generation<=m_floor) return {};
    m_issued.insert(generation); Authorization authorization; authorization.m_binding=*binding; authorization.m_uri=uri;
    authorization.m_generation=generation; authorization.m_reason=reason; authorization.m_epoch=state.epoch;
    authorization.m_approval=state.approvals.value(id).toObject().value("id").toString(); authorization.m_owner=m_owner; authorization.m_reset=m_reset;
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
    if (authorization.m_owner!=m_owner || authorization.m_uri!=uri || authorization.m_generation!=generation
        || !sourceCurrent(authorization.m_binding) || !admissionCurrent(authorization.m_binding,uri)) return false;
    std::lock_guard<std::mutex> guard(m_mutex); const auto id=key(authorization.m_binding); State state;
    return !m_closed && !m_denyAll && authorization.m_reset==m_reset && !m_withdrawn.contains(id) && load(&state)
        && state.epoch==authorization.m_epoch && state.approvals.value(id).toObject().value("id").toString()==authorization.m_approval;
}
void AddonSourceConsent::close()
{ std::lock_guard<std::mutex> guard(m_mutex); m_closed=true; ++m_reset; ++m_ticketEpoch; m_tickets.clear(); m_intents.clear(); m_issued.clear(); }
}
