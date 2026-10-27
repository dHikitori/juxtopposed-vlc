/*****************************************************************************
 * interface_widgets.cpp : Custom widgets for the main interface
 ****************************************************************************
 * Copyright (C) 2006-2010 the VideoLAN team
 *
 * Authors: Clément Stenac <zorglub@videolan.org>
 *          Jean-Baptiste Kempf <jb@videolan.org>
 *          Rafaël Carré <funman@videolanorg>
 *          Ilkka Ollakka <ileoo@videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
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
#include "interface_widgets.hpp"
#include "util/artwork_approval.hpp"
#include "dialogs/dialogs_provider.hpp"
#include "player/player_controller.hpp"  /* Speed control */
#include "maininterface/mainctx.hpp" /* Interface integration */
#include "dialogs/mediainfo/info_panels.hpp"
#include "util/interface_translator.hpp"
#include "util/colorizedsvgicon.hpp"

#include <QApplication>
#include <algorithm>
#include <QAction>
#include <QUrl>
#include <QPainter>
#include <QPainterPath>
#include <vlc_input_item.h>

#include <math.h>
#include <cassert>

#include <vlc_player.h>

CoverArtLabel::CoverArtLabel( QWidget *parent, qt_intf_t *_p_i )
    : QLabel( parent ), p_intf( _p_i )
{
    // Only refresh this item's cover. A different playing item must not
    // replace the selected item's art or an unsaved cover choice.
    connect( THEMIM, QOverload<QString>::of(&PlayerController::artChanged), this, [this] {
        if (p_item.get() == THEMIM->getInput() && selectedArtPath.isEmpty()) showArtUpdate();
    } );
    connect( THEMIM, QOverload<input_item_t *>::of(&PlayerController::artChanged), this, [this](input_item_t *item) {
        if (item == p_item.get() && selectedArtPath.isEmpty()) showArtUpdate();
    } );
    setScaledContents( false );
    setAlignment( Qt::AlignCenter );
    auto *download = new QAction( qtr("Download cover art"), this );
    connect( download, &QAction::triggered, this, &CoverArtLabel::askForUpdate );
    addAction( download );
    auto *fromFile = new QAction( qtr("Add cover art from file"), this );
    connect( fromFile, &QAction::triggered, this, &CoverArtLabel::setArtFromFile );
    addAction( fromFile );
    const auto translate = [this, download, fromFile] {
        setAccessibleName(qtr("Cover art"));
        download->setText(qtr("Download cover art"));
        fromFile->setText(qtr("Add cover art from file"));
    };
    translate();
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, translate);
    const auto scale = [this] {
        const auto factor = p_intf->p_mi ? p_intf->p_mi->getIntfScaleFactor() : 1.;
        setFixedSize(qRound(128 * factor), qRound(128 * factor));
        update();
    };
    scale();
    if (p_intf->p_mi) connect(p_intf->p_mi, &MainCtx::intfScaleFactorChanged, this, scale);

    setItem( SharedInputItem{ THEMIM->getInput() } );
    if ( !p_item )
        showArtUpdate( );
}

CoverArtLabel::~CoverArtLabel()
{
    QList< QAction* > artActions = actions();
    foreach( QAction *act, artActions )
        removeAction( act );
}

void CoverArtLabel::setItem( const SharedInputItem& _p_item )
{
    selectedArtPath.clear();
    p_item = _p_item;

    if( p_item )
        setContextMenuPolicy( Qt::ActionsContextMenu );
    else
        setContextMenuPolicy( Qt::NoContextMenu );

    showArtUpdate( );
}

void CoverArtLabel::mouseDoubleClickEvent( QMouseEvent *event )
{
    if( ! p_item && qobject_cast<MetaPanel *>(this->window()) == NULL )
    {
        THEDP->mediaInfoDialog();
    }
    event->accept();
}

void CoverArtLabel::showArtUpdate( const QString& url )
{
    artwork = QPixmap();
    if (!url.isEmpty()) artwork.load(url);
    update();
}

void CoverArtLabel::showArtUpdate()
{
    showArtUpdate(p_item ? THEMIM->decodeArtURL(p_item.get()) : QString());
}

void CoverArtLabel::paintEvent( QPaintEvent * )
{
    const auto factor = p_intf->p_mi ? p_intf->p_mi->getIntfScaleFactor() : 1.;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath shape;
    shape.addRoundedRect(QRectF(rect()), qRound(6 * factor), qRound(6 * factor));
    painter.setClipPath(shape);
    painter.fillRect(rect(), palette().color(QPalette::Window));
    if (!artwork.isNull())
    {
        const qreal side = std::min(artwork.width(), artwork.height());
        const QRectF source((artwork.width() - side) / 2., (artwork.height() - side) / 2., side, side);
        painter.drawPixmap(QRectF(rect()), artwork, source);
        return;
    }
    bool video = false;
    if (p_item)
    {
        vlc_mutex_locker lock(&p_item->lock);
        for (size_t i = 0; i < p_item->es_vec.size; ++i)
            if (p_item->es_vec.data[i].es.i_cat == VIDEO_ES) { video = true; break; }
    }
    const auto color = palette().color(QPalette::WindowText).name();
    const auto icon = ColorizedSvgIcon(video ? QStringLiteral(":/menu/redesign/video.svg") : QStringLiteral(":/menu/redesign/music.svg"), {}, {}, {},
        {{QStringLiteral("#8F8F8F"), color}, {QStringLiteral("#D37918"), color}});
    const auto size = qRound(80 * factor);
    const auto pixmap = icon.pixmap(QSize(size, size), devicePixelRatioF());
    painter.drawPixmap(QRect((width() - size) / 2, (height() - size) / 2, size, size), pixmap);
}

void CoverArtLabel::askForUpdate()
{
    if (!p_item) return;

    THEMIM->requestArtUpdate( p_item.get() );
}

void CoverArtLabel::setArtFromFile()
{
    if (!p_item) return;

    const bool restricted = vlc_artwork_item_IsRestricted(p_item.get());
    QUrl fileUrl = QFileDialog::getOpenFileUrl(this, qtr("Choose Cover Art"), p_intf->p_mi->getDialogFilePath(),
        restricted ? qtr("Image files (*.jpg *.jpeg *.png *.webp)") : qtr("Image Files (*.gif *.jpg *.jpeg *.png)"));

    if( fileUrl.isEmpty() )
        return;
    if (restricted && !ArtworkApproval::validUserFile(fileUrl)) return;

    selectedArtPath = fileUrl.toLocalFile();
    showArtUpdate( selectedArtPath );
    emit artSelected( fileUrl.toString() );
    emit editing();
}

void CoverArtLabel::clear()
{
    setItem({});
}
