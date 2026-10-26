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
    property string actionError: ""
    readonly property bool supported: !!backend && typeof backend.setSafeMode === "function"
    readonly property bool busy: !!backend && backend.busy === true
    readonly property bool safeMode: { if (!backend) return false; backend.revision; return backend.safeMode === true }
    readonly property var recoverable: {
        if (!backend) return []
        backend.revision
        return (backend.packages || []).filter(row => row.recoveryRequired === true || (row.suppressed === true && row.distributionSource === true))
    }
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme }
    visible: supported
    spacing: theme.dp(8)
    function request(id, operation) {
        if (busy || !recoverable.some(row => row.id === id)) return false
        const row = recoverable.find(row => row.id === id)
        if (operation === "restore" ? row.suppressed !== true || row.distributionSource !== true || typeof backend.restoreDistributionPackage !== "function"
                                    : operation !== "recover" || row.recoveryRequired !== true || typeof backend.recoverPackage !== "function") return false
        actionError = ""; confirmation.packageId = id; confirmation.operation = operation; confirmation.open(); return true
    }
    RedesignButton {
        objectName: "addonSafeMode"
        caption: root.safeMode ? qsTr("Use add-ons") : qsTr("Temporarily disable add-ons")
        text: caption; tooltipTextFormat: Text.PlainText
        enabled: !root.busy
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
        onClicked: if (!root.busy) {
            const success = root.backend.setSafeMode(!root.safeMode)
            root.actionError = success ? "" : root.backend.errorMessage || qsTr("VLC cannot change the add-on state.")
        }
    }
    Text {
        Layout.fillWidth: true
        text: root.safeMode ? qsTr("Add-ons are temporarily disabled. Installed packages and their settings stay in the app.")
            : qsTr("Use this control if an add-on causes a problem.")
        textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
    }
    Repeater {
        model: root.recoverable
        ColumnLayout {
            id: row
            required property var modelData
            readonly property var retained: {
                root.backend.revision
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
                text: row.modelData.recoveryRequired === true ? qsTr("This add-on needs recovery.") : qsTr("This app package was removed.")
                textFormat: Text.PlainText; wrapMode: Text.Wrap
                color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
            }
            Text {
                Layout.fillWidth: true
                visible: !!row.modelData.error
                text: row.modelData.error || ""; textFormat: Text.PlainText; wrapMode: Text.Wrap
                color: root.theme.danger; font.pixelSize: root.theme.dp(13)
            }
            Text {
                Layout.fillWidth: true
                visible: row.modelData.recoveryRequired === true && row.retained.length > 0
                text: qsTr("Saved versions: %1").arg(row.retained.map(entry => entry.version).join(", "))
                textFormat: Text.PlainText; wrapMode: Text.Wrap
                color: root.theme.foreground; font.pixelSize: root.theme.dp(13)
            }
            RedesignButton {
                objectName: "addonRecover_" + row.modelData.id
                visible: row.modelData.recoveryRequired === true
                enabled: !root.busy && typeof root.backend.recoverPackage === "function"
                caption: qsTr("Recover add-on"); text: caption; tooltipTextFormat: Text.PlainText
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.request(row.modelData.id, "recover")
            }
            RedesignButton {
                objectName: "addonRestore_" + row.modelData.id
                visible: row.modelData.suppressed === true && row.modelData.distributionSource === true
                enabled: !root.busy && typeof root.backend.restoreDistributionPackage === "function"
                caption: qsTr("Restore app package"); text: caption; tooltipTextFormat: Text.PlainText
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.request(row.modelData.id, "restore")
            }
        }
    }
    Text {
        objectName: "addonRecoveryError"
        Layout.fillWidth: true
        text: root.actionError; textFormat: Text.PlainText; wrapMode: Text.Wrap
        visible: text.length > 0
        color: root.theme.danger; font.pixelSize: root.theme.dp(13)
        Accessible.role: Accessible.AlertMessage
    }
    RedesignConfirmationDialog {
        id: confirmation
        objectName: "addonRecoveryConfirmation"
        parent: root.Window.contentItem
        property string packageId: ""
        property string operation: ""
        title: operation === "restore" ? qsTr("Restore app package?") : qsTr("Recover add-on?")
        message: operation === "restore" ? qsTr("VLC will restore and enable the package from the app installation.")
            : qsTr("VLC will select a saved package that passed validation. The add-on will stay disabled. Its settings will match that package.")
        confirmText: operation === "restore" ? qsTr("Restore app package") : qsTr("Recover add-on")
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
        videoBackdropContent: root.videoBackdropContent
        onAccepted: {
            if (root.busy || !root.recoverable.some(row => row.id === packageId
                && (operation === "restore" ? row.suppressed === true && row.distributionSource === true : row.recoveryRequired === true))) return
            const success = operation === "restore" ? root.backend.restoreDistributionPackage(packageId) : root.backend.recoverPackage(packageId)
            root.actionError = success ? "" : root.backend.errorMessage || qsTr("VLC cannot recover this add-on.")
        }
    }
}
