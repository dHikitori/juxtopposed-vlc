// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Linux link wrapping holds real saved-state readers before their first I/O.
// The app has no runtime reader setter or test-only preparation backend.
#ifdef HAKONE_METADATA_PREPARATION_WRAP
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "addons/addon_manager.hpp"
namespace metadataPreparationTest {
struct Probe {
    const std::thread::id owner = std::this_thread::get_id();
    std::atomic_bool entered{false}, onOwner{false};
    std::mutex mutex;
    std::condition_variable changed;
    bool released = false;
    void wait() {
        onOwner.store(std::this_thread::get_id() == owner);
        entered.store(true);
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait(lock, [this] { return released; });
    }
    void release() { { std::lock_guard<std::mutex> lock(mutex); released = true; } changed.notify_all(); }
};
inline std::shared_ptr<Probe> catalogue, authority;
}
extern "C" vlc::addons::MetadataCatalogueReader
__real__ZNK3vlc6addons12PackageStore23metadataCatalogueReaderEv(const vlc::addons::PackageStore*);
extern "C" vlc::addons::MetadataCatalogueReader
__wrap__ZNK3vlc6addons12PackageStore23metadataCatalogueReaderEv(const vlc::addons::PackageStore* store)
{
    auto reader = __real__ZNK3vlc6addons12PackageStore23metadataCatalogueReaderEv(store);
    const auto probe = metadataPreparationTest::catalogue;
    if (!probe) return reader;
    return [reader = std::move(reader), probe](auto* result, auto* error) {
        probe->wait(); return reader(result, error);
    };
}
extern "C" vlc::addons::AddonNativeAuthorityReader
__real__ZNK3vlc6addons12AddonManager21nativeAuthorityReaderERK7QString(const vlc::addons::AddonManager*, const QString&);
extern "C" vlc::addons::AddonNativeAuthorityReader
__wrap__ZNK3vlc6addons12AddonManager21nativeAuthorityReaderERK7QString(const vlc::addons::AddonManager* manager, const QString& id)
{
    auto reader = __real__ZNK3vlc6addons12AddonManager21nativeAuthorityReaderERK7QString(manager, id);
    const auto probe = metadataPreparationTest::authority;
    if (!probe) return reader;
    return [reader = std::move(reader), probe]() {
        probe->wait(); return reader();
    };
}
#endif
