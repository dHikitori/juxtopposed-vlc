// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "subtitle_source_service.hpp"
#include "subtitle_matching.hpp"
#include "addons/addon_manager.hpp"
#include "addons/addon_catalogue_view.hpp"
#include "addons/addon_metadata_coordinator.hpp"
#include "addons/addon_metadata_service.hpp"
#include <QDir>
#include <QCoreApplication>
#include <QEvent>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSet>
#include <QStringDecoder>
#include <QTimer>
#include <QUrlQuery>
#include <QXmlStreamReader>
#include <QUuid>
#include <memory>
#include <algorithm>
#include <limits>
#if __has_include("subtitle_consumer_key.hpp")
#include "subtitle_consumer_key.hpp"
#else
static constexpr char subtitleConsumerKey[] = "";
#endif

namespace {
SubtitleSourceService::Options defaultOptions()
{
    SubtitleSourceService::Options options;
    options.consumerApiKey = subtitleConsumerKey;
    return options;
}
QString safeName(QString name, const QString& format = QStringLiteral("srt"))
{
    name.replace('\\', '/'); name = QFileInfo(name).fileName();
    name.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]")), QStringLiteral("_"));
    while (name.endsWith('.') || name.endsWith(' ')) name.chop(1);
    name = QFileInfo(name).completeBaseName().left(96);
    if (name.isEmpty() || name == "." || name == "..") name = QStringLiteral("subtitle");
    static const QRegularExpression device(QStringLiteral("^(?:con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\\.|$)"), QRegularExpression::CaseInsensitiveOption);
    if (device.match(name).hasMatch()) name.prepend(QLatin1Char('_'));
    return name + QLatin1Char('.') + format;
}
class HashJob : public QThread
{
public:
    explicit HashJob(const QUrl& url) : m_url(url) {}
    MediaFingerprint::Fingerprint fingerprint;
protected:
    void run() override { fingerprint = MediaFingerprint::openSubtitles(m_url, [this] { return isInterruptionRequested(); }); }
private:
    QUrl m_url;
};

void mergeSources(QVariantList& sources, const QVariantList& additions)
{
    const auto sourceKey = [](const QVariantMap& source) {
        const auto provider = source.value("provider").toString();
        return provider.isEmpty() ? QString::number(source.value("fileId").toLongLong())
            : provider + '/' + source.value("id").toString();
    };
    QHash<QString, qsizetype> indexes;
    for (qsizetype i = 0; i < sources.size(); ++i) indexes.insert(sourceKey(sources[i].toMap()), i);
    for (const auto& value : additions) {
        auto source = value.toMap(); const auto id = sourceKey(source);
        if (!indexes.contains(id)) { indexes.insert(id, sources.size()); sources.append(source); continue; }
        auto existing = sources[indexes.value(id)].toMap();
        auto hashes = existing.value("hashFor").toStringList(); hashes.append(source.value("hashFor").toStringList()); hashes.removeDuplicates();
        existing.insert("hashFor", hashes); sources[indexes.value(id)] = existing;
    }
}

const QString builtinSubtitles = QStringLiteral("hakone.builtin.opensubtitles/search");

QString sourceUrl(qint64 id, const QString& name)
{
    QUrl url; url.setScheme(QStringLiteral("opensubtitles"));
    url.setPath(QStringLiteral("/%1/%2").arg(id).arg(name));
    return url.toString(QUrl::FullyEncoded);
}
}

SubtitleSourceService::SubtitleSourceService(QObject* parent)
    : SubtitleSourceService(defaultOptions(), parent) {}

SubtitleSourceService::SubtitleSourceService(const Options& options, QObject* parent)
    : QObject(parent), m_options(options), m_network(this), m_apiKey(options.consumerApiKey)
{
    m_languages = {QVariantMap{{"code", "en"}, {"text", QT_TR_NOOP("English")}},
        QVariantMap{{"code", "es"}, {"text", QT_TR_NOOP("Spanish")}}, QVariantMap{{"code", "ja"}, {"text", QT_TR_NOOP("Japanese")}},
        QVariantMap{{"code", "de"}, {"text", QT_TR_NOOP("German")}}, QVariantMap{{"code", "nl"}, {"text", QT_TR_NOOP("Dutch")}},
        QVariantMap{{"code", "fr"}, {"text", QT_TR_NOOP("French")}}};
    qApp->installEventFilter(this);
}

QVariantList SubtitleSourceService::languages() const
{
    auto languages = m_languages;
    for (auto& entry : languages) {
        auto row = entry.toMap();
        const auto source = row.value(QStringLiteral("text")).toString().toUtf8();
        row.insert(QStringLiteral("text"), tr(source.constData()));
        entry = row;
    }
    return languages;
}

bool SubtitleSourceService::eventFilter(QObject* object, QEvent* event)
{
    if (object == qApp && event->type() == QEvent::LanguageChange)
        emit languagesChanged();
    return QObject::eventFilter(object, event);
}

SubtitleSourceService::~SubtitleSourceService()
{
    cancelSearch(); cancelPreparation();
    if (m_sharedMetadata) m_sharedMetadata->unregisterBuiltinProvider(builtinSubtitles);
    if (m_addonManager) m_addonManager->unregisterBuiltinProvider(builtinSubtitles);
    if (m_hashJob) m_hashJob->requestInterruption();
    for (auto* reply : m_network.findChildren<QNetworkReply*>()) {
        disconnect(reply, nullptr, this, nullptr); reply->abort();
    }
}

bool SubtitleSourceService::configured() const
{
    if (m_providerDisplayMode) {
        if (!providerDisplayCurrent()) return false;
        const auto providers = displayProviders();
        return std::any_of(providers.cbegin(), providers.cend(), [](const QVariant& value) {
            const auto provider = value.toMap();
            return provider.value("enabled").toBool() && provider.value("configurationKnown").toBool()
                && provider.value("configured").toBool();
        });
    }
    if (m_addonManagerRetired) return false;
    if (!m_addonManager) return !m_apiKey.isEmpty();
    // Unmounted contract-test/legacy callers keep the synchronous status API.
    // A mounted display never falls back to it, including during retirement.
    const auto providers = m_addonManager->providersForFeature(QStringLiteral("subtitles"));
    return std::any_of(providers.cbegin(), providers.cend(), [](const QVariant& value) {
        const auto provider = value.toMap();
        return provider.value("enabled").toBool() && provider.value("configured").toBool();
    });
}

bool SubtitleSourceService::configuredForExecution() const
{
    if (m_addonManagerRetired) return false;
    if (!m_addonManager) return !m_apiKey.isEmpty();
    // Fresh configuration is checked by the asynchronous coordinator.
    return m_metadata || m_sharedMetadata;
}

bool SubtitleSourceService::providerDisplayBound() const
{
    return m_providerDisplayMode && !m_addonManagerRetired && m_addonManager && m_providerDisplay
        && vlc::addons::AddonCatalogueView::attached(m_addonManager) == m_providerDisplay;
}
bool SubtitleSourceService::providerDisplayCurrent() const { return providerDisplayBound() && m_providerDisplay->isCurrent(); }
bool SubtitleSourceService::providerDisplayLoading() const { return providerDisplayBound() && m_providerDisplay->loading(); }
QString SubtitleSourceService::providerDisplayError() const
{
    if (!m_providerDisplayMode) return {};
    return providerDisplayBound() ? m_providerDisplay->error() : tr("The subtitle provider list is not available.");
}
QVariantList SubtitleSourceService::displayProviders() const
{
    if (!m_providerDisplayMode) return !m_addonManagerRetired && m_addonManager
        ? m_addonManager->providersForFeature(QStringLiteral("subtitles")) : QVariantList{};
    return providerDisplayBound() ? m_addonManager->displayProvidersForFeature(QStringLiteral("subtitles")) : QVariantList{};
}
bool SubtitleSourceService::configurationKnown() const
{
    if (!m_providerDisplayMode) return !m_addonManagerRetired;
    if (!providerDisplayCurrent()) return false;
    bool unknown = false;
    for (const auto& value : displayProviders()) {
        const auto provider = value.toMap();
        if (!provider.value("enabled").toBool()) continue;
        if (provider.value("configurationKnown").toBool() && provider.value("configured").toBool()) return true;
        unknown |= !provider.value("configurationKnown").toBool();
    }
    return !unknown;
}
void SubtitleSourceService::retireAddonManager()
{
    // Do not call Manager once its retirement signal starts. Execution still
    // uses its authoritative paths while the bound Manager is open.
    if (m_addonBindingEpoch != std::numeric_limits<quint64>::max()) ++m_addonBindingEpoch;
    m_addonManagerRetired = true; m_addonManager = nullptr;
    const QPointer<SubtitleSourceService> alive(this);
    cancelSearch(); if (!alive) return;
    cancelPreparation(); if (!alive) return;
    if (m_metadata) m_metadata->invalidate(); if (!alive) return;
    emit configurationChanged();
}

