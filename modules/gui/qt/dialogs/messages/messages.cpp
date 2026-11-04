/*****************************************************************************
 * messages.cpp : Information about an item
 ****************************************************************************
 * Copyright (C) 2006-2011 the VideoLAN team
 *
 * Authors: Jean-Baptiste Kempf <jb (at) videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <QPlainTextEdit>
#include <QTextCursor>
#include <QTextBlock>
#include <QFileDialog>
#include <QTextStream>
#include <QSaveFile>
#include <QPointer>
#include "dialogs/dialogs_provider.hpp"
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QScrollBar>
#include <QLabel>
#include <QLocale>
#include <QShowEvent>
#include <QHideEvent>
#include <QEvent>
#include <QStyleHints>
#include <QKeyEvent>
#include <QScreen>
#include <QAbstractButton>

#include <cassert>
#include <limits>

#include <vlc_common.h>
#include <vlc_playlist.h>
#include <vlc_interface.h>
#include <vlc_log_observer.h>
#include "maininterface/mainctx.hpp"
#include <vlc_input_item.h>


#include "dialogs/messages/messages.hpp"
#include "dialogs/dialogs/dialogmodel.hpp"
#include "util/interface_translator.hpp"
#include "util/message_log.hpp"
#include "util/colorizedsvgicon.hpp"
#include "util/color_scheme_model.hpp"

MessagesDialog::MessagesDialog( qt_intf_t *_p_intf)
               : QVLCFrame( _p_intf )
{
    setWindowTitle( qtr( "Messages" ) );
    setWindowRole( "vlc-messages" );
    setObjectName(QStringLiteral("redesignMessages"));
    /* Build Ui */
    ui.setupUi( this );
    ui.messages->setUndoRedoEnabled(false);
    ui.errors->setUndoRedoEnabled(false);
    ui.messages->setTabChangesFocus(true);
    ui.errors->setTabChangesFocus(true);
    auto* closeButton = new QPushButton(qtr("&Close"), this);
    closeButton->setObjectName(QStringLiteral("messagesClose"));
    closeButton->setAutoDefault(false);
    ui.saveLogButton->setAutoDefault(false);
    ui.bottomButtonsBox->addButton(closeButton, QDialogButtonBox::RejectRole);
    m_historyInfo = new QLabel(this);
    m_historyInfo->setObjectName(QStringLiteral("logHistoryInfo"));
    m_historyInfo->setWordWrap(true);
    ui.msgLayout->addWidget(m_historyInfo, 3, 0, 1, 6);
    updateHistoryInfo();
    m_logTimer.setInterval(50);
    connect(&m_logTimer, &QTimer::timeout, this, &MessagesDialog::drainMessages);

    /* Error messages */
    DialogErrorModel *errors = DialogErrorModel::getInstance<false>();
    connect( errors, &DialogErrorModel::countChanged, this, &MessagesDialog::errorsCountChanged );
    ui.mainTab->setTabText(1, qtr("Errors and warnings"));
    connect(errors, &QAbstractItemModel::rowsAboutToBeRemoved, this,
        [this](const QModelIndex &, int first, int last) {
            int offset = 0, length = 0;
            for (int row = 0; row < first; ++row) offset += m_errorRowLengths.at(row);
            for (int row = first; row <= last; ++row) length += m_errorRowLengths.at(row);
            QTextCursor cursor(ui.errors->document());
            cursor.setPosition(offset);
            cursor.setPosition(offset + length, QTextCursor::KeepAnchor);
            cursor.removeSelectedText();
            m_errorRowLengths.remove(first, last - first + 1);
        });
    for(int i=0; i<errors->count(); i++)
        addError(i);

    /* Modules tree */
    ui.modulesTree->setHeaderHidden( true );

    /* Buttons and general layout */
    ui.saveLogButton->setToolTip(qtr("Save the log lines that match the filter. (%1)").arg(QKeySequence(QKeySequence::Save).toString(QKeySequence::NativeText)));
    ui.filterEdit->setToolTip(qtr("Show lines that contain this text. Letter case does not matter. (%1)").arg(QKeySequence(QKeySequence::Find).toString(QKeySequence::NativeText)));

    int i_verbosity = var_InheritInteger( p_intf, "verbose" );
    changeVerbosity( i_verbosity );
    ui.verbosityBox->setValue( qMin( i_verbosity, 2 ) );
    ui.verbosityBox->retranslate();

    ui.filterEdit->setText( getSettings()->value( "Messages/messages-filter" ).toString() );

    updateButton = new QPushButton(this);
    updateButton->setObjectName(QStringLiteral("logActionButton"));
    updateButton->setAutoDefault(false);
    ui.actionsLayout->insertWidget(0, updateButton);

