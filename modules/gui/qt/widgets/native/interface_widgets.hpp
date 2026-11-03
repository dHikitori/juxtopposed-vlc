/*****************************************************************************
 * interface_widgets.hpp : Custom widgets for the main interface
 ****************************************************************************
 * Copyright (C) 2006-2008 the VideoLAN team
 *
 * Authors: Clément Stenac <zorglub@videolan.org>
 *          Jean-Baptiste Kempf <jb@videolan.org>
 *          Rafaël Carré <funman@videolanorg>
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

#ifndef VLC_QT_INTERFACE_WIDGETS_HPP_
#define VLC_QT_INTERFACE_WIDGETS_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "util/shared_input_item.hpp"
#include "util/artwork_selection.hpp"

#include <QLabel>
#include <QPixmap>

class QWidget;
class QMouseEvent;
class MetaPanel;

struct qt_intf_t;
struct input_item_t;

class CoverArtLabel : public QLabel
{
    Q_OBJECT
public:
    CoverArtLabel( QWidget *parent, qt_intf_t * );
    void setItem( const SharedInputItem & );
    virtual ~CoverArtLabel();
    bool artworkPending() const noexcept { return preparingArt; }
    std::shared_ptr<ArtworkSelection::Receipt> selectedReceipt() const { return preparedArt; }
    void chooseArtFile(const QUrl&);
    void discardArtSelection();
    void setArtworkSelectionLifetime(vlc::addons::AddonStreamLifetime::Handle, QString cacheDirectory = {});

protected:
    void mouseDoubleClickEvent( QMouseEvent *event ) override;
    void paintEvent( QPaintEvent *event ) override;

private:
    qt_intf_t *p_intf;
    SharedInputItem p_item;
    QPixmap artwork;
    QString selectedArtPath;
    ArtworkSelection* selection = nullptr;
    ArtworkNativePreview* preview = nullptr;
    std::shared_ptr<ArtworkSelection::Receipt> preparedArt;
    bool preparingArt = false;
    quint64 artworkChoiceEpoch = 0;
    vlc::addons::AddonStreamLifetime::Handle artworkLifetime;
    QString artworkDirectory;
    bool ensureArtworkHelpers();

    void showArtUpdate( const QString& );
    void showArtUpdate( );

public slots:
    void askForUpdate();
    void setArtFromFile();
    void clear();

signals:
    void editing();
    void artSelected( const QString& );
    void artSelectionReset();
    void artworkPreparationChanged(bool);
};

#endif
