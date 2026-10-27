// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "addon_native_library_targets.hpp"
#include <QUuid>
#include <vector>
#include <string>
#include <vlc_common.h>
#include <vlc_addon_medialibrary.h>

namespace vlc::addons {
struct AddonNativeLibraryTargets::Listing {
    quint64 id = 0, task = 0, version = 0;
    std::string query;
    std::vector<std::pair<std::shared_ptr<vlc_addon_playlist_snapshot>,std::string>> rows;
    std::shared_ptr<std::atomic_int> phase = std::make_shared<std::atomic_int>(0);
    AddonDiscoveryService::PlaylistBackend::Choices done;
    bool available = false, success = false, more = false;
};
AddonNativeLibraryTargets::AddonNativeLibraryTargets(AddonNativeLibraryStore::Dispatcher dispatcher,
    AddonNativeLibraryStore* store, QObject* parent)
    : QObject(parent), m_dispatcher(std::move(dispatcher)), m_lifetime(m_dispatcher.lifetime), m_store(store)
{
    if (!store || !m_lifetime || !m_dispatcher.run || !m_dispatcher.cancel) m_closed->store(true);
    if (m_lifetime) connect(m_lifetime,&QObject::destroyed,this,&AddonNativeLibraryTargets::close);
    if (store) {
        connect(store,&QObject::destroyed,this,&AddonNativeLibraryTargets::close);
    }
}
AddonNativeLibraryTargets::~AddonNativeLibraryTargets() { close(); }
AddonDiscoveryService::PlaylistBackend AddonNativeLibraryTargets::backend()
{
    const QPointer<AddonNativeLibraryTargets> owner(this);
    return {
        [owner](const QString& query,auto done) {
            if (owner) return owner->list(query,std::move(done));
            done(false,{},false,{}); return AddonQueueTransactions::Cancel{};
        },
        [owner](const QString& choice,auto done) {
            if (owner) return owner->prepare(choice,std::move(done));
            done({},QObject::tr("The library is not available.")); return AddonQueueTransactions::Cancel{};
        },
        [owner](const QString& target) { if (owner && owner->m_store) owner->m_store->releaseTarget(target); },
        [owner](const QStringList& choices) { if (owner) for (const auto& choice:choices) owner->m_choices.remove(choice); }
    };
}
AddonQueueTransactions::Cancel AddonNativeLibraryTargets::list(const QString& query,AddonDiscoveryService::PlaylistBackend::Choices done)
{
    if (m_closed->load() || query.size()>256 || m_listings.size()>=4) {
        done(false,{},false,tr("The library picker is not available. Try again.")); return {};
    }
    m_choices.clear(); ++m_version;
    auto job=std::make_shared<Listing>(); job->id=++m_next; job->version=m_version;
    job->query=query.trimmed().toStdString(); job->done=std::move(done); m_listings.insert(job->id,job);
    const auto closed=m_closed; const auto dispatcher=m_dispatcher; const auto lifetime=m_lifetime;
    const QPointer<AddonNativeLibraryTargets> owner(this); const QObject* receiver=this;
    const auto task=dispatcher.run(receiver,[closed,job](vlc_medialibrary_t* ml) {
        if (closed->load() || job->phase->load()!=0 || !vlc_addon_playlist_cas_available(ml)) return;
        job->available=true;
        vlc_ml_query_params_t params{}; params.i_nbResults=201; params.i_sort=VLC_ML_SORTING_ALPHA;
        params.psz_pattern=job->query.empty()?nullptr:job->query.c_str();
        auto* values=vlc_ml_list_playlists(ml,&params,VLC_ML_PLAYLIST_TYPE_ALL);
        if (!values) return;
        job->more=values->i_nb_items>200;
        for (size_t i=0;i<values->i_nb_items && i<200;++i) {
            const auto& value=values->p_items[i];
            if (value.i_id<=0 || value.b_is_read_only || closed->load() || job->phase->load()!=0) continue;
            std::shared_ptr<vlc_addon_playlist_snapshot> snapshot(vlc_addon_playlist_snapshot_create(ml,value.i_id),vlc_addon_playlist_snapshot_destroy);
            if (!snapshot) continue;
            // Read the label after capture. A later replacement cannot acquire
            // the earlier snapshot's token, even if it has the same name.
            auto* current=vlc_ml_get_playlist(ml,value.i_id);
            if (current && !current->b_is_read_only && current->psz_name)
                job->rows.emplace_back(std::move(snapshot),std::string(current->psz_name).substr(0,1024));
            if (current) vlc_ml_playlist_release(current);
        }
        vlc_ml_playlist_list_release(values); job->success=true;
    },[owner,job] { if (owner) owner->listed(job); });
    if (owner && owner->m_listings.value(job->id)==job) {
        job->task=task; if (!task) owner->listed(job);
    } else if (lifetime && task) dispatcher.cancel(receiver,task);
    return [phase=job->phase] { int pending=0; phase->compare_exchange_strong(pending,1); };
}
void AddonNativeLibraryTargets::listed(const std::shared_ptr<Listing>& job)
{
    if (m_listings.take(job->id)!=job) return;
    int pending=0;
    if (m_closed->load() || !job->phase->compare_exchange_strong(pending,2)) return;
    QVariantList choices;
    if (job->version==m_version && job->success) for (const auto& row:job->rows) {
        const QString token=QStringLiteral("playlist-choice:")+QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_choices.insert(token,row.first); choices.append(QVariantMap{{"ref",token},{"name",QString::fromUtf8(row.second).left(256)}});
    }
    const bool current=job->version==m_version;
    job->done(current && job->available,choices,job->more,
        current && job->success?QString():tr("The app cannot read the current playlists."));
}
AddonQueueTransactions::Cancel AddonNativeLibraryTargets::prepare(const QString& choice,AddonDiscoveryService::PlaylistBackend::Prepared done)
{
    if (m_closed->load() || !m_store || !m_choices.contains(choice)) {
        done({},tr("The playlist list changed. Select it again.")); return {};
    }
    const QPointer<AddonNativeLibraryTargets> owner(this); const QPointer<AddonNativeLibraryStore> store(m_store);
    const auto target=store->adoptSnapshot(m_choices.take(choice));
    if (!owner || m_closed->load()) {
        if (store && !target.isEmpty()) store->releaseTarget(target);
        done({},QObject::tr("The library picker was closed.")); return {};
    }
    if (target.isEmpty()) { done({},tr("The app cannot prepare this playlist. Try again.")); return {}; }
    done(target,{}); return {};
}
void AddonNativeLibraryTargets::close()
{
    if (m_closed->exchange(true)) return;
    m_choices.clear(); const auto listings=m_listings; m_listings.clear();
    const auto dispatcher=m_dispatcher; const auto lifetime=m_lifetime; const QObject* receiver=this;
    for (const auto& job:listings) { job->phase->store(1); if (lifetime && job->task) dispatcher.cancel(receiver,job->task); }
}
}
