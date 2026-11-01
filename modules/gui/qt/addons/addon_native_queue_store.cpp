// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "addon_native_queue_store.hpp"
#include "addon_player_discovery.hpp"
#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <QUuid>
#include <vlc_common.h>
#include <vlc_input_item.h>
#include <vlc_input_policy.h>
#include <vlc_playlist.h>
#include <vector>
#include <cstring>

namespace vlc::addons {
namespace {
bool authorityCurrent(const AddonQueueTransactions::Transaction& transaction) noexcept
{
    try { return !transaction.authorityCurrent || transaction.authorityCurrent(); } catch(...) { return false; }
}
QString displayTitle(const QVariantMap& reference)
{
    auto display = reference.value("snapshot").toMap().value("title").toString();
    for (auto& character : display) if (character.category() == QChar::Other_Control || character.category() == QChar::Separator_Line || character.category() == QChar::Separator_Paragraph) character = QLatin1Char(' ');
    return display;
}
QList<quint64> refreshItems(vlc_playlist_t* playlist, const QStringList& uris, const QVariantList& references)
{
    QHash<QString, QString> titles;
    for (int index = 0; index < uris.size(); ++index) titles.insert(uris[index], displayTitle(references[index].toMap()));
    QList<quint64> changed;
    for (size_t index = 0; index < vlc_playlist_Count(playlist); ++index) {
        auto* entry = vlc_playlist_Get(playlist, index); auto* item = vlc_playlist_item_GetMedia(entry);
        char* rawUri = input_item_GetURI(item); const auto uri = QString::fromUtf8(rawUri); free(rawUri);
        if (!titles.contains(uri)) continue;
        char* previous = input_item_GetMetaExtra(item, "vlc:addon-source-title");
        if (!previous) continue;
        const auto before = QString::fromUtf8(previous); free(previous);
        char* rawName = input_item_GetName(item); const auto name = QString::fromUtf8(rawName); free(rawName);
        char* rawTitle = input_item_GetTitle(item); const auto title = QString::fromUtf8(rawTitle); free(rawTitle);
        char* rawManual = input_item_GetMetaExtra(item, "vlc:addon-manual-title"); bool manual = rawManual && !strcmp(rawManual, "1"); free(rawManual);
        if (name != before || (!title.isEmpty() && title != before)) { manual = true; input_item_SetMetaExtra(item, "vlc:addon-manual-title", "1"); }
        const auto next = titles.value(uri).toUtf8();
        if (!manual && name != titles.value(uri)) { input_item_SetName(item, next.constData()); input_item_SetTitle(item, next.constData()); changed.append(vlc_playlist_item_GetId(entry)); }
        input_item_SetMetaExtra(item, "vlc:addon-source-title", next.constData());
    }
    return changed;
}
}
struct AddonNativeQueueStore::Pending {
    enum Kind { Append, Play, Refresh } kind = Append;
    // Set before the worker starts. The worker only reads them.
    AddonQueueTransactions::Transaction transaction;
    AddonQueueTransactions::Completion completion;
    std::shared_ptr<std::atomic_bool> canceled = std::make_shared<std::atomic_bool>(false);
    // The worker sets these. This thread reads them after the worker returns.
    QStringList uris;
    QString error;
    bool recorded = false;
};
AddonNativeQueueNotifications* AddonNativeQueueStore::notifications()
{ static AddonNativeQueueNotifications bus; return &bus; }
const vlc_playlist_callbacks* AddonNativeQueueStore::callbacks()
{
    static const vlc_playlist_callbacks value = [] {
        vlc_playlist_callbacks cb{};
        cb.on_items_reset = [](vlc_playlist_t*, vlc_playlist_item_t* const[], size_t, void* self) { static_cast<AddonNativeQueueStore*>(self)->changed(); };
        cb.on_items_added = [](vlc_playlist_t*, size_t, vlc_playlist_item_t* const[], size_t, void* self) { static_cast<AddonNativeQueueStore*>(self)->changed(); };
        cb.on_items_moved = [](vlc_playlist_t*, size_t, size_t, size_t, void* self) { static_cast<AddonNativeQueueStore*>(self)->changed(); };
        cb.on_items_removed = [](vlc_playlist_t*, size_t, size_t, void* self) { static_cast<AddonNativeQueueStore*>(self)->changed(); };
        cb.on_current_index_changed = [](vlc_playlist_t*, ssize_t, void* self) { static_cast<AddonNativeQueueStore*>(self)->changed(); };
        return cb;
    }();
    return &value;
}
AddonNativeQueueStore::AddonNativeQueueStore(vlc_playlist_t* playlist, std::shared_ptr<AddonSourceAdmissions> admissions, QObject* parent)
    : QObject(parent), m_playlist(playlist), m_admissions(std::move(admissions)), m_identity(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    if (m_playlist) { vlc_playlist_Lock(m_playlist); m_listener = vlc_playlist_AddListener(m_playlist, callbacks(), this, false); vlc_playlist_Unlock(m_playlist); }
}
AddonNativeQueueStore::~AddonNativeQueueStore() { close(); }
void AddonNativeQueueStore::close()
{
    m_closed->store(true);
    if (m_playlist) {
        vlc_playlist_Lock(m_playlist);
        if (m_listener) vlc_playlist_RemoveListener(m_playlist, m_listener);
        vlc_playlist_Unlock(m_playlist);
    }
    m_listener = nullptr; m_playlist = nullptr;
    // A worker that is still saving finds no front action when it returns.
    const auto pending = std::move(m_pending); m_pending.clear(); m_working = false;
    for (const auto& job : pending) { job->canceled->store(true); if (job->completion) job->completion(false, tr("The queue is not available.")); }
}
void AddonNativeQueueStore::changed()
{
    ++m_revision;
    QMetaObject::invokeMethod(this, [this] { emit revisionChanged(); }, Qt::QueuedConnection);
}
AddonQueueTransactions::Snapshot AddonNativeQueueStore::snapshot(const QString& target) const
{
    if (target != QLatin1String("queue") || !m_playlist || !m_listener || !m_admissions) return {};
    vlc_playlist_Lock(m_playlist);
    const AddonQueueTransactions::Snapshot value{true, m_identity, m_revision};
    vlc_playlist_Unlock(m_playlist); return value;
}
AddonQueueTransactions::Cancel AddonNativeQueueStore::append(const AddonQueueTransactions::Transaction& transaction, AddonQueueTransactions::Completion completion)
{
    const auto before = snapshot(transaction.target);
    if (!before.available || before.identity != transaction.targetIdentity || before.revision != transaction.expectedRevision) {
        completion(false, tr("The queue changed. Select the items again.")); return {};
    }
    QString error; const QPointer<AddonNativeQueueStore> alive(this);
    const bool admitted = m_admissions->check(transaction.references, &error);
    if (!alive) { completion(false, tr("The queue is not available.")); return {}; }
    if (!admitted) { completion(false, error); return {}; }
    auto job = std::make_shared<Pending>(); job->kind = Pending::Append;
    job->transaction = transaction; job->completion = std::move(completion);
    return submit(job);
}
AddonQueueTransactions::Cancel AddonNativeQueueStore::submit(const std::shared_ptr<Pending>& job)
{
    if (!m_playlist) { job->completion(false, tr("The queue is not available.")); return {}; }
    m_pending.push_back(job); schedule();
    return [canceled = job->canceled] { canceled->store(true); };
}
void AddonNativeQueueStore::schedule()
{
    if (m_working || m_pending.empty()) return;
    m_working = true;
    // The profile can be on a slow share: the admission file is read and
    // written on the worker, never on this thread.
    const auto job = m_pending.front(); const auto admissions = m_admissions; const auto closed = m_closed;
    const QPointer<AddonNativeQueueStore> owner(this);
    QThreadPool::globalInstance()->start([job, admissions, closed, owner]() mutable {
        if (!job->canceled->load() && !closed->load()) {
            const auto& references = job->transaction.references;
            if (job->kind == Pending::Refresh && admissions->uriForReference(references.first().toMap()).isEmpty()) job->recorded = false;
            else job->recorded = admissions->record(references, &job->uris, &job->error);
        }
        // Move the job, so its callbacks are released on this thread, not the worker.
        QMetaObject::invokeMethod(QCoreApplication::instance(), [job = std::move(job), owner] { if (owner) owner->recorded(job); }, Qt::QueuedConnection);
    });
}
void AddonNativeQueueStore::recorded(const std::shared_ptr<Pending>& job)
{
    if (m_pending.empty() || m_pending.front() != job) return;
    m_pending.pop_front(); m_working = false;
    const QPointer<AddonNativeQueueStore> alive(this);
    QString error;
    if (!m_playlist) job->completion(false, tr("The queue is not available."));
    else if (job->canceled->load()) job->completion(false, tr("The discovery action was canceled."));
    else if (!job->recorded || job->uris.size() != job->transaction.references.size())
        job->completion(false, !job->error.isEmpty() ? job->error : tr("The app cannot save the discovery references."));
    // The source or account can change while the worker saves. Check it
    // again here; authorityCurrent is still the last check, under the lock.
    else if (!m_admissions->check(job->transaction.references, &error)) job->completion(false, error);
    else if (!alive || !m_playlist) job->completion(false, tr("The queue is not available."));
    else if (job->kind == Pending::Append) commitAppend(*job);
    else if (job->kind == Pending::Play) commitPlay(*job);
    else commitRefresh(*job);
    if (alive) schedule();
}
void AddonNativeQueueStore::commitAppend(const Pending& job)
{
    const auto& transaction = job.transaction; const auto& uris = job.uris; const auto& completion = job.completion;
    std::vector<std::shared_ptr<input_item_t>> held;
    std::vector<input_item_t*> items;
    for (int index = 0; index < uris.size(); ++index) {
        const auto uri = uris[index].toUtf8();
        const auto title = displayTitle(transaction.references[index].toMap()).toUtf8();
        auto* item = input_item_NewExt(uri.constData(), title.constData(), INPUT_DURATION_UNSET, ITEM_TYPE_STREAM, ITEM_NET);
        if (!item) { completion(false, tr("The app cannot create the queue items.")); return; }
        auto* policy=vlc_input_policy_HoldSourceItem(item,uri.constData());
        if(!policy) { input_item_Release(item); completion(false,tr("The source item cannot use its native policy.")); return; }
        vlc_input_policy_Release(policy);
        input_item_SetTitle(item, title.constData()); input_item_SetMetaExtra(item, "vlc:addon-source-title", title.constData());
        if (!transaction.fillTag.isEmpty()) {
            input_item_SetMetaExtra(item, "vlc:addon-fill", transaction.fillTag.toUtf8().constData());
            if (!transaction.fillCredit.isEmpty()) input_item_SetMetaExtra(item, "vlc:addon-fill-source", transaction.fillCredit.toUtf8().constData());
        }
        held.emplace_back(item, input_item_Release); items.push_back(item);
    }
    vlc_playlist_Lock(m_playlist);
    const bool current = transaction.targetIdentity == m_identity && transaction.expectedRevision == m_revision
        && authorityCurrent(transaction);
    const bool success = current && vlc_playlist_Append(m_playlist, items.data(), items.size()) == VLC_SUCCESS;
    const auto metadata = success ? refreshItems(m_playlist, uris, transaction.references) : QList<quint64>{};
    vlc_playlist_Unlock(m_playlist);
    if (!metadata.isEmpty()) emit notifications()->metadataChanged(reinterpret_cast<quintptr>(m_playlist), metadata);
    completion(success, success ? QString() : current ? tr("The app cannot add the selection to the queue.") : tr("The queue changed. Select the items again."));
}
AddonQueueTransactions::Cancel AddonNativeQueueStore::playReference(const AddonQueueTransactions::Transaction& transaction, AddonQueueTransactions::Completion completion)
{
    const auto before=snapshot(transaction.target);
    if(!completion) return {};
    if(!before.available || before.identity!=transaction.targetIdentity || before.revision!=transaction.expectedRevision
        || transaction.references.size()!=1 || !transaction.playbackHandle.isEmpty()) {
        completion(false,tr("The queue changed. Select the item again.")); return {};
    }
    QString error; const QPointer<AddonNativeQueueStore> alive(this);
    const bool admitted=m_admissions->check(transaction.references,&error);
    if(!alive) { completion(false,tr("The queue is not available.")); return {}; }
    if(!admitted) { completion(false,error); return {}; }
    auto job=std::make_shared<Pending>(); job->kind=Pending::Play;
    job->transaction=transaction; job->completion=std::move(completion);
    return submit(job);
}
void AddonNativeQueueStore::commitPlay(const Pending& job)
{
    const auto& transaction=job.transaction; const auto& uris=job.uris; const auto& completion=job.completion;
    const auto uri=uris.first().toUtf8(); const auto title=displayTitle(transaction.references.first().toMap()).toUtf8();
    const auto item=std::shared_ptr<input_item_t>(input_item_NewExt(uri.constData(),title.constData(),INPUT_DURATION_UNSET,ITEM_TYPE_STREAM,ITEM_NET),input_item_Release);
    if(!item) { completion(false,tr("The app cannot create the queue item.")); return; }
    auto* policy=vlc_input_policy_HoldSourceItem(item.get(),uri.constData());
    if(!policy) { completion(false,tr("The source item cannot use its native policy.")); return; }
    vlc_input_policy_Release(policy); input_item_SetTitle(item.get(),title.constData());
    input_item_SetMetaExtra(item.get(),"vlc:addon-source-title",title.constData());
    vlc_playlist_Lock(m_playlist);
    const bool current=transaction.targetIdentity==m_identity && transaction.expectedRevision==m_revision
        && authorityCurrent(transaction);
    bool success=false; bool appended=false; bool blocked=false; size_t index=vlc_playlist_Count(m_playlist);
    if(current) {
        // Reuse the exact stable locator. A second Play action adds no duplicate.
        for(size_t at=0;at<vlc_playlist_Count(m_playlist);++at) {
            auto* queued=vlc_playlist_item_GetMedia(vlc_playlist_Get(m_playlist,at));
            char* address=input_item_GetURI(queued); const bool same=address && !strcmp(address,uri.constData()); free(address);
            if(!same) continue;
            auto* existing=vlc_input_policy_HoldSourceItem(queued,uri.constData());
            if(existing) { vlc_input_policy_Release(existing); index=at; } else blocked=true;
            break;
        }
        if(!blocked && index==vlc_playlist_Count(m_playlist)) {
            appended=vlc_playlist_AppendOne(m_playlist,item.get())==VLC_SUCCESS;
        }
        if(!blocked && index<vlc_playlist_Count(m_playlist)) success=vlc_playlist_PlayAt(m_playlist,index)==VLC_SUCCESS;
        if(!success && appended) vlc_playlist_RemoveOne(m_playlist,index);
    }
    const auto metadata=success?refreshItems(m_playlist,uris,transaction.references):QList<quint64>{};
    vlc_playlist_Unlock(m_playlist);
    if(!metadata.isEmpty()) emit notifications()->metadataChanged(reinterpret_cast<quintptr>(m_playlist),metadata);
    completion(success,success?QString():current?tr("The app cannot start this queue item."):tr("The queue changed. Select the item again."));
}
AddonDiscoveryNativeBackend AddonNativeQueueStore::backend()
{
    const QPointer<AddonNativeQueueStore> owner(this);
    AddonDiscoveryNativeBackend result;
    result.stableReferencePlaybackAvailable=true;
    result.playReference=[owner](const auto& transaction,auto done) {
        if(owner) return owner->playReference(transaction,std::move(done));
        done(false,QObject::tr("The queue is not available.")); return AddonQueueTransactions::Cancel{};
    };
    result.snapshot = [owner](const QString& target) { return owner ? owner->snapshot(target) : AddonQueueTransactions::Snapshot{}; };
    result.pendingAhead = [owner](const QString& tag) { return owner ? owner->pendingAhead(tag) : -1; };
    result.append = [owner](const auto& transaction, auto completion) {
        if (!owner) { completion(false, QObject::tr("The queue is not available.")); return AddonQueueTransactions::Cancel{}; }
        return owner->append(transaction, std::move(completion));
    };
    // The source factory enables this path only while a native lazy owner is active.
    return result;
}
int AddonNativeQueueStore::pendingAhead(const QString& fillTag) const
{
    if (!m_playlist || fillTag.isEmpty()) return -1;
    const auto tag = fillTag.toUtf8();
    int count = 0;
    vlc_playlist_Lock(m_playlist);
    const auto current = vlc_playlist_GetCurrentIndex(m_playlist);
    for (size_t index = current < 0 ? 0 : size_t(current) + 1; index < vlc_playlist_Count(m_playlist); ++index) {
        char* value = input_item_GetMetaExtra(vlc_playlist_item_GetMedia(vlc_playlist_Get(m_playlist, index)), "vlc:addon-fill");
        if (value && tag == value) ++count;
        free(value);
    }
    vlc_playlist_Unlock(m_playlist);
    return count;
}
QVariantMap AddonNativeQueueStore::queuedReference(quint64 itemId) const
{
    if (!m_playlist || !m_admissions) return {};
    QString uri;
    vlc_playlist_Lock(m_playlist);
    for (size_t index = 0; index < vlc_playlist_Count(m_playlist); ++index) {
        auto* item = vlc_playlist_Get(m_playlist, index);
        if (vlc_playlist_item_GetId(item) != itemId) continue;
        char* value = input_item_GetURI(vlc_playlist_item_GetMedia(item));
        if (value) { uri = QString::fromUtf8(value); free(value); }
        break;
    }
    vlc_playlist_Unlock(m_playlist);
    return admittedReference(uri);
}
QVariantMap AddonNativeQueueStore::admittedReference(const QString& uri) const
{ return m_admissions ? m_admissions->currentReference(uri) : QVariantMap{}; }
void AddonNativeQueueStore::refreshDisplay(const QVariantMap& reference, std::function<void(bool)> done)
{
    // The worker refreshes only a reference that the file already admits.
    auto job = std::make_shared<Pending>(); job->kind = Pending::Refresh; job->transaction.references = {reference};
    job->completion = [done = std::move(done)](bool success, const QString&) { if (done) done(success); };
    const QPointer<AddonNativeQueueStore> alive(this);
    if (!m_playlist || !m_admissions || !m_admissions->check(job->transaction.references) || !alive) { job->completion(false, {}); return; }
    submit(job);
}
void AddonNativeQueueStore::commitRefresh(const Pending& job)
{
    vlc_playlist_Lock(m_playlist); const auto metadata = refreshItems(m_playlist, job.uris, job.transaction.references); vlc_playlist_Unlock(m_playlist);
    if (!metadata.isEmpty()) emit notifications()->metadataChanged(reinterpret_cast<quintptr>(m_playlist), metadata);
    job.completion(true, {});
}
}
