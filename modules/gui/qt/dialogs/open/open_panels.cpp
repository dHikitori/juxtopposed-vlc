/*****************************************************************************
 * open_panels.cpp : Panels for the open dialogs
 ****************************************************************************
 * Copyright (C) 2006-2009 the VideoLAN team
 * Copyright (C) 2007 Société des arts technologiques
 * Copyright (C) 2007 Savoir-faire Linux
 *
 *
 * Authors: Clément Stenac <zorglub@videolan.org>
 *          Jean-Baptiste Kempf <jb@videolan.org>
 *          Pierre-Luc Beaudoin <pierre-luc.beaudoin@savoirfairelinux.com>
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

#include <cassert>
#include <cmath>

#include "qt.hpp"
#include "maininterface/mainctx.hpp"
#include "open_panels.hpp"
#include "dialogs/open/open.hpp"
#include "dialogs/dialogs_provider.hpp" /* Open Subtitle file */
#include "util/qt_dirs.hpp"
#include "util/validators.hpp"
#include "util/interface_translator.hpp"
#include "util/colorizedsvgicon.hpp"
#include "util/redesign_widget_theme.hpp"
#include "util/scroll_focus_tracker.hpp"
#include "util/color_scheme_model.hpp"
#include "widgets/native/embedded_file_browser.hpp"
#include <vlc_intf_strings.h>
#include <vlc_modules.h>
#include <vlc_plugin.h>
#include <vlc_url.h>

#include <QFileDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QStackedLayout>
#include <QFormLayout>
#include <QCompleter>
#include <QScrollArea>
#include <QUrl>
#include <QMimeData>
#include <QDropEvent>
#include <QRegularExpression>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QShortcut>
#include <QScreen>
#include <QKeyEvent>
#include <QApplication>
#include <QPointer>
#include <QResizeEvent>
#include <QStyleHints>

#define I_DEVICE_TOOLTIP \
    I_DIR_OR_FOLDER( N_("Select a device or a VIDEO_TS directory"), \
                     N_("Select a device or a VIDEO_TS folder") )

#include "../../../eject.c"

/* Populate a combobox with the devices matching a pattern.
   Combobox will automatically do autocompletion on the edit zone */
#define POPULATE_WITH_DEVS(ppsz_devlist, targetCombo) \
    QStringList targetCombo ## StringList = QStringList(); \
    for ( size_t i = 0; i< ARRAY_SIZE(ppsz_devlist); i++ ) \
        targetCombo ## StringList << QString( ppsz_devlist[ i ] ); \
    targetCombo->addItems( QDir( "/dev/" )\
        .entryList( targetCombo ## StringList, QDir::System )\
        .replaceInStrings( QRegularExpression(QStringLiteral("^")), "/dev/" ) \
    );

static const char psz_devModule[][8] = { "v4l2", "pvr", "dtv",
                                       "dshow", "screen", "jack" };

namespace {
class CaptureOptionsDialog final : public QDialog
{
public:
    using QDialog::QDialog;
    void setContext(MainCtx *context)
    {
        ctx = context;
        scroll = findChild<QScrollArea*>();
        metrics.setSingleShot(true);
        connect(&metrics, &QTimer::timeout, this, [this] { updateMetrics(); });
        if (ctx) connect(ctx, &MainCtx::intfScaleFactorChanged, this, [this] { scheduleMetrics(); });
        if (scroll) scroll->viewport()->installEventFilter(this);
        if (scroll && scroll->widget())
            for (auto *label : scroll->widget()->findChildren<QLabel*>()) label->setWordWrap(true);
        updateMetrics();
    }
    void setSearchField(QLineEdit *field)
    {
        search = field;
        for (auto *widget : findChildren<QWidget*>()) widget->installEventFilter(this);
        installEventFilter(this);
    }
protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Resize && scroll && watched == scroll->viewport()) scheduleMetrics();
        if (search && (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress))
        {
            auto *key = static_cast<QKeyEvent*>(event);
            const bool find = key->matches(QKeySequence::Find);
            const bool clear = watched == search && key->key() == Qt::Key_Escape && !search->text().isEmpty();
            if (find || clear)
            {
                if (event->type() == QEvent::KeyPress)
                {
                    if (find) { search->setFocus(); search->selectAll(); }
                    else search->clear();
                }
                key->accept();
                return true;
            }
        }
        return QDialog::eventFilter(watched, event);
    }
    void resizeEvent(QResizeEvent *event) override
    {
        QDialog::resizeEvent(event);
        scheduleMetrics();
    }
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        {
            if (auto *button = qobject_cast<QPushButton*>(QApplication::focusWidget()))
                if (isAncestorOf(button)) button->click();
            event->accept();
            return;
        }
        QDialog::keyPressEvent(event);
    }
private:
    QLineEdit *search = nullptr;
    QScrollArea *scroll = nullptr;
    QPointer<MainCtx> ctx;
    QTimer metrics;

    void scheduleMetrics()
    {
        if (ctx && !metrics.isActive()) metrics.start(0);
    }
    void updateMetrics()
    {
        if (!ctx) return;
        const auto dp = [this](int n) { return qRound(n * ctx->getIntfScaleFactor()); };
        const bool compact = height() < dp(540);
        const int margin = dp(compact ? 12 : 24);
        layout()->setContentsMargins(margin, margin, margin, margin);
        layout()->setSpacing(dp(compact ? 4 : 12));
        if (!scroll || !scroll->widget()) return;
        auto *form = scroll->widget();
        form->layout()->setContentsMargins(dp(6), dp(6), dp(6), dp(6));
        form->layout()->setSpacing(dp(8));
        for (auto *spin : form->findChildren<QAbstractSpinBox*>()) {
            spin->ensurePolished();
            spin->setFixedWidth(qMax(dp(110), spin->sizeHint().width()));
        }
        for (auto *choice : form->findChildren<QComboBox*>()) {
            choice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            choice->setMinimumContentsLength(8);
            choice->setMinimumWidth(dp(120));
            choice->setMaximumWidth(dp(260));
        }
    }
};

bool hasScreenCapture()
{
    module_t **modules = nullptr;
    size_t strictMatches;
    const auto count = vlc_module_match("access", "screen", true, &modules, &strictMatches);
    free(modules);
    return count > 0;
}

template<typename Widget>
Widget* captureText(const char* source, QWidget* parent)
{
    auto* widget = new Widget(qtr(source), parent);
    const QByteArray text(source);
    if (auto* translator = InterfaceTranslator::instance())
        QObject::connect(translator, &InterfaceTranslator::languageChanged, widget,
                         [widget, text] { widget->setText(qtr(text.constData())); });
    return widget;
}

void appendFileItem(QListWidget *list, const QUrl &url)
{
    const auto text = toNativeSeparators(url.toDisplayString(
        QUrl::RemovePassword | QUrl::PreferLocalFile | QUrl::NormalizePathSegments));
    auto *item = new QListWidgetItem(text, list);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    item->setToolTip(QStringLiteral("<qt>%1</qt>").arg(text.toHtmlEscaped()));
}

void watchCaptureMode(QComboBox* combo, int row, const char* source)
{
    const QByteArray text(source);
    if (auto* translator = InterfaceTranslator::instance())
        QObject::connect(translator, &InterfaceTranslator::languageChanged, combo, [combo, row, text] {
            const QSignalBlocker blocker(combo);
            combo->setItemText(row, qtr(text.constData()));
        });
}
}

/**************************************************************************
 * Open Files and subtitles                                               *
 **************************************************************************/
FileOpenPanel::FileOpenPanel( QWidget *_parent, qt_intf_t *_p_intf ) :
                                OpenPanel( _parent, _p_intf ), dialogBox( NULL )
{
    /* Classic UI Setup */
    ui.setupUi( this );
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    ui.label->setWordWrap(true);
    ui.fileListWidg->setTextElideMode(Qt::ElideMiddle);
    connect(ui.fileListWidg, &QListWidget::itemSelectionChanged, this, &FileOpenPanel::updateButtons);
    if (auto* translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, [this] {
            ui.retranslateUi(this);
            if (dialogBox) {
                dialogBox->setLabelText(QFileDialog::FileName, qtr("File names:"));
                dialogBox->setLabelText(QFileDialog::FileType, qtr("File type:"));
            }
        });

    setAcceptDrops( true );

    /* Subtitles */
    /* Deactivate the subtitles control by default. */
    ui.subGroupBox->setEnabled( false );

    ui.fileBrowseButton->setIcon( ColorizedSvgIcon::colorizedIconForWidget( ":/menu/add.svg", ui.fileBrowseButton ) );

    /* Connects  */
    BUTTONACT( ui.fileBrowseButton, &FileOpenPanel::browseFile );
    BUTTONACT( ui.removeFileButton, &FileOpenPanel::removeFile );

    BUTTONACT( ui.subBrowseButton, &FileOpenPanel::browseFileSub );
    connect( ui.subGroupBox, &QGroupBox::toggled, this, &FileOpenPanel::updateMRL );

    connect( ui.fileListWidg, &QListWidget::itemChanged, this, &FileOpenPanel::updateMRL );
    connect( ui.subInput, &QLineEdit::textChanged, this, &FileOpenPanel::updateMRL );
    onFocus();
    updateButtons();
}

void FileOpenPanel::createEmbeddedBrowser()
{
    dialogBox = new EmbeddedFileBrowser(this);
    dialogBox->setDirectoryUrl(p_intf->p_mi->getDialogFilePath());
    dialogBox->restoreState(getSettings()->value("file-dialog-state").toByteArray());
    dialogBox->setLabelText(QFileDialog::FileName, qtr("File names:"));
    dialogBox->setLabelText(QFileDialog::FileType, qtr("File type:"));
    connect(dialogBox, &EmbeddedFileBrowser::selectionChanged, this, &FileOpenPanel::updateMRL);
    connect(dialogBox, &EmbeddedFileBrowser::filesActivated, this, &OpenPanel::acceptRequested);
    connect(dialogBox, &EmbeddedFileBrowser::cancelRequested, this, &OpenPanel::cancelRequested);
    ui.gridLayout->addWidget(dialogBox, 0, 0, 1, 3);
}

void FileOpenPanel::onFocus()
{
    const bool enabled = var_InheritBool(p_intf, "qt-embedded-open");
    if (enabled == embedded) return;
    clear();
    if (enabled && !dialogBox) createEmbeddedBrowser();
    embedded = enabled;
    ui.tempWidget->setVisible(!embedded);
    if (dialogBox) dialogBox->setVisible(embedded);
    updateMRL();
}

