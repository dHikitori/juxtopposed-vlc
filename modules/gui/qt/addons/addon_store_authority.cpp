// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_store_authority.hpp"
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QStringList>
#include <atomic>
#include <limits>
#include <mutex>

namespace vlc::addons {
struct StoreAuthorityState {
    std::atomic<quint64> revision{0};
    std::atomic_uint writers{0};
    std::atomic_bool retired{false};
};
namespace {
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
void StoreAuthority::invalidate() const noexcept
{
    auto revision = m_state->revision.load(std::memory_order_relaxed);
    while (revision != std::numeric_limits<quint64>::max())
        if (m_state->revision.compare_exchange_weak(revision, revision + 1, std::memory_order_release, std::memory_order_relaxed)) return;
    m_state->retired.store(true, std::memory_order_release);
}
StoreAuthorityToken captureStoreAuthority(const QString& directory)
{ return StoreAuthority(directory).capture(); }
namespace {
void advance(const std::shared_ptr<StoreAuthorityState>& state) noexcept
{
    auto revision = state->revision.load(std::memory_order_relaxed);
    while (revision != std::numeric_limits<quint64>::max())
        if (state->revision.compare_exchange_weak(revision, revision + 1, std::memory_order_release, std::memory_order_relaxed)) return;
    state->retired.store(true, std::memory_order_release);
}
}
StoreAuthorityMutation::StoreAuthorityMutation(std::shared_ptr<StoreAuthorityState> state) noexcept : m_state(std::move(state))
{ m_state->writers.fetch_add(1, std::memory_order_acq_rel); advance(m_state); }
StoreAuthorityMutation::StoreAuthorityMutation(StoreAuthorityMutation&& other) noexcept : m_state(std::move(other.m_state)) {}
StoreAuthorityMutation::~StoreAuthorityMutation()
{ if (m_state) { advance(m_state); m_state->writers.fetch_sub(1, std::memory_order_release); } }
StoreAuthorityMutation StoreAuthority::mutation() const noexcept
{ return StoreAuthorityMutation(m_state); }
}
