/*****************************************************************************
 * mediainfo.cpp : Information about an item
 ****************************************************************************
 * Copyright (C) 2006-2008 the VideoLAN team
 *
 * Authors: Clément Stenac <zorglub@videolan.org>
 *          Jean-Baptiste Kempf <jb@videolan.org>
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
 ******************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "mediainfo.hpp"
#include "player/player_controller.hpp"
#include "maininterface/mainctx.hpp"
#include "util/redesign_widget_theme.hpp"
#include "util/interface_translator.hpp"
#include "util/scroll_focus_tracker.hpp"

#include <vlc_url.h>

#include <QTabWidget>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QTabBar>
#include <QScreen>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QPointer>

/* This Dialog has two main modes:
    - General Mode that shows the current Played item, and the stats
    - Single mode that shows the info on ONE SINGLE Item on the playlist
   Please be Careful of not breaking one the modes behaviour... */

MediaInfoDialog::MediaInfoDialog(qt_intf_t *_p_intf,
                                 SharedInputItem p_item )
    : QVLCFrame( _p_intf )
{
    isMainInputInfo = ( p_item == NULL );

    if ( isMainInputInfo )
        setWindowTitle( qtr( "Current Media Information" ) );
    else
        setWindowTitle( qtr( "Media Information" ) );
    setWindowRole( "vlc-media-info" );
    setObjectName( "redesignMediaInfo" );
    setAttribute( Qt::WA_StyledBackground );

    setWindowFlags( Qt::Window | Qt::CustomizeWindowHint |
                    Qt::WindowCloseButtonHint | Qt::WindowMinimizeButtonHint );

    /* TabWidgets and Tabs creation */
    infoTabW = new QTabWidget;
    infoTabW->setObjectName( "mediaInfoTabs" );
    infoTabW->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );

    MP = new MetaPanel( infoTabW, p_intf );
    auto *metaScrollArea = new QScrollArea( infoTabW );
    metaScrollArea->setObjectName( "metadataScrollArea" );
    metaScrollArea->setWidgetResizable( true );
    metaScrollArea->setFrameShape( QFrame::NoFrame );
    metaScrollArea->setWidget( MP );
    infoTabW->insertTab( META_PANEL, metaScrollArea, qtr( "&General" ) );
    EMP = new ExtraMetaPanel( infoTabW );
    infoTabW->insertTab( EXTRAMETA_PANEL, EMP, qtr( "&Metadata" ) );
    IP = new InfoPanel( infoTabW );
    infoTabW->insertTab( INFO_PANEL, IP, qtr( "Co&dec" ) );
    if( isMainInputInfo )
    {
        ISP = new InputStatsPanel( infoTabW, p_intf->p_mi );
        infoTabW->insertTab( INPUTSTATS_PANEL, ISP, qtr( "S&tatistics" ) );
    }

    auto *layout = new QVBoxLayout( this );
    auto *heading = new QLabel( qtr("Media information"), this );
    heading->setProperty( "redesignHeading", true );
    heading->setObjectName( "mediaInfoHeading" );
    heading->setWordWrap( true );
    layout->addWidget( heading );
    layout->addWidget( infoTabW, 1 );

    auto *locationLayout = new QHBoxLayout;
    locationLayout->setObjectName(QStringLiteral("mediaInfoLocationRow"));
    auto *uriLabel = new QLabel( qtr("Location:"), this );
    uriLine = new QLineEdit( this );
    uriLine->setObjectName( "mediaInfoLocation" );
    uriLine->setReadOnly( true );
    uriLabel->setBuddy( uriLine );
    locationLayout->addWidget( uriLabel );
    locationLayout->addWidget( uriLine, 1 );
    layout->addLayout( locationLayout );

    auto *actions = new QHBoxLayout;
    actions->setObjectName(QStringLiteral("mediaInfoActions"));
    editingStatus = new QLabel( this );
    editingStatus->setObjectName( "metadataEditStatus" );
    editingStatus->setWordWrap( true );
    actions->addWidget( editingStatus, 1 );
    saveMetaButton = new QPushButton( qtr("&Save"), this );
    saveMetaButton->setObjectName( "saveMetadataButton" );
    closeButton = new QPushButton( qtr("&Close"), this );
    closeButton->setObjectName( "closeMediaInfoButton" );
    actions->addWidget( saveMetaButton );
    actions->addWidget( closeButton );
    layout->addLayout( actions );
    connect( closeButton, &QPushButton::clicked, this, &MediaInfoDialog::close );
    connect( saveMetaButton, &QPushButton::clicked, this, &MediaInfoDialog::saveMeta );
    connect( MP, &MetaPanel::uriSet, this, &MediaInfoDialog::updateURI );
    const auto updateEditingContext = [this](bool) {
        const bool wasLocked = inputContextLocked;
        inputContextLocked = MP->isInputLocked();
        if (inputContextLocked && !wasLocked && displayedInput)
        {
            IP->update(displayedInput.get());
            EMP->update(displayedInput.get());
        }
        else if (!inputContextLocked)
            refreshCurrentInput();
        updateButtons();
    };
    connect(MP, &MetaPanel::editModeChanged, this, updateEditingContext);
    connect(MP, &MetaPanel::lookupInProgressChanged, this, updateEditingContext);

    /* If using the General Mode */
    if( isMainInputInfo )
    {
        msg_Dbg( p_intf, "Using a general info windows" );
        /**
         * Connects on the various signals of input_Manager
         * For the currently playing element
         **/
        connect( THEMIM, &PlayerController::infoChanged, this, [this](input_item_t *item) {
            if (!MP->isInputLocked() && item == displayedInput.get()) IP->update(item);
        } );
        connect( THEMIM, &PlayerController::inputChanged, this, [this] {
            refreshCurrentInput();
            updateButtons();
        } );
        connect( THEMIM, &PlayerController::currentMetaChanged, this, [this](input_item_t *item) {
            if (MP->isInputLocked() || item != displayedInput.get()) return;
            MP->update(displayedInput);
            EMP->update(item);
        } );
        connect( THEMIM, &PlayerController::statisticsUpdated, this, [this](const input_stats_t &stats) {
            if (!MP->isInputLocked() && THEMIM->getInput() == displayedInput.get()) ISP->update(stats);
        } );
        connect(THEMIM, &PlayerController::playingStateChanged, this, [this](PlayerController::PlayingState state) {
            if (MP->isInputLocked() || THEMIM->getInput() != displayedInput.get()) return;
            if (state == PlayerController::PLAYING_STATE_STARTED) ISP->clear();
            if (state == PlayerController::PLAYING_STATE_STOPPED && displayedInput && displayedInput->p_stats)
                ISP->update(*displayedInput->p_stats);
            ISP->setStopped(state == PlayerController::PLAYING_STATE_STOPPED);
        });

        p_item = SharedInputItem{ THEMIM->getInput() };
    }
    else
        msg_Dbg( p_intf, "Using an item specific info windows" );

    /* Call update at start, so info is filled up at beginning */
    if( p_item )
        updateAllTabs( p_item );
    else
        clearAllTabs();

    const auto translate = [this, heading, uriLabel, metaScrollArea] {
        heading->setText(qtr("Media information"));
        infoTabW->setTabText(META_PANEL, qtr("&General"));
        infoTabW->setTabText(EXTRAMETA_PANEL, qtr("&Metadata"));
        infoTabW->setTabText(INFO_PANEL, qtr("Co&dec"));
        if (ISP) infoTabW->setTabText(INPUTSTATS_PANEL, qtr("S&tatistics"));
        saveMetaButton->setText(qtr("&Save"));
        uriLabel->setText(qtr("Location:"));
        metaScrollArea->setAccessibleName(qtr("General"));
        updateButtons();
    };
    translate();
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, translate);

    applyRedesignWidgetTheme(this, p_intf->p_mi);
    layoutReady = true;
    updateMetrics();
    trackScrollFocus(this, p_intf->p_mi);
    if (p_intf->p_mi) connect(p_intf->p_mi, &MainCtx::intfScaleFactorChanged, this, &MediaInfoDialog::updateMetrics);
    const double factor = p_intf->p_mi ? p_intf->p_mi->getIntfScaleFactor() : 1.;
    QSize initial(qRound(760 * factor), qRound(860 * factor));
    if (screen()) initial = initial.boundedTo(screen()->availableGeometry().size() - QSize(32, 32));
    restoreWidgetPosition( "Mediainfo", initial );
}

