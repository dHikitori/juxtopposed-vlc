/*****************************************************************************
 * plugins.cpp : Plug-ins and extensions listing
 ****************************************************************************
 * Copyright (C) 2008-2010 the VideoLAN team
 *
 * Authors: Jean-Baptiste Kempf <jb (at) videolan.org>
 *          Jean-Philippe André <jpeg (at) videolan.org>
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

#include "plugins.hpp"

#include "dialogs/extensions/extensions_manager.hpp"
#include "network/addonsmodel.hpp"
#include "util/imagehelper.hpp"
#include "util/redesign_widget_theme.hpp"
#include "util/interface_translator.hpp"
#include "util/color_scheme_model.hpp"
#include "util/scroll_focus_tracker.hpp"
#include "maininterface/mainctx.hpp"

#include <cassert>

#include <vlc_modules.h>
#include <vlc_extensions.h>

#include <QTreeWidget>
#include <QStringList>
#include <QTabWidget>
#include <QHeaderView>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QComboBox>
#include <QHBoxLayout>
#include <QSpacerItem>
#include <QListView>
#include <QPainter>
#include <QStyleOptionViewItem>
#include <QKeyEvent>
#include <QPushButton>
#include <QCheckBox>
#include <QPixmap>
#include <QStylePainter>
#include <QProgressBar>
#include <QTextEdit>
#include <QUrl>
#include <QMimeData>
#include <QPainterPath>
#include <QApplication>
#include <QSignalBlocker>
#include <QShortcut>
#include <QResizeEvent>
#include <QTextCursor>
#include <QTextBlock>
#include <QTextFragment>
#include <QScreen>
#include <QStyleHints>
#include <QTextDocument>
#include <QTextBrowser>
#include <QScrollBar>
#include <QScrollArea>

//match the image source (width/height)
#define SCORE_ICON_WIDTH_SCALE 4
#define SPINNER_SIZE 32

static QPixmap *loadPixmapFromData( char *, int size );


namespace {
int pluginDp(qt_intf_t *intf, int n)
{
    return qRound(n * (intf->p_mi ? intf->p_mi->getIntfScaleFactor() : 1.));
}
QString plainDescription(const QString &text)
{
    if (!Qt::mightBeRichText(text)) return text.simplified();
    QTextDocument document;
    document.setHtml(text);
    return document.toPlainText().simplified();
}
QVBoxLayout *pluginBody(QVBoxLayout *root)
{
    auto *scroll = new QScrollArea(root->parentWidget());
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *body = new QWidget(scroll);
    auto *layout = new QVBoxLayout(body);
    layout->setContentsMargins(0, 0, 0, 0);
    scroll->setWidget(body);
    root->addWidget(scroll, 1);
    return layout;
}

void preservePluginRows(QAbstractItemView *view, qt_intf_t *intf)
{
    const auto metrics = [view, intf] { view->setMinimumHeight(pluginDp(intf, 120)); };
    metrics();
    if (intf->p_mi) QObject::connect(intf->p_mi, &MainCtx::intfScaleFactorChanged, view, metrics);
}

class PluginActionRow final : public QWidget
{
public:
    explicit PluginActionRow(QWidget *parent) : QWidget(parent), box(new QBoxLayout(QBoxLayout::LeftToRight, this))
    {
        box->setContentsMargins(0, 0, 0, 0);
    }
    QBoxLayout *box;
protected:
    bool event(QEvent *event) override
    {
        const bool result = QWidget::event(event);
        if (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest)
        {
            int needed = 0, widgets = 0;
            for (int i = 0; i < box->count(); ++i)
                if (auto *widget = box->itemAt(i)->widget())
                {
                    needed += widget->sizeHint().width();
                    widgets++;
                }
            needed += qMax(0, widgets - 1) * box->spacing();
            const auto direction = needed > width() ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight;
            if (box->direction() != direction) box->setDirection(direction);
        }
        return result;
    }
};
class ExtensionSearch final : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    QString query;
    void search(const QString &text) { query = text.trimmed(); invalidateFilter(); }
protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override
    {
        const auto index = sourceModel()->index(row, 0, parent);
        const QString text = index.data().toString() + QLatin1Char(' ')
            + plainDescription(index.data(ExtensionListModel::SummaryRole).toString())
            + QLatin1Char(' ') + index.data(ExtensionListModel::AuthorRole).toString()
            + QLatin1Char(' ') + index.data(ExtensionListModel::FilenameRole).toString();
        return text.contains(query, Qt::CaseInsensitive);
    }
};
}

PluginDialog::PluginDialog( qt_intf_t *_p_intf ) : QVLCFrame( _p_intf )
{
    setObjectName(QStringLiteral("redesignPlugins"));
    setProperty("redesignDialog", true);
    setWindowRole("vlc-plugins");
    auto *root = new QVBoxLayout(this);
    auto *heading = new QLabel(this);
    heading->setProperty("redesignHeading", true);
    root->addWidget(heading);
    auto *help = new QLabel(this);
    help->setObjectName(QStringLiteral("pluginHelp"));
    help->setWordWrap(true);
    root->addWidget(help);
    tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("pluginTabs"));
    tabs->setUsesScrollButtons(true);
    tabs->setElideMode(Qt::ElideRight);
    tabs->addTab(addonsTab = new AddonsTab(p_intf), {});
    tabs->addTab(extensionTab = new ExtensionTab(p_intf), {});
    tabs->addTab(pluginTab = new PluginTab(p_intf), {});
    root->addWidget(tabs, 1);
    auto *footer = new QHBoxLayout;
    footer->addStretch();
    auto *close = new QPushButton(this);
    close->setObjectName(QStringLiteral("pluginsClose"));
    close->setAutoDefault(false);
    footer->addWidget(close);
    root->addLayout(footer);
    connect(close, &QPushButton::clicked, this, &PluginDialog::close);
    const auto text = [this, heading, help, close] {
        setWindowTitle(qtr("Plugins and extensions"));
        heading->setText(qtr("Plugins and extensions"));
        help->setText(qtr("Manage add-ons, view local extensions, or find a built-in module."));
        heading->setToolTip(help->text());
        heading->setAccessibleDescription(help->text());
        tabs->setTabText(0, qtr("Add-ons"));
        tabs->setTabText(1, qtr("Local extensions"));
        tabs->setTabText(2, qtr("Built-in modules"));
        for (int i = 0; i < tabs->count(); ++i) tabs->setTabToolTip(i, tabs->tabText(i));
        close->setText(qtr("&Close"));
    };
    text();
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, text);
    applyRedesignWidgetTheme(this, p_intf->p_mi);
    if (p_intf->p_mi) connect(p_intf->p_mi, &MainCtx::intfScaleFactorChanged, this, &PluginDialog::updateMetrics);
    updateMetrics();
    restoreWidgetPosition("PluginsDialog", QSize(pluginDp(p_intf, 740), pluginDp(p_intf, 620)));
    if (screen()) resize(size().boundedTo(screen()->availableGeometry().size()));
    for (auto *child : findChildren<QWidget*>()) child->installEventFilter(this);
    trackScrollFocus(this, p_intf->p_mi);
}

void PluginDialog::updateMetrics()
{
    // Reserve room for the active form before the optional introduction.
    const bool compact = height() < pluginDp(p_intf, 520);
    const int margin = pluginDp(p_intf, compact ? 16 : 24);
    layout()->setContentsMargins(margin, margin, margin, margin);
    layout()->setSpacing(pluginDp(p_intf, compact ? 8 : 12));
    findChild<QLabel*>(QStringLiteral("pluginHelp"))->setVisible(!compact);
    const auto available = screen() ? screen()->availableGeometry().size() : QSize(1280, 720);
    setMinimumSize(QSize(pluginDp(p_intf, 400), pluginDp(p_intf, 460)).boundedTo(available));
}

void PluginDialog::resizeEvent(QResizeEvent *event)
{
    QVLCFrame::resizeEvent(event);
    updateMetrics();
}

QLineEdit *PluginDialog::activeSearch() const
{
    auto *current = tabs->currentWidget();
    return current ? current->findChild<QLineEdit*>(QStringLiteral("pluginSearch")) : nullptr;
}

bool PluginDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)
    {
        auto *key = static_cast<QKeyEvent*>(event);
        auto *search = activeSearch();
        const bool find = key->matches(QKeySequence::Find);
        auto *button = qobject_cast<QPushButton*>(watched);
        const bool activate = button && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter);
        const bool clear = watched == search && key->key() == Qt::Key_Escape && search && !search->text().isEmpty();
        if ((search && (find || clear)) || activate)
        {
            if (event->type() == QEvent::KeyPress)
            {
                if (activate) button->click();
                else if (find) {
                    search->setFocus();
                    search->selectAll();
                    // Focus may already be in a search scrolled out of view.
                    for (auto *parent = search->parentWidget(); parent && parent != this; parent = parent->parentWidget())
                        if (auto *scroll = qobject_cast<QScrollArea*>(parent)) {
                            // Reveal the full field, including its focus border.
                            const QRect rect(search->mapTo(scroll->widget(), QPoint()), search->size());
                            const int margin = pluginDp(p_intf, 6);
                            scroll->ensureVisible(rect.center().x(), rect.center().y(),
                                qMin(rect.width() / 2 + margin, scroll->viewport()->width() / 2),
                                qMin(rect.height() / 2 + margin, scroll->viewport()->height() / 2));
                        }
                }
                else search->clear();
            }
            key->accept();
            return true;
        }
    }
    return QVLCFrame::eventFilter(watched, event);
}

void PluginDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) { close(); event->accept(); }
    else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) event->accept();
    else QVLCFrame::keyPressEvent(event);
}

PluginDialog::~PluginDialog()
{
    delete pluginTab;
    delete extensionTab;
    delete addonsTab;
    saveWidgetPosition( "PluginsDialog" );
}

/* Plugins tab */

