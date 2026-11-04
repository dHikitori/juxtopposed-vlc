/*****************************************************************************
 * extended_panels.cpp : Extended controls panels
 ****************************************************************************
 * Copyright (C) 2006-2013 the VideoLAN team
 *
 * Authors: Clément Stenac <zorglub@videolan.org>
 *          Antoine Cellerier <dionoea .t videolan d@t org>
 *          Jean-Baptiste Kempf <jb@videolan.org>
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

#include <math.h>

#include <algorithm>
#include <vector>
#include <memory>

#include <QColorDialog>
#include <QToolButton>
#include <QLabel>
#include <QVariant>
#include <QString>
#include <QFont>
#include <QGridLayout>
#include <QComboBox>
#include <QTimer>
#include <QFileDialog>
#include <QRegularExpression>
#include <QApplication>
#include <QFormLayout>
#include <QStandardItemModel>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QHideEvent>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QLocale>
#include <QKeyEvent>
#include <QKeySequence>
#include "util/interface_translator.hpp"
#include "util/redesign_widget_theme.hpp"

#include "extended_panels.hpp"
#include "dialogs/preferences/preferences.hpp"
#include "qt.hpp"
#include "maininterface/mainctx.hpp"
#include "player/player_controller.hpp"
#include "util/qt_dirs.hpp"
#include "util/filter_chain_edit.hpp"
#include "widgets/native/customwidgets.hpp"
#include "dialogs/dialogs_provider.hpp"
#include "util/vlctick.hpp"

#include "../../../../audio_filter/equalizer_presets.h"
#include <vlc_configuration.h>
#include <vlc_modules.h>
#include <vlc_plugin.h>
#include <vlc_player.h>
#include <vlc_interface.h>

static bool filterIsPresent( const QString &filters, const QString &filter )
{
    QStringList list = filters.split( ':', Qt::SkipEmptyParts );

    foreach( const QString &filterCmp, list )
    {
        if( filterCmp.compare( filter ) == 0 )
            return true;
    }
    return false;
}

static const char* GetVFilterType( qt_intf_t *p_intf, const char *psz_name )
{
    module_t *p_obj = module_find( psz_name );
    if( !p_obj )
    {
        msg_Err( p_intf, "Unable to find filter module \"%s\".", psz_name );
        return NULL;
    }

    if( module_provides( p_obj, "video splitter" ) )
        return "video-splitter";
    else if( module_provides( p_obj, "video filter" ) )
        return "video-filter";
    else if( module_provides( p_obj, "sub source" ) )
        return "sub-source";
    else if( module_provides( p_obj, "sub filter" ) )
        return "sub-filter";
    else
    {
        msg_Err( p_intf, "Unknown video filter type." );
        return NULL;
    }
}

static const QString ModuleFromWidgetName( QObject *obj )
{
    return obj->objectName().replace( "Enable","" );
}

static QString OptionFromWidgetName( QObject *obj )
{
    /* Gruik ? ... nah */
    return obj->objectName()
        .remove( QRegularExpression( QStringLiteral( "Slider|Combo|Dial|Check|Spin|Text" ) ) )
        .replace( QRegularExpression( QStringLiteral( "([A-Z])" ) ), QStringLiteral( "-\\1" ) )
        .toLower();
}

static inline void setup_vfilter( qt_intf_t *p_intf, const char* psz_name, QWidget *widget )
{
    const char *psz_filter_type = GetVFilterType( p_intf, psz_name );
    if( psz_filter_type == NULL )
        return;

    char *psz_filters = var_InheritString( p_intf, psz_filter_type );
    if( psz_filters == NULL )
        return;

    QCheckBox *checkbox = qobject_cast<QCheckBox*>( widget );
    QGroupBox *groupbox = qobject_cast<QGroupBox*>( widget );
    if( filterIsPresent( qfu(psz_filters), qfu(psz_name) ) )
    {
        if( checkbox ) checkbox->setChecked( true ); \
        else if (groupbox) groupbox->setChecked( true ); \
    }
    else
    {
        if( checkbox ) checkbox->setChecked( false );
        else if (groupbox) groupbox->setChecked( false );
    }
    free( psz_filters );
}

#define SETUP_VFILTER( widget, type ) \
    setup_vfilter( p_intf, #widget, ui.widget##Enable ); \
    connect( ui.widget##Enable, &type::clicked, this, &ExtVideo::updateFilters );

#define SETUP_VFILTER_cb( widget ) SETUP_VFILTER( widget, QCheckBox )
#define SETUP_VFILTER_gb( widget ) SETUP_VFILTER( widget, QGroupBox )

#define SETUP_VFILTER_OPTION( widget, signal ) \
    initComboBoxItems( ui.widget ); \
    setWidgetValue( ui.widget ); \
    connect( ui.widget, signal, this, &ExtVideo::updateFilterOptions );

#define SETUP_VFILTER_VALUE_LABEL( name ) \
    setupSliderValueLabel( ui.name##Slider, ui.name##ValueLabel );

namespace {
class VideoColorValidator final : public QValidator
{
public:
    VideoColorValidator(qint64 minimum, qint64 maximum, QObject* parent)
        : QValidator(parent), m_minimum(qMax<qint64>(0, minimum)), m_maximum(qMin<qint64>(0xffffff, maximum)) {}
    State validate(QString& text, int&) const override
    {
        auto digits = text;
        if (digits.startsWith(QLatin1Char('#'))) digits.remove(0, 1);
        static const QRegularExpression hex(QStringLiteral("^[0-9a-fA-F]{0,6}$"));
        if (!hex.match(digits).hasMatch()) return Invalid;
        if (digits.size() != 6) return Intermediate;
        const auto value = digits.toLongLong(nullptr, 16);
        return value >= m_minimum && value <= m_maximum ? Acceptable : Intermediate;
    }
private:
    qint64 m_minimum, m_maximum;
};
}

ExtVideo::ExtVideo( qt_intf_t *_p_intf, QTabWidget *_parent ) :
            QObject( _parent ), m_tabs(_parent), p_intf( _p_intf )
{
    ui.setupUi( _parent );

    SETUP_VFILTER_gb( adjust )
    SETUP_VFILTER_OPTION( hueSlider, &QSlider::valueChanged )
    SETUP_VFILTER_OPTION( brightnessSlider, &QSlider::valueChanged )
    SETUP_VFILTER_OPTION( contrastSlider, &QSlider::valueChanged )
    SETUP_VFILTER_OPTION( saturationSlider, &QSlider::valueChanged )
    SETUP_VFILTER_OPTION( gammaSlider, &QSlider::valueChanged )

    SETUP_VFILTER_VALUE_LABEL( hue )
    SETUP_VFILTER_VALUE_LABEL( brightness )
    SETUP_VFILTER_VALUE_LABEL( contrast )
    SETUP_VFILTER_VALUE_LABEL( saturation )
    SETUP_VFILTER_VALUE_LABEL( gamma )

    SETUP_VFILTER_gb( extract )
    SETUP_VFILTER_OPTION( extractComponentText, &QLineEdit::editingFinished )

    SETUP_VFILTER_cb( posterize )

    SETUP_VFILTER_gb( colorthres )
    SETUP_VFILTER_OPTION( colorthresColorText, &QLineEdit::editingFinished )
    SETUP_VFILTER_OPTION( colorthresSaturationthresSlider, &QSlider::valueChanged )
    SETUP_VFILTER_OPTION( colorthresSimilaritythresSlider, &QSlider::valueChanged )

    SETUP_VFILTER_gb( sepia )
    SETUP_VFILTER_OPTION( sepiaIntensitySpin, QOverload<int>::of(&QSpinBox::valueChanged) )

    SETUP_VFILTER_cb( invert )

    SETUP_VFILTER_gb( gradient )
    SETUP_VFILTER_OPTION( gradientModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged) )
    SETUP_VFILTER_OPTION( gradientColorCheck, &QtCheckboxChanged )
    SETUP_VFILTER_OPTION( gradientCartoonCheck, &QtCheckboxChanged )

    SETUP_VFILTER_gb( motionblur )
    SETUP_VFILTER_OPTION( blurFactorSlider, &QSlider::valueChanged )

    SETUP_VFILTER_cb( motiondetect )

    SETUP_VFILTER_cb( psychedelic )

    SETUP_VFILTER_gb( sharpen )
    SETUP_VFILTER_OPTION( sharpenSigmaSlider, &QSlider::valueChanged )

    SETUP_VFILTER_cb( ripple )

    SETUP_VFILTER_cb( wave )

    SETUP_VFILTER_gb( transform )
    SETUP_VFILTER_OPTION( transformTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged) )

    SETUP_VFILTER_gb( rotate )
    SETUP_VFILTER_OPTION( rotateAngleDial, &VLCQDial::valueChanged )
    ui.rotateAngleDial->setWrapping( true );
    ui.rotateAngleDial->setNotchesVisible( true );

    SETUP_VFILTER_gb( puzzle )
    SETUP_VFILTER_OPTION( puzzleRowsSpin, QOverload<int>::of(&QSpinBox::valueChanged) )
    SETUP_VFILTER_OPTION( puzzleColsSpin, QOverload<int>::of(&QSpinBox::valueChanged) )

    SETUP_VFILTER_cb( magnify )

    SETUP_VFILTER_gb( clone )
    SETUP_VFILTER_OPTION( cloneCountSpin, QOverload<int>::of(&QSpinBox::valueChanged) )

    SETUP_VFILTER_gb( wall )
    SETUP_VFILTER_OPTION( wallRowsSpin, QOverload<int>::of(&QSpinBox::valueChanged) )
    SETUP_VFILTER_OPTION( wallColsSpin, QOverload<int>::of(&QSpinBox::valueChanged) )


    SETUP_VFILTER_gb( erase )
    SETUP_VFILTER_OPTION( eraseMaskText, &QLineEdit::editingFinished )
    SETUP_VFILTER_OPTION( eraseYSpin, QOverload<int>::of(&QSpinBox::valueChanged) )
    SETUP_VFILTER_OPTION( eraseXSpin, QOverload<int>::of(&QSpinBox::valueChanged) )
    BUTTONACT( ui.eraseBrowseBtn, &ExtVideo::browseEraseFile );

    SETUP_VFILTER_gb( marq )
    SETUP_VFILTER_OPTION( marqMarqueeText, &QLineEdit::editingFinished )
    SETUP_VFILTER_OPTION( marqPositionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged) )

    SETUP_VFILTER_gb( logo )
    SETUP_VFILTER_OPTION( logoFileText, &QLineEdit::editingFinished )
    SETUP_VFILTER_OPTION( logoYSpin, QOverload<int>::of(&QSpinBox::valueChanged) )
    SETUP_VFILTER_OPTION( logoXSpin, QOverload<int>::of(&QSpinBox::valueChanged) )
    SETUP_VFILTER_OPTION( logoOpacitySlider, &QSlider::valueChanged )
    BUTTONACT( ui.logoBrowseBtn, &ExtVideo::browseLogo );

    SETUP_VFILTER_gb( gradfun )
    SETUP_VFILTER_OPTION( gradfunRadiusSlider, &QSlider::valueChanged )

    SETUP_VFILTER_gb( grain )
    SETUP_VFILTER_OPTION( grainVarianceSlider, &QSlider::valueChanged )

    SETUP_VFILTER_cb( mirror )

    SETUP_VFILTER_gb( gaussianblur )
    SETUP_VFILTER_OPTION( gaussianblurSigmaSlider, &QSlider::valueChanged )

    SETUP_VFILTER_gb( antiflicker )
    SETUP_VFILTER_OPTION( antiflickerSofteningSizeSlider, &QSlider::valueChanged )

    SETUP_VFILTER_gb( hqdn3d )
    SETUP_VFILTER_OPTION( hqdn3dLumaSpatSlider, &QSlider::valueChanged )
    SETUP_VFILTER_OPTION( hqdn3dLumaTempSlider, &QSlider::valueChanged )
    SETUP_VFILTER_OPTION( hqdn3dChromaSpatSlider, &QSlider::valueChanged )
    SETUP_VFILTER_OPTION( hqdn3dChromaTempSlider, &QSlider::valueChanged )


    SETUP_VFILTER_cb( anaglyph )

#undef SETUP_VFILTER
#undef SETUP_VFILTER_OPTION

    connect( ui.cropTopPx, QOverload<int>::of(&QSpinBox::valueChanged), this, &ExtVideo::cropChange );
    connect( ui.cropBotPx, QOverload<int>::of(&QSpinBox::valueChanged), this, &ExtVideo::cropChange );
    connect( ui.cropLeftPx, QOverload<int>::of(&QSpinBox::valueChanged), this, &ExtVideo::cropChange );
    connect( ui.cropRightPx, QOverload<int>::of(&QSpinBox::valueChanged), this, &ExtVideo::cropChange );
    connect( ui.leftRightCropSync, &QCheckBox::toggled, this, &ExtVideo::cropChange );
    connect( ui.topBotCropSync, &QCheckBox::toggled, this, &ExtVideo::cropChange );
    connect( ui.topBotCropSync, &QCheckBox::toggled, ui.cropBotPx, &QSpinBox::setDisabled );
    connect( ui.leftRightCropSync, &QCheckBox::toggled, ui.cropRightPx, &QSpinBox::setDisabled );
    auto* posterize = new QSpinBox(_parent);
    posterize->setObjectName(QStringLiteral("posterizeLevelSpin"));
    posterize->setRange(2, 256);
    posterize->setProperty("videoEffectModule", QStringLiteral("posterize"));
    setWidgetValue(posterize);
    connect(posterize, QOverload<int>::of(&QSpinBox::valueChanged), this, &ExtVideo::updateFilterOptions);
    buildControls();
}

void ExtVideo::cropChange()
{
    if( ui.topBotCropSync->isChecked() )
        ui.cropBotPx->setValue( ui.cropTopPx->value() );
    if( ui.leftRightCropSync->isChecked() )
        ui.cropRightPx->setValue( ui.cropLeftPx->value() );
    
    PlayerController::VOutThreadList p_vouts = THEMIM->getVouts();
    for( auto p_vout: p_vouts )
    {
        var_SetInteger( p_vout.get(), "crop-top", ui.cropTopPx->value() );
        var_SetInteger( p_vout.get(), "crop-bottom", ui.cropBotPx->value() );
        var_SetInteger( p_vout.get(), "crop-left", ui.cropLeftPx->value() );
        var_SetInteger( p_vout.get(), "crop-right", ui.cropRightPx->value() );
    }
}

void ExtVideo::clean()
{
    ui.cropTopPx->setValue( 0 );
    ui.cropBotPx->setValue( 0 );
    ui.cropLeftPx->setValue( 0 );
    ui.cropRightPx->setValue( 0 );
}

static QString ChangeFiltersString( qt_intf_t *p_intf, const char *psz_filter_type, const char *psz_name, bool b_add )
{
    char *psz_chain = nullptr;
    if (!strcmp(psz_filter_type, "audio-filter")) {
        if (const auto output = THEMIM->getAout()) psz_chain = var_GetString(output.get(), psz_filter_type);
        if (!psz_chain) psz_chain = var_InheritString(p_intf, psz_filter_type);
    } else {
        if (const auto vout = THEMIM->getVout()) psz_chain = var_GetString(vout.get(), psz_filter_type);
        if (!psz_chain) psz_chain = var_InheritString(p_intf, psz_filter_type);
    }

    QString const chain = QString( psz_chain ? psz_chain : "" );
    QStringList list = chain.split( ':', Qt::SkipEmptyParts );

    if( b_add && std::find(list.begin(), list.end(), psz_name) == list.end() ) {
        const int transform = !strcmp(psz_filter_type, "video-filter") ? list.indexOf(QStringLiteral("transform")) : -1;
        if (transform >= 0) list.insert(transform, qfu(psz_name));
        else list << psz_name;
    }
    else if (!b_add)
        list.removeAll( psz_name );

    free( psz_chain );

    return list.join( ":" );
}

static void UpdateVFiltersString( qt_intf_t *p_intf,
                                  const char *psz_filter_type, const char *value )
{
    /* Try to set non splitter filters on the fly */
    if( strcmp( psz_filter_type, "video-splitter" ) )
    {
        auto p_vout = THEMIM->getVout();
        if( p_vout )
            var_SetString( p_vout.get(), psz_filter_type, value );
    }
}

void ExtVideo::changeVFiltersString( const char *psz_name, bool b_add )
{
    if (b_add && !module_exists(psz_name)) return;
    const auto cached = m_filterTypes.value(qfu(psz_name));
    const auto cachedType = cached.toUtf8();
    const char *psz_filter_type = cached.isEmpty() ? GetVFilterType(p_intf, psz_name) : cachedType.constData();
    if (!psz_filter_type) return;

    QString result = ChangeFiltersString( p_intf, psz_filter_type, psz_name, b_add );

    if (var_Type(p_intf, psz_filter_type) == 0) var_Create(p_intf, psz_filter_type, VLC_VAR_STRING | VLC_VAR_DOINHERIT);
    var_SetString(p_intf, psz_filter_type, qtu(result));
    emit configChanged( qfu( psz_filter_type ), result );

    UpdateVFiltersString( p_intf, psz_filter_type, qtu( result ) );
}

void ExtVideo::updateFilters()
{
    QString module = ModuleFromWidgetName( sender() );

    QCheckBox *checkbox = qobject_cast<QCheckBox*>( sender() );
    QGroupBox *groupbox = qobject_cast<QGroupBox*>( sender() );

    changeVFiltersString( qtu( module ),
                          checkbox ? checkbox->isChecked()
                                   : groupbox->isChecked() );
}

void ExtVideo::browseLogo()
{
    const auto file = QFileDialog::getOpenFileUrl(m_tabs, qtr("Choose a logo image"),
        p_intf->p_mi->getDialogFilePath(), QStringLiteral("%1 (*.png *.jpg);;%2 (*)").arg(TITLE_EXTENSIONS_IMAGE, TITLE_EXTENSIONS_ALL),
        nullptr, QFileDialog::Options(), {QStringLiteral("file")}).toLocalFile();
    if (file.isEmpty()) return;
    ui.logoFileText->setText(toNativeSeparators(file)); ui.logoFileText->setModified(false);
    setFilterOption("logo", "logo-file", 0, 0, qtu(toNativeSeparators(file)));
}

void ExtVideo::browseEraseFile()
{
    const auto file = QFileDialog::getOpenFileUrl(m_tabs, qtr("Choose a mask image"),
        p_intf->p_mi->getDialogFilePath(), QStringLiteral("%1 (*.png *.jpg);;%2 (*)").arg(TITLE_EXTENSIONS_IMAGE, TITLE_EXTENSIONS_ALL),
        nullptr, QFileDialog::Options(), {QStringLiteral("file")}).toLocalFile();
    if (file.isEmpty()) return;
    ui.eraseMaskText->setText(toNativeSeparators(file)); ui.eraseMaskText->setModified(false);
    setFilterOption("erase", "erase-mask", 0, 0, qtu(toNativeSeparators(file)));
}