MediaInfoDialog::~MediaInfoDialog()
{
    saveWidgetPosition( "Mediainfo" );
}

void MediaInfoDialog::updateMetrics()
{
    if (!layoutReady) return;
    const auto dp = [this](int n) { return qRound(n * (p_intf->p_mi ? p_intf->p_mi->getIntfScaleFactor() : 1.)); };
    const bool compact = height() < dp(700);
    const int margin = dp(compact ? 16 : 24);
    layout()->setContentsMargins(margin, margin, margin, margin);
    layout()->setSpacing(dp(compact ? 8 : 16));
    findChild<QHBoxLayout*>(QStringLiteral("mediaInfoLocationRow"))->setSpacing(dp(8));
    findChild<QHBoxLayout*>(QStringLiteral("mediaInfoActions"))->setSpacing(dp(8));
    uriLine->setFixedHeight(dp(37));
    for (auto *button : {saveMetaButton, closeButton}) {
        button->setFixedHeight(dp(32));
        button->setMinimumWidth(dp(100));
    }
    for (auto *panel : {static_cast<QWidget *>(EMP), static_cast<QWidget *>(IP), static_cast<QWidget *>(ISP)})
        if (panel && panel->layout()) panel->layout()->setContentsMargins(0, dp(compact ? 8 : 16), 0, 0);
}

