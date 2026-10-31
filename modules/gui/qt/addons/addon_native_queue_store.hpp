// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_queue_transactions.hpp"
#include "addon_source_admissions.hpp"
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
    bool refreshDisplay(const QVariantMap& reference);
    QVariantMap queuedReference(quint64 itemId) const;
    int pendingAhead(const QString& fillTag) const;
    QVariantMap admittedReference(const QString& uri) const;
    void close();
signals:
    void revisionChanged();
private:
    static const vlc_playlist_callbacks* callbacks();
    void changed();
    vlc_playlist_t* m_playlist;
    vlc_playlist_listener_id* m_listener = nullptr;
    std::shared_ptr<AddonSourceAdmissions> m_admissions;
    QString m_identity;
    quint64 m_revision = 1;
};
}
