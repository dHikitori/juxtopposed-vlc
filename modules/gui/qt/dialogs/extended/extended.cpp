/*****************************************************************************
 * extended.cpp : Extended controls - Undocked
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
 *****************************************************************************/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <cassert>
#include <cmath>

#include "extended.hpp"
#include "util/interface_translator.hpp"
#include "dialogs/dialogs_provider.hpp"

#include "maininterface/mainctx.hpp"
#include "maininterface/compositor.hpp" /* Needed for external MI size */
#include "player/player_controller.hpp"

#include <QTimer>
#include <vlc_aout.h>
#include <vlc_vout.h>
#include <QTabWidget>
#include <QTabBar>
#include <QComboBox>
#include <QPointer>
#include <QScreen>
#include <QGridLayout>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QWindow>
#include <QKeyEvent>
#include <QAbstractSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <vlc_configuration.h>
#include <vlc_modules.h>

namespace {
class EffectsSectionPicker final : public QObject
{
public:
    EffectsSectionPicker(QTabWidget* tabs, MainCtx* ctx, const char* label) : QObject(tabs), m_tabs(tabs), m_ctx(ctx), m_label(label)
    {
        auto* parent = tabs->parentWidget();
        auto* wrapper = new QWidget(parent);
        auto* oldItem = parent->layout()->replaceWidget(tabs, wrapper);
        delete oldItem;
        auto* layout = new QVBoxLayout(wrapper); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(0);
        m_picker = new QComboBox(wrapper);
        m_picker->setObjectName(QStringLiteral("effectsSectionPicker"));
        layout->addWidget(m_picker, 0, Qt::AlignLeft); layout->addWidget(tabs, 1);
        tabs->installEventFilter(this);
        tabs->tabBar()->installEventFilter(this);
        connect(m_picker.data(), QOverload<int>::of(&QComboBox::activated), tabs, &QTabWidget::setCurrentIndex);
        connect(tabs, &QTabWidget::currentChanged, this, [this] { refresh(); });
        if (ctx) connect(ctx, &MainCtx::intfScaleFactorChanged, this, [this] { schedule(); });
        schedule();
    }
    void refresh()
    {
        if (!m_tabs || !m_picker) return;
        const QSignalBlocker block(m_picker.data());
        m_picker->setAccessibleName(qtr(m_label));
        m_picker->setToolTip(qtr("Select a section."));
        bool changed = m_picker->count() != m_tabs->count();
        for (int i = 0; !changed && i < m_tabs->count(); ++i) changed = m_picker->itemText(i) != m_tabs->tabText(i);
        if (changed) { m_picker->clear(); for (int i = 0; i < m_tabs->count(); ++i) m_picker->addItem(m_tabs->tabText(i)); }
        m_picker->setCurrentIndex(m_tabs->currentIndex());
        const bool compact = m_tabs->tabBar()->sizeHint().width() > m_tabs->contentsRect().width();
        const int width = qMin(m_tabs->contentsRect().width(), qMax(qRound(180 * (m_ctx ? m_ctx->getIntfScaleFactor() : 1)), m_picker->sizeHint().width()));
        if (m_picker->width() != width) m_picker->setFixedWidth(width);
        if (m_tabs->tabBar()->isHidden() != compact) m_tabs->tabBar()->setVisible(!compact);
        if (m_picker->isHidden() == compact) m_picker->setVisible(compact);
    }
protected:
    bool eventFilter(QObject* object, QEvent* event) override
    {
        if (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest || event->type() == QEvent::Show) schedule();
        return QObject::eventFilter(object, event);
    }
private:
    void schedule()
    {
        if (m_queued) return;
        m_queued = true;
        QTimer::singleShot(0, this, [this] { m_queued = false; refresh(); });
    }
    QPointer<QTabWidget> m_tabs;
    QPointer<MainCtx> m_ctx;
    QPointer<QComboBox> m_picker;
    const char* m_label;
    bool m_queued = false;
};
QVariant savedEffectValue(const QString& name, const QVariant& type)
{
    const auto key = name.toUtf8();
    if (type.metaType().id() == QMetaType::QString) {
        char* value = config_GetPsz(key.constData());
        const auto result = qfu(value ? value : ""); free(value); return result;
    }
    if (type.metaType().id() == QMetaType::Float || type.metaType().id() == QMetaType::Double)
        return config_GetFloat(key.constData());
    return QVariant::fromValue(config_GetInt(key.constData()));
}
template<typename Output>
QVariant currentEffectValue(Output* output, const QString& name)
{
    if (!output) return {};
    const auto key = name.toUtf8();
    // Command and choice flags do not change the variable value type.
    switch (var_Type(output, key.constData()) & VLC_VAR_CLASS) {
    case VLC_VAR_FLOAT: { const float value = var_GetFloat(output, key.constData()); return std::isfinite(value) ? QVariant(value) : QVariant(); }
    case VLC_VAR_BOOL: return var_GetBool(output, key.constData());
    case VLC_VAR_INTEGER: return QVariant::fromValue(var_GetInteger(output, key.constData()));
    case VLC_VAR_STRING: {
        char* value = var_GetString(output, key.constData());
        const auto result = qfu(value ? value : ""); free(value); return result;
    }
    default: return {};
    }
}
}

