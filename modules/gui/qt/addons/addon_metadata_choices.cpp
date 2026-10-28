// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_metadata_choices.hpp"
#include "util/media_identity.hpp"
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QPointer>
#include <QScopeGuard>
#include <QUuid>
#include <algorithm>
#include <limits>

namespace {
QString reference(const char* kind)
{ return QLatin1String(kind) + QUuid::createUuid().toString(QUuid::WithoutBraces); }
QVariantMap identity(const QVariant& value)
{
    auto input = value.toMap();
    input.insert(QStringLiteral("type"), input.value(QStringLiteral("kind"), input.value(QStringLiteral("type"))));
    const auto parsed = MediaIdentity::fromMap(input);
    if (parsed.title.isEmpty() || parsed.title.size() > 1024 || parsed.ids.size() > 32
        || parsed.aliases.size() > 32 || parsed.episodes.size() > 128) return {};
    for (auto it = parsed.ids.cbegin(); it != parsed.ids.cend(); ++it)
        if (it.key().size() > 128 || it.value().size() > 256) return {};
    auto result = MediaIdentity::toMap(parsed);
    result.insert(QStringLiteral("kind"), result.value(QStringLiteral("type")));
    result.remove(QStringLiteral("provenance")); result.remove(QStringLiteral("conflicts"));
    return result;
}
QVariantMap credit(const QVariant& value)
{
    const auto input = value.toMap(); QVariantMap result;
    const auto name = input.value(QStringLiteral("name")).toString();
    const auto license = input.value(QStringLiteral("license")).toString();
    const QUrl url(input.value(QStringLiteral("url")).toString(), QUrl::StrictMode);
    if (name.isEmpty() || name.size() > 128 || license.size() > 128 || !url.isValid()
        || url.scheme() != QStringLiteral("https") || url.host().isEmpty() || !url.userInfo().isEmpty()
        || url.toEncoded().size() > 2048) return {};
    result.insert(QStringLiteral("name"), name); result.insert(QStringLiteral("url"), url.toString(QUrl::FullyEncoded));
    if (!license.isEmpty()) result.insert(QStringLiteral("license"), license);
    return result;
}
qint64 serializedBytes(const QVariantMap& value)
{ return QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact).size(); }
}

AddonMetadataChoices::AddonMetadataChoices(Backend backend, QObject* parent)
    : AddonMetadataChoices(std::move(backend), Options{}, parent) {}
