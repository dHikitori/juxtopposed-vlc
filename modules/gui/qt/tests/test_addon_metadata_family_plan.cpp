// SPDX-License-Identifier: GPL-2.0-or-later
#include <QtTest/QtTest>
#include "addons/addon_metadata_family_plan.hpp"

namespace {
QVariantMap row(const QString& id, bool enabled = true, bool configured = true)
{ return {{QStringLiteral("id"), id}, {QStringLiteral("enabled"), enabled}, {QStringLiteral("configured"), configured}}; }
QStringList ids(const QVariantList& rows)
{ QStringList values; for (const auto& value : rows) values.append(value.toMap().value(QStringLiteral("id")).toString()); return values; }
QVariantList all()
{
    QVariantList values;
    for (const auto& binding : AddonMetadataFamilyPlan::bindings()) values.append(row(binding.portableContribution));
    for (const auto& binding : AddonMetadataFamilyPlan::bindings()) values.append(row(binding.nativeContribution));
    return values;
}
}
class TestAddonMetadataFamilyPlan : public QObject
{
    Q_OBJECT
private slots:
    void nativeDefaultsRetainTheOriginalFamilyOrder()
    {
        auto values = all(); values.prepend(row(QStringLiteral("third-party/artwork")));
        const auto result = ids(AddonMetadataFamilyPlan::select(QStringLiteral("artwork"), QStringLiteral("enrich"), values, {}, true));
        QStringList expected; for (const auto& binding : AddonMetadataFamilyPlan::bindings()) expected.append(binding.nativeContribution);
        expected.append(QStringLiteral("third-party/artwork")); QCOMPARE(result, expected);
    }
    void explicitPortableChoiceRemovesItsNativeDuplicate()
    {
        const auto& music = AddonMetadataFamilyPlan::bindings().first(); auto values = all();
        values.append(row(QStringLiteral("third-party/artwork")));
        const QStringList order{QStringLiteral("third-party/artwork"), music.portableContribution, music.nativeContribution};
        const auto result = ids(AddonMetadataFamilyPlan::select(QStringLiteral("artwork"), QStringLiteral("enrich"), values, order, true));
        QCOMPARE(result.first(), order.first()); QCOMPARE(result.at(1), music.portableContribution);
        QVERIFY(!result.contains(music.nativeContribution)); QCOMPARE(result.size(), 5);
    }
    void disabledOrUnconfiguredImplementationsCannotEnterThePlan()
    {
        const auto& music = AddonMetadataFamilyPlan::bindings().first();
        for (const bool enabled : {false, true}) {
            const QVariantList values{row(music.nativeContribution, enabled, !enabled), row(music.portableContribution)};
            QCOMPARE(ids(AddonMetadataFamilyPlan::select(QStringLiteral("artwork"), QStringLiteral("enrich"), values,
                {music.nativeContribution}, true)), QStringList{music.portableContribution});
        }
    }
    void anUnscopedOrAssetSearchRequestCannotChooseNativeExecution()
    {
        for (const auto& operation : {QStringLiteral("enrich"), QStringLiteral("searchAssets"), QStringLiteral("resolveAsset")})
            for (const bool nativeScope : {false, true}) {
                if (nativeScope && operation == QLatin1String("enrich")) continue;
                const auto result = ids(AddonMetadataFamilyPlan::select(QStringLiteral("artwork"), operation, all(), {}, nativeScope));
                QStringList expected; for (const auto& binding : AddonMetadataFamilyPlan::bindings()) expected.append(binding.portableContribution);
                QCOMPARE(result, expected);
            }
    }
    void unknownProvidersKeepTheirOrderAndEachFamilyHasOneMember()
    {
        auto values = all(); values.prepend(row(QStringLiteral("other/first"))); values.append(row(QStringLiteral("other/last")));
        const QStringList explicitOrder{QStringLiteral("other/last"), QStringLiteral("org.videolan.google-images/artwork"),
            QStringLiteral("other/first")};
        const auto result = ids(AddonMetadataFamilyPlan::select(QStringLiteral("artwork"), QStringLiteral("enrich"), values, explicitOrder, true));
        QCOMPARE(result.mid(0, 3), explicitOrder);
        for (const auto& binding : AddonMetadataFamilyPlan::bindings())
            QCOMPARE(int(result.contains(binding.nativeContribution)) + int(result.contains(binding.portableContribution)), 1);
        const auto unchanged = AddonMetadataFamilyPlan::select(QStringLiteral("video.details"), QStringLiteral("enrich"), values, {}, true);
        QCOMPARE(unchanged, values);
    }
};
QTEST_GUILESS_MAIN(TestAddonMetadataFamilyPlan)
#include "test_addon_metadata_family_plan.moc"
