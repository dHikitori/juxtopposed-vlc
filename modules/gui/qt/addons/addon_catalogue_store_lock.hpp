// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_store_authority.hpp"
#include <QLockFile>
#include <utility>
namespace vlc::addons {
class CatalogueStoreLock final : public QLockFile {
public:
    CatalogueStoreLock(QString path,StoreAuthority authority)
        :QLockFile(std::move(path)),m_authority(std::move(authority)) {}
    ~CatalogueStoreLock() {
        if(isLocked()) {
            unlock();
            m_authority.displayUnlocked();
        }
    }
private:
    StoreAuthority m_authority;
};
}
