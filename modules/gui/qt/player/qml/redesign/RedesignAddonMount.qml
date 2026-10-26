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
    property var descriptor: ({})
    property bool ready: false
    readonly property bool usable: sessionId.length > 0 && view.valid
    onUsableChanged: if (usable && registry) registry.acknowledgeSession(sessionId)
    implicitHeight: usable ? view.implicitHeight : 0
    signal failed()

    function release() {
        if (registry && sessionId.length) registry.closeSession(sessionId)
        sessionId = ""; descriptor = ({})
    }
    function restart() {
        if (!ready) return
        release()
        if (!registry || !contributionId.length) return
        sessionId = registry.createSession(contributionId, parameters, hostContext)
        if (!sessionId.length) { descriptor = ({}); failed(); return }
        descriptor = sessionId.length ? registry.session(sessionId) : ({})
        if (!visible && sessionId.length) registry.suspendSession(sessionId, true)
    }
    function fail() {
        if (!sessionId.length) return
        const id = contributionId
        release()
        failed()
        if (registry) registry.reportViewFailure(id)
    }
    onContributionIdChanged: restartTimer.restart()
    onParametersChanged: restartTimer.restart()
    onHostContextChanged: restartTimer.restart()
    onRegistryChanged: restartTimer.restart()
    onVisibleChanged: if (registry && sessionId.length) {
        registry.suspendSession(sessionId, !visible)
        if (visible && usable) registry.acknowledgeSession(sessionId)
    }
    Component.onCompleted: { ready = true; restart() }
    Component.onDestruction: release()
    Timer { id: restartTimer; interval: 0; onTriggered: root.restart() }
    Connections {
        target: root.registry
        function onChanged() { restartTimer.restart() }
        function onSessionChanged(id) { if (id === root.sessionId) root.descriptor = root.registry.session(id) }
        function onSessionFailed(id) { if (id === root.sessionId) root.fail() }
    }
    RedesignAddonView {
        id: view
        width: parent.width; height: implicitHeight
        document: root.descriptor.document || null
        state: root.descriptor.state || ({})
        messages: root.descriptor.messages || ({})
        registeredCommands: root.descriptor.registeredCommands || []
        assetUrls: root.descriptor.assetUrls || ({})
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
        onCommandRequested: (command, arguments) => {
            if (!root.registry.dispatchSession(root.sessionId, command, arguments)) root.fail()
        }
        onErrorMessageChanged: if (errorMessage.length) failureTimer.start()
    }
    Timer { id: failureTimer; interval: 0; onTriggered: if (view.errorMessage.length) root.fail() }
}