PluginTab::PluginTab(qt_intf_t *intf) : QVLCFrame(intf)
{
    auto *outer = new QVBoxLayout(this);
    auto *root = pluginBody(outer);
    edit = new QLineEdit(this);
    edit->setObjectName(QStringLiteral("pluginSearch"));
    edit->setClearButtonEnabled(true);
    root->addWidget(edit);
    treePlugins = new QTreeWidget(this);
    treePlugins->setObjectName(QStringLiteral("builtInModules"));
    preservePluginRows(treePlugins, p_intf);
    treePlugins->setRootIsDecorated(false);
    treePlugins->setAlternatingRowColors(true);
    treePlugins->setSelectionMode(QAbstractItemView::SingleSelection);
    treePlugins->header()->setSectionsMovable(false);
    treePlugins->header()->setSortIndicatorShown(true);
    treePlugins->setColumnCount(3);
    FillTree();
    treePlugins->setSortingEnabled(true);
    treePlugins->sortByColumn(CAPABILITY, Qt::AscendingOrder);
    treePlugins->header()->setSectionResizeMode(NAME, QHeaderView::Stretch);
    treePlugins->header()->setSectionResizeMode(CAPABILITY, QHeaderView::ResizeToContents);
    treePlugins->header()->setSectionResizeMode(SCORE, QHeaderView::ResizeToContents);
    root->addWidget(treePlugins, 1);
    searchInfo = new QLabel(this);
    searchInfo->setObjectName(QStringLiteral("pluginSearchInfo"));
    searchInfo->setWordWrap(true);
    root->addWidget(searchInfo);
    connect(edit, &QLineEdit::textChanged, this, &PluginTab::search);
    const auto text = [this] {
        edit->setPlaceholderText(qtr("Search names or capabilities"));
        edit->setAccessibleName(qtr("Search built-in modules"));
        treePlugins->setHeaderLabels({qtr("Name"), qtr("Capability"), qtr("Score")});
        search(edit->text());
    };
    text();
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, text);
}

