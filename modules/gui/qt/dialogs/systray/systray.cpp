/*****************************************************************************
 * Copyright (C) 2024 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * ( at your option ) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#include "systray.hpp"
#include "notification_policy.hpp"

#include <QImageReader>
#include <QStyleHints>

#include "maininterface/mainctx.hpp"
#include "maininterface/compositor.hpp"
#include "menus/redesign_menu_style.hpp"
#include "util/colorizedsvgicon.hpp"
#include "util/color_scheme_model.hpp"
#include "util/interface_translator.hpp"
#include "menus/menus.hpp"
#include "playlist/playlist_controller.hpp"
#include "player/player_controller.hpp"
#include "dialogs/dialogs_provider.hpp"
#include "widgets/native/qvlcframe.hpp"

using namespace vlc::playlist;

VLCSystray::VLCSystray(MainCtx* ctx, QObject* parent)
    : QSystemTrayIcon(parent)
    , m_ctx(ctx)
    , m_intf(ctx->getIntf())
{
    assert(ctx);
    assert(m_intf);

    QIcon iconVLC;
    if( m_ctx->useXmasCone() )
        iconVLC = QIcon::fromTheme( "vlc-xmas", QIcon( ":/logo/vlc128-xmas.png" ) );
    else
        iconVLC = QIcon::fromTheme( "vlc", QIcon( ":/logo/vlc256.png" ) );

    setIcon(iconVLC);
    setToolTip( qtr( "Hakone Media Player" ));

    m_menu = std::make_unique<VLCMenu>( qtr( "Hakone Media Player"), m_intf );
    m_menu->setIcon( iconVLC );
    m_menu->setObjectName(QStringLiteral("redesignTrayMenu"));
    styleRedesignMenus(m_menu.get(), m_ctx);
    createMenu();
    setContextMenu(m_menu.get());
    connect(m_menu.get(), &QMenu::aboutToShow, this, &VLCSystray::update);
    update();
    show();

    connect( this, &QSystemTrayIcon::activated,
            this, &VLCSystray::handleClick );

    /* Connects on nameChanged() */
    connect( m_intf->p_mainPlayerController, &PlayerController::nameChanged,
            this, &VLCSystray::updateTooltipName );
    /* Connect PLAY_STATUS on the systray */
    connect( m_intf->p_mainPlayerController, &PlayerController::playingStateChanged,
            this, &VLCSystray::update );
    connect(m_intf->p_mainPlayerController, &PlayerController::inputChanged, this, &VLCSystray::update);
    connect(m_intf->p_mainPlayerController, &PlayerController::soundMuteChanged, this, &VLCSystray::update);
    connect(m_intf->p_mainPlayerController, &PlayerController::recordingChanged, this, &VLCSystray::update);
    connect(m_intf->p_mainPlaylistController, &PlaylistController::isEmptyChanged, this, &VLCSystray::update);
    connect(m_ctx, &MainCtx::intfScaleFactorChanged, this, &VLCSystray::update);
    connect(m_ctx, &MainCtx::redesignAccentChanged, this, &VLCSystray::update);
    connect(m_ctx->getColorScheme(), &ColorSchemeModel::currentChanged, this, &VLCSystray::update);
    connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, &VLCSystray::update);
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, &VLCSystray::update);
}

VLCSystray::~VLCSystray()
{}

bool VLCSystray::isAvailableAndVisible() const
{
    return isVisible() && isSystemTrayAvailable();
}

/**
 * Updates the VLCSystray Icon's menu and toggle the main interface
 */
void VLCSystray::toggleUpdateMenu()
{
    if (!isAvailableAndVisible() && m_ctx->interfaceVisibility() != QWindow::Hidden)
        emit m_ctx->requestInterfaceMinimized();
    else
        emit m_ctx->toggleWindowVisibility(true);
    update();
}

/* First Item of the systray menu */
void VLCSystray::showUpdateMenu()
{
    m_ctx->setInterfaceVisibible(true, true);
    update();
}

