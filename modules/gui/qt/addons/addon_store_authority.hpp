// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
#include <memory>

namespace vlc::addons {
struct StoreAuthorityState;
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
private:
    friend class StoreAuthority;
    std::shared_ptr<const StoreAuthorityState> m_state;
    quint64 m_revision = 0;
};
class StoreAuthority final
{
public:
    explicit StoreAuthority(const QString& directory);
    StoreAuthorityToken capture() const noexcept;
    // Hold the store lock before this gate. Keep it through the last write.
    StoreAuthorityMutation mutation() const noexcept;
    void invalidate() const noexcept;
private:
    std::shared_ptr<StoreAuthorityState> m_state;
};
// Directory normalization can read the filesystem. Use a retained handle on GUI.
StoreAuthorityToken captureStoreAuthority(const QString& directory);
}
