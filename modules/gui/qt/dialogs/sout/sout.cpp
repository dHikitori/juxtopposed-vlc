/*****************************************************************************
 * sout.cpp : Stream output dialog ( old-style )
 ****************************************************************************
 * Copyright (C) 2007-2009 the VideoLAN team
 *
 *
 * Authors: Clément Stenac <zorglub@videolan.org>
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

#include "dialogs/sout/sout.hpp"
#include "util/qt_dirs.hpp"
#include "util/interface_translator.hpp"
#include "dialogs/sout/sout_widgets.hpp"
#include "widgets/native/qvlcframe.hpp"
#include "util/colorizedsvgicon.hpp"
#include "util/redesign_widget_theme.hpp"
#include "maininterface/mainctx.hpp"

#include <QString>

#include <cassert>
#include <QWindow>
#include <QApplication>
#include <QTimer>
#include <QStyle>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QFileInfo>
#include <QDir>
#include <QSet>
#include <QScreen>

SoutDialog::SoutDialog( QWindow *parent, qt_intf_t *_p_intf, const QString& inputChain )
{
    p_intf = _p_intf;
    sourceMRL = inputChain;
    setObjectName(QStringLiteral("redesignStream"));

    setWindowTitle( qtr( "Stream Output" ) );
    setWindowRole( "vlc-stream-output" );

    // NOTE: Aero style seems to be incompatible with
    //       Qt Modern Windows Style dark mode
    setWizardStyle(QWizard::ClassicStyle);
    setTitleFormat(Qt::RichText);

    QVLCDialog::setWindowTransientParent(this, parent , p_intf);

    /* UI stuff */
    ui.setupUi(this);
    setWizardStyle(QWizard::ClassicStyle);
    setOption(QWizard::NoBackButtonOnStartPage);
    setOption(QWizard::IndependentPages);
    setMinimumSize(0, 0);
    if (inputChain.isEmpty()) setStartId(pageIds().value(1));
    ui.verticalLayout_4->addWidget(ui.localOutput);
    ui.destBox->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    ui.destBox->setMinimumContentsLength(1);
    ui.destBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ui.destTab->setUsesScrollButtons(true);
    ui.destTab->setElideMode(Qt::ElideMiddle);
    auto *profileChoice = ui.profileSelect->findChild<QComboBox *>();
    if (profileChoice)
    {
        profileChoice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        profileChoice->setMinimumContentsLength(1);
        profileChoice->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        profileChoice->setAccessibleName(qtr("Streaming profile"));
    }
    for (auto *page : {ui.sourceWizardPage, ui.destinationWizardPage, ui.wizardPage, ui.optionsWizardPage})
    {
        auto *body = new QWidget(page);
        body->setLayout(page->layout());
        auto *scroll = new QScrollArea(page);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(body);
        auto *pageLayout = new QVBoxLayout(page);
        pageLayout->setContentsMargins(0, 0, 0, 0);
        pageLayout->addWidget(scroll, 1);
        auto *status = new QLabel(page);
        status->setWordWrap(true);
        status->setTextFormat(Qt::PlainText);
        status->setObjectName(QStringLiteral("streamFeedback%1").arg(feedback.size()));
        pageLayout->addWidget(status);
        status->setAccessibleName(qtr("Setup status"));
        feedback.append(status);
    }
    review = new QLabel(ui.optionsWizardPage);
    review->setObjectName(QStringLiteral("streamReview"));
    review->setTextFormat(Qt::PlainText);
    review->setWordWrap(true);
    review->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    ui.verticalLayout_2->insertWidget(0, review);
    customOptions = new QCheckBox(ui.optionsWizardPage);
    customOptions->setObjectName(QStringLiteral("streamCustomOptions"));
    ui.verticalLayout_2->insertWidget(2, customOptions);
    ui.groupBox_4->hide();
    ui.mrlEdit->setReadOnly(true);
    ui.mrlEdit->setTabChangesFocus(true);
    connect(ui.mrlEdit, &QTextEdit::cursorPositionChanged, this, [this] {
        // Let the text editor finish scrolling before adjusting the page.
        QTimer::singleShot(0, this, &SoutDialog::ensureFocusVisible);
    });
    ui.verticalLayout->addStretch();
    ui.verticalLayout_3->addStretch();
    ui.verticalLayout_2->addStretch();
    ui.gridLayout_11->setAlignment(Qt::AlignTop);
    connect(customOptions, &QCheckBox::toggled, this, [this](bool custom) {
        ui.groupBox_4->setVisible(custom);
        ui.mrlEdit->setReadOnly(!custom);
        if (!custom) updateChain();
        else {
            updateStatus();
            ui.mrlEdit->setFocus(Qt::OtherFocusReason);
        }
    });
    connect(ui.mrlEdit, &QTextEdit::textChanged, this, &SoutDialog::updateStatus);
    connect(this, &QWizard::currentIdChanged, this, [this] { updateChain(); });
    ui.profileSelect->setThemeContext(p_intf->p_mi);
    ui.inputBox->setMRL( inputChain );
    ui.destTab->setTabsClosable( true );
    QTabBar* tb = ui.destTab->findChild<QTabBar*>();
    if (tb && tb->tabButton(0, QTabBar::RightSide)) tb->tabButton(0, QTabBar::RightSide)->hide();
    connect( ui.destTab, &QTabWidget::tabCloseRequested, this, &SoutDialog::closeTab );
    ui.destTab->setTabIcon( 0, ColorizedSvgIcon::colorizedIconForWidget( ":/menu/add.svg", ui.destTab ) );

    ui.destBox->addItem( qtr( "File" ) );
    ui.destBox->addItem( "HTTP" );
    ui.destBox->addItem( "MS-WMSP (MMSH)" );
    ui.destBox->addItem( "RTSP" );
    ui.destBox->addItem( "SRT / MPEG Transport Stream" );
    ui.destBox->addItem( "RIST / MPEG Transport Stream" );
    ui.destBox->addItem( "RTP / MPEG Transport Stream" );
    ui.destBox->addItem( "RTP Audio/Video Profile" );
    ui.destBox->addItem( "UDP (legacy)" );
    ui.destBox->addItem( "Icecast" );

    BUTTONACT( ui.addButton, &SoutDialog::addDest );

    /* Connect everything to the updateChain function */

    /* Misc */
    connect( ui.soutAll, &QCheckBox::toggled, this, &SoutDialog::updateChain );
    connect( ui.localOutput, &QCheckBox::toggled, this, &SoutDialog::updateChain );
    connect( ui.transcodeBox, &QCheckBox::toggled, this, &SoutDialog::updateChain );
    connect( ui.profileSelect, &VLCProfileSelector::optionsChanged, this, &SoutDialog::updateChain );

    for (const auto role : {QWizard::NextButton, QWizard::FinishButton})
    {
        auto *primary = button(role);
        primary->setProperty("redesignPrimary", true);
        primary->style()->unpolish(primary);
        primary->style()->polish(primary);
    }

    ui.localOutput->setChecked( var_InheritBool( p_intf, "sout-display" ) );
    applyRedesignWidgetTheme(this, p_intf->p_mi);
    const auto scale = [this] {
        const double factor = p_intf->p_mi->getIntfScaleFactor();
        for (auto *page : {ui.sourceWizardPage, ui.destinationWizardPage, ui.wizardPage, ui.optionsWizardPage})
        {
            const int pad = qRound(16 * factor);
            page->layout()->setContentsMargins(pad, pad, pad, pad);
            page->layout()->setSpacing(qRound(12 * factor));
        }
        for (auto *port : findChildren<QSpinBox *>())
            port->setMaximumWidth(qRound(120 * factor));
    };
    scale();
    connect(p_intf->p_mi, &MainCtx::intfScaleFactorChanged, this, [this, scale] { scale(); retranslate(); });
    const double factor = p_intf->p_mi->getIntfScaleFactor();
    QSize initial(qRound(760 * factor), qRound(620 * factor));
    if (screen()) initial = initial.boundedTo(screen()->availableGeometry().size() - QSize(32, 32));
    resize(initial);
    retranslate();
    updateChain();
    if (auto* translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, &SoutDialog::retranslate);
    for (auto *widget : findChildren<QWidget*>()) widget->installEventFilter(this);
    setTabOrder(ui.soutAll, customOptions);
    setTabOrder(customOptions, ui.mrlEdit);
    setTabOrder(ui.mrlEdit, button(QWizard::BackButton));
    setTabOrder(button(QWizard::BackButton), button(QWizard::NextButton));
    setTabOrder(button(QWizard::NextButton), button(QWizard::FinishButton));
    setTabOrder(button(QWizard::FinishButton), button(QWizard::CancelButton));
}

