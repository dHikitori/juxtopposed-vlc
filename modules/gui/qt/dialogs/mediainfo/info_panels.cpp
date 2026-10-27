/*****************************************************************************
 * info_panels.cpp : Panels for the information dialogs
 ****************************************************************************
 * Copyright (C) 2006-2007 the VideoLAN team
 *
 * Authors: Clément Stenac <zorglub@videolan.org>
 *          Jean-Baptiste Kempf <jb@videolan.org>
 *          Ilkka Ollakka <ileoo@videolan.org>
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

#include "qt.hpp"
#include "info_panels.hpp"
#include "dialogs/dialogs_provider.hpp"
#include "player/player_controller.hpp"
#include "maininterface/mainctx.hpp"
#include "widgets/native/interface_widgets.hpp"
#include "info_widgets.hpp"
#include "dialogs/fingerprint/fingerprintdialog.hpp"
#include "dialogs/fingerprint/chromaprint.hpp"

#include <cassert>
#include <vlc_url.h>
#include <vlc_artwork_policy.h>
#include <vlc_meta.h>
#include <vlc_input_item.h>
#include <vlc_list.hpp>

#include "util/interface_translator.hpp"
#include "util/artwork_approval.hpp"

#include <QTreeWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QStringList>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QResizeEvent>
#include <QLineEdit>
#include <QLabel>
#include <QTextEdit>
#include <QApplication>
#include <QPushButton>
#include <QRegularExpression>
#include <QMessageBox>
#include <QEvent>
#include <QClipboard>
#include <QFrame>
#include <QScrollArea>
#include <QLocale>
#include <cmath>
#include <QKeyEvent>
#include <QShortcut>
#include <QScrollBar>
#include <QSet>
#include <algorithm>

namespace {
QLabel* translatedLabel(const char* source, QWidget* parent)
{
    auto* label = new QLabel(qtr(source), parent);
    if (auto* translator = InterfaceTranslator::instance())
        QObject::connect(translator, &InterfaceTranslator::languageChanged, label,
                         [label, source] { label->setText(qtr(source)); });
    return label;
}
}

/************************************************************************
 * Single panels
 ************************************************************************/

/**
 * First Panel - Meta Info
 * All the usual MetaData are displayed and can be changed.
 **/
MetaPanel::MetaPanel( QWidget *parent,
                      qt_intf_t *_p_intf )
                      : QWidget( parent ), p_intf( _p_intf )
{
    setObjectName( "metadataForm" );
    metaLayout = new QGridLayout( this );
    const auto addField = [this](const char *source, const char *name, QLineEdit *&field) {
        auto *block = new QWidget( this );
        auto *layout = new QVBoxLayout( block );
        layout->setContentsMargins( 0, 0, 0, 0 );
        auto *label = translatedLabel( source, block );
        field = new QLineEdit( block );
        field->setObjectName( QLatin1String(name) );
        label->setBuddy( field );
        label->setWordWrap( true );
        layout->addWidget( label );
        layout->addWidget( field );
        connect( field, &QLineEdit::textEdited, this, &MetaPanel::enterEditMode );
        return block;
    };
    const auto field = [&](vlc_meta_type_t type, const char *name, QLineEdit *&edit) {
        return addField(vlc_meta_TypeToString(type), name, edit);
    };
    auto *title = field( vlc_meta_Title, "metadataTitle", title_text );
    auto *artist = field( vlc_meta_Artist, "metadataArtist", artist_text );
    auto *album = field( vlc_meta_Album, "metadataAlbum", collection_text );
    auto *date = field( vlc_meta_Date, "metadataDate", date_text );
    // Tags can contain a year or a full release date. A year mask loses data
    // even when the user only changes a different field.
    auto *genre = field( vlc_meta_Genre, "metadataGenre", genre_text );

    auto *track = new QWidget( this );
    auto *trackLayout = new QVBoxLayout( track );
    trackLayout->setContentsMargins( 0, 0, 0, 0 );
    auto *trackLabel = translatedLabel( vlc_meta_TypeToString(vlc_meta_TrackNumber), track );
    auto *trackFields = new QHBoxLayout;
    seqnum_text = new QLineEdit( track );
    seqnum_text->setObjectName( "metadataTrackNumber" );
    seqtot_text = new QLineEdit( track );
    seqtot_text->setObjectName( "metadataTrackTotal" );
    trackLabel->setBuddy( seqnum_text );
    trackFields->addWidget( seqnum_text );
    trackFields->addWidget( new QLabel( QStringLiteral("/"), track ) );
    trackFields->addWidget( seqtot_text );
    trackLayout->addWidget( trackLabel );
    trackLayout->addLayout( trackFields );
    connect( seqnum_text, &QLineEdit::textEdited, this, &MetaPanel::enterEditMode );
    connect( seqtot_text, &QLineEdit::textEdited, this, &MetaPanel::enterEditMode );

    auto *language = field( vlc_meta_Language, "metadataLanguage", language_text );
    auto *publisher = field( vlc_meta_Publisher, "metadataPublisher", publisher_text );
    auto *copyright = field( vlc_meta_Copyright, "metadataCopyright", copyright_text );
    auto *encodedBy = field( vlc_meta_EncodedBy, "metadataEncodedBy", encodedby_text );
    auto *nowPlaying = field( vlc_meta_NowPlaying, "metadataNowPlaying", nowplaying_text );
    nowplaying_text->setReadOnly( true );

    commentsBlock = new QWidget( this );
    auto *commentsLayout = new QVBoxLayout( commentsBlock );
    commentsLayout->setContentsMargins( 0, 0, 0, 0 );
    auto *commentsLabel = translatedLabel( N_("Comments"), commentsBlock );
    description_text = new QTextEdit( commentsBlock );
    description_text->setObjectName( "metadataComments" );
    description_text->setAcceptRichText( false );
    commentsLabel->setBuddy( description_text );
    commentsLayout->addWidget( commentsLabel );
    commentsLayout->addWidget( description_text );
    connect( description_text, &QTextEdit::textChanged, this, &MetaPanel::enterEditMode );

    auto *cover = new QWidget( this );
    auto *coverLayout = new QVBoxLayout( cover );
    coverLayout->setContentsMargins( 0, 0, 0, 0 );
    coverLayout->addWidget( translatedLabel(N_("Cover art"), cover) );
    art_cover = new CoverArtLabel( cover, p_intf );
    art_cover->setObjectName( "metadataCover" );
    coverLayout->addWidget( art_cover, 0, Qt::AlignHCenter );
    changeCoverButton = new QPushButton( qtr("Change cover"), cover );
    changeCoverButton->setObjectName( "metadataChangeCover" );
    coverLayout->addWidget( changeCoverButton, 0, Qt::AlignHCenter );
    connect( changeCoverButton, &QPushButton::clicked, art_cover, &CoverArtLabel::setArtFromFile );
    fingerprintButton = new QPushButton( qtr("Find tags"), cover );
    fingerprintButton->setObjectName( "metadataFingerprint" );
    fingerprintButton->setToolTip( qtr("Find tags for this audio file.") );
    fingerprintButton->setEnabled( false );
    coverLayout->addWidget( fingerprintButton, 0, Qt::AlignHCenter );
    coverLayout->addStretch();
    connect( fingerprintButton, &QPushButton::clicked, this, &MetaPanel::fingerprint );
    connect( art_cover, &CoverArtLabel::editing, this, &MetaPanel::enterEditMode );
    connect(art_cover, &CoverArtLabel::artSelected, this, [this](const QString& url) { currentURL = url; artworkSelectedByUser = true; });

    fieldRows = {{title, artist}, {album, date}, {genre, track}, {language, publisher},
                 {copyright, encodedBy}, {nowPlaying, nullptr}, {commentsBlock, cover}};
    const auto translate = [this] {
        seqtot_text->setAccessibleName( qtr(vlc_meta_TypeToString(vlc_meta_TrackTotal)) );
        changeCoverButton->setText( qtr("Change cover") );
        fingerprintButton->setText( qtr("Find tags") );
        fingerprintButton->setToolTip( qtr("Find tags for this audio file.") );
    };
    translate();
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, translate);
    if (p_intf->p_mi)
        connect(p_intf->p_mi, &MainCtx::intfScaleFactorChanged, this, &MetaPanel::updateLayout);
    updateLayout();
    setEnabled( false );
}

