// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "JuxtopposedQueue"
    width: 400; height: 600
    visible: true; when: windowShown
    property int activeBackdrops: 0
    Component {
        id: backdropProbe
        Item {
            objectName: "contextBackdropProbe"
            property real cornerRadius: 0
            property bool menuShadow: false
            Component.onCompleted: testCase.activeBackdrops++
            Component.onDestruction: testCase.activeBackdrops--
        }
    }
    ListModel { id: media }
    ListModel {
        id: treeFixture
        function populate() {
            clear()
            append({title: "Folder", isCurrent: false, queueIndex: -1, treeDepth: 0, treeGroup: true, treeExpanded: true, treeKey: "folder", treePlaylist: false, containsCurrent: true})
            for (let i = 0; i < 3; ++i) append({title: ["First", "Second", "Third"][i], isCurrent: i === 1, queueIndex: i, treeDepth: 1, treeGroup: false, treeExpanded: false, treeKey: String(i), treePlaylist: false, containsCurrent: false})
        }
        function sourceIndexAt(row) { return row >= 0 && row < count ? get(row).queueIndex : -1 }
        function rowForSourceIndex(index, reveal) { if (reveal && count === 1) populate(); return count > 1 ? index + 1 : -1 }
        function rowForGroupKey(key) { return key === "folder" ? 0 : -1 }
        function expandedAt(row) { return row === 0 && count > 1 }
        function setExpanded(row, expanded) { if (row !== 0) return; if (expanded) populate(); else { if (count > 1) remove(1, 3); setProperty(0, "treeExpanded", false) } }
        function toggleExpanded(row) { setExpanded(row, !expandedAt(row)) }
        function insertionIndexAt(row, after) { return row === 0 ? (after ? 3 : 0) : row - 1 + (after ? 1 : 0) }
        function rowForInsertion(index) { return index === 0 ? 0 : index + 1 }
    }
    RedesignQueue {
        id: queue
        anchors.fill: parent
        queueModel: media; itemCount: media.count; currentIndex: 1
        totalDurationText: "1h 12m"
        sortChoices: [{ criteria: 11, text: "Title" }, { criteria: 22, text: "Duration" }]
        onMoveRequested: (from, to) => media.move(from, to > from ? to - 1 : to, 1)
        onRemoveRequested: index => media.remove(index)
        onSortRequested: (key, descending) => { sortKey = key; sortDescending = descending }
    }
    SignalSpy { id: moveSpy; target: queue; signalName: "moveRequested" }
    SignalSpy { id: removeSpy; target: queue; signalName: "removeRequested" }
    SignalSpy { id: sortSpy; target: queue; signalName: "sortRequested" }
    SignalSpy { id: playSpy; target: queue; signalName: "playRequested" }
    SignalSpy { id: saveSpy; target: queue; signalName: "saveRequested" }
    SignalSpy { id: clearSpy; target: queue; signalName: "clearRequested" }
    function test_contextBackdropLifetime() {
        queue.videoBackdropContent = backdropProbe
        for (const name of ["queueSortMenu", "queueActionsMenu", "queueGroupMenu"]) {
            const menu = findChild(queue, name)
            compare(menu.videoBackdropContent, backdropProbe)
            menu.open()
            tryCompare(testCase, "activeBackdrops", 1)
            const backdrop = findChild(menu.background, "contextBackdropProbe")
            verify(backdrop !== null)
            compare(backdrop.width, menu.background.width)
            compare(backdrop.height, menu.background.height)
            compare(backdrop.cornerRadius, 10)
            compare(backdrop.menuShadow, true)
            for (let parent = menu.background; parent; parent = parent.parent)
                verify(parent !== queue, "The menu must stay outside its capture source.")
            menu.close()
            tryCompare(testCase, "activeBackdrops", 0)
        }
        queue.videoBackdropContent = null
    }
    function test_groupDisclosureFollowsMirroring() {
        queue.hierarchyModel = treeFixture
        const button = findChild(queue, "queueGroupToggle_0")
        verify(button !== null)
        compare(button.iconName, "chevron-down")
        try {
            queue.LayoutMirroring.childrenInherit = true
            queue.LayoutMirroring.enabled = true
            treeFixture.setExpanded(0, false)
            tryVerify(() => findChild(queue, "queueGroupToggle_0") && findChild(queue, "queueGroupToggle_0").iconName === "chevron-left")
            mouseClick(findChild(queue, "queueGroupToggle_0"))
            tryVerify(() => findChild(queue, "queueGroupToggle_0") && findChild(queue, "queueGroupToggle_0").iconName === "chevron-down")
            compare(playSpy.count, 0)
            compare(removeSpy.count, 0)
        } finally {
            queue.LayoutMirroring.enabled = false
            queue.LayoutMirroring.childrenInherit = false
        }
        treeFixture.setExpanded(0, false)
        tryVerify(() => findChild(queue, "queueGroupToggle_0") && findChild(queue, "queueGroupToggle_0").iconName === "chevron-right")
    }
    function init() {
        queue.videoBackdropContent = null
        queue.hierarchyModel = null; queue.selectedGroupKey = ""
        treeFixture.populate()
        queue.visible = true
        media.clear()
        media.append({title: "First", isCurrent: false})
        media.append({title: "Second", isCurrent: true})
        media.append({title: "Third", isCurrent: false})
        queue.selectedIndex = -1; queue.sortKey = -1; queue.sortDescending = false
        moveSpy.clear(); removeSpy.clear(); sortSpy.clear(); playSpy.clear(); saveSpy.clear(); clearSpy.clear()
        findChild(queue, "queueSortMenu").close()
        findChild(queue, "queueActionsMenu").close()
        findChild(queue, "queueGroupMenu").close()
        wait(50)
    }
    function test_treeGroupDisclosureAndKeyboardNeverPlayOrRemoveMedia() {
        queue.hierarchyModel = treeFixture
        wait(50)
        mouseDoubleClickSequence(findChild(queue, "queueGroup_0"))
        compare(treeFixture.count, 1)
        compare(playSpy.count, 0)
        queue.forceActiveFocus(); keyClick(Qt.Key_Delete)
        compare(removeSpy.count, 0); compare(media.count, 3)
        keyClick(Qt.Key_Right); compare(treeFixture.count, 4)
        keyClick(Qt.Key_Left); compare(treeFixture.count, 1)
        keyClick(Qt.Key_Menu)
        const menu = findChild(queue, "queueGroupMenu")
        tryCompare(menu, "opened", true); wait(150)
        mouseClick(findChild(menu, "libraryAction_toggle"))
        compare(treeFixture.count, 4); compare(playSpy.count, 0); compare(removeSpy.count, 0)
    }
    function test_treeChildActionsUseCoreIndexInsteadOfVisibleRow() {
        queue.hierarchyModel = treeFixture
        wait(50)
        const child = findChild(queue, "queueItem_1")
        mouseDoubleClickSequence(child)
        compare(playSpy.signalArguments[0][0], 1)
        mouseClick(child, 60, child.height / 2, Qt.RightButton)
        const menu = findChild(queue, "queueActionsMenu")
        tryCompare(menu, "opened", true); wait(150)
        compare(queue.selectedIndex, 1)
        mouseClick(findChild(menu, "libraryAction_remove"))
        compare(removeSpy.signalArguments[0][0], 1)
        compare(media.get(1).title, "Third")
    }
    function test_treeDragDoesNotCountVirtualHeadersAsQueueItems() {
        queue.hierarchyModel = treeFixture
        wait(50)
        const handle = findChild(queue, "queueMoveHandle_0")
        mouseDrag(handle, handle.width / 2, handle.height / 2, 0, 88)
        compare(moveSpy.count, 1)
        compare(Array.from(moveSpy.signalArguments[0]), [0, 3])
        compare(media.get(2).title, "First"); compare(playSpy.count, 0)
    }
    function test_singleClickSelectsWithoutPlaying() {
        const button = findChild(queue, "queueItem_0")
        mouseClick(button)
        compare(queue.selectedIndex, 0)
        compare(playSpy.count, 0)
        wait(500)
        mouseDoubleClickSequence(button)
        compare(playSpy.count, 1)
        queue.singleClickActivate = true
        wait(500)
        mouseClick(button)
        compare(playSpy.count, 2)
        queue.singleClickActivate = false
    }
    function test_summaryAndRemoveUseCurrentThenSelectedItem() {
        // VLC's translator uses the disambiguation string for plurals.
        // Standalone Qt has no VLC translator, so check the count and duration here.
        verify(findChild(queue, "queueSummary").text.startsWith("3 "))
        verify(findChild(queue, "queueSummary").text.endsWith(" - 1h 12m"))
        mouseClick(findChild(queue, "queueRemoveButton"))
        compare(removeSpy.signalArguments[0][0], 1)
        compare(media.get(1).title, "Third")
        mouseDoubleClickSequence(findChild(queue, "queueItem_0"))
        compare(playSpy.signalArguments[0][0], 0)
        mouseClick(findChild(queue, "queueRemoveButton"))
        compare(media.get(0).title, "Third")
        compare(findChild(queue, "queueSummary").text, "1 item - 1h 12m")
    }
    function test_dragReordersWithoutPlayingAndHandlesDownwardInsertion() {
        const handle = findChild(queue, "queueMoveHandle_0")
        mouseDrag(handle, handle.width / 2, handle.height / 2, 0, 88)
        compare(moveSpy.count, 1)
        compare(Array.from(moveSpy.signalArguments[0]), [0, 3])
        compare(media.get(0).title, "Second")
        compare(media.get(2).title, "First")
        compare(playSpy.count, 0)
    }
    function test_listKeepsOrdinaryArrowNavigation() {
        const list = findChild(queue, "queueList")
        list.currentIndex = 0
        list.forceActiveFocus()
        keyClick(Qt.Key_Down)
        compare(list.currentIndex, 1)
        compare(moveSpy.count, 0)
        compare(media.get(0).title, "First")
    }
    function test_keyboardMoveAndDeleteKeepSelectionOnMovedItem() {
        mouseClick(findChild(queue, "queueMoveHandle_0"))
        compare(queue.selectedIndex, 0)
        verify(queue.activeFocus)
        findChild(queue, "queueList").forceActiveFocus()
        keyClick(Qt.Key_Down, Qt.AltModifier)
        compare(media.get(1).title, "First")
        compare(queue.selectedIndex, 1)
        keyClick(Qt.Key_Delete)
        compare(media.get(0).title, "Second")
        compare(media.get(1).title, "Third")
        compare(playSpy.count, 0)
    }
    function test_hiddenQueueCancelsRowDrag() {
        const handle = findChild(queue, "queueMoveHandle_0")
        mousePress(handle, handle.width / 2, handle.height / 2)
        mouseMove(handle, handle.width / 2, handle.height / 2 + 88, 50)
        queue.visible = false
        mouseRelease(handle)
        compare(moveSpy.count, 0)
        compare(media.get(0).title, "First")
    }
    function test_reverseWithoutSortStartsWithFirstNativeCriterion() {
        queue.sortKey = 999
        mouseClick(findChild(queue, "queueSortButton"))
        const menu = findChild(queue, "queueSortMenu")
        tryCompare(menu, "opened", true); wait(200)
        mouseClick(findChild(menu, "libraryAction_reverse"))
        compare(Array.from(sortSpy.signalArguments[0]), [11, true])
    }
    function test_sortMenuAnchorsAndToggles() {
        const button = findChild(queue, "queueSortButton")
        const menu = findChild(queue, "queueSortMenu")
        mouseMove(testCase, 10, 580)
        button.forceActiveFocus(); keyClick(Qt.Key_Space)
        tryCompare(menu, "opened", true)
        const point = menu.background.mapToItem(testCase, 0, 0)
        compare(point.y, button.mapToItem(testCase, 0, button.height).y)
        verify(point.x <= button.mapToItem(testCase, 0, 0).x + button.width)
        mouseClick(button)
        tryCompare(menu, "visible", false)
    }
    QtObject {
        id: fakeAddonRegistry
        property int revision: 0
        property var triggered: []
        readonly property string icon: "<path d=\"M4 6h16\" fill=\"none\" stroke=\"COLOR\" stroke-width=\"1.5\"/>"
        property var commands: [
            {id: "org.example.cleaner/sweep", name: "Cleaner", iconMarkup: icon, document: {hook: "queue.actions@1", label: "Clean up the queue"}},
            {id: "org.example.cleaner/other", name: "Other", iconMarkup: icon, document: {hook: "queue.item.actions@1", label: "Other"}},
            {id: "org.example.cleaner/plain", name: "Plain", iconMarkup: "", document: {hook: "queue.actions@1", label: "No icon"}}]
        function triggerCommand(id, args) { triggered = triggered.concat([id]); return true }
    }
    function test_addonQueueButtonOpensItsCommand() {
        queue.addonRegistry = fakeAddonRegistry
        try {
            const button = findChild(queue, "queueAddonButton")
            verify(button)
            // Only queue.actions@1 commands with an icon get a header button.
            compare(queue.addonButtons.length, 1)
            compare(button.text, "Clean up the queue")
            compare(button.tooltipTextFormat, Text.PlainText)
            verify(button.iconMarkup.length > 0)
            mouseClick(button)
            compare(fakeAddonRegistry.triggered, ["org.example.cleaner/sweep"])
        } finally {
            queue.addonRegistry = null
        }
    }
    function test_rightClickSelectsRowAtClickPosition_data() {
        return [{tag: "first", index: 0}, {tag: "scrolled", index: 12}]
    }
    function test_rightClickSelectsRowAtClickPosition(data) {
        for (let index = 3; index < 30; ++index) media.append({title: "Item " + index, isCurrent: false})
        const list = findChild(queue, "queueList")
        list.forceLayout()
        list.positionViewAtIndex(data.index, ListView.Beginning)
        wait(50)
        const row = findChild(queue, "queueItem_" + data.index)
        const point = row.mapToItem(testCase, 60, row.height / 2)
        mouseClick(row, 60, row.height / 2, Qt.RightButton)
        const menu = findChild(queue, "queueActionsMenu")
        tryCompare(menu, "opened", true)
        compare(queue.selectedIndex, data.index)
        const origin = menu.background.mapToItem(testCase, 0, 0)
        compare(origin.x, point.x); compare(origin.y, point.y)
        compare(playSpy.count, 0)
    }
    function test_keyboardContextScrollsToSelectedRow() {
        for (let index = 3; index < 30; ++index) media.append({title: "Item " + index, isCurrent: false})
        queue.selectedIndex = 25
        queue.forceActiveFocus(); keyClick(Qt.Key_F10, Qt.ShiftModifier)
        const menu = findChild(queue, "queueActionsMenu")
        const list = findChild(queue, "queueList")
        tryCompare(menu, "opened", true)
        verify(list.contentY > 0)
        const row = findChild(queue, "queueItem_25")
        verify(row !== null)
        const point = row.mapToItem(list, 0, 0)
        verify(point.y >= 0 && point.y + row.height <= list.height)
        compare(menu.parent, list)
        compare(playSpy.count, 0)
    }
    function test_sortAndReverseUseSelectedNativeCriterion() {
        mouseClick(findChild(queue, "queueSortButton"))
        const menu = findChild(queue, "queueSortMenu")
        tryCompare(menu, "opened", true); wait(200)
        mouseClick(findChild(menu, "libraryAction_22"))
        compare(Array.from(sortSpy.signalArguments[0]), [22, false])
        tryCompare(menu, "opened", false)
        mouseClick(findChild(queue, "queueSortButton"))
        tryCompare(menu, "opened", true); wait(200)
        verify(findChild(menu, "libraryAction_22").checked)
        mouseClick(findChild(menu, "libraryAction_reverse"))
        compare(Array.from(sortSpy.signalArguments[1]), [22, true])
    }
    function test_contextActionsSaveAndClearAndEmptyRemovalIsDisabled() {
        queue.forceActiveFocus(); keyClick(Qt.Key_Menu)
        const menu = findChild(queue, "queueActionsMenu")
        tryCompare(menu, "opened", true); wait(200)
        mouseClick(findChild(menu, "libraryAction_save"))
        compare(saveSpy.count, 1)
        tryCompare(menu, "opened", false)
        queue.forceActiveFocus(); keyClick(Qt.Key_Menu)
        tryCompare(menu, "opened", true); wait(200)
        mouseClick(findChild(menu, "libraryAction_clear"))
        compare(clearSpy.count, 1)
        media.clear()
        verify(!findChild(queue, "queueRemoveButton").enabled)
        queue.removeSelection(); compare(removeSpy.count, 0)
    }
}
