// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "addon_native_queue_store.hpp"
#include "addon_player_discovery.hpp"
#include <QMetaObject>
#include <QPointer>
#include <QUuid>
#include <vlc_common.h>
#include <vlc_input_item.h>
#include <vlc_playlist.h>
#include <vector>
#include <cstring>

namespace vlc::addons {
namespace {
QString displayTitle(const QVariantMap& reference)
{
    auto display = reference.value("snapshot").toMap().value("title").toString();
    for (auto& character : display) if (character.category() == QChar::Other_Control || character.category() == QChar::Separator_Line || character.category() == QChar::Separator_Paragraph) character = QLatin1Char(' ');
    return display;
}
QList<quint64> refreshItems(vlc_playlist_t* playlist, const QStringList& uris, const QVariantList& references)
{
    QHash<QString, QString> titles;
    for (int index = 0; index < uris.size(); ++index) titles.insert(uris[index], displayTitle(references[index].toMap()));
    QList<quint64> changed;
    for (size_t index = 0; index < vlc_playlist_Count(playlist); ++index) {
        auto* entry = vlc_playlist_Get(playlist, index); auto* item = vlc_playlist_item_GetMedia(entry);
        char* rawUri = input_item_GetURI(item); const auto uri = QString::fromUtf8(rawUri); free(rawUri);
        if (!titles.contains(uri)) continue;
        char* previous = input_item_GetMetaExtra(item, "vlc:addon-source-title");
        if (!previous) continue;
        const auto before = QString::fromUtf8(previous); free(previous);
        char* rawName = input_item_GetName(item); const auto name = QString::fromUtf8(rawName); free(rawName);
        char* rawTitle = input_item_GetTitle(item); const auto title = QString::fromUtf8(rawTitle); free(rawTitle);
        char* rawManual = input_item_GetMetaExtra(item, "vlc:addon-manual-title"); bool manual = rawManual && !strcmp(rawManual, "1"); free(rawManual);
        if (name != before || (!title.isEmpty() && title != before)) { manual = true; input_item_SetMetaExtra(item, "vlc:addon-manual-title", "1"); }
        const auto next = titles.value(uri).toUtf8();
        if (!manual && name != titles.value(uri)) { input_item_SetName(item, next.constData()); input_item_SetTitle(item, next.constData()); changed.append(vlc_playlist_item_GetId(entry)); }
        input_item_SetMetaExtra(item, "vlc:addon-source-title", next.constData());
    }
    return changed;
}
}
AddonNativeQueueNotifications* AddonNativeQueueStore::notifications()
{ static AddonNativeQueueNotifications bus; return &bus; }
const vlc_playlist_callbacks* AddonNativeQueueStore::callbacks()
{
    static const vlc_playlist_callbacks value = [] {
        vlc_playlist_callbacks cb{};
        cb.on_items_reset = [](vlc_playlist_t*, vlc_playlist_item_t* const[], size_t, void* self) { static_cast<AddonNativeQueueStore*>(self)->changed(); };
        cb.on_items_added = [](vlc_playlist_t*, size_t, vlc_playlist_item_t* const[], size_t, void* self) { static_cast<AddonNativeQueueStore*>(self)->changed(); };
        cb.on_items_moved = [](vlc_playlist_t*, size_t, size_t, size_t, void* self) { static_cast<AddonNativeQueueStore*>(self)->changed(); };
        cb.on_items_removed = [](vlc_playlist_t*, size_t, size_t, void* self) { static_cast<AddonNativeQueueStore*>(self)->changed(); };
        cb.on_current_index_changed = [](vlc_playlist_t*, ssize_t, void* self) { static_cast<AddonNativeQueueStore*>(self)->changed(); };
        return cb;
    }();
    return &value;
}
AddonNativeQueueStore::AddonNativeQueueStore(vlc_playlist_t* playlist, std::shared_ptr<AddonSourceAdmissions> admissions, QObject* parent)
    : QObject(parent), m_playlist(playlist), m_admissions(std::move(admissions)), m_identity(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
    if (m_playlist) { vlc_playlist_Lock(m_playlist); m_listener = vlc_playlist_AddListener(m_playlist, callbacks(), this, false); vlc_playlist_Unlock(m_playlist); }
}
AddonNativeQueueStore::~AddonNativeQueueStore() { close(); }
void AddonNativeQueueStore::close()
{
    if (!m_playlist) return;
    vlc_playlist_Lock(m_playlist);
    if (m_listener) vlc_playlist_RemoveListener(m_playlist, m_listener);
    vlc_playlist_Unlock(m_playlist);
    m_listener = nullptr; m_playlist = nullptr;
}
void AddonNativeQueueStore::changed()
{
    ++m_revision;
    QMetaObject::invokeMethod(this, [this] { emit revisionChanged(); }, Qt::QueuedConnection);
}
AddonQueueTransactions::Snapshot AddonNativeQueueStore::snapshot(const QString& target) const
{
    if (target != QLatin1String("queue") || !m_playlist || !m_listener || !m_admissions) return {};
    vlc_playlist_Lock(m_playlist);
    const AddonQueueTransactions::Snapshot value{true, m_identity, m_revision};
    vlc_playlist_Unlock(m_playlist); return value;
}
AddonQueueTransactions::Cancel AddonNativeQueueStore::append(const AddonQueueTransactions::Transaction& transaction, AddonQueueTransactions::Completion completion)
{
    const auto before = snapshot(transaction.target);
    if (!before.available || before.identity != transaction.targetIdentity || before.revision != transaction.expectedRevision) {
        completion(false, tr("The queue changed. Select the items again.")); return {};
    }
    QStringList uris; QString error;
    if (!m_admissions->admit(transaction.references, &uris, &error)) { completion(false, error); return {}; }
    std::vector<std::shared_ptr<input_item_t>> held;
    std::vector<input_item_t*> items;
    for (int index = 0; index < uris.size(); ++index) {
        const auto uri = uris[index].toUtf8();
        const auto title = displayTitle(transaction.references[index].toMap()).toUtf8();
        auto* item = input_item_NewExt(uri.constData(), title.constData(), INPUT_DURATION_UNSET, ITEM_TYPE_STREAM, ITEM_NET);
        if (!item) { completion(false, tr("The app cannot create the queue items.")); return {}; }
        input_item_SetTitle(item, title.constData()); input_item_SetMetaExtra(item, "vlc:addon-source-title", title.constData());
        held.emplace_back(item, input_item_Release); items.push_back(item);
    }
    vlc_playlist_Lock(m_playlist);
    const bool current = transaction.targetIdentity == m_identity && transaction.expectedRevision == m_revision;
    const bool success = current && vlc_playlist_Append(m_playlist, items.data(), items.size()) == VLC_SUCCESS;
    const auto metadata = success ? refreshItems(m_playlist, uris, transaction.references) : QList<quint64>{};
    vlc_playlist_Unlock(m_playlist);
    if (!metadata.isEmpty()) emit notifications()->metadataChanged(reinterpret_cast<quintptr>(m_playlist), metadata);
    completion(success, success ? QString() : current ? tr("The app cannot add the selection to the queue.") : tr("The queue changed. Select the items again."));
    return {};
}
AddonDiscoveryNativeBackend AddonNativeQueueStore::backend()
{
    const QPointer<AddonNativeQueueStore> owner(this);
    AddonDiscoveryNativeBackend result;
    result.snapshot = [owner](const QString& target) { return owner ? owner->snapshot(target) : AddonQueueTransactions::Snapshot{}; };
    result.append = [owner](const auto& transaction, auto completion) {
        if (!owner) { completion(false, QObject::tr("The queue is not available.")); return AddonQueueTransactions::Cancel{}; }
        return owner->append(transaction, std::move(completion));
    };
    // A core lazy resolver must provide playback. An admitted URI is not a URL.
    return result;
}
QVariantMap AddonNativeQueueStore::queuedReference(quint64 itemId) const
{
    if (!m_playlist || !m_admissions) return {};
    QString uri;
    vlc_playlist_Lock(m_playlist);
    for (size_t index = 0; index < vlc_playlist_Count(m_playlist); ++index) {
        auto* item = vlc_playlist_Get(m_playlist, index);
        if (vlc_playlist_item_GetId(item) != itemId) continue;
        char* value = input_item_GetURI(vlc_playlist_item_GetMedia(item));
        if (value) { uri = QString::fromUtf8(value); free(value); }
        break;
    }
    vlc_playlist_Unlock(m_playlist);
    return admittedReference(uri);
}
QVariantMap AddonNativeQueueStore::admittedReference(const QString& uri) const
{ return m_admissions ? m_admissions->currentReference(uri) : QVariantMap{}; }
bool AddonNativeQueueStore::refreshDisplay(const QVariantMap& reference)
{
    if (!m_playlist || !m_admissions) return false;
    const auto uri = m_admissions->uriForReference(reference);
    if (uri.isEmpty() || m_admissions->currentReference(uri).isEmpty()) return false;
    QStringList uris;
    if (!m_admissions->admit({reference}, &uris)) return false;
    vlc_playlist_Lock(m_playlist); const auto metadata = refreshItems(m_playlist, uris, {reference}); vlc_playlist_Unlock(m_playlist);
    if (!metadata.isEmpty()) emit notifications()->metadataChanged(reinterpret_cast<quintptr>(m_playlist), metadata);
    return true;
}
}