QVariant ExtVideo::currentValue(const QString& key, const QHash<QString, QVariant>& pending) const
{
    const auto bytes = key.toUtf8();
    const auto output = THEMIM->getVout();
    const int type = config_GetType(bytes.constData()) & VLC_VAR_CLASS;
    if (!type) return {};
    if (output && key != QLatin1String("video-splitter") && var_Type(output.get(), bytes.constData())) {
        switch (type) {
        case VLC_VAR_FLOAT: return var_GetFloat(output.get(), bytes.constData());
        case VLC_VAR_INTEGER: return QVariant::fromValue(var_GetInteger(output.get(), bytes.constData()));
        case VLC_VAR_BOOL: return var_GetBool(output.get(), bytes.constData());
        case VLC_VAR_STRING: { char* value = var_GetString(output.get(), bytes.constData()); const auto result = qfu(value ? value : ""); free(value); return result; }
        }
    }
    if (pending.contains(key)) return pending.value(key);
    switch (type) {
    case VLC_VAR_FLOAT: return config_GetFloat(bytes.constData());
    case VLC_VAR_INTEGER: return QVariant::fromValue(config_GetInt(bytes.constData()));
    case VLC_VAR_BOOL: return bool(config_GetInt(bytes.constData()));
    case VLC_VAR_STRING: {
        const bool chain = key == QLatin1String("video-filter") || key == QLatin1String("sub-source")
            || key == QLatin1String("sub-filter") || key == QLatin1String("video-splitter");
        char* value = chain ? var_InheritString(p_intf, bytes.constData()) : config_GetPsz(bytes.constData());
        const auto result = qfu(value ? value : ""); free(value); return result;
    }
    default: return {};
    }
}

void ExtVideo::refreshVideoValues(const QHash<QString, QVariant>& pending) { if (m_refreshVideoValues) m_refreshVideoValues(pending); }
bool ExtVideo::hasDraft() const { return m_hasDraft && m_hasDraft(); }
bool ExtVideo::commitDrafts() { return !m_commitDrafts || m_commitDrafts(); }

bool ExtVideo::eventFilter(QObject* object, QEvent* event)
{
    if (object->property("videoEffectsViewport").toBool() && event->type() == QEvent::Resize)
        QTimer::singleShot(0, this, [this] { if (m_layoutControls) m_layoutControls(); });
    auto* crop = object->property("videoCropField").toBool() ? qobject_cast<QSpinBox*>(object) : nullptr;
    auto* input = qobject_cast<QLineEdit*>(object);
    if (crop) input = crop->findChild<QLineEdit*>();
    if (input && !crop && input->property("videoCropField").toBool()) crop = qobject_cast<QSpinBox*>(input->parentWidget());
    if (input && event->type() == QEvent::ShortcutOverride) {
        const auto key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Escape) { event->accept(); return true; }
    }
    if (input && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (crop && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
            if (crop->hasAcceptableInput()) { crop->interpretText(); input->setModified(false); }
            input->selectAll(); return true;
        }
        if (crop && key->key() == Qt::Key_Escape) {
            const QSignalBlocker block(crop);
            crop->setValue(input->property("videoCropCurrent").toInt()); input->setModified(false); crop->clearFocus();
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            if (input->hasAcceptableInput()) { QMetaObject::invokeMethod(input, "editingFinished"); input->setModified(false); emit draftChanged(); }
            input->selectAll(); return true;
        }
        if (key->key() == Qt::Key_Escape) {
            const QSignalBlocker block(input);
            input->setText(input->property("videoEffectCurrent").toString()); input->setModified(false); input->clearFocus();
            emit draftChanged(); return true;
        }
    }
    return QObject::eventFilter(object, event);
}

void ExtVideo::initComboBoxItems( QObject *widget )
{
    QComboBox *combobox = qobject_cast<QComboBox*>( widget );
    if( !combobox ) return;

    QString option = OptionFromWidgetName( widget );
    module_config_t *p_item = config_FindConfig( qtu( option ) );
    if( p_item == NULL )
    {
        msg_Err( p_intf, "Couldn't find option \"%s\".", qtu( option ) );
        return;
    }

    if( p_item->i_type == CONFIG_ITEM_INTEGER
     || p_item->i_type == CONFIG_ITEM_BOOL )
    {
        int64_t *values;
        char **texts;
        ssize_t count = config_GetIntChoices( qtu( option ), &values, &texts );
        for( ssize_t i = 0; i < count; i++ )
        {
            combobox->addItem( qfut( texts[i] ), qlonglong(values[i]) );
            free( texts[i] );
        }
        free( texts );
        free( values );
    }
    else if( p_item->i_type == CONFIG_ITEM_STRING )
    {
        char **values;
        char **texts;
        ssize_t count = config_GetPszChoices( qtu( option ), &values, &texts );
        for( ssize_t i = 0; i < count; i++ )
        {
            combobox->addItem( qfut( texts[i] ), qfu(values[i]) );
            free( texts[i] );
            free( values[i] );
        }
        free( texts );
        free( values );
    }
}

void ExtVideo::setWidgetValue( QObject *widget )
{
    auto* control = qobject_cast<QWidget*>(widget);
    if (control && !m_options.contains(control)) m_options.append(control);
    const auto module = widget->property("videoEffectModule").isValid() ? widget->property("videoEffectModule").toString()
        : ModuleFromWidgetName(widget->parent());
    if (control) m_modules.insert(control, module);
    const auto option = OptionFromWidgetName(widget);
    const auto current = currentValue(option);
    if (!current.isValid()) return;
    const int i_type = config_GetType(qtu(option)) & VLC_VAR_CLASS;
    vlc_value_t val {};
    if (i_type == VLC_VAR_INTEGER) val.i_int = current.toLongLong();
    else if (i_type == VLC_VAR_BOOL) val.i_int = current.toBool();
    else if (i_type == VLC_VAR_FLOAT) val.f_float = current.toFloat();
    else if (i_type == VLC_VAR_STRING) val.psz_string = strdup(qtu(current.toString()));

    /* Try to cast to all the widgets we're likely to encounter. Only
     * one of the casts is expected to work. */
    QSlider        *slider        = qobject_cast<QSlider*>       ( widget );
    QCheckBox      *checkbox      = qobject_cast<QCheckBox*>     ( widget );
    QSpinBox       *spinbox       = qobject_cast<QSpinBox*>      ( widget );
    QDoubleSpinBox *doublespinbox = qobject_cast<QDoubleSpinBox*>( widget );
    VLCQDial       *dial          = qobject_cast<VLCQDial*>      ( widget );
    QLineEdit      *lineedit      = qobject_cast<QLineEdit*>     ( widget );
    QComboBox      *combobox      = qobject_cast<QComboBox*>     ( widget );

    if( i_type == VLC_VAR_INTEGER || i_type == VLC_VAR_BOOL )
    {
        if( slider )        slider->setValue( val.i_int );
        else if( checkbox ) checkbox->setCheckState( val.i_int? Qt::Checked
                                                              : Qt::Unchecked );
        else if( spinbox )  spinbox->setValue( val.i_int );
        else if( dial )     dial->setValue( (360 - val.i_int) % 360 );
        else if( lineedit )
        {
            char str[30];
            snprintf( str, sizeof(str), "%06" PRIX64, val.i_int );
            lineedit->setText( str );
        }
        else if( combobox ) combobox->setCurrentIndex(
                            combobox->findData( qlonglong(val.i_int) ) );
        else msg_Warn( p_intf, "Could not find the correct Integer widget" );
    }
    else if( i_type == VLC_VAR_FLOAT )
    {
        if( slider ) slider->setValue( qRound(val.f_float * slider->tickInterval()) ); /* hack alert! */
        else if( doublespinbox ) doublespinbox->setValue( val.f_float );
        else if( dial ) dial->setValue( (360 - lroundf(val.f_float)) % 360 );
        else msg_Warn( p_intf, "Could not find the correct Float widget" );
    }
    else if( i_type == VLC_VAR_STRING )
    {
        if( lineedit ) lineedit->setText( qfu( val.psz_string ) );
        else if( combobox ) combobox->setCurrentIndex(
                            combobox->findData( qfu( val.psz_string ) ) );
        else msg_Warn( p_intf, "Could not find the correct String widget" );
        free( val.psz_string );
    }
}

void ExtVideo::setupSliderValueLabel( QSlider *slider, QLabel *label )
{
    auto update = [ slider, label ]( int value ) {
        int interval = slider->tickInterval();
        label->setText( QString::number( value / ( double )interval, 'f', 1 ) );
    };
    connect( slider, &QSlider::valueChanged, label, update );
    update( slider->value() );
}

void ExtVideo::setFilterOption( const char *psz_module, const char *psz_option,
        int i_int, double f_float, const char *psz_string )
{
    auto p_vout = THEMIM->getVout();
    int i_type = 0;

    if (p_vout) i_type = var_Type(p_vout.get(), psz_option);
    if( i_type == 0 )
        i_type = config_GetType( psz_option );

    vlc_value_t val;
    i_type &= VLC_VAR_CLASS;
    if( i_type == VLC_VAR_INTEGER || i_type == VLC_VAR_BOOL )
    {
        emit configChanged( qfu( psz_option ), QVariant( i_int ) );
        if( i_type == VLC_VAR_INTEGER )
            val.i_int = i_int;
        else
            val.b_bool = i_int;
    }
    else if( i_type == VLC_VAR_FLOAT )
    {
        emit configChanged( qfu( psz_option ), QVariant( f_float ) );
        val.f_float = f_float;
    }
    else if( i_type == VLC_VAR_STRING )
    {
        if( psz_string == NULL )
            psz_string = "";
        emit configChanged( qfu( psz_option ), qfu(psz_string) );
        val.psz_string = (char *) psz_string;
    }
    else
    {
        msg_Err( p_intf,
                 "Module %s's %s variable is of an unsupported type ( %d )",
                 psz_module,
                 psz_option,
                 i_type );
        return;
    }

    if (p_vout) {
        if (var_Type(p_vout.get(), psz_option) == 0) var_Create(p_vout.get(), psz_option, i_type | VLC_VAR_DOINHERIT | VLC_VAR_ISCOMMAND);
        var_SetChecked(p_vout.get(), psz_option, i_type, val);
    }
}

void ExtVideo::updateFilterOptions()
{
    QString module = m_modules.value(qobject_cast<QWidget*>(sender()), ModuleFromWidgetName(sender()->parent()));
    //msg_Dbg( p_intf, "Module name: %s", qtu( module ) );
    QString option = OptionFromWidgetName( sender() );
    //msg_Dbg( p_intf, "Option name: %s", qtu( option ) );

    /* Try to cast to all the widgets we're likely to encounter. Only
     * one of the casts is expected to work. */
    QSlider        *slider        = qobject_cast<QSlider*>       ( sender() );
    QCheckBox      *checkbox      = qobject_cast<QCheckBox*>     ( sender() );
    QSpinBox       *spinbox       = qobject_cast<QSpinBox*>      ( sender() );
    QDoubleSpinBox *doublespinbox = qobject_cast<QDoubleSpinBox*>( sender() );
    VLCQDial       *dial          = qobject_cast<VLCQDial*>      ( sender() );
    QLineEdit      *lineedit      = qobject_cast<QLineEdit*>     ( sender() );
    QComboBox      *combobox      = qobject_cast<QComboBox*>     ( sender() );

    int i_int = -1;
    double f_float = -1.;
    QString val;

    if( slider ) {
        i_int = slider->value();
        f_float = ( double )slider->value() / ( double )slider->tickInterval(); /* hack alert! */
    }
    else if( checkbox ) i_int = checkbox->checkState() == Qt::Checked;
    else if( spinbox ) i_int = spinbox->value();
    else if( doublespinbox ) f_float = doublespinbox->value();
    else if( dial ) {
        i_int = (360 - dial->value()) % 360;
        f_float = i_int;
    }
    else if( lineedit ) {
        if (!lineedit->isModified() || !lineedit->hasAcceptableInput()) return;
        auto hex = lineedit->text(); hex.remove(QLatin1Char('#'));
        i_int = hex.toInt(nullptr, 16);
        f_float = lineedit->text().toDouble();
        val = lineedit->text();
    }
    else if( combobox ) {
        i_int = combobox->itemData( combobox->currentIndex() ).toInt();
        val = combobox->itemData( combobox->currentIndex() ).toString();
    }

    setFilterOption( qtu( module ), qtu( option ), i_int, f_float, qtu( val ) );
    if (lineedit) lineedit->setModified(false);
    emit draftChanged();
}

/**********************************************************************
 * v4l2 controls
 **********************************************************************/

namespace {
struct CameraControl
{
    QString key, label, text;
    int type = 0;
    qint64 value = 0, minimum = 0, maximum = 0, step = 1;
    QList<QPair<QString, qint64>> choices;
};

QList<CameraControl> cameraControls(vlc_object_t *device, bool& failed)
{
    QList<CameraControl> controls;
    vlc_value_t *values = nullptr;
    char **names = nullptr;
    size_t count = 0;
    failed = var_Change(device, "controls", VLC_VAR_GETCHOICES, &count, &values, &names) != VLC_SUCCESS;
    if (failed) return controls;
    for (size_t i = 0; i < count; ++i)
    {
        CameraControl item;
        item.key = qfu(names[i]);
        free(names[i]);
        if (item.key.isEmpty()) continue;
        const auto key = item.key.toUtf8();
        item.type = var_Type(device, key.constData());
        if (!item.type) continue;
        char *label = nullptr;
        if (var_Change(device, key.constData(), VLC_VAR_GETTEXT, &label) == VLC_SUCCESS)
            item.label = qfut(label);
        free(label);
        if (item.label.isEmpty()) item.label = item.key;
        switch (item.type & VLC_VAR_TYPE)
        {
            case VLC_VAR_INTEGER:
            {
                item.value = var_GetInteger(device, key.constData());
                vlc_value_t bound {};
                var_Change(device, key.constData(), VLC_VAR_GETMIN, &bound); item.minimum = bound.i_int;
                var_Change(device, key.constData(), VLC_VAR_GETMAX, &bound); item.maximum = bound.i_int;
                var_Change(device, key.constData(), VLC_VAR_GETSTEP, &bound); item.step = qMax<qint64>(1, bound.i_int);
                if (item.type & VLC_VAR_HASCHOICE)
                {
                    vlc_value_t *choices = nullptr;
                    char **labels = nullptr;
                    size_t size = 0;
                    if (var_Change(device, key.constData(), VLC_VAR_GETCHOICES, &size, &choices, &labels) == VLC_SUCCESS)
                    {
                        for (size_t j = 0; j < size; ++j)
                        {
                            item.choices.append({labels[j] ? qfut(labels[j]) : QString::number(choices[j].i_int), choices[j].i_int});
                            free(labels[j]);
                        }
                        free(labels); free(choices);
                    }
                }
                break;
            }
            case VLC_VAR_BOOL: item.value = var_GetBool(device, key.constData()); break;
            case VLC_VAR_STRING:
            {
                char *text = var_GetString(device, key.constData());
                item.text = qfu(text); free(text); break;
            }
        }
        controls.append(item);
    }
    free(names); free(values);
    return controls;
}
}

ExtV4l2::ExtV4l2(qt_intf_t *intf, QWidget *parent) : QWidget(parent), p_intf(intf)
{
    setObjectName(QStringLiteral("cameraControls"));
    auto *root = new QVBoxLayout(this);
    auto *heading = new QLabel(qtr("Camera controls"), this);
    heading->setTextFormat(Qt::PlainText);
    heading->setProperty("redesignHeading", true);
    root->addWidget(heading);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("cameraStatus"));
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    root->addWidget(m_status);
    auto *actions = new QHBoxLayout;
    m_open = new QPushButton(qtr("Open capture device"), this);
    m_open->setObjectName(QStringLiteral("cameraOpen"));
    m_refresh = new QPushButton(qtr("Refresh"), this);
    m_refresh->setObjectName(QStringLiteral("cameraRefresh"));
    m_open->installEventFilter(this); m_refresh->installEventFilter(this);
    actions->addWidget(m_open); actions->addStretch(); actions->addWidget(m_refresh);
    root->addLayout(actions);
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("cameraScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_content = new QWidget;
    m_content->setObjectName(QStringLiteral("cameraContent"));
    m_body = new QVBoxLayout(m_content);
    m_scroll->setWidget(m_content);
    root->addWidget(m_scroll, 1);
    root->addStretch(1);
    m_poll = new QTimer(this);
    m_poll->setInterval(2000);
    connect(m_poll, &QTimer::timeout, this, &ExtV4l2::checkDevice);
    connect(m_refresh, &QPushButton::clicked, this, &ExtV4l2::Refresh);
    connect(m_open, &QPushButton::clicked, this, [] { DialogsProvider::getInstance()->openCaptureDialog(); });
    connect(THEMIM, &PlayerController::inputChanged, this, [this] {
        if (isVisible()) QTimer::singleShot(0, this, &ExtV4l2::Refresh);
    });
    connect(THEMIM, &PlayerController::playingStateChanged, this, [this](PlayerController::PlayingState state) {
        if (isVisible() && state == PlayerController::PLAYING_STATE_STOPPED)
            QTimer::singleShot(0, this, &ExtV4l2::Refresh);
    });
    if (const auto ctx = p_intf->p_mi)
        connect(ctx, &MainCtx::intfScaleFactorChanged, this, &ExtV4l2::updateMetrics);
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, [this, heading] {
            heading->setText(qtr("Camera controls"));
            m_open->setText(qtr("Open capture device")); m_refresh->setText(qtr("Refresh"));
            updateStatus();
        });
    applyRedesignWidgetTheme(this, p_intf->p_mi);
    updateMetrics();
}

void ExtV4l2::updateStatus()
{
    m_status->setText(!m_device ? qtr("Open and play a camera to show its controls.")
        : m_retry ? qtr("Hakone cannot read the camera controls. Select Refresh to try again.")
        : m_controlCount == 0 ? qtr("This camera has no controls for Hakone.")
        : qtr("Changes apply to the open camera."));
    m_status->setAccessibleName(m_status->text());
}

void ExtV4l2::updateMetrics()
{
    const auto dp = [this](int value) { return qRound(value * (p_intf->p_mi ? p_intf->p_mi->getIntfScaleFactor() : 1)); };
    layout()->setContentsMargins(dp(16), dp(16), dp(16), dp(16));
    layout()->setSpacing(dp(12));
    m_body->setContentsMargins(0, 0, 0, 0); m_body->setSpacing(dp(12));
    if (auto *card = m_content->findChild<QFrame*>(QStringLiteral("cameraControlCard")))
    {
        card->layout()->setContentsMargins(dp(16), dp(16), dp(16), dp(16));
        card->layout()->setSpacing(dp(12));
        for (auto *label : card->findChildren<QLabel*>())
            if (label->property("cameraLabel").toBool()) { label->setMinimumWidth(dp(140)); label->setMaximumWidth(dp(200)); }
    }
}