/* First Item of the systray menu */
void VLCSystray::hideUpdateMenu()
{
    if (isAvailableAndVisible())
        emit m_ctx->setInterfaceVisibible(false);
    else
        emit m_ctx->requestInterfaceMinimized();
    update();
}

/* Click on systray Icon */
void VLCSystray::handleClick(
    QSystemTrayIcon::ActivationReason reason )
{
    switch( reason )
    {
    case QSystemTrayIcon::Trigger:
    case QSystemTrayIcon::DoubleClick:
        toggleUpdateMenu();
        break;
    case QSystemTrayIcon::MiddleClick:
        if (PlaylistController* const playlistController = m_intf->p_mainPlaylistController)
            playlistController->togglePlayPause();
        break;
    default:
        break;
    }
}

/**
 * Updates the name of the systray Icon tooltip.
 * Doesn't check if the systray exists, check before you call it.
 **/
void VLCSystray::updateTooltipName( const QString& name )
{
    if( name.isEmpty() )
    {
        setToolTip( qtr( "Hakone Media Player" ) );
    }
    else
    {
        setToolTip( name );
        const auto visibility = m_ctx->interfaceVisibility();
        const auto notificationSetting = var_InheritInteger(m_intf, "qt-notification");
        if (vlc::systray::shouldNotify(notificationSetting, visibility))
        {
            const auto showMessageTemplate = [this, &name](const auto &icon) {
                showMessage(qtr("Hakone Media Player"), name, icon, var_InheritInteger(m_intf, "qt-notification-timeout"));
            };

            assert(m_intf);
            assert(m_intf->p_mainPlayerController);
            const QUrl& art = m_intf->p_mainPlayerController->getArtwork();
            if (art.isValid())
            {
                // If there is artwork, use it but only when it is (almost) square:
                const QString& fileName = art.toLocalFile();
                const QImageReader imageReader(fileName);
                if (Q_LIKELY(imageReader.canRead()))
                {
                    const QSize& size = imageReader.size(); // this does not read the whole image
                    if (Q_LIKELY(!size.isEmpty()))
                    {
                        const double ratio = static_cast<double>(size.width()) / size.height();
                        if (Q_LIKELY(std::abs(ratio - 1.0) < 0.2))
                        {
                            showMessageTemplate(QIcon(fileName));
                            return;
                        }
                    }
                }
            }

            showMessageTemplate( QSystemTrayIcon::NoIcon );
        }
    }
    update();
}

void VLCSystray::createMenu()
{
#ifndef Q_OS_MAC
    auto *visibility = m_menu->addAction(qtr("&Hide VLC"), this, [this] {
        if (m_ctx->interfaceVisibility() == QWindow::Hidden) showUpdateMenu();
        else hideUpdateMenu();
    });
    visibility->setObjectName(QStringLiteral("trayVisibility"));
    m_menu->addSeparator();
#endif
    VLCMenuBar::PopupMenuPlaylistEntries(m_menu.get(), m_intf);
    VLCMenuBar::PopupMenuControlEntries(m_menu.get(), m_intf, false);
    VLCMenuBar::VolumeEntries(m_intf, m_menu.get());
    m_menu->addSeparator();
    m_menu->addAction(qtr("&Open Media"), THEDP, &DialogsProvider::openFileDialog)->setObjectName(QStringLiteral("trayOpen"));
    m_menu->addAction(qtr("&Quit"), THEDP, &DialogsProvider::quit)->setObjectName(QStringLiteral("trayQuit"));
}