inline void PluginTab::FillTree()
{
    size_t count;
    module_t **p_list = module_list_get( &count );

    for( unsigned int i = 0; i < count; i++ )
    {
        module_t *p_module = p_list[i];

        QStringList qs_item;
        qs_item << qfu( module_GetLongName( p_module ) )
                << qfu( module_get_capability( p_module ) )
                << QString::number( module_get_score( p_module ) );
#ifndef DEBUG
        if( qs_item.at(1).isEmpty() ) continue;
#endif

        QTreeWidgetItem *item = new PluginTreeItem( qs_item );
        item->setToolTip(NAME, qs_item.join(QStringLiteral("\n")));
        item->setTextAlignment(SCORE, Qt::AlignRight | Qt::AlignVCenter);
        treePlugins->addTopLevelItem( item );
    }
    module_list_free( p_list );
}

void PluginTab::search(const QString &query)
{
    int visible = 0;
    for (int row = 0; row < treePlugins->topLevelItemCount(); ++row)
    {
        auto *item = treePlugins->topLevelItem(row);
        const bool match = item->text(NAME).contains(query.trimmed(), Qt::CaseInsensitive)
            || item->text(CAPABILITY).contains(query.trimmed(), Qt::CaseInsensitive);
        item->setHidden(!match);
        if (!match && item->isSelected()) item->setSelected(false);
        visible += match;
    }
    searchInfo->setText(visible ? qtr("%1 / %2 modules").arg(visible).arg(treePlugins->topLevelItemCount())
                               : qtr("No modules match your search."));
}

PluginTab::~PluginTab() = default;

void PluginTab::keyPressEvent( QKeyEvent *keyEvent )
{
    if( keyEvent->key() == Qt::Key_Return ||
        keyEvent->key() == Qt::Key_Enter )
        keyEvent->accept();
    else
        keyEvent->ignore();
}

bool PluginTreeItem::operator< ( const QTreeWidgetItem & other ) const
{
    int col = treeWidget()->sortColumn();
    if( col == PluginTab::SCORE )
        return text( col ).toInt() < other.text( col ).toInt();
    else if ( col == PluginTab::CAPABILITY )
    {
        if ( text( PluginTab::CAPABILITY ) == other.text( PluginTab::CAPABILITY ) )
            return text( PluginTab::NAME ) < other.text( PluginTab::NAME );
        else
            return text( PluginTab::CAPABILITY ) < other.text( PluginTab::CAPABILITY );
    }
    return text( col ) < other.text( col );
}

/* Extensions tab */
ExtensionTab::ExtensionTab(qt_intf_t *intf) : QVLCFrame(intf)
{
    auto *outer = new QVBoxLayout(this);
    auto *root = pluginBody(outer);
    searchInput = new QLineEdit(this);
    searchInput->setObjectName(QStringLiteral("pluginSearch"));
    searchInput->setClearButtonEnabled(true);
    root->addWidget(searchInput);
    extList = new QListView(this);
    extList->setObjectName(QStringLiteral("localExtensions"));
    preservePluginRows(extList, p_intf);
    extList->setItemDelegate(new ExtensionItemDelegate(extList));
    extList->setAlternatingRowColors(true);
    extList->setSelectionMode(QAbstractItemView::SingleSelection);
    extList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(extList, 1);
    listInfo = new QLabel(this);
    listInfo->setObjectName(QStringLiteral("extensionListInfo"));
    listInfo->setWordWrap(true);
    outer->addWidget(listInfo);
    auto *proxy = new ExtensionSearch(this);
    proxy->setSourceModel(new ExtensionListModel(proxy, ExtensionsManager::getInstance(p_intf)));
    extList->setModel(proxy);
    connect(searchInput, &QLineEdit::textChanged, this, [this, proxy](const QString &text) {
        proxy->search(text);
        updateButtons();
    });
    auto *actions = new PluginActionRow(this);
    auto *buttons = actions->box;
    auto *reload = new QPushButton(this);
    reload->setObjectName(QStringLiteral("extensionsReload"));
    butMoreInfo = new QPushButton(this);
    butMoreInfo->setObjectName(QStringLiteral("extensionInfo"));
    for (auto *button : {reload, butMoreInfo}) button->setAutoDefault(false);
    buttons->addWidget(reload);
    buttons->addStretch();
    buttons->addWidget(butMoreInfo);
    outer->addWidget(actions);
    connect(reload, &QPushButton::clicked, this, [this] {
        ExtensionsManager::getInstance(p_intf)->reloadExtensions();
        updateButtons();
    });
    connect(butMoreInfo, &QPushButton::clicked, this, &ExtensionTab::moreInformation);
    connect(extList, &QListView::activated, this, &ExtensionTab::moreInformation);
    connect(extList->selectionModel(), &QItemSelectionModel::selectionChanged, this, &ExtensionTab::updateButtons);
    connect(proxy, &QAbstractItemModel::modelReset, this, &ExtensionTab::updateButtons);
    const auto text = [this, reload] {
        searchInput->setPlaceholderText(qtr("Search local extensions"));
        searchInput->setAccessibleName(qtr("Search local extensions"));
        reload->setText(qtr("Reload"));
        reload->setToolTip(qtr("Reload local extension files. This closes active extensions."));
        butMoreInfo->setText(qtr("More information"));
        updateButtons();
    };
    text();
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, text);
}

ExtensionTab::~ExtensionTab() = default;

void ExtensionTab::updateButtons()
{
    butMoreInfo->setEnabled(extList->currentIndex().isValid() && extList->selectionModel()->hasSelection());
    if (extList->model()->rowCount() > 0) listInfo->setText(extList->model()->rowCount() == 1 ? qtr("1 extension") : qtr("%1 extensions").arg(extList->model()->rowCount()));
    else if (!searchInput->text().trimmed().isEmpty()) listInfo->setText(qtr("No extensions match your search."));
    else if (ExtensionsManager::getInstance(p_intf)->cannotLoad()) listInfo->setText(qtr("VLC cannot load local extensions in this build."));
    else listInfo->setText(qtr("No local extensions were found."));
}

void ExtensionTab::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) event->accept();
    else event->ignore();
}

void ExtensionTab::moreInformation()
{
    const auto selected = extList->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    QWidget *owner = window();
    ExtensionInfoDialog dialog(selected.first(), p_intf, owner ? owner->windowHandle() : nullptr);
    dialog.exec();
}

