// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "JuxtopposedPlayerChrome"
    width: 1920; height: 1080
    visible: true; when: windowShown
    RedesignPlayerView {
        id: player
        anchors.fill: parent
        started: true; hasVideo: true; seekable: true
        hasPrevious: true; hasNext: true
        timeText: "00:02:10"; lengthText: "00:25:40"
        mediaTitle: "The Wand S01E11"; mediaFolderTitle: "S01"
        videoContent: Rectangle { color: "#2d2b29" }
    }
    SignalSpy { id: playSpy; target: player; signalName: "togglePlayback" }
    SignalSpy { id: volumeSpy; target: player; signalName: "volumeRequested" }
    SignalSpy { id: fullscreenSpy; target: player; signalName: "fullscreenRequested" }
    SignalSpy { id: closeSpy; target: player; signalName: "closeRequested" }
    function item(name) { return findChild(player, name) }
    function point(name) { return item(name).mapToItem(player, 0, 0) }
    function settledDefault() {
        tryVerify(() => Math.abs(point("seekBar").x - 254) < 1
                  && Math.abs(item("seekBar").width - 1238) < 1
                  && item("mainMenuButton").parent.objectName !== "customHeaderLeft")
    }
    function init() {
        player.playerLayout = ""; player.layoutModel.reload()
        player.scaleFactor = 1; player.lightTheme = false
        player.libraryOpen = false; player.libraryExpanded = false
        player.addonPanels = []; player.panelOpen = ""; player.settingsPage = ""
        player.fullscreen = false; player.controlsHideTimeout = 1800
        player.fullscreenControlsOpacity = 1
        player.maximized = false
        player.showWindowButtons = true
        player.customMenuOpen = false
        testCase.width = 1920; testCase.height = 1080
        player.mediaTitle = "The Wand S01E11"; player.mediaFolderTitle = "S01"
        playSpy.clear(); volumeSpy.clear(); fullscreenSpy.clear(); closeSpy.clear()
        settledDefault()
    }
    function alternate() {
        const model = player.layoutModel
        model.begin(); model.move("timeline", "above", 0)
        for (const id of ["previous", "play", "next"]) model.move(id, "footerCenter", model.layout.zones.footerCenter.length)
        for (const id of ["menu", "library", "divider:1", "folder", "title"]) model.move(id, "footerLeft", model.layout.zones.footerLeft.length)
        model.move("fullscreen", "footerRight", model.layout.zones.footerRight.length)
        tryVerify(() => item("mainMenuButton").parent.objectName === "customFooterLeft")
        tryVerify(() => Math.abs(point("seekBar").x - 104) < 1 && Math.abs(point("seekBar").y - 986) < 1)
    }
    function test_defaultEpisodesButtonSitsBetweenSettingsAndQueue() {
        verify(!item("episodesButton").visible)
        player.addonPanels = [{id: "org.videolan.episodes/episodes", name: "Episodes", label: "Show the other episodes", available: true,
            iconMarkup: "<path d=\"M4 6h16\" stroke=\"COLOR\"/>"}]
        compare(player.layoutModel.layout.zones.footerRight.slice(-3), ["settings", "addon:org.videolan.episodes/episodes", "queue"])
        compare(item("episodesButton").iconMarkup, "<path d=\"M4 6h16\" stroke=\"COLOR\"/>")
        tryVerify(() => item("episodesButton").visible
            && point("playbackSettingsButton").x + item("playbackSettingsButton").width <= point("episodesButton").x
            && point("episodesButton").x + item("episodesButton").width <= point("queueButton").x)
    }
    function test_settingsButtonClosesEverySettingsPage_data() {
        return ["playback", "video", "audio", "subtitles"].map(page => ({tag: page, page: page}))
    }
    function test_settingsButtonClosesEverySettingsPage(data) {
        const button = item("playbackSettingsButton")
        player.settingsPage = data.page
        verify(button.selected)
        mouseClick(button)
        compare(player.settingsPage, "")
        verify(!button.selected)
        mouseClick(button)
        compare(player.settingsPage, "playback")
        mouseClick(button)
        compare(player.settingsPage, "")
    }
    function test_alternateNavigationUsesReferenceDividerSpacing_data() {
        return [{tag: "normal", scale: 1}, {tag: "fractional", scale: 1.125}]
    }
    function test_alternateNavigationUsesReferenceDividerSpacing(data) {
        alternate()
        player.scaleFactor = data.scale
        // Qt layouts round each control independently at fractional scales.
        const tolerance = data.scale === 1 ? 1 : 3
        const divider = player.chromeItems["divider:1"]
        tryVerify(() => Math.abs(divider.mapToItem(player, 0, 0).x - 99 * data.scale) < tolerance)
        const folderIcon = findChild(item("mediaFolderButton"), "redesignButtonIcon")
        verify(folderIcon)
        tryVerify(() => Math.abs(folderIcon.mapToItem(player, 0, 0).x - 113 * data.scale) < tolerance)
        const saved = player.layoutModel.serialize()
        player.layoutModel.finish(true)
        player.playerLayout = ""
        player.layoutModel.reload()
        player.scaleFactor = 1
        settledDefault()
        player.scaleFactor = data.scale
        player.playerLayout = saved
        tryVerify(() => Math.abs(divider.mapToItem(player, 0, 0).x - 99 * data.scale) < tolerance)
        tryVerify(() => Math.abs(folderIcon.mapToItem(player, 0, 0).x - 113 * data.scale) < tolerance)
    }
    function test_fullscreenOpacityBlendsBackgroundWithoutFadingControls_data() {
        return [{tag: "dark quarter", light: false, alpha: 0.25},
                {tag: "light quarter", light: true, alpha: 0.25},
                {tag: "dark solid", light: false, alpha: 1},
                {tag: "light solid", light: true, alpha: 1}]
    }
    function test_fullscreenOpacityBlendsBackgroundWithoutFadingControls(data) {
        player.lightTheme = data.light
        player.fullscreenControlsOpacity = data.alpha
        player.fullscreen = true
        player.controlsHideTimeout = 10000
        player.revealControls()
        tryCompare(player, "topVisible", true); tryCompare(player, "bottomVisible", true)
        const header = item("redesignHeader"), toolbar = item("redesignToolbar")
        fuzzyCompare(header.color.a, data.alpha, 0.0001)
        fuzzyCompare(toolbar.color.a, data.alpha, 0.0001)
        compare(header.opacity, 1); compare(toolbar.opacity, 1); compare(item("playButton").opacity, 1)
        wait(30)
        const image = testImages.grabWindow(player)
        const video = Qt.color("#2d2b29"), chrome = player.theme.background
        const expected = ["r", "g", "b"].map(channel => Math.round(255 * (video[channel] * (1 - data.alpha) + chrome[channel] * data.alpha)))
        for (const y of [2, player.height - 2]) {
            const p = image.point(player, 400, y)
            verify(Math.abs(image.red(p.x, p.y) - expected[0]) <= 2)
            verify(Math.abs(image.green(p.x, p.y) - expected[1]) <= 2)
            verify(Math.abs(image.blue(p.x, p.y) - expected[2]) <= 2)
        }
        player.fullscreen = false
        compare(header.color.a, 1); compare(toolbar.color.a, 1)
    }
    function test_windowButtonsRecoverAfterScaledCustomLayout() {
        alternate(); player.scaleFactor = 1.125
        wait(20)
        player.playerLayout = ""; player.layoutModel.reload(); player.scaleFactor = 1
        settledDefault()
        player.maximized = true
        const close = item("windowCloseButton")
        tryCompare(close, "width", 42); tryCompare(close, "height", 40)
        tryVerify(() => close.mapToItem(player, close.width, 0).x === player.width
                  && close.mapToItem(player, close.width, 0).y === 0)
    }
    function test_closeReachesScreenCornerWithoutMovingIcon_data() {
        return [{tag: "maximized", max: true, scale: 1},
                {tag: "fullscreen", full: true, scale: 1},
                {tag: "scaled", max: true, scale: 1.5}]
    }
    function test_closeReachesScreenCornerWithoutMovingIcon(data) {
        player.scaleFactor = data.scale
        const close = item("windowCloseButton")
        verify(close !== null)
        const icon = findChild(close, "redesignButtonIcon")
        tryVerify(() => close.visible && close.mapToItem(player, close.width, 0).x === player.width - player.theme.dp(6))
        const before = icon.mapToItem(player, 0, 0)
        const titleWidth = item("mediaTitleControl").width
        player.maximized = !!data.max; player.fullscreen = !!data.full
        tryVerify(() => close.mapToItem(player, close.width, 0).x === player.width && close.mapToItem(player, 0, 0).y === 0)
        compare(close.mapToItem(player, close.width, 0).x, player.width)
        compare(close.mapToItem(player, 0, 0).y, 0)
        compare(icon.mapToItem(player, 0, 0), before)
        compare(item("mediaTitleControl").width, titleWidth)
        mouseClick(player, player.width - 1, 0)
        compare(closeSpy.count, 1)
        close.forceActiveFocus(); keyClick(Qt.Key_Space)
        compare(closeSpy.count, 2)
    }
    function test_customCloseCornerFollowsTheActualControl() {
        player.maximized = true
        alternate()
        player.playerLayout = player.layoutModel.serialize()
        tryVerify(() => !player.layoutModel.editing)
        verify(waitForRendering(player))
        mouseClick(player, player.width - 1, 0)
        compare(closeSpy.count, 1)
        player.layoutModel.begin()
        player.layoutModel.move("close", "footerRight", player.layoutModel.layout.zones.footerRight.length)
        player.playerLayout = player.layoutModel.serialize()
        tryVerify(() => item("windowCloseButton").parent.objectName === "customFooterRight")
        mouseClick(player, player.width - 1, 0)
        compare(closeSpy.count, 1)
        mouseClick(item("windowCloseButton"))
        compare(closeSpy.count, 2)
    }
    function test_hiddenWindowButtonsKeepTheirOuterMargin() {
        player.showWindowButtons = false; player.maximized = true
        const fullscreen = item("fullscreenButton")
        tryVerify(() => fullscreen.mapToItem(player, fullscreen.width, 0).x === player.width - player.theme.dp(6))
        mouseClick(player, player.width - 1, 0)
        compare(closeSpy.count, 0)
    }
    function test_referenceAlternateMovesRealControls_data() {
        return [{tag: "dark"}, {tag: "light", light: true}]
    }
    function test_referenceAlternateMovesRealControls(data) {
        player.lightTheme = !!data.light
        const seek = item("seekBar"), play = item("playButton"), volume = item("volumeSlider")
        const video = item("redesignViewport")
        const originalHeight = video.height
        alternate()
        compare(item("seekBar"), seek); compare(item("playButton"), play); compare(item("volumeSlider"), volume)
        compare(player.controlsHeight, 108)
        compare(item("seekBar").width, 1712)
        compare(point("playButton").x + play.width / 2, width / 2)
        compare(item("fullscreenButton").parent.objectName, "customFooterRight")
        compare(item("mediaTitleControl").parent.objectName, "customFooterLeft")
        compare(video.height, originalHeight - 54)
        compare(player.positionSliderY, point("seekBar").y)
        mouseClick(play); compare(playSpy.count, 1)
        mouseClick(item("fullscreenButton")); compare(fullscreenSpy.count, 1)
        mouseClick(volume, volume.width / 2, volume.height / 2); verify(volumeSpy.count > 0)
    }
    function test_cancelRestoresOriginalParentsAndGeometry() {
        const menuParent = item("mainMenuButton").parent
        const playParent = item("playButton").parent
        const timelineParent = item("timelineControl").parent
        alternate(); player.layoutModel.cancel()
        settledDefault()
        compare(item("mainMenuButton").parent, menuParent)
        compare(item("playButton").parent, playParent)
        compare(item("timelineControl").parent, timelineParent)
        compare(player.controlsHeight, 54)
        compare(point("playButton").x, 58)
        compare(player.positionSliderY, point("seekBar").y)
    }
    function test_removedControlAndRestoredControl() {
        player.layoutModel.begin(); player.layoutModel.remove("subtitles")
        tryVerify(() => !item("subtitleButton").visible)
        player.layoutModel.move("subtitles", "headerLeft", 0)
        tryVerify(() => item("subtitleButton").visible && item("subtitleButton").parent.objectName === "customHeaderLeft")
        mouseClick(item("subtitleButton"))
        player.layoutModel.cancel(); settledDefault()
        verify(item("subtitleButton").visible)
    }
    function test_editingKeepsFullscreenControlsVisible() {
        player.controlsHideTimeout = 30; player.fullscreen = true
        player.layoutModel.begin()
        mouseMove(player, width / 2, height / 2)
        wait(60)
        verify(player.topVisible); verify(player.bottomVisible)
        player.layoutModel.cancel()
        tryCompare(player, "topVisible", false)
        tryCompare(player, "bottomVisible", false)
    }
    function test_customMenuKeepsFullscreenControlsVisible() {
        player.controlsHideTimeout = 30; player.fullscreen = true
        player.customMenuOpen = true
        mouseMove(player, width / 2, height / 2)
        wait(60)
        verify(player.topVisible); verify(player.bottomVisible)
        player.customMenuOpen = false
        tryCompare(player, "topVisible", false); tryCompare(player, "bottomVisible", false)
    }
    function test_savedLayoutAppliesWithoutEditor() {
        alternate()
        const saved = player.layoutModel.serialize()
        player.playerLayout = saved
        verify(!player.layoutModel.editing)
        tryVerify(() => Math.abs(point("seekBar").y - 986) < 1)
        compare(player.controlsHeight, 108)
        player.playerLayout = ""; settledDefault()
        compare(player.controlsHeight, 54)
    }
    function test_panelsDoNotMoveVideoWithAlternateLayout() {
        alternate()
        const video = item("redesignViewport"), bounds = video.mapToItem(player, 0, 0), size = Qt.size(video.width, video.height)
        player.libraryOpen = true; player.settingsPage = "video"
        wait(170)
        compare(video.mapToItem(player, 0, 0), bounds); compare(Qt.size(video.width, video.height), size)
        player.settingsPage = ""; player.queueOpen = true
        compare(video.mapToItem(player, 0, 0), bounds); compare(Qt.size(video.width, video.height), size)
        player.queueOpen = false
    }
    function test_alternateLayoutAtSmallSizesAndScale_data() {
        return [{tag: "small", w: 640, h: 360, scale: 1},
                {tag: "medium", w: 900, h: 600, scale: 1},
                {tag: "wide", w: 1280, h: 720, scale: 1},
                {tag: "scaled", w: 1920, h: 1080, scale: 1.5}]
    }
    function test_alternateLayoutAtSmallSizesAndScale(data) {
        alternate()
        player.mediaTitle = "A long media title that must not cover the playback controls"
        player.mediaFolderTitle = "A long folder name that must not cover the playback controls"
        testCase.width = data.w; testCase.height = data.h; player.scaleFactor = data.scale
        const left = item("customFooterLeft"), center = item("customFooterCenter"), right = item("customFooterRight")
        tryVerify(() => {
            const l = left.mapToItem(player, 0, 0), c = center.mapToItem(player, 0, 0), r = right.mapToItem(player, 0, 0)
            return l.x + left.width + 10 * data.scale <= c.x && c.x + center.width + 10 * data.scale <= r.x
        })
        compare(item("mediaTitleControl").visible, data.w > 1200 * data.scale)
        tryVerify(() => Math.abs(point("seekBar").y - (data.h - 94 * data.scale)) <= 1)
        compare(player.positionSliderY, point("seekBar").y)
        mouseClick(item("playButton")); compare(playSpy.count, 1)
    }
}
