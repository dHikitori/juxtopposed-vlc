// SPDX-License-Identifier: GPL-2.0-or-later
// Explicit opt-in live probe. The key is read from stdin and never displayed,
// persisted to a file, passed to the worker or included in process arguments.
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QTimer>
#include <QTextStream>
#include "addons/addon_manager.hpp"
#include "util/subtitle_source_service.hpp"

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
    const QString package = QStringLiteral("org.hakone.thesubtitledb");
    if (!manager.installLocalPackage(app.arguments()[1], true)) {
        QTextStream(stdout) << "Package setup failed: " << manager.errorMessage() << '\n'; return 1;
    }
    const bool saved = manager.saveAddonSettings(package, {{"language", "en"}},
        {{"apiKey", QVariantMap{{"operation", "replace"}, {"value", QString::fromUtf8(key)}, {"sessionOnly", true}}}});
    key.fill('\0');
    if (!saved || !manager.setEnabled(package, true)) {
        QTextStream(stdout) << "Account setup failed.\n"; return 1;
    }
    SubtitleSourceService service; service.setStorageDirectory(directory.filePath(QStringLiteral("subtitles")));
    service.setAddonManager(&manager);
    const QString mediaUrl = QStringLiteral("https://example.com/public-domain-film.mkv");
    int count = 0;
    QObject::connect(&service, &SubtitleSourceService::searchFinished, &app, [&](quint64, const QVariantMap& result) {
        const auto candidates = result.value("sources").toList(); count = candidates.size();
        if (!result.value("success").toBool() || candidates.isEmpty()) { fail(QStringLiteral("subtitle search")); return; }
        const auto selected = candidates.first().toMap().value("url").toString();
        if (!selected.startsWith(QLatin1String("addon-subtitle:"))) { fail(QStringLiteral("host candidate handle")); return; }
        service.prepareRows({QVariantMap{{"url", mediaUrl}, {"tracks", QStringList{selected}},
            {"baseline", QStringList{}}, {"initialTracks", QStringList{}}}});
    });
    QObject::connect(&service, &SubtitleSourceService::preparationFinished, &app, [&](quint64, const QVariantMap& result) {
        const auto rows = result.value("rows").toList();
        if (!result.value("success").toBool() || rows.isEmpty()) { fail(QStringLiteral("subtitle Save preparation")); return; }
        const auto tracks = rows.first().toMap().value("tracks").toStringList();
        if (tracks.size() != 1 || !QUrl(tracks.first()).isLocalFile()) { fail(QStringLiteral("local track resolution")); return; }
        QFile subtitle(QUrl(tracks.first()).toLocalFile());
        if (!subtitle.open(QIODevice::ReadOnly)) { fail(QStringLiteral("managed subtitle file")); return; }
        const auto bytes = subtitle.readAll();
        if (bytes.isEmpty() || (!bytes.contains("-->") && !bytes.contains("Dialogue:"))) { fail(QStringLiteral("subtitle validation")); return; }
        QTextStream(stdout) << "Authenticated subtitle search: " << count << " candidates. Save resolved a host handle to "
            << bytes.size() << " validated local subtitle bytes.\n";
        app.exit(0);
        });
    QTimer::singleShot(90000, &app, [&] { fail(QStringLiteral("deadline")); });
    service.searchMedia({{"url", mediaUrl},
        {"identity", QVariantMap{{"type", "movie"}, {"ids", QVariantMap{{"imdb", "tt0028119"}}}}}}, QStringLiteral("en"));
    return app.exec();
}