void MetaPanel::resizeEvent( QResizeEvent *event )
{
    QWidget::resizeEvent( event );
    updateLayout();
}

void MetaPanel::updateLayout()
{
    const auto dp = [this](int value) { return qRound(value * (p_intf->p_mi ? p_intf->p_mi->getIntfScaleFactor() : 1.)); };
    const bool compact = width() < dp(560);
    while (auto *item = metaLayout->takeAt(0)) delete item;
    for (int row = 0; row < metaLayout->rowCount(); ++row) metaLayout->setRowStretch(row, 0);
    metaLayout->setContentsMargins(0, dp(16), 0, 0);
    metaLayout->setHorizontalSpacing(dp(16));
    metaLayout->setVerticalSpacing(dp(16));
    metaLayout->setColumnStretch(0, compact ? 1 : 3);
    metaLayout->setColumnStretch(1, compact ? 0 : 2);
    int row = 0;
    for (const auto &fields : fieldRows)
    {
        if (!fields.first->isHidden())
        {
            metaLayout->addWidget(fields.first, row, 0, 1, compact || !fields.second ? 2 : 1);
            if (fields.first == commentsBlock) metaLayout->setRowStretch(row, 1);
            if (compact || !fields.second) ++row;
        }
        if (fields.second) metaLayout->addWidget(fields.second, row++, compact ? 0 : 1, 1, compact ? 2 : 1);
    }
    for (auto *layout : findChildren<QLayout *>())
        if (layout != metaLayout) layout->setSpacing(dp(6));
    for (auto *edit : findChildren<QLineEdit *>()) edit->setFixedHeight(dp(37));
    description_text->setMinimumHeight(dp(120));
    changeCoverButton->setFixedHeight(dp(32));
    fingerprintButton->setFixedHeight(dp(32));
    changeCoverButton->setMinimumWidth(dp(128));
    fingerprintButton->setMinimumWidth(dp(128));

}

/**
 * Update all the MetaData and art
 **/
void MetaPanel::update( const SharedInputItem& p_item )
{
    input_item_t * const inputItem = p_item.get();

    if( !inputItem )
    {
        clear();
        return;
    }

    /* Don't update if you are in edit mode */
    if( isInputLocked() ) return;
    p_input = p_item;
    setEnabled( true );

    char *psz_meta;
#define UPDATE_META( meta, widget ) {                                   \
    psz_meta = input_item_Get##meta( inputItem );                       \
    widget->setText( !EMPTY_STR( psz_meta ) ? qfu( psz_meta ) : "" );   \
    free( psz_meta ); }

#define UPDATE_META_INT( meta, widget ) {            \
    psz_meta = input_item_Get##meta( inputItem );    \
    if( !EMPTY_STR( psz_meta ) )                     \
        widget->setValue( atoi( psz_meta ) ); }      \
    free( psz_meta );

    /* Name / Title */
    psz_meta = input_item_GetTitleFbName( inputItem );
    if( psz_meta )
    {
        title_text->setText( qfu( psz_meta ) );
        free( psz_meta );
    }
    else
        title_text->setText( "" );

    /* URL / URI */
    psz_meta = input_item_GetURI( inputItem );
    if( !EMPTY_STR( psz_meta ) )
        emit uriSet( qfu( psz_meta ) );
    fingerprintButton->setEnabled( Chromaprint::isSupported( QString( psz_meta ) ) );
    free( psz_meta );

    /* Other classic though */
    UPDATE_META( Artist, artist_text );
    UPDATE_META( Genre, genre_text );
    UPDATE_META( Copyright, copyright_text );
    UPDATE_META( Album, collection_text );
    disconnect( description_text, &QTextEdit::textChanged, this, &MetaPanel::enterEditMode );
    psz_meta = input_item_GetDescription( inputItem );
    description_text->setPlainText( !EMPTY_STR(psz_meta) ? qfu(psz_meta) : QString() );
    free( psz_meta );
    connect( description_text, &QTextEdit::textChanged, this, &MetaPanel::enterEditMode );
    UPDATE_META( Language, language_text );
    UPDATE_META( Publisher, publisher_text );
    UPDATE_META( EncodedBy, encodedby_text );

    UPDATE_META( Date, date_text );
    UPDATE_META( TrackNum, seqnum_text );
    UPDATE_META( TrackTotal, seqtot_text );

    /* Now Playing || ES Now Playing */
    psz_meta = input_item_GetNowPlayingFb( inputItem );
    if( !EMPTY_STR( psz_meta ) )
        nowplaying_text->setText( qfu( psz_meta ) );
    else
        nowplaying_text->clear();
    free( psz_meta );
    nowplaying_text->parentWidget()->setVisible( !nowplaying_text->text().isEmpty() );
    updateLayout();

