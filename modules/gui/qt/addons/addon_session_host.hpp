// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_native_authority.hpp"
#include "addon_session_player.hpp"
#include "addon_session_websocket.hpp"
#include "addon_host_services.hpp"
#include "addon_stream_lifetime.hpp"
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>
#include <memory>

namespace vlc::addons {
// Host C++ only. Package views cannot approve, resume, or replace a session.
class AddonSessionHost final : public QObject {
    Q_OBJECT
public:
    struct AuthoritySource {
        QObject* lifetime=nullptr;
        // Call capture on the app thread. Run its returned reader in joined work.
        std::function<AddonNativeAuthorityReader(const QString&)> capture;
    };
    AddonSessionHost(SessionPlayerBackend,AuthoritySource,AddonStreamLifetime::Handle,QObject* parent=nullptr);
    ~AddonSessionHost() override;
    bool mount(AddonHostServices*);
    using TransportFactory=std::function<std::shared_ptr<AddonSessionWebSocket>(AddonSessionWebSocket::Spec,AddonSessionWebSocket::Events)>;
    bool setTransportFactoryForTesting(TransportFactory);
    QString prepareApproval(const QString& contribution,const QUrl& relay,const QString& roomLabel,
                            const QStringList& operations,const QStringList& protocols={});
    QVariantMap approval(const QString& ticket) const;
    // An empty operations list permits observation only. Each ticket has one use.
    QString approve(const QString& ticket,const QStringList& operations);
    void cancelApproval(const QString& ticket);
    bool resume(const QString& session);
    void end(const QString& session);
    void revokePackage(const QString& package);
    QVariantList sessions() const;
    bool current(const QString& session) const;
    QVariantMap invocationContext(const QString& session) const;
    bool checkForInvocation(const QString& session,std::function<void(bool)>);
    // C++ only. The deadline uses steady_clock microseconds from its epoch.
    bool bindInvocation(const QString& session,const QString& invocation,qint64 steadyDeadlineUs);
    bool releaseInvocation(const QString& session,const QString& invocation);
    void close();
signals:
    void approvalReady(const QString& ticket);
    void sessionChanged(const QString& session,const QString& status);
private:
    friend class AddonSessionHostTestAccess;
    struct State;
    struct Gate;
    struct Control;
    struct Approval;
    struct Session;
    struct Command;
    void prepared(const QString&,std::optional<AddonNativeAuthority>);
    void activated(const QString&,bool);
    void updateTimer();
    void tick();
    QVariantMap observation(const QString&) const;
    AddonHostServices::Cancel handle(const AddonHostServices::Context&,const QString&,const QJsonObject&,AddonHostServices::Completion);
    AddonHostServices::Cancel submit(const QString&,const QJsonObject&,AddonHostServices::Completion);
    void execute(const QString&);
    void admitted(const QString&,bool);
    void finishCommand(const QString&,const QString&,const QString& error={});
    void cancelCommand(const QString&);
    void transportEvents(const QString&);
    void finishReceive(const QString&);
    bool authorize(const std::shared_ptr<Session>&,std::function<void(bool)>,bool bindToInvocation=false);
    bool invocationCurrent(const std::shared_ptr<Session>&,const QString&) const;
    void checked(quint64,bool);
    SessionPlayerBackend m_player;
    AuthoritySource m_authorities;
    QPointer<QObject> m_authorityLifetime,m_playerLifetime;
    AddonStreamLifetime::Handle m_lifetime;
    std::shared_ptr<Gate> m_gate;
    std::unique_ptr<State> m_state;
    QTimer m_timer;
};
}
