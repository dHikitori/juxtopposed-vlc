// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <QtTest/QtTest>
#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QNetworkInterface>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vlc/vlc.h>
#include "lib/libvlc_internal.h"
#include <vlc_artwork_policy.h>
#include <vlc_input_item.h>
#include "addons/addon_native_artwork_assets.hpp"
#include "addon_artwork_test_images.hpp"

namespace {
using Native = AddonNativeArtworkAssets;
QByteArray image()
{
    QImage pixels(4, 4, QImage::Format_RGB32); pixels.fill(Qt::blue);
    QByteArray bytes; QBuffer output(&bytes); output.open(QIODevice::WriteOnly); pixels.save(&output, "PNG"); return bytes;
}
struct Job {
    vlc_metadata_backend_t* backend = nullptr;
    input_item_t* item = nullptr;
    vlc_metadata_job_t* job = nullptr;
    int ended = 0;
    static int dispatch(void*, vlc_metadata_job_t*) { return VLC_SUCCESS; }
    static void cancel(void*, vlc_metadata_job_t*) {}
    static void complete(vlc_metadata_job_t*, int, void* opaque) { ++static_cast<Job*>(opaque)->ended; }
    explicit Job(const char* media = "file:///private/song.flac", const char* art = "http://127.0.0.1/art.png") {
        const vlc_metadata_backend_operations operations{dispatch, cancel, nullptr}; backend = vlc_metadata_backend_New(&operations, nullptr);
        item = input_item_New(media, "Song"); input_item_SetArtURL(item, art);
        const char* fields[]{"artwork"}; vlc_metadata_request request{};
        request.feature = "artwork"; request.operation = "enrich"; request.fields = fields; request.field_count = 1;
        request.item = item; request.flags = VLC_METADATA_REQUEST_NETWORK | VLC_METADATA_REQUEST_AUTOMATIC;
        job = vlc_metadata_job_New(backend, &request, complete, this); if (job) vlc_metadata_job_Start(job);
    }
    ~Job() { vlc_metadata_backend_Close(backend); vlc_metadata_job_Release(job); vlc_metadata_backend_Release(backend); input_item_Release(item); }
    int publish(const Native::Result& result) {
        auto* copy = input_item_Copy(vlc_metadata_job_GetRequest(job)->item); if (!copy) return VLC_ENOMEM;
        const auto uri = result.file.toEncoded(); const auto status = vlc_artwork_item_SetApprovedURI(copy, uri.constData());
        if (status != VLC_SUCCESS) { input_item_Release(copy); return status; }
        vlc_metadata_job_Complete(job, VLC_SUCCESS, copy, nullptr, 0); input_item_Release(copy);
        return result.permit.admit([this] { return vlc_metadata_job_Apply(job, item, uint32_t(1) << vlc_meta_ArtworkURL, nullptr); });
    }
};
struct Controlled {
    std::atomic_int calls{0}; std::atomic_bool release{false}; QByteArray bytes = image();
    Native::Backend backend() { return {[this](const QUrl&, qint64, const std::function<bool()>& live) {
        ++calls; while (!release.load() && live()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return Native::ReadResult{bytes, {}};
    }}; }
};
}
class TestAddonNativeArtworkAssets : public QObject
{
    Q_OBJECT
private slots:
    void installedNativeFormatsKeepPixelsThroughCachePublication_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        const auto readers = QImageReader::supportedImageFormats();
        for (const QByteArray codec : {"bmp", "cur", "gif", "ico", "jfif", "jpeg", "jpg", "pbm", "pgm", "png", "ppm", "svg", "svgz", "webp", "xbm", "xpm"}) {
            if (!readers.contains(codec)) continue;
            QByteArray bytes;
            if (codec == "svg") bytes = NativeArtworkFixture::svgBytes();
            else if (codec == "svgz") bytes = NativeArtworkFixture::compressedSvg();
            else if (codec == "gif") bytes = QByteArray::fromBase64("R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7");
            else bytes = NativeArtworkFixture::patterned(codec);
            QVERIFY2(!bytes.isEmpty(), codec.constData());
            QTest::newRow(codec.constData()) << bytes;
        }
        QTest::newRow("jpeg-exif") << NativeArtworkFixture::exifJpeg(6, true, true);
    }
    void installedNativeFormatsKeepPixelsThroughCachePublication()
    {
        QFETCH(QByteArray, bytes);
        QTemporaryDir cache; Controlled source; source.bytes = bytes; source.release = true;
        Native assets(source.backend(), cache.path(), {}); assets.setNetworkAllowed(true); Job job;
        Native::Result result; int completed = 0;
        QVERIFY(assets.resolve(job.job, [&](const auto& value) { result = value; ++completed; }));
        QTRY_COMPARE(completed, 1); QCOMPARE(source.calls.load(), 1);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error)); QVERIFY(result.file.isLocalFile());
        QFile file(result.file.toLocalFile()); QVERIFY(file.open(QIODevice::ReadOnly)); const auto png = file.readAll();
        QVERIFY(png.startsWith(QByteArray::fromHex("89504e470d0a1a0a")));
        const auto pixels = NativeArtworkFixture::baseline(png);
        QVERIFY(!pixels.isNull()); QCOMPARE(pixels, NativeArtworkFixture::baseline(bytes)); QVERIFY(pixels.textKeys().isEmpty());
        QVERIFY(result.permit.current()); QCOMPARE(job.publish(result), VLC_SUCCESS);
        char* art = vlc_artwork_item_GetURI(job.item); QCOMPARE(QByteArray(art), result.file.toEncoded()); free(art);
    }
    void consentAndNativeSnapshotAdmissionPrecedeIO()
    {
        QTemporaryDir cache; Controlled source; source.release = true; Native assets(source.backend(), cache.path(), {});
        Job normal; QVERIFY(normal.job); QVERIFY(!assets.resolve(normal.job, [](const auto&) {})); QCOMPARE(source.calls.load(), 0);
        assets.setNetworkAllowed(true);
        for (const auto* uri : {"hakone-addon-source:v1:source", "HAKONE-ADDON-SOURCE:v1:imported",
                                "vlc-addon-source:v1:source", "VLC-ADDON-SOURCE:v1:imported"}) {
            Job scoped(uri); QVERIFY(!assets.resolve(scoped.job, [](const auto&) {}));
        }
        Job local("file:///private/song.flac", "file:///private/cover.png"); QVERIFY(!assets.resolve(local.job, [](const auto&) {}));
        QCOMPARE(source.calls.load(), 0);
        Native::Result result; int completed = 0;
        QVERIFY(assets.resolve(normal.job, [&](const auto& value) { result = value; ++completed; }));
        QTRY_COMPARE(completed, 1); QCOMPARE(source.calls.load(), 1); QVERIFY(result.error.isEmpty()); QVERIFY(result.file.isLocalFile());
        QVERIFY(result.permit.current()); QCOMPARE(normal.publish(result), VLC_SUCCESS);
        char* art = vlc_artwork_item_GetURI(normal.item); QCOMPARE(QByteArray(art), result.file.toEncoded()); free(art);
        QFile file(result.file.toLocalFile()); QVERIFY(file.open(QIODevice::ReadOnly)); QVERIFY(file.readAll().startsWith(QByteArray::fromHex("89504e47")));
        assets.setNetworkAllowed(false); QVERIFY(!result.permit.current());
        QCOMPARE(result.permit.admit([] { return VLC_SUCCESS; }), VLC_EACCES);
    }
    void sameValueAndABAWritesPreventNativePublication()
    {
        for (int change = 0; change < 4; ++change) {
            QTemporaryDir cache; Controlled source; Native assets(source.backend(), cache.path(), {}); assets.setNetworkAllowed(true); Job job;
            int completed = 0, commit = 0;
            QVERIFY(assets.resolve(job.job, [&](const auto& result) { commit = job.publish(result); ++completed; }));
            QTRY_COMPARE(source.calls.load(), 1);
            if (change == 1) input_item_SetArtURL(job.item, "http://127.0.0.1/other.png");
            if (change < 2) input_item_SetArtURL(job.item, "http://127.0.0.1/art.png");
            if (change == 2) input_item_SetURI(job.item, "file:///private/other.flac");
            if (change == 3) QCOMPARE(vlc_artwork_item_SetApprovedURI(job.item, "file:///private/user.png"), VLC_SUCCESS);
            source.release = true;
            QTRY_COMPARE(completed, 1); QCOMPARE(commit, -EAGAIN);
            char* raw = input_item_GetArtURL(job.item); QCOMPARE(QByteArray(raw), QByteArray(change == 3 ? "file:///private/user.png" : "http://127.0.0.1/art.png")); free(raw);
        }
    }
    void nativeCancellationAndCloseNeedNoQtDelivery()
    {
        QTemporaryDir cache; Controlled source; Native assets(source.backend(), cache.path(), {}); assets.setNetworkAllowed(true); Job job;
        int completed = 0; auto cancel = assets.resolve(job.job, [&](const auto&) { ++completed; }); QVERIFY(cancel);
        QTRY_COMPARE(source.calls.load(), 1);
        std::thread canceler([cancel] { cancel(); }); canceler.join();
        const auto before = std::chrono::steady_clock::now(); assets.close();
        QVERIFY(std::chrono::steady_clock::now() - before < std::chrono::seconds(1));
        QCOMPARE(job.ended, 1); QCOMPARE(completed, 0);
        QCoreApplication::sendPostedEvents(&assets, QEvent::MetaCall); QCOMPARE(completed, 0);
    }
    void consentABARejectsAQueuedNativeResult()
    {
        QTemporaryDir cache; Controlled source; Native assets(source.backend(), cache.path(), {}); assets.setNetworkAllowed(true); Job job;
        int completed = 0; QVERIFY(assets.resolve(job.job, [&](const auto&) { ++completed; }));
        QTRY_COMPARE(source.calls.load(), 1); assets.setNetworkAllowed(false); assets.setNetworkAllowed(true); source.release = true;
        assets.close(); QCoreApplication::sendPostedEvents(&assets, QEvent::MetaCall);
        QCOMPARE(completed, 0); QCOMPARE(job.ended, 1); QVERIFY(QDir(cache.path()).entryList(QDir::Files).isEmpty());
    }
    void nativeDeadlineAndCacheLimitAreBounded()
    {
        QTemporaryDir cache; Controlled source; Native::Options options; options.timeoutMs = 40;
        Native assets(source.backend(), cache.path(), options); assets.setNetworkAllowed(true); Job job;
        int completed = 0; Native::Result result;
        QVERIFY(assets.resolve(job.job, [&](const auto& value) { result = value; ++completed; }));
        QTRY_COMPARE(completed, 1); QVERIFY(result.file.isEmpty()); QVERIFY(!result.error.isEmpty());
        QTemporaryDir smallCache; source.release = true; options.maximumCacheBytes = 1;
        Native small(source.backend(), smallCache.path(), options); small.setNetworkAllowed(true); Job second;
        QVERIFY(small.resolve(second.job, [&](const auto& value) { result = value; ++completed; }));
        QTRY_COMPARE(completed, 2); QVERIFY(result.file.isEmpty()); QVERIFY(!result.error.isEmpty());
        QVERIFY(QDir(smallCache.path()).entryList(QDir::Files).isEmpty());
    }
    void pendingDeliveryAndGlobalIOHaveSeparateCaps()
    {
        QTemporaryDir cache; Controlled source; Native assets(source.backend(), cache.path(), {}); assets.setNetworkAllowed(true);
        std::vector<std::unique_ptr<Job>> jobs;
        for (int index = 0; index < 33; ++index) {
            jobs.push_back(std::make_unique<Job>());
            const auto cancel = assets.resolve(jobs.back()->job, [](const auto&) {});
            QCOMPARE(bool(cancel), index < 32);
        }
        QTRY_COMPARE(source.calls.load(), 2); assets.close();
        for (int index = 0; index < 32; ++index) QCOMPARE(jobs[index]->ended, 1);
    }
    void aCompletionCanDestroyTheAdapter()
    {
        QTemporaryDir cache; Controlled source; source.release = true;
        auto* assets = new Native(source.backend(), cache.path(), {}); assets->setNetworkAllowed(true); Job job;
        int completed = 0; QVERIFY(assets->resolve(job.job, [&](const auto& result) {
            QVERIFY(!result.file.isEmpty()); delete assets; assets = nullptr; ++completed;
        }));
        QTRY_COMPARE(completed, 1); QVERIFY(!assets);
    }
    void actualNativeHTTPRetainsLoopbackAndLANAccess()
    {
        qputenv("VLC_PLUGIN_PATH", VLC_ADDON_NATIVE_BUILD_ROOT "/modules");
        const char* args[]{"--ignore-config", "--no-plugins-cache", "--no-media-library", "--quiet"};
        auto* instance = libvlc_new(4, args); QVERIFY(instance);
        QTcpServer server; QVERIFY(server.listen(QHostAddress::AnyIPv4)); int requests = 0;
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            while (auto* socket = server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                    const auto request = socket->readAll(); if (!request.contains("\r\n\r\n")) return;
                    ++requests; const auto bytes = image();
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nConnection: close\r\nContent-Length: "
                        + QByteArray::number(bytes.size()) + "\r\n\r\n" + bytes); socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        QStringList addresses{QStringLiteral("127.0.0.1")};
        for (const auto& address : QNetworkInterface::allAddresses()) if (address.protocol() == QAbstractSocket::IPv4Protocol
            && !address.isLoopback() && (address.isInSubnet(QHostAddress("10.0.0.0"), 8)
                || address.isInSubnet(QHostAddress("172.16.0.0"), 12) || address.isInSubnet(QHostAddress("192.168.0.0"), 16))) {
            addresses.append(address.toString()); break;
        }
        qInfo() << "Native HTTP address scopes:" << addresses.size();
        for (const auto& address : addresses) {
            QTemporaryDir cache; Native assets(VLC_OBJECT(instance->p_libvlc_int), cache.path(), {});
            const auto url = QStringLiteral("http://%1:%2/art.png").arg(address).arg(server.serverPort()).toUtf8();
            Job job("file:///private/native.flac", url.constData()); int completed = 0; Native::Result result;
            QVERIFY(!assets.resolve(job.job, [](const auto&) {}));
            assets.setNetworkAllowed(true);
            QVERIFY(assets.resolve(job.job, [&](const auto& value) { result = value; ++completed; }));
            QTRY_COMPARE_WITH_TIMEOUT(completed, 1, 5000); QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
            QVERIFY(result.file.isLocalFile()); QCOMPARE(job.publish(result), VLC_SUCCESS);
        }
        QCOMPARE(requests, addresses.size()); libvlc_release(instance);
    }
};
QTEST_GUILESS_MAIN(TestAddonNativeArtworkAssets)
#include "test_addon_native_artwork_assets.moc"
