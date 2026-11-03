/*****************************************************************************
 * profile_selector.cpp : A small profile selector and editor
 ****************************************************************************
 * Copyright (C) 2009 the VideoLAN team
 *
 * Authors: Jean-Baptiste Kempf <jb@videolan.org>
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

#include "dialogs/sout/profile_selector.hpp"
#include "dialogs/dialogs_provider.hpp"
#include "dialogs/sout/profiles.hpp"
#include "dialogs/sout/sout.hpp"
#include "util/soutchain.hpp"
#include "util/redesign_widget_theme.hpp"
#include "util/interface_translator.hpp"
#include "maininterface/mainctx.hpp"
#include "util/colorizedsvgicon.hpp"
#include "util/color_scheme_model.hpp"

#include <QHBoxLayout>
#include <QToolButton>
#include <QComboBox>
#include <QLabel>
#include <QPersistentModelIndex>
#include <QRadioButton>
#include <QRegularExpression>
#include <QButtonGroup>
#include <QSpinBox>
#include <QUrl>
#include <QListWidgetItem>
#include <QFontMetrics>
#include <QSignalBlocker>
#include <QStyleHints>
#include <QApplication>
#include <QScrollArea>
#include <QScreen>
#include <QKeyEvent>
#include <QLocale>
#include <cmath>

#include <cassert>
#include <vlc_modules.h>

#define CATPROP2NAME( val ) QString("valueholder_%1").arg( val )
#define CATANDPROP( cat, prop ) QString("%1_%2").arg( cat ).arg( prop )
#define OLDFORMAT "^\\w+;\\d+;\\d+;\\d+;"

namespace {
constexpr int FilterNameRole = Qt::UserRole + 1;
constexpr int FilterHelpRole = Qt::UserRole + 2;
constexpr int MissingFilterRole = Qt::UserRole + 3;
constexpr int OtherValueRole = Qt::UserRole + 1;
}

VLCProfileSelector::VLCProfileSelector( QWidget *_parent ): QWidget( _parent )
{
    QHBoxLayout *layout = new QHBoxLayout( this );

    QLabel *prLabel = new QLabel( qtr( "Profile"), this );
    layout->addWidget( prLabel );

    profileBox = new QComboBox( this );
    profileBox->setObjectName(QStringLiteral("profileChoice"));
    profileBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    profileBox->setMinimumContentsLength(8);
    prLabel->setBuddy(profileBox);
    layout->addWidget(profileBox, 1);

    QToolButton *editButton = m_editButton = new QToolButton(this);
    editButton->setObjectName(QStringLiteral("profileEdit"));
    editButton->setIcon( QIcon( ":/menu/preferences.svg" ) );
    editButton->setToolTip( qtr( "Edit selected profile" ) );
    layout->addWidget( editButton );

    QToolButton *deleteButton = m_deleteButton = new QToolButton(this);
    deleteButton->setObjectName(QStringLiteral("profileDelete"));
    deleteButton->setIcon( QIcon( ":/menu/clear.svg" ) );
    deleteButton->setToolTip( qtr("Remove selected profile") );
    layout->addWidget( deleteButton );

    QToolButton *newButton = m_newButton = new QToolButton(this);
    newButton->setObjectName(QStringLiteral("profileNew"));
    newButton->setIcon( QIcon( ":/menu/profile_new.svg" ) );
    newButton->setToolTip( qtr( "Create a new profile" ) );
    layout->addWidget(newButton);

    BUTTONACT( newButton, &VLCProfileSelector::newProfile );
    BUTTONACT( editButton, &VLCProfileSelector::editProfile );
    BUTTONACT( deleteButton, &VLCProfileSelector::deleteProfile );
    fillProfilesCombo();

    connect( profileBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
             this, &VLCProfileSelector::updateOptions );
    updateOptions(profileBox->currentIndex());

    for (auto *button : {editButton, deleteButton, newButton}) button->setAccessibleName(button->toolTip());
    updateActionIcons();
    if (auto* translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this,
                [prLabel, editButton, deleteButton, newButton] {
            prLabel->setText(qtr("Profile"));
            editButton->setToolTip(qtr("Edit selected profile"));
            deleteButton->setToolTip(qtr("Remove selected profile"));
            newButton->setToolTip(qtr("Create a new profile"));
            for (auto *button : {editButton, deleteButton, newButton}) button->setAccessibleName(button->toolTip());
        });
}

VLCProfileSelector::~VLCProfileSelector()
{
    QSettings settings(
#ifdef _WIN32
            QSettings::IniFormat,
#else
            QSettings::NativeFormat,
#endif
            QSettings::UserScope, QT_SETTINGS_ORGANIZATION, "vlc-qt-interface" );
    ;
    settings.setValue( "codecs-profiles-selected", profileBox->currentText() );
}

void VLCProfileSelector::setThemeContext(MainCtx* ctx)
{
    if (m_themeContext == ctx) return;
    m_themeContext = ctx;
    if (ctx)
    {
        connect(ctx, &MainCtx::intfScaleFactorChanged, this, &VLCProfileSelector::updateActionIcons);
        connect(ctx->getColorScheme(), &ColorSchemeModel::currentChanged, this, &VLCProfileSelector::updateActionIcons);
        connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, &VLCProfileSelector::updateActionIcons);
    }
    updateActionIcons();
}

void VLCProfileSelector::updateActionIcons()
{
    QColor color = palette().color(QPalette::WindowText);
    if (const auto ctx = m_themeContext)
    {
        const auto scheme = ctx->getColorScheme()->currentScheme();
        const bool light = scheme == ColorSchemeModel::Day ||
            (scheme == ColorSchemeModel::System && qApp->styleHints()->colorScheme() == Qt::ColorScheme::Light);
        color = QColor(light ? "#404040" : "#8f8f8f");
    }
    const QString foreground = color.name();
    const int size = qRound(20 * (m_themeContext ? m_themeContext->getIntfScaleFactor() : 1));
    m_editButton->setIcon(ColorizedSvgIcon(QStringLiteral(":/menu/preferences.svg"), {}, {}, {},
        {{QStringLiteral("#747474"), foreground}}));
    m_deleteButton->setIcon(ColorizedSvgIcon(QStringLiteral(":/menu/redesign/remove.svg"), {}, {}, {},
        {{QStringLiteral("#8f8f8f"), foreground}}));
    m_newButton->setIcon(ColorizedSvgIcon(QStringLiteral(":/menu/profile_new.svg"), {}, {}, {},
        {{QStringLiteral("#747474"), foreground}, {QStringLiteral("#ff950d"), foreground}}));
    for (auto *button : {m_editButton, m_deleteButton, m_newButton}) button->setIconSize(QSize(size,size));
}

inline void VLCProfileSelector::fillProfilesCombo()
{
    QSettings settings(
#ifdef _WIN32
            QSettings::IniFormat,
#else
            QSettings::NativeFormat,
#endif
            QSettings::UserScope, QT_SETTINGS_ORGANIZATION, "vlc-qt-interface" );

    const bool savedProfiles = settings.contains(QStringLiteral("codecs-profiles/size"));
    int i_size = settings.beginReadArray( "codecs-profiles" );

    for( int i = 0; i < i_size; i++ )
    {
        settings.setArrayIndex( i );
        if( settings.value( "Profile-Name" ).toString().isEmpty() ) continue;
        profileBox->addItem( settings.value( "Profile-Name" ).toString(),
                settings.value( "Profile-Value" ) );
    }
    if (i_size == 0 && !savedProfiles)
    {
        for( size_t i = 0; i < NB_PROFILE; i++ )
        {
            profileBox->addItem( video_profile_name_list[i],
                                 video_profile_value_list[i] );
        }
    }
    settings.endArray();

    profileBox->setCurrentIndex(qMax(0,
        profileBox->findText(
            settings.value( "codecs-profiles-selected" ).toString() )));

}

void VLCProfileSelector::newProfile()
{
    editProfilePrivate( "", "" );
}

void VLCProfileSelector::editProfile()
{
    if (profileBox->currentIndex() < 0) return;
    editProfilePrivate( profileBox->currentText(),
                 profileBox->itemData( profileBox->currentIndex() ).toString() );
}

void VLCProfileSelector::editProfilePrivate(const QString& original, const QString& value)
{
    QPointer<VLCProfileSelector> selector(this);
    QWidget *owner = window();
    QStringList names;
    for (int i = 0; i < profileBox->count(); ++i) names.append(profileBox->itemText(i));
    VLCProfileEditor editor(original, value, owner ? owner->windowHandle() : nullptr,
                            m_themeContext.data(), names);
    connect(this, &QObject::destroyed, &editor, &QDialog::reject);
    const auto result = editor.exec();
    if (!selector || result != QDialog::Accepted) return;
    {
        const QSignalBlocker blocker(profileBox);
        if (original.isEmpty())
        {
            profileBox->addItem(editor.name, editor.transcodeValue());
            profileBox->setCurrentIndex(profileBox->count() - 1);
        }
        else
        {
            const int index = profileBox->findText(original);
            if (index < 0) return;
            profileBox->setItemText(index, editor.name);
            profileBox->setItemData(index, editor.transcodeValue());
        }
    }
    updateOptions(profileBox->currentIndex());
    saveProfiles();
}

void VLCProfileSelector::deleteProfile()
{
    const int index = profileBox->currentIndex();
    if (index < 0) return;
    QPointer<VLCProfileSelector> selector(this);
    const QPersistentModelIndex target(profileBox->model()->index(index,
        profileBox->modelColumn(), profileBox->rootModelIndex()));
    const QString name = target.data(Qt::DisplayRole).toString();
    const QVariant settings = target.data(Qt::UserRole);
    const auto answer = DialogsProvider::getInstance()->messageDialog(
        qtr("Remove this profile?\n\n%1").arg(name), qtr("Remove profile"),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel,
        QMessageBox::Question, this, qtr("Remove"));
    if (!selector || answer != QMessageBox::Yes) return;
    // A row can move or change while the confirmation runs its nested event loop.
    // Only the exact, unchanged profile named in the prompt may be removed.
    if (!target.isValid() || target.model() != profileBox->model()
        || target.parent() != profileBox->rootModelIndex()
        || target.column() != profileBox->modelColumn()
        || target.data(Qt::DisplayRole).toString() != name
        || target.data(Qt::UserRole) != settings)
    {
        updateOptions(profileBox->currentIndex());
        if (!selector) return;
        DialogsProvider::getInstance()->messageDialog(
            qtr("The profile changed. Check the selected profile and try again."),
            qtr("Remove profile"), QMessageBox::Close, QMessageBox::Close,
            QMessageBox::Information, this);
        return;
    }
    {
        const QSignalBlocker blocker(profileBox);
        profileBox->removeItem(target.row());
    }
    updateOptions(profileBox->currentIndex());
    saveProfiles();
}

void VLCProfileSelector::saveProfiles()
{
    QSettings settings(
#ifdef _WIN32
            QSettings::IniFormat,
#else
            QSettings::NativeFormat,
#endif
            QSettings::UserScope, QT_SETTINGS_ORGANIZATION, "vlc-qt-interface" );

    settings.remove( "codecs-profiles" ); /* Erase old profiles to be rewritten */
    settings.beginWriteArray( "codecs-profiles" );
    for( int i = 0; i < profileBox->count(); i++ )
    {
        settings.setArrayIndex( i );
        settings.setValue( "Profile-Name", profileBox->itemText( i ) );
        settings.setValue( "Profile-Value", profileBox->itemData( i ).toString() );
    }
    settings.endArray();
    settings.setValue("codecs-profiles-selected", profileBox->currentText());
}

