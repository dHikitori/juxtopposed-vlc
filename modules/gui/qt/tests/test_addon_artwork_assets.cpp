// SPDX-License-Identifier: GPL-2.0-or-later
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QBuffer>
#include <QImage>
#include <QFile>
#include <QDir>
#include <QtEndian>
#include <zlib.h>
#include "addons/addon_artwork_assets.hpp"

namespace {
QByteArray image(int width = 2, int height = 2, const char* format = "PNG")
{
    QImage image(width, height, QImage::Format_RGB32); image.fill(Qt::red);
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer, format); return bytes;
}
QByteArray pngChunk(const QByteArray& type, const QByteArray& payload)
{
    QByteArray result(4, '\0'); qToBigEndian<quint32>(quint32(payload.size()), result.data()); result += type; result += payload;
    const auto crc = crc32(0, reinterpret_cast<const Bytef*>(result.constData() + 4), uInt(result.size() - 4));
    QByteArray tail(4, '\0'); qToBigEndian<quint32>(quint32(crc), tail.data()); return result + tail;
}
struct Fake {
    struct Call { quint64 id; QString provider; QJsonObject request; vlc::addons::NetworkBroker::Callback complete; };
    QList<Call> calls; QList<quint64> canceled;
    QString partition = QStringLiteral("release-1:account-1:settings-1");
    AddonArtworkAssets::Backend backend()
    {
        return {
            [this](const QString&) { return QVariantMap{{"enabled", true}, {"configured", true}, {"cachePartition", partition}}; },
            [this](const QString& provider, const QJsonObject& request, vlc::addons::NetworkBroker::Callback complete) {
                const auto id = quint64(calls.size() + 1); calls.append({id, provider, request, std::move(complete)}); return id;
            }, [this](quint64 id) { canceled.append(id); }};
    }
    void complete(int index, const QByteArray& bytes = image(), bool truncated = false)
    {
        vlc::addons::NetworkBroker::Response response; response.status = 200; response.body = bytes; response.truncated = truncated;
        const auto callback = calls[index].complete; callback(response);
    }
};
}
class TestAddonArtworkAssets : public QObject
{
    Q_OBJECT
private slots:
    void firstMissCountsFilesCreatedAfterConstructionBeforeAnyFetch()
    {
        QTemporaryDir directory; Fake fake; AddonArtworkAssets::Options options; options.maxEntries = 1;
        AddonArtworkAssets assets(fake.backend(), directory.path(), options);
        QFile seed(directory.filePath(QStringLiteral("existing.png"))); QVERIFY(seed.open(QIODevice::WriteOnly));
        const auto bytes = image(); QCOMPARE(seed.write(bytes), qint64(bytes.size())); seed.close();
        int completed = 0; QUrl selected; QString error;
        assets.resolve("example/art", QStringLiteral("https://images.example.org/miss.png"), [&](const QUrl& value, const QString& failure) { selected = value; error = failure; ++completed; });
        QTRY_COMPARE(completed, 1); QVERIFY(selected.isEmpty()); QVERIFY(!error.isEmpty()); QVERIFY(fake.calls.isEmpty());
        QVERIFY(QFileInfo::exists(seed.fileName()));
    }
    void cachedHitDoesNotMeasureTheDirectoryBeforeTheFirstMiss()
    {
        QTemporaryDir directory; Fake fake; const QString url = QStringLiteral("https://images.example.org/selected.png"); QUrl selected;
        {
            AddonArtworkAssets seed(fake.backend(), directory.path(), {});
            seed.resolve("example/art", url, [&](const QUrl& value, const QString& error) { QVERIFY(error.isEmpty()); selected = value; });
            QTRY_COMPARE(fake.calls.size(), 1); fake.complete(0); QVERIFY(!selected.isEmpty());
        }
        AddonArtworkAssets::Options options; options.maxEntries = 2;
        AddonArtworkAssets assets(fake.backend(), directory.path(), options); int completed = 0;
        assets.resolve("example/art", url, [&](const QUrl& value, const QString& error) { QCOMPARE(value, selected); QVERIFY(error.isEmpty()); ++completed; });
        QTRY_COMPARE(completed, 1); QCOMPARE(fake.calls.size(), 1);
        // A scan in either the constructor or the hit path would miss this
        // second file and incorrectly permit another download at the limit.
        QFile later(directory.filePath(QStringLiteral("later.png"))); QVERIFY(later.open(QIODevice::WriteOnly));
        const auto bytes = image(); QCOMPARE(later.write(bytes), qint64(bytes.size())); later.close();
        QString failure;
        assets.resolve("example/art", QStringLiteral("https://images.example.org/miss.png"), [&](const QUrl& value, const QString& error) { QVERIFY(value.isEmpty()); failure = error; ++completed; });
        QTRY_COMPARE(completed, 2); QCOMPARE(fake.calls.size(), 1); QVERIFY(!failure.isEmpty());
        QVERIFY(QFileInfo::exists(selected.toLocalFile())); QVERIFY(QFileInfo::exists(later.fileName()));
        assets.resolve("example/art", url, [&](const QUrl& value, const QString& error) { QCOMPARE(value, selected); QVERIFY(error.isEmpty()); ++completed; });
        QTRY_COMPARE(completed, 3); QCOMPARE(fake.calls.size(), 1);
    }
    void firstMissEnforcesPreexistingByteLimit()
    {
        QTemporaryDir directory; Fake fake; const auto bytes = image();
        AddonArtworkAssets::Options options; options.maxCacheBytes = bytes.size();
        AddonArtworkAssets assets(fake.backend(), directory.path(), options);
        QFile seed(directory.filePath(QStringLiteral("full.webp"))); QVERIFY(seed.open(QIODevice::WriteOnly));
        QCOMPARE(seed.write(bytes), qint64(bytes.size())); seed.close();
        int completed = 0;
        assets.resolve("example/art", QStringLiteral("https://images.example.org/miss.png"), [&](const QUrl& value, const QString& error) { QVERIFY(value.isEmpty()); QVERIFY(!error.isEmpty()); ++completed; });
        QTRY_COMPARE(completed, 1); QVERIFY(fake.calls.isEmpty()); QVERIFY(QFileInfo::exists(seed.fileName()));
    }
    void invalidAndCanceledRequestsDoNotStartCacheAccounting()
    {
        QTemporaryDir directory; Fake fake; AddonArtworkAssets::Options options; options.maxEntries = 1;
        AddonArtworkAssets assets(fake.backend(), directory.path(), options); int completed = 0;
        assets.resolve("example/art", QStringLiteral("file:///invalid.png"), [&](const QUrl& value, const QString& error) { QVERIFY(value.isEmpty()); QVERIFY(!error.isEmpty()); ++completed; });
        QTRY_COMPARE(completed, 1); QVERIFY(fake.calls.isEmpty());
        const auto canceled = assets.resolve("example/art", QStringLiteral("https://images.example.org/canceled.png"), [&](const QUrl&, const QString&) { ++completed; });
        QVERIFY(!canceled.isEmpty()); assets.cancel(canceled); QTest::qWait(0); QCOMPARE(completed, 1); QVERIFY(fake.calls.isEmpty());
        QFile seed(directory.filePath(QStringLiteral("later.jpg"))); QVERIFY(seed.open(QIODevice::WriteOnly));
        const auto bytes = image(); QCOMPARE(seed.write(bytes), qint64(bytes.size())); seed.close();
        assets.resolve("example/art", QStringLiteral("https://images.example.org/miss.png"), [&](const QUrl& value, const QString& error) { QVERIFY(value.isEmpty()); QVERIFY(!error.isEmpty()); ++completed; });
        QTRY_COMPARE(completed, 2); QVERIFY(fake.calls.isEmpty());
    }
    void decodeAllowsOnlyBoundedImages()
    {
        AddonArtworkAssets::Options options; QString extension;
        QVERIFY(AddonArtworkAssets::imageFormat(image(), options, &extension)); QCOMPARE(extension, "png");
        QVERIFY(AddonArtworkAssets::imageFormat(image(2, 2, "JPEG"), options, &extension)); QCOMPARE(extension, "jpg");
        QVERIFY(!AddonArtworkAssets::imageFormat(image(1025, 1), options, &extension));
        QVERIFY(!AddonArtworkAssets::imageFormat("<svg xmlns='http://www.w3.org/2000/svg'><image href='file:///private'/></svg>", options, &extension));
        QVERIFY(!AddonArtworkAssets::imageFormat(QByteArray(1024 * 1024 + 1, 'x'), options, &extension));
        QVERIFY(!AddonArtworkAssets::imageFormat("<html>not an image</html>", options, &extension));
    }
    void metadataAndAnimationDoNotReachTheArtworkCache()
    {
        QByteArray text("note\0\0", 6); text += qCompress(QByteArray(1024 * 1024, 'x')).mid(4);
        QByteArray animation(8, '\0'); qToBigEndian<quint32>(2, animation.data());
        for (const auto& extra : {pngChunk("zTXt", text), pngChunk("acTL", animation)}) {
            auto bytes = image(); bytes.insert(33, extra);
            QVERIFY(!AddonArtworkAssets::imageFormat(bytes, {}, nullptr));
            QTemporaryDir directory; Fake fake; AddonArtworkAssets assets(fake.backend(), directory.path(), {});
            int completed = 0;
            assets.resolve("example/art", QStringLiteral("https://images.example.org/image.png"), [&](const QUrl& file, const QString& error) {
                QVERIFY(file.isEmpty()); QVERIFY(!error.isEmpty()); ++completed;
            });
            QTRY_COMPARE(fake.calls.size(), 1); fake.complete(0, bytes); QCOMPARE(completed, 1);
            QVERIFY(QDir(directory.path()).entryList(QDir::Files).isEmpty());
        }
    }
    void materializationUsesManagedFilesAndSharedDownloads()
    {
        QTemporaryDir directory; Fake fake; AddonArtworkAssets assets(fake.backend(), directory.path(), {});
        QList<QUrl> files; QStringList errors;
        const QVariantMap claim{{"url", "https://images.example.org/image.png"}, {"fileName", "../../private.png"},
            {"method", "POST"}, {"allowTruncated", true}, {"credential", "apiKey"}};
        const auto first = assets.resolve("example/art", claim, [&](const QUrl& file, const QString& error) { files.append(file); errors.append(error); });
        assets.resolve("example/art", claim, [&](const QUrl& file, const QString& error) { files.append(file); errors.append(error); });
        QTRY_COMPARE(fake.calls.size(), 1);
        QCOMPARE(fake.calls[0].request.value("method").toString(), "GET");
        QVERIFY(!fake.calls[0].request.contains("fileName")); QVERIFY(!fake.calls[0].request.contains("allowTruncated"));
        QCOMPARE(fake.calls[0].request.value("credential").toString(), "apiKey");
        assets.cancel(first); QVERIFY(fake.canceled.isEmpty()); fake.complete(0);
        QCOMPARE(files.size(), 1); QVERIFY(errors[0].isEmpty()); QVERIFY(files[0].isLocalFile());
        QVERIFY(files[0].toLocalFile().startsWith(directory.path() + QLatin1Char('/')));
        QFile file(files[0].toLocalFile()); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), image());
        assets.resolve("example/art", claim, [&](const QUrl& path, const QString& error) { files.append(path); errors.append(error); });
        QTRY_COMPARE(files.size(), 2); QCOMPARE(fake.calls.size(), 1); QCOMPARE(files[0], files[1]);
    }
    void canceledOrChangedRequestsCannotWriteAnImage()
    {
        QTemporaryDir directory; Fake fake; AddonArtworkAssets assets(fake.backend(), directory.path(), {});
        int completed = 0;
        const auto request = assets.resolve("example/art", QStringLiteral("https://images.example.org/a.png"),
            [&](const QUrl&, const QString&) { ++completed; });
        QTRY_COMPARE(fake.calls.size(), 1); assets.cancel(request); QCOMPARE(fake.canceled, QList<quint64>{1});
        fake.complete(0); QCOMPARE(completed, 0); QVERIFY(QDir(directory.path()).entryList(QDir::Files).isEmpty());
        QString error; QUrl result;
        assets.resolve("example/art", QStringLiteral("https://images.example.org/b.png"), [&](const QUrl& file, const QString& value) { result = file; error = value; ++completed; });
        QTRY_COMPARE(fake.calls.size(), 2); fake.partition = "release-1:account-2:settings-2"; fake.complete(1);
        QCOMPARE(completed, 1); QVERIFY(result.isEmpty()); QVERIFY(!error.isEmpty());
        QVERIFY(QDir(directory.path()).entryList(QDir::Files).isEmpty());
    }
    void byteAndEntryLimitsKeepExistingSelectedFiles()
    {
        QTemporaryDir directory; Fake fake; AddonArtworkAssets::Options options; options.maxEntries = 1;
        AddonArtworkAssets assets(fake.backend(), directory.path(), options);
        QUrl selected; QString secondError; int completed = 0;
        assets.resolve("example/art", QStringLiteral("https://images.example.org/a.png"), [&](const QUrl& file, const QString&) { selected = file; ++completed; });
        QTRY_COMPARE(fake.calls.size(), 1); fake.complete(0); QCOMPARE(completed, 1); QVERIFY(QFileInfo::exists(selected.toLocalFile()));
        assets.resolve("example/art", QStringLiteral("https://images.example.org/b.png"), [&](const QUrl&, const QString& error) { secondError = error; ++completed; });
        QTRY_COMPARE(completed, 2); QCOMPARE(fake.calls.size(), 1); QVERIFY(!secondError.isEmpty()); QVERIFY(QFileInfo::exists(selected.toLocalFile()));
        assets.resolve("example/art", QStringLiteral("https://images.example.org/a.png"), [&](const QUrl& file, const QString& error) { QCOMPARE(file, selected); QVERIFY(error.isEmpty()); ++completed; });
        QTRY_COMPARE(completed, 3); QCOMPARE(fake.calls.size(), 1);
        QTemporaryDir small; options.maxCacheBytes = 1;
        AddonArtworkAssets limited(fake.backend(), small.path(), options); QUrl rejected;
        limited.resolve("example/art", QStringLiteral("https://images.example.org/c.png"), [&](const QUrl& file, const QString&) { rejected = file; ++completed; });
        QTRY_COMPARE(fake.calls.size(), 2); fake.complete(1); QCOMPARE(completed, 4); QVERIFY(rejected.isEmpty());
        QVERIFY(QDir(small.path()).entryList(QDir::Files).isEmpty());
    }
    void invalidOrTruncatedImagesAreNotCacheEntries()
    {
        for (const bool truncated : {false, true}) {
            QTemporaryDir directory; Fake fake; AddonArtworkAssets assets(fake.backend(), directory.path(), {});
            int completed = 0; QUrl result; QString error;
            assets.resolve("example/art", QStringLiteral("https://images.example.org/a.png"), [&](const QUrl& file, const QString& value) { result = file; error = value; ++completed; });
            QTRY_COMPARE(fake.calls.size(), 1); fake.complete(0, truncated ? image() : QByteArray("<html>image</html>"), truncated);
            QCOMPARE(completed, 1); QVERIFY(result.isEmpty()); QVERIFY(!error.isEmpty()); QVERIFY(QDir(directory.path()).entryList(QDir::Files).isEmpty());
        }
    }
    void completionCanDestroyTheAdapter()
    {
        QTemporaryDir directory; Fake fake; auto* assets = new AddonArtworkAssets(fake.backend(), directory.path(), {});
        int completed = 0;
        assets->resolve("example/art", QStringLiteral("https://images.example.org/a.png"), [&](const QUrl&, const QString&) { ++completed; delete assets; assets = nullptr; });
        QTRY_COMPARE(fake.calls.size(), 1); fake.complete(0); QCOMPARE(completed, 1); QVERIFY(!assets);
    }
};
QTEST_GUILESS_MAIN(TestAddonArtworkAssets)
#include "test_addon_artwork_assets.moc"
