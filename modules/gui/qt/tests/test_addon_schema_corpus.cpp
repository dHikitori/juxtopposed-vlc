// SPDX-License-Identifier: GPL-2.0-or-later
// The host side of the shared schema corpus (contrib-addons/sdk/schemas).
// Each case is {"description", "valid", "value", "context"?}. A valid case
// must be accepted in full by the host's own validator; an invalid case must
// be refused, or lose content. check-corpus.py runs the same cases through
// the published JSON Schemas.
#include "addons/addon_device_authorization.hpp"
#include "addons/addon_discovery_coordinator.hpp"
#include "addons/addon_manifest.hpp"
#include "addons/addon_result_retention.hpp"
#include "addons/addon_panel_icon.hpp"
#include "addons/addon_ui_registry.hpp"
#include "addons/addon_visual_scene.hpp"
#include <QDir>
#include <QFile>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>
using namespace vlc::addons;

namespace {
const QString Root = QStringLiteral(VLC_ADDON_SOURCE_ROOT);
QJsonObject read(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject{};
}
QStringList strings(const QJsonValue& value)
{
    QStringList result;
    for (const auto& item : value.toArray()) result.append(item.toString());
    return result;
}
bool vlcAnnotation(const QJsonValue& value)
{
    if (value.isObject()) {
        const auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
            if (it.key().startsWith(QLatin1String("x-vlc-")) || vlcAnnotation(it.value())) return true;
    } else if (value.isArray()) {
        for (const auto& item : value.toArray()) if (vlcAnnotation(item)) return true;
    }
    return false;
}
}