ExtendedDialog::ExtendedDialog( qt_intf_t *_p_intf )
               : QVLCDialog( nullptr, _p_intf )
{
#ifdef __APPLE__
    setWindowFlags( Qt::Drawer );
#else
    setWindowFlags( Qt::Tool );
#endif

    setWindowOpacity( var_InheritFloat( p_intf, "qt-opacity" ) );
    setWindowTitle( qtr( "Adjustments and Effects" ) );
    setWindowRole( "vlc-extended" );

    QVBoxLayout *layout = new QVBoxLayout( this );
    layout->setContentsMargins( 0, 2, 0, 1 );
    layout->setSpacing( 3 );

    mainTabW = new QTabWidget( this );

    /* AUDIO effects */
    QWidget *audioWidget = new QWidget;
    QHBoxLayout *audioLayout = new QHBoxLayout( audioWidget );
    QTabWidget *audioTab = new QTabWidget( audioWidget );
    audioTab->setObjectName(QStringLiteral("audioEffectTabs"));

    equal = new Equalizer( p_intf, audioTab );
    connect( equal, &AudioFilterControlWidget::configChanged, this, &ExtendedDialog::putAudioConfig );
    audioTab->addTab( equal, qtr( "Equalizer" ) );

    Compressor *compres = new Compressor( p_intf, audioTab );
    connect( compres, &AudioFilterControlWidget::configChanged, this, &ExtendedDialog::putAudioConfig );
    audioTab->addTab( compres, qtr( "Compressor" ) );

    Expander *expand = new Expander( p_intf, audioTab );
    connect( expand, &AudioFilterControlWidget::configChanged, this, &ExtendedDialog::putAudioConfig );
    audioTab->addTab( expand, qtr( "Expander" ) );

    Limiter *limit = new Limiter( p_intf, audioTab );
    connect( limit, &AudioFilterControlWidget::configChanged, this, &ExtendedDialog::putAudioConfig );
    audioTab->addTab( limit, qtr( "Limiter" ) );

    Spatializer *spatial = new Spatializer( p_intf, audioTab );
    connect( spatial, &AudioFilterControlWidget::configChanged, this, &ExtendedDialog::putAudioConfig );
    audioTab->addTab( spatial, qtr( "Spatializer" ) );

    StereoWidener *stereowiden = new StereoWidener( p_intf, audioTab );
    connect( stereowiden, &AudioFilterControlWidget::configChanged, this, &ExtendedDialog::putAudioConfig );
    audioTab->addTab( stereowiden, qtr( "Stereo Widener" ) );

    QWidget *advancedTab = new QWidget;
    QGridLayout *advancedTabLayout = new QGridLayout;

    PitchShifter *pitchshifter = new PitchShifter( p_intf, audioTab );
    connect( pitchshifter, &AudioFilterControlWidget::configChanged, this, &ExtendedDialog::putAudioConfig );

    StereoPanner *stereopanner = new StereoPanner( p_intf, audioTab );
    connect( stereopanner, &AudioFilterControlWidget::configChanged, this, &ExtendedDialog::putAudioConfig );

    advancedTabLayout->setColumnStretch( 0, 1 );
    advancedTabLayout->setColumnStretch( 1, 1 );
    advancedTabLayout->addWidget( pitchshifter, 0, 0 );
    advancedTabLayout->addWidget( stereopanner, 0, 1 );

    advancedTab->setLayout( advancedTabLayout );
    audioTab->addTab( advancedTab, qtr( "Advanced" ) );

    audioLayout->addWidget( audioTab );

    mainTabW->insertTab( AUDIO_TAB, audioWidget, qtr( "Audio Effects" ) );

    /* Video Effects */
    QWidget *videoWidget = new QWidget;
    QHBoxLayout *videoLayout = new QHBoxLayout( videoWidget );
    QTabWidget *videoTab = new QTabWidget( videoWidget );

    videoEffect = new ExtVideo( p_intf, videoTab );
    connect( videoEffect, &ExtVideo::configChanged, this, &ExtendedDialog::putVideoConfig );
    videoLayout->addWidget( videoTab );
    videoTab->setSizePolicy( QSizePolicy::Preferred, QSizePolicy::Maximum );

    mainTabW->insertTab( VIDEO_TAB, videoWidget, qtr( "Video Effects" ) );

    syncW = new SyncControls( p_intf, mainTabW );
    mainTabW->insertTab( SYNCHRO_TAB, syncW, qtr( "Synchronization" ) );

    if( module_exists( "v4l2" ) )
    {
        ExtV4l2 *v4l2 = new ExtV4l2( p_intf, mainTabW );
        mainTabW->insertTab( V4L2_TAB, v4l2, qtr( "Camera controls" ) );
        if (auto *translator = InterfaceTranslator::instance())
            connect(translator, &InterfaceTranslator::languageChanged, this, [this, v4l2] {
                mainTabW->setTabText(mainTabW->indexOf(v4l2), qtr("Camera controls"));
            });
    }

    layout->addWidget( mainTabW );
    connect( mainTabW, &QTabWidget::currentChanged, this, &ExtendedDialog::currentTabChanged );

    /* Bottom buttons */
    QDialogButtonBox *buttonBox = new QDialogButtonBox( Qt::Horizontal, this );

    m_applyButton = new QPushButton( qtr("&Save"), this );
    m_applyButton->setObjectName(QStringLiteral("saveEffects"));
    m_applyButton->setEnabled( false );
    buttonBox->addButton( m_applyButton, QDialogButtonBox::ApplyRole );

    auto* closeButton = new QPushButton(qtr("&Close"), this);
    buttonBox->addButton(closeButton, QDialogButtonBox::RejectRole);
    layout->addWidget( buttonBox );

    connect( buttonBox, &QDialogButtonBox::rejected, this, &ExtendedDialog::close );
    connect( m_applyButton, &QPushButton::clicked, this, &ExtendedDialog::saveConfig );

    /* Restore geometry or move this dialog on the left pane of the MI */
    if( !restoreGeometry( getSettings()->value("EPanel/geometry").toByteArray() ) )
    {
        resize( QSize( 400, 280 ) );

        QWindow *window = p_intf->p_compositor->interfaceMainWindow();
        if( window && window->x() > 50 )
            move( ( window->x() - frameGeometry().width() - 10 ), window->y() );
        else
            move ( 450 , 0 );
    }

    connect( THEMIM, &PlayerController::playingStateChanged, this, &ExtendedDialog::changedItem );
    m_savedAudioChain = savedEffectValue(QStringLiteral("audio-filter"), QString()).toString();
    auto* refresh = new QTimer(this);
    refresh->setInterval(250);
    connect(refresh, &QTimer::timeout, this, [this] { if (isVisible()) { refreshAudioConfig(); refreshVideoConfig(); } });
    connect(THEMIM, &PlayerController::inputChanged, this, &ExtendedDialog::refreshAudioConfig);
    connect(THEMIM, &PlayerController::inputChanged, this, &ExtendedDialog::refreshVideoConfig);
    if (p_intf->p_mi) connect(p_intf->p_mi, &MainCtx::redesignPlayerPreferencesChanged,
        this, &ExtendedDialog::refreshAudioConfig);
    for (auto* controls : findChildren<AudioFilterControlWidget*>())
        connect(controls, &AudioFilterControlWidget::draftChanged, this, &ExtendedDialog::updateAudioSave);
    if (p_intf->p_mi) connect(p_intf->p_mi, &MainCtx::redesignPlayerPreferencesChanged,
        this, &ExtendedDialog::refreshVideoConfig);
    connect(videoEffect, &ExtVideo::draftChanged, this, &ExtendedDialog::updateVideoSave);
    for (const auto* type : {"video-filter", "sub-source", "sub-filter", "video-splitter"})
        m_savedVideoChains.insert(qfu(type), savedEffectValue(qfu(type), QString()).toString());
    auto* mainSections = new EffectsSectionPicker(mainTabW, p_intf->p_mi, N_("Effects page"));
    auto* audioSections = new EffectsSectionPicker(audioTab, p_intf->p_mi, N_("Audio effects section"));
    auto* videoSections = new EffectsSectionPicker(videoTab, p_intf->p_mi, N_("Video effects section"));
    const auto refreshText = [this, audioTab, closeButton, mainSections, audioSections, videoSections] {
        setWindowTitle(qtr("Adjustments and Effects"));
        mainTabW->setTabText(AUDIO_TAB, qtr("Audio Effects")); mainTabW->setTabText(VIDEO_TAB, qtr("Video Effects")); mainTabW->setTabText(SYNCHRO_TAB, qtr("Synchronization"));
        const char* labels[] = {N_("Equalizer"), N_("Compressor"), N_("Expander"), N_("Limiter"), N_("Spatializer"), N_("Stereo Widener"), N_("Advanced")};
        for (int i = 0; i < qMin(audioTab->count(), 7); ++i) audioTab->setTabText(i, qtr(labels[i]));
        m_applyButton->setText(qtr("&Save")); closeButton->setText(qtr("&Close")); mainSections->refresh(); audioSections->refresh(); videoSections->refresh();
    };
    if (auto* translator = InterfaceTranslator::instance()) connect(translator, &InterfaceTranslator::languageChanged, this, refreshText);
    refreshText();
    refresh->start();
    refreshAudioConfig();
    refreshVideoConfig();
}