#undef UPDATE_META_INT
#undef UPDATE_META

    art_cover->setItem( p_item );
    char *psz_art = vlc_artwork_item_GetURI( inputItem );
    currentURL = qfu( psz_art ? psz_art : "" );
    artworkSelectedByUser = false;
    free( psz_art );
}

/**
 * Save the MetaData, triggered by parent->save Button
 **/
bool MetaPanel::saveMeta()
{
    if (lookupInProgress) return false;
    const auto input = p_input.get();

    if( input == NULL ) {
        DialogsProvider::getInstance()->messageDialog(qtr("Open a file before you save its details."),
            qtr("Save file details"), QMessageBox::Close, QMessageBox::Close, QMessageBox::Warning, this);
        return false;
    }

    const auto warn = [this, input] {
        const auto uri = vlc::wrap_cptr(input_item_GetURI(input));
        const QUrl url = QUrl::fromEncoded(QByteArray(uri ? uri.get() : ""));
        const QString location = url.isLocalFile() ? url.toLocalFile() : url.toDisplayString(QUrl::RemovePassword);
        DialogsProvider::getInstance()->messageDialog(
            qtr("VLC cannot save the file details.\n\nLocation: %1\n\nYour changes remain in this window.")
                .arg(location), qtr("Save file details"), QMessageBox::Close, QMessageBox::Close,
            QMessageBox::Warning, this);
    };
    // Keep the live item unchanged until the file write succeeds.
    const SharedInputItem edited { input_item_Copy( input ), false };
    if( !edited )
    {
        warn();
        return false;
    }
    const auto applyEdits = [this]( input_item_t *item ) {
        if (pendingTags)
        {
            vlc_mutex_locker lock(&item->lock);
            if (!item->p_meta) item->p_meta = vlc_meta_New();
            if (item->p_meta) vlc_meta_Merge(item->p_meta, pendingTags->p_meta);
        }
        input_item_SetTitle( item, qtu( title_text->text() ) );
        input_item_SetArtist( item, qtu( artist_text->text() ) );
        input_item_SetAlbum( item, qtu( collection_text->text() ) );
        input_item_SetGenre( item, qtu( genre_text->text() ) );
        input_item_SetTrackNum( item, qtu( seqnum_text->text() ) );
        input_item_SetTrackTotal( item, qtu( seqtot_text->text() ) );
        input_item_SetDate( item, qtu( date_text->text() ) );
        input_item_SetLanguage( item, qtu( language_text->text() ) );
        input_item_SetCopyright( item, qtu( copyright_text->text() ) );
        input_item_SetPublisher( item, qtu( publisher_text->text() ) );
        input_item_SetDescription( item, qtu( description_text->toPlainText() ) );
        input_item_SetEncodedBy( item, qtu( encodedby_text->text() ) );
        if (!currentURL.isEmpty() && artworkSelectedByUser) {
            if (!ArtworkApproval::userFile(item, QUrl(currentURL))) return false;
        }
        return true;
    };
    if (!applyEdits(edited.get())) { warn(); return false; }
    int vlcret = input_item_WriteMeta( VLC_OBJECT(p_intf), edited.get() );
    if (vlcret != VLC_SUCCESS) {
        warn();
        return false;
    }
    if (!applyEdits(input)) { warn(); return false; }
    artworkSelectedByUser = false;
    pendingTags.reset();
    art_cover->setItem( p_input );
    /* Refresh the player controller caches for the currently playing item so
     * the Qt UI sees the new artwork and metadata without reopening. */
    THEMIM->refreshMediaMeta( input );
    setEditMode( false );
    return true;
}


bool MetaPanel::isInEditMode()
{
    return b_inEditMode;
}

void MetaPanel::enterEditMode()
{
    setEditMode( true );
}

void MetaPanel::discardEdits()
{
    if (fingerprintDialog) fingerprintDialog->reject();
    pendingTags.reset();
    if( !b_inEditMode )
        return;
    b_inEditMode = false;
    update( p_input );
    emit editModeChanged( false );
}

void MetaPanel::setEditMode( bool b_editing )
{
    if( b_inEditMode == b_editing ) return;
    b_inEditMode = b_editing;
    emit editModeChanged( b_editing );
}

/*
 * Clear all the metadata widgets
 */
void MetaPanel::clear()
{
    pendingTags.reset();
    p_input.reset();
    setEnabled( false );
    title_text->clear();
    artist_text->clear();
    genre_text->clear();
    copyright_text->clear();
    collection_text->clear();
    seqnum_text->clear();
    seqtot_text->clear();
    disconnect( description_text, &QTextEdit::textChanged, this, &MetaPanel::enterEditMode );
    description_text->clear();
    connect( description_text, &QTextEdit::textChanged, this, &MetaPanel::enterEditMode );
    date_text->clear();
    language_text->clear();
    nowplaying_text->clear();
    publisher_text->clear();
    encodedby_text->clear();
    art_cover->clear();
    currentURL.clear();
    artworkSelectedByUser = false;
    fingerprintButton->setEnabled( false );

    nowplaying_text->parentWidget()->hide();
    updateLayout();
    setEditMode( false );
    emit uriSet( "" );
}

