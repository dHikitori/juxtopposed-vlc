// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_queue_transactions.hpp"
#include "addon_source_admissions.hpp"
#include <atomic>
#include <deque>
#include <memory>
struct input_item_t;
struct vlc_playlist;
typedef struct vlc_playlist vlc_playlist_t;
struct vlc_playlist_listener_id;
struct vlc_playlist_callbacks;

namespace vlc::addons {
struct AddonDiscoveryNativeBackend;
class AddonNativeQueueNotifications final : public QObject
{
    Q_OBJECT
signals:
    void metadataChanged(quintptr playlistIdentity, const QList<quint64>& itemIds);
};
// Call this owner on its Qt thread. The playlist must outlive the owner.
// Admissions are saved on a worker, one action at a time in the order the
// actions arrive; each action then commits on this thread.
class AddonNativeQueueStore final : public QObject
{
    Q_OBJECT
public:
    AddonNativeQueueStore(vlc_playlist_t*, std::shared_ptr<AddonSourceAdmissions>, QObject* parent = nullptr);
    ~AddonNativeQueueStore() override;
    AddonQueueTransactions::Snapshot snapshot(const QString& target = QStringLiteral("queue")) const;
    AddonQueueTransactions::Cancel append(const AddonQueueTransactions::Transaction&, AddonQueueTransactions::Completion);
    AddonQueueTransactions::Cancel playReference(const AddonQueueTransactions::Transaction&, AddonQueueTransactions::Completion);
    AddonDiscoveryNativeBackend backend();
    static AddonNativeQueueNotifications* notifications();
    // done runs on this thread: at once when the reference is not current,
    // otherwise after the admission is saved, or with false at close().
    void refreshDisplay(const QVariantMap& reference, std::function<void(bool)> done = {});
    QVariantMap queuedReference(quint64 itemId) const;
    int pendingAhead(const QString& fillTag) const;
    QVariantMap admittedReference(const QString& uri) const;
    void close();
signals:
    void revisionChanged();
private:
    struct Pending;
    static const vlc_playlist_callbacks* callbacks();
    void changed();
    AddonQueueTransactions::Cancel submit(const std::shared_ptr<Pending>&);
    void schedule();
    void recorded(const std::shared_ptr<Pending>&);
    void commitAppend(const Pending&);
    void commitPlay(const Pending&);
    void commitRefresh(const Pending&);
    vlc_playlist_t* m_playlist;
    vlc_playlist_listener_id* m_listener = nullptr;
    std::shared_ptr<AddonSourceAdmissions> m_admissions;
    QString m_identity;
    quint64 m_revision = 1;
    // The front action is on the worker while m_working is set.
    std::deque<std::shared_ptr<Pending>> m_pending;
    bool m_working = false;
    std::shared_ptr<std::atomic_bool> m_closed = std::make_shared<std::atomic_bool>(false);
};
}