void SubtitleSourceService::setAddonManager(vlc::addons::AddonManager* manager)
{
    if (m_addonManager == manager && !m_addonManagerRetired) return;
    if (m_addonBindingEpoch == std::numeric_limits<quint64>::max()) return;
    const auto epoch = ++m_addonBindingEpoch;
    const QPointer<vlc::addons::AddonManager> incoming(manager);
    const bool hasIncoming = manager != nullptr;
    const QPointer<SubtitleSourceService> alive(this);
    const auto current = [&] { return alive && epoch == m_addonBindingEpoch && (!hasIncoming || incoming); };
    setMetadataService(nullptr);
    if (!current()) return;
    cancelSearch(); if (!current()) return;
    cancelPreparation(); if (!current()) return;
    // From this point an aborted transition cannot revive the old native path.
    // The unregister notification may delete either Manager or bind a newer one.
    const auto previousManager = m_addonManager;
    m_addonManager = nullptr; m_addonManagerRetired = true;
    const auto publishAbort = [&] { if (alive && epoch == m_addonBindingEpoch) emit configurationChanged(); };
    if (m_providerDisplay) disconnect(m_providerDisplay, nullptr, this, nullptr);
    if (previousManager) {
        disconnect(previousManager, nullptr, this, nullptr);
        previousManager->unregisterBuiltinProvider(builtinSubtitles);
        if (!current()) { publishAbort(); return; }
    }
    const auto metadata = m_metadata; m_metadata = nullptr; delete metadata; if (!current()) { publishAbort(); return; }
    const auto builtin = m_builtin; m_builtin = nullptr; delete builtin; if (!current()) { publishAbort(); return; }
    m_builtinSearches.clear(); m_addonRecords.clear();
    auto* const target = incoming.data();
    m_addonManager = target;
    m_addonManagerRetired = false;
    m_providerDisplayMode = target && target->displayCatalogueAttached();
    m_providerDisplay = m_providerDisplayMode ? vlc::addons::AddonCatalogueView::attached(target) : nullptr;
    if (!target) { emit configurationChanged(); return; }
    connect(target, &vlc::addons::AddonManager::retiring, this, &SubtitleSourceService::retireAddonManager, Qt::DirectConnection);
    connect(target, &QObject::destroyed, this, &SubtitleSourceService::retireAddonManager);
    connect(target, &vlc::addons::AddonManager::capabilitiesChanged, this, &SubtitleSourceService::configurationChanged);
    if (m_providerDisplay) {
        connect(m_providerDisplay, &vlc::addons::AddonCatalogueView::catalogueChanged, this, &SubtitleSourceService::configurationChanged);
        connect(m_providerDisplay, &vlc::addons::AddonCatalogueView::freshnessChanged, this, &SubtitleSourceService::configurationChanged);
        connect(m_providerDisplay, &QObject::destroyed, this, &SubtitleSourceService::configurationChanged);
    }
    m_builtin = new SubtitleSourceService(m_options, this);
    if (!m_storageDirectory.isEmpty()) m_builtin->setStorageDirectory(m_storageDirectory);
    if (!current()) return;
    AddonMetadataCoordinator::Backend backend;
    backend.providersForFeature = [this](const QString& feature) {
        return m_addonManager ? m_addonManager->providersForFeature(feature) : QVariantList{};
    };
    backend.selectProviders = [this](const QString& feature, AddonMetadataCoordinator::SelectionComplete complete) {
        return m_addonManager ? m_addonManager->selectMetadataProviders(feature, std::move(complete))
            : AddonMetadataCoordinator::SelectionCancel{};
    };
    backend.invoke = [this](const QString& contribution, const QString& operation, const QVariantMap& context) {
        if (contribution != builtinSubtitles)
            return m_addonManager ? m_addonManager->invokeMetadata(contribution, operation, context) : QString{};
        if (!providerAvailable(builtinSubtitles) || !m_builtin || operation != QLatin1String("searchAssets") || m_targetIndex >= m_searchTargets.size()) return QString{};
        const auto invocation = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto& target = m_searchTargets[m_targetIndex];
        const QVariantMap request{{"url", target.url}, {"identity", context.value("identity")},
            {"skipHash", !target.useHash}};
        const auto requestId = m_builtin->searchMedia(request, context.value("language").toString());
        m_builtinSearches.insert(requestId, invocation);
        return invocation;
    };
    backend.cancel = [this](const QString& invocation) {
        const auto requests = m_builtinSearches.keys(invocation);
        if (!requests.isEmpty()) {
            for (const auto request : requests) m_builtinSearches.remove(request);
            if (m_builtin) m_builtin->cancelSearch();
        } else if (m_addonManager) m_addonManager->cancel(invocation);
    };
    m_metadata = new AddonMetadataCoordinator(std::move(backend), this);
    connect(target, &vlc::addons::AddonManager::operationFinished, m_metadata, &AddonMetadataCoordinator::operationFinished);
    connect(m_metadata, &AddonMetadataCoordinator::finished, this, &SubtitleSourceService::finishProviderSearch);
    connect(m_builtin, &SubtitleSourceService::searchFinished, this, [this](quint64 request, const QVariantMap& result) {
        const auto invocation = m_builtinSearches.take(request);
        if (invocation.isEmpty() || !m_metadata) return;
        QVariantList candidates;
        for (const auto& value : result.value("sources").toList()) {
            auto candidate = value.toMap();
            candidate.insert("id", candidate.value("fileId").toString());
            candidates.append(candidate);
        }
        if (!result.value("warning").toString().isEmpty()) m_searchWarning = result.value("warning").toString();
        const auto status = !candidates.isEmpty() ? QStringLiteral("match")
            : result.value("success").toBool() ? QStringLiteral("no_match") : QStringLiteral("error");
        m_metadata->operationFinished(invocation, builtinSubtitles, status, {{"candidates", candidates}}, result.value("error").toString());
    });
    connect(m_builtin, &SubtitleSourceService::preparationFinished, this, [this](quint64 request, const QVariantMap& result) {
        if (request != m_builtinPreparation || m_builtinPrepareGeneration != m_prepareGeneration || !m_preparing) return;
        m_builtinPreparation = 0;
        const auto source = m_builtinPrepareSource;
        const auto record = m_addonRecords.value(source);
        const auto resolved = result.value("resolved").toMap().value(record.nativeUrl).toString();
        if (!result.value("success").toBool() || resolved.isEmpty()) {
            finishPreparation(m_prepareGeneration, result.value("error").toString()); return;
        }
        m_ready.insert(source, resolved);
        m_pending.removeAll(source);
        const auto generation = m_prepareGeneration;
        QTimer::singleShot(0, this, [this, generation] { downloadNext(generation); });
    });
    connect(target, &vlc::addons::AddonManager::operationFinished, this,
        [this](const QString& invocation, const QString& contribution, const QString& status, const QVariantMap& data, const QString& error) {
        if (invocation != m_providerResolve || m_providerResolveGeneration != m_prepareGeneration || !m_preparing) return;
        m_providerResolve.clear();
        const auto generation = m_prepareGeneration;
        const auto source = m_providerResolveSource;
        if (!m_addonManager || (status != QLatin1String("match") && status != QLatin1String("partial"))) {
            finishPreparation(generation, error.isEmpty() ? tr("The subtitle provider could not resolve this file.") : error); return;
        }
        auto descriptor = QJsonObject::fromVariantMap(data);
        descriptor.insert(QStringLiteral("method"), QStringLiteral("GET"));
        const auto record = m_addonRecords.value(source);
        const QPointer<SubtitleSourceService> owner(this);
        m_providerAsset = m_addonManager->fetchAsset(contribution, descriptor,
            [owner, generation, source, record](const vlc::addons::NetworkBroker::Response& response) {
            if (!owner || generation != owner->m_prepareGeneration || !owner->m_preparing) return;
            owner->m_providerAsset = 0;
            if (!response.error.isEmpty() || response.status < 200 || response.status >= 300
                || response.body.size() > owner->m_options.maxFileBytes) {
                owner->finishPreparation(generation, response.error.isEmpty() ? tr("The subtitle download failed.") : response.error); return;
            }
            const auto format = owner->linkFormat(response.body);
            if (format.isEmpty()) { owner->finishPreparation(generation, tr("This provider returned an unsupported subtitle file.")); return; }
            const auto path = owner->addonCachePath(record, format);
            if (!owner->writeSubtitle(path, response.body)) { owner->finishPreparation(generation, tr("Hakone cannot save these subtitles.")); return; }
            owner->m_ready.insert(source, QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded));
            owner->m_pending.removeAll(source);
            QTimer::singleShot(0, owner, [owner, generation] { if (owner) owner->downloadNext(generation); });
        });
    });
    connect(target, &vlc::addons::AddonManager::revisionChanged, this, [this] {
        if (!m_providerSearch.isEmpty()) finishSearch(m_searchGeneration, tr("Subtitle provider settings changed. Search again."));
        if (m_preparing) finishPreparation(m_prepareGeneration, tr("Subtitle provider settings changed. Try again."));
        if (m_metadata) m_metadata->invalidate();
        cancelSearch(); cancelPreparation(); m_addonRecords.clear(); emit configurationChanged();
    });
    refreshBuiltinRegistration();
    if (!current() || m_addonManager != incoming) return;
    emit configurationChanged();
}