void MetaPanel::fingerprint()
{
    if (!p_input) return;
    if (fingerprintDialog)
    {
        fingerprintDialog->raise();
        fingerprintDialog->activateWindow();
        return;
    }
    lookupInProgress = true;
    emit lookupInProgressChanged(true);
    auto *dialog = new FingerprintDialog(this, p_intf, p_input.get());
    fingerprintDialog = dialog;
    connect(dialog, &FingerprintDialog::tagsSelected, this, [this, source = p_input](const SharedInputItem &tags) {
        if (source == p_input) fingerprintUpdate(tags);
    });
    connect(dialog, &QDialog::finished, this, [this] {
        fingerprintDialog.clear();
        lookupInProgress = false;
        emit lookupInProgressChanged(false);
    });
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->show();
}

void MetaPanel::fingerprintUpdate( const SharedInputItem& tags )
{
    if (!tags) return;
    pendingTags = tags;
    const QPair<vlc_meta_type_t, QLineEdit *> fields[] = {
        {vlc_meta_Title, title_text}, {vlc_meta_Artist, artist_text},
        {vlc_meta_Album, collection_text}, {vlc_meta_Date, date_text},
        {vlc_meta_Genre, genre_text}, {vlc_meta_TrackNumber, seqnum_text},
        {vlc_meta_TrackTotal, seqtot_text}, {vlc_meta_Language, language_text},
        {vlc_meta_Publisher, publisher_text}, {vlc_meta_Copyright, copyright_text},
        {vlc_meta_EncodedBy, encodedby_text}
    };
    // Only replace fields supplied by the match. Other unsaved edits stay.
    for (const auto &field : fields)
        if (const auto *value = vlc_meta_Get(tags->p_meta, field.first)) field.second->setText(qfu(value));
    if (const auto *value = vlc_meta_Get(tags->p_meta, vlc_meta_Description))
        description_text->setPlainText(qfu(value));
    setEditMode(true);
}

/**
 * Second Panel - Shows the extra metadata in a table, non editable.
 **/
ExtraMetaPanel::ExtraMetaPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("extraMetadata"));
    auto *layout = new QVBoxLayout(this);
    auto *tools = new QHBoxLayout;
    filter = new QLineEdit(this);
    filter->setObjectName(QStringLiteral("extraMetadataFilter"));
    filter->setClearButtonEnabled(true);
    filter->installEventFilter(this);
    copyButton = new QPushButton(this);
    copyButton->setObjectName(QStringLiteral("copyMetadataButton"));
    copyButton->installEventFilter(this);
    tools->addWidget(filter, 1);
    tools->addWidget(copyButton);
    layout->addLayout(tools);

    extraMeta = new QTableWidget(this);
    extraMeta->setObjectName(QStringLiteral("extraMetadataTable"));
    extraMeta->setColumnCount(2);
    extraMeta->verticalHeader()->hide();
    extraMeta->setAlternatingRowColors(true);
    extraMeta->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    extraMeta->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    extraMeta->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    extraMeta->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    extraMeta->setSelectionBehavior(QAbstractItemView::SelectRows);
    extraMeta->setSelectionMode(QAbstractItemView::ExtendedSelection);
    extraMeta->setEditTriggers(QAbstractItemView::NoEditTriggers);
    extraMeta->setWordWrap(false);
    extraMeta->setTextElideMode(Qt::ElideRight);
    extraMeta->setSortingEnabled(true);
    extraMeta->sortItems(0, Qt::AscendingOrder);
    extraMeta->installEventFilter(this);
    layout->addWidget(extraMeta, 1);
    emptyMessage = new QLabel(extraMeta->viewport());
    emptyMessage->setObjectName(QStringLiteral("extraMetadataEmpty"));
    emptyMessage->setTextFormat(Qt::PlainText);
    emptyMessage->setWordWrap(true);
    emptyMessage->setAlignment(Qt::AlignCenter);
    emptyMessage->setAttribute(Qt::WA_TransparentForMouseEvents);
    extraMeta->viewport()->installEventFilter(this);
    connect(filter, &QLineEdit::textChanged, this, &ExtraMetaPanel::filterMetadata);
    connect(copyButton, &QPushButton::clicked, this, [this] { copyMetadata(); });
    auto *find = new QShortcut(QKeySequence::Find, this);
    find->setContext(Qt::WidgetWithChildrenShortcut);
    connect(find, &QShortcut::activated, this, [this] { filter->setFocus(); filter->selectAll(); });
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, &ExtraMetaPanel::retranslate);
    retranslate();
}

void ExtraMetaPanel::retranslate()
{
    extraMeta->setAccessibleName(qtr("Metadata"));
    extraMeta->setHorizontalHeaderLabels({qtr("Property"), qtr("Value")});
    filter->setPlaceholderText(qtr("Search metadata"));
    filter->setAccessibleName(qtr("Search metadata"));
    copyButton->setText(qtr("Copy metadata"));
    filterMetadata();
}