FileOpenPanel::~FileOpenPanel()
{
    if( dialogBox )
        getSettings()->setValue( "file-dialog-state", dialogBox->saveState() );
}

void FileOpenPanel::dragEnterEvent( QDragEnterEvent *event )
{
    event->acceptProposedAction();
}

void FileOpenPanel::dragMoveEvent( QDragMoveEvent *event )
{
    event->acceptProposedAction();
}

void FileOpenPanel::dragLeaveEvent( QDragLeaveEvent *event )
{
    event->accept();
}

void FileOpenPanel::dropEvent( QDropEvent *event )
{
    if( event->possibleActions() & Qt::CopyAction )
       event->setDropAction( Qt::CopyAction );
    else
        return;

    const QMimeData *mimeData = event->mimeData();
    if (embedded) {
        auto urls = dialogBox->selectedMediaUrls();
        for (const auto& url : mimeData->urls()) if (url.isValid()) urls.append(url);
        dialogBox->selectUrls(urls);
        event->accept();
        return;
    }
    foreach( const QUrl &url, mimeData->urls() )
    {
        if( url.isValid() )
        {
            urlList << url;
            appendFileItem(ui.fileListWidg, url);
        }
    }
    updateMRL();
    updateButtons();
    event->accept();
}

void FileOpenPanel::browseFile()
{
    QStringList files = THEDP->showSimpleOpen( qtr( "Select one or multiple files" ) );
    foreach( const QString &file, files )
    {
        QUrl url(file);
        urlList << url;
        appendFileItem(ui.fileListWidg, url);
        p_intf->p_mi->setDialogFilePath(url);
    }
    updateButtons();
    updateMRL();
}

void FileOpenPanel::removeFile()
{
    const auto selected = ui.fileListWidg->selectedItems();
    const int i = selected.isEmpty() ? -1 : ui.fileListWidg->row(selected.first());
    if( i != -1 )
    {
        QListWidgetItem *temp = ui.fileListWidg->takeItem( i );
        delete temp;
        urlList.removeAt( i );
    }

    updateMRL();
    updateButtons();
}

/* Show a fileBrowser to select a subtitle */
void FileOpenPanel::browseFileSub()
{
    QStringList urls = THEDP->showSimpleOpen( qtr("Open subtitle file"),
                           EXT_FILTER_SUBTITLE, p_intf->p_mi->getDialogFilePath() );

    if( urls.isEmpty() ) {
        return;
    }

    subUrl = QUrl(urls[0]);
    ui.subInput->setText( subUrl.toDisplayString(QUrl::RemovePassword | QUrl::PreferLocalFile | QUrl::NormalizePathSegments) );
    updateMRL();
}

/* Update the current MRL */
void FileOpenPanel::updateMRL()
{
    QStringList fileList;
    QString mrl;

    /* File Listing */
    if( !embedded )
        foreach( const QUrl& url, urlList )
            fileList << url.toEncoded();
    else
    {
        QList<QUrl> urls = dialogBox->selectedMediaUrls();
        foreach( const QUrl &url, urls )
            fileList.append( url.toEncoded() );
    }

    /* Options */
    if( ui.subGroupBox->isChecked() &&  !subUrl.isEmpty() ) {
        mrl.append( " :sub-file=" + colon_escape( toNativeSeparators( subUrl.toLocalFile() ) ) );
    }

    updateButtons();
    emit methodChanged( "file-caching" );
    emit mrlUpdated( fileList, mrl );
}

bool FileOpenPanel::validateSelection()
{
    return !embedded || dialogBox->validateSelection();
}

QString FileOpenPanel::validationFeedback() const
{
    return embedded && dialogBox->hasSelectionError()
        ? qtr("Check the file names and locations. Then try again.") : QString{};
}

/* Function called by Open Dialog when clicked on Play/Enqueue */
void FileOpenPanel::onAccept()
{
    if (embedded) p_intf->p_mi->setDialogFilePath(dialogBox->directoryUrl());
}

/* Function called by Open Dialog when clicked on cancel */
void FileOpenPanel::clear()
{
    ui.fileListWidg->clear();
    ui.subInput->clear();
    urlList.clear();
    subUrl = QUrl();
    ui.subGroupBox->setChecked(false);
    if (dialogBox) dialogBox->clearSelection();
    updateMRL();
}

/* Update buttons depending on current selection */
void FileOpenPanel::updateButtons()
{
    const bool b_has_files = embedded ? !dialogBox->selectedMediaUrls().isEmpty() : !urlList.empty();
    ui.removeFileButton->setEnabled(b_has_files && !ui.fileListWidg->selectedItems().isEmpty());
    ui.subGroupBox->setEnabled( b_has_files );
}

/**************************************************************************
 * Open Discs ( DVD, CD, VCD and similar devices )                        *
 **************************************************************************/
DiscOpenPanel::DiscOpenPanel( QWidget *_parent, qt_intf_t *_p_intf ) :
                                OpenPanel( _parent, _p_intf )
{
    ui.setupUi( this );
    if (auto* translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, [this] {
            ui.retranslateUi(this);
            ui.browseDiscButton->setToolTip(qfut(I_DEVICE_TOOLTIP));
            ui.deviceCombo->setToolTip(qfut(I_DEVICE_TOOLTIP));
            ui.titleLabel->setText(m_discType == Vcd ? qtr("Entry") : m_discType == Cdda ? qtr("Track") : qtr("Title"));
            updateEditorText();
        });

    /* Get the default configuration path for the devices */
    psz_dvddiscpath = var_InheritString( p_intf, "dvd" );
    psz_vcddiscpath = var_InheritString( p_intf, "vcd" );
    psz_cddadiscpath = config_GetType("cd-audio") ?
                       var_InheritString( p_intf, "cd-audio" ) : NULL;

    /* State to avoid overwriting the users changes with the configuration */
    m_discType = None;

    ui.browseDiscButton->setToolTip( qfut( I_DEVICE_TOOLTIP ));
    ui.deviceCombo->setToolTip( qfut( I_DEVICE_TOOLTIP ) );
    ui.deviceCombo->setInsertPolicy( QComboBox::InsertAtTop );
    ui.deviceCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    ui.deviceCombo->setMinimumContentsLength(8);
    if (p_intf->p_mi)
    {
        connect(p_intf->p_mi, &MainCtx::intfScaleFactorChanged, this, &DiscOpenPanel::updateLayoutMetrics);
        connect(p_intf->p_mi->getColorScheme(), &ColorSchemeModel::currentChanged, this, &DiscOpenPanel::updateLayoutMetrics);
    }
    connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, &DiscOpenPanel::updateLayoutMetrics);

#if !defined( _WIN32 ) && !defined( __OS2__ )
    char const * const ppsz_discdevices[] = {
        "sr*",
        "sg*",
        "scd*",
        "dvd*",
        "cd*"
    };
    QComboBox *discCombo = ui.deviceCombo; /* avoid namespacing in macro */
    POPULATE_WITH_DEVS( ppsz_discdevices, discCombo );
    char *psz_config = config_GetPsz( "dvd" );
    int temp = ui.deviceCombo->findData( { const_cast<const char *>( psz_config ) }, Qt::UserRole, Qt::MatchStartsWith );
    free( psz_config );
    if( temp != -1 )
        ui.deviceCombo->setCurrentIndex( temp );
#endif

    ui.ejectButton->setIcon( ColorizedSvgIcon::colorizedIconForWidget( ":/menu/ic_fluent_arrow_eject.svg", ui.ejectButton ) );

    /* CONNECTs */
    BUTTONACT( ui.dvdRadioButton,     &DiscOpenPanel::updateButtons );
    BUTTONACT( ui.dvdaRadioButton,    &DiscOpenPanel::updateButtons );
    BUTTONACT( ui.bdRadioButton,      &DiscOpenPanel::updateButtons );
    BUTTONACT( ui.vcdRadioButton,     &DiscOpenPanel::updateButtons );
    BUTTONACT( ui.audioCDRadioButton, &DiscOpenPanel::updateButtons );
    BUTTONACT( ui.dvdsimple,          &DiscOpenPanel::updateButtons );
    BUTTONACT( ui.browseDiscButton,   &DiscOpenPanel::browseDevice );
    BUTTONACT( ui.ejectButton,  &DiscOpenPanel::eject );

    connect( ui.deviceCombo, &QComboBox::editTextChanged, this, &DiscOpenPanel::updateMRL );
    connect( ui.deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
             this, &DiscOpenPanel::updateMRL );
    connect( ui.titleSpin, QOverload<int>::of(&QSpinBox::valueChanged),
             this, &DiscOpenPanel::updateMRL );
    connect( ui.chapterSpin, QOverload<int>::of(&QSpinBox::valueChanged),
             this, &DiscOpenPanel::updateMRL );
    connect( ui.audioSpin, QOverload<int>::of(&QSpinBox::valueChanged),
             this, &DiscOpenPanel::updateMRL );
    connect( ui.subtitlesSpin, QOverload<int>::of(&QSpinBox::valueChanged),
             this, &DiscOpenPanel::updateMRL );
    connect( ui.dvdsimple, &QCheckBox::toggled, this, &DiscOpenPanel::updateMRL );

    connect(ui.titleSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &DiscOpenPanel::updateEditorText);
    /* Run once the updateButtons function in order to fill correctly the comboBoxes */
    updateButtons();
}

