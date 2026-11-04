/*****************************************************************************
 * help.cpp : Help and About dialogs
 ****************************************************************************
 * Copyright (C) 2007 the VideoLAN team
 *
 * Authors: Jean-Baptiste Kempf <jb (at) videolan.org>
 *          Rémi Duraffort <ivoire (at) via.ecp.fr>
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
#include "help.hpp"
#include "aboutmodel.hpp"
#include "maininterface/mainctx.hpp"
#include "util/color_scheme_model.hpp"
#include "util/qt_dirs.hpp"
#include "util/interface_translator.hpp"
#include "util/colorizedsvgicon.hpp"


#include <vlc_intf_strings.h>

#ifdef UPDATE_CHECK
# include "util/github_release_service.hpp"
# include <QDesktopServices>
#endif

#include <QTextBrowser>
#include <QString>
#include <QDialogButtonBox>
#include <QEvent>
#include <QPushButton>
#include <QApplication>
#include <QStyleHints>
#include <QStyle>
#include <QScrollBar>
#include <QLineEdit>
#include <QTextBlock>
#include <QClipboard>
#include <QKeyEvent>
#include <QToolButton>
#include <QTimer>
#include <QScopedValueRollback>
#include <QScreen>

#include <cassert>
#include <algorithm>

namespace {
int helpDp(qt_intf_t *intf, int value)
{
    return qRound(value * (intf->p_mi ? intf->p_mi->getIntfScaleFactor() : 1));
}

void colorBrowserLinks(QTextBrowser *browser, const QColor &linkColor)
{
    // Change only the anchor format; keep selections and the reading position.
    QList<QTextCursor> anchors;
    for (auto block = browser->document()->firstBlock(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it)
        {
            const auto fragment = it.fragment();
            if (!fragment.isValid() || !fragment.charFormat().isAnchor()) continue;
            QTextCursor cursor(browser->document());
            cursor.setPosition(fragment.position());
            cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
            anchors.append(cursor);
        }
    QTextCharFormat color;
    color.setForeground(linkColor);
    for (auto &cursor : anchors) cursor.mergeCharFormat(color);
}

bool activateHelpButton(QKeyEvent *event, QWidget *focused)
{
    if (event->key() != Qt::Key_Return && event->key() != Qt::Key_Enter) return false;
    if (auto *button = qobject_cast<QAbstractButton*>(focused)) button->click();
    event->accept();
    return true;
}
}

HelpDialog::HelpDialog(qt_intf_t *intf) : QVLCFrame(intf)
{
    setObjectName(QStringLiteral("redesignHelp"));
    setWindowRole("vlc-help");
    auto *root = new QVBoxLayout(this);
    auto *heading = new QLabel(this);
    heading->setProperty("redesignHeading", true);
    root->addWidget(heading);
    auto *findRow = new QHBoxLayout;
    auto *findLabel = new QLabel(this);
    m_find = new QLineEdit(this);
    m_find->setObjectName(QStringLiteral("helpFind"));
    m_find->setClearButtonEnabled(true);
    findLabel->setBuddy(m_find);
    m_previous = new QPushButton(this);
    m_previous->setObjectName(QStringLiteral("helpPrevious"));
    m_next = new QPushButton(this);
    m_next->setObjectName(QStringLiteral("helpNext"));
    findRow->addWidget(findLabel);
    findRow->addWidget(m_find, 1);
    findRow->addWidget(m_previous);
    findRow->addWidget(m_next);
    root->addLayout(findRow);
    m_browser = new QTextBrowser(this);
    m_browser->setObjectName(QStringLiteral("helpBrowser"));
    m_browser->setOpenExternalLinks(true);
    m_browser->setTextInteractionFlags(Qt::TextBrowserInteraction | Qt::TextSelectableByKeyboard);
    m_browser->setUndoRedoEnabled(false);
    m_browser->setTabChangesFocus(true);
    root->addWidget(m_browser, 1);
    auto *footer = new QHBoxLayout;
    m_findInfo = new QLabel(this);
    m_findInfo->setObjectName(QStringLiteral("helpFindInfo"));
    m_findInfo->setWordWrap(true);
    footer->addWidget(m_findInfo, 1);
    auto *closeButton = new QPushButton(this);
    closeButton->setObjectName(QStringLiteral("helpClose"));
    footer->addWidget(closeButton);
    root->addLayout(footer);
    for (auto *button : {m_previous, m_next, closeButton}) button->setAutoDefault(false);
    connect(closeButton, &QPushButton::clicked, this, &HelpDialog::close);
    connect(m_find, &QLineEdit::textChanged, this, &HelpDialog::updateSearch);
    connect(m_previous, &QPushButton::clicked, this, [this] { showMatch(m_currentMatch - 1); });
    connect(m_next, &QPushButton::clicked, this, [this] { showMatch(m_currentMatch + 1); });
    const auto reloadText = [this, heading, findLabel, closeButton] {
        const auto scroll = m_browser->verticalScrollBar()->value();
        setWindowTitle(qtr("Help"));
        heading->setText(qtr("Help"));
        findLabel->setText(qtr("&Find:"));
        m_find->setPlaceholderText(qtr("Find in this page"));
        m_find->setAccessibleName(qtr("Find in this page"));
        m_find->setToolTip(QKeySequence(QKeySequence::Find).toString(QKeySequence::NativeText));
        m_previous->setText(qtr("&Previous"));
        m_next->setText(qtr("&Next"));
        m_previous->setToolTip(QKeySequence(QKeySequence::FindPrevious).toString(QKeySequence::NativeText));
        m_next->setToolTip(QKeySequence(QKeySequence::FindNext).toString(QKeySequence::NativeText));
        closeButton->setText(qtr("&Close"));
        m_browser->setHtml(qfut(I_LONGHELP));
        colorBrowserLinks(m_browser, palette().color(QPalette::Link));
        updateSearch();
        m_browser->verticalScrollBar()->setValue(scroll);
    };
    const auto metrics = [this, root] {
        root->setContentsMargins(helpDp(p_intf, 24), helpDp(p_intf, 24), helpDp(p_intf, 24), helpDp(p_intf, 24));
        root->setSpacing(helpDp(p_intf, 12));
    };
    reloadText();
    metrics();
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, reloadText);
    if (auto *ctx = p_intf->p_mi)
    {
        connect(ctx, &MainCtx::intfScaleFactorChanged, this, metrics);
        const auto links = [this] { colorBrowserLinks(m_browser, palette().color(QPalette::Link)); };
        connect(ctx, &MainCtx::redesignAccentChanged, this, links);
        connect(ctx->getColorScheme(), &ColorSchemeModel::currentChanged, this, links);
        connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, links);
    }
    restoreWidgetPosition("Help", QSize(helpDp(p_intf, 720), helpDp(p_intf, 580)));
    if (screen()) resize(size().boundedTo(screen()->availableGeometry().size()));
    setTabOrder(m_find, m_previous);
    setTabOrder(m_previous, m_next);
    setTabOrder(m_next, m_browser);
    setTabOrder(m_browser, closeButton);
    for (auto *child : findChildren<QWidget*>()) child->installEventFilter(this);
}

HelpDialog::~HelpDialog()
{
    saveWidgetPosition("Help");
}

void HelpDialog::updateSearch()
{
    m_matches.clear();
    if (!m_find->text().isEmpty())
    {
        auto cursor = m_browser->document()->find(m_find->text());
        while (!cursor.isNull())
        {
            m_matches.append(cursor);
            cursor = m_browser->document()->find(m_find->text(), cursor);
        }
    }
    showMatch(0);
}

void HelpDialog::showMatch(int index)
{
    m_previous->setEnabled(!m_matches.isEmpty());
    m_next->setEnabled(!m_matches.isEmpty());
    if (m_matches.isEmpty())
    {
        m_currentMatch = -1;
        auto cursor = m_browser->textCursor();
        cursor.clearSelection();
        m_browser->setTextCursor(cursor);
        m_findInfo->setText(m_find->text().isEmpty() ? QString() : qtr("No text found."));
        return;
    }
    m_currentMatch = (index % m_matches.size() + m_matches.size()) % m_matches.size();
    m_browser->setTextCursor(m_matches.at(m_currentMatch));
    m_browser->ensureCursorVisible();
    m_findInfo->setText(qtr("Match %1 of %2").arg(m_currentMatch + 1).arg(m_matches.size()));
}

bool HelpDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)
    {
        auto *key = static_cast<QKeyEvent*>(event);
        const bool find = key->matches(QKeySequence::Find);
        const bool next = key->matches(QKeySequence::FindNext) ||
            (watched == m_find && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter));
        const bool previous = key->matches(QKeySequence::FindPrevious);
        const bool reset = watched == m_find && key->key() == Qt::Key_Escape && !m_find->text().isEmpty();
        const bool copy = watched == m_browser && key->matches(QKeySequence::Copy);
        const bool selectAll = watched == m_browser && key->matches(QKeySequence::SelectAll);
        if (find || next || previous || reset || copy || selectAll)
        {
            if (event->type() == QEvent::KeyPress)
            {
                if (find) { m_find->setFocus(); m_find->selectAll(); }
                else if (reset) m_find->clear();
                else if (copy) m_browser->copy();
                else if (selectAll) m_browser->selectAll();
                else showMatch(m_currentMatch + (previous ? -1 : 1));
            }
            key->accept();
            return true;
        }
    }
    return QVLCFrame::eventFilter(watched, event);
}

void HelpDialog::keyPressEvent(QKeyEvent *event)
{
    if (!activateHelpButton(event, focusWidget())) QVLCFrame::keyPressEvent(event);
}

AboutDialog::AboutDialog(qt_intf_t *intf) : QVLCDialog(nullptr, intf)
{
    ui.setupUi(this);
    setWindowRole("vlc-about");
    setWindowModality(Qt::WindowModal);
    ui.version->setText(AboutModel::getVersion());
    ui.licensePage->setPlainText(AboutModel::getLicense());
    ui.authorsPage->setPlainText(AboutModel::getAuthors());
    ui.creditPage->setPlainText(AboutModel::getThanks());
    ui.MainBlabla->setUndoRedoEnabled(false);
    ui.MainBlabla->setTabChangesFocus(true);
    ui.overviewLayout->setContentsMargins(0, 0, 0, 0);
    ui.buildDetails->hide();
    connect(ui.buildDetailsButton, &QToolButton::toggled, this, [this](bool show) {
        ui.buildDetails->setVisible(show);
        updateHeaderLayout();
        layout()->activate();
        if (screen()) resize(size().boundedTo(screen()->availableGeometry().size()));
    });
#ifdef UPDATE_CHECK
    if (p_intf->p_mi) m_updateModel = p_intf->p_mi->getUpdateModel();
    connect(ui.checkUpdatesButton, &QToolButton::clicked, this, [this] {
        const auto model = m_updateModel;
        if (!model) return;
        if (model->updateStatus() == UpdateModel::NeedUpdate) {
            m_inlineCheckActive = false;
            accept();
            if (model) model->presentUpdate();
        } else {
            m_inlineCheckActive = true;
            model->checkUpdateInline();
        }
    });
    if (m_updateModel) connect(m_updateModel, &UpdateModel::updateStatusChanged, this, [this] {
        const auto model = m_updateModel;
        updateUpdateButton();
        if (model && isVisible() && m_inlineCheckActive && model->inlineCheck()
            && model->updateStatus() == UpdateModel::NeedUpdate) {
            // Release About's modality before showing the available release.
            m_inlineCheckActive = false;
            accept();
            if (model) model->presentUpdate();
        }
    });
#else
    ui.checkUpdatesButton->setEnabled(false);
#endif
    connect(ui.closeButton, &QPushButton::clicked, this, &QDialog::reject);
    auto *copyTimer = new QTimer(this);
    copyTimer->setSingleShot(true);
    copyTimer->setInterval(2500);
    connect(copyTimer, &QTimer::timeout, ui.copyFeedback, &QLabel::clear);
    connect(ui.copyVersionButton, &QToolButton::clicked, this, [this, copyTimer] {
        QGuiApplication::clipboard()->setText(QStringLiteral("Hakone Media Player ") + AboutModel::getVersion());
        ui.copyFeedback->setText(qtr("Version copied."));
        copyTimer->start();
    });
    const auto updateText = [this, copyTimer] {
        ui.retranslateUi(this);
        setWindowTitle(qtr("About"));
        ui.title->setText(qtr("Hakone Media Player"));
        ui.closeButton->setText(qtr("&Close"));
        ui.aboutTabs->setTabText(ui.aboutTabs->indexOf(ui.authorsPage), qtr("Authors"));
        ui.aboutTabs->setTabText(ui.aboutTabs->indexOf(ui.creditPage), qtr("Credits"));
        ui.aboutTabs->setTabText(ui.aboutTabs->indexOf(ui.licensePage), qtr("License"));
        ui.copyVersionButton->setToolTip(qtr("Copy this version to the clipboard."));
        ui.copyVersionButton->setAccessibleName(qtr("Copy version"));
        ui.checkUpdatesButton->setAccessibleName(qtr("Check for updates"));
        ui.buildDetailsButton->setToolTip(qtr("Show information about this build."));
        ui.buildDetailsButton->setAccessibleName(qtr("Build details"));
        ui.buildDetails->setAccessibleName(qtr("Build details"));
        ui.buildDetails->setPlainText(qtr("Build user: %1\nBuild host: %2\nBuild date: %3 %4\nCompiler: %5")
            .arg(qfu(VLC_CompileBy()), qfu(VLC_CompileHost()), QStringLiteral(__DATE__), QStringLiteral(__TIME__), qfu(VLC_Compiler())));
#ifdef UPDATE_CHECK
        ui.checkUpdatesButton->setToolTip(qtr("Check for a new version of Hakone."));
#else
        ui.checkUpdatesButton->setToolTip(qtr("Update checks are not available in this build."));
#endif
        copyTimer->stop();
        ui.copyFeedback->clear();
        ui.MainBlabla->setHtml("<html><head/><body>" +
        qtr( "<p>Hakone Media Player is a free and open source media player, encoder, and streamer. Hakone is a fork of VLC media player, made by the volunteers of the <a href=\"https://www.videolan.org/\"><span style=\" text-decoration: underline; color:#0057ae;\">VideoLAN</span></a> community.</p><p>Hakone uses its internal codecs, works on essentially every popular platform, and can read almost all files, CDs, DVDs, network streams, capture cards and other media formats!</p><p><a href=\"https://hakone.org/\"><span style=\" text-decoration: underline; color:#0057ae;\">Help and join us!</span></a>" ).remove(QStringLiteral("color:#0057ae;")) +
        "</p>" + AboutModel::getDesignAttribution() + "</body> </html>");
        colorBrowserLinks(ui.MainBlabla, palette().color(QPalette::Link));
        updateUpdateButton();
        updateHeaderLayout();
    };
    updateText();
    const auto metrics = [this] {
        ui.titleLayout->setSpacing(helpDp(p_intf, 4));
        ui.versionLayout->setSpacing(helpDp(p_intf, 4));
        const QSize iconSize(helpDp(p_intf, 20), helpDp(p_intf, 20));
        for (auto *button : {ui.copyVersionButton, ui.buildDetailsButton})
        {
            button->setIconSize(iconSize);
            button->setFixedSize(helpDp(p_intf, 32), helpDp(p_intf, 32));
        }
        ui.checkUpdatesButton->setIconSize(iconSize);
        ui.checkUpdatesButton->setFixedHeight(helpDp(p_intf, 32));
        ui.buildDetails->setFixedHeight(helpDp(p_intf, 128));
        QPixmap cone(QStringLiteral(":/logo/hakone256.png"));
        cone = cone.scaled(QSize(helpDp(p_intf, 64),helpDp(p_intf, 64)) * devicePixelRatioF(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        cone.setDevicePixelRatio(devicePixelRatioF());
        ui.VLCcone->setPixmap(cone);
        ui.VLCcone->setFixedSize(helpDp(p_intf, 64),helpDp(p_intf, 64));
        updateHeaderLayout();
    };
    metrics();
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, updateText);
    if (auto *ctx = p_intf->p_mi)
    {
        connect(ctx, &MainCtx::intfScaleFactorChanged, this, metrics);
        const auto colors = [this] {
            updateHeaderIcons();
            colorBrowserLinks(ui.MainBlabla, palette().color(QPalette::Link));
        };
        colors();
        connect(ctx, &MainCtx::redesignAccentChanged, this, colors);
        connect(ctx->getColorScheme(), &ColorSchemeModel::currentChanged, this, colors);
        connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, colors);
    }
    QVLCTools::restoreWidgetPosition(p_intf,"About",this,QSize(helpDp(p_intf, 720),helpDp(p_intf, 560)));
    if (screen()) resize(size().boundedTo(screen()->availableGeometry().size()));
    setTabOrder(ui.version, ui.copyVersionButton);
    setTabOrder(ui.copyVersionButton, ui.buildDetailsButton);
    setTabOrder(ui.buildDetailsButton, ui.checkUpdatesButton);
    setTabOrder(ui.checkUpdatesButton, ui.buildDetails);
    setTabOrder(ui.buildDetails, ui.aboutTabs);
    setTabOrder(ui.aboutTabs, ui.MainBlabla);
    for (auto *view : {static_cast<QWidget*>(ui.MainBlabla), static_cast<QWidget*>(ui.authorsPage),
                      static_cast<QWidget*>(ui.creditPage), static_cast<QWidget*>(ui.licensePage)})
        setTabOrder(view, ui.closeButton);
    setTabOrder(ui.closeButton, ui.version);
    for (auto *button : {ui.copyVersionButton, ui.buildDetailsButton, ui.checkUpdatesButton})
    {
        button->installEventFilter(this);
        const auto refresh = [this] { updateHeaderIcons(); };
        connect(button, &QToolButton::pressed, this, refresh);
        connect(button, &QToolButton::released, this, refresh);
        connect(button, &QToolButton::toggled, this, refresh);
    }
    // Read-only text shortcuts belong to the dialog, before player shortcuts.
    for (auto *view : {static_cast<QWidget*>(ui.MainBlabla), static_cast<QWidget*>(ui.authorsPage),
                      static_cast<QWidget*>(ui.creditPage), static_cast<QWidget*>(ui.licensePage),
                      static_cast<QWidget*>(ui.version), static_cast<QWidget*>(ui.buildDetails)}) view->installEventFilter(this);
}

void AboutDialog::updateUpdateButton()
{
#ifdef UPDATE_CHECK
    const auto status = m_updateModel ? m_updateModel->updateStatus() : UpdateModel::Unchecked;
    QString text = qtr("Check for updates");
    QString hint = qtr("Check for a new version of Hakone.");
    switch (status) {
    case UpdateModel::Checking:
        text = qtr("Checking for updates");
        hint = text;
        break;
    case UpdateModel::UpToDate:
        text = qtr("Hakone is up to date");
        hint = qtr("Hakone is up to date. Select this button to check again.");
        break;
    case UpdateModel::CheckFailed:
        text = qtr("Cannot check for updates");
        hint = text + QLatin1Char('\n') + qtr("Check your network connection. Select this button to check again.");
        break;
    case UpdateModel::NeedUpdate:
        text = qtr("View update");
        hint = qtr("A new version is available.");
        break;
    default:
        break;
    }
    ui.checkUpdatesButton->setText(text);
    ui.checkUpdatesButton->setAccessibleName(text);
    ui.checkUpdatesButton->setToolTip(m_updateModel ? hint : qtr("Update checks are not available."));
    ui.checkUpdatesButton->setEnabled(m_updateModel && status != UpdateModel::Checking && status != UpdateModel::Downloading);
#endif
    updateHeaderIcons();
    updateHeaderLayout();
}

void AboutDialog::showEvent(QShowEvent *event)
{
    QVLCDialog::showEvent(event);
    updateUpdateButton();
#ifdef UPDATE_CHECK
    const auto model = m_updateModel;
    if (model && model->updateStatus() == UpdateModel::Checking && !model->explicitCheck()) {
        // Join a startup check without sending another request.
        m_inlineCheckActive = true;
        model->checkUpdateInline();
    }
#endif
}

void AboutDialog::hideEvent(QHideEvent *event)
{
#ifdef UPDATE_CHECK
    if (m_inlineCheckActive) {
        m_inlineCheckActive = false;
        if (m_updateModel && m_updateModel->inlineCheck()) m_updateModel->dismissPresentation();
    }
#endif
    QVLCDialog::hideEvent(event);
}

void AboutDialog::updateHeaderLayout()
{
    if (m_headerLayoutUpdating) return;
    QScopedValueRollback<bool> guard(m_headerLayoutUpdating, true);
    const bool shortScreen = screen() && screen()->availableGeometry().height() < helpDp(p_intf, 400);
    const int margin = helpDp(p_intf, shortScreen ? 12 : 24);
    layout()->setContentsMargins(margin, margin, margin, margin);
    layout()->setSpacing(helpDp(p_intf, shortScreen ? 6 : 12));
    ui.headerLayout->setSpacing(helpDp(p_intf, shortScreen ? 8 : 16));
    ui.identityLayout->setSpacing(helpDp(p_intf, shortScreen ? 8 : 16));
    // Measure the full caption regardless of the currently displayed style.
    ui.checkUpdatesButton->setMinimumWidth(0);
    ui.checkUpdatesButton->setMaximumWidth(QWIDGETSIZE_MAX);
    ui.checkUpdatesButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    const int fullWidth = ui.checkUpdatesButton->sizeHint().width();
    const int compactWidth = helpDp(p_intf, 32);
    const int required = ui.identityLayout->sizeHint().width() + fullWidth + ui.headerLayout->spacing();
    const bool compact = contentsRect().width() - layout()->contentsMargins().left()
        - layout()->contentsMargins().right() < required;
    ui.checkUpdatesButton->setToolButtonStyle(compact ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
    ui.checkUpdatesButton->setFixedWidth(compact ? compactWidth : fullWidth);
    if (ui.checkUpdatesButton->property("redesignCompact") != QVariant(compact))
    {
        ui.checkUpdatesButton->setProperty("redesignCompact", compact);
        ui.checkUpdatesButton->style()->unpolish(ui.checkUpdatesButton);
        ui.checkUpdatesButton->style()->polish(ui.checkUpdatesButton);
    }
    ui.headerLayout->setAlignment(ui.checkUpdatesButton, Qt::AlignRight | (compact ? Qt::AlignTop : Qt::AlignVCenter));
    // Reserve room for the icon so it stays beside the identity at small sizes.
    ui.aboutHeader->setMinimumWidth(ui.identityLayout->minimumSize().width() + compactWidth + ui.headerLayout->spacing());
    if (screen()) {
        // Keep the scrollable build details within the work area at large text sizes.
        int minimum = layout()->totalMinimumHeightForWidth(width());
        if (minimum < 0) minimum = layout()->minimumSize().height();
        const bool hidden = ui.buildDetails->isHidden();
        const int otherHeight = minimum - (hidden ? 0 : ui.buildDetails->minimumHeight());
        const int room = screen()->availableGeometry().height() - otherHeight
            - (hidden ? layout()->spacing() : 0);
        ui.buildDetails->setFixedHeight(std::clamp(room, 1, helpDp(p_intf, 128)));
    }
}

void AboutDialog::updateHeaderIcons(QToolButton *hoveredButton, bool hovered)
{
    const auto apply = [this, hoveredButton, hovered](QToolButton *button, const QString &path) {
        const bool active = (button == hoveredButton ? hovered : button->underMouse()) || button->isDown() || button->isChecked();
        const auto color = palette().color(!button->isEnabled() ? QPalette::Mid : active ? QPalette::Text : QPalette::WindowText);
        button->setIcon(ColorizedSvgIcon(path, color, {}, {},
            {{QStringLiteral("#212121"), color.name()}, {QStringLiteral("#8F8F8F"), color.name()}}));
    };
    apply(ui.copyVersionButton, QStringLiteral(":/icons/copy.svg"));
    apply(ui.buildDetailsButton, QStringLiteral(":/icons/info.svg"));
#ifdef UPDATE_CHECK
    if (m_updateModel && m_updateModel->updateStatus() == UpdateModel::UpToDate) {
        const QColor color = palette().color(QPalette::Window).lightness() > 128
            ? QColor(QStringLiteral("#248a3d")) : QColor(QStringLiteral("#63c174"));
        ui.checkUpdatesButton->setIcon(ColorizedSvgIcon(QStringLiteral(":/icons/check.svg"), color, {}, {},
            {{QStringLiteral("black"), color.name()}}));
    } else
#endif
        apply(ui.checkUpdatesButton, QStringLiteral(":/menu/redesign/refresh.svg"));
}

void AboutDialog::resizeEvent(QResizeEvent *event)
{
    QVLCDialog::resizeEvent(event);
    updateHeaderLayout();
}

AboutDialog::~AboutDialog()
{
    QVLCTools::saveWidgetPosition(p_intf,"About",this);
}

bool AboutDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui.copyVersionButton || watched == ui.buildDetailsButton || watched == ui.checkUpdatesButton)
    {
        if (event->type() == QEvent::Enter || event->type() == QEvent::Leave)
            updateHeaderIcons(static_cast<QToolButton*>(watched), event->type() == QEvent::Enter);
    }
    if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)
    {
        auto *key = static_cast<QKeyEvent*>(event);
        const bool copy = key->matches(QKeySequence::Copy);
        const bool selectAll = key->matches(QKeySequence::SelectAll);
        if (copy || selectAll)
        {
            if (event->type() == QEvent::KeyPress)
            {
                if (auto *plain = qobject_cast<QPlainTextEdit*>(watched))
                {
                    if (copy) plain->copy(); else plain->selectAll();
                }
                else if (auto *rich = qobject_cast<QTextEdit*>(watched))
                {
                    if (copy) rich->copy(); else rich->selectAll();
                }
                else if (auto *label = qobject_cast<QLabel*>(watched))
                {
                    if (selectAll) label->setSelection(0, label->text().size());
                    else if (label->hasSelectedText()) QGuiApplication::clipboard()->setText(label->selectedText());
                }
            }
            key->accept();
            return true;
        }
    }
    return QVLCDialog::eventFilter(watched, event);
}

void AboutDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat()) { event->accept(); return; }
    if (event->key() == Qt::Key_Escape && ui.buildDetailsButton->isChecked())
    {
        ui.buildDetailsButton->setChecked(false);
        ui.buildDetailsButton->setFocus();
        event->accept();
        return;
    }
    if (!activateHelpButton(event, focusWidget())) QVLCDialog::keyPressEvent(event);
}

#ifdef UPDATE_CHECK

class UpdateModelPrivate
{
public:
    GithubReleaseService* service = nullptr;
    std::optional<vlc::updates::Release> release;
    UpdateModel::Status status = UpdateModel::Unchecked;
    bool explicitCheck = false;
    bool presentationDismissed = false;
    bool inlineCheck = false;
};

UpdateModel::UpdateModel(qt_intf_t * p_intf)
    : d_ptr(new UpdateModelPrivate)
{
    Q_UNUSED(p_intf);
    Q_D(UpdateModel);
    d->service = new GithubReleaseService(this);
    connect(d->service, &GithubReleaseService::finished, this, [this](bool success) {
        Q_D(UpdateModel);
        d->release = d->service->release();
        d->status = !success ? CheckFailed : d->release ? NeedUpdate : UpToDate;
        emit updateStatusChanged();
    });
}

UpdateModel::~UpdateModel() = default;

void UpdateModel::checkUpdate(bool explicitCheck)
{
    startCheck(explicitCheck, false);
}

void UpdateModel::checkUpdateInline()
{
    startCheck(true, true);
}

bool UpdateModel::inlineCheck() const
{
    Q_D(const UpdateModel);
    return d->inlineCheck;
}

void UpdateModel::presentUpdate()
{
    Q_D(UpdateModel);
    if (d->status != NeedUpdate) return;
    d->inlineCheck = false;
    d->presentationDismissed = false;
    if (!d->explicitCheck) {
        d->explicitCheck = true;
        emit explicitCheckChanged();
    }
    emit presentationRequested();
}

void UpdateModel::startCheck(bool explicitCheck, bool inlineCheck)
{
    Q_D(UpdateModel);
    if (d->status == Checking)
    {
        if (explicitCheck)
        {
            d->inlineCheck = inlineCheck;
            d->presentationDismissed = false;
            if (!d->explicitCheck)
            {
                d->explicitCheck = true;
                emit explicitCheckChanged();
            }
            // Present the existing request without sending another one.
            if (!inlineCheck) emit presentationRequested();
        }
        return;
    }
    d->presentationDismissed = false;
    d->inlineCheck = inlineCheck;
    if (d->explicitCheck != explicitCheck)
    {
        d->explicitCheck = explicitCheck;
        emit explicitCheckChanged();
    }
    d->release.reset();
    auto installed = vlc::updates::Version::parse(QStringLiteral(HAKONE_VERSION));
    if (installed)
        installed->generation = PACKAGE_UPDATE_GENERATION;
    d->status = installed ? Checking : CheckFailed;
    emit updateStatusChanged();
    if (installed)
        d->service->check(*installed, vlc::updates::Target::current());
}

bool UpdateModel::openReleasePage()
{
    Q_D(UpdateModel);
    return d->status == NeedUpdate && d->release &&
           QDesktopServices::openUrl(d->release->pageUrl);
}

// Keep the existing invokables for interface compatibility. GitHub discovery
// never hands an unsigned asset to the core's signed download/install backend.
bool UpdateModel::download(QString destDir)
{
    Q_UNUSED(destDir);
    return openReleasePage();
}

bool UpdateModel::download()
{
    return openReleasePage();
}

void UpdateModel::resetStatus()
{
    Q_D(UpdateModel);
    if (d->status == Unchecked || d->status == Checking)
        return;
    d->release.reset();
    d->status = Unchecked;
    emit updateStatusChanged();
}

UpdateModel::Status UpdateModel::updateStatus() const
{
    Q_D(const UpdateModel);
    return d->status;
}

int UpdateModel::getMajor() const
{
    Q_D(const UpdateModel);
    return d->release ? int(d->release->version.major) : 0;
}
int UpdateModel::getMinor() const
{
    Q_D(const UpdateModel);
    return d->release ? int(d->release->version.minor) : 0;
}
int UpdateModel::getRevision() const
{
    Q_D(const UpdateModel);
    return d->release ? int(d->release->version.patch) : 0;
}
int UpdateModel::getExtra() const
{
    return 0;
}
QString UpdateModel::getVersion() const
{
    Q_D(const UpdateModel);
    return d->release ? d->release->version.text : QString();
}
QString UpdateModel::getDescription() const
{
    Q_D(const UpdateModel);
    return d->release ? d->release->notes : QString();
}
QString UpdateModel::getProductName() const
{
    Q_D(const UpdateModel);
    return d->release ? d->release->productName : QString();
}
QString UpdateModel::getSubtitle() const
{
    Q_D(const UpdateModel);
    return d->release ? d->release->subtitle : QString();
}
QString UpdateModel::getUrl() const
{
    Q_D(const UpdateModel);
    return d->release ? d->release->pageUrl.toString() : QString();
}
QString UpdateModel::getAssetUrl() const
{
    Q_D(const UpdateModel);
    return d->release ? d->release->asset.url.toString() : QString();
}

double UpdateModel::getProgress() const
{
    return 0.0;
}

bool UpdateModel::explicitCheck() const
{
    Q_D(const UpdateModel);
    return d->explicitCheck;
}

void UpdateModel::resetExplicitCheck()
{
    Q_D(UpdateModel);
    if (!d->explicitCheck)
        return;
    d->explicitCheck = false;
    emit explicitCheckChanged();
}

bool UpdateModel::presentationDismissed() const
{
    Q_D(const UpdateModel);
    return d->presentationDismissed;
}

void UpdateModel::dismissPresentation()
{
    Q_D(UpdateModel);
    d->presentationDismissed = true;
    resetExplicitCheck();
}

/*****************************************************************************
 * UpdateDialog
 *****************************************************************************/