void SubtitleSourceService::setMetadataService(AddonMetadataService* service)
{
    if (m_sharedMetadata == service) return;
    cancelSearch(); cancelPreparation();
    if (m_sharedMetadata) {
        disconnect(m_sharedMetadata, nullptr, this, nullptr);
        m_sharedMetadata->unregisterBuiltinProvider(builtinSubtitles);
    }
    const auto requests = m_nativeSearches;
    m_nativeSearches.clear();
    for (const auto& request : requests) if (request) { request->cancelSearch(); request->deleteLater(); }
    m_sharedMetadata = service;
    if (!service) return;
    m_ownedMetadataProfile = true;
    delete m_metadata; m_metadata = nullptr;
    connect(service, &AddonMetadataService::finished, this, &SubtitleSourceService::finishProviderSearch);
    connect(service, &AddonMetadataService::canceled, this, [this](const QString& consumer) {
        if (consumer != m_providerSearch) return;
        m_providerSearch.clear();
        finishSearch(m_searchGeneration, tr("The metadata request was canceled. Search again."));
    });
    refreshBuiltinRegistration();
}

void SubtitleSourceService::refreshBuiltinRegistration()
{
    ++m_nativeRevision;
    if (!m_addonManager) return;
    const QVariantMap descriptor{
        {"id", builtinSubtitles}, {"packageId", "hakone.builtin.opensubtitles"}, {"name", "OpenSubtitles"},
        {"builtin", true}, {"configured", !m_apiKey.isEmpty()}, {"enabled", true},
        {"features", QStringList{"subtitles"}}, {"mediaKinds", QStringList{"movie", "episode", "unknown"}},
        {"cachePartition", QStringLiteral("native-opensubtitles:%1").arg(m_nativeRevision)}};
    if (!m_sharedMetadata) { m_addonManager->registerBuiltinProvider(QStringLiteral("subtitles"), descriptor); return; }
    m_sharedMetadata->registerBuiltinProvider(QStringLiteral("subtitles"), descriptor, this,
        [this](const QString& invocation, const QString& operation, const QVariantMap& context) {
            if (!m_sharedMetadata || operation != QLatin1String("searchAssets")) return;
            auto* request = new SubtitleSourceService(m_options, this);
            if (!m_storageDirectory.isEmpty()) request->setStorageDirectory(m_storageDirectory);
            m_nativeSearches.insert(invocation, request);
            const QPointer<AddonMetadataService> owner(m_sharedMetadata);
            connect(request, &SubtitleSourceService::searchFinished, this,
                [this, request, owner, invocation](quint64, const QVariantMap& result) {
                if (m_nativeSearches.take(invocation) != request) return;
                QVariantList candidates;
                for (const auto& value : result.value("sources").toList()) {
                    auto candidate = value.toMap(); candidate.insert("id", candidate.value("fileId").toString());
                    if (!m_builtin) continue;
                    const auto native = sourceUrl(candidate.value("fileId").toLongLong(), candidate.value("name").toString());
                    if (!m_builtin->m_records.contains(native)
                        && m_builtin->m_records.size() >= std::clamp(m_options.maxAddonRecords, 1, 8192)) continue;
                    const auto record = request->m_records.constFind(native);
                    if (record == request->m_records.cend()) continue;
                    m_builtin->m_records.insert(native, *record);
                    candidates.append(candidate);
                }
                request->deleteLater();
                const auto status = !candidates.isEmpty() ? QStringLiteral("match")
                    : result.value("success").toBool() ? QStringLiteral("no_match") : QStringLiteral("error");
                if (owner) owner->completeBuiltin(invocation, status, {{"candidates", candidates}}, result.value("error").toString());
            });
            request->searchMedia({{"url", m_sharedMetadata->privateUrlForInvocation(invocation)},
                {"identity", context.value("identity")}, {"skipHash", context.value("manual").toBool()}}, context.value("language").toString());
        }, [this](const QString& invocation) {
            const auto request = m_nativeSearches.take(invocation);
            if (request) { request->cancelSearch(); request->deleteLater(); }
        });
}

void SubtitleSourceService::setStorageDirectory(const QString& directory)
{
    cancelSearch(); cancelPreparation();
    ++m_languagesGeneration;
    if (m_languagesReply) m_languagesReply->abort();
    m_languagesReply = nullptr; m_languagesLoaded = false;
    m_storageDirectory = QFileInfo(directory).isAbsolute() ? directory : QString();
    m_apiKey = m_options.consumerApiKey; m_customKey = false;
    if (!m_storageDirectory.isEmpty()) {
        QSettings settings(QDir(directory).filePath(QStringLiteral("source-settings.ini")), QSettings::IniFormat);
        const auto key = settings.value(QStringLiteral("OpenSubtitles/api-key")).toByteArray();
        const bool valid = !key.isEmpty() && key.size() <= 512 && std::all_of(key.cbegin(), key.cend(), [](unsigned char ch) { return ch >= 0x21 && ch <= 0x7e; });
        if (valid) { m_apiKey = key; m_customKey = true; }
    }
    if (m_builtin) m_builtin->setStorageDirectory(m_storageDirectory);
    refreshBuiltinRegistration();
    emit configurationChanged();
}

bool SubtitleSourceService::setApiKey(const QString& value)
{
    const auto text = value.trimmed();
    const auto key = text.toLatin1();
    if (text != QString::fromLatin1(key) || key.isEmpty() || key.size() > 512 || m_storageDirectory.isEmpty() || m_preparing) return false;
    for (const char ch : key) if (static_cast<unsigned char>(ch) < 0x21 || ch > 0x7e) return false;
    if (!QDir().mkpath(m_storageDirectory)) return false;
    const auto path = QDir(m_storageDirectory).filePath(QStringLiteral("source-settings.ini"));
    // Create the configuration with owner permissions before QSettings writes the key.
    if (!QFileInfo::exists(path)) {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return false;
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        if (!file.commit()) return false;
    }
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(QStringLiteral("OpenSubtitles/api-key"), key); settings.sync();
    if (settings.status() != QSettings::NoError) return false;
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    cancelSearch(); cancelPreparation(); ++m_languagesGeneration;
    if (m_languagesReply) m_languagesReply->abort();
    m_languagesReply = nullptr; m_languagesLoaded = false; m_apiKey = key; m_customKey = true;
    if (m_builtin) m_builtin->setStorageDirectory(m_storageDirectory);
    refreshBuiltinRegistration(); emit configurationChanged(); return true;
}

