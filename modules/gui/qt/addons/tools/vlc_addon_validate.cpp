// SPDX-License-Identifier: GPL-2.0-or-later
// vlc-addon-validate: check an add-on package folder with the host's own
// install rules, without running any package code.
//
//   vlc-addon-validate [--json] [--ui <AddonUi.js>] <package folder>...
//
// It reads and hashes the declared files, parses the manifest, checks
// permissions, contributions, settings, themes and declared resources as an
// install would, and (with --ui) applies the settings form rules of the app.
// Exit status: 0 when every package is valid, 1 when one is not, 2 for usage.
#include "addons/addon_manifest.hpp"
#include "addons/addon_package_store.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

using namespace vlc::addons;

namespace {
QString settingsFormError(QJSEngine& engine, const QString& uiScript, const PackageCandidate& candidate)
{
    if (uiScript.isEmpty() || candidate.manifest.settingsSchema.isEmpty()) return {};
    static bool loaded = false;
    if (!loaded) {
        QFile file(uiScript);
        if (!file.open(QIODevice::ReadOnly)) return QStringLiteral("The app form rules cannot be read: ") + uiScript;
        // The QML library pragma is not JavaScript; the functions are.
        const auto result = engine.evaluate(QString::fromUtf8(file.readAll()).replace(QStringLiteral(".pragma library"), QString{}), uiScript);
        if (result.isError()) return QStringLiteral("The app form rules did not load.");
        loaded = true;
    }
    // A plain JavaScript value, as the app passes it: a converted
    // QVariantList is not an Array in Qt 6.8.
    const auto schema = engine.globalObject().property(QStringLiteral("JSON")).property(QStringLiteral("parse"))
        .call({QString::fromUtf8(candidate.contents.value(candidate.manifest.settingsSchema))});
    if (schema.isError()) return QStringLiteral("The settings schema is not JSON.");
    const auto error = engine.globalObject().property(QStringLiteral("validateSettingsSchema")).call({schema}).toString();
    return error.isEmpty() ? QString{} : QStringLiteral("Settings form: ") + error;
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout), err(stderr);
    auto arguments = app.arguments().mid(1);
    bool json = false;
    QString uiScript;
    QStringList folders;
    for (int i = 0; i < arguments.size(); ++i) {
        if (arguments[i] == QLatin1String("--json")) json = true;
        else if (arguments[i] == QLatin1String("--ui") && i + 1 < arguments.size()) uiScript = arguments[++i];
        else if (arguments[i].startsWith(QLatin1String("--"))) { err << "Unknown option: " << arguments[i] << '\n'; return 2; }
        else folders.append(arguments[i]);
    }
    if (folders.isEmpty()) {
        err << "Usage: vlc-addon-validate [--json] [--ui AddonUi.js] <package folder>...\n";
        return 2;
    }
    QJSEngine engine;
    QJsonArray reports;
    bool allValid = true;
    for (const auto& folder : folders) {
        QJsonObject report{{"folder", QDir(folder).absolutePath()}};
        QString error;
        Manifest manifest;
        PackageCandidate candidate;
        bool valid = Manifest::loadDirectory(folder, &manifest, nullptr, &error);
        if (valid) {
            // A developer identity: the folder is checked as a local install.
            ReleaseIdentity identity;
            identity.source = manifest.source.toString();
            identity.tag = QStringLiteral("v") + manifest.version;
            identity.developerSource = true;
            valid = PackageStore::inspectDirectory(folder, identity, &candidate, &error);
        }
        if (valid) {
            error = settingsFormError(engine, uiScript, candidate);
            valid = error.isEmpty();
        }
        report.insert("valid", valid);
        if (!valid) report.insert("error", error);
        if (!manifest.id.isEmpty()) {
            report.insert("id", manifest.id);
            report.insert("version", manifest.version);
            QJsonArray contributions, permissions;
            for (const auto& contribution : manifest.contributions)
                contributions.append(QString(contribution.type + QLatin1Char(':') + contribution.id));
            for (const auto& name : manifest.requiredPermissions.keys()) permissions.append(name);
            for (const auto& name : manifest.optionalPermissions.keys()) permissions.append(QString(name + QStringLiteral(" (optional)")));
            report.insert("contributions", contributions);
            report.insert("permissions", permissions);
        }
        if (valid) report.insert("digest", QString::fromLatin1(candidate.digest));
        allValid &= valid;
        reports.append(report);
        if (!json) {
            out << (valid ? "VALID   " : "INVALID ") << report.value("id").toString(folder);
            if (!manifest.version.isEmpty()) out << ' ' << manifest.version;
            out << '\n';
            if (!valid) out << "        " << error << '\n';
        }
    }
    if (json) out << QJsonDocument(QJsonObject{{"valid", allValid}, {"packages", reports}}).toJson(QJsonDocument::Indented);
    return allValid ? 0 : 1;
}