#ifdef _WIN32 /* Disc drives probing for Windows */
void DiscOpenPanel::onFocus()
{
    const auto previousText = ui.deviceCombo->currentText();
    const int previousIndex = ui.deviceCombo->findText(previousText);
    const auto previousData = previousIndex >= 0 ? ui.deviceCombo->itemData(previousIndex) : QVariant();
    const auto previousPath = previousData.isValid() ? previousData.toString() : previousText;
    const QSignalBlocker choicesBlock(ui.deviceCombo), editBlock(ui.deviceCombo->lineEdit());
    ui.deviceCombo->clear();
    wchar_t szDrives[512];
    szDrives[0] = L'\0';
    if( GetLogicalDriveStringsW( ARRAY_SIZE(szDrives) - 1, szDrives ) )
    {
        wchar_t *drive = szDrives;
        DWORD oldMode;
        SetThreadErrorMode( SEM_FAILCRITICALERRORS, &oldMode );
        while( *drive )
        {
            if( GetDriveTypeW(drive) == DRIVE_CDROM )
            {
                wchar_t psz_name[512] = L"";
                GetVolumeInformationW( drive, psz_name, 511, NULL, NULL, NULL, NULL, 0 );

                QString psz_drive = QString::fromWCharArray( drive );
                QString displayName = psz_drive;
                QString psz_title = QString::fromWCharArray( psz_name );
                if( !psz_title.isEmpty()) {
                    displayName = displayName + " - "  + psz_title;
                }

                ui.deviceCombo->addItem( displayName, psz_drive );
            }

            /* go to next drive */
            while( *(drive++) );
        }
        SetThreadErrorMode(oldMode, NULL);
    }

    if (m_deviceListInitialized)
    {
        const int index = ui.deviceCombo->findData(previousPath);
        if (index >= 0) ui.deviceCombo->setCurrentIndex(index);
        // Keep an entered path, an empty draft, or a drive that is now absent.
        else ui.deviceCombo->setEditText(previousPath);
    }
    else
    {
        char *psz_config = config_GetPsz("dvd");
        const int index = ui.deviceCombo->findData(qfu(psz_config), Qt::UserRole, Qt::MatchStartsWith);
        free(psz_config);
        if (index >= 0) ui.deviceCombo->setCurrentIndex(index);
        m_deviceListInitialized = true;
    }
}
#elif defined( __OS2__ ) /* Disc drives probing for OS/2 */
void DiscOpenPanel::onFocus()
{

    ui.deviceCombo->clear();

#define IOCTL_CDROMDISK2        0x82
#define CDROMDISK2_DRIVELETTERS 0x60

    HFILE hcd2;
    ULONG ulAction;
    ULONG ulParamLen;
    ULONG ulData;
    ULONG ulDataLen;

    if( DosOpen(( PSZ )"CD-ROM2$", ( PHFILE )&hcd2, &ulAction, 0, FILE_NORMAL,
                OPEN_ACTION_OPEN_IF_EXISTS | OPEN_ACTION_FAIL_IF_NEW,
                OPEN_ACCESS_READONLY | OPEN_SHARE_DENYNONE,
                NULL ))
        return;

    if( !DosDevIOCtl( hcd2, IOCTL_CDROMDISK2, CDROMDISK2_DRIVELETTERS,
                      NULL, 0, &ulParamLen, &ulData, sizeof( ulData ), &ulDataLen ))
    {
        char szDriveName[] = "X:\\";

        int count = LOUSHORT( ulData );
        int drive = HIUSHORT( ulData );

        for( ; count; --count, ++drive )
        {
            szDriveName[ 0 ] = 'A' + drive;

            QString name = qfu( szDriveName );

            ui.deviceCombo->addItem( name, name );
        }
    }

    DosClose( hcd2 );
}
#endif

void DiscOpenPanel::updateContext(int c)
{
    if (context == c) return;
    OpenPanel::updateContext(c);
    ui.dvdsimple->setChecked(defaultSkipDiscMenus());
}

bool DiscOpenPanel::defaultSkipDiscMenus() const
{
    return context == OpenPanel::CONTEXT_BATCH ||
        (ui.bdRadioButton->isChecked() &&
         (!var_Type(p_intf, "bluray-menu") || !var_InheritBool(p_intf, "bluray-menu")));
}

DiscOpenPanel::~DiscOpenPanel()
{
    free( psz_dvddiscpath );
    free( psz_vcddiscpath );
    free( psz_cddadiscpath );
}

void DiscOpenPanel::clear()
{
    ui.titleSpin->setValue( 0 );
    ui.chapterSpin->setValue( 0 );
    ui.subtitlesSpin->setValue(0);
    ui.audioSpin->setValue(0);
    ui.dvdsimple->setChecked(defaultSkipDiscMenus());
    m_discType = None;
}

#if defined( _WIN32 ) || defined( __OS2__ )
    #define setDrive( psz_name ) {\
    int index = ui.deviceCombo->findText( qfu( psz_name ) ); \
    if( index != -1 ) ui.deviceCombo->setCurrentIndex( index );}
#else
    #define setDrive( psz_name ) {\
    ui.deviceCombo->setEditText( qfu( psz_name ) ); }
#endif

/* update the buttons according the type of device */
void DiscOpenPanel::updateButtons()
{
    if ( ui.dvdRadioButton->isChecked() )
    {
        if( m_discType != Dvd )
        {
            setDrive( psz_dvddiscpath );
            m_discType = Dvd;
        }
        ui.titleLabel->setText( qtr("Title") );
        ui.chapterLabel->show();
        ui.chapterSpin->show();
        ui.diskOptionBox_2->show();
        ui.dvdsimple->setEnabled( true );
    }
    else if ( ui.dvdaRadioButton->isChecked() )
    {
        if( m_discType != Dvda )
        {
            setDrive( psz_dvddiscpath );
            m_discType = Dvda;
        }
        ui.titleLabel->setText( qtr("Title") );
        ui.chapterLabel->show();
        ui.chapterSpin->show();
        ui.diskOptionBox_2->hide();
        ui.dvdsimple->setEnabled( false );
    }
    else if ( ui.bdRadioButton->isChecked() )
    {
        // Without the Blu-ray module there are no menus to offer.
        const bool dvdsimpleEnable = var_Type( p_intf, "bluray-menu" ) != 0;
        if( m_discType != BRD )
        {
            setDrive( psz_dvddiscpath );
            m_discType = BRD;
            ui.dvdsimple->setChecked(defaultSkipDiscMenus());
        }
        ui.titleLabel->setText( qtr("Title") );
        ui.chapterLabel->hide();
        ui.chapterSpin->hide();
        ui.diskOptionBox_2->hide();
        ui.dvdsimple->setEnabled( dvdsimpleEnable );
    }
    else if ( ui.vcdRadioButton->isChecked() )
    {
        if( m_discType != Vcd )
        {
            setDrive( psz_vcddiscpath );
            m_discType = Vcd;
        }
        ui.titleLabel->setText( qtr("Entry") );
        ui.chapterLabel->hide();
        ui.chapterSpin->hide();
        ui.diskOptionBox_2->show();
        ui.dvdsimple->setEnabled( false );
    }
    else /* CDDA */
    {
        if( m_discType != Cdda )
        {
            setDrive( psz_cddadiscpath );
            m_discType = Cdda;
        }
        ui.titleLabel->setText( qtr("Track") );
        ui.chapterLabel->hide();
        ui.chapterSpin->hide();
        ui.diskOptionBox_2->hide();
        ui.dvdsimple->setEnabled( false );
    }

    updateEditorText();
    updateMRL();
}

void DiscOpenPanel::updateEditorText()
{
    ui.titleSpin->setSpecialValueText(m_discType == Cdda ? qtr("All tracks") : qtr("Default"));
    ui.chapterSpin->setSpecialValueText(qtr("Default"));
    ui.chapterSpin->setEnabled(ui.titleSpin->value() > 0);
    ui.chapterLabel->setEnabled(ui.titleSpin->value() > 0);
    ui.chapterSpin->setToolTip(qtr("Select a title first. Then select a chapter."));
    const auto trackHelp = qtr("Auto uses your track preferences. Track numbers start at 1.");
    ui.audioSpin->setToolTip(trackHelp);
    ui.subtitlesSpin->setToolTip(trackHelp);
    ui.dvdsimple->setToolTip(qtr("Play the disc without its menus. Use this for conversion or streaming."));
    ui.ejectButton->setAccessibleName(qtr("Eject disc"));
    updateLayoutMetrics();
}

void DiscOpenPanel::resizeEvent(QResizeEvent *event)
{
    OpenPanel::resizeEvent(event);
    updateLayoutMetrics();
}

bool DiscOpenPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Resize)
        QMetaObject::invokeMethod(this, &DiscOpenPanel::updateLayoutMetrics, Qt::QueuedConnection);
    return OpenPanel::eventFilter(watched, event);
}

void DiscOpenPanel::updateLayoutMetrics()
{
    const auto dp = [this](int n) { return qRound(n * (p_intf->p_mi ? p_intf->p_mi->getIntfScaleFactor() : 1)); };
    int available = width() - dp(40);
    for (QWidget *parent = parentWidget(); parent; parent = parent->parentWidget())
        if (auto *scroll = qobject_cast<QScrollArea*>(parent))
        {
            scroll->viewport()->installEventFilter(this);
            available = qMin(available, scroll->viewport()->width() - dp(40));
            break;
        }
    const QList<QRadioButton*> choices = {ui.dvdRadioButton, ui.dvdaRadioButton,
        ui.bdRadioButton, ui.audioCDRadioButton, ui.vcdRadioButton};
    int choiceWidth = 1;
    for (auto *choice : choices)
    {
        choice->ensurePolished();
        choiceWidth = qMax(choiceWidth, choice->sizeHint().width());
        choice->setMinimumWidth(choice->sizeHint().width());
    }
    const int gap = dp(12);
    const int columns = qBound(1, (available + gap) / (choiceWidth + gap), int(choices.size()));
    if (ui.discTypeLayout->property("columns").toInt() != columns)
    {
        for (int i = 0; i < choices.size(); ++i) ui.discTypeLayout->addWidget(choices[i], i / columns, i % columns);
        ui.discTypeLayout->setProperty("columns", columns);
    }
    const bool stacked = available < dp(500);
    if (!ui.discDeviceLayout->property("stacked").isValid()
        || ui.discDeviceLayout->property("stacked").toBool() != stacked)
    {
        ui.discDeviceLayout->addWidget(ui.deviceLabel, 0, 0, 1, stacked ? 3 : 1);
        ui.discDeviceLayout->addWidget(ui.deviceCombo, stacked ? 1 : 0, stacked ? 0 : 1);
        ui.discDeviceLayout->addWidget(ui.ejectButton, stacked ? 1 : 0, 2 - int(stacked));
        ui.discDeviceLayout->addWidget(ui.browseDiscButton, stacked ? 1 : 0, 3 - int(stacked));
        for (int col = 0; col < 4; ++col) ui.discDeviceLayout->setColumnStretch(col, col == int(!stacked) ? 1 : 0);
        const auto arrange = [stacked](QGridLayout *grid, QWidget *firstLabel, QWidget *first,
                                       QWidget *secondLabel, QWidget *second) {
            grid->addWidget(firstLabel, 0, 0); grid->addWidget(first, 0, 1);
            grid->addWidget(secondLabel, stacked ? 1 : 0, stacked ? 0 : 2);
            grid->addWidget(second, stacked ? 1 : 0, stacked ? 1 : 3);
        };
        arrange(ui.discPositionLayout, ui.titleLabel, ui.titleSpin, ui.chapterLabel, ui.chapterSpin);
        arrange(ui.discTrackLayout, ui.audioLabel, ui.audioSpin, ui.subtitlesLabel, ui.subtitlesSpin);
        ui.discDeviceLayout->setProperty("stacked", stacked);
    }
    ui.ejectButton->setFixedSize(dp(32), dp(32));
    ui.ejectButton->setIconSize(QSize(dp(20), dp(20)));
    ui.ejectButton->ensurePolished();
    ui.ejectButton->setIcon(ColorizedSvgIcon::colorizedIconForWidget(":/menu/ic_fluent_arrow_eject.svg", ui.ejectButton));
}

