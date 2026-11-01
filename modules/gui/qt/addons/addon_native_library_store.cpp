// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "addon_native_library_store.hpp"
#include "addon_manager.hpp"
#include "addon_player_discovery.hpp"
#include <QDir>
#include <QLockFile>
#include <QUuid>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>
#include <vlc_common.h>
#include <vlc_addon_medialibrary.h>

namespace vlc::addons {
struct AddonNativeLibraryStore::Shared {
    std::mutex mutex;
    bool closed = false;
    quint64 epoch = 1;
};
struct AddonNativeLibraryStore::Target {
    QString reference;
    qint64 playlistId = 0;
    quint64 revision = 0;
    bool ready = false, busy = false;
    std::shared_ptr<std::atomic_bool> current = std::make_shared<std::atomic_bool>(true);
    std::shared_ptr<vlc_addon_playlist_snapshot_t> native;
};
struct AddonNativeLibraryStore::Pending {
    struct Configuration { InstalledPackage package; QJsonObject settings; quint64 revision = 0; };
    quint64 id = 0, task = 0, epoch = 0;
    std::shared_ptr<Target> target;
    std::shared_ptr<std::atomic_int> phase = std::make_shared<std::atomic_int>(0);
    std::shared_ptr<vlc_addon_playlist_snapshot_t> native;
    // An append reads the packages in one native task (Reading), returns
    // here to check the sources again, then commits in a second (Committing).
    enum Stage { Selecting, Reading, Committing } stage = Selecting;
    // Set before the first dispatch. Native work only reads them.
    QVariantList references;
    QStringList packages, sources;
    std::vector<std::string> titles;
    std::vector<Configuration> configurations;
    std::shared_ptr<PackageStore> store;
    std::shared_ptr<AddonSourceAdmissions> admissions;
    AddonQueueTransactions::Completion completion;
    QString message;
    bool success = false;
    enum Error { Unavailable, Changed, Canceled, Busy, Record, Commit } error = Unavailable;
};
namespace {
QString errorMessage(int value)
{
    switch (value) {
    case 1: return QObject::tr("The playlist, source, or account changed. Select the items again.");
    case 2: return QObject::tr("The library action was canceled.");
    case 3: return QObject::tr("The add-on store is busy. Try again.");
    case 4: return QObject::tr("The app cannot create the library media records.");
    case 5: return QObject::tr("The playlist changed or the app cannot add the complete group.");
    default: return QObject::tr("This library cannot accept discovery items.");
    }
}
bool permitted(const InstalledPackage& package, const QString& source)
{
    const auto* contribution = package.manifest.contribution(source.section('/',1));
    if (!contribution || contribution->type != QLatin1String("discovery.source")) return false;
    for (const auto& permission : contribution->requires) if (!package.grants.value(permission).isObject()) return false;
    return true;
}
}
AddonNativeLibraryStore::AddonNativeLibraryStore(Dispatcher dispatcher, std::shared_ptr<AddonSourceAdmissions> admissions,
                                                 AddonManager* manager, QObject* parent)
    : QObject(parent), m_dispatcher(std::move(dispatcher)), m_dispatcherLifetime(m_dispatcher.lifetime),
      m_manager(manager), m_admissions(std::move(admissions)), m_shared(std::make_shared<Shared>())
{
    if (!manager || !m_dispatcherLifetime || !m_dispatcher.run || !m_dispatcher.cancel || !m_admissions) m_shared->closed = true;
    if (manager) {
        connect(manager, &AddonManager::revisionChanged, this, &AddonNativeLibraryStore::invalidate);
        connect(manager, &AddonManager::packageWithdrawn, this, &AddonNativeLibraryStore::invalidate);
        connect(manager, &QObject::destroyed, this, &AddonNativeLibraryStore::close);
    }
    if (m_dispatcherLifetime) connect(m_dispatcherLifetime, &QObject::destroyed, this, &AddonNativeLibraryStore::close);
}
AddonNativeLibraryStore::~AddonNativeLibraryStore() { close(); }
QString AddonNativeLibraryStore::adoptSnapshot(std::shared_ptr<vlc_addon_playlist_snapshot> snapshot)
{
    { std::lock_guard<std::mutex> lock(m_shared->mutex); if (m_shared->closed || !snapshot || m_targets.size()>=16) return {}; }
    auto target=std::make_shared<Target>(); target->reference=QStringLiteral("library:")+QUuid::createUuid().toString(QUuid::WithoutBraces);
    target->revision=++m_next; target->native=std::move(snapshot); target->ready=true;
    m_targets.insert(target->reference,target); emit revisionChanged(); return target->reference;
}
QString AddonNativeLibraryStore::selectPlaylist(qint64 playlistId)
{
    {
        std::lock_guard<std::mutex> lock(m_shared->mutex);
        if (m_shared->closed || playlistId <= 0 || m_targets.size() >= 16 || m_jobs.size() >= 4) return {};
    }
    auto target = std::make_shared<Target>();
    target->reference = QStringLiteral("library:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    target->playlistId = playlistId; target->revision = ++m_next;
    auto job = std::make_shared<Pending>(); job->id = ++m_next; job->target = target;
    { std::lock_guard<std::mutex> lock(m_shared->mutex); job->epoch = m_shared->epoch; }
    m_targets.insert(target->reference, target); m_jobs.insert(job->id, job);
    const auto shared = m_shared; const QPointer<AddonNativeLibraryStore> owner(this);
    const auto dispatcher = m_dispatcher; const auto lifetime = m_dispatcherLifetime;
    const QObject* receiver = this;
    const auto task = dispatcher.run(receiver, [shared, job](vlc_medialibrary_t* ml) {
        {
            std::lock_guard<std::mutex> lock(shared->mutex);
            if (shared->closed || shared->epoch != job->epoch || job->phase->load() != 0 || !job->target->current->load()) {
                job->error = Pending::Canceled; return;
            }
        }
        if (!vlc_addon_playlist_cas_available(ml)) return;
        job->native.reset(vlc_addon_playlist_snapshot_create(ml, job->target->playlistId), vlc_addon_playlist_snapshot_destroy);
        job->success = bool(job->native);
    }, [owner, job] { if (owner) owner->completed(job); });
    if (owner) submitted(job, task);
    else if (lifetime && task) dispatcher.cancel(receiver, task);
    return target->reference;
}
void AddonNativeLibraryStore::submitted(const std::shared_ptr<Pending>& job, quint64 task)
{
    if (m_jobs.value(job->id) != job) {
        const auto dispatcher = m_dispatcher; const auto lifetime = m_dispatcherLifetime;
        const QObject* receiver = this;
        if (lifetime && task) dispatcher.cancel(receiver, task);
        return;
    }
    job->task = task;
    if (!task) completed(job);
}
AddonQueueTransactions::Snapshot AddonNativeLibraryStore::snapshot(const QString& reference) const
{
    const auto target = m_targets.value(reference);
    std::lock_guard<std::mutex> lock(m_shared->mutex);
    return !m_shared->closed && target && target->ready && !target->busy && target->current->load()
        ? AddonQueueTransactions::Snapshot{true, reference, target->revision} : AddonQueueTransactions::Snapshot{};
}
void AddonNativeLibraryStore::completed(const std::shared_ptr<Pending>& job)
{
    if (m_jobs.value(job->id) != job) return;
    bool current;
    { std::lock_guard<std::mutex> lock(m_shared->mutex); current = !m_shared->closed && m_shared->epoch == job->epoch && job->target->current->load(); }
    if (job->stage == Pending::Reading && job->success) {
        job->success = false;
        if (!current || job->phase->load() != 0 || m_targets.value(job->target->reference) != job->target) job->error = Pending::Canceled;
        else if (resume(job)) return;
    }
    m_jobs.remove(job->id);
    if (job->stage == Pending::Selecting) {
        const bool ready = current && job->phase->load() == 0 && job->success && m_targets.value(job->target->reference) == job->target;
        job->target->ready = ready; if (ready) job->target->native = job->native;
        const auto reference = job->target->reference;
        const QPointer<AddonNativeLibraryStore> owner(this);
        emit targetReady(reference, ready, ready ? QString() : errorMessage(job->error));
        if (owner) emit revisionChanged();
        return;
    }
    if (m_targets.value(job->target->reference) == job->target) m_targets.remove(job->target->reference);
    const auto completion = job->completion;
    const QPointer<AddonNativeLibraryStore> owner(this);
    if (completion) completion(job->success, job->success ? QString() : !job->message.isEmpty() ? job->message : errorMessage(job->error));
    if (owner) emit revisionChanged();
}
bool AddonNativeLibraryStore::resume(const std::shared_ptr<Pending>& job)
{
    // The packages were read after append() checked the sources. Check them
    // again: the commit guard then proves no change since that read.
    for (const auto& value : job->references)
        if (!m_manager || !discoveryReferenceAvailable(m_manager, value.toMap())) { job->error = Pending::Changed; return false; }
    const QPointer<AddonNativeLibraryStore> alive(this);
    const bool admitted = m_admissions->check(job->references, &job->message);
    if (!alive) return true;
    if (!admitted) return false;
    job->stage = Pending::Committing;
    dispatchAppend(job);
    return true;
}
AddonQueueTransactions::Cancel AddonNativeLibraryStore::append(const AddonQueueTransactions::Transaction& transaction,
                                                              AddonQueueTransactions::Completion completion)
{
    const auto expected = snapshot(transaction.target); const auto target = m_targets.value(transaction.target);
    if (!expected.available || expected.identity != transaction.targetIdentity || expected.revision != transaction.expectedRevision
        || !m_manager || m_jobs.size() >= 4 || transaction.references.isEmpty() || transaction.references.size() > 200) {
        completion(false, errorMessage(Pending::Changed)); return {};
    }
    // Only checks that need the manager run here. Reading a package hashes
    // each of its files, and the profile can be on a slow share, so the
    // package reads and the admission write run as native work.
    auto job = std::make_shared<Pending>(); job->id = ++m_next; job->stage = Pending::Reading; job->target = target;
    job->native = target->native; job->store = std::make_shared<PackageStore>(m_manager->storageDirectory());
    job->admissions = m_admissions; job->references = transaction.references;
    for (const auto& value : transaction.references) {
        const auto reference = value.toMap();
        if (!discoveryReferenceAvailable(m_manager, reference)) { completion(false, errorMessage(Pending::Changed)); return {}; }
        const auto source = reference.value("source").toString();
        const auto packageId = source.section('/',0,0);
        job->sources.append(source);
        if (!job->packages.contains(packageId)) job->packages.append(packageId);
        auto title=reference.value("snapshot").toMap().value("title").toString().left(512);
        for (auto& character:title)
            if (character.category()==QChar::Other_Control || character.category()==QChar::Separator_Line || character.category()==QChar::Separator_Paragraph) character=QLatin1Char(' ');
        title=title.trimmed(); if (title.isEmpty()) title=tr("Media item");
        job->titles.push_back(title.toStdString());
    }
    target->busy = true; job->completion = std::move(completion);
    { std::lock_guard<std::mutex> lock(m_shared->mutex); job->epoch = m_shared->epoch; }
    m_jobs.insert(job->id, job);
    const auto phase = job->phase;
    dispatchAppend(job);
    return [phase] { int expectedPhase = 0; phase->compare_exchange_strong(expectedPhase, 1); };
}
void AddonNativeLibraryStore::dispatchAppend(const std::shared_ptr<Pending>& job)
{
    const auto shared = m_shared; const QPointer<AddonNativeLibraryStore> owner(this);
    const auto dispatcher = m_dispatcher; const auto lifetime = m_dispatcherLifetime;
    const QObject* receiver = this;
    const auto pending = [shared, job] {
        std::lock_guard<std::mutex> lock(shared->mutex);
        return !shared->closed && shared->epoch == job->epoch && job->phase->load() == 0 && job->target->current->load();
    };
    NativeWork work;
    if (job->stage == Pending::Reading) work = [pending, job](vlc_medialibrary_t*) {
        if (!pending()) { job->error = Pending::Canceled; return; }
        for (const auto& packageId : job->packages) {
            Pending::Configuration captured;
            if (!job->store->installed(packageId, &captured.package)
                || !job->store->networkConfiguration(captured.package, &captured.settings, &captured.revision)) {
                job->error = Pending::Changed; return;
            }
            job->configurations.push_back(std::move(captured));
        }
        for (const auto& source : job->sources) {
            const auto captured = std::find_if(job->configurations.begin(), job->configurations.end(),
                [&](const auto& value) { return value.package.manifest.id == source.section('/',0,0); });
            if (captured == job->configurations.end() || !permitted(captured->package, source)) { job->error = Pending::Changed; return; }
        }
        job->success = true;
    };
    else work = [shared, pending, job](vlc_medialibrary_t* ml) {
        if (!pending()) { job->error = Pending::Canceled; return; }
        if (!vlc_addon_playlist_cas_available(ml)) return;
        QStringList uris;
        if (!job->admissions->record(job->references, &uris, &job->message)) return;
        if (size_t(uris.size()) != job->titles.size()) return;
        std::vector<int64_t> ids;
        for (size_t index=0;index<job->titles.size();++index) {
            if (!pending()) { job->error = Pending::Canceled; return; }
            int64_t id=0;
            if (vlc_addon_source_media_prepare(ml,uris[qsizetype(index)].toStdString().c_str(),job->titles[index].c_str(),&id)!=VLC_SUCCESS || id<=0) {
                job->error=Pending::Record; return;
            }
            ids.push_back(id);
        }
        // Keep the same store-to-database lock order as other native admissions.
        QLockFile configuration(QDir(job->store->storageDirectory()).filePath(QStringLiteral("store.lock")));
        if (!configuration.tryLock(0)) { job->error = Pending::Busy; return; }
        for (const auto& captured : job->configurations)
            if (!job->store->networkConfigurationMatches(captured.package, captured.settings, captured.revision)) { job->error = Pending::Changed; return; }
        {
            std::lock_guard<std::mutex> lock(shared->mutex); int expectedPhase = 0;
            if (shared->closed || shared->epoch != job->epoch || !job->target->current->load()
                || !job->phase->compare_exchange_strong(expectedPhase, 2)) { job->error = Pending::Canceled; return; }
        }
        // Admission is the cancellation boundary. Later cancellation cannot
        // reverse this one native transaction. No Qt event is necessary.
        job->success = vlc_addon_playlist_append_if_unchanged(ml, job->native.get(), ids.data(), ids.size()) == VLC_SUCCESS;
        if (!job->success) job->error = Pending::Commit;
    };
    const auto task = dispatcher.run(receiver, std::move(work), [owner, job] { if (owner) owner->completed(job); });
    if (owner) submitted(job, task);
    else if (lifetime && task) dispatcher.cancel(receiver, task);
}
void AddonNativeLibraryStore::releaseTarget(const QString& reference)
{
    if (const auto target = m_targets.take(reference)) target->current->store(false);
    emit revisionChanged();
}
void AddonNativeLibraryStore::invalidate()
{
    { std::lock_guard<std::mutex> lock(m_shared->mutex); ++m_shared->epoch; }
    for (const auto& target : m_targets) target->current->store(false);
    m_targets.clear();
    emit revisionChanged();
}
void AddonNativeLibraryStore::close()
{
    { std::lock_guard<std::mutex> lock(m_shared->mutex); if (m_shared->closed) return; m_shared->closed = true; ++m_shared->epoch; }
    const auto jobs = m_jobs; m_jobs.clear();
    const auto dispatcher = m_dispatcher;
    const auto lifetime = m_dispatcherLifetime;
    const QObject* receiver = this;
    for (const auto& target : m_targets) target->current->store(false);
    m_targets.clear();
    for (const auto& job : jobs) {
        int phase = 0; job->phase->compare_exchange_strong(phase, 1);
        if (lifetime && job->task) dispatcher.cancel(receiver, job->task);
    }
}
AddonDiscoveryNativeBackend AddonNativeLibraryStore::backend()
{
    const QPointer<AddonNativeLibraryStore> owner(this);
    AddonDiscoveryNativeBackend result;
    result.snapshot = [owner](const QString& target) { return owner ? owner->snapshot(target) : AddonQueueTransactions::Snapshot{}; };
    result.append = [owner](const auto& transaction, auto completion) {
        if (!owner) { completion(false, errorMessage(Pending::Unavailable)); return AddonQueueTransactions::Cancel{}; }
        return owner->append(transaction, std::move(completion));
    };
    return result;
}
}