void VLCProfileSelector::updateOptions( int i )
{
    mux.clear();
    transcode.clear();
    m_editButton->setEnabled(i >= 0 && i < profileBox->count());
    m_deleteButton->setEnabled(i >= 0 && i < profileBox->count());
    const QString options = profileBox->itemData(i).toString();
    if (options.isEmpty()) { emit optionsChanged(); return; }
    QRegularExpression rx(QStringLiteral(OLDFORMAT));
    if ( rx.match( options ).hasMatch() )
        return updateOptionsOldFormat( i );

    transcode.clear();

    QStringList tuples = options.split( ";" );
    typedef QHash<QString, QString> proptovalueHashType;
    QHash<QString, proptovalueHashType *> categtopropHash;
    proptovalueHashType *proptovalueHash;
    QString value;

    /* Build a double hash structure because we need to make ordered lookups */
    foreach ( const QString &tuple, tuples )
    {
        const int equal = tuple.indexOf(QLatin1Char('='));
        if (equal <= 0) continue;
        QString key = tuple.left(equal);
        value = tuple.mid(equal + 1);
        QStringList keyvalue = key.split( "_" );
        if ( keyvalue.count() != 2 ) continue;
        QString categ = keyvalue[0];
        QString prop = keyvalue[1];

        if ( ! categtopropHash.contains( categ ) )
        {
            proptovalueHash = new proptovalueHashType();
            categtopropHash.insert( categ, proptovalueHash );
        } else {
            proptovalueHash = categtopropHash.value( categ );
        }
        proptovalueHash->insert( prop, value );
    }

    /* Now we can build the/translate into MRL */
#define HASHPICK( categ, prop ) \
    if ( categtopropHash.contains( categ ) ) \
    {\
        proptovalueHash = categtopropHash.value( categ );\
        value = proptovalueHash->take( prop );\
    }\
    else value = QString()

    transcode.begin( "transcode" );

    /* First muxer options */
    HASHPICK( "muxer", "mux" );
    if ( value.isEmpty() ) goto cleanup;
    mux = value;

    HASHPICK( "video", "enable" );
    if ( !value.isEmpty() )
    {
        HASHPICK( "video", "codec" );

        if ( !value.isEmpty() )
        {
            const bool x264 = value == QStringLiteral("h264");
            transcode.option( "vcodec", value );

            HASHPICK( "vcodec", "bitrate" );
            if ( value.toInt() > 0 )
            {
                transcode.option( "vb", value.toInt() );
            }

            HASHPICK( "video", "filters" );
            if ( !value.isEmpty() )
            {
                QStringList valuesList = QUrl::fromPercentEncoding( value.toUtf8() ).split( ";" );
                transcode.option( "vfilter", valuesList.join( ":" ) );
            }

            if (x264)
            {
                QStringList codecoptions;

                HASHPICK( "vcodec", "qp" );
                if( value.toInt() > 0 )
                    codecoptions << QString( "qp=%1" ).arg( value );

                HASHPICK( "vcodec", "custom" );
                if( !value.isEmpty() )
                    codecoptions << QUrl::fromPercentEncoding( value.toUtf8() );

                if ( codecoptions.count() )
                    transcode.option( "venc",
                        QString("x264{%1}").arg( codecoptions.join(",") ) );
            }

            HASHPICK( "vcodec", "framerate" );
            if (!value.isEmpty() && value.toDouble() > 0)
                transcode.option("fps", value);

            HASHPICK( "vcodec", "scale" );
            if ( !value.isEmpty() )
                transcode.option( "scale", value );

            HASHPICK( "vcodec", "width" );
            if ( !value.isEmpty() && value.toInt() > 0 )
                transcode.option( "width", value );

            HASHPICK( "vcodec", "height" );
            if ( !value.isEmpty() && value.toInt() > 0 )
                transcode.option( "height", value );
        }
    } else {
        transcode.option( "vcodec", "none" );
    }

    HASHPICK( "audio", "enable" );
    if ( !value.isEmpty() )
    {
        HASHPICK( "audio", "codec" );
        if ( !value.isEmpty() )
        {
            transcode.option( "acodec", value );

            HASHPICK( "acodec", "bitrate" );
            transcode.option( "ab", value.toInt() );

            HASHPICK( "acodec", "channels" );
            transcode.option( "channels", value.toInt() );

            HASHPICK( "acodec", "samplerate" );
            transcode.option( "samplerate", value.toInt() );

            /* Build audio filter chain: user-selected filters + optional gain boost */
            {
                QStringList audioFilters;

                HASHPICK( "audio", "filters" );
                if ( !value.isEmpty() )
                    audioFilters << QUrl::fromPercentEncoding( value.toUtf8() ).split( ";" );

                HASHPICK("audio", "gain");
                const bool explicitVolume = !value.isEmpty();
                HASHPICK("acodec", "gain");
                bool validVolume;
                const double gainPct = value.toDouble(&validVolume);
                // Legacy zero means no change. The explicit control can mute.
                if (validVolume && std::isfinite(gainPct) && gainPct >= 0
                    && (explicitVolume || gainPct > 0) && gainPct != 100)
                    audioFilters.prepend(QStringLiteral("gain{value=%1}")
                        .arg(gainPct / 100.0, 0, 'g', 9));

                if ( !audioFilters.isEmpty() )
                    transcode.option( "afilter", audioFilters.join( ":" ) );
            }

        }
    } else {
        transcode.option( "acodec", "none" );
    }

    HASHPICK( "subtitles", "enable" );
    if( !value.isEmpty() )
    {
        HASHPICK( "subtitles", "overlay" );
        if ( value.isEmpty() )
        {
            HASHPICK( "subtitles", "codec" );
            transcode.option( "scodec", value );
        }
        else
        {
            transcode.option( "soverlay" );
        }
    } else {
        transcode.option( "scodec", "none" );
    }
    transcode.end();
#undef HASHPICK

    cleanup:
    if (mux.isEmpty()) transcode.clear();
    /* Temp hash tables cleanup */
    foreach( proptovalueHashType *hash, categtopropHash )
        delete hash;

    emit optionsChanged();
}