#undef setDrive

/* Update the current MRL */
void DiscOpenPanel::updateMRL()
{
    QString discPath;
    QStringList fileList;

    discPath = ui.deviceCombo->currentText();
    if (discPath.trimmed().isEmpty())
    {
        ui.ejectButton->setEnabled(false);
        emit mrlUpdated({}, {});
        return;
    }
    ui.ejectButton->setEnabled(true);

    int tmp = ui.deviceCombo->findText( discPath );
    if( tmp != -1 &&  ui.deviceCombo->itemData( tmp ).isValid() )
        discPath = ui.deviceCombo->itemData( tmp ).toString();

    /* MRL scheme */
    const char *scheme;
    /* DVD */
    if( ui.dvdRadioButton->isChecked() ) {
        if( !ui.dvdsimple->isChecked() )
            scheme = "dvd";
        else
            scheme = "dvdsimple";
    } else if ( ui.dvdaRadioButton->isChecked() )
        scheme = "dvda";
    else if ( ui.bdRadioButton->isChecked() )
        scheme = "bluray";
    /* VCD */
    else if ( ui.vcdRadioButton->isChecked() )
        scheme = "vcd";
    /* CDDA */
    else
        scheme = "cdda";

    char *mrl = vlc_path2uri( qtu(discPath), scheme );
    if( unlikely(mrl == NULL) )
    {
        emit mrlUpdated({}, {});
        return;
    }

    /* Title/chapter encoded in MRL */
    QString anchor = "";
    if( ui.titleSpin->value() > 0 ) {
        if( ui.dvdRadioButton->isChecked() || ui.bdRadioButton->isChecked() || ui.dvdaRadioButton->isChecked() ) {
            anchor = QString("#%1").arg( ui.titleSpin->value() );
            if ( ui.chapterSpin->value() > 0 )
                anchor += QString(":%1").arg( ui.chapterSpin->value() );
        }
        else if ( ui.vcdRadioButton->isChecked() )
            anchor = QString("#%1").arg( ui.titleSpin->value() );
    }

    emit methodChanged( "disc-caching" );

    fileList << (qfu(mrl) + anchor);
    free(mrl);

    QString opts = "";

    /* Input item options */
    if ( ui.dvdRadioButton->isChecked() || ui.vcdRadioButton->isChecked())
    {
        if (ui.audioSpin->value() > 0)
            opts += " :audio-track=" +
                QString::number(ui.audioSpin->value() - 1);
        if (ui.subtitlesSpin->value() > 0)
            opts += " :sub-track=" +
                QString::number(ui.subtitlesSpin->value() - 1);
    }
    else if( ui.audioCDRadioButton->isChecked() )
    {
        if( ui.titleSpin->value() > 0 )
            opts += QString(" :cdda-track=%1").arg( ui.titleSpin->value() );
    }
    else if ( ui.bdRadioButton->isChecked() )
    {
        if ( ui.dvdsimple->isChecked() )
            opts += " :no-bluray-menu";
    }
    emit mrlUpdated( fileList, opts );
}

QString DiscOpenPanel::validationError() const
{
    return ui.deviceCombo->currentText().trimmed().isEmpty()
        ? qtr("Select a disc device or folder.") : QString();
}

void DiscOpenPanel::browseDevice()
{
    const QStringList schemes = QStringList(QStringLiteral("file"));
    QString dir = QFileDialog::getExistingDirectoryUrl( this,
            qfut( I_DEVICE_TOOLTIP ), p_intf->p_mi->getDialogFilePath(),
            QFileDialog::ShowDirsOnly, schemes ).toLocalFile();
    if( !dir.isEmpty() )
    {
        const QString dirNoSlash = toNativeSepNoSlash( std::move(dir) );
        ui.deviceCombo->addItem( dirNoSlash );
        ui.deviceCombo->setCurrentIndex( ui.deviceCombo->findText( dirNoSlash ) );
    }

    updateMRL();
}

void DiscOpenPanel::eject()
{
    intf_Eject( p_intf, qtu( ui.deviceCombo->currentText() ) );
}

void DiscOpenPanel::accept()
{}

/**************************************************************************
 * Open Network streams and URL pages                                     *
 **************************************************************************/
NetOpenPanel::NetOpenPanel(QWidget *parent, qt_intf_t *intf) : OpenPanel(parent, intf)
{
    ui.setupUi(this);
    setObjectName(QStringLiteral("networkOpenPanel"));
    ui.urlComboBox->setMaxCount(10);
    ui.urlComboBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    ui.urlComboBox->setMinimumContentsLength(12);
    if (historyEnabled()) ui.urlComboBox->addItems(savedHistory());
    ui.urlComboBox->lineEdit()->setClearButtonEnabled(true);
    ui.urlComboBox->setValidator(new UrlValidator(this));
    ui.urlComboBox->completer()->setCaseSensitivity(Qt::CaseSensitive);
    ui.networkAddressRow->setStretchFactor(ui.urlComboBox, 1);
    ui.networkAddressRow->setAlignment(ui.clearHistoryButton, Qt::AlignRight);
    ui.examples->setTextFormat(Qt::PlainText);
    ui.examples->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    ui.examples->setFocusPolicy(Qt::StrongFocus);
    ui.groupBox->installEventFilter(this);
    ui.clearHistoryButton->installEventFilter(this);
    ui.urlComboBox->installEventFilter(this);
    ui.urlComboBox->lineEdit()->installEventFilter(this);
    connect(ui.urlComboBox, &QComboBox::editTextChanged, this, &NetOpenPanel::updateMRL);
    connect(ui.clearHistoryButton, &QPushButton::clicked, this, &NetOpenPanel::clearHistory);
    if (auto *ctx = p_intf->p_mi)
    {
        connect(ctx, &MainCtx::redesignPlayerPreferencesChanged, this, &NetOpenPanel::refreshHistory);
        connect(ctx, &MainCtx::intfScaleFactorChanged, this, &NetOpenPanel::scheduleLayoutUpdate);
    }
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, &NetOpenPanel::retranslate);
    retranslate();
}

NetOpenPanel::EditorState NetOpenPanel::editorState() const
{
    const auto *edit = ui.urlComboBox->lineEdit();
    return {edit->text(), edit->cursorPosition(), edit->selectionStart(), int(edit->selectedText().size())};
}

void NetOpenPanel::restoreSelection(const EditorState &state)
{
    auto *edit = ui.urlComboBox->lineEdit();
    if (state.start >= 0)
        edit->setSelection(state.cursor == state.start ? state.start + state.length : state.start,
                           state.cursor == state.start ? -state.length : state.length);
    else edit->setCursorPosition(state.cursor);
}

bool NetOpenPanel::historyEnabled() const
{
    return var_InheritBool(p_intf, "save-recentplay");
}

QStringList NetOpenPanel::savedHistory() const
{
    QStringList result;
    for (const auto &entry : getSettings()->value(QStringLiteral("OpenDialog/netMRL")).toStringList())
    {
        const auto address = entry.trimmed();
        if (!address.isEmpty() && !result.contains(address)) result.append(address);
        if (result.size() == 10) break;
    }
    return result;
}

void NetOpenPanel::refreshHistory()
{
    const auto history = savedHistory();
    const bool enabled = historyEnabled();
    const auto wanted = enabled ? history : QStringList();
    QStringList displayed;
    for (int i = 0; i < ui.urlComboBox->count(); ++i) displayed.append(ui.urlComboBox->itemText(i));
    {
        auto *edit = ui.urlComboBox->lineEdit();
        const auto state = editorState();
        // Do not publish a transient empty source while rebuilding the choices.
        const QSignalBlocker comboBlock(ui.urlComboBox), editBlock(edit);
        if (displayed != wanted)
        {
            ui.urlComboBox->clear();
            ui.urlComboBox->addItems(wanted);
        }
        for (int i = 0; i < ui.urlComboBox->count(); ++i)
            ui.urlComboBox->setItemData(i, Qt::convertFromPlainText(ui.urlComboBox->itemText(i)), Qt::ToolTipRole);
        ui.urlComboBox->setEditText(state.text);
        restoreSelection(state);
    }
    ui.clearHistoryButton->setEnabled(!history.isEmpty());
    ui.historyStatus->setText(qtr("Address history is off."));
    ui.historyStatus->setVisible(!enabled);
    scheduleLayoutUpdate();
}

void NetOpenPanel::retranslate()
{
    ui.retranslateUi(this);
    ui.label_1->setTextFormat(Qt::PlainText);
    ui.label_1->setWordWrap(true);
    ui.historyStatus->setTextFormat(Qt::PlainText);
    ui.urlComboBox->setAccessibleName(qtr("Media address"));
    ui.urlComboBox->lineEdit()->setAccessibleName(qtr("Media address"));
    ui.urlComboBox->lineEdit()->setAccessibleDescription(ui.label_1->text());
    ui.urlComboBox->lineEdit()->setPlaceholderText(QStringLiteral("https://example.com/media"));
    refreshHistory();
}

void NetOpenPanel::scheduleLayoutUpdate()
{
    if (m_layoutPending) return;
    m_layoutPending = true;
    QTimer::singleShot(0, this, [this] {
        m_layoutPending = false;
        const auto dp = [this](int value) { return qRound(value * (p_intf->p_mi ? p_intf->p_mi->getIntfScaleFactor() : 1)); };
        const int available = ui.networkAddressRow->geometry().width();
        if (available <= 0) return;
        ui.networkAddressRow->setSpacing(dp(8));
        ui.networkAddressRow->setDirection(available < dp(260) + ui.clearHistoryButton->sizeHint().width() + dp(8)
            ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    });
}

void NetOpenPanel::resizeEvent(QResizeEvent *event)
{
    OpenPanel::resizeEvent(event);
    scheduleLayoutUpdate();
}

bool NetOpenPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui.urlComboBox && event->type() == QEvent::FocusOut)
        m_lastFocusedState = editorState();
    if (event->type() == QEvent::LayoutRequest || event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange)
        scheduleLayoutUpdate();
    return OpenPanel::eventFilter(watched, event);
}