AddonMetadataChoices::AddonMetadataChoices(Backend backend, const Options& options, QObject* parent)
    : QObject(parent), m_backend(std::move(backend)), m_options(options), m_sweep(this)
{
    m_options.maxSets = std::clamp(options.maxSets, 1, 64);
    m_options.maxChoices = std::clamp(options.maxChoices, 1, 8192);
    m_options.maxSelections = std::clamp(options.maxSelections, 1, 256);
    m_options.maxBytes = std::clamp<qint64>(options.maxBytes, 1, 16 * 1024 * 1024);
    m_options.lifetimeMs = std::clamp(options.lifetimeMs, 10, 300000);
    m_clock.start();
    m_sweep.setInterval(std::min(m_options.lifetimeMs, 1000));
    connect(&m_sweep, &QTimer::timeout, this, [this] { expire(); });
}
AddonMetadataChoices::~AddonMetadataChoices() { m_destroying = true; invalidate(); }
bool AddonMetadataChoices::blocked() const
{ return m_invalidating || m_destroying || m_retired; }
bool AddonMetadataChoices::changed()
{
    if (m_epoch == std::numeric_limits<quint64>::max()) { m_retired = true; return false; }
    ++m_epoch; return true;
}
QByteArray AddonMetadataChoices::binding(const QVariantMap& context, const QVariantMap& original)
{
    const auto media = context.value(QStringLiteral("media")).toString();
    if (media.isEmpty() || media.size() > 128) return {};
    QVariantMap fields;
    const auto revisions = context.value(QStringLiteral("fieldRevisions")).toMap();
    if (revisions.size() > 64) return {};
    for (auto it = revisions.cbegin(); it != revisions.cend(); ++it) fields.insert(it.key(), it.value().toString());
    const QVariantMap scope{{QStringLiteral("media"), media}, {QStringLiteral("identity"), original},
        {QStringLiteral("generation"), context.value(QStringLiteral("generation"), context.value(QStringLiteral("contextGeneration"))).toString()},
        {QStringLiteral("targetRevision"), context.value(QStringLiteral("targetRevision")).toString()},
        {QStringLiteral("fieldRevisions"), fields}};
    const auto bytes = QJsonDocument::fromVariant(scope).toJson(QJsonDocument::Compact);
    return bytes.size() <= 65536 ? QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) : QByteArray{};
}
bool AddonMetadataChoices::providerCurrent(const QVariantMap& registration, const QString& provider, const QString& partition)
{
    return !partition.isEmpty() && registration.value(QStringLiteral("id")) == provider
        && registration.value(QStringLiteral("enabled")).toBool() && registration.value(QStringLiteral("configured")).toBool()
        && registration.value(QStringLiteral("cachePartition")) == partition;
}
qint64 AddonMetadataChoices::selectionBytes(const QString& id, const Selection& value)
{
    return serializedBytes({{QStringLiteral("reference"), id}, {QStringLiteral("media"), value.media},
        {QStringLiteral("feature"), value.feature}, {QStringLiteral("provider"), value.provider},
        {QStringLiteral("partition"), value.partition}, {QStringLiteral("binding"), QString::fromLatin1(value.binding.toHex())},
        {QStringLiteral("identity"), value.identity}, {QStringLiteral("provenance"), value.provenance},
        {QStringLiteral("expires"), value.expires}});
}
QVariantMap AddonMetadataChoices::issue(const QString& featureInput, const QVariantMap& contextInput, const QVariantList& candidatesInput)
{
    const QVariantMap unavailable{{QStringLiteral("error"), tr("The identity choice context is not available.")}};
    if (blocked() || m_active || featureInput.isEmpty() || featureInput.size() > 128) return unavailable;
    const QPointer<AddonMetadataChoices> owner(this);
    m_active = true;
    const auto reset = qScopeGuard([owner] { if (owner) owner->m_active = false; });
    const auto feature = featureInput;
    const auto context = contextInput;
    const auto candidates = candidatesInput.mid(0, 500);
    const bool truncated = candidatesInput.size() > 500;
    if (!expire() || !owner) return unavailable;
    if (m_sets.size() >= m_options.maxSets)
        return {{QStringLiteral("error"), tr("The app cannot add more identity choice sets.")}};
    const auto epoch = m_epoch;
    const auto backend = m_backend;
    const auto options = m_options;
    const auto current = [owner, epoch] { return owner && !owner->blocked() && owner->m_epoch == epoch; };
    const auto media = context.value(QStringLiteral("media")).toString();
    if (media.isEmpty() || media.size() > 128 || !backend.mediaIdentity || !backend.registration
        || !backend.holdMedia || !backend.releaseMedia) return unavailable;
    const auto original = backend.mediaIdentity(media);
    if (!current() || !original) return unavailable;
    const auto scope = binding(context, *original);
    if (scope.isEmpty()) return unavailable;
    const auto setId = reference("metadata-choice-set:");
    Set set{media, feature, scope, {}, 0};
    QHash<QString, Choice> staged;
    QVariantList rows;
    const auto usedChoices = m_choices.size();
    const auto usedBytes = m_bytes;
    qint64 stagedBytes = 0;
    bool limited = truncated;
    for (const auto& value : candidates) {
        const auto candidate = value.toMap();
        const auto encoded = QJsonDocument::fromVariant(candidate).toJson(QJsonDocument::Compact);
        if (encoded.size() > 8192) continue;
        const auto entity = identity(candidate.value(QStringLiteral("identity")));
        const auto provider = candidate.value(QStringLiteral("provider")).toString();
        const auto provenance = candidate.value(QStringLiteral("provenance")).toMap();
        const auto partition = provenance.value(QStringLiteral("cachePartition")).toString();
        if (entity.isEmpty() || provenance.value(QStringLiteral("provider")) != provider) continue;
        const auto registration = backend.registration(feature, provider);
        if (!current()) return unavailable;
        if (!providerCurrent(registration, provider, partition)) continue;
        const auto choiceId = reference("metadata-choice:");
        QVariantMap source{{QStringLiteral("provider"), provider}, {QStringLiteral("feature"), feature}, {QStringLiteral("cachePartition"), partition}};
        const auto attribution = credit(candidate.value(QStringLiteral("attribution")));
        if (!attribution.isEmpty()) source.insert(QStringLiteral("attribution"), attribution);
        const auto bytes = std::max<qint64>(encoded.size(), serializedBytes({{QStringLiteral("reference"), choiceId},
            {QStringLiteral("set"), setId}, {QStringLiteral("provider"), provider}, {QStringLiteral("partition"), partition},
            {QStringLiteral("identity"), entity}, {QStringLiteral("provenance"), source}}));
        if (usedChoices + staged.size() >= options.maxChoices || bytes > options.maxBytes - usedBytes - stagedBytes) { limited = true; break; }
        staged.insert(choiceId, {setId, provider, partition, entity, source, bytes});
        stagedBytes += bytes; set.choices.append(choiceId);
        rows.append(QVariantMap{{QStringLiteral("reference"), choiceId}, {QStringLiteral("identity"), entity},
            {QStringLiteral("name"), entity.value(QStringLiteral("title"))}, {QStringLiteral("provider"), provider},
            {QStringLiteral("attribution"), attribution}, {QStringLiteral("requiresConfirmation"), true}});
    }
    if (rows.isEmpty()) return {{QStringLiteral("error"), tr("No identity choice is available within the host limits.")}};
    bool held = false;
    const auto balance = qScopeGuard([&held, release=backend.releaseMedia, media] { if (held) release(media); });
    held = backend.holdMedia(media);
    if (!current() || !held) return unavailable;
    const auto latest = backend.mediaIdentity(media);
    if (!current() || !latest || binding(context, *latest) != scope) return unavailable;
    const auto verification = staged.values();
    for (const auto& value : verification) {
        const auto registration = backend.registration(feature, value.provider);
        if (!current() || !providerCurrent(registration, value.provider, value.partition)) return unavailable;
    }
    if (!changed()) return unavailable;
    set.expires = m_clock.elapsed() + options.lifetimeMs;
    m_sets.insert(setId, set);
    for (auto it = staged.cbegin(); it != staged.cend(); ++it) m_choices.insert(it.key(), it.value());
    m_bytes += stagedBytes; held = false;
    if (!m_sweep.isActive()) m_sweep.start();
    QVariantMap result{{QStringLiteral("choiceSet"), setId}, {QStringLiteral("candidates"), rows}, {QStringLiteral("requiresSelection"), true},
        {QStringLiteral("limited"), limited}};
    if (limited) result.insert(QStringLiteral("warning"), tr("The app cannot show more identity choices within the host limit."));
    return result;
}
QString AddonMetadataChoices::select(const QString& choiceId, const QVariantMap& contextInput)
{
    if (blocked() || m_active) return {};
    const QPointer<AddonMetadataChoices> owner(this);
    m_active = true;
    const auto reset = qScopeGuard([owner] { if (owner) owner->m_active = false; });
    const auto context = contextInput;
    const auto choiceReference = choiceId;
    if (!expire() || !owner || m_selections.size() >= m_options.maxSelections || !m_choices.contains(choiceReference)) return {};
    const auto choice = m_choices.value(choiceReference);
    if (!m_sets.contains(choice.set)) return {};
    const auto set = m_sets.value(choice.set);
    if (set.expires <= m_clock.elapsed()) return {};
    const auto epoch = m_epoch;
    const auto backend = m_backend;
    const auto current = [owner, epoch] { return owner && !owner->blocked() && owner->m_epoch == epoch; };
    if (!backend.mediaIdentity || !backend.registration || !backend.holdMedia || !backend.releaseMedia) return {};
    const auto original = backend.mediaIdentity(set.media);
    if (!current() || !original || binding(context, *original) != set.binding) return {};
    const auto registration = backend.registration(set.feature, choice.provider);
    if (!current() || !providerCurrent(registration, choice.provider, choice.partition)) return {};
    bool held = false;
    const auto balance = qScopeGuard([&held, release=backend.releaseMedia, media=set.media] { if (held) release(media); });
    held = backend.holdMedia(set.media);
    if (!current() || !held) return {};
    const auto latest = backend.mediaIdentity(set.media);
    if (!current() || !latest || binding(context, *latest) != set.binding) return {};
    const auto latestProvider = backend.registration(set.feature, choice.provider);
    if (!current() || !providerCurrent(latestProvider, choice.provider, choice.partition)) return {};
    const auto id = reference("metadata-selection:");
    Selection value{set.media, set.feature, choice.provider, choice.partition, set.binding, choice.identity, choice.provenance,
        m_clock.elapsed() + m_options.lifetimeMs, 0};
    value.bytes = selectionBytes(id, value);
    if (set.expires <= m_clock.elapsed() || value.bytes > m_options.maxBytes - m_bytes || !changed()) return {};
    m_bytes += value.bytes; m_selections.insert(id, value); held = false;
    if (!m_sweep.isActive()) m_sweep.start();
    return id;
}
QVariantMap AddonMetadataChoices::selected(const QString& id, const QVariantMap& contextInput) const
{
    if (blocked() || m_active || !m_selections.contains(id)) return {};
    const auto value = m_selections.value(id);
    if (value.expires <= m_clock.elapsed()) return {};
    const QPointer<AddonMetadataChoices> owner(const_cast<AddonMetadataChoices*>(this));
    m_active = true;
    const auto reset = qScopeGuard([owner] { if (owner) owner->m_active = false; });
    const auto epoch = m_epoch;
    const auto backend = m_backend;
    const auto context = contextInput;
    const auto current = [owner, epoch] { return owner && !owner->blocked() && owner->m_epoch == epoch; };
    if (!backend.mediaIdentity || !backend.registration) return {};
    const auto original = backend.mediaIdentity(value.media);
    if (!current() || !original || binding(context, *original) != value.binding) return {};
    const auto registration = backend.registration(value.feature, value.provider);
    if (!current() || !providerCurrent(registration, value.provider, value.partition)) return {};
    if (value.expires <= m_clock.elapsed()) return {};
    return {{QStringLiteral("identity"), value.identity}, {QStringLiteral("provenance"), value.provenance}};
}
void AddonMetadataChoices::releaseSet(const QString& id)
{
    if (!m_sets.contains(id)) return;
    const auto release = m_backend.releaseMedia;
    changed(); const auto set = m_sets.take(id);
    for (const auto& choice : set.choices) m_bytes -= m_choices.take(choice).bytes;
    if (m_sets.isEmpty() && m_selections.isEmpty()) m_sweep.stop();
    if (release) release(set.media);
}
void AddonMetadataChoices::releaseSelection(const QString& id)
{
    if (!m_selections.contains(id)) return;
    const auto release = m_backend.releaseMedia;
    changed(); const auto value = m_selections.take(id); m_bytes -= value.bytes;
    if (m_sets.isEmpty() && m_selections.isEmpty()) m_sweep.stop();
    if (release) release(value.media);
}
void AddonMetadataChoices::invalidate()
{
    if (m_invalidating) return;
    const QPointer<AddonMetadataChoices> owner(this);
    m_invalidating = true; changed(); m_sweep.stop();
    const auto release = m_backend.releaseMedia;
    QHash<QString, Set> sets; sets.swap(m_sets);
    QHash<QString, Selection> selections; selections.swap(m_selections);
    QHash<QString, Choice> choices; choices.swap(m_choices);
    m_bytes = 0;
    QStringList holds;
    for (const auto& set : sets) holds.append(set.media);
    for (const auto& selection : selections) holds.append(selection.media);
    // Detached holds must all end even if the first callback deletes this store.
    if (release) for (const auto& media : holds) release(media);
    if (owner) owner->m_invalidating = false;
}
bool AddonMetadataChoices::expire()
{
    if (blocked()) return false;
    const auto now = m_clock.elapsed();
    QStringList sets, selections;
    for (auto it = m_sets.cbegin(); it != m_sets.cend(); ++it) if (it->expires <= now) sets.append(it.key());
    for (auto it = m_selections.cbegin(); it != m_selections.cend(); ++it) if (it->expires <= now) selections.append(it.key());
    if (sets.isEmpty() && selections.isEmpty()) return true;
    const QPointer<AddonMetadataChoices> owner(this);
    const auto release = m_backend.releaseMedia;
    changed();
    QStringList holds;
    for (const auto& id : sets) {
        const auto set = m_sets.take(id); holds.append(set.media);
        for (const auto& choice : set.choices) m_bytes -= m_choices.take(choice).bytes;
    }
    for (const auto& id : selections) {
        const auto value = m_selections.take(id); holds.append(value.media); m_bytes -= value.bytes;
    }
    if (m_sets.isEmpty() && m_selections.isEmpty()) m_sweep.stop();
    const auto epoch = m_epoch;
    if (release) for (const auto& media : holds) release(media);
    return owner && !owner->blocked() && owner->m_epoch == epoch;
}
