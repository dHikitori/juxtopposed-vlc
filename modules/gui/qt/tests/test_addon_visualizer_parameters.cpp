// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QJsonArray>
#include "addons/addon_visualizer_parameters.hpp"
using namespace vlc::addons;

class TestAddonVisualizerParameters : public QObject
{
    Q_OBJECT
    static QJsonObject scene()
    {
        return {{"contract", "visualizer.scene@1"}, {"background", "theme.background"},
            {"nodes", QJsonArray{QJsonObject{{"type", "group"}, {"id", "group"}, {"rotation", 0}, {"children", QJsonArray{
                QJsonObject{{"type", "spectrum"}, {"id", "bars"}, {"count", 32}, {"gain", 1}, {"color", "theme.accent"}}}}}}},
            {"parameters", QJsonObject{{"gain", QJsonObject{{"node", "bars"}, {"property", "gain"}, {"minimum", 0}, {"maximum", 4}}},
                {"rotation", QJsonObject{{"node", "group"}, {"property", "rotation"}, {"minimum", -90}, {"maximum", 90}}}}}};
    }
private slots:
    void approvedFieldsCanChangeNestedNodes()
    {
        const auto original = scene(); QVERIFY(AddonVisualizerParameters::validate(original)); QJsonObject changed;
        QVERIFY(AddonVisualizerParameters::apply(original, {{"gain", 3.5}, {"rotation", 30}}, &changed));
        const auto group = changed.value("nodes").toArray()[0].toObject(); QCOMPARE(group.value("rotation").toDouble(), 30);
        QCOMPARE(group.value("children").toArray()[0].toObject().value("gain").toDouble(), 3.5);
        QCOMPARE(original.value("nodes").toArray()[0].toObject().value("rotation").toDouble(), 0);
        QJsonObject restored; QVERIFY(AddonVisualizerParameters::apply(original, {}, &restored)); QCOMPARE(restored, original);
    }
    void incorrectUpdatesLeaveTheOutputUnchanged()
    {
        const auto document = scene();
        for (const auto& update : QList<QJsonObject>{{{"gain", 5}}, {{"gain", "3"}}, {{"gain", true}}, {{"unknown", 1}}, {{"gain", 2}, {"rotation", 100}}}) {
            QJsonObject output{{"sentinel", true}}; QString error;
            QVERIFY(!AddonVisualizerParameters::apply(document, update, &output, &error)); QVERIFY(!error.isEmpty());
            QCOMPARE(output, QJsonObject({{"sentinel", true}}));
        }
    }
    void declarationsCannotNameOtherFieldsOrBroaderRanges()
    {
        const auto original = scene();
        for (const auto& invalid : QList<QJsonObject>{
            {{"node", "bars"}, {"property", "color"}, {"minimum", 0}, {"maximum", 1}},
            {{"node", "bars"}, {"property", "gain"}, {"minimum", -1}, {"maximum", 9}},
            {{"node", "unknown"}, {"property", "gain"}, {"minimum", 0}, {"maximum", 2}},
            {{"node", "bars"}, {"property", "count"}, {"minimum", 1.5}, {"maximum", 64}},
            {{"node", "bars"}, {"property", "gain"}, {"minimum", 2}, {"maximum", 4}},
            {{"node", "bars"}, {"property", "gain"}, {"minimum", 0}, {"maximum", 4}, {"url", "https://example.invalid"}}}) {
            auto document = original; auto declarations = document.value("parameters").toObject(); declarations.insert("gain", invalid); document.insert("parameters", declarations);
            QVERIFY(!AddonVisualizerParameters::validate(document));
        }
    }
    void duplicateIdsAndParameterTargetsFail()
    {
        auto document = scene(); auto nodes = document.value("nodes").toArray(); nodes.append(nodes[0]); document.insert("nodes", nodes);
        QVERIFY(!AddonVisualizerParameters::validate(document));
        document = scene(); auto declarations = document.value("parameters").toObject(); declarations.insert("second", declarations.value("gain")); document.insert("parameters", declarations);
        QVERIFY(!AddonVisualizerParameters::validate(document));
        document = scene(); declarations = document.value("parameters").toObject(); declarations.insert("__proto__", declarations.value("gain")); document.insert("parameters", declarations);
        QVERIFY(!AddonVisualizerParameters::validate(document));
    }
    void updatesKeepTheTotalParticleLimit()
    {
        QJsonArray nodes; QJsonObject declarations;
        for (int index = 0; index < 2; ++index) {
            const auto id = QStringLiteral("particles%1").arg(index);
            nodes.append(QJsonObject{{"type", "particles"}, {"id", id}, {"count", 512}});
            declarations.insert(id, QJsonObject{{"node", id}, {"property", "count"}, {"minimum", 1}, {"maximum", 2048}});
        }
        const QJsonObject document{{"nodes", nodes}, {"parameters", declarations}}; QVERIFY(AddonVisualizerParameters::validate(document)); QJsonObject output;
        QVERIFY(AddonVisualizerParameters::apply(document, {{"particles0", 1024}, {"particles1", 1024}}, &output));
        QVERIFY(!AddonVisualizerParameters::apply(document, {{"particles0", 2048}, {"particles1", 2048}}, &output));
        QVERIFY(!AddonVisualizerParameters::apply(document, {{"particles0", 1.5}}, &output));
    }
};
QTEST_GUILESS_MAIN(TestAddonVisualizerParameters)
#include "test_addon_visualizer_parameters.moc"
