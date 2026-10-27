// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_stream_lifetime.hpp"
#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <vector>

class QCoreApplication;
namespace vlc::addons {
// Construct on the app thread, after QCoreApplication, as a separate owner.
// Destroy this owner before the application. Do not parent it to the app.
class AddonStreamShutdown final : public QObject
{
public:
    enum class Result { Drained,TimedOut };
    using Complete=std::function<void(Result)>;
    explicit AddonStreamShutdown(QCoreApplication&);
    ~AddonStreamShutdown() override;
    AddonStreamLifetime::Handle handle() const noexcept;
    // Normal quit: call app.exit only from a Drained callback.
    // A timeout leaves the app alive. It does not terminate the process.
    bool request(Complete,int timeoutMs=5000);
    // Call on every exit/error path before QCoreApplication destruction.
    // This includes all Qt file/admission work, not only networking.
    // Failure requires a host decision that preserves Qt until drain succeeds.
    // This final fallback waits at most 5000 ms. Normal quit does not wait here.
    [[nodiscard]] bool finalGuard(int timeoutMs=0) noexcept;
private:
    void poll();
    AddonStreamLifetime m_lifetime;
    QTimer m_timer;
    QElapsedTimer m_clock;
    std::vector<Complete> m_callbacks;
    qint64 m_deadline=0;
    bool m_valid=false,m_running=false,m_destroying=false;
};
}
