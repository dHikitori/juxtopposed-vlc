// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_store_authority.hpp"
#include "addon_native_notifications.hpp"
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QStringList>
#include <atomic>
#include <limits>
#include <mutex>
#include <condition_variable>
#include <array>
#include <map>
#include <string_view>

namespace vlc::addons {
static_assert(StoreAuthority::ChangeLimit==NativeNotifications::Limit);
struct StoreAuthorityState {
    std::atomic<quint64> revision{0};
    std::atomic_uint writers{0};
    std::atomic_bool retired{false};
    struct PackageOrder {
        using is_transparent = void;
        bool operator()(std::string_view a, std::string_view b) const noexcept { return a < b; }
    };
    std::mutex packageMutex;
    std::map<std::string, unsigned, PackageOrder> packages;
    mutable std::mutex callbackMutex;
    mutable std::condition_variable callbacksEnded;
    mutable std::array<std::weak_ptr<StoreAuthorityCallback>, StoreAuthorityToken::WatchLimit> callbacks;
    mutable std::atomic_uint callbackCount{0};
    mutable unsigned callbackBatches = 0;
    NativeNotifications changes;
};
struct StoreAuthorityCallback {
    struct Quota {
        std::shared_ptr<const StoreAuthorityState> state;
        bool counted = false;
        ~Quota() { if (counted) state->callbackCount.fetch_sub(1, std::memory_order_release); }
    };
    StoreAuthorityCallback(std::shared_ptr<const StoreAuthorityState> owner, quint64 epoch, std::function<void()> callback)
        : quota{std::move(owner), false}, revision(epoch), cancel(std::move(callback)) {}
    void fire() noexcept {
        if (!fired.exchange(true, std::memory_order_acq_rel)) {
            try { cancel(); } catch (...) {}
        }
    }
    // Destroy the native callback capture before its retiring quota is released.
    Quota quota;
    const quint64 revision;
    const std::function<void()> cancel;
    std::atomic_bool fired{false};
};
struct StoreAuthorityChangeCallback {
    // The wrapper preserves the public opaque one-handle Store token. Release
    // the subscription/native capture before its final Store owner reference.
    std::shared_ptr<const StoreAuthorityState> owner;
    NativeNotification subscription;
};
namespace {
void notifyChanged(const std::shared_ptr<StoreAuthorityState>& state) noexcept
{ state->changes.notify(); }
void cancelNative(const std::function<void()>& cancel) noexcept
{ if (cancel) try { cancel(); } catch (...) {} }
void endCallbackBatch(const std::shared_ptr<const StoreAuthorityState>& state) noexcept
{
    std::unique_lock<std::mutex> lock(state->callbackMutex);
    --state->callbackBatches;
    state->callbacksEnded.notify_all();
    state->callbacksEnded.wait(lock, [&] { return state->callbackBatches == 0; });
}
void cancelRefused(const std::shared_ptr<const StoreAuthorityState>& state,
                   const std::function<void()>& cancel) noexcept
{
    { std::lock_guard<std::mutex> lock(state->callbackMutex); ++state->callbackBatches; }
    cancelNative(cancel);
    endCallbackBatch(state);
}
void advance(const std::shared_ptr<StoreAuthorityState>& state) noexcept
{
    std::array<std::shared_ptr<StoreAuthorityCallback>, StoreAuthorityToken::WatchLimit> pending;
    unsigned count = 0;
    {
        std::lock_guard<std::mutex> lock(state->callbackMutex);
        ++state->callbackBatches;
        const auto revision = state->revision.load(std::memory_order_relaxed);
        if (revision == std::numeric_limits<quint64>::max()) state->retired.store(true, std::memory_order_release);
        else state->revision.store(revision + 1, std::memory_order_release);
        for (auto& weak : state->callbacks) {
            if (auto callback = weak.lock()) pending[count++] = std::move(callback);
            weak.reset();
        }
    }
    // A second writer must also wait for callbacks taken by the first writer.
    // No callback, package, or registry lock covers native cancellation.
    for (unsigned index = 0; index < count; ++index) pending[index]->fire();
    endCallbackBatch(state);
    notifyChanged(state);
}
QString normalized(const QString& directory)
{
    QString path = QDir::cleanPath(QFileInfo(directory).absoluteFilePath());
    QStringList suffix;
    while (true) {
        const QFileInfo info(path);
        const auto canonical = info.canonicalFilePath();
        if (!canonical.isEmpty()) {
            path = canonical;
            for (const auto& part : suffix) path = QDir(path).filePath(part);
            break;
        }
        const auto parent = info.absolutePath();
        if (parent == path) break;
        suffix.prepend(info.fileName()); path = parent;
    }
#ifdef Q_OS_WIN
    path = path.toCaseFolded();
#endif
    return QDir::cleanPath(path);
}
struct Registry {
    std::mutex mutex;
    QHash<QString, std::weak_ptr<StoreAuthorityState>> stores;
};
Registry& registry() { static Registry value; return value; }
}
StoreAuthority::StoreAuthority(const QString& directory)
{
    const auto key = normalized(directory);
    auto& shared = registry(); std::lock_guard<std::mutex> lock(shared.mutex);
    for (auto it = shared.stores.begin(); it != shared.stores.end();)
        if (it->expired()) it = shared.stores.erase(it); else ++it;
    m_state = shared.stores.value(key).lock();
    if (!m_state) { m_state = std::make_shared<StoreAuthorityState>(); shared.stores.insert(key, m_state); }
}
StoreAuthorityToken StoreAuthority::capture() const noexcept
{
    StoreAuthorityToken token;
    if (m_state->writers.load(std::memory_order_acquire)) return token;
    const auto revision = m_state->revision.load(std::memory_order_acquire);
    if (m_state->writers.load(std::memory_order_acquire)
        || m_state->revision.load(std::memory_order_acquire) != revision) return token;
    token.m_state = m_state; token.m_revision = revision; return token;
}
bool StoreAuthorityToken::current() const noexcept
{
    return m_state && !m_state->retired.load(std::memory_order_acquire)
        && !m_state->writers.load(std::memory_order_acquire)
        && m_state->revision.load(std::memory_order_acquire) == m_revision
        && !m_state->writers.load(std::memory_order_acquire)
        && m_state->revision.load(std::memory_order_acquire) == m_revision;
}
StoreAuthorityWatch::operator bool() const noexcept
{
    if (!m_callback || m_callback->fired.load(std::memory_order_acquire)) return false;
    const auto& state = m_callback->quota.state;
    return !state->retired.load(std::memory_order_acquire)
        && !state->writers.load(std::memory_order_acquire)
        && state->revision.load(std::memory_order_acquire) == m_callback->revision
        && !state->writers.load(std::memory_order_acquire)
        && state->revision.load(std::memory_order_acquire) == m_callback->revision;
}
StoreAuthorityWatch StoreAuthorityToken::onInvalidated(std::function<void()> cancel) const noexcept
{
    if (!cancel) return {};
    if (!m_state) { cancelNative(cancel); return {}; }
    std::shared_ptr<StoreAuthorityCallback> callback;
    try { callback = std::make_shared<StoreAuthorityCallback>(m_state, m_revision, std::move(cancel)); }
    catch (...) { cancelRefused(m_state, cancel); return {}; }
    bool admitted = false;
    {
        std::lock_guard<std::mutex> lock(m_state->callbackMutex);
        if (current() && m_state->callbackCount.load(std::memory_order_acquire) < WatchLimit) {
            for (auto& slot : m_state->callbacks) if (slot.expired()) {
                callback->quota.counted = true;
                m_state->callbackCount.fetch_add(1, std::memory_order_release);
                slot = callback; admitted = true; break;
            }
        }
        if (!admitted) ++m_state->callbackBatches;
    }
    if (!admitted) { callback->fire(); endCallbackBatch(m_state); return {}; }
    StoreAuthorityWatch watch; watch.m_callback = std::move(callback); return watch;
}
void StoreAuthority::invalidate() const noexcept
{
    advance(m_state);
}
void StoreAuthority::displayUnlocked() const noexcept { notifyChanged(m_state); }
StoreAuthorityChanges::operator bool() const noexcept
{ return m_callback && !m_callback->owner->retired.load(std::memory_order_acquire); }
StoreAuthorityChanges StoreAuthority::onChanged(std::function<void()> nativeWake) const noexcept
{
    StoreAuthorityChanges result;
    try {
        if(!nativeWake || m_state->retired.load(std::memory_order_acquire))return result;
        auto subscription=m_state->changes.subscribe(std::move(nativeWake));
        if(!subscription)return result;
        auto callback=std::make_shared<StoreAuthorityChangeCallback>();
        callback->owner=m_state;callback->subscription=std::move(subscription);
        result.m_callback=std::move(callback);
    }catch(...){}
    return result;
}
StoreAuthorityToken captureStoreAuthority(const QString& directory)
{ return StoreAuthority(directory).capture(); }
StoreAuthorityMutation::StoreAuthorityMutation(std::shared_ptr<StoreAuthorityState> state) noexcept : m_state(std::move(state))
{ m_state->writers.fetch_add(1, std::memory_order_acq_rel); advance(m_state); }
StoreAuthorityMutation::StoreAuthorityMutation(StoreAuthorityMutation&& other) noexcept : m_state(std::move(other.m_state)) {}
StoreAuthorityMutation::~StoreAuthorityMutation()
{ if (m_state) {
    advance(m_state);
    m_state->writers.fetch_sub(1, std::memory_order_release);
    notifyChanged(m_state);
} }
StoreAuthorityMutation StoreAuthority::mutation() const noexcept
{ return StoreAuthorityMutation(m_state); }
namespace {
bool packageKey(const QString& id, std::array<char, 128>& bytes, size_t* length) noexcept
{
    if (id.size() < 3 || id.size() > qsizetype(bytes.size())) return false;
    bool separator = false, previousSeparator = true;
    for (qsizetype i = 0; i < id.size(); ++i) {
        const auto ch = id.at(i).unicode();
        const bool split = ch == '.' || ch == '-';
        if ((i == 0 && (ch < 'a' || ch > 'z'))
            || (!split && !(ch >= 'a' && ch <= 'z') && !(ch >= '0' && ch <= '9'))
            || (split && previousSeparator)) return false;
        bytes[size_t(i)] = char(ch);
        previousSeparator = split; separator |= split;
    }
    *length = size_t(id.size());
    return separator && !previousSeparator;
}
}
StorePackageRetirement::StorePackageRetirement(std::shared_ptr<StoreAuthorityState> state, std::string package)
    : m_state(std::move(state)), m_package(std::move(package))
{
    {
        std::lock_guard<std::mutex> lock(m_state->packageMutex);
        auto& count = m_state->packages[m_package];
        if (count == std::numeric_limits<unsigned>::max()) m_state->retired.store(true, std::memory_order_release);
        else { ++count; m_counted = true; }
    }
    advance(m_state);
}
StorePackageRetirement::StorePackageRetirement(StorePackageRetirement&& other) noexcept
    : m_state(std::move(other.m_state)), m_package(std::move(other.m_package)), m_counted(other.m_counted) {}
StorePackageRetirement::~StorePackageRetirement()
{
    if (!m_state || !m_counted) return;
    {
        std::lock_guard<std::mutex> lock(m_state->packageMutex);
        const auto found = m_state->packages.find(m_package);
        if (found != m_state->packages.end() && --found->second == 0) m_state->packages.erase(found);
    }
    advance(m_state);
}
StorePackageRetirement StoreAuthority::retirePackage(const QString& id) const
{
    std::array<char, 128> bytes; size_t length = 0;
    if (!packageKey(id, bytes, &length)) return {};
    return StorePackageRetirement(m_state, std::string(bytes.data(), length));
}
bool StoreAuthority::packageRetiring(const QString& id) const noexcept
{
    std::array<char, 128> bytes; size_t length = 0;
    if (!packageKey(id, bytes, &length)) return true;
    std::lock_guard<std::mutex> lock(m_state->packageMutex);
    return m_state->packages.find(std::string_view(bytes.data(), length)) != m_state->packages.end();
}
}