#ifndef NDEBUG
    QWidget *pldebugTab = new QWidget();
    QVBoxLayout *pldebugTabLayout = new QVBoxLayout();
    pldebugTab->setLayout( pldebugTabLayout );
    ui.mainTab->addTab( pldebugTab, QString() );
    pldebugTree = new QTreeWidget();
    pldebugTree->setObjectName(QStringLiteral("playlistDebugTree"));
    pldebugTabLayout->addWidget( pldebugTree );
    retranslatePLTree();
#endif

    tabChanged(0);
    if (p_intf->p_mi)
        connect(p_intf->p_mi, &MainCtx::intfScaleFactorChanged, this, [this] {
            updateActionIcon();
            updateMetrics();
        });

    BUTTONACT( updateButton, &MessagesDialog::updateOrClear );
    BUTTONACT( ui.saveLogButton, &MessagesDialog::save );
    connect( ui.filterEdit, &QLineEdit::editingFinished, this, &MessagesDialog::updateConfig );
    connect( ui.filterEdit, &QLineEdit::textChanged, this, &MessagesDialog::filterMessages );
    connect( ui.bottomButtonsBox, &QDialogButtonBox::rejected, this, &MessagesDialog::hide );
    connect( ui.verbosityBox, QOverload<int>::of(&QVLCDebugLevelSpinBox::valueChanged),
             this, &MessagesDialog::changeVerbosity );

    connect( ui.mainTab, &QTabWidget::currentChanged, this, &MessagesDialog::tabChanged );

    if (auto* translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, [this, closeButton] {
            ui.retranslateUi(this);
            ui.mainTab->setTabText(1, qtr("Errors and warnings"));
            ui.saveLogButton->setToolTip(qtr("Save the log lines that match the filter. (%1)").arg(QKeySequence(QKeySequence::Save).toString(QKeySequence::NativeText)));
            ui.filterEdit->setToolTip(qtr("Show lines that contain this text. Letter case does not matter. (%1)").arg(QKeySequence(QKeySequence::Find).toString(QKeySequence::NativeText)));
            closeButton->setText(qtr("&Close"));
            ui.verbosityBox->retranslate();
            updateHistoryInfo();
#ifndef NDEBUG
            retranslatePLTree();
#endif
            tabChanged(ui.mainTab->currentIndex());
            updateMetrics();
        });

    /* General action */
    const auto dp = [this](int v) { return qRound(v * (p_intf->p_mi ? p_intf->p_mi->getIntfScaleFactor() : 1)); };
    updateMetrics();
    restoreWidgetPosition("Messages", QSize(dp(760), dp(600)));
    if (screen()) resize(size().boundedTo(screen()->availableGeometry().size()));
    setTabOrder(ui.verbosityBox, ui.filterEdit);
    setTabOrder(ui.filterEdit, ui.messages);
    setTabOrder(ui.messages, updateButton);
    setTabOrder(updateButton, ui.saveLogButton);
    setTabOrder(ui.saveLogButton, closeButton);
    for (auto *child : findChildren<QWidget*>()) child->installEventFilter(this);

    /* Hook up to LibVLC messaging */
    static const struct vlc_logger_operations log_ops =
    {
        MessagesDialog::MsgCallback,
        NULL
    };
    libvlc_int_t *vlc = vlc_object_instance(p_intf);

    m_logSubscription = vlc_LogSubscribe(vlc, &log_ops, this);
    if (!m_logSubscription)
        msg_Err(p_intf, "Cannot read log messages.");

    buildTree( NULL, VLC_OBJECT(vlc) );
    updateHistoryInfo();
}

