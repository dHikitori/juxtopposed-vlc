// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_credentials.hpp"
#include "addon_package_store.hpp"
#include <QHash>
#include <QJsonObject>
#include <functional>
#include <memory>

namespace vlc::addons {
// Capture these rows on the Service owner thread, before queued work starts.
// The binding must identify the exact registration and its retained lifetime.
struct ArtworkBuiltinRegistration {
    QJsonObject descriptor;
    QString binding;
    bool available = false;
};

class ArtworkAttemptPlan final {
public:
    static constexpr qsizetype MaximumRawRows = 256;
    static constexpr qsizetype MaximumSlots = 16;
    static constexpr qsizetype MaximumBytes = 2 * 1024 * 1024;
    enum class Implementation { Portable, Native, Missing };
    enum class Disposition {
        NonMetadata, OtherFeature, OtherMediaKind, Disabled, SafeMode,
        FamilyPeer, Selected, SelectedBlocked, RemovedPreference
    };
    struct Decision {
        QString contribution, family;
        Implementation implementation = Implementation::Missing;
        Disposition disposition = Disposition::SelectedBlocked;
        QString reason;
        QJsonObject descriptor;
    };
    struct Slot {
        unsigned ordinal = 0;
        QString contribution, family, contract;
        QString feature = QStringLiteral("artwork"), operation = QStringLiteral("enrich");
        QStringList mediaKinds, requestedFields{QStringLiteral("artwork")};
        Implementation implementation = Implementation::Missing;
        InstalledPackage package;
        Contribution declaration;
        QJsonObject settings, schema, approvedGrants, projectedContext;
        QJsonObject credentialPresence;
        QByteArray releaseDigest, projectedContextDigest;
        quint64 releaseGeneration = 0, settingsRevision = 0;
        QString accountReference, partition, nativeBinding;
        bool blocked = true;
        QString reason;
    };
    struct Capture {
        MetadataCatalogueReader catalogue;
        CredentialStore::Reader credentialReader;
        QHash<QString, QHash<QString, bool>> knownPresence;
        QList<ArtworkBuiltinRegistration> builtins;
        QByteArray registryIdentity;
        QByteArray subject;
        QString privateScope;
        QJsonObject requestContext;
        // Use the Core monotonic clock and its microsecond tick unit.
        qint64 deadline = 0;
        std::function<qint64()> clock;
        // This callback reads only native or memory state. It covers the exact
        // native registry epoch, app lifetime and Core child admission.
        std::function<bool()> live;
        bool workerAvailable = false;
    };

    // Run on joined background work. The returned plan has no credential reader
    // or secret bytes. A blocked slot can still permit later positive attempts;
    // it permanently prevents a final negative certificate for this plan.
    // Consumes the capture: its callbacks, including the credential reader, are
    // released when capture() returns, even in the caller's moved-from object.
    static std::shared_ptr<const ArtworkAttemptPlan> capture(Capture&&, QString* error = nullptr);
    bool current() const noexcept;
    bool hasBlockedSlot() const noexcept;
    const QByteArray subject, requestDigest, rawCatalogueDigest, planDigest;
    const QString privateScope;
    const qint64 deadline;
    const MetadataCatalogueSnapshot rawCatalogue;
    const QList<Decision> rawDecisions;
    const QList<Slot> attempts;
    const StoreAuthorityToken queuedAuthority, authority;
    const qsizetype retainedBytes;
private:
    struct Data;
    explicit ArtworkAttemptPlan(Data&&);
    const std::function<qint64()> m_clock;
    const std::function<bool()> m_live;
};
}
