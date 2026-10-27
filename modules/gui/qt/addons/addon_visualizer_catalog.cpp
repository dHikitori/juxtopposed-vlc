// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_visualizer_catalog.hpp"
#include "addon_manager.hpp"
#include "addon_store_authority.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QUuid>

namespace vlc::addons {
namespace {
QJsonObject object(const QString& path, qint64 maximum)
{
    QFile file(path);
    if (QFileInfo(file).isSymLink() || file.size() < 1 || file.size() > maximum || !file.open(QIODevice::ReadOnly)) return {};
    const auto bytes = file.read(maximum + 1); if (bytes.size() > maximum) return {};
    return QJsonDocument::fromJson(bytes).object();
}
}
AddonUiAssets::Backend AddonVisualizerCatalog::backend(AddonManager* manager, const std::shared_ptr<std::atomic<quint64>>& epoch)
{
    const QPointer<AddonManager> owner(manager);
    return {[owner, epoch](const QString&) -> UiAssetReader {
        if (!owner) return {};
        const auto directory = owner->storageDirectory(); const auto expected = epoch->load(std::memory_order_acquire);
        const auto authority = owner->storeAuthorityToken();
        return [directory, epoch, expected, authority] {
            const PackageStore store(directory);
            UiAssetCapture capture; QVariantList choices;
            const auto root = store.storageDirectory(); const bool safe = store.safeMode();
            static const QRegularExpression digest(QStringLiteral("^[0-9a-f]{64}$"));
            const auto ids = store.installedIds().mid(0, 256);
            for (const auto& id : ids) {
                if (!authority.current() || epoch->load(std::memory_order_acquire) != expected) return capture;
                const auto folder = QDir(root).filePath(QStringLiteral("packages/") + id);
                auto state = object(QDir(folder).filePath(QStringLiteral("state.json")), 4 * 1024 * 1024);
                bool recovered = false;
                if (state.value("id").toString() != id || state.value("formatVersion").toInt() != 1) {
                    state = object(QDir(folder).filePath(QStringLiteral("state.json.backup")), 4 * 1024 * 1024); recovered = true;
                }
                if (state.value("id").toString() != id || state.value("formatVersion").toInt() != 1) continue;
                const auto current = state.value("current").toObject(); const auto release = current.value("digest").toString();
                if (!digest.match(release).hasMatch()) continue;
                QFile manifest(QDir(folder).filePath(QStringLiteral("releases/") + release + QStringLiteral("/addon.json")));
                if (QFileInfo(manifest).isSymLink() || manifest.size() > 256 * 1024 || !manifest.open(QIODevice::ReadOnly)) continue;
                Manifest parsed;
                if (!Manifest::parse(manifest.read(256 * 1024 + 1), &parsed) || parsed.id != id) continue;
                const auto granted = current.value("grants").toObject().value(QStringLiteral("audio.analysis")).toObject().value(QStringLiteral("channels")).toArray();
                // Catalog rows are display hints. Activation verifies the full
                // inventory, current configuration, and grants separately.
                for (const auto& contribution : parsed.contributions) {
                    if (contribution.type != QLatin1String("visualizer") || contribution.contract != QLatin1String("visualizer@1")) continue;
                    const auto channels = contribution.document.value("analysisChannels").toArray();
                    bool enabled = !safe && !recovered && state.value("enabled").toBool() && !state.value("recoveryRequired").toBool() && !channels.isEmpty();
                    for (const auto& channel : channels) enabled &= granted.contains(channel);
                    const QString global = id + '/' + contribution.id;
                    choices.append(QVariantMap{{"id", global}, {"packageId", id}, {"name", contribution.document.value("name").toString(parsed.name)}, {"enabled", enabled}});
                    if (choices.size() >= 256) break;
                }
                if (choices.size() >= 256) break;
            }
            const auto key = QUuid::createUuid().toString(QUuid::WithoutBraces);
            capture.registration = {{"packageId", "vlc.visualizer-catalog"}, {"enabled", true}, {"configured", true},
                {"accountReference", "public:catalog"}, {"assetStamp", key}, {"assets", QVariantMap{}}, {"choices", choices}};
            capture.current = [epoch, expected, authority] { return authority.current() && epoch->load(std::memory_order_acquire) == expected; };
            capture.live = capture.current; return capture;
        };
    }};
}
AddonVisualizerCatalog::AddonVisualizerCatalog(AddonManager* manager, QObject* parent)
    : QObject(parent), m_manager(manager), m_epoch(std::make_shared<std::atomic<quint64>>(0)), m_assets(new AddonUiAssets(backend(manager, m_epoch), {}, this))
{
    connect(m_assets, &AddonUiAssets::sessionAssetsChanged, this, [this](const QString& id) { if (id == m_session) completed(); });
    if (manager) {
        connect(manager, &AddonManager::revisionChanged, this, &AddonVisualizerCatalog::reload);
        connect(manager, &QObject::destroyed, this, &AddonVisualizerCatalog::reload);
    }
    reload();
}
AddonVisualizerCatalog::~AddonVisualizerCatalog()
{ m_epoch->fetch_add(1, std::memory_order_acq_rel); disconnect(m_assets, nullptr, this, nullptr); m_assets->close(); }
void AddonVisualizerCatalog::reload()
{
    m_epoch->fetch_add(1, std::memory_order_acq_rel); const auto previous = m_session; m_session.clear();
    if (!previous.isEmpty()) m_assets->closeSession(previous);
    m_choices.clear();
    if (m_manager) {
        m_session = QUuid::createUuid().toString(QUuid::WithoutBraces);
        m_assets->openSession(m_session, QStringLiteral("catalog"));
    }
    emit changed();
}
bool AddonVisualizerCatalog::loading() const
{ return !m_session.isEmpty() && m_assets->sessionLoading(m_session); }
void AddonVisualizerCatalog::completed()
{
    if (loading()) return;
    const auto session = m_session; m_choices = m_assets->sessionRegistration(session).value("choices").toList();
    m_session.clear(); m_assets->closeSession(session); emit changed();
}
}