void VLCProfileSelector::updateOptionsOldFormat( int i )
{
    QStringList options = profileBox->itemData( i ).toString().split( ";" );
    if (options.count() < 16) { emit optionsChanged(); return; }

    mux = options[0];

    if( options[1].toInt() || options[2].toInt() || options[3].toInt() )
    {
        transcode.begin( "transcode" );

        if( options[1].toInt() )
        {
            transcode.option( "vcodec", options[4] );
            if( options[4] != "none" )
            {
                transcode.option( "vb", options[5].toInt() );
                if( !options[7].isEmpty() && options[7].toInt() > 0 )
                    transcode.option( "fps", options[7] );
                if( !options[6].isEmpty() )
                    transcode.option( "scale", options[6] );
                if( !options[8].isEmpty() && options[8].toInt() > 0 )
                    transcode.option( "width", options[8].toInt() );
                if( !options[9].isEmpty() && options[9].toInt() > 0 )
                    transcode.option( "height", options[9].toInt() );
            }
        }

        if( options[2].toInt() )
        {
            transcode.option( "acodec", options[10] );
            if( options[10] != "none" )
            {
                transcode.option( "ab", options[11].toInt() );
                transcode.option( "channels", options[12].toInt() );
                transcode.option( "samplerate", options[13].toInt() );
            }
        }

        if( options[3].toInt() )
        {
            transcode.option( "scodec", options[14] );
            if( options[15].toInt() )
                transcode.option( "soverlay" );
        }

        transcode.end();
    }
    else
        transcode.clear();
    emit optionsChanged();
}


/**
 * VLCProfileEditor
 **/
