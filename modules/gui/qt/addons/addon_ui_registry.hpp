// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_manager.hpp"
#include <QMap>
#include <QSet>

namespace vlc::addons {
// The registry names surfaces and commands; package data never names Qt objects.
class AddonUiRegistry : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed FINAL)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList routes READ routes NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList commands READ commands NOTIFY changed FINAL)
public:
    explicit AddonUiRegistry(AddonManager*, QObject* parent = nullptr);
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
    static bool validViewDocument(const QJsonObject&);
signals:
    void changed();
    void routeRequested(const QString& route, const QVariantMap& parameters);
    void modalRequested(const QString& contribution, const QVariantMap& parameters);
    void commandCompleted(const QString& command, bool success, const QVariantMap& result);
private:
    void reload();
    bool permitted(const QVariantMap&, const QString& hook = {}) const;
    static bool replaceable(const QString& hook);
    static bool parametersValid(const QVariantMap&, const QJsonObject& schema);
    AddonManager* m_manager;
    QMap<QString, QVariantMap> m_entries;
    QJsonObject m_selected;
    QSet<QString> m_failed;
    QHash<QString, QString> m_runningCommands;
    QString m_error;
    int m_revision = 0;
};
}