void VLCSystray::update()
{
    // Retain actions and submenus while playback changes. Rebuilding here used
    // to destroy an open submenu and the keyboard's active action.
    auto *window = m_intf->p_compositor ? m_intf->p_compositor->interfaceMainWindow() : nullptr;
    if (window != m_window)
    {
        if (m_window) disconnect(m_window, nullptr, this, nullptr);
        m_window = window;
        if (window) connect(window, &QWindow::visibilityChanged, this, [this] { update(); }, Qt::QueuedConnection);
    }
    styleRedesignMenus(m_menu.get(), m_ctx);
    const auto set = [this](const char *name, const QString& text, [[maybe_unused]] const QString& icon = {}) {
        if (auto *action = m_menu->findChild<QAction*>(QLatin1String(name)))
        {
            if (action->text() != text) action->setText(text);
#ifndef __APPLE__
            if (!icon.isEmpty())
            {
                const auto foreground = m_menu->palette().text().color();
                const auto accent = m_ctx->redesignAccent();
                const auto mutedAccent = accent == QColor(QStringLiteral("#d37918"))
                    ? QColor(QStringLiteral("#ab6314")) : accent.darker(123);
                action->setIcon(icon.endsWith(QLatin1String(".svg"))
                    ? QIcon(ColorizedSvgIcon(icon, foreground, {}, accent,
                        {{QStringLiteral("#8F8F8F"), foreground.name()},
                         {QStringLiteral("#747474"), foreground.name()},
                         {QStringLiteral("#f0f"), foreground.name()},
                         {QStringLiteral("#D37918"), accent.name()},
                         {QStringLiteral("#ff950d"), accent.name()},
                         {QStringLiteral("fill=\"red\""), QStringLiteral("fill=\"%1\"").arg(accent.name())},
                         {QStringLiteral("#AB6314"), mutedAccent.name()}}))
                    : QIcon(icon));
            }
#endif
            return action;
        }
        return static_cast<QAction*>(nullptr);
    };
    auto *player = m_intf->p_mainPlayerController;
    const bool hasInput = player->hasInput();
    const bool started = player->isStarted();
    const bool playing = hasInput && player->getPlayingState() == PlayerController::PLAYING_STATE_PLAYING;
#ifndef Q_OS_MAC
    set("trayVisibility", m_ctx->interfaceVisibility() == QWindow::Hidden ? qtr("&Show VLC") : qtr("&Hide VLC"), QStringLiteral(":/logo/vlc16.png"));
#endif
    set("vlcMenuPlayPause", playing ? qtr("Pause") : qtr("&Play"), playing
        ? QStringLiteral(":/menu/ic_pause_filled.svg") : QStringLiteral(":/menu/redesign/play.svg"));
    if (auto *stop = set("vlcMenuStop", qtr("&Stop"), QStringLiteral(":/menu/redesign/stop.svg"))) stop->setEnabled(started);
    set("vlcMenuPrevious", qtr("Pre&vious"), QStringLiteral(":/menu/redesign/previous.svg"));
    set("vlcMenuNext", qtr("Ne&xt"), QStringLiteral(":/menu/redesign/next.svg"));
    if (auto *record = set("vlcMenuRecord", player->isRecording() ? qtr("Stop recording") : qtr("Record"), QStringLiteral(":/menu/redesign/record.svg"))) record->setEnabled(started);
    if (auto *speed = m_menu->findChild<QMenu*>(QStringLiteral("vlcMenuSpeed"))) speed->setTitle(qtr("Sp&eed"));
    set("vlcMenuSpeedFasterFine", qtr("Faster (fine)"), QStringLiteral(":/menu/ic_fluent_fast_forward.svg"));
    set("vlcMenuSpeedNormal", qtr("N&ormal Speed"));
    set("vlcMenuSpeedSlowerFine", qtr("Slower (fine)"), QStringLiteral(":/menu/ic_fluent_rewind.svg"));
    set("vlcMenuVolumeUp", qtr("&Increase Volume"), QStringLiteral(":/menu/volume-high.svg"));
    set("vlcMenuVolumeDown", qtr("&Decrease Volume"), QStringLiteral(":/menu/volume-low.svg"));
    set("vlcMenuMute", player->isMuted() ? qtr("&Unmute") : qtr("&Mute"), player->isMuted()
        ? QStringLiteral(":/menu/volume-high.svg") : QStringLiteral(":/menu/volume-muted.svg"));
    set("trayOpen", qtr("&Open Media"), QStringLiteral(":/menu/redesign/open.svg"));
    set("trayQuit", qtr("&Quit"), QStringLiteral(":/menu/redesign/quit.svg"));
}