class TestAddonSchemaCorpus : public QObject
{
    Q_OBJECT
    QJSEngine m_engine;
    QJSValue m_ui;
    bool loadUi()
    {
        if (!m_ui.isUndefined()) return true;
        QFile file(Root + QStringLiteral("/modules/gui/qt/player/qml/redesign/AddonUi.js"));
        if (!file.open(QIODevice::ReadOnly)) return false;
        // The QML library pragma is not JavaScript; the functions are.
        auto source = QString::fromUtf8(file.readAll()).replace(QStringLiteral(".pragma library"), QString{});
        const auto result = m_engine.evaluate(source, file.fileName());
        if (result.isError()) return false;
        m_ui = m_engine.globalObject();
        return true;
    }
    QString callUi(const QString& function, const QList<QJsonValue>& arguments)
    {
        QJSValueList values;
        // Plain JavaScript values, as the app passes them (AddonUi.plain):
        // a converted QVariantList is not an Array in Qt 6.8.
        const auto parse = m_engine.globalObject().property(QStringLiteral("JSON")).property(QStringLiteral("parse"));
        for (const auto& argument : arguments)
            values.append(parse.call({QString::fromUtf8(QJsonDocument(QJsonArray{argument}).toJson(QJsonDocument::Compact))}).property(0));
        const auto result = m_ui.property(function).call(values);
        return result.isError() ? result.toString() : result.toString();
    }
    // True when the host accepts the whole value.
    bool accepted(const QString& kind, const QJsonValue& value, const QJsonObject& context, QString* detail)
    {
        if (kind == QLatin1String("manifest")) {
            Manifest manifest;
            return Manifest::parse(QJsonDocument(value.toObject()).toJson(), &manifest, detail);
        }
        if (kind == QLatin1String("settings")) {
            if (!loadUi()) { *detail = QStringLiteral("AddonUi.js did not load"); return false; }
            *detail = callUi(QStringLiteral("validateSettingsSchema"), QList<QJsonValue>{value});
            if (!detail->isEmpty()) return false;
            if (vlcAnnotation(value)) { *detail = QStringLiteral("x-vlc annotation"); return false; }
            const auto properties = value.toObject().value("properties").toObject();
            for (auto it = properties.begin(); it != properties.end(); ++it) {
                const auto field = it.value().toObject();
                if (field.value("x-hakone-control").toString() != QLatin1String("account")) continue;
                if (!DeviceRecipe::from(field.value("x-hakone-account").toObject(), strings(context.value("networkOrigins")), detail)) return false;
            }
            return true;
        }
        if (kind == QLatin1String("ui-view")) {
            if (!loadUi()) { *detail = QStringLiteral("AddonUi.js did not load"); return false; }
            *detail = callUi(QStringLiteral("validateView"), QList<QJsonValue>{value, context.value("commands").isArray() ? context.value("commands") : QJsonValue(QJsonArray{})});
            return detail->isEmpty();
        }
        if (kind == QLatin1String("visual-scene")) return AddonVisualScene::validate(value.toObject(), detail);
        if (kind == QLatin1String("metadata-result")) {
            // The worker's envelope rules and the coordinator's retention record.
            static const QStringList statuses{"match", "partial", "no_match", "auth_required", "rate_limited", "offline",
                "ambiguous", "unsupported", "error"};
            static const QStringList keys{"type", "id", "status", "data", "error"};
            const auto message = value.toObject();
            for (auto it = message.begin(); it != message.end(); ++it)
                if (!keys.contains(it.key())) { *detail = QStringLiteral("key ") + it.key(); return false; }
            const auto status = message.value("status").toString();
            if (message.value("type") != QStringLiteral("result") || !message.value("id").isString() || message.value("id").toString().isEmpty()
                || message.value("id").toString().size() > 128 || !statuses.contains(status) || !message.value("data").isObject()
                || (message.contains("error") && !message.value("error").isString())) { *detail = QStringLiteral("envelope"); return false; }
            if (!ResultRetention::from(status, message.value("data").toObject().toVariantMap()).valid) { *detail = QStringLiteral("retention"); return false; }
            return true;
        }
        if (kind == QLatin1String("discovery-page")) {
            // One reply through a real coordinator: accepted in full when the
            // page keeps every row and its revision, check and cursor.
            const auto operation = context.value("operation").toString(QStringLiteral("roots"));
            const auto source = QStringLiteral("org.example.discovery/catalogue");
            QString invocation;
            AddonDiscoveryCoordinator::Backend backend;
            backend.sources = [source] {
                return QVariantList{QVariantMap{{"id", source}, {"enabled", true}, {"configured", true}, {"cachePartition", "p"},
                    {"accountReference", "a"}, {"operations", QStringList{"roots", "suggest", "subscribe"}}}};
            };
            backend.invoke = [&invocation](const QString&, const QString&, const QVariantMap&) { invocation = QStringLiteral("call-1"); return invocation; };
            AddonDiscoveryCoordinator coordinator(backend);
            QSignalSpy done(&coordinator, &AddonDiscoveryCoordinator::finished);
            if (operation == QLatin1String("suggest")) coordinator.suggest(source, {}, context.value("limit").toInt(50), "view", 1);
            else if (operation == QLatin1String("subscribe")) coordinator.subscribe(source, {}, {}, "view", 1);
            else coordinator.roots(source, "view", 1);
            if (!QTest::qWaitFor([&] { return !invocation.isEmpty(); }, 2000)) { *detail = QStringLiteral("no call"); return false; }
            const auto data = value.toObject();
            coordinator.operationFinished(invocation, source, context.value("status").toString(QStringLiteral("match")), data.toVariantMap(), {});
            if (!QTest::qWaitFor([&] { return !done.isEmpty(); }, 2000)) { *detail = QStringLiteral("no answer"); return false; }
            const auto status = done.first().at(1).toString();
            const auto result = done.first().at(2).toMap();
            *detail = status;
            if (status != QLatin1String("match") && status != QLatin1String("no_match")) return false;
            const auto entries = data.value("items").toArray().size() + data.value("nodes").toArray().size();
            if (result.value("page").toList().size() != entries) { *detail = QStringLiteral("rows dropped"); return false; }
            if (data.contains("revision") && (!data.value("revision").isString() || !result.contains("revision")
                    || result.value("revision").toString() != data.value("revision").toString())) { *detail = QStringLiteral("revision dropped"); return false; }
            if (data.contains("nextCheckSeconds") && (!result.contains("nextCheckSeconds")
                    || double(result.value("nextCheckSeconds").toInt()) != data.value("nextCheckSeconds").toDouble())) {
                *detail = QStringLiteral("check interval changed"); return false;
            }
            if (data.contains("cursor") && !data.value("cursor").isNull() && !result.contains("cursor")) { *detail = QStringLiteral("cursor dropped"); return false; }
            return true;
        }
        *detail = QStringLiteral("unknown kind");
        return false;
    }

private slots:
    void corpus_data()
    {
        QTest::addColumn<QString>("kind"); QTest::addColumn<QString>("path");
        const QDir root(Root + QStringLiteral("/contrib-addons/sdk/schemas/corpus"));
        int cases = 0;
        for (const auto& kind : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
            for (const auto& name : QDir(root.filePath(kind)).entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
                QTest::newRow(qPrintable(kind + QLatin1Char('/') + name)) << kind << QDir(root.filePath(kind)).filePath(name);
                ++cases;
            }
        if (!cases) QTest::newRow("empty") << QString{} << QString{};
    }
    // A final newline is not part of a token. PCRE's "$" accepts one before
    // it, and so does Python's re, so the shared corpus cannot hold these.
    void aFinalNewlineIsNeverPartOfAToken()
    {
        const QDir corpus(Root + QStringLiteral("/contrib-addons/sdk/schemas/corpus"));
        auto manifest = read(corpus.filePath(QStringLiteral("manifest/01-theme-minimal.json"))).value("value").toObject();
        QVERIFY(!manifest.isEmpty());
        QString detail;
        QVERIFY2(accepted(QStringLiteral("manifest"), manifest, {}, &detail), qPrintable(detail));
        manifest.insert("id", manifest.value("id").toString() + QLatin1Char('\n'));
        QVERIFY(!accepted(QStringLiteral("manifest"), manifest, {}, &detail));
        const QJsonObject recipe{{"recipe", "oauth-device"}, {"deviceAuthorizationUrl", "https://auth.example.org/device"},
            {"tokenUrl", "https://auth.example.org/token"}, {"clientId", "vlc\n"}};
        QVERIFY(!DeviceRecipe::from(recipe, {QStringLiteral("https://auth.example.org")}));
        const QJsonObject page{{"items", QJsonArray{QJsonObject{{"id", "a"}, {"kind", "media"}, {"title", "A"}}}}, {"revision", "r1\n"}};
        QVERIFY(!accepted(QStringLiteral("discovery-page"), page, {}, &detail));
        QCOMPARE(detail, QStringLiteral("revision dropped"));
    }
    // The bundled Episodes package and the sample state its renderer makes
    // (examples/episodes/tests/sample-state.json) pass the app's own rules.
    void theEpisodesPackageAgreesWithTheApp()
    {
        const QDir package(Root + QStringLiteral("/contrib-addons/examples/episodes"));
        QFile file(package.filePath(QStringLiteral("addon.json"))); QVERIFY(file.open(QIODevice::ReadOnly));
        Manifest manifest; QString error;
        QVERIFY2(Manifest::parse(file.readAll(), &manifest, &error), qPrintable(error));
        const auto* panel = manifest.contribution(QStringLiteral("episodes"));
        QVERIFY(panel && panel->type == QLatin1String("ui.panel"));
        QJsonArray commands;
        for (const auto& contribution : manifest.contributions)
            if (contribution.type == QLatin1String("ui.command")) commands.append(manifest.id + QLatin1Char('/') + contribution.id);
        const auto view = read(package.filePath(QStringLiteral("views/episodes.json")));
        QVERIFY(AddonUiRegistry::validViewDocument(view));
        QVERIFY(loadUi());
        QCOMPARE(callUi(QStringLiteral("validateView"), {view, commands}), QString());
        const auto state = read(package.filePath(QStringLiteral("tests/sample-state.json")));
        QVERIFY(!state.isEmpty());
        QVERIFY(validateUiValue(panel->document.value("stateSchema").toObject(), state));
        QCOMPARE(callUi(QStringLiteral("validateProjection"), {view.value("root"), state, QJsonValue(), QJsonObject{}}), QString());
        QCOMPARE(callUi(QStringLiteral("validateProjection"), {view.value("root"), panel->document.value("state"), QJsonValue(), QJsonObject{}}), QString());
        QFile icon(package.filePath(QStringLiteral("icons/episodes.svg"))); QVERIFY(icon.open(QIODevice::ReadOnly));
        QVERIFY(panelIcon::markup(icon.readAll()).contains(QStringLiteral("COLOR")));
    }
    void corpus()
    {
        QFETCH(QString, kind); QFETCH(QString, path);
        QVERIFY2(!path.isEmpty(), "The schema corpus has no cases.");
        const auto entry = read(path);
        QVERIFY2(entry.contains("valid") && entry.contains("value"), qPrintable(path));
        QString detail;
        const bool host = accepted(kind, entry.value("value"), entry.value("context").toObject(), &detail);
        QVERIFY2(host == entry.value("valid").toBool(),
                 qPrintable(QStringLiteral("%1: host %2 (%3)").arg(entry.value("description").toString(),
                     host ? QStringLiteral("accepts") : QStringLiteral("refuses"), detail)));
    }
};
QTEST_GUILESS_MAIN(TestAddonSchemaCorpus)
#include "test_addon_schema_corpus.moc"
