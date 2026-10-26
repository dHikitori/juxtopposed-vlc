// SPDX-License-Identifier: GPL-2.0-or-later
// Explicit opt-in live probe. The key is read from stdin and never displayed,
// persisted to a file, passed to the worker or included in process arguments.
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QTimer>
#include <QTextStream>
#include "addons/addon_manager.hpp"

using namespace vlc::addons;
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 3) return 2;
    QFile input; if (!input.open(stdin, QIODevice::ReadOnly)) return 2;
    auto key = input.readLine(4098).trimmed();
    if (key.isEmpty() || key.size() > 4096) return 2;
    QTemporaryDir directory; if (!directory.isValid()) return 2;
    AddonManager manager(directory.path()); manager.setWorkerExecutable(app.arguments()[2]);
    auto fail = [&app](const QString& stage) {
        QTextStream(stdout) << "Provider probe failed at " << stage << ".\n";
        app.exit(1);
    };
    const QString package = QStringLiteral("org.videolan.thesubtitledb"), provider = package + QStringLiteral("/search");
    if (!manager.installLocalPackage(app.arguments()[1], true)) {
        QTextStream(stdout) << "Package setup failed: " << manager.errorMessage() << '\n'; return 1;
    }
    const bool saved = manager.saveAddonSettings(package, {{"language", "en"}},
        {{"apiKey", QVariantMap{{"operation", "replace"}, {"value", QString::fromUtf8(key)}, {"sessionOnly", true}}}});
    key.fill('\0');
    if (!saved || !manager.setEnabled(package, true)) {
        QTextStream(stdout) << "Account setup failed.\n"; return 1;
    }
    QString searchRequest, resolveRequest;
    int count = 0;
    QObject::connect(&manager, &AddonManager::operationFinished, &app,
        [&](const QString& request, const QString&, const QString& status, const QVariantMap& data, const QString&) {
            if (request != searchRequest && request != resolveRequest) return;
            if (status != QLatin1String("match") && status != QLatin1String("partial")) { fail(QStringLiteral("lookup/resolve")); return; }
            if (request == searchRequest) {
                const auto candidates = data.value("candidates").toList(); count = candidates.size();
                if (candidates.isEmpty()) { fail(QStringLiteral("candidate lookup")); return; }
                resolveRequest = manager.invoke(provider, QStringLiteral("resolveAsset"), {{"candidate", candidates.first()}});
            } else {
                manager.fetchAsset(provider, QJsonObject::fromVariantMap(data), [&](const NetworkBroker::Response& response) {
                    if (!response.error.isEmpty() || response.status != 200 || response.body.isEmpty()
                        || (!response.body.contains("-->") && !response.body.contains("Dialogue:"))) {
                        fail(QStringLiteral("asset download")); return;
                    }
                    QTextStream(stdout) << "Authenticated portable provider lookup: " << count
                        << " candidates. Validated subtitle download: " << response.body.size() << " bytes.\n";
                    app.exit(0);
                });
            }
        });
    QTimer::singleShot(90000, &app, [&] { fail(QStringLiteral("deadline")); });
    searchRequest = manager.invoke(provider, QStringLiteral("searchAssets"),
        {{"identity", QVariantMap{{"kind", "movie"}, {"type", "movie"}, {"ids", QVariantMap{{"imdb", "tt0028119"}}}}},
         {"language", "en"}});
    return app.exec();
}
