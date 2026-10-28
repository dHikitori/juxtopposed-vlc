// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_stream_shutdown.hpp"
#include <QObject>
#include <QTimer>
#include <functional>
#include <vector>

namespace vlc::addons {
// Keep this owner on the app thread, between app construction and destruction.
class AddonAppShutdown final : public QObject
{
public:
    using Action=std::function<void()>;
    // userQuitting is optional, app-thread only, once per real user quit.
    // Admission is closed before it runs. It may request playback Stop;
    // retirement and Core quit stay behind its return and the joined barrier.
    struct Hooks { Action retiring,joined,timedOut; Action userQuitting{}; };
    struct Options { int timeoutMs=5000,retryMs=100; };
    explicit AddonAppShutdown(QCoreApplication&);
    AddonAppShutdown(QCoreApplication&,Hooks,Options);
    ~AddonAppShutdown() override;
    AddonStreamLifetime::Handle handle() const noexcept { return m_stream.handle(); }
    // Close native posting gates in retiring. Destroy their Qt payloads in joined.
    bool setHooks(Hooks);
    bool setUserQuit(Action);
    bool requestRetirement(Action afterJoined);
    // Core teardown has one reserved slot, separate from user callbacks.
    bool requestNativeClose(Action afterJoined);
    void requestUserQuit();
    void requestExit(int code=0);
    // A direct exit can end exec before work joins. Reenter exec after this call.
    void recoverEventLoopExit();
    [[nodiscard]] bool canDestroyApp() noexcept;
    bool retiring() const noexcept { return m_retiring; }
    bool joined() const noexcept { return m_joined; }
private:
    bool onAppThread() const;
    void begin();
    void waitForWork();
    void finish();
    void scheduleActions();
    bool deliverReadyActions();
    QCoreApplication& m_app;
    AddonStreamShutdown m_stream;
    QTimer m_retry;
    Hooks m_hooks;
    Options m_options;
    Action m_userQuit;
    Action m_nativeClose;
    std::vector<Action> m_ready;
    bool m_valid=false,m_retiring=false,m_joined=false,m_waiting=false;
    bool m_finishing=false,m_retiringHook=false,m_failed=false,m_destroying=false,m_reported=false;
    bool m_userRequested=false,m_userQueued=false,m_userDelivered=false,m_userQuittingHook=false,m_deliveringReady=false;
    bool m_exitRequested=false,m_exitQueued=false;
    int m_exitCode=0;
};
}