void ExtV4l2::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    Refresh(); m_poll->start();
}

void ExtV4l2::hideEvent(QHideEvent *event)
{
    m_poll->stop();
    QWidget::hideEvent(event);
}

void ExtV4l2::checkDevice()
{
    if (!isVisible()) return;
    bool changed;
    {
        vlc_player_locker lock(p_intf->p_player);
        changed = vlc_player_GetV4l2Object(p_intf->p_player) != m_device;
    }
    if (changed || m_retry) Refresh();
}

QVariant ExtV4l2::applyValue(const QString& key, int type, const QVariant& value)
{
    QVariant result;
    {
        vlc_player_locker lock(p_intf->p_player);
        auto *device = vlc_player_GetV4l2Object(p_intf->p_player);
        const auto name = key.toUtf8();
        const int actual = device ? var_Type(device, name.constData()) : 0;
        if (device && device == m_device && (actual & VLC_VAR_TYPE) == type)
        {
            switch (type)
            {
                case VLC_VAR_INTEGER:
                {
                    bool allowed = true;
                    if (actual & VLC_VAR_HASCHOICE)
                    {
                        vlc_value_t *choices = nullptr; char **labels = nullptr; size_t size = 0;
                        allowed = false;
                        if (var_Change(device, name.constData(), VLC_VAR_GETCHOICES, &size, &choices, &labels) == VLC_SUCCESS)
                        {
                            for (size_t i = 0; i < size; ++i) { allowed |= choices[i].i_int == value.toLongLong(); free(labels[i]); }
                            free(labels); free(choices);
                        }
                    }
                    if (allowed && var_SetInteger(device, name.constData(), value.toLongLong()) == VLC_SUCCESS)
                        result = qlonglong(var_GetInteger(device, name.constData()));
                    break;
                }
                case VLC_VAR_BOOL:
                    if (var_SetBool(device, name.constData(), value.toBool()) == VLC_SUCCESS)
                        result = var_GetBool(device, name.constData());
                    break;
                case VLC_VAR_STRING:
                    if (var_SetString(device, name.constData(), value.toString().toUtf8().constData()) == VLC_SUCCESS)
                    {
                        char *text = var_GetString(device, name.constData()); result = qfu(text); free(text);
                    }
                    break;
                case VLC_VAR_VOID:
                    if (actual & VLC_VAR_ISCOMMAND) { var_TriggerCallback(device, name.constData()); result = true; }
                    break;
            }
        }
    }
    if (!result.isValid())
    {
        m_status->setText(qtr("The camera controls changed. Select Refresh to try again."));
        QTimer::singleShot(0, this, &ExtV4l2::Refresh);
    }
    return result;
}

void ExtV4l2::Refresh()
{
    QList<CameraControl> controls;
    vlc_object_t *device;
    bool failed = false;
    {
        // Snapshot borrowed driver data, then release the player before building UI.
        vlc_player_locker lock(p_intf->p_player);
        device = vlc_player_GetV4l2Object(p_intf->p_player);
        if (device) controls = cameraControls(device, failed);
    }
    m_device = device; m_retry = failed; m_controlCount = controls.size();
    const auto focus = QApplication::focusWidget();
    const auto spin = focus ? focus->property("cameraSpinBox").value<QObject*>() : nullptr;
    const auto focusName = focus && m_content->isAncestorOf(focus) ? (spin ? spin->objectName() : focus->objectName()) : QString();
    const int scroll = m_scroll->verticalScrollBar()->value();
    while (auto *item = m_body->takeAt(0)) { delete item->widget(); delete item; }
    m_open->setVisible(!device);
    const bool ready = device && !failed && !controls.isEmpty();
    m_scroll->setVisible(ready);
    auto *root = static_cast<QVBoxLayout*>(layout());
    root->setStretch(root->count() - 1, ready ? 0 : 1);
    updateStatus();
    if (!device || failed || controls.isEmpty()) return;
    auto *card = new QFrame(m_content);
    card->setObjectName(QStringLiteral("cameraControlCard"));
    card->setProperty("redesignCard", true);
    auto *form = new QFormLayout(card);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_body->addWidget(card); m_body->addStretch();
    const auto labelFor = [card](const CameraControl& control) {
        auto *label = new QLabel(control.label, card);
        label->setTextFormat(Qt::PlainText); label->setWordWrap(true);
        label->setProperty("cameraLabel", true);
        label->setObjectName(QStringLiteral("cameraLabel_") + control.key);
        label->setMinimumWidth(0); label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        return label;
    };
    for (const auto& control : controls)
    {
        const auto objectName = QStringLiteral("cameraControl_") + control.key;
        const int type = control.type & VLC_VAR_TYPE;
        if (type == VLC_VAR_INTEGER && (control.type & VLC_VAR_HASCHOICE))
        {
            auto *choice = new QComboBox(card);
            choice->setObjectName(objectName); choice->setAccessibleName(control.label);
            choice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            choice->setMinimumContentsLength(1); choice->setMinimumWidth(0);
            int selected = -1;
            for (const auto& entry : control.choices)
            {
                choice->addItem(entry.first, qlonglong(entry.second));
                if (entry.second == control.value) selected = choice->count() - 1;
            }
            if (selected < 0)
            {
                selected = choice->count();
                choice->addItem(qtr("Current value: %1").arg(control.value), qlonglong(control.value));
                if (auto *model = qobject_cast<QStandardItemModel*>(choice->model())) model->item(selected)->setEnabled(false);
            }
            choice->setCurrentIndex(selected); choice->setEnabled(!control.choices.isEmpty());
            connect(choice, QOverload<int>::of(&QComboBox::activated), this, [this, choice, control](int row) {
                if (row < 0) return;
                const auto actual = applyValue(control.key, VLC_VAR_INTEGER, choice->itemData(row));
                if (actual.isValid()) { const QSignalBlocker block(choice); choice->setCurrentIndex(choice->findData(actual)); }
            });
            form->addRow(labelFor(control), choice);
        }
        else if (type == VLC_VAR_INTEGER && control.minimum >= INT_MIN && control.maximum <= INT_MAX
                 && control.minimum <= control.maximum && control.step <= INT_MAX
                 && control.value >= control.minimum && control.value <= control.maximum)
        {
            auto *row = new QWidget(card); auto *layout = new QHBoxLayout(row); layout->setContentsMargins(0, 0, 0, 0);
            auto *slider = new QSlider(Qt::Horizontal, row);
            slider->setObjectName(objectName + QStringLiteral("_slider")); slider->setAccessibleName(control.label);
            auto *number = new QSpinBox(row);
            number->setObjectName(objectName); number->setAccessibleName(control.label);
            number->setKeyboardTracking(false);
            slider->setRange(int(control.minimum), int(control.maximum)); number->setRange(int(control.minimum), int(control.maximum));
            slider->setSingleStep(int(control.step)); number->setSingleStep(int(control.step));
            slider->setValue(int(control.value)); number->setValue(int(control.value));
            number->setProperty("cameraCommitted", int(control.value));
            number->installEventFilter(this);
            if (auto *edit = number->findChild<QLineEdit*>()) { edit->setProperty("cameraSpinBox", QVariant::fromValue<QObject*>(number)); edit->installEventFilter(this); }
            const auto help = qtr("Range: %1 to %2. Step: %3.").arg(control.minimum).arg(control.maximum).arg(control.step);
            number->setAccessibleDescription(help); slider->setAccessibleDescription(help);
            connect(slider, &QSlider::valueChanged, number, &QSpinBox::setValue);
            connect(number, QOverload<int>::of(&QSpinBox::valueChanged), slider, &QSlider::setValue);
            connect(number, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, number, slider, control](int value) {
                const auto actual = applyValue(control.key, VLC_VAR_INTEGER, value);
                if (actual.isValid())
                {
                    const QSignalBlocker a(number), b(slider);
                    number->setValue(actual.toInt()); slider->setValue(actual.toInt());
                    number->setProperty("cameraCommitted", actual);
                }
            });
            layout->addWidget(slider, 1); layout->addWidget(number);
            form->addRow(labelFor(control), row);
        }
        else if (type == VLC_VAR_INTEGER || type == VLC_VAR_STRING)
        {
            auto *row = new QWidget(card); auto *layout = new QVBoxLayout(row); layout->setContentsMargins(0, 0, 0, 0);
            auto *edit = new QLineEdit(type == VLC_VAR_STRING ? control.text : QString::number(control.value), row);
            edit->setObjectName(objectName); edit->setAccessibleName(control.label);
            edit->setProperty("cameraCommitted", edit->text()); edit->installEventFilter(this);
            auto *error = new QLabel(row); error->setTextFormat(Qt::PlainText); error->setWordWrap(true);
            error->setObjectName(objectName + QStringLiteral("_error")); error->hide();
            if (type == VLC_VAR_INTEGER)
                edit->setAccessibleDescription(qtr("Range: %1 to %2. Step: %3.").arg(control.minimum).arg(control.maximum).arg(control.step));
            layout->addWidget(edit); layout->addWidget(error);
            connect(edit, &QLineEdit::textEdited, error, &QLabel::hide);
            connect(edit, &QLineEdit::editingFinished, this, [this, edit, error, control, type] {
                if (edit->text() == edit->property("cameraCommitted").toString()) return;
                QVariant value = edit->text();
                if (type == VLC_VAR_INTEGER)
                {
                    bool ok = false; const auto number = edit->text().toLongLong(&ok);
                    if (!ok || number < control.minimum || number > control.maximum)
                    {
                        error->setText(qtr("Enter a whole number from %1 to %2.").arg(control.minimum).arg(control.maximum)); error->show(); return;
                    }
                    value = qlonglong(number);
                }
                const auto actual = applyValue(control.key, type, value);
                if (actual.isValid()) { edit->setText(actual.toString()); edit->setProperty("cameraCommitted", edit->text()); error->hide(); }
            });
            form->addRow(labelFor(control), row);
        }
        else if (type == VLC_VAR_BOOL)
        {
            auto *check = new QCheckBox(card);
            check->setObjectName(objectName); check->setAccessibleName(control.label); check->setChecked(control.value);
            check->installEventFilter(this);
            connect(check, &QCheckBox::clicked, this, [this, check, control](bool value) {
                const auto actual = applyValue(control.key, VLC_VAR_BOOL, value);
                if (actual.isValid()) { const QSignalBlocker block(check); check->setChecked(actual.toBool()); }
            });
            form->addRow(labelFor(control), check);
        }
        else if (type == VLC_VAR_VOID && (control.type & VLC_VAR_ISCOMMAND))
        {
            auto *button = new QPushButton(card);
            button->setObjectName(objectName); button->setAccessibleName(control.label);
            button->setToolTip(QStringLiteral("<qt>%1</qt>").arg(control.label.toHtmlEscaped()));
            button->setProperty("cameraCaption", control.label); button->installEventFilter(this);
            button->setText(QString(control.label).replace(QStringLiteral("&"), QStringLiteral("&&")));
            button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); button->setMinimumWidth(0);
            connect(button, &QPushButton::clicked, this, [this, control] {
                if (applyValue(control.key, VLC_VAR_VOID).isValid()) QTimer::singleShot(0, this, &ExtV4l2::Refresh);
            });
            form->addRow(button);
        }
        else form->addRow(labelFor(control), new QLabel(qtr("This control is not supported."), card));
    }
    updateMetrics();
    QTimer::singleShot(0, this, [this, scroll, focusName] {
        m_scroll->verticalScrollBar()->setValue(scroll);
        if (!focusName.isEmpty()) if (auto *control = m_content->findChild<QWidget*>(focusName)) control->setFocus();
    });
}

bool ExtV4l2::eventFilter(QObject *object, QEvent *event)
{
    // Keep the application's Stream shortcut out of native value editors.
    if (event->type() == QEvent::ShortcutOverride
        && static_cast<QKeyEvent*>(event)->matches(QKeySequence::Save)
        && (qobject_cast<QLineEdit*>(object) || qobject_cast<QSpinBox*>(object)))
    {
        event->accept(); return true;
    }
    if (auto *button = qobject_cast<QPushButton*>(object); button && object->property("cameraCaption").isValid()
        && (event->type() == QEvent::Resize || event->type() == QEvent::FontChange))
    {
        const auto caption = button->fontMetrics().elidedText(object->property("cameraCaption").toString(), Qt::ElideRight,
            qMax(0, button->width() - button->fontMetrics().height() * 2));
        button->setText(QString(caption).replace(QStringLiteral("&"), QStringLiteral("&&")));
    }
    if (event->type() == QEvent::KeyPress)
    {
        auto *key = static_cast<QKeyEvent*>(event);
        if (auto *button = qobject_cast<QAbstractButton*>(object);
            button && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter))
        {
            if (!key->isAutoRepeat()) button->click();
            key->accept(); return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter
            || key->matches(QKeySequence::Save) || key->key() == Qt::Key_Escape)
        {
            auto *number = qobject_cast<QSpinBox*>(object);
            if (!number) number = qobject_cast<QSpinBox*>(object->property("cameraSpinBox").value<QObject*>());
            if (number)
            {
                if (key->key() == Qt::Key_Escape) { const QSignalBlocker block(number); number->setValue(number->property("cameraCommitted").toInt()); number->clearFocus(); }
                else number->interpretText();
                key->accept(); return true;
            }
            if (auto *edit = qobject_cast<QLineEdit*>(object))
            {
                if (key->key() == Qt::Key_Escape)
                {
                    edit->setText(edit->property("cameraCommitted").toString());
                    if (auto *error = edit->parent()->findChild<QLabel*>(edit->objectName() + QStringLiteral("_error"))) error->hide();
                    edit->clearFocus();
                }
                else QMetaObject::invokeMethod(edit, "editingFinished");
                key->accept(); return true;
            }
        }
    }
    return QWidget::eventFilter(object, event);
}

/**********************************************************************
 * Sliders
 **********************************************************************/

AudioFilterControlWidget::AudioFilterControlWidget
( qt_intf_t *_p_intf, QWidget *parent, const char *_name ) :
    QWidget( parent ), slidersBox( NULL ), p_intf( _p_intf ), name( _name ),
    i_smallfont(0)
{}

void AudioFilterControlWidget::refreshAudioValues(const QHash<QString, QVariant>& pending)
{
    if (m_refreshAudioValues) m_refreshAudioValues(pending);
}

namespace {
double audioFilterValue(float value)
{
    // Do not expose binary float noise in values such as 0.3 or 30 percent.
    return QString::number(value, 'g', 7).toDouble();
}

class AudioFilterSpinBox final : public QDoubleSpinBox
{
public:
    explicit AudioFilterSpinBox(QWidget* parent) : QDoubleSpinBox(parent)
    {
        lineEdit()->installEventFilter(this);
        connect(lineEdit(), &QLineEdit::textEdited, this, [this] { m_draft = true; m_draftApplied = false; m_draftValid = hasAcceptableInput(); });
        connect(this, &QDoubleSpinBox::valueChanged, this, [this] { if (m_draft) m_draftApplied = true; });
        connect(this, &QDoubleSpinBox::editingFinished, this, [this] {
            const bool apply = m_draft && m_draftValid && !m_draftApplied;
            m_draft = false; m_draftApplied = false;
            // Explicitly keeping the current value must also allow Save when
            // another control changed that live value without saving defaults.
            if (apply) emit valueChanged(value());
        });
    }
    void setResetValue(QSlider* slider, double value) { m_slider = slider; m_reset = value; slider->installEventFilter(this); }
    void resetValue() { setValue(m_reset); }
    void updateCurrent(double current)
    {
        current = qBound(minimum(), QString::number(current, 'f', decimals()).toDouble(), maximum());
        auto* input = lineEdit();
        const bool draft = input->isModified();
        if (!draft) { m_currentChanged = false; m_draft = false; m_draftApplied = false; }
        if (value() == current) return;
        const auto text = input->text();
        const int cursor = input->cursorPosition(), selection = input->selectionStart(), length = input->selectedText().size();
        const QSignalBlocker block(this);
        setValue(current);
        if (draft) {
            input->setText(text); input->setCursorPosition(cursor);
            if (selection >= 0) input->setSelection(selection, length);
            input->setModified(true); m_currentChanged = true;
        }
    }
    bool currentChangedWhileTyping() const { return lineEdit()->isModified() && m_currentChanged; }
    QString currentText() const { return !specialValueText().isEmpty() && value() == minimum() ? specialValueText() : textFromValue(value()) + suffix(); }
protected:
    QString textFromValue(double value) const override
    {
        auto format = locale();
        format.setNumberOptions(format.numberOptions() | QLocale::OmitGroupSeparator);
        auto text = format.toString(value, 'f', decimals());
        while (text.contains(format.decimalPoint()) && text.endsWith(format.zeroDigit())) text.chop(format.zeroDigit().size());
        if (text.endsWith(format.decimalPoint())) text.chop(format.decimalPoint().size());
        return text;
    }
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::ShortcutOverride) {
            const auto key = static_cast<QKeyEvent*>(event)->key();
            if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Escape || static_cast<QKeyEvent*>(event)->matches(QKeySequence::Save)) {
                event->accept();
                return true;
            }
        }
        return QDoubleSpinBox::event(event);
    }
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == m_slider && event->type() == QEvent::MouseButtonDblClick) { resetValue(); return true; }
        if (watched == lineEdit() && event->type() == QEvent::KeyPress && handleKey(static_cast<QKeyEvent*>(event))) return true;
        return QDoubleSpinBox::eventFilter(watched, event);
    }
    void keyPressEvent(QKeyEvent* event) override
    {
        if (!handleKey(event)) QDoubleSpinBox::keyPressEvent(event);
    }
private:
    QPointer<QSlider> m_slider;
    double m_reset = 0;
    bool m_currentChanged = false;
    bool m_draft = false;
    bool m_draftApplied = false;
    bool m_draftValid = false;
    bool handleKey(QKeyEvent* event)
    {
        if (event->matches(QKeySequence::Save)) {
            if (hasAcceptableInput()) {
                interpretText(); lineEdit()->setModified(false); emit editingFinished();
                if (auto* save = window()->findChild<QPushButton*>(QStringLiteral("saveEffects")); save && save->isVisible()) save->click();
            }
        } else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            if (hasAcceptableInput()) { interpretText(); lineEdit()->setModified(false); emit editingFinished(); }
            lineEdit()->selectAll();
        } else if (event->key() == Qt::Key_Escape) {
            m_draft = false; m_draftApplied = false;
            setValue(value());
            lineEdit()->setModified(false);
            clearFocus();
        } else return false;
        event->accept();
        return true;
    }
};

const char* audioFilterTitle(const QString& name)
{
    if (name == QLatin1String("compressor")) return N_("Compressor");
    if (name == QLatin1String("expander")) return N_("Expander");
    if (name == QLatin1String("limiter")) return N_("Limiter");
    if (name == QLatin1String("spatializer")) return N_("Spatializer");
    if (name == QLatin1String("stereo_widen")) return N_("Stereo widening");
    if (name == QLatin1String("scaletempo_pitch")) return N_("Pitch shift");
    return N_("Stereo pan");
}