ExtendedDialog::~ExtendedDialog()
{
    getSettings()->setValue("EPanel/geometry", saveGeometry());
}

bool ExtendedDialog::event(QEvent* event)
{
    if (event->type() == QEvent::Show) {
        refreshAudioConfig(); refreshVideoConfig();
        QTimer::singleShot(0, this, [this] {
            if (!isVisible() || !screen() || isMaximized() || isFullScreen()) return;
            const auto available = screen()->availableGeometry();
            resize(size().boundedTo(available.size()));
            const auto frame = frameGeometry();
            const QPoint position(qBound(available.left(), frame.left(), qMax(available.left(), available.right() - frame.width() + 1)),
                                  qBound(available.top(), frame.top(), qMax(available.top(), available.bottom() - frame.height() + 1)));
            move(pos() + position - frame.topLeft());
        });
    }
    // The main interface also has an application-wide Ctrl+S streaming action.
    if (event->type() == QEvent::ShortcutOverride && static_cast<QKeyEvent*>(event)->matches(QKeySequence::Save)) {
        event->accept();
        return true;
    }
    return QVLCDialog::event(event);
}

void ExtendedDialog::keyPressEvent(QKeyEvent* event)
{
    if (event->matches(QKeySequence::Save)) {
        if (m_applyButton->isVisible()) {
            for (auto* focus = focusWidget(); focus && focus != this; focus = focus->parentWidget()) {
                if (auto* field = qobject_cast<QAbstractSpinBox*>(focus)) {
                    if (!field->hasAcceptableInput()) { field->setFocus(); event->accept(); return; }
                    field->interpretText();
                    break;
                }
            }
            m_applyButton->click();
        }
        event->accept();
        return;
    }
    QVLCDialog::keyPressEvent(event);
}

