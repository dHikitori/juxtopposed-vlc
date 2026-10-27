// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <vlc_common.h>
#include <QtTest/QtTest>
#include <QFile>
#include <QImage>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtEndian>
#include <zlib.h>
#include <memory>
#include <vlc_artwork_policy.h>
#include <vlc_input_item.h>
#include <vlc_input_policy.h>
#include <vlc_player.h>
#include <vlc_playlist.h>
#include "lib/libvlc_internal.h"
#include "src/libvlc.h"
#include "maininterface/compositor.hpp"
#include "medialibrary/mlfileartwork.hpp"
#include "medialibrary/mlmedia.hpp"
#include "player/player_controller.hpp"
#include "playlist/playlist_item.hpp"
#include "util/artwork_approval.hpp"

namespace {
struct Core {
    libvlc_int_t *instance = libvlc_InternalCreate();
    bool initialized = false;
    Core()
    {
        const char *options[] = {"artwork-consumer-test", "--ignore-config", "--no-plugins-cache",
            "--no-media-library", "--no-audio", "--no-video", "--quiet"};
        if (instance) initialized = libvlc_InternalInit(instance, ARRAY_SIZE(options), options) == VLC_SUCCESS;
        if (initialized) {
            var_Create(instance, "metadata-network-access", VLC_VAR_BOOL);
            var_SetBool(instance, "metadata-network-access", false);
        }
    }
    ~Core()
    {
        if (initialized) libvlc_InternalCleanup(instance);
        if (instance) libvlc_InternalDestroy(instance);
    }
};
bool allowUrl(void*, const char*) { return true; }
bool allowConnection(void*, const char*, unsigned, bool) { return true; }
vlc_input_policy_t *policy()
{
    static const vlc_input_policy_operations operations = {allowUrl, allowConnection, nullptr};
    return vlc_input_policy_New(&operations, nullptr);
}
struct MediaRow {
    vlc_ml_media_t value{};
    vlc_ml_file_list_t *files = static_cast<vlc_ml_file_list_t *>(calloc(1, sizeof(*files) + sizeof(vlc_ml_file_t)));
    QByteArray uri;
    explicit MediaRow(const QByteArray& source) : uri(source)
    {
        Q_ASSERT(files);
        files->i_nb_items = 1; files->p_items[0].i_type = VLC_ML_FILE_TYPE_MAIN; files->p_items[0].psz_mrl = uri.data();
        value.i_id = 1; value.i_type = VLC_ML_MEDIA_TYPE_VIDEO; value.p_files = files;
        value.thumbnails[VLC_ML_THUMBNAIL_SMALL] = {const_cast<char *>("http://127.0.0.1/private.png"), VLC_ML_THUMBNAIL_STATUS_AVAILABLE};
        value.thumbnails[VLC_ML_THUMBNAIL_BANNER] = {const_cast<char *>("file:///serialized.png"), VLC_ML_THUMBNAIL_STATUS_AVAILABLE};
    }
    ~MediaRow() { free(files); }
};
bool write(const QString& path, const QByteArray& data)
{
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}
QUrl png(const QTemporaryDir& directory, const QString& name = QStringLiteral("approved.png"), int width = 2)
{
    QImage image(width, 2, QImage::Format_ARGB32); image.fill(qRgb(10, 20, 30));
    const auto path = directory.filePath(name);
    return image.save(path, "PNG") ? QUrl::fromLocalFile(path) : QUrl{};
}
}

