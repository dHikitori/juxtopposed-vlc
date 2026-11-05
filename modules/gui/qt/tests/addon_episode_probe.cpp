// SPDX-License-Identifier: GPL-2.0-or-later
// This optional probe uses the real episode consumer and public TVmaze API.
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QTimer>
#include <QTextStream>
#include "addons/addon_manager.hpp"
#include "addons/addon_metadata_service.hpp"
#include "player/episode_metadata.hpp"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 3) return 2;
    QTemporaryDir directory;
    if (!directory.isValid()) return 2;
    vlc::addons::AddonManager manager(directory.filePath(QStringLiteral("addons")));
    manager.setWorkerExecutable(app.arguments()[2]);
    if (!manager.ensureDistributionPackage(app.arguments()[1], QStringLiteral("org.hakone.tvmaze"))) {
        QTextStream(stdout) << "The distribution package import failed: " << manager.errorMessage() << '\n'; return 1;
    }
    AddonMetadataService metadata(&manager); metadata.setAutomaticNetworkAllowed(true);
    EpisodeMetadata episode; episode.setMetadataService(&metadata);
    const auto target = QUrl::fromLocalFile(directory.filePath(QStringLiteral("Under.the.Dome.S01E01.mkv")));
    QObject::connect(&episode, &EpisodeMetadata::titleReady, &app,
        [&app, target](const QUrl& url, const QString& title, const QUrl& source) {
            if (url != target) return;
            const bool valid = !title.isEmpty() && source.scheme() == QStringLiteral("https")
                && source.host() == QStringLiteral("www.tvmaze.com");
            QTextStream(stdout) << "Episode consumer live result: " << (valid ? "PASS" : "FAIL")
                << "; title=" << (!title.isEmpty() ? "present" : "absent")
                << "; source credit=" << (!source.isEmpty() ? "present" : "absent") << ".\n";
            app.exit(valid ? 0 : 1);
        });
    QTimer::singleShot(60000, &app, [&app] {
        QTextStream(stdout) << "The episode consumer probe time limit has ended.\n"; app.exit(1);
    });
    episode.request(target, {{QStringLiteral("showName"), QStringLiteral("Under the Dome")},
        {QStringLiteral("year"), 2013}, {QStringLiteral("season"), 1}, {QStringLiteral("episode"), 1}});
    return app.exec();
}
