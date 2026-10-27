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
#include <limits>
#include <QAction>
#include <QUrl>
#include <QPointer>
#include <QStandardPaths>
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
        if (p_item.get() == THEMIM->getInput() && selectedArtPath.isEmpty() && !preparingArt) showArtUpdate();
    } );
    connect( THEMIM, QOverload<input_item_t *>::of(&PlayerController::artChanged), this, [this](input_item_t *item) {
        if (item == p_item.get() && selectedArtPath.isEmpty() && !preparingArt) showArtUpdate();
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
    if(selection) selection->close();
    if(preview) preview->close();
    QList< QAction* > artActions = actions();
    foreach( QAction *act, artActions )
        removeAction( act );
}

void CoverArtLabel::setItem( const SharedInputItem& _p_item )
{
    const QPointer<CoverArtLabel> owner(this);
    discardArtSelection();
    if(!owner) return;
    if(preview) preview->cancel();
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

bool CoverArtLabel::ensureArtworkHelpers()
{
    if(selection && preview) return true;
    auto* ctx=p_intf->p_mi;
    auto lifetime=artworkLifetime;
    if(!lifetime.current() && ctx) lifetime=ctx->addonStreamLifetime();
    if(!lifetime.current()) return false;
    if(!selection) {
        const auto directory=artworkDirectory.isEmpty()
            ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+QStringLiteral("/selected-artwork") : artworkDirectory;
        selection=new ArtworkSelection(lifetime,directory,this);
        if(ctx) connect(ctx,&MainCtx::addonWorkRetiring,selection,&ArtworkSelection::close,Qt::DirectConnection);
    }
    if(!preview) {
        preview=new ArtworkNativePreview(lifetime,this);
        if(ctx) connect(ctx,&MainCtx::addonWorkRetiring,preview,&ArtworkNativePreview::close,Qt::DirectConnection);
    }
    return true;
}

void CoverArtLabel::setArtworkSelectionLifetime(vlc::addons::AddonStreamLifetime::Handle lifetime,QString directory)
{
    const QPointer<CoverArtLabel> owner(this);
    discardArtSelection();
    if(!owner) return;
    delete selection; selection=nullptr;
    delete preview; preview=nullptr;
    artworkLifetime=std::move(lifetime); artworkDirectory=std::move(directory); showArtUpdate();
}

void CoverArtLabel::showArtUpdate( const QString& path )
{
    artwork=QPixmap(); update();
    if(path.isEmpty() || !ensureArtworkHelpers()) return;
    const auto item=p_item;
    preview->request(path,[this,item,path](QImage pixels) {
        if(item!=p_item || preparingArt || !selectedArtPath.isEmpty()
            || !item || THEMIM->decodeArtURL(item.get())!=path) return;
        artwork=QPixmap::fromImage(std::move(pixels)); update();
    });
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
    if(!p_item || !p_intf->p_mi) return;
    const QPointer<CoverArtLabel> owner(this);
    const auto file=QFileDialog::getOpenFileUrl(this,qtr("Choose cover art"),p_intf->p_mi->getDialogFilePath(),
        qtr("Image files (*.bmp *.cur *.gif *.ico *.jfif *.jpeg *.jpg *.pbm *.pgm *.png *.ppm *.svg *.svgz *.webp *.xbm *.xpm);;All files (*)"));
    if(owner && !file.isEmpty()) chooseArtFile(file);
}

void CoverArtLabel::chooseArtFile(const QUrl& file)
{
    if(!p_item || !ensureArtworkHelpers() || artworkChoiceEpoch==std::numeric_limits<quint64>::max()) return;
    const auto epoch=++artworkChoiceEpoch;
    const QPointer<CoverArtLabel> owner(this);
    const auto item=p_item;
    if(preview) preview->cancel();
    selection->cancel(); preparedArt.reset(); selectedArtPath.clear();
    emit artSelectionReset();
    if(!owner || item!=p_item || epoch!=artworkChoiceEpoch) return;
    preparingArt=true; emit artworkPreparationChanged(true);
    if(!owner || !preparingArt || item!=p_item || epoch!=artworkChoiceEpoch) return;
    emit editing();
    if(!owner || !preparingArt || item!=p_item || epoch!=artworkChoiceEpoch) return;
    const auto generation=selection->prepare(item.get(),file,[this,item,epoch](ArtworkSelection::Result result) {
        if(item!=p_item || epoch!=artworkChoiceEpoch || !selection || selection->generation()!=result.generation) return;
        const QPointer<CoverArtLabel> owner(this);
        if(!result.receipt || !result.receipt->current(item.get(),result.generation)) {
            preparingArt=false; emit artworkPreparationChanged(false);
            if(!owner || item!=p_item || epoch!=artworkChoiceEpoch || preparingArt) return;
            showArtUpdate();
            DialogsProvider::getInstance()->messageDialog(qtr("The app cannot prepare this cover image."),
                qtr("Cover art"),QMessageBox::Close,QMessageBox::Close,QMessageBox::Warning,this);
            return;
        }
        preparedArt=std::move(result.receipt); selectedArtPath=preparedArt->file().toLocalFile();
        artwork=QPixmap::fromImage(std::move(result.preview)); update();
        emit artSelected(preparedArt->file().toString(QUrl::FullyEncoded));
        if(!owner || item!=p_item || epoch!=artworkChoiceEpoch || !preparingArt) return;
        preparingArt=false; emit artworkPreparationChanged(false);
        if(owner && epoch==artworkChoiceEpoch) emit editing();
    });
    if(!generation) {
        preparingArt=false; emit artworkPreparationChanged(false);
        if(!owner || item!=p_item || epoch!=artworkChoiceEpoch || preparingArt) return;
        showArtUpdate();
        DialogsProvider::getInstance()->messageDialog(qtr("The app cannot prepare this cover image."),
            qtr("Cover art"),QMessageBox::Close,QMessageBox::Close,QMessageBox::Warning,this);
    }
}

void CoverArtLabel::discardArtSelection()
{
    if(artworkChoiceEpoch!=std::numeric_limits<quint64>::max()) ++artworkChoiceEpoch;
    if(selection) selection->cancel();
    preparedArt.reset(); selectedArtPath.clear();
    if(preparingArt) { preparingArt=false; emit artworkPreparationChanged(false); }
}

void CoverArtLabel::clear()
{
    setItem({});
}
