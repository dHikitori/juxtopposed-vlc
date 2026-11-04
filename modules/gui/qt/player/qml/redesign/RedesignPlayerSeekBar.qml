// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick

RedesignSeekBar {
    id: root
    property bool mediaStarted: false
    property real playerPosition: 0
    property bool dragActive: false
    property real dragPosition: 0
    property bool pointerCancelled: false
    signal dragStarted()
    signal dragFinished(real position, bool commit)
    signal seekRequested(real position)
    // The wheel over the bar jumps through the media, as in the classic
    // interface. The player turns the event into short jumps.
    signal wheelSeekRequested(var event)
    playbackPosition: playerPosition

    function beginDrag() {
        finishDragTimer.stop()
        if (dragActive) return
        dragActive = true
        dragPosition = value
        dragStarted()
    }
    function finishDrag(commit) {
        if (!dragActive) return
        dragActive = false
        dragFinished(dragPosition, commit && enabled && visible && mediaStarted)
    }
    onPressedChanged: {
        if (pressed && dragPointer.active) beginDrag()
        else if (!pressed && dragActive) finishDragTimer.restart()
    }
    // Slider commits a track click after clearing pressed.
    Timer { id: finishDragTimer; interval: 0; onTriggered: root.finishDrag(!root.pointerCancelled) }
    PointHandler {
        id: dragPointer
        acceptedButtons: Qt.LeftButton
        onActiveChanged: {
            if (active) {
                root.pointerCancelled = false
                if (root.pressed) root.beginDrag()
            } else if (!root.pressed && root.dragActive) finishDragTimer.restart()
        }
        onCanceled: { root.pointerCancelled = true; finishDragTimer.stop(); root.finishDrag(false) }
    }
    WheelHandler {
        target: null
        orientation: Qt.Vertical | Qt.Horizontal
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: event => {
            if (root.mediaStarted && !root.pressed && !root.dragActive) root.wheelSeekRequested(event)
            event.accepted = true
        }
    }
    Binding on value {
        when: !root.pressed && !root.dragActive
        value: { root.from; root.to; return root.playerPosition }
        restoreMode: Binding.RestoreBindingOrValue
    }
    onMoved: { dragPosition = value; seekRequested(value) }
    onVisibleChanged: visibilityCheck.restart()
    Timer { id: visibilityCheck; interval: 0; onTriggered: if (!root.visible) root.finishDrag(false) }
    onEnabledChanged: if (!enabled) finishDrag(false)
    onMediaStartedChanged: if (!mediaStarted) finishDrag(false)
    Component.onDestruction: finishDrag(false)
}
