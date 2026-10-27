// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_stream_lifetime.hpp"
#include "addon_stream_dns.hpp"
#include "addon_stream_policy.hpp"
#include <QByteArray>
#include <QList>
#include <QSet>
#include <QSslCertificate>
#include <QUrl>
#include <atomic>
#include <functional>
#include <memory>

class QSslSocket;
namespace vlc::addons {
// The guest has no access to this type or its test endpoint controls.
class AddonSessionWebSocket final {
public:
    using Current=std::function<bool()>;
    using Header=QPair<QByteArray,QByteArray>;
    struct Spec {
        QUrl url;
        QSet<QString> declaredOrigins,grantedOrigins;
        QStringList protocols;
        Current live; // Memory only. No QObject, Core, or disk call.
        Current authorize; // Runs in joined Qt work before a batch.
        std::function<QList<Header>()> credentials; // Trusted captured fields only.
        std::function<void()> nativeCancel; // Native exact-lease revoke only.
        AddonStreamLifetime::Handle lifetime;
    };
    enum class Kind { Ready,Text,Binary,Closed,Error };
    struct Event { Kind kind; QByteArray bytes; quint16 code=0; QString error; };
    // Events run on the joined network thread. Capture a held posting gate.
    using Events=std::function<void(Event)>;
    struct Testing {
        std::function<std::vector<AddonStreamDns::Address>(const std::string&)> resolve;
        std::function<void(QSslSocket*,const AddonStreamPolicy::Plan&)> connect;
        QList<QSslCertificate> authorities;
        int idleMs=30000;
    };
    static std::shared_ptr<AddonSessionWebSocket> open(Spec,Events);
    static std::shared_ptr<AddonSessionWebSocket> openForTesting(Spec,Testing,Events);
    ~AddonSessionWebSocket();
    bool send(bool text,const QByteArray&);
    // Native socket shutdown and lease revoke occur before return.
    void cancel() noexcept;
    bool active() const noexcept;
    bool waitStopped(int milliseconds) const;
    static bool permitted(const Spec&);
    static unsigned processCount() noexcept;
    static constexpr unsigned ProcessLimit=8,MaximumQueuedMessages=16;
    static constexpr qsizetype MaximumQueuedBytes=256*1024;
private:
    struct State;
    struct Worker;
    explicit AddonSessionWebSocket(std::shared_ptr<State>);
    static std::shared_ptr<AddonSessionWebSocket> create(Spec,std::shared_ptr<Testing>,Events);
    std::shared_ptr<State> m_state;
};
}
