// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "addon_identity_choice.hpp"
#include "addon_accepted_identities.hpp"
#include "addon_metadata_service.hpp"
#include "util/media_identity_vlc.hpp"
#include "util/series_discovery.hpp"
#include "../../../../src/preparser/metadata_local.h"
#include <vlc_metadata_backend.h>
#include <QJsonDocument>
#include <QUrl>
#include <utility>

namespace vlc::addons {
namespace {
void anchorEnded(vlc_metadata_job_t*, int, void*) {}
QString detail(const QVariantMap& identity)
{
    QStringList parts;
    if (const int year = identity.value(QStringLiteral("year")).toInt(); year > 0) parts.append(QString::number(year));
    const auto episodes = identity.value(QStringLiteral("episodes")).toList();
    const int season = identity.value(QStringLiteral("season")).toInt();
    if (season > 0 && episodes.size() == 1)
        parts.append(AddonIdentityChoice::tr("Season %1, episode %2").arg(season).arg(episodes.first().toInt()));
    return parts.join(QStringLiteral(" · "));
}
}

AddonIdentityChoice::AddonIdentityChoice(AddonMetadataService* service, std::shared_ptr<AddonAcceptedIdentities> accepted,
                                         CurrentItem current, QObject* parent)
    : QObject(parent), m_service(service), m_accepted(std::move(accepted)), m_current(std::move(current))
{
    if (!m_service) return;
    connect(m_service, &AddonMetadataService::finished, this, &AddonIdentityChoice::finished);
    connect(m_service, &AddonMetadataService::canceled, this, [this](const QString& consumer) {
        if (!consumer.isEmpty() && consumer == m_consumer) fail(tr("Add-on settings changed. Try again."));
    });
    connect(m_service, &AddonMetadataService::retiring, this, &AddonIdentityChoice::close, Qt::DirectConnection);
}

AddonIdentityChoice::~AddonIdentityChoice() { close(); }

void AddonIdentityChoice::set(const QString& state, const QString& message)
{
    m_state = state;
    m_message = message;
    emit changed();
}

void AddonIdentityChoice::fail(const QString& message)
{
    release();
    m_rows.clear();
    set(QStringLiteral("failed"), message);
}

void AddonIdentityChoice::release()
{
    const QPointer<AddonMetadataService> service(m_service);
    const auto consumer = std::exchange(m_consumer, {});
    const auto selection = std::exchange(m_selection, {});
    const auto choiceSet = std::exchange(m_choiceSet, {});
    const auto target = std::exchange(m_target, {});
    m_references.clear();
    if (service && !consumer.isEmpty()) service->cancel(consumer);
    if (service && !selection.isEmpty()) service->releaseIdentitySelection(selection);
    if (service && !choiceSet.isEmpty()) service->releaseIdentityChoiceSet(choiceSet);
    // Releasing the target cancels its retained anchor.
    if (service && !target.isEmpty()) service->releaseIdentityTarget(target);
    if (m_anchor) vlc_metadata_job_Cancel(m_anchor.get());
    m_anchor.reset();
    m_item.reset();
}

void AddonIdentityChoice::cancel()
{
    if (m_state == QLatin1String("idle")) return;
    release();
    m_rows.clear();
    set(QStringLiteral("idle"));
}

void AddonIdentityChoice::close()
{
    if (m_closed) return;
    m_closed = true;
    release();
    m_rows.clear();
    m_state = QStringLiteral("idle");
}

bool AddonIdentityChoice::start(const QString& queryInput)
{
    const auto query = queryInput.trimmed().left(1024);
    release();
    m_rows.clear();
    if (m_closed || !m_service || !m_current) {
        fail(tr("Add-ons are not available."));
        return false;
    }
    std::shared_ptr<input_item_t> item(m_current(), [](input_item_t* value) { if (value) input_item_Release(value); });
    if (!item) {
        fail(tr("Play an episode first."));
        return false;
    }
    char* uri = input_item_GetURI(item.get());
    const QUrl url(QString::fromUtf8(uri ? uri : ""));
    free(uri);
    const auto local = SeriesDiscovery::identify(url, MediaIdentity::inputMetadata(item.get()));
    if (local.kind != MediaIdentity::Kind::Episode || local.season < 1 || local.episodes.size() != 1
        || (local.title.isEmpty() && query.isEmpty())) {
        fail(tr("Identify works with one numbered TV episode."));
        return false;
    }
    m_local = MediaIdentity::toMap(local);
    m_query = query.isEmpty() ? local.title : query;
    auto* backend = m_service->holdCoreBackend();
    if (!backend) {
        fail(tr("Add-ons are not available."));
        return false;
    }
    const char* fields[]{"episodeTitle"};
    const auto context = QJsonDocument::fromVariant(QVariantMap{{QStringLiteral("identity"), m_local}}).toJson(QJsonDocument::Compact);
    vlc_metadata_request request{};
    request.feature = "video.details";
    request.operation = "enrich";
    request.fields = fields;
    request.field_count = 1;
    request.context_json = context.constData();
    request.context_size = size_t(context.size());
    request.item = item.get();
    // A manual request: network, but not automatic.
    request.flags = VLC_METADATA_REQUEST_NETWORK;
    std::shared_ptr<vlc_metadata_job_t> anchor(vlc_metadata_job_New(backend, &request, anchorEnded, nullptr),
        [](vlc_metadata_job_t* job) { if (job) vlc_metadata_job_Release(job); });
    vlc_metadata_backend_Release(backend);
    if (!anchor || vlc_metadata_job_StartLocal(anchor.get()) != VLC_SUCCESS) {
        fail(tr("The app cannot search for this item now."));
        return false;
    }
    m_item = item;
    m_anchor = anchor;
    QVariantMap hostContext;
    if (!query.isEmpty()) hostContext = {{QStringLiteral("manual"), true}, {QStringLiteral("query"), query}};
    m_target = m_service->bindIdentityTarget(anchor.get(), item.get(), hostContext);
    if (m_target.isEmpty()) {
        fail(tr("The app cannot search for this item now."));
        return false;
    }
    m_consumer = m_service->identify(QStringLiteral("video.identity"), m_target);
    if (m_consumer.isEmpty()) {
        fail(tr("No add-on can identify this item."));
        return false;
    }
    set(QStringLiteral("searching"), tr("Searching…"));
    return true;
}

void AddonIdentityChoice::finished(const QString& consumer, const QString& status, const QVariantMap& data,
                                   const QString& error)
{
    if (consumer.isEmpty() || consumer != m_consumer) return;
    m_consumer.clear();
    if (m_state == QLatin1String("searching")) offer(data, error);
    else if (m_state == QLatin1String("applying")) apply(status, data);
}

void AddonIdentityChoice::offer(const QVariantMap& data, const QString& error)
{
    m_choiceSet = data.value(QStringLiteral("choiceSet")).toString();
    m_references.clear();
    m_rows.clear();
    for (const auto& value : data.value(QStringLiteral("candidates")).toList()) {
        const auto row = value.toMap();
        const auto identity = row.value(QStringLiteral("identity")).toMap();
        const auto reference = row.value(QStringLiteral("reference")).toString();
        if (reference.isEmpty()) continue;
        const auto credit = row.value(QStringLiteral("attribution")).toMap();
        m_references.append(reference);
        m_rows.append(QVariantMap{{QStringLiteral("name"), row.value(QStringLiteral("name"))},
            {QStringLiteral("detail"), detail(identity)},
            {QStringLiteral("source"), credit.value(QStringLiteral("name"))},
            {QStringLiteral("link"), credit.value(QStringLiteral("url"))}});
    }
    if (m_rows.isEmpty()) {
        fail(error.isEmpty() ? tr("No match was found. Try other search text.") : error);
        return;
    }
    set(QStringLiteral("choosing"), data.value(QStringLiteral("warning")).toString());
}

bool AddonIdentityChoice::choose(int index)
{
    if (m_state != QLatin1String("choosing") || index < 0 || index >= m_references.size() || !m_service) return false;
    m_selection = m_service->acceptIdentity(m_target, m_references.at(index));
    if (m_selection.isEmpty()) {
        fail(tr("This choice is no longer current. Search again."));
        return false;
    }
    m_consumer = m_service->enrichSelected(QStringLiteral("video.details"), m_target, m_selection,
        {QStringLiteral("episodeTitle")});
    if (m_consumer.isEmpty()) {
        fail(tr("This choice is no longer current. Search again."));
        return false;
    }
    set(QStringLiteral("applying"), tr("Saving your choice…"));
    return true;
}

void AddonIdentityChoice::apply(const QString& status, const QVariantMap& data)
{
    // The selection is spent by Apply, so read the accepted identity first.
    const auto selected = m_service ? m_service->selectedIdentity(m_target, m_selection) : QVariantMap{};
    const auto identity = selected.value(QStringLiteral("identity")).toMap();
    const auto provider = selected.value(QStringLiteral("provenance")).toMap().value(QStringLiteral("provider")).toString();
    if (identity.isEmpty() || provider.isEmpty()) {
        fail(tr("This choice is no longer current. Search again."));
        return;
    }
    QString title;
    if (status == QLatin1String("match") || status == QLatin1String("partial"))
        title = data.value(QStringLiteral("fields")).toMap().value(QStringLiteral("episodeTitle")).toString().trimmed().left(1024);
    if (!title.isEmpty()) {
        auto* held = m_service->holdIdentityAnchor(m_target, m_selection);
        std::shared_ptr<vlc_metadata_job_t> job(held, [](vlc_metadata_job_t* value) { if (value) vlc_metadata_job_Release(value); });
        input_item_t* copy = job ? vlc_metadata_job_CopyLocalItem(job.get()) : nullptr;
        int applied = VLC_EGENERIC;
        if (copy) {
            const auto provenance = QJsonDocument::fromVariant(QVariantMap{{QStringLiteral("source"), QStringLiteral("identity-choice")},
                {QStringLiteral("provider"), provider}}).toJson(QJsonDocument::Compact);
            input_item_SetMeta(copy, vlc_meta_Title, title.toUtf8().constData());
            if (vlc_metadata_job_PrepareLocal(job.get(), copy, nullptr) == VLC_SUCCESS
                && vlc_metadata_job_Complete(job.get(), VLC_SUCCESS, copy, provenance.constData(), size_t(provenance.size())))
                // The user chose this work, so its title replaces the current one.
                applied = vlc_metadata_job_Apply(job.get(), m_item.get(), 1u << vlc_meta_Title, nullptr);
            input_item_Release(copy);
        }
        if (applied != VLC_SUCCESS) {
            fail(applied == -EAGAIN ? tr("The item changed while it was identified. Try again.")
                                    : tr("The episode title cannot be saved."));
            return;
        }
    }
    QString error;
    const auto key = AddonAcceptedIdentities::key(m_local);
    const auto revision = m_accepted ? m_accepted->accept(key, identity, provider, &error) : 0;
    release();
    m_rows.clear();
    if (!revision) {
        set(QStringLiteral("failed"), error.isEmpty() ? tr("Your choice cannot be saved.") : error);
        return;
    }
    if (m_service) m_service->noteIdentityAccepted();
    set(QStringLiteral("done"), title.isEmpty()
        ? tr("Saved. %1 is used for this series.").arg(identity.value(QStringLiteral("title")).toString())
        : tr("Saved. This episode is \"%1\".").arg(title));
    emit accepted(revision);
}

}
