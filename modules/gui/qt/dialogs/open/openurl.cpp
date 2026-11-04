/*****************************************************************************
 * openurl.cpp: Open a MRL or clipboard content
 *****************************************************************************
 * Copyright © 2009 the VideoLAN team
 *
 * Authors: Jean-Philippe André <jpeg@videolan.org>
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

#include "dialogs/open/openurl.hpp"
#include "maininterface/mainctx.hpp"
#include "util/interface_translator.hpp"
#include "util/color_scheme_model.hpp"

#include <QAccessible>
#include <QApplication>
#include <QBoxLayout>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QShowEvent>
#include <QTimer>
#include <QUrl>

OpenUrlDialog::OpenUrlDialog(qt_intf_t *intf, bool clipboard)
    : QVLCDialog(nullptr, intf), m_ctx(intf->p_mi), bClipboard(clipboard)
{
    setObjectName(QStringLiteral("redesignOpenUrl"));
    setWindowRole("vlc-open-url");
    auto *layout = new QVBoxLayout(this);
    scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *body = new QWidget(scroll);
    auto *form = new QVBoxLayout(body);
    form->setContentsMargins(0, 0, 0, 0);
    heading = new QLabel(body);
    heading->setProperty("redesignHeading", true);
    heading->setTextFormat(Qt::PlainText);
    heading->setWordWrap(true);
    form->addWidget(heading);
    info = new QLabel(body);
    info->setTextFormat(Qt::PlainText);
    info->setWordWrap(true);
    form->addWidget(info);
    edit = new QLineEdit(body);
    edit->setObjectName(QStringLiteral("openUrlAddress"));
    edit->setClearButtonEnabled(true);
    edit->setPlaceholderText(QStringLiteral("https://example.com/media"));
    edit->installEventFilter(this);
    form->addWidget(edit);
    feedback = new QLabel(body);
    feedback->setObjectName(QStringLiteral("openUrlFeedback"));
    feedback->setProperty("redesignValue", true);
    feedback->setTextFormat(Qt::PlainText);
    feedback->setWordWrap(true);
    feedback->hide();
    form->addWidget(feedback);
    form->addStretch();
    scroll->setWidget(body);
    layout->addWidget(scroll, 1);
    actions = new QBoxLayout(QBoxLayout::LeftToRight);
    actions->addStretch();
    cancelButton = new QPushButton(this);
    cancelButton->setObjectName(QStringLiteral("openUrlCancel"));
    actions->addWidget(cancelButton);
    queueButton = new QPushButton(this);
    queueButton->setObjectName(QStringLiteral("openUrlQueue"));
    actions->addWidget(queueButton);
    playButton = new QPushButton(this);
    playButton->setObjectName(QStringLiteral("openUrlPlay"));
    playButton->setProperty("redesignPrimary", true);
    playButton->setDefault(true);
    actions->addWidget(playButton);
    layout->addLayout(actions);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(queueButton, &QPushButton::clicked, this, &OpenUrlDialog::enqueue);
    connect(playButton, &QPushButton::clicked, this, &OpenUrlDialog::play);
    connect(edit, &QLineEdit::textChanged, this, [this] {
        feedback->hide();
        playButton->setEnabled(!edit->text().trimmed().isEmpty());
        queueButton->setEnabled(playButton->isEnabled());
    });
    if (m_ctx)
    {
        connect(m_ctx, &MainCtx::intfScaleFactorChanged, this, &OpenUrlDialog::updateMetrics);
        connect(m_ctx->getColorScheme(), &ColorSchemeModel::currentChanged, this, &OpenUrlDialog::updateMetrics);
        connect(m_ctx, &QObject::destroyed, this, &QDialog::reject);
    }
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, &OpenUrlDialog::retranslate);
    retranslate();
    playButton->setEnabled(false);
    queueButton->setEnabled(false);
    resize(QSize(dp(560), dp(220)).boundedTo(availableSize()));
}

int OpenUrlDialog::dp(int value) const
{
    return qRound(value * (m_ctx ? m_ctx->getIntfScaleFactor() : 1));
}

QSize OpenUrlDialog::availableSize() const
{
    const auto available = screen() ? screen()->availableGeometry().size() : QSize(dp(560), dp(400));
    return (available - QSize(dp(32), dp(32))).expandedTo(QSize(1, 1));
}

void OpenUrlDialog::retranslate()
{
    setWindowTitle(qtr("Open media"));
    heading->setText(windowTitle());
    info->setText(qtr("Enter a media address or file path."));
    edit->setAccessibleName(qtr("Address or file path"));
    edit->setAccessibleDescription(info->text());
    cancelButton->setText(qtr("&Cancel"));
    queueButton->setText(qtr("Add to &queue"));
    playButton->setText(qtr("&Play"));
    if (feedback->isVisible()) feedback->setText(inputError());
    updateMetrics();
}

void OpenUrlDialog::updateMetrics()
{
    const auto available = availableSize();
    const int margin = qMin(dp(24), available.width() / 12);
    layout()->setContentsMargins(margin, margin, margin, margin);
    layout()->setSpacing(dp(12));
    scroll->widget()->layout()->setSpacing(dp(12));
    actions->setSpacing(dp(8));
    const int required = cancelButton->sizeHint().width() + queueButton->sizeHint().width()
        + playButton->sizeHint().width() + 2 * actions->spacing();
    const bool stacked = width() - 2 * margin < required;
    actions->setDirection(stacked ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    for (auto *button : {cancelButton, queueButton, playButton}) button->setIcon({});
    setMinimumSize(QSize(dp(320), dp(stacked ? 260 : 220)).boundedTo(available));
    resize(size().boundedTo(available));
    if (edit->hasFocus()) revealInput();
}

void OpenUrlDialog::resizeEvent(QResizeEvent *event)
{
    QVLCDialog::resizeEvent(event);
    if (playButton) updateMetrics();
}

bool OpenUrlDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == edit && event->type() == QEvent::FocusIn) revealInput();
    return QVLCDialog::eventFilter(watched, event);
}

void OpenUrlDialog::revealInput()
{
    QTimer::singleShot(0, this, [this] { scroll->ensureWidgetVisible(edit, 0, dp(8)); });
}

QString OpenUrlDialog::inputError() const
{
    const auto text = edit->text().trimmed();
    if (text.isEmpty()) return qtr("Enter a media address or file path.");
    if (text.contains(QLatin1Char('\n')) || text.contains(QLatin1Char('\r')))
        return qtr("Enter one media address or file path.");
    if (text.contains(QLatin1Char('\0')))
        return qtr("Hakone cannot use this address. Check the address. Try again.");
    if (text.contains(QStringLiteral("://")))
    {
        const QUrl url(text);
        const bool webAddress = url.scheme() == QStringLiteral("http") || url.scheme() == QStringLiteral("https");
        if (!url.isValid() || (webAddress && url.host().isEmpty()))
            return qtr("Hakone cannot use this address. Check the address. Try again.");
    }
    return {};
}

void OpenUrlDialog::submit(bool enqueue)
{
    const auto error = inputError();
    if (!error.isEmpty())
    {
        feedback->setText(error);
        feedback->show();
        edit->setFocus();
        QTimer::singleShot(0, this, [this] { scroll->ensureWidgetVisible(feedback, 0, dp(8)); });
        QAccessibleEvent event(feedback, QAccessible::Alert);
        QAccessible::updateAccessibility(&event);
        return;
    }
    lastUrl = edit->text().trimmed();
    bShouldEnqueue = enqueue;
    QVLCDialog::accept();
}

void OpenUrlDialog::enqueue() { submit(true); }
void OpenUrlDialog::play() { submit(false); }
void OpenUrlDialog::accept() { play(); }
QString OpenUrlDialog::url() const { return lastUrl; }
bool OpenUrlDialog::shouldEnqueue() const { return bShouldEnqueue; }

void OpenUrlDialog::showEvent(QShowEvent *event)
{
    QVLCDialog::showEvent(event);
    if (!initialized)
    {
        initialized = true;
        if (bClipboard)
        {
            // Do not stat an absolute or network path on the UI thread.
            const auto candidate = [](const QMimeData *mime) -> QString {
                if (!mime) return {};
                if (mime->hasUrls())
                    return mime->urls().isEmpty() ? QString() : mime->urls().first().toString(QUrl::PrettyDecoded);
                const auto text = mime->text().trimmed();
                if (text.isEmpty() || text.contains(QLatin1Char('\n')) || text.contains(QLatin1Char('\r'))) return {};
                if (text.contains(QStringLiteral("://")) || QDir::isAbsolutePath(text)
                    || text.startsWith(QStringLiteral("\\\\"))
                    || text.startsWith(QStringLiteral("./")) || text.startsWith(QStringLiteral("../"))
                    || (text.size() >= 3 && text[0].isLetter() && text[1] == QLatin1Char(':')
                        && (text[2] == QLatin1Char('\\') || text[2] == QLatin1Char('/')))
                    || QFile::exists(text)) return text;
                return {};
            };
            if (auto *clipboard = QApplication::clipboard())
            {
                // Match MainCtx::pasteFromClipboard: plain selected text must
                // not replace the address chosen from the regular clipboard.
                const auto *selection = clipboard->supportsSelection()
                    ? clipboard->mimeData(QClipboard::Selection) : nullptr;
                QString text = selection && selection->hasUrls() ? candidate(selection) : QString();
                if (text.isEmpty()) text = candidate(clipboard->mimeData(QClipboard::Clipboard));
                edit->setText(text);
            }
        }
        edit->selectAll();
    }
    edit->setFocus(Qt::OtherFocusReason);
    revealInput();
}
