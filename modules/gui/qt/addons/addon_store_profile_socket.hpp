// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_store_profile_protocol.hpp"
#include <QObject>
#include <QLocalSocket>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <functional>
#include <memory>
#include <optional>

namespace vlc::addons {
class StoreProfileEndpoint final {
public:
    // Filesystem work. The profile must exist. The first call creates an
    // owner-only endpoint secret in the profile, so other users cannot
    // predict the name. Unix uses a private directory: the given one, else
    // $XDG_RUNTIME_DIR/hakone, the macOS user temporary directory, then a
    // secret-derived /tmp directory. Windows uses a local named pipe only.
    // Processes with different runtime environments do not find each other;
    // their peers then stay unavailable and do not open admission.
    static std::optional<StoreProfileEndpoint> forProfile(const QString& directory, const QString& unixRuntime = {});
    const QString& address() const noexcept { return m_address; }
private:
    explicit StoreProfileEndpoint(QString address) : m_address(std::move(address)) {}
    const QString m_address;
};
class StoreProfileListener;
// Use only on an independent joined control thread with an event loop. This
// class never waits for connection/data delivery and never starts a process.
class StoreProfileSocket final : public QObject {
public:
    using Message = StoreProfileProtocol::Message;
    struct Callbacks {
        // Mandatory native-only closure. It must not throw, wait, call Qt or
        // reenter this socket. Destruction calls it too, before closing I/O.
        std::function<void()> revoke;
        // Control callbacks can stop or destroy this socket. They must not
        // wait for GUI, store locks or native cancellation.
        std::function<void()> opened;
        std::function<void(const Message&)> received;
        std::function<void()> lost;
    };
    static constexpr qint64 MaximumQueuedBytes = StoreProfileProtocol::MaximumInput;
    StoreProfileSocket();
    ~StoreProfileSocket() override;
    bool connectTo(const StoreProfileEndpoint&, Callbacks);
    bool startAccepted(Callbacks);
    bool send(const Message&);
    void stop();
    bool ready() const noexcept { return m_ready && !m_closed; }
private:
    friend class StoreProfileListener;
    static std::unique_ptr<StoreProfileSocket> accept(qintptr);
    bool setCallbacks(Callbacks);
    void opened();
    void readReady();
    void scheduleRead();
    void closeAdmission();
    void ioLost();
    void finish(bool notify);
    QLocalSocket m_socket;
    QTimer m_deadline;
    StoreProfileProtocol::Decoder m_decoder;
    Callbacks m_callbacks;
    bool m_started = false, m_accepted = false, m_ready = false, m_closed = false, m_readQueued = false;
    bool m_ioClosed = false, m_lossDelivered = false;
};
// The caller must hold the profile coordinator owner lock before listening.
// No stale endpoint is removed here. Unix stale-path removal belongs to that
// locked owner. Closing stops accepted sockets, including retained wrappers.
class StoreProfileListener final : public QObject {
public:
    using Accepted = std::function<void(std::unique_ptr<StoreProfileSocket>)>;
    explicit StoreProfileListener(Accepted);
    ~StoreProfileListener() override;
    bool listen(const StoreProfileEndpoint&);
    void stop();
    bool listening() const noexcept { return m_listening; }
    static constexpr qsizetype MaximumConnections = StoreProfileArbiter::MaximumPeers;
private:
    struct Native;
    friend struct Native;
    void queue(qintptr);
    void deliver(qintptr);
    void close(bool notify);
    const Accepted m_accepted;
    std::unique_ptr<Native> m_native;
    QSet<StoreProfileSocket*> m_sockets;
    qsizetype m_pending = 0;
    quint64 m_serial = 0;
    bool m_listening = false;
};
}
