// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>
#include <QHash>
#include <QUrl>
#include <functional>

namespace vlc::addons {
class AddonSessionHost;
class AddonManager;
// Trusted host QML only. Do not expose this object through guest UI commands.
class AddonSessionUi final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList controllers READ controllers NOTIFY changed)
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY changed)
    Q_PROPERTY(QVariantMap review READ review NOTIFY changed)
    Q_PROPERTY(bool preparing READ preparing NOTIFY changed)
    Q_PROPERTY(bool starting READ starting NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool displayCurrent READ displayCurrent NOTIFY changed)
    Q_PROPERTY(bool displayLoading READ displayLoading NOTIFY changed)
    Q_PROPERTY(QString displayError READ displayError NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
public:
    struct Catalog {
        QObject* lifetime=nullptr;
        std::function<QVariantList()> read;
        // Attached display mode is fixed at construction. These callbacks read
        // memory or hold one explicit feature ticket. They grant no session.
        bool attached=false;
        std::function<QVariantMap()> state;
        std::function<void(bool)> setActive;
        std::function<void()> retry;
        QPointer<QObject> changes;
        std::function<void()> cataloguePublished;
    };
    struct Backend {
        QObject* lifetime=nullptr;
        Catalog catalog;
        std::function<QString(const QString&,const QUrl&,const QString&,const QStringList&)> prepare;
        std::function<QVariantMap(const QString&)> review;
        std::function<QString(const QString&,const QStringList&)> accept;
        std::function<void(const QString&)> cancel,end;
        std::function<bool(const QString&)> resume;
        std::function<QVariantList()> sessions;
    };
    explicit AddonSessionUi(Backend,QObject* parent=nullptr);
    AddonSessionUi(AddonSessionHost*,Catalog,QObject* parent=nullptr);
    AddonSessionUi(AddonSessionHost*,AddonManager*,QObject* parent=nullptr);
    ~AddonSessionUi() override;
    QVariantList controllers() const { return m_controllers; }
    QVariantList sessions() const { return m_sessions; }
    QVariantMap review() const { return m_review; }
    bool preparing() const { return m_preparing; }
    bool starting() const { return !m_starting.isEmpty(); }
    QString errorMessage() const { return m_error; }
    QString status() const { return m_status; }
    bool displayCurrent() const { return m_displayCurrent; }
    bool displayLoading() const { return m_displayLoading; }
    QString displayError() const { return m_displayError; }
    int revision() const { return m_revision; }
    Q_INVOKABLE bool prepare(const QString& controller,const QString& relay,const QString& room);
    Q_INVOKABLE bool accept(const QStringList& operations={});
    Q_INVOKABLE void cancelReview();
    Q_INVOKABLE void viewClosed();
    Q_INVOKABLE void setDisplayActive(bool active);
    Q_INVOKABLE void retryDisplay();
    Q_INVOKABLE bool resume(const QString& session);
    Q_INVOKABLE bool end(const QString& session);
    Q_INVOKABLE void refresh();
    void close();
    // App-thread signal adapters. These methods grant no approval.
    void approvalReady(const QString& ticket);
    void sessionChanged(const QString& session,const QString& status);
signals:
    void changed();
private:
    friend class AddonSessionUiTestAccess;
    void publish();
    void clearReview();
    bool fail(const QString&);
    bool currentDisplay(const QString& controller);
    Backend m_backend;
    QPointer<QObject> m_owner,m_catalogOwner;
    QVariantList m_controllers,m_sessions;
    QVariantMap m_review;
    QString m_ticket,m_reviewController,m_earlyReady,m_starting,m_error,m_status;
    QString m_displayError;
    qint64 m_expires=0;
    QTimer m_timer;
    int m_revision=0;
    quint64 m_generation=0;
    quint64 m_refreshEpoch=0;
    quint64 m_displayEpoch=0;
    QHash<QString,QString> m_earlySessions;
    bool m_closed=false,m_preparing=false,m_creating=false,m_accepting=false,m_destroying=false;
    bool m_displayActive=false,m_displayCurrent=true,m_displayLoading=false;
};
}
