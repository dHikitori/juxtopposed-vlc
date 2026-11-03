/*****************************************************************************
 * dialogs_provider.cpp : Dialog Provider
 *****************************************************************************
 * Copyright (C) 2006-2009 the VideoLAN team
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

#include <vlc_common.h>
#include <vlc_interface.h>
#include <vlc_intf_strings.h>
#include <vlc_preparser.h>

#include "qt.hpp"
#include "dialogs_provider.hpp"
#include <QStyleOptionButton>
#include "player/player_controller.hpp" /* Load Subtitles */
#include "playlist/playlist_controller.hpp"
#include "menus/menus.hpp"
#include "util/qt_dirs.hpp"
#include "util/local_file_url.hpp"
#include "util/media_identity_vlc.hpp"
#include "util/vlchotkeyconverter.hpp"
#include "maininterface/mainctx.hpp"

/* The dialogs */
#include "dialogs/bookmarks/bookmarks.hpp"
#include "medialibrary/mlbookmarkmodel.hpp"
#include "dialogs/preferences/preferences.hpp"
#include "dialogs/mediainfo/mediainfo.hpp"
#include "dialogs/messages/messages.hpp"
#include "dialogs/extended/extended.hpp"
#include "dialogs/vlm/vlm.hpp"
#include "dialogs/sout/sout.hpp"
#include "dialogs/sout/convert.hpp"
#include "dialogs/open/open.hpp"
#include "dialogs/open/openurl.hpp"
#include "dialogs/help/help.hpp"
#include "dialogs/gototime/gototime.hpp"
#include "dialogs/podcast/podcast_configuration.hpp"
#include "dialogs/plugins/plugins.hpp"
#include "dialogs/epg/epg.hpp"
#include "dialogs/playlists/playlists.hpp"
#include "medialibrary/medialib.hpp"
#include "util/redesign_widget_theme.hpp"
#include "util/interface_translator.hpp"
#include "util/colorizedsvgicon.hpp"
#include "util/color_scheme_model.hpp"

#include <vlc_url.h>
#include <vlc_playlist.h>

#include <QEvent>
#include <QApplication>
#include <QSignalMapper>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QTemporaryFile>
#include <QSaveFile>
#include <QUrl>
#include <QInputDialog>
#include <QPointer>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QComboBox>
#include <QListView>
#include <QScreen>
#include <QScopeGuard>
#include <QRegularExpression>
#include <QTextBrowser>
#include <QPlainTextEdit>
#include <QShortcut>
#include <QAbstractTextDocumentLayout>
#include <QClipboard>
#include <QKeyEvent>
#include <QLabel>
#include <QTimer>
#include <QStyle>
#include <QGuiApplication>
#include <QWindow>
#include <algorithm>
#include <utility>

namespace {
// Keep the original destination until the exporter and the final write succeed.
// The temporary file is in the destination folder so relative media paths agree.
bool exportPlayQueue(qt_intf_t *intf, const QString &file, const char *format)
{
    const QFileInfo destination(file);
    QTemporaryFile temporary(destination.dir().filePath(QStringLiteral(".vlc-export-XXXXXX")));
    if (!temporary.open()) return false;
    temporary.close();
    int status;
    {
        vlc::playlist::vlc_playlist_locker lock { intf->p_playlist };
        status = vlc_playlist_Export(intf->p_playlist, qtu(temporary.fileName()), format);
    }
    if (status != VLC_SUCCESS) return false;
    QFile input(temporary.fileName());
    QSaveFile output(file);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) return false;
    while (!input.atEnd()) {
        const auto bytes = input.read(65536);
        if (bytes.isEmpty() || output.write(bytes) != bytes.size()) return false;
    }
    return input.error() == QFile::NoError && output.commit();
}

// The clipboard can contain a list, so keep editing and review in one step.
class NativeMediaListDialog final : public QVLCDialog
{
public:
    NativeMediaListDialog(qt_intf_t *intf, const QString &text)
        : QVLCDialog(nullptr, intf), m_ctx(intf->p_mi)
    {
        setObjectName(QStringLiteral("redesignMediaList"));
        setWindowRole("vlc-paste-media");
        auto *layout = new QVBoxLayout(this);
        m_heading = new QLabel(this);
        m_heading->setProperty("redesignHeading", true);
        m_heading->setTextFormat(Qt::PlainText);
        m_heading->setWordWrap(true);
        layout->addWidget(m_heading);
        m_help = new QLabel(this);
        m_help->setTextFormat(Qt::PlainText);
        m_help->setWordWrap(true);
        layout->addWidget(m_help);
        m_editor = new QPlainTextEdit(this);
        m_editor->setObjectName(QStringLiteral("clipboardMediaList"));
        m_editor->setPlainText(text);
        m_editor->setTabChangesFocus(true);
        m_editor->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        m_editor->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        layout->addWidget(m_editor, 1);
        m_count = new QLabel(this);
        m_count->setObjectName(QStringLiteral("clipboardMediaCount"));
        m_count->setTextFormat(Qt::PlainText);
        m_count->setWordWrap(true);
        layout->addWidget(m_count);
        m_actions = new QBoxLayout(QBoxLayout::LeftToRight);
        m_actions->addStretch();
        m_cancel = new QPushButton(this);
        m_cancel->setObjectName(QStringLiteral("clipboardMediaCancel"));
        m_cancel->setAutoDefault(true);
        m_actions->addWidget(m_cancel);
        m_add = new QPushButton(this);
        m_add->setObjectName(QStringLiteral("clipboardMediaAdd"));
        m_add->setAutoDefault(true);
        m_add->setDefault(true);
        m_add->setProperty("redesignPrimary", true);
        m_actions->addWidget(m_add);
        layout->addLayout(m_actions);
        connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_add, &QPushButton::clicked, this, [this] { if (!items().isEmpty()) accept(); });
        connect(m_editor, &QPlainTextEdit::textChanged, this, [this] { updateCount(); });
        // Return inserts a line. Only the explicit shortcut submits the list.
        for (auto key : {Qt::Key_Return, Qt::Key_Enter})
        {
            auto *shortcut = new QShortcut(QKeySequence(Qt::CTRL | key), this);
            shortcut->setAutoRepeat(false);
            connect(shortcut, &QShortcut::activated, m_add, &QPushButton::click);
        }
        applyRedesignWidgetTheme(this, m_ctx);
        if (m_ctx)
        {
            connect(m_ctx, &MainCtx::intfScaleFactorChanged, this, [this] { updateMetrics(); });
            connect(m_ctx->getColorScheme(), &ColorSchemeModel::currentChanged, this, [this] { updateMetrics(); });
            connect(m_ctx, &QObject::destroyed, this, &QDialog::reject);
        }
        if (auto *translator = InterfaceTranslator::instance())
            connect(translator, &InterfaceTranslator::languageChanged, this, [this] { retranslate(); });
        retranslate();
        const auto available = availableSize();
        resize(QSize(dp(560), dp(380)).boundedTo(available));
        m_editor->setFocus();
        m_editor->selectAll();
    }

    QStringList items() const
    {
        QStringList result;
        const auto text = m_editor->toPlainText();
        for (const auto &line : QStringView(text).split(u'\n'))
        {
            const auto item = line.trimmed();
            if (!item.isEmpty()) result.append(item.toString());
        }
        return result;
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QVLCDialog::resizeEvent(event);
        updateActions();
    }

private:
    int dp(int value) const { return qRound(value * (m_ctx ? m_ctx->getIntfScaleFactor() : 1)); }
    QSize availableSize() const
    {
        const auto size = screen()->availableGeometry().size() - QSize(dp(32), dp(32));
        return size.expandedTo(QSize(1, 1));
    }
    void updateCount()
    {
        const auto count = items().size();
        m_count->setText(qtr("Items: %1").arg(count));
        m_add->setEnabled(count > 0);
    }
    void retranslate()
    {
        setWindowTitle(qtr("Paste media"));
        m_heading->setText(windowTitle());
        const auto help = qtr("Use one address or file path per line.");
        m_help->setText(help + QLatin1Char(' ') + qtr("Press %1 to add the items.").arg(
            QKeySequence(Qt::CTRL | Qt::Key_Return).toString(QKeySequence::NativeText)));
        m_editor->setAccessibleName(qtr("Media addresses"));
        m_editor->setAccessibleDescription(m_help->text());
        m_cancel->setText(qtr("Cancel"));
        m_add->setText(qtr("Add to queue"));
        updateCount();
        updateMetrics();
    }
    void updateActions()
    {
        const int required = m_cancel->sizeHint().width() + m_add->sizeHint().width() + m_actions->spacing();
        m_actions->setDirection(contentsRect().width() - 2 * layout()->contentsMargins().left() < required
            ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    }
    void updateMetrics()
    {
        const auto available = availableSize();
        const int margin = qMin(dp(24), available.width() / 12);
        layout()->setContentsMargins(margin, margin, margin, margin);
        layout()->setSpacing(dp(12));
        m_actions->setSpacing(dp(8));
        m_editor->setMinimumSize(0, dp(48));
        setMinimumSize(QSize(dp(320), dp(260)).boundedTo(available));
        resize(size().boundedTo(available));
        updateActions();
    }
    QPointer<MainCtx> m_ctx;
    QLabel *m_heading;
    QLabel *m_help;
    QLabel *m_count;
    QPlainTextEdit *m_editor;
    QBoxLayout *m_actions;
    QPushButton *m_cancel;
    QPushButton *m_add;
};