bool SubtitleSourceService::clearApiKey()
{
    if (m_storageDirectory.isEmpty() || m_preparing) return false;
    QSettings settings(QDir(m_storageDirectory).filePath(QStringLiteral("source-settings.ini")), QSettings::IniFormat);
    settings.remove(QStringLiteral("OpenSubtitles/api-key")); settings.sync();
    if (settings.status() != QSettings::NoError) return false;
    cancelSearch(); ++m_languagesGeneration;
    if (m_languagesReply) m_languagesReply->abort();
    m_languagesReply = nullptr; m_languagesLoaded = false; m_apiKey = m_options.consumerApiKey; m_customKey = false;
    if (m_builtin) m_builtin->setStorageDirectory(m_storageDirectory);
    refreshBuiltinRegistration();
    emit configurationChanged(); return true;
}

QString SubtitleSourceService::networkError(int status, QNetworkReply::NetworkError error, bool api) const
{
    if (status == 401 || status == 403) return api ? (m_customKey
        ? tr("OpenSubtitles refused this request. Make sure that the key and download settings are correct.")
        : tr("OpenSubtitles refused this request. Try again later, or add your own key in setup."))
        : tr("The subtitle server refused this link. Make sure that the link is correct. Try again.");
    if (status == 406 || status == 429) return api ? tr("OpenSubtitles cannot download more files now. Try again later.")
        : tr("The subtitle server cannot download more files now. Try again later.");
    if (status >= 400) return tr("The subtitle server could not complete this request. Try again.");
    if (error != QNetworkReply::NoError) return tr("Hakone cannot connect to the subtitle server. Try again.");
    if (status >= 300) return tr("The subtitle server returned an invalid response. Try again.");
    return {};
}

QNetworkReply* SubtitleSourceService::request(const QUrl& url, const QByteArray& body,
                                              bool api, qint64 limit, ReplyCallback callback)
{
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Hakone v" PACKAGE_VERSION);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
        api ? QNetworkRequest::SameOriginRedirectPolicy : QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(5);
    if (api) {
        request.setRawHeader("Api-Key", m_apiKey);
        request.setRawHeader("Accept", "application/json");
        if (!body.isEmpty()) request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    }
    auto* reply = body.isEmpty() ? m_network.get(request) : m_network.post(request, body);
    struct State { QByteArray bytes; bool tooLarge = false; bool timedOut = false; };
    auto state = std::make_shared<State>();
    auto* timer = new QTimer(reply); timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [reply, state] { state->timedOut = true; reply->abort(); });
    connect(reply, &QNetworkReply::readyRead, this, [reply, state, limit] {
        state->bytes.append(reply->readAll());
        if (state->bytes.size() > limit) { state->tooLarge = true; reply->abort(); }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, state, timer, limit, api, callback = std::move(callback)] {
        timer->stop(); if (reply->isReadable()) state->bytes.append(reply->readAll());
        const auto error = state->tooLarge || state->bytes.size() > limit ? tr("This subtitle response is too large.")
            : state->timedOut ? tr("The subtitle request took too long. Try again.")
            : networkError(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), reply->error(), api);
        reply->deleteLater(); callback(state->bytes, error);
    });
    timer->start(m_options.timeoutMs); return reply;
}

void SubtitleSourceService::cancelSearch()
{
    ++m_searchGeneration;
    const auto providerSearch = m_providerSearch;
    m_providerSearch.clear();
    if (m_sharedMetadata && !providerSearch.isEmpty()) m_sharedMetadata->cancel(providerSearch);
    if (m_metadata && !providerSearch.isEmpty()) m_metadata->cancel(providerSearch);
    if (m_hashJob) m_hashJob->requestInterruption();
    m_hashJob = nullptr;
    m_searchTargets.clear(); m_searchSources.clear(); m_targetSources.clear(); m_searchIdentities.clear();
    if (m_searchReply) m_searchReply->abort();
    m_searchReply = nullptr; m_targetIndex = 0;
    emit searchProgressChanged();
}

QVariantList SubtitleSourceService::parseSources(const QByteArray& bytes, QString& error)
{
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject() || !document.object().value("data").isArray()) {
        error = tr("OpenSubtitles returned an invalid response. Try again."); return {};
    }
    QVariantList sources; QSet<qint64> seen;
    for (const auto& entry : document.object().value("data").toArray()) {
        const auto attributes = entry.toObject().value("attributes").toObject();
        for (const auto& value : attributes.value("files").toArray()) {
            const auto file = value.toObject();
            const auto id = file.value("file_id").toInteger();
            if (id <= 0 || seen.contains(id)) continue;
            const auto name = safeName(file.value("file_name").toString(attributes.value("release").toString()));
            const auto url = sourceUrl(id, name); seen.insert(id); m_records.insert(url, {id, name});
            const QFileInfo cached(cachePath({id, name}));
            const auto available = cached.isFile() && cached.size() > 0 && cached.size() <= m_options.maxFileBytes
                ? QUrl::fromLocalFile(cached.absoluteFilePath()).toString(QUrl::FullyEncoded) : url;
            QVariantMap source{{"url", available}, {"name", name}, {"fileId", id},
                {"filename", file.value("file_name").toString()},
                {"feature", attributes.value("feature_details").toObject().toVariantMap()},
                {"hashMatched", attributes.value("moviehash_match").toBool()},
                {"fps", attributes.value("fps").toDouble()},
                {"downloads", attributes.value("download_count").toDouble()},
                {"rating", attributes.value("ratings").toDouble()}, {"votes", attributes.value("votes").toInt()},
                {"trusted", attributes.value("from_trusted").toBool()},
                {"language", attributes.value("language").toString()},
                {"release", attributes.value("release").toString()},
                {"hearingImpaired", attributes.value("hearing_impaired").toBool()},
                {"forced", attributes.value("foreign_parts_only").toBool()}};
            auto identity = SubtitleMatching::identity(source);
            if (source.value("fps").toDouble() > 0) identity.release.insert("fps", QString::number(source.value("fps").toDouble()));
            source.insert("identity", MediaIdentity::toMap(identity)); sources.append(source);
            if (sources.size() >= 500) return sources;
        }
    }
    return sources;
}

void SubtitleSourceService::fetchLanguages()
{
    if (m_addonManagerRetired) return;
    if (m_apiKey.isEmpty() || m_languagesLoaded || m_languagesReply) return;
    if (m_addonManager && !providerAvailable(builtinSubtitles)) { m_languagesWanted = true; return; }
    m_languagesWanted = false;
    const auto generation = ++m_languagesGeneration;
    m_languagesReply = request(m_options.apiBase.resolved(QUrl(QStringLiteral("infos/languages"))), {}, true,
        m_options.maxJsonBytes, [this, generation](const QByteArray& bytes, const QString& error) {
            if (generation != m_languagesGeneration) return;
            m_languagesReply = nullptr;
            if (!error.isEmpty()) return;
            const auto data = QJsonDocument::fromJson(bytes).object().value("data").toArray();
            QVariantList languages; QSet<QString> seen;
            const QRegularExpression code(QStringLiteral("^[a-z]{2,3}(?:-[a-z]{2,3})?$"));
            for (const auto& value : data) {
                const auto item = value.toObject();
                const auto key = item.value("language_code").toString();
                const auto name = item.value("language_name").toString();
                if (seen.contains(key) || name.isEmpty() || !code.match(key).hasMatch()) continue;
                seen.insert(key); languages.append(QVariantMap{{"code", key}, {"text", name}});
            }
            if (languages.isEmpty()) return;
            std::sort(languages.begin(), languages.end(), [](const QVariant& a, const QVariant& b) {
                return QString::localeAwareCompare(a.toMap().value("text").toString(), b.toMap().value("text").toString()) < 0;
            });
            m_languages = languages; m_languagesLoaded = true; emit languagesChanged();
        });
}

QVariantMap SubtitleSourceService::identifyMedia(const QUrl& url, const QVariantMap& metadata) const
{
    return MediaIdentity::toMap(MediaIdentity::identify(url, metadata));
}

QVariantMap SubtitleSourceService::parseMediaName(const QString& name) const
{
    return MediaIdentity::toMap(MediaIdentity::parse(name));
}

QVariantList SubtitleSourceService::rankSources(const QVariantMap& identity, const QVariantList& sources,
                                               const QString& mediaUrl, const QString& language) const
{
    return SubtitleMatching::rank(MediaIdentity::fromMap(identity), sources, mediaUrl, language);
}

