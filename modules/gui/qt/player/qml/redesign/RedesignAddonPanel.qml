// SPDX-License-Identifier: GPL-2.0-or-later
// The app's frame for an add-on player panel (player.panels@1): the title,
// the close button, focus, scrolling and the recovery state are the app's.
// The add-on supplies only the view inside it. A panel that names a header
// row draws its own title there; the frame then adds only the close button
// at the end of that row, and the view runs edge to edge.
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
    readonly property string headerRow: root.entry && typeof root.entry.header === "string" ? root.entry.header : ""
    // The frame shows the highlighted row (the playing episode, say) once
    // when the panel opens, like the app's own lists.
    property bool positioned: false
    function selectedRow(item) {
        if (!item) return null
        if (String(item.objectName).startsWith("addonMediaRow_") && item.selected === true && item.visible) return item
        for (let i = 0; i < item.children.length; ++i) { const found = selectedRow(item.children[i]); if (found) return found }
        return null
    }
    function positionSelected() {
        if (positioned || !mount.usable || scroller.height <= 0) return
        const row = selectedRow(mount)
        if (!row) return
        positioned = true
        const top = row.mapToItem(mount, 0, 0).y
        scroller.contentY = Math.max(0, Math.min(top, scroller.contentHeight - scroller.height))
    }
    signal closeRequested()
    signal manageRequested()
    objectName: "addonPlayerPanel"
    Keys.onEscapePressed: closeRequested()
    Component.onCompleted: if (!headerRow.length) closeButton.forceActiveFocus()
    Component {
        id: headerClose
        RedesignButton {
            objectName: "addonPanelClose"
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            iconName: "close"; text: qsTr("Close panel")
            onClicked: root.closeRequested()
            Component.onCompleted: forceActiveFocus()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        // A failed view has no header row: the frame's own title row returns.
        readonly property bool own: !root.headerRow.length || root.failed
        anchors.margins: own ? root.theme.dp(16) : 0
        spacing: own ? root.theme.dp(12) : 0
        RowLayout {
            Layout.fillWidth: true
            visible: parent.own
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
        // The view's header row, outside the scrolled area.
        Item {
            id: headerHost
            objectName: "addonPanelHeaderHost"
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            visible: !parent.own && !root.failed
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
                bleed: root.headerRow.length > 0; headerRow: root.headerRow; headerTrailing: root.headerRow.length ? headerClose : null
                headerHost: root.headerRow.length ? headerHost : null
                scaleFactor: root.scaleFactor; accentColor: root.accentColor; lightTheme: root.lightTheme
                onSessionIdChanged: if (sessionId.length && root.registry && typeof root.registry.attachPanelSession === "function")
                    root.registry.attachPanelSession(sessionId)
                onFailed: root.failed = true
                onImplicitHeightChanged: Qt.callLater(root.positionSelected)
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
