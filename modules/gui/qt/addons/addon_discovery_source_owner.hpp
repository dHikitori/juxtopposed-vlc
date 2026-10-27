// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_native_source_resolver.hpp"
#include "addon_native_authority.hpp"
#include "addon_stream_proxy.hpp"
#include <QHash>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>

namespace vlc::addons {
class AddonManager;
class AddonSourceAdmissions;
struct DiscoverySourceTestAccess;
// Call this owner on the manager thread. The native instance must outlive close().
// Activation does not give a source playback approval. Only a native primary
// input open can consume a source intent and ask a provider for a stream URL.
class AddonDiscoverySourceOwner final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed FINAL)
public:
    AddonDiscoverySourceOwner(AddonManager*, vlc_object_t*, std::shared_ptr<AddonSourceAdmissions>,
                              AddonStreamLifetime::Handle, QObject* parent = nullptr);
    ~AddonDiscoverySourceOwner() override;
    bool activate();
    bool active() const;
    QString errorMessage() const { return m_error; }
    Q_INVOKABLE QString refreshSource(const QString& contribution);
    Q_INVOKABLE bool canUseSource(const QString& contribution) const;
    bool canUseReference(const QVariantMap& reference) const;
    std::function<bool()> referenceGuard(const QVariantList& references) const;
    Q_INVOKABLE QString requestApproval(const QString& contribution);
    Q_INVOKABLE QVariantMap approval(const QString& request) const;
    Q_INVOKABLE bool approve(const QString& request);
    Q_INVOKABLE bool revoke(const QString& request);
    Q_INVOKABLE bool resetApprovals();
    Q_INVOKABLE void discardApproval(const QString& request);
    void close();
signals:
    void changed();
    void approvalReady(const QString& request);
    void actionFinished(const QString& request, bool success, const QString& error);
private:
    friend struct DiscoverySourceTestAccess;
    struct NativeStop;
    struct Shared;
    struct Gate;
    struct Review;
    struct Cache;
    struct Resolve;
    struct SourceCapture;
    using Prepared = AddonNativeSourceResolver::PreparedCallback;
    using Cancel = AddonNativeSourceResolver::Cancel;
    using Current = AddonNativeSourceResolver::Current;
    using Admission = AddonNativeSourceResolver::Admission;
    using PrepareTransport = std::function<Cancel(const Admission&,Current,AddonStreamProxy::Spec,Prepared)>;
    static bool post(const std::shared_ptr<Gate>&, std::function<void(AddonDiscoverySourceOwner*)>);
    static std::optional<AddonSourceConsent::Binding> binding(const AddonNativeAuthority&);
    static bool admitted(const std::shared_ptr<AddonSourceAdmissions>&,const AddonSourceConsent::Binding&,const QString&);
    void initialized(bool,const QString&);
    QString captureSource(const QString&,bool review);
    void captured(const QString&,const QString&,std::shared_ptr<SourceCapture>,const QString&);
    bool changeApproval(const QString&,bool approve);
    void changedApproval(const QString&,const QString&,bool,const QString&);
    AddonNativeSourceResolver::AdmissionReader captureAdmission(const QString&,quint64);
    Cancel resolve(const Admission&,Current,Prepared);
    void resolved(const QString&,const QString&,const QString&,const QVariantMap&,const QString&);
    void cancelResolve(const QString&);
    void tick();
    void notify();
    QPointer<AddonManager> m_manager;
    vlc_object_t* m_instance;
    AddonStreamLifetime::Handle m_lifetime;
    std::shared_ptr<NativeStop> m_stop;
    std::shared_ptr<Shared> m_shared;
    std::shared_ptr<Gate> m_gate;
    std::unique_ptr<AddonNativeSourceResolver> m_resolver;
    QHash<QString,std::shared_ptr<Review>> m_reviews;
    QHash<QString,std::shared_ptr<Cache>> m_cache;
    QHash<QString,QString> m_refreshes,m_invocations;
    QHash<QString,std::shared_ptr<Resolve>> m_resolves;
    PrepareTransport m_prepareTransport;
    QTimer m_poll;
    QString m_error;
    unsigned m_controls=0;
    bool m_starting=false,m_closing=false,m_noticeQueued=false;
};
}
