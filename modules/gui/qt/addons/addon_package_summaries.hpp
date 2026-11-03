// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_package_reader.hpp"
#include "addon_repository_jobs.hpp"
#include <QObject>
#include <QPointer>
#include <QTimer>

namespace vlc::addons {
class AddonManager;
class AddonCatalogueView;
// Owner-thread UI projection. All disk/platform credential reads run on joined
// Qt work. Call isCurrent at action and final confirmation time; rows are not
// authority for a Manager mutation. Close before the app's joined drain.
class AddonPackageSummaries final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList rows READ rows NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool available READ isCurrent NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool retainedAvailable READ retainedAvailable NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool safeMode READ safeMode NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool safeModeKnown READ safeModeKnown NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool displayBacked READ displayBacked CONSTANT FINAL)
public:
    using Capture = std::function<PackageSummaryReader()>;
    AddonPackageSummaries(AddonManager*, AddonStreamLifetime::Handle, QObject* parent = nullptr);
    // An attached display view owns the only reader and native notifications.
    AddonPackageSummaries(AddonCatalogueView&,AddonStreamLifetime::Handle,QObject* parent=nullptr);
    // C++ host/test injection. Capture runs only on this owner's Qt thread.
    AddonPackageSummaries(Capture, AddonStreamLifetime::Handle, QObject* parent = nullptr);
    ~AddonPackageSummaries() override;
    QVariantList rows() const;
    bool loading() const noexcept;
    QString error() const;
    bool safeMode() const;
    bool retainedAvailable() const noexcept;
    bool safeModeKnown() const;
    bool displayBacked() const noexcept { return m_displayMode; }
    Q_INVOKABLE bool isCurrent() const noexcept;
    Q_INVOKABLE void refresh();
    void close();
signals:
    void stateChanged();
    void loadingFinished(bool success);
private:
    void attachDisplay(AddonCatalogueView*);
    void scheduleDisplay();
    void schedule();
    void start();
    void completed(quint64, const std::shared_ptr<std::optional<PackageSummarySnapshot>>&, AddonRepositoryJobs::Result);
    void publish(quint64, bool terminal, bool success);
    QPointer<AddonCatalogueView> m_display;
    bool m_displayMode=false,m_displayScheduled=false;
    QPointer<AddonManager> m_bootstrapManager;
    Capture m_capture;
    AddonStreamLifetime::Handle m_lifetime;
    AddonRepositoryJobs m_jobs;
    QTimer m_poll;
    std::optional<PackageSummarySnapshot> m_snapshot;
    QString m_error;
    quint64 m_generation = 0, m_publishedGeneration = 0;
    bool m_loading = false, m_pending = false, m_scheduled = false, m_closed = false, m_started = false;
};
}
