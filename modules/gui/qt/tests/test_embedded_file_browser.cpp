// SPDX-License-Identifier: GPL-2.0-or-later
#include "widgets/native/embedded_file_browser.hpp"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileSystemModel>
#include <QLabel>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QVBoxLayout>

class TestEmbeddedFileBrowser : public QObject
{
    Q_OBJECT
    QTemporaryDir directory;

    QAbstractItemView* fileView(EmbeddedFileBrowser& browser)
    {
        for (auto* view : browser.findChildren<QAbstractItemView*>())
            if (view->isVisible() && qobject_cast<QFileSystemModel*>(view->model())) return view;
        return nullptr;
    }
    // Where a file's row is, once the model has listed it; empty until then.
    QRect fileRect(EmbeddedFileBrowser& browser, const QString& name)
    {
        auto* view = fileView(browser);
        if (!view) return {};
        auto* model = qobject_cast<QFileSystemModel*>(view->model());
        const auto index = model->index(directory.filePath(name));
        if (!index.isValid()) return {};
        view->scrollTo(index);
        QCoreApplication::processEvents();
        return view->visualRect(index);
    }
    bool fileShown(EmbeddedFileBrowser& browser, const QString& name) { return !fileRect(browser, name).isEmpty(); }
    // Not for QTRY_*: those evaluate their expression again after it holds,
    // which would click twice.
    bool clickFile(EmbeddedFileBrowser& browser, const QString& name, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        const auto rect = fileRect(browser, name);
        if (rect.isEmpty()) return false;
        QTest::mouseClick(fileView(browser)->viewport(), Qt::LeftButton, modifiers, rect.center());
        return true;
    }
    QLineEdit* nameEdit(EmbeddedFileBrowser& browser)
    {
        for (auto* label : browser.findChildren<QLabel*>())
            if (label->text() == browser.labelText(QFileDialog::FileName))
                return qobject_cast<QLineEdit*>(label->buddy());
        return nullptr;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(directory.isValid());
        for (const auto& name : {"First file.mkv", "Second file.mkv"}) {
            QFile file(directory.filePath(QLatin1String(name)));
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        QVERIFY(QDir(directory.path()).mkdir(QStringLiteral("Folder")));
    }
    void selectionUpdatesForSingleMultipleAndDeselectedRows()
    {
        QWidget host;
        QVBoxLayout layout(&host);
        EmbeddedFileBrowser browser(&host);
        layout.addWidget(&browser);
        browser.setDirectory(directory.path());
        host.resize(800, 600); host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        QSignalSpy changed(&browser, &EmbeddedFileBrowser::selectionChanged);
        QTRY_VERIFY(fileShown(browser, QStringLiteral("First file.mkv"))); QVERIFY(clickFile(browser, QStringLiteral("First file.mkv")));
        QTRY_COMPARE(browser.selectedMediaUrls(), QList<QUrl>{QUrl::fromLocalFile(directory.filePath("First file.mkv"))});
        const auto firstChanges = changed.size();
        QVERIFY(clickFile(browser, QStringLiteral("Second file.mkv"), Qt::ControlModifier));
        QTRY_COMPARE(browser.selectedMediaUrls().size(), 2);
        QVERIFY(changed.size() > firstChanges);
        const auto multipleChanges = changed.size();
        QVERIFY(clickFile(browser, QStringLiteral("First file.mkv"), Qt::ControlModifier));
        QTRY_COMPARE(browser.selectedMediaUrls(), QList<QUrl>{QUrl::fromLocalFile(directory.filePath("Second file.mkv"))});
        QVERIFY(changed.size() > multipleChanges);
    }
    void clearingRemovesBothRowSelectionAndTypedFilename()
    {
        QWidget host;
        QVBoxLayout layout(&host);
        EmbeddedFileBrowser browser(&host);
        layout.addWidget(&browser);
        browser.setDirectory(directory.path());
        host.resize(800, 600); host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        QTRY_VERIFY(fileShown(browser, QStringLiteral("First file.mkv"))); QVERIFY(clickFile(browser, QStringLiteral("First file.mkv")));
        browser.clearSelection();
        QVERIFY(browser.selectedMediaUrls().isEmpty());
        auto* edit = nameEdit(browser);
        QVERIFY(edit); edit->setFocus();
        QTest::keyClicks(edit, "Second file.mkv");
        QTRY_VERIFY(!browser.selectedMediaUrls().isEmpty());
        QSignalSpy changed(&browser, &EmbeddedFileBrowser::selectionChanged);
        browser.clearSelection();
        QVERIFY(browser.selectedUrls().isEmpty());
        QVERIFY(edit->text().isEmpty());
        QCOMPARE(changed.size(), 1);
    }
    void hostOwnsAcceptanceCancellationAndButtons()
    {
        QWidget host;
        QVBoxLayout layout(&host);
        EmbeddedFileBrowser browser(&host);
        layout.addWidget(&browser);
        browser.setDirectory(directory.path());
        host.resize(800, 600); host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        QVERIFY(!browser.isWindow());
        QVERIFY(browser.testOption(QFileDialog::DontUseNativeDialog));
        QVERIFY(browser.testOption(QFileDialog::ReadOnly));
        for (auto* buttons : browser.findChildren<QDialogButtonBox*>()) QVERIFY(!buttons->isVisible());
        QTRY_VERIFY(fileShown(browser, QStringLiteral("First file.mkv"))); QVERIFY(clickFile(browser, QStringLiteral("First file.mkv")));
        QSignalSpy activated(&browser, &EmbeddedFileBrowser::filesActivated);
        QSignalSpy cancelled(&browser, &EmbeddedFileBrowser::cancelRequested);
        QVERIFY(QMetaObject::invokeMethod(&browser, "accept"));
        QCOMPARE(activated.size(), 1);
        QVERIFY(browser.isVisible());
        QVERIFY(QMetaObject::invokeMethod(&browser, "reject"));
        QCOMPARE(cancelled.size(), 1);
        QVERIFY(browser.isVisible());
    }
    void folderActivationNavigatesRatherThanAcceptingMedia()
    {
        QWidget host;
        QVBoxLayout layout(&host);
        EmbeddedFileBrowser browser(&host);
        layout.addWidget(&browser);
        browser.setDirectory(directory.path());
        host.resize(800, 600); host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        QTRY_VERIFY(fileShown(browser, QStringLiteral("Folder"))); QVERIFY(clickFile(browser, QStringLiteral("Folder")));
        QSignalSpy activated(&browser, &EmbeddedFileBrowser::filesActivated);
        QVERIFY(QMetaObject::invokeMethod(&browser, "accept"));
        QCOMPARE(browser.directory().absolutePath(), directory.filePath("Folder"));
        QCOMPARE(activated.size(), 0);
        QVERIFY(browser.selectedMediaUrls().isEmpty());
    }
    void validatingHostReceivesOneFileSelection()
    {
        QWidget host;
        QVBoxLayout layout(&host);
        EmbeddedFileBrowser browser(&host);
        layout.addWidget(&browser);
        browser.setDirectory(directory.path());
        host.resize(800, 600); host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        QTRY_VERIFY(fileShown(browser, QStringLiteral("First file.mkv"))); QVERIFY(clickFile(browser, QStringLiteral("First file.mkv")));
        QSignalSpy selected(&browser, &QFileDialog::filesSelected);
        QSignalSpy activated(&browser, &EmbeddedFileBrowser::filesActivated);
        int accepted = 0;
        connect(&browser, &EmbeddedFileBrowser::filesActivated, &host, [&] {
            if (browser.validateSelection()) ++accepted;
        });
        QVERIFY(QMetaObject::invokeMethod(&browser, "accept"));
        QCOMPARE(activated.size(), 1);
        QCOMPARE(accepted, 1);
        QCOMPARE(selected.size(), 1);
        QCOMPARE(selected.first().first().toStringList(), QStringList{directory.filePath("First file.mkv")});
        QVERIFY(browser.isVisible());
    }
    void droppedUrlsStayLosslessUntilTheUserChoosesAnotherFile()
    {
        QWidget host;
        QVBoxLayout layout(&host);
        EmbeddedFileBrowser browser(&host);
        layout.addWidget(&browser);
        browser.setDirectory(directory.path());
        host.resize(800, 600); host.show();
        QVERIFY(QTest::qWaitForWindowExposed(&host));
        const QList<QUrl> urls {QUrl::fromLocalFile(directory.filePath("First file.mkv")),
            QUrl::fromLocalFile(directory.filePath("Other folder/Name with \"quotes\".mkv")),
            QUrl(QStringLiteral("smb://example.invalid/Movies/An%20episode.mkv"))};
        browser.selectUrls(urls);
        QCOMPARE(browser.selectedMediaUrls(), urls);
        QSignalSpy activated(&browser, &EmbeddedFileBrowser::filesActivated);
        QVERIFY(QMetaObject::invokeMethod(&browser, "accept"));
        QCOMPARE(activated.size(), 1);
        QTRY_VERIFY(fileShown(browser, QStringLiteral("Second file.mkv"))); QVERIFY(clickFile(browser, QStringLiteral("Second file.mkv")));
        QTRY_COMPARE(browser.selectedMediaUrls(), QList<QUrl>{QUrl::fromLocalFile(directory.filePath("Second file.mkv"))});
        browser.selectUrls(urls);
        browser.clearSelection();
        QVERIFY(browser.selectedMediaUrls().isEmpty());
    }
};
QTEST_MAIN(TestEmbeddedFileBrowser)
#include "test_embedded_file_browser.moc"