VLCProfileEditor::VLCProfileEditor(const QString& original, const QString& value,
        QWindow *parent, MainCtx *ctx, const QStringList& existingNames)
    : QVLCDialog(parent, nullptr), m_themeContext(ctx), m_originalName(original), m_existingNames(existingNames)
{
    ui.setupUi(this);
    setObjectName(QStringLiteral("redesignProfileEditor"));
    setWindowRole("vlc-profile-editor");
    ui.buttonGroup->setObjectName(CATPROP2NAME(CATANDPROP("muxer","mux")));
    ui.profileLine->setText(original);
    loadCapabilities();
    registerCodecs();
    registerFilters();
    for (auto *combo : findChildren<QComboBox*>())
    {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(8);
    }
    m_saveButton = new QPushButton(this);
    m_saveButton->setObjectName(QStringLiteral("profileSave"));
    m_saveButton->setProperty("redesignPrimary", true);
    m_saveButton->setAutoDefault(false);
    ui.buttonBox->addButton(m_saveButton, QDialogButtonBox::AcceptRole);
    auto *cancel = new QPushButton(qtr("&Cancel"), this);
    cancel->setObjectName(QStringLiteral("profileCancel"));
    cancel->setAutoDefault(false);
    ui.buttonBox->addButton(cancel, QDialogButtonBox::RejectRole);
    connect(ui.buttonBox, &QDialogButtonBox::accepted, this, &VLCProfileEditor::accept);
    connect(ui.buttonBox, &QDialogButtonBox::rejected, this, &VLCProfileEditor::reject);
    connect(ui.valueholder_video_copy, &QtCheckboxChanged, this, &VLCProfileEditor::activatePanels);
    connect(ui.valueholder_audio_copy, &QtCheckboxChanged, this, &VLCProfileEditor::activatePanels);
    connect(ui.valueholder_subtitles_overlay, &QtCheckboxChanged, this, &VLCProfileEditor::activatePanels);
    connect(ui.valueholder_vcodec_bitrate, &QSpinBox::editingFinished, this, &VLCProfileEditor::fixBirateState);
    connect(ui.valueholder_vcodec_qp, &QSpinBox::editingFinished, this, &VLCProfileEditor::fixQPState);
    connect(ui.valueholder_video_codec, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &VLCProfileEditor::codecSelected);
    reset();
    fillProfile(value);
    bool legacyVolume = false;
    for (const auto& option : value.split(QLatin1Char(';')))
        if (option.startsWith(QStringLiteral("acodec_gain=")))
            legacyVolume = option.mid(QStringLiteral("acodec_gain=").size()).toDouble() > 0;
    const bool volumeActive = ui.valueholder_audio_gain->isChecked() || legacyVolume;
    ui.valueholder_audio_gain->setChecked(volumeActive);
    if (!volumeActive) ui.valueholder_acodec_gain->setValue(100);
    ui.valueholder_acodec_gain->setEnabled(volumeActive);
    connect(ui.valueholder_audio_gain,&QCheckBox::toggled,
            ui.valueholder_acodec_gain,&QWidget::setEnabled);
    muxSelected();
    codecSelected();
    for (auto *group : {ui.valueholder_video_enable, ui.valueholder_audio_enable, ui.valueholder_subtitles_enable})
        connect(group, &QGroupBox::toggled, this, &VLCProfileEditor::updateValidation);
    for (auto *box : {ui.valueholder_video_copy, ui.valueholder_audio_copy})
        connect(box, &QtCheckboxChanged, this, &VLCProfileEditor::updateValidation);
    connect(ui.profileLine, &QLineEdit::textChanged, this, &VLCProfileEditor::updateValidation);
    connect(ui.valueholder_vcodec_scale, &QComboBox::currentTextChanged, this, &VLCProfileEditor::updateValidation);
    connect(ui.profileVideoFilterFind,&QLineEdit::textChanged,this,&VLCProfileEditor::updateFilterSearch);
    connect(ui.profileAudioFilterFind,&QLineEdit::textChanged,this,&VLCProfileEditor::updateFilterSearch);
    for (auto *list : {ui.valueholder_video_filters, ui.valueholder_audio_filters})
    {
        connect(list,&QListWidget::itemChanged,this,&VLCProfileEditor::updateFilterSearch);
        list->installEventFilter(this);
    }
    ui.profileVideoFilterFind->installEventFilter(this);
    ui.profileAudioFilterFind->installEventFilter(this);
    updateEditorText();
    applyRedesignWidgetTheme(this, ctx);
    updateLayoutMetrics();
    if (ctx) connect(ctx, &MainCtx::intfScaleFactorChanged, this, &VLCProfileEditor::updateLayoutMetrics);
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, [this,cancel] {
            auto *scale = ui.valueholder_vcodec_scale;
            const bool automatic = scaleValue() == QStringLiteral("0");
            const QString previous = scale->currentText();
            const QSignalBlocker blocker(scale);
            ui.retranslateUi(this);
            scale->setItemText(0, qtr("Auto"));
            if (automatic) { scale->setCurrentIndex(0); scale->setEditText(scale->itemText(0)); }
            else scale->setEditText(previous);
            cancel->setText(qtr("&Cancel"));
            retranslateFilters();
            updateEditorText();
            muxSelected();
        });
    const auto dp = [ctx](int n) { return qRound(n * (ctx ? ctx->getIntfScaleFactor() : 1)); };
    resize(dp(760),dp(620));
    if (screen()) resize(size().boundedTo(screen()->availableGeometry().size()));
    ui.profileLine->setFocus();
    ui.profileLine->selectAll();
}

void VLCProfileEditor::updateEditorText()
{
    const QString title = m_originalName.isEmpty() ? qtr("Create profile") : qtr("Edit profile");
    setWindowTitle(title);
    ui.profileHeading->setText(title);
    ui.profileHelp->setText(qtr("Select a file format. Turn on each track that you want to keep."));
    m_saveButton->setText(m_originalName.isEmpty() ? qtr("&Create") : qtr("&Save"));
    ui.valueholder_acodec_bitrate->setSpecialValueText(qtr("Auto"));
    ui.valueholder_acodec_channels->setSpecialValueText(qtr("Auto"));
    ui.valueholder_acodec_samplerate->setItemText(0,qtr("Auto"));
    ui.valueholder_video_enable->setToolTip(qtr("Turn off to remove the video track from the output."));
    ui.valueholder_audio_enable->setToolTip(qtr("Turn off to remove the audio track from the output."));
    ui.valueholder_subtitles_enable->setToolTip(qtr("Turn off to remove subtitles from the output."));
    ui.valueholder_audio_gain->setToolTip(qtr("Leave this off to keep the source volume."));
    ui.valueholder_acodec_gain->setAccessibleName(qtr("Output volume"));
    ui.valueholder_acodec_gain->setToolTip(qtr("100% keeps the source volume. 0% mutes the output. Values above 100% can cause distortion."));
    ui.valueholder_audio_copy->setToolTip(qtr("Keep the source audio codec. Turn this off to change the output volume or use audio filters."));
    const auto x264Help = qtr("These settings use the x264 encoder. Select H.264 to use them.");
    ui.valueholder_vcodec_custom->setToolTip(x264Help);
    ui.valueholder_vcodec_qp->setToolTip(x264Help);
    for (auto *field : {ui.profileVideoFilterFind,ui.profileAudioFilterFind})
    {
        field->setPlaceholderText(qtr("Find a filter"));
        field->setAccessibleName(qtr("Find a filter"));
    }
    for (auto *button : ui.buttonGroup->buttons())
        if (button->property("profileCustomMux").toBool()) button->setText(qtr("Other: %1").arg(button->property("sout").toString()));
    for (auto *combo : findChildren<QComboBox*>())
        for (int row = 0; row < combo->count(); ++row)
            if (combo->itemData(row,OtherValueRole).toBool()) combo->setItemText(row,qtr("Other: %1").arg(combo->itemData(row).toString()));
    updateFilterSearch();
    updateValidation();
}