/* Add-ons tab */
AddonsTab::AddonsTab(qt_intf_t *intf) : QVLCFrame(intf)
{
    auto *outer = new QVBoxLayout(this);
    auto *root = pluginBody(outer);
    searchInput = new QLineEdit(this);
    searchInput->setObjectName(QStringLiteral("pluginSearch"));
    searchInput->setClearButtonEnabled(true);
    root->addWidget(searchInput);
    category = new QComboBox(this);
    category->setObjectName(QStringLiteral("addonCategory"));
    root->addWidget(category);
    installedOnly = new QCheckBox(this);
    installedOnly->setObjectName(QStringLiteral("addonsInstalledOnly"));
    root->addWidget(installedOnly);
    loadingIndicator = new QProgressBar(this);
    loadingIndicator->setRange(0, 0);
    loadingIndicator->setTextVisible(false);
    loadingIndicator->setMaximumHeight(pluginDp(p_intf, 4));
    root->addWidget(loadingIndicator);
    m_model = std::make_unique<AddonsModel>(this);
    m_model->setObjectName(QStringLiteral("nativeAddonsModel"));
    m_model->classBegin();
    m_model->setCtx(p_intf->p_mi);
    addonsView = new QListView(this);
    addonsView->setObjectName(QStringLiteral("nativeAddonsList"));
    preservePluginRows(addonsView, p_intf);
    addonsView->setModel(m_model.get());
    addonsView->setItemDelegate(new AddonItemDelegate(addonsView));
    addonsView->setAlternatingRowColors(true);
    addonsView->setSelectionMode(QAbstractItemView::SingleSelection);
    addonsView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    addonsView->setAcceptDrops(true);
    addonsView->setDefaultDropAction(Qt::CopyAction);
    addonsView->setDragDropMode(QAbstractItemView::DropOnly);
    addonsView->viewport()->installEventFilter(this);
    root->addWidget(addonsView, 1);
    listInfo = new QLabel(this);
    listInfo->setObjectName(QStringLiteral("addonsListInfo"));
    listInfo->setWordWrap(true);
    outer->addWidget(listInfo);
    auto *actions = new PluginActionRow(this);
    auto *buttons = actions->box;
    repositoryButton = new QPushButton(this);
    repositoryButton->setObjectName(QStringLiteral("addonsFindOnline"));
    infoButton = new QPushButton(this);
    infoButton->setObjectName(QStringLiteral("addonInfo"));
    manageButton = new QPushButton(this);
    manageButton->setObjectName(QStringLiteral("addonManage"));
    manageButton->setProperty("redesignPrimary", true);
    for (auto *button : {repositoryButton, infoButton, manageButton}) button->setAutoDefault(false);
    buttons->addWidget(repositoryButton);
    buttons->addStretch();
    buttons->addWidget(infoButton);
    buttons->addWidget(manageButton);
    outer->addWidget(actions);
    connect(repositoryButton, &QPushButton::clicked, this, &AddonsTab::reposync);
    connect(infoButton, &QPushButton::clicked, this, &AddonsTab::moreInformation);
    connect(addonsView, &QListView::activated, this, &AddonsTab::moreInformation);
    connect(manageButton, &QPushButton::clicked, this, [this] {
        const auto index = addonsView->currentIndex();
        if (!manageButton->isEnabled() || !index.isValid()) return;
        const auto id = index.data(AddonsModel::IDENTIFIER).toString();
        if (index.data(AddonsModel::STATE).toInt() == int(AddonsModel::State::STATE_INSTALLED)) m_model->removeAddon(id);
        else m_model->installAddon(id);
        updateControls();
    });
    connect(searchInput, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_model->setSearchPattern(text);
        updateControls();
    });
    connect(category, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        m_model->setTypeFilter(static_cast<AddonsModel::Type>(category->currentData().toInt()));
        updateControls();
    });
    connect(installedOnly, &QtCheckboxChanged, this, &AddonsTab::installChecked);
    connect(addonsView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &AddonsTab::updateControls);
    connect(m_model.get(), &AddonsModel::catalogChanged, this, &AddonsTab::updateControls);
    connect(m_model.get(), &AddonsModel::loadingChanged, this, &AddonsTab::updateControls);
    connect(m_model.get(), &AddonsModel::countChanged, this, &AddonsTab::updateControls);
    connect(m_model.get(), &QAbstractItemModel::modelAboutToBeReset, this, [this] {
        const auto id = addonsView->currentIndex().data(AddonsModel::IDENTIFIER).toString();
        if (!id.isEmpty()) restoreSelectionId = id;
    });
    connect(m_model.get(), &QAbstractItemModel::modelReset, this, &AddonsTab::updateControls);
    connect(m_model.get(), &QAbstractItemModel::dataChanged, this, &AddonsTab::updateControls);
    connect(m_model.get(), &QAbstractItemModel::rowsInserted, this, &AddonsTab::updateControls);
    connect(m_model.get(), &QAbstractItemModel::rowsRemoved, this, &AddonsTab::updateControls);
    const auto text = [this] {
        searchInput->setPlaceholderText(qtr("Search add-ons"));
        searchInput->setAccessibleName(qtr("Search add-ons"));
        category->setAccessibleName(qtr("Add-on type"));
        const QSignalBlocker blocked(category);
        const int type = category->currentIndex() < 0 ? int(AddonsModel::Type::TYPE_NONE) : category->currentData().toInt();
        category->clear();
        category->addItem(qtr("All types"), int(AddonsModel::Type::TYPE_NONE));
        for (auto type : {AddonsModel::Type::TYPE_EXTENSION, AddonsModel::Type::TYPE_SKIN2,
             AddonsModel::Type::TYPE_PLAYLIST_PARSER, AddonsModel::Type::TYPE_SERVICE_DISCOVERY,
             AddonsModel::Type::TYPE_INTERFACE, AddonsModel::Type::TYPE_META})
            if (AddonsModel::isTypeVisible(type)) category->addItem(AddonsModel::getLabelForType(type), int(type));
        category->setCurrentIndex(qMax(0, category->findData(type)));
        installedOnly->setText(qtr("Only installed"));
        updateControls();
    };
    text();
    if (auto *translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, text);
    m_model->componentComplete();
    updateControls();
}

