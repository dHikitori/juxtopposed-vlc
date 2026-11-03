// SPDX-License-Identifier: GPL-2.0-or-later
// vlc-addon-preview: render an add-on package's themes and views with the
// app's own components, without running package code.
//
//   vlc-addon-preview --qml <redesign QML folder> --output <folder> <package folder>
//
// For each theme it renders sample controls in the dark and light variants,
// also with high contrast. For each view (ui.route, ui.modal, ui.section,
// ui.replacement, ui.panel) it renders the document with its declared initial state,
// in both variants. It writes PNG files and prints their names. Run it with
// QT_QPA_PLATFORM=offscreen on a machine without a display.
#include "addons/addon_package_store.hpp"
#include "addons/addon_theme_document.hpp"
#include <QDir>
#include <QJSValue>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

using namespace vlc::addons;

namespace {
// The theme service the app components read, as a plain script object
// that returns the tokens the host resolved for each variant.
QJSValue themeService(QQmlEngine& engine, const QJsonObject& document, bool highContrast)
{
    // Without a theme the app components use their own defaults.
    const auto tokens = [&](bool light) {
        return document.isEmpty() ? QStringLiteral("undefined") : QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(
            themeDocument::resolve(document, light, {}, highContrast, false))).toJson(QJsonDocument::Compact));
    };
    return engine.evaluate(QStringLiteral("({revision: 1, tokens: function(light) { return light ? %1 : %2 }})")
        .arg(tokens(true), tokens(false)));
}

const char* const ThemeScene = R"QML(
import QtQuick
import QtQuick.Layouts
import "%1"
Rectangle {
    width: 640; height: 420
    readonly property RedesignTheme theme: RedesignTheme { lightTheme: %2 }
    color: theme.background
    RedesignDialogSurface { anchors.fill: parent; anchors.margins: 24; theme: parent.theme }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 48; spacing: 14
        Text { text: "Theme preview"; color: parent.parent.theme.bright; font.pixelSize: 22 }
        Text { text: "Body text in the secondary color."; color: parent.parent.theme.foreground; font.pixelSize: 14 }
        RowLayout {
            spacing: 10
            RedesignButton { caption: "Primary"; text: caption; primaryAction: true; lightTheme: %2 }
            RedesignButton { caption: "Secondary"; text: caption; lightTheme: %2 }
            RedesignButton { caption: "Disabled"; text: caption; enabled: false; lightTheme: %2 }
        }
        RedesignComboBox { model: ["First choice", "Second choice"]; Layout.preferredWidth: 260; lightTheme: %2 }
        RedesignSwitchRow { label: "A switch that is on"; checked: true; Layout.fillWidth: true; lightTheme: %2 }
        RedesignSwitchRow { label: "A switch that is off"; Layout.fillWidth: true; lightTheme: %2 }
    }
}
)QML";

const char* const ViewScene = R"QML(
import QtQuick
import "%1"
Rectangle {
    width: 640; height: 480
    readonly property RedesignTheme theme: RedesignTheme { lightTheme: %2 }
    color: theme.panel
    RedesignAddonView {
        objectName: "view"
        x: 16; y: 16; width: parent.width - 32
        lightTheme: %2
        document: %3
        state: %4
        registeredCommands: %5
    }
}
)QML";

