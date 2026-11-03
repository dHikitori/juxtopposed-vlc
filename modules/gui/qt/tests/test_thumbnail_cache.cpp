// SPDX-License-Identifier: GPL-2.0-or-later
#include "config.h"
#include <QTest>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QFile>
#include <QDateTime>
#include <memory>
#include <thread>
#include "../util/thumbnail_cache.hpp"

namespace {
constexpr int imageReady = 1; // Image.Ready
QString idOf(const QUrl& source) { return source.toString().section(QLatin1Char('/'), 3); }
// Qt's image threads, not the GUI thread, are where files are read.
QImage fromImageThread(QQuickImageProvider& provider, const QUrl& source, const QSize& size)
{
    QImage image;
    std::thread([&] { QSize ignored; image = provider.requestImage(idOf(source), &ignored, size); }).join();
    return image;
}
}

class ThumbnailCacheTest : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;
    QUrl write(const QString& name, QSize size, QColor color)
    {
        QImage image(size, QImage::Format_RGB32);
        image.fill(color);
        const auto path = m_dir.filePath(name);
        if (!image.save(path, "PNG")) return {};
        return QUrl::fromLocalFile(path);
    }
private slots:
    void sourceWrapsOnlyLocalFiles()
    {
        ThumbnailCache cache;
        const auto file = QUrl::fromLocalFile(m_dir.filePath("Film #1 & more?.png"));
        const auto source = cache.source(file);
        QVERIFY(source.toString().startsWith("image://librarythumb/"));
        QCOMPARE(QUrl::fromEncoded(QByteArray::fromBase64(idOf(source).toLatin1(), QByteArray::Base64UrlEncoding)), file);
        for (const auto& other : {QUrl("https://example.org/a.jpg"), QUrl("image://mlcustomcover?id=1"), QUrl()})
            QCOMPARE(cache.source(other), other);
    }

    void theGuiThreadIsAnsweredFromMemoryOnly()
    {
        ThumbnailCache cache;
        std::unique_ptr<QQuickImageProvider> provider(cache.createProvider());
        const auto source = cache.source(write("gui.png", {400, 200}, Qt::red));
        QSize size;
        QVERIFY(provider->requestImage(idOf(source), &size, {100, 100}).isNull());
        QVERIFY(!cache.ready(source, {100, 100}));
        // Decoded to cover the tile, as a cropped Image would be.
        const auto image = fromImageThread(*provider, source, {100, 100});
        QCOMPARE(image.size(), QSize(200, 100));
        QVERIFY(cache.ready(source, {100, 100}));
        QVERIFY(!cache.ready(source, {50, 50}));
        QCOMPARE(provider->requestImage(idOf(source), &size, {100, 100}).cacheKey(), image.cacheKey());
        QCOMPARE(size, QSize(200, 100));
        // Small pictures are kept as they are.
        QCOMPARE(fromImageThread(*provider, source, {800, 800}).size(), QSize(400, 200));
    }

    void revalidatingRereadsOnlyChangedFiles()
    {
        ThumbnailCache cache;
        std::unique_ptr<QQuickImageProvider> provider(cache.createProvider());
        const auto file = write("changing.png", {64, 64}, Qt::red);
        const auto kept = cache.source(write("kept.png", {64, 64}, Qt::green));
        const auto source = cache.source(file);
        const auto first = fromImageThread(*provider, source, {64, 64});
        const auto keptImage = fromImageThread(*provider, kept, {64, 64});
        QCOMPARE(first.pixelColor(0, 0), QColor(Qt::red));
        QVERIFY(write("changing.png", {64, 64}, Qt::blue).isValid());
        QFile changed(file.toLocalFile());
        QVERIFY(changed.open(QIODevice::ReadWrite));
        QVERIFY(changed.setFileTime(QDateTime::currentDateTime().addSecs(60), QFileDevice::FileModificationTime));
        changed.close();
        cache.revalidate();
        QVERIFY(!cache.ready(source, {64, 64}));
        QVERIFY(!cache.ready(kept, {64, 64}));
        QCOMPARE(fromImageThread(*provider, kept, {64, 64}).cacheKey(), keptImage.cacheKey());
        QVERIFY(cache.ready(kept, {64, 64}));
        QCOMPARE(fromImageThread(*provider, source, {64, 64}).pixelColor(0, 0), QColor(Qt::blue));
        QVERIFY(cache.ready(source, {64, 64}));
    }

    void theOldestPicturesGoFirstWhenFull()
    {
        // Each picture is 80 KB; room for three.
        ThumbnailCache cache(250 * 1024);
        std::unique_ptr<QQuickImageProvider> provider(cache.createProvider());
        QList<QUrl> sources;
        for (int i = 0; i < 4; ++i) {
            sources.append(cache.source(write(QStringLiteral("full%1.png").arg(i), {200, 100}, Qt::gray)));
            QVERIFY(!fromImageThread(*provider, sources.last(), {200, 100}).isNull());
            if (i == 2) QVERIFY(cache.ready(sources[0], {200, 100})); // seen again: kept
        }
        QVERIFY(cache.ready(sources[0], {200, 100}));
        QVERIFY(!cache.ready(sources[1], {200, 100}));
        QVERIFY(cache.ready(sources[3], {200, 100}));
    }

    void aTileThatScrollsBackShowsItsPictureAtOnce()
    {
        ThumbnailCache cache;
        QQuickView view;
        view.engine()->addImageProvider(QLatin1String(ThumbnailCache::providerId), cache.createProvider());
        view.engine()->rootContext()->setContextProperty(QStringLiteral("redesignThumbnails"), &cache);
        const auto poster = write("poster.png", {640, 360}, Qt::red);
        const auto other = write("other.png", {640, 360}, Qt::blue);
        const auto big = write("big.png", {1000, 1000}, Qt::gray);
        QQmlComponent component(view.engine());
        component.setData("import QtQuick\nimport \".\" as Redesign\n"
            "Redesign.RedesignLibraryArtwork { width: 160; height: 90; iconName: \"video\" }",
            QUrl::fromLocalFile(QStringLiteral(TOP_SRCDIR "/modules/gui/qt/player/qml/redesign/")));
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        QQmlComponent plain(view.engine());
        plain.setData("import QtQuick\nImage { asynchronous: true }", QUrl());
        auto* root = new QQuickItem;
        view.setContent(QUrl(), nullptr, root); view.resize(400, 300); view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        const auto tile = [&](const QUrl& source) {
            auto* item = qobject_cast<QQuickItem*>(component.createWithInitialProperties({{"source", source}}));
            if (item) item->setParentItem(root);
            return item;
        };
        const auto image = [](QQuickItem* item) { return item->findChild<QObject*>("libraryArtworkImage"); };

        auto* first = tile(poster);
        QVERIFY2(first, qPrintable(component.errorString()));
        QVERIFY(image(first)->property("asynchronous").toBool());
        QTRY_VERIFY(first->property("hasArtwork").toBool());
        delete first;
        // Push the poster out of Qt's own small cache of unshown images.
        auto* filler = qobject_cast<QQuickItem*>(plain.createWithInitialProperties({{"source", big}}));
        QVERIFY(filler); filler->setParentItem(root);
        QTRY_COMPARE(filler->property("status").toInt(), imageReady);
        delete filler;
        QCoreApplication::processEvents();

        // Created again, as a scrolled-back tile is, it is ready before a frame.
        auto* again = tile(poster);
        QVERIFY(again);
        QCOMPARE(image(again)->property("status").toInt(), imageReady);
        QVERIFY(again->property("hasArtwork").toBool());
        // A tile reused for a picture not read yet loads it in the background,
        // and one switched back to a read picture shows it at once.
        again->setProperty("source", other);
        QVERIFY(image(again)->property("asynchronous").toBool());
        QTRY_VERIFY(again->property("hasArtwork").toBool());
        again->setProperty("source", poster);
        QCOMPARE(image(again)->property("status").toInt(), imageReady);
        delete again;
    }
};

QTEST_MAIN(ThumbnailCacheTest)
#include "test_thumbnail_cache.moc"
