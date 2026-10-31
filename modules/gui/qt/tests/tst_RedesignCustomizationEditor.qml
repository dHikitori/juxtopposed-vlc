// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Window
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "JuxtopposedCustomizationEditor"
    width: 1920; height: 1080; visible: true; when: windowShown
    RedesignPlayerView {
        id: player; anchors.fill: parent; started: true; hasVideo: true
        mediaTitle: "The Wand S01E11"; mediaFolderTitle: "S01"
        timeText: "00:02:10"; lengthText: "00:25:40"
    }
    RedesignCustomizationEditor { id: editor; player: player }
    SignalSpy { id: toggled; target: player; signalName: "togglePlayback" }
    SignalSpy { id: saved; target: editor; signalName: "saveRequested" }
    function item(name) {
        const found = findChild(editor, name)
        if (found) return found
        const p = findChild(editor, "customizationPalette")
        if (p) for (let n = 0; n < p.count; ++n) {
            const cell = p.itemAtIndex(n)
            if (cell && cell.objectName === name) return cell
        }
        return null
    }
    function init() {
        editor.saving = false; editor.cancel(); editor.presetId = ""
        player.playerLayout = ""; player.layoutModel.reload()
        player.scaleFactor = 1; player.lightTheme = false; player.fullscreen = false; player.showFullscreenControls = true
        testCase.x = 0; testCase.y = 0
        testCase.width = 1920; testCase.height = 1080
        player.customControlStates = ({})
        saved.clear()
    }
    function cleanup() { editor.saving = false; editor.cancel() }
    function test_initialGeometryAndPalette() {
        editor.begin(); tryCompare(editor, "opened", true)
        const panel = item("customizationPanel")
        compare(panel.width, 1048); compare(panel.height, 655)
        compare(panel.x, 436); compare(panel.y, 189.5)
        compare(editor.available.length, 31)
        verify(!item("customizationSave").enabled)
        const cancel = item("customizationCancel"), save = item("customizationSave")
        compare(cancel.x, 20); compare(cancel.y, 603); compare(cancel.width, 100); compare(cancel.height, 32)
        compare(save.x, 928); compare(save.y, 603)
        const palette = item("customizationPalette")
        compare(Math.round(palette.width / palette.cellWidth), 9)
        tryVerify(() => item("customizeHandle_timeline") !== null)
    }
    function test_paletteUsesReferenceOutlineBounds() {
        editor.begin()
        for (const row of [["spacer", 24, 32, 318], ["divider", 24, 32, 318],
                           ["speed", 30, 32, 318], ["forward15", 28, 28, 318],
                           ["rightSidebar", 32, 32, 438], ["home", 28, 28, 558],
                           ["favorite", 28, 28, 674], ["bookmark", 30, 32, 674]]) {
            tryVerify(() => item("customizePalette_" + row[0]) !== null)
            const outline = findChild(item("customizePalette_" + row[0]), "customizePaletteOutline_" + row[0])
            verify(outline !== null)
            compare(outline.width, row[1]); compare(outline.height, row[2])
            fuzzyCompare(outline.mapToItem(editor, 0, 0).y, row[3], 0.6)
        }
    }
    Component { id: sceneOutlineReference; Image { z: 100000; sourceSize: Qt.size(width * Screen.devicePixelRatio, height * Screen.devicePixelRatio) } }
    function test_outlinePaintMatchesSceneVector_data() {
        return [1, 1.125, 1.25, 1.5, 2].map(scale => ({tag: String(scale), scale: scale}))
            .concat([{tag: "light", scale: 1.125, light: true}, {tag: "translated", scale: 1.125, translated: true}])
    }
    function test_outlinePaintMatchesSceneVector(data) {
        player.lightTheme = !!data.light
        player.scaleFactor = data.scale
        editor.begin()
        tryVerify(() => item("customizeOutline_menu") !== null)
        const outline = item("customizeOutline_menu")
        let scene = testCase
        while (scene.parent) scene = scene.parent
        tryCompare(outline, "width", 28 * data.scale)
        verify(waitForPolish(player))
        scene.Window.window.update()
        verify(waitForRendering(scene))
        if (data.translated) {
            testCase.x = 0.25; testCase.y = 0.5
            scene.Window.window.update()
            verify(waitForRendering(scene))
        }
        const origin = outline.mapToItem(scene, 0, 0)
        // Paint the original 28px source rect directly in scene coordinates.
        // This oracle has no knowledge of the production image's raster bounds.
        const svg = '<svg xmlns="http://www.w3.org/2000/svg" width="' + scene.width
            + '" height="' + scene.height + '" viewBox="0 0 ' + scene.width + ' ' + scene.height + '">'
            + '<g transform="translate(' + origin.x + ' ' + origin.y + ') scale(' + data.scale + ')">'
            + '<rect x="0.5" y="0.5" width="27" height="27" fill="none" stroke="' + outline.stroke + '" stroke-dasharray="4 4"/></g></svg>'
        scene.Window.window.update()
        verify(waitForRendering(scene))
        const actual = testImages.grabWindow(scene)
        let reference = null
        try {
            outline.visible = false
            reference = sceneOutlineReference.createObject(scene, {width: scene.width, height: scene.height,
                source: "data:image/svg+xml;utf8," + encodeURIComponent(svg)})
            verify(reference !== null)
            tryCompare(reference, "status", Image.Ready)
            scene.Window.window.update()
            verify(waitForRendering(scene))
            const expected = testImages.grabWindow(scene)
            const first = actual.point(scene, origin.x, origin.y)
            const last = actual.point(scene, origin.x + outline.width, origin.y + outline.height)
            const left = first.x, top = first.y
            const right = last.x + 1, bottom = last.y + 1
            for (let y = top; y < bottom; ++y) for (let x = left; x < right; ++x) {
                verify(Math.abs(actual.red(x, y) - expected.red(x, y)) <= 1
                    && Math.abs(actual.green(x, y) - expected.green(x, y)) <= 1
                    && Math.abs(actual.blue(x, y) - expected.blue(x, y)) <= 1,
                    "Outline differs from the scene vector at " + x + "," + y
                        + ": " + actual.red(x, y) + "," + actual.green(x, y) + "," + actual.blue(x, y)
                        + " expected " + expected.red(x, y) + "," + expected.green(x, y) + "," + expected.blue(x, y))
            }
        } finally {
            if (reference) { reference.visible = false; reference.destroy(); wait(0) }
            outline.visible = true
            testCase.x = 0; testCase.y = 0
            player.scaleFactor = 1
            verify(waitForPolish(player))
            scene.Window.window.update()
            verify(waitForRendering(scene))
        }
    }
    function outlineMatches(id, x, y, w, h, scale) {
        tryVerify(() => {
            const outline = item("customizeOutline_" + id)
            if (!outline) return false
            // Qt rounds each layout item; the right group accumulates those
            // differences and the stretched timeline absorbs the remainder.
            const p = outline.mapToItem(editor, 0, 0), tolerance = scale === 1 ? 0.6 : 6
            const widthTolerance = id === "timeline" && scale !== 1 ? 8 : tolerance
            return Math.abs(p.x - x * scale) <= tolerance && Math.abs(p.y - y * scale) <= tolerance
                && Math.abs(outline.width - w * scale) <= widthTolerance && Math.abs(outline.height - h * scale) <= tolerance
        }, 5000, "reference outline " + id)
    }
    function test_referenceOutlineBounds_data() {
        return [{tag: "normal", scale: 1}, {tag: "fractional", scale: 1.125}]
    }
    function test_referenceOutlineBounds(data) {
        // Keep the design's logical frame size at both display scales.
        testCase.width = 1920 * data.scale; testCase.height = 1080 * data.scale
        player.scaleFactor = data.scale
        editor.begin(); tryCompare(player, "layoutReady", true)
        for (const row of [["menu", 51, 6, 28, 28], ["library", 89, 6, 28, 28],
                           ["divider:1", 117, 6, 24, 28],
                           ["minimize", 1770, 8, 36, 24], ["close", 1878, 8, 36, 24],
                           ["previous", 20, 1039, 30, 28], ["play", 58, 1036, 54, 34],
                           ["next", 120, 1039, 30, 28], ["timeline", 170, 1035, 1364, 36],
                           ["volume", 1549, 1035, 130, 36], ["audio", 1694, 1037, 30, 32],
                           ["subtitles", 1736, 1037, 30, 32], ["settings", 1778, 1037, 30, 32], ["addon:org.videolan.episodes/episodes", 1828, 1037, 30, 32],
                           ["queue", 1870, 1037, 30, 32]])
            outlineMatches(...row, data.scale)
        // Caption width follows the application's font and translated text.
        outlineMatches("folder", 141, 6, player.chromeItems.folder.width / data.scale - 9, 28, data.scale)
        verify(!item("customizeHandle_mute").visible)
        const menu = item("customizeHandle_menu"), outline = item("customizeOutline_menu")
        verify(menu.width > outline.width); verify(menu.height > outline.height)
        player.layoutModel.move("timeline", "above", 0)
        outlineMatches("timeline", 20, 981, 1880, 36, data.scale)
        for (const id of ["previous", "play", "next"])
            player.layoutModel.move(id, "footerCenter", player.layoutModel.layout.zones.footerCenter.length)
        outlineMatches("previous", 895, 1039, 30, 28, data.scale)
        outlineMatches("play", 933, 1036, 54, 34, data.scale)
        outlineMatches("next", 995, 1039, 30, 28, data.scale)
        player.layoutModel.move("fullscreen", "footerRight", 7)
        outlineMatches("volume", 1498, 1035, 130, 36, data.scale)
        outlineMatches("audio", 1643, 1037, 30, 32, data.scale)
        outlineMatches("fullscreen", 1864, 1041, 36, 24, data.scale)
        for (const id of ["menu", "library", "divider:1", "folder", "title"])
            player.layoutModel.move(id, "footerLeft", player.layoutModel.layout.zones.footerLeft.length)
        outlineMatches("menu", 21, 1039, 28, 28, data.scale)
        outlineMatches("library", 59, 1039, 28, 28, data.scale)
        outlineMatches("divider:1", 87, 1039, 24, 28, data.scale)
    }
    function test_volumePairMovesRemovesAndReturnsWithRealPointer() {
        editor.begin(); tryCompare(editor, "groupedVolume", true)
        dragFromTo(item("customizeHandle_volume"), width / 2, height - 80)
        tryCompare(player.layoutModel, "layout", player.layoutModel.draft)
        tryVerify(() => JSON.stringify(player.layoutModel.layout.zones.above) === JSON.stringify(["mute", "volume"]))
        verify(editor.groupedVolume)
        const panel = item("customizationPanel")
        dragFromTo(item("customizeHandle_volume"), panel.x + 80, panel.y + 150)
        tryVerify(() => !player.layoutModel.location("mute") && !player.layoutModel.location("volume"))
        verify(editor.available.some(row => row.id === "volume"))
        verify(!editor.available.some(row => row.id === "mute"))
        const palette = item("customizationPalette")
        const row = editor.available.findIndex(row => row.id === "volume")
        palette.positionViewAtIndex(row, GridView.Contain)
        tryVerify(() => item("customizePalette_volume") !== null)
        dragFromTo(item("customizePalette_volume"), width - 5, height - 25)
        tryVerify(() => editor.groupedVolume)
        compare(player.layoutModel.layout.zones.footerRight.slice(-2), ["mute", "volume"])
    }
    function test_legacySeparatedVolumeControlsStayIndependent() {
        const layout = player.layoutModel.defaults()
        layout.zones.footerRight.splice(0, 1); layout.zones.headerLeft.push("mute")
        player.playerLayout = JSON.stringify(layout)
        editor.begin(); tryVerify(() => item("customizeHandle_mute") !== null)
        verify(!editor.groupedVolume); tryVerify(() => item("customizeHandle_mute").visible)
        editor.queueRemove("volume")
        tryVerify(() => !player.layoutModel.location("volume"))
        compare(player.layoutModel.location("mute").zone, "headerLeft")
        editor.queueMove("volume", "above", 0)
        tryVerify(() => player.layoutModel.location("volume") !== null)
        compare(player.layoutModel.layout.zones.above, ["volume"])
    }
    function test_narrowPlayerKeepsVolumePairTogether() {
        testCase.width = 700; testCase.height = 600
        editor.begin(); verify(editor.pairedVolume); verify(!editor.groupedVolume)
        tryVerify(() => item("customizeHandle_mute").visible)
        editor.queueMove("mute", "above", 0)
        tryVerify(() => JSON.stringify(player.layoutModel.layout.zones.above) === JSON.stringify(["mute", "volume"]))
    }
    function test_cancelCloseAndEscapeRestore() {
        for (const method of ["cancel", "close", "escape"]) {
            editor.begin(); player.layoutModel.move("favorite", "footerLeft", 3)
            tryVerify(() => item("customizeHandle_favorite") !== null)
            verify(item("customizationSave").enabled)
            if (method === "cancel") mouseClick(item("customizationCancel"))
            else if (method === "close") mouseClick(item("customizationClose"))
            else keyClick(Qt.Key_Escape)
            tryCompare(editor, "opened", false)
            compare(player.layoutModel.location("favorite"), null)
            compare(player.controlsHeight, 54)
        }
    }
    function test_saveFailureKeepsDraftAndSuccessfulEchoCloses() {
        editor.begin(); player.layoutModel.move("timeline", "above", 0)
        tryCompare(player, "controlsHeight", 108)
        mouseClick(item("customizationSave")); compare(saved.count, 1); verify(editor.saving)
        verify(!item("customizationCancel").enabled)
        editor.completeSave(false, "Cannot save this file.")
        verify(editor.opened); verify(player.layoutModel.editing); compare(editor.errorMessage, "Cannot save this file.")
        compare(player.layoutModel.location("timeline").zone, "above")
        mouseClick(item("customizationSave")); compare(saved.count, 2)
        // MainCtx echoes persisted preferences before the save callback returns.
        player.playerLayout = saved.signalArguments[1][0]
        verify(editor.opened)
        editor.completeSave(true, "")
        tryCompare(editor, "opened", false)
        compare(player.layoutModel.location("timeline").zone, "above")
    }
    function test_externalProfileChangeCancelsDraft() {
        editor.begin(); player.layoutModel.move("favorite", "footerLeft", 3)
        const other = player.layoutModel.defaults(); other.zones.above = ["timeline"]; other.zones.footerCenter = []
        player.playerLayout = JSON.stringify(other)
        tryCompare(editor, "opened", false)
        compare(player.layoutModel.location("favorite"), null)
        compare(player.layoutModel.location("timeline").zone, "above")
    }
    function dragFromTo(source, targetX, targetY) {
        tryCompare(player, "layoutReady", true)
        tryVerify(() => source.visible && source.width > 0 && source.height > 0)
        player.Window.window.update()
        verify(waitForRendering(source))
        const start = source.mapToItem(editor, source.width / 2, source.height / 2)
        mousePress(editor, start.x, start.y)
        mouseMove(editor, start.x + 10, start.y + 10, 20)
        for (let n = 1; n <= 12; ++n)
            mouseMove(editor, start.x + (targetX - start.x) * n / 12, start.y + (targetY - start.y) * n / 12, 10)
        wait(20)
        mouseRelease(editor, targetX, targetY)
        wait(20)
    }
    function test_pointerOutlinesStayGrayAndKeyboardFocusIsVisible() {
        for (const id of ["menu", "spacer"]) {
            editor.begin()
            const controlName = (id === "menu" ? "customizeHandle_" : "customizePalette_") + id
            tryVerify(() => item(controlName) !== null)
            const control = item(controlName)
            const outline = findChild(control, (id === "menu" ? "customizeOutline_" : "customizePaletteOutline_") + id)
            control.forceActiveFocus(Qt.TabFocusReason)
            tryCompare(outline, "stroke", player.theme.accent)
            mousePress(control, control.width / 2, control.height / 2)
            tryCompare(outline, "stroke", player.theme.playerLabel)
            mouseRelease(control, control.width / 2, control.height / 2)
            editor.cancel()
            tryCompare(editor, "opened", false)
        }
    }
    function test_realPointerAddRemoveAndMoveTimeline() {
        editor.begin(); tryVerify(() => item("customizePalette_favorite") !== null)
        dragFromTo(item("customizePalette_favorite"), 180, height - 25)
        tryVerify(() => !!player.layoutModel.location("favorite"))
        verify(!editor.available.some(row => row.id === "favorite"))
        const panel = item("customizationPanel")
        dragFromTo(item("customizeHandle_favorite"), panel.x + 80, panel.y + 150)
        tryCompare(player.layoutModel, "dirty", false)
        tryVerify(() => item("customizeHandle_timeline") !== null)
        dragFromTo(item("customizeHandle_timeline"), width / 2, height - 80)
        tryCompare(player, "controlsHeight", 108)
        compare(player.layoutModel.location("timeline").zone, "above")
    }
    function test_referenceSequenceAndReorder() {
        editor.begin()
        tryVerify(() => item("customizeHandle_timeline") !== null)
        dragFromTo(item("customizeHandle_timeline"), width / 2, height - 80)
        tryCompare(player, "controlsHeight", 108)
        for (const id of ["previous", "play", "next"])
            dragFromTo(item("customizeHandle_" + id), width / 2 + 100, height - 25)
        tryCompare(player.layoutModel, "draft", player.layoutModel.layout)
        compare(player.layoutModel.layout.zones.footerCenter, ["previous", "play", "next"])
        dragFromTo(item("customizeHandle_title"), 20, height - 25)
        tryVerify(() => player.layoutModel.location("title").zone === "footerLeft")
        for (const id of ["menu", "library", "folder"])
            dragFromTo(item("customizeHandle_" + id), 1, height - 25)
        tryVerify(() => player.layoutModel.location("folder").zone === "footerLeft")
        dragFromTo(item("customizeHandle_fullscreen"), width - 20, height - 25)
        tryVerify(() => player.layoutModel.location("fullscreen").zone === "footerRight")
        dragFromTo(item("customizePalette_favorite"), 20, height - 25)
        tryVerify(() => player.layoutModel.location("favorite") !== null)
        const before = player.layoutModel.layout.zones.footerLeft.slice()
        const moved = before[before.length - 1]
        dragFromTo(item("customizeHandle_" + moved), 0, height - 25)
        tryVerify(() => player.layoutModel.layout.zones.footerLeft[0] === moved)
        mouseClick(item("customizationSave"))
        editor.completeSave(true, "")
        tryCompare(editor, "opened", false)
        verify(player.layoutModel.location("favorite") !== null)
        compare(player.controlsHeight, 108)
    }
    Component { id: titlebarProbe; Item { objectName: "windowDragProbe" } }
    function test_editingStopsNativeWindowDrag() {
        player.titleBarDragContent = titlebarProbe
        tryVerify(() => findChild(player, "windowDragProbe") !== null)
        editor.begin(); tryVerify(() => findChild(player, "windowDragProbe") === null)
        editor.cancel(); tryVerify(() => findChild(player, "windowDragProbe") !== null)
        player.titleBarDragContent = null
    }
    function test_editingRevealsControlsInFullscreen() {
        player.fullscreen = true; player.showFullscreenControls = false
        verify(!player.bottomVisible)
        editor.begin(); verify(player.topVisible); verify(player.bottomVisible)
        editor.cancel(); verify(!player.bottomVisible)
    }
    function test_profileSwitchWithSameLayoutCancelsDraft() {
        editor.presetId = "first"; editor.begin()
        player.layoutModel.move("favorite", "footerLeft", 0)
        editor.presetId = "second"
        tryCompare(editor, "opened", false)
        compare(player.layoutModel.location("favorite"), null)
    }
    function test_staleDropDoesNotModifyAnotherDraft() {
        editor.begin()
        editor.queueMove("favorite", "footerLeft", 0)
        editor.cancel(); editor.begin(); wait(20)
        compare(player.layoutModel.location("favorite"), null)
        verify(!player.layoutModel.dirty)
    }
    function test_thinDividerHasUsableDragArea() {
        editor.begin(); tryVerify(() => item("customizeHandle_divider:1") !== null)
        const handle = item("customizeHandle_divider:1")
        verify(handle.width >= 12); verify(handle.height >= 28)
        dragFromTo(handle, 20, height - 25)
        tryVerify(() => player.layoutModel.location("divider:1").zone === "footerLeft")
    }
    function test_disabledControlsStillHaveActiveDragHandles() {
        player.customControlStates = ({snapshot: {enabled: false}})
        editor.begin(); player.layoutModel.move("snapshot", "headerLeft", 4)
        tryVerify(() => item("customizeHandle_snapshot") !== null)
        verify(item("customizeHandle_snapshot").enabled)
        verify(!findChild(player, "customButton_snapshot").enabled)
        dragFromTo(item("customizeHandle_snapshot"), width / 2, height - 25)
        tryVerify(() => player.layoutModel.location("snapshot").zone === "footerCenter")
        player.customControlStates = ({})
    }
    function test_keyboardCanPlaceAndRemoveControls() {
        editor.begin(); tryVerify(() => item("customizePalette_favorite") !== null)
        item("customizePalette_favorite").forceActiveFocus(); keyClick(Qt.Key_Return)
        // The first menu row is Top left.
        tryCompare(item("customizationPlacement"), "currentIndex", 0); keyClick(Qt.Key_Return)
        tryVerify(() => player.layoutModel.location("favorite") !== null)
        tryVerify(() => item("customizeHandle_favorite") !== null)
        item("customizeHandle_favorite").forceActiveFocus(); keyClick(Qt.Key_Return)
        tryCompare(item("customizationPlacement"), "currentIndex", 0)
        for (let n = 0; n < 7; ++n) keyClick(Qt.Key_Down)
        compare(item("customizationPlacement").currentIndex, 7); keyClick(Qt.Key_Return)
        tryCompare(player.layoutModel, "dirty", false)
    }
    function test_closingEditorRestoresPlayerPointerInput() {
        editor.begin(); editor.cancel(); toggled.clear()
        verify(waitForRendering(player))
        mouseClick(findChild(player, "playButton"))
        tryCompare(toggled, "count", 1)
    }
    function test_smallPaletteRevealsKeyboardFocus() {
        testCase.width = 800; testCase.height = 600
        editor.begin()
        const palette = item("customizationPalette")
        tryVerify(() => palette.itemAtIndex(palette.count - 1) !== null)
        const last = palette.itemAtIndex(palette.count - 1)
        last.forceActiveFocus()
        tryVerify(() => last.mapToItem(palette, 0, 0).y >= -1
                  && last.mapToItem(palette, 0, last.height).y <= palette.height + 1)
        // Scrolling to the last row can release the first delegate.
        for (let n = 0; n < 5; ++n) keyClick(Qt.Key_Up)
        compare(palette.currentIndex, 0)
        tryVerify(() => Math.abs(palette.contentY) < 1)
    }
    function test_smallPaletteArrowKeysReachLastRow() {
        testCase.width = 800; testCase.height = 600
        editor.begin()
        const palette = item("customizationPalette")
        tryVerify(() => palette.itemAtIndex(0) !== null)
        palette.itemAtIndex(0).forceActiveFocus(Qt.TabFocusReason)
        for (let n = 0; n < 5; ++n) keyClick(Qt.Key_Down)
        compare(palette.currentIndex, 30)
        const last = palette.itemAtIndex(30)
        verify(last !== null && last.activeFocus)
        tryCompare(last, "visualFocus", true)
        verify(last.mapToItem(palette, 0, last.height).y <= palette.height + 1)
        keyClick(Qt.Key_Return)
        tryCompare(item("customizationPlacement"), "currentIndex", 0)
        for (let n = 0; n < 3; ++n) keyClick(Qt.Key_Down)
        keyClick(Qt.Key_Return)
        tryVerify(() => player.layoutModel.location("playlists") !== null)
    }
    function test_paletteKeepsFocusVisibleAfterResize() {
        editor.begin()
        const palette = item("customizationPalette")
        tryVerify(() => palette.itemAtIndex(30) !== null)
        palette.itemAtIndex(30).forceActiveFocus()
        testCase.width = 800; testCase.height = 600
        tryVerify(() => palette.currentItem && palette.currentItem.activeFocus
                  && palette.currentItem.mapToItem(palette, 0, palette.currentItem.height).y <= palette.height + 1)
        player.scaleFactor = 1.5
        // A tall row cannot fit here. Keep its focus outline at the top.
        tryVerify(() => palette.currentItem && palette.currentItem.activeFocus
                  && palette.currentItem.mapToItem(palette, 0, 0).y >= -1
                  && palette.currentItem.mapToItem(palette, 0, 46 * player.scaleFactor).y <= palette.height + 1)
    }
    function test_overlayFollowsResizeWhileClosedAndOpen() {
        testCase.width = 1528; testCase.height = 894
        tryCompare(editor, "width", 1528); tryCompare(editor, "height", 894)
        testCase.width = 1920; testCase.height = 1080
        tryCompare(editor, "width", 1920); tryCompare(editor, "height", 1080)
        editor.begin()
        testCase.width = 800; testCase.height = 600
        tryCompare(editor, "width", 800); tryCompare(editor, "height", 600)
        editor.cancel()
    }
    function test_smallWindowAndScale_data() {
        return [{tag: "small", w: 800, h: 600, scale: 1}, {tag: "dpi", w: 1600, h: 1000, scale: 1.5}, {tag: "light", w: 1920, h: 1080, scale: 1}]
    }
    function test_smallWindowAndScale(data) {
        testCase.width = data.w; testCase.height = data.h; player.scaleFactor = data.scale; player.lightTheme = data.tag === "light"
        editor.begin(); tryCompare(editor, "opened", true)
        const panel = item("customizationPanel")
        verify(panel.x >= 0 && panel.y >= 40 * data.scale); verify(panel.x + panel.width <= width); verify(panel.y + panel.height <= height - player.controlsHeight)
        const palette = item("customizationPalette")
        verify(palette.height > 0); verify(palette.cellWidth > 0)
        if (data.tag === "small") verify(palette.contentHeight > palette.height)
    }
}
