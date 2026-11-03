// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "addon_store_profile_session.hpp"

namespace vlc::addons {
StoreProfileSession::StoreProfileSession(Options options, std::unique_ptr<StoreAuthorityPeer> peer, QObject* parent)
    : QObject(parent), m_options(std::move(options))
{
    StoreProfileClient::Options client;
    client.directory = m_options.directory;
    client.work = m_options.work;
    client.unixRuntime = m_options.unixRuntime;
    client.changed = [this](StoreProfileClient::State state) { clientChanged(state); };
    m_client = std::make_unique<StoreProfileClient>(std::move(client), std::move(peer));
}

StoreProfileSession::~StoreProfileSession()
{
    close();
}

bool StoreProfileSession::start()
{
    if (m_closed)
        return false;
    claim();
    return m_client->start();
}

void StoreProfileSession::claim()
{
    if (m_closed || m_coordinator)
        return;
    StoreProfileCoordinator::Options options;
    options.directory = m_options.directory;
    options.recovery = m_options.recovery;
    options.work = m_options.work;
    options.unixRuntime = m_options.unixRuntime;
    options.changed = [this](StoreProfileCoordinator::State state) { coordinatorChanged(state); };
    m_coordinator = std::make_unique<StoreProfileCoordinator>(std::move(options));
    m_coordinator->start();
}

void StoreProfileSession::coordinatorChanged(StoreProfileCoordinator::State state)
{
    switch (state) {
    case StoreProfileCoordinator::State::Serving:
        setCoordinating(true);
        // A client that gave up while no process coordinated tries again.
        if (m_client->state() == StoreProfileClient::State::Unavailable)
            m_client->retry();
        return;
    case StoreProfileCoordinator::State::Peer:
    case StoreProfileCoordinator::State::Failed:
    case StoreProfileCoordinator::State::Closed:
        setCoordinating(false);
        // This callback can run inside the coordinator. Delete it later.
        if (m_coordinator)
            m_coordinator.release()->deleteLater();
        return;
    case StoreProfileCoordinator::State::Starting:
    case StoreProfileCoordinator::State::Recovering:
        return;
    }
}

void StoreProfileSession::clientChanged(StoreProfileClient::State state)
{
    // A client that is looking for a coordinator starts a claim. Only one
    // process gets the owner lock; the others report Peer and stay clients.
    if (state == StoreProfileClient::State::Connecting || state == StoreProfileClient::State::Unavailable)
        claim();
    const auto changed = m_options.changed;
    if (changed)
        changed(state);
}

void StoreProfileSession::setCoordinating(bool coordinating)
{
    if (m_coordinating == coordinating)
        return;
    m_coordinating = coordinating;
    const auto changed = m_options.coordinating;
    if (changed)
        changed(coordinating);
}

void StoreProfileSession::close()
{
    if (m_closed)
        return;
    m_closed = true;
    m_client->close();
    if (m_coordinator)
        m_coordinator->close();
    m_coordinator.reset();
    m_coordinating = false;
}

bool StoreProfileSession::coordinating() const noexcept
{
    return m_coordinating;
}

StoreProfileClient::State StoreProfileSession::state() const noexcept
{
    return m_client->state();
}

std::optional<StoreProfileClient::Epoch> StoreProfileSession::epoch() const
{
    return m_client->epoch();
}

StoreProfileClient::ReadyWait StoreProfileSession::readyWait() const
{
    return m_client->readyWait();
}

StoreProfileClient::WritePort StoreProfileSession::writePort() const
{
    return m_client->writePort();
}

bool StoreProfileSession::timerActive() const noexcept
{
    return m_client->timerActive() || (m_coordinator && m_coordinator->timerActive());
}
}