bool SoutDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::FocusIn
        || (event->type() == QEvent::Resize && watched == ui.mrlEdit->viewport()))
        QTimer::singleShot(0, this, &SoutDialog::ensureFocusVisible);
    return QWizard::eventFilter(watched, event);
}

void SoutDialog::resizeEvent(QResizeEvent *event)
{
    QWizard::resizeEvent(event);
    QTimer::singleShot(0, this, &SoutDialog::ensureFocusVisible);
}

void SoutDialog::ensureFocusVisible()
{
    auto *widget = QApplication::focusWidget();
    if (!isVisible() || !widget || !isAncestorOf(widget)) return;
    QScrollArea *scroll = nullptr;
    for (auto *parent = widget->parentWidget(); parent && parent != this; parent = parent->parentWidget())
        if ((scroll = qobject_cast<QScrollArea*>(parent))) break;
    if (!scroll || !scroll->widget()->isAncestorOf(widget)) return;
    const auto dp = [this](int n) { return qRound(n * p_intf->p_mi->getIntfScaleFactor()); };
    QRect rect(widget->mapTo(scroll->widget(), QPoint()), widget->size());
    if (auto *editor = qobject_cast<QTextEdit*>(widget)) {
        editor->ensureCursorVisible();
        if (editor->height() > scroll->viewport()->height()) {
            rect = editor->cursorRect();
            rect.moveTopLeft(editor->viewport()->mapTo(scroll->widget(), rect.topLeft()));
        }
    } else if (qobject_cast<QGroupBox*>(widget)) {
        // Focus belongs to the title, rather than the expanded group contents.
        rect.setHeight(widget->fontMetrics().height() + dp(12));
    }
    const auto viewport = scroll->viewport()->size();
    scroll->ensureVisible(rect.center().x(), rect.center().y(),
        qMin(rect.width() / 2 + dp(6), viewport.width() / 2),
        qMin(rect.height() / 2 + dp(6), viewport.height() / 2));
}

