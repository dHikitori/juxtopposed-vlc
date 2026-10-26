// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

C.Dialog {
    id: root
    popupType: C.Popup.Item
    property var backend: null
    property string packageId: ""
    property string packageName: ""
    property var messages: ({})
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    property Component videoBackdropContent
    readonly property alias form: form
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme }
    signal pickerRequested(string fieldName, string kind)
    signal connectionRequested(string fieldName)
    title: packageName.length ? qsTr("%1 settings").arg(packageName) : qsTr("Add-on settings")
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    width: Math.max(0, Math.min(theme.dp(560), parent ? parent.width - theme.dp(32) : theme.dp(560)))
    height: Math.min(implicitHeight, parent ? Math.max(0, parent.height - theme.dp(32)) : implicitHeight)
    modal: true; dim: true; focus: true
    padding: theme.dp(24)
    closePolicy: C.Popup.CloseOnEscape
    header: null; footer: null
    onOpened: form.reload()
    onClosed: form.discard()
    background: RedesignDialogSurface { theme: root.theme; videoBackdropContent: root.videoBackdropContent }
    C.Overlay.modal: Rectangle { color: "#80000000" }
    contentItem: C.ScrollView {
        id: scroll
        clip: true; contentWidth: availableWidth
        implicitHeight: contents.implicitHeight
        C.ScrollBar.horizontal.policy: C.ScrollBar.AlwaysOff
        ColumnLayout {
            id: contents
            width: scroll.availableWidth; spacing: root.theme.dp(16)
            Accessible.role: Accessible.Dialog; Accessible.name: root.title
            Text {
                Layout.fillWidth: true
                text: root.title; textFormat: Text.PlainText; wrapMode: Text.Wrap
                color: root.theme.bright; font.pixelSize: root.theme.dp(20)
            }
            RedesignAddonSettings {
                id: form
                objectName: "addonSettingsDialogForm"
                Layout.fillWidth: true
                backend: root.backend; packageId: root.packageId
                messages: {
                    if (root.backend && typeof root.backend.settingsMessages === "function") {
                        root.backend.revision
                        return Object.assign({}, root.backend.settingsMessages(root.packageId), root.messages)
                    }
                    return root.messages
                }
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onSaved: root.close()
                onDiscarded: if (root.visible) root.close()
                onPickerRequested: (name, kind) => root.pickerRequested(name, kind)
                onConnectionRequested: name => root.connectionRequested(name)
            }
        }
    }
}
