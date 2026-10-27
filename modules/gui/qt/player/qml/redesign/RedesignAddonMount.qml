// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

Item {
    id: root
    required property var registry
    property string contributionId: ""
    property var parameters: ({})
    property var hostContext: ({})
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    property string sessionId: ""
    property var sessionRegistry: null
    property var descriptor: ({})
    property bool ready: false
    readonly property bool usable: sessionId.length > 0 && view.valid
    onUsableChanged: if (usable && sessionRegistry) sessionRegistry.acknowledgeSession(sessionId)
    implicitHeight: usable ? view.implicitHeight : 0
    signal failed()

    function release() {
        const backend = sessionRegistry, old = sessionId
        sessionId = ""; descriptor = ({}); sessionRegistry = null
        if (backend && old.length) backend.closeSession(old)
    }
    function restart() {
        if (!ready) return
        release()
        if (!registry || !contributionId.length) return
        sessionRegistry = registry
        sessionId = sessionRegistry.createSession(contributionId, parameters, hostContext)
        if (!sessionId.length) { descriptor = ({}); sessionRegistry = null; failed(); return }
        if (typeof sessionRegistry.bindSessionOwner === "function" && !sessionRegistry.bindSessionOwner(sessionId, root)) { fail(); return }
        if (!loadDescriptor()) return
        if (!visible) sessionRegistry.suspendSession(sessionId, true)
    }
    function loadDescriptor() {
        // Convert native value lists to JSON arrays before renderer validation.
        try { descriptor = JSON.parse(JSON.stringify(sessionRegistry.session(sessionId))); return true }
        catch (error) { fail(); return false }
    }
    function fail() {
        if (!sessionId.length) return
        const id = contributionId, backend = sessionRegistry
        release()
        failed()
        if (backend) backend.reportViewFailure(id)
    }
    onContributionIdChanged: restartTimer.restart()
    onParametersChanged: restartTimer.restart()
    onHostContextChanged: restartTimer.restart()
    onRegistryChanged: restartTimer.restart()
    onVisibleChanged: if (sessionRegistry && sessionId.length) {
        sessionRegistry.suspendSession(sessionId, !visible)
        if (visible && usable) sessionRegistry.acknowledgeSession(sessionId)
    }
    Component.onCompleted: { ready = true; restart() }
    Component.onDestruction: release()
    Timer { id: restartTimer; interval: 0; onTriggered: root.restart() }
    Connections {
        target: root.sessionRegistry || root.registry
        function onChanged() { restartTimer.restart() }
        function onSessionChanged(id) { if (id === root.sessionId && root.sessionRegistry) root.loadDescriptor() }
        function onSessionFailed(id) { if (id === root.sessionId) root.fail() }
    }
    RedesignAddonView {
        id: view
        objectName: "addonUiRenderer"
        width: parent.width; height: implicitHeight
        document: root.descriptor.document || null
        state: root.descriptor.state || ({})
        messages: root.descriptor.messages || ({})
        registeredCommands: root.descriptor.registeredCommands || []
        assetUrls: root.descriptor.assetUrls || ({})
        registry: root.sessionRegistry; sessionId: root.sessionId
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
        onCommandRequested: (command, arguments) => {
            if (!root.sessionRegistry || !root.sessionRegistry.dispatchSession(root.sessionId, command, arguments)) root.fail()
        }
        onErrorMessageChanged: if (errorMessage.length) failureTimer.start()
    }
    Timer { id: failureTimer; interval: 0; onTriggered: if (view.errorMessage.length) root.fail() }
}
