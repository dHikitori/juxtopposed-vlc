// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
#include <memory>
#include <string>

namespace vlc::addons {
struct StoreAuthorityState;
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
    // Memory-only package withdrawal. Retain it through background cleanup.
    StorePackageRetirement retirePackage(const QString&) const;
    bool packageRetiring(const QString&) const noexcept;
private:
    std::shared_ptr<StoreAuthorityState> m_state;
};
// Directory normalization can read the filesystem. Use a retained handle on GUI.
StoreAuthorityToken captureStoreAuthority(const QString& directory);
}