// Keep the provider's synchronous result contract, with a bounded reading area.
class NativeMessageDialog final : public QVLCDialog
{
public:
    NativeMessageDialog(qt_intf_t *intf, const QString &text, const QString &title,
                        QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton,
                        QMessageBox::Icon icon, const QString &acceptText = {}, QWidget *owner = nullptr)
        : QVLCDialog(owner ? owner->window()->windowHandle() : nullptr, intf), m_ctx(intf->p_mi), m_text(text), m_acceptText(acceptText), m_iconType(icon)
    {
        setObjectName(QStringLiteral("redesignNativeMessage"));
        setWindowRole("vlc-message");
        setWindowTitle(title.isEmpty() ? qtr("Hakone Media Player") : title);
        auto *root = new QVBoxLayout(this);
        auto *headingRow = new QHBoxLayout;
        m_icon = new QLabel(this);
        headingRow->addWidget(m_icon, 0, Qt::AlignTop);
        auto *heading = new QLabel(windowTitle(), this);
        heading->setObjectName(QStringLiteral("messageHeading"));
        heading->setTextFormat(Qt::PlainText);
        heading->setWordWrap(true);
        heading->setProperty("redesignHeading", true);
        headingRow->addWidget(heading, 1);
        root->addLayout(headingRow);
        m_body = new QTextBrowser(this);
        m_body->setObjectName(QStringLiteral("messageBody"));
        m_body->setAccessibleName(windowTitle());
        m_body->setPlainText(text);
        m_body->setOpenLinks(false);
        m_body->setUndoRedoEnabled(false);
        m_body->setTabChangesFocus(true);
        m_body->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        m_body->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        m_body->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_body->installEventFilter(this);
        root->addWidget(m_body, 1);
        m_footer = new QWidget(this);
        m_footer->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_actions = new QHBoxLayout(m_footer);
        m_actions->setContentsMargins(0, 0, 0, 0);
        m_copy = new QPushButton(qtr("Copy"), this);
        m_copy->setObjectName(QStringLiteral("messageCopy"));
        m_copy->setAutoDefault(false);
        m_copy->setToolTip(qtr("Copy the full message."));
        m_actions->addWidget(m_copy, 0, Qt::AlignLeft | Qt::AlignTop);
        m_actions->addStretch(1);
        if (buttons == QMessageBox::NoButton) buttons = QMessageBox::Close;
        m_buttons = new QDialogButtonBox(QDialogButtonBox::StandardButtons(int(buttons)), this);
        m_buttons->setObjectName(QStringLiteral("messageActions"));
        updateNamedActions();
        m_actions->addWidget(m_buttons);
        root->addWidget(m_footer);
        connect(m_buttons, &QDialogButtonBox::clicked, this, [this](QAbstractButton *button) {
            done(int(m_buttons->standardButton(button)));
        });
        // QDialogButtonBox and QMessageBox share the public standard-button values.
        for (auto *button : m_buttons->buttons())
            static_cast<QPushButton*>(button)->setAutoDefault(false);
        if (!buttons.testFlag(defaultButton) || defaultButton == QMessageBox::NoButton)
            for (auto candidate : {QMessageBox::Close, QMessageBox::Cancel, QMessageBox::No, QMessageBox::Ok, QMessageBox::Yes})
                if (buttons.testFlag(candidate)) { defaultButton = candidate; break; }
        if (auto *button = m_buttons->button(QDialogButtonBox::StandardButton(defaultButton)))
        {
            button->setDefault(true);
            button->setFocus();
        }
        for (auto candidate : {QDialogButtonBox::Cancel, QDialogButtonBox::Close, QDialogButtonBox::No})
            if (m_buttons->button(candidate)) { m_escape = candidate; break; }
        if (m_escape == QDialogButtonBox::NoButton && m_buttons->buttons().size() == 1)
            m_escape = m_buttons->standardButton(m_buttons->buttons().first());
        // A stack-owned message must not become a child of its caller. Close it
        // safely if the caller disappears during the nested modal event loop.
        if (owner) connect(owner, &QObject::destroyed, this, [this] {
            done(int(m_escape == QDialogButtonBox::NoButton ? QDialogButtonBox::Cancel : m_escape));
        });
        m_copyTimer = new QTimer(this);
        m_copyTimer->setSingleShot(true);
        m_copyTimer->setInterval(2500);
        connect(m_copyTimer, &QTimer::timeout, this, [this] { m_copy->setText(qtr("Copy")); });
        connect(m_copy, &QPushButton::clicked, this, [this] {
            QGuiApplication::clipboard()->setText(m_text);
            m_copy->setText(qtr("Copied."));
            m_copyTimer->start();
        });
        const auto metrics = [this] { updateMetrics(); };
        if (m_ctx)
        {
            connect(m_ctx, &MainCtx::intfScaleFactorChanged, this, metrics);
            connect(m_ctx, &MainCtx::redesignAccentChanged, this, metrics);
            connect(m_ctx->getColorScheme(), &ColorSchemeModel::currentChanged, this, metrics);
        }
        if (auto *translator = InterfaceTranslator::instance())
            connect(translator, &InterfaceTranslator::languageChanged, this, [this] {
                m_copyTimer->stop();
                m_copy->setText(qtr("Copy"));
                m_copy->setToolTip(qtr("Copy the full message."));
                updateMetrics();
            });
        updateMetrics();
        const auto available = screen()->availableGeometry().size() - QSize(dp(32), dp(32));
        resize(qMin(dp(520), available.width()), qMin(dp(500), available.height()));
        root->activate();
        // Short notices use only the space they need. Long text can scroll or grow.
        const int chrome = height() - m_body->height();
        // QTextBrowser lays out its live document lazily before the first show.
        // Measure a plain document at the actual reading width instead.
        m_body->ensurePolished();
        QTextDocument measured;
        measured.setDefaultFont(m_body->font());
        measured.setDefaultTextOption(m_body->document()->defaultTextOption());
        measured.setDocumentMargin(m_body->document()->documentMargin());
        measured.setPlainText(text);
        measured.setTextWidth(qMax(1, m_body->width() - 2 * m_body->frameWidth()));
        const int content = qCeil(measured.size().height()) + dp(4);
        resize(width(), qMin(available.height(), chrome + qBound(dp(56), content, dp(320))));
    }

    void reject() override
    {
        if (m_escape != QDialogButtonBox::NoButton) done(int(m_escape));
    }

protected:
    void changeEvent(QEvent *event) override
    {
        QVLCDialog::changeEvent(event);
        if (event->type() == QEvent::LanguageChange)
            QTimer::singleShot(0, this, [this] {
                // The standard button box retranslates its children first.
                updateNamedActions();
                updateMetrics();
            });
    }
    void resizeEvent(QResizeEvent *event) override
    {
        QVLCDialog::resizeEvent(event);
        updateActions();
    }
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_body && (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress))
        {
            auto *key = static_cast<QKeyEvent*>(event);
            if (key->matches(QKeySequence::Copy) || key->matches(QKeySequence::SelectAll))
            {
                if (event->type() == QEvent::KeyPress)
                {
                    if (key->matches(QKeySequence::Copy)) m_body->copy(); else m_body->selectAll();
                }
                key->accept();
                return true;
            }
        }
        return QVLCDialog::eventFilter(watched, event);
    }
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        {
            if (!event->isAutoRepeat())
                if (auto *button = qobject_cast<QAbstractButton*>(focusWidget())) button->click();
            event->accept();
        }
        else QVLCDialog::keyPressEvent(event);
    }

private:
    void updateNamedActions()
    {
        if (m_acceptText.isEmpty()) return;
        if (auto *button = m_buttons->button(QDialogButtonBox::Yes)) button->setText(m_acceptText);
        if (auto *button = m_buttons->button(QDialogButtonBox::No)) button->setText(qtr("Cancel"));
    }
    int dp(int value) const { return qRound(value * (m_ctx ? m_ctx->getIntfScaleFactor() : 1)); }
    void updateActions()
    {
        m_buttons->setOrientation(Qt::Horizontal);
        const int available = width() - 2 * dp(24);
        const int copyWidth = qMax(m_copy->minimumWidth(), m_copy->sizeHint().width());
        const bool vertical = copyWidth + dp(12) + m_buttons->sizeHint().width() > available;
        m_buttons->setOrientation(vertical ? Qt::Vertical : Qt::Horizontal);
        int minimum = 0;
        for (auto *button : m_buttons->buttons()) minimum = qMax(minimum, button->sizeHint().width());
        const bool stackCopy = vertical && copyWidth + dp(12) + minimum > available;
        m_actions->setDirection(stackCopy ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        m_buttons->setMinimumWidth(vertical ? minimum : 0);
        m_footer->setMinimumWidth(qMax(minimum, copyWidth));
    }
    void updateMetrics()
    {
        layout()->setContentsMargins(dp(24), dp(24), dp(24), dp(24));
        layout()->setSpacing(dp(12));
        setMinimumWidth(dp(360));
        m_body->setMinimumSize(dp(240), dp(56));
        m_body->document()->setDocumentMargin(dp(12));
        m_actions->setSpacing(dp(12));
        for (auto *button : m_buttons->buttons()) button->setIcon({});
        m_copy->ensurePolished();
        const auto copy = qtr("Copy"), copied = qtr("Copied.");
        const auto metrics = m_copy->fontMetrics();
        const auto copySize = metrics.size(Qt::TextShowMnemonic, copy);
        const auto copiedSize = metrics.size(Qt::TextShowMnemonic, copied);
        QStyleOptionButton option;
        option.initFrom(m_copy);
        option.text = copySize.width() > copiedSize.width() ? copy : copied;
        m_copy->setMinimumWidth(m_copy->style()->sizeFromContents(QStyle::CT_PushButton, &option,
            copySize.expandedTo(copiedSize), m_copy).width());
        m_icon->setVisible(m_iconType != QMessageBox::NoIcon);
        if (m_iconType != QMessageBox::NoIcon)
        {
            QIcon icon;
            if (m_iconType == QMessageBox::Question || m_iconType == QMessageBox::Information)
                icon = ColorizedSvgIcon(m_iconType == QMessageBox::Question ? QStringLiteral(":/menu/help.svg") : QStringLiteral(":/menu/info.svg"), {}, {}, {},
                    {{QStringLiteral("#ff950d"), palette().color(QPalette::Highlight).name()}, {QStringLiteral("#fafafa"), palette().color(QPalette::Window).name()}});
            else icon = style()->standardIcon(m_iconType == QMessageBox::Warning ? QStyle::SP_MessageBoxWarning : QStyle::SP_MessageBoxCritical);
            m_icon->setPixmap(icon.pixmap(QSize(dp(32), dp(32)), devicePixelRatioF()));
        }
        updateActions();
        if (screen()) resize(size().boundedTo(screen()->availableGeometry().size() - QSize(dp(32), dp(32))));
    }
    QPointer<MainCtx> m_ctx;
    QString m_text;
    QString m_acceptText;
    QMessageBox::Icon m_iconType;
    QLabel *m_icon;
    QTextBrowser *m_body;
    QWidget *m_footer;
    QBoxLayout *m_actions;
    QDialogButtonBox *m_buttons;
    QPushButton *m_copy;
    QTimer *m_copyTimer;
    QDialogButtonBox::StandardButton m_escape = QDialogButtonBox::NoButton;
};
}

