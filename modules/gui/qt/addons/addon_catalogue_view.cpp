// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_catalogue_view.hpp"
#include "addon_native_wake.hpp"
#include "addon_package_configuration.hpp"
#include "addon_repository_cancellation.hpp"
#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <limits>
namespace vlc::addons {
struct AddonCatalogueView::Data {
    StoreAuthorityToken ownerQueued,queued,authority;
    QVariantList rows,contributions,themes;
    QString selectedTheme;
    QSet<QString> knownDocuments,knownSettings,loadedDocuments,loadedSettings,attemptedSettings;
    QHash<QString,QHash<QString,bool>> presence;
    qint64 documentBytes=0;
    quint64 tlsDemandIdentity=0;
    QHash<QString,qint64> resourceBytes,messageBytes,settingsBytes;
    QJsonObject preferences,displayPreferences;
    QHash<QString,QJsonObject> schemas,values,messages,grants;
    QHash<QString,QVariantMap> secrets;
    QMap<QString,QJsonObject> resources;
    QByteArray digest,documentDigest;
    bool safeMode=false,workerAvailable=false,tlsKnown=false,tlsAvailable=false;
    bool current() const noexcept { return ownerQueued.current() && queued.current() && authority.current(); }
};
struct AddonCatalogueView::Result {
    std::shared_ptr<const Data> data;QString error;
    DisplayCatalogueSnapshot::Refusal refusal=DisplayCatalogueSnapshot::Refusal::None;
};
std::shared_ptr<const AddonCatalogueView::Data> AddonCatalogueView::project(DisplayCatalogueSnapshot snapshot,DisplayCatalogueSource source,const DisplayCatalogueRequest& request)
{
    if(!snapshot.current() || !source.current() || RepositoryCancellation::requested()
        || source.distributionIds.size()>256 || source.builtins.size()>256 || source.bootstrapResults.size()>256
        || source.removing.size()>256 || source.withdrawing.size()>256)return {};
    auto result=std::make_shared<Data>();
    result->ownerQueued=source.queuedAuthority;result->queued=snapshot.queuedAuthority;result->authority=snapshot.authority;
    result->safeMode=snapshot.safeMode;result->preferences=snapshot.preferences;result->displayPreferences=snapshot.displayPreferences;
    result->themes=snapshot.themes;result->selectedTheme=snapshot.selectedTheme;
    for(auto& value:result->themes) {
        auto row=value.toMap();const auto id=row.value(QStringLiteral("packageId")).toString();
        if(source.withdrawing.contains(id) || source.removing.contains(id))row.insert(QStringLiteral("enabled"),false);
        value=row;
    }
    result->loadedDocuments=QSet<QString>(request.documents.begin(),request.documents.end());
    result->attemptedSettings=QSet<QString>(request.settingsPackages.begin(),request.settingsPackages.end());

    // Native file probing belongs to joined work, never a basic-UI getter.
    const QFileInfo worker(source.workerExecutable);
    result->workerAvailable=worker.isFile() && worker.isExecutable();
    for(auto& row:snapshot.packages) {
        const auto id=row.package.manifest.id;
        if(row.corrupt) {
            result->rows.append(QVariantMap{{"id",id},{"name",id},{"installed",true},{"enabled",false},
                {"configured",false},{"configurationKnown",true},{"recoveryRequired",true},{"error",row.error},{"removing",source.removing.contains(id)}});
            continue;
        }
        const auto& package=row.package;
        bool known=true;
        const bool enabled=package.enabled && !source.withdrawing.contains(id) && !source.removing.contains(id);
        auto presence=source.credentialPresence.value(id);
        const auto needsPresence=[&] {
            if(request.settingsCurrent(id))return true;
            if(enabled)for(const auto& contribution:package.manifest.contributions)
                for(const auto& feature:request.presenceFeatures)
                    if(request.featureCurrent(feature) && (contribution.type==feature || contribution.features.contains(feature)))return true;
            return false;
        };
        if(!row.summaryOnly && needsPresence() && source.credentials) {
            const auto properties=row.schema.value(QStringLiteral("properties")).toObject();
            const auto references=row.settings.value(QStringLiteral("_credentials")).toObject();
            QSet<QString> needed;
            for(const auto& value:row.schema.value(QStringLiteral("required")).toArray())needed.insert(value.toString());
            const auto approved=package.grants.value(QStringLiteral("credentials.use")).toObject().value(QStringLiteral("bindings")).toArray();
            for(const auto& value:package.manifest.permissionScope(QStringLiteral("credentials.use")).value(QStringLiteral("bindings")).toArray())
                if(approved.contains(value))needed.insert(value.toObject().value(QStringLiteral("field")).toString());
            for(auto it=properties.begin();it!=properties.end();++it) {
                if(!packageConfiguration::secretField(it.value().toObject()) || (!request.settingsCurrent(id) && !needed.contains(it.key())))continue;
                const auto reference=references.value(it.key()).toString();
                if(reference.isEmpty() || presence.contains(reference))continue;
                if(!source.current() || !snapshot.current() || RepositoryCancellation::requested())return {};
                if(!needsPresence())break; // last page/settings demand was withdrawn
                auto value=source.credentials(id,reference);const bool found=!value.isEmpty();
                value.fill('\0');value.clear();
                if(!source.current() || !snapshot.current())return {};
                presence.insert(reference,found);
            }
        }
        result->presence.insert(id,presence);
        const auto present=[&](const QString& reference) {
            if(reference.isEmpty())return false;
            if(!presence.contains(reference)){known=false;return false;}
            return presence.value(reference);
        };
        const auto account=packageConfiguration::packageAccountReference(package,row.settings,row.schema,present);
        bool configured=packageConfiguration::configured(row.settings,row.schema,present);
        configured=known && configured && !row.retiring && !source.withdrawing.contains(id) && !source.removing.contains(id);
        const QString settingsPartition=QString::fromLatin1(package.digest)+QLatin1Char(':')+QString::number(package.generation)
            +QLatin1Char(':')+QString::number(row.settingsRevision);
        QVariantMap summary{{"id",id},{"name",package.manifest.name},{"version",package.manifest.version},
            {"source",package.identity.source},{"tag",package.identity.tag},{"digest",QString::fromLatin1(package.digest)},
            {"commit",package.identity.distributionSource?QString{}:package.identity.commitId},
            {"distributionSource",package.identity.distributionSource},{"developerSource",package.identity.developerSource},
            {"publisherVerified",false},{"enabled",package.enabled},{"savedEnabled",row.savedEnabled},
            {"displayPartition",settingsPartition},{"configured",configured},{"configurationKnown",known},{"configurationState",!known?QStringLiteral("unknown"):configured?QStringLiteral("ready"):QStringLiteral("missing")},{"installed",true},
            {"recoveryRequired",package.recoveryRequired},{"removing",source.removing.contains(id)},
            {"hasPrevious",!package.previousDigest.isEmpty()},
            // A package without a settings field has nothing to open.
            {"hasSettings",!row.schema.value(QStringLiteral("properties")).toObject().isEmpty()}};
        const auto app=source.distributions.value(id);
        SemanticVersion offered,installed;
        const bool update=package.identity.distributionSource && !package.recoveryRequired
            && app.value("source").toString()==package.identity.source
            && SemanticVersion::parse(app.value("version").toString(),&offered)
            && SemanticVersion::parse(package.manifest.version,&installed) && offered.compare(installed)>0;
        summary.insert(QStringLiteral("appUpdateAvailable"),update);
        summary.insert(QStringLiteral("appUpdateVersion"),update?app.value("version").toString():QString{});
        result->rows.append(summary);
        if(row.summaryOnly)continue; // recovery summary has no document/action inputs
        result->knownSettings.insert(id);
        const bool settingsReady=request.settingsPackages.contains(id) && row.messagesReady
            && row.schema.value(QStringLiteral("type")).toString()==QLatin1String("object");
        if(settingsReady){result->schemas.insert(id,row.schema);result->loadedSettings.insert(id);}
        if(!row.messages.isEmpty()) {
            result->messages.insert(id,row.messages);
            const auto bytes=QJsonDocument(row.messages).toJson(QJsonDocument::Compact).size();
            result->messageBytes.insert(id,bytes);result->documentBytes+=bytes;
        }
        result->grants.insert(id,package.grants);
        QJsonObject publicValues;QVariantMap secretStatus;
        const auto properties=row.schema.value(QStringLiteral("properties")).toObject();
        const auto references=row.settings.value(QStringLiteral("_credentials")).toObject();
        for(auto it=properties.begin();it!=properties.end();++it) {
            const auto field=it.value().toObject();
            if(packageConfiguration::secretField(field)) {
                const auto reference=references.value(it.key()).toString();
                secretStatus.insert(it.key(),QVariantMap{{"present",presence.value(reference,false)},
                    {"known",reference.isEmpty() || presence.contains(reference)}});
            } else if(row.settings.contains(it.key())) publicValues.insert(it.key(),row.settings.value(it.key()));
            else if(field.contains(QStringLiteral("default")))publicValues.insert(it.key(),field.value(QStringLiteral("default")));
        }
        if(settingsReady) {
            result->values.insert(id,publicValues);result->secrets.insert(id,secretStatus);
            const auto bytes=QJsonDocument(QJsonObject{{"schema",row.schema},{"values",publicValues},
                {"secrets",QJsonObject::fromVariantMap(secretStatus)}}).toJson(QJsonDocument::Compact).size();
            result->settingsBytes.insert(id,bytes);result->documentBytes+=bytes;
        }
        for(const auto& contribution:package.manifest.contributions) {
            QString field;
            if(contribution.type==QLatin1String("theme"))field=contribution.document.contains(QStringLiteral("theme"))?QStringLiteral("theme"):QStringLiteral("resource");
            else if(contribution.type.startsWith(QLatin1String("ui.")))field=contribution.document.contains(QStringLiteral("view"))?QStringLiteral("view"):QStringLiteral("resource");
            else if(contribution.type==QLatin1String("visualizer"))field=QStringLiteral("scene");
            if(!field.isEmpty() && package.manifest.files.contains(contribution.document.value(field).toString()))
                result->knownDocuments.insert(id+QLatin1Char('/')+contribution.id+QLatin1Char('/')+field);
            const QString global=id+QLatin1Char('/')+contribution.id;
            const QString partition=QString::fromLatin1(package.digest)+QLatin1Char(':')+QString::number(package.generation);
            const QString displayPartition=partition+QLatin1Char(':')+QString::number(row.settingsRevision);
            // A panel can translate its name and label with the messages
            // "<contribution>.name" and "<contribution>.label".
            auto document=contribution.document;
            if(contribution.type==QLatin1String("ui.panel"))
                for(const auto& field:{QStringLiteral("name"),QStringLiteral("label")}) {
                    const auto message=row.messages.value(contribution.id+QLatin1Char('.')+field);
                    if(message.isString() && !message.toString().trimmed().isEmpty() && message.toString().size()<=128)
                        document.insert(field,message.toString());
                }
            result->contributions.append(QVariantMap{{"id",global},{"packageId",id},
                {"name",document.value(QStringLiteral("name")).toString(package.manifest.name)},
                {"enabled",enabled},{"configured",configured},{"configurationKnown",known},{"configurationState",!known?QStringLiteral("unknown"):configured?QStringLiteral("ready"):QStringLiteral("missing")},
                {"document",document.toVariantMap()},{"accountReference",account},
                {"iconMarkup",row.resources.value(contribution.id+QStringLiteral("/@icon")).value(QStringLiteral("markup")).toString()},
                {"features",contribution.features},{"operations",contribution.operations},{"mediaKinds",contribution.mediaKinds},
                {"cachePartition",partition},{"displayPartition",displayPartition},{"settingsRevision",QVariant::fromValue(row.settingsRevision)}});
        }
        for(auto it=row.resources.cbegin();it!=row.resources.cend();++it) {
            if(it.key().endsWith(QLatin1String("/@icon")))continue;
            const auto* declaration=package.manifest.contribution(it.key().section(QLatin1Char('/'),0,0));
            if(!row.messagesReady && (!declaration || declaration->type!=QLatin1String("theme")))continue;
            const QString key=id+QLatin1Char('/')+it.key();result->resources.insert(key,it.value());
            const auto bytes=QJsonDocument(it.value()).toJson(QJsonDocument::Compact).size();
            result->resourceBytes.insert(key,bytes);result->documentBytes+=bytes;
        }
    }
    if(request.presenceFeatures.contains(QStringLiteral("session.controller"))
        && request.featureCurrent(QStringLiteral("session.controller"))) {
        bool needed=false;
        for(const auto& value:result->contributions) {
            const auto row=value.toMap(),document=row.value(QStringLiteral("document")).toMap();
            needed|=row.value(QStringLiteral("enabled")).toBool()
                && document.value(QStringLiteral("type")).toString()==QLatin1String("session.controller")
                && document.value(QStringLiteral("contract")).toString()==QLatin1String("session@1");
        }
        if(needed) {
            auto supported=source.preparedTls;
            if(!supported && source.tlsProbe) {
                if(!source.current() || !snapshot.current() || RepositoryCancellation::requested())return {};
                if(request.featureCurrent(QStringLiteral("session.controller")))supported=source.tlsProbe();
            }
            if(supported){result->tlsKnown=true;result->tlsAvailable=*supported;result->tlsDemandIdentity=request.featureIdentity(QStringLiteral("session.controller"));}
        }
    }
    QSet<QString> displayed;
    for(const auto& value:result->rows)displayed.insert(value.toMap().value(QStringLiteral("id")).toString());
    const auto removed=snapshot.distributionPolicy.value(QStringLiteral("removed")).toObject();
    for(const auto& id:source.distributionIds)
        if(!displayed.contains(id) && removed.value(id).toBool()) {
            result->rows.append(QVariantMap{{"id",id},{"name",source.distributions.value(id).value(QStringLiteral("name"),id)},
                {"installed",false},{"distributionSource",true},{"publisherVerified",false},{"suppressed",true},{"enabled",false},
                {"configured",false},{"configurationKnown",true},{"savedData",snapshot.savedDataIds.contains(id)}});
            displayed.insert(id);
        }
    for(const auto& id:snapshot.savedDataIds)if(!displayed.contains(id)) {
        result->rows.append(QVariantMap{{"id",id},{"name",id},{"installed",false},{"savedData",true},
            {"enabled",false},{"configured",false},{"configurationKnown",true},{"removing",source.removing.contains(id)}});displayed.insert(id);
    }
    for(const auto& value:source.bootstrapResults) {
        const auto bootstrap=value.toMap();if(bootstrap.value(QStringLiteral("success")).toBool())continue;
        const auto id=bootstrap.value(QStringLiteral("id")).toString(),error=bootstrap.value(QStringLiteral("error")).toString();
        if(id.isEmpty())continue;
        bool found=false;
        for(auto& existing:result->rows) {
            auto row=existing.toMap();if(row.value(QStringLiteral("id")).toString()!=id)continue;
            row.insert(QStringLiteral("bootstrapFailed"),true);row.insert(QStringLiteral("bootstrapError"),error);
            if(row.value(QStringLiteral("error")).toString().isEmpty())row.insert(QStringLiteral("error"),error);
            existing=row;found=true;break;
        }
        if(!found) {
            result->rows.append(QVariantMap{{"id",id},{"name",bootstrap.value(QStringLiteral("name"),id)},
                {"installed",false},{"enabled",false},{"configured",false},{"configurationKnown",true},
                {"bootstrapFailed",true},{"error",error},{"recoveryRequired",bootstrap.value(QStringLiteral("recoveryRequired"))},
                {"bootstrapStatus",bootstrap.value(QStringLiteral("status"))}});
            displayed.insert(id);
        }
    }
    auto removing=source.removing.values();removing.sort();
    for(const auto& id:removing) {
        bool present=false;
        for(auto& value:result->rows) {
            auto row=value.toMap();if(row.value(QStringLiteral("id")).toString()!=id)continue;
            row.insert(QStringLiteral("removing"),true);value=row;present=true;break;
        }
        if(!present)result->rows.append(QVariantMap{{"id",id},{"name",id},{"installed",false},
            {"enabled",false},{"configured",false},{"configurationKnown",true},{"removing",true}});
    }
    if(result->rows.size()>DisplayCatalogueSnapshot::MaximumCandidates)return {};
    const auto builtinEnabled=snapshot.preferences.value(QStringLiteral("_builtinEnabled")).toObject();
    for(const auto& value:source.builtins) {
        auto row=value.toMap();const auto package=row.value(QStringLiteral("packageId")).toString();
        if(builtinEnabled.contains(package))row.insert(QStringLiteral("enabled"),builtinEnabled.value(package).toBool());
        row.insert(QStringLiteral("configurationKnown"),true);result->contributions.append(row);
    }
    QJsonObject resources;
    for(auto it=result->resources.cbegin();it!=result->resources.cend();++it)resources.insert(it.key(),it.value());
    QJsonObject messages,schemas,values,grants,secrets;
    for(auto it=result->messages.cbegin();it!=result->messages.cend();++it)messages.insert(it.key(),it.value());
    for(auto it=result->schemas.cbegin();it!=result->schemas.cend();++it)schemas.insert(it.key(),it.value());
    for(auto it=result->values.cbegin();it!=result->values.cend();++it)values.insert(it.key(),it.value());
    for(auto it=result->grants.cbegin();it!=result->grants.cend();++it)grants.insert(it.key(),it.value());
    for(auto it=result->secrets.cbegin();it!=result->secrets.cend();++it)secrets.insert(it.key(),QJsonObject::fromVariantMap(it.value()));
    const auto encoded=QJsonDocument(QJsonObject{{"rows",QJsonArray::fromVariantList(result->rows)},
        {"contributions",QJsonArray::fromVariantList(result->contributions)},{"grants",grants},
        {"themes",QJsonArray::fromVariantList(result->themes)},{"selectedTheme",result->selectedTheme},
        {"preferences",result->preferences},{"displayPreferences",result->displayPreferences},{"safeMode",result->safeMode},
        {"workerAvailable",result->workerAvailable},{"tlsKnown",result->tlsKnown},{"tlsAvailable",result->tlsAvailable}}).toJson(QJsonDocument::Compact);
    result->digest=QCryptographicHash::hash(encoded,QCryptographicHash::Sha256);
    result->documentDigest=QCryptographicHash::hash(QJsonDocument(QJsonObject{{"resources",resources},
        {"messages",messages},{"schemas",schemas},{"values",values},{"secrets",secrets}}).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256);
    if(!result->current())return {};
    return result;
}
DisplayCatalogueRequest AddonCatalogueView::requests() const
{
    DisplayCatalogueRequest result;QSet<QString> wanted;
    for(const auto& key:m_tickets)wanted.insert(key);
    for(const auto& key:m_lru)wanted.insert(key);
    result.presenceFeatures=m_presenceFeatures;
    for(const auto& key:wanted) {
        if(key.startsWith(QLatin1String("@feature/"))) {
            const auto feature=key.mid(9);if(!result.presenceFeatures.contains(feature))result.presenceFeatures.append(feature);
        } else if(key.endsWith(QLatin1String("/@settings")))result.settingsPackages.append(key.chopped(10));
        else result.documents.append(key);
    }
    for(auto it=m_activeDemands.cbegin();it!=m_activeDemands.cend();++it)result.activeDemands.insert(it.key(),it.value());
    result.documents.sort();result.settingsPackages.sort();result.presenceFeatures.sort();
    return result;
}
bool AddonCatalogueView::knownKey(const QString& key) const
{
    if(!m_data)return false;
    if(key.endsWith(QLatin1String("/@settings")))return m_data->knownSettings.contains(key.chopped(10));
    return m_data->knownDocuments.contains(key);
}
QString AddonCatalogueView::acquire(const QString& key)
{
    if(m_closed || !knownKey(key) || m_tickets.size()>=32 || m_ticketNext==std::numeric_limits<quint64>::max())return {};
    const QString ticket=QString::number(++m_ticketNext,16);
    if(key.endsWith(QLatin1String("/@settings")) && !m_activeDemands.contains(key))m_activeDemands.insert(key,std::make_shared<DisplayDemand>(m_ticketNext));
    m_tickets.insert(ticket,key);m_lru.removeAll(key);
    resourcesChanged();return ticket;
}
QString AddonCatalogueView::acquireDocument(const QString& id,const QString& field)
{ return acquire(id+QLatin1Char('/')+field); }
QString AddonCatalogueView::acquireSettings(const QString& id)
{ return acquire(id+QStringLiteral("/@settings")); }
QString AddonCatalogueView::acquireFeature(const QString& feature)
{
    if(m_closed || feature.isEmpty() || feature.size()>96 || m_tickets.size()>=32
        || m_ticketNext==std::numeric_limits<quint64>::max())return {};
    const auto wanted=requests();if(!wanted.presenceFeatures.contains(feature) && wanted.presenceFeatures.size()>=16)return {};
    const QString key=QStringLiteral("@feature/")+feature;
    const bool present=m_activeDemands.contains(key);
    if(!present)m_activeDemands.insert(key,std::make_shared<DisplayDemand>(m_ticketNext+1));
    const QString ticket=QString::number(++m_ticketNext,16);m_tickets.insert(ticket,key);
    if(!present)resourcesChanged();
    return ticket;
}
bool AddonCatalogueView::requestCurrent(const QString& ticket) const noexcept
{ return isCurrent() && m_tickets.contains(ticket); }
void AddonCatalogueView::release(const QString& ticket)
{
    const auto found=m_tickets.find(ticket);if(found==m_tickets.end())return;
    const auto key=found.value();m_tickets.erase(found);
    if(m_closed)return; // Close already revoked every native demand; no new display publication.
    if(!m_tickets.values().contains(key)) {
        const auto demand=m_activeDemands.take(key);if(demand)demand->open.store(false,std::memory_order_release);
        if(key.startsWith(QLatin1String("@feature/")))return;
        m_lru.removeAll(key);m_lru.prepend(key);
        while(m_lru.size()>16)m_lru.removeLast();
        trim();
    }
}
bool AddonCatalogueView::ensureDocument(const QString& id,const QString& field)
{
    const QString key=id+QLatin1Char('/')+field;
    if(m_closed || !knownKey(key))return false;
    if(m_data && m_data->resources.contains(key))return true;
    m_lru.removeAll(key);m_lru.prepend(key);while(m_lru.size()>16)m_lru.removeLast();
    resourcesChanged();return true;
}
bool AddonCatalogueView::requestPresence(const QString& feature)
{
    if(m_closed || feature.isEmpty() || feature.size()>96)return false;
    if(m_presenceFeatures.contains(feature))return true;
    if(m_presenceFeatures.size()>=16)return false;
    m_presenceFeatures.append(feature);resourcesChanged();return true;
}
void AddonCatalogueView::resourcesChanged()
{
    if(m_closed)return;
    if(m_requestRevision==std::numeric_limits<quint64>::max()){close();return;}
    ++m_requestRevision;m_pending=true;
    // Data authority does not change just because a document is opened.
    if(m_jobs.busy())m_jobs.cancel();else schedule();
}
void AddonCatalogueView::trim()
{
    if(!m_data)return;
    const auto wanted=requests();QSet<QString> keys(wanted.documents.begin(),wanted.documents.end());
    QSet<QString> settings(wanted.settingsPackages.begin(),wanted.settingsPackages.end());
    auto next=std::make_shared<Data>(*m_data);bool changed=false;
    for(auto it=next->resources.begin();it!=next->resources.end();) {
        const auto key=it.key();const bool selected=key.startsWith(next->selectedTheme+QLatin1Char('/')) && !next->selectedTheme.isEmpty();
        const bool form=key.endsWith(QLatin1String("/@settingsUi")) && settings.contains(key.section(QLatin1Char('/'),0,0));
        if(!selected && !form && !keys.contains(key)){next->documentBytes-=next->resourceBytes.take(key);next->loadedDocuments.remove(key);it=next->resources.erase(it);changed=true;}
        else ++it;
    }
    for(auto it=next->schemas.begin();it!=next->schemas.end();) {
        if(!settings.contains(it.key())){next->documentBytes-=next->settingsBytes.take(it.key());next->values.remove(it.key());next->secrets.remove(it.key());next->loadedSettings.remove(it.key());it=next->schemas.erase(it);changed=true;}
        else ++it;
    }
    for(auto it=next->attemptedSettings.begin();it!=next->attemptedSettings.end();) {
        if(!settings.contains(*it)){it=next->attemptedSettings.erase(it);changed=true;}else ++it;
    }
    QSet<QString> packages=settings;
    for(const auto& key:keys)packages.insert(key.section(QLatin1Char('/'),0,0));
    for(auto it=next->messages.begin();it!=next->messages.end();) {
        if(!packages.contains(it.key())){next->documentBytes-=next->messageBytes.take(it.key());it=next->messages.erase(it);changed=true;}else ++it;
    }
    if(changed) {
        next->documentDigest.clear();m_data=std::move(next);
        publishDocuments();
    }
}
void AddonCatalogueView::publishDocuments()
{
    if(m_documentRevision==std::numeric_limits<quint64>::max()){close();return;}
    ++m_documentRevision;emit documentsChanged();
}
bool AddonCatalogueView::holdBootstrap(DisplayCatalogueSource source)
{
    if(!source.bootstrapping)return false;
    const bool first=!m_inputs || !m_inputs->bootstrapping || !source.sameInputs(*m_inputs);
    source.reader={};source.credentials={};source.tlsProbe={};m_inputs=std::move(source);
    m_pending=true;m_loading=true;
    if(first){if(!advance())return true;m_jobs.cancel();publishFreshness();}
    return true;
}
QVariantList AddonCatalogueView::themes() const
{
    QVariantList result;if(m_data)for(const auto& value:m_data->themes)
        if(value.toMap().value(QStringLiteral("enabled")).toBool())result.append(value);
    return result;
}
QString AddonCatalogueView::selectedTheme() const {return m_data?m_data->selectedTheme:QString{};}
QVariantMap AddonCatalogueView::preferences(const QString& section) const {return m_data?m_data->displayPreferences.value(section).toObject().toVariantMap():QVariantMap{};}
QString AddonCatalogueView::documentState(const QString& id,const QString& field) const
{
    const QString key=id+QLatin1Char('/')+field;
    if(!m_data || !m_data->knownDocuments.contains(key))return QStringLiteral("unavailable");
    if(m_data->resources.contains(key))return QStringLiteral("ready");
    return m_data->loadedDocuments.contains(key)?QStringLiteral("unavailable"):QStringLiteral("pending");
}
bool AddonCatalogueView::settingsReady(const QString& id) const
{return settingsState(id)==QLatin1String("ready");}
QString AddonCatalogueView::settingsState(const QString& id) const
{
    if(!m_data || !m_data->knownSettings.contains(id))return QStringLiteral("unavailable");
    if(m_data->loadedSettings.contains(id))return QStringLiteral("ready");
    return m_data->attemptedSettings.contains(id)?QStringLiteral("unavailable"):QStringLiteral("pending");
}
QString AddonCatalogueView::settingsStamp(const QString& id) const
{
    if(!m_data || !m_data->knownSettings.contains(id))return {};
    for(const auto& value:m_data->rows){const auto row=value.toMap();if(row.value(QStringLiteral("id")).toString()==id)return row.value(QStringLiteral("displayPartition")).toString();}
    return {};
}
qint64 AddonCatalogueView::retainedDocumentBytes() const noexcept
{return m_data?m_data->documentBytes:0;}
AddonCatalogueView::AddonCatalogueView(Backend backend,AddonStreamLifetime::Handle lifetime,QObject* parent)
    :QObject(parent),m_backend(std::move(backend)),m_lifetime(std::move(lifetime)),m_jobs(m_lifetime)
{
    m_wake=std::make_unique<AddonNativeWake>([this] {
        if(m_nativeWakes!=std::numeric_limits<unsigned>::max())++m_nativeWakes;
        // A refused reader can still have its owner completion queued when
        // the actual Store unlock arrives. Do not lose that last notice.
        if(m_jobs.busy() && m_runningGeneration==m_generation)m_retryNative=true;
        inputsChanged();
    });
    if(m_wake->valid() && m_backend.subscribe)m_subscription=m_backend.subscribe(m_wake->nativeCallback());
    if(!m_lifetime.current() || !m_subscription) {
        m_error=tr("The add-on catalogue notification is not available.");m_closed=true;m_wake->close();return;
    }
    refresh();
}
AddonCatalogueView::~AddonCatalogueView() { m_destroying=true;close(); }
AddonCatalogueView* AddonCatalogueView::attached(QObject* owner)
{
    if(!owner || QThread::currentThread()!=owner->thread())return nullptr;
    return owner->findChild<AddonCatalogueView*>(QStringLiteral("hakoneDisplayCatalogue"),Qt::FindDirectChildrenOnly);
}
bool AddonCatalogueView::isCurrent() const noexcept
{ return retainedCatalogueCurrent() && !m_loading && m_publishedGeneration==m_generation; }
bool AddonCatalogueView::retainedCatalogueCurrent() const noexcept
{ return !m_closed && m_lifetime.current() && m_data && m_data->current(); }
bool AddonCatalogueView::advance()
{
    if(m_generation==std::numeric_limits<quint64>::max()){close();return false;}
    ++m_generation;m_retryNative=false;return true;
}
void AddonCatalogueView::publishFreshness() { emit freshnessChanged(); }
void AddonCatalogueView::refresh()
{
    Q_ASSERT(QThread::currentThread()==thread());
    if(m_closed)return;
    if(!m_lifetime.current()){close();return;}
    if(!advance())return;
    m_pending=true;m_loading=true;m_error.clear();
    if(m_jobs.busy())m_jobs.cancel();else schedule();
    publishFreshness();
}
void AddonCatalogueView::inputsChanged()
{
    if(m_closed)return;
    const auto capture=m_backend.capture;const QPointer<AddonCatalogueView> alive(this);
    std::optional<DisplayCatalogueSource> source;
    try {if(capture)source=capture();}catch(...){}
    if(!alive || m_closed)return;
    if(source && holdBootstrap(*source))return;
    if(source && source->current() && m_inputs && source->sameInputs(*m_inputs)) {
        if(isCurrent()) {
            // A lazy document can wait for a lock while base rows stay current.
            // Resume that pending work without invalidating the base snapshot.
            if(m_pending&&!m_jobs.busy())schedule();
            return;
        }
        if((m_jobs.busy()&&m_inputs->queuedAuthority.current())
            || (!m_loading&&!m_pending&&!m_error.isEmpty()&&m_inputs->queuedAuthority.current()&&m_lifetime.current()))return;
    }
    // A post-unlock notice cannot repair a fatal read of the same current
    // inputs. Actual authority/input changes and explicit refresh still retry.
    refresh();
}
void AddonCatalogueView::schedule()
{
    if(m_closed || m_scheduled)return;
    m_scheduled=true;
    QTimer::singleShot(0,this,[this] {m_scheduled=false;start();});
}
bool AddonCatalogueView::waitForProcessSlot()
{
    // Availability belongs to the real process permit, not an artificial Store
    // change. The native wake never retains a QObject or a reader.
    try {if(!m_processWake) {
        m_processWake=std::make_unique<AddonNativeWake>([this] {
            if(!m_closed && m_waitingProcess && m_pending
                && AddonRepositoryJobs::processCount()<AddonRepositoryJobs::ProcessLimit)schedule();
        });
    }}catch(...){return false;}
    if(!m_processWake->valid())return false;
    if(!m_processSubscription)
        m_processSubscription=AddonRepositoryJobs::onProcessAvailable(m_processWake->nativeCallback());
    if(!m_processSubscription)return false;
    m_waitingProcess=true;m_pending=true;m_loading=true;
    // A permit can disappear between refusal and subscription. Observe the
    // count again after arming so that release cannot be lost.
    if(AddonRepositoryJobs::processCount()<AddonRepositoryJobs::ProcessLimit)schedule();
    return true;
}
void AddonCatalogueView::stopProcessWait()
{
    m_waitingProcess=false;m_processSubscription={};
}
void AddonCatalogueView::start()
{
    if(m_closed || !m_pending || m_jobs.busy())return;
    if(m_waitingProcess && AddonRepositoryJobs::processCount()>=AddonRepositoryJobs::ProcessLimit)return;
    stopProcessWait();m_retryNative=false;
    const auto generation=m_generation;const auto capture=m_backend.capture;const QPointer<AddonCatalogueView> alive(this);
    std::optional<DisplayCatalogueSource> source;
    try {if(capture)source=capture();}catch(...){}
    if(!alive || m_closed || generation!=m_generation)return;
    if(source && holdBootstrap(*source))return;
    if(!source || !source->current()) {
        // A mutation may still be writing. Its AFTER-depth wake schedules the
        // retry; there is no repeating idle timer or unbounded retry loop.
        m_loading=true;publishFreshness();return;
    }
    m_pending=false;
    auto inputs=*source;inputs.reader={};inputs.credentials={};inputs.tlsProbe={};m_inputs=std::move(inputs);
    if(m_data && m_data->current())
        for(auto it=m_data->presence.cbegin();it!=m_data->presence.cend();++it)
            for(auto value=it.value().cbegin();value!=it.value().cend();++value)
                if(!source->credentialPresence[it.key()].contains(value.key()))source->credentialPresence[it.key()].insert(value.key(),value.value());
    const auto wanted=requests();const auto requestRevision=m_requestRevision;
    if(m_data && m_data->current() && m_data->tlsKnown
        && m_data->tlsDemandIdentity==wanted.featureIdentity(QStringLiteral("session.controller")))source->preparedTls=m_data->tlsAvailable;
    auto result=std::make_shared<Result>();if(m_captures!=std::numeric_limits<unsigned>::max())++m_captures;
    AddonRepositoryJobs::Refusal refusal=AddonRepositoryJobs::Refusal::None;
    const bool accepted=m_jobs.submit([source=std::move(*source),result,wanted] () mutable {
        DisplayCatalogueSnapshot snapshot;
        if(source.reader && source.current()) {
            const bool read=source.reader(wanted,&snapshot,&result->error);
            result->refusal=snapshot.refusal;
            if(read)result->data=project(std::move(snapshot),std::move(source),wanted);
        }
    },[this,generation,requestRevision,result](auto state){completed(generation,requestRevision,result,state);},&refusal);
    if(!alive || m_closed || generation!=m_generation)return;
    if(accepted)m_runningGeneration=generation;
    if(!accepted) {
        if(refusal==AddonRepositoryJobs::Refusal::ProcessQuota && waitForProcessSlot()) {
            m_error=tr("Wait for other add-on work to end.");publishFreshness();return;
        }
        // Qt admission and resource errors have no availability contract here.
        // Never spin or turn an arbitrary failure into an automatic retry.
        stopProcessWait();m_loading=false;
        m_error=tr("The add-on catalogue cannot start. Refresh the list to try again.");publishFreshness();
    }
}
void AddonCatalogueView::completed(quint64 generation,quint64 requestRevision,const std::shared_ptr<Result>& result,AddonRepositoryJobs::Result state)
{
    if(m_closed)return;
    const bool nativeRetry=m_retryNative && m_runningGeneration==generation;
    m_retryNative=false;m_runningGeneration=0;
    if(generation!=m_generation || requestRevision!=m_requestRevision){if(m_pending)schedule();return;}
    m_loading=false;
    if(state==AddonRepositoryJobs::Result::Finished && result->data && result->data->current() && m_lifetime.current()) {
        const bool changed=!m_data || m_data->digest!=result->data->digest;
        const bool documents=!m_data || m_data->documentDigest!=result->data->documentDigest;
        auto previous=std::move(m_data);m_data=result->data;m_publishedGeneration=generation;m_error.clear();if(m_publications!=std::numeric_limits<unsigned>::max())++m_publications;
        const QPointer<AddonCatalogueView> alive(this);
        if(changed) {
            if(m_revision==std::numeric_limits<quint64>::max()){close();return;}
            ++m_revision;emit catalogueChanged();
        }
        previous.reset();if(!alive || m_closed)return;
        if(documents)publishDocuments();
        if(alive && !m_closed)publishFreshness();
    } else {
        const bool storeBusy=state==AddonRepositoryJobs::Result::Finished
            && result->refusal==DisplayCatalogueSnapshot::Refusal::StoreBusy && m_lifetime.current();
        // Keep real lock contention pending for an actual unlock, with no idle
        // timer. Do not retry a fatal reader because of its own unlock notice.
        m_pending=storeBusy;
        if(storeBusy && nativeRetry) {
            m_loading=!isCurrent();m_error.clear();schedule();publishFreshness();return;
        }
        m_error=result->error.isEmpty()?tr("The add-on catalogue changed, or the app cannot read it."):result->error;
        publishFreshness();
    }
}
void AddonCatalogueView::close()
{
    if(m_closed && !m_wake)return;
    m_closed=true;m_pending=false;m_loading=false;m_retryNative=false;m_runningGeneration=0;
    for(const auto& demand:m_activeDemands)demand->open.store(false,std::memory_order_release);
    m_activeDemands.clear();
    if(m_wake)m_wake->close();
    if(m_processWake)m_processWake->close();
    stopProcessWait();m_processWake.reset();
    m_subscription={};m_wake.reset();m_jobs.close();
    Backend backend;std::swap(backend,m_backend);
    if(!m_destroying)publishFreshness();
}
QVariantList AddonCatalogueView::rows() const { return m_data?m_data->rows:QVariantList{}; }
QVariantList AddonCatalogueView::contributions(const QString& type,bool includeDisabled) const
{
    QVariantList result;if(!m_data)return result;
    for(const auto& value:m_data->contributions) {
        const auto row=value.toMap();const auto document=row.value(QStringLiteral("document")).toMap();
        if(document.value(QStringLiteral("type")).toString()==type && (includeDisabled || row.value(QStringLiteral("enabled")).toBool()))result.append(row);
    }
    return result;
}
QVariantList AddonCatalogueView::providers(const QString& feature) const
{
    QVariantList result;if(!m_data)return result;
    for(const auto& value:m_data->contributions) {
        auto row=value.toMap();
        if(!row.value(QStringLiteral("features")).toStringList().contains(feature))continue;
        row.insert(QStringLiteral("status"),!row.value(QStringLiteral("enabled")).toBool()?tr("Disabled")
            :!row.value(QStringLiteral("configurationKnown")).toBool()?tr("Settings not checked")
            :row.value(QStringLiteral("configured")).toBool()?tr("Ready"):tr("Set up first"));
        result.append(row);
    }
    const auto order=m_data->preferences.value(feature).toArray();
    const auto rank=[&](const QVariant& row) {const auto id=row.toMap().value(QStringLiteral("id")).toString();for(qsizetype i=0;i<order.size();++i)if(order.at(i).toString()==id)return i;return order.size();};
    std::stable_sort(result.begin(),result.end(),[&](const auto& a,const auto& b){return rank(a)<rank(b);});return result;
}
QJsonObject AddonCatalogueView::resource(const QString& contribution,const QString& field) const
{ return m_data?m_data->resources.value(contribution+QLatin1Char('/')+field):QJsonObject{}; }
QVariantMap AddonCatalogueView::settingsSchema(const QString& id) const {return m_data?m_data->schemas.value(id).toVariantMap():QVariantMap{};}
QVariantMap AddonCatalogueView::settingsValues(const QString& id) const {return m_data?m_data->values.value(id).toVariantMap():QVariantMap{};}
QVariantMap AddonCatalogueView::settingsMessages(const QString& id) const {return m_data?m_data->messages.value(id).toVariantMap():QVariantMap{};}
QVariantMap AddonCatalogueView::settingsSecretStatus(const QString& id) const {return m_data?m_data->secrets.value(id):QVariantMap{};}
QJsonObject AddonCatalogueView::grantedScope(const QString& id,const QString& capability) const {return m_data?m_data->grants.value(id).value(capability).toObject():QJsonObject{};}
QVariantMap AddonCatalogueView::capabilities() const
{
    return {{QStringLiteral("workerAvailable"),m_data && m_data->workerAvailable},{QStringLiteral("workerKnown"),bool(m_data)},
        {QStringLiteral("safeMode"),!m_data || m_data->safeMode},{QStringLiteral("safeModeKnown"),bool(m_data)},{QStringLiteral("networkHttpKnown"),m_data && m_data->tlsKnown},
        {QStringLiteral("networkHttpAvailable"),m_data && m_data->tlsAvailable},{QStringLiteral("webSocketKnown"),m_data && m_data->tlsKnown},{QStringLiteral("webSocketAvailable"),m_data && m_data->tlsAvailable}};
}
}
