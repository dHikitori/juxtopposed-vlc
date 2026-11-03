// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_stream_lifetime.hpp"
#include <QObject>
#include <QPointer>

struct vlc_player_t;
namespace vlc::addons {
class AddonManager;
class AddonNativeSessionPlayer;
class AddonSessionHost;
class AddonSessionController;
class AddonSessionUi;

// The app owns this object and closes it before the native player is destroyed.
class AddonPlayerSessions final : public QObject {
    Q_OBJECT
public:
    AddonPlayerSessions(AddonManager*,vlc_player_t*,QObject* playerLifetime,
                        AddonStreamLifetime::Handle,QObject* parent=nullptr);
    ~AddonPlayerSessions() override;
    // Register host operations after construction. This grants no session.
    bool activate();
    bool active() const noexcept { return m_active && !m_closed; }
    AddonSessionUi* ui() const;
    void close();
private:
    friend class AddonPlayerSessionsTestAccess;
    QPointer<AddonManager> m_manager;
    QPointer<QObject> m_playerLifetime;
    AddonStreamLifetime::Handle m_lifetime;
    QPointer<AddonNativeSessionPlayer> m_player;
    QPointer<AddonSessionHost> m_host;
    QPointer<AddonSessionController> m_controller;
    QPointer<AddonSessionUi> m_ui;
    bool m_active=false,m_closed=false;
};
}
