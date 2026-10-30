// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
#include <memory>
#include <string>
#include <functional>

namespace vlc::addons {
struct StoreAuthorityState;
struct StoreAuthorityCallback;
struct StoreAuthorityChangeCallback;
struct StoreAuthorityPeerState;
class StoreProcessFence;
class StoreAuthorityPeer final {
public:
    ~StoreAuthorityPeer();
    StoreAuthorityPeer(const StoreAuthorityPeer&) = delete;
    StoreAuthorityPeer& operator=(const StoreAuthorityPeer&) = delete;
    // Immediate memory-only admission closure. Each call also invalidates
    // older pending joins. Zero means the owner cannot admit more work.
    quint64 close() const noexcept;
    // Native weak handles. They do not retain a peer or a process fence.
    std::function<quint64()> closeHandle() const;
    std::function<bool()> pending(quint64 closedStamp) const;
    // Use joined background work. Drain waits for native cancellation and
    // releases the fence after callbacks and their local captures end.
    bool drain(quint64 closedStamp) const noexcept;
    // Supply only the canonical profile's checked shared fence after an exact
    // server Ready confirmation. This helper does not authenticate a socket
    // or validate a saved epoch. A stale drained stamp cannot reopen admission.
    bool admit(quint64 closedStamp, std::shared_ptr<const StoreProcessFence>) const noexcept;
private:
    friend class StoreAuthority;
    explicit StoreAuthorityPeer(std::shared_ptr<StoreAuthorityPeerState>);
    const std::shared_ptr<StoreAuthorityPeerState> m_state;
};
class StoreAuthorityChanges final {
public:
    StoreAuthorityChanges() = default;
    explicit operator bool() const noexcept;
private:
    friend class StoreAuthority;
    std::shared_ptr<StoreAuthorityChangeCallback> m_callback;
};
class StoreAuthorityWatch final
{
public:
    StoreAuthorityWatch() = default;
    // Retain the watch until the native lease has ended or been revoked.
    explicit operator bool() const noexcept;
private:
    friend class StoreAuthorityToken;
    std::shared_ptr<StoreAuthorityCallback> m_callback;
};
class StorePackageRetirement final
{
public:
    StorePackageRetirement() = default;
    StorePackageRetirement(StorePackageRetirement&&) noexcept;
    StorePackageRetirement(const StorePackageRetirement&) = delete;
    StorePackageRetirement& operator=(const StorePackageRetirement&) = delete;
    ~StorePackageRetirement();
    explicit operator bool() const noexcept { return bool(m_state); }
private:
    friend class StoreAuthority;
    StorePackageRetirement(std::shared_ptr<StoreAuthorityState>, std::string);
    std::shared_ptr<StoreAuthorityState> m_state;
    std::string m_package;
    bool m_counted = false;
};
class StoreAuthorityMutation final
{
public:
    StoreAuthorityMutation(StoreAuthorityMutation&&) noexcept;
    StoreAuthorityMutation(const StoreAuthorityMutation&) = delete;
    StoreAuthorityMutation& operator=(const StoreAuthorityMutation&) = delete;
    ~StoreAuthorityMutation();
private:
    friend class StoreAuthority;
    explicit StoreAuthorityMutation(std::shared_ptr<StoreAuthorityState>) noexcept;
    std::shared_ptr<StoreAuthorityState> m_state;
};
class StoreAuthorityToken final
{
public:
    StoreAuthorityToken() = default;
    // This check has no lock, file access, or QObject call.
    bool current() const noexcept;
    // The callback must use native state only. It must not throw, wait for Qt,
    // or reenter this store except to read current(). A refused registration calls
    // it before return. Keep the returned watch through the native lease.
    StoreAuthorityWatch onInvalidated(std::function<void()> nativeCancel) const noexcept;
    static constexpr unsigned WatchLimit = 256;
private:
    friend class StoreAuthority;
    std::shared_ptr<const StoreAuthorityState> m_state;
    quint64 m_revision = 0;
    quint64 m_peerStamp = 0;
};
class StoreAuthority final
{
public:
    explicit StoreAuthority(const QString& directory);
    StoreAuthorityToken capture() const noexcept;
    // Install once per canonical profile on joined startup work, before any
    // native consumer starts. Admission then remains closed until an admitted
    // peer owns its fence. Owner loss never restores the legacy ungated mode.
    // Destruction also drains; never destroy or drain from a Store callback.
    std::unique_ptr<StoreAuthorityPeer> attachPeer() const;
    // Hold the store lock before this gate. Keep it through the last write.
    StoreAuthorityMutation mutation() const noexcept;
    void invalidate() const noexcept;
    // True once a peer was attached to this profile in this process. From then
    // on, store writes need the profile coordinator's permit (Step 2.4).
    bool peerAttached() const noexcept;
    // Persistent display wake only. Callback uses native/std state, must not
    // block, throw, retain Qt values or reenter Store. No authority is granted.
    // Notifications may repeat. The native wake/caller coalesces them; a final
    // mutation notification follows writer drop.
    StoreAuthorityChanges onChanged(std::function<void()> nativeWake) const noexcept;
    static constexpr unsigned ChangeLimit = 64;
    // Call only after an owned store.lock is released. Display notification,
    // not an epoch advance or authority grant. No native cancellation changes.
    void displayUnlocked() const noexcept;
    // Memory-only package withdrawal. Retain it through background cleanup.
    StorePackageRetirement retirePackage(const QString&) const;
    bool packageRetiring(const QString&) const noexcept;
private:
    std::shared_ptr<StoreAuthorityState> m_state;
};
// Directory normalization can read the filesystem. Use a retained handle on GUI.
StoreAuthorityToken captureStoreAuthority(const QString& directory);
}
