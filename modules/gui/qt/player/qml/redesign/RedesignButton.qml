// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Templates as T
import QtQuick.Controls.Basic as C

T.Button {
    id: root
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    property string iconName
    property string selectedIconName: iconName
    property int tooltipTextFormat: Text.AutoText
    property bool tooltipEnabled: true
    property string caption
    property int captionTextFormat: Text.AutoText
    property bool leftAligned: false
    property bool accent: false
    property bool primaryAction: false
    property bool filled: false
    property real cornerRadius: theme.controlRadius
    property bool selected: false
    property bool highlightSelection: true
    // Media rows use selection on click and a separate activation gesture.
    property bool mediaItem: false
    property bool handleMediaKeys: true
    property bool singleClickActivate: false
    signal activated()
    onClicked: if (mediaItem && singleClickActivate) activated()
    onDoubleClicked: if (mediaItem && !singleClickActivate) activated()
    Keys.onPressed: event => {
        if (mediaItem && handleMediaKeys && [Qt.Key_Return, Qt.Key_Enter, Qt.Key_Space].includes(event.key)) {
            if (!event.isAutoRepeat) activated()
            event.accepted = true
        } else event.accepted = false
    }
    property color foreground: primaryAction && enabled ? theme.accent : theme.foreground
    property color iconForeground: foreground
    property color selectedForeground: theme.accent
    property color selectedIconForeground: theme.accent
    property real iconSize: theme.dp(24)
    property real iconReveal: 0
    property var iconDetailColor
    property real captionFontSize: 14
    property int captionFontWeight: primaryAction ? Font.Medium : Font.Normal
    property real captionSpacing: 6
    property real contentLeftMargin: 12
    property real captionVerticalOffset: 0
    property real captionMaximumWidth: Infinity
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }

    implicitWidth: caption.length > 0 ? Math.max(theme.dp(36), contentItem.implicitWidth + theme.dp(16)) : theme.dp(36)
    implicitHeight: theme.dp(36)
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: text
    Accessible.role: root.checkable ? Accessible.CheckBox : Accessible.Button
    Accessible.onPressAction: root.mediaItem ? root.activated() : root.click()
    Accessible.onToggleAction: if (root.checkable) root.click()
    RedesignToolTip {
        theme: root.theme
        text: root.text
        contentTextFormat: root.tooltipTextFormat
        visible: root.tooltipEnabled && !root.pressed && root.text.length > 0 && (root.hovered || root.visualFocus)
    }

    background: Rectangle {
        radius: theme.dp(root.cornerRadius)
        color: root.primaryAction ? !root.enabled ? theme.hover : root.hovered ? theme.hover : theme.progress
            : root.accent ? theme.accent : (root.down || (root.selected && root.highlightSelection)) ? theme.selected : root.hovered ? theme.hover : root.filled ? theme.background : "transparent"
        border.width: root.visualFocus ? theme.dp(root.primaryAction ? 1 : 2) : 0
        border.color: theme.focusRing
        opacity: root.enabled ? 1 : root.primaryAction ? 0.5 : 0.35
    }
    contentItem: Item {
        implicitWidth: contentRow.implicitWidth
        implicitHeight: contentRow.implicitHeight
        Row {
        id: contentRow
        spacing: theme.dp(root.captionSpacing)
        anchors.verticalCenter: parent.verticalCenter
        // Keep the row's natural width when switching between labeled and icon-only buttons.
        x: root.leftAligned ? theme.dp(root.contentLeftMargin) : (parent.width - width) / 2
        RedesignIcon {
            objectName: "redesignButtonIcon"
            width: root.iconName ? root.iconSize : 0
            height: root.iconSize
            anchors.verticalCenter: parent.verticalCenter
            name: root.selected ? root.selectedIconName : root.iconName
            sidebarReveal: root.iconReveal
            color: root.iconName === "cone" ? theme.accent : root.accent ? theme.onAccent : root.selected ? root.selectedIconForeground : root.hovered || root.visualFocus ? theme.bright : root.iconForeground
            detailColor: root.iconDetailColor === undefined ? color : root.iconDetailColor
            opacity: root.enabled ? 1 : root.primaryAction ? 0.5 : 0.35
        }
        Text {
            visible: root.caption.length > 0
            width: Math.min(implicitWidth, root.captionMaximumWidth)
            elide: Text.ElideRight
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: theme.dp(root.captionVerticalOffset)
            text: root.caption
            textFormat: root.captionTextFormat
            color: root.iconName === "cone" ? theme.accent : root.accent ? theme.onAccent : root.selected ? root.selectedForeground : root.foreground
            font.pixelSize: theme.dp(root.captionFontSize)
            font.weight: root.captionFontWeight
            opacity: root.enabled ? 1 : root.primaryAction ? 0.5 : 0.35
        }
    }
    }
}