UpdateDialog::UpdateDialog(qt_intf_t *intf) : QVLCFrame(intf)
{
    setObjectName(QStringLiteral("redesignUpdates"));
    setWindowRole("vlc-update");
    ui.setupUi(this);
    m_ready = true;
    ui.updateTitle->setProperty("redesignHeading", true);
    ui.updateOpenRelease->setProperty("redesignPrimary", true);
    ui.releaseLayout->setContentsMargins(0, 0, 0, 0);
    ui.releaseLinkLayout->setContentsMargins(0, 0, 0, 0);
    ui.updateActions->setContentsMargins(0, 0, 0, 0);
    m_model = p_intf->p_mi->getUpdateModel();
    m_copyTimer = new QTimer(this);
    m_copyTimer->setSingleShot(true);
    m_copyTimer->setInterval(2500);
    connect(m_copyTimer, &QTimer::timeout, this, [this] { m_feedback = None; updateUI(); });
    connect(ui.updateClose, &QPushButton::clicked, this, &UpdateDialog::hide);
    connect(ui.updateCheck, &QPushButton::clicked, this, [this] { if (m_model) m_model->checkUpdate(true); });
    connect(ui.updateOpenRelease, &QPushButton::clicked, this, &UpdateDialog::openRelease);
    connect(ui.updateCopyLink, &QPushButton::clicked, this, [this] {
        if (!m_model || m_model->updateStatus() != UpdateModel::NeedUpdate || !QGuiApplication::clipboard()) return;
        QGuiApplication::clipboard()->setText(m_model->getUrl());
        m_feedback = LinkCopied;
        m_copyTimer->start();
        updateUI();
    });
    connect(m_model, &UpdateModel::updateStatusChanged, this, [this] {
        m_copyTimer->stop();
        m_feedback = None;
        updateUI();
    });
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, &UpdateDialog::updateUI);
    connect(p_intf->p_mi, &MainCtx::intfScaleFactorChanged, this, &UpdateDialog::updateMetrics);
    ui.updateNotes->installEventFilter(this);
    ui.updateReleaseUrl->installEventFilter(this);
    ui.updateFeedback->installEventFilter(this);
    updateUI();
    updateMetrics();
    restoreWidgetPosition("Update", QSize(helpDp(p_intf, 640), helpDp(p_intf, 500)));
    if (screen()) resize(size().boundedTo(screen()->availableGeometry().size()));
    setTabOrder(ui.updateNotes, ui.updateReleaseUrl);
    setTabOrder(ui.updateReleaseUrl, ui.updateCopyLink);
    setTabOrder(ui.updateCopyLink, ui.updateClose);
    setTabOrder(ui.updateClose, ui.updateCheck);
    setTabOrder(ui.updateCheck, ui.updateOpenRelease);
}

