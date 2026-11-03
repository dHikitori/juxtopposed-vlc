// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

Item {
    id: root
    required property var renderer
    property string artworkHandle: ""
    property string iconName: ""
    property int fillMode: Image.PreserveAspectFit
    property string claim: ""
    property var claimRegistry: null
    property bool ready: false
    readonly property var registry: renderer ? renderer.registry : null
    readonly property string sessionId: renderer ? renderer.sessionId : ""
    readonly property string available: renderer ? renderer.assetFor(artworkHandle) : ""
    readonly property string imageUrl: claim.length && renderer ? renderer.claimedAssetFor(artworkHandle, claim) : ""
    // Media artwork that the app supplies for its own items (media: handles).
    readonly property string hostUrl: renderer && typeof renderer.hostArtworkFor === "function" ? renderer.hostArtworkFor(artworkHandle) : ""
    clip: true

    function release() {
        const old = claim, backend = claimRegistry; claim = ""; claimRegistry = null
        if (old.length && backend && typeof backend.releaseImage === "function") backend.releaseImage(old)
    }
    function refresh() {
        if (!ready) return
        if (!visible || !available.length || !registry || !sessionId.length || typeof registry.claimImage !== "function") { release(); return }
        if (!claim.length) { claimRegistry = registry; claim = registry.claimImage(sessionId, root) }
    }
    onAvailableChanged: { release(); refreshTimer.restart() }
    onRegistryChanged: { release(); refreshTimer.restart() }
    onSessionIdChanged: { release(); refreshTimer.restart() }
    onVisibleChanged: refreshTimer.restart()
    Component.onCompleted: { ready = true; refreshTimer.start() }
    Component.onDestruction: release()
    Timer { id: refreshTimer; interval: 0; onTriggered: root.refresh() }
    Connections {
        target: root.registry
        ignoreUnknownSignals: true
        function onImageBudgetChanged() { if (!root.claim.length) refreshTimer.restart() }
    }
    Rectangle { anchors.fill: parent; visible: root.iconName.length && picture.status !== Image.Ready; color: root.renderer.theme.panel }
    RedesignIcon {
        anchors.centerIn: parent; width: root.renderer.theme.dp(24); height: width
        visible: root.iconName.length && picture.status !== Image.Ready
        name: root.iconName; color: root.renderer.theme.foreground
    }
    Image {
        id: picture
        objectName: "addonAssetImage"
        anchors.fill: parent
        source: root.hostUrl || root.imageUrl
        sourceSize: Qt.size(512, 512)
        cache: false; asynchronous: true; fillMode: root.fillMode
        Accessible.ignored: true
    }
}