void NetOpenPanel::clear()
{
    ui.urlComboBox->lineEdit()->clear();
    m_lastFocusedState = {};
}

void NetOpenPanel::onAccept()
{
    if (historyEnabled())
    {
        const auto address = ui.urlComboBox->currentText().trimmed();
        if (!address.isEmpty())
        {
            auto history = savedHistory();
            history.removeAll(address);
            history.prepend(address);
            getSettings()->setValue(QStringLiteral("OpenDialog/netMRL"), history.mid(0, 10));
        }
    }
    refreshHistory();
}

void NetOpenPanel::onFocus()
{
    refreshHistory();
    ui.urlComboBox->setFocus();
    ui.urlComboBox->lineEdit()->selectAll();
}

void NetOpenPanel::updateMRL()
{
    QString url = ui.urlComboBox->lineEdit()->text().trimmed();

    emit methodChanged( qfu( "network-caching" ) );

    QStringList qsl;
    if (url.isEmpty())
    {
        emit mrlUpdated({}, {});
        return;
    }

    if( url.contains( QLatin1String( "://" ) ) )
    {
        char *uri = vlc_uri_fixup( qtu( url ) );
        if (uri == NULL)
        {
            emit mrlUpdated({}, {});
            return;
        }
        url = qfu(uri);
        free( uri );
    }

    qsl << url;
    emit mrlUpdated( qsl, "" );
}

void NetOpenPanel::clearHistory()
{
    auto *edit = ui.urlComboBox->lineEdit();
    auto state = editorState();
    // An editable combo can deselect its text when the history button gains focus.
    if (!edit->hasFocus() && m_lastFocusedState.text == state.text)
        state = m_lastFocusedState;
    getSettings()->remove(QStringLiteral("OpenDialog/netMRL"));
    refreshHistory();
    edit->setFocus(Qt::OtherFocusReason);
    restoreSelection(state);
}

/**************************************************************************
 * Open Capture device ( DVB, PVR, V4L, and similar )                     *
 **************************************************************************/
CaptureOpenPanel::CaptureOpenPanel( QWidget *_parent, qt_intf_t *_p_intf ) :
                                OpenPanel( _parent, _p_intf )
{
    isInitialized = false;
    screenFPS = nullptr;
#ifdef _WIN32
    vdevDshowW = NULL;
    adevDshowW = NULL;
#endif
}

