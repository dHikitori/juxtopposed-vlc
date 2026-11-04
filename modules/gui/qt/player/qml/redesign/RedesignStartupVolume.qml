// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

Rectangle {
    id: root
    required property var backend
    property real scaleFactor: 1
    property bool lightTheme: false
    property color accentColor: "#d37918"
    readonly property bool remember: { backend.revision; return !!backend.value("volume-save") }
    readonly property int startupVolume: { backend.revision; return Number(backend.value("redesign-startup-volume")) }
    onRememberChanged: if (remember) {
        field.invalid = false
        field.text = startupVolume.toString()
    }
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    implicitHeight: theme.dp(78)
    color: theme.field; radius: theme.dp(6)
    ColumnLayout {
        anchors.fill: parent; anchors.margins: theme.dp(2)
        spacing: theme.dp(2)
        RedesignButton {
            objectName: "rememberStartupVolume"
            Layout.fillWidth: true; Layout.preferredHeight: theme.dp(36)
            scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
            leftAligned: true; caption: qsTr("Remember last volume"); text: caption
            selected: root.remember
            Accessible.role: Accessible.RadioButton
            Accessible.checkable: true; Accessible.checked: selected
            background: Rectangle { color: parent.selected ? theme.progress : root.lightTheme ? theme.background : "#191919"; radius: theme.dp(4); border.color: parent.visualFocus ? theme.accent : theme.field }
            onClicked: root.backend.stage("volume-save", true)
        }
        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: theme.dp(36)
            color: root.remember ? root.lightTheme ? theme.background : "#191919" : theme.progress; radius: theme.dp(4)
            RowLayout {
                anchors.fill: parent
                spacing: 0
                RedesignButton {
                    objectName: "fixedStartupVolume"
                    Layout.preferredWidth: theme.dp(132); Layout.fillHeight: true
                    scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
                    leftAligned: true; caption: qsTr("Always starts at:"); text: caption
                    foreground: root.remember ? theme.playerLabel : theme.foreground
                    selected: !root.remember
                    Accessible.role: Accessible.RadioButton
                    Accessible.checkable: true; Accessible.checked: selected
                    background: Rectangle { color: "transparent"; radius: theme.dp(4); border.color: parent.visualFocus ? theme.accent : "transparent" }
                    onClicked: root.backend.stage("volume-save", false)
                }
                C.TextField {
                    id: field
                    objectName: "startupVolumeField"
                    Layout.fillWidth: true; Layout.fillHeight: true
                    Layout.topMargin: theme.dp(2); Layout.bottomMargin: theme.dp(2); Layout.rightMargin: theme.dp(2)
                    enabled: !root.remember
                    property bool invalid: false
                    text: root.startupVolume.toString()
                    color: invalid ? "#e56b6b" : enabled ? theme.foreground : Qt.rgba(theme.foreground.r, theme.foreground.g, theme.foreground.b, 0.4)
                    font.pixelSize: theme.dp(14)
                    leftPadding: theme.dp(12); rightPadding: theme.dp(30)
                    selectByMouse: true
                    inputMethodHints: Qt.ImhDigitsOnly
                    Accessible.name: qsTr("Startup volume in percent")
                    background: Rectangle { color: theme.field; radius: theme.dp(2); border.width: field.invalid || field.activeFocus ? 1 : 0; border.color: field.invalid ? "#e56b6b" : field.activeFocus ? theme.accent : theme.border }
                    TextMetrics { id: valueMetrics; text: field.text; font: field.font }
                    Text { x: field.leftPadding + valueMetrics.advanceWidth; anchors.verticalCenter: parent.verticalCenter; text: "%"; color: field.color; font.pixelSize: theme.dp(14); visible: !field.activeFocus }
                    onEditingFinished: invalid = !root.backend.stage("redesign-startup-volume", text)
                    onActiveFocusChanged: if (activeFocus) selectAll()
                    RedesignToolTip { theme: root.theme; text: qsTr("Volume on the next Hakone startup (0–300%)."); visible: parent.hovered; delay: 0 }
                }
            }
        }
    }
}
