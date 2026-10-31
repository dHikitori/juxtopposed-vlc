// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import "AddonUi.js" as Contract

Item {
    id: root
    property var document: null
    property var state: ({})
    property var messages: ({})
    property var registeredCommands: []
    // Supplied by the host asset broker, never by a package document/state patch.
    property var assetUrls: ({})
    // Artwork of app items in the view's state (media:<id> handles). The app
    // supplies it; a package document or state cannot add an entry.
    property var hostArtwork: ({})
    property var registry: null
    property string sessionId: ""
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme }
    readonly property bool valid: errorMessage.length === 0 && acceptedDocument !== null
    property string errorMessage: ""
    property var acceptedDocument: null
    property var acceptedState: ({})
    property var acceptedMessages: ({})
    property bool ready: false
    signal commandRequested(string id, var arguments)
    implicitHeight: contents.implicitHeight
    clip: true

    function accept() {
        if (!ready) return
        if (document === null) { acceptedDocument = null; acceptedState = ({}); acceptedMessages = ({}); errorMessage = ""; return }
        const nextDocument = Contract.plain(document, null), nextState = Contract.plain(state, {})
        const nextMessages = Contract.plain(messages, {})
        const problem = Contract.validateView(nextDocument, Contract.plain(registeredCommands, []))
            || Contract.validateData(nextState) || Contract.validateData(nextMessages)
            || Contract.validateProjection(nextDocument.root, nextState, null, nextMessages)
        if (problem) { errorMessage = qsTr("The add-on view has an error. %1").arg(problem); return }
        acceptedState = nextState
        acceptedMessages = nextMessages
        if (JSON.stringify(nextDocument) !== JSON.stringify(acceptedDocument)) acceptedDocument = nextDocument
        errorMessage = ""
    }
    function resolve(value, item) { return Contract.resolve(value, acceptedState, item, acceptedMessages) }
    function text(value, item) { const result = resolve(value, item); return typeof result === "string" ? result : "" }
    function assetFor(handle) {
        if (typeof handle !== "string" || !Contract.own(assetUrls, handle)) return ""
        const url = String(assetUrls[handle])
        return /^image:\/\/addonassets\/[A-Za-z0-9_-]{1,96}$/.test(url) ? url : ""
    }
    function hostArtworkFor(handle) {
        if (typeof handle !== "string" || !handle.startsWith("media:") || !Contract.own(hostArtwork, handle)) return ""
        return String(hostArtwork[handle])
    }
    function claimedAssetFor(handle, claim) {
        const base = assetFor(handle)
        if (!base.length || typeof claim !== "string" || !claim.length || !registry || typeof registry.assetUrlForClaim !== "function") return ""
        const url = registry.assetUrlForClaim(sessionId, handle, claim)
        return url === base + "/" + claim && /^[A-Za-z0-9_-]{1,96}$/.test(claim) ? url : ""
    }
    function dispatch(action, item) {
        if (!valid || Contract.actionError(action, registeredCommands)) return
        const args = Contract.resolveArguments(action.arguments || {}, acceptedState, item, acceptedMessages)
        if (Contract.validateData(args)) return
        // A user gesture reaches the host here; only the broker grants the operation.
        commandRequested(action.command, args)
    }
    onDocumentChanged: accept()
    onStateChanged: if (ready && !stateUpdate.running) stateUpdate.start()
    Timer {
        id: stateUpdate
        interval: 34
        onTriggered: root.accept()
    }
    onMessagesChanged: accept()
    onRegisteredCommandsChanged: accept()
    Component.onCompleted: { ready = true; accept() }
    ColumnLayout {
        id: contents
        width: root.width
        spacing: root.theme.dp(8)
        Text {
            objectName: "addonViewError"
            Layout.fillWidth: true; visible: root.errorMessage.length > 0
            text: root.errorMessage; textFormat: Text.PlainText; wrapMode: Text.Wrap
            color: root.theme.danger; font.pixelSize: root.theme.dp(13)
            Accessible.role: Accessible.AlertMessage
        }
        RedesignAddonNode {
            objectName: "addonViewRoot"
            Layout.fillWidth: true
            node: root.acceptedDocument ? root.acceptedDocument.root : ({})
            renderer: root
            visible: root.acceptedDocument !== null && (node.visible === undefined || root.resolve(node.visible, null) === true)
        }
    }
}
