// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_metadata_family_plan.hpp"
#include <QHash>
#include <QSet>

const QList<AddonMetadataFamilyPlan::Binding>& AddonMetadataFamilyPlan::bindings()
{
    static const QList<Binding> value{
        {QStringLiteral("musicbrainz"), QStringLiteral("hakone.builtin.musicbrainz/artwork"), QStringLiteral("org.videolan.musicbrainz/artwork")},
        {QStringLiteral("google-images"), QStringLiteral("hakone.builtin.google-images/artwork"), QStringLiteral("org.videolan.google-images/artwork")},
        {QStringLiteral("french-tv"), QStringLiteral("hakone.builtin.french-tv/artwork"), QStringLiteral("org.videolan.french-tv/artwork")},
        {QStringLiteral("lastfm"), QStringLiteral("hakone.builtin.lastfm/artwork"), QStringLiteral("org.videolan.lastfm/artwork")}};
    return value;
}
QVariantList AddonMetadataFamilyPlan::select(const QString& feature, const QString& operation,
    const QVariantList& eligible, const QStringList& explicitOrder, bool nativeScope)
{
    if (feature != QLatin1String("artwork")) return eligible;
    QHash<QString, QVariant> rows; QStringList inputOrder;
    for (const auto& value : eligible) {
        const auto row = value.toMap(); const auto id = row.value(QStringLiteral("id")).toString();
        if (id.isEmpty() || rows.contains(id) || !row.value(QStringLiteral("enabled")).toBool()
            || !row.value(QStringLiteral("configured")).toBool()) continue;
        rows.insert(id, value); inputOrder.append(id);
    }
    QSet<QString> familyMembers, selected;
    for (const auto& binding : bindings()) {
        familyMembers.insert(binding.nativeContribution); familyMembers.insert(binding.portableContribution);
        const bool native = nativeScope && operation == QLatin1String("enrich") && rows.contains(binding.nativeContribution);
        const bool portable = rows.contains(binding.portableContribution);
        QString choice;
        for (const auto& id : explicitOrder)
            if ((native && id == binding.nativeContribution) || (portable && id == binding.portableContribution)) { choice = id; break; }
        if (choice.isEmpty()) choice = native ? binding.nativeContribution : portable ? binding.portableContribution : QString{};
        if (!choice.isEmpty()) selected.insert(choice);
    }
    QStringList order = explicitOrder;
    if (nativeScope && operation == QLatin1String("enrich"))
        for (const auto& binding : bindings()) if (!order.contains(binding.nativeContribution)) order.append(binding.nativeContribution);
    for (const auto& id : inputOrder) if (!order.contains(id)) order.append(id);
    QVariantList result; QSet<QString> emitted;
    for (const auto& id : order) {
        if (!rows.contains(id) || emitted.contains(id) || (familyMembers.contains(id) && !selected.contains(id))) continue;
        emitted.insert(id); result.append(rows.value(id));
    }
    return result;
}