void ExtraMetaPanel::update(input_item_t *item)
{
    if (!item) { clear(); return; }
    QVector<QPair<QString, QString>> values;
    {
        vlc_mutex_locker locker(&item->lock);
        if (const auto *meta = item->p_meta)
        {
            const auto append = [&values](const char *key, const char *value) {
                if (value) values.append({qfu(key), qfu(value)});
            };
            append(VLC_META_DISCNUMBER, vlc_meta_Get(meta, vlc_meta_DiscNumber));
            append(VLC_META_URL, vlc_meta_Get(meta, vlc_meta_URL));
            if (char **keys = vlc_meta_CopyExtraNames(meta))
            {
                for (int i = 0; keys[i]; ++i)
                {
                    append(keys[i], vlc_meta_GetExtra(meta, keys[i]));
                    free(keys[i]);
                }
                free(keys);
            }
        }
    }
    // Extra-name enumeration has no stable order. Compare a canonical snapshot
    // so repeated input updates do not reset the user's selection or scroll.
    std::sort(values.begin(), values.end());
    const bool sameInput = currentItem.get() == item;
    if (sameInput && values == metadata) return;
    if (!sameInput) filter->clear();
    currentItem = SharedInputItem(item);
    metadata = std::move(values);
    QSet<QString> selected;
    QString current;
    const int scroll = sameInput ? extraMeta->verticalScrollBar()->value() : 0;
    if (sameInput)
    {
        for (const auto &index : extraMeta->selectionModel()->selectedRows())
            selected.insert(extraMeta->item(index.row(), 0)->text());
        if (extraMeta->currentRow() >= 0)
            current = extraMeta->item(extraMeta->currentRow(), 0)->text();
    }
    extraMeta->setSortingEnabled(false);
    extraMeta->setRowCount(0);
    for (const auto &entry : metadata)
    {
        const int row = extraMeta->rowCount();
        extraMeta->insertRow(row);
        extraMeta->setItem(row, 0, new QTableWidgetItem(entry.first));
        extraMeta->setItem(row, 1, new QTableWidgetItem(entry.second));
        for (int column = 0; column < 2; ++column)
        {
            auto *cell = extraMeta->item(row, column);
            cell->setToolTip(QStringLiteral("<qt>%1</qt>")
                .arg(cell->text().toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br/>"))));
        }
        if (entry.first == current)
            extraMeta->setCurrentCell(row, 0, QItemSelectionModel::NoUpdate);
        if (selected.contains(entry.first))
            extraMeta->selectionModel()->select(extraMeta->model()->index(row, 0),
                                                QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }
    extraMeta->setSortingEnabled(true);
    filterMetadata();
    extraMeta->resizeRowsToContents();
    extraMeta->doItemsLayout();
    extraMeta->verticalScrollBar()->setValue(scroll);
}

void ExtraMetaPanel::filterMetadata()
{
    const QString query = filter->text().trimmed();
    bool any = false;
    for (int row = 0; row < extraMeta->rowCount(); ++row)
    {
        const bool match = query.isEmpty() || extraMeta->item(row, 0)->text().contains(query, Qt::CaseInsensitive)
            || extraMeta->item(row, 1)->text().contains(query, Qt::CaseInsensitive);
        extraMeta->setRowHidden(row, !match);
        any |= match;
    }
    copyButton->setEnabled(any);
    emptyMessage->setText(extraMeta->rowCount() ? qtr("No matches.") : qtr("No extra metadata is available."));
    emptyMessage->setGeometry(extraMeta->viewport()->rect().adjusted(16, 16, -16, -16));
    emptyMessage->setVisible(!any);
}

void ExtraMetaPanel::copyMetadata(bool selectionOnly)
{
    const auto selectedRows = extraMeta->selectionModel()->selectedRows();
    const bool selected = selectionOnly && std::any_of(selectedRows.cbegin(), selectedRows.cend(), [this](const auto &index) {
        return !extraMeta->isRowHidden(index.row());
    });
    QStringList lines;
    for (int row = 0; row < extraMeta->rowCount(); ++row)
        if (!extraMeta->isRowHidden(row) && (!selected || extraMeta->item(row, 0)->isSelected()))
            lines << extraMeta->item(row, 0)->text() + QStringLiteral(": ") + extraMeta->item(row, 1)->text();
    if (!lines.isEmpty()) QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
}

bool ExtraMetaPanel::eventFilter(QObject *watched, QEvent *event)
{
    if ((event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)
        && (watched == extraMeta || watched == filter || watched == copyButton)
        && static_cast<QKeyEvent *>(event)->matches(QKeySequence::Find))
    {
        if (event->type() == QEvent::KeyPress)
        {
            filter->setFocus();
            filter->selectAll();
        }
        event->accept();
        return true;
    }
    if (watched == extraMeta->viewport() && event->type() == QEvent::Resize)
        emptyMessage->setGeometry(extraMeta->viewport()->rect().adjusted(16, 16, -16, -16));
    if (watched == extraMeta && event->type() == QEvent::FontChange)
    {
        extraMeta->setColumnWidth(0, extraMeta->fontMetrics().horizontalAdvance(QStringLiteral("Metadata property"))
                                   + 3 * extraMeta->fontMetrics().height());
        extraMeta->resizeRowsToContents();
    }
    if (watched == filter && event->type() == QEvent::ShortcutOverride)
    {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter
            || (key->key() == Qt::Key_Escape && !filter->text().isEmpty()))
        {
            event->accept();
            return true;
        }
    }
    if (watched == extraMeta && event->type() == QEvent::ShortcutOverride
        && static_cast<QKeyEvent *>(event)->matches(QKeySequence::Copy))
    {
        event->accept();
        return true;
    }
    if (event->type() == QEvent::KeyPress)
    {
        auto *key = static_cast<QKeyEvent *>(event);
        if (watched == extraMeta && key->matches(QKeySequence::Copy))
        {
            copyMetadata(true);
            return true;
        }
        if (watched == filter && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter))
        {
            extraMeta->setFocus();
            return true;
        }
        if (watched == filter && key->key() == Qt::Key_Escape && !filter->text().isEmpty())
        {
            filter->clear();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ExtraMetaPanel::clear()
{
    currentItem.reset();
    metadata.clear();
    extraMeta->setRowCount(0);
    filter->clear();
    filterMetadata();
}

/**
 * Third panel - Stream info
 * Display all codecs and muxers info that we could gather.
 **/
InfoPanel::InfoPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("codecInformation"));
    auto *layout = new QVBoxLayout(this);
    auto *tools = new QHBoxLayout;
    filter = new QLineEdit(this);
    filter->setObjectName(QStringLiteral("codecInformationFilter"));
    filter->setClearButtonEnabled(true);
    filter->installEventFilter(this);
    copyButton = new QPushButton(this);
    copyButton->setObjectName(QStringLiteral("copyCodecInformationButton"));
    copyButton->installEventFilter(this);
    tools->addWidget(filter, 1);
    tools->addWidget(copyButton);
    layout->addLayout(tools);
    InfoTree = new QTreeWidget(this);
    InfoTree->setObjectName(QStringLiteral("codecInformationTree"));
    InfoTree->setColumnCount(2);
    InfoTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    InfoTree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    InfoTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    InfoTree->header()->setStretchLastSection(true);
    InfoTree->setTextElideMode(Qt::ElideRight);
    InfoTree->installEventFilter(this);
    layout->addWidget(InfoTree, 1);
    emptyMessage = new QLabel(InfoTree->viewport());
    emptyMessage->setObjectName(QStringLiteral("codecInformationEmpty"));
    emptyMessage->setTextFormat(Qt::PlainText);
    emptyMessage->setWordWrap(true);
    emptyMessage->setAlignment(Qt::AlignCenter);
    InfoTree->viewport()->installEventFilter(this);
    // Empty-state text overlays the unused view without changing its size.
    emptyMessage->setAttribute(Qt::WA_TransparentForMouseEvents);
    connect(filter, &QLineEdit::textChanged, this, &InfoPanel::filterInformation);
    connect(copyButton, &QPushButton::clicked, this, [this] { copyInformation(); });
    auto *find = new QShortcut(QKeySequence::Find, this);
    find->setContext(Qt::WidgetWithChildrenShortcut);
    connect(find, &QShortcut::activated, this, [this] { filter->setFocus(); filter->selectAll(); });
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, &InfoPanel::retranslate);
    retranslate();
}

void InfoPanel::retranslate()
{
    InfoTree->setAccessibleName(qtr("Codec"));
    InfoTree->setHeaderLabels({qtr("Property"), qtr("Value")});
    filter->setPlaceholderText(qtr("Search codec information"));
    filter->setAccessibleName(qtr("Search codec information"));
    copyButton->setText(qtr("Copy information"));
    filterInformation();
}

void InfoPanel::update(input_item_t *item)
{
    if (!item) { clear(); return; }
    const bool sameInput = currentItem.get() == item;
    if (!sameInput) filter->clear();
    currentItem = SharedInputItem(item);
    QSet<QString> collapsed;
    QVector<QStringList> selected;
    const int scroll = sameInput ? InfoTree->verticalScrollBar()->value() : 0;
    if (sameInput)
        for (int i = 0; i < InfoTree->topLevelItemCount(); ++i)
        {
            auto *category = InfoTree->topLevelItem(i);
            if (!category->isExpanded()) collapsed.insert(category->text(0));
            for (int j = 0; j < category->childCount(); ++j)
            {
                auto *row = category->child(j);
                if (row->isSelected()) selected.append({category->text(0), row->text(0), row->text(1)});
            }
        }
    InfoTree->clear();
    {
        vlc_mutex_locker locker(&item->lock);
        for (auto &category : vlc::from(item->categories, &info_category_t::node))
        {
            if (info_category_IsHidden(&category)) continue;
            auto *group = new QTreeWidgetItem(InfoTree, {qfu(category.psz_name)});
            group->setFirstColumnSpanned(true);
            QFont heading = InfoTree->font();
            heading.setWeight(QFont::DemiBold);
            group->setFont(0, heading);
            for (auto &info : vlc::from(category.infos, &info_t::node))
            {
                auto *row = new QTreeWidgetItem(group, {qfu(info.psz_name), qfu(info.psz_value)});
                for (int column = 0; column < 2; ++column)
                    row->setToolTip(column, QStringLiteral("<qt>%1</qt>").arg(row->text(column).toHtmlEscaped()));
                row->setSelected(selected.contains({group->text(0), row->text(0), row->text(1)}));
            }
            group->setExpanded(!collapsed.contains(group->text(0)));
        }
    }
    filterInformation();
    InfoTree->doItemsLayout();
    InfoTree->verticalScrollBar()->setValue(scroll);
}

void InfoPanel::filterInformation()
{
    const QString query = filter->text().trimmed();
    bool any = false;
    for (int i = 0; i < InfoTree->topLevelItemCount(); ++i)
    {
        auto *category = InfoTree->topLevelItem(i);
        bool visible = false;
        for (int j = 0; j < category->childCount(); ++j)
        {
            auto *row = category->child(j);
            const bool match = query.isEmpty() || category->text(0).contains(query, Qt::CaseInsensitive)
                || row->text(0).contains(query, Qt::CaseInsensitive) || row->text(1).contains(query, Qt::CaseInsensitive);
            row->setHidden(!match);
            visible |= match;
        }
        category->setHidden(!visible);
        if (visible && !query.isEmpty()) category->setExpanded(true);
        any |= visible;
    }
    copyButton->setEnabled(any);
    emptyMessage->setText(InfoTree->topLevelItemCount() ? qtr("No matches.") : qtr("No codec information is available."));
    emptyMessage->setGeometry(InfoTree->viewport()->rect().adjusted(16, 16, -16, -16));
    emptyMessage->setVisible(!any);
}

void InfoPanel::copyInformation(bool selectionOnly)
{
    QStringList result;
    const auto selectedItems = InfoTree->selectedItems();
    const bool selected = selectionOnly && std::any_of(selectedItems.cbegin(), selectedItems.cend(), [](auto *item) {
        return !item->isHidden() && (!item->parent() || !item->parent()->isHidden());
    });
    for (int i = 0; i < InfoTree->topLevelItemCount(); ++i)
    {
        auto *category = InfoTree->topLevelItem(i);
        if (category->isHidden()) continue;
        QStringList lines;
        for (int j = 0; j < category->childCount(); ++j)
        {
            auto *row = category->child(j);
            if (!row->isHidden() && (!selected || row->isSelected() || category->isSelected()))
                lines << row->text(0) + QStringLiteral(": ") + row->text(1);
        }
        if (!lines.isEmpty())
        {
            if (!result.isEmpty()) result << QString();
            result << category->text(0) << lines;
        }
    }
    if (!result.isEmpty()) QApplication::clipboard()->setText(result.join(QLatin1Char('\n')));
}

bool InfoPanel::eventFilter(QObject *watched, QEvent *event)
{
    if ((event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)
        && (watched == InfoTree || watched == filter || watched == copyButton)
        && static_cast<QKeyEvent *>(event)->matches(QKeySequence::Find))
    {
        if (event->type() == QEvent::KeyPress)
        {
            filter->setFocus();
            filter->selectAll();
        }
        event->accept();
        return true;
    }
    if (watched == InfoTree->viewport() && event->type() == QEvent::Resize)
        emptyMessage->setGeometry(InfoTree->viewport()->rect().adjusted(16, 16, -16, -16));
    if (watched == InfoTree && event->type() == QEvent::FontChange)
    {
        InfoTree->setIndentation(InfoTree->fontMetrics().height());
        InfoTree->setColumnWidth(0, InfoTree->fontMetrics().horizontalAdvance(QStringLiteral("Audio sampling rate"))
                                   + 3 * InfoTree->fontMetrics().height());
    }
    if (watched == filter && event->type() == QEvent::ShortcutOverride)
    {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter
            || (key->key() == Qt::Key_Escape && !filter->text().isEmpty()))
        {
            event->accept();
            return true;
        }
    }
    if (watched == InfoTree && event->type() == QEvent::ShortcutOverride
        && static_cast<QKeyEvent *>(event)->matches(QKeySequence::Copy))
    {
        event->accept();
        return true;
    }
    if (event->type() == QEvent::KeyPress)
    {
        auto *key = static_cast<QKeyEvent *>(event);
        if (watched == InfoTree && key->matches(QKeySequence::Copy))
        {
            copyInformation(true);
            return true;
        }
        if (watched == filter && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter))
        {
            InfoTree->setFocus();
            return true;
        }
        if (watched == filter && key->key() == Qt::Key_Escape && !filter->text().isEmpty())
        {
            filter->clear();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void InfoPanel::clear()
{
    currentItem.reset();
    InfoTree->clear();
    filter->clear();
    filterInformation();
}

/**
 * Fourth Panel - Stats
 * Displays the Statistics for reading/displaying in a tree
 */
InputStatsPanel::InputStatsPanel(QWidget *parent, MainCtx *context)
    : QWidget(parent), ctx(context)
{
    setObjectName(QStringLiteral("mediaStatistics"));
    auto *layout = new QVBoxLayout(this);
    auto *heading = new QHBoxLayout;
    summary = new QLabel(this);
    summary->setObjectName(QStringLiteral("statisticsStatus"));
    summary->setWordWrap(true);
    summary->setTextFormat(Qt::PlainText);
    copyButton = new QPushButton(this);
    copyButton->setObjectName(QStringLiteral("copyStatisticsButton"));
    heading->addWidget(summary, 1);
    heading->addWidget(copyButton, 0, Qt::AlignTop);
    layout->addLayout(heading);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("statisticsScrollArea"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(scroll);
    auto *body = new QVBoxLayout(content);
    body->setContentsMargins(0, 0, 0, 0);
    cards = new QGridLayout;
    body->addLayout(cards);
    body->addStretch(1);
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);

    const auto section = [this, content](const char *name, QLabel *&title) {
        auto *card = new QFrame(content);
        card->setObjectName(QLatin1String(name));
        card->setProperty("redesignCard", true);
        auto *box = new QVBoxLayout(card);
        title = new QLabel(card);
        title->setProperty("redesignSectionHeading", true);
        title->setTextFormat(Qt::PlainText);
        title->setWordWrap(true);
        box->addWidget(title);
        auto *grid = new QGridLayout;
        grid->setColumnStretch(0, 3);
        grid->setColumnStretch(1, 2);
        box->addLayout(grid);
        return card;
    };
    inputCard = section("statisticsInput", inputHeading);
    audioCard = section("statisticsAudio", audioHeading);
    videoCard = section("statisticsVideo", videoHeading);
    const auto row = [this](QWidget *card, const char *name, const char *unit, const char *objectName) {
        auto *grid = qobject_cast<QGridLayout *>(card->layout()->itemAt(1)->layout());
        auto *label = new QLabel(card);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(true);
        auto *value = new QLabel(card);
        value->setObjectName(QLatin1String(objectName));
        value->setProperty("redesignValue", true);
        value->setTextFormat(Qt::PlainText);
        value->setWordWrap(true);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        value->setFocusPolicy(Qt::StrongFocus);
        value->installEventFilter(this);
        const int index = grid->count() / 2;
        grid->addWidget(label, index, 0);
        grid->addWidget(value, index, 1);
        label->setBuddy(value);
        rows.append({name, unit, label, value,
                     card == inputCard ? inputHeading : card == audioCard ? audioHeading : videoHeading});
    };
    row(inputCard, N_("Media data size"), "KiB", "statisticsReadBytes");
    row(inputCard, N_("Input bitrate"), "kb/s", "statisticsInputBitrate");
    row(inputCard, N_("Demuxed data size"), "KiB", "statisticsDemuxBytes");
    row(inputCard, N_("Content bitrate"), "kb/s", "statisticsContentBitrate");
    row(inputCard, N_("Discarded (corrupted)"), "", "statisticsCorrupted");
    row(inputCard, N_("Dropped (discontinued)"), "", "statisticsDiscontinuity");
    row(audioCard, N_("Decoded"), N_("blocks"), "statisticsDecodedAudio");
    row(audioCard, N_("Played"), N_("buffers"), "statisticsPlayedAudio");
    row(audioCard, N_("Lost"), N_("buffers"), "statisticsLostAudio");
    row(videoCard, N_("Decoded"), N_("blocks"), "statisticsDecodedVideo");
    row(videoCard, N_("Displayed"), N_("frames"), "statisticsDisplayedVideo");
    row(videoCard, N_("Late"), N_("frames"), "statisticsLateVideo");
    row(videoCard, N_("Lost"), N_("frames"), "statisticsLostVideo");

    statsView = new VLCStatsView(inputCard);
    statsView->setObjectName(QStringLiteral("statisticsBitrateGraph"));
    inputCard->layout()->addWidget(statsView);
    graphLegend = new QLabel(inputCard);
    graphLegend->setWordWrap(true);
    inputCard->layout()->addWidget(graphLegend);
    for (auto *card : {inputCard, audioCard, videoCard})
        static_cast<QVBoxLayout *>(card->layout())->addStretch(1);

    connect(copyButton, &QPushButton::clicked, this, &InputStatsPanel::copyStatistics);
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, &InputStatsPanel::retranslate);
    if (ctx)
    {
        connect(ctx, &MainCtx::intfScaleFactorChanged, this, &InputStatsPanel::updateLayout);
        connect(ctx, &MainCtx::redesignAccentChanged, this, &InputStatsPanel::retranslate);
    }
    retranslate();
    updateLayout();
}

void InputStatsPanel::retranslate()
{
    inputHeading->setText(qtr("Input/Read"));
    audioHeading->setText(qtr("Audio"));
    videoHeading->setText(qtr("Video"));
    copyButton->setText(qtr("Copy statistics"));
    for (const auto &row : rows)
    {
        row.label->setText(qtr(row.name));
        row.value->setAccessibleName(row.heading->text() + QStringLiteral(": ") + qtr(row.name));
        QPalette colors = row.value->palette();
        colors.setColor(QPalette::Highlight, ctx ? ctx->redesignAccent() : window()->palette().color(QPalette::Highlight));
        colors.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#161616")));
        row.value->setPalette(colors);
    }
    statsView->setAccessibleName(qtr("Input bitrate"));
    const auto help = qtr("The dashed line shows the average of the values in the graph.");
    statsView->setToolTip(help);
    statsView->setAccessibleDescription(help);
    QPalette graphColors = palette();
    graphColors.setColor(QPalette::Highlight, ctx ? ctx->redesignAccent() : window()->palette().color(QPalette::Highlight));
    statsView->setPalette(graphColors);
    statsView->update();
    graphLegend->setText(QStringLiteral("<span style=\"color:%1\">● %2</span>　<span style=\"color:%3\">— %4</span>")
        .arg(graphColors.color(QPalette::Highlight).name(), QString(qtr("Input bitrate") + QStringLiteral(" (kb/s)")).toHtmlEscaped(),
             palette().color(QPalette::Text).name(), qtr("Average").toHtmlEscaped()));
    updateValues();
}

void InputStatsPanel::updateLayout()
{
    const auto dp = [this](int value) { return qRound(value * (ctx ? ctx->getIntfScaleFactor() : 1.)); };
    auto *outer = static_cast<QVBoxLayout *>(layout());
    outer->setSpacing(dp(12));
    outer->itemAt(0)->layout()->setSpacing(dp(12));
    copyButton->setFixedHeight(dp(32));
    statsView->setFixedHeight(dp(144));
    const bool compact = width() < dp(620);
    while (auto *item = cards->takeAt(0)) delete item;
    cards->setContentsMargins(0, 0, 0, 0);
    cards->setSpacing(dp(16));
    cards->setColumnStretch(0, 1);
    cards->setColumnStretch(1, compact ? 0 : 1);
    cards->addWidget(inputCard, 0, 0, compact ? 1 : 2, 1);
    cards->addWidget(audioCard, compact ? 1 : 0, compact ? 0 : 1);
    cards->addWidget(videoCard, compact ? 2 : 1, compact ? 0 : 1);
    for (auto *card : {inputCard, audioCard, videoCard})
    {
        card->layout()->setContentsMargins(dp(16), dp(16), dp(16), dp(16));
        card->layout()->setSpacing(dp(8));
        auto *grid = static_cast<QGridLayout *>(card->layout()->itemAt(1)->layout());
        grid->setHorizontalSpacing(dp(12));
        grid->setVerticalSpacing(dp(4));
        for (int row = 0; row < grid->rowCount(); ++row) grid->setRowMinimumHeight(row, dp(28));
    }
}

bool InputStatsPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)
        if (auto *label = qobject_cast<QLabel *>(watched);
            label && static_cast<QKeyEvent *>(event)->matches(QKeySequence::Copy))
        {
            if (event->type() == QEvent::KeyPress)
                QApplication::clipboard()->setText(label->hasSelectedText() ? label->selectedText() : label->text());
            event->accept();
            return true;
        }
    return QWidget::eventFilter(watched, event);
}

void InputStatsPanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateLayout();
}

void InputStatsPanel::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::LocaleChange)
        retranslate();
}

void InputStatsPanel::updateValues()
{
    summary->setText(!hasSnapshot ? qtr("Play a file to show its statistics.")
        : stopped ? qtr("Playback stopped. These are the final statistics.") : qtr("Media statistics"));
    copyButton->setEnabled(hasSnapshot);
    const auto integer = [this](uint64_t value) { return locale().toString(qulonglong(value)); };
    const auto rate = [this](float value) {
        return std::isfinite(value) && value >= 0 ? locale().toString(double(value) * 8000, 'f', 0)
                                                : QStringLiteral("—");
    };
    const QStringList values = {
        integer(snapshot.i_read_bytes / 1024), rate(snapshot.f_input_bitrate),
        integer(snapshot.i_demux_read_bytes / 1024), rate(snapshot.f_demux_bitrate),
        integer(snapshot.i_demux_corrupted), integer(snapshot.i_demux_discontinuity),
        integer(snapshot.i_decoded_audio), integer(snapshot.i_played_abuffers), integer(snapshot.i_lost_abuffers),
        integer(snapshot.i_decoded_video), integer(snapshot.i_displayed_pictures),
        integer(snapshot.i_late_pictures), integer(snapshot.i_lost_pictures)
    };
    for (qsizetype i = 0; i < rows.size(); ++i)
    {
        const auto &row = rows[i];
        row.value->setText(!hasSnapshot ? QStringLiteral("—") : values[i]
            + (*row.unit ? QStringLiteral(" ") + qtr(row.unit) : QString()));
    }
}

void InputStatsPanel::showSnapshot()
{
    updateValues();
    if (pendingGraphSample)
    {
        statsView->addValue(double(snapshot.f_input_bitrate) * 8000);
        pendingGraphSample = false;
    }
}

void InputStatsPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    showSnapshot();
}

void InputStatsPanel::update(const input_stats_t &stats)
{
    snapshot = stats;
    hasSnapshot = true;
    pendingGraphSample = true;
    if (isVisible()) showSnapshot();
}

void InputStatsPanel::setStopped(bool value)
{
    stopped = value;
    updateValues();
}

void InputStatsPanel::clear()
{
    stopped = false;
    snapshot = {};
    hasSnapshot = false;
    pendingGraphSample = false;
    statsView->reset();
    updateValues();
}

void InputStatsPanel::copyStatistics()
{
    if (!hasSnapshot) return;
    QStringList text;
    QLabel *heading = nullptr;
    for (const auto &row : rows)
    {
        if (row.heading != heading)
        {
            if (heading) text << QString();
            heading = row.heading;
            text << heading->text();
        }
        text << row.label->text() + QStringLiteral(": ") + row.value->text();
    }
    QApplication::clipboard()->setText(text.join(QLatin1Char('\n')));
}
