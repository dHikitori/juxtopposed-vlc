// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_artwork_attempt_plan.hpp"
#include "addon_metadata_family_plan.hpp"
#include "addon_metadata_identity.hpp"
#include "addon_package_configuration.hpp"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <algorithm>

namespace vlc::addons {
namespace {
using Plan = ArtworkAttemptPlan;
QByteArray compact(const QJsonObject& value)
{ return QJsonDocument(value).toJson(QJsonDocument::Compact); }
QByteArray digest(const QJsonObject& value)
{ return QCryptographicHash::hash(compact(value), QCryptographicHash::Sha256); }
QString hex(const QByteArray& value) { return QString::fromLatin1(value.toHex()); }
QString packagePart(const QString& value) { return value.section(QLatin1Char('/'), 0, 0); }
bool atom(const QString& value, qsizetype limit)
{
    if (value.isEmpty() || value.size() > limit) return false;
    return std::none_of(value.cbegin(), value.cend(), [](QChar c) { return c.category() == QChar::Other_Control; });
}
bool strings(const QJsonValue& value, bool empty = false)
{
    if (!value.isArray() || value.toArray().size() > 128 || (!empty && value.toArray().isEmpty())) return false;
    QSet<QString> seen;
    for (const auto& item : value.toArray()) {
        if (!item.isString() || !atom(item.toString(), 128) || seen.contains(item.toString())) return false;
        seen.insert(item.toString());
    }
    return true;
}
QString familyFor(const QString& id)
{
    for (const auto& binding : AddonMetadataFamilyPlan::bindings())
        if (binding.nativeContribution == id || binding.portableContribution == id) return binding.family;
    return {};
}
QJsonObject releaseIdentity(const ReleaseIdentity& value)
{
    return {{QStringLiteral("source"), value.source}, {QStringLiteral("tag"), value.tag},
        {QStringLiteral("tagObjectId"), value.tagObjectId}, {QStringLiteral("commitId"), value.commitId},
        {QStringLiteral("objectFormat"), value.objectFormat}, {QStringLiteral("developerSource"), value.developerSource},
        {QStringLiteral("distributionSource"), value.distributionSource}};
}
QJsonObject packageDescriptor(const MetadataCataloguePackage& row)
{
    QJsonArray declarations;
    for (const auto& contribution : row.contributions) declarations.append(contribution.document);
    return {{QStringLiteral("manifest"), row.package.manifest.document}, {QStringLiteral("contributions"), declarations},
        {QStringLiteral("releaseIdentity"), releaseIdentity(row.package.identity)},
        {QStringLiteral("releaseDigest"), QString::fromLatin1(row.package.digest)},
        {QStringLiteral("generation"), double(row.package.generation)},
        {QStringLiteral("settingsRevision"), double(row.settingsRevision)},
        {QStringLiteral("settings"), row.settings}, {QStringLiteral("schema"), row.schema},
        {QStringLiteral("grants"), row.package.grants}, {QStringLiteral("directory"), row.package.directory},
        {QStringLiteral("previousDigest"), row.package.previousDigest},
        {QStringLiteral("savedEnabled"), row.savedEnabled}, {QStringLiteral("enabled"), row.package.enabled},
        {QStringLiteral("recoveryRequired"), row.package.recoveryRequired}};
}
QStringList effectiveOrder(const QJsonObject& preferences, const QString& kind, const QString& language)
{
    const auto scoped = QStringLiteral("artwork/") + kind;
    const auto key = preferences.contains(scoped + QLatin1Char('/') + language)
        ? scoped + QLatin1Char('/') + language : preferences.contains(scoped) ? scoped : QStringLiteral("artwork");
    QStringList order;
    for (const auto& value : preferences.value(key).toArray()) order.append(value.toString());
    return order;
}
QJsonObject slotDescriptor(const Plan::Slot& slot)
{
    return {{QStringLiteral("ordinal"), int(slot.ordinal)}, {QStringLiteral("contribution"), slot.contribution},
        {QStringLiteral("family"), slot.family}, {QStringLiteral("implementation"), int(slot.implementation)},
        {QStringLiteral("contract"), slot.contract}, {QStringLiteral("feature"), slot.feature},
        {QStringLiteral("operation"), slot.operation}, {QStringLiteral("mediaKinds"), QJsonArray::fromStringList(slot.mediaKinds)},
        {QStringLiteral("declaration"), slot.declaration.document},
        {QStringLiteral("releaseDigest"), QString::fromLatin1(slot.releaseDigest)},
        {QStringLiteral("releaseGeneration"), double(slot.releaseGeneration)},
        {QStringLiteral("settingsRevision"), double(slot.settingsRevision)},
        {QStringLiteral("accountReference"), slot.accountReference}, {QStringLiteral("partition"), slot.partition},
        {QStringLiteral("approvedGrants"), slot.approvedGrants}, {QStringLiteral("settings"), slot.settings},
        {QStringLiteral("schema"), slot.schema}, {QStringLiteral("credentialPresence"), slot.credentialPresence},
        {QStringLiteral("requestedFields"), QJsonArray::fromStringList(slot.requestedFields)},
        {QStringLiteral("projectedContext"), slot.projectedContext},
        {QStringLiteral("projectedContextDigest"), hex(slot.projectedContextDigest)},
        {QStringLiteral("nativeBinding"), slot.nativeBinding}, {QStringLiteral("blocked"), slot.blocked},
        {QStringLiteral("reason"), slot.reason}};
}
struct Row {
    Plan::Decision decision;
    Plan::Slot slot;
    bool enabled = false;
    bool candidate = false;
    bool preferred = false;
};
}
struct ArtworkAttemptPlan::Data {
    QByteArray subject, requestDigest, rawCatalogueDigest, planDigest;
    QString privateScope;
    qint64 deadline = 0;
    MetadataCatalogueSnapshot catalogue;
    QList<Decision> decisions;
    QList<Slot> attempts;
    qsizetype retainedBytes = 0;
    std::function<qint64()> clock;
    std::function<bool()> live;
};
ArtworkAttemptPlan::ArtworkAttemptPlan(Data&& data)
    : subject(std::move(data.subject)), requestDigest(std::move(data.requestDigest)),
      rawCatalogueDigest(std::move(data.rawCatalogueDigest)), planDigest(std::move(data.planDigest)),
      privateScope(std::move(data.privateScope)), deadline(data.deadline), rawCatalogue(std::move(data.catalogue)),
      rawDecisions(std::move(data.decisions)), attempts(std::move(data.attempts)),
      queuedAuthority(rawCatalogue.queuedAuthority), authority(rawCatalogue.authority), retainedBytes(data.retainedBytes),
      m_clock(std::move(data.clock)), m_live(std::move(data.live)) {}
bool ArtworkAttemptPlan::current() const noexcept
{
    try { return m_clock && m_live && queuedAuthority.current() && authority.current()
        && m_clock() < deadline && m_live(); } catch (...) { return false; }
}
bool ArtworkAttemptPlan::hasBlockedSlot() const noexcept
{ return std::any_of(attempts.cbegin(), attempts.cend(), [](const Slot& slot) { return slot.blocked; }); }

std::shared_ptr<const ArtworkAttemptPlan> ArtworkAttemptPlan::capture(Capture input, QString* error)
{
    const auto fail = [error](const QString& message) -> std::shared_ptr<const ArtworkAttemptPlan> {
        if (error) *error = message;
        return {};
    };
    const auto captureCurrent = [&] {
        try { return input.live && input.clock && input.live() && input.clock() < input.deadline; }
        catch (...) { return false; }
    };
    qint64 now = 0;
    try { if (input.clock) now = input.clock(); } catch (...) {}
    if (input.subject.size() != 32 || input.subject == QByteArray(32, '\0') || !atom(input.privateScope, 128)
        || input.registryIdentity.size() != 32 || !input.catalogue || !captureCurrent()
        || now <= 0 || input.deadline <= now || input.deadline - now > 30 * 1000 * 1000
        || compact(input.requestContext).size() > 128 * 1024 || input.builtins.size() > 128)
        return fail(QStringLiteral("The artwork plan capture is not available."));
    std::sort(input.builtins.begin(), input.builtins.end(), [](const auto& a, const auto& b) {
        return a.descriptor.value(QStringLiteral("id")).toString() < b.descriptor.value(QStringLiteral("id")).toString();
    });
    Data result;
    if (!input.catalogue(&result.catalogue, error) || !result.catalogue.current() || !captureCurrent())
        return fail(QStringLiteral("The metadata catalogue changed. Start the request again."));
    if (result.catalogue.digest.size() != 32 || result.catalogue.packages.size() > MetadataCatalogueSnapshot::MaximumCandidates
        || result.catalogue.candidateCount != result.catalogue.packages.size()
        || result.catalogue.rawContributionCount > MetadataCatalogueSnapshot::MaximumContributions
        || result.catalogue.retainedBytes < 0 || result.catalogue.retainedBytes > MetadataCatalogueSnapshot::MaximumBytes)
        return fail(QStringLiteral("The metadata catalogue exceeds the plan limits."));
    const auto normalized = normalizeMetadataIdentity(input.requestContext.value(QStringLiteral("identity")).toObject().toVariantMap());
    if (!normalized || (input.requestContext.contains(QStringLiteral("identity"))
        && !input.requestContext.value(QStringLiteral("identity")).isObject()))
        return fail(QStringLiteral("The media identity is not valid."));
    QJsonObject context;
    for (const auto& name : {QStringLiteral("language"), QStringLiteral("locale"), QStringLiteral("query"),
            QStringLiteral("manual"), QStringLiteral("cursor"), QStringLiteral("fingerprints"), QStringLiteral("media")})
        if (input.requestContext.contains(name)) context.insert(name, input.requestContext.value(name));
    context.insert(QStringLiteral("identity"), QJsonObject::fromVariantMap(*normalized));
    context.insert(QStringLiteral("feature"), QStringLiteral("artwork"));
    context.insert(QStringLiteral("fields"), QJsonArray{QStringLiteral("artwork")});
    const auto kind = normalized->value(QStringLiteral("kind")).toString();
    const auto order = effectiveOrder(result.catalogue.providerPreferences, kind,
        context.value(QStringLiteral("language")).toString());
    const QSet<QString> preferred(order.cbegin(), order.cend());
    QList<Row> rows;
    QHash<QString, qsizetype> rowIndices;
    QHash<QString, const MetadataCataloguePackage*> packages;
    qsizetype rawCount = 0;
    const auto add = [&](Row row) {
        if (++rawCount > MaximumRawRows || !atom(row.decision.contribution, 256)
            || rowIndices.contains(row.decision.contribution)) return false;
        rowIndices.insert(row.decision.contribution, rows.size()); rows.append(std::move(row)); return true;
    };
    for (const auto& package : result.catalogue.packages) {
        if (!result.catalogue.current() || !captureCurrent())
            return fail(QStringLiteral("The artwork plan is no longer current."));
        if (packages.contains(package.package.manifest.id))
            return fail(QStringLiteral("The metadata catalogue has duplicate packages."));
        packages.insert(package.package.manifest.id, &package);
        QJsonObject presence;
        auto known = input.knownPresence.value(package.package.manifest.id);
        bool completePresence = true;
        const auto present = [&](const QString& reference) {
            if (reference.isEmpty()) return false;
            if (known.contains(reference)) { presence.insert(reference, known.value(reference)); return known.value(reference); }
            if (!input.credentialReader || !captureCurrent() || !result.catalogue.current()) {
                completePresence = false; return false;
            }
            auto secret = input.credentialReader(package.package.manifest.id, reference);
            const bool available = !secret.isEmpty(); secret.fill('\0');
            known.insert(reference, available); presence.insert(reference, available); return available;
        };
        const bool configured = packageConfiguration::configured(package.settings, package.schema, present);
        const auto account = packageConfiguration::packageAccountReference(package.package, package.settings, package.schema, present);
        if (!result.catalogue.current() || !captureCurrent())
            return fail(QStringLiteral("The artwork plan is no longer current."));
        for (const auto& declaration : package.contributions) {
            Row row;
            const auto id = package.package.manifest.id + QLatin1Char('/') + declaration.id;
            row.preferred = preferred.contains(id); row.enabled = package.package.enabled;
            row.decision.contribution = id; row.decision.family = familyFor(id);
            row.decision.implementation = Implementation::Portable;
            row.decision.descriptor = {{QStringLiteral("package"), package.package.manifest.id},
                {QStringLiteral("contribution"), declaration.document},
                {QStringLiteral("credentialPresence"), presence}, {QStringLiteral("accountReference"), account},
                {QStringLiteral("configured"), configured}, {QStringLiteral("presenceComplete"), completePresence}};
            row.slot.contribution = id; row.slot.family = row.decision.family;
            row.slot.implementation = Implementation::Portable; row.slot.package = package.package;
            row.slot.declaration = declaration; row.slot.contract = declaration.contract; row.slot.mediaKinds = declaration.mediaKinds;
            row.slot.settings = package.settings; row.slot.schema = package.schema;
            row.slot.credentialPresence = presence; row.slot.approvedGrants = package.package.grants;
            row.slot.releaseDigest = package.package.digest; row.slot.releaseGeneration = package.package.generation;
            row.slot.settingsRevision = package.settingsRevision; row.slot.accountReference = account;
            row.slot.partition = QString::fromLatin1(package.package.digest) + QLatin1Char(':') + QString::number(package.package.generation)
                + QLatin1Char(':') + QString::number(package.settingsRevision);
            row.slot.projectedContext = packageConfiguration::projectMetadataContext(package.package.manifest, package.package.grants, context);
            row.slot.projectedContextDigest = digest(row.slot.projectedContext);
            if (!row.enabled) {
                row.decision.disposition = result.catalogue.safeMode && package.savedEnabled ? Disposition::SafeMode : Disposition::Disabled;
                row.decision.reason = QStringLiteral("The add-on is disabled.");
            } else if (declaration.type != QLatin1String("metadata.provider") && !row.preferred) {
                row.decision.disposition = Disposition::NonMetadata;
                row.decision.reason = QStringLiteral("This contribution does not provide metadata.");
            } else if (!declaration.features.contains(QStringLiteral("artwork")) && !row.preferred) {
                row.decision.disposition = Disposition::OtherFeature;
                row.decision.reason = QStringLiteral("This provider does not provide artwork.");
            } else if (!declaration.mediaKinds.isEmpty() && !declaration.mediaKinds.contains(kind) && !row.preferred) {
                row.decision.disposition = Disposition::OtherMediaKind;
                row.decision.reason = QStringLiteral("This provider uses another media type.");
            } else {
                row.candidate = true;
                bool permissions = true;
                for (const auto& requirement : declaration.requires)
                    permissions &= package.package.grants.contains(requirement);
                row.slot.blocked = declaration.type != QLatin1String("metadata.provider")
                    || declaration.contract != QLatin1String("metadata@1")
                    || !declaration.features.contains(QStringLiteral("artwork"))
                    || !declaration.operations.contains(QStringLiteral("enrich"))
                    || (!declaration.mediaKinds.isEmpty() && !declaration.mediaKinds.contains(kind))
                    || !permissions || !configured || !completePresence || !input.workerAvailable
                    || account.startsWith(QStringLiteral("disconnected:"));
                row.slot.reason = row.slot.blocked ? QStringLiteral("The selected metadata provider is not available.") : QString{};
                row.decision.disposition = row.slot.blocked ? Disposition::SelectedBlocked : Disposition::Selected;
                row.decision.reason = row.slot.reason;
            }
            if (!add(std::move(row))) return fail(QStringLiteral("The artwork plan has too many or duplicate raw rows."));
        }
    }
    if (rawCount != result.catalogue.rawContributionCount)
        return fail(QStringLiteral("The metadata catalogue contribution count is not correct."));
    QJsonArray nativeDescriptors;
    for (const auto& registration : input.builtins) {
        const auto& descriptor = registration.descriptor;
        const auto id = descriptor.value(QStringLiteral("id")).toString();
        if (!id.startsWith(QLatin1String("hakone.builtin."))
            || descriptor.value(QStringLiteral("packageId")).toString() != packagePart(id)
            || !descriptor.value(QStringLiteral("enabled")).isBool()
            || !descriptor.value(QStringLiteral("configured")).isBool()
            || !strings(descriptor.value(QStringLiteral("features")))
            || !strings(descriptor.value(QStringLiteral("operations")))
            || (descriptor.contains(QStringLiteral("mediaKinds")) && !strings(descriptor.value(QStringLiteral("mediaKinds")), true))
            || !atom(registration.binding, 256))
            return fail(QStringLiteral("The native metadata registration is not correct."));
        Row row;
        row.preferred = preferred.contains(id);
        const auto enabled = result.catalogue.providerPreferences.value(QStringLiteral("_builtinEnabled")).toObject();
        row.enabled = enabled.contains(packagePart(id)) ? enabled.value(packagePart(id)).toBool()
            : descriptor.value(QStringLiteral("enabled")).toBool();
        row.decision.contribution = id; row.decision.family = familyFor(id); row.decision.implementation = Implementation::Native;
        row.decision.descriptor = {{QStringLiteral("registration"), descriptor}, {QStringLiteral("binding"), registration.binding},
            {QStringLiteral("available"), registration.available}, {QStringLiteral("enabled"), row.enabled}};
        nativeDescriptors.append(row.decision.descriptor);
        row.slot.contribution = id; row.slot.family = row.decision.family; row.slot.implementation = Implementation::Native;
        row.slot.contract = QStringLiteral("native-artwork@1"); row.slot.nativeBinding = registration.binding;
        row.slot.projectedContext = context; row.slot.projectedContextDigest = digest(context);
        row.slot.partition = descriptor.value(QStringLiteral("cachePartition")).toString();
        row.slot.accountReference = descriptor.value(QStringLiteral("accountReference")).toString();
        for (const auto& value : descriptor.value(QStringLiteral("mediaKinds")).toArray()) row.slot.mediaKinds.append(value.toString());
        const auto features = descriptor.value(QStringLiteral("features")).toArray();
        const auto operations = descriptor.value(QStringLiteral("operations")).toArray();
        if (!row.enabled) {
            row.decision.disposition = Disposition::Disabled; row.decision.reason = QStringLiteral("The native provider is disabled.");
        } else if (!features.contains(QStringLiteral("artwork")) && !row.preferred) {
            row.decision.disposition = Disposition::OtherFeature; row.decision.reason = QStringLiteral("This provider does not provide artwork.");
        } else if (!row.slot.mediaKinds.isEmpty() && !row.slot.mediaKinds.contains(kind) && !row.preferred) {
            row.decision.disposition = Disposition::OtherMediaKind; row.decision.reason = QStringLiteral("This provider uses another media type.");
        } else {
            row.candidate = true;
            row.slot.blocked = !registration.available || !descriptor.value(QStringLiteral("configured")).toBool()
                || !features.contains(QStringLiteral("artwork")) || !operations.contains(QStringLiteral("enrich"))
                || (!row.slot.mediaKinds.isEmpty() && !row.slot.mediaKinds.contains(kind))
                || row.slot.family.isEmpty() || row.slot.partition.isEmpty();
            row.slot.reason = row.slot.blocked ? QStringLiteral("The selected native metadata provider is not available.") : QString{};
            row.decision.disposition = row.slot.blocked ? Disposition::SelectedBlocked : Disposition::Selected;
            row.decision.reason = row.slot.reason;
        }
        if (!add(std::move(row))) return fail(QStringLiteral("The artwork plan has too many or duplicate raw rows."));
    }
    for (const auto& id : order) {
        if (rowIndices.contains(id)) continue;
        Row row;
        row.preferred = true; row.decision.contribution = id; row.decision.family = familyFor(id);
        row.decision.implementation = Implementation::Missing;
        row.decision.descriptor = {{QStringLiteral("preference"), id}};
        const auto package = packages.value(packagePart(id));
        // A restore keeps the historical removal marker. A current enabled
        // package takes precedence, even if its old contribution is absent.
        const bool removed = !package && !id.startsWith(QLatin1String("hakone.builtin."))
            && result.catalogue.distributionPolicy.value(QStringLiteral("removed")).toObject().value(packagePart(id)).toBool();
        if (removed || (package && !package->package.enabled)) {
            row.decision.disposition = removed ? Disposition::RemovedPreference : Disposition::Disabled;
            row.decision.reason = QStringLiteral("The preferred provider was removed or disabled.");
        } else if (id.startsWith(QLatin1String("hakone.builtin."))
            && result.catalogue.providerPreferences.value(QStringLiteral("_builtinEnabled")).toObject().contains(packagePart(id))
            && !result.catalogue.providerPreferences.value(QStringLiteral("_builtinEnabled")).toObject().value(packagePart(id)).toBool()) {
            row.decision.disposition = Disposition::Disabled;
            row.decision.reason = QStringLiteral("The preferred native provider is disabled.");
        } else {
            row.enabled = true; row.candidate = true;
            row.slot.contribution = id; row.slot.family = row.decision.family; row.slot.implementation = Implementation::Missing;
            row.slot.reason = QStringLiteral("The preferred metadata provider is not registered.");
            row.slot.projectedContext = context; row.slot.projectedContextDigest = digest(context);
            row.decision.disposition = Disposition::SelectedBlocked; row.decision.reason = row.slot.reason;
        }
        if (!add(std::move(row))) return fail(QStringLiteral("The artwork plan has too many or duplicate raw rows."));
    }
    QSet<QString> familyMembers, selected;
    for (const auto& binding : AddonMetadataFamilyPlan::bindings()) {
        familyMembers.insert(binding.nativeContribution); familyMembers.insert(binding.portableContribution);
        QString choice;
        for (const auto& id : order)
            if ((id == binding.nativeContribution || id == binding.portableContribution)
                && rowIndices.contains(id) && rows.at(rowIndices.value(id)).candidate) { choice = id; break; }
        if (choice.isEmpty() && rowIndices.contains(binding.nativeContribution)
            && rows.at(rowIndices.value(binding.nativeContribution)).candidate) choice = binding.nativeContribution;
        if (choice.isEmpty() && rowIndices.contains(binding.portableContribution)
            && rows.at(rowIndices.value(binding.portableContribution)).candidate) choice = binding.portableContribution;
        if (!choice.isEmpty()) selected.insert(choice);
    }
    QStringList executionOrder = order;
    for (const auto& binding : AddonMetadataFamilyPlan::bindings()) {
        const auto chosen = selected.contains(binding.nativeContribution) ? binding.nativeContribution
            : selected.contains(binding.portableContribution) ? binding.portableContribution : QString{};
        if (!chosen.isEmpty() && !executionOrder.contains(chosen)) executionOrder.append(chosen);
    }
    for (const auto& row : rows) if (!executionOrder.contains(row.decision.contribution)) executionOrder.append(row.decision.contribution);
    QSet<QString> emitted;
    for (const auto& id : executionOrder) {
        if (!rowIndices.contains(id) || emitted.contains(id)) continue;
        emitted.insert(id);
        auto& row = rows[rowIndices.value(id)];
        if (!row.candidate) continue;
        if (familyMembers.contains(id) && !selected.contains(id)) {
            row.decision.disposition = Disposition::FamilyPeer;
            row.decision.reason = QStringLiteral("The plan selected another implementation of this family.");
            continue;
        }
        if (result.attempts.size() >= MaximumSlots) return fail(QStringLiteral("The artwork plan has too many selected providers."));
        row.slot.ordinal = unsigned(result.attempts.size()); result.attempts.append(std::move(row.slot));
    }
    QJsonArray decisions, attempts, packagesForDigest;
    for (auto& row : rows) {
        decisions.append(QJsonObject{{QStringLiteral("contribution"), row.decision.contribution},
            {QStringLiteral("family"), row.decision.family}, {QStringLiteral("implementation"), int(row.decision.implementation)},
            {QStringLiteral("disposition"), int(row.decision.disposition)}, {QStringLiteral("reason"), row.decision.reason},
            {QStringLiteral("descriptor"), row.decision.descriptor}});
        result.decisions.append(std::move(row.decision));
    }
    for (const auto& package : result.catalogue.packages) packagesForDigest.append(packageDescriptor(package));
    for (const auto& slot : result.attempts) attempts.append(slotDescriptor(slot));
    result.subject = std::move(input.subject); result.privateScope = std::move(input.privateScope); result.deadline = input.deadline;
    result.requestDigest = digest(input.requestContext);
    result.rawCatalogueDigest = digest({{QStringLiteral("format"), QStringLiteral("hakone-artwork-raw-plan@1")},
        {QStringLiteral("catalogueDigest"), hex(result.catalogue.digest)}, {QStringLiteral("packages"), packagesForDigest},
        {QStringLiteral("preferences"), result.catalogue.providerPreferences},
        {QStringLiteral("distributionPolicy"), result.catalogue.distributionPolicy}, {QStringLiteral("safeMode"), result.catalogue.safeMode},
        {QStringLiteral("registryIdentity"), hex(input.registryIdentity)}, {QStringLiteral("builtins"), nativeDescriptors}});
    const QJsonObject stamp{{QStringLiteral("format"), QStringLiteral("hakone-artwork-plan@1")},
        {QStringLiteral("subject"), hex(result.subject)}, {QStringLiteral("scope"), result.privateScope},
        {QStringLiteral("deadline"), QString::number(result.deadline)}, {QStringLiteral("requestDigest"), hex(result.requestDigest)},
        {QStringLiteral("rawCatalogueDigest"), hex(result.rawCatalogueDigest)}, {QStringLiteral("decisions"), decisions},
        {QStringLiteral("attempts"), attempts}, {QStringLiteral("workerAvailable"), input.workerAvailable}};
    result.retainedBytes = result.catalogue.retainedBytes + compact(stamp).size();
    if (result.retainedBytes > MaximumBytes) return fail(QStringLiteral("The artwork plan exceeds the byte limit."));
    if (!result.catalogue.current() || !captureCurrent()) return fail(QStringLiteral("The artwork plan is no longer current."));
    result.planDigest = digest(stamp); result.clock = std::move(input.clock); result.live = std::move(input.live);
    if (error) error->clear();
    return std::shared_ptr<const ArtworkAttemptPlan>(new ArtworkAttemptPlan(std::move(result)));
}
}
