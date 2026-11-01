// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

C.Popup {
    id: root
    popupType: C.Popup.Item
    modal: true
    dim: true
    required property var backend
    property var settingsBackend: null
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme }
    signal manageRequested()
    parent: C.Overlay.overlay
    width: Math.min(theme.dp(960), parent.width - theme.dp(24))
    height: Math.min(theme.dp(720), parent.height - theme.dp(24))
    x: (parent.width - width) / 2; y: (parent.height - height) / 2
    padding: theme.dp(16); focus: true
    closePolicy: C.Popup.CloseOnEscape
    function openSource(source) { open(); browser.openSource(source || "") }
    onClosed: browser.closeView()
    background: RedesignDialogSurface { theme: root.theme }
    contentItem: ColumnLayout {
        spacing: root.theme.dp(12)
        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: qsTr("Media sources"); color: root.theme.bright; font.pixelSize: root.theme.dp(18) }
            RedesignButton {
                caption: qsTr("Add-on settings"); text: caption
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: { root.close(); root.manageRequested() }
            }
            RedesignButton {
                caption: qsTr("Back"); text: caption
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onClicked: root.close()
            }
        }
        RedesignAddonDiscovery {
            id: browser
            objectName: "addonDiscoveryWindowBrowser"
            Layout.fillWidth: true; Layout.fillHeight: true
            backend: root.backend
            displayActive: root.visible
            scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
            onConfigureRequested: packageId => {
                if (!root.settingsBackend) return
                settingsDialog.packageId = packageId; settingsDialog.open()
            }
        }
    }
    RedesignAddonSettingsDialog {
        id: settingsDialog
        backend: root.settingsBackend
        scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
    }
}
