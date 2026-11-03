// SPDX-License-Identifier: GPL-2.0-or-later
// This optional probe uses the public TVmaze API. It does not use a secret.
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include "addons/addon_manager.hpp"
#include "addons/addon_metadata_service.hpp"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 3) return 2;
    QTemporaryDir directory;
    if (!directory.isValid()) return 2;
    vlc::addons::AddonManager manager(directory.path());
    manager.setWorkerExecutable(app.arguments()[2]);
    if (!manager.installLocalPackage(app.arguments()[1], true)
        || !manager.setEnabled(QStringLiteral("org.videolan.tvmaze"), true)) {
        QTextStream(stdout) << "The TVmaze package setup failed: " << manager.errorMessage() << '\n';
        return 1;
    }
    AddonMetadataService metadata(&manager);
    QObject::connect(&metadata, &AddonMetadataService::finished, &app,
        [&app](const QString&, const QString& status, const QVariantMap& data, const QString& error) {
            const auto title = data.value(QStringLiteral("fields")).toMap().value(QStringLiteral("episodeTitle")).toString();
            const auto credit = data.value(QStringLiteral("provenance")).toMap().value(QStringLiteral("episodeTitle")).toMap()
                .value(QStringLiteral("attribution")).toMap();
            const bool valid = status == QStringLiteral("match") && !title.isEmpty()
                && credit.value(QStringLiteral("name")) == QStringLiteral("TVmaze")
                && QUrl(credit.value(QStringLiteral("url")).toString()).host() == QStringLiteral("www.tvmaze.com");
            QTextStream(stdout) << "TVmaze live result: " << (valid ? "PASS" : "FAIL") << "; status=" << status
                << "; title=" << (!title.isEmpty() ? "present" : "absent")
                << "; source credit=" << (!credit.isEmpty() ? "present" : "absent") << ".\n";
            if (!valid && !error.isEmpty()) QTextStream(stdout) << error << '\n';
            app.exit(valid ? 0 : 1);
        });
    QTimer::singleShot(60000, &app, [&app] { QTextStream(stdout) << "The TVmaze probe time limit has expired.\n"; app.exit(1); });
    metadata.enrich(QStringLiteral("video.details"), {{QStringLiteral("identity"), QVariantMap{
        {QStringLiteral("kind"), QStringLiteral("episode")}, {QStringLiteral("title"), QStringLiteral("Under the Dome")},
        {QStringLiteral("year"), 2013}, {QStringLiteral("season"), 1}, {QStringLiteral("episodes"), QVariantList{1}}}}},
        {QStringLiteral("episodeTitle")});
    return app.exec();
}