void CaptureOpenPanel::initialize()
{
    if( isInitialized ) return;

    isInitialized = true;

    ui.setupUi( this );

    BUTTONACT( ui.advancedButton, &CaptureOpenPanel::advancedDialog );
    connect( ui.deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
             this, &CaptureOpenPanel::enableAdvancedDialog );

    /* Create two stacked layouts in the main comboBoxes */
    QStackedLayout *stackedDevLayout = new QStackedLayout;
    ui.cardBox->setLayout( stackedDevLayout );

    QStackedLayout *stackedPropLayout = new QStackedLayout;
    ui.optionsBox->setLayout( stackedPropLayout );

    /* Creation and connections of the WIdgets in the stacked layout */
#define addModuleAndLayouts( number, name, label, layout )            \
    QWidget * name ## DevPage = new QWidget( this );                  \
    QWidget * name ## PropPage = new QWidget( this );                 \
    stackedDevLayout->addWidget( name ## DevPage );        \
    stackedPropLayout->addWidget( name ## PropPage );      \
    layout * name ## DevLayout = new layout;                \
    layout * name ## PropLayout = new layout;               \
    name ## DevPage->setLayout( name ## DevLayout );                  \
    name ## PropPage->setLayout( name ## PropLayout );                \
    ui.deviceCombo->addItem( qfut( label ), QVariant( number ) );        \
    watchCaptureMode(ui.deviceCombo, ui.deviceCombo->count() - 1, label);

#define CuMRL( widget, slot ) connect( widget, slot, this, &CaptureOpenPanel::updateMRL );

#ifdef _WIN32
    /*********************
     * DirectShow Stuffs *
     *********************/
    if( module_exists( "dshow" ) ){
    addModuleAndLayouts( DSHOW_DEVICE, dshow, "DirectShow", QFormLayout );

    /* dshow Main */
    const auto addDevice = [dshowDevLayout](StringListConfigControl *control) {
        QGridLayout row;
        control->insertInto(&row, 0);
        auto *labelItem = row.takeAt(0);
        auto *fieldItem = row.takeAt(0);
        auto *label = qobject_cast<QLabel*>(labelItem->widget());
        auto *field = fieldItem->widget();
        label->setBuddy(field);
        dshowDevLayout->addRow(label, field);
        delete labelItem;
        delete fieldItem;
    };
    module_config_t *p_config = config_FindConfig( "dshow-vdev" );
    vdevDshowW = new StringListConfigControl( p_config, this );
    addDevice(vdevDshowW);

    p_config = config_FindConfig( "dshow-adev" );
    adevDshowW = new StringListConfigControl( p_config, this );
    addDevice(adevDshowW);

    /* dshow Properties */
    QLabel *dshowVSizeLabel = captureText<QLabel>(N_("Video size"), this);

    dshowVSizeLine = new QLineEdit;
    dshowVSizeLabel->setBuddy(dshowVSizeLine);
    dshowPropLayout->addRow(dshowVSizeLabel, dshowVSizeLine);

    /* dshow CONNECTs */
    CuMRL( vdevDshowW, &StringListConfigControl::changed );
    CuMRL( adevDshowW, &StringListConfigControl::changed );
    CuMRL( dshowVSizeLine, &QLineEdit::textChanged );
    configList << "dshow-vdev" << "dshow-adev" << "dshow-size";
    }
#else /* _WIN32 */
    /*******
     * V4L2*
     *******/
    char const * const ppsz_v4lvdevices[] = {
        "video*"
    };
    if( module_exists( "v4l2" ) ){
    addModuleAndLayouts( V4L2_DEVICE, v4l2, N_("Video camera"), QFormLayout );

    /* V4L2 main panel */
    QLabel *v4l2VideoDeviceLabel = captureText<QLabel>(N_("Video device name"), this);

    v4l2VideoDevice = new QComboBox( this );
    v4l2VideoDevice->setEditable( true );
    POPULATE_WITH_DEVS( ppsz_v4lvdevices, v4l2VideoDevice );
    v4l2VideoDeviceLabel->setBuddy(v4l2VideoDevice);
    v4l2DevLayout->addRow(v4l2VideoDeviceLabel, v4l2VideoDevice);

    QLabel *v4l2AudioDeviceLabel = captureText<QLabel>(N_("Audio device name"), this);

    v4l2AudioDevice = new QComboBox( this );
    v4l2AudioDevice->setEditable( true );
    {
        QStringList patterns = QStringList();
        patterns << QString( "pcmC*D*c" );

        QStringList nodes = QDir( "/dev/snd" ).entryList( patterns,
                                                          QDir::System );
        QStringList names = nodes.replaceInStrings( QRegularExpression(QStringLiteral("^pcmC")), "hw:" )
                                 .replaceInStrings( QRegularExpression(QStringLiteral("c$")), "" )
                                 .replaceInStrings( QRegularExpression(QStringLiteral("D")), "," );
        v4l2AudioDevice->addItems( names );
    }
    v4l2AudioDevice->clearEditText();
    v4l2AudioDeviceLabel->setBuddy(v4l2AudioDevice);
    v4l2DevLayout->addRow(v4l2AudioDeviceLabel, v4l2AudioDevice);

    /* v4l2 Props panel */
    QLabel *v4l2StdLabel = captureText<QLabel>(N_("Video standard"), this);

    v4l2StdBox = new QComboBox;
    setfillVLCConfigCombo( "v4l2-standard", v4l2StdBox );
    v4l2StdLabel->setBuddy(v4l2StdBox);
    v4l2PropLayout->addRow(v4l2StdLabel, v4l2StdBox);

    /* v4l2 CONNECTs */
    CuMRL( v4l2VideoDevice->lineEdit(), &QLineEdit::textChanged );
    CuMRL( v4l2VideoDevice, QOverload<int>::of(&QComboBox::currentIndexChanged) );
    CuMRL( v4l2AudioDevice->lineEdit(), &QLineEdit::textChanged );
    CuMRL( v4l2AudioDevice, QOverload<int>::of(&QComboBox::currentIndexChanged) );
    CuMRL( v4l2StdBox, QOverload<int>::of(&QComboBox::currentIndexChanged) );
    configList << "v4l2-standard";
    }

    /*******
     * JACK *
     *******/
    if( module_exists( "jack" ) ){
    addModuleAndLayouts( JACK_DEVICE, jack, N_("JACK Audio Connection Kit"),
                         QFormLayout);

    /* Jack Main panel */
    /* Channels */
    QLabel *jackChannelsLabel = captureText<QLabel>(N_("Channels:"), this);

    jackChannels = new QSpinBox;
    setSpinBoxFreq( jackChannels );
    jackChannels->setMaximum(255);
    jackChannels->setValue(2);
    jackChannels->setAlignment( Qt::AlignRight );
    jackChannelsLabel->setBuddy(jackChannels);
    jackDevLayout->addRow(jackChannelsLabel, jackChannels);

    /* Selected ports */
    QLabel *jackPortsLabel = captureText<QLabel>(N_("Selected ports:"), this);

    jackPortsSelected = new QLineEdit( qfu( ".*" ) );
    jackPortsSelected->setAlignment( Qt::AlignRight );
    jackPortsLabel->setBuddy(jackPortsSelected);
    jackDevLayout->addRow(jackPortsLabel, jackPortsSelected);

    /* Jack Props panel */

    /* Pace */
    jackPace = captureText<QCheckBox>(N_("Use Hakone pace"), this);
    jackPropLayout->addRow(jackPace);

    /* Auto Connect */
    jackConnect = captureText<QCheckBox>(N_("Auto connection"), this);
    jackPropLayout->addRow(jackConnect);

    /* Jack CONNECTs */
    CuMRL( jackChannels, QOverload<int>::of(&QSpinBox::valueChanged) );
    CuMRL( jackPace, &QtCheckboxChanged );
    CuMRL( jackConnect, &QtCheckboxChanged );
    CuMRL( jackPortsSelected, &QLineEdit::textChanged );
    configList << "jack-input-use-vlc-pace" << "jack-input-auto-connect";
    }
#endif

    /*************
     * DVB Stuff *
     *************/
    if( module_exists( "dtv" ) ){
    addModuleAndLayouts( DTV_DEVICE, dvb, N_("TV - digital"), QFormLayout );

    /* DVB Main */
    QLabel *dvbDeviceLabel = captureText<QLabel>(N_("Tuner card"), this);
    QLabel *dvbTypeLabel = captureText<QLabel>(N_("Delivery system"), this);

    dvbCard = new QSpinBox;
    dvbCard->setObjectName(QStringLiteral("captureTuner"));
    dvbCard->setAlignment( Qt::AlignRight );
#ifdef __unix__
    dvbCard->setPrefix( "/dev/dvb/adapter" );
#endif
    dvbDeviceLabel->setBuddy(dvbCard);
    dvbDevLayout->addRow(dvbDeviceLabel, dvbCard);

    dvbc = new QRadioButton( "DVB-C" );
    dvbs = new QRadioButton( "DVB-S" );
    dvbs2 = new QRadioButton( "DVB-S2" );
    dvbt = new QRadioButton( "DVB-T" );
    dvbt2 = new QRadioButton( "DVB-T2" );
    atsc = new QRadioButton( "ATSC" );
    cqam = new QRadioButton( "Clear QAM" );
    dvbt->setChecked( true );

    auto *delivery = new QWidget(this);
    delivery->setObjectName(QStringLiteral("captureDelivery"));
    auto *deliveryLayout = new QGridLayout(delivery);
    deliveryLayout->setContentsMargins(0, 0, 0, 0);
    const QList<QRadioButton*> systems = {dvbc, dvbs, dvbs2, dvbt, dvbt2, atsc, cqam};
    for (int i = 0; i < systems.size(); ++i)
    {
        systems[i]->setParent(delivery);
        deliveryLayout->addWidget(systems[i], i, 0);
    }
    dvbTypeLabel->setBuddy(dvbt);
    dvbDevLayout->addRow(dvbTypeLabel, delivery);

    /* DVB Props panel */
    QLabel *dvbFreqLabel =
                    captureText<QLabel>(N_("Frequency"), this);

    dvbFreq = new QSpinBox;
    dvbFreq->setObjectName(QStringLiteral("captureFrequency"));
    dvbFreqLabel->setBuddy(dvbFreq);
    dvbFreq->setAlignment( Qt::AlignRight );
    dvbFreq->setSuffix(" kHz");
    dvbFreq->setSingleStep( 1000 );
    setSpinBoxFreq( dvbFreq  );
    dvbFreqLabel->setBuddy(dvbFreq);
    dvbPropLayout->addRow(dvbFreqLabel, dvbFreq);

    dvbSrateLabel = captureText<QLabel>(N_("Symbol rate"), this);

    dvbSrate = new QSpinBox;
    dvbSrate->setAlignment( Qt::AlignRight );
    dvbSrate->setSuffix(" Bd");
    setSpinBoxFreq( dvbSrate );
    dvbSrateLabel->setBuddy(dvbSrate);
    dvbPropLayout->addRow(dvbSrateLabel, dvbSrate);

    dvbModLabel = captureText<QLabel>(N_("Modulation"), this);

    dvbQamBox = new QComboBox;
    dvbQamBox->addItem( qtr( "Automatic" ), qfu("QAM") );
    dvbQamBox->addItem( "256-QAM", qfu("256QAM") );
    dvbQamBox->addItem( "128-QAM", qfu("128QAM") );
    dvbQamBox->addItem( "64-QAM", qfu("64QAM") );
    dvbQamBox->addItem( "32-QAM", qfu("32QAM") );
    dvbQamBox->addItem( "16-QAM", qfu("16QAM") );
    dvbModLabel->setBuddy(dvbQamBox);

    dvbPskBox = new QComboBox;
    dvbPskBox->addItem( "QPSK", qfu("QPSK") );
    dvbPskBox->addItem( "DQPSK", qfu("DQPSK") );
    dvbPskBox->addItem( "8-PSK", qfu("8PSK") );
    dvbPskBox->addItem( "16-APSK", qfu("16APSK") );
    dvbPskBox->addItem( "32-APSK", qfu("32APSK") );
    auto *modulations = new QWidget(this);
    auto *modulationLayout = new QVBoxLayout(modulations);
    modulationLayout->setContentsMargins(0, 0, 0, 0);
    modulationLayout->setSpacing(0);
    modulationLayout->addWidget(dvbQamBox);
    modulationLayout->addWidget(dvbPskBox);
    modulations->setObjectName(QStringLiteral("captureModulation"));
    dvbPropLayout->addRow(dvbModLabel, modulations);

    dvbModLabel->hide();
    dvbQamBox->hide();
    dvbPskBox->hide();

    dvbBandLabel = captureText<QLabel>(N_("Bandwidth"), this);

    dvbBandBox = new QComboBox;
    setfillVLCConfigCombo( "dvb-bandwidth", dvbBandBox );
    dvbBandLabel->setBuddy(dvbBandBox);
    dvbPropLayout->addRow(dvbBandLabel, dvbBandBox);

    dvbBandLabel->hide();
    dvbBandBox->hide();


    /* DVB CONNECTs */
    CuMRL( dvbCard, QOverload<int>::of(&QSpinBox::valueChanged) );
    CuMRL( dvbFreq, QOverload<int>::of(&QSpinBox::valueChanged) );
    CuMRL( dvbSrate, QOverload<int>::of(&QSpinBox::valueChanged) );
    CuMRL( dvbQamBox, QOverload<int>::of(&QComboBox::currentIndexChanged) );
    CuMRL( dvbPskBox, QOverload<int>::of(&QComboBox::currentIndexChanged) );
    CuMRL( dvbBandBox, QOverload<int>::of(&QComboBox::currentIndexChanged) );

    BUTTONACT( dvbc, &CaptureOpenPanel::updateButtons );
    BUTTONACT( dvbs, &CaptureOpenPanel::updateButtons );
    BUTTONACT( dvbs2, &CaptureOpenPanel::updateButtons );
    BUTTONACT( dvbt, &CaptureOpenPanel::updateButtons );
    BUTTONACT( dvbt2, &CaptureOpenPanel::updateButtons );
    BUTTONACT( atsc, &CaptureOpenPanel::updateButtons );
    BUTTONACT( cqam, &CaptureOpenPanel::updateButtons );
    BUTTONACT( dvbc, &CaptureOpenPanel::updateMRL );
    BUTTONACT( dvbt, &CaptureOpenPanel::updateMRL );
    BUTTONACT( dvbt2, &CaptureOpenPanel::updateMRL );
    BUTTONACT( dvbs, &CaptureOpenPanel::updateMRL );
    BUTTONACT( dvbs2, &CaptureOpenPanel::updateMRL );
    BUTTONACT( atsc, &CaptureOpenPanel::updateMRL );
    BUTTONACT( cqam, &CaptureOpenPanel::updateMRL );
    configList << "dvb-adapter" << "dvb-frequency" << "dvb-modulation"
               << "dvb-bandwidth";
    }

#ifndef _WIN32
    /************
     * PVR      *
     ************/
    if( module_exists( "v4l2" ) ){
    addModuleAndLayouts( PVR_DEVICE, pvr, N_("TV - analog"), QFormLayout );

    /* PVR Main panel */
    QLabel *pvrDeviceLabel = captureText<QLabel>(N_("Device name"), this);

    pvrDevice = new QComboBox;
    pvrDevice->setEditable( true );
    POPULATE_WITH_DEVS( ppsz_v4lvdevices, pvrDevice );
    pvrDevice->clearEditText();
    pvrDeviceLabel->setBuddy(pvrDevice);
    pvrDevLayout->addRow(pvrDeviceLabel, pvrDevice);

    QLabel *pvrAudioDeviceLabel = captureText<QLabel>(N_("Audio device name"), this);

    pvrAudioDevice = new QComboBox( this );
    pvrAudioDevice->setEditable( true );
    {
        QStringList patterns = QStringList();
        patterns << QString( "pcmC*D*c" );

        QStringList nodes = QDir( "/dev/snd" ).entryList( patterns,
                                                          QDir::System );
        QStringList names = nodes.replaceInStrings( QRegularExpression(QStringLiteral("^pcmC")), "hw:" )
                                 .replaceInStrings( QRegularExpression(QStringLiteral("c$")), "" )
                                 .replaceInStrings( QRegularExpression(QStringLiteral("D")), "," );
        pvrAudioDevice->addItems( names );
    }
    pvrAudioDevice->clearEditText();
    pvrAudioDeviceLabel->setBuddy(pvrAudioDevice);
    pvrDevLayout->addRow(pvrAudioDeviceLabel, pvrAudioDevice);

    /* PVR props panel */
    QLabel *pvrNormLabel = captureText<QLabel>(N_("Video standard"), this);

    pvrNormBox = new QComboBox;
    setfillVLCConfigCombo( "v4l2-standard", pvrNormBox );
    pvrNormLabel->setBuddy(pvrNormBox);
    pvrPropLayout->addRow(pvrNormLabel, pvrNormBox);

    QLabel *pvrFreqLabel = captureText<QLabel>(N_("Frequency"), this);

    pvrFreq = new QSpinBox;
    pvrFreq->setAlignment( Qt::AlignRight );
    pvrFreq->setSuffix(" kHz");
    setSpinBoxFreq( pvrFreq );
    pvrFreqLabel->setBuddy(pvrFreq);
    pvrPropLayout->addRow(pvrFreqLabel, pvrFreq);


    /* PVR CONNECTs */
    CuMRL( pvrDevice, &QComboBox::editTextChanged );
    CuMRL( pvrAudioDevice, &QComboBox::editTextChanged );
    CuMRL( pvrFreq, QOverload<int>::of(&QSpinBox::valueChanged) );
    CuMRL( pvrNormBox, QOverload<int>::of(&QComboBox::currentIndexChanged) );
    }
#endif

    /**********
     * Screen *
     **********/
    if (hasScreenCapture()) {
    addModuleAndLayouts( SCREEN_DEVICE, screen, N_("Desktop"), QFormLayout );
    QLabel *screenLabel = captureText<QLabel>(N_("Capture your display. You can play, stream, or save the video."), this);
    screenLabel->setWordWrap( true );
    screenDevLayout->addRow(screenLabel);

    QLabel *screenFPSLabel = captureText<QLabel>(N_("Frame rate"), this);

    screenFPS = new QDoubleSpinBox;
    screenFPS->setObjectName(QStringLiteral("captureFrameRate"));
    screenFPSLabel->setBuddy(screenFPS);
    const double configuredFPS = var_InheritFloat(p_intf, "screen-fps");
    const double initialFPS = std::isfinite(configuredFPS) && configuredFPS > 0 ? configuredFPS : 2.;
    screenFPS->setRange(.01, qMax(100., initialFPS));
    screenFPS->setValue(initialFPS);
    screenFPS->setAlignment( Qt::AlignRight );
    /* xgettext: frames per second */
    screenFPS->setSuffix( qtr( " f/s" ) );
    screenFPSLabel->setBuddy(screenFPS);
    screenPropLayout->addRow(screenFPSLabel, screenFPS);

    /* Screen connect */
    CuMRL( screenFPS, QOverload<double>::of(&QDoubleSpinBox::valueChanged) );
    }

    for (auto *form : findChildren<QFormLayout*>())
    {
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    }
    if (p_intf->p_mi)
        connect(p_intf->p_mi, &MainCtx::intfScaleFactorChanged, this, &CaptureOpenPanel::updateLayoutMetrics);
    updateLayoutMetrics();

    /* Keep the visible page and source in step for mouse and keyboard changes. */
    connect(ui.deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [=](int index) {
        stackedDevLayout->setCurrentIndex(index);
        stackedPropLayout->setCurrentIndex(index);
        updateButtons();
        updateMRL();
    });
    updateButtons();
    enableAdvancedDialog(ui.deviceCombo->currentIndex());

    if (auto* translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, [this] {
            ui.retranslateUi(this);
            if (screenFPS)
            {
                const QSignalBlocker blocker(screenFPS);
                /* xgettext: frames per second */
                screenFPS->setSuffix(qtr(" f/s"));
            }
        });

#undef CuMRL
#undef addModuleAndLayouts
}

CaptureOpenPanel::~CaptureOpenPanel()
{
#ifdef _WIN32
    delete vdevDshowW;
    delete adevDshowW;
#endif
}

void CaptureOpenPanel::clear()
{
    advancedMRLs.clear();
    advancedValues.clear();
}

QString CaptureOpenPanel::validationError() const
{
    if (!isInitialized) return {};
    if (ui.deviceCombo->currentIndex() < 0) return qtr("No capture modes are available in this build.");
    if (ui.deviceCombo->currentData().toInt() == DTV_DEVICE && dvbFreq->value() == 0)
        return qtr("Set a frequency above zero.");
    return {};
}

void CaptureOpenPanel::updateMRL()
{
    if (!isInitialized) return;
    if (ui.deviceCombo->currentIndex() < 0)
    {
        emit mrlUpdated({}, {});
        return;
    }
    QString mrl = "";
    QStringList fileList;
    int i_devicetype = ui.deviceCombo->itemData(
            ui.deviceCombo->currentIndex() ).toInt();
    switch( i_devicetype )
    {
#ifdef _WIN32
    case DSHOW_DEVICE:
        fileList << "dshow://";
        mrl+= " :dshow-vdev=" +
            colon_escape( QString("%1").arg( vdevDshowW->getValue() ) );
        mrl+= " :dshow-adev=" +
            colon_escape( QString("%1").arg( adevDshowW->getValue() ) )+" ";
        if( dshowVSizeLine->isModified() )
            mrl += ":dshow-size=" + dshowVSizeLine->text();
        break;
#else
    case V4L2_DEVICE:
        fileList << "v4l2://" + v4l2VideoDevice->currentText();
        mrl += ":v4l2-standard="
            + v4l2StdBox->itemData( v4l2StdBox->currentIndex() ).toString();
        if( !v4l2AudioDevice->currentText().isEmpty() )
            mrl += " :input-slave=alsa://" + v4l2AudioDevice->currentText();
        break;
    case JACK_DEVICE:
        mrl = "jack://";
        mrl += "channels=" + QString::number( jackChannels->value() );
        mrl += ":ports=" + jackPortsSelected->text();
        fileList << mrl; mrl = "";

        if ( jackPace->isChecked() )
        {
                mrl += " :jack-input-use-vlc-pace";
        }
        if ( jackConnect->isChecked() )
        {
                mrl += " :jack-input-auto-connect";
        }
        break;
    case PVR_DEVICE:
        fileList << "v4l2://" + pvrDevice->currentText();
        mrl += ":v4l2-standard="
            + pvrNormBox->itemData( pvrNormBox->currentIndex() ).toString();
        if( pvrFreq->value() )
            mrl += ":v4l2-tuner-frequency=" + QString::number( pvrFreq->value() );
        if( !pvrAudioDevice->currentText().isEmpty() )
            mrl += " :input-slave=" + pvrAudioDevice->currentText();
        break;
#endif
    case DTV_DEVICE:
        if( dvbc->isChecked() ) mrl = "dvb-c://";
        else
        if( dvbs->isChecked() ) mrl = "dvb-s://";
        else
        if( dvbs2->isChecked() ) mrl = "dvb-s2://";
        else
        if( dvbt->isChecked() ) mrl = "dvb-t://";
        else
        if( dvbt2->isChecked() ) mrl = "dvb-t2://";
        else
        if( atsc->isChecked() ) mrl = "atsc://";
        else
        if( cqam->isChecked() ) mrl = "cqam://";

        mrl += "frequency=" + QString::number( dvbFreq->value() ) + "000";

        if( dvbc->isChecked() || cqam->isChecked() )
            mrl += ":modulation="
                + dvbQamBox->itemData( dvbQamBox->currentIndex() ).toString();
        if( dvbs2->isChecked() )
            mrl += ":modulation="
                + dvbPskBox->itemData( dvbPskBox->currentIndex() ).toString();
        if( dvbc->isChecked() || dvbs->isChecked() || dvbs2->isChecked() )
            mrl += ":srate=" + QString::number( dvbSrate->value() );
        if( dvbt->isChecked() || dvbt2->isChecked() )
            mrl += ":bandwidth=" +
                QString::number( dvbBandBox->itemData(
                    dvbBandBox->currentIndex() ).toInt() );

        fileList << mrl; mrl= "";
        mrl += " :dvb-adapter=" + QString::number( dvbCard->value() );
        break;
    case SCREEN_DEVICE:
        fileList << "screen://";
        mrl = " :screen-fps=" + QString::number( screenFPS->value(), 'f' );
        updateButtons();
        break;
    }
    emit methodChanged( "live-caching" );

    const auto advanced = advancedMRLs.value(i_devicetype);
    if (!advanced.isEmpty()) mrl += QLatin1Char(' ') + advanced;

    emit mrlUpdated( fileList, mrl );
}

/**
 * Update the Buttons (show/hide) for the GUI as all device type don't
 * use the same ui. elements.
 **/
void CaptureOpenPanel::updateButtons()
{
    const bool available = ui.deviceCombo->currentIndex() >= 0;
    ui.deviceCombo->setEnabled(available);
    ui.cardBox->setVisible(available);
    ui.optionsBox->setVisible(available);
    ui.advancedButton->setVisible(available);
    if (!available) return;
    /* Restore fields that the previous capture mode hid. */
    ui.optionsBox->show();
    ui.advancedButton->show();
    /* Get the current Device Number */
    int i_devicetype = ui.deviceCombo->itemData(
                                ui.deviceCombo->currentIndex() ).toInt();
    switch( i_devicetype )
    {
    case DTV_DEVICE:
        dvbSrate->hide();
        dvbSrateLabel->hide();
        dvbQamBox->hide();
        dvbPskBox->hide();
        dvbModLabel->hide();
        dvbBandBox->hide();
        dvbBandLabel->hide();

        if( dvbc->isChecked() || cqam->isChecked() )
        {
            dvbSrate->setVisible(dvbc->isChecked());
            dvbSrateLabel->setVisible(dvbc->isChecked());
            dvbQamBox->show();
            dvbModLabel->setBuddy(dvbQamBox);
            dvbModLabel->show();
        }
        else if( dvbs->isChecked() )
        {
            dvbSrate->show();
            dvbSrateLabel->show();
        }
        else if( dvbs2->isChecked() )
        {
            dvbSrate->show();
            dvbSrateLabel->show();
            dvbPskBox->show();
            dvbModLabel->setBuddy(dvbPskBox);
            dvbModLabel->show();
        }
        else if( dvbt->isChecked() || dvbt2->isChecked() )
        {
            dvbBandBox->show();
            dvbBandLabel->show();
        }
        findChild<QWidget*>(QStringLiteral("captureModulation"))->setVisible(dvbc->isChecked() || cqam->isChecked() || dvbs2->isChecked());
        break;
    case SCREEN_DEVICE:
        //ui.optionsBox->hide();
        ui.advancedButton->hide();
        break;
    }

}

void CaptureOpenPanel::resizeEvent(QResizeEvent *event)
{
    OpenPanel::resizeEvent(event);
    updateLayoutMetrics();
}

bool CaptureOpenPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Resize)
        QMetaObject::invokeMethod(this, &CaptureOpenPanel::updateLayoutMetrics, Qt::QueuedConnection);
    return OpenPanel::eventFilter(watched, event);
}

