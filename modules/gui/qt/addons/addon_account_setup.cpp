// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_account_setup.hpp"
#include "addon_device_authorization.hpp"
#include "addon_manager.hpp"
#include <QClipboard>
#include <QDesktopServices>
#include <QGuiApplication>

namespace vlc::addons {
AddonAccountSetup::AddonAccountSetup(QObject* parent) : QObject(parent) {}
AddonAccountSetup::~AddonAccountSetup() { cancel(); }

QObject* AddonAccountSetup::manager() const { return m_manager.data(); }
void AddonAccountSetup::setManager(QObject* manager)
{
    if (m_manager == manager) return;
    cancel();
    if (m_manager) disconnect(m_manager, nullptr, this, nullptr);
    m_saveRequest.clear();
    m_manager = qobject_cast<AddonManager*>(manager);
    if (m_manager) connect(m_manager, &AddonManager::writeFinished, this, &AddonAccountSetup::saveFinished);
    emit changed();
}

QString AddonAccountSetup::state() const
{
    if (m_saved) return QStringLiteral("connected");
    if (!m_saveRequest.isEmpty()) return QStringLiteral("requesting");
    if (!m_flow) return m_message.isEmpty() ? QStringLiteral("idle") : QStringLiteral("failed");
    switch (m_flow->state()) {
    case DeviceAuthorization::State::Requesting: return QStringLiteral("requesting");
    case DeviceAuthorization::State::Waiting: return QStringLiteral("waiting");
    case DeviceAuthorization::State::Canceled: return QStringLiteral("canceled");
    case DeviceAuthorization::State::Idle: return QStringLiteral("idle");
    case DeviceAuthorization::State::Connected:
    case DeviceAuthorization::State::Failed: return QStringLiteral("failed");
    }
    return QStringLiteral("failed");
}
QString AddonAccountSetup::userCode() const { return m_flow ? m_flow->userCode() : QString{}; }
QString AddonAccountSetup::verificationHost() const { return m_flow ? m_flow->verificationUrl().host() : QString{}; }
QString AddonAccountSetup::message() const
{ return !m_message.isEmpty() ? m_message : m_flow ? m_flow->message() : QString{}; }
void AddonAccountSetup::saveFinished(const QString& request, bool success, const QString& error)
{
    if (request.isEmpty() || request != m_saveRequest) return;
    m_saveRequest.clear();
    if (success) {
        m_saved = true;
        emit changed();
        emit connected();
        return;
    }
    m_message = error.isEmpty() ? tr("The app cannot keep this account.") : error;
    emit changed();
}
int AddonAccountSetup::remainingSeconds() const { return m_flow ? m_flow->remainingSeconds() : 0; }

bool AddonAccountSetup::start()
{
    cancel();
    m_message.clear(); m_saved = false; m_saveRequest.clear();
    const auto recipe = m_manager ? m_manager->accountRecipe(m_packageId, m_field) : std::nullopt;
    if (!recipe) {
        m_message = tr("This add-on does not have an account sign-in that the app can use.");
        emit changed();
        return false;
    }
    m_flow = std::make_unique<DeviceAuthorization>(*recipe, m_manager->accountTransport());
    connect(m_flow.get(), &DeviceAuthorization::changed, this, &AddonAccountSetup::changed);
    connect(m_flow.get(), &DeviceAuthorization::connected, this, [this](const DeviceTokens& tokens) {
        m_saveRequest = m_manager ? m_manager->requestConnectAccount(m_packageId, m_field, tokens, m_sessionOnly) : QString{};
        if (m_saveRequest.isEmpty()) m_message = tr("The app cannot keep this account.");
        emit changed();
    });
    m_flow->start();
    return true;
}

void AddonAccountSetup::cancel()
{
    if (m_flow) m_flow->cancel();
}

bool AddonAccountSetup::openPage()
{
    if (!m_flow || m_flow->state() != DeviceAuthorization::State::Waiting) return false;
    return QDesktopServices::openUrl(m_flow->completeVerificationUrl());
}

void AddonAccountSetup::copyCode()
{
    if (m_flow && m_flow->state() == DeviceAuthorization::State::Waiting)
        if (auto* clipboard = QGuiApplication::clipboard()) clipboard->setText(m_flow->userCode());
}
}
