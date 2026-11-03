// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_session_host.hpp"
#include <QHash>
#include <QPointer>
#include <QTimer>

namespace vlc::addons {
// Only this owner selects the session.controller run entry point.
class AddonSessionController final : public QObject {
public:
    struct Backend {
        QObject* lifetime=nullptr;
        std::function<QString(const QString&,const QString&,const QVariantMap&)> invoke;
        std::function<void(const QString&)> cancel;
    };
    AddonSessionController(AddonSessionHost*,Backend,QObject* parent=nullptr);
    ~AddonSessionController() override;
    void completed(const QString& request,const QString& contribution,const QString& status,const QVariantMap&,const QString& error);
    void close();
private:
    struct Loop { QString contribution,request; QPointer<QTimer> timer,deadline; qint64 expires=0; bool checking=false; };
    struct Early { QString contribution,status,error; QVariantMap data; };
    void changed(const QString&,const QString&);
    void begin(const QString&);
    void checked(const QString&,bool);
    void stop(const QString&,bool end);
    QPointer<AddonSessionHost> m_host;
    QPointer<QObject> m_lifetime;
    Backend m_backend;
    QHash<QString,Loop> m_loops;
    QHash<QString,QString> m_requests;
    QHash<QString,Early> m_early;
    QString m_creating;
    bool m_closed=false;
};
}