void SoutDialog::retranslate()
{
    const QSignalBlocker blocker(ui.destBox);
    ui.retranslateUi(this);
    setWindowTitle(qtr("Stream media"));
    const auto title = [this](const QString &text) {
        return QStringLiteral("<span style=\"font-size:%1px; font-weight:400\">%2</span>")
            .arg(qRound(20 * p_intf->p_mi->getIntfScaleFactor())).arg(text.toHtmlEscaped());
    };
    ui.sourceWizardPage->setTitle(title(qtr("Source")));
    ui.sourceWizardPage->setSubTitle(qtr("Check the media that you want to stream."));
    ui.destinationWizardPage->setTitle(title(qtr("Destinations")));
    ui.destinationWizardPage->setSubTitle(qtr("Add a file or network destination."));
    ui.wizardPage->setTitle(title(qtr("Format")));
    ui.wizardPage->setSubTitle(qtr("Select the output format and codec settings."));
    ui.optionsWizardPage->setTitle(title(qtr("Review")));
    ui.optionsWizardPage->setSubTitle(qtr("Check the settings, then select Stream to start."));
    ui.helpEdit->setText(qtr("Next: choose where to send this media. The stream starts only when you select Stream."));
    ui.label_4->setText(qtr("Choose a destination type, then select Add. You can send the media to more than one destination."));
    ui.label_3->setText(qtr("Destination type"));
    ui.label_3->setBuddy(ui.destBox);
    ui.destTab->setTabText(0, qtr("Add destination"));
    ui.destBox->setAccessibleName(qtr("Destination type"));
    for (auto *status : feedback) status->setAccessibleName(qtr("Setup status"));
    if (auto *choice = ui.profileSelect->findChild<QComboBox *>()) choice->setAccessibleName(qtr("Streaming profile"));
    ui.localOutput->setText(qtr("Play on this device too"));
    ui.localOutput->setToolTip(qtr("Play the media on this device while it is sent to the destinations."));
    ui.transcodeBox->setText(qtr("Convert audio and video"));
    ui.transcodeBox->setToolTip(qtr("Turn this off to keep the source codecs. The selected format still sets the output container."));
    ui.groupBox_3->setTitle(qtr("Tracks"));
    ui.soutAll->setText(qtr("Stream all tracks"));
    ui.soutAll->setToolTip(qtr("Include all audio, video and subtitle tracks that the output format supports."));
    customOptions->setText(qtr("Edit output options"));
    ui.groupBox_4->setTitle(qtr("Custom output options"));
    ui.mrlEdit->setAccessibleName(qtr("Output options"));
    ui.mrlEdit->setToolTip(qtr("Change these options only if you know the Hakone stream output syntax."));

    setButtonText( QWizard::BackButton, qtr("Back") );
    setButtonText( QWizard::CancelButton, qtr("Cancel") );
    setButtonText( QWizard::NextButton, qtr("Next") );
    setButtonText( QWizard::FinishButton, qtr("Stream") );
    ui.destBox->setItemText(0, qtr("File"));
    for (int tab = 1; tab < ui.destTab->count(); ++tab)
        if (qobject_cast<FileDestBox*>(ui.destTab->widget(tab)))
            ui.destTab->setTabText(tab, qtr("File"));
    updateStatus();
}

