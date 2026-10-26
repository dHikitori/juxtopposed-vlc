// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Controls.Basic as C
import QtQuick.Layouts

C.Menu {
    id: root
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    property Component videoBackdropContent
    property var entries: []
    property string checkedAction: ""
    property Item buttonAnchor: null
    readonly property Item tooltipTrigger: buttonAnchor && ("tooltipEnabled" in buttonAnchor) ? buttonAnchor : null
    // Cancel a pending hint while this button owns an open popup.
    Binding {
        target: root.tooltipTrigger
        property: "tooltipEnabled"
        value: false
        when: root.visible && !!root.tooltipTrigger
        restoreMode: Binding.RestoreBindingOrValue
    }

    readonly property real windowHeight: parent && parent.Window.window ? parent.Window.window.height : implicitHeight + 2 * margins
    readonly property real spaceAbove: buttonAnchor ? Math.max(0, Math.min(windowHeight,
        buttonAnchor.mapToItem(buttonAnchor.Window.window.contentItem, 0, 0).y) - margins) : 0
    readonly property real spaceBelow: buttonAnchor ? Math.max(0, windowHeight - margins
        - buttonAnchor.mapToItem(buttonAnchor.Window.window.contentItem, 0, buttonAnchor.height).y) : 0
    readonly property bool opensBelow: implicitHeight <= spaceBelow || spaceBelow >= spaceAbove
    signal actionRequested(string action)
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    width: theme.dp(theme.compactMenus ? 220 : 265)
    padding: theme.menuPadding
    margins: theme.dp(10)
    height: Math.max(0, Math.min(implicitHeight, buttonAnchor ? opensBelow ? spaceBelow : spaceAbove : windowHeight - 2 * margins))
    closePolicy: C.Popup.CloseOnEscape | (buttonAnchor ? C.Popup.CloseOnPressOutsideParent : C.Popup.CloseOnPressOutside)
    function toggleFrom(anchor) {
        if (visible) { close(); return }
        buttonAnchor = anchor
        parent = anchor
        x = Qt.binding(() => root.buttonAnchor && root.mirrored ? root.buttonAnchor.width - root.width : 0)
        y = Qt.binding(() => root.buttonAnchor ? root.opensBelow ? root.buttonAnchor.height : -root.height : 0)
        open()
    }
    onClosed: buttonAnchor = null
    function focusCheckedChoice() {
        if (!Qt.isQtObject(root) || !opened || !checkedAction.length || currentIndex >= 0) return
        const selected = entries.findIndex(entry => entry.action === checkedAction && entry.enabled !== false)
        if (selected >= 0) {
            contentItem.forceLayout()
            itemAt(selected).forceActiveFocus(Qt.PopupFocusReason)
        }
    }
    // Let the menu's ListView finish its current-index update first.
    onOpened: Qt.callLater(focusCheckedChoice)
    onButtonAnchorChanged: if (!buttonAnchor && visible) close()
    popupType: C.Popup.Item
    background: Rectangle {
        id: surface
        readonly property color fill: root.theme.menuPanel
        color: Qt.rgba(fill.r, fill.g, fill.b, root.theme.floatingOpacity)
        radius: root.theme.dp(10)
        Loader {
            anchors.fill: parent
            z: -1
            active: root.visible
            sourceComponent: root.videoBackdropContent
            onLoaded: {
                if (item.cornerRadius !== undefined)
                    item.cornerRadius = Qt.binding(() => surface.radius)
                if (item.menuShadow !== undefined) item.menuShadow = true
            }
        }
    }
    Instantiator {
        model: root.entries
        delegate: C.MenuItem {
            id: row
            required property var modelData
            objectName: "libraryAction_" + modelData.action
            implicitHeight: root.theme.menuItemHeight
            leftPadding: root.theme.menuItemPadding
            rightPadding: root.theme.menuItemPadding
            text: modelData.label
            enabled: modelData.enabled !== false
            checkable: root.checkedAction.length > 0 || modelData.checked !== undefined
            autoExclusive: root.checkedAction.length > 0
            checked: root.visible && (root.checkedAction.length > 0 ? modelData.action === root.checkedAction : modelData.checked === true)
            indicator: null
            arrow: null
            background: Rectangle { radius: root.theme.dp(6); color: !row.highlighted ? "transparent" : root.lightTheme ? root.theme.hover : "#1f1f1f" }
            contentItem: RowLayout {
                spacing: root.theme.dp(10)
                opacity: row.enabled ? 1 : 0.35
                RedesignIcon {
                    id: actionIcon
                    objectName: "libraryActionIcon_" + row.modelData.action
                    visible: !root.theme.compactMenus && !!row.modelData.icon
                    Layout.preferredWidth: root.theme.dp(24)
                    Layout.preferredHeight: root.theme.dp(24)
                    name: row.modelData.icon || ""
                    color: row.modelData.accent && !actionIcon.hasDetail ? root.theme.menuAccent : root.theme.foreground
                    detailColor: row.modelData.accent ? root.theme.menuAccent : root.theme.foreground
                }
                Text {
                    Layout.fillWidth: true
                    text: row.text; textFormat: Text.PlainText
                    color: root.theme.foreground
                    font.pixelSize: root.theme.dp(14)
                    elide: Text.ElideRight
                }
                RedesignIcon {
                    objectName: "libraryActionCheck_" + row.modelData.action
                    visible: row.checkable
                    opacity: row.checked ? 1 : 0
                    Layout.preferredWidth: root.theme.dp(24)
                    Layout.preferredHeight: root.theme.dp(24)
                    name: "check-active"
                    color: root.theme.accent
                }
            }
            onTriggered: root.actionRequested(modelData.action)
        }
        onObjectAdded: (index, object) => root.insertItem(index, object)
        onObjectRemoved: (index, object) => root.removeItem(object)
    }
}