bool render(QQmlEngine& engine, const QString& source, const QString& output, QString* error)
{
    QQuickView view(&engine, nullptr);
    view.setResizeMode(QQuickView::SizeViewToRootObject);
    view.setColor(Qt::black);
    QQmlComponent component(&engine);
    component.setData(source.toUtf8(), QUrl::fromLocalFile(QDir::current().filePath(QStringLiteral("preview.qml"))));
    auto* root = qobject_cast<QQuickItem*>(component.create());
    if (!root) { *error = component.errorString(); return false; }
    view.setContent(QUrl(), &component, root);
    view.show();
    // Let bindings, layouts and the first frame settle.
    QEventLoop wait; QTimer::singleShot(150, &wait, &QEventLoop::quit); wait.exec();
    if (auto* item = root->findChild<QObject*>(QStringLiteral("view")); item && !item->property("valid").toBool()) {
        *error = QStringLiteral("The view document is not valid: ") + item->property("errorMessage").toString();
        return false;
    }
    const auto image = view.grabWindow();
    if (image.isNull() || !image.save(output)) { *error = QStringLiteral("Cannot write ") + output; return false; }
    return true;
}
QString json(const QJsonValue& value)
{ return QString::fromUtf8(QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact)).mid(1).chopped(1); }
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QTextStream out(stdout), err(stderr);
    QString qmlDir, outputDir, folder;
    const auto arguments = app.arguments().mid(1);
    for (int i = 0; i < arguments.size(); ++i) {
        if (arguments[i] == QLatin1String("--qml") && i + 1 < arguments.size()) qmlDir = arguments[++i];
        else if (arguments[i] == QLatin1String("--output") && i + 1 < arguments.size()) outputDir = arguments[++i];
        else folder = arguments[i];
    }
    if (qmlDir.isEmpty() || outputDir.isEmpty() || folder.isEmpty()) {
        err << "Usage: vlc-addon-preview --qml <redesign QML folder> --output <folder> <package folder>\n";
        return 2;
    }
    Manifest manifest; PackageCandidate candidate; QString error;
    if (!Manifest::loadDirectory(folder, &manifest, nullptr, &error)) { err << "INVALID " << error << '\n'; return 1; }
    ReleaseIdentity identity; identity.source = manifest.source.toString(); identity.tag = QStringLiteral("v") + manifest.version;
    identity.developerSource = true;
    if (!PackageStore::inspectDirectory(folder, identity, &candidate, &error)) { err << "INVALID " << error << '\n'; return 1; }
    QDir().mkpath(outputDir);
    const QString qml = QUrl::fromLocalFile(QDir(qmlDir).absolutePath()).toString();
    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("redesignAppearance"),
        QVariant::fromValue(engine.evaluate(QStringLiteral("({backdropBlurEnabled: false})"))));
    const auto useTheme = [&](const QJsonObject& document, bool contrast) {
        engine.rootContext()->setContextProperty(QStringLiteral("redesignThemeService"), QVariant::fromValue(themeService(engine, document, contrast)));
    };
    useTheme({}, false);
    int written = 0;
    bool failed = false;
    const auto write = [&](const QString& source, const QString& name) {
        const auto path = QDir(outputDir).filePath(name);
        QString renderError;
        if (render(engine, source, path, &renderError)) { out << path << '\n'; ++written; }
        else { err << name << ": " << renderError << '\n'; failed = true; }
    };
    for (const auto& contribution : manifest.contributions) {
        if (contribution.type == QLatin1String("theme")) {
            const auto resource = contribution.document.value(QStringLiteral("resource")).toString();
            const auto document = QJsonDocument::fromJson(candidate.contents.value(resource)).object();
            for (const bool contrast : {false, true})
                for (const bool light : {false, true}) {
                    useTheme(document, contrast);
                    write(QString::fromLatin1(ThemeScene).arg(qml, light ? QStringLiteral("true") : QStringLiteral("false")),
                          QStringLiteral("theme-%1-%2%3.png").arg(contribution.id, light ? QStringLiteral("light") : QStringLiteral("dark"),
                              contrast ? QStringLiteral("-contrast") : QString{}));
                }
            useTheme({}, false);
        } else if (QStringList{"ui.route", "ui.modal", "ui.section", "ui.replacement", "ui.panel"}.contains(contribution.type)) {
            const auto viewPath = contribution.document.value(QStringLiteral("view")).toString();
            const auto document = QJsonDocument::fromJson(candidate.contents.value(viewPath)).object();
            QJsonArray commands;
            for (const auto& other : manifest.contributions)
                if (other.type == QLatin1String("ui.command")) commands.append(QString(manifest.id + QLatin1Char('/') + other.id));
            for (const bool light : {false, true})
                write(QString::fromLatin1(ViewScene).arg(qml, light ? QStringLiteral("true") : QStringLiteral("false"), json(document),
                          json(contribution.document.value(QStringLiteral("state")).toObject()), json(commands)),
                      QStringLiteral("view-%1-%2.png").arg(contribution.id, light ? QStringLiteral("light") : QStringLiteral("dark")));
        }
    }
    if (!written && !failed) out << "The package has no theme or view to preview.\n";
    return failed ? 1 : 0;
}