void ExtendedDialog::showTab( int i )
{
    mainTabW->setCurrentIndex( i );
    show();
}

int ExtendedDialog::currentTab()
{
    return mainTabW->currentIndex();
}

void ExtendedDialog::changedItem( PlayerController::PlayingState i_status )
{
    if( i_status != PlayerController::PLAYING_STATE_STOPPED ) return;
    syncW->clean();
    videoEffect->clean();
}

void ExtendedDialog::currentTabChanged( int i )
{
    if( i == AUDIO_TAB || i == VIDEO_TAB )
    {
        m_applyButton->setVisible( true );
        m_applyButton->setEnabled( !m_hashConfigs[i].isEmpty() );
        m_applyButton->setFocusPolicy( Qt::StrongFocus );
    }
    else
    {
        m_applyButton->setVisible( false );
        m_applyButton->setFocusPolicy( Qt::NoFocus );
    }
}

void ExtendedDialog::putAudioConfig( const QString& name, const QVariant value )
{
    if (!m_hashConfigs[AUDIO_TAB].contains(name)) m_savedAudioValues.insert(name, savedEffectValue(name, value));
    m_hashConfigs[AUDIO_TAB].insert( name, value );
    m_applyButton->setEnabled( true );
}

void ExtendedDialog::putVideoConfig( const QString& name, const QVariant value )
{
    if (!m_hashConfigs[VIDEO_TAB].contains(name)) m_savedVideoValues.insert(name, savedEffectValue(name, value));
    m_hashConfigs[VIDEO_TAB].insert( name, value );
    m_applyButton->setEnabled( true );
}

