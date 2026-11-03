// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <vlc_common.h>
#include <QtTest/QtTest>
#include <QFile>
#include <QImage>
#include <QLineEdit>
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
#include "widgets/native/interface_widgets.hpp"
#include "dialogs/mediainfo/info_panels.hpp"

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
QByteArray wave()
{
    QByteArray bytes(204,'\0'); bytes.replace(0,4,"RIFF"); bytes.replace(8,8,"WAVEfmt ");
    qToLittleEndian<quint32>(196,bytes.data()+4); qToLittleEndian<quint32>(16,bytes.data()+16);
    qToLittleEndian<quint16>(1,bytes.data()+20); qToLittleEndian<quint16>(1,bytes.data()+22);
    qToLittleEndian<quint32>(8000,bytes.data()+24); qToLittleEndian<quint32>(16000,bytes.data()+28);
    qToLittleEndian<quint16>(2,bytes.data()+32); qToLittleEndian<quint16>(16,bytes.data()+34);
    bytes.replace(36,4,"data"); qToLittleEndian<quint32>(160,bytes.data()+40); return bytes;
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
    void localChoiceUsesAPreparedNativeReceiptForScopedMedia()
    {
        vlc::addons::AddonStreamLifetime lifetime; QTemporaryDir directory,cache;
        ArtworkSelection selection(lifetime.handle(),cache.path());
        const auto valid=png(directory,QStringLiteral("wide.png"),1025); QVERIFY(!valid.isEmpty());
        SharedInputItem source(input_item_New("hakone-addon-source:v1:choice","Source"),false);
        input_item_SetArtURL(source.get(),"http://127.0.0.1/private.png");
        ArtworkSelection::Result result; bool completed=false;
        const auto generation=selection.prepare(source.get(),valid,[&](auto value) { result=std::move(value); completed=true; }); QVERIFY(generation);
        QTRY_VERIFY(completed); QVERIFY(result.receipt); QVERIFY(PlayerController::decodeArtURL(source.get()).isEmpty());
        QCOMPARE(result.receipt->apply(source.get(),generation),VLC_SUCCESS);
        QCOMPARE(PlayerController::decodeArtURL(source.get()),result.receipt->file().toLocalFile());
        QVERIFY(result.receipt->file()!=valid);
        const auto external=directory.filePath(QStringLiteral("external.svg"));
        QVERIFY(write(external,"<svg xmlns='http://www.w3.org/2000/svg' width='2' height='2'><image href='http://127.0.0.1/private.png'/></svg>"));
        const auto original=PlayerController::decodeArtURL(source.get()); result={}; completed=false;
        QVERIFY(selection.prepare(source.get(),QUrl::fromLocalFile(external),[&](auto value) { result=std::move(value); completed=true; }));
        QTRY_VERIFY(completed); QVERIFY(!result.receipt); QVERIFY(!result.error.isEmpty());
        QCOMPARE(PlayerController::decodeArtURL(source.get()),original);
        for(const auto& invalid:{QStringLiteral("http://127.0.0.1/private.png"),QStringLiteral("file://server/share/image.png"),
                QStringLiteral("file:////server/share/image.png"),QStringLiteral("file:///%5cserver/share/image.png")})
            QVERIFY(!selection.prepare(source.get(),QUrl(invalid),[](auto) {}));
        selection.close(); lifetime.close(); QVERIFY(lifetime.handle().waitQt(std::chrono::seconds(3)));
    }
    void scopedUserChoiceNormalizesNativeCompressedMetadata()
    {
        vlc::addons::AddonStreamLifetime lifetime; QTemporaryDir directory,cache; const auto original = png(directory); QVERIFY(!original.isEmpty());
        QFile input(original.toLocalFile()); QVERIFY(input.open(QIODevice::ReadOnly)); auto bytes = input.readAll(); input.close();
        QByteArray payload("note\0\0", 6); payload += qCompress(QByteArray(1024 * 1024, 'x')).mid(4);
        QByteArray chunk(4, '\0'); qToBigEndian<quint32>(quint32(payload.size()), chunk.data()); chunk += "zTXt"; chunk += payload;
        const auto crc = crc32(0, reinterpret_cast<const Bytef*>(chunk.constData() + 4), uInt(chunk.size() - 4));
        QByteArray tail(4, '\0'); qToBigEndian<quint32>(quint32(crc), tail.data()); chunk += tail; bytes.insert(33, chunk);
        const auto candidate = QUrl::fromLocalFile(directory.filePath(QStringLiteral("metadata.png")));
        QVERIFY(write(candidate.toLocalFile(), bytes));
        ArtworkSelection selection(lifetime.handle(),cache.path());
        SharedInputItem source(input_item_New("hakone-addon-source:v1:choice","Source"),false);
        ArtworkSelection::Result result; bool completed=false;
        const auto generation=selection.prepare(source.get(),candidate,[&](auto value) { result=std::move(value); completed=true; }); QVERIFY(generation);
        QTRY_VERIFY(completed); QVERIFY2(result.error.isEmpty(),qPrintable(result.error)); QVERIFY(result.receipt);
        QCOMPARE(result.receipt->apply(source.get(),generation),VLC_SUCCESS);
        QFile normalized(result.receipt->file().toLocalFile()); QVERIFY(normalized.open(QIODevice::ReadOnly));
        QVERIFY(!normalized.readAll().contains("zTXt"));
        QCOMPARE(PlayerController::decodeArtURL(source.get()),result.receipt->file().toLocalFile());
        result={}; selection.close(); lifetime.close(); QVERIFY(lifetime.handle().waitQt(std::chrono::seconds(3)));
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
    void metadataChoicePreviewNeedsSaveAndCancelDiscardsLateWork()
    {
        auto* player=vlc_player_New(VLC_OBJECT(m_core->instance),VLC_PLAYER_LOCK_NORMAL); QVERIFY(player);
        const auto deletePlayer=qScopeGuard([&] { vlc_player_Delete(player); });
        auto* intf=vlc_object_create<qt_intf_t>(m_core->instance); QVERIFY(intf);
        const auto deleteIntf=qScopeGuard([&] { vlc_object_delete(intf); });
        intf->p_player=player; intf->p_mi=nullptr;
        var_Create(intf,"qt-discover-cast-devices",VLC_VAR_BOOL); var_SetBool(intf,"qt-discover-cast-devices",false);
        vlc::addons::AddonStreamLifetime lifetime; QTemporaryDir directory,cache;
        PlayerController controller(intf); intf->p_mainPlayerController=&controller;
        MetaPanel panel(nullptr,intf); SharedInputItem target(input_item_New("file:///native.flac","Native"),false);
        input_item_SetArtURL(target.get(),"file:///old-native.png"); panel.update(target);
        auto* cover=panel.findChild<CoverArtLabel*>(QStringLiteral("metadataCover")); QVERIFY(cover);
        cover->setArtworkSelectionLifetime(lifetime.handle(),cache.path()); const auto file=png(directory); QVERIFY(!file.isEmpty());
        cover->chooseArtFile(file); QVERIFY(cover->artworkPending()); QVERIFY(panel.isInputLocked()); QVERIFY(!panel.canSaveMeta());
        QTRY_VERIFY(!cover->artworkPending()); QVERIFY(cover->selectedReceipt()); QVERIFY(panel.canSaveMeta());
        QCOMPARE(PlayerController::decodeArtURL(target.get()),QStringLiteral("/old-native.png"));
        panel.discardEdits(); QVERIFY(!panel.isInEditMode()); QVERIFY(!cover->selectedReceipt());
        QCOMPARE(PlayerController::decodeArtURL(target.get()),QStringLiteral("/old-native.png"));
        cover->chooseArtFile(file); QVERIFY(cover->artworkPending()); panel.discardEdits();
        QVERIFY(!cover->artworkPending()); QVERIFY(!panel.isInEditMode());
        lifetime.close(); QVERIFY(lifetime.handle().waitQt(std::chrono::seconds(3))); QCoreApplication::processEvents();
        QVERIFY(!panel.isInEditMode()); QVERIFY(!cover->selectedReceipt());
        QCOMPARE(PlayerController::decodeArtURL(target.get()),QStringLiteral("/old-native.png"));
    }
    void aReentrantCancelCannotStartOrPublishAnArtworkChoice()
    {
        auto* player=vlc_player_New(VLC_OBJECT(m_core->instance),VLC_PLAYER_LOCK_NORMAL); QVERIFY(player);
        const auto deletePlayer=qScopeGuard([&] { vlc_player_Delete(player); });
        auto* intf=vlc_object_create<qt_intf_t>(m_core->instance); QVERIFY(intf);
        const auto deleteIntf=qScopeGuard([&] { vlc_object_delete(intf); });
        intf->p_player=player; intf->p_mi=nullptr;
        var_Create(intf,"qt-discover-cast-devices",VLC_VAR_BOOL); var_SetBool(intf,"qt-discover-cast-devices",false);
        vlc::addons::AddonStreamLifetime lifetime; QTemporaryDir directory,cache;
        PlayerController controller(intf); intf->p_mainPlayerController=&controller;
        MetaPanel panel(nullptr,intf); SharedInputItem target(input_item_New("file:///native.flac","Native"),false);
        panel.update(target); auto* cover=panel.findChild<CoverArtLabel*>(QStringLiteral("metadataCover")); QVERIFY(cover);
        cover->setArtworkSelectionLifetime(lifetime.handle(),cache.path()); const auto file=png(directory); QVERIFY(!file.isEmpty());
        connect(cover,&CoverArtLabel::artworkPreparationChanged,&panel,[&](bool pending) { if(pending) panel.discardEdits(); });
        cover->chooseArtFile(file); QVERIFY(!cover->artworkPending()); QVERIFY(!panel.isInEditMode());
        lifetime.close(); QVERIFY(lifetime.handle().waitQt(std::chrono::seconds(3))); QCoreApplication::processEvents();
        QVERIFY(!cover->selectedReceipt()); QVERIFY(!panel.isInEditMode()); QVERIFY(PlayerController::decodeArtURL(target.get()).isEmpty());
    }
    void saveWritesPreparedCoverBytesBeforeItAppliesLiveTitleEdits()
    {
        auto* player=vlc_player_New(VLC_OBJECT(m_core->instance),VLC_PLAYER_LOCK_NORMAL); QVERIFY(player);
        const auto deletePlayer=qScopeGuard([&] { vlc_player_Delete(player); });
        auto* intf=vlc_object_create<qt_intf_t>(m_core->instance); QVERIFY(intf);
        const auto deleteIntf=qScopeGuard([&] { vlc_object_delete(intf); });
        intf->p_player=player; intf->p_mi=nullptr;
        var_Create(intf,"qt-discover-cast-devices",VLC_VAR_BOOL); var_SetBool(intf,"qt-discover-cast-devices",false);
        vlc::addons::AddonStreamLifetime lifetime; QTemporaryDir directory,cache;
        const auto media=directory.filePath(QStringLiteral("native.wav")); QVERIFY(write(media,wave()));
        const auto mediaUri=QUrl::fromLocalFile(media).toEncoded();
        PlayerController controller(intf); intf->p_mainPlayerController=&controller;
        MetaPanel panel(nullptr,intf); SharedInputItem target(input_item_New(mediaUri.constData(),"Native"),false);
        input_item_SetTitle(target.get(),"Old title"); panel.update(target);
        auto* cover=panel.findChild<CoverArtLabel*>(QStringLiteral("metadataCover")); QVERIFY(cover);
        cover->setArtworkSelectionLifetime(lifetime.handle(),cache.path()); const auto file=png(directory); QVERIFY(!file.isEmpty());
        auto* title=panel.findChild<QLineEdit*>(QStringLiteral("metadataTitle")); QVERIFY(title); title->setText(QStringLiteral("Saved title"));
        bool receiptReady=false,saveAllowed=false,saved=false; QUrl prepared;
        connect(cover,&CoverArtLabel::artworkPreparationChanged,&panel,[&](bool pending) {
            if(pending) return;
            const auto receipt=cover->selectedReceipt(); receiptReady=bool(receipt);
            if(!receipt) return;
            prepared=receipt->file(); saveAllowed=panel.canSaveMeta(); saved=panel.saveMeta();
        });
        cover->chooseArtFile(file); QTRY_VERIFY(!cover->artworkPending()); QVERIFY(receiptReady); QVERIFY(saveAllowed); QVERIFY(saved);
        QCOMPARE(PlayerController::decodeArtURL(target.get()),prepared.toLocalFile());
        const auto liveTitle=std::unique_ptr<char,decltype(&free)>(input_item_GetTitle(target.get()),&free); QVERIFY(liveTitle);
        QCOMPARE(QByteArray(liveTitle.get()),QByteArray("Saved title")); QVERIFY(!panel.isInEditMode());
        QFile tags(media); QVERIFY(tags.open(QIODevice::ReadOnly)); const auto written=tags.readAll();
        QVERIFY(written.contains("APIC")); QVERIFY(written.contains("image/png")); QVERIFY(written.contains(QByteArray::fromHex("89504e470d0a1a0a")));
        lifetime.close(); QVERIFY(lifetime.handle().waitQt(std::chrono::seconds(3)));
    }
    void aMediaChangeDuringNotificationCannotPublishTheOldCover()
    {
        auto* player=vlc_player_New(VLC_OBJECT(m_core->instance),VLC_PLAYER_LOCK_NORMAL); QVERIFY(player);
        const auto deletePlayer=qScopeGuard([&] { vlc_player_Delete(player); });
        auto* intf=vlc_object_create<qt_intf_t>(m_core->instance); QVERIFY(intf);
        const auto deleteIntf=qScopeGuard([&] { vlc_object_delete(intf); });
        intf->p_player=player; intf->p_mi=nullptr;
        var_Create(intf,"qt-discover-cast-devices",VLC_VAR_BOOL); var_SetBool(intf,"qt-discover-cast-devices",false);
        vlc::addons::AddonStreamLifetime lifetime; QTemporaryDir directory,cache;
        PlayerController controller(intf); controller.setArtworkSelectionLifetime(lifetime.handle(),cache.path());
        SharedInputItem source(input_item_New("file:///first.flac","First"),false),next(input_item_New("file:///next.flac","Next"),false);
        input_item_SetTitle(source.get(),"First"); input_item_SetArtURL(source.get(),"file:///before.png");
        input_item_SetTitle(next.get(),"Next"); input_item_SetArtURL(next.get(),"file:///next.png");
        { vlc_player_locker lock(player); QCOMPARE(vlc_player_SetCurrentMedia(player,source.get()),VLC_SUCCESS); }
        QTRY_COMPARE(controller.getTitle(),QStringLiteral("First"));
        bool changed=false,oldCoverAfterChange=false; QString prepared;
        connect(&controller,&PlayerController::currentMetaChanged,&controller,[&](input_item_t* target) {
            if(target!=source.get() || changed) return;
            const auto path=PlayerController::decodeArtURL(source.get()); if(path.isEmpty() || path==QStringLiteral("/before.png")) return;
            changed=true; prepared=path;
            vlc_player_locker lock(player); QCOMPARE(vlc_player_SetCurrentMedia(player,next.get()),VLC_SUCCESS);
        });
        connect(&controller,QOverload<QString>::of(&PlayerController::artChanged),&controller,[&](const QString& path) {
            if(!changed || path!=prepared) return;
            vlc_player_locker lock(player); oldCoverAfterChange=vlc_player_GetCurrentMedia(player)==next.get();
        });
        const auto chosen=png(directory); QVERIFY(!chosen.isEmpty()); controller.setArt(source.get(),chosen.toString(QUrl::FullyEncoded));
        QTRY_VERIFY(changed); QTRY_COMPARE(controller.getTitle(),QStringLiteral("Next"));
        QVERIFY(!oldCoverAfterChange); QCOMPARE(controller.getArtwork(),QUrl(QStringLiteral("file:///next.png")));
        lifetime.close(); QVERIFY(lifetime.handle().waitQt(std::chrono::seconds(3)));
    }
    void mountedPlayerUsesTheNativeArtworkGate()
    {
        auto *player = vlc_player_New(VLC_OBJECT(m_core->instance), VLC_PLAYER_LOCK_NORMAL); QVERIFY(player);
        const auto deletePlayer = qScopeGuard([&] { vlc_player_Delete(player); });
        auto *intf = vlc_object_create<qt_intf_t>(m_core->instance); QVERIFY(intf);
        const auto deleteIntf = qScopeGuard([&] { vlc_object_delete(intf); });
        intf->p_player = player;
        var_Create(intf, "qt-discover-cast-devices", VLC_VAR_BOOL); var_SetBool(intf, "qt-discover-cast-devices", false);
        vlc::addons::AddonStreamLifetime artworkLifetime; QTemporaryDir artworkCache;
        PlayerController controller(intf); controller.setArtworkSelectionLifetime(artworkLifetime.handle(),artworkCache.path());
        SharedInputItem source(input_item_New("hakone-addon-source:v1:player", "Source"), false);
        input_item_SetTitle(source.get(), "Source title"); input_item_SetArtURL(source.get(), "http://127.0.0.1/private.png");
        { vlc_player_locker lock(player); QCOMPARE(vlc_player_SetCurrentMedia(player, source.get()), VLC_SUCCESS); }
        QTRY_COMPARE(controller.getTitle(), QStringLiteral("Source title")); QVERIFY(controller.getArtwork().isEmpty());
        QTemporaryDir directory; const auto chosen = png(directory); QVERIFY(!chosen.isEmpty());
        controller.setArt(source.get(), chosen.toString(QUrl::FullyEncoded));
        QTRY_VERIFY(controller.getArtwork().isLocalFile()); QVERIFY(controller.getArtwork()!=chosen);
        const auto prepared=controller.getArtwork();
        input_item_SetArtURL(source.get(), prepared.toEncoded().constData()); QVERIFY(controller.getArtwork().isEmpty());
        controller.refreshMediaMeta(source.get()); QVERIFY(controller.getArtwork().isEmpty());
        auto *scope = vlc_input_policy_HoldItem(source.get()); QVERIFY(scope);
        const auto release = qScopeGuard([&] { vlc_input_policy_Release(scope); });
        QVERIFY(vlc_input_policy_IsSource(scope, "hakone-addon-source:v1:player"));
        controller.setArt(source.get(), chosen.toString(QUrl::FullyEncoded)); QTRY_VERIFY(controller.getArtwork().isLocalFile());
        vlc_input_policy_Revoke(scope); QVERIFY(controller.getArtwork().isEmpty());
        SharedInputItem native(input_item_New("file:///native.flac", "Native"), false);
        input_item_SetTitle(native.get(), "Native title"); input_item_SetArtURL(native.get(), "http://127.0.0.1/native.png");
        { vlc_player_locker lock(player); QCOMPARE(vlc_player_SetCurrentMedia(player, native.get()), VLC_SUCCESS); }
        QTRY_COMPARE(controller.getTitle(), QStringLiteral("Native title"));
        QCOMPARE(controller.getArtwork(), QUrl(QStringLiteral("http://127.0.0.1/native.png")));
        artworkLifetime.close(); QVERIFY(artworkLifetime.handle().waitQt(std::chrono::seconds(3)));
    }
};
QTEST_MAIN(TestAddonArtworkConsumers)
#include "test_addon_artwork_consumers.moc"