AddonsTab::~AddonsTab() = default;

void AddonsTab::updateControls()
{
    if (!restoreSelectionId.isEmpty() && !addonsView->currentIndex().isValid())
        for (int row = 0; row < m_model->rowCount(); ++row)
        {
            const auto candidate = m_model->index(row, 0);
            if (candidate.data(AddonsModel::IDENTIFIER).toString() == restoreSelectionId)
            {
                restoreSelectionId.clear();
                addonsView->setCurrentIndex(candidate);
                break;
            }
        }
    const auto index = addonsView->currentIndex();
    const bool selected = index.isValid() && addonsView->selectionModel()->hasSelection();
    const auto state = static_cast<AddonsModel::State>(index.data(AddonsModel::STATE).toInt());
    const auto details = selected ? m_model->addonById(index.data(AddonsModel::IDENTIFIER).toString()) : QVariantMap{};
    const bool busy = details.value(QStringLiteral("pending")).toBool()
        || state == AddonsModel::State::STATE_INSTALLING || state == AddonsModel::State::STATE_UNINSTALLING;
    const bool installed = state == AddonsModel::State::STATE_INSTALLED;
    manageButton->setText(busy ? (installed || state == AddonsModel::State::STATE_UNINSTALLING ? qtr("Removing...") : qtr("Installing..."))
                               : (installed ? qtr("Remove") : qtr("Install")));
    manageButton->setEnabled(selected && !busy && index.data(AddonsModel::MANAGEABLE).toBool()
        && (installed || !index.data(AddonsModel::BROKEN).toBool()));
    infoButton->setText(qtr("More information"));
    infoButton->setEnabled(selected);
    repositoryButton->setText(onlineCatalogRequested ? qtr("Refresh") : qtr("Find online"));
    repositoryButton->setEnabled(!m_model->loading());
    loadingIndicator->setVisible(m_model->loading());
    if (!m_model->errorMessage().isEmpty()) listInfo->setText(m_model->errorMessage());
    else if (m_model->loading()) listInfo->setText(qtr("Loading add-ons..."));
    else if (m_model->rowCount()) listInfo->setText(m_model->rowCount() == 1 ? qtr("1 add-on") : qtr("%1 add-ons").arg(m_model->rowCount()));
    else if (!searchInput->text().trimmed().isEmpty() || category->currentData().toInt() != int(AddonsModel::Type::TYPE_NONE) || installedOnly->isChecked())
        listInfo->setText(qtr("No add-ons match your filters."));
    else if (onlineCatalogRequested) listInfo->setText(qtr("No add-ons were found."));
    else listInfo->setText(qtr("No local add-ons were found. Select Find online to load the add-on list."));
}

void AddonsTab::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) event->accept();
    else event->ignore();
}

bool AddonsTab::eventFilter(QObject *object, QEvent *event)
{
    if (object != addonsView->viewport()) return false;
    if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove || event->type() == QEvent::Drop)
    {
        auto *drop = static_cast<QDropEvent*>(event);
        const auto urls = drop->mimeData()->urls();
        const bool valid = drop->proposedAction() == Qt::CopyAction && urls.size() == 1
            && urls.first().isLocalFile() && urls.first().path().endsWith(QLatin1String(".vlp"), Qt::CaseInsensitive);
        if (!valid) { drop->ignore(); return true; }
        if (event->type() == QEvent::Drop) m_model->loadFromExternalRepository(urls.first());
        drop->acceptProposedAction();
        return true;
    }
    return QVLCFrame::eventFilter(object, event);
}

void AddonsTab::moreInformation()
{
    const auto selected = addonsView->selectionModel()->selectedIndexes();
    if (selected.isEmpty()) return;
    QWidget *owner = window();
    AddonInfoDialog dialog(selected.first(), p_intf, owner ? owner->windowHandle() : nullptr);
    dialog.exec();
}

void AddonsTab::installChecked(int value)
{
    m_model->setStateFilter(value == Qt::Checked ? AddonsModel::State::STATE_INSTALLED : AddonsModel::State::STATE_NONE);
    updateControls();
}

void AddonsTab::reposync()
{
    if (m_model->loading()) return;
    onlineCatalogRequested = true;
    m_model->loadFromDefaultRepository();
    updateControls();
}

/* Safe copy of the extension_t struct */
ExtensionListModel::ExtensionCopy::ExtensionCopy( extension_t *p_ext )
{
    name = qfu( p_ext->psz_name );
    description = qfu( p_ext->psz_description );
    shortdesc = qfu( p_ext->psz_shortdescription );
    if( description.isEmpty() )
        description = shortdesc;
    if( shortdesc.isEmpty() && !description.isEmpty() )
        shortdesc = description;
    title = qfu( p_ext->psz_title );
    if (title.isEmpty()) title = name;
    author = qfu( p_ext->psz_author );
    version = qfu( p_ext->psz_version );
    url = qfu( p_ext->psz_url );
    icon = loadPixmapFromData( p_ext->p_icondata, p_ext->i_icondata_size );
}

ExtensionListModel::ExtensionCopy::~ExtensionCopy()
{
    delete icon;
}

QVariant ExtensionListModel::ExtensionCopy::data( int role ) const
{
    switch( role )
    {
    case Qt::DisplayRole:
        return title;
    case Qt::DecorationRole:
        if ( !icon ) return QPixmap( ":/logo/hakone48.png" );
        return *icon;
    case SummaryRole:
        return shortdesc;
    case DescriptionRole:
        return description;
    case Qt::ToolTipRole:
        return QString(title + QLatin1Char('\n') + plainDescription(shortdesc));
    case VersionRole:
        return version;
    case AuthorRole:
        return author;
    case LinkRole:
        return url;
    case FilenameRole:
        return name;
    default:
        return QVariant();
    }
}

/* Extensions list model for the QListView */
ExtensionListModel::ExtensionListModel( QObject *parent )
    : QAbstractListModel( parent ), EM( NULL )
{

}

ExtensionListModel::ExtensionListModel( QObject *parent, ExtensionsManager* EM_ )
        : QAbstractListModel( parent ), EM( EM_ )
{
    // Connect to ExtensionsManager::extensionsUpdated()
    connect( EM, &ExtensionsManager::extensionsUpdated, this, &ExtensionListModel::updateList );

    // Load extensions now if not already loaded
    EM->loadExtensions();
}

