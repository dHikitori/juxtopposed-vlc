// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_session_ui.hpp"
#include "addon_session_host.hpp"
#include "addon_manager.hpp"
#include "addon_catalogue_view.hpp"
#include <QJsonArray>
#include <memory>

namespace vlc::addons {
namespace {
AddonSessionUi::Catalog legacyCatalog(AddonManager* manager) {
    const QPointer<AddonManager> owner(manager);
    return {manager,[owner] {
        QVariantList result; if(!owner) return result;
        const auto capabilities=owner->capabilities();
        if(!capabilities.value("workerAvailable").toBool() || !capabilities.value("webSocketAvailable").toBool()) return result;
        for(const auto& value:owner->contributionsOfType(QStringLiteral("session.controller"))) {
            const auto row=value.toMap(),document=row.value("document").toMap();
            if(document.value("contract").toString()!=QLatin1String("session@1") || !document.value("operations").toStringList().contains(QStringLiteral("run"))) continue;
            const auto id=row.value("packageId").toString(); auto projected=row;
            projected.insert("origins",owner->grantedScope(id,QStringLiteral("network.websocket")).value("origins").toArray().toVariantList());
            projected.insert("operations",owner->grantedScope(id,QStringLiteral("playback.control")).value("operations").toArray().toVariantList());
            result.append(projected);
        }
        return result;
    }};
}
// This state belongs to the app thread. It holds a display demand, not native
// approval. It never crosses into a session process or a Core cancellation.
struct DisplayCatalog final : std::enable_shared_from_this<DisplayCatalog> {
    QPointer<AddonManager> manager;
    QPointer<AddonCatalogueView> view;
    QString ticket,error;
    quint64 epoch=0;
    bool active=false,pending=false,attempted=false,closed=false;
    explicit DisplayCatalog(AddonManager* owner):manager(owner),view(AddonCatalogueView::attached(owner)) {}
    ~DisplayCatalog() { retire(); }
    bool exact() const {
        return !closed && manager && view && AddonCatalogueView::attached(manager)==view;
    }
    QVariantMap capabilities() const { return exact() ? manager->displayCapabilities() : QVariantMap{}; }
    bool platformReady(const QVariantMap& caps) const {
        return caps.value("workerKnown").toBool() && caps.value("workerAvailable").toBool()
            && caps.value("webSocketKnown").toBool() && caps.value("webSocketAvailable").toBool()
            && caps.value("safeModeKnown").toBool() && !caps.value("safeMode").toBool();
    }
    QVariantList rows() const {
        QVariantList result;
        if(!exact()) return result;
        const auto caps=capabilities();
        for(const auto& value:view->contributions(QStringLiteral("session.controller"),true)) {
            const auto row=value.toMap(),document=row.value("document").toMap();
            if(document.value("contract").toString()!=QLatin1String("session@1")
                || !document.value("operations").toStringList().contains(QStringLiteral("run"))) continue;
            auto projected=row; const auto package=row.value("packageId").toString();
            const auto origins=view->grantedScope(package,QStringLiteral("network.websocket")).value("origins").toArray().toVariantList();
            projected.insert("origins",origins);
            projected.insert("operations",view->grantedScope(package,QStringLiteral("playback.control")).value("operations").toArray().toVariantList());
            const bool enabled=row.value("enabled").toBool(),known=row.value("configurationKnown").toBool();
            const bool configured=known && row.value("configured").toBool();
            QString state;
            if(!enabled) state=AddonSessionUi::tr("Disabled");
            else if(!known) state=AddonSessionUi::tr("Settings not checked");
            else if(!configured) state=AddonSessionUi::tr("Set up first");
            else if(!caps.value("workerKnown").toBool() || !caps.value("webSocketKnown").toBool()) state=AddonSessionUi::tr("Checking app support");
            else if(!platformReady(caps)) state=AddonSessionUi::tr("This controller is not available.");
            else if(!view->isCurrent()) state=AddonSessionUi::tr("The controller list changed.");
            projected.insert("ready",enabled && configured && platformReady(caps) && !origins.isEmpty());
            projected.insert("state",state); result.append(projected);
        }
        return result;
    }
    QVariantMap state() const {
        const bool present=exact();
        const bool current=present && view->isCurrent();
        const bool ready=current && active && !pending && !ticket.isEmpty() && platformReady(capabilities());
        QString message=error;
        if(message.isEmpty() && !present) message=AddonSessionUi::tr("The controller list is not available.");
        else if(message.isEmpty() && present) message=view->error();
        return {{"current",current},{"ready",ready},{"loading",present && view->loading()},{"error",message}};
    }
    bool eligible() const {
        const auto caps=capabilities();
        if((caps.value("workerKnown").toBool() && !caps.value("workerAvailable").toBool())
            || (caps.value("safeModeKnown").toBool() && caps.value("safeMode").toBool())) return false;
        for(const auto& value:rows()) {
            const auto row=value.toMap();
            if(row.value("enabled").toBool() && (!row.value("configurationKnown").toBool() || row.value("configured").toBool())
                && !row.value("origins").toList().isEmpty()) return true;
        }
        return false;
    }
    void release() {
        QString retired; retired.swap(ticket);
        const auto held=manager;
        if(held && !retired.isEmpty()) held->releaseDisplayRequest(retired);
    }
    void maintain() {
        const auto held=shared_from_this();
        if(!active || pending || !exact() || !view->isCurrent()) return;
        if(!eligible()) { attempted=false; release(); return; }
        if(!ticket.isEmpty() || attempted) return;
        attempted=true; pending=true; const auto capturedEpoch=epoch; const auto capturedView=view; const auto capturedManager=manager;
        // Keep the pending state before a call that may notify observers.
        const auto acquired=capturedManager->prepareDisplayFeature(QStringLiteral("session.controller"));
        pending=false;
        if(closed || !active || epoch!=capturedEpoch || !exact() || view!=capturedView) {
            if(capturedManager && !acquired.isEmpty()) capturedManager->releaseDisplayRequest(acquired);
            return;
        }
        ticket=acquired;
        if(ticket.isEmpty()) error=AddonSessionUi::tr("The app cannot check this controller. Try again.");
    }
    void setActive(bool value) {
        if(closed || active==value) return;
        active=value; ++epoch;
        if(!active) { attempted=false; error.clear(); release(); return; }
        maintain();
    }
    void retry() {
        if(!active || pending || !exact()) return;
        attempted=false; error.clear(); view->refresh();
        // Admission waits for a verified catalogue publication. A memory-only
        // capability notification does not acquire another feature ticket.
    }
    void retire() {
        if(closed) return;
        closed=true; active=false; ++epoch; release();
    }
};
AddonSessionUi::Catalog catalog(AddonManager* manager) {
    // The mode cannot change to legacy if an attached view later disappears.
    if(!manager || !manager->displayCatalogueAttached()) return legacyCatalog(manager);
    auto display=std::make_shared<DisplayCatalog>(manager);
    AddonSessionUi::Catalog result; result.lifetime=manager; result.attached=true;
    result.read=[display] { return display->rows(); };
    result.state=[display] { return display->state(); };
    result.setActive=[display](bool active) { display->setActive(active); };
    result.retry=[display] { display->retry(); };
    result.changes=display->view;
    result.cataloguePublished=[display] { display->maintain(); };
    return result;
}
}
AddonSessionUi::AddonSessionUi(AddonSessionHost* host,AddonManager* manager,QObject* parent)
    :AddonSessionUi(host,catalog(manager),parent) {
    if(m_backend.catalog.attached) {
        const auto view=qobject_cast<AddonCatalogueView*>(m_backend.catalog.changes.data());
        if(view) {
            const auto published=[this] {
                const QPointer<AddonSessionUi> alive(this); auto published=m_backend.catalog.cataloguePublished;
                if(published) published(); published={}; if(alive) refresh();
            };
            connect(view,&AddonCatalogueView::catalogueChanged,this,published);
            connect(view,&AddonCatalogueView::freshnessChanged,this,published);
            connect(view,&QObject::destroyed,this,&AddonSessionUi::refresh);
        }
    }
    if(manager) { connect(manager,&AddonManager::revisionChanged,this,&AddonSessionUi::refresh);
        connect(manager,&AddonManager::capabilitiesChanged,this,&AddonSessionUi::refresh);
        connect(manager,&AddonManager::retiring,this,&AddonSessionUi::close,Qt::DirectConnection); }
    refresh();
}
}