void SoutDialog::closeTab( int i )
{
    if( i == 0 ) return;

    QWidget* temp = ui.destTab->widget( i );
    ui.destTab->removeTab( i );
    delete temp;
    updateChain();
}

void SoutDialog::addDest( )
{
    VirtualDestBox *db;
    QString caption;

    switch( ui.destBox->currentIndex() )
    {
        case 0:
            db = new FileDestBox( this, p_intf );
            caption = qtr( "File" );
            break;
        case 1:
            db = new HTTPDestBox( this );
            caption = qfu( "HTTP" );
            break;
        case 2:
            db = new MMSHDestBox( this );
            caption = qfu( "WMSP" );
            break;
        case 3:
            db = new RTSPDestBox( this );
            caption = qfu( "RTSP" );
            break;
        case 4:
            db = new SRTDestBox( this, "ts" );
            caption = "SRT/TS";
            break;
        case 5:
            db = new RISTDestBox( this, "ts" );
            caption = "RIST/TS";
            break;
        case 6:
            db = new RTPDestBox( this, "ts" );
            caption = "RTP/TS";
            break;
        case 7:
            db = new RTPDestBox( this );
            caption = "RTP/AVP";
            break;
        case 8:
            db = new UDPDestBox( this );
            caption = "UDP";
            break;
        case 9:
            db = new ICEDestBox( this );
            caption = "Icecast";
            break;
        default:
            vlc_assert_unreachable();
            return;
    }

    int index = ui.destTab->addTab( db, caption );
    connect( db, &VirtualDestBox::mrlUpdated, this, &SoutDialog::updateChain );
    ui.destTab->setCurrentIndex(index);
    for (auto *button : db->findChildren<QPushButton *>()) button->setAutoDefault(false);
    for (auto *edit : db->findChildren<QLineEdit *>())
        if (!edit->objectName().startsWith(QStringLiteral("qt_"))) edit->setClearButtonEnabled(true);
    if (auto *grid = qobject_cast<QGridLayout *>(db->QWidget::layout()))
    {
        grid->setAlignment(Qt::AlignTop);
        for (int row = 1; row < grid->rowCount(); ++row)
        {
            auto *left = grid->itemAtPosition(row, 0);
            auto *right = grid->itemAtPosition(row, 1);
            if (left && right)
                if (auto *label = qobject_cast<QLabel *>(left->widget())) label->setBuddy(right->widget());
        }
    }
    for (auto *widget : db->findChildren<QWidget*>()) widget->installEventFilter(this);
    if (auto *field = db->findChild<QLineEdit *>()) field->setFocus();
    updateChain();
}