ExtensionListModel::~ExtensionListModel()
{
    // Clear extensions list
    while( !extensions.isEmpty() )
        delete extensions.takeLast();
}

void ExtensionListModel::updateList()
{
    beginResetModel();
    qDeleteAll(extensions);
    extensions.clear();
    if (auto *manager = EM ? EM->getManager() : nullptr)
    {
        vlc_mutex_locker lock{&manager->lock};
        extension_t *extension;
        ARRAY_FOREACH(extension, manager->extensions)
            extensions.append(new ExtensionCopy(extension));
    }
    endResetModel();
}

int ExtensionListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : extensions.size();
}

QVariant ExtensionListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.model() != this || index.column() != 0 || index.row() < 0 || index.row() >= extensions.size()) return {};
    return extensions.at(index.row())->data(role);
}

QModelIndex ExtensionListModel::index(int row, int column, const QModelIndex &parent) const
{
    if (parent.isValid() || column != 0 || row < 0 || row >= extensions.size()) return {};
    return createIndex(row, column);
}

static void commonPaint(
    QPainter *painter,
    const QStyleOptionViewItem &option,
    const QMargins& margins,
    const QPixmap& icon,
    const QString& name,
    const QString& description,
    int titleInset = 0
    )
{
    QStyleOptionViewItem opt = option;

    // Draw background
    if ( opt.state & QStyle::State_Selected )
        painter->fillRect( opt.rect, opt.palette.highlight() );

    // Icon
    if( !icon.isNull() )
    {
        painter->drawPixmap( opt.rect.left() + margins.left(),
                             opt.rect.top() + margins.top(),
                             icon.scaled( opt.decorationSize,
                                          Qt::KeepAspectRatio,
                                          Qt::SmoothTransformation )
        );
    }

    painter->save();
    painter->setRenderHint( QPainter::TextAntialiasing );

    painter->setPen((opt.state & QStyle::State_Selected) ? opt.palette.highlightedText().color() : opt.palette.text().color());

    QFont font( option.font );
    font.setBold( true );
    painter->setFont( font );
    QRect textrect( opt.rect );
    textrect.adjust( 2 * margins.left() + margins.right() + opt.decorationSize.width(),
                     margins.top(),
                     - margins.right(),
                     - margins.bottom() - opt.fontMetrics.height() );

    painter->drawText(textrect.adjusted(0, 0, -titleInset, 0), Qt::AlignLeft, QFontMetrics(font).elidedText(name, Qt::ElideRight, qMax(0, textrect.width() - titleInset)));

    font.setBold( false );
    painter->setFont( font );
    painter->setPen((opt.state & QStyle::State_Selected) ? opt.palette.highlightedText().color() : opt.palette.windowText().color());
    painter->drawText( textrect.translated( 0, option.fontMetrics.height() ),
                       Qt::AlignLeft,
                       QFontMetrics(font).elidedText(plainDescription(description), Qt::ElideRight, qMax(0, textrect.width())) );

    painter->restore();
}

/* Extension List Widget Item */
ExtensionItemDelegate::ExtensionItemDelegate( QObject *parent )
        : QStyledItemDelegate( parent )
{
    margins = QMargins( 4, 4, 4, 4 );
}

ExtensionItemDelegate::~ExtensionItemDelegate()
{
}

void ExtensionItemDelegate::paint( QPainter *painter,
                                   const QStyleOptionViewItem &option,
                                   const QModelIndex &index ) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption( &opt, index );

    /* Draw common base  */
    QString name = index.data( Qt::DisplayRole ).toString();
    QPixmap icon = index.data( Qt::DecorationRole ).value<QPixmap>();
    QString description = index.data( ExtensionListModel::SummaryRole ).toString();

    commonPaint(
        painter,
        opt,
        margins,
        icon,
        name,
        plainDescription(description)
        );
}

QSize ExtensionItemDelegate::sizeHint( const QStyleOptionViewItem &option,
                                       const QModelIndex &index ) const
{
    if ( index.isValid() )
    {
        return QSize( 200, 2 * option.fontMetrics.height()
                      + margins.top() + margins.bottom() );
    }
    else
        return QSize();
}

void ExtensionItemDelegate::initStyleOption( QStyleOptionViewItem *option,
                                             const QModelIndex &index ) const
{
    QStyledItemDelegate::initStyleOption( option, index );
    option->decorationSize = QSize( option->rect.height(), option->rect.height() );
    option->decorationSize -= QSize( margins.left() + margins.right(),
                                     margins.top() + margins.bottom() );
}

AddonItemDelegate::AddonItemDelegate( QObject *parent )
    : QStyledItemDelegate( parent )
    , margins( 4,4,4,4 )
{ }

AddonItemDelegate::~AddonItemDelegate() = default;