#define I_OP_DIR_WINTITLE I_DIR_OR_FOLDER( N_("Open Directory"), \
                                           N_("Open Folder") )

#define BASE_DIALOG(X) \
    X(prefsDialog) \
    X(extendedDialog) \
    X(messagesDialog) \
    X(vlmDialog) \
    X(helpDialog) \
    X(aboutDialog) \
    X(mediaInfoDialog) \
    X(bookmarkDialog) \
    X(podcastDialog) \
    X(pluginDialog)\
    X(egpDialog)


#ifdef UPDATE_CHECK
#define ALL_DIALOG(X) \
    BASE_DIALOG(X) \
    X(updateDialog)
#else
#define ALL_DIALOG(X) BASE_DIALOG(X)
#endif

DialogsProvider::DialogsProvider( qt_intf_t *_p_intf )
    : QObject( NULL ), p_intf( _p_intf ), m_parser( nullptr, &input_item_parser_id_Release )

{
    b_isDying = false;
}

DialogsProvider::~DialogsProvider()
{
    m_openDialog.reset();
    m_discOpenDialog.reset();
    m_captureOpenDialog.reset();
    /* free parentless menus  */
    VLCMenuBar::freeRendererMenu();
}

QString DialogsProvider::getSaveFileName( QWidget *parent,
                                          const QString &caption,
                                          const QUrl &dir,
                                          const QString &filter,
                                          QString *selectedFilter )
{
    const QStringList schemes = QStringList(QStringLiteral("file"));
    return QFileDialog::getSaveFileUrl( parent, caption, dir, filter, selectedFilter, QFileDialog::Options(), schemes).toLocalFile();
}

QStringList DialogsProvider::getMediaListDialog(const QString &text)
{
    NativeMediaListDialog dialog(p_intf, text);
    return dialog.exec() == QDialog::Accepted ? dialog.items() : QStringList();
}

QVariant DialogsProvider::getTextDialog(QWidget *parent,
                                        const QString &title,
                                        const QString &label,
                                        const QString &placeholder,
                                        bool *ok)
{
    QInputDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("redesignTextInput"));
    dialog.setWindowRole("vlc-text-input");
    dialog.setWindowTitle(title);
    dialog.setInputMode(QInputDialog::TextInput);
    dialog.setLabelText(label);
    dialog.setTextEchoMode(QLineEdit::Normal);
    dialog.setTextValue(placeholder);
    // Create the actions before applying the theme and measuring the form.
    dialog.setOkButtonText(qtr("OK"));
    dialog.setCancelButtonText(qtr("Cancel"));
    for (auto *caption : dialog.findChildren<QLabel*>())
    {
        caption->setTextFormat(Qt::PlainText);
        // Keep the input's label buddy, without interpreting data as mnemonics.
        caption->setText(QString(label).replace(QStringLiteral("&"), QStringLiteral("&&")));
        caption->setWordWrap(true);
    }
    QVLCDialog::setWindowTransientParent(&dialog, parent ? parent->window()->windowHandle() : nullptr, p_intf);
    applyRedesignWidgetTheme(&dialog, p_intf->p_mi);
    const auto metrics = [&dialog, ctx = QPointer<MainCtx>(p_intf->p_mi)] {
        const auto dp = [ctx](int value) { return qRound(value * (ctx ? ctx->getIntfScaleFactor() : 1)); };
        if (auto *layout = dialog.layout())
        {
            layout->setContentsMargins(dp(24), dp(24), dp(24), dp(24));
            layout->setSpacing(dp(12));
        }
        dialog.setMinimumWidth(dp(360));
        for (auto *button : dialog.findChildren<QPushButton*>()) button->setIcon({});
        if (dialog.screen()) dialog.resize(dialog.size().boundedTo(dialog.screen()->availableGeometry().size() - QSize(dp(32), dp(32))));
    };
    if (auto *ctx = p_intf->p_mi)
    {
        connect(ctx, &MainCtx::intfScaleFactorChanged, &dialog, metrics);
        connect(ctx->getColorScheme(), &ColorSchemeModel::currentChanged, &dialog, metrics);
    }
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, &dialog, [&dialog, metrics] {
            dialog.setOkButtonText(qtr("OK"));
            dialog.setCancelButtonText(qtr("Cancel"));
            metrics();
        });
    metrics();
    QSize preferred(qRound(480 * (p_intf->p_mi ? p_intf->p_mi->getIntfScaleFactor() : 1)), dialog.sizeHint().height());
    if (dialog.screen()) preferred = preferred.boundedTo(dialog.screen()->availableGeometry().size());
    dialog.resize(preferred);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    const QString ret = accepted ? dialog.textValue() : QString();
    if (ok) *ok = accepted;

    if (!ok)
    {
        // When this function is called from the QML side, instead of setting `ok` parameter
        // a QVariantMap with key `ok` and `text` is returned instead

        QVariantMap map;
        map["text"] = ret;
        map["ok"] = accepted;
        return map;
    }
    else
    {
        return ret;
    }
}

void DialogsProvider::quit()
{
    if (b_isDying)
        return;
    b_isDying = true;
    assert(qApp);
    const QPointer<DialogsProvider> alive(this);
    // The rest of the quit can wait for playback, a network read or add-on
    // work to stop. Do not show a frozen window meanwhile.
    for (QWindow* window : QGuiApplication::topLevelWindows())
        window->hide();
    // A real quit stops playback before joined add-on cleanup. Window-close
    // to the system tray does not call this path.
    if(p_intf->p_mi) p_intf->p_mi->addonStreamLifetime().close();
    if(p_intf->p_mainPlaylistController) p_intf->p_mainPlaylistController->stop();
    if(!alive) return;
    qApp->setProperty("isDying", true);
    if(!alive) return;
    if(p_intf->p_mi)
    {
        const bool accepted=p_intf->p_mi->requestAddonRetirement([intf=p_intf] {
            libvlc_Quit(vlc_object_instance(intf));
        });
        if(!alive) return;
        if(!accepted)
        {
            if(!m_quitRetryReported)
            {
                m_quitRetryReported=true;
                msg_Err(p_intf, "The app cannot stop add-on work.");
            }
            b_isDying=false;
            qApp->setProperty("isDying",false);
            if(!alive) return;
            if(!m_quitRetryScheduled)
            {
                m_quitRetryScheduled=true;
                QTimer::singleShot(100,this,[this] { m_quitRetryScheduled=false; quit(); });
            }
        }
        return;
    }
    libvlc_Quit( vlc_object_instance(p_intf) );
}

void DialogsProvider::customEvent( QEvent *event )
{
    if( event->type() == DialogEvent::DialogEvent_Type )
    {
        DialogEvent *de = static_cast<DialogEvent*>(event);
        switch( de->i_dialog )
        {
        case INTF_DIALOG_FILE_SIMPLE:
        case INTF_DIALOG_FILE:
            openDialog(); break;
        case INTF_DIALOG_FILE_GENERIC:
            openFileGenericDialog( de->p_arg ); break;
        case INTF_DIALOG_DISC:
            openDiscDialog(); break;
        case INTF_DIALOG_NET:
            openNetDialog(); break;
        case INTF_DIALOG_SAT:
        case INTF_DIALOG_CAPTURE:
            openCaptureDialog(); break;
        case INTF_DIALOG_DIRECTORY:
            PLAppendDir(); break;
        case INTF_DIALOG_PLAYLIST:
            //FIXME
            //playlistDialog(); break;
            break;
        // case INTF_DIALOG_PLAYLISTS:
        // FIXME: `intf_dialog_args_t` is not supported by `PlaylistsDialog`.
        case INTF_DIALOG_MESSAGES:
            messagesDialog(); break;
        case INTF_DIALOG_FILEINFO:
           mediaInfoDialog(); break;
        case INTF_DIALOG_PREFS:
           prefsDialog(); break;
        case INTF_DIALOG_BOOKMARKS:
           bookmarksDialog(); break;
        case INTF_DIALOG_EXTENDED:
           extendedDialog(); break;
        case INTF_DIALOG_SENDKEY:
           sendKey( de->i_arg ); break;
#ifdef ENABLE_VLM
        case INTF_DIALOG_VLM:
           vlmDialog(); break;
#endif
        case INTF_DIALOG_POPUPMENU:
        {
           popupMenu.reset();
           bool show = (de->i_arg != 0);
           if( show )
           {
              //popping a QMenu prevents mouse release events to be received,
              //this ensures the coherency of the vout mouse state.
              emit releaseMouseEvents();
              popupMenu.reset(VLCMenuBar::PopupMenu( p_intf, true ));
           }
           break;
        }
        case INTF_DIALOG_AUDIOPOPUPMENU:
        {
           audioPopupMenu.reset();
           bool show = (de->i_arg != 0);
           if( show )
               audioPopupMenu.reset(VLCMenuBar::AudioPopupMenu( p_intf, show ));
           break;
        }
        case INTF_DIALOG_VIDEOPOPUPMENU:
        {
           videoPopupMenu.reset();
           bool show = (de->i_arg != 0);
           if( show )
               videoPopupMenu.reset(VLCMenuBar::VideoPopupMenu( p_intf, show ));
           break;
        }
        case INTF_DIALOG_MISCPOPUPMENU:
        {
           miscPopupMenu.reset();
           bool show = (de->i_arg != 0);
           if( show )
               miscPopupMenu.reset(VLCMenuBar::MiscPopupMenu( p_intf, show ));
           break;
        }
        case INTF_DIALOG_WIZARD:
        case INTF_DIALOG_STREAMWIZARD:
            openAndStreamingDialogs(); break;
#ifdef UPDATE_CHECK
        case INTF_DIALOG_UPDATEVLC:
            updateDialog(); break;
#endif
        case INTF_DIALOG_EXIT:
            quit(); break;
        default:
           msg_Warn( p_intf, "unimplemented dialog" );
        }
    }
}