void SoutDialog::done(int result)
{
    if (result == QDialog::Accepted)
    {
        updateStatus();
        if (!settingsError().isEmpty() || !ui.optionsWizardPage->isComplete()) return;
        QString error;
        acceptedOptions = customOptions->isChecked() ? parseOptions(&error) : generatedOptions;
        if (!error.isEmpty() || acceptedOptions.isEmpty()) return;
    }
    QWizard::done(result);
}

bool SoutDialog::validateCurrentPage()
{
    updateStatus();
    return currentPage()->isComplete();
}

void SoutDialog::updateChain()
{
    QString qs_mux = ui.profileSelect->getMux();

    SoutChain smrl( ":sout=#" );
    if( !ui.profileSelect->getTranscode().to_string().isEmpty() && ui.transcodeBox->isChecked() )
    {
        smrl.begin( ui.profileSelect->getTranscode().to_string() );
        smrl.end();
    }

    bool multi = false;

    if( ui.destTab->count() >= 3 ||
        ( ui.destTab->count() == 2 && ui.localOutput->isChecked() ) )
        multi = true;

    if( multi )
        smrl.begin( "duplicate" );

    for( int i = 1; i < ui.destTab->count(); i++ )
    {
        VirtualDestBox *vdb = qobject_cast<VirtualDestBox *>(ui.destTab->widget( i ));
        if( !vdb )
            continue;

        QString tempMRL = vdb->getMRL( qs_mux );
        if( tempMRL.isEmpty() ) continue;

        if( multi )
            smrl.option( "dst", tempMRL, true );
        else
        {
            smrl.begin( tempMRL);
            smrl.end();
        }
    }
    if( ui.localOutput->isChecked() )
    {
        if( multi )
            smrl.option( "dst", "display" );
        else
        {
            smrl.begin( "display" );
            smrl.end();
        }
    }

    if ( multi ) smrl.end();

    chain = smrl.to_string();

    generatedOptions = {QStringLiteral("sout=#") + chain.mid(QStringLiteral(":sout=#").size()),
                        ui.soutAll->isChecked() ? QStringLiteral("sout-all") : QStringLiteral("no-sout-all")};

    for( const char *option : { "sout-audio", "sout-video", "sout-spu" } )
    {
        generatedOptions.append((var_InheritBool(p_intf, option) ? QString() : QStringLiteral("no-")) + qfu(option));
    }

    generatedOptions.append(QStringLiteral("sout-keep"));
    chain = QLatin1Char(':') + generatedOptions.join(QStringLiteral(" :"));
    if (!customOptions->isChecked())
    {
        const QSignalBlocker block(ui.mrlEdit);
        ui.mrlEdit->setPlainText(chain);
    }
    updateStatus();
}

QString SoutDialog::settingsError()
{
    if (ui.destTab->count() == 1 && !ui.localOutput->isChecked())
        return qtr("Add at least one destination.");
    QSet<QString> files;
    const auto mux = ui.profileSelect->getMux();
    const QFileInfo source(QUrl(sourceMRL).toLocalFile());
    for (int i = 1; i < ui.destTab->count(); ++i)
    {
        auto *box = qobject_cast<VirtualDestBox *>(ui.destTab->widget(i));
        if (!box) continue;
        const auto error = box->validationError(mux);
        if (!error.isEmpty()) return qtr("%1: %2").arg(ui.destTab->tabText(i), error);
        if (auto *file = qobject_cast<FileDestBox *>(box))
        {
            const QFileInfo target(file->outputPath(mux));
            QString key = QDir(target.absolutePath()).canonicalPath() + QLatin1Char('/') + target.fileName();
#ifdef _WIN32
            key = key.toCaseFolded();
#endif
            if (files.contains(key)) return qtr("Two destinations use the same output file. Change one file name.");
            files.insert(key);
            if (QUrl(sourceMRL).isLocalFile() && !source.canonicalFilePath().isEmpty() && source.canonicalFilePath() == target.canonicalFilePath())
                return qtr("The output file is also the source file. Change the output file name.");
        }
    }
    return {};
}

