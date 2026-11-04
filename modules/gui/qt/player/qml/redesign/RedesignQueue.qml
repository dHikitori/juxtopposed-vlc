// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic as C

FocusScope {
    id: root
    objectName: "redesignQueue"
    property real scaleFactor: 1
    property color accentColor: "#d37918"
    property bool lightTheme: false
    property var addonRegistry: typeof redesignAddonUi === "undefined" ? null : redesignAddonUi
    readonly property var addonActions: {
        if (!addonRegistry) return []
        addonRegistry.revision
        return addonRegistry.commands.filter(entry => entry.document.hook === "queue.item.actions@1").slice(0, 8)
    }
    property Component videoBackdropContent
    property bool singleClickActivate: false
    property var hierarchyModel: null
    property string selectedGroupKey: ""
    property var queueModel
    property int itemCount: 0
    property int currentIndex: -1
    property int selectedIndex: -1
    property string totalDurationText: ""
    property var sortChoices: []
    property int sortKey: -1
    property bool sortDescending: false
    property bool random: false
    property bool repeating: false
    readonly property int actionIndex: selectedGroupKey.length ? -1 : selectedIndex >= 0 && selectedIndex < itemCount ? selectedIndex : currentIndex
    readonly property RedesignTheme theme: RedesignTheme { scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor }
    signal closeRequested()
    signal addRequested()
    signal saveRequested()
    signal clearRequested()
    signal shuffleRequested()
    signal repeatRequested()
    signal sortRequested(int key, bool descending)
    signal playRequested(int index)
    signal removeRequested(int index)
    // The destination is the insertion point before removal, as in moveItemsPre.
    signal moveRequested(int from, int to)
    signal libraryDropRequested(var source)
    signal filesDropped(var urls)
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Queue")

    function removeSelection() {
        if (actionIndex >= 0 && actionIndex < itemCount) removeRequested(actionIndex)
    }
    function moveToRow(from, to) {
        if (from < 0 || from >= itemCount || to < 0 || to >= itemCount || from === to) return
        moveToInsertion(from, to > from ? to + 1 : to)
    }
    function moveToInsertion(from, before) {
        if (from < 0 || from >= itemCount || before < 0 || before > itemCount) return
        const to = before > from ? before - 1 : before
        if (to === from) return
        moveRequested(from, before)
        selectedGroupKey = ""
        selectedIndex = to
    }
    function openActions(anchor, point) {
        if (hierarchyModel && selectedGroupKey.length) {
            const groupRow = hierarchyModel.rowForGroupKey(selectedGroupKey)
            if (groupRow < 0) return
            if (anchor && point) { groupMenu.popup(anchor, point); return }
            list.positionViewAtIndex(groupRow, ListView.Contain); list.forceLayout()
            const group = list.itemAtIndex(groupRow)
            groupMenu.popup(list, group ? group.mapToItem(list, theme.dp(24), group.height) : Qt.point(0, 0))
            return
        }
        if (anchor && point) { actionsMenu.popup(anchor, point); return }
        if (actionIndex >= 0 && actionIndex < itemCount) {
            const visibleIndex = hierarchyModel ? hierarchyModel.rowForSourceIndex(actionIndex, true) : actionIndex
            list.positionViewAtIndex(visibleIndex, ListView.Contain)
            list.forceLayout()
            const row = list.itemAtIndex(visibleIndex)
            if (row) { actionsMenu.popup(list, row.mapToItem(list, theme.dp(24), row.height)); return }
        }
        actionsMenu.popup(list, Qt.point(0, 0))
    }
    function runAction(action) {
        if (action.startsWith("addon/")) { if (addonRegistry) addonRegistry.triggerCommand(action.slice(6), {}); return }
        if (action === "remove") removeSelection()
        else if (action === "earlier") moveToRow(actionIndex, actionIndex - 1)
        else if (action === "later") moveToRow(actionIndex, actionIndex + 1)
        else if (action === "save" && itemCount > 0) saveRequested()
        else if (action === "clear" && itemCount > 0) clearRequested()
        else if (action === "close") closeRequested()
    }
    function durationText(value) {
        return value && value.valid && value.valid() ? value.formatHMS(0) : ""
    }
    function summaryDuration(value) {
        if (!value || !value.valid || !value.valid()) return ""
        const seconds = value.toSeconds()
        const minutes = Math.floor(seconds / 60)
        if (minutes < 1) return qsTr("%1s").arg(seconds)
        if (minutes < 60) return qsTr("%1m").arg(minutes)
        const hours = Math.floor(minutes / 60)
        return minutes % 60 ? qsTr("%1h %2m").arg(hours).arg(minutes % 60) : qsTr("%1h").arg(hours)
    }
    onItemCountChanged: if (selectedIndex >= itemCount) selectedIndex = itemCount - 1
    Keys.onDeletePressed: removeSelection()
    Keys.onPressed: event => {
        if (hierarchyModel && selectedGroupKey.length && (event.key === Qt.Key_Left || event.key === Qt.Key_Right)) {
            hierarchyModel.setExpanded(hierarchyModel.rowForGroupKey(selectedGroupKey), event.key === Qt.Key_Right)
            event.accepted = true
        } else if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && event.modifiers & Qt.ShiftModifier)) {
            openActions(); event.accepted = true
        } else if (event.modifiers & Qt.AltModifier && (event.key === Qt.Key_Up || event.key === Qt.Key_Down)) {
            moveToRow(actionIndex, actionIndex + (event.key === Qt.Key_Up ? -1 : 1)); event.accepted = true
        }
    }
    component QueueButton: RedesignButton {
        Keys.forwardTo: [root]
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
    }
    component QueueMediaButton: RedesignMediaButton {
        Keys.forwardTo: [root]
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
    }
    component QueueToolButton: QueueButton {
        foreground: theme.playerLabel
        selectedIconForeground: theme.playerLabel
        iconDetailColor: selected ? theme.menuAccent : theme.playerLabel
        topPadding: theme.dp(2)
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: theme.dp(28); anchors.rightMargin: theme.dp(24)
        anchors.topMargin: theme.dp(16); anchors.bottomMargin: theme.dp(12)
        spacing: theme.dp(12)
        RowLayout {
            Layout.fillWidth: true
            Layout.rightMargin: -theme.dp(10)
            spacing: 0
            Text {
                objectName: "queueSummary"
                Layout.fillWidth: true
                text: qsTr("%n item", "%n items", root.itemCount) + (root.totalDurationText.length > 0 ? " - " + root.totalDurationText : "")
                elide: Text.ElideRight; color: theme.playerLabel; font.pixelSize: theme.dp(14)
            }
            QueueToolButton { objectName: "queueSortButton"; Layout.rightMargin: theme.dp(4); iconName: "sort"; text: qsTr("Sort queue"); enabled: root.itemCount > 1 && root.sortChoices.length > 0; onClicked: sortMenu.toggleFrom(this) }
            QueueToolButton { objectName: "queueRepeatButton"; Layout.rightMargin: theme.dp(4); iconName: "queue-repeat"; selectedIconName: "queue-repeat-active"; text: qsTr("Repeat"); selected: root.repeating; onClicked: root.repeatRequested() }
            QueueToolButton { objectName: "queueShuffleButton"; Layout.rightMargin: theme.dp(4); iconName: "queue-shuffle"; selectedIconName: "queue-shuffle-active"; text: qsTr("Shuffle"); selected: root.random; onClicked: root.shuffleRequested() }
            QueueToolButton { objectName: "queueRemoveButton"; iconName: "remove"; text: qsTr("Remove from queue"); enabled: root.actionIndex >= 0 && root.actionIndex < root.itemCount; onClicked: root.removeSelection() }
            QueueToolButton { objectName: "queueAddButton"; iconName: "add"; text: qsTr("Add files to queue"); onClicked: root.addRequested() }
        }
        ListView {
            id: list
            objectName: "queueList"
            Keys.forwardTo: [root]
            Layout.leftMargin: -theme.dp(4)
            Layout.fillWidth: true; Layout.fillHeight: true
            model: root.hierarchyModel || root.queueModel
            clip: true
            C.ScrollBar.vertical: C.ScrollBar {}
            delegate: RowLayout {
                id: row
                required property int index
                required property string title
                required property bool isCurrent
                required property var model
                readonly property int queueIndex: root.hierarchyModel ? Number(model.queueIndex) : index
                readonly property bool group: root.hierarchyModel ? Boolean(model.treeGroup) : false
                readonly property int depth: root.hierarchyModel ? Number(model.treeDepth) : 0
                readonly property string groupKey: root.hierarchyModel ? String(model.treeKey) : ""
                readonly property bool containsCurrent: root.hierarchyModel ? Boolean(model.containsCurrent) : false
                width: list.width; height: theme.dp(44)
                spacing: theme.dp(14)
                Item { visible: row.depth > 0; Layout.preferredWidth: theme.dp(Math.min(row.depth * 12, 96)) }
                QueueButton {
                    objectName: "queueGroupToggle_" + row.index
                    visible: row.group
                    Layout.preferredWidth: theme.dp(24)
                    iconName: row.model.treeExpanded ? "chevron-down" : (LayoutMirroring.enabled ? "chevron-left" : "chevron-right")
                    text: row.model.treeExpanded ? qsTr("Collapse") : qsTr("Expand")
                    foreground: row.containsCurrent ? theme.accent : theme.foreground
                    onClicked: {
                        root.selectedGroupKey = row.groupKey; root.selectedIndex = -1
                        root.forceActiveFocus(); root.hierarchyModel.toggleExpanded(row.index)
                    }
                }
                QueueButton {
                    objectName: "queueMoveHandle_" + row.queueIndex
                    visible: row.queueIndex >= 0
                    Layout.preferredWidth: theme.dp(24)
                    iconName: row.isCurrent ? "play" : "drag"
                    foreground: row.isCurrent ? theme.accent : theme.foreground
                    text: qsTr("Move queue item")
                    onClicked: { root.selectedGroupKey = ""; root.selectedIndex = row.queueIndex; forceActiveFocus(Qt.MouseFocusReason) }
                    DragHandler {
                        id: mover
                        target: null
                        property int originIndex: -1
                        property int originViewIndex: -1
                        property int destinationInsertion: -1
                        property int destinationViewBoundary: -1
                        onActiveChanged: {
                            if (active) { originIndex = row.queueIndex; originViewIndex = row.index; destinationInsertion = originIndex; destinationViewBoundary = row.index; root.selectedGroupKey = ""; root.selectedIndex = originIndex }
                            else if (originIndex >= 0) { if (root.visible) root.moveToInsertion(originIndex, destinationInsertion); originIndex = -1 }
                        }
                        onActiveTranslationChanged: if (active) {
                            const target = Math.max(0, Math.min(list.count - 1, originViewIndex + Math.round(activeTranslation.y / row.height)))
                            const after = target > originViewIndex
                            destinationInsertion = root.hierarchyModel ? root.hierarchyModel.insertionIndexAt(target, after) : target + (after ? 1 : 0)
                            destinationViewBoundary = root.hierarchyModel ? root.hierarchyModel.rowForInsertion(destinationInsertion) : target + (after ? 1 : 0)
                        }
                        onCanceled: originIndex = -1
                    }
                }
                QueueMediaButton {
                    objectName: row.queueIndex >= 0 ? "queueItem_" + row.queueIndex : "queueGroup_" + row.index
                    Layout.fillWidth: true
                    text: row.title
                    tooltipTextFormat: Text.PlainText
                    selected: row.queueIndex >= 0 ? !root.selectedGroupKey.length && root.selectedIndex === row.queueIndex : root.selectedGroupKey === row.groupKey
                    selectedForeground: row.isCurrent ? theme.accent : theme.foreground
                    contentItem: RowLayout {
                        spacing: theme.dp(12)
                        RedesignIcon { visible: row.group; Layout.preferredWidth: theme.dp(18); Layout.preferredHeight: theme.dp(18); name: row.model.treePlaylist ? "playlist" : "folder"; color: row.containsCurrent ? theme.accent : theme.foreground }
                        Text { Layout.fillWidth: true; text: row.title; textFormat: Text.PlainText; elide: Text.ElideRight; font.pixelSize: theme.dp(14); color: row.isCurrent ? theme.accent : theme.foreground }
                        Text { objectName: "queueDuration_" + row.queueIndex; text: root.durationText(row.model.duration); font.pixelSize: theme.dp(14); color: theme.playerLabel }
                    }
                    singleClickActivate: root.singleClickActivate
                    onActivated: {
                        if (row.queueIndex < 0) root.hierarchyModel.toggleExpanded(row.index)
                        else root.playRequested(row.queueIndex)
                    }
                    onClicked: {
                        if (row.queueIndex < 0) {
                            root.selectedGroupKey = row.groupKey; root.selectedIndex = -1
                            forceActiveFocus(Qt.MouseFocusReason)
                        } else {
                            root.selectedGroupKey = ""; root.selectedIndex = row.queueIndex; forceActiveFocus(Qt.MouseFocusReason)
                        }
                    }
                }
                Rectangle {
                    parent: list.contentItem
                    x: 0; y: mover.destinationViewBoundary * row.height
                    width: list.width; height: theme.dp(2); color: theme.accent
                    visible: mover.active
                }
            }
            Text {
                anchors.fill: parent; anchors.topMargin: theme.dp(12)
                visible: root.itemCount === 0
                text: qsTr("Drop files here, or add media from your library.")
                wrapMode: Text.WordWrap; color: theme.foreground; font.pixelSize: theme.dp(14)
            }
        }
    }
    // Item views can parent pointer handlers to their scrolling contentItem.
    // Map through scene coordinates to get a position in the viewport.
    TapHandler {
        parent: list
        acceptedButtons: Qt.RightButton
        onTapped: point => {
            const position = list.mapFromItem(null, point.scenePosition)
            const index = list.indexAt(position.x + list.contentX, position.y + list.contentY)
            if (index >= 0) {
                const row = list.itemAtIndex(index)
                root.selectedGroupKey = row && row.queueIndex < 0 ? row.groupKey : ""
                root.selectedIndex = root.hierarchyModel ? root.hierarchyModel.sourceIndexAt(index) : index
            }
            root.openActions(list, position)
        }
    }
    RedesignActionMenu {
        id: sortMenu
        videoBackdropContent: root.videoBackdropContent
        objectName: "queueSortMenu"
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        onAboutToShow: {
            entries = root.sortChoices.map(choice => ({ action: String(choice.criteria), label: choice.text, checked: choice.criteria === root.sortKey }))
                .concat([{ action: "reverse", label: qsTr("Reverse order"), checked: root.sortDescending }])
        }
        onActionRequested: action => {
            if (action === "reverse") {
                if (root.sortChoices.length) root.sortRequested(root.sortChoices.some(choice => choice.criteria === root.sortKey) ? root.sortKey : root.sortChoices[0].criteria, !root.sortDescending)
            } else root.sortRequested(Number(action), root.sortDescending)
        }
    }
    RedesignActionMenu {
        id: groupMenu
        objectName: "queueGroupMenu"
        videoBackdropContent: root.videoBackdropContent
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        onAboutToShow: entries = [{ action: "toggle", label: root.hierarchyModel && root.hierarchyModel.expandedAt(root.hierarchyModel.rowForGroupKey(root.selectedGroupKey)) ? qsTr("Collapse") : qsTr("Expand") }]
        onActionRequested: if (root.hierarchyModel) root.hierarchyModel.toggleExpanded(root.hierarchyModel.rowForGroupKey(root.selectedGroupKey))
    }
    RedesignActionMenu {
        id: actionsMenu
        videoBackdropContent: root.videoBackdropContent
        objectName: "queueActionsMenu"
        scaleFactor: root.scaleFactor; lightTheme: root.lightTheme; accentColor: root.accentColor
        onAboutToShow: entries = [
            { action: "earlier", label: qsTr("Move earlier"), enabled: root.actionIndex > 0 },
            { action: "later", label: qsTr("Move later"), enabled: root.actionIndex >= 0 && root.actionIndex < root.itemCount - 1 },
            { action: "remove", label: qsTr("Remove from queue"), icon: "remove", enabled: root.actionIndex >= 0 && root.actionIndex < root.itemCount },
            { action: "save", label: qsTr("Save queue to file"), enabled: root.itemCount > 0 },
            { action: "clear", label: qsTr("Clear queue"), enabled: root.itemCount > 0 },
            { action: "close", label: qsTr("Close queue"), icon: "close" }
        ].concat(root.addonActions.map(entry => ({action: "addon/" + entry.id, label: entry.name || entry.id, enabled: root.actionIndex >= 0 && root.actionIndex < root.itemCount})))
        onActionRequested: action => root.runAction(action)
    }
    DropArea {
        anchors.fill: parent
        keys: ["vlc-redesign-library", "text/uri-list"]
        onDropped: drop => {
            if (drop.source && drop.source["libraryView"]) {
                root.libraryDropRequested(drop.source)
                drop.acceptProposedAction()
            } else if (drop.hasUrls && drop.urls.length > 0) {
                root.filesDropped(drop.urls)
                drop.acceptProposedAction()
            }
        }
    }
}
