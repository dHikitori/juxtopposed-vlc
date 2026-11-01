// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QPointer>
#include <memory>

namespace vlc::addons {
class AddonManager;
class DeviceAuthorization;

// The app's account sign-in view model for one settings field. It shows
// only the code and the page the sign-in service names; tokens go straight
// to the manager's credential store. Nothing runs until start(), and
// polling stops on cancel, failure, success or destruction.
class AddonAccountSetup : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject* manager READ manager WRITE setManager NOTIFY changed FINAL)
    Q_PROPERTY(QString packageId MEMBER m_packageId NOTIFY changed FINAL)
    Q_PROPERTY(QString field MEMBER m_field NOTIFY changed FINAL)
    Q_PROPERTY(bool sessionOnly MEMBER m_sessionOnly NOTIFY changed FINAL)
    // idle, requesting, waiting, connected, failed or canceled.
    Q_PROPERTY(QString state READ state NOTIFY changed FINAL)
    Q_PROPERTY(QString userCode READ userCode NOTIFY changed FINAL)
    Q_PROPERTY(QString verificationHost READ verificationHost NOTIFY changed FINAL)
    Q_PROPERTY(QString message READ message NOTIFY changed FINAL)
    Q_PROPERTY(int remainingSeconds READ remainingSeconds NOTIFY changed FINAL)
public:
    explicit AddonAccountSetup(QObject* parent = nullptr);
    ~AddonAccountSetup() override;
    QObject* manager() const;
    void setManager(QObject*);
    QString state() const;
    QString userCode() const;
    QString verificationHost() const;
    QString message() const;
    int remainingSeconds() const;
    Q_INVOKABLE bool start();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool openPage();
    Q_INVOKABLE void copyCode();
signals:
    void changed();
    void connected();
private:
    void saveFinished(const QString& request, bool success, const QString& error);
    QPointer<AddonManager> m_manager;
    // The manager saves the account on background work.
    QString m_packageId, m_field, m_message, m_saveRequest;
    bool m_sessionOnly = false;
    bool m_saved = false;
    std::unique_ptr<DeviceAuthorization> m_flow;
};
}