UpdateDialog::~UpdateDialog()
{
    saveWidgetPosition("Update");
}

void UpdateDialog::present(bool forceActivation)
{
    const bool activate = forceActivation || (m_model && m_model->explicitCheck());
    setAttribute(Qt::WA_ShowWithoutActivating, !activate);
    show();
    if (activate)
    {
        raise();
        activateWindow();
    }
}

bool UpdateDialog::event(QEvent *event)
{
    if (m_model)
    {
        if (event->type() == QEvent::Hide || event->type() == QEvent::Close || event->type() == QEvent::Destroy)
            m_model->dismissPresentation();
        else if (event->type() == QEvent::Show)
        {
            updateUI();
            // Opening a window must not make Return launch a browser.
            ui.updateClose->setFocus(Qt::OtherFocusReason);
        }
    }
    return QVLCFrame::event(event);
}

void UpdateDialog::openRelease()
{
    if (!m_model || m_model->updateStatus() != UpdateModel::NeedUpdate) return;
    if (m_model->openReleasePage()) hide();
    else
    {
        m_copyTimer->stop();
        m_feedback = BrowserFailed;
        updateUI();
    }
}

void UpdateDialog::updateUI()
{
    if (!m_model) return;
    ui.retranslateUi(this);
    const auto status = m_model->updateStatus();
    const bool available = status == UpdateModel::NeedUpdate;
    const bool busy = status == UpdateModel::Checking || status == UpdateModel::Downloading;
    ui.updateProgress->setVisible(busy);
    ui.updateCheck->setEnabled(!busy);
    ui.updateOpenRelease->setVisible(available);
    ui.updateReleaseDetails->setVisible(available);
    ui.updateReleaseLink->setVisible(available);
    const auto notes = m_model->getDescription();
    if (ui.updateNotes->toPlainText() != notes) ui.updateNotes->setPlainText(notes);
    const auto url = m_model->getUrl();
    if (ui.updateReleaseUrl->text() != url) ui.updateReleaseUrl->setText(url);
    const auto subtitle = m_model->getSubtitle();
    ui.updateSubtitle->setVisible(available && !subtitle.isEmpty());
    ui.updateSubtitle->setText(subtitle);
    switch (status)
    {
    case UpdateModel::NeedUpdate:
        ui.updateTitle->setText(qtr("A new version is available"));
        ui.updateSummary->setText(qtr("%1 %2\nOpen the release page in your web browser.")
            .arg(m_model->getProductName(), m_model->getVersion()));
        break;
    case UpdateModel::UpToDate:
        ui.updateTitle->setText(qtr("No update is available"));
        ui.updateSummary->setText(qtr("No newer compatible release is available for Hakone %1.").arg(QStringLiteral(HAKONE_VERSION)));
        break;
    case UpdateModel::CheckFailed:
        ui.updateTitle->setText(qtr("Cannot check for updates"));
        ui.updateSummary->setText(qtr("Check your network connection. Then select Check again."));
        break;
    case UpdateModel::Checking:
        ui.updateTitle->setText(qtr("Checking for updates"));
        ui.updateSummary->setText(qtr("You can close this window while Hakone checks for updates."));
        break;
    case UpdateModel::Downloading:
        ui.updateTitle->setText(qtr("Downloading update"));
        ui.updateSummary->setText(qtr("The update is downloading."));
        break;
    case UpdateModel::Unchecked:
        ui.updateTitle->setText(qtr("Hakone updates"));
        ui.updateSummary->setText(qtr("Installed version: %1\nCheck for a new version of Hakone.").arg(QStringLiteral(HAKONE_VERSION)));
        break;
    }
    ui.updateFeedback->setVisible(available && m_feedback != None);
    ui.updateFeedback->setText(m_feedback == LinkCopied ? qtr("Link copied.")
        : m_feedback == BrowserFailed ? qtr("Cannot open the browser. Copy the link and open it in your browser.") : QString());
    const bool primary = !available;
    if (ui.updateCheck->property("redesignPrimary").toBool() != primary)
    {
        ui.updateCheck->setProperty("redesignPrimary", primary);
        ui.updateCheck->style()->unpolish(ui.updateCheck);
        ui.updateCheck->style()->polish(ui.updateCheck);
    }
    updateActions();
}