const char* audioFilterDescription(const QString& name)
{
    if (name == QLatin1String("compressor")) return N_("Reduce the level of loud sounds.");
    if (name == QLatin1String("expander")) return N_("Reduce the level of quiet sounds.");
    if (name == QLatin1String("limiter")) return N_("Keep loud sounds below a set level.");
    if (name == QLatin1String("spatializer")) return N_("Add room sound to the audio.");
    if (name == QLatin1String("stereo_widen")) return N_("Change how wide stereo sound is.");
    if (name == QLatin1String("scaletempo_pitch")) return N_("Change the pitch of the sound.");
    return N_("Move sound to the left or right.");
}
}

void ExtVideo::buildControls()
{
    struct Section { QString module, type; QWidget* body; QCheckBox* toggle; QLabel* title; QLabel* help; QLabel* missing; QPushButton* reset; QGroupBox* group; QCheckBox* original; bool available; };
    struct Control { QWidget* original; AudioFilterSpinBox* number; QLabel* label; QLabel* notice; QLabel* source; QString key, module; int type; double factor, sliderScale; QGridLayout* row; QToolButton* swatch; };
    QList<Section> sections;
    QList<Control> controls;
    QList<QVBoxLayout*> pageLayouts, cardLayouts;
    QList<QLabel*> hints;
    QList<QScrollArea*> scrolls;
    QFrame* cropCard = nullptr;
    QLabel* cropCaption = nullptr;
    QPushButton* cropReset = nullptr;
    applyRedesignWidgetTheme(m_tabs, p_intf->p_mi);
    QHash<QWidget*, QLabel*> labels;
    for (auto* label : m_tabs->findChildren<QLabel*>()) if (label->buddy()) labels.insert(label->buddy(), label);
    const auto display = [](const Control& control, const QVariant& value) {
        return control.type == VLC_VAR_INTEGER ? QStringLiteral("%1").arg(value.toLongLong(), 6, 16, QLatin1Char('0')).toUpper() : value.toString();
    };
    const auto apply = [this](const Control& control, const QVariant& value) {
        setFilterOption(qtu(control.module), qtu(control.key), value.toInt(), value.toDouble(), qtu(value.toString()));
    };
    const auto description = [](const QString& module) -> const char* {
        if (module == QLatin1String("adjust")) return "";
        if (module == QLatin1String("sharpen")) return N_("Make edges more clear.");
        if (module == QLatin1String("gradfun")) return N_("Reduce visible color bands.");
        if (module == QLatin1String("grain")) return N_("Add grain to the image.");
        if (module == QLatin1String("extract")) return N_("Keep one color from the image.");
        if (module == QLatin1String("gradient")) return N_("Show edges and color changes.");
        if (module == QLatin1String("colorthres")) return N_("Keep colors within a set range.");
        if (module == QLatin1String("sepia")) return N_("Give the image a warm brown color.");
        if (module == QLatin1String("transform")) return N_("Turn or flip the image.");
        if (module == QLatin1String("rotate")) return N_("Turn the image by a set angle.");
        if (module == QLatin1String("puzzle")) return N_("Split the image into movable tiles.");
        if (module == QLatin1String("magnify")) return N_("Zoom into part of the image.");
        if (module == QLatin1String("logo")) return N_("Place an image over the video.");
        if (module == QLatin1String("marq")) return N_("Place text over the video.");
        if (module == QLatin1String("erase")) return N_("Hide part of the image with a mask.");
        if (module == QLatin1String("gaussianblur")) return N_("Soften the image.");
        if (module == QLatin1String("motionblur")) return N_("Add blur to motion.");
        if (module == QLatin1String("antiflicker")) return N_("Reduce changes in image brightness.");
        if (module == QLatin1String("hqdn3d")) return N_("Reduce noise in the image.");
        if (module == QLatin1String("clone") || module == QLatin1String("wall")) return N_("Select Save, then open the video again.");
        return "";
    };
    std::function<void(QLayout*, QList<QWidget*>&)> collect = [&](QLayout* layout, QList<QWidget*>& found) {
        for (int i = 0; i < layout->count(); ++i) {
            auto* item = layout->itemAt(i);
            if (item->layout()) collect(item->layout(), found);
            else if (auto* widget = item->widget(); widget && (qobject_cast<QGroupBox*>(widget) || qobject_cast<QCheckBox*>(widget))) found.append(widget);
        }
    };
    for (int pageIndex = 0; pageIndex < m_tabs->count(); ++pageIndex) {
        auto* page = m_tabs->widget(pageIndex);
        auto* oldLayout = page->layout();
        QList<QWidget*> originals;
        if (pageIndex != 1) collect(oldLayout, originals);
        QWidget* cropBody = nullptr;
        if (pageIndex == 1) { cropBody = new QWidget(page); cropBody->setLayout(oldLayout); }
        else delete oldLayout;
        auto* outer = new QVBoxLayout(page); pageLayouts.append(outer);
        auto* scroll = new QScrollArea(page); scrolls.append(scroll);
        scroll->setObjectName(QStringLiteral("videoEffectsScroll%1").arg(pageIndex));
        scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* content = new QWidget(scroll); auto* list = new QVBoxLayout(content); cardLayouts.append(list); list->setContentsMargins(0, 0, 0, 0);
        scroll->setWidget(content); outer->addWidget(scroll, 1);
        auto* hint = new QLabel(page); hint->setWordWrap(true); hints.append(hint); outer->addWidget(hint);
        if (cropBody) {
            cropCard = new QFrame(content); cropCard->setProperty("redesignCard", true); auto* layout = new QVBoxLayout(cropCard);
            cropCaption = new QLabel(qtr("Crop"), cropCard); cropCaption->setProperty("redesignSectionHeading", true); layout->addWidget(cropCaption); layout->addWidget(cropBody);
            cropReset = new QPushButton(qtr("Reset crop"), cropCard); cropReset->setObjectName(QStringLiteral("resetVideoCrop")); cropReset->setAutoDefault(false); layout->addWidget(cropReset, 0, Qt::AlignRight);
            list->addWidget(cropCard); list->addStretch();
            for (auto* field : {ui.cropTopPx, ui.cropBotPx, ui.cropLeftPx, ui.cropRightPx}) {
                field->setKeyboardTracking(false); field->setProperty("videoCropField", true); field->installEventFilter(this);
                auto* input = field->findChild<QLineEdit*>(); input->setProperty("videoCropField", true); input->installEventFilter(this);
            }
            connect(cropReset, &QPushButton::clicked, this, [this] {
                const QSignalBlocker a(ui.cropTopPx), b(ui.cropBotPx), c(ui.cropLeftPx), d(ui.cropRightPx);
                for (auto* field : {ui.cropTopPx, ui.cropBotPx, ui.cropLeftPx, ui.cropRightPx}) field->setValue(0);
                cropChange();
            });
            continue;
        }
        for (auto* original : originals) {
            auto* group = qobject_cast<QGroupBox*>(original);
            auto* originalCheck = qobject_cast<QCheckBox*>(original);
            const auto module = ModuleFromWidgetName(original);
            const bool available = module_exists(qtu(module));
            QString type = module == QLatin1String("clone") || module == QLatin1String("wall") ? QStringLiteral("video-splitter")
                : module == QLatin1String("logo") || module == QLatin1String("marq") ? QStringLiteral("sub-source") : QStringLiteral("video-filter");
            if (available) if (const auto* actual = GetVFilterType(p_intf, qtu(module))) type = qfu(actual);
            m_filterTypes.insert(module, type);
            auto* card = new QFrame(content); card->setObjectName(module + QStringLiteral("Card")); card->setProperty("redesignCard", true);
            auto* layout = new QVBoxLayout(card); cardLayouts.append(layout);
            auto* heading = new QHBoxLayout;
            auto* title = new QLabel(group ? group->title() : originalCheck->text(), card); title->setWordWrap(true); title->setProperty("redesignSectionHeading", true);
            auto* toggle = group ? new QCheckBox(card) : originalCheck;
            toggle->setObjectName(group ? module + QStringLiteral("Toggle") : originalCheck->objectName());
            heading->addWidget(title, 1); heading->addWidget(toggle); layout->addLayout(heading);
            auto* help = new QLabel(card); help->setWordWrap(true); layout->addWidget(help);
            auto* missing = new QLabel(card); missing->setObjectName(module + QStringLiteral("Unavailable")); missing->setWordWrap(true); layout->addWidget(missing);
            QWidget* body = group ? static_cast<QWidget*>(group) : new QWidget(card);
            if (group) {
                group->setTitle({}); group->setCheckable(false); group->setFocusPolicy(Qt::NoFocus); group->setProperty("videoEffectBody", true);
                delete group->layout();
                for (auto* child : group->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly)) child->hide();
            }
            body->setMinimumSize(0, 0); body->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            auto* fields = new QVBoxLayout(body); fields->setContentsMargins(0, 0, 0, 0); cardLayouts.append(fields); layout->addWidget(body);
            QList<Control> sectionControls;
            for (auto* option : m_options) {
                if (m_modules.value(option) != module) continue;
                const auto key = OptionFromWidgetName(option);
                const auto* config = config_FindConfig(qtu(key));
                if (!config) continue;
                const int valueType = config_GetType(qtu(key)) & VLC_VAR_CLASS;
                auto* row = new QWidget(body); auto* grid = new QGridLayout(row); grid->setContentsMargins(0, 0, 0, 0);
                auto* label = new QLabel(row); label->setWordWrap(true);
                auto* notice = new QLabel(row); notice->setTextFormat(Qt::PlainText); notice->setObjectName(key + QStringLiteral("CurrentNotice")); notice->setWordWrap(true); notice->hide();
                AudioFilterSpinBox* number = nullptr;
                QToolButton* swatch = nullptr;
                auto* slider = qobject_cast<QSlider*>(option); auto* dial = qobject_cast<VLCQDial*>(option); auto* spin = qobject_cast<QSpinBox*>(option);
                if (slider || dial || spin) {
                    number = new AudioFilterSpinBox(row); number->setObjectName(key + QStringLiteral("Value")); number->setKeyboardTracking(false);
                    number->setDecimals(valueType == VLC_VAR_FLOAT ? 6 : 0);
                    const double sliderScale = slider && valueType == VLC_VAR_FLOAT ? slider->tickInterval() : 1.0;
                    double minimum = slider ? slider->minimum() / sliderScale : spin ? spin->minimum() : 0;
                    double maximum = slider ? slider->maximum() / sliderScale : spin ? spin->maximum() : 360;
                    const double nativeMin = valueType == VLC_VAR_FLOAT ? config->min.f : config->min.i;
                    const double nativeMax = valueType == VLC_VAR_FLOAT ? config->max.f : config->max.i;
                    if (nativeMax > nativeMin) { minimum = qMax(minimum, nativeMin); maximum = qMin(maximum, nativeMax); }
                    if (valueType == VLC_VAR_INTEGER) { minimum = qMax(minimum, double(INT_MIN)); maximum = qMin(maximum, double(INT_MAX)); }
                    if (key == QLatin1String("logo-x") || key == QLatin1String("logo-y")) { minimum = -1; number->setSpecialValueText(qtr("Default")); }
                    const double factor = key == QLatin1String("logo-opacity") ? 100.0 / 255.0 : 1.0;
                    number->setRange(minimum * factor, maximum * factor); number->setSingleStep(factor != 1 ? 1 : slider ? 1.0 / sliderScale : 1.0);
                    if (factor != 1) number->setSuffix(QStringLiteral("%"));
                    if (spin) number->setSuffix(spin->suffix());
                    const double reset = valueType == VLC_VAR_FLOAT ? config->orig.f : config->orig.i;
                    if (slider) { slider->setTickPosition(QSlider::NoTicks); number->setResetValue(slider, reset * factor); }
                    number->setValue(currentValue(key).toDouble() * factor); label->setBuddy(number);
                    grid->addWidget(label, 0, 0); grid->addWidget(number, 0, 1);
                    if (!spin) { grid->addWidget(option, 1, 0, 1, 2); option->show(); }
                    else option->hide();
                } else {
                    label->setBuddy(option);
                    if (auto* check = qobject_cast<QCheckBox*>(option)) { label->hide(); grid->addWidget(check, 0, 0, 1, 2); }
                    else { grid->addWidget(label, 0, 0); grid->addWidget(option, 0, 1); }
                    option->show();
                    if (auto* input = qobject_cast<QLineEdit*>(option)) {
                        input->setProperty("videoEffectKey", key); input->installEventFilter(this);
                        if (valueType == VLC_VAR_INTEGER) input->setValidator(new VideoColorValidator(config->min.i, config->max.i, input));
                        connect(input, &QLineEdit::textEdited, this, &ExtVideo::draftChanged);
                        if (valueType == VLC_VAR_INTEGER) {
                            input->setPlaceholderText(QStringLiteral("RRGGBB"));
                            swatch = new QToolButton(row); swatch->setObjectName(key + QStringLiteral("ColorButton"));
                            grid->addWidget(swatch, 0, 2);
                            connect(swatch, &QToolButton::clicked, this, [this, input, key] {
                                const QColor initial = QColor::fromRgb(currentValue(key).toUInt() & 0xffffff);
                                const QColor chosen = QColorDialog::getColor(initial, m_tabs, qtr("Choose color"));
                                if (!chosen.isValid()) return;
                                input->setText(chosen.name().mid(1).toUpper()); input->setModified(true);
                                if (input->hasAcceptableInput()) QMetaObject::invokeMethod(input, "editingFinished");
                                emit draftChanged();
                            });
                        }
                    }
                    auto* browse = key == QLatin1String("logo-file") ? ui.logoBrowseBtn : key == QLatin1String("erase-mask") ? ui.eraseBrowseBtn : nullptr;
                    if (browse) { grid->addWidget(browse, 0, 2); browse->show(); }
                }
                const bool compactInput = number || (valueType == VLC_VAR_INTEGER && qobject_cast<QLineEdit*>(option));
                grid->setColumnStretch(0, compactInput ? 1 : 0); grid->setColumnStretch(1, compactInput ? 0 : 1);
                grid->addWidget(notice, 2, 0, 1, 3); fields->addWidget(row);
                Control record {option, number, label, notice, labels.value(option), key, module, valueType, key == QLatin1String("logo-opacity") ? 100.0 / 255.0 : 1.0, slider && valueType == VLC_VAR_FLOAT ? double(slider->tickInterval()) : 1.0, grid, swatch};
                controls.append(record); sectionControls.append(record);
                if (number) {
                    connect(number, &QDoubleSpinBox::valueChanged, this, [this, record](double displayed) {
                        const double value = displayed / record.factor;
                        setFilterOption(qtu(record.module), qtu(record.key), qRound(value), value, nullptr);
                        const QSignalBlocker block(record.original);
                        if (auto* slider = qobject_cast<QSlider*>(record.original)) slider->setValue(qRound(value * record.sliderScale));
                        else if (auto* dial = qobject_cast<VLCQDial*>(record.original)) dial->setValue((360 - qRound(value)) % 360);
                        else if (auto* spin = qobject_cast<QSpinBox*>(record.original)) spin->setValue(qRound(value));
                    });
                    if (slider) connect(slider, &QSlider::valueChanged, number, [number, slider, record](int position) { const QSignalBlocker block(number); number->setValue(position / record.sliderScale * record.factor); });
                    if (dial) connect(dial, &VLCQDial::valueChanged, number, [number](int position) { const QSignalBlocker block(number); number->setValue((360 - position) % 360); });
                    connect(number->findChild<QLineEdit*>(), &QLineEdit::textEdited, this, &ExtVideo::draftChanged);
                    connect(number, &QDoubleSpinBox::editingFinished, this, &ExtVideo::draftChanged);
                }
            }
            auto* reset = new QPushButton(card); reset->setObjectName(module + QStringLiteral("Reset")); reset->setAutoDefault(false); layout->addWidget(reset, 0, Qt::AlignRight);
            reset->setVisible(!sectionControls.isEmpty()); body->setVisible(!sectionControls.isEmpty());
            layout->removeWidget(reset); heading->insertWidget(1, reset);
            connect(reset, &QPushButton::clicked, this, [this, sectionControls, display, apply] {
                for (const auto& record : sectionControls) {
                    const auto* config = config_FindConfig(qtu(record.key)); if (!config) continue;
                    const QVariant value = record.type == VLC_VAR_FLOAT ? QVariant(config->orig.f) : record.type == VLC_VAR_STRING ? QVariant(qfu(config->orig.psz ? config->orig.psz : "")) : QVariant::fromValue(config->orig.i);
                    const QSignalBlocker block(record.original);
                    if (record.number) { const QSignalBlocker editorBlock(record.number); record.number->setValue(value.toDouble() * record.factor); record.number->findChild<QLineEdit*>()->setModified(false); }
                    else if (auto* input = qobject_cast<QLineEdit*>(record.original)) { input->setText(display(record, value)); input->setModified(false); }
                    else if (auto* check = qobject_cast<QCheckBox*>(record.original)) check->setChecked(value.toBool());
                    else if (auto* combo = qobject_cast<QComboBox*>(record.original)) combo->setCurrentIndex(combo->findData(value));
                    apply(record, value);
                }
                emit draftChanged();
            });
            if (group) connect(toggle, &QCheckBox::clicked, this, [this, module](bool enabled) { changeVFiltersString(qtu(module), enabled); });
            connect(toggle, &QCheckBox::toggled, body, [body, reset, available](bool checked) { body->setEnabled(available && checked); reset->setEnabled(available && checked); });
            sections.append({module, type, body, toggle, title, help, missing, reset, group, originalCheck, available});
            list->addWidget(card);
        }
        list->addStretch();
    }
    m_layoutControls = [this, controls] {
        const double scale = p_intf->p_mi ? p_intf->p_mi->getIntfScaleFactor() : 1.0;
        for (const auto& control : controls) {
            if (!control.number || !qobject_cast<QSlider*>(control.original)) continue;
            const bool compact = control.row->parentWidget()->width() >= qRound(500 * scale);
            if (control.row->property("videoInline").isValid() && control.row->property("videoInline").toBool() == compact) continue;
            control.row->setProperty("videoInline", compact);
            control.row->removeWidget(control.label); control.row->removeWidget(control.number); control.row->removeWidget(control.original);
            control.row->addWidget(control.label, 0, 0);
            control.row->addWidget(control.number, 0, compact ? 2 : 1);
            control.row->addWidget(control.original, compact ? 0 : 1, compact ? 1 : 0, 1, compact ? 1 : 2);
            control.row->setColumnStretch(0, compact ? 0 : 1); control.row->setColumnStretch(1, compact ? 1 : 0); control.row->setColumnStretch(2, 0);
        }
    };
    for (auto* scroll : scrolls) { scroll->viewport()->setProperty("videoEffectsViewport", true); scroll->viewport()->installEventFilter(this); }
    QWidget* previous = m_tabs->tabBar();
    const auto appendTabStop = [&previous](QWidget* widget) {
        QWidget::setTabOrder(previous, widget); previous = widget;
    };
    for (int page = 0; page < m_tabs->count(); ++page) {
        if (page == 1) {
            for (auto* field : {ui.cropTopPx, ui.cropBotPx, ui.cropLeftPx, ui.cropRightPx}) appendTabStop(field);
            appendTabStop(ui.topBotCropSync); appendTabStop(ui.leftRightCropSync); appendTabStop(cropReset);
            continue;
        }
        for (const auto& section : sections) {
            if (!m_tabs->widget(page)->isAncestorOf(section.body)) continue;
            appendTabStop(section.toggle);
            for (const auto& control : controls) {
                if (control.module != section.module) continue;
                appendTabStop(control.number ? static_cast<QWidget*>(control.number) : control.original);
                if (control.number && !qobject_cast<QSpinBox*>(control.original)) appendTabStop(control.original);
                if (control.swatch) appendTabStop(control.swatch);
                auto* browse = control.key == QLatin1String("logo-file") ? ui.logoBrowseBtn : control.key == QLatin1String("erase-mask") ? ui.eraseBrowseBtn : nullptr;
                if (browse) appendTabStop(browse);
            }
            appendTabStop(section.reset);
        }
    }
    m_hasDraft = [controls] {
        for (const auto& control : controls) {
            auto* editor = control.number ? static_cast<QWidget*>(control.number) : control.original;
            if (!editor->isEnabled() || !editor->isVisible()) continue;
            if (control.number) { if (control.number->findChild<QLineEdit*>()->isModified() && control.number->hasAcceptableInput()) return true; }
            else if (auto* input = qobject_cast<QLineEdit*>(editor); input && input->isModified() && input->hasAcceptableInput()) return true;
        }
        return false;
    };
    m_commitDrafts = [this, controls] {
        for (const auto& control : controls) {
            auto* editor = control.number ? static_cast<QWidget*>(control.number) : control.original;
            if (!editor->isEnabled() || !editor->isVisible()) continue;
            if (control.number && control.number->findChild<QLineEdit*>()->isModified()) {
                if (!control.number->hasAcceptableInput()) { control.number->setFocus(); return false; }
                control.number->interpretText(); control.number->findChild<QLineEdit*>()->setModified(false);
                QMetaObject::invokeMethod(control.number, "editingFinished");
            } else if (auto* input = qobject_cast<QLineEdit*>(editor); input && input->isModified()) {
                if (!input->hasAcceptableInput()) { input->setFocus(); return false; }
                QMetaObject::invokeMethod(input, "editingFinished"); input->setModified(false);
            }
        }
        return true;
    };
    m_refreshVideoValues = [this, controls, sections, hints, cropCard, cropReset, display](const QHash<QString, QVariant>& pending) {
        // Disable transitions must not commit a draft through focus loss.
        std::vector<QSignalBlocker> blocks;
        blocks.reserve(controls.size() * 2);
        for (const auto& control : controls) { blocks.emplace_back(control.original); if (control.number) blocks.emplace_back(control.number); }
        for (const auto& section : sections) {
            const auto chain = currentValue(section.type, pending).toString();
            const bool checked = filterIsPresent(chain, section.module);
            const QSignalBlocker block(section.toggle);
            section.toggle->setChecked(checked); section.toggle->setEnabled(section.available || checked);
            section.body->setEnabled(section.available && checked); section.reset->setEnabled(section.available && checked);
            section.missing->setVisible(!section.available);
        }
        for (const auto& control : controls) {
            const auto value = currentValue(control.key, pending); if (!value.isValid() || (control.type == VLC_VAR_FLOAT && !std::isfinite(value.toDouble()))) continue;
            bool conflict = false; QString current;
            if (control.number) {
                control.number->updateCurrent((control.type == VLC_VAR_FLOAT ? audioFilterValue(value.toFloat()) : value.toDouble()) * control.factor);
                conflict = control.number->currentChangedWhileTyping(); current = control.number->currentText();
                if (auto* slider = qobject_cast<QSlider*>(control.original)) slider->setValue(qRound(value.toDouble() * control.sliderScale));
                else if (auto* dial = qobject_cast<VLCQDial*>(control.original)) dial->setValue((360 - qRound(value.toDouble())) % 360);
            } else if (auto* input = qobject_cast<QLineEdit*>(control.original)) {
                current = display(control, value);
                if (input->isModified() && input->property("videoEffectCurrent").isValid() && input->property("videoEffectCurrent").toString() != current) input->setProperty("videoEffectConflict", true);
                if (!input->isModified()) { input->setProperty("videoEffectConflict", false); input->setText(current); }
                input->setProperty("videoEffectCurrent", current);
                conflict = input->isModified() && input->property("videoEffectConflict").toBool();
            } else if (auto* check = qobject_cast<QCheckBox*>(control.original)) check->setChecked(value.toBool());
            else if (auto* combo = qobject_cast<QComboBox*>(control.original)) combo->setCurrentIndex(combo->findData(value));
            if (control.swatch) {
                QColor color = QColor::fromRgb(value.toUInt() & 0xffffff);
                auto* input = qobject_cast<QLineEdit*>(control.original);
                auto draft = input->text(); if (!draft.startsWith(QLatin1Char('#'))) draft.prepend(QLatin1Char('#'));
                if (input->isModified() && QColor(draft).isValid()) color = QColor(draft);
                const int size = qRound(32 * (p_intf->p_mi ? p_intf->p_mi->getIntfScaleFactor() : 1.0));
                const auto style = QStringLiteral("QToolButton { background: %1; min-width: %2px; max-width: %2px; min-height: %2px; max-height: %2px; padding: 0; border: 1px solid palette(mid); border-radius: 4px; } QToolButton:hover, QToolButton:focus { border-color: palette(text); }").arg(color.name()).arg(size);
                if (control.swatch->styleSheet() != style) control.swatch->setStyleSheet(style);
            }
            control.notice->setText(qtr("Current value: %1. Press Escape to use it.").arg(current));
            if (auto* input = qobject_cast<QLineEdit*>(control.original); input && control.type == VLC_VAR_INTEGER && input->isModified() && !input->hasAcceptableInput()) {
                const auto digits = QString(input->text()).remove(QLatin1Char('#'));
                control.notice->setText(digits == QLatin1String("000000")
                    ? qtr("Use a color other than black. Press Escape to use %1.").arg(current)
                    : qtr("Enter six color digits. Press Escape to use %1.").arg(current));
                conflict = true;
            }
            control.notice->setVisible(conflict);
            (control.number ? static_cast<QWidget*>(control.number) : control.original)->setAccessibleDescription(conflict ? control.notice->text() : QString());
        }
        const auto output = THEMIM->getVout();
        cropCard->setEnabled(bool(output)); cropReset->setEnabled(bool(output));
        const QSignalBlocker a(ui.cropTopPx), b(ui.cropBotPx), c(ui.cropLeftPx), d(ui.cropRightPx), e(ui.topBotCropSync), f(ui.leftRightCropSync);
        if (output) {
            for (const auto& pair : {qMakePair(ui.cropTopPx, "crop-top"), qMakePair(ui.cropBotPx, "crop-bottom"), qMakePair(ui.cropLeftPx, "crop-left"), qMakePair(ui.cropRightPx, "crop-right")}) {
                auto* field = pair.first;
                const int current = var_GetInteger(output.get(), pair.second);
                auto* input = field->findChild<QLineEdit*>(); input->setProperty("videoCropCurrent", current);
                if (!input->isModified()) field->setValue(current);
            }
            if (ui.cropTopPx->value() != ui.cropBotPx->value()) ui.topBotCropSync->setChecked(false);
            if (ui.cropLeftPx->value() != ui.cropRightPx->value()) ui.leftRightCropSync->setChecked(false);
            ui.cropBotPx->setEnabled(!ui.topBotCropSync->isChecked()); ui.cropRightPx->setEnabled(!ui.leftRightCropSync->isChecked());
        }
        for (int i = 0; i < hints.size(); ++i) hints[i]->setText(i == 1 ? output ? qtr("Crop affects the current video.") : qtr("Open a video to crop it.")
            : output ? qtr("Changes apply now. Select Save to keep them.") : qtr("Select Save to use these settings when you open a video."));
    };
    const auto refreshText = [this, controls, sections, description, cropCaption, cropReset] {
        struct Draft { QLineEdit* input; QString text; int cursor, selection, length; bool modified, preserve; };
        QList<Draft> inputs;
        std::vector<QSignalBlocker> blockers;
        for (auto* input : m_tabs->findChildren<QLineEdit*>()) {
            inputs.append({input, input->text(), input->cursorPosition(), input->selectionStart(), int(input->selectedText().size()), input->isModified(), input->isModified() || m_options.contains(input)});
            blockers.emplace_back(input);
        }
        ui.retranslateUi(m_tabs);
        for (const auto& section : sections) {
            const auto title = section.group ? section.group->title() : section.original->text();
            section.title->setText(title); if (section.group) section.group->setTitle({});
            section.toggle->setText(qtr("Enable")); section.toggle->setAccessibleName(qtr("Enable %1").arg(title));
            const auto* help = description(section.module); section.help->setText(*help ? qtr(help) : QString()); section.help->setVisible(*help);
            section.missing->setText(qtr("This video effect is not available in this build."));
            section.reset->setText(qtr("Reset values")); section.reset->setAccessibleName(qtr("Reset %1").arg(title));
        }
        for (const auto& control : controls) {
            const auto* config = config_FindConfig(qtu(control.key));
            const auto text = control.source ? control.source->text() : config ? qfut(config->psz_text) : control.key;
            auto caption = text;
            if (control.key == QLatin1String("logo-opacity")) caption = qtr("Opacity");
            else if (control.key == QLatin1String("sharpen-sigma")) caption = qtr("Strength");
            else if (control.key == QLatin1String("gaussianblur-sigma") || control.key == QLatin1String("blur-factor")) caption = qtr("Blur amount");
            else if (control.key == QLatin1String("grain-variance")) caption = qtr("Grain amount");
            else if (control.key == QLatin1String("extract-component") || control.key == QLatin1String("colorthres-color")) caption = qtr("Color (RRGGBB)");
            control.label->setText(caption);
            if (control.number) {
                control.number->setAccessibleName(caption);
                if (control.key == QLatin1String("logo-x") || control.key == QLatin1String("logo-y")) control.number->setSpecialValueText(qtr("Default"));
            }
            control.original->setAccessibleName(caption);
            if (control.swatch) { control.swatch->setAccessibleName(qtr("Choose color")); control.swatch->setToolTip(qtr("Choose color")); }
            const char* tip = nullptr;
            if (control.key == QLatin1String("hue")) tip = N_("Shift the colors in the image.");
            else if (control.key == QLatin1String("brightness")) tip = N_("Change the light level in the image.");
            else if (control.key == QLatin1String("contrast")) tip = N_("Change the difference between light and dark areas.");
            else if (control.key == QLatin1String("saturation")) tip = N_("Change how strong colors are.");
            else if (control.key == QLatin1String("gamma")) tip = N_("Change the light level in dark areas.");
            if (tip) { control.label->setToolTip(qtr(tip)); if (control.number) control.number->setToolTip(qtr(tip)); }
            if (auto* combo = qobject_cast<QComboBox*>(control.original)) {
                const QSignalBlocker block(combo); const auto value = combo->currentData();
                combo->clear(); initComboBoxItems(combo); combo->setCurrentIndex(combo->findData(value));
            }
        }
        cropCaption->setText(qtr("Crop")); cropReset->setText(qtr("Reset crop"));
        ui.logoBrowseBtn->setText(qtr("Choose file")); ui.eraseBrowseBtn->setText(qtr("Choose file"));
        for (const auto& saved : inputs) {
            if (!saved.preserve) continue;
            saved.input->setText(saved.text); saved.input->setCursorPosition(saved.cursor);
            if (saved.selection >= 0) saved.input->setSelection(saved.selection, saved.length);
            saved.input->setModified(saved.modified);
        }
    };
    const auto metrics = [this, controls, pageLayouts, cardLayouts, scrolls] {
        const auto dp = [this](int value) { return qRound(value * (p_intf->p_mi ? p_intf->p_mi->getIntfScaleFactor() : 1.0)); };
        for (auto* layout : pageLayouts) { layout->setContentsMargins(dp(16), dp(16), dp(16), dp(12)); layout->setSpacing(dp(12)); }
        for (auto* layout : cardLayouts) { layout->setSpacing(dp(12)); if (qobject_cast<QFrame*>(layout->parentWidget())) layout->setContentsMargins(dp(12), dp(12), dp(12), dp(12)); }
        for (auto* area : scrolls) area->setMinimumHeight(dp(140));
        for (const auto& control : controls) {
            if (control.number) control.number->setFixedWidth(dp(140));
            else if (control.type == VLC_VAR_INTEGER && qobject_cast<QLineEdit*>(control.original)) control.original->setFixedWidth(dp(160));
            if (auto* dial = qobject_cast<VLCQDial*>(control.original)) dial->setFixedSize(dp(100), dp(100));
        }
    };
    if (p_intf->p_mi) connect(p_intf->p_mi, &MainCtx::intfScaleFactorChanged, this, [this, metrics] { metrics(); if (m_layoutControls) m_layoutControls(); });
    if (auto* translator = InterfaceTranslator::instance()) connect(translator, &InterfaceTranslator::languageChanged, this, refreshText);
    refreshText(); metrics(); refreshVideoValues({}); m_layoutControls();
}

