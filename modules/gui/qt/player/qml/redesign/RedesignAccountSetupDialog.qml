// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

// The app's account sign-in. It shows the code and the sign-in site that the
// add-on's declared service names, and nothing from the add-on itself. The
// sign-in stops when the dialog closes.
C.Dialog {
    id: root
    popupType: C.Popup.Item
    // An AddonAccountSetup, or null when add-ons are not available.
    property var setup: null
    property string accountLabel: ""
    property bool credentialPersistenceAvailable: true
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    property Component videoBackdropContent
    property int remaining: 0
    readonly property string flowState: setup ? setup.state : "failed"
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme }
    signal connected()
    title: accountLabel.length ? qsTr("Connect %1").arg(accountLabel) : qsTr("Connect account")
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    width: Math.max(0, Math.min(theme.dp(480), parent ? parent.width - theme.dp(32) : theme.dp(480)))
    modal: true; dim: true; focus: true
    padding: theme.dp(24)
    closePolicy: C.Popup.CloseOnEscape
    header: null; footer: null
    onClosed: if (setup) setup.cancel()
    background: RedesignDialogSurface { theme: root.theme; videoBackdropContent: root.videoBackdropContent }
    C.Overlay.modal: Rectangle { color: "#80000000" }
    Connections {
        target: root.setup
        ignoreUnknownSignals: true
        function onConnected() { root.connected(); root.close() }
        function onChanged() { root.remaining = root.setup.remainingSeconds }
    }
    // Only while a code is shown: the remaining time.
    Timer {
        interval: 1000; repeat: true; running: root.visible && root.flowState === "waiting"
        onTriggered: root.remaining = root.setup ? root.setup.remainingSeconds : 0
    }
    contentItem: ColumnLayout {
        spacing: root.theme.dp(14)
        Accessible.role: Accessible.Dialog; Accessible.name: root.title
        Text {
            Layout.fillWidth: true
            text: root.title; textFormat: Text.PlainText; wrapMode: Text.Wrap
            color: root.theme.bright; font.pixelSize: root.theme.dp(20)
        }
        Text {
            objectName: "accountSetupStatus"
            Layout.fillWidth: true; textFormat: Text.PlainText; wrapMode: Text.Wrap
            color: root.flowState === "failed" ? root.theme.danger : root.theme.foreground
            font.pixelSize: root.theme.dp(14)
            Accessible.role: root.flowState === "failed" ? Accessible.AlertMessage : Accessible.StaticText
            text: root.flowState === "requesting" ? qsTr("The app asks the sign-in service for a code.")
                : root.flowState === "waiting" ? qsTr("Open %1 in your browser and enter this code. The app connects when you finish.").arg(root.setup.verificationHost)
                : root.flowState === "connected" ? qsTr("The account is connected.")
                : root.flowState === "canceled" ? qsTr("The sign-in stopped.")
                : root.flowState === "failed" ? (root.setup && root.setup.message.length ? root.setup.message : qsTr("The sign-in did not complete."))
                : qsTr("Select Start to get a sign-in code.")
        }
        Text {
            objectName: "accountSetupCode"
            Layout.alignment: Qt.AlignHCenter
            visible: root.flowState === "waiting"
            text: root.setup ? root.setup.userCode : ""; textFormat: Text.PlainText
            color: root.theme.bright; font.pixelSize: root.theme.dp(28); font.letterSpacing: root.theme.dp(2)
            Accessible.name: qsTr("Sign-in code %1").arg(text)
        }
        Text {
            Layout.fillWidth: true; visible: root.flowState === "waiting" && root.remaining > 0
            text: qsTr("The code expires in %1 min.").arg(Math.max(1, Math.ceil(root.remaining / 60)))
            textFormat: Text.PlainText; wrapMode: Text.Wrap
            color: root.theme.foreground; font.pixelSize: root.theme.dp(12)
        }
        RedesignSwitchRow {
            objectName: "accountSetupSessionOnly"
            Layout.fillWidth: true
            visible: root.setup && ["idle", "failed", "canceled"].includes(root.flowState)
            label: root.credentialPersistenceAvailable ? qsTr("Keep the account only for this session")
                : qsTr("Keep the account only for this session. The secure credential store is not available.")
            checked: !root.credentialPersistenceAvailable || (root.setup && root.setup.sessionOnly)
            enabled: root.credentialPersistenceAvailable
            scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
            onToggled: checked => { if (root.setup) root.setup.sessionOnly = checked }
        }
        Flow {
            Layout.fillWidth: true; spacing: root.theme.dp(8)
            RedesignButton {
                objectName: "accountSetupOpen"
                visible: root.flowState === "waiting"
                caption: qsTr("Open the sign-in page"); text: caption; primaryAction: true
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.setup.openPage()
            }
            RedesignButton {
                objectName: "accountSetupCopy"
                visible: root.flowState === "waiting"
                caption: qsTr("Copy the code"); text: caption
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.setup.copyCode()
            }
            RedesignButton {
                objectName: "accountSetupStart"
                visible: root.setup && ["idle", "failed", "canceled"].includes(root.flowState)
                caption: root.flowState === "idle" ? qsTr("Start") : qsTr("Try again"); text: caption; primaryAction: true
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: {
                    if (!root.credentialPersistenceAvailable) root.setup.sessionOnly = true
                    root.setup.start()
                }
            }
            RedesignButton {
                objectName: "accountSetupCancel"
                caption: root.flowState === "connected" ? qsTr("Close") : qsTr("Cancel"); text: caption
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.close()
            }
        }
    }
}
