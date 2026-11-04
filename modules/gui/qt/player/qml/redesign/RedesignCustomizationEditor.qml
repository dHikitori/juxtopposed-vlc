// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic as C

Item {
    id: root
    required property RedesignPlayerView player
    property Component videoBackdropContent
    property string errorMessage: ""
    property string presetId: ""
    property string editPresetId: ""
    onPresetIdChanged: if (opened && !saving && presetId !== editPresetId) cancel()
    property bool saving: false
    readonly property alias opened: panel.visible
    readonly property RedesignPlayerLayout model: player.layoutModel
    readonly property RedesignTheme theme: player.theme
    readonly property var available: catalog.available(model.layout).filter(row =>
        row.id !== "mute" || !!model.location("volume"))
    readonly property bool pairedVolume: {
        const mute = model.location("mute"), volume = model.location("volume")
        return !!mute && !!volume && mute.zone === volume.zone && mute.index + 1 === volume.index
    }
    readonly property bool groupedVolume: pairedVolume && player.chromeItems.mute.visible && player.chromeItems.volume.visible
    readonly property var paletteLabels: ({
        speed: qsTr("Playback\nSpeed"), forward10: qsTr("Forward\n10s"), backward10: qsTr("Backward\n10s"),
        forward15: qsTr("Forward\n15s"), backward15: qsTr("Backward\n15s"), pip: qsTr("Picture in\nPicture"),
        rightSidebar: qsTr("Right\nSidebar"), playlistName: qsTr("Playlist\nName")
    })
    property int generation: 0
    property string dragId: ""
    property bool dragging: false
    property point pressPosition
    property var dropLocation: null
    property var previousFocus: null
    signal saveRequested(string layout)
    signal finished()
    parent: root.active ? root.player.C.Overlay.overlay : root.player
    width: player.width; height: player.height
    readonly property point playerOrigin: {
        // The view can change windows at startup. Refresh each edit session.
        root.active
        const playerWindow = root.player.Window.window
        const overlayWindow = parent ? parent.Window.window : null
        if (playerWindow) { playerWindow.x; playerWindow.y; playerWindow.width; playerWindow.height }
        if (overlayWindow) { overlayWindow.x; overlayWindow.y; overlayWindow.width; overlayWindow.height }
        if (!parent || parent === player) return Qt.point(0, 0)
        for (let ancestor = player; ancestor; ancestor = ancestor.parent) { ancestor.x; ancestor.y }
        return player.mapToItem(parent, 0, 0)
    }
    x: playerOrigin.x; y: playerOrigin.y
    property bool active: false
    visible: active

    function begin() {
        if (opened) return
        previousFocus = Window.window ? Window.window.activeFocusItem : null
        ++generation; errorMessage = ""; saving = false; editPresetId = presetId
        active = true; model.begin(); panel.open()
    }
    function cancel() { if (!saving) { ++generation; endDrag(); model.cancel(); panel.close() } }
    function save() {
        if (!model.dirty || saving) return
        saving = true; errorMessage = ""
        root.saveRequested(model.serialize())
    }
    function completeSave(success, error) {
        saving = false
        if (success) { model.finish(true); panel.close() }
        else errorMessage = error || qsTr("Hakone cannot save this layout. Try again.")
    }
    function bounds(item) {
        root.x; root.y
        if (!item) return Qt.rect(0, 0, 0, 0)
        // mapToItem alone does not observe changes in reparented ancestors.
        for (let ancestor = item; ancestor && ancestor !== root.parent; ancestor = ancestor.parent) {
            ancestor.x; ancestor.y; ancestor.width; ancestor.height; ancestor.scale; ancestor.visible
        }
        const point = item.mapToItem(root, 0, 0)
        return Qt.rect(point.x, point.y, item.width, item.height)
    }
    function locationFor(bar, x) {
        const zones = bar === "header" ? ["headerLeft", "headerCenter", "headerRight"] : ["footerLeft", "footerCenter", "footerRight"]
        let best = zones[1], distance = Infinity
        for (let column = 0; column < zones.length; ++column) {
            const zone = zones[column], ids = model.layout.zones[zone]
            let left = root.width * column / 3, right = root.width * (column + 1) / 3
            if (ids.length) {
                const rects = ids.map(id => bounds(player.chromeItems[id]))
                left = Math.min(...rects.map(r => r.x)); right = Math.max(...rects.map(r => r.x + r.width))
            }
            const delta = x < left ? left - x : x > right ? x - right : 0
            if (delta < distance) { best = zone; distance = delta }
        }
        return {zone: best, index: insertionIndex(best, x)}
    }
    function insertionIndex(zone, x) {
        const ids = model.layout.zones[zone]
        for (let n = 0; n < ids.length; ++n) {
            const rect = bounds(player.chromeItems[ids[n]])
            if (x < rect.x + rect.width / 2) return n
        }
        return ids.length
    }
    function queueMove(id, zone, index) {
        const owner = root, token = generation, ids = moveIds(id)
        // A move can replace the delegate that owns the current pointer grab.
        Qt.callLater(() => {
            if (Qt.isQtObject(owner) && owner.opened && owner.model.editing && !owner.saving && token === owner.generation) {
                if (ids.length > 1) owner.model.moveGroup(ids, zone, index)
                else owner.model.move(id, zone, index)
            }
        })
    }
    function queueRemove(id) {
        const owner = root, token = generation, ids = moveIds(id)
        Qt.callLater(() => { if (Qt.isQtObject(owner) && owner.opened && owner.model.editing && !owner.saving && token === owner.generation) owner.model.removeGroup(ids) })
    }
    function moveIds(id) {
        return ((id === "mute" || id === "volume") && pairedVolume)
            || (id === "volume" && !model.location("mute") && !model.location("volume")) ? ["mute", "volume"] : [id]
    }
    function united(a, b) {
        const x = Math.min(a.x, b.x), y = Math.min(a.y, b.y)
        return Qt.rect(x, y, Math.max(a.x + a.width, b.x + b.width) - x,
            Math.max(a.y + a.height, b.y + b.height) - y)
    }
    function paletteOutlineSize(id) {
        if (id === "spacer" || id === "divider") return Qt.size(24, 32)
        if (id === "rightSidebar") return Qt.size(32, 32)
        const tall = ["speed", "forward10", "backward10", "stop", "repeat", "shuffle", "record",
            "snapshot", "pip", "hdr", "zoom", "bookmark", "playlists"]
        return tall.includes(id) ? Qt.size(30, 32) : Qt.size(28, 28)
    }
    function outlineBounds(id, control) {
        const actual = bounds(control), dp = theme.dp
        const location = model.location(id), zone = location ? location.zone : ""
        function centered(width, height) {
            return Qt.rect(actual.x + (actual.width - dp(width)) / 2,
                actual.y + (actual.height - dp(height)) / 2, dp(width), dp(height))
        }
        if (id.startsWith("divider:")) return centered(24, 28)
        if (["menu", "library", "favorite", "mute"].includes(id)) return centered(28, 28)
        if (id === "folder") return Qt.rect(actual.x + dp(6), actual.y + (actual.height - dp(28)) / 2,
            Math.max(0, actual.width - dp(9)), dp(28))
        if (id === "title") {
            const width = Math.min(actual.width, Math.ceil(control.implicitWidth / root.player.scaleFactor) * root.player.scaleFactor)
            const height = Math.max(0, actual.height - dp(9))
            return Qt.rect(actual.x + (actual.width - width) / 2, actual.y + (actual.height - height) / 2, width, height)
        }
        if (["minimize", "maximize", "fullscreen", "close"].includes(id)) {
            const rect = centered(actual.width / root.player.scaleFactor, 24)
            if (zone.startsWith("footer")) rect.x -= dp(2)
            return rect
        }
        if (id === "play") return centered(54, 34)
        if (["audio", "subtitles", "settings", "queue"].includes(id) || id.startsWith("addon:")) return centered(30, 32)
        if (id === "volume" && groupedVolume) {
            const rect = united(actual, bounds(player.chromeItems.mute))
            return Qt.rect(rect.x + dp(1), rect.y + (rect.height - dp(36)) / 2, rect.width + dp(2), dp(36))
        }
        if (id === "timeline") {
            const inline = JSON.stringify(model.layout) === JSON.stringify(model.defaults())
            return Qt.rect(actual.x + dp(inline ? 8 : 0), actual.y - dp(zone === "above" ? 5 : 4),
                Math.max(0, actual.width - dp(inline ? 12 : 0)), dp(36))
        }
        return actual
    }
    function startPointer(id, area, mouse) {
        if (saving) return
        placement.close()
        dragId = id; pressPosition = area.mapToItem(root, mouse.x, mouse.y)
    }
    function movePointer(area, mouse) {
        if (!dragId || saving) return
        const point = area.mapToItem(root, mouse.x, mouse.y)
        if (!dragging && Math.hypot(point.x - pressPosition.x, point.y - pressPosition.y) < theme.dp(6)) return
        const mapped = root.mapToItem(ghost.parent, point.x, point.y)
        ghost.x = mapped.x - ghost.width / 2; ghost.y = mapped.y - ghost.height / 2
        dragging = true
    }
    function releasePointer(id, anchor) {
        if (dragging) ghost.Drag.drop()
        else if (id && !saving) showPlacement(id, anchor)
        endDrag()
    }
    function endDrag() { dragging = false; dragId = ""; dropLocation = null }
    function showPlacement(id, anchor) {
        placement.controlId = id
        const labels = [qsTr("Top left"), qsTr("Top center"), qsTr("Top right"), qsTr("Bottom left"), qsTr("Bottom center"), qsTr("Bottom right"), qsTr("Above controls")]
        placement.entries = model.zones.map((zone, n) => ({action: zone, label: labels[n]}))
            .concat(model.location(id) ? [{action: "remove", label: qsTr("Remove from player"), separatorBefore: true}] : [])
        const rect = bounds(anchor)
        placementAnchor.x = rect.x; placementAnchor.y = rect.y
        placementAnchor.width = rect.width; placementAnchor.height = rect.height
        placement.toggleFrom(placementAnchor)
    }
    RedesignCustomizationCatalog { id: catalog; panels: root.player.addonPanels || [] }
    Connections {
        target: root.model
        function onEditingChanged() { if (root.opened && !root.model.editing && !root.saving) panel.close() }
    }
    Keys.onEscapePressed: root.cancel()

    component Outline: Item {
        id: outline
        property color stroke: root.theme.playerLabel
        readonly property real unit: root.theme.dp(1)
        readonly property real pixelRatio: Screen.devicePixelRatio
        readonly property int rasterGrid: {
            // sourceSize is rounded to whole logical pixels before Qt applies
            // the display scale. Use a logical grid with whole physical pixels.
            for (let step = 1; step <= 64; ++step) {
                const pixels = step * pixelRatio
                if (Math.abs(pixels - Math.round(pixels)) < 0.000001) return step
            }
            return 1
        }
        readonly property point sceneOrigin: {
            // Observe every translation that can move the overlay on the pixel grid.
            for (let ancestor = outline; ancestor; ancestor = ancestor.parent) {
                ancestor.x; ancestor.y
            }
            return mapToItem(null, 0, 0)
        }
        // Rasterize once on the display grid. Keep the vector at its original
        // fractional position instead of resizing a rounded image back onto it.
        RedesignSvg {
            id: outlinePaint
            // This frame already matches the display grid. SVG intrinsic sizes
            // are rounded to integers; fitting that aspect ratio would shift
            // fractional frames before they reach their authored position.
            fillMode: Image.Stretch
            x: Math.floor(outline.sceneOrigin.x * outline.pixelRatio) / outline.pixelRatio - outline.sceneOrigin.x
            y: Math.floor(outline.sceneOrigin.y * outline.pixelRatio) / outline.pixelRatio - outline.sceneOrigin.y
            width: Math.ceil((outline.width - x) / outline.rasterGrid) * outline.rasterGrid
            height: Math.ceil((outline.height - y) / outline.rasterGrid) * outline.rasterGrid
            // Keep the dash phase continuous through the corners, as in the design.
            svg: outline.width > outline.unit && outline.height > outline.unit
                ? '<svg xmlns="http://www.w3.org/2000/svg" width="' + width + '" height="' + height
                    + '" viewBox="0 0 ' + width + ' ' + height + '">'
                    + '<rect x="' + (outline.unit / 2 - x) + '" y="' + (outline.unit / 2 - y)
                    + '" width="' + (outline.width - outline.unit) + '" height="' + (outline.height - outline.unit)
                    + '" fill="none" stroke="' + outline.stroke.toString() + '" stroke-width="' + outline.unit
                    + '" stroke-dasharray="' + outline.unit * 4 + ' ' + outline.unit * 4 + '"/></svg>' : ""
        }
    }
    component PointerArea: MouseArea {
        id: pointerArea
        preventStealing: true
        required property string controlId
        required property Item anchorItem
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        enabled: !root.saving
        cursorShape: root.dragging ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        onPressed: mouse => {
            root.startPointer(controlId, pointerArea, mouse)
            anchorItem.focusReason = Qt.MouseFocusReason
            anchorItem.forceActiveFocus(Qt.MouseFocusReason)
        }
        onPositionChanged: mouse => { if (pressed && pressedButtons === Qt.LeftButton) root.movePointer(pointerArea, mouse) }
        onReleased: root.releasePointer(controlId, anchorItem)
        onCanceled: root.endDrag()
    }
    Repeater {
        model: root.opened ? Object.keys(root.player.chromeItems).filter(id => !!root.model.location(id)) : []
        delegate: C.Control {
            id: handle
            required property string modelData
            readonly property Item control: root.player.chromeItems[modelData]
            readonly property rect outlineRect: control ? root.outlineBounds(modelData, control) : Qt.rect(0, 0, 0, 0)
            readonly property rect rect: {
                // Keep full button hit areas and the visible border available.
                if (modelData === "title") return outlineRect
                let actual = root.bounds(control)
                if (modelData === "volume" && root.groupedVolume) actual = root.united(actual, root.bounds(root.player.chromeItems.mute))
                return root.united(actual, outlineRect)
            }
            activeFocusOnTab: true
            objectName: "customizeHandle_" + modelData
            x: rect.x; y: rect.y; width: rect.width; height: rect.height
            visible: root.player.layoutReady && !!control && control.visible && width > 0 && height > 0
                && !(modelData === "mute" && root.groupedVolume)
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Move %1").arg(catalog.definition(modelData).label)
            Accessible.onPressAction: root.showPlacement(modelData, handle)
            Keys.onReturnPressed: root.showPlacement(modelData, handle)
            Keys.onEnterPressed: root.showPlacement(modelData, handle)
            Keys.onSpacePressed: root.showPlacement(modelData, handle)
            Outline {
                objectName: "customizeOutline_" + handle.modelData
                x: handle.outlineRect.x - handle.x; y: handle.outlineRect.y - handle.y
                width: handle.outlineRect.width; height: handle.outlineRect.height
                stroke: handle.visualFocus ? root.theme.accent : root.theme.playerLabel
            }
            PointerArea { anchors.fill: parent; controlId: handle.modelData; anchorItem: handle }
        }
    }
    component BarDrop: DropArea {
        property string bar
        enabled: root.opened && root.dragging && !root.saving
        function update(drag) {
            if (drag.source !== ghost) return
            root.dropLocation = bar === "above" ? {zone: "above", index: root.insertionIndex("above", drag.x)} : root.locationFor(bar, drag.x)
        }
        onEntered: drag => update(drag)
        onPositionChanged: drag => update(drag)
        onExited: root.dropLocation = null
        onDropped: drop => {
            if (drop.source !== ghost || !root.dropLocation) return
            const target = root.dropLocation
            root.queueMove(root.dragId, target.zone, target.index)
            drop.acceptProposedAction()
        }
    }
    BarDrop { width: parent.width; height: root.theme.dp(40); bar: "header" }
    BarDrop { width: parent.width; height: root.theme.dp(54); y: parent.height - height; bar: "footer" }
    BarDrop {
        id: upperDrop
        width: parent.width; height: root.theme.dp(54)
        y: parent.height - Math.max(root.player.controlsHeight, root.theme.dp(108))
        bar: "above"
        Outline { anchors.fill: parent; anchors.margins: root.theme.dp(6); visible: root.opened && root.dragging; stroke: upperDrop.containsDrag ? root.theme.accent : root.theme.playerLabel }
    }
    Rectangle {
        visible: root.dragging && !!root.dropLocation
        width: root.theme.dp(3); height: root.theme.dp(28); radius: width / 2; color: root.theme.accent
        x: {
            if (!root.dropLocation) return 0
            const zone = root.dropLocation.zone, ids = root.model.layout.zones[zone], index = root.dropLocation.index
            if (!ids.length) return root.theme.dp(20)
            const rect = root.bounds(root.player.chromeItems[ids[Math.min(index, ids.length - 1)]])
            return index < ids.length ? rect.x - root.theme.dp(5) : rect.x + rect.width + root.theme.dp(3)
        }
        y: !root.dropLocation ? 0 : root.dropLocation.zone.startsWith("header") ? root.theme.dp(6)
            : root.dropLocation.zone === "above" ? upperDrop.y + root.theme.dp(13) : root.height - root.theme.dp(41)
    }
    Rectangle {
        id: ghost
        objectName: "customizationDragGhost"
        parent: root.active ? root.player.C.Overlay.overlay : root
        z: 10000
        readonly property string controlId: root.dragId
        width: root.theme.dp(60); height: root.theme.dp(46)
        radius: root.theme.dp(6); color: root.theme.panel; border.color: root.theme.accent
        visible: root.opened && root.dragging
        Drag.active: root.dragging
        Drag.source: ghost
        Drag.supportedActions: Qt.MoveAction
        Drag.proposedAction: Qt.MoveAction
        Drag.hotSpot.x: width / 2; Drag.hotSpot.y: height / 2
        RedesignIcon {
            anchors.centerIn: parent; width: root.theme.dp(24); height: width
            name: {
                const definition = catalog.definition(root.dragId) || {}
                return definition.paletteIcon || definition.icon || ""
            }
            markup: (catalog.definition(root.dragId) || {}).iconMarkup || ""
            color: root.theme.foreground
        }
    }
    C.Popup {
        id: panel
        objectName: "customizationPanel"
        z: root.z + 1
        popupType: C.Popup.Item
        parent: root.player
        width: Math.max(0, Math.min(root.theme.dp(1048), parent.width - root.theme.dp(30)))
        height: Math.max(0, Math.min(root.theme.dp(655), parent.height - root.theme.dp(70) - root.player.controlsHeight))
        x: (parent.width - width) / 2
        y: Math.max(root.theme.dp(55), Math.min((parent.height - height) / 2 - root.theme.dp(23), parent.height - root.player.controlsHeight - height - root.theme.dp(15)))
        padding: 0; modal: false; dim: false; focus: true
        closePolicy: root.saving ? C.Popup.NoAutoClose : C.Popup.CloseOnEscape
        background: RedesignDialogSurface { theme: root.theme; fill: root.theme.lightTheme ? root.theme.panel : "#0e0e0e"; videoBackdropContent: root.videoBackdropContent }
        onAboutToShow: controlPalette.positionViewAtBeginning()
        onClosed: {
            ++root.generation; root.endDrag(); placement.close()
            if (root.model.editing) root.model.cancel()
            if (Qt.isQtObject(root.previousFocus) && root.previousFocus.visible) root.previousFocus.forceActiveFocus()
            else root.player.forceActiveFocus()
            root.active = false; root.finished()
        }
        contentItem: Item {
            Keys.onEscapePressed: root.cancel()
            Rectangle {
                width: parent.width; height: root.theme.dp(94); radius: root.theme.dp(14); color: root.theme.field
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: parent.height / 2; color: parent.color }
            }
            Text {
                x: root.theme.dp(19); y: root.theme.dp(9.5); height: root.theme.dp(32); width: parent.width - root.theme.dp(80)
                text: qsTr("Customize Player"); color: root.theme.foreground; font.pixelSize: root.theme.dp(14)
                verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight
            }
            RedesignButton {
                objectName: "customizationClose"
                x: parent.width - width - root.theme.dp(7); y: root.theme.dp(7)
                scaleFactor: root.theme.scaleFactor; lightTheme: root.theme.lightTheme; accentColor: root.theme.accentColor
                iconName: "close"; text: qsTr("Close player editor"); enabled: !root.saving
                onClicked: root.cancel()
            }
            Text {
                x: root.theme.dp(19); y: root.theme.dp(52.5); width: parent.width - root.theme.dp(40); height: root.theme.dp(34)
                text: qsTr("Drag items to the player to add or move them. Drag items into this box to remove them.")
                color: root.theme.playerLabel; font.pixelSize: root.theme.dp(14); wrapMode: Text.WordWrap; verticalAlignment: Text.AlignVCenter
            }
            GridView {
                id: controlPalette
                objectName: "customizationPalette"
                x: root.theme.dp(22); y: root.theme.dp(114); width: Math.max(0, parent.width - root.theme.dp(44))
                height: Math.max(0, parent.height - y - root.theme.dp(74)); clip: true
                cellWidth: Math.min(width, root.theme.dp(110))
                cellHeight: root.theme.dp(120)
                model: root.available
                Keys.onPressed: event => {
                    if ([Qt.Key_Left, Qt.Key_Right, Qt.Key_Up, Qt.Key_Down, Qt.Key_Home, Qt.Key_End].includes(event.key)) {
                        Qt.callLater(() => {
                            if (currentItem && currentItem.activeFocus) currentItem.focusReason = Qt.TabFocusReason
                        })
                    }
                    event.accepted = false
                }
                function revealFocusedCell() { Qt.callLater(revealFocusedCellNow) }
                function revealFocusedCellNow() {
                    if (!root.opened || !currentItem || !currentItem.activeFocus) return
                    forceLayout()
                    positionViewAtIndex(currentIndex, cellHeight > height ? GridView.Beginning : GridView.Contain)
                }
                onWidthChanged: revealFocusedCell()
                onHeightChanged: revealFocusedCell()
                onCellWidthChanged: revealFocusedCell()
                onCellHeightChanged: revealFocusedCell()
                C.ScrollBar.vertical: C.ScrollBar { }
                delegate: C.Control {
                    id: cell
                    required property var modelData
                    required property int index
                    readonly property int referenceRow: Math.floor(controlPalette.width / controlPalette.cellWidth) === 9
                        ? Math.min(3, Math.floor(index / 9)) : 0
                    readonly property size outlineSize: root.paletteOutlineSize(modelData.id)
                    focus: GridView.isCurrentItem
                    activeFocusOnTab: true
                    onActiveFocusChanged: if (activeFocus) {
                        controlPalette.currentIndex = index
                        controlPalette.revealFocusedCell()
                    }
                    objectName: "customizePalette_" + modelData.id
                    width: controlPalette.cellWidth; height: controlPalette.cellHeight
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Add %1").arg(modelData.label)
                    Accessible.onPressAction: root.showPlacement(modelData.id, cell)
                    Keys.onReturnPressed: root.showPlacement(modelData.id, cell)
                    Keys.onEnterPressed: root.showPlacement(modelData.id, cell)
                    Keys.onSpacePressed: root.showPlacement(modelData.id, cell)
                    Outline {
                        id: cellOutline
                        objectName: "customizePaletteOutline_" + cell.modelData.id
                        anchors.horizontalCenter: parent.horizontalCenter; y: root.theme.dp(14 - Math.max(0, cell.referenceRow - 2) * 4)
                        width: root.theme.dp(cell.outlineSize.width); height: root.theme.dp(cell.outlineSize.height)
                        stroke: cell.visualFocus ? root.theme.accent : root.theme.playerLabel
                        RedesignIcon {
                            anchors.centerIn: parent; width: root.theme.dp(24); height: width
                            name: cell.modelData.paletteIcon || cell.modelData.icon
                            markup: cell.modelData.iconMarkup || ""
                            color: {
                                if (cell.modelData.id === "thumbnail") return root.theme.lightTheme ? "#a3a3a3" : "#303030"
                                if (cell.modelData.id === "divider") return root.theme.lightTheme ? root.theme.border : "#373737"
                                return root.theme.foreground
                            }
                        }
                    }
                    Text {
                        x: root.theme.dp(5); y: cellOutline.y + cellOutline.height + root.theme.dp(16.5)
                        width: parent.width - root.theme.dp(10); height: root.theme.dp(40)
                        text: root.paletteLabels[cell.modelData.id] || cell.modelData.label; textFormat: Text.PlainText
                        font.pixelSize: root.theme.dp(14); color: root.theme.foreground
                        wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter
                    }
                    PointerArea { anchors.fill: parent; controlId: cell.modelData.id; anchorItem: cell }
                }
            }
            DropArea {
                x: controlPalette.x; y: root.theme.dp(94); width: controlPalette.width; height: parent.height - y - root.theme.dp(74)
                enabled: root.dragging && !root.saving
                onDropped: drop => { if (drop.source === ghost) { root.queueRemove(root.dragId); drop.acceptProposedAction() } }
            }
            RedesignButton {
                objectName: "customizationCancel"
                x: root.theme.dp(20); y: parent.height - root.theme.dp(52); width: root.theme.dp(100); height: root.theme.dp(32)
                scaleFactor: root.theme.scaleFactor; lightTheme: root.theme.lightTheme; accentColor: root.theme.accentColor
                caption: qsTr("Cancel"); captionFontWeight: Font.DemiBold; captionVerticalOffset: 1; text: caption; enabled: !root.saving; onClicked: root.cancel()
                background: Rectangle { radius: root.theme.dp(6); color: parent.hovered ? root.theme.hover : root.theme.background; border.width: parent.visualFocus ? 1 : 0; border.color: root.theme.accent }
            }
            Text {
                x: root.theme.dp(136); y: parent.height - root.theme.dp(52); width: Math.max(0, parent.width - root.theme.dp(272)); height: root.theme.dp(32)
                objectName: "customizationError"
                Accessible.role: Accessible.StaticText; Accessible.name: text; Accessible.ignored: text.length === 0
                text: root.errorMessage; textFormat: Text.PlainText; color: root.theme.danger; font.pixelSize: root.theme.dp(12)
                wrapMode: Text.WordWrap; verticalAlignment: Text.AlignVCenter
            }
            RedesignButton {
                objectName: "customizationSave"
                x: parent.width - width - root.theme.dp(20); y: parent.height - root.theme.dp(52); width: root.theme.dp(100); height: root.theme.dp(32)
                scaleFactor: root.theme.scaleFactor; lightTheme: root.theme.lightTheme; accentColor: root.theme.accentColor
                caption: qsTr("Save"); text: caption; enabled: root.model.dirty && !root.saving; onClicked: root.save()
                primaryAction: true; captionFontWeight: Font.DemiBold; captionVerticalOffset: 1
            }
        }
    }
    Item { id: placementAnchor }
    RedesignActionMenu {
        id: placement
        objectName: "customizationPlacement"
        z: root.z + 2
        onOpened: { currentIndex = 0; itemAt(0).forceActiveFocus() }
        property string controlId: ""
        parent: root
        scaleFactor: root.theme.scaleFactor; lightTheme: root.theme.lightTheme; accentColor: root.theme.accentColor
        videoBackdropContent: root.videoBackdropContent
        onActionRequested: action => {
            if (action === "remove") root.queueRemove(controlId)
            else root.queueMove(controlId, action, root.model.layout.zones[action].length)
        }
    }
}
