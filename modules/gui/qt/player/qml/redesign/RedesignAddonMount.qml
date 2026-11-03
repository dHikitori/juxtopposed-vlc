// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Controls.Basic as C

Item {
    id: root
    required property var registry
    property string contributionId: ""
    property var parameters: ({})
    property var hostContext: ({})
    property var hostArtwork: ({})
    property bool bleed: false
    property string headerRow: ""
    property Component headerTrailing: null
    property Item headerHost: null
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    property string sessionId: ""
    property var sessionRegistry: null
    property var descriptor: ({})
    property bool ready: false
    property bool forceRestart: false
    readonly property bool catalogueAvailable: {
        const backend = sessionRegistry || registry
        return !!backend && (backend.available === undefined || backend.available === true)
    }
    readonly property bool pending: contributionId.length > 0 && (!!descriptor.pending || !!registry && registry.loading === true)
    readonly property bool usable: sessionId.length > 0 && view.valid
    readonly property bool actionable: usable && catalogueAvailable && descriptor.pending !== true && descriptor.unavailable !== true
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme }
    onUsableChanged: if (usable && actionable && sessionRegistry) sessionRegistry.acknowledgeSession(sessionId)
    implicitHeight: usable ? view.implicitHeight : pending ? theme.dp(40) : 0
    signal failed()

    function release() {
        const backend = sessionRegistry, old = sessionId
        sessionId = ""; descriptor = ({}); sessionRegistry = null
        if (backend && old.length && typeof backend.closeSession === "function") backend.closeSession(old)
    }
    function schedule(force) {
        if (!ready) return
        forceRestart = forceRestart || force
        restartTimer.restart()
    }
    function synchronize() {
        if (!ready) return
        const forced = forceRestart
        forceRestart = false
        if (!forced && sessionId.length && sessionRegistry === registry) {
            const current = sessionRegistry.session(sessionId)
            if (current && current.id === contributionId) {
                loadDescriptor()
                if (visible && catalogueAvailable) sessionRegistry.suspendSession(sessionId, false)
                if (actionable) acknowledgeTimer.restart()
                return
            }
        }
        if (!catalogueAvailable && registry && registry.loading === true) {
            if (forced) release()
            forceRestart = forced
            return
        }
        restart()
    }
    function restart() {
        if (!ready) return
        release()
        if (!registry || !contributionId.length) return
        if (registry.available === false) return
        sessionRegistry = registry
        sessionId = sessionRegistry.createSession(contributionId, parameters, hostContext)
        if (!sessionId.length) { console.warn("Add-on view " + contributionId + ": the app could not open it"); descriptor = ({}); sessionRegistry = null; failed(); return }
        if (typeof sessionRegistry.bindSessionOwner === "function" && !sessionRegistry.bindSessionOwner(sessionId, root)) { unavailable(); return }
        if (!loadDescriptor()) return
        if (!visible) sessionRegistry.suspendSession(sessionId, true)
    }
    function loadDescriptor() {
        try {
            descriptor = JSON.parse(JSON.stringify(sessionRegistry.session(sessionId)))
            if (descriptor.unavailable === true) { unavailable(); return false }
            return true
        } catch (error) { fail(); return false }
    }
    function unavailable() { console.warn("Add-on view " + contributionId + ": it is not available"); release(); failed() }
    function fail() {
        if (!sessionId.length || pending || !catalogueAvailable) return
        const id = contributionId, backend = sessionRegistry
        console.warn("Add-on view " + id + " stopped" + (view.errorMessage.length ? ": " + view.errorMessage : ""))
        release()
        failed()
        if (backend) backend.reportViewFailure(id)
    }
    onContributionIdChanged: schedule(true)
    onParametersChanged: schedule(true)
    onHostContextChanged: schedule(true)
    onRegistryChanged: schedule(true)
    onVisibleChanged: if (sessionRegistry && sessionId.length) {
        sessionRegistry.suspendSession(sessionId, !visible)
        if (visible && actionable) sessionRegistry.acknowledgeSession(sessionId)
    }
    Component.onCompleted: { ready = true; forceRestart = false; restartTimer.stop(); restart() }
    Component.onDestruction: release()
    Timer { id: restartTimer; interval: 0; onTriggered: root.synchronize() }
    Timer { id: acknowledgeTimer; interval: 0; onTriggered: if (root.actionable && root.sessionRegistry) root.sessionRegistry.acknowledgeSession(root.sessionId) }
    Connections {
        target: root.sessionRegistry || root.registry
        ignoreUnknownSignals: true
        function onChanged() { root.schedule(false) }
        function onAvailabilityChanged() { root.schedule(false) }
        function onSessionChanged(id) {
            if (id !== root.sessionId || !root.sessionRegistry) return
            if (root.loadDescriptor() && root.actionable) acknowledgeTimer.restart()
        }
        function onSessionFailed(id) {
            if (id !== root.sessionId || !root.sessionRegistry) return
            const current = root.sessionRegistry.session(id)
            if (!current || !current.id || current.unavailable === true) root.unavailable()
            else root.fail()
        }
    }
    Row {
        id: loadingRow
        objectName: "addonMountLoading"
        anchors.centerIn: parent
        spacing: root.theme.dp(8)
        visible: root.pending && !root.usable
        C.BusyIndicator { width: root.theme.dp(24); height: width; running: loadingRow.visible && root.visible }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Loading add-on view…"); textFormat: Text.PlainText
            color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
            Accessible.role: Accessible.StaticText
        }
    }
    RedesignAddonView {
        id: view
        objectName: "addonUiRenderer"
        width: parent.width; height: implicitHeight
        enabled: root.catalogueAvailable && root.descriptor.pending !== true
        document: root.descriptor.document || null
        state: root.descriptor.state || ({})
        messages: root.descriptor.messages || ({})
        registeredCommands: root.descriptor.registeredCommands || []
        assetUrls: root.descriptor.assetUrls || ({})
        hostArtwork: root.hostArtwork
        bleed: root.bleed; headerRow: root.headerRow; headerTrailing: root.headerTrailing; headerHost: root.headerHost
        registry: root.sessionRegistry; sessionId: root.sessionId
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
        onCommandRequested: (command, arguments) => {
            if (!root.actionable) return
            if (!root.sessionRegistry || !root.sessionRegistry.dispatchSession(root.sessionId, command, arguments)) root.fail()
        }
        onErrorMessageChanged: if (errorMessage.length && !root.pending && root.catalogueAvailable) failureTimer.start()
    }
    Timer { id: failureTimer; interval: 0; onTriggered: if (view.errorMessage.length && !root.pending && root.catalogueAvailable) root.fail() }
}