void AddonItemDelegate::paint( QPainter *painter,
                               const QStyleOptionViewItem &option,
                               const QModelIndex &index ) const
{
    QStyleOptionViewItem newopt = option;
    auto i_state = index.data( AddonsModel::Role::STATE ).value<AddonsModel::State>();

    /* Draw base info from parent */
    initStyleOption( &newopt, index );

    /* Draw common base  */
    QString name = index.data( Qt::DisplayRole ).toString();
    QPixmap icon = index.data(Qt::DecorationRole).value<QPixmap>();
    if (icon.isNull()) icon = QPixmap(QStringLiteral(":/logo/vlc48.png"));
    QString description = index.data( AddonsModel::Role::SUMMARY ).toString();

    QFont badgeFont(newopt.font);
    badgeFont.setBold(true);
    const int badgeWidth = i_state == AddonsModel::State::STATE_INSTALLED
        ? QFontMetrics(badgeFont).horizontalAdvance(qtr("Installed")) + 16 : 0;
    commonPaint(painter, newopt, margins, icon, name, description, badgeWidth);

    painter->save();
    painter->setRenderHint( QPainter::TextAntialiasing );

    /* Addon status */
    if ( i_state == AddonsModel::State::STATE_INSTALLED )
    {
        painter->save();
        painter->setRenderHint( QPainter::Antialiasing );
        QMargins statusMargins( 5, 2, 5, 2 );
        QFont font( newopt.font );
        font.setBold( true );
        QFontMetrics metrics( font );
        painter->setFont( font );
        QRect statusRect = metrics.boundingRect( qtr("Installed") );
        statusRect.moveTopLeft(QPoint(newopt.rect.right() - statusRect.width(), newopt.rect.top() + margins.top()));
        statusRect.adjust( - statusMargins.left() - statusMargins.right(),
                           0, 0,
                           statusMargins.top() + statusMargins.bottom() );
        QPainterPath path;
        path.addRoundedRect( statusRect, 2.0, 2.0 );
        painter->fillPath(path, newopt.palette.highlight());
        painter->setPen(newopt.palette.highlightedText().color());
        painter->drawText(
            statusRect.adjusted( statusMargins.left(), statusMargins.top(),
                                 -statusMargins.right(), -statusMargins.bottom() ),
            qtr("Installed") );
        painter->restore();
    }

    painter->setPen((newopt.state & QStyle::State_Selected) ? newopt.palette.highlightedText().color() : newopt.palette.windowText().color());

    /* Start below text */
    QRect textrect( newopt.rect );
    textrect.adjust( 2 * margins.left() + margins.right() + newopt.decorationSize.width(),
                     margins.top(),
                     - margins.right(),
                     - margins.bottom() - newopt.fontMetrics.height() );
    textrect.translate( 0, newopt.fontMetrics.height() * 2 );

    /* Version */
    QString version = index.data( AddonsModel::Role::ADDON_VERSION).toString();
    if ( !version.isEmpty() )
        painter->drawText( textrect, Qt::AlignLeft, qtr("Version %1").arg( version ) );

    textrect.translate( 0, newopt.fontMetrics.height() );

    /* Score */
    int i_score = index.data( AddonsModel::Role::SCORE).toInt();
    QPixmap scoreicon;
    if ( i_score )
    {
        int i_scoreicon_height = newopt.fontMetrics.height();
        int i_scoreicon_width = i_scoreicon_height * SCORE_ICON_WIDTH_SCALE;
        scoreicon = ImageHelper::loadSvgToPixmap( ":/addons/addon_score.svg",
                    i_scoreicon_width, i_scoreicon_height );
        int i_width = ( (float) i_score / AddonsModel::getMaxScore() ) * i_scoreicon_width;
        /* Erase the end (value) of our pixmap with a shadow */
        QPainter erasepainter( &scoreicon );
        erasepainter.setCompositionMode( QPainter::CompositionMode_SourceIn );
        erasepainter.fillRect( QRect( i_width, 0,
                                      scoreicon.width() - i_width, scoreicon.height() ),
                               newopt.palette.color( QPalette::Dark ) );
        erasepainter.end();
        painter->drawPixmap( textrect.topLeft(), scoreicon );
    }

    /* Downloads # */
    int i_downloads = index.data( AddonsModel::DOWNLOAD_COUNT).toInt();
    if ( i_downloads )
        painter->drawText( textrect.translated( scoreicon.width() + margins.left(), 0 ),
                           Qt::AlignLeft, qtr("%1 downloads").arg( i_downloads ) );

    painter->restore();


}

QSize AddonItemDelegate::sizeHint( const QStyleOptionViewItem &option,
                                   const QModelIndex &index ) const
{
    if ( index.isValid() )
    {
        return QSize( 200, 4 * option.fontMetrics.height()
                      + margins.top() + margins.bottom() );
    }
    else
        return QSize();
}

void AddonItemDelegate::initStyleOption( QStyleOptionViewItem *option,
                                            const QModelIndex &index ) const
{
    QStyledItemDelegate::initStyleOption( option, index );
    option->decorationSize = QSize( option->rect.height(), option->rect.height() );
    option->decorationSize -= QSize( margins.left() + margins.right(),
                                    margins.top() + margins.bottom() );
}

/* "More information" dialog */

