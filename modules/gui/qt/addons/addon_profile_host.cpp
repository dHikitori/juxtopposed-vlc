// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "addon_profile_host.hpp"
#include "addon_manager.hpp"
#include "addon_package_store.hpp"

namespace vlc::addons {
AddonProfileHost::AddonProfileHost(AddonManager* manager, AddonStreamLifetime::Handle work, QObject* parent)
    : QObject(parent), m_manager(manager), m_work(std::move(work))
{
    m_thread.setObjectName(QStringLiteral("hakone-profile"));
}

AddonProfileHost::~AddonProfileHost()
{
    close();
}

bool AddonProfileHost::start()
{
    if (m_started || m_closed || !m_manager || !m_work.current())
        return false;
    m_started = true;
    m_directory = m_manager->storageDirectory();
    // Memory only. From here, native admission and ordinary store writes wait
    // for the profile coordinator.
    auto peer = StoreAuthority(m_directory).attachPeer();
    if (!peer)
        return false;
    m_context = new QObject;
    m_context->moveToThread(&m_thread);
    m_thread.start();
    PackageStore::ProfileWriter writer;
    auto shared = std::make_shared<std::unique_ptr<StoreAuthorityPeer>>(std::move(peer));
    // The control thread is new and idle, so this wait is short.
    QMetaObject::invokeMethod(m_context, [this, shared, &writer] {
        StoreProfileSession::Options options;
        options.directory = m_directory;
        options.work = m_work;
        options.recovery = [](const StoreProfileWriteContext& context, QString* error) {
            return PackageStore::recoverProfile(context, error);
        };
        options.changed = [this](StoreProfileClient::State state) { sessionChanged(state); };
        options.coordinating = [this](bool coordinating) {
            m_coordinating.store(coordinating, std::memory_order_release);
        };
        m_session = new StoreProfileSession(std::move(options), std::move(*shared));
        writer.waitReady = m_session->readyWait();
        writer.permit = m_session->writePort();
        m_session->start();
    }, Qt::BlockingQueuedConnection);
    if (!writer.permit)
        return false;
    m_manager->setProfileWriter(std::move(writer));
    return true;
}

void AddonProfileHost::sessionChanged(StoreProfileClient::State state)
{
    // Control thread.
    const bool ready = state == StoreProfileClient::State::Ready;
    m_ready.store(ready, std::memory_order_release);
    if (!ready || !m_session)
        return;
    const auto epoch = m_session->epoch();
    const bool changed = m_lastEpoch && epoch && *m_lastEpoch != *epoch;
    m_lastEpoch = epoch;
    if (!changed)
        return;
    // Another process's change, or this process's own write, published a new
    // revision. Reload the manager's cached profile state.
    const QPointer<AddonManager> manager = m_manager;
    QMetaObject::invokeMethod(this, [manager] {
        if (manager)
            manager->profileChanged();
    }, Qt::QueuedConnection);
}

void AddonProfileHost::close()
{
    if (m_closed)
        return;
    m_closed = true;
    m_ready.store(false, std::memory_order_release);
    if (m_manager)
        m_manager->setProfileWriter({});
    if (m_context && m_thread.isRunning()) {
        QMetaObject::invokeMethod(m_context, [this] {
            if (m_session) {
                m_session->close();
                delete m_session;
                m_session = nullptr;
            }
        }, Qt::BlockingQueuedConnection);
    }
    m_thread.quit();
    m_thread.wait();
    delete m_context;
    m_context = nullptr;
    m_coordinating.store(false, std::memory_order_release);
}
}