MessagesDialog::~MessagesDialog()
{
    m_logTimer.stop();
    vlc_LogUnsubscribe(m_logSubscription);
    saveWidgetPosition( "Messages" );
};

void MessagesDialog::changeVerbosity( int i_verbosity )
{
    verbosity = i_verbosity;
}

void MessagesDialog::updateConfig()
{
    getSettings()->setValue( "Messages/messages-filter", ui.filterEdit->text() );
}

void MessagesDialog::addError(int row)
{
    auto *errors = DialogErrorModel::getInstance<false>();
    auto *view = ui.errors;
    const bool autoscroll = view->verticalScrollBar()->value() + view->verticalScrollBar()->pageStep()
        >= view->verticalScrollBar()->maximum();
    const int before = view->document()->characterCount();
    QTextCursor cursor(view->document());
    cursor.movePosition(QTextCursor::End);
    QTextCharFormat format;
    format.setFontItalic(true);
    const bool dark = view->palette().color(QPalette::Base).lightness() < 128;
    const bool warning = errors->data(errors->index(row), DialogErrorModel::DIALOG_WARNING).toBool();
    format.setForeground(warning ? QColor(dark ? "#d69c38" : "#775100")
                                : QColor(dark ? "#ff7373" : "#a31b1b"));
    cursor.insertText(errors->data(errors->index(row), DialogErrorModel::DIALOG_TITLE).toString() + ": ", format);
    format.setFontItalic(false);
    format.setForeground(view->palette().color(QPalette::Text));
    cursor.insertText(errors->data(errors->index(row), DialogErrorModel::DIALOG_TEXT).toString(), format);
    cursor.insertBlock();
    m_errorRowLengths.append(view->document()->characterCount() - before);
    if (autoscroll) view->verticalScrollBar()->setValue(view->verticalScrollBar()->maximum());
}

void MessagesDialog::errorsCountChanged(){
    DialogErrorModel *errors = DialogErrorModel::getInstance(p_intf);
    addError(errors->count() - 1);
}

void MessagesDialog::filterMessages()
{
    QPlainTextEdit *messages = ui.messages;
    QTextBlock block = messages->document()->firstBlock();

    while( block.isValid() )
    {
        block.setVisible( matchFilter( block.text() ) );
        block = block.next();
    }

    /* Consider the whole QTextDocument as dirty now */
    messages->document()->markContentsDirty( 0, messages->document()->characterCount() );

    /* FIXME This solves a bug (Qt?) with the viewport not resizing the
       vertical scroll bar when one or more QTextBlock are hidden */
    QSize vsize = messages->viewport()->size();
    messages->viewport()->resize( vsize + QSize( 1, 1 ) );
    messages->viewport()->resize( vsize );
    updateHistoryInfo();
}

bool MessagesDialog::matchFilter( const QString& text )
{
    return messageLogMatches(text, ui.filterEdit->text());
}

void MessagesDialog::drainMessages()
{
    auto batch = m_pendingMessages.take();
    const QColor accent = p_intf->p_mi ? p_intf->p_mi->redesignAccent() : palette().color(QPalette::Link);
    appendMessageLog(ui.messages, batch.entries, ui.filterEdit->text(), accent);
    if (batch.skipped)
    {
        const auto maximum = std::numeric_limits<quint64>::max();
        m_skippedMessages = maximum - m_skippedMessages < batch.skipped
            ? maximum : m_skippedMessages + batch.skipped;
    }
    if (!batch.entries.isEmpty() || batch.skipped) updateHistoryInfo();
}