bool DialogsProvider::eventFilter(QObject *watched, QEvent *event) {
    assert(event);
    assert(watched);

#define CASE_DIALOG(dialog) \
    if (watched == m_ ## dialog .get()) { \
        emit dialog ## VisibleChanged(); \
        break;\
    }

    switch (event->type()) {
    case QEvent::Hide:
    case QEvent::Show:
    case QEvent::Close:
    case QEvent::Destroy:
    {
        if (watched == m_openDialog.get() || watched == m_discOpenDialog.get()
            || watched == m_captureOpenDialog.get())
        {
            emit openDialogVisibleChanged();
            break;
        }
        if (watched == m_gotoTimeDialog.get()) {
            emit gotoTimeDialogVisibleChanged();
            break;
        }
        ALL_DIALOG(CASE_DIALOG)
        break;
    }
    default:
        break;
    }
    return false;
}


/****************************************************************************
 * Individual simple dialogs
 ****************************************************************************/
const QEvent::Type DialogEvent::DialogEvent_Type =
        (QEvent::Type)QEvent::registerEventType();

void DialogsProvider::prefsDialog()
{
    if (p_intf->p_mi) {
        emit showRedesignPreferences();
        return;
    }
    if (Q_LIKELY(!m_prefsDialog))
    {
        m_prefsDialog = new PrefsDialog( nullptr, p_intf );
        m_prefsDialog->setAttribute(Qt::WA_DeleteOnClose);
        m_prefsDialog->installEventFilter(this);
        m_prefsDialog->open();
    }
    else
    {
        m_prefsDialog->reject();
    }
}

void DialogsProvider::prefsPageDialog(const QString& page)
{
    if (p_intf->p_mi)
        emit showRedesignPreferencesPage(page);
    else
        prefsDialog();
}

void DialogsProvider::firstRunDialog()
{
    if (m_firstRunDialogVisible) return;
    m_firstRunDialogVisible = true;
    emit firstRunDialogVisibleChanged();
}

void DialogsProvider::extendedDialog()
{
    ensureDialog(m_extendedDialog);
    if( !m_extendedDialog->isVisible() || /* Hidden */
        m_extendedDialog->currentTab() != 0 )  /* wrong tab */
        m_extendedDialog->showTab( 0 );
    else
        m_extendedDialog->hide();
}

void DialogsProvider::synchroDialog()
{
    ensureDialog(m_extendedDialog);
    if( !m_extendedDialog->isVisible() || /* Hidden */
        m_extendedDialog->currentTab() != 2 )  /* wrong tab */
        m_extendedDialog->showTab( 2 );
    else
        m_extendedDialog->hide();
}

void DialogsProvider::messagesDialog(int page)
{
    ensureDialog(m_messagesDialog);
    if(!m_messagesDialog->isVisible() || page)
        m_messagesDialog->showTab(page);
    else
        m_messagesDialog->toggleVisible();
}

void DialogsProvider::gotoTimeDialog()
{
    if (p_intf->p_mi) {
        emit showRedesignGotoTime();
        return;
    }
    toggleDialogVisible(m_gotoTimeDialog);
}

bool DialogsProvider::gotoTimeDialogVisible() const
{
    return m_redesignGotoTimeVisible || (m_gotoTimeDialog && m_gotoTimeDialog->isVisible());
}

void DialogsProvider::setRedesignGotoTimeVisible(bool visible)
{
    if (m_redesignGotoTimeVisible == visible)
        return;
    m_redesignGotoTimeVisible = visible;
    emit gotoTimeDialogVisibleChanged();
}

#ifdef ENABLE_VLM
void DialogsProvider::vlmDialog()
{
    toggleDialogVisible(m_vlmDialog);
}
#endif

void DialogsProvider::helpDialog()
{
    toggleDialogVisible(m_helpDialog);
}

#ifdef UPDATE_CHECK
void DialogsProvider::updateDialog(Mode mode)
{
    ensureDialog(m_updateDialog);
    assert(m_updateDialog);

    switch (mode)
    {
    case Mode::Show:
        m_updateDialog->present();
        break;
    case Mode::Hide:
        m_updateDialog->hide();
        break;
    case Mode::Toggle:
    default:
        if (m_updateDialog->isVisible()) m_updateDialog->hide();
        else m_updateDialog->present(true);
    };
}
#endif

void DialogsProvider::aboutDialog()
{
    toggleDialogVisible(m_aboutDialog);
}

void DialogsProvider::mediaInfoDialog( void )
{
    ensureDialog(m_mediaInfoDialog);
    if( !m_mediaInfoDialog->isVisible() || m_mediaInfoDialog->currentTab() != MediaInfoDialog::META_PANEL )
        m_mediaInfoDialog->showTab( MediaInfoDialog::META_PANEL );
    else
        m_mediaInfoDialog->hide();
}

void DialogsProvider::mediaInfoDialog( const SharedInputItem& inputItem )
{
    assert(inputItem.get());

    MediaInfoDialog * const mid = new MediaInfoDialog( p_intf, inputItem );
    mid->setWindowFlag( Qt::Dialog );
    mid->setAttribute( Qt::WA_DeleteOnClose );
    mid->showTab( MediaInfoDialog::META_PANEL );
}

void DialogsProvider::mediaInfoDialog( const PlaylistItem& pItem )
{
    input_item_t *p_input = nullptr;

    vlc_playlist_item_t * const playlistItem = pItem.raw();

    if( playlistItem )
    {
        p_input = vlc_playlist_item_GetMedia(playlistItem);
    }

    if( p_input )
    {
        mediaInfoDialog( SharedInputItem{ p_input } );
    }
}

void DialogsProvider::mediaInfoDialog( const MLItemId& itemId )
{
    assert( p_intf );

    vlc_medialibrary_t* const ml = vlc_ml_instance_get( p_intf );
    assert( ml );

    if (!itemId.isValid() || itemId.hasParent()) return;
    input_item_t * const inputItem = vlc_ml_get_input_item( ml, itemId.id );
    if (!inputItem) return;

    const SharedInputItem sharedInputItem { inputItem, false };
    if ( input_item_IsPreparsed( inputItem ) )
    {
        mediaInfoDialog( sharedInputItem );
    }
    else
    {
        static const input_item_parser_cbs_t cbs = {
            // on_preparse_ended
            []( input_item_t * const item, int status, void * const userData ) {
                const auto dp = static_cast<DialogsProvider *>( userData );

                if ( status != VLC_SUCCESS )
                    qWarning( "Could not preparse input item %p. Status %i", item, status );

                const SharedInputItem sharedInputItem{ item };

                QMetaObject::invokeMethod( dp, [dp, sharedInputItem]() {
                        dp->m_parser.reset();
                        dp->mediaInfoDialog( sharedInputItem );
                    }, Qt::QueuedConnection );
            },
            // on_subtree_added
            NULL,
            // on_attachments_added
            NULL,
        };

        const struct input_item_parser_cfg cfg = {
            .cbs = &cbs,
            .cbs_data = this,
            .subitems = true,
            .interact = false,
        };

        m_parser = vlc::wrap_cptr( input_item_Parse( VLC_OBJECT( p_intf ),
                                   inputItem, &cfg ),
                                   &input_item_parser_id_Release );
    }
}

bool DialogsProvider::questionDialog(const QString& text, const QString &title, const QString &acceptText) const
{
    NativeMessageDialog dialog(p_intf, text, title, QMessageBox::Yes | QMessageBox::No,
                               QMessageBox::No, QMessageBox::Question, acceptText);
    int result = dialog.exec();
    return result == QMessageBox::Yes;
}

void DialogsProvider::mediaCodecDialog()
{
    ensureDialog(m_mediaInfoDialog);

    if( !m_mediaInfoDialog->isVisible() || m_mediaInfoDialog->currentTab() != MediaInfoDialog::INFO_PANEL )
        m_mediaInfoDialog->showTab( MediaInfoDialog::INFO_PANEL );
    else
        m_mediaInfoDialog->hide();
}

QMessageBox::StandardButton DialogsProvider::messageDialog(const QString &text,
                                                           const QString &title,
                                                           const QMessageBox::StandardButtons buttons,
                                                           const QMessageBox::StandardButton defaultButton,
                                                           const QMessageBox::Icon icon,
                                                           QWidget *owner, const QString &acceptText)
{
    NativeMessageDialog dialog(p_intf, text, title, buttons, defaultButton, icon, acceptText, owner);
    return static_cast<QMessageBox::StandardButton>(dialog.exec());
}

void DialogsProvider::playlistsDialog( const QVariantList & medias, MLPlaylistListModel::PlaylistType type )
{
    if (medias.isEmpty()) return;
    if (!p_intf->p_mi || !p_intf->p_mi->hasMediaLibrary()) {
        messageDialog(qtr("The media library is not available."), qtr("Add to Playlist"),
                      QMessageBox::Close, QMessageBox::Close, QMessageBox::Information);
        return;
    }
    emit showRedesignAddToPlaylist(medias, type);
}