void UpdateDialog::updateMetrics()
{
    const int margin = helpDp(p_intf, 24);
    ui.updateLayout->setContentsMargins(margin, margin, margin, margin);
    ui.updateLayout->setSpacing(helpDp(p_intf, 12));
    ui.releaseLayout->setSpacing(helpDp(p_intf, 8));
    ui.releaseLinkLayout->setSpacing(helpDp(p_intf, 8));
    ui.updateActions->setSpacing(helpDp(p_intf, 8));
    ui.updateNotes->setMinimumHeight(helpDp(p_intf, 80));
    ui.updateProgress->setFixedHeight(helpDp(p_intf, 8));
    auto minimum = QSize(helpDp(p_intf, 420), helpDp(p_intf, 260));
    if (screen()) minimum = minimum.boundedTo(screen()->availableGeometry().size());
    setMinimumSize(minimum);
    updateActions();
}

void UpdateDialog::updateActions()
{
    if (!m_ready || m_layoutUpdating) return;
    m_layoutUpdating = true;
    int width = 0, count = 0;
    for (auto *button : {ui.updateClose, ui.updateCheck, ui.updateOpenRelease})
        if (!button->isHidden()) { width += button->sizeHint().width(); ++count; }
    width += qMax(0, count - 1) * ui.updateActions->spacing();
    const auto margin = ui.updateLayout->contentsMargins();
    ui.updateActions->setDirection(width > this->width() - margin.left() - margin.right()
        ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    int minimum = qMax(helpDp(p_intf, 260), ui.updateLayout->totalMinimumHeightForWidth(this->width()));
    if (screen()) minimum = qMin(minimum, screen()->availableGeometry().height());
    setMinimumHeight(minimum);
    m_layoutUpdating = false;
}

void UpdateDialog::resizeEvent(QResizeEvent *event)
{
    updateActions();
    QVLCFrame::resizeEvent(event);
}

void UpdateDialog::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && event->isAutoRepeat())
        event->accept();
    else if (!activateHelpButton(event, focusWidget())) QVLCFrame::keyPressEvent(event);
}

bool UpdateDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)
    {
        auto *key = static_cast<QKeyEvent*>(event);
        const bool copy = key->matches(QKeySequence::Copy);
        const bool selectAll = key->matches(QKeySequence::SelectAll);
        if (copy || selectAll)
        {
            if (event->type() == QEvent::KeyPress)
            {
                if (watched == ui.updateNotes)
                {
                    if (copy) ui.updateNotes->copy(); else ui.updateNotes->selectAll();
                }
                else if (watched == ui.updateReleaseUrl)
                {
                    if (copy) ui.updateReleaseUrl->copy(); else ui.updateReleaseUrl->selectAll();
                }
                else if (watched == ui.updateFeedback)
                {
                    if (selectAll) ui.updateFeedback->setSelection(0, ui.updateFeedback->text().size());
                    else if (ui.updateFeedback->hasSelectedText()) QGuiApplication::clipboard()->setText(ui.updateFeedback->selectedText());
                }
            }
            key->accept();
            return true;
        }
    }
    return QVLCFrame::eventFilter(watched, event);
}

#endif
