// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_stream_policy.hpp"
#include "addon_stream_lifetime.hpp"
#include "addon_stream_dns.hpp"
#include <QSslCertificate>
#include <QSslSocket>
#include <QVariantMap>
#include <atomic>
#include <functional>
#include <memory>

namespace vlc::addons {
class AddonStreamProxy final
{
public:
    enum class Format { Auto, Progressive, Hls, Dash };
    using Cancel = std::function<void()>;
    using Current = std::function<bool()>;
    using Started = std::function<void(bool)>;
    struct Spec {
        QUrl upstream;
        Format format = Format::Auto;
        // This predicate must read memory only. It must not call a QObject.
        Current live;
        // Check saved consent, configuration, and admission on a background task.
        Current authorize;
        AddonStreamLifetime::Handle lifetime;
    };
    struct Testing {
        // Explicit C++ fixtures only. Production has no endpoint override.
        std::function<QList<QHostAddress>(const QString&)> resolve;
        // Native/std fixture only. Do not capture a Qt value or callback.
        std::function<std::vector<AddonStreamDns::Address>(const std::string&)> nativeResolve;
        std::function<void(QSslSocket*, const AddonStreamPolicy::Plan&)> connect;
        QList<QSslCertificate> authorities;
        std::function<void()> afterReady, afterWrite;
        int idleMs = 15000;
    };
    using Ready = std::function<void(std::shared_ptr<AddonStreamProxy>, const QString&)>;
    // Call prepare/start on the app thread or within joined Qt work.
    static Cancel prepare(Spec, Ready);
    static Cancel prepareForTesting(Spec, Testing, Ready);
    ~AddonStreamProxy();
    QString uri() const;
    unsigned port() const;
    QByteArray pathPrefix() const;
    std::shared_ptr<std::atomic_bool> activeFlag() const;
    // The resolver calls this only after native policy/lease attachment.
    // The disk admission check runs outside the network event loop.
    void start(Current admission, Started);
    // Close native sockets before return. No GUI event is necessary.
    void cancel() noexcept;
    // One native/std cleanup hook. It runs once, outside the lifetime mutex.
    // Its callback and captured destructors must not use Qt.
    bool onCancelled(Cancel);
    bool waitStopped(int milliseconds) const;
    QVariantMap statistics() const;
    static unsigned liveSessions();
private:
    struct State;
    struct Worker;
    explicit AddonStreamProxy(std::shared_ptr<State>);
    static Cancel create(Spec, std::shared_ptr<Testing>, Ready);
    std::shared_ptr<State> m_state;
};
}
