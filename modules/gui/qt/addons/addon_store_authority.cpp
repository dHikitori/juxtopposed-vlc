// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_store_authority.hpp"
#include "addon_native_notifications.hpp"
#include "addon_store_process_fence.hpp"
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
    // Zero is the existing ungated mode. An odd stamp closes admission.
    // A later even stamp admits a confirmed peer. Never return to zero.
    std::atomic<quint64> peerStamp{0};
    std::mutex peerMutex;
    std::weak_ptr<StoreAuthorityPeerState> peer;
    struct PackageOrder {
        using is_transparent = void;
        bool operator()(std::string_view a, std::string_view b) const noexcept { return a < b; }
    };
    mutable std::mutex packageMutex;
    std::map<std::string, unsigned, PackageOrder> packages;
    // Per package write revisions of this process; allPackages counts
    // store-wide changes. Both only grow.
    std::map<std::string, quint64, PackageOrder> packageRevisions;
    std::atomic<quint64> allPackages{0};
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
    StoreAuthorityCallback(std::shared_ptr<const StoreAuthorityState> owner, quint64 epoch, quint64 peer, std::function<void()> callback)
        : quota{std::move(owner), false}, revision(epoch), peerStamp(peer), cancel(std::move(callback)) {}
    void fire() noexcept {
        if (!fired.exchange(true, std::memory_order_acq_rel)) {
            try { cancel(); } catch (...) {}
        }
    }
    // Destroy the native callback capture before its retiring quota is released.
    Quota quota;
    const quint64 revision;
    const quint64 peerStamp;
    const std::function<void()> cancel;
    std::atomic_bool fired{false};
};
struct StoreAuthorityChangeCallback {
    // The wrapper preserves the public opaque one-handle Store token. Release
    // the subscription/native capture before its final Store owner reference.
    std::shared_ptr<const StoreAuthorityState> owner;
    NativeNotification subscription;
};
struct StoreAuthorityPeerState {
    const std::shared_ptr<StoreAuthorityState> authority;
    std::atomic_bool open{true};
    // Only drain, admit and destruction use this mutex. No Store callback
    // runs under peerMutex or the process registry lock.
    std::mutex work;
    std::shared_ptr<const StoreProcessFence> fence;
    quint64 drained = 0;
    explicit StoreAuthorityPeerState(std::shared_ptr<StoreAuthorityState> state) : authority(std::move(state)) {}
};
namespace {
bool peerAdmits(quint64 stamp) noexcept { return (stamp & 1) == 0; }
quint64 closePeerAuthority(const std::shared_ptr<StoreAuthorityState>& state) noexcept
{
    auto stamp = state->peerStamp.load(std::memory_order_acquire);
    for (;;) {
        if (stamp >= std::numeric_limits<quint64>::max() - 2) {
            state->retired.store(true, std::memory_order_release); return 0;
        }
        const auto next = stamp + (peerAdmits(stamp) ? 1 : 2);
        if (state->peerStamp.compare_exchange_weak(stamp, next, std::memory_order_acq_rel, std::memory_order_acquire)) return next;
    }
}
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
StoreAuthorityPeer::StoreAuthorityPeer(std::shared_ptr<StoreAuthorityPeerState> state) : m_state(std::move(state)) {}
std::unique_ptr<StoreAuthorityPeer> StoreAuthority::attachPeer() const
{
    std::lock_guard<std::mutex> lock(m_state->peerMutex);
    if (!m_state->peer.expired() || m_state->retired.load(std::memory_order_acquire)) return {};
    auto state = std::make_shared<StoreAuthorityPeerState>(m_state);
    if (!closePeerAuthority(m_state)) return {};
    m_state->peer = state;
    return std::unique_ptr<StoreAuthorityPeer>(new StoreAuthorityPeer(std::move(state)));
}
StoreAuthorityPeer::~StoreAuthorityPeer()
{
    const auto state = m_state;
    state->open.store(false, std::memory_order_release);
    closePeerAuthority(state->authority);
    std::lock_guard<std::mutex> lock(state->work);
    advance(state->authority);
    state->fence.reset();
}
quint64 StoreAuthorityPeer::close() const noexcept
{
    const auto state = m_state;
    return state->open.load(std::memory_order_acquire) ? closePeerAuthority(state->authority) : 0;
}
std::function<quint64()> StoreAuthorityPeer::closeHandle() const
{
    return [weak = std::weak_ptr<StoreAuthorityPeerState>(m_state)] {
        const auto state = weak.lock();
        return state && state->open.load(std::memory_order_acquire) ? closePeerAuthority(state->authority) : quint64(0);
    };
}
std::function<bool()> StoreAuthorityPeer::pending(quint64 stamp) const
{
    return [weak = std::weak_ptr<StoreAuthorityPeerState>(m_state), stamp] {
        const auto state = weak.lock();
        return state && (stamp & 1) && state->open.load(std::memory_order_acquire)
            && !state->authority->retired.load(std::memory_order_acquire)
            && state->authority->peerStamp.load(std::memory_order_acquire) == stamp;
    };
}
bool StoreAuthorityPeer::drain(quint64 stamp) const noexcept
{
    const auto state = m_state;
    std::lock_guard<std::mutex> lock(state->work);
    if (!(stamp & 1) || !state->open.load(std::memory_order_acquire)
        || state->authority->peerStamp.load(std::memory_order_acquire) != stamp) return false;
    advance(state->authority);
    state->fence.reset();
    if (!state->open.load(std::memory_order_acquire) || state->authority->peerStamp.load(std::memory_order_acquire) != stamp) return false;
    state->drained = stamp; return true;
}
bool StoreAuthorityPeer::admit(quint64 stamp, std::shared_ptr<const StoreProcessFence> fence) const noexcept
{
    const auto state = m_state;
    std::lock_guard<std::mutex> lock(state->work);
    if (!fence || !(stamp & 1) || stamp == std::numeric_limits<quint64>::max()
        || !state->open.load(std::memory_order_acquire) || state->drained != stamp || state->fence
        || state->authority->retired.load(std::memory_order_acquire) || state->authority->writers.load(std::memory_order_acquire)) return false;
    state->fence = std::move(fence);
    auto expected = stamp;
    if (!state->authority->peerStamp.compare_exchange_strong(expected, stamp + 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
        state->fence.reset(); return false;
    }
    return state->open.load(std::memory_order_acquire)
        && state->authority->peerStamp.load(std::memory_order_acquire) == stamp + 1;
}
StoreAuthorityToken StoreAuthority::capture() const noexcept
{
    StoreAuthorityToken token;
    const auto peer = m_state->peerStamp.load(std::memory_order_acquire);
    if (!peerAdmits(peer) || m_state->writers.load(std::memory_order_acquire)) return token;
    const auto revision = m_state->revision.load(std::memory_order_acquire);
    if (m_state->writers.load(std::memory_order_acquire)
        || m_state->revision.load(std::memory_order_acquire) != revision
        || m_state->peerStamp.load(std::memory_order_acquire) != peer) return token;
    token.m_state = m_state; token.m_revision = revision; token.m_peerStamp = peer; return token;
}
bool StoreAuthorityToken::current() const noexcept
{
    return m_state && !m_state->retired.load(std::memory_order_acquire)
        && peerAdmits(m_peerStamp) && m_state->peerStamp.load(std::memory_order_acquire) == m_peerStamp
        && !m_state->writers.load(std::memory_order_acquire)
        && m_state->revision.load(std::memory_order_acquire) == m_revision
        && !m_state->writers.load(std::memory_order_acquire)
        && m_state->revision.load(std::memory_order_acquire) == m_revision
        && m_state->peerStamp.load(std::memory_order_acquire) == m_peerStamp;
}
StoreAuthorityWatch::operator bool() const noexcept
{
    if (!m_callback || m_callback->fired.load(std::memory_order_acquire)) return false;
    const auto& state = m_callback->quota.state;
    return !state->retired.load(std::memory_order_acquire)
        && peerAdmits(m_callback->peerStamp) && state->peerStamp.load(std::memory_order_acquire) == m_callback->peerStamp
        && !state->writers.load(std::memory_order_acquire)
        && state->revision.load(std::memory_order_acquire) == m_callback->revision
        && !state->writers.load(std::memory_order_acquire)
        && state->revision.load(std::memory_order_acquire) == m_callback->revision
        && state->peerStamp.load(std::memory_order_acquire) == m_callback->peerStamp;
}
StoreAuthorityWatch StoreAuthorityToken::onInvalidated(std::function<void()> cancel) const noexcept
{
    if (!cancel) return {};
    if (!m_state) { cancelNative(cancel); return {}; }
    std::shared_ptr<StoreAuthorityCallback> callback;
    try { callback = std::make_shared<StoreAuthorityCallback>(m_state, m_revision, m_peerStamp, std::move(cancel)); }
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
bool StoreAuthority::peerAttached() const noexcept
{
    return m_state->peerStamp.load(std::memory_order_acquire) != 0;
}
void StoreAuthority::invalidate() const noexcept
{
    if (m_state->peerStamp.load(std::memory_order_acquire)) closePeerAuthority(m_state);
    advance(m_state);
}
void StoreAuthority::displayUnlocked() const noexcept { notifyChanged(m_state); }
StorePackageStamp StoreAuthority::packageStamp(const QString& packageId) const
{
    StorePackageStamp stamp;
    stamp.m_state = m_state; stamp.m_package = packageId.toStdString();
    stamp.m_peerStamp = m_state->peerStamp.load(std::memory_order_acquire);
    stamp.m_all = m_state->allPackages.load(std::memory_order_acquire);
    std::lock_guard<std::mutex> lock(m_state->packageMutex);
    const auto found = m_state->packageRevisions.find(stamp.m_package);
    stamp.m_revision = found == m_state->packageRevisions.end() ? 0 : found->second;
    return stamp;
}
void StoreAuthority::packageChanged(const QString& packageId) const noexcept
{
    if (packageId.isEmpty()) { m_state->allPackages.fetch_add(1, std::memory_order_acq_rel); return; }
    try {
        std::lock_guard<std::mutex> lock(m_state->packageMutex);
        ++m_state->packageRevisions[packageId.toStdString()];
    } catch (...) {
        // Without a package entry, end every stamp instead.
        m_state->allPackages.fetch_add(1, std::memory_order_acq_rel);
    }
}
bool StorePackageStamp::current() const noexcept
{
    if (!m_state || m_state->retired.load(std::memory_order_acquire)
        || m_state->allPackages.load(std::memory_order_acquire) != m_all) return false;
    std::lock_guard<std::mutex> lock(m_state->packageMutex);
    const auto found = m_state->packageRevisions.find(m_package);
    return (found == m_state->packageRevisions.end() ? 0 : found->second) == m_revision;
}
bool StorePackageStamp::peerCurrent() const noexcept
{ return m_state && m_state->peerStamp.load(std::memory_order_acquire) == m_peerStamp; }
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
{
    m_state->writers.fetch_add(1, std::memory_order_acq_rel);
    if (m_state->peerStamp.load(std::memory_order_acquire)) closePeerAuthority(m_state);
    advance(m_state);
}
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
