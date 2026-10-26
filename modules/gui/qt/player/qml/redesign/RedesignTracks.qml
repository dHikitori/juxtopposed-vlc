// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import QtQuick.Controls.Basic as C

ColumnLayout {
    id: root
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    property var trackModel
    property string heading
    property bool allowDisable: false
    property int trackGroup: -1 // All tracks, embedded tracks, or external tracks.
    property bool showEmptyMessage: true
    property bool editableTracks: false
    property bool canEditTracks: true
    property bool refreshable: false
    property bool menuMode: false
    readonly property bool compact: menuMode && theme.compactMenus
    readonly property int count: {
        let total = 0
        for (let i = 0; i < choices.count; ++i) {
            const row = choices.itemAt(i)
            if (row && row.accepted) ++total
        }
        return total
    }
    readonly property bool hasSelection: {
        for (let i = 0; i < choices.count; ++i) {
            const row = choices.itemAt(i)
            if (row && row.checked) return true
        }
        return false
    }
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    signal selectionRequested(int index, bool selected)
    signal disableRequested()
    signal editRequested()
    signal addRequested()
    signal refreshRequested()
    spacing: 0

    component TrackChoice: T.Button {
        id: choice
        property bool chosen: false
        property string description: text
        Layout.fillWidth: true
        implicitHeight: root.theme.dp(root.compact ? 28 : 44)
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.name: text
        Accessible.role: Accessible.RadioButton
        Accessible.checkable: true
        Accessible.checked: chosen
        background: Rectangle {
            radius: root.theme.dp(4)
            color: choice.down || choice.hovered ? root.theme.hover : "transparent"
            border.width: choice.visualFocus ? 1 : 0
            border.color: root.theme.accent
        }
        contentItem: Item {
            RedesignIcon {
                objectName: "selectionMark"
                name: "check-active"
                visible: choice.chosen
                width: root.theme.dp(root.compact ? 20 : 24); height: width
                anchors.left: parent.left
                anchors.leftMargin: root.theme.dp(root.compact ? 8 : 24)
                anchors.verticalCenter: parent.verticalCenter
                color: root.theme.accent
            }
            Text {
                anchors.left: parent.left; anchors.leftMargin: root.theme.dp(root.compact ? 36 : 62)
                anchors.right: parent.right; anchors.rightMargin: root.theme.dp(20)
                anchors.verticalCenter: parent.verticalCenter
                text: choice.text
                textFormat: Text.PlainText
                color: root.theme.foreground
                font.pixelSize: root.theme.dp(14)
                elide: Text.ElideRight
            }
        }
        RedesignToolTip { theme: root.theme; text: parent.description; contentTextFormat: Text.PlainText; visible: (parent.hovered || parent.visualFocus) && parent.description.length > 0 }
    }
    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: theme.dp(root.compact ? 12 : 28); Layout.rightMargin: theme.dp(root.compact ? 12 : 26)
        Layout.topMargin: theme.dp(root.compact ? 10 : 22); Layout.bottomMargin: theme.dp(root.compact ? 6 : root.allowDisable ? 18 : 14)
        spacing: theme.dp(root.compact ? 12 : 16)
        Text {
            text: root.heading; color: root.theme.playerLabel
            font.pixelSize: root.theme.dp(14)
            Layout.fillWidth: true
            elide: Text.ElideRight
        }
        RedesignButton {
            objectName: "editExternalTracks"
            visible: root.editableTracks
            enabled: root.canEditTracks && root.count > 0
            Layout.preferredWidth: root.theme.dp(root.compact ? 54 : 28); Layout.preferredHeight: root.theme.dp(24)
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            caption: root.compact ? qsTr("Edit") : ""
            iconName: root.compact ? "" : "edit"; iconSize: root.theme.dp(24); iconForeground: theme.playerLabel; text: qsTr("Edit external tracks")
            onClicked: root.editRequested()
        }
        RedesignButton {
            objectName: "addExternalTracks"
            visible: root.editableTracks
            enabled: root.canEditTracks
            Layout.preferredWidth: root.theme.dp(root.compact ? 54 : 28); Layout.preferredHeight: root.theme.dp(24)
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            caption: root.compact ? qsTr("Add") : ""
            iconName: root.compact ? "" : "add"; iconSize: root.theme.dp(24); iconForeground: theme.playerLabel; text: qsTr("Add external tracks")
            onClicked: root.addRequested()
        }
        RedesignButton {
            objectName: "refreshTrackDevices"
            visible: root.refreshable
            Layout.preferredWidth: root.theme.dp(root.compact ? 54 : 28); Layout.preferredHeight: root.theme.dp(24)
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            caption: root.compact ? qsTr("Refresh") : ""
            iconName: root.compact ? "" : "refresh-two-tone"; iconSize: root.theme.dp(24); iconForeground: theme.playerLabel; text: qsTr("Refresh output devices")
            onClicked: root.refreshRequested()
        }
    }
    TrackChoice {
        objectName: "disableTracks"
        visible: root.allowDisable
        text: qsTr("None")
        chosen: !root.hasSelection
        onClicked: root.disableRequested()
    }
    Repeater {
        id: choices
        model: root.trackModel
        Item {
            id: track
            required property int index
            required property string display
            required property bool checked
            required property var model
            readonly property bool external: typeof model.external === "boolean" && model.external
            readonly property bool accepted: root.trackGroup < 0 || external === (root.trackGroup === 1)
            readonly property string sourceName: typeof model.sourceName === "string" ? model.sourceName : ""
            readonly property string sourceUri: model.sourceUri === undefined ? "" : String(model.sourceUri)
            readonly property string label: typeof model.displayLabel === "string" && model.displayLabel.length ? model.displayLabel : display
            visible: accepted
            Layout.fillWidth: true
            implicitHeight: accepted ? root.theme.dp(root.compact ? 28 : 44) : 0
            TrackChoice {
                anchors.fill: parent
                objectName: "trackChoice_" + track.index
                text: track.external && track.sourceName.length ? track.sourceName : track.label
                description: track.external && track.sourceUri.length ? text + "\n" + track.sourceUri : track.display
                chosen: track.checked
                onClicked: root.selectionRequested(track.index, true)
            }
        }
    }
    Text {
        visible: root.count === 0 && !root.allowDisable && root.showEmptyMessage
        text: qsTr("No tracks available"); color: theme.foreground
        font.pixelSize: theme.dp(13)
        Layout.leftMargin: theme.dp(28); Layout.bottomMargin: theme.dp(16)
    }
}