void VLCProfileEditor::updateFilterSearch()
{
    const auto update = [](QListWidget *list,QLineEdit *field,QLabel *status) {
        int shown = 0, selected = 0;
        const QString query = field->text().trimmed();
        for (int row = 0; row < list->count(); ++row)
        {
            auto *item = list->item(row);
            const bool match = query.isEmpty() || item->text().contains(query,Qt::CaseInsensitive)
                || item->data(Qt::UserRole).toString().contains(query,Qt::CaseInsensitive);
            item->setHidden(!match);
            shown += match;
            selected += item->checkState() == Qt::Checked;
        }
        const auto text = qtr("Filters shown: %1 of %2. Selected: %3.").arg(shown).arg(list->count()).arg(selected);
        status->setText(shown || query.isEmpty() ? text : qtr("No filters match this text.") + QLatin1Char('\n') + text);
    };
    update(ui.valueholder_video_filters,ui.profileVideoFilterFind,ui.profileVideoFilterInfo);
    update(ui.valueholder_audio_filters,ui.profileAudioFilterFind,ui.profileAudioFilterInfo);
}

void VLCProfileEditor::updateLayoutMetrics()
{
    const auto dp = [this](int n) { return qRound(n * (m_themeContext ? m_themeContext->getIntfScaleFactor() : 1)); };
    layout()->setContentsMargins(dp(24),dp(24),dp(24),dp(24));
    layout()->setSpacing(dp(12));
    for (auto *scroll : findChildren<QScrollArea*>()) scroll->setFrameShape(QFrame::NoFrame);
    ui.muxerwarning->ensurePolished();
    updateMuxerWarning();
}

QString VLCProfileEditor::scaleValue(bool *valid) const
{
    const QString text = ui.valueholder_vcodec_scale->currentText().trimmed();
    if (text == ui.valueholder_vcodec_scale->itemText(0))
    {
        if (valid) *valid = true;
        return QStringLiteral("0");
    }
    bool ok;
    double value = text.toDouble(&ok);
    if (!ok) value = QLocale().toDouble(text,&ok);
    ok = ok && std::isfinite(value) && value >= 0;
    if (valid) *valid = ok;
    return ok ? QString::number(value,'g',15) : QString();
}

QString VLCProfileEditor::validationError() const
{
    const QString candidate = ui.profileLine->text().trimmed();
    if (candidate.isEmpty()) return qtr("Enter a profile name.");
    if (ui.profileLine->text() != m_originalName)
        for (const auto& name : m_existingNames)
            if (name != m_originalName && candidate.compare(name.trimmed(),Qt::CaseInsensitive) == 0)
                return qtr("This profile name is in use. Enter another name.");
    if (!ui.buttonGroup->checkedButton() || ui.buttonGroup->checkedButton()->property("sout").toString().isEmpty())
        return qtr("Select a file format.");
    if (!ui.valueholder_video_enable->isChecked() && !ui.valueholder_audio_enable->isChecked()
        && !ui.valueholder_subtitles_enable->isChecked()) return qtr("Turn on video, audio, or subtitles.");
    if (ui.valueholder_video_enable->isChecked() && !ui.valueholder_video_copy->isChecked())
    {
        bool valid;
        scaleValue(&valid);
        if (!valid) return qtr("Enter %1 or a scale greater than zero.").arg(ui.valueholder_vcodec_scale->itemText(0));
    }
    return {};
}

void VLCProfileEditor::updateValidation()
{
    const auto error = validationError();
    ui.profileFeedback->setText(error.isEmpty() ? qtr("Ready to save the profile.") : error);
    m_saveButton->setEnabled(error.isEmpty());
}

bool VLCProfileEditor::eventFilter(QObject *watched,QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)
    {
        auto *key = static_cast<QKeyEvent*>(event);
        const bool video = watched == ui.valueholder_video_filters || watched == ui.profileVideoFilterFind;
        auto *field = video ? ui.profileVideoFilterFind : ui.profileAudioFilterFind;
        const bool find = key->matches(QKeySequence::Find);
        const bool reset = watched == field && key->key() == Qt::Key_Escape && !field->text().isEmpty();
        if (find || reset)
        {
            if (event->type() == QEvent::KeyPress)
            {
                if (find) { field->setFocus(); field->selectAll(); }
                else field->clear();
            }
            key->accept();
            return true;
        }
    }
    return QVLCDialog::eventFilter(watched,event);
}

void VLCProfileEditor::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
    {
        if (focusWidget() == ui.profileVideoFilterFind) ui.valueholder_video_filters->setFocus();
        else if (focusWidget() == ui.profileAudioFilterFind) ui.valueholder_audio_filters->setFocus();
        else if (auto *button = qobject_cast<QAbstractButton*>(focusWidget())) button->click();
        event->accept();
        return;
    }
    QVLCDialog::keyPressEvent(event);
}

void VLCProfileEditor::selectComboValue(QComboBox *box, const QString& value)
{
    int index = box->findData(value);
    if (index < 0 && !box->isEditable())
    {
        box->addItem(qtr("Other: %1").arg(value), value);
        index = box->count() - 1;
        box->setItemData(index,QStringLiteral("<qt>%1</qt>").arg(value.toHtmlEscaped()),Qt::ToolTipRole);
        box->setItemData(index,true,OtherValueRole);
    }
    box->setCurrentIndex(index);
    if (box->isEditable() && index < 0) box->setEditText(value);
}

void VLCProfileEditor::loadCapabilities()
{
    size_t count;
    module_t **p_all = module_list_get (&count);

    /* Parse the module list for capabilities and probe each of them */
    for (size_t i = 0; i < count; i++)
    {
         module_t *p_module = p_all[i];

         if( module_provides( p_module, "sout mux" ) )
             caps["muxers"].insert( module_get_object( p_module ) );
//        else if ( module_provides( p_module, "encoder" ) )
//            caps["encoders"].insert( module_get_object( p_module ) );
    }
    module_list_free (p_all);
}

inline void VLCProfileEditor::registerFilters()
{
    size_t count;
    module_t **p_all = module_list_get (&count);

    for (size_t i = 0; i < count; i++)
    {
        module_t *p_module = p_all[i];
        if ( module_get_score( p_module ) > 0 ) continue;

        QString capability = module_get_capability( p_module );
        QListWidget *listWidget = NULL;
        QListWidgetItem *item;

        if ( capability == "video filter" )
            listWidget = ui.valueholder_video_filters;
        else if ( capability == "audio filter" )
            listWidget = ui.valueholder_audio_filters;

        if ( !listWidget ) continue;

        item = new QListWidgetItem( module_GetLongName( p_module ) );
        item->setCheckState( Qt::Unchecked );
        item->setToolTip( QString( module_get_help( p_module ) ) );
        item->setData( Qt::UserRole, QString( module_get_object( p_module ) ) );
        item->setData(FilterNameRole, QByteArray(module_GetLongName(p_module)));
        item->setData(FilterHelpRole, QByteArray(module_get_help(p_module)));
        listWidget->addItem( item );
    }
    module_list_free (p_all);

    ui.valueholder_video_filters->sortItems();
    ui.valueholder_audio_filters->sortItems();
    // Keep the canonical order when translating filter names.
    retranslateFilters();
}