void AudioFilterControlWidget::build()
{
    setObjectName(QStringLiteral("audioFilterControls"));
    setProperty("audioFilterName", name);
    auto* ctx = p_intf->p_mi;
    applyRedesignWidgetTheme(this, ctx);
    auto* layout = new QVBoxLayout(this);
    auto* heading = new QHBoxLayout;
    auto* title = new QLabel(this);
    title->setProperty("redesignSectionHeading", true);
    auto* enabled = new QCheckBox(this);
    enabled->setObjectName(QStringLiteral("audioFilterEnabled"));
    heading->addWidget(title, 1);
    heading->addWidget(enabled);
    layout->addLayout(heading);
    auto* description = new QLabel(this);
    description->setWordWrap(true);
    layout->addWidget(description);
    auto* unavailable = new QLabel(this);
    unavailable->setObjectName(QStringLiteral("audioFilterUnavailable"));
    unavailable->setWordWrap(true);
    const bool available = module_exists(qtu(name));
    unavailable->setVisible(!available);
    enabled->setEnabled(available);
    layout->addWidget(unavailable);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("audioFilterScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget(scroll);
    auto* body = new QVBoxLayout(content);
    body->setContentsMargins(0, 0, 0, 0);
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    QVector<QDoubleSpinBox*> editors;
    QVector<QGridLayout*> rows;
    QVector<QLabel*> notices;
    for (const auto& data : controls)
    {
        auto* card = new QFrame(content);
        card->setProperty("redesignCard", true);
        auto* row = new QGridLayout(card);
        rows.append(row);
        auto* label = new QLabel(card);
        label->setObjectName(data.name + QStringLiteral("Label"));
        label->setWordWrap(true);
        auto* slider = new QSlider(Qt::Horizontal, card);
        slider->setObjectName(data.name + QStringLiteral("Slider"));
        slider->setMinimum(qRound(data.f_min / data.f_resolution));
        slider->setMaximum(qRound(data.f_max / data.f_resolution));
        slider->setSingleStep(1);
        auto* value = new AudioFilterSpinBox(card);
        value->setObjectName(data.name + QStringLiteral("Value"));
        value->setDecimals(6);
        value->setKeyboardTracking(false);
        connect(value->findChild<QLineEdit*>(), &QLineEdit::textEdited, this, &AudioFilterControlWidget::draftChanged);
        connect(value, &QDoubleSpinBox::editingFinished, this, &AudioFilterControlWidget::draftChanged);
        value->setRange(audioFilterValue(data.f_min) * data.f_visual_multiplier, audioFilterValue(data.f_max) * data.f_visual_multiplier);
        value->setSingleStep(audioFilterValue(data.f_resolution) * data.f_visual_multiplier);
        const auto* original = config_FindConfig(qtu(data.name));
        value->setResetValue(slider, audioFilterValue(original && original->i_type == CONFIG_ITEM_FLOAT
            ? original->orig.f : data.f_value) * data.f_visual_multiplier);
        double initial = data.f_value;
        if (const auto output = THEMIM->getAout(); output && (var_Type(output.get(), qtu(data.name)) & VLC_VAR_CLASS) == VLC_VAR_FLOAT)
            initial = var_GetFloat(output.get(), qtu(data.name));
        else if (config_FindConfig(qtu(data.name))) initial = config_GetFloat(qtu(data.name));
        if (!std::isfinite(initial)) initial = data.f_value;
        value->setValue(audioFilterValue(initial) * data.f_visual_multiplier);
        slider->setValue(qRound(initial / data.f_resolution));
        editors.append(value);
        label->setBuddy(value);
        row->addWidget(label, 0, 0);
        row->addWidget(value, 0, 1);
        row->addWidget(slider, 1, 0, 1, 2);
        auto* notice = new QLabel(card);
        notice->setObjectName(data.name + QStringLiteral("CurrentNotice"));
        notice->setWordWrap(true); notice->hide(); notices.append(notice);
        row->addWidget(notice, 2, 0, 1, 2);
        row->setColumnStretch(0, 1);
        body->addWidget(card);
        connect(slider, &QSlider::valueChanged, value, [value, data](int position) {
            value->setValue(position * double(data.f_resolution) * data.f_visual_multiplier);
        });
        connect(value, &QDoubleSpinBox::valueChanged, this, [this, slider, data](double displayed) {
            const float actual = displayed / data.f_visual_multiplier;
            const QSignalBlocker block(slider);
            slider->setValue(qRound(actual / data.f_resolution));
            if (const auto output = THEMIM->getAout()) {
                if (var_Type(output.get(), qtu(data.name)) == 0)
                    var_Create(output.get(), qtu(data.name), VLC_VAR_FLOAT | VLC_VAR_DOINHERIT);
                var_SetFloat(output.get(), qtu(data.name), actual);
            }
            emit configChanged(data.name, QVariant(actual));
        });
    }
    body->addStretch();
    auto* footer = new QHBoxLayout;
    auto* hint = new QLabel(this);
    hint->setWordWrap(true);
    auto* reset = new QPushButton(this);
    reset->setObjectName(QStringLiteral("resetAudioFilter"));
    reset->setAutoDefault(false);
    footer->addWidget(hint, 1);
    footer->addWidget(reset);
    layout->addLayout(footer);
    const auto refreshText = [this, title, description, enabled, unavailable, hint, reset, editors] {
        title->setText(qtr(audioFilterTitle(name)));
        description->setText(qtr(audioFilterDescription(name)));
        enabled->setText(qtr("Enable"));
        enabled->setAccessibleName(qtr("Enable %1").arg(title->text()));
        unavailable->setText(qtr("This audio filter is not available in this build."));
        hint->setText(qtr("Changes apply now. Select Save to keep them."));
        reset->setText(qtr("Reset values"));
        reset->setAccessibleName(qtr("Reset %1").arg(title->text()));
        for (int i = 0; i < controls.size(); ++i) {
            const auto& data = controls[i];
            const auto label = qtr(qtu(data.descs));
            findChild<QLabel*>(data.name + QStringLiteral("Label"))->setText(label);
            auto* editor = editors[i];
            // Keep a value that is still being typed when the language changes.
            auto* input = editor->findChild<QLineEdit*>();
            const bool draft = input->isModified();
            auto text = input->text();
            const auto oldSuffix = editor->suffix();
            if (!oldSuffix.isEmpty() && text.endsWith(oldSuffix)) text.chop(oldSuffix.size());
            const int cursor = input->cursorPosition();
            const int selection = input->selectionStart();
            const int selected = input->selectedText().size();
            const auto space = data.units == QLatin1String(":1") || data.units == QLatin1String("%") ? QString() : QStringLiteral(" ");
            editor->setSuffix(data.units.isEmpty() ? QString() : space + qtr(qtu(data.units)));
            if (draft) {
                input->setText(text + editor->suffix());
                input->setCursorPosition(qMin(cursor, text.size()));
                if (selection >= 0) input->setSelection(selection, qMin(selected, text.size() - selection));
                input->setModified(true);
            }
            editor->setAccessibleName(label);
            findChild<QSlider*>(data.name + QStringLiteral("Slider"))->setAccessibleName(label);
        }
    };
    const auto metrics = [ctx, layout, body, rows, editors, scroll] {
        const auto dp = [ctx](int n) { return qRound(n * (ctx ? ctx->getIntfScaleFactor() : 1.0)); };
        layout->setContentsMargins(dp(16), dp(16), dp(16), dp(16));
        layout->setSpacing(dp(12));
        body->setSpacing(dp(8));
        scroll->setMinimumHeight(dp(120));
        for (auto* row : rows) { row->setContentsMargins(dp(12), dp(10), dp(12), dp(10)); row->setVerticalSpacing(dp(6)); }
        for (auto* editor : editors) editor->setFixedWidth(dp(140));
    };
    if (ctx) connect(ctx, &MainCtx::intfScaleFactorChanged, this, metrics);
    if (auto* translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, refreshText);
    refreshText();
    metrics();
    char* chain = nullptr;
    if (const auto output = THEMIM->getAout()) chain = var_GetString(output.get(), "audio-filter");
    if (!chain) chain = var_InheritString(p_intf, "audio-filter");
    enabled->setChecked(filterIsPresent(qfu(chain ? chain : ""), name));
    enabled->setEnabled(available || enabled->isChecked());
    free(chain);
    content->setEnabled(available && enabled->isChecked());
    reset->setEnabled(available && enabled->isChecked());
    connect(enabled, &QCheckBox::toggled, this, [content, reset, enabled, available](bool checked) {
        content->setEnabled(available && checked);
        reset->setEnabled(available && checked);
        enabled->setEnabled(available || checked);
    });
    m_refreshAudioValues = [this, enabled, content, reset, available, editors, notices](const QHash<QString, QVariant>& pending) {
        const auto output = THEMIM->getAout();
        char* raw = output ? var_GetString(output.get(), "audio-filter") : nullptr;
        if (!raw && !pending.contains(QStringLiteral("audio-filter"))) raw = var_InheritString(p_intf, "audio-filter");
        const auto chain = raw ? qfu(raw) : pending.contains(QStringLiteral("audio-filter"))
            ? pending.value(QStringLiteral("audio-filter")).toString() : QString();
        free(raw);
        const bool checked = filterIsPresent(chain, name);
        const QSignalBlocker block(enabled);
        enabled->setChecked(checked); enabled->setEnabled(available || checked);
        std::vector<QSignalBlocker> fieldBlocks;
        fieldBlocks.reserve(editors.size());
        for (auto* editor : editors) fieldBlocks.emplace_back(editor);
        content->setEnabled(available && checked); reset->setEnabled(available && checked);
        for (int i = 0; i < controls.size(); ++i) {
            const auto& data = controls[i];
            double current = config_FindConfig(qtu(data.name)) ? config_GetFloat(qtu(data.name)) : data.f_value;
            if (output && (var_Type(output.get(), qtu(data.name)) & VLC_VAR_CLASS) == VLC_VAR_FLOAT) current = var_GetFloat(output.get(), qtu(data.name));
            else if (pending.contains(data.name)) current = pending.value(data.name).toFloat();
            if (!std::isfinite(current)) continue;
            auto* editor = static_cast<AudioFilterSpinBox*>(editors[i]);
            editor->updateCurrent(audioFilterValue(current) * data.f_visual_multiplier);
            auto* slider = findChild<QSlider*>(data.name + QStringLiteral("Slider"));
            const QSignalBlocker sliderBlock(slider); slider->setValue(qRound(current / data.f_resolution));
            notices[i]->setVisible(editor->currentChangedWhileTyping());
            notices[i]->setText(qtr("Current value: %1. Press Escape to use it.").arg(editor->currentText()));
            editor->setAccessibleDescription(editor->currentChangedWhileTyping() ? notices[i]->text() : QString());
        }
    };
    connect(enabled, &QCheckBox::toggled, this, &AudioFilterControlWidget::enable);
    connect(reset, &QPushButton::clicked, this, [this, editors] {
        for (auto* editor : editors) static_cast<AudioFilterSpinBox*>(editor)->resetValue();
    });
}

void AudioFilterControlWidget::enable( bool b_enable )
{
    module_t *p_obj = module_find( qtu(name) );
    if( b_enable && !p_obj )
    {
        msg_Err( p_intf, "Unable to find filter module \"%s\".", qtu(name) );
        return;
    }

    QString result = ChangeFiltersString( p_intf, "audio-filter", qtu(name),
                                          b_enable );
    if (var_Type(p_intf, "audio-filter") == 0)
        var_Create(p_intf, "audio-filter", VLC_VAR_STRING | VLC_VAR_DOINHERIT);
    var_SetString(p_intf, "audio-filter", qtu(result));
    emit configChanged( qfu("audio-filter"), result );
    vlc_player_aout_EnableFilter( p_intf->p_player, qtu(name), b_enable );
}

/**********************************************************************
 * Equalizer
 **********************************************************************/

Equalizer::Equalizer( qt_intf_t *p_intf, QWidget *parent )
    : AudioFilterControlWidget( p_intf, parent, "equalizer" )
{
    bool b_vlcBands = var_InheritBool( p_intf, "equalizer-vlcfreqs" );

    const AudioFilterSliderData vlc_bands[10] =
    {
        { "equalizer-bands", qtr("60 Hz"),  qtr("dB"), -20.0f, 20.0f, 0.0f, 0.1f, 1.0 },
        { "equalizer-bands", qtr("170 Hz"), qtr("dB"), -20.0f, 20.0f, 0.0f, 0.1f, 1.0 },
        { "equalizer-bands", qtr("310 Hz"), qtr("dB"), -20.0f, 20.0f, 0.0f, 0.1f, 1.0 },
        { "equalizer-bands", qtr("600 Hz"), qtr("dB"), -20.0f, 20.0f, 0.0f, 0.1f, 1.0 },
        { "equalizer-bands", qtr("1 kHz"),  qtr("dB"), -20.0f, 20.0f, 0.0f, 0.1f, 1.0 },
        { "equalizer-bands", qtr("3 kHz"),  qtr("dB"), -20.0f, 20.0f, 0.0f, 0.1f, 1.0 },
        { "equalizer-bands", qtr("6 kHz"),  qtr("dB"), -20.0f, 20.0f, 0.0f, 0.1f, 1.0 },
        { "equalizer-bands", qtr("12 kHz"), qtr("dB"), -20.0f, 20.0f, 0.0f, 0.1f, 1.0 },
        { "equalizer-bands", qtr("14 kHz"), qtr("dB"), -20.0f, 20.0f, 0.0f, 0.1f, 1.0 },
        { "equalizer-bands", qtr("16 kHz"), qtr("dB"), -20.0f, 20.0f, 0.0f, 0.1f, 1.0 },
    };
    const AudioFilterSliderData iso_bands[10] =
    {
        { "equalizer-bands", qtr("31 Hz"),  qtr("dB"), -20.0f, 20.0f, 0.0f, 0.1f, 1.0 },
        { "equalizer-bands", qtr("63 Hz"),  qtr("dB"), -20.0f, 20.0f, 0.0f, 0.1f, 1.0 },
        { "equalizer-bands", qtr("125 Hz"), qtr("dB"), -20.0f, 20.0f, 0.0f, 0.1f, 1.0 },
        { "equalizer-bands", qtr("250 Hz"), qtr("dB"), -20.0f, 20.0f, 0.0f, 0.1f, 1.0 },
        { "equalizer-bands", qtr("500 Hz"), qtr("dB"), -20.0f, 20.0f, 0.0f, 0.1f, 1.0 },
        { "equalizer-bands", qtr("1 kHz"),  qtr("dB"), -20.0f, 20.0f, 0.0f, 0.1f, 1.0 },
        { "equalizer-bands", qtr("2 kHz"),  qtr("dB"), -20.0f, 20.0f, 0.0f, 0.1f, 1.0 },
        { "equalizer-bands", qtr("4 kHz"),  qtr("dB"), -20.0f, 20.0f, 0.0f, 0.1f, 1.0 },
        { "equalizer-bands", qtr("8 kHz"),  qtr("dB"), -20.0f, 20.0f, 0.0f, 0.1f, 1.0 },
        { "equalizer-bands", qtr("16 kHz"), qtr("dB"), -20.0f, 20.0f, 0.0f, 0.1f, 1.0 },
    };
    for( int i=0; i<10 ;i++ ) controls.append( (b_vlcBands) ? vlc_bands[i] : iso_bands[i] );
    build();
}

void Equalizer::build()
{
    setObjectName(QStringLiteral("audioFilterControls"));
    setProperty("audioFilterName", name);
    applyRedesignWidgetTheme(this, p_intf->p_mi);
    auto* layout = new QVBoxLayout(this);
    auto* heading = new QHBoxLayout;
    auto* title = new QLabel(this); title->setProperty("redesignSectionHeading", true);
    auto* enabled = new QCheckBox(this); enabled->setObjectName(QStringLiteral("enableCheck"));
    title->hide(); heading->addWidget(enabled); layout->addLayout(heading);
    const bool available = module_exists("equalizer");
    auto* missing = new QLabel(this); missing->setWordWrap(true); missing->setObjectName(QStringLiteral("equalizerUnavailable")); missing->setVisible(!available); layout->addWidget(missing);
    auto* scroll = new QScrollArea(this); scroll->setObjectName(QStringLiteral("equalizerScroll")); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget(scroll); auto* body = new QVBoxLayout(content); body->setContentsMargins(0, 0, 0, 0); scroll->setWidget(content); layout->addWidget(scroll, 1);
    auto* presetLabel = new QLabel(this); auto* presets = new QComboBox(this); presets->setObjectName(QStringLiteral("presetsCombo")); presetLabel->setBuddy(presets);
    auto* twice = new QCheckBox(this); twice->setObjectName(QStringLiteral("eq2PassCheck"));
    auto* preampLabel = new QLabel(this); auto* preamp = new AudioFilterSpinBox(this); preamp->setObjectName(QStringLiteral("equalizer-preampValue")); preampLabel->setBuddy(preamp);
    auto* preampSlider = new QSlider(Qt::Horizontal, this); preampSlider->setObjectName(QStringLiteral("preampSlider")); preampSlider->setRange(-200, 200);
    auto* preampNotice = new QLabel(this); preampNotice->setTextFormat(Qt::PlainText); preampNotice->setWordWrap(true); preampNotice->setObjectName(QStringLiteral("equalizer-preampCurrentNotice"));
    heading->insertWidget(0, presetLabel); heading->insertWidget(1, presets, 1);
    auto* bandsCard = new QFrame(content); bandsCard->setProperty("redesignCard", true); auto* bandsLayout = new QVBoxLayout(bandsCard);
    auto* bandsTitle = new QLabel(bandsCard); bandsTitle->setProperty("redesignSectionHeading", true); auto* bandsHeading = new QHBoxLayout; bandsHeading->addWidget(bandsTitle, 1); bandsHeading->addWidget(twice); bandsLayout->addLayout(bandsHeading);
    auto* graph = new QWidget(bandsCard); auto* graphLayout = new QGridLayout(graph); graphLayout->setContentsMargins(0, 0, 0, 0); bandsLayout->addWidget(graph, 1);
    QList<QWidget*> columns; QList<QSlider*> sliders; QList<QPushButton*> gainButtons;
    auto* preampColumn = new QWidget(graph); auto* preampColumnLayout = new QVBoxLayout(preampColumn); preampColumnLayout->setContentsMargins(0, 0, 0, 0);
    preampSlider->setOrientation(Qt::Vertical); preampSlider->setProperty("equalizerBand", -1); preampSlider->installEventFilter(this);
    auto* preampGain = new QPushButton(preampColumn); preampGain->setObjectName(QStringLiteral("equalizerPreampGain")); preampGain->setAutoDefault(false); preampGain->setFlat(true);
    preampLabel->setAlignment(Qt::AlignCenter); preampColumnLayout->addWidget(preampSlider, 1, Qt::AlignHCenter); preampColumnLayout->addWidget(preampLabel); preampColumnLayout->addWidget(preampGain); graphLayout->addWidget(preampColumn, 0, 0); columns.append(preampColumn);
    for (int i = 0; i < 10; ++i) {
        auto* column = new QWidget(graph); column->setObjectName(QStringLiteral("equalizerColumn%1").arg(i)); auto* columnLayout = new QVBoxLayout(column); columnLayout->setContentsMargins(0, 0, 0, 0);
        auto* slider = new QSlider(Qt::Vertical, column); slider->setObjectName(QStringLiteral("equalizerBand%1Slider").arg(i)); slider->setProperty("equalizerBand", i); slider->setRange(-200, 200); slider->installEventFilter(this);
        auto* frequency = new QLabel(controls[i].descs, column); frequency->setAlignment(Qt::AlignCenter); frequency->setBuddy(slider);
        auto* gain = new QPushButton(column); gain->setObjectName(QStringLiteral("equalizerBand%1Gain").arg(i)); gain->setAutoDefault(false); gain->setFlat(true);
        columnLayout->addWidget(slider, 1, Qt::AlignHCenter); columnLayout->addWidget(frequency); columnLayout->addWidget(gain);
        graphLayout->addWidget(column, 0, i + 1); columns.append(column); sliders.append(slider); gainButtons.append(gain);
    }
    auto* gainRow = new QHBoxLayout; auto* gainLabel = new QLabel(bandsCard); auto* gain = new AudioFilterSpinBox(bandsCard); gain->setObjectName(QStringLiteral("equalizerBandValue")); gainLabel->setBuddy(gain); gainRow->addWidget(gainLabel, 1); gainRow->addWidget(gain); gainRow->addWidget(preamp); bandsLayout->addLayout(gainRow); bandsLayout->addWidget(preampNotice);
    auto* gainNotice = new QLabel(bandsCard); gainNotice->setTextFormat(Qt::PlainText); gainNotice->setWordWrap(true); gainNotice->setObjectName(QStringLiteral("equalizerBandCurrentNotice")); bandsLayout->addWidget(gainNotice); body->addWidget(bandsCard, 1);
    auto* footer = new QHBoxLayout; auto* hint = new QLabel(this); hint->setWordWrap(true); auto* reset = new QPushButton(this); reset->setObjectName(QStringLiteral("resetEqualizer")); reset->setAutoDefault(false); footer->addWidget(hint, 1); footer->addWidget(reset); layout->addLayout(footer);
    struct State { QHash<QString, QVariant> values; int selected = 0; };
    auto state = std::make_shared<State>();
    const auto read = [this, state](const QString& key) -> QVariant {
        const auto output = THEMIM->getAout(); const auto bytes = key.toUtf8();
        if (output) switch (var_Type(output.get(), bytes.constData()) & VLC_VAR_CLASS) {
        case VLC_VAR_FLOAT: return var_GetFloat(output.get(), bytes.constData());
        case VLC_VAR_BOOL: return var_GetBool(output.get(), bytes.constData());
        case VLC_VAR_STRING: { char* raw = var_GetString(output.get(), bytes.constData()); const auto result = qfu(raw ? raw : ""); free(raw); return result; }
        }
        return state->values.value(key);
    };
    const auto values = [read] {
        const auto parts = read(QStringLiteral("equalizer-bands")).toString().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        const auto preset = read(QStringLiteral("equalizer-preset")).toString();
        const eqz_preset_t* fallback = nullptr;
        for (const auto& row : eqz_preset_10b) if (preset == qfu(row.psz_name)) fallback = &row;
        QList<double> result;
        for (int i = 0; i < 10; ++i) {
            bool valid = false; double value = i < parts.size() ? QLocale::c().toDouble(parts[i], &valid) : 0;
            if (!valid || !std::isfinite(value)) value = fallback ? fallback->f_amp[i] : 0;
            result.append(qBound(-20.0, value, 20.0));
        }
        return result;
    };
    const auto write = [this, state](const QString& key, const QVariant& value) {
        state->values.insert(key, value);
        if (const auto output = THEMIM->getAout()) {
            const auto bytes = key.toUtf8(); const int type = config_GetType(bytes.constData()) & VLC_VAR_CLASS;
            if (!var_Type(output.get(), bytes.constData())) var_Create(output.get(), bytes.constData(), type | VLC_VAR_DOINHERIT);
            if (type == VLC_VAR_FLOAT) var_SetFloat(output.get(), bytes.constData(), value.toFloat());
            else if (type == VLC_VAR_BOOL) var_SetBool(output.get(), bytes.constData(), value.toBool());
            else if (key == QLatin1String("equalizer-preset") && value.toString().isEmpty()) {
                // Custom has no DSP preset callback. Clear only its name.
                vlc_value_t custom; custom.psz_string = const_cast<char*>("");
                var_Change(output.get(), bytes.constData(), VLC_VAR_SETVALUE, custom);
            } else var_SetString(output.get(), bytes.constData(), qtu(value.toString()));
        }
        emit configChanged(key, value);
    };
    const auto editBand = [this, state, values, write](int index, double value) {
        auto current = values(); current[index] = value; QStringList bands;
        for (const auto gain : current) bands.append(QString::number(gain, 'g', 9));
        write(QStringLiteral("equalizer-preset"), QString()); write(QStringLiteral("equalizer-bands"), bands.join(QLatin1Char(' '))); refreshAudioValues(state->values);
    };
    for (auto* number : {preamp, gain}) {
        number->setDecimals(6); number->setRange(-20, 20); number->setSingleStep(.1); number->setKeyboardTracking(false);
        connect(number->findChild<QLineEdit*>(), &QLineEdit::textEdited, this, &AudioFilterControlWidget::draftChanged);
        connect(number, &QDoubleSpinBox::editingFinished, this, &AudioFilterControlWidget::draftChanged);
    }
    preamp->setResetValue(preampSlider, config_FindConfig("equalizer-preamp")->orig.f);
    const auto selectionText = [this, state, gainLabel, gain, preamp] {
        const auto caption = state->selected < 0 ? qtr("Preamp") : qtr("%1 gain").arg(controls[state->selected].descs); gainLabel->setText(caption); gain->setAccessibleName(caption); preamp->setVisible(state->selected < 0); gain->setVisible(state->selected >= 0);
    };
    m_selectBand = [state, values, read, gain, preamp, selectionText](int index, bool edit) {
        if (index != state->selected) { state->selected = index; auto* editor = index < 0 ? preamp : gain; const QSignalBlocker block(editor); editor->setValue(index < 0 ? read(QStringLiteral("equalizer-preamp")).toDouble() : values()[index]); editor->findChild<QLineEdit*>()->setModified(false); }
        selectionText();
        if (edit) { auto* editor = index < 0 ? preamp : gain; editor->setFocus(); editor->selectAll(); }
    };
    connect(preampGain, &QPushButton::clicked, this, [this] { m_selectBand(-1, true); });
    for (int i = 0; i < 10; ++i) {
        connect(sliders[i], &QSlider::valueChanged, this, [this, i, editBand](int position) { m_selectBand(i, false); editBand(i, position / 10.0); });
        connect(gainButtons[i], &QPushButton::clicked, this, [this, i] { m_selectBand(i, true); });
    }
    connect(gain, &QDoubleSpinBox::valueChanged, this, [state, editBand](double value) { if (state->selected >= 0) editBand(state->selected, value); });
    connect(preampSlider, &QSlider::valueChanged, preamp, [preamp](int position) { preamp->setValue(position / 10.0); });
    connect(preamp, &QDoubleSpinBox::valueChanged, this, [this, state, write](double value) { write(QStringLiteral("equalizer-preset"), QString()); write(QStringLiteral("equalizer-preamp"), value); refreshAudioValues(state->values); });
    const auto applyPreset = [this, state, write](int index) {
        if (index < 0 || index >= NB_PRESETS) return;
        const auto& preset = eqz_preset_10b[index]; QStringList bands;
        for (int i = 0; i < 10; ++i) bands.append(QString::number(preset.f_amp[i], 'g', 9));
        write(QStringLiteral("equalizer-preset"), qfu(preset.psz_name)); write(QStringLiteral("equalizer-bands"), bands.join(QLatin1Char(' '))); write(QStringLiteral("equalizer-preamp"), preset.f_preamp); refreshAudioValues(state->values);
    };
    connect(presets, QOverload<int>::of(&QComboBox::activated), this, [applyPreset](int index) { applyPreset(index - 1); });
    connect(reset, &QPushButton::clicked, this, [applyPreset] { for (int i = 0; i < NB_PRESETS; ++i) if (!strcmp(eqz_preset_10b[i].psz_name, "flat")) { applyPreset(i); break; } });
    connect(twice, &QCheckBox::toggled, this, [this, state, write](bool checked) { write(QStringLiteral("equalizer-2pass"), checked); refreshAudioValues(state->values); });
    connect(enabled, &QCheckBox::clicked, this, [this, state](bool checked) { enable(checked); char* raw = var_InheritString(p_intf, "audio-filter"); state->values.insert(QStringLiteral("audio-filter"), qfu(raw ? raw : "")); free(raw); refreshAudioValues(state->values); });
    m_refreshAudioValues = [this, state, read, values, enabled, available, content, reset, presets, twice, preamp, gain, preampSlider, sliders, gainButtons, preampGain, preampNotice, gainNotice, hint, selectionText](const QHash<QString, QVariant>& pending) {
        for (const auto* key : {"equalizer-bands", "equalizer-preset", "equalizer-preamp", "equalizer-2pass", "audio-filter"}) {
            const auto name = qfu(key); const int type = config_GetType(key) & VLC_VAR_CLASS; QVariant saved;
            if (type == VLC_VAR_STRING) { char* raw = config_GetPsz(key); saved = qfu(raw ? raw : ""); free(raw); }
            else if (type == VLC_VAR_FLOAT) saved = config_GetFloat(key); else saved = bool(config_GetInt(key));
            state->values.insert(name, pending.value(name, saved));
        }
        const QSignalBlocker a(enabled), b(twice), c(presets), d(preamp), e(gain), f(preampSlider);
        const bool checked = filterIsPresent(read(QStringLiteral("audio-filter")).toString(), QStringLiteral("equalizer"));
        enabled->setChecked(checked); enabled->setEnabled(available || checked); content->setEnabled(available && checked); reset->setEnabled(available && checked);
        const auto bands = values();
        double amplification = read(QStringLiteral("equalizer-preamp")).toDouble(); if (!std::isfinite(amplification)) amplification = 0;
        preamp->updateCurrent(audioFilterValue(amplification)); preampSlider->setValue(qRound(amplification * 10)); if (state->selected >= 0) gain->updateCurrent(bands[state->selected]);
        preampGain->setText(QString::number(amplification, 'f', 1)); preampGain->setAccessibleDescription(qtr("Current gain: %1 dB").arg(QString::number(amplification, 'g', 7)));
        for (int i = 0; i < 10; ++i) { const QSignalBlocker block(sliders[i]); sliders[i]->setValue(qRound(bands[i] * 10)); gainButtons[i]->setText(QString::number(bands[i], 'f', 1)); gainButtons[i]->setAccessibleDescription(qtr("Current gain: %1 dB").arg(QString::number(bands[i], 'g', 7))); }
        twice->setChecked(read(QStringLiteral("equalizer-2pass")).toBool());
        int selected = 0; const auto presetName = read(QStringLiteral("equalizer-preset")).toString();
        for (int i = 0; i < NB_PRESETS; ++i) {
            const auto& preset = eqz_preset_10b[i]; if (presetName != qfu(preset.psz_name) || qAbs(amplification - preset.f_preamp) > .00001) continue;
            bool same = true; for (int band = 0; band < 10; ++band) if (qAbs(bands[band] - preset.f_amp[band]) > .00001) same = false;
            if (same) selected = i + 1;
        }
        presets->setCurrentIndex(selected);
        for (int i = 0; i < 10; ++i) sliders[i]->setProperty("equalizerResetGain", selected > 0 ? eqz_preset_10b[selected - 1].f_amp[i] : 0.0f);
        for (auto pair : {qMakePair(preamp, preampNotice), qMakePair(gain, gainNotice)}) {
            const auto* number = pair.first; pair.second->setText(qtr("Current value: %1. Press Escape to use it.").arg(number->currentText())); pair.second->setVisible(number->isVisible() && number->currentChangedWhileTyping()); pair.first->setAccessibleDescription(number->currentChangedWhileTyping() ? pair.second->text() : QString());
        }
        hint->setText(THEMIM->getAout() ? qtr("Select Save to keep these settings.") : qtr("Select Save before you open a file.")); selectionText();
    };
    const auto refreshText = [this, title, enabled, missing, presetLabel, presets, twice, preampLabel, preamp, bandsTitle, gain, reset, sliders, gainButtons, preampGain, selectionText] {
        struct Draft { QLineEdit* input; QString text; int cursor, start, length; bool modified; };
        QList<Draft> drafts;
        const QSignalBlocker preampBlock(preamp), gainBlock(gain);
        for (auto* number : {preamp, gain}) {
            auto* input = number->findChild<QLineEdit*>();
            drafts.append({input, input->text(), input->cursorPosition(), input->selectionStart(), int(input->selectedText().size()), input->isModified()});
        }
        title->setText(qtr("Equalizer")); enabled->setText(qtr("Enable")); enabled->setAccessibleName(qtr("Enable Equalizer")); missing->setText(qtr("This audio filter is not available in this build.")); presetLabel->setText(qtr("Preset"));
        const QSignalBlocker block(presets); const auto current = presets->currentData(); presets->clear(); presets->addItem(qtr("Custom"), QString()); for (int i = 0; i < NB_PRESETS; ++i) presets->addItem(qfut(preset_list_text[i]), qfu(preset_list[i])); presets->setCurrentIndex(qMax(0, presets->findData(current)));
        twice->setText(qtr("Two passes")); twice->setToolTip(qtr("Apply the equalizer twice.")); preampLabel->setText(qtr("Preamp")); preamp->setAccessibleName(qtr("Preamp")); preampGain->setAccessibleName(qtr("Enter preamp gain")); preamp->setSuffix(QStringLiteral(" dB")); gain->setSuffix(QStringLiteral(" dB")); bandsTitle->setText(qtr("Gain (dB)")); reset->setText(qtr("Reset to Flat"));
        for (int i = 0; i < 10; ++i) { sliders[i]->setAccessibleName(controls[i].descs); gainButtons[i]->setAccessibleName(qtr("Enter gain for %1").arg(controls[i].descs)); gainButtons[i]->setToolTip(qtr("Enter gain for %1").arg(controls[i].descs)); }
        selectionText();
        for (const auto& draft : drafts) if (draft.modified) {
            const QSignalBlocker block(draft.input);
            draft.input->setText(draft.text); draft.input->setCursorPosition(draft.cursor);
            if (draft.start >= 0) draft.input->setSelection(draft.start, draft.length);
            draft.input->setModified(true);
        }
    };
    m_layoutBands = [this, graph, graphLayout, columns, scroll] {
        const double scale = p_intf->p_mi ? p_intf->p_mi->getIntfScaleFactor() : 1.0;
        const int minimum = qRound(42 * scale);
        const int available = scroll->viewport()->width() - qRound(24 * scale);
        const int count = available >= 11 * minimum + 10 * graphLayout->horizontalSpacing() ? 10
            : qBound(1, (available + graphLayout->horizontalSpacing()) / (minimum + graphLayout->horizontalSpacing()) - 1, 5);
        if (graph->property("bandColumns").toInt() == count) return;
        graph->setProperty("bandColumns", count);
        for (auto* column : columns) graphLayout->removeWidget(column);
        graphLayout->addWidget(columns[0], 0, 0, (10 + count - 1) / count, 1);
        for (int i = 1; i < columns.size(); ++i) graphLayout->addWidget(columns[i], (i - 1) / count, (i - 1) % count + 1);
    };
    graph->setProperty("equalizerGraph", true); graph->installEventFilter(this); scroll->viewport()->setProperty("equalizerGraph", true); scroll->viewport()->installEventFilter(this);
    const auto metrics = [this, layout, body, bandsLayout, graphLayout, sliders, gainButtons, columns, preampSlider, preampGain, gain, preamp] {
        const auto dp = [this](int n) { return qRound(n * (p_intf->p_mi ? p_intf->p_mi->getIntfScaleFactor() : 1)); };
        layout->setContentsMargins(dp(12), dp(12), dp(12), dp(12)); layout->setSpacing(dp(10)); body->setSpacing(dp(10)); bandsLayout->setContentsMargins(dp(10), dp(10), dp(10), dp(10)); bandsLayout->setSpacing(dp(6)); graphLayout->setHorizontalSpacing(dp(4)); graphLayout->setVerticalSpacing(dp(10));
        for (auto* slider : sliders) slider->setMinimumHeight(dp(80));
        preampSlider->setMinimumHeight(dp(80));
        for (auto* column : columns) for (auto* label : column->findChildren<QLabel*>()) { auto font = label->font(); font.setPixelSize(dp(12)); label->setFont(font); }
        auto buttons = gainButtons; buttons.append(preampGain); for (auto* button : buttons) { auto font = button->font(); font.setPixelSize(dp(12)); button->setFont(font); button->setContentsMargins(0, 0, 0, 0); }
        gain->setFixedWidth(dp(140)); preamp->setFixedWidth(dp(140)); m_layoutBands();
    };
    if (p_intf->p_mi) connect(p_intf->p_mi, &MainCtx::intfScaleFactorChanged, this, metrics);
    if (auto* translator = InterfaceTranslator::instance()) connect(translator, &InterfaceTranslator::languageChanged, this, refreshText);
    QWidget* previous = enabled; for (auto* next : {static_cast<QWidget*>(presets), static_cast<QWidget*>(twice), static_cast<QWidget*>(preampSlider)}) { QWidget::setTabOrder(previous, next); previous = next; }
    QWidget::setTabOrder(previous, preampGain); previous = preampGain;
    for (int i = 0; i < 10; ++i) { QWidget::setTabOrder(previous, sliders[i]); QWidget::setTabOrder(sliders[i], gainButtons[i]); previous = gainButtons[i]; } QWidget::setTabOrder(previous, gain); QWidget::setTabOrder(gain, preamp); QWidget::setTabOrder(preamp, reset);
    refreshText(); metrics(); refreshAudioValues({});
}

bool Equalizer::eventFilter(QObject* object, QEvent* event)
{
    if (object->property("equalizerGraph").toBool() && event->type() == QEvent::Resize)
        QTimer::singleShot(0, this, [this] { if (m_layoutBands) m_layoutBands(); });
    if (object->property("equalizerBand").isValid() && event->type() == QEvent::MouseButtonDblClick) {
        if (auto* slider = qobject_cast<QSlider*>(object)) slider->setValue(qRound(slider->property("equalizerResetGain").toDouble() * 10));
        return true;
    }
    if (object->property("equalizerBand").isValid() && event->type() == QEvent::FocusIn && m_selectBand)
        m_selectBand(object->property("equalizerBand").toInt(), false);
    return AudioFilterControlWidget::eventFilter(object, event);
}


/**********************************************************************
 * Audio filters
 **********************************************************************/

/**********************************************************************
 * Dynamic range compressor
 **********************************************************************/

Compressor::Compressor( qt_intf_t *p_intf, QWidget *parent )
    : AudioFilterControlWidget( p_intf, parent, "compressor" )
{
    i_smallfont = -2;
    const AudioFilterSliderData a[7] =
    {
        { "compressor-rms-peak",    qfu(N_("RMS/peak")),         "",       0.0f,   1.0f,   0.00f, 0.001f, 1.0 },
        { "compressor-attack",      qfu(N_("Attack")),       qfu(N_("ms")),   1.5f, 400.0f,  25.00f, 0.100f, 1.0 },
        { "compressor-release",     qfu(N_("Release")),      qfu(N_("ms")),   2.0f, 800.0f, 100.00f, 0.100f, 1.0 },
        { "compressor-threshold",   qfu(N_("Threshold")),    qfu(N_("dB")), -30.0f,   0.0f, -11.00f, 0.010f, 1.0 },
        { "compressor-ratio",       qfu(N_("Ratio")),            ":1",     1.0f,  20.0f,   8.00f, 0.010f, 1.0 },
        { "compressor-knee",        qfu(N_("Knee radius")), qfu(N_("dB")),   1.0f,  10.0f,   2.50f, 0.010f, 1.0 },
        { "compressor-makeup-gain", qfu(N_("Makeup gain")), qfu(N_("dB")),   0.0f,  24.0f,   7.00f, 0.010f, 1.0 },
    };
    for( int i=0; i<7 ;i++ ) controls.append( a[i] );
    build();
}

/**********************************************************************
 * Dynamic range expander
 **********************************************************************/

Expander::Expander( qt_intf_t *p_intf, QWidget *parent )
    : AudioFilterControlWidget( p_intf, parent, "expander" )
{
    i_smallfont = -2;
    const AudioFilterSliderData a[7] =
    {
        { "expander-rms-peak",    qfu(N_("RMS/peak")),         "",       0.0f,   1.0f,   0.20f, 0.001f, 1.0 },
        { "expander-attack",      qfu(N_("Attack")),       qfu(N_("ms")),   1.5f, 400.0f,  25.00f, 0.100f, 1.0 },
        { "expander-release",     qfu(N_("Release")),      qfu(N_("ms")),   2.0f, 800.0f, 100.00f, 0.100f, 1.0 },
        { "expander-threshold",   qfu(N_("Threshold")),    qfu(N_("dB")), -60.0f,   0.0f, -25.00f, 0.010f, 1.0 },
        { "expander-ratio",       qfu(N_("Ratio")),            ":1",     1.0f,  20.0f,   2.00f, 0.010f, 1.0 },
        { "expander-knee",        qfu(N_("Knee radius")), qfu(N_("dB")),   1.0f,  10.0f,   5.00f, 0.010f, 1.0 },
        { "expander-makeup-gain", qfu(N_("Makeup gain")), qfu(N_("dB")),   0.0f,  24.0f,   0.00f, 0.010f, 1.0 },
    };
    for( int i=0; i<7 ;i++ ) controls.append( a[i] );
    build();
}

/**********************************************************************
 * Peak limiter
 **********************************************************************/

Limiter::Limiter( qt_intf_t *p_intf, QWidget *parent )
    : AudioFilterControlWidget( p_intf, parent, "limiter" )
{
    i_smallfont = -2;
    const AudioFilterSliderData a[7] =
    {
        { "limiter-rms-peak",    qfu(N_("RMS/peak")),         "",       0.0f,   1.0f,   1.00f, 0.001f, 1.0 },
        { "limiter-attack",      qfu(N_("Attack")),       qfu(N_("ms")),   1.5f, 400.0f,   1.50f, 0.100f, 1.0 },
        { "limiter-release",     qfu(N_("Release")),      qfu(N_("ms")),   2.0f, 800.0f,  50.00f, 0.100f, 1.0 },
        { "limiter-threshold",   qfu(N_("Threshold")),    qfu(N_("dB")), -60.0f,   0.0f,  -3.00f, 0.010f, 1.0 },
        { "limiter-ratio",       qfu(N_("Ratio")),            ":1",     1.0f,  20.0f,  20.00f, 0.010f, 1.0 },
        { "limiter-knee",        qfu(N_("Knee radius")), qfu(N_("dB")),   1.0f,  10.0f,   1.00f, 0.010f, 1.0 },
        { "limiter-makeup-gain", qfu(N_("Makeup gain")), qfu(N_("dB")),   0.0f,  24.0f,   0.00f, 0.010f, 1.0 },
    };
    for( int i=0; i<7 ;i++ ) controls.append( a[i] );
    build();
}

/**********************************************************************
 * Spatializer
 **********************************************************************/

Spatializer::Spatializer( qt_intf_t *p_intf, QWidget *parent )
    : AudioFilterControlWidget( p_intf, parent, "spatializer" )
{
    i_smallfont = -1;
    const AudioFilterSliderData a[5] =
    {
        { "spatializer-roomsize",   qfu(N_("Room size")),    "", 0.0f, 1.1f, 0.85f, 0.1f, 1.0 },
        { "spatializer-width",      qfu(N_("Width")),   "", 0.0f, 1.0f, 1.0f, 0.1f, 1.0 },
        { "spatializer-wet",        qfu(N_("Room sound")),     "", 0.0f, 1.0f, 0.4f, 0.1f, 1.0 },
        { "spatializer-dry",        qfu(N_("Source sound")),     "", 0.0f, 1.0f, 0.5f, 0.1f, 1.0 },
        { "spatializer-damp",       qfu(N_("Damping")),    "", 0.0f, 1.0f, 0.5f, 0.1f, 1.0 },
    };
    for( int i=0; i<5 ;i++ ) controls.append( a[i] );
    build();
}

/**********************************************************************
 * Spatializer
 **********************************************************************/

StereoWidener::StereoWidener( qt_intf_t *p_intf, QWidget *parent )
    : AudioFilterControlWidget( p_intf, parent, "stereo_widen" )
{
    i_smallfont = -1;
    const AudioFilterSliderData a[4] =
    {
        { "stereowiden-delay",     qfu(N_("Delay time")),    "ms", 1.0, 100,  20, 1.0, 1.0 },
        { "stereowiden-feedback",  qfu(N_("Feedback gain")), "%",  0.0, 0.9, 0.3, 0.1, 100.0 },
        { "stereowiden-crossfeed", qfu(N_("Crossfeed")),     "%",  0.0, 0.8, 0.3, 0.1, 100.0 },
        { "stereowiden-dry-mix",   qfu(N_("Dry mix")),       "%",  0.0, 1.0, 0.8, 0.1, 100.0 },
    };
    for( int i=0; i<4 ;i++ ) controls.append( a[i] );
    build();
}

/**********************************************************************
 * Advanced
 **********************************************************************/

PitchShifter::PitchShifter( qt_intf_t *p_intf, QWidget *parent )
    : AudioFilterControlWidget( p_intf, parent, "scaletempo_pitch" )
{
    i_smallfont = -1;
    controls.append( { "pitch-shift", qfu(N_("Adjust pitch")), qfu(N_("semitones")),
                        -12.0, 12.0, 0.0, 0.25, 1.0 } );
    build();
}

StereoPanner::StereoPanner( qt_intf_t *p_intf, QWidget *parent )
    : AudioFilterControlWidget( p_intf, parent, "stereopan" )
{
    i_smallfont = -1;
    controls.append( { "pan-control", qfu(N_("Adjust pan")), "", 0.0, 1.0,
            0.5, 0.1, 1.0 } );
    build();
}

SyncControls::SyncControls(qt_intf_t* intf, QWidget* parent) : QWidget(parent), p_intf(intf)
{
    setObjectName(QStringLiteral("syncControls"));
    applyRedesignWidgetTheme(this, p_intf->p_mi);
    auto* outer = new QVBoxLayout(this);
    auto* scroll = new QScrollArea(this); scroll->setObjectName(QStringLiteral("syncScroll")); scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame); scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget(scroll); auto* list = new QVBoxLayout(content); list->setContentsMargins(0, 0, 0, 0); scroll->setWidget(content); outer->addWidget(scroll, 1);
    QList<QVBoxLayout*> cards;
    const auto card = [&](QLabel*& title) {
        auto* frame = new QFrame(content); frame->setProperty("redesignCard", true);
        auto* layout = new QVBoxLayout(frame); title = new QLabel(frame); title->setProperty("redesignSectionHeading", true); layout->addWidget(title); list->addWidget(frame); cards.append(layout); return layout;
    };
    struct Field { AudioFilterSpinBox* number; QLabel* label; QLabel* notice; QLabel* direction; QWidget* row; int kind; };
    QList<Field> fields;
    const auto addField = [&](QVBoxLayout* layout, const char* name, int kind, double minimum, double maximum, const QString& suffix) {
        auto* row = new QWidget(layout->parentWidget()); auto* grid = new QGridLayout(row); grid->setContentsMargins(0, 0, 0, 0); grid->setColumnStretch(0, 1);
        auto* label = new QLabel(row); label->setWordWrap(true);
        auto* number = new AudioFilterSpinBox(row); number->setObjectName(qfu(name)); number->setDecimals(3); number->setKeyboardTracking(false); number->setRange(minimum, maximum); number->setSingleStep(.1); number->setSuffix(suffix); label->setBuddy(number);
        auto* direction = new QLabel(row); direction->setObjectName(qfu(name) + QStringLiteral("Direction"));
        grid->addWidget(label, 0, 0); grid->addWidget(number, 0, 1); if (kind < 3) grid->addWidget(direction, 0, 2); else direction->hide();
        auto* notice = new QLabel(row); notice->setWordWrap(true); notice->setTextFormat(Qt::PlainText); notice->setObjectName(qfu(name) + QStringLiteral("CurrentNotice")); notice->hide(); grid->addWidget(notice, 1, 0, 1, 3);
        layout->addWidget(row); fields.append({number, label, notice, direction, row, kind}); return number;
    };
    QLabel *timingTitle, *displayTitle, *durationTitle;
    auto* timingLayout = card(timingTitle);
    addField(timingLayout, "audioDelayValue", 0, -86400, 86400, QStringLiteral(" s"));
    addField(timingLayout, "subtitleDelayValue", 1, -86400, 86400, QStringLiteral(" s"));
    addField(timingLayout, "secondarySubtitleDelayValue", 2, -86400, 86400, QStringLiteral(" s"));
    auto* timingHelp = new QLabel(timingLayout->parentWidget()); timingHelp->setWordWrap(true); timingLayout->addWidget(timingHelp);
    auto* displayLayout = card(displayTitle);
    auto* fps = addField(displayLayout, "subtitleFpsValue", 3, 0, 1000, QStringLiteral(" fps"));
    auto* fpsHelp = new QLabel(displayLayout->parentWidget()); fpsHelp->setWordWrap(true); displayLayout->addWidget(fpsHelp);
    auto* durationLayout = card(durationTitle);
    auto* enabled = new QCheckBox(durationLayout->parentWidget()); enabled->setObjectName(QStringLiteral("subtitleDurationEnabled")); durationLayout->insertWidget(1, enabled, 0, Qt::AlignLeft);
    auto* controls = new QWidget(durationLayout->parentWidget()); auto* durationFields = new QVBoxLayout(controls); durationFields->setContentsMargins(0, 0, 0, 0); durationLayout->addWidget(controls);
    auto* methodRow = new QHBoxLayout; auto* methodLabel = new QLabel(controls); auto* method = new QComboBox(controls); method->setObjectName(QStringLiteral("subtitleDurationMethod")); methodLabel->setBuddy(method); methodRow->addWidget(methodLabel, 1); methodRow->addWidget(method); durationFields->addLayout(methodRow);
    auto* factor = addField(durationFields, "subtitleDurationValue", 4, 0, 20, {});
    auto* durationHelp = new QLabel(controls); durationHelp->setWordWrap(true); durationFields->addWidget(durationHelp);
    auto* unavailable = new QLabel(durationLayout->parentWidget()); unavailable->setObjectName(QStringLiteral("subtitleDurationUnavailable")); unavailable->setWordWrap(true); durationLayout->addWidget(unavailable); const bool available = module_exists("subsdelay");
    list->addStretch();
    auto* footer = new QHBoxLayout; auto* hint = new QLabel(this); hint->setWordWrap(true); auto* reset = new QPushButton(this); reset->setObjectName(QStringLiteral("resetTiming")); reset->setAutoDefault(false); footer->addWidget(hint, 1); footer->addWidget(reset); outer->addLayout(footer);
    const auto applyDuration = [this, available](int mode, double factor, bool enable) {
        if (!THEMIM->isStarted()) return;
        enable &= factor > 0;
        if (enable && !available) return;
        for (const auto& output : THEMIM->getVouts()) {
            if (enable) {
                if (!var_Type(output.get(), "subsdelay-mode")) var_Create(output.get(), "subsdelay-mode", VLC_VAR_INTEGER | VLC_VAR_DOINHERIT | VLC_VAR_ISCOMMAND);
                if (!var_Type(output.get(), "subsdelay-factor")) var_Create(output.get(), "subsdelay-factor", VLC_VAR_FLOAT | VLC_VAR_DOINHERIT | VLC_VAR_ISCOMMAND);
                var_SetInteger(output.get(), "subsdelay-mode", mode); var_SetFloat(output.get(), "subsdelay-factor", factor);
            }
            char* raw = var_GetString(output.get(), "sub-filter"); const auto old = qfu(raw ? raw : ""); free(raw);
            const auto updated = FilterChainEdit::withFilter(old, QStringLiteral("subsdelay"), enable).first;
            if (old != updated) var_SetString(output.get(), "sub-filter", qtu(updated));
        }
        update();
    };
    for (const auto& field : fields) connect(field.number, &QDoubleSpinBox::valueChanged, this, [this, field, method, enabled, applyDuration](double value) {
        if (!THEMIM->isStarted()) return;
        if (field.kind < 3) field.direction->setText(value < 0 ? qtr("Earlier") : value > 0 ? qtr("Later") : QString());
        switch (field.kind) {
        case 0: THEMIM->setAudioDelay(VLCDuration(vlc_tick_from_sec(value))); break;
        case 1: THEMIM->setSubtitleDelay(VLCDuration(vlc_tick_from_sec(value))); break;
        case 2: THEMIM->setSecondarySubtitleDelay(VLCDuration(vlc_tick_from_sec(value))); break;
        case 3: THEMIM->setSubtitleFPS(value); break;
        case 4: applyDuration(method->currentData().toInt(), value, enabled->isChecked()); break;
        }
    });
    connect(enabled, &QCheckBox::clicked, this, [method, factor, applyDuration](bool checked) { applyDuration(method->currentData().toInt(), checked && factor->value() <= 0 ? 2 : factor->value(), checked); });
    connect(method, QOverload<int>::of(&QComboBox::activated), this, [method, factor, enabled, applyDuration] { applyDuration(method->currentData().toInt(), factor->value(), enabled->isChecked()); });
    connect(reset, &QPushButton::clicked, this, [this, fields, method, factor, applyDuration] {
        for (const auto& field : fields) field.number->findChild<QLineEdit*>()->setModified(false);
        const auto zero = VLCDuration::fromMS(0); THEMIM->setAudioDelay(zero); THEMIM->setSubtitleDelay(zero); THEMIM->setSecondarySubtitleDelay(zero); THEMIM->setSubtitleFPS(0);
        applyDuration(method->currentData().toInt(), factor->value(), false); update();
    });
    struct State { int mode = -1; bool methodConflict = false; };
    auto state = std::make_shared<State>();
    m_refresh = [this, fields, state, method, factor, enabled, controls, available, unavailable, durationHelp, hint, reset] {
        std::vector<QSignalBlocker> blocks; for (const auto& field : fields) blocks.emplace_back(field.number);
        const QSignalBlocker a(method), b(enabled);
        const bool active = THEMIM->isStarted() && THEMIM->hasInput(); const auto output = THEMIM->getVout();
        int selected = 0; auto* tracks = THEMIM->getSubtitleTracks(); for (int i = 0; i < tracks->rowCount(); ++i) if (tracks->data(tracks->index(i), Qt::CheckStateRole).toBool()) ++selected;
        double mode = config_FindConfig("subsdelay-mode") ? config_GetInt("subsdelay-mode") : 1;
        double value = config_FindConfig("subsdelay-factor") ? config_GetFloat("subsdelay-factor") : 2;
        QString chain;
        if (output) {
            if (var_Type(output.get(), "subsdelay-mode")) mode = var_GetInteger(output.get(), "subsdelay-mode");
            if (var_Type(output.get(), "subsdelay-factor")) value = var_GetFloat(output.get(), "subsdelay-factor");
            char* raw = var_GetString(output.get(), "sub-filter"); chain = qfu(raw ? raw : ""); free(raw);
        }
        const int currentMode = qBound(0, int(mode), 2);
        auto* input = factor->findChild<QLineEdit*>();
        if (!input->isModified()) state->methodConflict = false;
        else if (state->mode >= 0 && state->mode != currentMode) state->methodConflict = true;
        state->mode = currentMode;
        const bool checked = FilterChainEdit::withFilter(chain, QStringLiteral("subsdelay"), false).second;
        enabled->setChecked(checked); enabled->setEnabled(active && bool(output) && (available || checked)); controls->setEnabled(active && bool(output) && available && checked);
        method->setCurrentIndex(method->findData(currentMode)); unavailable->setVisible(!available); reset->setEnabled(active);
        const QString suffix = currentMode == 0 ? QStringLiteral(" s") : currentMode == 1 ? QStringLiteral("×") : QString();
        if (factor->suffix() != suffix) {
            auto text = input->text(); const auto previous = factor->suffix(); if (!previous.isEmpty() && text.endsWith(previous)) text.chop(previous.size()); const int cursor = input->cursorPosition(); const bool draft = input->isModified();
            factor->setSuffix(suffix); if (draft) { input->setText(text + suffix); input->setCursorPosition(qMin(cursor, int(text.size()))); input->setModified(true); }
        }
        durationHelp->setText(currentMode == 0 ? qtr("Add this time to each subtitle.") : currentMode == 1 ? qtr("Multiply the time for each subtitle by this value.") : qtr("Use the subtitle text and this value to set its display time."));
        const double values[] = {THEMIM->getAudioDelay().toSecf(), THEMIM->getSubtitleDelay().toSecf(), THEMIM->getSecondarySubtitleDelay().toSecf(), THEMIM->getSubtitleFPS(), value};
        for (int i = 0; i < fields.size(); ++i) {
            const auto& field = fields[i];
            if (i < 4) field.row->setEnabled(active && (i != 2 || selected > 1) && (i != 0 || THEMIM->getAudioTracks()->rowCount() > 0));
            if (std::isfinite(values[i])) field.number->updateCurrent(values[i]);
            field.direction->setText(field.number->value() < 0 ? qtr("Earlier") : field.number->value() > 0 ? qtr("Later") : QString());
            const bool conflict = field.number->currentChangedWhileTyping() || (i == 4 && state->methodConflict);
            field.notice->setText(i == 4 && state->methodConflict ? qtr("The method changed. Press Escape to use the current value.") : qtr("Current value: %1. Press Escape to use it.").arg(field.number->currentText())); field.notice->setVisible(conflict); field.number->setAccessibleDescription(conflict ? field.notice->text() : QString());
            if (i == 4) field.label->setText(currentMode == 0 ? qtr("Extra time") : currentMode == 1 ? qtr("Duration multiplier") : qtr("Text factor"));
        }
        fields[4].number->setAccessibleName(fields[4].label->text());
        hint->setText(!active ? qtr("Open a file to change timing.") : !output ? qtr("Open a video to set subtitle display time.") : qtr("Changes affect playback."));
    };
    const auto refreshText = [this, fields, timingTitle, timingHelp, displayTitle, fps, fpsHelp, durationTitle, methodLabel, method, enabled, unavailable, reset] {
        struct Draft { QLineEdit* input; QString text; int cursor, start, length; bool modified; };
        QList<Draft> drafts; std::vector<QSignalBlocker> blockers;
        for (const auto& field : fields) { auto* input = field.number->findChild<QLineEdit*>(); drafts.append({input, input->text(), input->cursorPosition(), input->selectionStart(), int(input->selectedText().size()), input->isModified()}); blockers.emplace_back(field.number); }
        timingTitle->setText(qtr("Track timing")); timingHelp->setText(qtr("Use a positive value to play a track later. Use a negative value to play it earlier."));
        const char* labels[] = {N_("Audio delay"), N_("Primary subtitles"), N_("Secondary subtitles"), N_("Frame rate"), N_("Value")};
        for (int i = 0; i < fields.size(); ++i) { fields[i].label->setText(qtr(labels[i])); fields[i].number->setAccessibleName(qtr(labels[i])); }
        fields[2].row->setToolTip(qtr("Select a secondary subtitle track to set its delay."));
        displayTitle->setText(qtr("Subtitle frame rate")); fps->setSpecialValueText(qtr("Automatic")); fpsHelp->setText(qtr("Use this for subtitles that use frame numbers. Select Automatic to use the source rate."));
        durationTitle->setText(qtr("Subtitle display time")); enabled->setText(qtr("Enable")); enabled->setAccessibleName(qtr("Enable subtitle display time")); methodLabel->setText(qtr("Method"));
        const QSignalBlocker block(method); const auto current = method->currentData(); method->clear(); method->addItem(qtr("Add time"), 0); method->addItem(qtr("Multiply duration"), 1); method->addItem(qtr("Use text length"), 2); method->setCurrentIndex(qMax(0, method->findData(current)));
        unavailable->setText(qtr("This subtitle effect is not available in this build.")); reset->setText(qtr("Reset timing"));
        for (const auto& draft : drafts) if (draft.modified) { const QSignalBlocker inputBlock(draft.input); draft.input->setText(draft.text); draft.input->setCursorPosition(draft.cursor); if (draft.start >= 0) draft.input->setSelection(draft.start, draft.length); draft.input->setModified(true); }
        update();
    };
    const auto metrics = [this, outer, list, cards, fields, durationFields] {
        const auto dp = [this](int n) { return qRound(n * (p_intf->p_mi ? p_intf->p_mi->getIntfScaleFactor() : 1)); };
        outer->setContentsMargins(dp(16), dp(16), dp(16), dp(12)); outer->setSpacing(dp(12)); list->setSpacing(dp(12)); durationFields->setSpacing(dp(12));
        for (auto* card : cards) { card->setContentsMargins(dp(12), dp(12), dp(12), dp(12)); card->setSpacing(dp(10)); }
        for (const auto& field : fields) { field.number->setFixedWidth(dp(140)); field.direction->setMinimumWidth(dp(48)); if (auto* grid = qobject_cast<QGridLayout*>(field.row->layout())) grid->setHorizontalSpacing(dp(10)); }
    };
    if (p_intf->p_mi) { connect(p_intf->p_mi, &MainCtx::intfScaleFactorChanged, this, metrics); connect(p_intf->p_mi, &MainCtx::redesignPlayerPreferencesChanged, this, &SyncControls::update); }
    if (auto* translator = InterfaceTranslator::instance()) connect(translator, &InterfaceTranslator::languageChanged, this, refreshText);
    connect(THEMIM, &PlayerController::audioDelayChanged, this, &SyncControls::update); connect(THEMIM, &PlayerController::subtitleDelayChanged, this, &SyncControls::update); connect(THEMIM, &PlayerController::secondarySubtitleDelayChanged, this, &SyncControls::update); connect(THEMIM, &PlayerController::subtitleFPSChanged, this, &SyncControls::update); connect(THEMIM, &PlayerController::inputChanged, this, &SyncControls::update); connect(THEMIM, &PlayerController::playingStateChanged, this, &SyncControls::update);
    auto* timer = new QTimer(this); timer->setInterval(250); connect(timer, &QTimer::timeout, this, [this] { if (isVisible()) update(); }); timer->start();
    QWidget* previous = fields[0].number; for (int i = 1; i < 4; ++i) { QWidget::setTabOrder(previous, fields[i].number); previous = fields[i].number; } QWidget::setTabOrder(previous, enabled); QWidget::setTabOrder(enabled, method); QWidget::setTabOrder(method, factor); QWidget::setTabOrder(factor, reset);
    metrics(); refreshText();
}

void SyncControls::clean() { update(); }
void SyncControls::update() { if (m_refresh) m_refresh(); }
