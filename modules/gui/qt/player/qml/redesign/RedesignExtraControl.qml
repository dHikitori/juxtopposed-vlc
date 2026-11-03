// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts

Item {
    id: root
    required property string controlId
    required property RedesignTheme theme
    required property var specification
    property var controlState: ({})
    // A panel button shows only while its panel has content.
    property bool shown: true
    visible: shown
    readonly property string kind: specification.kind || "button"
    readonly property string valueText: controlState.value === undefined || controlState.value === null || String(controlState.value).length === 0 ? "—" : String(controlState.value)
    signal triggered(Item anchor)
    objectName: "customControl_" + controlId
    implicitWidth: kind === "divider" ? theme.dp(1) : kind === "text" ? Math.min(theme.dp(220), label.implicitWidth)
        : kind === "spacer" ? theme.dp(24) : kind === "image" ? theme.dp(44) : theme.dp(36)
    implicitHeight: theme.dp(36)
    Layout.fillWidth: kind === "spacer"
    Layout.leftMargin: kind === "divider" ? theme.dp(4) : 0
    Layout.rightMargin: kind === "divider" ? theme.dp(4) : 0
    enabled: controlState.enabled !== false
    Accessible.role: kind === "text" ? Accessible.StaticText : kind === "image" ? Accessible.Graphic : Accessible.NoRole
    Accessible.name: kind === "text" ? specification.label + ": " + valueText : kind === "image" ? specification.label : ""
    RedesignButton {
        objectName: "customButton_" + root.controlId
        anchors.fill: parent
        visible: root.kind === "button"
        scaleFactor: root.theme.scaleFactor; lightTheme: root.theme.lightTheme; accentColor: root.theme.accentColor
        text: root.controlState.description || root.specification.label
        iconName: root.controlState.icon || root.specification.icon
        iconMarkup: root.specification.iconMarkup || ""
        selected: !!root.controlState.checked
        checkable: root.controlState.checked !== undefined
        checked: !!root.controlState.checked
        selectedIconForeground: root.controlId === "record" ? root.theme.danger : root.theme.accent
        onClicked: root.triggered(this)
        Text {
            anchors.right: parent.right; anchors.top: parent.top
            anchors.rightMargin: root.theme.dp(3); anchors.topMargin: root.theme.dp(2)
            visible: !!root.controlState.badge
            text: root.controlState.badge || ""
            color: root.theme.accent; font.pixelSize: root.theme.dp(9)
        }
    }
    Text {
        id: label
        objectName: "customLabel_" + root.controlId
        anchors.fill: parent
        visible: root.kind === "text"
        text: root.valueText
        textFormat: Text.PlainText
        font.pixelSize: root.theme.dp(14)
        color: root.theme.foreground
        elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter
        Accessible.ignored: true
    }
    Image {
        objectName: "customThumbnail"
        anchors.fill: parent; anchors.margins: root.theme.dp(4)
        visible: root.kind === "image"
        source: root.visible && root.kind === "image" ? root.controlState.artwork || "" : ""
        sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio)
        asynchronous: true
        fillMode: Image.PreserveAspectFit
        RedesignIcon {
            anchors.centerIn: parent; width: root.theme.dp(24); height: width
            visible: !parent.source.toString().length || parent.status === Image.Error
            name: "thumbnail"; color: root.theme.foreground
        }
    }
    Rectangle {
        anchors.centerIn: parent
        visible: root.kind === "divider"
        width: root.theme.dp(1); height: root.theme.dp(18)
        color: root.theme.border
    }
}