void DialogsProvider::bookmarksDialog()
{
    if (p_intf->p_mi && p_intf->p_mi->hasMediaLibrary())
    {
        emit showRedesignBookmarks();
        return;
    }
    bookmarkDetailsDialog();
}

void DialogsProvider::bookmarkDetailsDialog()
{
    toggleDialogVisible(m_bookmarkDialog);
}

MLBookmarkModel* DialogsProvider::bookmarkModel()
{
    if (!p_intf->p_mi || !p_intf->p_mi->hasMediaLibrary())
        return nullptr;
    if (!m_bookmarkModel)
    {
        m_bookmarkModel = std::make_unique<MLBookmarkModel>();
        m_bookmarkModel->setPlayer(THEMIM);
        m_bookmarkModel->sort(1, Qt::AscendingOrder);
        m_bookmarkModel->setMl(p_intf->p_mi->getMediaLibrary());
    }
    return m_bookmarkModel.get();
}

void DialogsProvider::addBookmark()
{
    if (auto *model = bookmarkModel())
        model->add();
}

void DialogsProvider::podcastConfigureDialog()
{
    toggleDialogVisible(m_podcastDialog);
}

void DialogsProvider::pluginDialog()
{
    toggleDialogVisible(m_pluginDialog);
}

void DialogsProvider::epgDialog()
{
    toggleDialogVisible(m_egpDialog);
}

void DialogsProvider::setPopupMenu()
{
    popupPlayerMenu(QCursor::pos());
}

void DialogsProvider::popupPlayerMenu(const QPoint& position)
{
    emit releaseMouseEvents();
    popupMenu.reset(VLCMenuBar::PopupMenu(p_intf, false));
    if (popupMenu) popupMenu->popup(position);
}

void DialogsProvider::destroyPopupMenu()
{
    popupMenu.reset();
}

/* Generic open file */
void DialogsProvider::openFileGenericDialog( intf_dialog_args_t *p_arg )
{
    if( p_arg == NULL )
    {
        msg_Warn( p_intf, "openFileGenericDialog() called with NULL arg" );
        return;
    }

    /* Replace the extensions to a Qt format */
    int i = 0;
    QString extensions = qfu( p_arg->psz_extensions );
    while ( ( i = extensions.indexOf( "|", i ) ) != -1 )
    {
        if( ( extensions.count( "|" ) % 2 ) == 0 )
            extensions.replace( i, 1, ");;" );
        else
            extensions.replace( i, 1, "(" );
    }
    extensions.replace( ";*", " *" );
    extensions.append( ")" );

    /* Save */
    if( p_arg->b_save )
    {
        QString file = getSaveFileName( NULL,
                                        qfu( p_arg->psz_title ),
                                        p_intf->p_mi->getDialogFilePath(), extensions );
        if( !file.isEmpty() )
        {
            p_arg->i_results = 1;
            p_arg->psz_results = (char **)vlc_alloc( p_arg->i_results, sizeof( char * ) );
            p_arg->psz_results[0] = strdup( qtu( toNativeSepNoSlash( std::move(file) ) ) );
        }
        else
            p_arg->i_results = 0;
    }
    else /* non-save mode */
    {
        QList<QUrl> urls = QFileDialog::getOpenFileUrls( NULL, qfu( p_arg->psz_title ),
                                       p_intf->p_mi->getDialogFilePath(), extensions );
        p_arg->i_results = urls.count();
        p_arg->psz_results = (char **)vlc_alloc( p_arg->i_results, sizeof( char * ) );
        i = 0;
        foreach( const QUrl &uri, urls )
            p_arg->psz_results[i++] = strdup( uri.toEncoded().constData() );
        if( !urls.isEmpty() )
            p_intf->p_mi->setDialogFilePath(urls.last());
    }

    /* Callback */
    if( p_arg->pf_callback )
        p_arg->pf_callback( p_arg );

    /* Clean afterwards */
    if( p_arg->psz_results )
    {
        for( i = 0; i < p_arg->i_results; i++ )
            free( p_arg->psz_results[i] );
        free( p_arg->psz_results );
    }
    free( p_arg->psz_title );
    free( p_arg->psz_extensions );
    free( p_arg );
}
/****************************************************************************
 * All the open/add stuff
 * Open Dialog first - Simple Open then
 ****************************************************************************/

void DialogsProvider::openDialog( OpenDialog::OpenTab i_tab )
{
    ensureDialog(m_openDialog);
    m_openDialog->showTab( i_tab, OpenDialog::OPEN_AND_PLAY );
}
void DialogsProvider::openDialog()
{
    openDialog( OpenDialog::OPEN_FILE_TAB );
}
void DialogsProvider::openFileDialog()
{
    openDialog( OpenDialog::OPEN_FILE_TAB );
}
void DialogsProvider::openDiscDialog()
{
    ensureDialog(m_discOpenDialog, nullptr, OpenDialog::OPEN_DISC_TAB);
    m_discOpenDialog->showTab(OpenDialog::OPEN_DISC_TAB, OpenDialog::OPEN_AND_PLAY);
}
void DialogsProvider::openNetDialog()
{
    if (p_intf->p_mi) {
        emit showRedesignNetworkStreams();
        return;
    }
    openDialog( OpenDialog::OPEN_NETWORK_TAB );
}
void DialogsProvider::openCaptureDialog()
{
    ensureDialog(m_captureOpenDialog, nullptr, OpenDialog::OPEN_CAPTURE_TAB);
    m_captureOpenDialog->showTab(OpenDialog::OPEN_CAPTURE_TAB, OpenDialog::OPEN_AND_PLAY);
}

/* Same as the open one, but force the enqueue */
void DialogsProvider::PLAppendDialog( OpenDialog::OpenTab tab )
{
    ensureDialog(m_openDialog);
    m_openDialog->showTab( tab, OpenDialog::OPEN_AND_ENQUEUE );
}

/**
 * Simple open
 ***/
QStringList DialogsProvider::showSimpleOpen( const QString& help,
                                             int filters,
                                             const QUrl& path )
{
    const QPointer<MainCtx> context = p_intf->p_mi;
    const bool shortcuts = context->getUseGlobalShortcuts();
    context->setUseGlobalShortcuts(false);
    const auto restoreShortcuts = qScopeGuard([context, shortcuts] {
        if (context) context->setUseGlobalShortcuts(shortcuts);
    });
    QString fileTypes = "";
    if( filters & EXT_FILTER_MEDIA ) {
        ADD_EXT_FILTER( fileTypes, EXTENSIONS_MEDIA );
    }
    if( filters & EXT_FILTER_VIDEO ) {
        ADD_EXT_FILTER( fileTypes, EXTENSIONS_VIDEO );
    }
    if( filters & EXT_FILTER_AUDIO ) {
        ADD_EXT_FILTER( fileTypes, EXTENSIONS_AUDIO );
    }
    if( filters & EXT_FILTER_PLAYLIST ) {
        ADD_EXT_FILTER( fileTypes, EXTENSIONS_PLAYLIST );
    }
    if( filters & EXT_FILTER_SUBTITLE ) {
        ADD_EXT_FILTER( fileTypes, EXTENSIONS_SUBTITLE );
    }
    ADD_EXT_FILTER( fileTypes, EXTENSIONS_ALL );
    fileTypes.replace( ";*", " *");
    fileTypes.chop(2); //remove trailing ";;"

    QFileDialog dialog(nullptr, help.isEmpty() ? qfut(I_OP_SEL_FILES) : help);
    dialog.setFileMode(QFileDialog::ExistingFiles);
    const QUrl initial = path.isEmpty() ? p_intf->p_mi->getDialogFilePath() : path;
    if (initial.isLocalFile() && QFileInfo(initial.toLocalFile()).isFile()) {
        dialog.setDirectoryUrl(initial.adjusted(QUrl::RemoveFilename));
        if (!path.isEmpty()) dialog.selectUrl(initial);
    } else {
        dialog.setDirectoryUrl(initial);
    }
    dialog.setNameFilter(fileTypes);
    if (auto *buttons = dialog.findChild<QDialogButtonBox*>())
        if (auto *open = buttons->button(QDialogButtonBox::Open))
            open->setProperty("redesignPrimary", true);
    applyRedesignWidgetTheme(&dialog, p_intf->p_mi);
    for (auto *combo : dialog.findChildren<QComboBox*>()) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(16);
    }
    const auto dp = [this](int value) { return qRound(value * p_intf->p_mi->getIntfScaleFactor()); };
    if (auto *sidebar = dialog.findChild<QListView*>(QStringLiteral("sidebar")))
        sidebar->setMinimumWidth(dp(160));
    QVLCDialog::setWindowTransientParent(&dialog, nullptr, p_intf);
    const auto available = dialog.screen()->availableGeometry().size();
    dialog.resize(std::min(dp(800), available.width() - 40), std::min(dp(560), available.height() - 40));
    const QList<QUrl> urls = dialog.exec() == QDialog::Accepted ? dialog.selectedUrls() : QList<QUrl>();

    if( !urls.isEmpty() )
        p_intf->p_mi->setDialogFilePath(urls.last());

    QStringList res;
    foreach( const QUrl &url, urls )
        res << url.toEncoded();

    return res;
}

void DialogsProvider::simpleOpenDialog(bool start)
{
    QStringList urls = DialogsProvider::showSimpleOpen();

    urls.sort();
    QVector<vlc::playlist::Media> medias;
    for( const QString& mrl : urls)
        medias.push_back( vlc::playlist::Media{mrl, QString {}} );
    if (!medias.empty())
        THEMPL->append(medias, start);
}

/* Url & Clipboard */
/**
 * Open a MRL.
 * If the clipboard contains URLs, the first is automatically 'preselected'.
 **/
