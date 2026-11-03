// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Window
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var backend
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    property Component videoBackdropContent
    property var packageSnapshot: null
    property var safeModeSnapshot: null
    property var actionGuard: null
    property bool actionsAvailable: true
    property bool ready: false
    Component.onCompleted: ready = true
    property string actionError: ""
    property string actionStatus: ""
    property string pendingAction: ""
    property string safeModeRequest: ""
    readonly property bool supported: !!backend && typeof backend.requestSafeMode === "function"
    // Store writes run on background work; recovery waits for them like a request.
    readonly property bool busy: !!backend && (backend.busy === true || backend.writing === true)
    readonly property bool safeMode: {
        if (safeModeSnapshot !== null) return safeModeSnapshot === true
        if (!ready || !backend) return false
        backend.revision
        return backend.safeMode === true
    }
    readonly property var packageRows: {
        if (packageSnapshot !== null) return packageSnapshot
        if (!ready || !backend) return []
        backend.revision
        return backend.packages || []
    }
    readonly property var recoverable: packageRows.filter(row => row.recoveryRequired === true || row.savedData === true || (row.suppressed === true && row.distributionSource === true))
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme }
    visible: supported
    spacing: theme.dp(8)
    function signature(row) {
        return JSON.stringify([row.id, row.version || "", row.digest || "", row.source || "", row.enabled === true,
            row.recoveryRequired === true, row.suppressed === true, row.distributionSource === true, row.savedData === true])
    }
    function currentRow(id) { return packageRows.find(row => row.id === id) || null }
    function permitted(row, operation) {
        if (!row || row.builtin === true) return false
        if (operation === "restore") return row.suppressed === true && row.distributionSource === true && typeof backend.stageDistributionPackage === "function"
        if (operation === "recover") return row.recoveryRequired === true && typeof backend.requestRecoverPackage === "function"
        return (operation === "remove" && row.recoveryRequired === true || operation === "removeData" && (row.recoveryRequired === true || row.savedData === true)) && typeof backend.uninstall === "function"
    }
    function completedAction(operation) {
        actionError = backend.errorMessage || ""
        actionStatus = actionError ? "" : operation === "restore" ? qsTr("The package is ready for a permission review.")
            : operation === "recover" ? qsTr("The app recovered the add-on. The add-on stays disabled.") : qsTr("The app removed the add-on.")
    }
    function result(operation, accepted) {
        if (!accepted) {
            actionError = backend.errorMessage || qsTr("The app cannot complete this action.")
            actionStatus = ""
        } else if (busy) {
            pendingAction = operation
            actionStatus = operation === "restore" ? qsTr("The app prepares the package for review.") : qsTr("The app removes the add-on.")
        } else completedAction(operation)
    }
    function currentAction() {
        if (!actionsAvailable) {
            actionError = qsTr("The extension list changed. Try this action again.")
            return false
        }
        if (!actionGuard || actionGuard()) return true
        actionError = qsTr("The extension list changed. Try this action again.")
        return false
    }
    function request(id, operation) {
        if (!currentAction()) return false
        const row = currentRow(id)
        if (busy || !permitted(row, operation)) return false
        actionError = ""; actionStatus = ""
        if (operation === "restore") {
            result(operation, backend.stageDistributionPackage(id))
            return true
        }
        confirmation.packageId = id; confirmation.operation = operation
        confirmation.packageState = signature(row); confirmation.revision = backend.revision; confirmation.savedDataOnly = row.installed === false
        confirmation.open(); return true
    }
    Connections {
        target: root.backend
        ignoreUnknownSignals: true
        function onWriteFinished(request, success, error) {
            if (!request || request !== root.safeModeRequest) return
            root.safeModeRequest = ""
            if (!success) root.actionError = error || qsTr("The app cannot change the add-on state.")
        }
    }
    onBusyChanged: if (!busy && pendingAction) {
        const operation = pendingAction; pendingAction = ""; completedAction(operation)
    }
    RedesignButton {
        objectName: "addonSafeMode"
        caption: root.safeMode ? qsTr("Use add-ons") : qsTr("Temporarily disable add-ons")
        text: caption; tooltipTextFormat: Text.PlainText
        enabled: root.supported
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
        onClicked: {
            root.safeModeRequest = root.backend.requestSafeMode(!root.safeMode)
            root.actionError = root.safeModeRequest ? "" : root.backend.errorMessage || qsTr("The app cannot change the add-on state.")
        }
    }
    Text {
        Layout.fillWidth: true
        text: root.safeMode ? qsTr("Add-ons are temporarily disabled. Installed packages and their settings stay in the app.")
            : qsTr("Use this control if an add-on causes a problem. You can use it while a repository request is in progress.")
        textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
    }
    Repeater {
        model: root.recoverable
        ColumnLayout {
            id: row
            required property var modelData
            readonly property var retained: {
                // The manager reads the package on background work and then signals readsChanged.
                root.backend.revision; root.backend.readRevision
                return typeof root.backend.retainedGenerations === "function" ? root.backend.retainedGenerations(modelData.id) : []
            }
            Layout.fillWidth: true
            spacing: root.theme.dp(6)
            Text {
                Layout.fillWidth: true
                text: row.modelData.name || row.modelData.id; textFormat: Text.PlainText; wrapMode: Text.Wrap
                color: root.theme.settingsHeading; font.pixelSize: root.theme.dp(15)
            }
            Text {
                Layout.fillWidth: true
                text: row.modelData.recoveryRequired === true ? qsTr("This add-on needs recovery.") : row.modelData.savedData === true ? qsTr("This add-on was removed. Its saved data stays in the app.") : qsTr("This app package was removed.")
                textFormat: Text.PlainText; wrapMode: Text.Wrap
                color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
            }
            Text {
                Layout.fillWidth: true; visible: !!row.modelData.error
                text: row.modelData.error || ""; textFormat: Text.PlainText; wrapMode: Text.Wrap
                color: root.theme.danger; font.pixelSize: root.theme.dp(13)
            }
            Text {
                Layout.fillWidth: true; visible: row.modelData.recoveryRequired === true && row.retained.length > 0
                text: qsTr("Saved versions: %1").arg(row.retained.map(entry => entry.version).join(", "))
                textFormat: Text.PlainText; wrapMode: Text.Wrap
                color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
            }
            RedesignButton {
                objectName: "addonRecover_" + row.modelData.id
                visible: row.modelData.recoveryRequired === true
                enabled: root.actionsAvailable && !root.busy && typeof root.backend.requestRecoverPackage === "function"
                caption: qsTr("Recover add-on"); text: caption; tooltipTextFormat: Text.PlainText
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.request(row.modelData.id, "recover")
            }
            RedesignButton {
                objectName: "addonRestore_" + row.modelData.id
                visible: row.modelData.suppressed === true && row.modelData.distributionSource === true
                enabled: root.actionsAvailable && !root.busy && typeof root.backend.stageDistributionPackage === "function"
                caption: qsTr("Review app package"); text: caption; tooltipTextFormat: Text.PlainText
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.request(row.modelData.id, "restore")
            }
            Flow {
                Layout.fillWidth: true; spacing: root.theme.dp(6)
                visible: row.modelData.recoveryRequired === true || row.modelData.savedData === true
                RedesignButton {
                    objectName: "addonRecoveryRemove_" + row.modelData.id
                    visible: row.modelData.recoveryRequired === true
                    enabled: root.actionsAvailable && !root.busy && typeof root.backend.uninstall === "function"
                    caption: qsTr("Remove add-on"); text: caption; tooltipTextFormat: Text.PlainText
                    scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                    onClicked: root.request(row.modelData.id, "remove")
                }
                RedesignButton {
                    objectName: "addonRecoveryRemoveData_" + row.modelData.id
                    enabled: root.actionsAvailable && !root.busy && typeof root.backend.uninstall === "function"
                    caption: row.modelData.installed === false ? qsTr("Delete saved data") : qsTr("Delete add-on and saved data"); text: caption; tooltipTextFormat: Text.PlainText
                    scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                    onClicked: root.request(row.modelData.id, "removeData")
                }
            }
        }
    }
    Text {
        objectName: "addonRecoveryStatus"
        Layout.fillWidth: true; visible: text.length > 0
        text: root.actionStatus; textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
        Accessible.role: Accessible.StaticText
    }
    Text {
        objectName: "addonRecoveryError"
        Layout.fillWidth: true; visible: text.length > 0
        text: root.actionError; textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.theme.danger; font.pixelSize: root.theme.dp(13)
        Accessible.role: Accessible.AlertMessage
    }
    RedesignConfirmationDialog {
        id: confirmation
        objectName: "addonRecoveryConfirmation"
        parent: root.Window.contentItem
        property string packageId: ""
        property string operation: ""
        property string packageState: ""
        property int revision: -1
        property bool savedDataOnly: false
        title: operation === "recover" ? qsTr("Recover add-on?") : operation === "removeData" ? savedDataOnly ? qsTr("Delete saved data?") : qsTr("Delete add-on and saved data?") : qsTr("Remove add-on?")
        message: operation === "recover" ? qsTr("The app will select a saved package that passed validation. The add-on will stay disabled. Its settings and private data will match that package.")
            : operation === "removeData" ? savedDataOnly ? qsTr("The app will delete the saved settings, saved keys, private data, and damaged package files for this add-on.") : qsTr("The app will remove the add-on and delete its saved settings, saved keys, and private data.")
            : qsTr("The app will remove the add-on and keep its saved settings, saved keys, and private data. Damaged files stay in the recovery folder.")
        confirmText: operation === "recover" ? qsTr("Recover add-on") : operation === "removeData" ? savedDataOnly ? qsTr("Delete saved data") : qsTr("Delete add-on and saved data") : qsTr("Remove add-on")
        destructive: operation !== "recover"
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
        videoBackdropContent: root.videoBackdropContent
        onAccepted: {
            if (!root.currentAction()) return
            const row = root.currentRow(packageId)
            if (root.busy || !root.permitted(row, operation) || root.backend.revision !== revision || root.signature(row) !== packageState) {
                root.actionError = qsTr("The add-on changed. Open this action again."); return
            }
            root.result(operation, operation === "recover" ? root.backend.requestRecoverPackage(packageId) !== "" : root.backend.uninstall(packageId, operation === "removeData"))
        }
    }
}
