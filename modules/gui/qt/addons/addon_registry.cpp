// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_registry.hpp"
#include "addon_manager.hpp"
#include "addon_manifest.hpp"
#include "addon_registry_install.hpp"
#include <QDateTime>
#include <QStringList>

namespace vlc::addons {
namespace {
// The index is cheap to ask for again (a 304), but there is no need to do it
// on every visit to Extensions.
constexpr qint64 IndexMaximumAgeMs = 10 * 60 * 1000;

// "metadata@1.0.0,settings@1.0.0,…": the contracts this host implements.
QString hostContractQuery()
{
    QStringList contracts;
    for (const auto& name : hostContracts()) contracts.append(name + QLatin1Char('@') + QLatin1String(HostAddonApiVersion));
    contracts.sort();
    return contracts.join(QLatin1Char(','));
}
}

AddonRegistry::AddonRegistry(AddonManager* manager, QString cacheDirectory, QObject* parent)
    : QObject(parent), m_manager(manager), m_client(std::move(cacheDirectory), this)
{
    readSettings();
}

void AddonRegistry::setSettingsSource(std::function<AddonRegistryClient::Settings()> source)
{
    m_client.setSettingsSource(std::move(source));
    readSettings();
}

bool AddonRegistry::developmentAddress() const
{
    return AddonRegistryClient::loopbackAddress(QUrl(m_settings.address));
}

void AddonRegistry::readSettings()
{
    const auto next = m_client.settings();
    if (next.enabled == m_settings.enabled && next.address == m_settings.address) return;
    const bool addressChanged = next.address != m_settings.address;
    m_settings = next;
    if (addressChanged || !m_settings.enabled) {
        // Another registry: what this one said no longer applies.
        m_client.cancel(m_searchRequest); m_client.cancel(m_currentRequest); m_client.cancel(m_releaseRequest);
        m_results.clear(); m_next.clear(); m_searchError.clear(); m_searching = false;
        m_current.reset(); m_currentError.clear(); m_currentLoading = false;
        m_indexLoadedAt = 0; m_indexError.clear();
        emit resultsChanged(); emit currentChanged();
        ++m_indexRevision; emit indexChanged();
    }
    emit settingsChanged();
}

void AddonRegistry::refresh(bool force)
{
    readSettings();
    if (!m_settings.enabled || !addressError().isEmpty()) return;
    const auto now = QDateTime::currentMSecsSinceEpoch();
    if (!force && m_indexLoadedAt && now - m_indexLoadedAt < IndexMaximumAgeMs) return;
    m_indexLoadedAt = now;
    const QPointer<AddonRegistry> owner(this);
    m_client.loadIndex([owner](bool, const AddonRegistryClient::Failure& failure) {
        if (!owner) return;
        owner->m_indexError = failure.message;
        // A failed check keeps the cached index; ask again on the next visit.
        if (!failure.code.isEmpty()) owner->m_indexLoadedAt = 0;
        ++owner->m_indexRevision;
        emit owner->indexChanged();
    });
}

void AddonRegistry::search(const QString& text, const QString& type)
{
    readSettings();
    m_query = text.trimmed().left(100);
    m_type = type.left(64);
    searchPage(false);
}

void AddonRegistry::loadMore()
{
    if (!m_searching && !m_next.isEmpty()) searchPage(true);
}

void AddonRegistry::searchPage(bool more)
{
    m_client.cancel(m_searchRequest);
    if (!more) { m_results.clear(); m_next.clear(); }
    QUrlQuery query;
    if (!m_query.isEmpty()) query.addQueryItem(QStringLiteral("q"), m_query);
    if (!m_type.isEmpty()) query.addQueryItem(QStringLiteral("type"), m_type);
    query.addQueryItem(QStringLiteral("host"), QLatin1String(HostAddonApiVersion));
    query.addQueryItem(QStringLiteral("contracts"), hostContractQuery());
    query.addQueryItem(QStringLiteral("limit"), QString::number(PageSize));
    if (more) query.addQueryItem(QStringLiteral("cursor"), m_next);
    m_searching = true; m_searchError.clear();
    emit resultsChanged();
    const QPointer<AddonRegistry> owner(this);
    const auto request = std::make_shared<quint64>(0);
    *request = m_searchRequest = m_client.search(query, [owner, request](const std::optional<RegistryPage>& page, const AddonRegistryClient::Failure& failure) {
        // A newer search replaced this one.
        if (!owner || (owner->m_searchRequest != *request && *request != 0) || failure.code == QLatin1String("canceled")) return;
        owner->m_searching = false;
        if (page) {
            for (const auto& item : page->items) owner->m_results.append(item.toVariant());
            owner->m_next = page->next;
        } else {
            owner->m_searchError = failure.message;
        }
        emit owner->resultsChanged();
    });
}

void AddonRegistry::open(const QString& id)
{
    m_client.cancel(m_currentRequest);
    m_current.reset(); m_currentError.clear(); m_currentLoading = true;
    emit currentChanged();
    const QPointer<AddonRegistry> owner(this);
    const auto request = std::make_shared<quint64>(0);
    *request = m_currentRequest = m_client.addon(id.left(128), [owner, request](const std::optional<RegistryAddon>& addon, const AddonRegistryClient::Failure& failure) {
        if (!owner || (owner->m_currentRequest != *request && *request != 0) || failure.code == QLatin1String("canceled")) return;
        owner->m_currentLoading = false;
        if (addon) owner->m_current = *addon; else owner->m_currentError = failure.message;
        emit owner->currentChanged();
    });
}

void AddonRegistry::closeCurrent()
{
    m_client.cancel(m_currentRequest);
    m_current.reset(); m_currentError.clear(); m_currentLoading = false;
    emit currentChanged();
}

bool AddonRegistry::stage(const RegistryRelease& release, bool update)
{
    if (!m_manager) return false;
    if (!hostAcceptsApi(release.addonApi, release.contracts)) {
        m_currentError = tr("This version needs a newer version of the app.");
        emit currentChanged();
        return false;
    }
    return m_manager->stageRegistryRelease(release, update);
}

bool AddonRegistry::install(const QString& id, const QString& version)
{
    if (m_current && m_current->id == id && m_current->latestRelease && m_current->latestRelease->version == version)
        return stage(*m_current->latestRelease, false);
    if (const auto* entry = m_client.index().find(id))
        for (const auto& release : entry->releases)
            if (release.version == version) return stage(release, false);
    // Not in what this object has read: ask for that one release.
    const QPointer<AddonRegistry> owner(this);
    m_client.cancel(m_releaseRequest);
    m_releaseRequest = m_client.release(id.left(128), version.left(128), [owner, id](const std::optional<RegistryRelease>& release, const AddonRegistryClient::Failure& failure) {
        if (!owner || failure.code == QLatin1String("canceled")) return;
        if (release && release->addonId == id) { owner->stage(*release, false); return; }
        owner->m_currentError = release ? owner->tr("The add-on registry sent an answer the app cannot use.") : failure.message;
        emit owner->currentChanged();
    });
    return true;
}

bool AddonRegistry::listed(const QString& id, const QString& source) const
{
    const auto* entry = m_client.index().find(id);
    return m_settings.enabled && entry && entry->source == source;
}

QVariantMap AddonRegistry::updateFor(const QString& id, const QString& version, const QString& source) const
{
    const auto* entry = m_client.index().find(id);
    if (!m_settings.enabled || !entry) return {};
    const auto update = selectRegistryUpdate(*entry, version, source, QStringLiteral("^") + version, false);
    if (!update) return {};
    return {{"version", update->version}, {"tag", update->install.tag}};
}

bool AddonRegistry::withdrawn(const QString& id, const QString& version) const
{
    const auto* entry = m_client.index().find(id);
    return m_settings.enabled && entry && entry->yanked.contains(version);
}

bool AddonRegistry::installUpdate(const QString& id, const QString& version, const QString& source)
{
    const auto* entry = m_client.index().find(id);
    if (!m_settings.enabled || !entry) return false;
    const auto update = selectRegistryUpdate(*entry, version, source, QStringLiteral("^") + version, false);
    return update && stage(*update, true);
}
}