void CaptureOpenPanel::updateLayoutMetrics()
{
    if (!isInitialized) return;
    const auto scale = p_intf->p_mi ? p_intf->p_mi->getIntfScaleFactor() : 1;
    int available = width() - qRound(40 * scale);
    for (QWidget *parent = parentWidget(); parent; parent = parent->parentWidget())
        if (auto *scroll = qobject_cast<QScrollArea*>(parent))
        {
            scroll->viewport()->installEventFilter(this);
            available = qMin(available, scroll->viewport()->width() - qRound(40 * scale));
            break;
        }
    const auto policy = available < qRound(500 * scale)
        ? QFormLayout::WrapAllRows : QFormLayout::WrapLongRows;
    for (auto *form : findChildren<QFormLayout*>()) form->setRowWrapPolicy(policy);

    auto *delivery = findChild<QWidget*>(QStringLiteral("captureDelivery"));
    if (!delivery) return;
    available = qMin(delivery->width(), available - qRound(40 * scale));
    delivery->installEventFilter(this);
    auto *grid = qobject_cast<QGridLayout*>(delivery->layout());
    const QList<QRadioButton*> systems = {dvbc, dvbs, dvbs2, dvbt, dvbt2, atsc, cqam};
    int choiceWidth = 1;
    for (auto *system : systems)
    {
        system->ensurePolished();
        system->setMinimumWidth(system->sizeHint().width());
        choiceWidth = qMax(choiceWidth, system->sizeHint().width());
    }
    const int gap = qMax(0, grid->horizontalSpacing());
    const int columns = qBound(1, (available + gap) / (choiceWidth + gap), int(systems.size()));
    if (grid->property("columns").toInt() != columns)
    {
        for (int i = 0; i < systems.size(); ++i) grid->addWidget(systems[i], i / columns, i % columns);
        grid->setProperty("columns", columns);
    }
}

