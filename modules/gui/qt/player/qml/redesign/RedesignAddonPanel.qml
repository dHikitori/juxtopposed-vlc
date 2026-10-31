// SPDX-License-Identifier: GPL-2.0-or-later
// The app's frame for an add-on player panel (player.panels@1): the title,
// the close button, focus, scrolling and the recovery state are the app's.
// The add-on supplies only the view inside it.
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

FocusScope {
    id: root
    required property var registry
    required property string panelId
    property var entry: null
    property var hostArtwork: ({})
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    property bool failed: false
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme }
    signal closeRequested()
    signal manageRequested()
    objectName: "addonPlayerPanel"
    Keys.onEscapePressed: closeRequested()
    Component.onCompleted: closeButton.forceActiveFocus()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: root.theme.dp(16)
        spacing: root.theme.dp(12)
        RowLayout {
            Layout.fillWidth: true
            spacing: root.theme.dp(8)
            Text {
                objectName: "addonPanelTitle"
                Layout.fillWidth: true
                text: root.entry ? root.entry.name : ""
                textFormat: Text.PlainText; elide: Text.ElideRight
                color: root.theme.bright; font.pixelSize: root.theme.dp(18); font.weight: Font.Medium
                Accessible.role: Accessible.Heading
            }
            RedesignButton {
                id: closeButton
                objectName: "addonPanelClose"
                scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                iconName: "close"; text: qsTr("Close panel")
                onClicked: root.closeRequested()
            }
        }
        Flickable {
            id: scroller
            Layout.fillWidth: true; Layout.fillHeight: true
            visible: !root.failed
            clip: true
            contentWidth: width; contentHeight: mount.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            C.ScrollBar.vertical: C.ScrollBar { }
            RedesignAddonMount {
                id: mount
                objectName: "addonPanelMount"
                width: scroller.width; height: implicitHeight
                registry: root.registry; contributionId: root.panelId
                hostContext: ({surface: "player.panels@1"})
                hostArtwork: root.hostArtwork
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onSessionIdChanged: if (sessionId.length && root.registry && typeof root.registry.attachPanelSession === "function")
                    root.registry.attachPanelSession(sessionId)
                onFailed: root.failed = true
            }
        }
        ColumnLayout {
            objectName: "addonPanelRecovery"
            Layout.fillWidth: true; Layout.fillHeight: true
            visible: root.failed
            spacing: root.theme.dp(10)
            Text {
                Layout.fillWidth: true
                text: qsTr("This panel stopped working. Reload it, or turn off its add-on in Extensions.")
                textFormat: Text.PlainText; wrapMode: Text.Wrap
                color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
                Accessible.role: Accessible.AlertMessage
            }
            RowLayout {
                spacing: root.theme.dp(8)
                RedesignButton {
                    objectName: "addonPanelReload"
                    caption: qsTr("Reload"); text: caption; primaryAction: true
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    onClicked: { root.failed = false; mount.schedule(true) }
                }
                RedesignButton {
                    objectName: "addonPanelManage"
                    caption: qsTr("Extensions"); text: qsTr("Open Extensions")
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    onClicked: root.manageRequested()
                }
            }
            Item { Layout.fillHeight: true }
        }
    }
}
