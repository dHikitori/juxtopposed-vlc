// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <vlc_common.h>
#include <QtTest/QtTest>
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <vlc_input_item.h>
#include <vlc_input_policy.h>
#include <vlc_artwork_policy.h>
#include "util/artwork_selection.hpp"

namespace {
using Lifetime=vlc::addons::AddonStreamLifetime;
using Item=std::shared_ptr<input_item_t>;
Item item(const char* uri="file:///native.flac") { return {input_item_New(uri,"Song"),input_item_Release}; }
QByteArray image(const char* format="PNG",int width=3,QColor color=Qt::red)
{
    QImage pixels(width,2,QImage::Format_RGB32); pixels.fill(color);
    QByteArray bytes; QBuffer output(&bytes); output.open(QIODevice::WriteOnly); pixels.save(&output,format); return bytes;
}
QByteArray entropyImage()
{
    QImage pixels(4096,4096,QImage::Format_ARGB32); quint32 state=0x6db027e1;
    for(int row=0;row<pixels.height();++row) {
        auto* values=reinterpret_cast<QRgb*>(pixels.scanLine(row));
        for(int column=0;column<pixels.width();++column) {
            state^=state<<13; state^=state>>17; state^=state<<5; values[column]=state;
        }
    }
    QByteArray bytes; QBuffer output(&bytes); output.open(QIODevice::WriteOnly); pixels.save(&output,"PNG"); return bytes;
}
QUrl write(QTemporaryDir& directory,const QByteArray& bytes,const QString& name=QStringLiteral("image.bin"))
{
    const auto path=directory.filePath(name); QFile output(path);
    if(!output.open(QIODevice::WriteOnly) || output.write(bytes)!=bytes.size()) return {};
    return QUrl::fromLocalFile(path);
}
QByteArray artwork(input_item_t* target)
{ char* raw=vlc_artwork_item_GetURI(target); const QByteArray value(raw ? raw : ""); free(raw); return value; }
struct Block {
    std::mutex mutex; std::condition_variable changed;
    std::atomic_uint entered{0}; bool released=false;
    void wait() { ++entered; std::unique_lock<std::mutex> lock(mutex); changed.wait(lock,[&] { return released; }); }
    void release() { { std::lock_guard<std::mutex> lock(mutex); released=true; } changed.notify_all(); }
};
}
class TestArtworkSelection : public QObject {
    Q_OBJECT
private slots:
    void cleanup() {
        QTRY_COMPARE_WITH_TIMEOUT(ArtworkSelection::retainedCount(),0u,5000);
        QTRY_COMPARE_WITH_TIMEOUT(ArtworkSelection::activeCount(),0u,5000);
    }
    void prepareDoesNotChangeTheTargetAndSaveUsesThePrivateSnapshot() {
        Lifetime lifetime; QTemporaryDir source,cache; const auto file=write(source,image()); QVERIFY(file.isLocalFile());
        ArtworkSelection selection(lifetime.handle(),cache.path()); auto target=item(); input_item_SetArtURL(target.get(),"file:///old.png");
        ArtworkSelection::Result result; bool completed=false;
        const auto generation=selection.prepare(target.get(),file,[&](auto value) { result=std::move(value); completed=true; }); QVERIFY(generation);
        QTRY_VERIFY(completed); QVERIFY2(result.error.isEmpty(),qPrintable(result.error)); QVERIFY(result.receipt); QVERIFY(!result.preview.isNull());
        QCOMPARE(artwork(target.get()),QByteArray("file:///old.png")); QVERIFY(result.receipt->current(target.get(),generation));
        QVERIFY(write(source,image("PNG",3,Qt::green))==file);
        QCOMPARE(result.preview.pixelColor(0,0),QColor(Qt::red));
        Item copy(input_item_Copy(target.get()),input_item_Release); QVERIFY(copy); QVERIFY(result.receipt->stampCopy(copy.get()));
        QVERIFY(result.receipt->file()!=file);
#ifdef Q_OS_UNIX
        QVERIFY(!(QFileInfo(result.receipt->file().toLocalFile()).permissions() & (QFileDevice::ReadGroup|QFileDevice::WriteGroup|QFileDevice::ReadOther|QFileDevice::WriteOther)));
#endif
        QCOMPARE(result.receipt->apply(target.get(),generation),VLC_SUCCESS);
        QCOMPARE(artwork(target.get()),result.receipt->file().toEncoded());
        QCOMPARE(QImage(result.receipt->file().toLocalFile()).pixelColor(0,0),QColor(Qt::red));
        QVERIFY(result.receipt->apply(target.get(),generation)!=VLC_SUCCESS);
        result={}; selection.close(); lifetime.close(); QVERIFY(lifetime.handle().waitQt(std::chrono::seconds(3)));
    }
    void nativeScopedChoiceAcceptsLargeBitmapAndBindsThePolicy() {
        Lifetime lifetime; QTemporaryDir source,cache; const auto file=write(source,image("BMP",1025));
        ArtworkSelection selection(lifetime.handle(),cache.path()); auto target=item("hakone-addon-source:v1:scoped");
        auto* raw=vlc_input_policy_HoldSourceItem(target.get(),"hakone-addon-source:v1:scoped"); QVERIFY(raw);
        const auto policy=std::shared_ptr<vlc_input_policy_t>(raw,vlc_input_policy_Release);
        QCOMPARE(vlc_input_policy_AttachItem(target.get(),policy.get()),VLC_SUCCESS);
        ArtworkSelection::Result result; bool completed=false;
        const auto generation=selection.prepare(target.get(),file,[&](auto value) { result=std::move(value); completed=true; }); QVERIFY(generation);
        QTRY_VERIFY(completed); QVERIFY2(result.error.isEmpty(),qPrintable(result.error)); QVERIFY(result.receipt);
        QCOMPARE(result.receipt->apply(target.get(),generation),VLC_SUCCESS); QVERIFY(!artwork(target.get()).isEmpty());
        vlc_input_policy_Revoke(policy.get()); QVERIFY(artwork(target.get()).isEmpty());
        result={}; selection.close(); lifetime.close(); QVERIFY(lifetime.handle().waitQt(std::chrono::seconds(3)));
    }
    void aNativeFileChoiceCanReadAnUncompressedBitmapAbove32MiB() {
        Lifetime lifetime; QTemporaryDir source,cache; QImage pixels(5000,4000,QImage::Format_RGB32); pixels.fill(Qt::red);
        QByteArray bytes; QBuffer output(&bytes); QVERIFY(output.open(QIODevice::WriteOnly)); QVERIFY(pixels.save(&output,"BMP"));
        QVERIFY(bytes.size()>32*1024*1024); const auto file=write(source,bytes); QVERIFY(file.isLocalFile());
        ArtworkSelection selection(lifetime.handle(),cache.path()); auto target=item(); bool completed=false; ArtworkSelection::Result result;
        const auto generation=selection.prepare(target.get(),file,[&](auto value) { result=std::move(value); completed=true; }); QVERIFY(generation);
        QTRY_VERIFY_WITH_TIMEOUT(completed,10000); QVERIFY2(result.receipt,qPrintable(result.error));
        QCOMPARE(result.preview.pixelColor(0,0),QColor(Qt::red)); QCOMPARE(result.receipt->apply(target.get(),generation),VLC_SUCCESS);
        result={}; selection.close(); lifetime.close(); QVERIFY(lifetime.handle().waitQt(std::chrono::seconds(3)));
    }
    void aHighEntropyChoiceKeepsTheFullPreparedDimensions() {
        Lifetime lifetime; QTemporaryDir source,cache; const auto bytes=entropyImage(); QVERIFY(bytes.size()>32*1024*1024);
        const auto file=write(source,bytes); QVERIFY(file.isLocalFile()); ArtworkSelection selection(lifetime.handle(),cache.path());
        auto target=item(); bool completed=false; ArtworkSelection::Result result;
        const auto generation=selection.prepare(target.get(),file,[&](auto value) { result=std::move(value); completed=true; }); QVERIFY(generation);
        QTRY_VERIFY_WITH_TIMEOUT(completed,30000); QVERIFY2(result.receipt,qPrintable(result.error));
        const auto prepared=result.receipt->file().toLocalFile(); QVERIFY(QFileInfo(prepared).size()>32*1024*1024);
        QVERIFY(QFileInfo(prepared).size()<=80*1024*1024); QImageReader reader(prepared); QCOMPARE(reader.size(),QSize(4096,4096));
        QCOMPARE(result.receipt->apply(target.get(),generation),VLC_SUCCESS);
        result={}; selection.close(); lifetime.close(); QVERIFY(lifetime.handle().waitQt(std::chrono::seconds(3)));
    }
    void changedTargetsAndSelectionGenerationsRejectTheReceipt() {
        for(int change=0;change<4;++change) {
            Lifetime lifetime; QTemporaryDir source,cache; const auto file=write(source,image());
            ArtworkSelection selection(lifetime.handle(),cache.path()); auto target=item(); input_item_SetArtURL(target.get(),"file:///old.png");
            ArtworkSelection::Result result; bool completed=false;
            const auto generation=selection.prepare(target.get(),file,[&](auto value) { result=std::move(value); completed=true; }); QVERIFY(generation);
            QTRY_VERIFY(completed); QVERIFY(result.receipt);
            if(change==0) input_item_SetArtURL(target.get(),"file:///old.png");
            if(change==1) input_item_SetTitle(target.get(),"Changed");
            if(change==2) input_item_SetURI(target.get(),"file:///other.flac");
            if(change==3) selection.cancel();
            QVERIFY(!result.receipt->current(target.get(),generation));
            QVERIFY(result.receipt->apply(target.get(),generation)!=VLC_SUCCESS);
            QCOMPARE(artwork(target.get()),QByteArray("file:///old.png"));
            result={}; selection.close(); lifetime.close(); QVERIFY(lifetime.handle().waitQt(std::chrono::seconds(3)));
        }
    }
    void aReturnedSourceAddressCannotReuseThePreparedNativeRevision() {
        Lifetime lifetime; QTemporaryDir source,cache; const auto file=write(source,image());
        ArtworkSelection selection(lifetime.handle(),cache.path()); auto target=item(); input_item_SetArtURL(target.get(),"file:///old.png");
        ArtworkSelection::Result result; bool completed=false;
        const auto generation=selection.prepare(target.get(),file,[&](auto value) { result=std::move(value); completed=true; }); QVERIFY(generation);
        QTRY_VERIFY(completed); QVERIFY(result.receipt);
        input_item_SetURI(target.get(),"file:///other.flac"); input_item_SetURI(target.get(),"file:///native.flac");
        QVERIFY(!result.receipt->current(target.get(),generation));
        Item copy(input_item_Copy(target.get()),input_item_Release); QVERIFY(copy); QVERIFY(!result.receipt->stampCopy(copy.get()));
        QVERIFY(result.receipt->apply(target.get(),generation)!=VLC_SUCCESS);
        QCOMPARE(artwork(target.get()),QByteArray("file:///old.png"));
        result={}; selection.close(); lifetime.close(); QVERIFY(lifetime.handle().waitQt(std::chrono::seconds(3)));
    }
    void cancelAndDestructionDoNotWaitForTheFileReader() {
        Lifetime lifetime; QTemporaryDir source,cache; const auto file=write(source,image());
        const auto block=std::make_shared<Block>(); const auto release=qScopeGuard([block] { block->release(); });
        ArtworkSelection::Backend backend; backend.read=[block](const QString&,qint64,const auto&,QString*) { block->wait(); return image(); };
        auto selection=std::make_unique<ArtworkSelection>(lifetime.handle(),cache.path(),ArtworkSelection::Options{},backend);
        auto target=item(); bool completed=false;
        QVERIFY(selection->prepare(target.get(),file,[&](auto) { completed=true; })); QTRY_COMPARE(block->entered.load(),1u);
        QElapsedTimer elapsed; elapsed.start(); selection.reset(); QVERIFY(elapsed.elapsed()<250);
        lifetime.close(); QVERIFY(!lifetime.handle().waitQt(std::chrono::milliseconds(10))); QVERIFY(!completed);
        block->release(); QVERIFY(lifetime.handle().waitQt(std::chrono::seconds(3))); QCoreApplication::processEvents(); QVERIFY(!completed);
    }
    void lateReaderCannotReplaceTheNewSelection() {
        Lifetime lifetime; QTemporaryDir source,cache; const auto file=write(source,image()); const auto other=write(source,image("PNG",3,Qt::green),QStringLiteral("green.png"));
        const auto block=std::make_shared<Block>(); const auto release=qScopeGuard([block] { block->release(); });
        std::atomic_uint calls{0}; ArtworkSelection::Backend backend;
        backend.read=[&](const QString&,qint64,const auto&,QString*) { if(++calls==1) { block->wait(); return image(); } return image("PNG",3,Qt::green); };
        ArtworkSelection selection(lifetime.handle(),cache.path(),{},backend); auto target=item(); int delivered=0; ArtworkSelection::Result result;
        QVERIFY(selection.prepare(target.get(),file,[&](auto) { ++delivered; })); QTRY_COMPARE(block->entered.load(),1u);
        const auto generation=selection.prepare(target.get(),other,[&](auto value) { result=std::move(value); ++delivered; }); QVERIFY(generation);
        QCOMPARE(ArtworkSelection::activeCount(),1u); block->release(); QTRY_COMPARE(delivered,1);
        QVERIFY(result.receipt); QCOMPARE(result.generation,generation); QCOMPARE(result.preview.pixelColor(0,0),QColor(Qt::green));
        result={}; selection.close(); lifetime.close(); QVERIFY(lifetime.handle().waitQt(std::chrono::seconds(3)));
    }
    void remoteChoicesAreRefusedBeforeTheReader() {
        Lifetime lifetime; QTemporaryDir cache; std::atomic_uint reads{0}; ArtworkSelection::Backend backend;
        backend.read=[&](const QString&,qint64,const auto&,QString*) { ++reads; return image(); };
        ArtworkSelection selection(lifetime.handle(),cache.path(),{},backend); auto target=item();
        for(const auto* uri:{"https://example.org/art.png","file://server/share/art.png","file:////server/share/art.png","file:///tmp/art.png?query"})
            QVERIFY(!selection.prepare(target.get(),QUrl(QString::fromLatin1(uri)),[](auto) {}));
        QCOMPARE(reads.load(),0u); selection.close(); lifetime.close(); QVERIFY(lifetime.handle().qtDrained());
    }
    void aFullPrivateCacheCannotChangeTheLiveArtwork() {
        Lifetime lifetime; QTemporaryDir source,cache; const auto file=write(source,image()); auto target=item();
        input_item_SetArtURL(target.get(),"file:///old.png"); ArtworkSelection::Options options; options.maximumCacheBytes=1;
        ArtworkSelection selection(lifetime.handle(),cache.path(),options,{}); ArtworkSelection::Result result; bool completed=false;
        QVERIFY(selection.prepare(target.get(),file,[&](auto value) { result=std::move(value); completed=true; }));
        QTRY_VERIFY(completed); QVERIFY(!result.receipt); QVERIFY(!result.error.isEmpty());
        QCOMPARE(artwork(target.get()),QByteArray("file:///old.png"));
        selection.close(); lifetime.close(); QVERIFY(lifetime.handle().waitQt(std::chrono::seconds(3)));
    }
    void retainedReceiptsKeepTheProcessLimitAcrossOwners() {
        Lifetime lifetime; QTemporaryDir source,cache; const auto file=write(source,image()); auto target=item();
        std::vector<std::unique_ptr<ArtworkSelection>> selections;
        std::vector<std::shared_ptr<ArtworkSelection::Receipt>> receipts;
        for(unsigned index=0;index<16;++index) {
            selections.push_back(std::make_unique<ArtworkSelection>(lifetime.handle(),cache.path())); bool completed=false;
            QVERIFY(selections.back()->prepare(target.get(),file,[&](auto result) { receipts.push_back(std::move(result.receipt)); completed=true; }));
            QTRY_VERIFY(completed); QVERIFY(receipts.back());
        }
        QCOMPARE(ArtworkSelection::retainedCount(),16u); ArtworkSelection excess(lifetime.handle(),cache.path());
        QVERIFY(!excess.prepare(target.get(),file,[](auto) {}));
        for(auto& selection:selections) selection->close();
        receipts.clear(); selections.clear();
        excess.close(); lifetime.close(); QVERIFY(lifetime.handle().waitQt(std::chrono::seconds(3)));
    }
    void nativePreviewKeepsTheExistingNativeReader() {
        Lifetime lifetime; QTemporaryDir directory; const auto file=write(directory,image("BMP",1025));
        ArtworkNativePreview preview(lifetime.handle()); QImage result; bool completed=false;
        preview.request(file.toLocalFile(),[&](QImage value) { result=std::move(value); completed=true; });
        QTRY_VERIFY(completed); QVERIFY(!result.isNull()); QVERIFY(result.width()<=512); QCOMPARE(result.pixelColor(0,0),QColor(Qt::red));
        preview.close(); lifetime.close(); QVERIFY(lifetime.handle().waitQt(std::chrono::seconds(3)));
    }
};
QTEST_GUILESS_MAIN(TestArtworkSelection)
#include "test_artwork_selection.moc"