class TestAddonArtworkConsumers : public QObject
{
    Q_OBJECT
    std::unique_ptr<Core> m_core;
private slots:
    void initTestCase()
    {
        m_core = std::make_unique<Core>(); QVERIFY(m_core->initialized);
    }
    void cleanupTestCase() { m_core.reset(); }
    void oldSourceNameKeepsArtworkRestricted()
    {
        SharedInputItem item(input_item_New("VLC-ADDON-SOURCE:v1:old", "Old source"), false);
        QVERIFY(item); input_item_SetArtURL(item.get(), "http://127.0.0.1/private.png");
        QVERIFY(PlayerController::decodeArtURL(item.get()).isEmpty());
        MediaRow source("VLC-ADDON-SOURCE:v1:old"); MLMedia row(&source.value);
        QVERIFY(row.smallCover().isEmpty()); QVERIFY(row.bannerCover().isEmpty());
    }
    void decoderRequiresCurrentNativeApproval()
    {
        SharedInputItem source(input_item_New("HAKONE-ADDON-SOURCE:v1:raw", "Source"), false);
        QVERIFY(source);
        input_item_SetArtURL(source.get(), "http://127.0.0.1/private.png");
        QVERIFY(PlayerController::decodeArtURL(source.get()).isEmpty());
        input_item_SetArtURL(source.get(), "file:///private.png");
        QVERIFY(PlayerController::decodeArtURL(source.get()).isEmpty());
        QCOMPARE(vlc_artwork_item_SetApprovedURI(source.get(), "file:///approved.png"), VLC_SUCCESS);
        QCOMPARE(PlayerController::decodeArtURL(source.get()), QStringLiteral("/approved.png"));
        input_item_SetArtURL(source.get(), "file:///approved.png");
        QVERIFY(PlayerController::decodeArtURL(source.get()).isEmpty());
        SharedInputItem native(input_item_New("file:///native.flac", "Native"), false);
        input_item_SetArtURL(native.get(), "file:///native.png");
        QCOMPARE(PlayerController::decodeArtURL(native.get()), QStringLiteral("/native.png"));
    }
    void serializedLibraryArtworkCannotGrantApproval()
    {
        MediaRow source("HAKONE-ADDON-SOURCE:v1:raw"); MLMedia row(&source.value);
        vlc_ml_thumbnail_status_t status = VLC_ML_THUMBNAIL_STATUS_AVAILABLE;
        QVERIFY(row.smallCover(&status).isEmpty()); QCOMPARE(status, VLC_ML_THUMBNAIL_STATUS_FAILURE);
        QVERIFY(row.bannerCover(&status).isEmpty()); QCOMPARE(status, VLC_ML_THUMBNAIL_STATUS_FAILURE);
        row.setSmallCover(VLC_ML_THUMBNAIL_STATUS_AVAILABLE, QStringLiteral("file:///new-serialized.png"));
        QVERIFY(row.smallCover().isEmpty());
        MediaRow native("file:///native.flac"); MLMedia ordinary(&native.value);
        QCOMPARE(ordinary.smallCover(), QStringLiteral("http://127.0.0.1/private.png"));
        QCOMPARE(ordinary.bannerCover(), QStringLiteral("file:///serialized.png"));
        ordinary.setSmallCover(VLC_ML_THUMBNAIL_STATUS_AVAILABLE, QStringLiteral("file:///new-native.png"));
        QCOMPARE(ordinary.smallCover(), QStringLiteral("file:///new-native.png"));
    }
    void nativeLibraryArtworkUsesTheCurrentRecord()
    {
        SharedInputItem source(input_item_New("hakone-addon-source:v1:episode", "Source"), false);
        auto *scope = policy(); QVERIFY(scope);
        const auto release = qScopeGuard([&] { vlc_input_policy_Release(scope); });
        QCOMPARE(vlc_input_policy_AttachItem(source.get(), scope), VLC_SUCCESS);
        MediaRow media("hakone-addon-source:v1:episode"); media.value.p_podcast = source.get(); MLMedia row(&media.value);
        QVERIFY(row.smallCover().isEmpty());
        QCOMPARE(vlc_artwork_item_SetApprovedURI(source.get(), "file:///approved.png"), VLC_SUCCESS);
        QCOMPARE(row.smallCover(), QStringLiteral("file:///approved.png"));
        QCOMPARE(row.bannerCover(), QStringLiteral("file:///approved.png"));
        input_item_SetArtURL(source.get(), "file:///approved.png"); QVERIFY(row.smallCover().isEmpty());
        QCOMPARE(vlc_artwork_item_SetApprovedURI(source.get(), "file:///approved.png"), VLC_SUCCESS);
        vlc_input_policy_Revoke(scope); QVERIFY(row.smallCover().isEmpty()); QVERIFY(row.bannerCover().isEmpty());
    }
    void localChoiceRequiresABoundedRasterFileForScopedMedia()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto valid = png(directory); QVERIFY(!valid.isEmpty()); QVERIFY(ArtworkApproval::validUserFile(valid));
        const auto svgPath = directory.filePath(QStringLiteral("image.png"));
        QVERIFY(write(svgPath, "<svg xmlns='http://www.w3.org/2000/svg'><image href='http://127.0.0.1/private.png'/></svg>"));
        QVERIFY(!ArtworkApproval::validUserFile(QUrl::fromLocalFile(svgPath)));
        const auto largePath = directory.filePath(QStringLiteral("large.png"));
        QFile large(largePath); QVERIFY(large.open(QIODevice::WriteOnly)); QVERIFY(large.resize(1024 * 1024 + 1)); large.close();
        QVERIFY(!ArtworkApproval::validUserFile(QUrl::fromLocalFile(largePath)));
        QVERIFY(!ArtworkApproval::validUserFile(png(directory, QStringLiteral("wide.png"), 1025)));
        QVERIFY(!ArtworkApproval::validUserFile(QUrl::fromLocalFile(directory.path())));
        for (const auto& invalid : {QStringLiteral("http://127.0.0.1/private.png"), QStringLiteral("file://server/share/image.png"),
            QStringLiteral("file:////server/share/image.png"), QStringLiteral("file:///%2fserver/share/image.png"),
            QStringLiteral("file:///%5cserver/share/image.png")}) QVERIFY(!ArtworkApproval::validUserFile(QUrl(invalid)));
        SharedInputItem source(input_item_New("hakone-addon-source:v1:choice", "Source"), false);
        input_item_SetArtURL(source.get(), "http://127.0.0.1/private.png");
        QVERIFY(!ArtworkApproval::userFile(source.get(), QUrl::fromLocalFile(svgPath)));
        QVERIFY(PlayerController::decodeArtURL(source.get()).isEmpty());
        QVERIFY(ArtworkApproval::userFile(source.get(), valid)); QCOMPARE(PlayerController::decodeArtURL(source.get()), valid.toLocalFile());
        SharedInputItem native(input_item_New("file:///native.flac", "Native"), false);
        QVERIFY(ArtworkApproval::userFile(native.get(), QUrl::fromLocalFile(svgPath)));
        QCOMPARE(PlayerController::decodeArtURL(native.get()), svgPath);
        auto *scope = policy(); QVERIFY(scope); const auto release = qScopeGuard([&] { vlc_input_policy_Release(scope); });
        QCOMPARE(vlc_input_policy_AttachItem(native.get(), scope), VLC_SUCCESS);
        QVERIFY(PlayerController::decodeArtURL(native.get()).isEmpty());
    }
    void scopedUserChoiceRefusesCompressedImageMetadata()
    {
        QTemporaryDir directory; const auto original = png(directory); QVERIFY(!original.isEmpty());
        QFile input(original.toLocalFile()); QVERIFY(input.open(QIODevice::ReadOnly)); auto bytes = input.readAll(); input.close();
        QByteArray payload("note\0\0", 6); payload += qCompress(QByteArray(1024 * 1024, 'x')).mid(4);
        QByteArray chunk(4, '\0'); qToBigEndian<quint32>(quint32(payload.size()), chunk.data()); chunk += "zTXt"; chunk += payload;
        const auto crc = crc32(0, reinterpret_cast<const Bytef*>(chunk.constData() + 4), uInt(chunk.size() - 4));
        QByteArray tail(4, '\0'); qToBigEndian<quint32>(quint32(crc), tail.data()); chunk += tail; bytes.insert(33, chunk);
        const auto candidate = QUrl::fromLocalFile(directory.filePath(QStringLiteral("metadata.png")));
        QVERIFY(write(candidate.toLocalFile(), bytes)); QVERIFY(!ArtworkApproval::validUserFile(candidate));
        SharedInputItem source(input_item_New("hakone-addon-source:v1:choice", "Source"), false);
        QVERIFY(ArtworkApproval::userFile(source.get(), original));
        QVERIFY(!ArtworkApproval::userFile(source.get(), candidate));
        QCOMPARE(PlayerController::decodeArtURL(source.get()), original.toLocalFile());
        SharedInputItem native(input_item_New("file:///native.flac", "Native"), false);
        QVERIFY(ArtworkApproval::userFile(native.get(), candidate));
    }
    void queueArtworkUsesTheCurrentNativeRecord()
    {
        auto *playlist = vlc_playlist_New(VLC_OBJECT(m_core->instance), VLC_PLAYLIST_PREPARSING_DISABLED, 1, 0); QVERIFY(playlist);
        const auto cleanup = qScopeGuard([&] { vlc_playlist_Delete(playlist); });
        SharedInputItem source(input_item_New("HAKONE-ADDON-SOURCE:v1:queue", "Source"), false);
        input_item_SetArtURL(source.get(), "http://127.0.0.1/private.png");
        PlaylistItem row;
        {
            vlc_playlist_Lock(playlist);
            const auto unlock = qScopeGuard([&] { vlc_playlist_Unlock(playlist); });
            QCOMPARE(vlc_playlist_AppendOne(playlist, source.get()), VLC_SUCCESS);
            row = PlaylistItem(vlc_playlist_Get(playlist, 0));
        }
        QVERIFY(row.getArtwork().isEmpty());
        QCOMPARE(vlc_artwork_item_SetApprovedURI(source.get(), "file:///approved.png"), VLC_SUCCESS);
        QCOMPARE(row.getArtwork(), QUrl(QStringLiteral("file:///approved.png")));
        input_item_SetArtURL(source.get(), "file:///approved.png"); QVERIFY(row.getArtwork().isEmpty());
    }
    void fileArtworkDoesNotUseASerializedSourceFallback()
    {
        MLFileArtwork artwork; QSignalSpy changes(&artwork, &MLFileArtwork::artworkChanged);
        const QUrl source(QStringLiteral("HAKONE-ADDON-SOURCE:v1:raw"));
        artwork.request(source, false, QStringLiteral("http://127.0.0.1/private.png"));
        QCOMPARE(changes.size(), 1); QCOMPARE(changes[0][0].toUrl(), source); QVERIFY(changes[0][1].toString().isEmpty());
    }
    void mountedPlayerUsesTheNativeArtworkGate()
    {
        auto *player = vlc_player_New(VLC_OBJECT(m_core->instance), VLC_PLAYER_LOCK_NORMAL); QVERIFY(player);
        const auto deletePlayer = qScopeGuard([&] { vlc_player_Delete(player); });
        auto *intf = vlc_object_create<qt_intf_t>(m_core->instance); QVERIFY(intf);
        const auto deleteIntf = qScopeGuard([&] { vlc_object_delete(intf); });
        intf->p_player = player;
        var_Create(intf, "qt-discover-cast-devices", VLC_VAR_BOOL); var_SetBool(intf, "qt-discover-cast-devices", false);
        PlayerController controller(intf);
        SharedInputItem source(input_item_New("hakone-addon-source:v1:player", "Source"), false);
        input_item_SetTitle(source.get(), "Source title"); input_item_SetArtURL(source.get(), "http://127.0.0.1/private.png");
        { vlc_player_locker lock(player); QCOMPARE(vlc_player_SetCurrentMedia(player, source.get()), VLC_SUCCESS); }
        QTRY_COMPARE(controller.getTitle(), QStringLiteral("Source title")); QVERIFY(controller.getArtwork().isEmpty());
        QTemporaryDir directory; const auto chosen = png(directory); QVERIFY(!chosen.isEmpty());
        controller.setArt(source.get(), chosen.toString(QUrl::FullyEncoded)); QCOMPARE(controller.getArtwork(), chosen);
        input_item_SetArtURL(source.get(), chosen.toEncoded().constData()); QVERIFY(controller.getArtwork().isEmpty());
        controller.refreshMediaMeta(source.get()); QVERIFY(controller.getArtwork().isEmpty());
        auto *scope = vlc_input_policy_HoldItem(source.get()); QVERIFY(scope);
        const auto release = qScopeGuard([&] { vlc_input_policy_Release(scope); });
        QVERIFY(vlc_input_policy_IsSource(scope, "hakone-addon-source:v1:player"));
        controller.setArt(source.get(), chosen.toString(QUrl::FullyEncoded)); QCOMPARE(controller.getArtwork(), chosen);
        vlc_input_policy_Revoke(scope); QVERIFY(controller.getArtwork().isEmpty());
        SharedInputItem native(input_item_New("file:///native.flac", "Native"), false);
        input_item_SetTitle(native.get(), "Native title"); input_item_SetArtURL(native.get(), "http://127.0.0.1/native.png");
        { vlc_player_locker lock(player); QCOMPARE(vlc_player_SetCurrentMedia(player, native.get()), VLC_SUCCESS); }
        QTRY_COMPARE(controller.getTitle(), QStringLiteral("Native title"));
        QCOMPARE(controller.getArtwork(), QUrl(QStringLiteral("http://127.0.0.1/native.png")));
    }
};
QTEST_MAIN(TestAddonArtworkConsumers)
#include "test_addon_artwork_consumers.moc"
