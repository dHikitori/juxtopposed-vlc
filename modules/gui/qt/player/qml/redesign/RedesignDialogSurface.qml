// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

Rectangle {
    id: root
    property RedesignTheme theme: RedesignTheme { }
    property Component videoBackdropContent
    property color fill: theme.customized ? theme.panel : theme.lightTheme ? theme.panel : theme.menuPanel
    color: Qt.rgba(fill.r, fill.g, fill.b, theme.floatingOpacity)
    radius: theme.dp(theme.panelRadius)
    Loader {
        objectName: "dialogBackdropLoader"
        anchors.fill: parent
        z: -1
        active: root.visible
        sourceComponent: root.videoBackdropContent
        onLoaded: {
            if (item.cornerRadius !== undefined) item.cornerRadius = Qt.binding(() => root.radius)
            if (item.blurRadius !== undefined) item.blurRadius = Qt.binding(() => root.theme.dp(25))
            if (item.menuShadow !== undefined) item.menuShadow = true
        }
    }
}
