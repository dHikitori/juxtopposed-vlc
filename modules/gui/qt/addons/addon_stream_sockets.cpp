// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#include "addon_stream_sockets.hpp"
#include <limits>

namespace vlc::addons {
namespace {
qintptr duplicate(qintptr descriptor)
{
#ifdef _WIN32
    WSAPROTOCOL_INFOW info{};
    if (WSADuplicateSocketW(SOCKET(descriptor),GetCurrentProcessId(),&info)) return -1;
    const auto copy = WSASocketW(FROM_PROTOCOL_INFO,FROM_PROTOCOL_INFO,FROM_PROTOCOL_INFO,&info,0,
        WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    return copy == INVALID_SOCKET ? qintptr(-1) : qintptr(copy);
#else
    return fcntl(int(descriptor),F_DUPFD_CLOEXEC,0);
#endif
}
void stop(qintptr socket) noexcept
{
#ifdef _WIN32
    shutdown(SOCKET(socket),SD_BOTH);
#else
    shutdown(int(socket),SHUT_RDWR);
#endif
}
void dispose(qintptr socket) noexcept
{
#ifdef _WIN32
    closesocket(SOCKET(socket));
#else
    close(int(socket));
#endif
}
}
AddonStreamSockets::~AddonStreamSockets()
{ cancel(); for (const auto& entry : m_sockets) dispose(entry.second); }
quint64 AddonStreamSockets::watch(qintptr socket)
{
    if (socket < 0) return 0;
    const auto copy = duplicate(socket); if (copy < 0) return 0;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_active.load(std::memory_order_acquire) || m_sockets.size() >= 32 || m_next == std::numeric_limits<quint64>::max()) {
        stop(copy); dispose(copy); return 0;
    }
    const auto id = ++m_next;
    try { m_sockets.emplace(id,copy); } catch (...) { stop(copy); dispose(copy); return 0; }
    return id;
}
void AddonStreamSockets::release(quint64 id)
{
    std::lock_guard<std::mutex> lock(m_mutex); const auto found = m_sockets.find(id);
    if (found != m_sockets.end()) { dispose(found->second); m_sockets.erase(found); }
}
void AddonStreamSockets::cancel() noexcept
{
    m_active.store(false,std::memory_order_release);
    std::lock_guard<std::mutex> lock(m_mutex); for (const auto& entry : m_sockets) stop(entry.second);
}
}