void DialogsProvider::openUrlDialog()
{
    OpenUrlDialog oud( p_intf );
    if( oud.exec() != QDialog::Accepted )
        return;

    QString url = oud.url();
    if( url.isEmpty() )
        return;

    char *uri;
    if( !url.contains( qfu( "://" ) ) )
    {
        uri = vlc_path2uri( qtu( url ), NULL );
    } else {
        uri = vlc_uri_fixup( qtu( url ) );
    }

    if( uri == NULL )
        return;
    url = qfu(uri);
    free( uri );

    QVector<vlc::playlist::Media> medias = { {url, QString {}} };
    THEMPL->append(medias, !oud.shouldEnqueue());
}

/* Directory */
/**
 * Open a directory,
 * pl helps you to choose from playlist or media library,
 * go to start or enqueue
 **/
static void openDirectory( qt_intf_t *p_intf, bool go )
{
    QString uri = DialogsProvider::getDirectoryDialog( p_intf );
    if( !uri.isEmpty() )
    {
        QVector<vlc::playlist::Media> medias = { {uri, QString {}} };
        THEMPL->append(medias, go);
    }
}

QStringList DialogsProvider::mediaNameFilters(bool audio) const
{
    return (audio ? QStringLiteral(EXTENSIONS_AUDIO)
                  : QStringLiteral(EXTENSIONS_VIDEO)).split(QLatin1Char(';'));
}

QUrl DialogsProvider::selectLibraryFolder(const QUrl& initial)
{
    const QPointer<MainCtx> context = p_intf->p_mi;
    if (!context) return {};
    const bool shortcuts = context->getUseGlobalShortcuts();
    context->setUseGlobalShortcuts(false);
    const auto restoreShortcuts = qScopeGuard([context, shortcuts] {
        if (context) context->setUseGlobalShortcuts(shortcuts);
    });
    QFileDialog dialog(nullptr, qtr("Add folder to media library"));
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly);
    dialog.setSupportedSchemes({QStringLiteral("file")});
    dialog.setDirectoryUrl(initial.isEmpty() ? context->getDialogFilePath() : initial);
    if (auto *buttons = dialog.findChild<QDialogButtonBox*>())
        if (auto *open = buttons->button(QDialogButtonBox::Open))
            open->setProperty("redesignPrimary", true);
    applyRedesignWidgetTheme(&dialog, context);
    for (auto *combo : dialog.findChildren<QComboBox*>()) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(16);
    }
    QVLCDialog::setWindowTransientParent(&dialog, nullptr, p_intf);
    const auto available = dialog.screen()->availableGeometry().size();
    dialog.resize(std::min(qRound(800 * context->getIntfScaleFactor()), available.width() - 40),
                  std::min(qRound(560 * context->getIntfScaleFactor()), available.height() - 40));
    if (dialog.exec() != QDialog::Accepted || !context || dialog.selectedUrls().isEmpty()) return {};
    const auto folder = dialog.selectedUrls().first();
    if (!folder.isLocalFile()) return {};
    context->setDialogFilePath(folder);
    return folder;
}

QUrl DialogsProvider::recoverLibraryFolder(const QUrl& folder)
{
    const QPointer<MainCtx> context = p_intf->p_mi;
    if (!context || folder.isEmpty() || !folder.isValid()) return {};
    const bool shortcuts = context->getUseGlobalShortcuts();
    context->setUseGlobalShortcuts(false);
    const auto restoreShortcuts = qScopeGuard([context, shortcuts] {
        if (context) context->setUseGlobalShortcuts(shortcuts);
    });
    const QString path = folder.isLocalFile() ? toNativeSeparators(folder.toLocalFile())
        : folder.toDisplayString(QUrl::RemovePassword);
    const auto answer = messageDialog(
        qtr("Hakone cannot add this folder.\nMake sure that you can read the folder.\n\n%1").arg(path),
        qtr("Add folder to media library"), QMessageBox::Retry | QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel, QMessageBox::Warning, nullptr, qtr("Select folder"));
    if (!context) return {};
    if (answer == QMessageBox::Retry) return folder;
    if (answer == QMessageBox::Yes) return selectLibraryFolder(folder);
    return {};
}

QStringList DialogsProvider::selectPlaylistFiles()
{
    return showSimpleOpen(qtr("Select files for playlist"));
}

QStringList DialogsProvider::selectLibraryFiles(const QUrl& initial)
{
    return showSimpleOpen(qtr("Add files to media library"), EXT_FILTER_MEDIA, initial);
}

QUrl DialogsProvider::selectPlaylistCover()
{
    return selectMediaArtwork();
}

QUrl DialogsProvider::selectMediaArtwork()
{
    const QPointer<MainCtx> context = p_intf->p_mi;
    if (!context) return {};
    const bool shortcuts = context->getUseGlobalShortcuts();
    context->setUseGlobalShortcuts(false);
    const auto restoreShortcuts = qScopeGuard([context, shortcuts] {
        if (context) context->setUseGlobalShortcuts(shortcuts);
    });
    QFileDialog dialog(nullptr, qtr("Select artwork"));
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setNameFilter(qtr("Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif);;All files (*)"));
    auto directory = context->getDialogFilePath();
    if (directory.isLocalFile() && QFileInfo(directory.toLocalFile()).isFile())
        directory = directory.adjusted(QUrl::RemoveFilename);
    dialog.setDirectoryUrl(directory);
    if (auto *buttons = dialog.findChild<QDialogButtonBox*>())
        if (auto *open = buttons->button(QDialogButtonBox::Open)) open->setProperty("redesignPrimary", true);
    applyRedesignWidgetTheme(&dialog, context);
    for (auto *combo : dialog.findChildren<QComboBox*>()) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(16);
    }
    QVLCDialog::setWindowTransientParent(&dialog, nullptr, p_intf);
    const auto available = dialog.screen()->availableGeometry().size();
    dialog.resize(std::min(qRound(800 * context->getIntfScaleFactor()), available.width() - 40),
                  std::min(qRound(560 * context->getIntfScaleFactor()), available.height() - 40));
    if (dialog.exec() != QDialog::Accepted || dialog.selectedUrls().isEmpty()) return {};
    const auto image = dialog.selectedUrls().first();
    if (!image.isLocalFile()) return {};
    context->setDialogFilePath(image);
    return image;
}

QString DialogsProvider::getDirectoryDialog( qt_intf_t *p_intf )
{
    const QStringList schemes = QStringList(QStringLiteral("file"));
    QUrl dirurl = QFileDialog::getExistingDirectoryUrl( NULL,
            qfut( I_OP_DIR_WINTITLE ), p_intf->p_mi->getDialogFilePath(),
            QFileDialog::ShowDirsOnly, schemes );

    if( dirurl.isEmpty() ) return QString();

    p_intf->p_mi->setDialogFilePath(dirurl);

    QString dir = dirurl.toLocalFile();
    const char *scheme = "directory";
    if( dir.endsWith( DIR_SEP "VIDEO_TS", Qt::CaseInsensitive ) )
        scheme = "dvd";
    else if( dir.endsWith( DIR_SEP "BDMV", Qt::CaseInsensitive ) )
    {
        scheme = "bluray";
        dir.remove( "BDMV" );
    }

    char *uri = vlc_path2uri( qtu( toNativeSeparators( dir ) ), scheme );
    if( unlikely(uri == NULL) )
        return QString();

    dir = qfu( uri );
    free( uri );

    return dir;
}

void DialogsProvider::PLOpenDir()
{
    openDirectory( p_intf, true );
}

void DialogsProvider::PLAppendDir()
{
    openDirectory( p_intf, false );
}

/****************
 * Playlist     *
 ****************/
void DialogsProvider::exportLibraryPlaylistDialog(const MLItemId& playlist, const QString& name)
{
    selectLibraryPlaylistExport(playlist, name);
}