quint64 SubtitleSourceService::search(const QString& text, const QString& language)
{
    return searchMedia({{"query", text}, {"manual", true}}, language);
}

quint64 SubtitleSourceService::searchMedia(const QVariantMap& input, const QString& language)
{
    cancelSearch(); const auto generation = m_searchGeneration;
    static const QRegularExpression code(QStringLiteral("^[a-z]{2,3}(?:-[a-z]{2,3})?$"));
    const auto query = input.value("query").toString().trimmed();
    auto targets = input.value("targets").toList();
    if (targets.isEmpty()) targets.append(input);
    const bool manual = input.value("manual").toBool();
    if (manual && !input.value("anchor").toUrl().isEmpty()) {
        const auto anchor = input.value("anchor").toUrl();
        const auto selected = std::find_if(targets.cbegin(), targets.cend(), [&anchor](const QVariant& value) {
            return value.toMap().value("url").toUrl() == anchor;
        });
        // A correction belongs to the edited file, not whichever batch row sorts first.
        targets = selected != targets.cend() ? QVariantList{*selected} : QVariantList{QVariantMap{{"url", anchor}}};
    }
    QString error;
    if (!configuredForExecution()) error = m_addonManager ? tr("Set up a subtitle provider in Add-on settings.")
        : tr("OpenSubtitles is not set up. Add an API key to use this source.");
    else if (query.size() > 512 || (!language.isEmpty() && !code.match(language).hasMatch())) error = tr("Enter a title and select a subtitle language.");
    else if (!input.value("details").toMap().value("airDate").toString().isEmpty()
             && !QDate::fromString(input.value("details").toMap().value("airDate").toString(), Qt::ISODate).isValid())
        error = tr("Enter a valid air date in YYYY-MM-DD format.");
    else if (!manual && targets.size() > 64) error = tr("Search up to 64 files at a time. Select a smaller folder or search one file.");
    else {
        QSet<QString> seen;
        for (const auto& value : targets) {
            const auto row = value.toMap(); const auto url = row.value("url").toUrl();
            auto identity = row.contains("identity") ? MediaIdentity::fromMap(row.value("identity").toMap())
                : MediaIdentity::identify(url, row.value("metadata").toMap());
            if (manual) {
                const auto release = identity.release;
                identity = MediaIdentity::parse(query); identity.release = release;
                for (auto it = identity.provenance.begin(); it != identity.provenance.end(); ++it) it.value() = "user";
                const auto details = input.value("details").toMap();
                MediaIdentity::applyMetadata(identity, details, "user");
                if (details.contains("year") && details.value("year").toInt() < 0) identity.year = -1;
                if (details.contains("season") && details.value("season").toInt() < 0) identity.season = -1;
                if (details.contains("episodes") && details.value("episodes").toList().isEmpty()) identity.episodes.clear();
                // User correction starts a new identity; never reuse the original file's IDs or hash.
            }
            const auto key = url.toString(QUrl::FullyEncoded);
            if (!key.isEmpty() && seen.contains(key)) continue;
            seen.insert(key);
            m_searchTargets.append({identity, url, !manual && !input.value("skipHash").toBool()});
            m_searchIdentities.insert(key, MediaIdentity::toMap(identity));
            if (manual) break; // A manual query is one explicit search, including in folder mode.
        }
        if (m_searchTargets.isEmpty() || (manual && m_searchTargets.first().identity.title.isEmpty()
            && m_searchTargets.first().identity.ids.isEmpty())) error = tr("Enter the movie or series title to search for subtitles.");
    }
    m_searchLanguage = language; m_searchAllProviders = input.value("allProviders").toBool();
    m_searchWarning.clear(); m_targetIndex = 0;
    emit searchProgressChanged();
    QTimer::singleShot(0, this, [this, generation, error] {
        if (generation != m_searchGeneration) return;
        if (!error.isEmpty()) finishSearch(generation, error);
        else beginSearchTarget(generation);
    });
    return generation;
}

void SubtitleSourceService::beginSearchTarget(quint64 generation)
{
    if (generation != m_searchGeneration) return;
    if (m_targetIndex >= m_searchTargets.size()) { finishSearch(generation); return; }
    m_targetSources.clear(); m_searchFingerprint = {}; m_searchPhase = 1; m_searchPage = 1;
    if (m_sharedMetadata || (!m_ownedMetadataProfile && m_metadata)) { beginProviderSearch(generation); return; }
    if (m_ownedMetadataProfile) { finishSearch(generation, tr("The metadata service is not available.")); return; }
    const auto& target = m_searchTargets[m_targetIndex];
    if (target.useHash && target.url.isLocalFile()) {
        auto* job = new HashJob(target.url); m_hashJob = job;
        connect(job, &QThread::finished, this, [this, generation, job] {
            if (generation != m_searchGeneration) return;
            m_hashJob = nullptr; m_searchFingerprint = job->fingerprint;
            if (!m_searchFingerprint.hash.isEmpty()) m_searchPhase = 0;
            requestSearchPage(generation);
        });
        connect(job, &QThread::finished, job, &QObject::deleteLater);
        job->start(QThread::LowPriority);
    } else requestSearchPage(generation);
}

void SubtitleSourceService::beginProviderSearch(quint64 generation)
{
    if (!m_addonManager || (!m_metadata && !m_sharedMetadata) || generation != m_searchGeneration || m_targetIndex >= m_searchTargets.size()) return;
    const auto& target = m_searchTargets[m_targetIndex];
    if (m_metadata) m_metadata->setProviderOrder(QStringLiteral("subtitles"), m_addonManager->providerOrder(QStringLiteral("subtitles")));
    const auto identity = MediaIdentity::toMap(target.identity);
    QVariantMap context{{"identity", identity}, {"language", m_searchLanguage}, {"query", target.identity.title},
        {"manual", !target.useHash}, {"interactive", true}, {"generation", QVariant::fromValue(generation)},
        {"timeoutMs", m_options.timeoutMs}};
    if (m_sharedMetadata) {
        const auto media = m_sharedMetadata->acquireMedia(target.url.isEmpty() ? QUrl(QStringLiteral("metadata-query:/")) : target.url, identity);
        if (media.isEmpty()) { finishSearch(generation, tr("The metadata request limit has been reached.")); return; }
        context.insert(QStringLiteral("media"), media);
        m_providerSearch = m_sharedMetadata->searchAssets(QStringLiteral("subtitles"), context, m_searchAllProviders);
        m_sharedMetadata->releaseMedia(media);
    } else m_providerSearch = m_metadata->searchAssets(QStringLiteral("subtitles"), context, m_searchAllProviders);
}

void SubtitleSourceService::finishProviderSearch(const QString& consumer, const QString& status,
                                                const QVariantMap& data, const QString& error)
{
    if (consumer != m_providerSearch || data.value("contextGeneration").toULongLong() != m_searchGeneration
        || m_targetIndex >= m_searchTargets.size()) return;
    m_providerSearch.clear();
    if (m_languagesWanted) fetchLanguages();
    const auto generation = m_searchGeneration;
    for (const auto& value : data.value("candidates").toList()) {
        const auto candidate = value.toMap();
        const auto contribution = candidate.value("provider").toString();
        if (contribution.isEmpty() || !providerAvailable(contribution)) continue;
        if (m_addonRecords.size() >= std::clamp(m_options.maxAddonRecords, 1, 8192)) {
            m_searchWarning = tr("The subtitle result limit has been reached. Save the selected tracks. Close this window. Open it again before another search.");
            break;
        }
        auto source = candidate;
        auto format = candidate.value("format", QStringLiteral("srt")).toString().toLower();
        static const QRegularExpression extension(QStringLiteral("^[a-z0-9]{1,8}$"));
        if (!extension.match(format).hasMatch()) format = QStringLiteral("srt");
        const auto name = safeName(candidate.value("name").toString(), format);
        QUrl reference; reference.setScheme(QStringLiteral("addon-subtitle"));
        reference.setPath('/' + QUuid::createUuid().toString(QUuid::WithoutBraces) + '/' + name);
        const auto key = reference.toString(QUrl::FullyEncoded);
        m_addonRecords.insert(key, {contribution, candidate,
            contribution == builtinSubtitles ? sourceUrl(candidate.value("fileId").toLongLong(), candidate.value("name").toString()) : QString{},
            candidate.value("provenance").toMap().value("cachePartition").toString()});
        auto entity = candidate.value("identity").toMap();
        if (!entity.contains("type")) entity.insert("type", entity.value("kind"));
        auto identity = MediaIdentity::fromMap(entity);
        if (candidate.value("fps").toDouble() > 0) identity.release.insert("fps", QString::number(candidate.value("fps").toDouble()));
        source.insert("identity", MediaIdentity::toMap(identity));
        source.insert("url", key); source.insert("name", name); source.insert("kind", QStringLiteral("addon"));
        source.insert("release", candidate.value("releaseName", candidate.value("release", candidate.value("name"))));
        // Only the trusted native adapter can bind an actual hash observation
        // to a local target URL. Guest hash claims never create hashFor entries.
        if (contribution != builtinSubtitles) source.remove("hashFor");
        m_targetSources.append(source);
    }
    if (status != QLatin1String("match") && status != QLatin1String("partial") && status != QLatin1String("no_match")) {
        const auto failure = error.isEmpty() ? tr("The subtitle search failed. Make sure that the provider settings are correct. Try again.") : error;
        finishSearch(generation, failure); return;
    }
    finishSearchTarget(generation);
}

