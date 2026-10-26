// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "media_identity.hpp"
#include "media_fingerprint.hpp"
#include <QThread>
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QUrl>
#include <QVariantList>
#include <QHash>
#include <functional>

namespace vlc::addons { class AddonManager; }
class AddonMetadataCoordinator;
class AddonMetadataService;

class SubtitleSourceService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool configured READ configured NOTIFY configurationChanged FINAL)
    Q_PROPERTY(bool hasCustomKey READ hasCustomKey NOTIFY configurationChanged FINAL)
    Q_PROPERTY(bool hasConsumerKey READ hasConsumerKey CONSTANT FINAL)
    Q_PROPERTY(QVariantList languages READ languages NOTIFY languagesChanged FINAL)
    Q_PROPERTY(bool preparing READ preparing NOTIFY preparingChanged FINAL)
    Q_PROPERTY(int searchCompleted READ searchCompleted NOTIFY searchProgressChanged FINAL)
    Q_PROPERTY(int searchTotal READ searchTotal NOTIFY searchProgressChanged FINAL)
public:
    struct Options {
        QUrl apiBase = QUrl(QStringLiteral("https://api.opensubtitles.com/api/v1/"));
        int timeoutMs = 20000;
        qint64 maxJsonBytes = 2 * 1024 * 1024;
        qint64 maxFileBytes = 16 * 1024 * 1024;
        int maxAddonRecords = 8192;
        QByteArray consumerApiKey;
    };
    explicit SubtitleSourceService(QObject* parent = nullptr);
    explicit SubtitleSourceService(const Options& options, QObject* parent = nullptr);
    ~SubtitleSourceService() override;
    void setAddonManager(vlc::addons::AddonManager* manager);
    void setMetadataService(AddonMetadataService* service);
    void setStorageDirectory(const QString& directory);
    bool configured() const;
    bool hasCustomKey() const { return m_customKey; }
    bool hasConsumerKey() const { return !m_options.consumerApiKey.isEmpty(); }
    QVariantList languages() const;
    Q_INVOKABLE void fetchLanguages();
    bool preparing() const { return m_preparing; }
    int searchCompleted() const { return m_targetIndex; }
    int searchTotal() const { return int(m_searchTargets.size()); }
    Q_INVOKABLE bool setApiKey(const QString& key);
    Q_INVOKABLE bool clearApiKey();
    Q_INVOKABLE quint64 search(const QString& query, const QString& language);
    Q_INVOKABLE quint64 searchMedia(const QVariantMap& request, const QString& language);
    Q_INVOKABLE QVariantMap identifyMedia(const QUrl& url, const QVariantMap& metadata = {}) const;
    Q_INVOKABLE QVariantMap parseMediaName(const QString& name) const;
    Q_INVOKABLE QVariantList rankSources(const QVariantMap& identity, const QVariantList& sources,
                                        const QString& mediaUrl, const QString& language) const;
    Q_INVOKABLE void cancelSearch();
    Q_INVOKABLE void resetSession();
    Q_INVOKABLE QVariantMap addLinks(const QStringList& links);
    Q_INVOKABLE quint64 prepareRows(const QVariantList& rows);
    Q_INVOKABLE void cancelPreparation();
signals:
    void configurationChanged();
    void preparingChanged();
    void languagesChanged();
    void searchProgressChanged();
    void searchFinished(quint64 request, const QVariantMap& result);
    void preparationFinished(quint64 request, const QVariantMap& result);
protected:
    bool eventFilter(QObject* object, QEvent* event) override;
private:
    struct AddonRecord { QString contribution; QVariantMap candidate; QString nativeUrl; QString partition; };
    void refreshBuiltinRegistration();
    void beginProviderSearch(quint64 generation);
    void finishProviderSearch(const QString& consumer, const QString& status,
                              const QVariantMap& data, const QString& error);
    void downloadAddon(quint64 generation, const QString& source, const AddonRecord& record);
    QString addonCachePath(const AddonRecord& record, const QString& format) const;
    bool providerAvailable(const QString& contribution) const;
    struct SearchTarget { MediaIdentity::Identity identity; QUrl url; bool useHash = true; };
    void beginSearchTarget(quint64 generation);
    void requestSearchPage(quint64 generation);
    void finishSearchPhase(quint64 generation);
    void finishSearchTarget(quint64 generation);
    void finishSearch(quint64 generation, const QString& error = {});
    struct Record { qint64 id; QString name; };
    struct Link { QUrl url; QString name; QString key; };
    using ReplyCallback = std::function<void(const QByteArray&, const QString&)>;
    QNetworkReply* request(const QUrl& url, const QByteArray& body, bool api,
                           qint64 limit, ReplyCallback callback);
    QString networkError(int status, QNetworkReply::NetworkError error, bool api) const;
    QVariantList parseSources(const QByteArray& data, QString& error);
    QString cachePath(const Record& record) const;
    QByteArray subtitleText(const QByteArray& bytes) const;
    void downloadNext(quint64 request);
    void finishPreparation(quint64 request, const QString& error = {});
    bool downloadUrlAllowed(const QUrl& url) const;
    QString linkFormat(const QByteArray& bytes) const;
    QString linkCachePath(const Link& link, const QString& format) const;
    void downloadLink(quint64 request, const QString& source, const Link& link);
    bool writeSubtitle(const QString& path, const QByteArray& bytes);
    Options m_options;
    QPointer<vlc::addons::AddonManager> m_addonManager;
    QPointer<AddonMetadataCoordinator> m_metadata;
    QPointer<AddonMetadataService> m_sharedMetadata;
    bool m_ownedMetadataProfile = false;
    QHash<QString, QPointer<SubtitleSourceService>> m_nativeSearches;
    QPointer<SubtitleSourceService> m_builtin;
    QHash<quint64, QString> m_builtinSearches;
    quint64 m_builtinPreparation = 0;
    quint64 m_builtinPrepareGeneration = 0;
    QString m_builtinPrepareSource;
    quint64 m_nativeRevision = 0;
    QString m_providerSearch;
    QString m_providerResolve;
    quint64 m_providerAsset = 0;
    QString m_providerResolveSource;
    quint64 m_providerResolveGeneration = 0;
    QHash<QString, AddonRecord> m_addonRecords;
    QNetworkAccessManager m_network;
    QString m_storageDirectory;
    QByteArray m_apiKey;
    bool m_customKey = false;
    QVariantList m_languages;
    bool m_languagesLoaded = false;
    quint64 m_languagesGeneration = 0;
    QPointer<QNetworkReply> m_languagesReply;
    QPointer<QNetworkReply> m_searchReply;
    QPointer<QThread> m_hashJob;
    QList<SearchTarget> m_searchTargets;
    QVariantList m_searchSources;
    QVariantList m_targetSources;
    QVariantMap m_searchIdentities;
    QString m_searchLanguage;
    bool m_searchAllProviders = false;
    QString m_searchWarning;
    int m_targetIndex = 0;
    int m_searchPhase = 0; // hash, structured identity, uncertain-year fallback
    int m_searchPage = 1;
    MediaFingerprint::Fingerprint m_searchFingerprint;
    QPointer<QNetworkReply> m_downloadReply;
    quint64 m_searchGeneration = 0;
    quint64 m_prepareGeneration = 0;
    bool m_preparing = false;
    QHash<QString, Record> m_records;
    QHash<QString, Link> m_links;
    QVariantList m_rows;
    QStringList m_pending;
    QHash<QString, QString> m_ready;
};