void VLCProfileEditor::retranslateFilters()
{
    for (auto* list : {ui.valueholder_video_filters, ui.valueholder_audio_filters})
    {
        const QSignalBlocker blocker(list);
        for (int row = 0; row < list->count(); ++row)
        {
            auto* item = list->item(row);
            if (item->data(MissingFilterRole).toBool())
            {
                item->setText(qtr("Other: %1").arg(item->data(Qt::UserRole).toString()));
                item->setToolTip(qtr("Keep this filter from the saved profile."));
                continue;
            }
            item->setText(qtr(item->data(FilterNameRole).toByteArray().constData()));
            item->setToolTip(qtr(item->data(FilterHelpRole).toByteArray().constData()));
        }
    }
}

inline void VLCProfileEditor::registerCodecs()
{
#define SETMUX( button, val, mod, vid, aud, men, sub, stream, chaps ) \
    ui.button->setProperty( "sout", val );\
    ui.button->setProperty( "module", mod );\
    ui.button->setProperty( "capvideo", vid );\
    ui.button->setProperty( "capaudio", aud );\
    ui.button->setProperty( "capmenu", men );\
    ui.button->setProperty( "capsubs", sub );\
    ui.button->setProperty( "capstream", stream );\
    ui.button->setProperty( "capchaps", chaps );\
    connect( ui.button, &QRadioButton::clicked, this, &VLCProfileEditor::muxSelected );
    SETMUX( PSMux, "ps", "ps",  true, true, false, true, false, true )
    SETMUX( TSMux, "ts", "ts",  true, true, false, true, true, false )
    SETMUX( WEBMux, "webm", "avformat", true, true, false, false, true, false )
    SETMUX( MPEG1Mux, "mpeg1", "ps", true, true, false, false, false, false )
    SETMUX( OggMux, "ogg", "ogg", true, true, false, false, true, true )
    SETMUX( ASFMux, "asf", "asf", true, true, false, true, true, true )
    SETMUX( MOVMux, "mp4", "mp4", true, true, true, true, true, false )
    SETMUX( WAVMux, "wav", "wav", false, true, false, false, false, false )
    SETMUX( FLACMux, "flac", "dummy", false, true, false, false, false, false )
    SETMUX( MP3Mux, "mp3", "dummy", false, true, false, false, false, false )
    SETMUX( RAWMux, "raw", "dummy", true, true, false, false, false, false )
    SETMUX( FLVMux, "flv", "avformat", true, true, false, false, true, false )
    SETMUX( MKVMux, "mkv", "avformat", true, true, true, true, true, true )
    SETMUX( AVIMux, "avi", "avi", true, true, false, false, false, false )
    SETMUX( MJPEGMux, "mpjpeg", "mpjpeg", true, false, false, false, false, false )
#undef SETMUX

#define ADD_VCODEC( name, fourcc ) \
            ui.valueholder_video_codec->addItem( name, QVariant( fourcc ) );
    ADD_VCODEC( "MPEG-1", "mp1v" )
    ADD_VCODEC( "MPEG-2", "mp2v" )
    ADD_VCODEC( "MPEG-4", "mp4v" )
    ADD_VCODEC( "DIVX 1" , "DIV1" )
    ADD_VCODEC( "DIVX 2" , "DIV2" )
    ADD_VCODEC( "DIVX 3" , "DIV3" )
    ADD_VCODEC( "H-263", "H263" )
    ADD_VCODEC( "H-264 (AVC)", "h264" )
    ADD_VCODEC( "H-265 (HEVC)", "hevc" )
    ADD_VCODEC( "AV1", "av01" )
    ADD_VCODEC( "VP8", "VP80" )
    ADD_VCODEC( "WMV1", "WMV1" )
    ADD_VCODEC( "WMV2" , "WMV2" )
    ADD_VCODEC( "M-JPEG", "MJPG" )
    ADD_VCODEC( "Theora", "theo" )
#undef ADD_VCODEC
    /* can do quality */
    qpcodecsList << "h264";

#define ADD_ACODEC( name, fourcc ) ui.valueholder_audio_codec->addItem( name, QVariant( fourcc ) );
    ADD_ACODEC( "MPEG Audio", "mpga" )
    ADD_ACODEC( "MP3", "mp3" )
    ADD_ACODEC( "MPEG 4 Audio ( AAC )", "mp4a" )
    ADD_ACODEC( "A52/AC-3", "a52" )
    ADD_ACODEC( "Vorbis", "vorb" )
    ADD_ACODEC( "Flac", "flac" )
    ADD_ACODEC( "Opus", "opus" )
    ADD_ACODEC( "Speex", "spx" )
    ADD_ACODEC( "PCM 16-bit", "s16l" )
    ADD_ACODEC( "WMA2", "wma2" )
#undef ADD_ACODEC

#define ADD_SCALING( factor ) ui.valueholder_vcodec_scale->addItem( factor );
    ADD_SCALING( qtr("Auto") );
    ui.valueholder_vcodec_scale->setItemData(0, QStringLiteral("0"));
    ADD_SCALING( "1" )
    ADD_SCALING( "0.25" )
    ADD_SCALING( "0.5" )
    ADD_SCALING( "0.75" )
    ADD_SCALING( "1.25" )
    ADD_SCALING( "1.5" )
    ADD_SCALING( "1.75" )
    ADD_SCALING( "2" )
#undef ADD_SCALING

#define ADD_SAMPLERATE( sample, val ) ui.valueholder_acodec_samplerate->addItem( sample, val );
    ADD_SAMPLERATE(qtr("Auto"),0)
    ADD_SAMPLERATE( "8000 Hz", 8000 )
    ADD_SAMPLERATE( "11025 Hz", 11025 )
    ADD_SAMPLERATE( "22050 Hz", 22050 )
    ADD_SAMPLERATE( "44100 Hz", 44100 )
    ADD_SAMPLERATE( "48000 Hz", 48000 )
    ADD_SAMPLERATE("88200 Hz",88200)
    ADD_SAMPLERATE("96000 Hz",96000)
    ADD_SAMPLERATE("176400 Hz",176400)
    ADD_SAMPLERATE("192000 Hz",192000)
#undef ADD_SAMPLERATE

#define ADD_SCODEC( name, fourcc ) ui.valueholder_subtitles_codec->addItem( name, QVariant( fourcc ) );
    ADD_SCODEC( "DVBS (DVB subtitles)", "dvbs" )
    ADD_SCODEC( "tx3g (MPEG-4 timed text)", "tx3g" )
    ADD_SCODEC( "T-REC 140 (for rtp)", "t140" )
#undef ADD_SCODEC
}