bool SubtitleSourceService::providerAvailable(const QString& contribution) const
{
    if (m_addonManagerRetired || !m_addonManager) return false;
    if (contribution == builtinSubtitles) {
        const auto provider = m_addonManager->builtinMetadataProvider(contribution);
        return provider.value("enabled").toBool() && provider.value("configured").toBool();
    }
    // Display filtering is advisory. Downloads validate their exact release,
    // account, grants and current configuration again in the asset broker.
    if (!m_providerDisplayMode) return true;
    for (const auto& value : displayProviders()) {
        const auto provider = value.toMap();
        if (provider.value("id").toString() == contribution)
            return provider.value("enabled").toBool() && provider.value("configured").toBool();
    }
    return false;
}

void SubtitleSourceService::requestSearchPage(quint64 generation)
{
    if (generation != m_searchGeneration) return;
    const auto& target = m_searchTargets[m_targetIndex]; const auto& identity = target.identity;
    QUrlQuery parameters;
    if (!m_searchLanguage.isEmpty()) parameters.addQueryItem(QStringLiteral("languages"), m_searchLanguage);
    if (m_searchPhase == 0) {
        parameters.addQueryItem(QStringLiteral("moviehash"), m_searchFingerprint.hash);
        parameters.addQueryItem(QStringLiteral("moviebytesize"), QString::number(m_searchFingerprint.size));
    } else {
        const QMap<QString, QString> ids{{"imdb", "imdb_id"}, {"seriesImdb", "parent_imdb_id"},
            {"tmdbMovie", "tmdb_id"}, {"tmdbEpisode", "tmdb_id"}, {"seriesTmdb", "parent_tmdb_id"}};
        bool hasId = false;
        for (auto it = ids.cbegin(); it != ids.cend(); ++it) {
            if (!identity.ids.contains(it.key())) continue;
            auto id = identity.ids.value(it.key()); if (id.startsWith("tt")) id.remove(0, 2);
            parameters.addQueryItem(it.value(), id); hasId = true;
        }
        if (!hasId && identity.title.isEmpty()) { finishSearchTarget(generation); return; }
        if (!hasId) parameters.addQueryItem(QStringLiteral("query"), identity.title.toLower());
        if (identity.kind != MediaIdentity::Kind::Unknown)
            parameters.addQueryItem(QStringLiteral("type"), identity.kind == MediaIdentity::Kind::Movie ? "movie" : "episode");
        if (identity.year >= 0 && m_searchPhase == 1) parameters.addQueryItem(QStringLiteral("year"), QString::number(identity.year));
        if (identity.season >= 0) parameters.addQueryItem(QStringLiteral("season_number"), QString::number(identity.season));
        if (identity.episodes.size() == 1) parameters.addQueryItem(QStringLiteral("episode_number"), QString::number(identity.episodes.first()));
    }
    if (m_searchPage > 1) parameters.addQueryItem(QStringLiteral("page"), QString::number(m_searchPage));
    auto url = m_options.apiBase.resolved(QUrl(QStringLiteral("subtitles"))); url.setQuery(parameters);
    m_searchReply = request(url, {}, true, m_options.maxJsonBytes,
        [this, generation](const QByteArray& bytes, QString error) {
            if (generation != m_searchGeneration) return;
            m_searchReply = nullptr;
            auto sources = error.isEmpty() ? parseSources(bytes, error) : QVariantList{};
            if (!error.isEmpty()) { mergeSources(m_searchSources, m_targetSources); finishSearch(generation, error); return; }
            const auto key = m_searchTargets[m_targetIndex].url.toString(QUrl::FullyEncoded);
            for (auto& value : sources) {
                auto source = value.toMap();
                source.insert("hashFor", m_searchPhase == 0 && source.value("hashMatched").toBool() ? QStringList{key} : QStringList{});
                value = source;
            }
            mergeSources(m_targetSources, sources);
            const int pages = QJsonDocument::fromJson(bytes).object().value("total_pages").toInt(1);
            if (pages > m_searchPage && m_searchPage < 3) { ++m_searchPage; requestSearchPage(generation); return; }
            if (pages > m_searchPage) m_searchWarning = tr("More subtitles are available. Refine the title or language to narrow the results.");
            finishSearchPhase(generation);
        });
}

void SubtitleSourceService::finishSearchPhase(quint64 generation)
{
    if (generation != m_searchGeneration) return;
    const auto& target = m_searchTargets[m_targetIndex];
    if (m_searchPhase == 0) {
        const auto key = target.url.toString(QUrl::FullyEncoded);
        const bool found = std::any_of(m_targetSources.cbegin(), m_targetSources.cend(), [&key](const QVariant& value) {
            return value.toMap().value("hashFor").toStringList().contains(key);
        });
        if (!found) { m_searchPhase = 1; m_searchPage = 1; requestSearchPage(generation); return; }
    } else if (m_searchPhase == 1 && target.identity.year >= 0 && target.identity.ids.isEmpty()) {
        const auto provenance = target.identity.provenance.value("year");
        const auto ranked = SubtitleMatching::rank(target.identity, m_targetSources, target.url.toString(QUrl::FullyEncoded), m_searchLanguage);
        const bool found = std::any_of(ranked.cbegin(), ranked.cend(), [](const QVariant& value) { return value.toMap().value("matchScore").toInt() > 0; });
        if (!found && (provenance == "filename" || provenance == "folder")) {
            m_searchPhase = 2; m_searchPage = 1; requestSearchPage(generation); return;
        }
    }
    finishSearchTarget(generation);
}

void SubtitleSourceService::finishSearchTarget(quint64 generation)
{
    if (generation != m_searchGeneration) return;
    mergeSources(m_searchSources, m_targetSources);
    ++m_targetIndex;
    emit searchProgressChanged();
    if (m_searchSources.size() >= 4000) {
        m_searchWarning = tr("Many subtitles were found. Use a more specific title or language before another search.");
        finishSearch(generation); return;
    }
    QTimer::singleShot(0, this, [this, generation] { beginSearchTarget(generation); });
}

void SubtitleSourceService::finishSearch(quint64 generation, const QString& error)
{
    if (generation != m_searchGeneration) return;
    QVariantList ranked = m_searchSources;
    if (!m_searchTargets.isEmpty()) ranked = SubtitleMatching::rank(m_searchTargets.first().identity,
        ranked, m_searchTargets.first().url.toString(QUrl::FullyEncoded), m_searchLanguage);
    const bool success = error.isEmpty() || !ranked.isEmpty();
    emit searchFinished(generation, {{"success", success}, {"error", success ? QString() : error},
        {"warning", !error.isEmpty() && success ? error : m_searchWarning}, {"sources", ranked},
        {"identities", m_searchIdentities}});
}

QString SubtitleSourceService::cachePath(const Record& record) const
{
    return QDir(m_storageDirectory).filePath(QStringLiteral("opensubtitles/%1/%2").arg(record.id).arg(record.name));
}