namespace {
void colorPluginLinks(QTextBrowser *browser)
{
    QList<QPair<int, int>> anchors;
    for (auto block = browser->document()->begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it)
        {
            const auto fragment = it.fragment();
            if (fragment.isValid() && fragment.charFormat().isAnchor())
                anchors.append({fragment.position(), fragment.length()});
        }
    for (const auto &anchor : anchors)
    {
        QTextCursor cursor(browser->document());
        cursor.setPosition(anchor.first);
        cursor.setPosition(anchor.first + anchor.second, QTextCursor::KeepAnchor);
        QTextCharFormat format;
        format.setForeground(browser->palette().color(QPalette::Link));
        cursor.mergeCharFormat(format);
    }
}

void createPluginInfo(QVLCDialog *dialog, qt_intf_t *intf, const QString &name, const QPixmap &art,
                      const QString &version, const QString &author, const QString &type,
                      const QString &summary, const QString &description, const QString &website,
                      const QStringList &files)
{
    dialog->setObjectName(QStringLiteral("redesignPluginInfo"));
    dialog->setProperty("redesignDialog", true);
    dialog->setWindowRole("vlc-plugin-info");
    dialog->setWindowModality(Qt::WindowModal);
    auto *root = new QVBoxLayout(dialog);
    auto *header = new QHBoxLayout;
    auto *icon = new QLabel(dialog);
    header->addWidget(icon, 0, Qt::AlignTop);
    auto *title = new QLabel(name, dialog);
    title->setTextFormat(Qt::PlainText);
    title->setWordWrap(true);
    title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    title->setProperty("redesignHeading", true);
    title->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    header->addWidget(title, 1);
    root->addLayout(header);
    auto *browser = new QTextBrowser(dialog);
    browser->setObjectName(QStringLiteral("pluginDetails"));
    browser->setOpenExternalLinks(true);
    browser->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    browser->setTabChangesFocus(true);
    root->addWidget(browser, 1);
    auto *footer = new QHBoxLayout;
    footer->addStretch();
    auto *close = new QPushButton(dialog);
    close->setObjectName(QStringLiteral("pluginInfoClose"));
    close->setAutoDefault(false);
    footer->addWidget(close);
    root->addLayout(footer);
    QObject::connect(close, &QPushButton::clicked, dialog, &QVLCDialog::reject);
    for (auto key : {Qt::Key_Return, Qt::Key_Enter})
    {
        auto *shortcut = new QShortcut(QKeySequence(key), close);
        shortcut->setContext(Qt::WidgetShortcut);
        QObject::connect(shortcut, &QShortcut::activated, close, &QPushButton::click);
    }
    const auto text = [=] {
        dialog->setWindowTitle(qtr("About") + QLatin1Char(' ') + name);
        close->setText(qtr("&Close"));
        QString html;
        const auto field = [&html](const QString &label, const QString &value) {
            if (!value.isEmpty()) html += QStringLiteral("<p><b>%1:</b> %2</p>").arg(label.toHtmlEscaped(), value.toHtmlEscaped());
        };
        field(qtr("Type"), type);
        field(qtr("Version"), version);
        field(qtr("Author"), author);
        if (!summary.isEmpty() && summary != description) html += QStringLiteral("<p>%1</p>").arg(Qt::mightBeRichText(summary) ? summary : summary.toHtmlEscaped());
        if (!description.isEmpty())
            html += Qt::mightBeRichText(description) ? description : QStringLiteral("<p>%1</p>").arg(QString(description.toHtmlEscaped()).replace(QLatin1Char('\n'), QStringLiteral("<br>")));
        const QUrl url(website);
        if (!website.isEmpty())
        {
            if (url.isValid() && (url.scheme() == QLatin1String("https") || url.scheme() == QLatin1String("http")))
                html += QStringLiteral("<p><b>%1:</b><br><a href=\"%2\">%3</a></p>").arg(qtr("Website").toHtmlEscaped(), QString::fromUtf8(url.toEncoded()).toHtmlEscaped(), website.toHtmlEscaped());
            else field(qtr("Website"), website);
        }
        if (!files.isEmpty())
        {
            html += QStringLiteral("<p><b>%1:</b></p>").arg(qtr("Files").toHtmlEscaped());
            for (const auto &file : files) html += QStringLiteral("<p>%1</p>").arg(file.toHtmlEscaped());
        }
        const int scroll = browser->verticalScrollBar()->value();
        browser->setHtml(html);
        browser->verticalScrollBar()->setValue(scroll);
        colorPluginLinks(browser);
    };
    text();
    if (auto *translator = InterfaceTranslator::instance())
        QObject::connect(translator, &InterfaceTranslator::languageChanged, dialog, text);
    applyRedesignWidgetTheme(dialog, intf->p_mi);
    const auto links = [browser] { colorPluginLinks(browser); };
    links();
    if (auto *ctx = intf->p_mi)
    {
        QObject::connect(ctx, &MainCtx::redesignAccentChanged, dialog, links);
        QObject::connect(ctx->getColorScheme(), &ColorSchemeModel::currentChanged, dialog, links);
        QObject::connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, dialog, links);
    }
    const auto metrics = [=] {
        const int margin = pluginDp(intf, 24);
        root->setContentsMargins(margin, margin, margin, margin);
        root->setSpacing(pluginDp(intf, 12));
        icon->setFixedSize(pluginDp(intf, 48), pluginDp(intf, 48));
        const auto pixmap = art.isNull() ? QPixmap(QStringLiteral(":/logo/vlc48.png")) : art;
        icon->setPixmap(pixmap.scaled(icon->size() * dialog->devicePixelRatioF(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        auto scaled = icon->pixmap(); scaled.setDevicePixelRatio(dialog->devicePixelRatioF()); icon->setPixmap(scaled);
        const auto available = dialog->screen() ? dialog->screen()->availableGeometry().size() : QSize(1280, 720);
        dialog->setMinimumSize(QSize(pluginDp(intf, 360), pluginDp(intf, 300)).boundedTo(available));
    };
    metrics();
    if (intf->p_mi) QObject::connect(intf->p_mi, &MainCtx::intfScaleFactorChanged, dialog, metrics);
    dialog->resize(pluginDp(intf, 620), pluginDp(intf, 520));
    if (dialog->screen()) dialog->resize(dialog->size().boundedTo(dialog->screen()->availableGeometry().size()));
}
}

ExtensionInfoDialog::ExtensionInfoDialog(const QModelIndex &index, qt_intf_t *intf, QWindow *parent)
    : QVLCDialog(parent, intf)
{
    createPluginInfo(this, intf, index.data().toString(), index.data(Qt::DecorationRole).value<QPixmap>(),
        index.data(ExtensionListModel::VersionRole).toString(), index.data(ExtensionListModel::AuthorRole).toString(), {},
        index.data(ExtensionListModel::SummaryRole).toString(), index.data(ExtensionListModel::DescriptionRole).toString(),
        index.data(ExtensionListModel::LinkRole).toString(), {index.data(ExtensionListModel::FilenameRole).toString()});
}

AddonInfoDialog::AddonInfoDialog(const QModelIndex &index, qt_intf_t *intf, QWindow *parent)
    : QVLCDialog(parent, intf)
{
    QStringList files;
    for (const auto &file : index.data(AddonsModel::FILENAME).toList()) files << file.toString();
    createPluginInfo(this, intf, index.data(AddonsModel::NAME).toString(), index.data(Qt::DecorationRole).value<QPixmap>(),
        index.data(AddonsModel::ADDON_VERSION).toString(), index.data(AddonsModel::AUTHOR).toString(),
        AddonsModel::getLabelForType(index.data(AddonsModel::TYPE).value<AddonsModel::Type>()),
        index.data(AddonsModel::SUMMARY).toString(), index.data(AddonsModel::DESCRIPTION).toString(),
        index.data(AddonsModel::LINK).toString(), files);
}

static QPixmap *loadPixmapFromData( char *data, int size )
{
    if( !data || size <= 0 )
        return NULL;
    QPixmap *pixmap = new QPixmap();
    if( !pixmap->loadFromData( (const uchar*) data, (uint) size ) )
    {
        delete pixmap;
        return NULL;
    }
    return pixmap;
}