void VLCProfileEditor::muxSelected()
{
    QRadioButton *current =
            qobject_cast<QRadioButton *>( ui.buttonGroup->checkedButton() );

    const bool unknown = current->property("profileCustomMux").toBool();
#define SETCAP(name,prop,title) ui.name->setText(qtr("%1: %2").arg(qtr(title), \
    unknown ? qtr("Unknown") : current->property(prop).toBool() ? qtr("Yes") : qtr("No")))
    SETCAP(capvideo,"capvideo","Video");
    SETCAP(capaudio,"capaudio","Audio");
    SETCAP(capmenu,"capmenu","Menus");
    SETCAP(capsubs,"capsubs","Subtitles");
    SETCAP(capstream,"capstream","Streamable");
    SETCAP(capchaps,"capchaps","Chapters");
#undef SETCAP
    updateMuxerWarning();
}

void VLCProfileEditor::updateMuxerWarning()
{
    const auto* current = ui.buttonGroup->checkedButton();
    int textsize = QFontMetrics(ui.muxerwarning->font()).ascent();
    if (current->property("profileCustomMux").toBool())
        ui.muxerwarning->setText(qtr("This file format uses a saved value. Check that it is available on this device."));
    else if( current->property("module").toString() == "avformat" )
        ui.muxerwarning->setText(
                    QString( "<img src=\":/menu/info.svg\" width=%2 height=%2/> %1" )
                    .arg( qtr( "This file format may not be available in this build." ) )
                    .arg(textsize)
                    );
    else if ( !caps["muxers"].contains( current->property("module").toString() ) &&
              !caps["muxers"].contains( "mux_" + current->property("module").toString() ) )
        ui.muxerwarning->setText(
                    QString( "<img src=\":/menu/clear.svg\" width=%2 height=%2/> %1" )
                    .arg( qtr( "This file format is not available in this build." ) )
                    .arg(textsize)
                    );
    else
        ui.muxerwarning->setText("");
}

void VLCProfileEditor::codecSelected()
{
    /* Enable quality preset */
    QString currentcodec = ui.valueholder_video_codec->
            itemData(ui.valueholder_video_codec->currentIndex() ).toString();
    const bool x264 = qpcodecsList.contains(currentcodec);
    ui.valueholder_vcodec_qp->setEnabled(x264);
    ui.valueholder_vcodec_custom->setEnabled(x264);
}

void VLCProfileEditor::fillProfile( const QString& qs )
{
    QRegularExpression rx(QStringLiteral(OLDFORMAT));
    if ( rx.match( qs ).hasMatch() ) return fillProfileOldFormat( qs );

    QStringList tuples = qs.split( ";" );
    foreach ( const QString &tuple, tuples )
    {
        const int equal = tuple.indexOf(QLatin1Char('='));
        if (equal <= 0) continue;
        const QString key = tuple.left(equal);
        const QString value = tuple.mid(equal + 1);
        QObject *object = findChild<QObject *>( CATPROP2NAME( key ) );
        if ( object )
        {
            if( object->inherits( "QButtonGroup" ) )
            { /* Buttongroup for Radios */
                const QButtonGroup *group = qobject_cast<const QButtonGroup *>( object );
                bool found = false;
                foreach( QAbstractButton *button, group->buttons() )
                {
                    if ( button->property("sout").toString() == value )
                    {
                        button->setChecked( true );
                        found = true;
                        break;/* radios are exclusive */
                    }
                }
                if (!found && group == ui.buttonGroup)
                {
                    auto *other = new QRadioButton(qtr("Other: %1").arg(value),ui.muxer);
                    other->setProperty("sout",value);
                    other->setProperty("profileCustomMux",true);
                    other->setToolTip(QStringLiteral("<qt>%1</qt>").arg(value.toHtmlEscaped()));
                    ui.buttonGroup->addButton(other);
                    auto *grid = qobject_cast<QGridLayout*>(ui.muxer->layout());
                    grid->addWidget(other,grid->rowCount(),0,1,grid->columnCount());
                    connect(other,&QRadioButton::clicked,this,&VLCProfileEditor::muxSelected);
                    other->setChecked(true);
                }
            }
            else if( object->inherits( "QCheckBox" ) )
            {
                QCheckBox *box = qobject_cast<QCheckBox *>( object );
                box->setChecked( ! value.isEmpty() );
            }
            else if( object->inherits( "QGroupBox" ) )
            {
                QGroupBox *box = qobject_cast<QGroupBox *>( object );
                box->setChecked( ! value.isEmpty() );
            }
            else if( object->inherits( "QSpinBox" ) )
            {
                QSpinBox *box = qobject_cast<QSpinBox *>( object );
                const int number = value.toInt();
                if (number > box->maximum()) box->setMaximum(number);
                box->setValue(number);
            }
            else if( object->inherits( "QDoubleSpinBox" ) )
            {
                QDoubleSpinBox *box = qobject_cast<QDoubleSpinBox *>( object );
                const double number = value.toDouble();
                if (std::isfinite(number) && number > box->maximum()) box->setMaximum(number);
                box->setValue(number);
            }
            else if( object->inherits( "QComboBox" ) )
            {
                QComboBox *box = qobject_cast<QComboBox *>( object );
                selectComboValue(box,value);
                if (box == ui.valueholder_vcodec_scale)
                {
                    // Old profiles can contain a translated Auto caption.
                    bool numeric;
                    const double factor = value.toDouble(&numeric);
                    if (!numeric || factor <= 0)
                    {
                        box->setCurrentIndex(0);
                        continue;
                    }
                }
                if ( box->lineEdit() && box->currentIndex() == -1 )
                    box->lineEdit()->setText( value );
            }
            else if( object->inherits( "QLineEdit" ) )
            {
                QLineEdit *box = qobject_cast<QLineEdit *>( object );
                box->setText( QUrl::fromPercentEncoding( value.toUtf8() ) );
            }
            else if ( object->inherits( "QListWidget" ) )
            {
                QStringList valuesList = QUrl::fromPercentEncoding( value.toUtf8() ).split( ";" );
                const QListWidget *list = qobject_cast<const QListWidget *>( object );
                for (const auto& value : valuesList)
                {
                    bool found = false;
                    for (int row = 0; row < list->count(); ++row)
                        found |= list->item(row)->data(Qt::UserRole).toString() == value;
                    if (!found && !value.isEmpty())
                    {
                        auto *extra = new QListWidgetItem(qtr("Other: %1").arg(value),const_cast<QListWidget*>(list));
                        extra->setData(Qt::UserRole,value);
                        extra->setData(MissingFilterRole,true);
                        extra->setToolTip(qtr("Keep this filter from the saved profile."));
                        extra->setFlags(extra->flags() | Qt::ItemIsUserCheckable);
                    }
                }
                for( int i=0; i < list->count(); i++ )
                {
                    QListWidgetItem *item = list->item( i );
                    if ( valuesList.contains( item->data( Qt::UserRole ).toString() ) )
                        item->setCheckState( Qt::Checked );
                    else
                        item->setCheckState( Qt::Unchecked );
                }
            }
        }
        else m_extraOptions.append(tuple);
    }
}

