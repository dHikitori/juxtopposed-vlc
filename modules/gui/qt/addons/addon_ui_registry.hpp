// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_manager.hpp"
#include <QMap>
#include <QSet>
#include <QTimer>
#include <functional>

namespace vlc::addons {
class AddonUiAssets;
class AddonCatalogueView;
// The registry names surfaces and commands; package data never names Qt objects.
class AddonUiRegistry : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed FINAL)
    Q_PROPERTY(bool available READ available NOTIFY availabilityChanged FINAL)
    Q_PROPERTY(bool loading READ loading NOTIFY availabilityChanged FINAL)
    Q_PROPERTY(quint64 documentRevision READ documentRevision NOTIFY documentsChanged FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList routes READ routes NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList commands READ commands NOTIFY changed FINAL)
    // Player panels (player.panels@1): one entry for each enabled panel, with
    // its checked icon markup and whether it has content for the current media.
    Q_PROPERTY(QVariantList panels READ panels NOTIFY panelsChanged FINAL)
    // True when an enabled panel can read the media siblings, so the app
    // prepares them for the current media. Otherwise the app does no work.
    Q_PROPERTY(bool siblingsWanted READ siblingsWanted NOTIFY panelsChanged FINAL)
public:
    struct Backend {
        std::function<QString(const QString&, const QString&, const QVariantMap&)> invoke;
        std::function<void(const QString&)> cancel;
    };
    explicit AddonUiRegistry(AddonManager*, QObject* parent = nullptr);
    AddonUiRegistry(AddonManager*, Backend, QObject* parent = nullptr);
    ~AddonUiRegistry() override;
    void setAssetService(AddonUiAssets*);
    int revision() const { return m_revision; }
    bool available() const noexcept;
    bool loading() const noexcept;
    quint64 documentRevision() const noexcept { return m_documentRevision; }
    QString errorMessage() const { return m_error; }
    QVariantList routes() const;
    QVariantList commands() const;
    QVariantList panels() const;
    bool siblingsWanted() const;
    // The app calls this when the playing media changes. Each panel gets one
    // context call; a reply for older media is ignored.
    Q_INVOKABLE void setPanelMedia(const QVariantMap& media);
    // Host data for open panels changed (titles, watch state, artwork).
    // Open panels get one more context call, at most once a second.
    Q_INVOKABLE void refreshPanels();
    // An open panel's session. The registry sends it the context call now.
    Q_INVOKABLE bool attachPanelSession(const QString& sessionId);
    Q_INVOKABLE QVariantList sections(const QString& hook) const;
    Q_INVOKABLE QVariantList replacementChoices(const QString& hook) const;
    Q_INVOKABLE QVariantMap replacement(const QString& hook) const;
    Q_INVOKABLE bool selectReplacement(const QString& hook, const QString& contributionId);
    Q_INVOKABLE QVariantMap view(const QString& contributionId) const;
    Q_INVOKABLE bool openRoute(const QString& route, const QVariantMap& parameters = {});
    Q_INVOKABLE bool openModal(const QString& contributionId, const QVariantMap& parameters = {});
    Q_INVOKABLE bool triggerCommand(const QString& command, const QVariantMap& arguments = {});
    Q_INVOKABLE bool triggerReplacement(const QString& hook, const QVariantMap& arguments = {});
    Q_INVOKABLE void reportViewFailure(const QString& contributionId);
    Q_INVOKABLE QString createSession(const QString& contributionId, const QVariantMap& parameters = {}, const QVariantMap& context = {});
    Q_INVOKABLE QVariantMap session(const QString& sessionId) const;
    Q_INVOKABLE bool dispatchSession(const QString& sessionId, const QString& command, const QVariantMap& arguments = {});
    Q_INVOKABLE void suspendSession(const QString& sessionId, bool suspended);
    Q_INVOKABLE void closeSession(const QString& sessionId);
    Q_INVOKABLE QString executeReplacement(const QString& hook, const QVariantMap& arguments = {});
    Q_INVOKABLE void acknowledgeSession(const QString& sessionId);
    Q_INVOKABLE bool completeModal(const QString& ticket, const QVariantMap& result);
    Q_INVOKABLE void cancelModal(const QString& ticket);
    // Display eligibility only; actions need a current, ready session.
    Q_INVOKABLE bool canPrepareSelection(const QString& contributionId, const QString& hook) const;
    Q_INVOKABLE bool canUseSelection(const QString& contributionId, const QString& hook) const;
    Q_INVOKABLE bool validateSessionResult(const QString& sessionId, const QVariantMap& result) const;
    Q_INVOKABLE bool bindSessionOwner(const QString& sessionId, QObject* owner);
    Q_INVOKABLE QString claimImage(const QString& sessionId, QObject* owner);
    Q_INVOKABLE void releaseImage(const QString& claim);
    Q_INVOKABLE QString assetUrlForClaim(const QString& sessionId, const QString& alias, const QString& claim) const;
    static bool validViewDocument(const QJsonObject&);
