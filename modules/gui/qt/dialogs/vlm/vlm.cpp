/*****************************************************************************
 * vlm.cpp : VLM Management
 ****************************************************************************
 * Copyright © 2008 the VideoLAN team
 *
 * Authors: Jean-Baptiste Kempf <jb@videolan.org>
 *          Jean-François Massol <jf.massol -at- gmail.com>
 *          Clément Sténac <zorglub@videolan.org>
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
 * Foundation, Inc., 51 Franklin street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#ifdef ENABLE_VLM
#include "dialogs/vlm/vlm.hpp"
#include "dialogs/dialogs_provider.hpp"
#include "dialogs/open/open.hpp"
#include "dialogs/sout/sout.hpp"
#include "maininterface/mainctx.hpp"
#include "util/interface_translator.hpp"
#include "util/qt_dirs.hpp"
#include <QApplication>
#include <QFileDialog>
#include <QHeaderView>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QPointer>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimer>
#include <vlc_variables.h>

namespace {
int dp(qt_intf_t *intf, int n) { return qRound(n * (intf->p_mi ? intf->p_mi->getIntfScaleFactor() : 1.)); }
QString token(QString value)
{
    value.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    value.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + value + QLatin1Char('"');
}
QStringList lines(const QString &text)
{
    QStringList result;
    for (const auto &line : text.split(QLatin1Char('\n')))
        if (!line.trimmed().isEmpty()) result.append(line);
    return result;
}
QStringList strings(int count, char **values)
{
    QStringList result;
    for (int i = 0; i < count; ++i) result.append(qfu(values[i]));
    return result;
}
QString outputChain(QString output)
{
    output = output.trimmed();
    if (output.startsWith(QLatin1String(":sout="))) output.remove(0, 6);
    else if (output.startsWith(QLatin1String("sout="))) output.remove(0, 5);
    return output;
}
struct Description {
    vlm_media_t value{};
    QByteArray name, output;
    QList<QByteArray> inputs, options;
    QList<char*> inputPointers, optionPointers;
    Description(qint64 id, const QString &title, const QStringList &sources, const QStringList &flags,
                const QString &destination, bool enabled, bool loop)
        : name(title.toUtf8()), output(destination.toUtf8())
    {
        for (const auto &s : sources) inputs.append(s.toUtf8());
        for (const auto &s : flags) options.append(s.toUtf8());
        for (auto &s : inputs) inputPointers.append(s.data());
        for (auto &s : options) optionPointers.append(s.data());
        value.id = id;
        value.psz_name = name.data();
        value.psz_output = output.isEmpty() ? nullptr : output.data();
        value.i_input = inputPointers.size(); value.ppsz_input = inputPointers.data();
        value.i_option = optionPointers.size(); value.ppsz_option = optionPointers.data();
        value.b_enabled = enabled; value.broadcast.b_loop = loop;
    }
};
struct Instances {
    vlm_media_instance_t **items = nullptr;
    int count = 0;
    Instances(vlm_t *vlm, qint64 id) { if (vlm_Control(vlm, VLM_GET_MEDIA_INSTANCES, int64_t(id), &items, &count)) count = 0; }
    ~Instances() { for (int i = 0; i < count; ++i) vlm_media_instance_Delete(items[i]); free(items); }
    int paused() const { int n = 0; for (int i = 0; i < count; ++i) n += items[i]->b_paused; return n; }
};
}

VLMDialog::VLMDialog(qt_intf_t *intf) : QVLCFrame(intf)
{
    setObjectName(QStringLiteral("redesignVLM"));
    setWindowRole("vlc-vlm");
    ui.setupUi(this);
    ready = true;
    ui.heading->setProperty("redesignHeading", true);
    ui.saveButton->setProperty("redesignPrimary", true);
    ui.feedback->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ui.inputLayout->setContentsMargins(0, 0, 0, 0);
    ui.outputLayout->setContentsMargins(0, 0, 0, 0);
    for (auto *edit : {ui.inputsText, ui.optionsText}) {
        edit->setTabChangesFocus(true);
        connect(edit, &QPlainTextEdit::cursorPositionChanged, this, [this] {
            // Wait for the text editor to finish scrolling its own viewport.
            QTimer::singleShot(0, this, &VLMDialog::ensureEditorFocusVisible);
        });
    }
    ui.editorActions->setStretch(0, 1);
    ui.broadcasts->setColumnCount(5);
    ui.broadcasts->verticalHeader()->hide();
    ui.broadcasts->setSortingEnabled(true);
    ui.broadcasts->setWordWrap(false);
    ui.broadcasts->setShowGrid(false);
    ui.broadcasts->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i < 5; ++i) ui.broadcasts->horizontalHeader()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    ui.broadcasts->sortItems(0, Qt::AscendingOrder);
    for (auto *button : findChildren<QPushButton*>()) button->setAutoDefault(false);
    vlm = vlm_New(vlc_object_instance(p_intf), nullptr);
    if (vlm) var_AddCallback(reinterpret_cast<vlc_object_t*>(vlm), "intf-event", &VLMDialog::vlmEvent, this);
    connect(ui.search, &QLineEdit::textChanged, this, &VLMDialog::filterRows);
    connect(ui.broadcasts, &QTableWidget::itemSelectionChanged, this, &VLMDialog::updateActions);
    connect(ui.broadcasts, &QTableWidget::itemDoubleClicked, this, [this] { startModifyVLMItem(); });
    connect(ui.newButton, &QPushButton::clicked, this, &VLMDialog::addVLMItem);
    connect(ui.editButton, &QPushButton::clicked, this, &VLMDialog::startModifyVLMItem);
    connect(ui.removeButton, &QPushButton::clicked, this, &VLMDialog::removeVLMItem);
    connect(ui.saveButton, &QPushButton::clicked, this, &VLMDialog::saveModifications);
    connect(ui.clearButton, &QPushButton::clicked, this, &VLMDialog::clearWidgets);
    connect(ui.playButton, &QPushButton::clicked, this, [this] { control(0); });
    connect(ui.pauseButton, &QPushButton::clicked, this, [this] { control(1); });
    connect(ui.stopButton, &QPushButton::clicked, this, [this] { control(2); });
    connect(ui.importButton, &QPushButton::clicked, this, &VLMDialog::importVLMConf);
    connect(ui.exportButton, &QPushButton::clicked, this, &VLMDialog::exportVLMConf);
    connect(ui.closeButton, &QPushButton::clicked, this, &VLMDialog::close);
    connect(ui.inputButton, &QPushButton::clicked, this, &VLMDialog::selectInput);
    connect(ui.outputButton, &QPushButton::clicked, this, &VLMDialog::selectOutput);
    connect(ui.advancedBox, &QGroupBox::toggled, ui.optionsText, &QWidget::setVisible);
    const auto changed = [this] { if (!loadingEditor) { dirty = true; updateActions(); } };
    connect(ui.nameLedit, &QLineEdit::textChanged, this, changed);
    connect(ui.outputLedit, &QLineEdit::textChanged, this, changed);
    connect(ui.inputsText, &QPlainTextEdit::textChanged, this, changed);
    connect(ui.optionsText, &QPlainTextEdit::textChanged, this, changed);
    connect(ui.enableCheck, &QtCheckboxChanged, this, changed);
    connect(ui.loopBCast, &QtCheckboxChanged, this, changed);
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, &VLMDialog::updateText);
    if (p_intf->p_mi) connect(p_intf->p_mi, &MainCtx::intfScaleFactorChanged, this, &VLMDialog::updateMetrics);
    clearWidgets(); updateText(); updateMetrics(); refresh();
    restoreWidgetPosition("VLM", QSize(dp(p_intf, 900), dp(p_intf, 740)));
    if (screen()) resize(size().boundedTo(screen()->availableGeometry().size()));
    for (auto *child : findChildren<QWidget*>()) child->installEventFilter(this);
}

VLMDialog::~VLMDialog()
{
    if (vlm) { var_DelCallback(reinterpret_cast<vlc_object_t*>(vlm), "intf-event", &VLMDialog::vlmEvent, this); vlm_Delete(vlm); }
    saveWidgetPosition("VLM");
}

int VLMDialog::vlmEvent(vlc_object_t*, const char*, vlc_value_t, vlc_value_t value, void *opaque)
{
    auto *self = static_cast<VLMDialog*>(opaque);
    const auto *event = static_cast<const vlm_event_t*>(value.p_address);
    if (!event) return VLC_SUCCESS;
    const auto id = event->id;
    const int type = event->i_type;
    const int state = type == VLM_EVENT_MEDIA_INSTANCE_STATE ? event->input_state : VLM_INIT_S;
    const QString instance = qfu(event->psz_instance_name), name = qfu(event->psz_name);
    QMetaObject::invokeMethod(self, [self, id, type, state, instance, name] {
        if (type == VLM_EVENT_MEDIA_INSTANCE_STATE) {
            // A stop notification follows an input failure. Keep the error
            // until this instance starts again or the user selects Stop.
            const bool keepError = self->states.value(id).value(instance) == VLM_ERROR_S
                && (state == VLM_INIT_S || state == VLM_END_S);
            if (!keepError) self->states[id][instance] = state;
            if (state == VLM_ERROR_S) self->feedback(qtr("Hakone cannot play %1. Check its input and output.").arg(name));
        } else if (type == VLM_EVENT_MEDIA_REMOVED) self->states.remove(id);
        self->queueRefresh();
    }, Qt::QueuedConnection);
    return VLC_SUCCESS;
}

void VLMDialog::queueRefresh()
{
    if (refreshQueued) return;
    refreshQueued = true;
    QTimer::singleShot(0, this, [this] { refreshQueued = false; refresh(); });
}

vlm_media_t *VLMDialog::find(qint64 id) const
{
    for (const auto &item : media) if (item->id == id) return item.get();
    return nullptr;
}
vlm_media_t *VLMDialog::selected() const
{
    const int row = ui.broadcasts->currentRow();
    if (row < 0 || ui.broadcasts->isRowHidden(row) || !ui.broadcasts->selectionModel()->hasSelection()) return nullptr;
    auto *item = ui.broadcasts->item(row, 0);
    return item ? find(item->data(Qt::UserRole).toLongLong()) : nullptr;
}

bool VLMDialog::refresh(qint64 selectId)
{
    if (!vlm) { feedback(qtr("VLM is not available.")); updateActions(); return false; }
    if (selectId < 0) if (auto *current = selected()) selectId = current->id;
    vlm_media_t **items = nullptr; int count = 0;
    if (vlm_Control(vlm, VLM_GET_MEDIAS, &items, &count)) { feedback(qtr("Hakone cannot read the broadcast list.")); return false; }
    media.clear();
    for (int i = 0; i < count; ++i) media.emplace_back(items[i], &vlm_media_Delete);
    free(items);
    const int position = ui.broadcasts->verticalScrollBar()->value();
    const QSignalBlocker blocker(ui.broadcasts);
    ui.broadcasts->setSortingEnabled(false);
    ui.broadcasts->setRowCount(count);
    for (int row = 0; row < count; ++row) {
        const auto *item = media[row].get();
        Instances instances(vlm, item->id);
        const auto state = states.value(item->id);
        bool failed = false, starting = instances.count > 0;
        for (auto value : state) failed |= value == VLM_ERROR_S;
        for (int i = 0; i < instances.count; ++i)
            starting &= state.value(qfu(instances.items[i]->psz_name), VLM_PLAYING_S) <= VLM_OPENING_S;
        const QString status = failed ? qtr("Error") : instances.count ? (instances.paused() == instances.count ? qtr("Paused") : starting ? qtr("Starting") : qtr("Playing")) : item->i_input ? qtr("Stopped") : qtr("No inputs");
        const QStringList values = {qfu(item->psz_name), status, QString::number(item->i_input), item->b_enabled ? qtr("Yes") : qtr("No"), item->broadcast.b_loop ? qtr("Yes") : qtr("No")};
        for (int col = 0; col < values.size(); ++col) ui.broadcasts->setItem(row, col, new QTableWidgetItem(values[col]));
        ui.broadcasts->item(row, 2)->setData(Qt::DisplayRole, item->i_input);
        ui.broadcasts->item(row, 0)->setData(Qt::UserRole, qlonglong(item->id));
        ui.broadcasts->item(row, 0)->setToolTip(strings(item->i_input, item->ppsz_input).join(QLatin1Char('\n')));
    }
    ui.broadcasts->setSortingEnabled(true);
    for (int row = 0; row < count; ++row) if (ui.broadcasts->item(row, 0)->data(Qt::UserRole).toLongLong() == selectId) ui.broadcasts->selectRow(row);
    ui.broadcasts->verticalScrollBar()->setValue(position);
    filterRows(); updateActions();
    return true;
}

void VLMDialog::filterRows()
{
    int count = 0;
    for (int row = 0; row < ui.broadcasts->rowCount(); ++row) {
        const auto *item = ui.broadcasts->item(row, 0);
        const QString text = item->text() + QLatin1Char(' ') + item->toolTip();
        const bool visible = text.contains(ui.search->text().trimmed(), Qt::CaseInsensitive);
        ui.broadcasts->setRowHidden(row, !visible);
        if (!visible && item->isSelected()) ui.broadcasts->clearSelection();
        count += visible;
    }
    ui.listInfo->setText(count ? qtr("%1 / %2 broadcasts").arg(count).arg(ui.broadcasts->rowCount()) : ui.search->text().trimmed().isEmpty() ? qtr("No broadcasts yet. Select New to add one.") : qtr("No broadcasts match your search."));
    updateActions();
}

QString VLMDialog::validationError() const
{
    if (!vlm) return qtr("VLM is not available.");
    const QString name = ui.nameLedit->text();
    if (name.trimmed().isEmpty()) return qtr("Enter a name.");
    if (name == QLatin1String("all") || name == QLatin1String("media")) return qtr("Choose a name other than all or media.");
    for (const auto &item : media) if (item->id != editingId && name == qfu(item->psz_name)) return qtr("This name is already in use.");
    if (editingId >= 0 && !find(editingId)) return qtr("This broadcast was removed. Select Cancel to start again.");
    for (const auto &value : {name, ui.outputLedit->text()})
        if (value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r'))) return qtr("Use one line for the name and output.");
    for (const auto &value : {name, ui.outputLedit->text(), ui.inputsText->toPlainText(), ui.optionsText->toPlainText()})
        if (value.contains(QChar(0))) return qtr("Remove the null character from this entry.");
    return {};
}

void VLMDialog::updateActions()
{
    auto *item = selected();
    ui.newButton->setEnabled(vlm); ui.importButton->setEnabled(vlm); ui.exportButton->setEnabled(vlm);
    ui.editButton->setEnabled(item); ui.removeButton->setEnabled(item);
    int active = 0, paused = 0;
    if (item && vlm) { Instances instances(vlm, item->id); active = instances.count; paused = instances.paused(); }
    ui.playButton->setEnabled(item && item->b_enabled && item->i_input && (!active || paused));
    ui.pauseButton->setEnabled(item && active > paused);
    ui.stopButton->setEnabled(item && active);
    ui.playButton->setToolTip(item && !item->i_input ? qtr("Add an input before you start this broadcast.") : QString());
    const auto error = validationError();
    ui.saveButton->setEnabled(dirty && error.isEmpty());
    ui.draftInfo->setText(dirty ? error.isEmpty() ? qtr("Changes are not saved.") : error : editingId >= 0 ? error : QString());
}

void VLMDialog::feedback(const QString &text)
{
    ui.feedback->setText(text);
    ui.feedback->setVisible(!text.isEmpty());
    updateMetrics();
}

void VLMDialog::edit(vlm_media_t *item)
{
    const qint64 id = item ? item->id : -1;
    if (dirty) {
        if (editingId != id) feedback(qtr("Save or cancel the current edit first."));
        ui.tabs->setCurrentIndex(1); return;
    }
    loadingEditor = true; editingId = id;
    ui.nameLedit->setText(item ? qfu(item->psz_name) : QString());
    ui.inputsText->setPlainText(item ? strings(item->i_input, item->ppsz_input).join(QLatin1Char('\n')) : QString());
    ui.optionsText->setPlainText(item ? strings(item->i_option, item->ppsz_option).join(QLatin1Char('\n')) : QString());
    ui.outputLedit->setText(item ? qfu(item->psz_output) : QString());
    ui.enableCheck->setChecked(item ? item->b_enabled : true);
    ui.loopBCast->setChecked(item && item->broadcast.b_loop);
    ui.advancedBox->setChecked(!ui.optionsText->toPlainText().isEmpty());
    ui.optionsText->setVisible(ui.advancedBox->isChecked());
    loadingEditor = false; dirty = false;
    ui.tabs->setTabText(1, item ? qtr("Edit broadcast") : qtr("New broadcast"));
    ui.tabs->setCurrentIndex(1); feedback({}); updateActions(); ui.nameLedit->setFocus();
}
void VLMDialog::addVLMItem() { edit(nullptr); }
void VLMDialog::startModifyVLMItem() { if (auto *item = selected()) edit(item); }
void VLMDialog::clearWidgets()
{
    dirty = false; edit(nullptr); dirty = false; ui.tabs->setCurrentIndex(0); updateActions();
}

void VLMDialog::saveModifications()
{
    const auto error = validationError();
    if (!error.isEmpty()) { feedback(error); return; }
    const auto output = outputChain(ui.outputLedit->text());
    Description draft(editingId < 0 ? 0 : editingId, ui.nameLedit->text(), lines(ui.inputsText->toPlainText()), lines(ui.optionsText->toPlainText()), output, ui.enableCheck->isChecked(), ui.loopBCast->isChecked());
    int64_t id = editingId;
    const bool active = editingId >= 0 && Instances(vlm, editingId).count > 0;
    const int result = editingId < 0 ? vlm_Control(vlm, VLM_ADD_MEDIA, &draft.value, &id) : vlm_Control(vlm, VLM_CHANGE_MEDIA, &draft.value);
    if (result) { refresh(); feedback(qtr("Hakone cannot save this broadcast. Check its name and try again.")); return; }
    if (!active) states.remove(id);
    clearWidgets(); refresh(id);
    feedback(active ? qtr("Saved. Stop and start this broadcast to use the new settings.") : qtr("Broadcast saved."));
}
void VLMDialog::removeVLMItem()
{
    auto *item = selected(); if (!item || !vlm) return;
    const auto id = item->id;
    if (vlm_Control(vlm, VLM_DEL_MEDIA, int64_t(id))) { feedback(qtr("Hakone cannot remove this broadcast.")); refresh(); return; }
    if (editingId == id && !dirty) clearWidgets();
    refresh(); feedback(qtr("Broadcast removed."));
}

void VLMDialog::control(int action)
{
    auto *item = selected(); if (!item || !vlm) return;
    const auto id = item->id;
    Instances instances(vlm, id);
    int result = VLC_SUCCESS;
    if (action == 2) result = vlm_Control(vlm, VLM_CLEAR_MEDIA_INSTANCES, int64_t(id));
    else if (action == 0 && !instances.count) result = vlm_Control(vlm, VLM_START_MEDIA_BROADCAST_INSTANCE, int64_t(id), nullptr, 0);
    else for (int i = 0; i < instances.count; ++i)
        if (instances.items[i]->b_paused == (action == 0))
            if (vlm_Control(vlm, VLM_PAUSE_MEDIA_INSTANCE, int64_t(id), instances.items[i]->psz_name)) result = VLC_EGENERIC;
    if (result) feedback(qtr("Hakone cannot complete this playback action."));
    else { if (action == 2) states.remove(id); feedback({}); }
    refresh(id);
}

void VLMDialog::selectInput()
{
    OpenDialog dialog(p_intf, windowHandle()); dialog.setActionFlag(OpenDialog::SELECT);
    if (dialog.exec() != QDialog::Accepted) return;
    auto sources = lines(ui.inputsText->toPlainText()); sources.append(dialog.getMRLs());
    ui.inputsText->setPlainText(sources.join(QLatin1Char('\n')));
    auto options = lines(ui.optionsText->toPlainText());
    for (auto option : dialog.getOptions().split(QStringLiteral(" :"), Qt::SkipEmptyParts)) {
        option = option.trimmed(); if (option.startsWith(QLatin1Char(':'))) option.remove(0, 1);
        if (!option.isEmpty() && !options.contains(option)) options.append(option);
    }
    ui.optionsText->setPlainText(options.join(QLatin1Char('\n')));
    if (!options.isEmpty()) ui.advancedBox->setChecked(true);
}
void VLMDialog::selectOutput()
{
    SoutDialog dialog(windowHandle(), p_intf, lines(ui.inputsText->toPlainText()).value(0));
    if (dialog.exec() != QDialog::Accepted) return;
    auto options = lines(ui.optionsText->toPlainText());
    for (const auto &option : dialog.getOptions()) {
        if (option.startsWith(QLatin1String("sout=")) || option.startsWith(QLatin1String(":sout="))) ui.outputLedit->setText(outputChain(option));
        else if (!options.contains(option)) options.append(option);
    }
    ui.optionsText->setPlainText(options.join(QLatin1Char('\n')));
    if (!options.isEmpty()) ui.advancedBox->setChecked(true);
}

bool VLMDialog::execute(const QString &command)
{
    if (!vlm) return false;
    vlm_message_t *message = nullptr;
    const int result = vlm_ExecuteCommand(vlm, qtu(command), &message);
    vlm_MessageDelete(message); return result == VLC_SUCCESS;
}

bool VLMDialog::importVLMConf()
{
    if (!vlm) return false;
    const auto path = QFileDialog::getOpenFileName(this, qtr("Import VLM configuration"), QVLCUserDir(VLC_DOCUMENTS_DIR), qtr("VLM files (*.vlm);;All files (*)"));
    if (path.isEmpty()) return false;
    const bool result = execute(QStringLiteral("load ") + token(path));
    refresh(); ui.tabs->setCurrentIndex(0);
    feedback(result ? qtr("Configuration loaded.") : qtr("Hakone cannot load the full file. Check the broadcast list."));
    return result;
}

bool VLMDialog::exportVLMConf()
{
    if (!vlm) return false;
    QPointer<VLMDialog> owner(this);
    QFileDialog dialog(nullptr, qtr("Export VLM configuration"), QVLCUserDir(VLC_DOCUMENTS_DIR), qtr("VLM files (*.vlm);;All files (*)"));
    dialog.setWindowModality(Qt::WindowModal);
    QVLCDialog::setWindowTransientParent(&dialog, windowHandle(), p_intf);
    connect(this, &QObject::destroyed, &dialog, &QDialog::reject);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setDefaultSuffix(QStringLiteral("vlm"));
    dialog.selectFile(lastExportPath.isEmpty() ? QStringLiteral("broadcasts.vlm") : lastExportPath);
    while (owner && dialog.exec() == QDialog::Accepted) {
        if (!owner || dialog.selectedFiles().isEmpty()) return false;
        const auto path = dialog.selectedFiles().first();
        if (!refresh()) return false;
        QString text = QStringLiteral("# VLC broadcast configuration\n");
        for (const auto &item : media) {
            const QString name = qfu(item->psz_name);
            QStringList values = strings(item->i_input, item->ppsz_input) + strings(item->i_option, item->ppsz_option);
            values << name << qfu(item->psz_output);
            for (const auto &value : values) if (value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r'))) { feedback(qtr("Hakone cannot save a value that contains a line break.")); return false; }
            text += QStringLiteral("new %1 broadcast %2%3\n").arg(token(name), item->b_enabled ? QStringLiteral("enabled") : QStringLiteral("disabled"), item->broadcast.b_loop ? QStringLiteral(" loop") : QString());
            for (const auto &input : strings(item->i_input, item->ppsz_input)) text += QStringLiteral("setup %1 input %2\n").arg(token(name), token(input));
            if (item->psz_output) text += QStringLiteral("setup %1 output %2\n").arg(token(name), token(qfu(item->psz_output)));
            for (const auto &option : strings(item->i_option, item->ppsz_option)) text += QStringLiteral("setup %1 option %2\n").arg(token(name), token(option));
        }
        bool saved;
        QString error;
        {
            QSaveFile file(path);
            const auto bytes = text.toUtf8();
            saved = file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
            if (!saved) error = file.errorString();
        }
        if (!saved) {
            feedback(qtr("Hakone did not save the configuration."));
            const auto answer = DialogsProvider::getInstance(p_intf)->messageDialog(
                qtr("Hakone cannot save this file.\n\n%1\n\n%2\n\nSelect another file or folder.")
                    .arg(error, toNativeSeparators(path)),
                qtr("Export VLM configuration"), QMessageBox::Yes | QMessageBox::Cancel,
                QMessageBox::Cancel, QMessageBox::Warning, this, qtr("Select file"));
            if (!owner || answer != QMessageBox::Yes) return false;
            dialog.selectFile(path);
            continue;
        }
        lastExportPath = path;
        feedback(dirty
            ? qtr("Hakone exported the saved broadcasts. Save the changes before the next export.")
            : qtr("Configuration saved."));
        return true;
    }
    return false;
}

void VLMDialog::updateText()
{
    ui.retranslateUi(this);
    ui.broadcasts->setHorizontalHeaderLabels({qtr("Name"), qtr("State"), qtr("Inputs"), qtr("Enabled"), qtr("Repeat")});
    ui.tabs->setTabText(1, editingId < 0 ? qtr("New broadcast") : qtr("Edit broadcast"));
    ui.search->setAccessibleName(qtr("Search broadcasts"));
    ui.enableCheck->setToolTip(qtr("Allow this broadcast to start."));
    refresh(); updateActions();
}
void VLMDialog::updateMetrics()
{
    const auto available = screen() ? screen()->availableGeometry().size() : QSize(1280, 720);
    const bool shortScreen = available.height() < dp(p_intf, 460);
    const int margin = dp(p_intf, shortScreen ? 12 : 24);
    ui.rootLayout->setContentsMargins(margin, margin, margin, margin);
    ui.rootLayout->setSpacing(dp(p_intf, shortScreen ? 4 : 12));
    ui.feedback->ensurePolished();
    ui.feedback->setMinimumHeight(ui.feedback->text().isEmpty() ? 0
        : ui.feedback->heightForWidth(qMax(1, width() - 2 * margin)));
    ui.inputLayout->setSpacing(dp(p_intf, 6)); ui.outputLayout->setSpacing(dp(p_intf, 6));
    ui.inputsText->setMinimumHeight(dp(p_intf, 100)); ui.optionsText->setMinimumHeight(dp(p_intf, 100));
    ui.broadcasts->setMinimumHeight(dp(p_intf, 120)); ui.broadcasts->verticalHeader()->setDefaultSectionSize(dp(p_intf, 36));
    // Keep the name readable on small screens. All fields remain in the editor.
    ui.broadcasts->setColumnHidden(2, width() < dp(p_intf, 580));
    ui.broadcasts->setColumnHidden(3, width() < dp(p_intf, 720));
    ui.broadcasts->setColumnHidden(4, width() < dp(p_intf, 720));
    setMinimumSize(QSize(dp(p_intf, 440), dp(p_intf, 460)).boundedTo(available));
    ui.form->setRowWrapPolicy(width() < dp(p_intf, 650) ? QFormLayout::WrapAllRows : QFormLayout::WrapLongRows);
    QTimer::singleShot(0, this, &VLMDialog::ensureEditorFocusVisible);
}
void VLMDialog::ensureEditorFocusVisible()
{
    auto *widget = focusWidget();
    if (!isVisible() || !widget || !ui.editorBody->isAncestorOf(widget)) return;
    QRect rect;
    if (auto *edit = qobject_cast<QPlainTextEdit*>(widget)) {
        if (edit->height() + dp(p_intf, 12) <= ui.editorScroll->viewport()->height())
            rect = QRect(edit->mapTo(ui.editorBody, QPoint()), edit->size());
        else {
            rect = edit->cursorRect();
            rect.moveTopLeft(edit->viewport()->mapTo(ui.editorBody, rect.topLeft()));
        }
    } else {
        rect = QRect(widget->mapTo(ui.editorBody, QPoint()), widget->size());
        // A checkable group needs its title in view, even when it is expanded.
        if (qobject_cast<QGroupBox*>(widget))
            rect.setHeight(widget->fontMetrics().height() + dp(p_intf, 12));
    }
    const auto viewport = ui.editorScroll->viewport()->size();
    ui.editorScroll->ensureVisible(rect.center().x(), rect.center().y(),
        qMin(rect.width() / 2 + dp(p_intf, 6), viewport.width() / 2),
        qMin(rect.height() / 2 + dp(p_intf, 6), viewport.height() / 2));
}
void VLMDialog::resizeEvent(QResizeEvent *event) { QVLCFrame::resizeEvent(event); if (ready) updateMetrics(); }
void VLMDialog::toggleVisible() { refresh(); QVLCFrame::toggleVisible(); }
void VLMDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) { close(); event->accept(); }
    else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) { if (auto *button = qobject_cast<QPushButton*>(focusWidget())) button->click(); event->accept(); }
    else QVLCFrame::keyPressEvent(event);
}
bool VLMDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::FocusIn)
        QTimer::singleShot(0, this, &VLMDialog::ensureEditorFocusVisible);
    if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent*>(event);
        const bool find = key->matches(QKeySequence::Find);
        const bool clear = watched == ui.search && key->key() == Qt::Key_Escape && !ui.search->text().isEmpty();
        auto *button = qobject_cast<QPushButton*>(watched);
        const bool activate = button && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter);
        if (find || clear || activate) {
            if (event->type() == QEvent::KeyPress) {
                if (activate) button->click();
                else if (find) { ui.tabs->setCurrentIndex(0); ui.search->setFocus(); ui.search->selectAll(); }
                else ui.search->clear();
            }
            key->accept(); return true;
        }
    }
    return QVLCFrame::eventFilter(watched, event);
}
#endif