void VLCProfileEditor::fillProfileOldFormat( const QString& qs )
{
    QStringList options = qs.split( ";" );
    if( options.count() < 16 )
        return;

    fillProfile(QStringLiteral("muxer_mux=") + options[0]);
    const auto setStoredValue = [](auto *box, auto value) {
        if (value > box->maximum()) box->setMaximum(value);
        box->setValue(value);
    };

    ui.valueholder_video_copy->setChecked( !options[1].toInt() );
    ui.valueholder_video_enable->setChecked( ( options[4] != "none" ) );
    ui.valueholder_audio_copy->setChecked( !options[2].toInt() );
    ui.valueholder_audio_enable->setChecked( ( options[10] != "none" ) );
    ui.valueholder_subtitles_enable->setChecked( options[3].toInt() );

    selectComboValue(ui.valueholder_video_codec,options[4]);
    setStoredValue(ui.valueholder_vcodec_bitrate,options[5].toInt());
    if ( options[6].toDouble() > 0 )
        ui.valueholder_vcodec_scale->setEditText( options[6] );
    else
        ui.valueholder_vcodec_scale->setCurrentIndex( 0 );
    setStoredValue(ui.valueholder_vcodec_framerate,options[7].toDouble());
    setStoredValue(ui.valueholder_vcodec_width,options[8].toInt());
    setStoredValue(ui.valueholder_vcodec_height,options[9].toInt());

    selectComboValue(ui.valueholder_audio_codec,options[10]);
    setStoredValue(ui.valueholder_acodec_bitrate,options[11].toInt());
    setStoredValue(ui.valueholder_acodec_channels,options[12].toInt());

    selectComboValue(ui.valueholder_acodec_samplerate,options[13]);

    selectComboValue(ui.valueholder_subtitles_codec,options[14]);
    ui.valueholder_subtitles_overlay->setChecked( options[15].toInt() );
}

void VLCProfileEditor::accept()
{
    updateValidation();
    if (!validationError().isEmpty()) return;
    name = ui.profileLine->text() == m_originalName ? m_originalName : ui.profileLine->text().trimmed();
    QVLCDialog::accept();
}

#define currentData( box ) box->itemData( box->currentIndex() )

QString VLCProfileEditor::transcodeValue()
{
    QList<QObject *> allwidgets = findChildren<QObject *>();
    QStringList configuration;
    const bool encodingVideo = ui.valueholder_video_enable->isChecked()
        && !ui.valueholder_video_copy->isChecked();

    foreach( const QObject *object, allwidgets )
    {
        if ( ! object->objectName().startsWith( CATPROP2NAME( "" ) ) )
            continue;
        const bool savedX264Setting = object == ui.valueholder_vcodec_custom || object == ui.valueholder_vcodec_qp;
        if (object->inherits("QWidget") && !qobject_cast<const QWidget*>(object)->isEnabled()
            && !(savedX264Setting && encodingVideo)) continue;

        QString name = object->objectName();
        QStringList vals = object->objectName().split( "_" );
        if ( vals.count() != 3 ) continue;
        QString &categ = vals[1];
        QString &prop = vals[2];
        QString value;

        if( object->inherits( "QButtonGroup" ) )
        {
            const QButtonGroup *group = qobject_cast<const QButtonGroup *>( object );
            value = group->checkedButton()->property( "sout" ).toString();
        }
        else if( object->inherits( "QCheckBox" ) )
        {
            const QCheckBox *box = qobject_cast<const QCheckBox *>( object );
            value = box->isChecked() ? "yes" : "";
        }
        else if( object->inherits( "QGroupBox" ) )
        {
            const QGroupBox *box = qobject_cast<const QGroupBox *>( object );
            value = box->isChecked() ? "yes" : "";
        }
        else if( object->inherits( "QSpinBox" ) )
        {
            const QSpinBox *box = qobject_cast<const QSpinBox *>( object );
            value = QString::number( box->value() );
        }
        else if( object->inherits( "QDoubleSpinBox" ) )
        {
            const QDoubleSpinBox *box = qobject_cast<const QDoubleSpinBox *>( object );
            value =  QString::number( box->value() );
        }
        else if( object->inherits( "QComboBox" ) )
        {
            const QComboBox *box = qobject_cast<const QComboBox *>( object );
            value = currentData( box ).toString();
            if (box->isEditable() && box->currentText() != box->itemText(box->currentIndex()))
                value = box->currentText();
            if ( value.isEmpty() && box->lineEdit() ) value = box->lineEdit()->text();
            if (box == ui.valueholder_vcodec_scale) value = scaleValue();
        }
        else if( object->inherits( "QLineEdit" ) )
        {
            const QLineEdit *box = qobject_cast<const QLineEdit *>( object );
            value = QUrl::toPercentEncoding( box->text(), "", "_;" );
        }
        else if ( object->inherits( "QListWidget" ) )
        {
            const QListWidget *list = qobject_cast<const QListWidget *>( object );
            QStringList valuesList;
            for( int i=0; i < list->count(); i++ )
            {
                const QListWidgetItem *item = list->item( i );
                if ( item->checkState() == Qt::Checked )
                    valuesList.append( item->data( Qt::UserRole ).toString() );
            }
            value = QUrl::toPercentEncoding( valuesList.join( ";" ), "", "_;" );
        }

        if ( !value.isEmpty() )
            configuration << QString( "%1_%2=%3" )
                             .arg( categ ).arg( prop ).arg( value );
    }

    configuration.append(m_extraOptions);
    return configuration.join( ";" );
}

void VLCProfileEditor::reset()
{
    /* reset to default state as we can only check/enable existing values */
    ui.valueholder_video_copy->setChecked( false );
    ui.valueholder_audio_copy->setChecked( false );
    activatePanels();
    fixBirateState(); /* defaults to bitrate, not qp */
    /* end with top level ones for cascaded setEnabled() */
    ui.valueholder_video_enable->setChecked( false );
    ui.valueholder_audio_enable->setChecked( false );
    ui.valueholder_subtitles_enable->setChecked( false );
}

void VLCProfileEditor::activatePanels()
{
    ui.transcodevideo->setEnabled( ! ui.valueholder_video_copy->isChecked() );
    ui.transcodeaudio->setEnabled( ! ui.valueholder_audio_copy->isChecked() );
    ui.valueholder_subtitles_codec->setEnabled( ! ui.valueholder_subtitles_overlay->isChecked() );
}

void VLCProfileEditor::fixBirateState()
{
    /* exclusive bitrate choice */
    ui.valueholder_vcodec_qp->setValue( 0 );
}

void VLCProfileEditor::fixQPState()
{
    /* exclusive bitrate choice */
    ui.valueholder_vcodec_bitrate->setValue( 0 );
}

#undef CATPROP2NAME
#undef CATANDPROP
#undef OLDFORMAT