QByteArray SubtitleSourceService::subtitleText(const QByteArray& bytes) const
{
    const auto encoding = QStringConverter::encodingForData(bytes).value_or(QStringConverter::Utf8);
    QStringDecoder decode(encoding); const QString text = decode(bytes);
    if (decode.hasError() || text.contains(QChar::Null)) return {};
    static const QRegularExpression timing(QStringLiteral(
        "^\\s*\\d{1,3}:[0-5]\\d:[0-5]\\d[,.]\\d{3}\\s*-->\\s*\\d{1,3}:[0-5]\\d:[0-5]\\d[,.]\\d{3}"),
        QRegularExpression::MultilineOption);
    if (!timing.match(text).hasMatch()) return {};
    return text.toUtf8();
}

bool SubtitleSourceService::downloadUrlAllowed(const QUrl& url) const
{
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty()) return false;
    if (url.scheme() == "https") return true;
    // A local HTTP endpoint lets the same client run against a controlled test server.
    return m_options.apiBase.scheme() == "http" && url.scheme() == "http"
        && url.host() == m_options.apiBase.host() && url.port() == m_options.apiBase.port()
        && (url.host() == "127.0.0.1" || url.host() == "localhost" || url.host() == "::1");
}

void SubtitleSourceService::cancelPreparation()
{
    ++m_prepareGeneration;
    if (m_addonManager) {
        if (!m_providerResolve.isEmpty()) m_addonManager->cancel(m_providerResolve);
        if (m_providerAsset) m_addonManager->cancelAsset(m_providerAsset);
    }
    m_providerResolve.clear(); m_providerAsset = 0; m_providerResolveSource.clear();
    if (m_builtin && m_builtinPreparation) m_builtin->cancelPreparation();
    m_builtinPreparation = 0; m_builtinPrepareSource.clear();
    if (m_downloadReply) m_downloadReply->abort();
    m_downloadReply = nullptr;
    const bool preparing = m_preparing; m_preparing = false;
    m_pending.clear(); m_rows.clear(); m_ready.clear();
    if (preparing) emit preparingChanged();
}

void SubtitleSourceService::resetSession()
{
    cancelSearch(); cancelPreparation(); m_records.clear(); m_links.clear(); m_addonRecords.clear(); ++m_languagesGeneration;
    if (m_builtin) m_builtin->resetSession();
    if (m_languagesReply) m_languagesReply->abort();
    m_languagesReply = nullptr;
}

quint64 SubtitleSourceService::prepareRows(const QVariantList& rows)
{
    cancelPreparation(); const auto generation = m_prepareGeneration;
    m_rows = rows; m_preparing = true; emit preparingChanged();
    for (const auto& item : rows) for (const auto& value : item.toMap().value("tracks").toList()) {
        const auto url = value.toUrl();
        if ((url.scheme() == "opensubtitles" || url.scheme() == "subtitlelink" || url.scheme() == "addon-subtitle")
            && !m_pending.contains(url.toString(QUrl::FullyEncoded)))
            m_pending.append(url.toString(QUrl::FullyEncoded));
    }
    QTimer::singleShot(0, this, [this, generation] { downloadNext(generation); });
    return generation;
}

void SubtitleSourceService::downloadNext(quint64 generation)
{
    if (generation != m_prepareGeneration || !m_preparing) return;
    if (m_pending.isEmpty()) { finishPreparation(generation); return; }
    const auto source = m_pending.first();
    if (QUrl(source).scheme() == QLatin1String("addon-subtitle")) {
        const auto record = m_addonRecords.constFind(source);
        if (record == m_addonRecords.cend() || !providerAvailable(record->contribution) || m_storageDirectory.isEmpty()) {
            finishPreparation(generation, tr("Search for these subtitles again before Save.")); return;
        }
        downloadAddon(generation, source, *record); return;
    }
    const auto link = m_links.constFind(source);
    if (link != m_links.cend()) { downloadLink(generation, source, *link); return; }
    const auto record = m_records.constFind(source);
    if (record == m_records.cend() || m_apiKey.isEmpty() || m_storageDirectory.isEmpty()) {
        finishPreparation(generation, tr("Search for these subtitles again before Save.")); return;
    }
    const auto path = cachePath(*record);
    QFile cached(path);
    if (cached.size() <= m_options.maxFileBytes && cached.open(QIODevice::ReadOnly) && !subtitleText(cached.readAll()).isEmpty()) {
        m_ready.insert(source, QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded));
        m_pending.removeFirst(); QTimer::singleShot(0, this, [this, generation] { downloadNext(generation); }); return;
    }
    const auto body = QJsonDocument(QJsonObject{{"file_id", record->id}, {"sub_format", "srt"}}).toJson(QJsonDocument::Compact);
    m_downloadReply = request(m_options.apiBase.resolved(QUrl(QStringLiteral("download"))), body, true,
        m_options.maxJsonBytes, [this, generation, source, path](const QByteArray& bytes, QString error) {
            if (generation != m_prepareGeneration) return;
            if (!error.isEmpty()) { finishPreparation(generation, error); return; }
            const auto document = QJsonDocument::fromJson(bytes);
            const QUrl link(document.object().value("link").toString());
            if (!downloadUrlAllowed(link)) { finishPreparation(generation, tr("The subtitle server returned an invalid download link.")); return; }
            // The temporary file link receives no API key or account credentials.
            m_downloadReply = request(link, {}, false, m_options.maxFileBytes,
                [this, generation, source, path](const QByteArray& data, QString error) {
                    if (generation != m_prepareGeneration) return;
                    if (!error.isEmpty()) { finishPreparation(generation, error); return; }
                    const auto text = subtitleText(data);
                    if (text.isEmpty()) { finishPreparation(generation, tr("This download does not contain valid subtitles.")); return; }
                    if (!writeSubtitle(path, text)) {
                        finishPreparation(generation, tr("Hakone cannot save these subtitles.")); return;
                    }
                    m_ready.insert(source, QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)); m_pending.removeFirst();
                    QTimer::singleShot(1000, this, [this, generation] { downloadNext(generation); });
                });
        });
}

QString SubtitleSourceService::addonCachePath(const AddonRecord& record, const QString& format) const
{
    const QString identity = record.contribution + '\n' + record.partition + '\n' + record.candidate.value("id").toString();
    const auto key = QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex());
    return QDir(m_storageDirectory).filePath(QStringLiteral("providers/%1/%2").arg(key,
        safeName(record.candidate.value("name").toString(), format)));
}

void SubtitleSourceService::downloadAddon(quint64 generation, const QString& source, const AddonRecord& record)
{
    if (record.contribution == builtinSubtitles) {
        if (!m_builtin || record.nativeUrl.isEmpty()) { finishPreparation(generation, tr("Search for these subtitles again before Save.")); return; }
        m_builtinPrepareSource = source;
        m_builtinPrepareGeneration = generation;
        m_builtinPreparation = m_builtin->prepareRows({QVariantMap{{"tracks", QStringList{record.nativeUrl}}}});
        return;
    }
    for (const auto& format : {"srt", "vtt", "ass", "ssa", "sub", "smi", "ttml", "usf"}) {
        const auto path = addonCachePath(record, QLatin1String(format));
        QFile cached(path);
        if (cached.size() <= m_options.maxFileBytes && cached.open(QIODevice::ReadOnly)
            && linkFormat(cached.readAll()) == QLatin1String(format)) {
            m_ready.insert(source, QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded));
            m_pending.removeFirst();
            QTimer::singleShot(0, this, [this, generation] { downloadNext(generation); }); return;
        }
    }
    if (!m_addonManager) { finishPreparation(generation, tr("The subtitle provider is unavailable.")); return; }
    m_providerResolveSource = source;
    m_providerResolveGeneration = generation;
    m_providerResolve = m_addonManager->invoke(record.contribution, QStringLiteral("resolveAsset"), {{"candidate", record.candidate}});
}

bool SubtitleSourceService::writeSubtitle(const QString& path, const QByteArray& bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) return false;
    output.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    return output.write(bytes) == bytes.size() && output.commit();
}

