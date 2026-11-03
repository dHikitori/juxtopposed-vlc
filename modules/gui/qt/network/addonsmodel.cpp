/*****************************************************************************
 * Copyright (C) 2020 VLC authors and VideoLAN
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

#include "addonsmodel.hpp"

#include "util/locallistbasemodel.hpp"

#include "medialibrary/mlhelper.hpp"

#include "playlist/media.hpp"
#include "playlist/playlist_controller.hpp"

#include <memory>
#include <QPixmap>
#include <QQmlFile>
#include <QHash>

#include <vlc_media_source.h>
#include <vlc_interface.h>
#include <vlc_addons.h>
#include <vlc_cxx_helpers.hpp>
#include <vlc_addons.h>

#define CHECK_ADDON_TYPE_MATCH(type) \
    static_assert(static_cast<addon_type_t>(AddonsModel::Type::TYPE_ ## type) == ADDON_##type)

CHECK_ADDON_TYPE_MATCH(UNKNOWN);
CHECK_ADDON_TYPE_MATCH(PLAYLIST_PARSER);
CHECK_ADDON_TYPE_MATCH(SERVICE_DISCOVERY);
CHECK_ADDON_TYPE_MATCH(SKIN2);
CHECK_ADDON_TYPE_MATCH(PLUGIN);
CHECK_ADDON_TYPE_MATCH(INTERFACE);
CHECK_ADDON_TYPE_MATCH(META);
CHECK_ADDON_TYPE_MATCH(OTHER);

#undef CHECK_ADDON_TYPE_MATCH

#define CHECK_ADDON_STATE_MATCH(type) \
    static_assert(static_cast<addon_state_t>(AddonsModel::State::STATE_ ## type) == ADDON_##type)

CHECK_ADDON_STATE_MATCH(INSTALLING);
CHECK_ADDON_STATE_MATCH(INSTALLED);
CHECK_ADDON_STATE_MATCH(UNINSTALLING);

#undef CHECK_ADDON_STATE_MATCH

namespace {

using AddonPtr = ::vlc::vlc_shared_data_ptr<addon_entry_t,
                                     &addon_entry_Hold, &addon_entry_Release>;

class AddonItem {
public:
    //
    AddonItem( AddonPtr addon )
    {
        vlc_mutex_locker locker{&addon->lock};
        name = qfu( addon->psz_name );
        summary = qfu( addon->psz_summary ).trimmed();
        description = qfu( addon->psz_description ).trimmed();
        author = qfu( addon->psz_author );
        sourceUrl = QUrl( addon->psz_source_uri );
        entry = addon;
        uuid = QByteArray( reinterpret_cast<const char*>(addon->uuid), sizeof( addon_uuid_t ));
        if (std::all_of(uuid.cbegin(), uuid.cend(), [](char value) { return value == 0; })) {
            const auto filename = addon->files.i_size ? qfu(addon->files.p_elems[0]->psz_filename) : name;
            identifier = QStringLiteral("local:%1:%2").arg(int(addon->e_type)).arg(filename);
        } else
            identifier = QString::fromLatin1(uuid.toHex());

        if ( addon->psz_image_data ) {
            char *cDir = config_GetUserDir( VLC_CACHE_DIR );
            if (likely(cDir != nullptr))
            {
                QDir dir( cDir );
                free(cDir);
                dir.mkdir("art");
                dir.cd("art");
                dir.mkdir("qt-addon-covers");
                dir.cd("qt-addon-covers");
                char* uuidStr = addons_uuid_to_psz( &addon->uuid );
                QString filename = QString("addon_thumbnail_%1.png").arg(uuidStr);
                free(uuidStr);
                QString absoluteFilePath =  dir.absoluteFilePath(filename);

                if ( !QFileInfo::exists( absoluteFilePath )) {
                    QPixmap pixmap;
                    pixmap.loadFromData( QByteArray::fromBase64( QByteArray( addon->psz_image_data ) ),
                                        0,
                                        Qt::AutoColor
                                        );
                    pixmap.save(absoluteFilePath);
                }
                artworkUrl = QUrl::fromLocalFile( absoluteFilePath );
            }
        }
        else if ( addon->e_flags & ADDON_BROKEN )
            artworkUrl = QUrl( ":/addons/addon_broken.svg" );
        else
            artworkUrl = QUrl( ":/addons/addon_default.svg" );
    }

public:
    QByteArray  uuid;
    QString identifier;
    QString name;
    QString summary;
    QString description;
    QString author;
    QUrl sourceUrl;
    QUrl artworkUrl;
    AddonPtr entry;
};

} //namespace

using AddonItemPtr = std::shared_ptr<AddonItem> ;
using AddonItemList = std::vector<AddonItemPtr> ;

// ListCache specialisation

template<>
bool ListCache<AddonItemPtr>::compareItems(const AddonItemPtr& a, const AddonItemPtr& b)
{
    return a->identifier == b->identifier;
}

// AddonsModelPrivate

class AddonsModelPrivate
    : public LocalListBaseModelPrivate<AddonItemPtr>
{

public:
    Q_DECLARE_PUBLIC(AddonsModel)

public: //ctor/dtor
    AddonsModelPrivate(AddonsModel* pub)
        : LocalListBaseModelPrivate<AddonItemPtr>(pub)
    {
    }

    ~AddonsModelPrivate()
    {
        if ( m_manager )
            addons_manager_Delete( m_manager );
    }

public:
    const AddonItem* getItemForRow(int row) const
    {
        const AddonItemPtr* ref = item(row);
        if (ref)
            return ref->get();
        return nullptr;
    }

    bool gatherRepository(const char* uri)
    {
        Q_Q(AddonsModel);
        if (!m_manager)
            return false;
        m_error.clear();
        m_revision++;
        emit q->catalogChanged();
        m_loading = true;
        emit q->loadingChanged();
        addons_manager_Gather( m_manager, uri);
        return true;
    }

public: //BaseModelPrivateT implementation
    bool initializeModel() override;

    LocalListCacheLoader<AddonItemPtr>::ItemCompare getSortFunction() const override
    {
        if (m_sortOrder == Qt::SortOrder::DescendingOrder)
            return [](const AddonItemPtr& a, const AddonItemPtr& b){
                return QString::compare(a->name, b->name) > 0;
            };
        else
            return [](const AddonItemPtr& a, const AddonItemPtr& b) {
                return QString::compare(a->name, b->name) < 0;
            };
    }



public: //discovery callbacks
    void addonFound( AddonPtr addon );
    void addonChanged( AddonPtr addon, addon_state_t state );
    void discoveryEnded(int status);

public: //LocalListCacheLoader implementation
    //return the data matching the pattern
    AddonItemList getModelData(const QString& pattern) const override
    {
        if (pattern.isEmpty()
            && m_typeFilter == AddonsModel::Type::TYPE_NONE
            && m_stateFilter == AddonsModel::State::STATE_NONE)
            return m_items;

        AddonItemList items;
        std::copy_if(
            m_items.cbegin(), m_items.cend(),
            std::back_inserter(items),
            [&pattern, filter = m_typeFilter, state = m_stateFilter](const AddonItemPtr& item) {
                if (state != AddonsModel::State::STATE_NONE
                    && static_cast<AddonsModel::State>(item->entry->e_state) != state)
                    return false;
                if (filter != AddonsModel::Type::TYPE_NONE
                    && static_cast<AddonsModel::Type>(item->entry->e_type) != filter)
                        return false;
                return item->name.contains(pattern, Qt::CaseInsensitive);
            });
        return items;
    }

public: // data
    MainCtx* m_ctx = nullptr;
    addons_manager_t* m_manager = nullptr;
    AddonItemList m_items;
    struct PendingAction { bool install; bool started = false; };
    QHash<QByteArray, PendingAction> m_pending;
    QString m_error;
    AddonsModel::Type m_typeFilter = AddonsModel::Type::TYPE_NONE;
    AddonsModel::State m_stateFilter = AddonsModel::State::STATE_NONE;
};

AddonsModel::AddonsModel( QObject* parent )
    : BaseModel( new AddonsModelPrivate(this), parent )
{
}

QVariant AddonsModel::data( const QModelIndex& index, int role ) const
{
    Q_D(const AddonsModel);
    if (!d->m_ctx)
        return {};

    const AddonItem* item = d->getItemForRow(index.row());
    if (!item)
        return {};

    switch ( role )
    {
        case Qt::DisplayRole:
        case Role::NAME:
            return item->name;
        case Role::AUTHOR:
            return item->author;
        case Role::SUMMARY:
            return item->summary;
        case Role::DESCRIPTION:
            return item->description;
        case Role::DOWNLOADS:
        {
            vlc_mutex_locker locker{&item->entry->lock};
            return QVariant::fromValue( item->entry->i_downloads );
        }
        case Role::SCORE:
        {
            vlc_mutex_locker locker{&item->entry->lock};
            return item->entry->i_score;
        }
        case Role::STATE:
        {
            vlc_mutex_locker locker{&item->entry->lock};
            return item->entry->e_state;
        }
        case Role::ARTWORK:
        {
            vlc_mutex_locker locker{&item->entry->lock};
            return item->artworkUrl;
        }
        case Role::TYPE:
        {
            vlc_mutex_locker locker{&item->entry->lock};
            return QVariant::fromValue(item->entry->e_type);
        }
        case Role::LINK:
        {
            vlc_mutex_locker locker{&item->entry->lock};
            return QString{ item->entry->psz_source_uri };
        }
        case Role::ADDON_VERSION:
        {
            vlc_mutex_locker locker{&item->entry->lock};
            return qfu(item->entry->psz_version);
        }
        case Role::UUID:
            return item->uuid;
        case Role::IDENTIFIER:
            return item->identifier;
        case Role::DOWNLOAD_COUNT:
        {
            vlc_mutex_locker locker{&item->entry->lock};
            return QVariant::fromValue(item->entry->i_downloads);
        }
        case Role::FILENAME:
        {
            vlc_mutex_locker locker{&item->entry->lock};
            QList<QString> list;
            addon_file_t *p_file;
            ARRAY_FOREACH( p_file, item->entry->files )
            list << qfu( p_file->psz_filename );
            return QVariant{ list };
        }
        case  Role::BROKEN:
        {
            vlc_mutex_locker locker{&item->entry->lock};
            return item->entry->e_flags & ADDON_BROKEN;
        }
        case  Role::MANAGEABLE:
        {
            vlc_mutex_locker locker{&item->entry->lock};
            return item->entry->e_flags & ADDON_MANAGEABLE;
        }
        case  Role::UPDATABLE:
        {
            vlc_mutex_locker locker{&item->entry->lock};
            return item->entry->e_flags & ADDON_UPDATABLE;
        }

        case Qt::ToolTipRole:
        {
            vlc_mutex_locker locker{&item->entry->lock};
            if ( !( item->entry->e_flags & ADDON_MANAGEABLE ) )
            {
                return qtr("This addon has been installed manually. Hakone can't manage it by itself.");
            }
            return QVariant{};
        }
        case Qt::DecorationRole:
            return QPixmap{QQmlFile::urlToLocalFileOrQrc(item->artworkUrl)};
        default:
            return {};
    }
}

bool AddonsModel::setData(const QModelIndex& index, const QVariant &value, int role)
{
    if ( role == Role::STATE )
    {
        auto i_value = value.value<AddonsModel::State>();
        if ( i_value == AddonsModel::State::STATE_INSTALLING )
        {
            installService(index.row());
        }
        else if ( i_value == AddonsModel::State::STATE_UNINSTALLING )
        {
            removeService(index.row());
        }
    }
    return true;
}

Qt::ItemFlags AddonsModel::flags( const QModelIndex &index ) const
{
    Q_D(const AddonsModel);
    Qt::ItemFlags qtFlags = BaseModel::flags(index);

    const AddonItem* item = d->getItemForRow(index.row());
    if (!item)
        return qtFlags;

    {
        vlc_mutex_locker locker{&item->entry->lock};
        addon_state_t addonState = item->entry->e_state;
        if (addonState == ADDON_INSTALLING || addonState == ADDON_UNINSTALLING)
            qtFlags &= ~Qt::ItemIsEnabled;

    }
    qtFlags |= Qt::ItemIsEditable;

    return qtFlags;
}

QHash<int, QByteArray> AddonsModel::roleNames() const
{
    return {
        { Role::NAME, QByteArrayLiteral("name") },
        { Role::AUTHOR, QByteArrayLiteral("author") },
        { Role::SUMMARY, QByteArrayLiteral("summary") },
        { Role::DESCRIPTION, QByteArrayLiteral("description") },
        { Role::DOWNLOADS, QByteArrayLiteral("downloads") },
        { Role::SCORE, QByteArrayLiteral("score") },
        { Role::STATE, QByteArrayLiteral("state") },
        { Role::ARTWORK, QByteArrayLiteral("artwork") },
        { Role::TYPE, QByteArrayLiteral("type") },
        { Role::LINK, QByteArrayLiteral("link") },
        { Role::FILENAME, QByteArrayLiteral("filename") },
        { Role::ADDON_VERSION, QByteArrayLiteral("version") },
        { Role::UUID, QByteArrayLiteral("uuid") },
        { Role::DOWNLOAD_COUNT, QByteArrayLiteral("downloadCount") },
        { Role::BROKEN, QByteArrayLiteral("broken") },
        { Role::MANAGEABLE, QByteArrayLiteral("manageable") },
        { Role::UPDATABLE, QByteArrayLiteral("updatable") },
        { Role::IDENTIFIER, QByteArrayLiteral("identifier") },
    };
}

void AddonsModel::installService(int idx)
{
    Q_D(AddonsModel);

    const AddonItem* item = d->getItemForRow(idx);
    if (!item)
        return;

    addon_uuid_t uuid;
    assert(sizeof(uuid) == item->uuid.size());
    memcpy( uuid, item->uuid.constData(), sizeof( uuid ) );
    addons_manager_Install( d->m_manager, uuid );
}

void AddonsModel::removeService(int idx)
{
    Q_D(AddonsModel);

    const AddonItem* item = d->getItemForRow(idx);
    if (!item)
        return;

    addon_uuid_t uuid;
    assert(sizeof(uuid) == item->uuid.size());
    memcpy( uuid, item->uuid.constData(), sizeof( uuid ) );
    addons_manager_Remove( d->m_manager, uuid );
}

void AddonsModel::loadFromDefaultRepository()
{
    Q_D(AddonsModel);
    if (d->m_loading) return;
    d->gatherRepository("repo://");
}

void AddonsModel::loadFromExternalRepository(QUrl uri)
{
    Q_D(AddonsModel);
    if (d->m_loading) return;
    d->gatherRepository(uri.toEncoded().constData());
}

unsigned int AddonsModel::revision() const
{
    Q_D(const AddonsModel);
    return d->m_revision;
}

QString AddonsModel::errorMessage() const
{
    Q_D(const AddonsModel);
    return d->m_error;
}

QVariantMap AddonsModel::addonById(const QString& identifier) const
{
    Q_D(const AddonsModel);
    QString key = identifier;
    if (!key.startsWith(QLatin1String("local:"))) {
        const auto uuid = QByteArray::fromHex(identifier.toLatin1());
        if (uuid.size() != sizeof(addon_uuid_t) || QString::fromLatin1(uuid.toHex()) != identifier.toLower()) return {};
        key = identifier.toLower();
    }
    for (const auto& item : d->m_items) {
        if (item->identifier != key) continue;
        vlc_mutex_locker locker{&item->entry->lock};
        QStringList files;
        addon_file_t* file;
        ARRAY_FOREACH(file, item->entry->files)
            files.append(qfu(file->psz_filename));
        return {{QStringLiteral("identifier"), identifier}, {QStringLiteral("name"), item->name},
            {QStringLiteral("summary"), item->summary}, {QStringLiteral("description"), item->description},
            {QStringLiteral("author"), item->author}, {QStringLiteral("version"), qfu(item->entry->psz_version)},
            {QStringLiteral("typeLabel"), getLabelForType(static_cast<Type>(item->entry->e_type))},
            {QStringLiteral("website"), item->sourceUrl}, {QStringLiteral("files"), files},
            {QStringLiteral("state"), item->entry->e_state}, {QStringLiteral("pending"), d->m_pending.contains(item->uuid)},
            {QStringLiteral("manageable"), bool(item->entry->e_flags & ADDON_MANAGEABLE)},
            {QStringLiteral("broken"), bool(item->entry->e_flags & ADDON_BROKEN)}};
    }
    return {};
}

bool AddonsModel::requestAddon(const QString& identifier, bool install)
{
    Q_D(AddonsModel);
    if (identifier.startsWith(QLatin1String("local:"))) return false;
    const auto selected = addonById(identifier);
    if (!d->m_manager || selected.isEmpty() || selected.value(QStringLiteral("pending")).toBool()
        || !selected.value(QStringLiteral("manageable")).toBool()
        || selected.value(QStringLiteral("state")).toInt() != (install ? ADDON_NOTINSTALLED : ADDON_INSTALLED)
        || (install && selected.value(QStringLiteral("broken")).toBool())) return false;
    const auto uuid = QByteArray::fromHex(identifier.toLatin1());
    addon_uuid_t nativeUuid;
    memcpy(nativeUuid, uuid.constData(), sizeof(nativeUuid));
    const int result = install ? addons_manager_Install(d->m_manager, nativeUuid) : addons_manager_Remove(d->m_manager, nativeUuid);
    if (result != VLC_SUCCESS) return false;
    d->m_pending.insert(uuid, {install, false});
    d->m_error.clear();
    d->m_revision++;
    emit catalogChanged();
    return true;
}

bool AddonsModel::installAddon(const QString& identifier) { return requestAddon(identifier, true); }
bool AddonsModel::removeAddon(const QString& identifier) { return requestAddon(identifier, false); }

void AddonsModel::setCtx(MainCtx* ctx)
{
    Q_D(AddonsModel);

    if (ctx == d->m_ctx)
        return;

    assert(ctx);
    d->m_ctx = ctx;
    d->initializeModel();
    emit ctxChanged();
}

MainCtx* AddonsModel::getCtx() const
{
    Q_D(const AddonsModel);
    return d->m_ctx;
}

AddonsModel::Type AddonsModel::getTypeFilter() const
{
    Q_D(const AddonsModel);
    return d->m_typeFilter;
}

void AddonsModel::setTypeFilter(AddonsModel::Type type)
{
    Q_D(AddonsModel);
    if (type == d->m_typeFilter)
        return;
    d->m_typeFilter = type;

    d->m_revision++;
    invalidateCache();

    emit typeFilterChanged();
}

AddonsModel::State AddonsModel::getStateFilter() const
{
    Q_D(const AddonsModel);
    return d->m_stateFilter;
}

void AddonsModel::setStateFilter(AddonsModel::State state)
{
    Q_D(AddonsModel);
    if (state == d->m_stateFilter)
        return;
    d->m_stateFilter = state;

    d->m_revision++;
    invalidateCache();

    emit stateFilterChanged();
}

QString AddonsModel::getLabelForType(AddonsModel::Type type)
{
    switch ( type )
    {
    case Type::TYPE_SKIN2 :
        return qtr( "Skins" );
    case Type::TYPE_PLAYLIST_PARSER:
        return qtr("Playlist parsers");
    case Type::TYPE_SERVICE_DISCOVERY:
        return qtr("Service Discovery");
    case Type::TYPE_INTERFACE:
        return qtr("Interfaces");
    case Type::TYPE_META:
        return qtr("Art and meta fetchers");
    case Type::TYPE_EXTENSION:
        return qtr("Extensions");
    default:
        return qtr("Unknown");
    }
}

QColor AddonsModel::getColorForType(AddonsModel::Type type)
{
    QColor color;
    switch( type )
    {
    case Type::TYPE_EXTENSION:
        color = QColor(0xDB, 0xC5, 0x40);
        break;
    case Type::TYPE_PLAYLIST_PARSER:
        color = QColor(0x36, 0xBB, 0x59);
        break;
    case Type::TYPE_SERVICE_DISCOVERY:
        color = QColor(0xDB, 0x52, 0x40);
        break;
    case Type::TYPE_SKIN2:
        color = QColor(0x8B, 0xD6, 0xFC);
        break;
    case Type::TYPE_INTERFACE:
        color = QColor(0x00, 0x13, 0x85);
        break;
    case Type::TYPE_META:
        color = QColor(0xCD, 0x23, 0xBF);
        break;
    case Type::TYPE_PLUGIN:
    case Type::TYPE_UNKNOWN:
    case Type::TYPE_OTHER:
    default:
        break;
    }
    return color;
}

QString AddonsModel::getIconForType(AddonsModel::Type type)
{
    switch( type )
    {
    case AddonsModel::Type::TYPE_EXTENSION:
        return QStringLiteral("qrc:///addons/addon_yellow.svg");
    case AddonsModel::Type::TYPE_PLAYLIST_PARSER:
        return QStringLiteral("qrc:///addons/addon_green.svg");
    case AddonsModel::Type::TYPE_SERVICE_DISCOVERY:
        return QStringLiteral("qrc:///addons/addon_red.svg");
    case AddonsModel::Type::TYPE_SKIN2:
        return QStringLiteral("qrc:///addons/addon_cyan.svg");
    case AddonsModel::Type::TYPE_INTERFACE:
        return QStringLiteral("qrc:///addons/addon_blue.svg");
    case AddonsModel::Type::TYPE_META:
        return QStringLiteral("qrc:///addons/addon_magenta.svg");
    default:
        return QStringLiteral("qrc:///addons/addon_default.svg");
    }
    vlc_assert_unreachable();
}

bool AddonsModel::isTypeVisible(Type type)
{
    // Interface scripts remain loadable when installed manually.
    constexpr bool showInterfaces = false;
    return (type != Type::TYPE_SKIN2 || VLC_SKINS_ENABLED)
        && (type != Type::TYPE_INTERFACE || showInterfaces);
}

int AddonsModel::getMaxScore()
{
    return ADDON_MAX_SCORE;
}

static void addonFoundCallback( addons_manager_t *manager, addon_entry_t *entry )
{
    AddonsModelPrivate* d = (AddonsModelPrivate*) manager->owner.sys;
    QMetaObject::invokeMethod( d->q_func(), [d, entryPtr = AddonPtr(entry)]()
        {
            d->addonFound( std::move( entryPtr ) );
        }, Qt::QueuedConnection);
}

static void addonsDiscoveryEndedCallback( addons_manager_t *manager )
{
    AddonsModelPrivate* d = (AddonsModelPrivate*) manager->owner.sys;
    const int status = addons_manager_GetDiscoveryStatus(manager);
    QMetaObject::invokeMethod( d->q_func(), [d, status]()
        {
            d->discoveryEnded(status);
        }, Qt::QueuedConnection);
}

static void addonChangedCallback( addons_manager_t *manager, addon_entry_t *entry )
{
    AddonsModelPrivate* d = (AddonsModelPrivate*) manager->owner.sys;
    addon_state_t state;
    {
        vlc_mutex_locker locker{&entry->lock};
        state = entry->e_state;
    }
    QMetaObject::invokeMethod( d->q_func(), [d, entryPtr = AddonPtr(entry), state]()
        {
            d->addonChanged( std::move( entryPtr ), state );
        }, Qt::QueuedConnection);
}

bool AddonsModelPrivate::initializeModel()
{
    Q_Q(AddonsModel);
    if (m_qmlInitializing || !m_ctx)
        return false;

    if ( m_manager )
        addons_manager_Delete( m_manager );

    struct addons_manager_owner owner =
    {
        this,
        addonFoundCallback,
        addonsDiscoveryEndedCallback,
        addonChangedCallback,
    };

    m_manager = addons_manager_New( VLC_OBJECT( m_ctx->getIntf() ), &owner );
    assert( m_manager );

    emit q->loadingChanged();
    addons_manager_LoadCatalog( m_manager );

    m_revision++;
    m_loading = false;
    emit q->loadingChanged();
    return true;
}


void AddonsModelPrivate::addonFound( AddonPtr addon )
{
    if (!AddonsModel::isTypeVisible(static_cast<AddonsModel::Type>(addon->e_type)))
        return;
    m_items.emplace_back(std::make_shared<AddonItem>(addon));
    m_revision++;
    emit q_func()->catalogChanged();
}

void AddonsModelPrivate::addonChanged( AddonPtr addon, addon_state_t state )
{
    if (!AddonsModel::isTypeVisible(static_cast<AddonsModel::Type>(addon->e_type)))
        return;
    AddonItemPtr sdItem = std::make_shared<AddonItem>(addon);
    for (auto& item : m_items)
        if (item->identifier == sdItem->identifier) { item = sdItem; break; }
    if (state == ADDON_INSTALLING || state == ADDON_UNINSTALLING) {
        if (m_pending.contains(sdItem->uuid)) m_pending[sdItem->uuid].started = true;
    } else if (m_pending.contains(sdItem->uuid) && m_pending[sdItem->uuid].started) {
        // An older queued completion must not clear a newer request.
        const bool install = m_pending[sdItem->uuid].install;
        if (state != (install ? ADDON_INSTALLED : ADDON_NOTINSTALLED))
            m_error = (install ? qtr("Hakone cannot add %1.") : qtr("Hakone cannot remove %1.")).arg(sdItem->name);
        m_pending.remove(sdItem->uuid);
    }
    if (m_cache) m_cache->updateItem(std::move(sdItem));

    m_revision++;
    invalidateCache();
    emit q_func()->catalogChanged();
}

void AddonsModelPrivate::discoveryEnded(int status)
{
    Q_Q(AddonsModel);
    assert( m_loading );
    if (status != VLC_SUCCESS)
        m_error = qtr("VLC cannot load the add-on list. Try again.");
    m_loading = false;
    emit q->loadingChanged();
    m_revision++;
    invalidateCache();
    emit q->catalogChanged();
}
