// SPDX-License-Identifier: GPL-2.0-or-later
// This optional probe uses public artwork services. It does not use a secret.
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include "addons/addon_manager.hpp"
#include "addons/addon_metadata_service.hpp"
#include "addons/addon_artwork_assets.hpp"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() < 4 || app.arguments().size() > 5) return 2;
    QTemporaryDir directory; if (!directory.isValid()) return 2;
    vlc::addons::AddonManager manager(directory.path()); manager.setWorkerExecutable(QFileInfo(app.arguments()[2]).absoluteFilePath());
    if (!manager.installLocalPackage(app.arguments()[1], true) || manager.packages().size() != 1) return 1;
    const auto id = manager.packages().first().toMap().value(QStringLiteral("id")).toString();
    if (!manager.setEnabled(id, true)) return 1;
    const auto finish = [&app](const QUrl& file, const QString& error) {
        QFile input(file.toLocalFile()); QByteArray bytes;
        if (file.isLocalFile() && input.open(QIODevice::ReadOnly)) bytes = input.read(1024 * 1024 + 1);
        QString format;
        const bool correct = AddonArtworkAssets::imageFormat(bytes, {}, &format);
        QTextStream(stdout) << "Artwork live result: " << (correct ? "PASS" : "FAIL")
            << "; bytes=" << bytes.size() << "; format=" << format << ".\n";
        if (!correct && !error.isEmpty()) QTextStream(stdout) << error << '\n';
        app.exit(correct ? 0 : 1);
    };
    AddonMetadataService metadata(&manager);
    AddonArtworkAssets assets(&manager, directory.filePath(QStringLiteral("probe-cache")));
    QObject::connect(&metadata, &AddonMetadataService::finished, &app,
        [finish](const QString&, const QString&, const QVariantMap& data, const QString& error) {
            finish(QUrl(data.value(QStringLiteral("fields")).toMap().value(QStringLiteral("artwork")).toString()), error);
        });
    QObject::connect(&metadata, &AddonMetadataService::canceled, &app, [&app, &manager](const QString&) {
        QTextStream(stdout) << "The artwork request was canceled. " << manager.errorMessage() << '\n'; app.exit(1);
    });
    QTimer::singleShot(60000, &app, [&app] { QTextStream(stdout) << "The artwork probe time limit has expired.\n"; app.exit(1); });
    if (app.arguments().size() == 5) assets.resolve(id + QStringLiteral("/artwork"), app.arguments()[4], finish);
    else {
        const auto identity = QJsonDocument::fromJson(app.arguments()[3].toUtf8()).object().toVariantMap();
        if (identity.isEmpty()) return 2;
        metadata.enrich(QStringLiteral("artwork"), {{QStringLiteral("identity"), identity}}, {QStringLiteral("artwork")});
    }
    return app.exec();
}
