// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "redesign_widget_theme.hpp"
#include "addons/addon_theme_service.hpp"
#include "maininterface/mainctx.hpp"
#include "color_scheme_model.hpp"
#include "colorizedsvgicon.hpp"
#include <QApplication>
#include <algorithm>
#include <QStyleHints>
#include <QWidget>
#include <QPointer>
#include <QMessageBox>
#include <QMenu>
#include <QAbstractButton>
#include <QToolTip>
#include <QFont>
#include <QWizard>

void applyRedesignWidgetTheme(QWidget* widget, MainCtx* ctx)
{
    // Menus use RedesignMenuStyle; a dialog stylesheet overrides its item
    // painting (including hover states and submenu chevrons).
    if (!ctx || qobject_cast<QMenu*>(widget) || widget->property("redesignDialog").toBool()) return;
    widget->setProperty("redesignDialog", true);
    widget->setAttribute(Qt::WA_StyledBackground);
    const auto box = qobject_cast<QMessageBox*>(widget);
    const auto messageIcon = box ? box->icon() : QMessageBox::NoIcon;
    const auto applyTheme = [widget, messageIcon, ctx = QPointer<MainCtx>(ctx)] {
        if (!ctx) return;
        const auto scheme = ctx->getColorScheme()->currentScheme();
        const bool light = scheme == ColorSchemeModel::Day ||
            (scheme == ColorSchemeModel::System && qApp->styleHints()->colorScheme() == Qt::ColorScheme::Light);
        const auto service = vlc::addons::AddonThemeService::instance();
        const auto snapshot = service ? service->tokens(light, {}, false, !ctx->backdropBlurRequested())
            : vlc::addons::AddonThemeService::resolve({}, light, ctx->redesignAccent().name());
        const auto semantic = snapshot.value(QStringLiteral("colors")).toMap();
        const auto color = [&semantic](const char* name) { return semantic.value(QString::fromLatin1(name)).toString(); };
        const auto background = color("surface.base"), field = color("surface.field");
        const auto foreground = color("text.secondary"), bright = color("text.primary");
        const auto border = color("border.default"), hover = color("surface.hover");
        const auto accent = color("accent.primary"), focus = color("focus.ring");
        const auto onAccent = snapshot.value(QStringLiteral("customized")).toBool() ? color("text.onAccent") : QStringLiteral("#161616");
        const auto dp = [ctx](int value) { return qRound(value * ctx->getIntfScaleFactor()); };
        // Stylesheet heights exclude padding and the two one-pixel borders.
        const int compactButtonContentHeight = std::max(0, dp(32) - 2 * dp(6) - 2);
        QPalette colors = widget->palette();
        colors.setColor(QPalette::Window, QColor(background));
        colors.setColor(QPalette::WindowText, QColor(foreground));
        colors.setColor(QPalette::Base, QColor(field));
        colors.setColor(QPalette::AlternateBase, QColor(background));
        colors.setColor(QPalette::Text, QColor(bright));
        colors.setColor(QPalette::PlaceholderText, QColor(foreground));
        colors.setColor(QPalette::Button, QColor(field));
        colors.setColor(QPalette::ButtonText, QColor(foreground));
        colors.setColor(QPalette::Highlight, QColor(accent));
        colors.setColor(QPalette::HighlightedText, QColor(onAccent));
        const QColor linkColor = light && !snapshot.value(QStringLiteral("highContrast")).toBool() ? QColor(accent).darker(200) : QColor(accent);
        colors.setColor(QPalette::Link, linkColor);
        colors.setColor(QPalette::LinkVisited, linkColor);
        colors.setColor(QPalette::Light, QColor(border));
        colors.setColor(QPalette::Midlight, QColor(border));
        colors.setColor(QPalette::Mid, QColor(border));
        colors.setColor(QPalette::Dark, QColor(border));
        colors.setColor(QPalette::Shadow, QColor(border));
        colors.setColor(QPalette::ToolTipBase, QColor(field));
        colors.setColor(QPalette::ToolTipText, QColor(bright));
        widget->setPalette(colors);
        QFont tooltipFont(QStringLiteral("Inter"));
        tooltipFont.setPixelSize(dp(14));
        QToolTip::setFont(tooltipFont);
        QToolTip::setPalette(colors);
        if (auto message = qobject_cast<QMessageBox*>(widget)) {
            for (auto button : message->buttons()) button->setIcon({});
            if (messageIcon == QMessageBox::Question || messageIcon == QMessageBox::Information) {
                const auto name = messageIcon == QMessageBox::Question ? QStringLiteral("help") : QStringLiteral("info");
                const auto icon = ColorizedSvgIcon(QStringLiteral(":/menu/%1.svg").arg(name), {}, {}, {},
                    {{QStringLiteral("#ff950d"), accent}, {QStringLiteral("#fafafa"), background}});
                message->setIconPixmap(icon.pixmap(QSize(dp(32), dp(32)), widget->devicePixelRatioF()));
            }
        }
        auto stylesheet = QStringLiteral(
            "QWidget { font-family: Inter; font-size: %1px; color: %2; }"
            "QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QAbstractSpinBox { placeholder-text-color: %2; }"
            "QToolTip { background: %4; color: %5; border: 1px solid %6; border-radius: 4px; padding: 6px; }"
            "QWidget[redesignDialog=\"true\"], QDialog, QTabWidget::pane { background: %3; border: none; }"
            "QWidget#aboutWidget QLabel#title { color: %5; font-size: %13px; font-weight: 600; }"
            "QWidget#aboutWidget QTabBar::tab { padding: %14px %15px; }"
            "QWidget#aboutWidget QTextEdit, QWidget#aboutWidget QPlainTextEdit { padding: %15px; border-radius: %14px; }"
            "QWidget#aboutWidget QToolButton { background: transparent; border: 1px solid transparent; padding: %21px; border-radius: %16px; }"
            "QWidget#aboutWidget QToolButton:hover, QWidget#aboutWidget QToolButton:pressed, QWidget#aboutWidget QToolButton:checked { background: %8; color: %5; }"
            "QWidget#aboutWidget QToolButton#checkUpdatesButton[redesignCompact=\"false\"] { border-color: %6; }"
            "QWidget#aboutWidget QToolButton:focus { border-color: %2; }"
            "QWidget#aboutWidget QToolButton#checkUpdatesButton:focus { border-color: %2; }"
            "QWidget#aboutWidget QPlainTextEdit#licensePage { font-family: 'DM Mono'; }"
            "QWidget#redesignNativeMessage QTextEdit#messageBody { padding: 0; border-radius: %14px; }"
            "QWidget#redesignNativeMessage QPushButton, QWidget#redesignTextInput QPushButton, QWidget#redesignMediaList QPushButton, QWidget#redesignOpenUrl QPushButton { min-height: %17px; max-height: %17px; padding: %16px %15px; border-radius: %16px; }"
            "QLabel#metadataCover { background: %4; color: %2; border-radius: %16px; }"
            "QLabel[redesignHeading=\"true\"] { color: %5; font-size: %13px; }"
            "QWidget#mediaStatistics QFrame[redesignCard=\"true\"], QWidget#redesignConvert QFrame[redesignCard=\"true\"], QWidget#syncControls QFrame[redesignCard=\"true\"], QWidget#ExtVideoWidget QFrame[redesignCard=\"true\"], QWidget#audioFilterControls QFrame[redesignCard=\"true\"], QWidget#cameraControls QFrame[redesignCard=\"true\"] { background: %4; border: 1px solid %6; border-radius: %16px; }"
            "QLabel[redesignSectionHeading=\"true\"] { color: %5; font-size: %19px; font-weight: 600; }"
            "QLabel[redesignValue=\"true\"] { color: %5; }"
            "QWidget#redesignMediaInfo QTabBar::tab { padding: %14px %15px; }"
            "QTreeWidget#codecInformationTree::item { padding: %16px; }"
            "QWidget#cameraControls QSlider::groove:horizontal, QWidget#audioFilterControls QSlider::groove:horizontal, QWidget#ExtVideoWidget QSlider::groove:horizontal { background: %6; height: %21px; border-radius: %21px; }"
            "QWidget#cameraControls QSlider::sub-page:horizontal, QWidget#audioFilterControls QSlider::sub-page:horizontal, QWidget#ExtVideoWidget QSlider::sub-page:horizontal { background: %7; border-radius: %21px; }"
            "QWidget#cameraControls QSlider::handle:horizontal, QWidget#audioFilterControls QSlider::handle:horizontal, QWidget#ExtVideoWidget QSlider::handle:horizontal { background: %2; border: 1px solid %6; width: %15px; margin: -%16px 0; border-radius: %16px; }"
            "QWidget#cameraControls QSlider::handle:horizontal:hover, QWidget#audioFilterControls QSlider::handle:horizontal:hover, QWidget#ExtVideoWidget QSlider::handle:horizontal:hover { background: %5; }"
            "QWidget#cameraControls QSlider::handle:horizontal:pressed, QWidget#audioFilterControls QSlider::handle:horizontal:pressed, QWidget#ExtVideoWidget QSlider::handle:horizontal:pressed { background: %7; border-color: %7; }"
            "QWidget#cameraControls QSlider::handle:horizontal:focus, QWidget#audioFilterControls QSlider::handle:horizontal:focus, QWidget#ExtVideoWidget QSlider::handle:horizontal:focus { border-color: %7; }"
            "QWidget#audioFilterControls QSlider::groove:vertical { background: %6; width: %21px; border-radius: %21px; }"
            "QWidget#audioFilterControls QSlider::add-page:vertical { background: %7; border-radius: %21px; }"
            "QWidget#audioFilterControls QSlider::handle:vertical { background: %2; border: 1px solid %6; height: %15px; margin: 0 -%16px; border-radius: %16px; }"
            "QWidget#audioFilterControls QSlider::handle:vertical:hover { background: %5; }"
            "QWidget#audioFilterControls QSlider::handle:vertical:focus { border-color: %7; }"
            "QWidget#audioFilterControls QSlider::handle:vertical:pressed { background: %7; }"
            "QWidget#audioFilterControls QSlider::add-page:vertical:disabled, QWidget#audioFilterControls QSlider::handle:vertical:disabled { background: %6; border-color: %6; }"
            "QWidget#audioFilterControls QPushButton[flat=\"true\"] { padding: %16px 0; border: 1px solid transparent; background: transparent; }"
            "QWidget#audioFilterControls QPushButton[flat=\"true\"]:hover { background: %8; color: %5; }"
            "QWidget#audioFilterControls QPushButton[flat=\"true\"]:focus { border-color: %2; }"
            "QWidget#ExtVideoWidget QToolButton { min-height: %17px; max-height: %17px; padding: %16px %15px; border-radius: %16px; border: 1px solid transparent; background: %4; color: %2; }"
            "QWidget#ExtVideoWidget QToolButton:hover { background: %8; color: %5; }"
            "QWidget#ExtVideoWidget QToolButton:focus { border-color: %2; }"
            "QWidget#ExtVideoWidget QGroupBox[videoEffectBody=\"true\"] { border: none; margin: 0; padding: 0; }"
            "QWidget#syncControls QLabel:disabled, QWidget#syncControls QDoubleSpinBox:disabled, QWidget#ExtVideoWidget QLabel:disabled, QWidget#ExtVideoWidget QLineEdit:disabled, QWidget#ExtVideoWidget QDoubleSpinBox:disabled, QWidget#ExtVideoWidget QPushButton:disabled, QWidget#ExtVideoWidget QToolButton:disabled { color: %2; }"
            "QWidget#audioFilterControls QLabel:disabled, QWidget#audioFilterControls QDoubleSpinBox:disabled, QWidget#audioFilterControls QPushButton:disabled { color: %2; }"
            "QWidget#audioFilterControls QSlider::sub-page:horizontal:disabled, QWidget#ExtVideoWidget QSlider::sub-page:horizontal:disabled { background: %6; }"
            "QWidget#audioFilterControls QSlider::handle:horizontal:disabled, QWidget#ExtVideoWidget QSlider::handle:horizontal:disabled { background: %6; border-color: %6; }"
            "QWidget#cameraControls QCheckBox::indicator, QWidget#audioFilterControls QCheckBox::indicator { width: %1px; height: %1px; }"
            "QWidget#syncControls QDoubleSpinBox, QWidget#syncControls QComboBox, QWidget#ExtVideoWidget QDoubleSpinBox, QWidget#ExtVideoWidget QSpinBox, QWidget#ExtVideoWidget QLineEdit, QWidget#audioFilterControls QDoubleSpinBox, QWidget#cameraControls QLineEdit, QWidget#cameraControls QSpinBox, QWidget#codecInformation QLineEdit, QWidget#extraMetadata QLineEdit, QWidget#redesignPodcasts QLineEdit, QWidget#redesignConvert QLineEdit, QWidget#redesignStream QLineEdit, QWidget#redesignMessages QLineEdit, QWidget#redesignHelp QLineEdit, QWidget#redesignProfileEditor QLineEdit, QWidget#redesignOpenMedia QLineEdit, QWidget#redesignCaptureOptions QLineEdit, QWidget#redesignPlugins QLineEdit, QWidget#redesignExtension QLineEdit, QWidget#redesignVLM QLineEdit { min-height: %17px; max-height: %17px; padding: %16px %14px; }"
            "QWidget#redesignMediaInfo QLineEdit, QWidget#redesignMediaInfo QTextEdit, QWidget#redesignMediaInfo QPushButton { border-radius: %16px; }"
            "QWidget#redesignMediaInfo QLineEdit:read-only { background: %3; color: %2; }"
            "QWidget#syncControls QPushButton, QWidget#ExtVideoWidget QPushButton, QWidget#audioFilterControls QPushButton, QWidget#cameraControls QPushButton, QWidget#redesignMediaInfo QPushButton, QWidget#redesignFingerprint QPushButton, QWidget#redesignProgramGuide QPushButton, QWidget#redesignPodcasts QPushButton, QWidget#redesignConvert QPushButton, QWidget#redesignStream QPushButton, QWidget#redesignBookmarks QPushButton, QWidget#redesignMessages QPushButton, QWidget#redesignHelp QPushButton, QWidget#aboutWidget QPushButton, QWidget#redesignProfileEditor QPushButton, QWidget#redesignOpenMedia QPushButton, QWidget#redesignCaptureOptions QPushButton, QWidget#redesignPlugins QPushButton, QWidget#redesignPluginInfo QPushButton, QWidget#redesignExtension QPushButton, QWidget#redesignVLM QPushButton { min-height: %17px; max-height: %17px; padding: %16px %15px; border-radius: %16px; }"
            "QWidget#redesignPlugins QProgressBar { background: %4; border: none; }"
            "QWidget#redesignPlugins QProgressBar::chunk { background: %7; }"
            "QWidget#redesignVLM QCheckBox::indicator, QWidget#redesignVLM QGroupBox::indicator { width: %1px; height: %1px; }"
            "QWidget#redesignVLM QGroupBox { border: 1px solid %6; border-radius: %16px; margin-top: %15px; padding: %14px; }"
            "QWidget#redesignVLM QGroupBox::title { subcontrol-origin: margin; left: %14px; padding: 0 %16px; }"
            "QWidget#redesignExtension QCheckBox::indicator { width: %1px; height: %1px; }"
            "QWidget#redesignPlugins QCheckBox::indicator { width: %1px; height: %1px; }"
            "QWidget#redesignProfileEditor QCheckBox::indicator, QWidget#redesignProfileEditor QGroupBox::indicator, QWidget#redesignProfileEditor QRadioButton::indicator { width: %1px; height: %1px; }"
            "QWidget#redesignOpenMedia QCheckBox::indicator, QWidget#redesignOpenMedia QRadioButton::indicator, QWidget#redesignCaptureOptions QCheckBox::indicator { width: %1px; height: %1px; }"
            "QWidget#redesignOpenMedia QRadioButton::indicator { border-radius: %20px; }"
            "QWidget#redesignOpenMedia QGroupBox { border: 1px solid %6; border-radius: %16px; margin-top: %15px; padding: %14px; }"
            "QWidget#redesignOpenMedia QGroupBox::title { subcontrol-origin: margin; left: %14px; padding: 0 %16px; }"
            "QWidget#redesignOpenMedia QFrame[frameShape=\"HLine\"] { border: none; background: %6; min-height: 1px; max-height: 1px; }"
            "QWidget#redesignOpenMedia QToolButton#playButton { background: %7; color: #161616; min-height: %17px; padding: %16px %11px %16px %15px; border-radius: %16px; }"
            "QWidget#redesignOpenMedia QToolButton#playButton:disabled { background: %8; color: %2; }"
            "QWidget#redesignOpenMedia QToolButton#playButton::menu-button { subcontrol-origin: border; subcontrol-position: top right; width: %11px; border-left: 1px solid %6; background: %7; }"
            "QWidget#redesignOpenMedia QToolButton#playButton::menu-arrow { image: url(:/icons/chevron-down-light.svg); width: %1px; height: %1px; }"
            "QWidget#redesignOpenMedia QToolButton#playButton::menu-button:disabled { background: %8; }"
            "QWidget#redesignOpenMedia QToolButton#playButton::menu-arrow:disabled { image: url(:/icons/chevron-down-light-disabled.svg); }"
            "QWidget#redesignOpenMedia QToolButton#playButton:focus, QWidget#redesignOpenMedia QPushButton[redesignPrimary=\"true\"]:focus, QWidget#redesignCaptureOptions QPushButton[redesignPrimary=\"true\"]:focus { border-color: %5; }"
            "QWidget#redesignProfileEditor QListWidget::item { padding: %16px; }"
            "QWidget#redesignProfileEditor QGroupBox { border: 1px solid %6; border-radius: %16px; margin-top: %15px; padding: %14px; }"
            "QWidget#redesignProfileEditor QGroupBox::title { subcontrol-origin: margin; left: %14px; padding: 0 %16px; }"
            "QWidget#redesignStream QFrame[frameShape=\"HLine\"] { border: none; background: %6; min-height: 1px; max-height: 1px; }"
            "QWidget#redesignProgramGuide QPushButton#guideZoomIn, QWidget#redesignProgramGuide QPushButton#guideZoomOut { padding: %16px 0; }"
            "QWidget#redesignPodcasts QListWidget::item { padding: %14px; }"
            "QWidget#redesignFingerprint QListWidget::item { padding: %14px; border-radius: %16px; }"
            "QWidget#redesignFingerprint QListWidget::item:selected { background: %7; color: #161616; }"
            "QWidget#redesignFingerprint QProgressBar { background: %4; border: none; border-radius: %18px; }"
            "QWidget#redesignFingerprint QProgressBar::chunk { background: %7; border-radius: %18px; }"
            "QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QAbstractSpinBox, QAbstractItemView { background: %4; alternate-background-color: %3; color: %5; border: 1px solid %6; border-radius: 4px; padding: 5px; selection-background-color: %7; selection-color: #161616; }"
            "QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QAbstractSpinBox:focus { border-color: %7; }"
            "QAbstractSpinBox { min-height: %9px; }"
            "QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { background: transparent; border: none; }"
            "QAbstractSpinBox QLineEdit { background: transparent; border: none; padding: 0; }"
            "QComboBox::drop-down { background: transparent; border: none; width: %11px; }"
            "QComboBox::down-arrow { image: url(:/icons/chevron-down-%12.svg); width: %10px; height: %10px; border: none; }"
            "QComboBox::down-arrow:disabled { image: url(:/icons/chevron-down-%12-disabled.svg); }"
            "QCheckBox::indicator, QGroupBox::indicator, QRadioButton::indicator { width: 14px; height: 14px; background: %4; border: 1px solid %6; }"
            "QCheckBox::indicator, QGroupBox::indicator { border-radius: 3px; }"
            "QRadioButton::indicator { border-radius: 7px; }"
            "QCheckBox::indicator:checked, QGroupBox::indicator:checked { background: %7; border-color: %7; image: url(:/icons/check.svg); }"
            "QRadioButton::indicator:checked { background: %7; border-color: %7; }"
            "QCheckBox::indicator:hover, QGroupBox::indicator:hover, QRadioButton::indicator:hover { border-color: %7; }"
            "QCheckBox::indicator:disabled, QGroupBox::indicator:disabled, QRadioButton::indicator:disabled { background: %8; border-color: %6; }"
            "QCheckBox::indicator:checked:disabled, QGroupBox::indicator:checked:disabled, QRadioButton::indicator:checked:disabled { background: %2; border-color: %2; }"
            "QPushButton { background: %4; border: 1px solid transparent; border-radius: 4px; padding: 6px 16px; min-height: 18px; }"
            "QToolButton { background: transparent; border: 1px solid transparent; border-radius: 4px; padding: 4px; }"
            "QToolButton:hover { background: %8; }"
            "QToolButton:focus { border-color: %7; }"
            "QToolButton::menu-button { background: %4; border: none; }"
            "QToolButton::menu-button:hover { background: %8; }"
            "QPushButton:hover { background: %8; }"
            "QPushButton:focus { border-color: %7; }"
            "QPushButton:pressed { background: %7; color: #161616; }"
            "QPushButton#saveMetadataButton, QPushButton[redesignPrimary=\"true\"] { background: %7; color: #161616; }"
            "QPushButton#saveMetadataButton:hover { border-color: %5; }"
            "QPushButton#saveMetadataButton:disabled, QPushButton[redesignPrimary=\"true\"]:disabled { background: %8; color: %2; }"
            "QWidget:disabled { color: %6; }"
            "QLineEdit:disabled, QTextEdit:disabled, QPlainTextEdit:disabled, QComboBox:disabled, QAbstractSpinBox:disabled { background: %3; color: %2; border-color: %6; }"
            "QLabel:disabled { color: %2; }"
            "QTabBar::tab { background: %4; padding: 10px 16px; border-bottom: 2px solid transparent; }"
            "QTabBar::tab:selected { color: %5; border-bottom-color: %7; }"
            "QTabBar::tab:hover { background: %8; }"
            "QHeaderView { background: %8; border: none; padding: 0; border-radius: 0; }"
            "QHeaderView::section { background: %8; color: %5; border: none; padding: 6px; }"
            "QTableCornerButton::section { background: %8; border: none; }"
            "QGraphicsView { background: %4; border: none; }"
            "QScrollBar:vertical { background: %3; width: 6px; margin: 0; }"
            "QScrollBar:horizontal { background: %3; height: 6px; margin: 0; }"
            "QScrollBar::handle { background: %6; border-radius: 3px; }"
            "QScrollBar::handle:vertical { min-height: 24px; }"
            "QScrollBar::handle:horizontal { min-width: 24px; }"
            "QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }"
            "QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }"
            "QScrollArea, QScrollArea > QWidget > QWidget { background: %3; border: none; }");
        stylesheet.replace(QStringLiteral("border-radius: %16px"), QStringLiteral("border-radius: %22px"));
        stylesheet.replace(QStringLiteral("border-radius: 4px"), QStringLiteral("border-radius: %22px"));
        stylesheet.replace(QStringLiteral(":focus { border-color: %7; }"), QStringLiteral(":focus { border-color: %23; }"));
        stylesheet.replace(QStringLiteral(":focus { border-color: %2; }"), QStringLiteral(":focus { border-color: %23; }"));
        stylesheet.replace(QStringLiteral(":focus { border-color: %5; }"), QStringLiteral(":focus { border-color: %23; }"));
        stylesheet.replace(QStringLiteral("color: #161616"), QStringLiteral("color: %24"));
        widget->setStyleSheet(stylesheet
            .arg(dp(14)).arg(foreground, background, field, bright, border, accent, hover)
            .arg(dp(18)).arg(dp(24)).arg(dp(32)).arg(light ? "light" : "dark").arg(dp(20)).arg(dp(8)).arg(dp(16)).arg(dp(6)).arg(compactButtonContentHeight).arg(dp(2)).arg(dp(16)).arg((dp(14) + 1) / 2).arg(dp(4))
            .arg(qRound(snapshot.value(QStringLiteral("shape")).toMap().value(QStringLiteral("controlRadius")).toDouble() * ctx->getIntfScaleFactor())).arg(focus, onAccent));
        // Reapply explicit palette roles after the stylesheet repolishes the window.
        widget->setPalette(colors);
        // Wizard headers and rulers paint with their palette rather than QSS.
        if (widget->objectName() == QStringLiteral("redesignStream"))
            for (auto *child : widget->findChildren<QWidget *>())
                if (child->metaObject() == &QWidget::staticMetaObject) child->setPalette(colors);
    };
    applyTheme();
    if (auto *wizard = qobject_cast<QWizard *>(widget))
        QObject::connect(wizard, &QWizard::currentIdChanged, widget, applyTheme);
    if (ctx) {
        QObject::connect(vlc::addons::AddonThemeService::notifications(), &vlc::addons::AddonThemeNotifications::changed, widget, applyTheme);
        QObject::connect(ctx, &MainCtx::redesignAccentChanged, widget, applyTheme);
        QObject::connect(ctx, &MainCtx::intfScaleFactorChanged, widget, applyTheme);
        QObject::connect(ctx->getColorScheme(), &ColorSchemeModel::currentChanged, widget, applyTheme);
        QObject::connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, widget, applyTheme);
    }
}
