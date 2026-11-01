// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_queue_transactions.hpp"
#include "addon_source_admissions.hpp"
#include <QPointer>
#include <memory>
struct vlc_medialibrary_t;
struct vlc_addon_playlist_snapshot;

namespace vlc::addons {
class AddonManager;
struct AddonDiscoveryNativeBackend;
// Call this owner on its Qt thread. Native work must not wait for Qt.
class AddonNativeLibraryStore final : public QObject
{
    Q_OBJECT
public:
    using NativeWork = std::function<void(vlc_medialibrary_t*)>;
    using GuiCompletion = std::function<void()>;
    struct Dispatcher {
        QObject* lifetime = nullptr;
        // Retain work until it exits. Keep the library valid during that call.
        // Invoke completion on the owner thread, or discard it with receiver.
        std::function<quint64(const QObject* receiver, NativeWork, GuiCompletion)> run;
        // Call only on the owner thread. Cancellation need not stop native work.
        std::function<void(const QObject* receiver, quint64 task)> cancel;
    };
    template<typename Library>
    static Dispatcher dispatcherFor(Library* library) {
        const QPointer<Library> owner(library);
        Dispatcher result; result.lifetime = library;
        result.run = [owner](const QObject* receiver, NativeWork work, GuiCompletion done) {
            return owner ? owner->runOnMLThread(receiver, std::move(work), std::move(done), "addon-native-library") : quint64(0);
        };
        result.cancel = [owner](const QObject* receiver, quint64 task) { if (owner) owner->cancelMLTask(receiver, task); };
        return result;
    }
    AddonNativeLibraryStore(Dispatcher, std::shared_ptr<AddonSourceAdmissions>, AddonManager*, QObject* parent = nullptr);
    ~AddonNativeLibraryStore() override;
    // Host C++ only. One target permits one append attempt. Never pass the ID
    // to a guest. A target owns its flat native snapshot until release or use.
    QString selectPlaylist(qint64 playlistId);
    // The host picker transfers its exact earlier snapshot. Never refresh it.
    QString adoptSnapshot(std::shared_ptr<vlc_addon_playlist_snapshot>);
    void releaseTarget(const QString&);
    AddonQueueTransactions::Snapshot snapshot(const QString& target) const;
    AddonQueueTransactions::Cancel append(const AddonQueueTransactions::Transaction&, AddonQueueTransactions::Completion);
    AddonDiscoveryNativeBackend backend();
    void close();
signals:
    void targetReady(const QString& target, bool available, const QString& error);
    void revisionChanged();
private:
    struct Shared;
    struct Target;
    struct Pending;
    void invalidate();
    void submitted(const std::shared_ptr<Pending>&, quint64 task);
    void completed(const std::shared_ptr<Pending>&);
    bool resume(const std::shared_ptr<Pending>&);
    void dispatchAppend(const std::shared_ptr<Pending>&);
    Dispatcher m_dispatcher;
    QPointer<QObject> m_dispatcherLifetime;
    QPointer<AddonManager> m_manager;
    std::shared_ptr<AddonSourceAdmissions> m_admissions;
    std::shared_ptr<Shared> m_shared;
    QHash<QString, std::shared_ptr<Target>> m_targets;
    QHash<quint64, std::shared_ptr<Pending>> m_jobs;
    quint64 m_next = 0;
};
}