QVariantMap SubtitleSourceService::addLinks(const QStringList& values)
{
    QVariantList sources; QSet<QString> seen; int skipped = 0;
    for (const auto& value : values) {
        const auto text = value.trimmed();
        QUrl url(text, QUrl::StrictMode); url.setFragment({});
        if (text.size() > 8192 || !url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty()
            || (url.scheme() != "https" && url.scheme() != "http")) { ++skipped; continue; }
        const auto key = QString::fromLatin1(QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Sha256).toHex());
        if (seen.contains(key)) continue;
        seen.insert(key);
        auto suffix = QFileInfo(url.fileName()).suffix().toLower();
        static const QRegularExpression extension(QStringLiteral("^[a-z0-9]{1,16}$"));
        if (!extension.match(suffix).hasMatch()) suffix = QStringLiteral("srt");
        const auto name = safeName(url.fileName(), suffix);
        QUrl source; source.setScheme(QStringLiteral("subtitlelink")); source.setPath(QLatin1Char('/') + key + QLatin1Char('/') + name);
        const auto id = source.toString(QUrl::FullyEncoded);
        const Link link{url, name, key}; m_links.insert(id, link);
        QString available = id, availableName = name;
        for (const auto& format : {"srt", "vtt", "ass", "ssa", "sub", "smi", "ttml", "usf"}) {
            const QFileInfo cached(linkCachePath(link, QLatin1String(format)));
            if (!m_storageDirectory.isEmpty() && cached.isFile() && cached.size() > 0 && cached.size() <= m_options.maxFileBytes) {
                available = QUrl::fromLocalFile(cached.absoluteFilePath()).toString(QUrl::FullyEncoded); availableName = cached.fileName(); break;
            }
        }
        sources.append(QVariantMap{{"url", available}, {"name", availableName}, {"kind", "link"}});
    }
    return {{"success", !sources.isEmpty()}, {"sources", sources}, {"error", skipped || sources.isEmpty()
        ? tr("Use direct subtitle file links that start with http:// or https://.") : QString()}};
}

QString SubtitleSourceService::linkCachePath(const Link& link, const QString& format) const
{
    return QDir(m_storageDirectory).filePath(QStringLiteral("links/%1/%2").arg(link.key, safeName(link.name, format)));
}

QString SubtitleSourceService::linkFormat(const QByteArray& bytes) const
{
    if (bytes.isEmpty()) return {};
    const auto encoding = QStringConverter::encodingForData(bytes).value_or(QStringConverter::Utf8);
    QStringDecoder decode(encoding); QString text = decode(bytes);
    // Keep the original bytes so that VLC can use the selected legacy text encoding.
    if (decode.hasError()) {
        if (encoding != QStringConverter::Utf8) return {};
        text = QString::fromLatin1(bytes);
    }
    if (text.contains(QChar::Null)) return {};
    const auto trimmed = text.trimmed();
    static const QRegularExpression html(QStringLiteral("<(?:!doctype\\s+html|html)\\b"), QRegularExpression::CaseInsensitiveOption);
    if (html.match(trimmed.left(4096)).hasMatch() || trimmed.startsWith(QLatin1String("<head"), Qt::CaseInsensitive)
        || trimmed.startsWith(QLatin1String("<body"), Qt::CaseInsensitive)) return {};
    static const QRegularExpression vtt(QStringLiteral("^(?:\\d+:)?\\d{2,}:[0-5]\\d\\.\\d{3}\\s+-->\\s+(?:\\d+:)?\\d{2,}:[0-5]\\d\\.\\d{3}"), QRegularExpression::MultilineOption);
    if (trimmed.startsWith(QLatin1String("WEBVTT")) && vtt.match(text).hasMatch()) return QStringLiteral("vtt");
    if (!subtitleText(bytes).isEmpty()) return QStringLiteral("srt");
    // ASCII timing and section markers remain readable in legacy encodings.
    static const QRegularExpression srt(QStringLiteral("^\\s*\\d{1,3}:[0-5]\\d:[0-5]\\d,\\d{3}\\s*-->\\s*\\d{1,3}:[0-5]\\d:[0-5]\\d,\\d{3}"), QRegularExpression::MultilineOption);
    if (srt.match(text).hasMatch()) return QStringLiteral("srt");
    static const QRegularExpression dialogue(QStringLiteral("^Dialogue\\s*:"), QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    if (text.contains(QLatin1String("[Events]"), Qt::CaseInsensitive) && dialogue.match(text).hasMatch())
        return text.contains(QLatin1String("[V4 Styles]"), Qt::CaseInsensitive) ? QStringLiteral("ssa") : QStringLiteral("ass");
    static const QRegularExpression micro(QStringLiteral("^\\{\\d+\\}\\{\\d+\\}.+"), QRegularExpression::MultilineOption);
    static const QRegularExpression viewer(QStringLiteral("^\\d{2}:[0-5]\\d:[0-5]\\d[,.]\\d{2},\\d{2}:[0-5]\\d:[0-5]\\d[,.]\\d{2}"), QRegularExpression::MultilineOption);
    if (micro.match(text).hasMatch() || viewer.match(text).hasMatch()) return QStringLiteral("sub");
    static const QRegularExpression sami(QStringLiteral("<sync\\s+[^>]*start\\s*="), QRegularExpression::CaseInsensitiveOption);
    if (text.contains(QLatin1String("<sami"), Qt::CaseInsensitive) && sami.match(text).hasMatch()) return QStringLiteral("smi");
    QXmlStreamReader xml(text); QString root; bool timed = false;
    while (!xml.atEnd()) {
        xml.readNext(); if (!xml.isStartElement()) continue;
        if (root.isEmpty()) root = xml.name().toString();
        timed = timed || xml.attributes().hasAttribute(QLatin1String("begin")) || xml.attributes().hasAttribute(QLatin1String("start"));
    }
    if (!xml.hasError() && timed) {
        if (root == QLatin1String("tt")) return QStringLiteral("ttml");
        if (root == QLatin1String("USFSubtitles")) return QStringLiteral("usf");
    }
    return {};
}

void SubtitleSourceService::downloadLink(quint64 generation, const QString& source, const Link& link)
{
    if (m_storageDirectory.isEmpty()) { finishPreparation(generation, tr("Hakone cannot create the subtitle folder.")); return; }
    for (const auto& format : {"srt", "vtt", "ass", "ssa", "sub", "smi", "ttml", "usf"}) {
        const auto path = linkCachePath(link, QLatin1String(format)); QFile file(path);
        if (file.size() <= m_options.maxFileBytes && file.open(QIODevice::ReadOnly) && linkFormat(file.readAll()) == QLatin1String(format)) {
            m_ready.insert(source, QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)); m_pending.removeFirst();
            QTimer::singleShot(0, this, [this, generation] { downloadNext(generation); }); return;
        }
    }
    m_downloadReply = request(link.url, {}, false, m_options.maxFileBytes,
        [this, generation, source, link](const QByteArray& bytes, const QString& error) {
            if (generation != m_prepareGeneration) return;
            if (!error.isEmpty()) { finishPreparation(generation, error); return; }
            const auto format = linkFormat(bytes);
            if (format.isEmpty()) {
                finishPreparation(generation, bytes.startsWith("PK\003\004")
                    ? tr("This link contains an archive. Extract its subtitle files, then add them from Computer.")
                    : tr("This link does not contain a subtitle file. Use a direct subtitle file link.")); return;
            }
            const auto path = linkCachePath(link, format);
            if (!writeSubtitle(path, bytes)) { finishPreparation(generation, tr("Hakone cannot save these subtitles.")); return; }
            m_ready.insert(source, QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)); m_pending.removeFirst();
            QTimer::singleShot(0, this, [this, generation] { downloadNext(generation); });
        });
}

void SubtitleSourceService::finishPreparation(quint64 generation, const QString& error)
{
    if (generation != m_prepareGeneration) return;
    m_downloadReply = nullptr;
    QVariantList rows;
    if (error.isEmpty()) for (const auto& item : m_rows) {
        auto row = item.toMap(); QStringList tracks;
        for (const auto& value : row.value("tracks").toList()) {
            const auto url = value.toUrl().toString(QUrl::FullyEncoded);
            tracks.append(m_ready.value(url, url));
        }
        row.insert("tracks", tracks); rows.append(row);
    }
    QVariantMap resolved;
    for (auto it = m_ready.cbegin(); it != m_ready.cend(); ++it) resolved.insert(it.key(), it.value());
    m_rows.clear(); m_pending.clear(); m_ready.clear();
    m_preparing = false; emit preparingChanged();
    emit preparationFinished(generation, {{"success", error.isEmpty()}, {"error", error}, {"rows", rows}, {"resolved", resolved}});
}