void DialogsProvider::selectLibraryPlaylistExport(const MLItemId& playlist, const QString& name,
                                                 const QUrl& retryFile, const QString& extension)
{
    const QPointer<MainCtx> context = p_intf->p_mi;
    if (!context || !context->hasMediaLibrary()) return;
    const bool shortcuts = context->getUseGlobalShortcuts();
    context->setUseGlobalShortcuts(false);
    const auto restoreShortcuts = qScopeGuard([context, shortcuts] {
        if (context) context->setUseGlobalShortcuts(shortcuts);
    });
    const QStringList extensions {"xspf", "m3u", "m3u8", "html"};
    const QStringList filters {qtr("XSPF playlist (*.xspf)"), qtr("M3U playlist (*.m3u)"),
                               qtr("M3U8 playlist (*.m3u8)"), qtr("HTML playlist (*.html)")};
    QFileDialog dialog(nullptr, qtr("Export Playlist"));
    dialog.setObjectName(QStringLiteral("exportLibraryPlaylist"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setSupportedSchemes({QStringLiteral("file")});
    dialog.setNameFilters(filters);
    int preferred = extensions.indexOf(extension.isEmpty()
        ? getSettings()->value("last-playlist-ext").toString() : extension);
    if (preferred < 0) preferred = 2;
    dialog.selectNameFilter(filters[preferred]);
    dialog.setDefaultSuffix(extensions[preferred]);
    connect(&dialog, &QFileDialog::filterSelected, &dialog, [&dialog, filters, extensions](const QString& filter) {
        const int index = filters.indexOf(filter);
        if (index >= 0) dialog.setDefaultSuffix(extensions[index]);
    });
    auto directory = retryFile.isLocalFile() ? retryFile.adjusted(QUrl::RemoveFilename)
                                            : context->getDialogFilePath();
    if (directory.isLocalFile() && QFileInfo(directory.toLocalFile()).isFile())
        directory = directory.adjusted(QUrl::RemoveFilename);
    dialog.setDirectoryUrl(directory);
    QString filename = name.trimmed();
    filename.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\r\\n]")), QStringLiteral("-"));
    dialog.selectFile(retryFile.isLocalFile() ? QFileInfo(retryFile.toLocalFile()).fileName()
        : filename.isEmpty() ? qtr("Playlist") : filename);
    if (auto *buttons = dialog.findChild<QDialogButtonBox*>())
        if (auto *save = buttons->button(QDialogButtonBox::Save)) save->setProperty("redesignPrimary", true);
    applyRedesignWidgetTheme(&dialog, context);
    for (auto *combo : dialog.findChildren<QComboBox*>()) {
        combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        combo->setMinimumContentsLength(16);
    }
    QVLCDialog::setWindowTransientParent(&dialog, nullptr, p_intf);
    const auto available = dialog.screen()->availableGeometry().size();
    dialog.resize(std::min(qRound(800 * context->getIntfScaleFactor()), available.width() - 40),
                  std::min(qRound(560 * context->getIntfScaleFactor()), available.height() - 40));
    if (dialog.exec() != QDialog::Accepted || !context || dialog.selectedUrls().isEmpty()) return;
    QUrl file = dialog.selectedUrls().first();
    if (!file.isLocalFile()) return;
    int index = extensions.indexOf(QFileInfo(file.toLocalFile()).suffix().toLower());
    if (index < 0) {
        index = filters.indexOf(dialog.selectedNameFilter());
        if (index < 0) return;
    }
    context->getMediaLibrary()->exportPlaylist(playlist, file,
        QByteArray("export-") + extensions[index].toLatin1(), this,
        [this, context, playlist, name, file, extension = extensions[index]](const MediaLib::PlaylistExportResult& result) {
            if (!context) return;
            if (result.failure == MediaLib::PlaylistExportResult::Failure::None) {
                context->setDialogFilePath(file);
                getSettings()->setValue("last-playlist-ext", extension);
                return;
            }
            const bool shortcuts = context->getUseGlobalShortcuts();
            context->setUseGlobalShortcuts(false);
            const auto restoreShortcuts = qScopeGuard([context, shortcuts] {
                if (context) context->setUseGlobalShortcuts(shortcuts);
            });
            if (result.failure != MediaLib::PlaylistExportResult::Failure::Destination) {
                messageDialog(result.error + QStringLiteral("\n\n") + name, qtr("Export Playlist"),
                    QMessageBox::Close, QMessageBox::Close, QMessageBox::Warning);
                return;
            }
            const auto answer = messageDialog(
                qtr("Hakone cannot save this file.\n\n%1\n\nSelect another file or folder.")
                    .arg(toNativeSeparators(file.toLocalFile())),
                qtr("Export Playlist"), QMessageBox::Yes | QMessageBox::Cancel,
                QMessageBox::Cancel, QMessageBox::Warning, nullptr, qtr("Select file"));
            if (context && answer == QMessageBox::Yes)
                selectLibraryPlaylistExport(playlist, name, file, extension);
        });
}

void DialogsProvider::savePlayingToPlaylist()
{
    const QPointer<MainCtx> context = p_intf->p_mi;
    if (!context) return;
    const bool shortcuts = context->getUseGlobalShortcuts();
    context->setUseGlobalShortcuts(false);
    const auto restoreShortcuts = qScopeGuard([context, shortcuts] {
        if (context) context->setUseGlobalShortcuts(shortcuts);
    });
    const QStringList extensions {"xspf", "m3u", "m3u8", "html"};
    const QStringList filters {qtr("XSPF playlist (*.xspf)"), qtr("M3U playlist (*.m3u)"),
                               qtr("M3U8 playlist (*.m3u8)"), qtr("HTML playlist (*.html)")};
    int preferred = extensions.indexOf(getSettings()->value("last-playlist-ext").toString());
    if (preferred < 0) preferred = 2;
    QFileDialog dialog(nullptr, qtr("Save play queue as..."));
    dialog.setObjectName(QStringLiteral("savePlayQueue"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setSupportedSchemes({QStringLiteral("file")});
    dialog.setNameFilters(filters);
    dialog.selectNameFilter(filters[preferred]);
    dialog.setDefaultSuffix(extensions[preferred]);
    connect(&dialog, &QFileDialog::filterSelected, &dialog, [&dialog, filters, extensions](const QString &filter) {
        const int index = filters.indexOf(filter);
        if (index >= 0) dialog.setDefaultSuffix(extensions[index]);
    });
    auto directory = context->getDialogFilePath();
    if (directory.isLocalFile() && QFileInfo(directory.toLocalFile()).isFile())
        directory = directory.adjusted(QUrl::RemoveFilename);
    dialog.setDirectoryUrl(directory);
    if (auto *buttons = dialog.findChild<QDialogButtonBox*>())
        if (auto *save = buttons->button(QDialogButtonBox::Save)) save->setProperty("redesignPrimary", true);
    applyRedesignWidgetTheme(&dialog, context);
    QVLCDialog::setWindowTransientParent(&dialog, nullptr, p_intf);
    while (context && dialog.exec() == QDialog::Accepted) {
        if (!context || dialog.selectedUrls().isEmpty()) return;
        const QUrl file = dialog.selectedUrls().first();
        if (!file.isLocalFile()) return;
        int index = extensions.indexOf(QFileInfo(file.toLocalFile()).suffix().toLower());
        if (index < 0) index = filters.indexOf(dialog.selectedNameFilter());
        if (index < 0) return;
        const QByteArray format = QByteArray("export-") + extensions[index].toLatin1();
        if (exportPlayQueue(p_intf, file.toLocalFile(), format.constData())) {
            context->setDialogFilePath(file);
            getSettings()->setValue("last-playlist-ext", extensions[index]);
            return;
        }
        const auto answer = messageDialog(
            qtr("Hakone cannot save this file.\n\n%1\n\nHakone did not change the play queue. Select another file or folder.")
                .arg(toNativeSeparators(file.toLocalFile())),
            qtr("Save play queue"), QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel, QMessageBox::Warning, nullptr, qtr("Select file"));
        if (answer != QMessageBox::Yes) return;
    }
}

/****************************************************************************
 * Sout emulation
 ****************************************************************************/

void DialogsProvider::streamingDialog( QWindow *parent,
                                       const QStringList& mrls,
                                       bool b_transcode_only,
                                       QStringList options )
{
    if (mrls.isEmpty()) return;
    QList<QStringList> outputOptions;

    /* Stream */
    // Does streaming multiple files make sense?  I suppose so, just stream one
    // after the other, but not at the moment.
    if( !b_transcode_only )
    {
        SoutDialog *s = new SoutDialog( parent, p_intf, mrls[0] );
        s->setAttribute( Qt::WA_QuitOnClose, false ); // See #4883
        if( s->exec() == QDialog::Accepted )
        {
            outputOptions.append(s->getOptions());
            delete s;
        }
        else
        {
            delete s; return;
        }
    } else {
    /* Convert */
        ConvertDialog *s = new ConvertDialog( parent, p_intf, mrls );
        s->setAttribute( Qt::WA_QuitOnClose, false ); // See #4883
        if( s->exec() == QDialog::Accepted )
        {
            /* Clear the playlist.  This is because we're going to be populating
               it */
            THEMPL->clear();
            outputOptions = s->getOptions();
            delete s;
        }
        else
        {
            delete s; return;
        }
    }

    /* Get SoutChain(s) */
    QVector<vlc::playlist::Media> outputMedias;

    for (int i = 0; i < outputOptions.size(); ++i)
    {
        const QString &mrl = mrls[i];
        QString title = "Converting " + mrl;
        outputMedias.append(vlc::playlist::Media(mrl, title, options + outputOptions[i]));
    }

    if( !outputMedias.empty() )
        THEMPL->append(outputMedias, true);
}

void DialogsProvider::streamingDialog(const QList<QUrl> &urls, bool b_stream )
{
    if(urls.isEmpty())
        return;

    QStringList _urls;
    std::transform(urls.begin(),
                   urls.end(),
                   std::back_inserter(_urls),
                   [](const QUrl& url){ return url.toString(); });

    streamingDialog(nullptr, _urls, b_stream);
}

void DialogsProvider::openAndStreamingDialogs()
{
    ensureDialog(m_openDialog);
    m_openDialog->showTab( OpenDialog::OPEN_FILE_TAB, OpenDialog::OPEN_AND_STREAM );
}

void DialogsProvider::openAndTranscodingDialogs()
{
    ensureDialog(m_openDialog);
    m_openDialog->showTab( OpenDialog::OPEN_FILE_TAB, OpenDialog::OPEN_AND_SAVE );
}

void  DialogsProvider::loadMediaFile( const es_format_category_e category, const int filter , const QString &dialogTitle)
{
    input_item_t *p_item = THEMIM->getInput();
    if( !p_item ) return;

    char *path = input_item_GetURI( p_item );
    QUrl url;
    if( path )
    {
        url.setUrl( qfu(path) );
        url = url.adjusted(QUrl::RemoveFilename);
        if (url.scheme() != "file")
            url.clear();
        free(path);
    }

    QStringList qsl = showSimpleOpen( dialogTitle,
                                      filter,
                                      url );

    foreach( const QString &qsUrl, qsl )
    {

        if ( THEMIM->AddAssociatedMedia( category, qsUrl, true, true, false ) )
            msg_Warn( p_intf, "unable to load media from '%s', category(%d)", qtu( qsUrl ), category );
    }
}

void DialogsProvider::loadSubtitlesFile()
{
    if (p_intf->p_mi && p_intf->p_mi->hasMediaLibrary()) {
        showTrackAssignments({{"scope", "file"}}, true);
        return;
    }
    loadMediaFile( SPU_ES, EXT_FILTER_SUBTITLE, qtr( "Open subtitles..." ) );
}

void DialogsProvider::loadAudioFile()
{
    if (p_intf->p_mi && p_intf->p_mi->hasMediaLibrary()) {
        showTrackAssignments({{"scope", "file"}}, false);
        return;
    }
    loadMediaFile( AUDIO_ES, EXT_FILTER_AUDIO, qtr( "Open audio..." ) );
}

void DialogsProvider::loadVideoFile()
{
    loadMediaFile( VIDEO_ES, EXT_FILTER_VIDEO, qtr( "Open video..." ) );
}

void DialogsProvider::addMediaTracksDialog(const QUrl& media, bool subtitles)
{
    showTrackAssignments({{"scope", "file"}, {"url", media}}, subtitles);
}

void DialogsProvider::showTrackAssignments(const QVariantMap& target, bool subtitles)
{
    if (!p_intf->p_mi || !p_intf->p_mi->hasMediaLibrary()) return;
    QVariantMap context = target;
    if (context.value("url").toUrl().isEmpty()) {
        const SharedInputItem current { THEMIM->getInput() };
        if (current) {
            char *uri = input_item_GetURI(current.get());
            context.insert("url", QUrl(qfu(uri))); free(uri);
        }
    }
    {
        vlc_player_locker lock {p_intf->p_player};
        if (auto* media = vlc_player_GetCurrentMedia(p_intf->p_player)) {
            char* uri = input_item_GetURI(media);
            context.insert("runtimeUrl", QUrl(qfu(uri))); free(uri);
            context.insert("runtimeCategory", vlc_player_GetTrackCount(p_intf->p_player, VIDEO_ES) > 0 ? 1
                : vlc_player_GetTrackCount(p_intf->p_player, AUDIO_ES) > 0 ? 2 : 0);
            QVariantList tracks;
            const auto category = subtitles ? SPU_ES : AUDIO_ES;
            QSet<QUrl> seen;
            for (size_t i = 0; i < vlc_player_GetTrackCount(p_intf->p_player, category); ++i) {
                const auto* track = vlc_player_GetTrackAt(p_intf->p_player, category, i);
                const char* source = vlc_es_id_GetSourceUri(track->es_id);
                if (!source) continue;
                const auto url = vlc::qt::normalizeLocalFileUrl(QUrl(qfu(source)));
                if (!seen.contains(url)) { seen.insert(url); tracks.append(url.toString(QUrl::FullyEncoded)); }
            }
            auto metadata = MediaIdentity::inputMetadata(media);
            QVariantMap release;
            QStringList audioLanguages;
            for (size_t i = 0; i < vlc_player_GetTrackCount(p_intf->p_player, VIDEO_ES); ++i) {
                const auto& video = vlc_player_GetTrackAt(p_intf->p_player, VIDEO_ES, i)->fmt.video;
                if (!video.i_visible_width || !video.i_visible_height) continue;
                metadata.insert("width", video.i_visible_width); metadata.insert("height", video.i_visible_height);
                if (video.i_frame_rate_base) release.insert("fps", QString::number(double(video.i_frame_rate) / video.i_frame_rate_base));
                break;
            }
            for (size_t i = 0; i < vlc_player_GetTrackCount(p_intf->p_player, AUDIO_ES); ++i) {
                const auto* language = vlc_player_GetTrackAt(p_intf->p_player, AUDIO_ES, i)->fmt.psz_language;
                if (language && *language) audioLanguages.append(QString::fromUtf8(language));
            }
            audioLanguages.removeDuplicates(); metadata.insert("audioLanguages", audioLanguages);
            metadata.insert("release", release); context.insert("runtimeMetadata", metadata);
            context.insert("runtimeTracks", tracks);
        }
    }
    QVariantList queue;
    vlc_playlist_Lock(p_intf->p_playlist);
    for (size_t i = 0; i < vlc_playlist_Count(p_intf->p_playlist); ++i) {
        auto* item = vlc_playlist_item_GetMedia(vlc_playlist_Get(p_intf->p_playlist, i));
        char *uri = input_item_GetURI(item);
        if (uri) queue.append(QUrl(qfu(uri)));
        free(uri);
    }
    vlc_playlist_Unlock(p_intf->p_playlist);
    context.insert("urls", queue);
    emit showRedesignTrackAssignments(context, subtitles);
}

QStringList DialogsProvider::selectTrackFiles(bool subtitles, const QUrl& folder)
{
    auto files = showSimpleOpen(subtitles ? qtr("Select subtitle files") : qtr("Select audio files"),
        subtitles ? EXT_FILTER_SUBTITLE : EXT_FILTER_AUDIO, folder);
    for (auto& file : files) file = vlc::qt::normalizeLocalFileUrl(QUrl(file)).toString(QUrl::FullyEncoded);
    return files;
}

QUrl DialogsProvider::selectTrackFolder(const QUrl& folder)
{
    return QFileDialog::getExistingDirectoryUrl(nullptr, qtr("Select track folder"),
        folder, QFileDialog::ShowDirsOnly, {QStringLiteral("file")});
}

bool DialogsProvider::applySavedTrackAssignments(const QVariantList& saved, bool subtitles)
{
    struct Change { QSet<QUrl> removed; QList<QUrl> added; };
    QHash<QUrl, Change> changes;
    for (const auto& value : saved) {
        const auto row = value.toMap();
        QSet<QUrl> before, after;
        const auto initial = row.value("initialTracks", row.value("baseline"));
        for (const auto& track : initial.toList()) before.insert(vlc::qt::normalizeLocalFileUrl(track.toUrl()));
        for (const auto& track : row.value("tracks").toList()) after.insert(vlc::qt::normalizeLocalFileUrl(track.toUrl()));
        Change change {before - after, {}};
        for (const auto& value : row.value("tracks").toList()) {
            const auto track = vlc::qt::normalizeLocalFileUrl(value.toUrl());
            if (!before.contains(track) && !change.added.contains(track)) change.added.append(track);
        }
        changes.insert(row.value("url").toUrl(), change);
    }
    auto removeSavedSlaves = [&](input_item_t* item) {
        char *uri = input_item_GetURI(item);
        const auto change = changes.constFind(QUrl(qfu(uri))); free(uri);
        if (change == changes.cend() || change->removed.isEmpty()) return;
        vlc_mutex_lock(&item->lock);
        int count = 0;
        for (int i = 0; i < item->i_slaves; ++i) {
            auto* slave = item->pp_slaves[i];
            if (change->removed.contains(vlc::qt::normalizeLocalFileUrl(QUrl(qfu(slave->psz_uri)))))
                input_item_slave_Delete(slave);
            else item->pp_slaves[count++] = slave;
        }
        item->i_slaves = count;
        vlc_mutex_unlock(&item->lock);
    };
    // Remove only explicitly unassigned tracks from queued item caches. This
    // preserves unrelated additions and applies the edit on their next play.
    vlc_playlist_Lock(p_intf->p_playlist);
    for (size_t i = 0; i < vlc_playlist_Count(p_intf->p_playlist); ++i)
        removeSavedSlaves(vlc_playlist_item_GetMedia(vlc_playlist_Get(p_intf->p_playlist, i)));
    vlc_playlist_Unlock(p_intf->p_playlist);
    const SharedInputItem current { THEMIM->getInput() };
    if (!current) return false;
    char *uri = input_item_GetURI(current.get());
    const auto change = changes.constFind(QUrl(qfu(uri))); free(uri);
    if (change == changes.cend()) return false;
    removeSavedSlaves(current.get());
    const auto category = subtitles ? SPU_ES : AUDIO_ES;
    vlc_player_Lock(p_intf->p_player);
    bool select = vlc_player_GetSelectedTrack(p_intf->p_player, category) == nullptr;
    vlc_player_Unlock(p_intf->p_player);
    for (const auto& track : change->added) {
        if (THEMIM->AddAssociatedMedia(category,
            track.toString(QUrl::FullyEncoded), select, false, false) == VLC_SUCCESS)
            select = false;
    }
    // The running demuxer has no remove-associated-media operation.
    return !change->removed.isEmpty();
}


/****************************************************************************
 * Menus
 ****************************************************************************/

void DialogsProvider::sendKey( int key )
{
     // translate from a vlc keycode into a Qt sequence
     QKeySequence kseq0( VLCKeyToString( key, true ) );

     if( !popupMenu )
     {
         // make sure at least a non visible popupmenu is available
         popupMenu.reset(VLCMenuBar::PopupMenu( p_intf, false ));
         if( unlikely( !popupMenu ) )
             return;
     }

     // test against key accelerators from the popupmenu
     QList<QAction*> actions = popupMenu->findChildren<QAction*>();
     for( int i = 0; i < actions.size(); i++ )
     {
         QAction* action = actions.at(i);
         QKeySequence kseq = action->shortcut();
         if( kseq == kseq0 )
         {
             action->trigger();
             return;
         }
     }

     // forward key to vlc core when not a key accelerator
     var_SetInteger( vlc_object_instance(p_intf), "key-pressed", key );
}


template<typename T, typename... Args>
void DialogsProvider::ensureDialog(std::unique_ptr<T>& dialog, Args&&... args)
{
    if (!dialog) {
        dialog = std::make_unique<T>(p_intf, std::forward<Args>(args)...);
        //install event filter to track visibility
        dialog->installEventFilter(this);
    }
}

template<typename T>
void DialogsProvider::toggleDialogVisible(std::unique_ptr<T>& dialog) {
    ensureDialog(dialog);
    dialog->toggleVisible();
}

#define DIALOG_VISIBLE_GETTER(dialog) \
    bool DialogsProvider:: dialog ## Visible() const { \
        if (!m_ ## dialog) \
            return false; \
        return m_ ## dialog->isVisible(); \
    }

ALL_DIALOG(DIALOG_VISIBLE_GETTER)

bool DialogsProvider::openDialogVisible() const
{
    return (m_openDialog && m_openDialog->isVisible())
        || (m_discOpenDialog && m_discOpenDialog->isVisible())
        || (m_captureOpenDialog && m_captureOpenDialog->isVisible());
}

void DialogsProvider::dismissFirstRunDialog()
{
    if (!m_firstRunDialogVisible) return;
    m_firstRunDialogVisible = false;
    emit firstRunDialogVisibleChanged();
}

bool DialogsProvider::firstRunDialogVisible() const
{
    return m_firstRunDialogVisible;
}
