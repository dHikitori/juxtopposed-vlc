// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <functional>
#include <memory>
namespace vlc::addons {
struct NativeNotificationState;
struct NativeNotificationCallback;
// Native display/scheduling notices only. This token grants no authority and
// its destruction does not join a callback already retained by notify().
class NativeNotification final {
public:
    NativeNotification()=default;
    explicit operator bool() const noexcept { return bool(m_callback); }
private:
    friend class NativeNotifications;
    std::shared_ptr<NativeNotificationCallback> m_callback;
};
class NativeNotifications final {
public:
    NativeNotifications() noexcept;
    // The callback must use native/std state only: no QObject, Qt value, file
    // reader, GUI wait or publisher reentry. Calls may overlap; captures must
    // be thread-safe and own any OS handle.
    NativeNotification subscribe(std::function<void()> nativeWake) const noexcept;
    void notify() const noexcept;
    static constexpr unsigned Limit=64;
private:
    std::shared_ptr<NativeNotificationState> m_state;
};
}