QStringList SoutDialog::parseOptions(QString *error) const
{
    const auto text = ui.mrlEdit->toPlainText().trimmed();
    QStringList result;
    QString part;
    QChar quote;
    int depth = 0;
    bool escape = false;
    for (int i = 0; i < text.size(); ++i)
    {
        const auto c = text[i];
        if (escape) { part += c; escape = false; continue; }
        if (c == QLatin1Char('\\')) { part += c; escape = true; continue; }
        if (!quote.isNull())
        {
            part += c;
            if (c == quote) quote = QChar();
            continue;
        }
        if (c == QLatin1Char('\'') || c == QLatin1Char('"')) { quote = c; part += c; continue; }
        if (c == QLatin1Char('{')) ++depth;
        if (c == QLatin1Char('}')) --depth;
        if (depth < 0) break;
        if (depth == 0 && c == QLatin1Char(':') && (i == 0 || text[i - 1].isSpace()))
        {
            if (!part.trimmed().isEmpty()) result << part.trimmed();
            part.clear();
        }
        else part += c;
    }
    if (!part.trimmed().isEmpty()) result << part.trimmed();
    if (depth != 0 || !quote.isNull() || escape || result.isEmpty() || !result.first().startsWith(QStringLiteral("sout=#")) || result.first() == QStringLiteral("sout=#"))
    {
        *error = qtr("Enter output options that start with :sout=# and have matching quotes and braces.");
        return {};
    }
    return result;
}

void SoutDialog::updateStatus()
{
    const auto destinationError = settingsError();
    const auto mux = ui.profileSelect->getMux();
    auto *choice = ui.profileSelect->findChild<QComboBox *>();
    if (choice) choice->setToolTip(QStringLiteral("<qt>%1</qt>").arg(choice->currentText().toHtmlEscaped()));
    const bool formatValid = !mux.isEmpty() && choice && choice->count() > 0;
    QString error = destinationError;
    if (error.isEmpty() && !formatValid) error = qtr("Select an output profile.");
    if (error.isEmpty() && customOptions->isChecked()) parseOptions(&error);
    ui.destinationWizardPage->setComplete(destinationError.isEmpty());
    ui.wizardPage->setComplete(formatValid);
    ui.optionsWizardPage->setComplete(error.isEmpty());
    feedback[0]->clear();
    feedback[1]->setText(destinationError.isEmpty() ? qtr("Destinations are ready.") : destinationError);
    feedback[2]->setText(formatValid ? QString() : qtr("Select an output profile."));
    feedback[3]->setText(error.isEmpty() ? (customOptions->isChecked() ? qtr("Custom output options are in use.") : qtr("Ready to stream.")) : error);
    QStringList lines;
    if (!sourceMRL.isEmpty()) lines << qtr("Source: %1").arg(QUrl(sourceMRL).toDisplayString(QUrl::RemovePassword | QUrl::PreferLocalFile));
    for (int i = 1; i < ui.destTab->count(); ++i)
        if (auto *box = qobject_cast<VirtualDestBox *>(ui.destTab->widget(i)))
            lines << qtr("%1: %2").arg(ui.destTab->tabText(i), box->summary(mux));
    if (ui.localOutput->isChecked()) lines << qtr("Play on this device too.");
    lines << (ui.transcodeBox->isChecked() && choice ? qtr("Profile: %1").arg(choice->currentText()) : qtr("Keep source codecs. Output container: %1").arg(mux));
    lines << (ui.soutAll->isChecked() ? qtr("Tracks: all supported tracks.") : qtr("Tracks: selected tracks."));
    if (customOptions->isChecked())
        review->setText(qtr("Custom output options replace the destination and format settings. Check the options below before you start."));
    else review->setText(lines.join(QStringLiteral("\n\n")));
    QTimer::singleShot(0, this, &SoutDialog::ensureFocusVisible);
}
