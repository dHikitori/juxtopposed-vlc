// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_manager.hpp"
#include <QMap>
#include <QSet>
#include <functional>

namespace vlc::addons {
class AddonUiAssets;
// The registry names surfaces and commands; package data never names Qt objects.
class AddonUiRegistry : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList routes READ routes NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList commands READ commands NOTIFY changed FINAL)
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
    QString errorMessage() const { return m_error; }
    QVariantList routes() const;
    QVariantList commands() const;
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
    void routeRequested(const QString& route, const QVariantMap& parameters);
    void modalRequested(const QString& contribution, const QVariantMap& parameters);
    void commandCompleted(const QString& command, bool success, const QVariantMap& result);
    void sessionChanged(const QString& sessionId);
    void sessionFailed(const QString& sessionId);
    void replacementFinished(const QString& ticket, bool success);
    void modalSessionRequested(const QString& ticket, const QString& contribution, const QVariantMap& parameters);
    void modalFinished(const QString& ticket, bool accepted, const QVariantMap& result);
    void imageBudgetChanged();
private:
    struct Session {
        QString contribution; QVariantMap descriptor, parameters, context;
        quint64 generation = 0; bool suspended = false;
        QPointer<QObject> owner = nullptr;
        QMetaObject::Connection ownerDestroyed{};
    };
    struct Pending { QString command, session, replacement; quint64 generation = 0; };
    bool dispatch(const QString& command, const QVariantMap& arguments, const QString& session, const QString& replacement);
    QVariantMap projectContext(const QVariantMap& entry, const QVariantMap& context) const;
    void cancelSessionJobs(const QString& session);
    void reload();
    bool permitted(const QVariantMap&, const QString& hook = {}) const;
    static bool replaceable(const QString& hook);
    static bool parametersValid(const QVariantMap&, const QJsonObject& schema);
    QPointer<AddonManager> m_manager;
    QPointer<AddonUiAssets> m_assets;
    QMap<QString, QVariantMap> m_entries;
    QJsonObject m_selected;
    QSet<QString> m_failed;
    QHash<QString, Pending> m_runningCommands;
    QHash<QString, Session> m_sessions;
    QHash<QString, QString> m_modals, m_replacementViews;
    Backend m_backend;
    quint64 m_generation = 0;
    QString m_error;
    int m_revision = 0;
};
}