void ExtendedDialog::refreshAudioConfig()
{
    const auto output = THEMIM->getAout();
    auto& pending = m_hashConfigs[AUDIO_TAB];
    for (auto it = pending.begin(); it != pending.end();) {
        const auto saved = savedEffectValue(it.key(), it.value());
        const auto current = currentEffectValue(output.get(), it.key());
        // A later defaults change supersedes this window's older pending value.
        // Failed local saves update the snapshot but retain the retry state.
        const bool superseded = saved != m_savedAudioValues.value(it.key());
        if (superseded || (!m_audioSaveFailed
            && (current.isValid() ? current == saved : it.value() == saved))) {
            m_savedAudioValues.remove(it.key()); it = pending.erase(it); continue;
        }
        if (current.isValid()) it.value() = current;
        ++it;
    }
    if (pending.isEmpty()) m_audioSaveFailed = false;
    const auto savedChain = savedEffectValue(QStringLiteral("audio-filter"), QString()).toString();
    const auto currentChain = currentEffectValue(output.get(), QStringLiteral("audio-filter"));
    // The interface shadow keeps sequential changes when no output exists.
    // Keep it current after defaults change elsewhere or the live chain changes.
    if (currentChain.isValid() || savedChain != m_savedAudioChain) {
        if (var_Type(p_intf, "audio-filter") == 0) var_Create(p_intf, "audio-filter", VLC_VAR_STRING);
        var_SetString(p_intf, "audio-filter", qtu(currentChain.isValid() ? currentChain.toString()
            : pending.value(QStringLiteral("audio-filter"), savedChain).toString()));
    }
    m_savedAudioChain = savedChain;
    for (auto* controls : findChildren<AudioFilterControlWidget*>()) controls->refreshAudioValues(pending);
    updateAudioSave();
}

void ExtendedDialog::updateAudioSave()
{
    if (currentTab() != AUDIO_TAB) return;
    bool draft = false;
    for (auto* controls : findChildren<AudioFilterControlWidget*>())
        for (auto* field : controls->findChildren<QDoubleSpinBox*>())
            if (field->isVisible() && field->isEnabled() && field->hasAcceptableInput()
                && field->findChild<QLineEdit*>()->isModified()) draft = true;
    m_applyButton->setEnabled(!m_hashConfigs[AUDIO_TAB].isEmpty() || draft);
}

