// SPDX-License-Identifier: GPL-2.0-or-later
#include <QTest>
#include <QApplication>
#include <QClipboard>
#include <QTextStream>
#include "util/message_log.hpp"

class TestMessageLog : public QObject
{
    Q_OBJECT
private:
    void populate(QPlainTextEdit& view)
    {
        view.resize(420, 180);
        view.setReadOnly(true);
        QStringList lines;
        for (int i = 0; i < 100; ++i)
            lines.append(QStringLiteral("Existing line %1: français العربية 中文").arg(i));
        view.setPlainText(lines.join(QLatin1Char('\n')));
        view.show();
        QTest::qWait(10);
    }
private slots:
    void selectionAndClipboard_data()
    {
        QTest::addColumn<bool>("atEnd");
        QTest::addColumn<bool>("reverse");
        QTest::newRow("middle-forward") << false << false;
        QTest::newRow("middle-reverse") << false << true;
        QTest::newRow("end-forward") << true << false;
        QTest::newRow("end-reverse") << true << true;
    }
    void selectionAndClipboard()
    {
        QFETCH(bool, atEnd); QFETCH(bool, reverse);
        QPlainTextEdit view; populate(view);
        const int end = atEnd ? view.document()->characterCount() - 1 : 100;
        QTextCursor cursor(view.document());
        cursor.setPosition(reverse ? end : end - 20);
        cursor.setPosition(reverse ? end - 20 : end, QTextCursor::KeepAnchor);
        view.setTextCursor(cursor);
        view.verticalScrollBar()->setValue(20);
        const auto selection = view.textCursor();
        const int position = selection.position(), anchor = selection.anchor();
        const auto selectedText = selection.selectedText();
        const int scroll = view.verticalScrollBar()->value();
        const auto point = view.cursorRect(view.cursorForPosition(QPoint(3, 3))).top();
        const QString clipboard = QStringLiteral("Keep the clipboard intact");
        QApplication::clipboard()->setText(clipboard);
        QList<MessageLogEntry> entries;
        for (int i = 0; i < 40; ++i)
            entries.append({VLC_MSG_WARN, QStringLiteral("decoder"), QStringLiteral("New line %1").arg(i)});
        appendMessageLog(&view, entries, {}, QColor("#228e53"));
        QCoreApplication::processEvents();
        QCOMPARE(view.textCursor().selectedText(), selectedText);
        QCOMPARE(view.textCursor().position(), position);
        QCOMPARE(view.textCursor().anchor(), anchor);
        QCOMPARE(view.verticalScrollBar()->value(), scroll);
        QCOMPARE(view.cursorRect(view.cursorForPosition(QPoint(3, 3))).top(), point);
        QCOMPARE(QApplication::clipboard()->text(), clipboard);
        QVERIFY(view.toPlainText().endsWith(QStringLiteral("decoder warning: New line 39")));
    }
    void scrolling_data()
    {
        QTest::addColumn<bool>("atBottom");
        QTest::newRow("reading-one-line-above-bottom") << false;
        QTest::newRow("follow-at-bottom") << true;
    }
    void scrolling()
    {
        QFETCH(bool, atBottom);
        QPlainTextEdit view; populate(view);
        QTextCursor cursor(view.document()); cursor.setPosition(0); view.setTextCursor(cursor);
        view.verticalScrollBar()->setValue(view.verticalScrollBar()->maximum() - (atBottom ? 0 : 1));
        const int scroll = view.verticalScrollBar()->value();
        appendMessageLog(&view, {{VLC_MSG_INFO, QStringLiteral("input"), QStringLiteral("New message")}}, {}, QColor("#228e53"));
        QCoreApplication::processEvents();
        QCOMPARE(view.verticalScrollBar()->value(), atBottom ? view.verticalScrollBar()->maximum() : scroll);
        QCOMPARE(view.textCursor().position(), 0);
        QVERIFY(!view.textCursor().hasSelection());
    }
    void liveFilterAppliesToEveryNewLine()
    {
        QPlainTextEdit view;
        const QString filter = QStringLiteral("FRANÇAIS");
        const QList<MessageLogEntry> entries{
            {VLC_MSG_INFO, QStringLiteral("input"), QString::fromUtf8("Not a match\nKeep français\nNot a match either")},
            {VLC_MSG_WARN, QStringLiteral("decoder"), QString::fromUtf8("français after a second prefix\nNo match")}
        };
        appendMessageLog(&view, entries, filter, QColor("#228e53"));
        QCOMPARE(view.document()->blockCount(), 5);
        const QList<bool> visible{false, true, false, true, false};
        int i = 0;
        QString exported; QTextStream output(&exported);
        for (auto block = view.document()->firstBlock(); block.isValid(); block = block.next())
        {
            QCOMPARE(block.isVisible(), visible.at(i++));
            if (block.isVisible()) output << block.text() << QLatin1Char('\n');
        }
        QCOMPARE(exported, QString::fromUtf8("Keep français\ndecoder warning: français after a second prefix\n"));
    }
    void severityAndAccent_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::newRow("dark") << true;
        QTest::newRow("light") << false;
    }
    void severityAndAccent()
    {
        QFETCH(bool, dark);
        QPlainTextEdit view;
        auto palette = view.palette();
        palette.setColor(QPalette::Base, QColor(dark ? "#161616" : "#ffffff"));
        palette.setColor(QPalette::Text, QColor(dark ? "#8f8f8f" : "#202020"));
        palette.setColor(QPalette::WindowText, QColor(dark ? "#cccccc" : "#303030"));
        view.setPalette(palette);
        const QColor accent("#228e53");
        const QList<MessageLogEntry> entries{
            {VLC_MSG_INFO, QStringLiteral("source"), QStringLiteral("Information")},
            {VLC_MSG_ERR, QStringLiteral("source"), QStringLiteral("Error")},
            {VLC_MSG_WARN, QStringLiteral("source"), QStringLiteral("Warning")},
            {VLC_MSG_DBG, QStringLiteral("source"), QStringLiteral("Debug")}
        };
        appendMessageLog(&view, entries, {}, accent);
        const QList<QColor> colors{dark ? accent : accent.darker(200), QColor(dark ? "#ff7373" : "#a31b1b"),
                                   QColor(dark ? "#d69c38" : "#775100"), palette.color(QPalette::WindowText)};
        const QStringList levels{QStringLiteral("info"), QStringLiteral("error"), QStringLiteral("warning"), QStringLiteral("debug")};
        int i = 0;
        for (auto block = view.document()->firstBlock(); block.isValid(); block = block.next(), ++i)
        {
            QVERIFY(block.text().startsWith(QStringLiteral("source ") + levels[i] + QStringLiteral(": ")));
            QTextCursor cursor(block); cursor.movePosition(QTextCursor::NextCharacter);
            QCOMPARE(cursor.charFormat().foreground().color(), dark ? accent : accent.darker(200));
            QVERIFY(cursor.charFormat().fontItalic());
            cursor.setPosition(block.position() + 8);
            QCOMPARE(cursor.charFormat().foreground().color(), colors[i]);
            cursor.movePosition(QTextCursor::EndOfBlock);
            QCOMPARE(cursor.charFormat().foreground().color(), palette.color(QPalette::Text));
            QVERIFY(!cursor.charFormat().fontItalic());
        }
    }
    void historyKeepsRecentLines()
    {
        QPlainTextEdit view;
        QList<MessageLogEntry> entries;
        for (int i = 0; i < MessageLogHistoryLines + 20; ++i)
            entries.append({VLC_MSG_INFO, {}, QStringLiteral("Row %1").arg(i)});
        appendMessageLog(&view, entries, {}, QColor("#228e53"));
        QCOMPARE(view.document()->blockCount(), MessageLogHistoryLines);
        QCOMPARE(view.document()->firstBlock().text(), QStringLiteral(" info: Row 20"));
        QCOMPARE(view.document()->lastBlock().text(), QStringLiteral(" info: Row 5019"));
        QVERIFY(!view.document()->isUndoRedoEnabled());
        QCOMPARE(view.document()->availableUndoSteps(), 0);
        QVERIFY(view.document()->characterCount() - 1 <= MessageLogHistoryCharacters);
    }
    void pruningPreservesRetainedViewportAndSelection_data()
    {
        QTest::addColumn<bool>("wrapped");
        QTest::addColumn<bool>("filtered");
        QTest::newRow("plain") << false << false;
        QTest::newRow("wrapped") << true << false;
        QTest::newRow("filtered") << false << true;
        QTest::newRow("wrapped-filtered") << true << true;
    }
    void pruningPreservesRetainedViewportAndSelection()
    {
        QFETCH(bool, wrapped); QFETCH(bool, filtered);
        QPlainTextEdit view; view.resize(300, 180); view.setReadOnly(true);
        const QString filter = filtered ? QStringLiteral("Keep") : QString();
        QStringList lines;
        for (int i = 0; i < MessageLogHistoryLines; ++i)
            lines.append(QStringLiteral("%1 row %2 %3").arg(i % 2 ? QStringLiteral("Other") : QStringLiteral("Keep"))
                .arg(i).arg(wrapped ? QString(110, QLatin1Char('w')) : QStringLiteral("short")));
        view.setPlainText(lines.join(QLatin1Char('\n')));
        for (auto block = view.document()->firstBlock(); block.isValid(); block = block.next())
            block.setVisible(messageLogMatches(block.text(), filter));
        view.document()->markContentsDirty(0, view.document()->characterCount());
        view.show(); QTest::qWait(20);
        QTextCursor selected(view.document()->findBlockByNumber(200));
        selected.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, 4);
        selected.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, 8);
        view.setTextCursor(selected); view.ensureCursorVisible();
        view.verticalScrollBar()->setValue(view.verticalScrollBar()->value() - 2);
        QCoreApplication::processEvents();
        // Wrapped lines can have fractional tops, and the view scrolls by whole
        // lines: probe inside the first line rather than on its edge, and allow
        // the old top to differ by a rounded pixel.
        const auto viewport = view.cursorForPosition(QPoint(0, 2));
        const QString firstText = viewport.block().text(), selection = view.textCursor().selectedText();
        const int firstOffset = viewport.positionInBlock(), top = view.cursorRect(viewport).top();
        const QString clipboard = QStringLiteral("Do not change the clipboard");
        QApplication::clipboard()->setText(clipboard);
        QList<MessageLogEntry> entries;
        for (int i = 0; i < 20; ++i)
            entries.append({VLC_MSG_INFO, QStringLiteral("input"), QStringLiteral("Keep newest %1").arg(i)});
        appendMessageLog(&view, entries, filter, QColor("#228e53"));
        QCoreApplication::processEvents();
        QCOMPARE(view.document()->blockCount(), MessageLogHistoryLines);
        QCOMPARE(view.textCursor().selectedText(), selection);
        const auto current = view.cursorForPosition(QPoint(0, 2));
        QCOMPARE(current.block().text(), firstText);
        QCOMPARE(current.positionInBlock(), firstOffset);
        QVERIFY2(qAbs(view.cursorRect(current).top() - top) <= 1,
                 qPrintable(QStringLiteral("%1 != %2").arg(view.cursorRect(current).top()).arg(top)));
        QCOMPARE(QApplication::clipboard()->text(), clipboard);
    }
    void discardedViewportStopsAtOldestRetainedLine()
    {
        QPlainTextEdit view; view.resize(300, 180);
        QStringList lines;
        for (int i = 0; i < MessageLogHistoryLines; ++i) lines.append(QStringLiteral("Old %1").arg(i));
        view.setPlainText(lines.join(QLatin1Char('\n'))); view.show(); QTest::qWait(10);
        view.verticalScrollBar()->setValue(0);
        QList<MessageLogEntry> entries;
        for (int i = 0; i < 50; ++i) entries.append({VLC_MSG_INFO, {}, QStringLiteral("New %1").arg(i)});
        appendMessageLog(&view, entries, {}, QColor("#228e53"));
        QCOMPARE(view.verticalScrollBar()->value(), view.verticalScrollBar()->minimum());
        QCOMPARE(view.cursorForPosition(QPoint(0, 0)).block().text(), QStringLiteral("Old 50"));
    }
    void characterLimitBoundsLongEntriesAndPreservesUnicode()
    {
        QPlainTextEdit view;
        QList<MessageLogEntry> entries;
        for (int i = 0; i < 300; ++i)
            entries.append({VLC_MSG_INFO, {}, QString(8000, QLatin1Char('x')) + QString::number(i)});
        appendMessageLog(&view, entries, {}, QColor("#228e53"));
        QVERIFY(view.document()->characterCount() - 1 <= MessageLogHistoryCharacters);
        QVERIFY(view.document()->blockCount() <= MessageLogHistoryLines);
        QVERIFY(view.toPlainText().endsWith(QStringLiteral("299")));
        view.clear();
        const QString emoji = QString::fromUtf8("😺");
        const QString large = QStringLiteral("x") + emoji + QString(MessageLogHistoryCharacters - 1, QLatin1Char('z'));
        appendMessageLog(&view, {{VLC_MSG_INFO, {}, large}}, {}, QColor("#228e53"));
        const auto retained = view.toPlainText();
        QVERIFY(retained.size() <= MessageLogHistoryCharacters);
        QVERIFY(!retained.front().isLowSurrogate());
        QVERIFY(!retained.contains(QChar::ReplacementCharacter));
        QVERIFY(retained.endsWith(QLatin1Char('z')));
    }
    void readOnlyLogsDoNotKeepRemovedTextForUndo()
    {
        QPlainTextEdit view;
        view.setReadOnly(true);
        for (int i = 0; i < 1000; ++i)
            appendMessageLog(&view, {{VLC_MSG_INFO, {}, QStringLiteral("Message %1").arg(i)}}, {}, QColor("#228e53"));
        QVERIFY(!view.document()->isUndoRedoEnabled());
        QCOMPARE(view.document()->availableUndoSteps(), 0);
        QCOMPARE(view.document()->availableRedoSteps(), 0);
    }
};

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    TestMessageLog test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_message_log.moc"