public slots:
    void operationFinished(const QString& request, const QString& contribution, const QString& status, const QVariantMap& data, const QString& error);
signals:
    void changed();
    void availabilityChanged();
    void documentsChanged();
    void routeRequested(const QString& route, const QVariantMap& parameters);
    void modalRequested(const QString& contribution, const QVariantMap& parameters);
    // A queue button asked for this player panel (package/contribution).
    void panelRequested(const QString& panel);
    void commandCompleted(const QString& command, bool success, const QVariantMap& result);
    void sessionChanged(const QString& sessionId);
    void sessionFailed(const QString& sessionId);
    void replacementFinished(const QString& ticket, bool success);
    void modalSessionRequested(const QString& ticket, const QString& contribution, const QVariantMap& parameters);
    void modalFinished(const QString& ticket, bool accepted, const QVariantMap& result);
    void imageBudgetChanged();
    void panelsChanged();
    // A command reply that a user click started asked for a host effect:
    // play, rescan, identify or source. The app checks the data and runs it.
    void panelEffect(const QString& sessionId, const QString& packageId, const QString& effect, const QVariantMap& value);
private:
    struct Session {
        QString contribution; QVariantMap descriptor, parameters, context;
        quint64 generation = 0; bool suspended = false;
        QPointer<QObject> owner = nullptr;
        QMetaObject::Connection ownerDestroyed{};
        QString documentTicket;
        bool ready=true,assetsOpened=false;
    };
    struct Pending { QString command, session, replacement, panel; quint64 generation = 0, panelRevision = 0; bool gesture = false; };
    bool dispatch(const QString& command, const QVariantMap& arguments, const QString& session, const QString& replacement,
                  const QVariantMap& extra = {}, const QString& panel = {}, bool gesture = false);
    void dispatchPanelContext(const QString& panel);
    void resumePanelContext(const QString& sessionId);
    void askNewPanels();
    void panelReply(const Pending&, bool success, const QVariantMap& data);
    QVariantMap projectContext(const QVariantMap& entry, const QVariantMap& context) const;
    void cancelSessionJobs(const QString& session);
    void reload();
    void reloadAttached();
    void viewFreshnessChanged();
    void viewDocumentsChanged();
    void retireSessions(bool notify=true);
    bool materializeSession(const QString&);
    QVariantMap descriptor(const QString&,bool request) const;
    QJsonObject scope(const QString& package,const QString& capability) const;
    static QString documentField(const QVariantMap&);
    QString holdDocument(const QString&);
    void dropDocument(const QString&);
    bool permitted(const QVariantMap&, const QString& hook = {}) const;
    static bool replaceable(const QString& hook);
    static bool parametersValid(const QVariantMap&, const QJsonObject& schema);
    const bool m_attached;
    QPointer<AddonCatalogueView> m_catalogue;
    QPointer<AddonManager> m_manager;
    QPointer<AddonUiAssets> m_assets;
    QMap<QString, QVariantMap> m_entries;
    QJsonObject m_selected;
    QSet<QString> m_failed;
    QHash<QString, Pending> m_runningCommands;
    QHash<QString, Session> m_sessions;
    QHash<QString, QString> m_modals, m_replacementViews;
    QHash<QString, QString> m_panelSessions; // panel contribution -> open session
    QSet<QString> m_panelContextWanted;      // panel sessions to call once ready and not suspended
    QHash<QString, bool> m_panelAvailable;
    QVariantMap m_panelMedia;
    quint64 m_panelRevision = 0;
    QTimer* m_panelRefresh = nullptr;
    Backend m_backend;
    quint64 m_generation = 0;
    QString m_error;
    int m_revision = 0;
    quint64 m_documentRevision=0;
    mutable quint64 m_validatedDocumentRevision=0;
    mutable QHash<QString,bool> m_documentValidity;
    bool m_ownerRetired=false,m_destroying=false,m_reloading=false;
};
}
