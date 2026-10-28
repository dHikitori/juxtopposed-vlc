// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_package_store.hpp"
#include "addon_credentials.hpp"
#include <QMap>
#include <QHash>
#include <QVariantList>
#include <QSet>
#include <functional>
#include <optional>
#include <atomic>

namespace vlc::addons {
// Private display projection. This is NOT MetadataCatalogueSnapshot and cannot
// supply a Metadata@1 receipt, native outcome or invocation admission.
struct DisplayDemand final { explicit DisplayDemand(quint64 value):identity(value){} const quint64 identity;std::atomic_bool open{true}; };
struct DisplayCatalogueRequest {
    // Host contribution/id/field keys, not paths. Unopened docs are not kept.
    QStringList documents,settingsPackages,presenceFeatures;
    QHash<QString,std::shared_ptr<const DisplayDemand>> activeDemands;
    bool featureCurrent(const QString& feature) const {
        const auto state=activeDemands.value(QStringLiteral("@feature/")+feature);
        return !state || state->open.load(std::memory_order_acquire);
    }
    quint64 featureIdentity(const QString& feature) const {
        const auto state=activeDemands.value(QStringLiteral("@feature/")+feature);return state?state->identity:0;
    }
    bool settingsCurrent(const QString& package) const {
        const auto state=activeDemands.value(package+QStringLiteral("/@settings"));
        return state && state->open.load(std::memory_order_acquire);
    }
};
struct DisplayCataloguePackage {
    InstalledPackage package;
    QJsonObject settings, schema;
    QMap<QString,QJsonObject> resources; // verified declared JSON only; never Wasm
    QJsonObject messages;
    quint64 settingsRevision = 0;
    bool savedEnabled = false, corrupt = false,messagesReady=true,retiring=false,summaryOnly=false;
    QString error;
};
struct DisplayCatalogueSnapshot {
    // A busy owned store lock can recover on its post-unlock notification.
    // Other failures require changed inputs/authority or an explicit refresh.
    enum class Refusal { None, StoreBusy };
    Refusal refusal = Refusal::None;
    static constexpr qsizetype MaximumCandidates = 256;
    static constexpr qint64 MaximumResourceBytes = (32+16)*2*256*1024 + 256*1024;
    static constexpr qint64 MaximumDescriptorBytes = MetadataCatalogueSnapshot::MaximumBytes;
    QList<DisplayCataloguePackage> packages;
    QVariantList themes;
    QString selectedTheme;
    QStringList savedDataIds;
    QJsonObject preferences, distributionPolicy,displayPreferences;
    StoreAuthorityToken queuedAuthority, authority;
    qint64 retainedResourceBytes = 0,retainedDescriptorBytes = 0;
    bool safeMode = false;
    bool current() const noexcept { return queuedAuthority.current() && authority.current(); }
};
using DisplayCatalogueReader = std::function<bool(const DisplayCatalogueRequest&,DisplayCatalogueSnapshot*,QString*)>;
// Captured on Manager owner thread; execute and destroy on joined Qt work.
struct DisplayCatalogueSource {
    DisplayCatalogueReader reader;
    CredentialStore::Reader credentials;
    // Joined Qt work only. The callable must own a native token, no QObject.
    std::function<std::optional<bool>()> tlsProbe;
    std::optional<bool> preparedTls; // same-epoch bool seed only
    StoreAuthorityToken queuedAuthority;
    bool current() const noexcept { return !retiring && !bootstrapping && queuedAuthority.current(); }
    QHash<QString,QHash<QString,bool>> credentialPresence;
    QVariantList builtins;
    QMap<QString,QVariantMap> distributions;
    QStringList distributionIds; // trusted registration IDs only, no paths
    QSet<QString> withdrawing, removing;
    QString workerExecutable,locale;
    bool retiring = false,bootstrapping=false;
    quint64 bootstrapGeneration=0;
    QVariantList bootstrapResults;
    // Equality covers only copied display inputs. No hashing or file access.
    bool sameInputs(const DisplayCatalogueSource& other) const {
        return credentialPresence==other.credentialPresence && builtins==other.builtins
            && distributions==other.distributions && distributionIds==other.distributionIds && withdrawing==other.withdrawing
            && removing==other.removing && workerExecutable==other.workerExecutable && locale==other.locale
            && retiring==other.retiring && bootstrapping==other.bootstrapping
            && bootstrapGeneration==other.bootstrapGeneration && bootstrapResults==other.bootstrapResults;
    }
};
}