void MessagesDialog::updateHistoryInfo()
{
    const auto *document = ui.messages->document();
    const int total = document->isEmpty() ? 0 : document->blockCount();
    int shown = total;
    if (!ui.filterEdit->text().isEmpty() && total)
    {
        shown = 0;
        for (auto block = document->firstBlock(); block.isValid(); block = block.next())
            if (block.isVisible()) ++shown;
    }
    auto text = ui.filterEdit->text().isEmpty()
        ? qtr("Log lines: %1").arg(QLocale().toString(total))
        : qtr("Log lines shown: %1 of %2").arg(QLocale().toString(shown), QLocale().toString(total));
    if (!ui.filterEdit->text().isEmpty() && !shown)
        text += QLatin1Char('\n') + qtr("No log lines match this filter.");
    text += QLatin1Char('\n') + qtr("This window shows up to %1 log lines.").arg(QLocale().toString(MessageLogHistoryLines));
    if (m_skippedMessages)
        text += QLatin1Char('\n') + qtr("Log messages not shown: %1").arg(QLocale().toString(m_skippedMessages));
    m_historyInfo->setText(text);
    const auto help = qtr("This window can show only part of a long log message. To keep a full log, turn on Save a log in Settings. After this change, close Hakone. Start Hakone again.");
    m_historyInfo->setToolTip(QStringLiteral("<qt>%1</qt>").arg(help.toHtmlEscaped()));
    m_historyInfo->setAccessibleDescription(help);
    ui.saveLogButton->setEnabled(shown > 0);
}

void MessagesDialog::showEvent(QShowEvent* event)
{
    QVLCFrame::showEvent(event);
    updateMetrics();
    layout()->activate();
    if (screen()) resize(size().boundedTo(screen()->availableGeometry().size()));
    tabChanged(ui.mainTab->currentIndex());
}

void MessagesDialog::updateMetrics()
{
    if (!m_historyInfo) return;
    const auto dp = [this](int v) { return qRound(v * (p_intf->p_mi ? p_intf->p_mi->getIntfScaleFactor() : 1.)); };
    const bool compact = height() < dp(520);
    const int margin = dp(compact ? 16 : 24);
    layout()->setContentsMargins(margin, margin, margin, margin);
    layout()->setSpacing(dp(compact ? 8 : 12));
    ui.messagesHelp->setVisible(!compact);
    ui.messagesHeading->setToolTip(ui.messagesHelp->text());
    ui.messagesHeading->setAccessibleDescription(ui.messagesHelp->text());
}

void MessagesDialog::resizeEvent(QResizeEvent *event)
{
    QVLCFrame::resizeEvent(event);
    updateMetrics();
}

void MessagesDialog::hideEvent(QHideEvent* event)
{
    m_logTimer.stop();
    updateConfig();
    QVLCFrame::hideEvent(event);
}

void MessagesDialog::changeEvent(QEvent* event)
{
    QVLCFrame::changeEvent(event);
    if (event->type() == QEvent::PaletteChange) updateActionIcon();
}

void MessagesDialog::updateActionIcon()
{
    if (!updateButton) return;
    const auto resource = ui.mainTab->currentIndex() > 1
        ? QStringLiteral(":/menu/redesign/refresh.svg") : QStringLiteral(":/menu/redesign/remove.svg");
    QColor foreground = palette().color(QPalette::WindowText);
    if (const auto ctx = p_intf->p_mi)
    {
        const auto scheme = ctx->getColorScheme()->currentScheme();
        const bool light = scheme == ColorSchemeModel::Day ||
            (scheme == ColorSchemeModel::System && qApp->styleHints()->colorScheme() == Qt::ColorScheme::Light);
        foreground = QColor(light ? "#404040" : "#8f8f8f");
    }
    updateButton->setIcon(ColorizedSvgIcon(resource, {}, {}, {},
        {{QStringLiteral("#8f8f8f"), foreground.name()}}));
    const int size = qRound(20 * (p_intf->p_mi ? p_intf->p_mi->getIntfScaleFactor() : 1));
    updateButton->setIconSize(QSize(size, size));
}

