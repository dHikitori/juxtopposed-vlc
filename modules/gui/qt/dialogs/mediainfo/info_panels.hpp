/*****************************************************************************
 * info_panels.hpp : Panels for the information dialogs
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

#ifndef VLC_QT_INFO_PANELS_HPP_
#define VLC_QT_INFO_PANELS_HPP_

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "qt.hpp"
#include "util/shared_input_item.hpp"

#include <vlc_common.h>
#include <vlc_input_item.h>
#include <QWidget>
#include <QVector>
#include <QPair>
#include <QPointer>

class QGridLayout;
class QResizeEvent;
class QTreeWidget;
class QTreeWidgetItem;
class QTableWidget;
class QLineEdit;
class CoverArtLabel;
class QTextEdit;
class QLabel;
class VLCStatsView;
class QPushButton;
class FingerprintDialog;

class MetaPanel: public QWidget
{
    Q_OBJECT
public:
    MetaPanel( QWidget *, qt_intf_t * );
    bool saveMeta();

    bool isInEditMode();
    bool canSaveMeta() const { return b_inEditMode && !lookupInProgress && !artworkPreparing; }
    bool isInputLocked() const { return b_inEditMode || lookupInProgress || artworkPreparing; }
    void setEditMode( bool );
    void discardEdits();

private:
    SharedInputItem p_input;
    qt_intf_t *p_intf;
    bool b_inEditMode = false;
    bool lookupInProgress = false;
    bool artworkPreparing = false;
    QPointer<FingerprintDialog> fingerprintDialog;
    SharedInputItem pendingTags;
    QGridLayout *metaLayout;
    QVector<QPair<QWidget *, QWidget *>> fieldRows;
    QWidget *commentsBlock;
    QPushButton *changeCoverButton;
    void updateLayout();

protected:
    void resizeEvent( QResizeEvent * ) override;

private:
    QLineEdit *title_text;
    QLineEdit *artist_text;
    QLineEdit *genre_text;
    QLineEdit *copyright_text;
    QLineEdit *collection_text;
    QLineEdit *seqnum_text;
    QLineEdit *seqtot_text;

    QTextEdit *description_text;
    QLineEdit *date_text;
    QLineEdit *language_text;
    QLineEdit *nowplaying_text;
    QLineEdit *publisher_text;
    QLineEdit *encodedby_text;
    CoverArtLabel *art_cover;

    QString  currentURL;
    bool artworkSelectedByUser = false;

    QPushButton *fingerprintButton;

public slots:
    void update( const SharedInputItem & );
    void clear();
    void fingerprint();
    void fingerprintUpdate( const SharedInputItem & );

private slots:
    void enterEditMode();

signals:
    void uriSet( const QString& );
    void editModeChanged( bool );
    void lookupInProgressChanged( bool );
};

class ExtraMetaPanel: public QWidget
{
    Q_OBJECT
public:
    ExtraMetaPanel( QWidget * );
protected:
    bool eventFilter(QObject *, QEvent *) override;
private:
    void retranslate();
    void filterMetadata();
    void copyMetadata(bool selectionOnly = false);
    QTableWidget *extraMeta;
    QLineEdit *filter;
    QPushButton *copyButton;
    QLabel *emptyMessage;
    SharedInputItem currentItem;
    QVector<QPair<QString, QString>> metadata;
public slots:
    void update( input_item_t * );
    void clear();
};

class InputStatsPanel: public QWidget
{
    Q_OBJECT
public:
    InputStatsPanel(QWidget *, MainCtx *);
protected:
    void showEvent(QShowEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void changeEvent(QEvent *) override;
private:
    struct Row {
        const char *name;
        const char *unit;
        QLabel *label;
        QLabel *value;
        QLabel *heading;
    };
    void retranslate();
    void updateLayout();
    void updateValues();
    void showSnapshot();
    void copyStatistics();
    QPointer<MainCtx> ctx;
    QLabel *summary;
    QPushButton *copyButton;
    QWidget *inputCard;
    QWidget *audioCard;
    QWidget *videoCard;
    QLabel *inputHeading;
    QLabel *audioHeading;
    QLabel *videoHeading;
    QLabel *graphLegend;
    QGridLayout *cards;
    QVector<Row> rows;
    VLCStatsView *statsView;
    input_stats_t snapshot{};
    bool hasSnapshot = false;
    bool pendingGraphSample = false;
    bool stopped = false;
public slots:
    void update(const input_stats_t &);
    void setStopped(bool);
    void clear();
};

class InfoPanel: public QWidget
{
    Q_OBJECT
public:
    InfoPanel( QWidget * );
protected:
    bool eventFilter(QObject *, QEvent *) override;
private:
    void retranslate();
    void filterInformation();
    void copyInformation(bool selectionOnly = false);
    QTreeWidget *InfoTree;
    QLineEdit *filter;
    QPushButton *copyButton;
    QLabel *emptyMessage;
    SharedInputItem currentItem;
public slots:
    void update( input_item_t * );
    void clear();
};

#endif
