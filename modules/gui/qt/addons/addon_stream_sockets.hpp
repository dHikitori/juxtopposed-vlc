// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QtGlobal>
#include <atomic>
#include <map>
#include <mutex>

namespace vlc::addons {
// Duplicates keep socket identity stable during native cancellation.
class AddonStreamSockets final
{
public:
    ~AddonStreamSockets();
    quint64 watch(qintptr);
    void release(quint64);
    void cancel() noexcept;
    bool active() const noexcept { return m_active.load(std::memory_order_acquire); }
private:
    std::mutex m_mutex;
    std::map<quint64,qintptr> m_sockets;
    std::atomic_bool m_active{true};
    quint64 m_next = 0;
};
}