void ExtendedDialog::refreshVideoConfig()
{
    const auto output = THEMIM->getVout();
    auto& pending = m_hashConfigs[VIDEO_TAB];
    for (auto it = pending.begin(); it != pending.end();) {
        const auto saved = savedEffectValue(it.key(), it.value());
        const auto current = it.key() == QLatin1String("video-splitter") ? QVariant() : currentEffectValue(output.get(), it.key());
        if (saved != m_savedVideoValues.value(it.key())
            || (!m_videoSaveFailed && current.isValid() && current == saved)) {
            m_savedVideoValues.remove(it.key()); it = pending.erase(it); continue;
        }
        if (current.isValid()) it.value() = current;
        ++it;
    }
    if (pending.isEmpty()) m_videoSaveFailed = false;
    for (const auto* type : {"video-filter", "sub-source", "sub-filter", "video-splitter"}) {
        const auto key = qfu(type);
        const auto saved = savedEffectValue(key, QString()).toString();
        const auto current = key == QLatin1String("video-splitter") ? QVariant() : currentEffectValue(output.get(), key);
        if (current.isValid() || saved != m_savedVideoChains.value(key)) {
            if (var_Type(p_intf, type) == 0) var_Create(p_intf, type, VLC_VAR_STRING);
            var_SetString(p_intf, type, qtu(current.isValid() ? current.toString() : pending.value(key, saved).toString()));
        }
        m_savedVideoChains.insert(key, saved);
    }
    videoEffect->refreshVideoValues(pending);
    updateVideoSave();
}

void ExtendedDialog::updateVideoSave()
{
    if (currentTab() == VIDEO_TAB) m_applyButton->setEnabled(!m_hashConfigs[VIDEO_TAB].isEmpty() || videoEffect->hasDraft());
}

void ExtendedDialog::saveConfig()
{
    assert( currentTab() == AUDIO_TAB || currentTab() == VIDEO_TAB );
    for (auto* focus = focusWidget(); focus && focus != this; focus = focus->parentWidget()) {
        if (auto* field = qobject_cast<QAbstractSpinBox*>(focus)) {
            if (!field->hasAcceptableInput()) { field->setFocus(); return; }
            field->interpretText();
            if (auto* input = field->findChild<QLineEdit*>()) input->setModified(false);
            break;
        }
    }
    if (currentTab() == AUDIO_TAB) refreshAudioConfig();
    else { if (!videoEffect->commitDrafts()) return; refreshVideoConfig(); }
    QHash<QString, QVariant> *hashConfig = &m_hashConfigs[currentTab()];

    for( QHash<QString, QVariant>::iterator i = hashConfig->begin();
         i != hashConfig->end(); ++i )
    {
        QVariant &value = i.value();
        switch( static_cast<QMetaType::Type>(value.typeId()) )
        {
            case QMetaType::QString:
                config_PutPsz( qtu(i.key()), qtu(value.toString()) );
                break;
            case QMetaType::Int:
            case QMetaType::Bool:
            case QMetaType::Long:
            case QMetaType::ULong:
            case QMetaType::UInt:
            case QMetaType::LongLong:
            case QMetaType::ULongLong:
                config_PutInt( qtu(i.key()), value.toLongLong() ) ;
                break;
            case QMetaType::Double:
            case QMetaType::Float:
                config_PutFloat( qtu(i.key()), value.toFloat() ) ;
                break;
            default:
                vlc_assert_unreachable();
        }
    }
    if (config_SaveConfigFile(p_intf) != VLC_SUCCESS) {
        if (currentTab() == AUDIO_TAB) {
            m_audioSaveFailed = true;
            for (auto it = hashConfig->cbegin(); it != hashConfig->cend(); ++it)
                m_savedAudioValues.insert(it.key(), savedEffectValue(it.key(), it.value()));
        }
        if (currentTab() == VIDEO_TAB) {
            m_videoSaveFailed = true;
            for (auto it = hashConfig->cbegin(); it != hashConfig->cend(); ++it)
                m_savedVideoValues.insert(it.key(), savedEffectValue(it.key(), it.value()));
        }
        DialogsProvider::getInstance()->messageDialog(
            qtr("Hakone cannot save these settings.\n\nYour changes stay in this window. Select Save to try again."),
            qtr("Save effects"), QMessageBox::Close, QMessageBox::Close, QMessageBox::Warning, this);
        return;
    }
    hashConfig->clear();
    if (currentTab() == AUDIO_TAB) { m_savedAudioValues.clear(); m_audioSaveFailed = false; }
    else { m_savedVideoValues.clear(); m_videoSaveFailed = false; }
    m_applyButton->setEnabled( false );
}