void CaptureOpenPanel::enableAdvancedDialog( int i_index )
{
    if (i_index < 0) { ui.advancedButton->setEnabled(false); return; }
    int i_devicetype = ui.deviceCombo->itemData( i_index ).toInt();
    module_t *p_module =
        module_find( psz_devModule[i_devicetype] );
    ui.advancedButton->setEnabled( NULL != p_module );
}

void CaptureOpenPanel::advancedDialog()
{
    if (!isInitialized || ui.deviceCombo->currentIndex() < 0) return;
    /* Get selected device type */
    int i_devicetype = ui.deviceCombo->itemData(
                                ui.deviceCombo->currentIndex() ).toInt();

    /* Get the corresponding module */
    module_t *p_module =
        module_find( psz_devModule[i_devicetype] );
    if( NULL == p_module ) return;

    /* Init */
    QList<ConfigControl *> controls;
    QHash<ConfigControl*, module_config_t*> controlItems;
    const auto previousValues = advancedValues.value(i_devicetype);
    // module_config_get returns a shallow copy; keep restored strings here.
    QHash<QString, QByteArray> restoredStrings;

    /* Get the confsize  */
    unsigned int i_confsize;
    module_config_t *p_config;
    p_config = module_config_get( p_module, &i_confsize );

    /* New Adv Prop dialog */
    auto *optionsDialog = new CaptureOptionsDialog(this);
    adv = optionsDialog;
    adv->setObjectName(QStringLiteral("redesignCaptureOptions"));
    adv->setWindowTitle(qtr("Capture options: %1").arg(ui.deviceCombo->currentText()));
    adv->setWindowRole("vlc-advanced-options");

    /* A main Layout with a Frame */
    QVBoxLayout *mainLayout = new QVBoxLayout(adv);
    auto *heading = new QLabel(adv->windowTitle(), adv);
    heading->setProperty("redesignHeading", true);
    heading->setWordWrap(true);
    mainLayout->addWidget(heading);
    auto *help = new QLabel(qtr("Changes apply to this capture mode."), adv);
    help->setWordWrap(true);
    mainLayout->addWidget(help);
    auto *find = new QLineEdit(adv);
    find->setObjectName(QStringLiteral("captureOptionFind"));
    find->setPlaceholderText(qtr("Find an option"));
    find->setAccessibleName(qtr("Find an option"));
    mainLayout->addWidget(find);
    auto *status = new QLabel(adv);
    status->setWordWrap(true);
    mainLayout->addWidget(status);
    QScrollArea *scroll = new QScrollArea;
    mainLayout->addWidget( scroll );

    QFrame *advFrame = new QFrame;
    /* GridLayout inside the Frame */
    QGridLayout *gLayout = new QGridLayout(advFrame);
    gLayout->setColumnStretch(1, 1);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable( true );
    scroll->setWidget( advFrame );

    /* Create the options inside the FrameLayout */
    for( int n = 0; n < (int)i_confsize; n++ )
    {
        module_config_t *p_item = p_config + n;
        QString name = p_item->psz_name;

        if( name.isEmpty() || configList.contains( name ) )
            continue;

        if (previousValues.contains(name))
        {
            const auto value = previousValues.value(name);
            switch (CONFIG_CLASS(p_item->i_type))
            {
            case CONFIG_ITEM_STRING:
                restoredStrings.insert(name, value.toString().toUtf8());
                p_item->value.psz = restoredStrings[name].data();
                break;
            case CONFIG_ITEM_INTEGER: case CONFIG_ITEM_BOOL: p_item->value.i = value.toLongLong(); break;
            case CONFIG_ITEM_FLOAT: p_item->value.f = value.toDouble(); break;
            }
        }
        ConfigControl *config = ConfigControl::createControl(p_item, advFrame, gLayout, n);
        if (config)
        {
            config->setObjectName(name);
            config->setParent(adv);
            controls.append(config);
            controlItems.insert(config, p_item);
            for (auto *field : advFrame->findChildren<QWidget*>())
                if (!field->property("captureOption").isValid()
                    && (qobject_cast<QLineEdit*>(field) || qobject_cast<QAbstractSpinBox*>(field)
                        || qobject_cast<QComboBox*>(field) || qobject_cast<QAbstractButton*>(field)))
                    field->setProperty("captureOption", name);
        }
    }

    /* Button stuffs */
    QDialogButtonBox *advButtonBox = new QDialogButtonBox( adv );
    QPushButton *closeButton = new QPushButton(qtr("&Apply"));
    closeButton->setObjectName(QStringLiteral("captureOptionsApply"));
    closeButton->setProperty("redesignPrimary", true);
    closeButton->setAutoDefault(false);
    QPushButton *cancelButton = new QPushButton(qtr("&Cancel"));
    cancelButton->setAutoDefault(false);
    cancelButton->setObjectName(QStringLiteral("captureOptionsCancel"));

    connect( closeButton, &QPushButton::clicked, adv, &QDialog::accept );
    connect( cancelButton, &QPushButton::clicked, adv, &QDialog::reject );

    advButtonBox->addButton( closeButton, QDialogButtonBox::AcceptRole );
    advButtonBox->addButton( cancelButton, QDialogButtonBox::RejectRole );

    mainLayout->addWidget( advButtonBox );

    const auto filter = [controls, controlItems, find, status] {
        const auto query = find->text().trimmed();
        int shown = 0;
        for (auto *control : controls)
        {
            const auto *item = controlItems.value(control);
            const QString text = qfu(item->psz_name) + QLatin1Char(' ') + qfut(item->psz_text)
                + QLatin1Char(' ') + qfut(item->psz_longtext);
            const bool match = query.isEmpty() || text.contains(query, Qt::CaseInsensitive);
            if (match) { control->show(); ++shown; }
            else control->hide();
        }
        status->setText(controls.isEmpty() ? qtr("This capture mode has no additional options.")
            : shown ? qtr("Options shown: %1 of %2.").arg(shown).arg(controls.size())
                    : qtr("No options match this text."));
    };
    connect(find, &QLineEdit::textChanged, adv, filter);
    filter();
    optionsDialog->setSearchField(find);
    for (auto *button : {closeButton, cancelButton})
        for (const auto key : {Qt::Key_Return, Qt::Key_Enter})
        {
            auto *shortcut = new QShortcut(QKeySequence(key), button);
            shortcut->setContext(Qt::WidgetShortcut);
            connect(shortcut, &QShortcut::activated, button, &QPushButton::click);
        }
    if (auto* translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, adv,
                [this, dialog = adv, closeButton, cancelButton, heading, help, find, filter] {
            dialog->setWindowTitle(qtr("Capture options: %1").arg(ui.deviceCombo->currentText()));
            heading->setText(dialog->windowTitle());
            help->setText(qtr("Changes apply to this capture mode."));
            find->setPlaceholderText(qtr("Find an option"));
            find->setAccessibleName(qtr("Find an option"));
            closeButton->setText(qtr("&Apply"));
            cancelButton->setText(qtr("&Cancel"));
            filter();
        });
    applyRedesignWidgetTheme(adv, p_intf->p_mi);
    optionsDialog->setContext(p_intf->p_mi);
    trackScrollFocus(adv, p_intf->p_mi);
    const auto dp = [this](int n) { return qRound(n * (p_intf->p_mi ? p_intf->p_mi->getIntfScaleFactor() : 1)); };
    QSize preferred(dp(760), dp(600));
    if (adv->screen()) preferred = preferred.boundedTo(adv->screen()->availableGeometry().size());
    adv->resize(preferred);
    find->setFocus();

    /* Creation of the MRL */
    if( adv->exec() )
    {
        QString tempMRL = "";
        QVariantMap values;
        for( int i = 0; i < controls.count(); i++ )
        {
            ConfigControl *control = controls[i];

            tempMRL += (i ? " :" : ":");

            int ctrl_type = control->getType();
            assert(ctrl_type != -1);

            ctrl_type = CONFIG_CLASS(ctrl_type);

            if( ctrl_type == CONFIG_ITEM_BOOL )
                if( !(qobject_cast<VIntConfigControl *>(control)->getValue() ) )
                    tempMRL += "no-";

            tempMRL += control->getName();

            const auto name = qfu(control->getName());
            if (ctrl_type == CONFIG_ITEM_STRING) values[name] = qobject_cast<VStringConfigControl*>(control)->getValue();
            else if (ctrl_type == CONFIG_ITEM_FLOAT) values[name] = qobject_cast<VFloatConfigControl*>(control)->getValue();
            else values[name] = qobject_cast<VIntConfigControl*>(control)->getValue();
            switch( ctrl_type )
            {
                case CONFIG_ITEM_STRING:
                    tempMRL += colon_escape( QString("=%1").arg( qobject_cast<VStringConfigControl *>(control)->getValue() ) );
                    break;
                case CONFIG_ITEM_INTEGER:
                    tempMRL += QString("=%1").arg( qobject_cast<VIntConfigControl *>(control)->getValue() );
                    break;
                case CONFIG_ITEM_FLOAT:
                    tempMRL += QString("=%1").arg( qobject_cast<VFloatConfigControl *>(control)->getValue() );
                    break;
            }
        }
        advancedMRLs.insert(i_devicetype, std::move(tempMRL));
        advancedValues.insert(i_devicetype, std::move(values));
        updateMRL();
    }
    qDeleteAll( controls );
    delete adv;
    module_config_free( p_config );
}