bool MessagesDialog::save()
{
    drainMessages();
    QPointer<MessagesDialog> owner(this);
    QString selected = QVLCUserDir(VLC_DOCUMENTS_DIR);
    while (owner)
    {
        selected = QFileDialog::getSaveFileName(this, qtr("Save log"), selected,
            qtr("Texts/Logs (*.log *.txt);; All (*.*)"));
        if (!owner || selected.isEmpty()) return false;
        drainMessages();
        QString error;
        {
            // Never truncate the previous log or use a non-atomic fallback.
            QSaveFile file(selected);
            file.setDirectWriteFallback(false);
            if (file.open(QFile::WriteOnly | QFile::Text))
            {
                bool written;
                {
                    QTextStream out(&file);
                    for (auto block = ui.messages->document()->firstBlock(); block.isValid(); block = block.next())
                        if (block.isVisible()) out << block.text() << "\n";
                    out.flush();
                    written = out.status() == QTextStream::Ok;
                }
                if (written && file.commit()) return true;
                error = file.errorString();
                file.cancelWriting();
            }
            else error = file.errorString();
        }
        const auto answer = DialogsProvider::getInstance()->messageDialog(
            qtr("Hakone cannot save the log.\n\nFile: %1\n\nError: %2\n\nThe log remains in this window.")
                .arg(selected, error), qtr("Save log"), QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel, QMessageBox::Warning, this, qtr("Choose file"));
        if (!owner || answer != QMessageBox::Yes) return false;
    }
    return false;
}

void MessagesDialog::buildTree( QTreeWidgetItem *parentItem,
                                vlc_object_t *p_obj )
{
    QTreeWidgetItem *item;

    if( parentItem )
        item = new QTreeWidgetItem( parentItem );
    else
        item = new QTreeWidgetItem( ui.modulesTree );

    char *name = vlc_object_get_name( p_obj );
    item->setText( 0, QString("%1%2 (0x%3)")
                   .arg( qfu( vlc_object_typename(p_obj) ) )
                   .arg( ( name != NULL )
                         ? QString( " \"%1\"" ).arg( qfu( name ) )
                             : "" )
                   .arg( (uintptr_t)p_obj, 0, 16 )
                 );
    free( name );
    item->setExpanded( true );

    size_t count = 0, size;
    vlc_object_t **tab = NULL;

    do
    {
        delete[] tab;
        size = count;
        tab = new vlc_object_t *[size];
        count = vlc_list_children(p_obj, tab, size);
    }
    while (size < count);

    for (size_t i = 0; i < count ; i++)
    {
        buildTree( item, tab[i] );
        vlc_object_release(tab[i]);
    }

    delete[] tab;
}

void MessagesDialog::updateOrClear()
{
    if( ui.mainTab->currentIndex() == 2)
    {
        ui.modulesTree->clear();
        buildTree( NULL, VLC_OBJECT( vlc_object_instance(p_intf) ) );
    }
    else if( ui.mainTab->currentIndex() == 0 )
    {
        m_pendingMessages.clear();
        m_skippedMessages = 0;
        ui.messages->clear();
        updateHistoryInfo();
    }
    else if( ui.mainTab->currentIndex() == 1 )
    {
        ui.errors->clear();
        m_errorRowLengths.fill(0);
    }
#ifndef NDEBUG
    else
        updatePLTree();
#endif
}

