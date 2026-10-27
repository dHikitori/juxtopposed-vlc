// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>
#include <functional>

namespace vlc::addons {
class AddonDiscoverySourceOwner;
// Trusted host UI only. Guest commands cannot access source approval actions.
class AddonSourceConsentUi final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool preparing READ preparing NOTIFY changed)
    Q_PROPERTY(bool acting READ acting NOTIFY changed)
    Q_PROPERTY(QVariantMap review READ review NOTIFY changed)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
public:
    struct Backend {
        QObject* lifetime=nullptr;
        std::function<bool()> active;
        std::function<QString(const QString&)> request;
        std::function<QVariantMap(const QString&)> read;
        std::function<bool(const QString&,bool approve)> change;
        std::function<void(const QString&)> discard;
    };
    explicit AddonSourceConsentUi(Backend,QObject* parent=nullptr);
    AddonSourceConsentUi(AddonDiscoverySourceOwner*,QObject* parent=nullptr);
    ~AddonSourceConsentUi() override;
    bool available() const { return m_available; }
    bool preparing() const { return m_preparing; }
    bool acting() const { return m_acting; }
    QVariantMap review() const { return m_review; }
    QString errorMessage() const { return m_error; }
    QString status() const { return m_status; }
    Q_INVOKABLE bool prepare(const QString& contribution);
    Q_INVOKABLE bool approve();
    Q_INVOKABLE bool revoke();
    Q_INVOKABLE void viewClosed();
    void refresh();
    void close();
    // Adapt native-owner signals on the shared Qt thread.
    void approvalReady(const QString& request);
    void actionFinished(const QString& request,bool success,const QString& error);
signals:
    void changed();
private:
    bool change(bool approve);
    bool fail(const QString&);
    quint64 clearReview();
    void publish();
    Backend m_backend;
    QPointer<QObject> m_owner;
    QTimer m_poll;
    QVariantMap m_review;
    QString m_request,m_source,m_error,m_status,m_earlyReady;
    struct Early { QString request,error; bool success=false; };
    Early m_earlyAction;
    quint64 m_generation=0;
    qint64 m_expires=0;
    bool m_closed=false,m_available=false,m_preparing=false,m_acting=false;
    bool m_creating=false,m_changing=false,m_approving=false,m_destroying=false;
};
}