void MediaInfoDialog::resizeEvent(QResizeEvent *event)
{
    QVLCFrame::resizeEvent(event);
    updateMetrics();
}

void MediaInfoDialog::showTab( panel i_tab = META_PANEL )
{
    if( isMainInputInfo )
    {
        const SharedInputItem input { THEMIM->getInput() };
        if( input )
            updateAllTabs( input );
        else
            clearAllTabs();
    }
    infoTabW->setCurrentIndex( i_tab );
    updateButtons();
    show();
}

int MediaInfoDialog::currentTab()
{
    return infoTabW->currentIndex();
}

void MediaInfoDialog::saveMeta()
{
    QPointer<MediaInfoDialog> owner(this);
    if (MP->isInEditMode()) MP->saveMeta();
    if (owner) updateButtons();
}

void MediaInfoDialog::updateAllTabs( const SharedInputItem& p_item )
{
    if (!p_item || MP->isInputLocked()) return;
    if (isMainInputInfo && displayedInput.get() != p_item.get()) ISP->clear();
    displayedInput = p_item;
    IP->update( p_item.get() );
    MP->update( p_item );
    EMP->update( p_item.get() );

    if (isMainInputInfo)
    {
        if (p_item->p_stats) ISP->update(*p_item->p_stats);
        ISP->setStopped(THEMIM->getPlayingState() == PlayerController::PLAYING_STATE_STOPPED);
    }
}

void MediaInfoDialog::clearAllTabs()
{
    if (MP->isInputLocked()) return;
    displayedInput.reset();
    IP->clear();
    MP->clear();
    EMP->clear();

    if( isMainInputInfo ) ISP->clear();
}

void MediaInfoDialog::close()
{
    hide();
    if( !isMainInputInfo )
        deleteLater();
}

void MediaInfoDialog::hideEvent( QHideEvent *event )
{
    MP->discardEdits();
    updateButtons();
    QVLCFrame::hideEvent( event );
}

void MediaInfoDialog::refreshCurrentInput()
{
    if (!isMainInputInfo || MP->isInputLocked()) return;
    const SharedInputItem input { THEMIM->getInput() };
    if (input) updateAllTabs(input);
    else clearAllTabs();
}

void MediaInfoDialog::updateButtons()
{
    const bool editing = MP->isInEditMode();
    const bool pendingInput = isMainInputInfo && displayedInput.get() != THEMIM->getInput();
    saveMetaButton->setEnabled(MP->canSaveMeta());
    saveMetaButton->setDefault(MP->canSaveMeta());
    closeButton->setText(editing ? qtr("&Cancel") : qtr("&Close"));
    closeButton->setDefault(!editing);
    const auto help = !displayedInput ? qtr("Open a file to view its information.")
        : pendingInput ? qtr("Save or cancel these changes to show the current media.")
        : editing ? qtr("Changes are not saved.") : QString();
    editingStatus->setText(!displayedInput ? qtr("No media")
        : !editing ? QString() : pendingInput ? qtr("Previous media") : qtr("Not saved"));
    editingStatus->setToolTip(help);
    editingStatus->setAccessibleDescription(help);
    setWindowTitle(isMainInputInfo && !pendingInput ? qtr("Current Media Information") : qtr("Media Information"));
}

bool MediaInfoDialog::event( QEvent *event )
{
    // The main interface has an application-wide Ctrl+S streaming shortcut.
    // Let this editor own the platform's Save key while its window has focus.
    if (event->type() == QEvent::ShortcutOverride
        && static_cast<QKeyEvent *>(event)->matches(QKeySequence::Save))
    {
        event->accept();
        return true;
    }
    return QVLCFrame::event(event);
}

void MediaInfoDialog::keyPressEvent( QKeyEvent *event )
{
    if (event->matches(QKeySequence::Save))
    {
        saveMeta();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
    {
        if (auto *button = qobject_cast<QPushButton *>(focusWidget()); button && button->isEnabled())
            button->click();
        else if (MP->isInEditMode())
            saveMeta();
        else
            close();
        event->accept();
        return;
    }
    QVLCFrame::keyPressEvent(event);
}

void MediaInfoDialog::updateURI( const QString& uri )
{
    QString location;

    /* If URI points to a local file, show the path instead of the URI */
    char *path = vlc_uri2path( qtu( uri ) );
    if( path != NULL )
    {
        location = qfu( path );
        free( path );
    }
    else
        location = uri;

    uriLine->setText( location );
}