void MessagesDialog::tabChanged( int i )
{
    if (i == 0 && isVisible())
    {
        drainMessages();
        m_logTimer.start();
    }
    else m_logTimer.stop();
    updateActionIcon();
    ui.saveLogButton->setVisible(i == 0);
    updateButton->setText(i > 1 ? qtr("&Refresh")
                               : i == 0 ? qtr("&Clear log") : qtr("&Clear errors"));
    updateButton->setToolTip(i > 1 ? qtr("Read the tree again.")
                                  : i == 0 ? qtr("Remove the log lines from this window.")
                                           : qtr("Remove errors and warnings from this window."));
}

bool MessagesDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)
    {
        auto *key = static_cast<QKeyEvent*>(event);
        const bool find = key->matches(QKeySequence::Find);
        const bool saveLog = key->matches(QKeySequence::Save);
        const bool resetFilter = watched == ui.filterEdit && key->key() == Qt::Key_Escape
            && !ui.filterEdit->text().isEmpty();
        if (find || saveLog || resetFilter)
        {
            if (event->type() == QEvent::KeyPress)
            {
                if (find)
                {
                    ui.mainTab->setCurrentIndex(0);
                    ui.filterEdit->setFocus();
                    ui.filterEdit->selectAll();
                }
                else if (saveLog && ui.mainTab->currentIndex() == 0 && ui.saveLogButton->isEnabled()) save();
                else if (resetFilter) ui.filterEdit->clear();
            }
            key->accept();
            return true;
        }
    }
    return QVLCFrame::eventFilter(watched, event);
}

void MessagesDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
    {
        if (focusWidget() == ui.filterEdit) ui.messages->setFocus();
        else if (auto *button = qobject_cast<QAbstractButton*>(focusWidget())) button->click();
        event->accept();
        return;
    }
    QVLCFrame::keyPressEvent(event);
}

void MessagesDialog::showTab( int i )
{
    ui.mainTab->setCurrentIndex(i);
    show();
    raise();
}

void MessagesDialog::MsgCallback( void *self, int type, const vlc_log_t *item,
                                  const char *format, va_list ap )
{
    MessagesDialog *dialog = (MessagesDialog *)self;
    int verbosity = dialog->verbosity.load();

    if( verbosity < 0 || verbosity < (type - VLC_MSG_ERR) )
        return;

    int canc = vlc_savecancel();
    dialog->m_pendingMessages.append(type, item->psz_module, format, ap);
    vlc_restorecancel( canc );
}

#ifndef NDEBUG

void MessagesDialog::retranslatePLTree()
{
    ui.mainTab->setTabText(ui.mainTab->indexOf(pldebugTree->parentWidget()), qtr("Playlist tree"));
    pldebugTree->setHeaderLabels({qtr("Name"), qtr("Playlist ID"), qtr("Item ID"),
                                qtr("Playlist flags"), qtr("Item flags")});
}

void MessagesDialog::updatePLTree()
{
    pldebugTree->clear();
    {
        vlc_playlist_t* playlist = p_intf->p_playlist;
        vlc_playlist_Lock(playlist);
        size_t count = vlc_playlist_Count( playlist );
        for (size_t i = 0; i < count; i++)
        {
            QTreeWidgetItem *current = new QTreeWidgetItem();
            vlc_playlist_item_t* item = vlc_playlist_Get( playlist, i );
            input_item_t* media = vlc_playlist_item_GetMedia( item );
            current->setText( 0, qfu( media->psz_name ) );
            current->setToolTip( 0, qfu( media->psz_uri ) );
            current->setText( 1, QString("%1").arg( i ) );
            current->setText( 2, QString("%1").arg( (uintptr_t)media ) );
            //current->setText( 3, QString("0x%1").arg( p_node->i_flags, 0, 16 ) );
            current->setText( 3, QString("0x%1").arg( media->i_type, 0, 16 ) );
            pldebugTree->addTopLevelItem( current );
        }
        vlc_playlist_Unlock(playlist);
    }

    pldebugTree->expandAll();
    for ( int i=0; i< 4; i++ )
        pldebugTree->resizeColumnToContents( i );
}
#endif
