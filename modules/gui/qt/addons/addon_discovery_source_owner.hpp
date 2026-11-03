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
// A source capture or a native primary open saves the approval that accepting
// the package's media.stream permission gave. Only a native primary input open
// can consume a source intent and ask a provider for a stream URL.
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
    // These are memory-only platform hints. Unknown does not prevent
    // configuration capture or stable-reference queue admission.
    bool transportKnown() const;
    bool transportAvailable() const;
    QString errorMessage() const { return m_error; }
    Q_INVOKABLE QString refreshSource(const QString& contribution);
    Q_INVOKABLE bool canUseSource(const QString& contribution) const;
    bool canUseReference(const QVariantMap& reference) const;
    std::function<bool()> referenceGuard(const QVariantList& references) const;
    void close();
signals:
    void changed();
private:
    friend struct DiscoverySourceTestAccess;
    struct NativeStop;
    struct Shared;
    struct Gate;
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
    QString captureSource(const QString&);
    void captured(const QString&,const QString&,std::shared_ptr<SourceCapture>,const QString&);
    AddonNativeSourceResolver::AdmissionReader captureAdmission(const QString&,quint64);
    Cancel resolve(const Admission&,Current,Prepared);
    void resolved(const QString&,const QString&,const QString&,const QVariantMap&,const QString&);
    void cancelResolve(const QString&);
    void tick();
    void scheduleMaintenance();
    void notify();
    QPointer<AddonManager> m_manager;
    vlc_object_t* m_instance;
    AddonStreamLifetime::Handle m_lifetime;
    std::shared_ptr<NativeStop> m_stop;
    std::shared_ptr<Shared> m_shared;
    std::shared_ptr<Gate> m_gate;
    std::unique_ptr<AddonNativeSourceResolver> m_resolver;
    QHash<QString,std::shared_ptr<Cache>> m_cache;
    QHash<QString,QString> m_refreshes,m_invocations;
    QHash<QString,std::shared_ptr<Resolve>> m_resolves;
    PrepareTransport m_prepareTransport;
    std::unique_ptr<AddonNativeWake> m_wake;
    QTimer m_poll;
    QString m_error;
    bool m_starting=false,m_closing=false,m_noticeQueued=false;
};
}
