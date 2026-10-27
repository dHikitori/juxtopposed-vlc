// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_session_ui.hpp"
#include "addon_session_host.hpp"
#include "addon_manager.hpp"
#include <QJsonArray>

namespace vlc::addons {
namespace {
AddonSessionUi::Catalog catalog(AddonManager* manager) {
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
}
AddonSessionUi::AddonSessionUi(AddonSessionHost* host,AddonManager* manager,QObject* parent)
    :AddonSessionUi(host,catalog(manager),parent) {
    if(manager) { connect(manager,&AddonManager::revisionChanged,this,&AddonSessionUi::refresh);
        connect(manager,&AddonManager::capabilitiesChanged,this,&AddonSessionUi::refresh);
        connect(manager,&AddonManager::retiring,this,&AddonSessionUi::close,Qt::DirectConnection); }
}
}
