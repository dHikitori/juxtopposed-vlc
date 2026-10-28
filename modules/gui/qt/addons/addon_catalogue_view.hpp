// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_display_catalogue_reader.hpp"
#include "addon_repository_jobs.hpp"
#include <QObject>
#include <memory>
namespace vlc::addons {
class AddonManager;
class AddonNativeWake;
// Display only. Parsed values remain readable while refresh is pending; callers
// must separately gate actions with isCurrent(). No getter reads files or keys.
class AddonCatalogueView final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ isCurrent NOTIFY freshnessChanged FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY freshnessChanged FINAL)
    Q_PROPERTY(QString error READ error NOTIFY freshnessChanged FINAL)
    Q_PROPERTY(quint64 revision READ revision NOTIFY catalogueChanged FINAL)
    Q_PROPERTY(quint64 documentRevision READ documentRevision NOTIFY documentsChanged FINAL)
public:
    struct Backend {
        std::function<std::optional<DisplayCatalogueSource>()> capture;
        std::function<StoreAuthorityChanges(std::function<void()>)> subscribe;
    };
    AddonCatalogueView(Backend,AddonStreamLifetime::Handle,QObject* parent=nullptr);
    AddonCatalogueView(AddonManager*,AddonStreamLifetime::Handle);
    ~AddonCatalogueView() override;
    bool isCurrent() const noexcept;
    // Display classification only; excludes refresh/loading and document demand.
    // Actions still require isCurrent() and their exact authoritative invoker.
    bool retainedCatalogueCurrent() const noexcept;
    bool loading() const noexcept { return m_loading; }
    QString error() const { return m_error; }
    quint64 revision() const noexcept { return m_revision; }
    quint64 documentRevision() const noexcept { return m_documentRevision; }
    Q_INVOKABLE QVariantList rows() const;
    Q_INVOKABLE QVariantList contributions(const QString&,bool includeDisabled=false) const;
    Q_INVOKABLE QVariantList providers(const QString&) const;
    Q_INVOKABLE QVariantList themes() const;
    Q_INVOKABLE QString selectedTheme() const;
    Q_INVOKABLE QVariantMap preferences(const QString& section) const;
    Q_INVOKABLE QString documentState(const QString&,const QString&) const;
    Q_INVOKABLE bool settingsReady(const QString&) const;
    Q_INVOKABLE QString settingsState(const QString&) const;
    Q_INVOKABLE QString settingsStamp(const QString&) const;
    // Host tickets hold at most 32 active documents/settings forms. Released
    // values use a 16-entry inactive LRU; unopened docs have no retained body.
    Q_INVOKABLE QString acquireDocument(const QString&,const QString&);
    Q_INVOKABLE QString acquireSettings(const QString&);
    Q_INVOKABLE QString acquireFeature(const QString&);
    Q_INVOKABLE void release(const QString& ticket);
    bool requestCurrent(const QString& ticket) const noexcept;
    Q_INVOKABLE bool ensureDocument(const QString&,const QString&);
    Q_INVOKABLE bool requestPresence(const QString& feature);
    Q_INVOKABLE QJsonObject resource(const QString& contribution,const QString& field) const;
    Q_INVOKABLE QVariantMap settingsSchema(const QString&) const;
    Q_INVOKABLE QVariantMap settingsValues(const QString&) const;
    Q_INVOKABLE QVariantMap settingsMessages(const QString&) const;
    Q_INVOKABLE QVariantMap settingsSecretStatus(const QString&) const;
    Q_INVOKABLE QJsonObject grantedScope(const QString& package,const QString& capability) const;
    Q_INVOKABLE QVariantMap capabilities() const;
    qint64 retainedDocumentBytes() const noexcept;
    Q_INVOKABLE void refresh();
    void inputsChanged(); // owner-thread memory comparison, no disk or hash
    void close();
    unsigned captures() const noexcept { return m_captures; }
    unsigned nativeWakes() const noexcept { return m_nativeWakes; }
    unsigned publications() const noexcept { return m_publications; }
    static AddonCatalogueView* attached(QObject*);
signals:
    void catalogueChanged(); // never forwarded to Manager::revisionChanged
    void freshnessChanged();
    void documentsChanged();
private:
    struct Data;
    struct Result;
    static std::shared_ptr<const Data> project(DisplayCatalogueSnapshot,DisplayCatalogueSource,const DisplayCatalogueRequest&);
    DisplayCatalogueRequest requests() const;
    bool knownKey(const QString&) const;
    QString acquire(const QString&);
    void resourcesChanged();
    void trim();
    void publishDocuments();
    bool holdBootstrap(DisplayCatalogueSource);
    void schedule();
    void start();
    bool waitForProcessSlot();
    void stopProcessWait();
    void completed(quint64,quint64,const std::shared_ptr<Result>&,AddonRepositoryJobs::Result);
    void publishFreshness();
    bool advance();
    Backend m_backend;
    AddonStreamLifetime::Handle m_lifetime;
    AddonRepositoryJobs m_jobs;
    std::unique_ptr<AddonNativeWake> m_wake;
    StoreAuthorityChanges m_subscription;
    std::unique_ptr<AddonNativeWake> m_processWake;
    NativeNotification m_processSubscription;
    std::shared_ptr<const Data> m_data;
    std::optional<DisplayCatalogueSource> m_inputs; // reader is always cleared
    QString m_error;
    QHash<QString,QString> m_tickets;
    QHash<QString,std::shared_ptr<DisplayDemand>> m_activeDemands;
    QStringList m_lru,m_presenceFeatures;
    quint64 m_ticketNext=0,m_requestRevision=0;
    quint64 m_generation=0,m_publishedGeneration=0,m_revision=0,m_documentRevision=0;
    bool m_loading=false,m_pending=false,m_scheduled=false,m_closed=false,m_destroying=false;
    bool m_waitingProcess=false,m_retryNative=false;
    quint64 m_runningGeneration=0;
    unsigned m_captures=0,m_nativeWakes=0,m_publications=0;
};
}
