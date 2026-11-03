// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_session_player.hpp"
#include <memory>
struct vlc_player_t;
struct vlc_addon_player_listener;

namespace vlc::addons {
// Close this owner before native player destruction. It starts no worker.
class AddonNativeSessionPlayer final : public QObject {
public:
    explicit AddonNativeSessionPlayer(vlc_player_t*,QObject* parent=nullptr);
    ~AddonNativeSessionPlayer() override;
    SessionPlayerBackend backend();
    void close();
    bool available() const noexcept;
private:
    struct Mailbox;
    struct Native;
    SessionPlayerSnapshot snapshot();
    QList<SessionPlayerCompletion> completions();
    vlc_addon_player_listener* m_listener=nullptr;
    std::shared_ptr<Native> m_native;
    std::unique_ptr<Mailbox> m_mailbox;
};
}
