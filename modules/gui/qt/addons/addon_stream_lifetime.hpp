// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <chrono>
#include <functional>
#include <memory>

namespace vlc::addons {
// This owner must outlive every Qt network owner that uses its handle.
class AddonStreamLifetime final
{
    struct State;
    struct Network;
public:
    using Work=std::function<void()>;
    using Cancel=std::function<void()>;
    class Handle;
    class NetworkTicket final
    {
    public:
        NetworkTicket()=default;
        explicit operator bool() const noexcept { return bool(m_network); }
        bool joined() const noexcept;
        bool waitJoined(std::chrono::milliseconds) const noexcept;
        // Retain one plain quota/resource owner through the full thread join.
        bool retain(std::shared_ptr<void>) const noexcept;
    private:
        friend class Handle;
        explicit NetworkTicket(std::shared_ptr<Network>);
        std::shared_ptr<Network> m_network;
    };
    class Handle final
    {
    public:
        Handle()=default;
        bool current() const noexcept;
        // Registered runs on the caller before the network thread starts.
        // Run must destroy all network objects before it returns.
        // Cancel must use native/std APIs only, outside the GUI thread.
        // Its captured values are destroyed on a counted cleanup thread.
        bool startNetwork(Work run,Cancel,std::function<void(NetworkTicket)> registered={}) const;
        // Join every Qt-using task before application teardown, including file I/O.
        bool startQtWork(Work run,Cancel,std::function<void(NetworkTicket)> registered={}) const;
        // Plain work must not use Qt, a GUI object, or a Core instance.
        // A blocked callback retains its process quota after close.
        bool startPlain(Work,std::shared_ptr<void> retainedNativeResource={}) const;
        void close() const noexcept;
        bool networkDrained() const noexcept;
        bool qtDrained() const noexcept;
        unsigned networkCount() const noexcept;
        unsigned qtWorkCount() const noexcept;
        unsigned plainCount() const noexcept;
        bool waitNetwork(std::chrono::milliseconds) const noexcept;
        bool waitQt(std::chrono::milliseconds) const noexcept;
    private:
        friend class AddonStreamLifetime;
        explicit Handle(std::shared_ptr<State>);
        bool start(Work,Cancel,std::function<void(NetworkTicket)>,bool network) const;
        std::shared_ptr<State> m_state;
    };
    AddonStreamLifetime();
    ~AddonStreamLifetime();
    AddonStreamLifetime(const AddonStreamLifetime&)=delete;
    AddonStreamLifetime& operator=(const AddonStreamLifetime&)=delete;
    Handle handle() const noexcept { return Handle(m_state); }
    void close() noexcept { handle().close(); }
    // Register the one application owner before it creates add-on services.
    // Unregister the same closed and joined owner before app destruction.
    static bool registerApplication(const Handle&) noexcept;
    static bool unregisterApplication(const Handle&) noexcept;
    static Handle applicationHandle() noexcept;
    static unsigned processNetworkCount() noexcept;
    static unsigned processPlainCount() noexcept;
    static unsigned processQtWorkCount() noexcept;
    static constexpr unsigned NetworkLimit=8,QtWorkLimit=40,PlainLimit=32;
private:
    std::shared_ptr<State> m_state;
};
}
