// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_registry_client.hpp"
#include <QObject>
#include <QPointer>
#include <QVariantList>

namespace vlc::addons {
class AddonManager;

// The add-on registry for QML: Browse (search and details), install, and
// update lookup for installed add-ons. QML passes add-on IDs and versions
// only. The release identity comes from a registry answer this object read,
// and the manager installs it from Git as it installs a repository release.
class AddonRegistry final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QString address READ address NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QString addressError READ addressError NOTIFY settingsChanged FINAL)
    // A registry on this computer, such as a development server.
    Q_PROPERTY(bool developmentAddress READ developmentAddress NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool defaultAddress READ defaultAddress NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool searching READ searching NOTIFY resultsChanged FINAL)
    Q_PROPERTY(QString searchError READ searchError NOTIFY resultsChanged FINAL)
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged FINAL)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY resultsChanged FINAL)
    Q_PROPERTY(QVariantMap current READ current NOTIFY currentChanged FINAL)
    Q_PROPERTY(bool currentLoading READ currentLoading NOTIFY currentChanged FINAL)
    Q_PROPERTY(QString currentError READ currentError NOTIFY currentChanged FINAL)
    // Raised whenever the index changes; update lookups read it.
    Q_PROPERTY(int indexRevision READ indexRevision NOTIFY indexChanged FINAL)
    Q_PROPERTY(QString indexError READ indexError NOTIFY indexChanged FINAL)
public:
    static constexpr int PageSize = 30;

    AddonRegistry(AddonManager*, QString cacheDirectory, QObject* parent = nullptr);
    void setSettingsSource(std::function<AddonRegistryClient::Settings()>);
    AddonRegistryClient& client() { return m_client; }

    bool enabled() const { return m_settings.enabled; }
    QString address() const { return m_settings.address; }
    QString addressError() const { return AddonRegistryClient::addressProblem(m_settings.address); }
    bool developmentAddress() const;
    bool defaultAddress() const { return m_settings.address == QLatin1String(AddonRegistryClient::DefaultAddress); }
    bool searching() const { return m_searching; }
    QString searchError() const { return m_searchError; }
    QVariantList results() const { return m_results; }
    bool hasMore() const { return !m_next.isEmpty(); }
    QVariantMap current() const { return m_current ? m_current->toVariant() : QVariantMap{}; }
    bool currentLoading() const { return m_currentLoading; }
    QString currentError() const { return m_currentError; }
    int indexRevision() const { return m_indexRevision; }
    QString indexError() const { return m_indexError; }

    // Reads the settings again, and the index when it is older than a few
    // minutes (force: now).
    Q_INVOKABLE void refresh(bool force = false);
    // type: a contribution type such as "metadata.provider", or empty.
    Q_INVOKABLE void search(const QString& text, const QString& type);
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE void open(const QString& id);
    Q_INVOKABLE void closeCurrent();
    // Starts the review of this release, as Add extension does.
    Q_INVOKABLE bool install(const QString& id, const QString& version);
    // From the index: whether the registry lists this installed add-on from
    // this source, its update {version, tag} or {}, and whether the
    // installed version was withdrawn.
    Q_INVOKABLE bool listed(const QString& id, const QString& source) const;
    Q_INVOKABLE QVariantMap updateFor(const QString& id, const QString& version, const QString& source) const;
    Q_INVOKABLE bool withdrawn(const QString& id, const QString& version) const;
    Q_INVOKABLE bool installUpdate(const QString& id, const QString& version, const QString& source);

signals:
    void settingsChanged();
    void resultsChanged();
    void currentChanged();
    void indexChanged();

private:
    void readSettings();
    void searchPage(bool more);
    bool stage(const RegistryRelease&, bool update);

    QPointer<AddonManager> m_manager;
    AddonRegistryClient m_client;
    AddonRegistryClient::Settings m_settings;
    QString m_query, m_type, m_next, m_searchError, m_currentError, m_indexError;
    QVariantList m_results;
    std::optional<RegistryAddon> m_current;
    quint64 m_searchRequest = 0, m_currentRequest = 0, m_releaseRequest = 0;
    qint64 m_indexLoadedAt = 0;
    int m_indexRevision = 0;
    bool m_searching = false, m_currentLoading = false;
};
}
