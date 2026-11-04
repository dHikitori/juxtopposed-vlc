// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "JuxtopposedPlayer"
    width: 1280
    height: 720
    when: windowShown
    visible: true
    // A scaled software window grab can contain physical pixels but report DPR 1.
    Item {
        id: pixelCaptureBounds
        width: testCase.width * Screen.devicePixelRatio
        height: testCase.height * Screen.devicePixelRatio
        visible: false
    }
    property var seekEventOrder: []
    Component { id: videoClickProbe; SignalSpy { signalName: "clicked" } }
    property bool consumeVideoHover: false
    property int activeBackdrops: 0
    Component {
        id: backdropProbe
        Item {
            objectName: "backdropProbe"
            property real cornerRadius: 0
            Component.onCompleted: testCase.activeBackdrops++
            Component.onDestruction: testCase.activeBackdrops--
        }
    }

    function test_backdropFollowsFloatingPanelLifetime() {
        player.videoBackdropContent = backdropProbe
        const video = findChild(player, "redesignViewport")
        const original = video.mapToItem(player, 0, 0)
        player.settingsPage = "playback"
        tryCompare(testCase, "activeBackdrops", 1)
        const panel = findChild(player, "redesignRightPanel")
        const backdrop = findChild(panel, "backdropProbe")
        verify(backdrop !== null)
        compare(backdrop.width, panel.width)
        compare(backdrop.height, panel.height)
        compare(video.mapToItem(player, 0, 0), original)
        player.settingsPage = ""
        tryCompare(testCase, "activeBackdrops", 0)
        player.queueOpen = true
        tryCompare(testCase, "activeBackdrops", 1)
        compare(video.mapToItem(player, 0, 0), original)
        player.queueOpen = false
        tryCompare(testCase, "activeBackdrops", 0)
    }

    function test_backdropSourceContainsChromeAndExcludesPanels() {
        function contains(item) {
            for (let parent = item; parent; parent = parent.parent)
                if (parent === player.backdropSource) return true
            return false
        }
        verify(contains(findChild(player, "redesignViewport")))
        verify(contains(findChild(player, "redesignLibraryPanel")))
        verify(contains(findChild(player, "redesignMiniPlayer")))
        verify(contains(findChild(player, "redesignHeader")))
        verify(contains(findChild(player, "redesignToolbar")))
        verify(!contains(findChild(player, "redesignRightPanel")))
        trackPicker.open()
        verify(!contains(trackPicker.background))
        trackPicker.close()
    }

    function test_trackBackdropFollowsPopupLifetime() {
        trackPicker.videoBackdropContent = backdropProbe
        trackPicker.page = "audio"
        const video = findChild(player, "redesignViewport")
        const original = video.mapToItem(player, 0, 0)
        trackPicker.open()
        tryCompare(testCase, "activeBackdrops", 1)
        const backdrop = findChild(trackPicker.background, "backdropProbe")
        verify(backdrop !== null)
        compare(backdrop.width, trackPicker.background.width)
        compare(backdrop.height, trackPicker.background.height)
        compare(video.mapToItem(player, 0, 0), original)
        trackPicker.page = "subtitles"
        tryCompare(testCase, "activeBackdrops", 1)
        trackPicker.close()
        tryCompare(testCase, "activeBackdrops", 0)
    }

    RedesignPlayerView {
        id: player
        anchors.fill: parent
        started: true
        hasVideo: true
        seekable: true
        hasPrevious: true
        hasNext: true
        position: 0.25
        volume: 0.5
        timeText: "00:02:10"
        lengthText: "00:25:40"
        timeForPosition: position => Number(position).toFixed(3)
        onTogglePlayback: playing = !playing
        onSeekRequested: value => { testCase.seekEventOrder.push("seek"); position = value }
        onSeekDragStarted: testCase.seekEventOrder.push("start")
        onSeekDragFinished: testCase.seekEventOrder.push("finish")
        onVolumeRequested: value => volume = value
        onMuteRequested: muted = !muted
        trackPopup: trackPicker
        // VideoSurface accepts hover events in the integrated player.
        videoContent: MouseArea {
            hoverEnabled: true
            acceptedButtons: testCase.consumeVideoHover ? Qt.AllButtons : Qt.NoButton
            enabled: testCase.consumeVideoHover
        }
        libraryContent: RedesignLibraryView {
            scaleFactor: player.scaleFactor; lightTheme: player.lightTheme
            itemCount: 2
            mediaModel: ListModel {
                ListElement { title: "First video"; thumbnail: "" }
                ListElement { title: "Second video"; thumbnail: "" }
            }
        }
        queueContent: RedesignQueue {
            itemCount: 2
            queueModel: ListModel {
                ListElement { title: "First video"; isCurrent: true }
                ListElement { title: "Second video"; isCurrent: false }
            }
        }
    }
    ListModel {
        id: tracks
        property int refreshCount: 0
        function refresh() { refreshCount++ }
        ListElement { display: "English"; displayLabel: ""; checked: true; external: false; sourceName: ""; sourceUri: "" }
        ListElement { display: "Japanese"; displayLabel: ""; checked: false; external: false; sourceName: ""; sourceUri: "" }
    }
    RedesignTrackPopup {
        id: trackPicker
        parent: testCase
        placementArea: player
        x: 20; y: 20
        videoTracks: tracks; audioTracks: tracks; subtitleTracks: tracks
        audioDevices: tracks
        onSelectionRequested: (model, index, selected) => {
            for (let i = 0; i < model.count; ++i) model.setProperty(i, "checked", i === index && selected)
        }
        onDisableRequested: model => {
            for (let i = 0; i < model.count; ++i) model.setProperty(i, "checked", false)
        }
    }
    SignalSpy { id: trackSelectionSpy; target: trackPicker; signalName: "selectionRequested" }
    SignalSpy { id: trackPreferencesSpy; target: trackPicker; signalName: "preferencesRequested" }
    SignalSpy { id: addTracksSpy; target: trackPicker; signalName: "addTracksRequested" }
    SignalSpy { id: editTracksSpy; target: trackPicker; signalName: "editTracksRequested" }
    SignalSpy { id: seekSpy; target: player; signalName: "seekRequested" }
    SignalSpy { id: seekDragStartSpy; target: player; signalName: "seekDragStarted" }
    SignalSpy { id: seekDragFinishSpy; target: player; signalName: "seekDragFinished" }
    SignalSpy { id: previewSpy; target: player; signalName: "seekPreviewRequested" }
    SignalSpy { id: previewEndSpy; target: player; signalName: "seekPreviewEnded" }
    SignalSpy { id: audioSpy; target: player; signalName: "audioTracksRequested" }
    SignalSpy { id: contextMenuSpy; target: player; signalName: "contextMenuRequested" }
    SignalSpy { id: subtitleSpy; target: player; signalName: "subtitleTracksRequested" }
    SignalSpy { id: toolbarVisibilitySpy; target: player; signalName: "bottomVisibleChanged" }
    SignalSpy { id: folderSpy; target: player; signalName: "mediaFolderRequested" }
    SignalSpy { id: artistSpy; target: player; signalName: "artistRequested" }
    SignalSpy { id: compactCategorySpy; target: player.libraryItem; signalName: "categoryRequested" }
    SignalSpy { id: resumeSpy; target: player; signalName: "resumeRequested" }

    RedesignSettingSlider {
        id: settingControl
        x: 20; y: 20; width: 360; z: 10
        visible: false
        label: "Subtitle Speed"; suffix: " fps"; decimals: 3
        from: 0; to: 60; stepSize: 0.001
        resetValue: 0; minimumValueText: "Automatic"
        onMoved: value => currentValue = value
    }
    SignalSpy { id: settingSpy; target: settingControl; signalName: "moved" }
    RedesignSwitchRow {
        id: statisticsSwitch
        x: 20; y: 20; width: 360; z: 10
        visible: false
        label: "Show Statistics"; checked: player.showStatistics
        onToggled: checked => player.showStatistics = checked
    }

    function init() {
        contextMenuSpy.clear()
        tracks.refreshCount = 0
        consumeVideoHover = false
        settingControl.visible = false
        settingControl.currentValue = 0
        settingControl.resetValue = 0
        settingSpy.clear()
        statisticsSwitch.visible = false
        player.showStatistics = false
        player.resumeAvailable = false
        player.audioOnly = false
        player.resumeTimeText = ""
        resumeSpy.clear()
        player.showSubtitleTimeline = false
        player.showSeekPreview = true
        player.subtitleTimelineCues = []
        player.subtitleTimelineState = "off"
        player.audioWaveformEnabled = false
        player.audioWaveformPeaks = []
        player.statistics = ({})
        player.started = true
        trackPicker.close()
        trackPicker.parent = testCase
        trackPicker.x = 20; trackPicker.y = 20
        trackPicker.page = "subtitles"
        trackPicker.deviceRefreshInterval = 2000
        tracks.clear()
        tracks.append({display:"English", displayLabel:"", checked:true, external:false, sourceName:"", sourceUri:""})
        tracks.append({display:"Japanese", displayLabel:"", checked:false, external:false, sourceName:"", sourceUri:""})
        trackPicker.canAddTracks = true
        trackSelectionSpy.clear()
        trackPreferencesSpy.clear(); addTracksSpy.clear(); editTracksSpy.clear()
        player.libraryOpen = false
        player.libraryExpanded = false
        player.miniPlayerVisible = true
        player.mediaFolderTitle = "Shows"
        player.canBrowseMedia = true
        player.artist = ""
        findChild(player, "redesignMiniPlayer").userMoved = false
        folderSpy.clear(); artistSpy.clear()
        compactCategorySpy.clear()
        player.queueOpen = false
        player.settingsPage = ""
        player.videoBackdropContent = null
        trackPicker.videoBackdropContent = null
        player.playing = false
        player.muted = false
        player.fullscreen = false
        player.controlsHideTimeout = 1800
        player.fullscreenControlsOpacity = 1
        player.fullscreenControlsSensitivity = 3
        player.minimalView = false
        player.started = true
        player.hasVideo = true
        player.autoHideMode = 1
        player.lightTheme = false
        player.scaleFactor = 1
        player.timeText = "00:02:10"
        player.lengthText = "00:25:40"
        player.seekPreviewImage = ""
        player.seekPreviewImagePosition = -1
        player.forceActiveFocus()
        mouseMove(player, 20, 200)
        testCase.width = 1280
        testCase.height = 720
        seekSpy.clear()
        seekEventOrder = []
        seekDragStartSpy.clear()
        seekDragFinishSpy.clear()
        previewSpy.clear()
        previewEndSpy.clear()
        audioSpy.clear()
        subtitleSpy.clear()
        tryCompare(findChild(player, "redesignLibraryPanel"), "reveal", 0)
        wait(20)
    }
    function test_queueContextClickDoesNotOpenAppMenu_data() {
        return [
            { tag: "item", item: true, floating: false },
            { tag: "container", item: false, floating: false },
            { tag: "floating item", item: true, floating: true },
            { tag: "floating container", item: false, floating: true }
        ]
    }
    function test_queueContextClickDoesNotOpenAppMenu(data) {
        player.hasVideo = false
        player.libraryOpen = data.floating
        player.libraryExpanded = data.floating
        player.queueOpen = true
        tryVerify(() => player.queueItem !== null)
        const queue = player.queueItem
        const target = findChild(queue, data.item ? "queueItem_0" : "queueList")
        const menu = findChild(queue, "queueActionsMenu")
        verify(target !== null)
        wait(200)
        mouseClick(target, target.width / 2, data.item ? target.height / 2 : target.height - 25, Qt.RightButton)
        tryCompare(menu, "opened", true)
        compare(contextMenuSpy.count, 0)
        compare(queue.selectedIndex, data.item ? 0 : -1)
        menu.close()
    }
    function test_backgroundContextClickRespectsVideoAndOverlays() {
        player.hasVideo = false
        const viewport = findChild(player, "redesignViewport")
        mouseClick(viewport, 35, 35, Qt.RightButton)
        compare(contextMenuSpy.count, 1)
        compare(contextMenuSpy.signalArguments[0][0], viewport.mapToGlobal(35, 35))
        contextMenuSpy.clear()
        player.settingsPage = "playback"
        const panel = findChild(player, "redesignRightPanel")
        mouseClick(panel, panel.width / 2, panel.height / 2, Qt.RightButton)
        compare(contextMenuSpy.count, 0)
        player.settingsPage = ""
        player.libraryOpen = true
        wait(200)
        const library = findChild(player, "redesignLibraryPanel")
        mouseClick(library, library.width / 2, library.height - 20, Qt.RightButton)
        compare(contextMenuSpy.count, 0)
        player.libraryOpen = false
        wait(200)
        player.hasVideo = true
        consumeVideoHover = true
        mouseClick(viewport, 35, 35, Qt.RightButton)
        compare(contextMenuSpy.count, 0)
    }
    function test_toolbarTimelineGeometry_data() {
        return [{tag: "dark", width: 1920, height: 1080, scale: 1},
                {tag: "light", width: 1920, height: 1080, scale: 1, light: true},
                {tag: "scaled", width: 1920, height: 1080, scale: 1.5},
                {tag: "small", width: 640, height: 360, scale: 1, compact: true},
                {tag: "elapsed-only", width: 700, height: 400, scale: 1, elapsedOnly: true}]
    }
    function test_toolbarTimelineGeometry(data) {
        testCase.width = data.width; testCase.height = data.height
        player.scaleFactor = data.scale; player.lightTheme = !!data.light
        const seek = findChild(player, "seekBar")
        const elapsed = findChild(player, "elapsedTimeLabel")
        const duration = findChild(player, "durationTimeLabel")
        const s = data.scale
        const left = data.compact ? 162 : 254 * s
        const right = data.compact || data.elapsedOnly ? 194 : 428 * s
        tryVerify(() => Math.abs(seek.mapToItem(player, 0, 0).x - left) <= 1
                  && Math.abs(seek.width - (data.width - left - right)) <= 1)
        fuzzyCompare(seek.mapToItem(player, 0, 0).y, data.height - 41 * s, 1)
        compare(elapsed.visible, !data.compact)
        compare(duration.visible, !data.compact && !data.elapsedOnly)
        for (const label of [elapsed, duration]) {
            if (!label.visible) continue
            verify(label.width >= label.implicitWidth)
            const point = label.mapToItem(player, 0, 0)
            const seekPoint = seek.mapToItem(player, 0, 0)
            verify(point.x + label.width <= seekPoint.x || point.x >= seekPoint.x + seek.width)
        }
    }
    function test_toolbarLongTimesRemainReadable() {
        const elapsed = findChild(player, "elapsedTimeLabel")
        const duration = findChild(player, "durationTimeLabel")
        const seek = findChild(player, "seekBar")
        player.timeText = "9999:59:59"; player.lengthText = "9999:59:59"
        tryVerify(() => elapsed.width >= elapsed.implicitWidth && duration.width >= duration.implicitWidth)
        const seekPoint = seek.mapToItem(player, 0, 0)
        verify(elapsed.mapToItem(player, 0, 0).x + elapsed.width <= seekPoint.x)
        verify(duration.mapToItem(player, 0, 0).x >= seekPoint.x + seek.width)
        verify(seek.width >= 40)
    }
    function test_statisticsSwitchUsesMouseAndKeyboard() {
        statisticsSwitch.visible = true
        const toggle = findChild(statisticsSwitch, "settingSwitch")
        mouseClick(toggle)
        verify(player.showStatistics)
        toggle.forceActiveFocus()
        keyClick(Qt.Key_Space)
        verify(!player.showStatistics)
        player.showStatistics = true
        verify(toggle.checked)
    }
    function test_zoomedSubtitleTimelineFollowsGlobalPlayerPosition() {
        player.showSubtitleTimeline = true
        player.subtitleTimelineState = "ready"
        player.subtitleTimelineCues = [{start: 10, stop: 20}, {start: 80, stop: 95}]
        player.durationSeconds = 100
        player.position = 0.45
        const seek = findChild(player, "seekBar")
        seek.zoomTimeline(4)
        player.position = 0.9
        tryVerify(() => seek.from <= 0.9 && seek.to >= 0.9)
        tryCompare(seek, "value", 0.9)
        const viewport = findChild(player, "redesignViewport")
        const before = [viewport.x, viewport.y, viewport.width, viewport.height]
        player.libraryOpen = true; player.settingsPage = "playback"
        wait(220)
        compare([viewport.x, viewport.y, viewport.width, viewport.height], before)
    }
    function test_dragPastSeekbarEndClampsAndCancelsWhenMediaStops() {
        const seek = findChild(player, "seekBar")
        player.position = 0.95
        mousePress(seek, seek.width - 20, seek.height / 2)
        mouseMove(seek, seek.width + 200, seek.height / 2, 40)
        compare(player.position, 1)
        verify(seekSpy.count > 0)
        player.started = false
        player.seekable = false
        tryCompare(seek, "pressed", false)
        verify(!seek.previewActive)
        compare(seekDragStartSpy.count, 1)
        tryCompare(seekDragFinishSpy, "count", 1)
        compare(seekDragFinishSpy.signalArguments[0][1], false)
        mouseRelease(seek, seek.width + 200, seek.height / 2)
        player.seekable = true
    }
    function test_dragPastSeekbarEndCommitsOnlyOnRelease() {
        const seek = findChild(player, "seekBar")
        player.position = 0.25
        mousePress(seek, seek.width / 2, seek.height / 2)
        compare(seekDragStartSpy.count, 1)
        compare(seekEventOrder[0], "start")
        mouseMove(seek, seek.width + 200, seek.height / 2, 40)
        compare(player.position, 1)
        compare(seekDragFinishSpy.count, 0)
        wait(100)
        verify(seek.pressed)
        mouseRelease(seek, seek.width + 200, seek.height / 2)
        tryCompare(seekDragFinishSpy, "count", 1)
        compare(seekDragFinishSpy.signalArguments[0][0], 1)
        compare(seekDragFinishSpy.signalArguments[0][1], true)
    }
    function test_pressAtSeekbarEndStartsHoldBeforeSeeking() {
        const seek = findChild(player, "seekBar")
        player.position = 0.25
        mousePress(seek, seek.width - 2, seek.height / 2)
        compare(seekEventOrder[0], "start")
        compare(seekDragStartSpy.count, 1)
        compare(seekDragFinishSpy.count, 0)
        mouseRelease(seek, seek.width - 2, seek.height / 2)
        tryCompare(seekDragFinishSpy, "count", 1)
        compare(seekDragFinishSpy.signalArguments[0][0], 1)
        compare(seekDragFinishSpy.signalArguments[0][1], true)
    }
    function test_keyboardSeekDoesNotStartPointerDrag() {
        const seek = findChild(player, "seekBar")
        player.position = 0.4
        seek.forceActiveFocus()
        keyClick(Qt.Key_Right)
        verify(seekSpy.count > 0)
        compare(seekDragStartSpy.count, 0)
        compare(seekDragFinishSpy.count, 0)
    }
    function test_subtitleTimelineControlsDoNotStartSeekDrag() {
        player.showSubtitleTimeline = true
        player.subtitleTimelineState = "ready"
        player.subtitleTimelineCues = [{start: 10, stop: 30}]
        player.durationSeconds = 100
        mouseClick(findChild(player, "subtitleEarlierButton"))
        mouseClick(findChild(player, "subtitleZoomInButton"))
        compare(seekDragStartSpy.count, 0)
        compare(seekDragFinishSpy.count, 0)
        compare(seekSpy.count, 0)
    }
    function test_statisticsOverlayUpdatesAndFitsVideo() {
        const overlay = findChild(player, "playbackStatisticsOverlay")
        verify(!overlay.visible)
        player.statistics = ({decodedVideo: 123, inputBitrate: 456})
        player.showStatistics = true
        verify(overlay.visible)
        compare(findChild(overlay, "statistic_decodedVideo").text, "123")
        compare(findChild(overlay, "statistic_inputBitrate").text, "456 kb/s")
        player.statistics = ({decodedVideo: 321, inputBitrate: 654})
        compare(findChild(overlay, "statistic_decodedVideo").text, "321")
        testCase.width = 640; testCase.height = 360
        player.libraryOpen = true; player.settingsPage = "playback"
        wait(20)
        const viewport = findChild(player, "redesignViewport")
        verify(overlay.width <= Math.max(0, viewport.width - 24))
        verify(overlay.height <= Math.max(0, viewport.height - 24))
        player.started = false
        verify(!overlay.visible)
    }
    function test_subtitleFrameRateAcceptsDecimalsAndAutomatic_data() {
        return [{tag: "decimal-point", locale: "en_US", decimal: Qt.Key_Period, expected: "23.976 fps"},
                {tag: "decimal-comma", locale: "de_DE", decimal: Qt.Key_Comma, expected: "23,976 fps"}]
    }
    function test_subtitleFrameRateAcceptsDecimalsAndAutomatic(data) {
        settingControl.visible = true
        const field = findChild(settingControl, "settingValueField")
        field.locale = Qt.locale(data.locale)
        compare(field.displayText, "Automatic")
        mouseClick(field)
        keyClick(Qt.Key_A, Qt.ControlModifier)
        keyClick(Qt.Key_2); keyClick(Qt.Key_3); keyClick(data.decimal)
        keyClick(Qt.Key_9); keyClick(Qt.Key_7); keyClick(Qt.Key_6)
        keyClick(Qt.Key_Return)
        compare(settingSpy.count, 1)
        compare(settingControl.currentValue, 23.976)
        compare(field.displayText, data.expected)
        compare(field.contentItem.text, data.expected)
        keyClick(Qt.Key_A, Qt.ControlModifier)
        for (const letter of "Automatic") keyClick(letter)
        keyClick(Qt.Key_Return)
        compare(settingControl.currentValue, 0)
        compare(field.displayText, "Automatic")
        compare(field.contentItem.text, "Automatic")
    }
    function test_settingResetUsesItsOwnDefault_data() {
        return [{tag: "subtitle-fps", value: 23.976, reset: 0},
                {tag: "playback-speed", value: 2, reset: 1},
                {tag: "track-delay", value: 4, reset: 0}]
    }
    function test_settingResetUsesItsOwnDefault(data) {
        settingControl.visible = true
        settingControl.currentValue = data.value
        settingControl.resetValue = data.reset
        mouseClick(findChild(settingControl, "settingResetButton"))
        compare(settingSpy.count, 1)
        compare(settingControl.currentValue, data.reset)
    }
    function test_subtitleFrameRateRejectsInvalidAndOutOfRangeInput_data() {
        return [{tag: "text", keys: [Qt.Key_X]},
                {tag: "empty", keys: [Qt.Key_Backspace]},
                {tag: "above-range", keys: [Qt.Key_6, Qt.Key_1]},
                {tag: "below-range", keys: [Qt.Key_Minus, Qt.Key_1]}]
    }
    function test_subtitleFrameRateRejectsInvalidAndOutOfRangeInput(data) {
        settingControl.visible = true
        settingControl.currentValue = 25
        const field = findChild(settingControl, "settingValueField")
        field.locale = Qt.locale("en_US")
        mouseClick(field)
        keyClick(Qt.Key_A, Qt.ControlModifier)
        for (const key of data.keys) keyClick(key)
        keyClick(Qt.Key_Return)
        compare(settingControl.currentValue, 25)
        compare(field.displayText, "25.000 fps")
        compare(field.contentItem.text, "25.000 fps")
        compare(settingSpy.count, 0)
    }
    function test_deviceListRefreshesOnlyWhileAudioIsOpen() {
        trackPicker.deviceRefreshInterval = 100
        trackPicker.page = "audio"
        trackPicker.open()
        tryCompare(trackPicker, "opened", true)
        compare(tracks.refreshCount, 1)
        tryVerify(() => tracks.refreshCount >= 3, 1000)
        trackPicker.page = "subtitles"
        const before = tracks.refreshCount
        wait(350)
        compare(tracks.refreshCount, before)
        trackPicker.close()
        trackPicker.page = "audio"
        wait(350)
        compare(tracks.refreshCount, before)
    }
    function test_externalTracksKeepTheirOriginalSelectionIndex() {
        tracks.append({display:"Track 3",checked:false,external:true,sourceName:"captions.srt",sourceUri:"file:///tracks/captions.srt"})
        trackPicker.open(); tryCompare(trackPicker,"opened",true)
        const embedded=findChild(trackPicker,"embeddedTrackChoices"), external=findChild(trackPicker,"externalTrackChoices")
        tryCompare(embedded,"count",2); tryCompare(external,"count",1)
        verify(!findChild(embedded,"trackChoice_2").visible)
        verify(!findChild(external,"trackChoice_0").visible)
        const source=findChild(external,"trackChoice_2")
        compare(source.text,"captions.srt")
        tryVerify(() => source.width > 0)
        mouseClick(source)
        compare(trackSelectionSpy.count,1)
        compare(trackSelectionSpy.signalArguments[0][1],2)
        verify(tracks.get(2).checked)
        verify(!findChild(embedded,"disableTracks").chosen)
        mouseClick(findChild(embedded,"disableTracks"))
        for (let i=0;i<tracks.count;++i) verify(!tracks.get(i).checked)
        verify(findChild(embedded,"disableTracks").chosen)
        tracks.remove(0)
        tryCompare(embedded,"count",1); tryCompare(external,"count",1)
        mouseClick(findChild(external,"trackChoice_1"))
        compare(trackSelectionSpy.signalArguments[1][1],1)
        verify(tracks.get(1).checked)
    }
    function test_conciseTrackLabelsKeepFullNamesInTooltips() {
        tracks.setProperty(0,"display","English - [English]")
        tracks.setProperty(0,"displayLabel","English")
        trackPicker.open();tryCompare(trackPicker,"opened",true)
        const embedded=findChild(trackPicker,"embeddedTrackChoices")
        const choice=findChild(embedded,"trackChoice_0")
        compare(choice.text,"English");compare(choice.description,"English - [English]")
        tracks.setProperty(0,"displayLabel","English (Forced)")
        tryCompare(choice,"text","English (Forced)")
        tracks.setProperty(0,"displayLabel","");tryCompare(choice,"text","English - [English]")
    }
    function test_externalTrackActionsClosePopup() {
        trackPicker.open(); tryCompare(trackPicker,"opened",true)
        const external=findChild(trackPicker,"externalTrackChoices")
        verify(!findChild(external,"editExternalTracks").enabled)
        mouseClick(findChild(external,"addExternalTracks"))
        compare(addTracksSpy.count,1); compare(addTracksSpy.signalArguments[0][0],"subtitles")
        tryCompare(trackPicker,"visible",false)
        tracks.append({display:"French",checked:false,external:true,sourceName:"french.srt",sourceUri:"file:///tracks/french.srt"})
        trackPicker.open(); tryCompare(trackPicker,"opened",true)
        mouseClick(findChild(external,"editExternalTracks"))
        compare(editTracksSpy.count,1); compare(editTracksSpy.signalArguments[0][0],"subtitles")
        tryCompare(trackPicker,"visible",false)
        trackPicker.canAddTracks=false; trackPicker.open(); tryCompare(trackPicker,"opened",true)
        verify(!findChild(external,"addExternalTracks").enabled)
    }
    function test_externalAudioTracksAndDeviceRefresh() {
        tracks.append({display:"Commentary",checked:false,external:true,sourceName:"commentary.wav",sourceUri:"file:///tracks/commentary.wav"})
        trackPicker.page="audio"; trackPicker.open(); tryCompare(trackPicker,"opened",true)
        const external=findChild(trackPicker,"externalTrackChoices")
        verify(external.visible); compare(external.count,1)
        const choice=findChild(external,"trackChoice_2")
        tryVerify(() => choice.width > 0)
        mouseClick(choice); verify(tracks.get(2).checked)
        const scroll=findChild(trackPicker,"trackScrollView").contentItem
        scroll.contentY=Math.max(0,scroll.contentHeight-scroll.height)
        wait(30)
        const before=tracks.refreshCount
        mouseClick(findChild(findChild(trackPicker,"outputDeviceChoices"),"refreshTrackDevices"))
        compare(tracks.refreshCount,before+1)
        tracks.remove(2); tryCompare(external,"visible",false)
    }
    function test_trackPickerSelectsAndDisablesTracks() {
        trackPicker.open()
        wait(30)
        const choices = findChild(trackPicker, "embeddedTrackChoices")
        verify(choices.hasSelection)
        const japanese = findChild(choices, "trackChoice_1")
        mouseClick(japanese)
        compare(trackSelectionSpy.count, 1)
        verify(tracks.get(1).checked)
        verify(!tracks.get(0).checked)
        // Selecting the current radio choice keeps it selected.
        mouseClick(japanese)
        verify(tracks.get(1).checked)
        const none = findChild(choices, "disableTracks")
        none.forceActiveFocus()
        keyClick(Qt.Key_Space)
        verify(!choices.hasSelection)
        verify(none.chosen)
        // The same None action is available on video and audio pages.
        mouseClick(findChild(trackPicker.contentItem, "trackTab_audio"))
        compare(trackPicker.page, "audio")
        verify(none.visible)
        mouseClick(findChild(trackPicker.contentItem, "trackTab_video"))
        compare(trackPicker.page, "video")
        verify(none.visible)
        mouseClick(findChild(trackPicker.contentItem, "trackPreferencesButton"))
        compare(trackPreferencesSpy.count, 1)
        compare(trackPreferencesSpy.signalArguments[0][0], "video")
        verify(!trackPicker.opened)
    }
    function test_trackPickerFitsSmallWindow() {
        testCase.width = 640; testCase.height = 360
        trackPicker.open()
        wait(30)
        verify(trackPicker.height <= testCase.height - 114)
        verify(findChild(trackPicker.contentItem, "trackTab_subtitles").visible)
    }
    function test_transportAndTrackActions() {
        mouseClick(findChild(player, "playButton"))
        verify(player.playing)
        mouseClick(findChild(player, "playButton"))
        verify(!player.playing)
        mouseClick(findChild(player, "muteButton"))
        verify(player.muted)
        mouseClick(findChild(player, "audioButton"))
        compare(audioSpy.count, 1)
        mouseClick(findChild(player, "subtitleButton"))
        compare(subtitleSpy.count, 1)
    }
    function test_settingsTabsKeepSettingsButtonSelected() {
        const settings = findChild(player, "playbackSettingsButton")
        const audio = findChild(player, "audioButton")
        const subtitles = findChild(player, "subtitleButton")
        mouseClick(settings)
        compare(player.settingsPage, "playback")
        for (const page of ["playback", "video", "audio", "subtitles"]) {
            player.settingsPage = page
            verify(settings.selected, page)
            verify(!audio.selected, page)
            verify(!subtitles.selected, page)
        }
        mouseClick(audio)
        tryCompare(trackPicker, "opened", true)
        verify(audio.selected)
        verify(!subtitles.selected)
        verify(!settings.selected)
        mouseClick(subtitles)
        compare(trackPicker.page, "subtitles")
        verify(subtitles.selected)
        verify(!audio.selected)
        verify(!settings.selected)
        mouseClick(subtitles)
        tryCompare(trackPicker, "visible", false)
        verify(!subtitles.selected)
        verify(!settings.selected)
    }
    // https://github.com/hakone-org/hakone/issues/37: a click soon after the
    // click that closed a menu must open it again, not count as a double-click.
    function test_quickClickReopensMenu() {
        const subtitles = findChild(player, "subtitleButton")
        mouseClick(subtitles)
        tryCompare(trackPicker, "opened", true)
        mouseDoubleClickSequence(subtitles)
        tryCompare(trackPicker, "opened", true)
        verify(subtitles.selected)
        mouseClick(subtitles)
        tryCompare(trackPicker, "visible", false)

        const settings = findChild(player, "playbackSettingsButton")
        mouseClick(settings)
        compare(player.settingsPage, "playback")
        mouseDoubleClickSequence(settings)
        compare(player.settingsPage, "playback")
        player.settingsPage = ""
    }
    function test_selectedToolbarGlyphs_data() {
        return [{tag: "settings-dark", button: "playbackSettingsButton", x: 7, y: 11, light: false, accent: "#d37918"},
                {tag: "subtitles-dark", button: "subtitleButton", x: 12, y: 8, light: false, accent: "#d37918"},
                {tag: "settings-light-green", button: "playbackSettingsButton", x: 7, y: 11, light: true, accent: "#0cb37a"},
                {tag: "subtitles-light-green", button: "subtitleButton", x: 12, y: 8, light: true, accent: "#0cb37a"}]
    }
    function test_selectedToolbarGlyphs(data) {
        player.lightTheme = data.light
        player.accentColor = data.accent
        const button = findChild(player, data.button), icon = findChild(button, "redesignButtonIcon")
        verify(icon !== null)
        mouseClick(button)
        tryCompare(button, "selected", true)
        tryCompare(icon, "status", Image.Ready)
        const point = icon.mapToItem(testCase, data.x + 0.5, data.y + 0.5)
        const image = grabImage(pixelCaptureBounds), expected = Qt.color(data.accent)
        const x = Math.floor(point.x * image.width / testCase.width)
        const y = Math.floor(point.y * image.height / testCase.height)
        verify(Math.abs(image.red(x, y) - Math.round(expected.r * 255)) <= 1
            && Math.abs(image.green(x, y) - Math.round(expected.g * 255)) <= 1
            && Math.abs(image.blue(x, y) - Math.round(expected.b * 255)) <= 1,
            "The active glyph must use the filled shape and the chosen accent.")
        mouseClick(button)
        tryCompare(button, "selected", false)
        tryCompare(icon, "status", Image.Ready)
        compare(icon.name, button.iconName)
        player.accentColor = "#d37918"
    }
    function test_headerFolderOpensCurrentMediaFolderWithoutMovingVideo() {
        const button = findChild(player, "mediaFolderButton")
        const bounds = videoBounds()
        compare(button.caption, "Shows")
        mouseClick(button)
        verify(player.libraryOpen)
        compare(folderSpy.count, 1)
        compare(videoBounds(), bounds)
        mouseClick(findChild(player, "libraryToggleButton"))
        verify(!player.libraryOpen)
        player.mediaFolderTitle = "A folder with a name that is too long to fit in the header of the player"
        verify(button.width < 300)
        player.mediaFolderTitle = ""
        compare(button.caption, player.locationTitle)
        mouseClick(button)
        verify(player.libraryOpen)
        compare(folderSpy.count, 1)
    }
    function test_subtitleButtonClosesOpenPopupAndExpandedSubtitlePanel() {
        const button = findChild(player, "subtitleButton")
        mouseClick(button)
        tryCompare(trackPicker, "opened", true)
        compare(trackPicker.page, "subtitles")
        compare(trackPicker.width, 400)
        const popupPosition = trackPicker.contentItem.mapToItem(player, 0, 0)
        compare(popupPosition.x + trackPicker.width, player.width - 20)
        compare(popupPosition.y + trackPicker.height, player.height - 54 - 20)
        verify(button.selected)
        mouseClick(button)
        tryCompare(trackPicker, "visible", false)
        verify(!button.selected)
        mouseClick(button)
        tryCompare(trackPicker, "opened", true)
        mouseClick(findChild(player, "audioButton"))
        tryCompare(trackPicker, "opened", true)
        compare(trackPicker.page, "audio")
        mouseClick(player, 100, 200)
        tryCompare(trackPicker, "visible", false)
        player.settingsPage = "subtitles"
        mouseClick(button)
        compare(player.settingsPage, "")
        verify(!trackPicker.visible)
    }
    function test_queueAndSettingsCardsHaveReferenceMargins() {
        testCase.width = 1920; testCase.height = 1080
        const before = videoBounds()
        const panel = findChild(player, "redesignRightPanel")
        player.queueOpen = true
        compare(panel.x, 1500)
        compare(panel.y, 60)
        compare(panel.width, 400)
        compare(panel.height, 946)
        compare(panel.radius, 10)
        compare(panel.border.width, 0)
        compare(videoBounds(), before)
        player.toggleSettings("video")
        verify(!player.queueOpen)
        compare(panel.x, 1500)
        compare(panel.y, 60)
        compare(panel.height, 946)
        compare(videoBounds(), before)
    }
    function test_trackButtonsStayClosedAfterSecondClick_data() {
        return [
            {tag: "subtitles-windowed", button: "subtitleButton", fullscreen: false},
            {tag: "subtitles-fullscreen", button: "subtitleButton", fullscreen: true},
            {tag: "audio-windowed", button: "audioButton", fullscreen: false},
            {tag: "audio-fullscreen", button: "audioButton", fullscreen: true}
        ]
    }
    function test_trackButtonsStayClosedAfterSecondClick(data) {
        player.fullscreen = data.fullscreen; player.revealControls()
        const button = findChild(player, data.button)
        mouseClick(button); tryCompare(trackPicker, "opened", true)
        mousePress(button); wait(50)
        verify(trackPicker.visible)
        mouseRelease(button); tryCompare(trackPicker, "visible", false)
        wait(650); verify(!trackPicker.visible)
        mouseClick(button); tryCompare(trackPicker, "opened", true)
    }
    function test_subtitlePopupKeepsFullscreenToolbarAvailable() {
        player.fullscreen = true
        player.controlsHideTimeout = 30
        player.revealControls()
        const button = findChild(player, "subtitleButton")
        mouseClick(button)
        tryCompare(trackPicker, "opened", true)
        mouseMove(player, 100, 200)
        wait(80)
        verify(player.bottomVisible)
        mouseClick(button)
        tryCompare(trackPicker, "visible", false)
        mouseMove(player, 100, 200)
        player.forceActiveFocus()
        wait(80)
        verify(!player.bottomVisible)
    }
    function test_openTrackPopupKeepsMarginsWhenWindowShrinks() {
        mouseClick(findChild(player, "subtitleButton"))
        tryCompare(trackPicker, "opened", true)
        testCase.width = 640; testCase.height = 360
        wait(50)
        const point = trackPicker.contentItem.mapToItem(player, 0, 0)
        verify(point.x >= 20 && point.y >= 60)
        verify(point.x + trackPicker.width <= player.width - 20)
        verify(point.y + trackPicker.height <= player.height - 74)
        mouseClick(findChild(player, "subtitleButton"))
        tryCompare(trackPicker, "visible", false)
    }
    function test_seekUsesPointerAndKeyboard() {
        const slider = findChild(player, "seekBar")
        mouseClick(slider, slider.width * 0.75, slider.height / 2)
        verify(seekSpy.count > 0)
        verify(player.position > 0.7 && player.position < 0.8)
        slider.forceActiveFocus()
        const before = player.position
        keyClick(Qt.Key_Right)
        verify(player.position > before)
    }
    readonly property url previewFixture: "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAJCAIAAAC0SDtlAAAAFUlEQVR4nGP8z0AaYCJR/agG2mgAAMeRARHH28OHAAAAAElFTkSuQmCC"
    function test_seekBackdropFollowsPreviewLifetime_data() {
        return [{tag: "player", mini: false}, {tag: "mini-player", mini: true}]
    }
    function test_seekBackdropFollowsPreviewLifetime(data) {
        player.videoBackdropContent = backdropProbe
        player.libraryOpen = data.mini
        player.libraryExpanded = data.mini
        const slider = findChild(player, data.mini ? "miniPlayerSeekBar" : "seekBar")
        const popup = findChild(slider, "seekThumbnailPopup")
        mouseMove(slider, slider.width / 2, slider.height / 2)
        tryVerify(() => previewSpy.count > 0)
        player.seekPreviewImagePosition = slider.previewPosition
        player.seekPreviewImage = previewFixture
        tryCompare(popup, "visible", true)
        tryCompare(testCase, "activeBackdrops", 1)
        const backdrop = findChild(popup.background, "backdropProbe")
        verify(backdrop !== null)
        compare(backdrop.width, popup.width)
        compare(backdrop.height, popup.height)
        compare(backdrop.cornerRadius, 14)
        player.showSeekPreview = false
        tryCompare(testCase, "activeBackdrops", 0)
    }
    function test_seekPreviewCanBeTurnedOffAndOn() {
        const slider = findChild(player, "seekBar")
        const popup = findChild(slider, "seekThumbnailPopup")
        verify(player.showSeekPreview)
        mouseMove(slider, slider.width / 2, slider.height / 2)
        tryVerify(() => previewSpy.count > 0)
        player.seekPreviewImagePosition = slider.previewPosition
        player.seekPreviewImage = previewFixture
        tryCompare(popup, "visible", true)
        player.showSeekPreview = false
        tryCompare(popup, "visible", false)
        const count = previewSpy.count
        mouseMove(slider, slider.width * .7, slider.height / 2)
        wait(200)
        compare(previewSpy.count, count)
        verify(previewEndSpy.count > 0)
        player.showSeekPreview = true
        tryVerify(() => previewSpy.count > count)
        tryCompare(popup, "visible", true)
    }
    function test_seekHoverPreviewDoesNotSeekOrMoveTheVideo() {
        const slider = findChild(player, "seekBar")
        const popup = findChild(slider, "seekThumbnailPopup")
        const bounds = videoBounds()
        const before = player.position
        mouseMove(slider, slider.width / 2, slider.height / 2)
        tryVerify(() => previewSpy.count > 0)
        compare(seekSpy.count, 0); compare(player.position, before)
        player.seekPreviewImagePosition = slider.previewPosition
        player.seekPreviewImage = previewFixture
        tryCompare(popup, "visible", true)
        compare(popup.width, 272); compare(popup.height, 189)
        compare(videoBounds(), bounds)
        mouseMove(player, 20, 200)
        tryCompare(popup, "visible", false)
        verify(previewEndSpy.count > 0)
    }
    // https://github.com/hakone-org/hakone/issues/34: the nearest prepared
    // frame stands in while the hovered one decodes. The time stays the
    // hovered time, and does not jump to the frame's.
    function test_seekPreviewShowsHoveredTimeWhileLoadingAndStopsForAudio() {
        const slider = findChild(player, "seekBar")
        const popup = findChild(slider, "seekThumbnailPopup")
        mouseMove(slider, slider.width * .25, slider.height / 2)
        tryVerify(() => previewSpy.count > 0)
        const hovered = slider.previewPosition
        player.seekPreviewImagePosition = hovered + .02
        player.seekPreviewImage = previewFixture
        tryCompare(popup, "visible", true)
        const time = findChild(popup.contentItem, "seekThumbnailTime")
        compare(time.text, player.timeForPosition(hovered))
        player.seekPreviewImagePosition = hovered
        compare(time.text, player.timeForPosition(hovered))
        mouseMove(slider, slider.width * .75, slider.height / 2)
        verify(popup.visible)
        compare(time.text, player.timeForPosition(slider.previewPosition))
        verify(time.text !== player.timeForPosition(hovered))
        const before = previewSpy.count
        player.refreshSeekPreview()
        tryVerify(() => previewSpy.count > before)
        player.hasVideo = false
        tryCompare(popup, "visible", false)
        const count = previewSpy.count
        mouseMove(slider, slider.width / 2, slider.height / 2)
        wait(200)
        compare(previewSpy.count, count)
    }
    function test_seekPreviewFitsWindowEdgesAndWorksDuringScrubbing() {
        testCase.width = 640
        const slider = findChild(player, "seekBar")
        const popup = findChild(slider, "seekThumbnailPopup")
        wait(30)
        mousePress(slider, slider.width - 2, slider.height / 2)
        tryVerify(() => previewSpy.count > 0)
        player.seekPreviewImagePosition = slider.previewPosition
        player.seekPreviewImage = previewFixture
        tryCompare(popup, "visible", true)
        const point = popup.contentItem.parent.mapToItem(testCase, 0, 0)
        verify(point.x >= 0); verify(point.x + popup.width <= testCase.width)
        mouseMove(slider, 2, slider.height / 2)
        tryVerify(() => previewSpy.count > 1)
        player.seekPreviewImagePosition = slider.previewPosition
        tryCompare(popup, "visible", true)
        verify(popup.contentItem.parent.mapToItem(testCase, 0, 0).x >= 0)
        mouseRelease(slider, 2, slider.height / 2)
    }
    function videoBounds() {
        const viewport = findChild(player, "redesignViewport")
        return Qt.rect(viewport.x, viewport.y, viewport.width, viewport.height)
    }
    function test_panelsOverlayVideo() {
        const viewport = findChild(player, "redesignViewport")
        const bounds = videoBounds()
        const sidebar = findChild(player, "redesignLibraryPanel")
        mouseClick(findChild(player, "libraryToggleButton"))
        verify(player.libraryOpen)
        tryCompare(sidebar, "reveal", 1)
        compare(videoBounds(), bounds)
        verify(sidebar.x < viewport.x + viewport.width && sidebar.x + sidebar.width > viewport.x)
        mouseClick(findChild(player, "queueButton"))
        verify(player.queueOpen)
        compare(videoBounds(), bounds)
        mouseClick(findChild(player, "playbackSettingsButton"))
        compare(player.settingsPage, "playback")
        verify(!player.queueOpen)
        compare(videoBounds(), bounds)
        player.forceActiveFocus()
        keyClick(Qt.Key_Escape)
        verify(!player.libraryOpen)
        compare(player.settingsPage, "")
        tryCompare(sidebar, "reveal", 0)
        compare(videoBounds(), bounds)
        trackPicker.open()
        wait(30)
        compare(videoBounds(), bounds)
    }
    function test_sidebarSlideAndIconStaySynchronized() {
        const sidebar = findChild(player, "redesignLibraryPanel")
        const button = findChild(player, "libraryToggleButton")
        const bounds = videoBounds()
        player.libraryOpen = true
        wait(32)
        verify(sidebar.reveal > 0 && sidebar.reveal < 1)
        verify(sidebar.x < 0 && sidebar.x > -sidebar.width)
        compare(button.iconReveal, sidebar.reveal)
        compare(videoBounds(), bounds)
        // A second click reverses an unfinished slide without a jump in video size.
        player.libraryOpen = false
        verify(sidebar.visible)
        tryCompare(sidebar, "reveal", 0)
        verify(!sidebar.visible)
        compare(button.iconReveal, 0)
        compare(videoBounds(), bounds)
        player.libraryOpen = true
        tryCompare(sidebar, "reveal", 1)
        compare(sidebar.x, 0)
        compare(button.iconReveal, 1)
    }
    function test_sidebarKeepsItsPageWhenClosed() {
        const sidebar = findChild(player, "redesignLibraryPanel")
        player.libraryOpen = true
        tryCompare(sidebar, "reveal", 1)
        const content = player.libraryItem
        content.category = 4
        player.libraryOpen = false
        tryCompare(sidebar, "reveal", 0)
        compare(player.libraryItem, content)
        player.libraryOpen = true
        tryCompare(sidebar, "reveal", 1)
        compare(player.libraryItem.category, 4)
    }
    function test_overlaysKeepVideoBoundsInFullscreenAndSmallWindow_data() {
        return [{tag: "fullscreen", fullscreen: true, width: 1280, height: 720},
                {tag: "small-window", fullscreen: false, width: 640, height: 360}]
    }
    function test_overlaysKeepVideoBoundsInFullscreenAndSmallWindow(data) {
        player.fullscreen = data.fullscreen
        testCase.width = data.width; testCase.height = data.height
        wait(20)
        const bounds = videoBounds()
        player.libraryOpen = true
        player.settingsPage = "audio"
        wait(170)
        compare(videoBounds(), bounds)
        player.settingsPage = "subtitles"
        compare(videoBounds(), bounds)
        player.settingsPage = ""
        player.queueOpen = true
        compare(videoBounds(), bounds)
    }
    function test_minimumWindowAndScaling() {
        testCase.width = 640
        testCase.height = 360
        wait(20)
        const seek = findChild(player, "seekBar")
        verify(seek.width >= 40)
        const queue = findChild(player, "queueButton")
        verify(queue.x + queue.width <= queue.parent.width)
        testCase.width = 1920
        testCase.height = 1080
        player.scaleFactor = 1.5
        wait(20)
        compare(findChild(player, "redesignToolbar").height, 81)
        verify(findChild(player, "redesignViewport").height > 0)
    }
    function test_pictureUsesClientPlaneAndMiniPlayerKeepsItsOwnSurface() {
        testCase.width = 1920; testCase.height = 1080
        wait(20)
        const video = player.videoItem
        for (const mode of ["normal", "minimal", "fullscreen"]) {
            player.minimalView = mode === "minimal"
            player.fullscreen = mode === "fullscreen"
            wait(20)
            compare(video.mapToItem(player, 0, 0), Qt.point(0, 0))
            compare(video.width, player.width)
            compare(video.height, player.height)
            const viewport = findChild(player, "redesignViewport")
            compare(player.subtitleArea, Qt.rect(viewport.x / player.width, viewport.y / player.height,
                                                 viewport.width / player.width, viewport.height / player.height))
        }
        player.fullscreen = false; player.minimalView = false
        testCase.width = 640; testCase.height = 360
        wait(20)
        compare(video.mapToItem(player, 0, 0), Qt.point(0, 0))
        compare(video.width, player.width)
        compare(video.height, player.height)
        const mini = openMiniPlayer()
        compare(player.videoItem, video)
        compare(video.mapToItem(mini.videoArea, 0, 0), Qt.point(0, 0))
        compare(video.width, mini.videoArea.width)
        compare(video.height, mini.videoArea.height)
        compare(player.subtitleArea, Qt.rect(0, 0, 1, 1))
        player.libraryOpen = false
        wait(20)
        compare(video.mapToItem(player, 0, 0), Qt.point(0, 0))
        compare(video.width, player.width)
        compare(video.height, player.height)
    }
    function test_picturePlaneDoesNotReceiveBorderOrChromeClicks() {
        consumeVideoHover = true
        const video = player.videoItem
        const spy = createTemporaryObject(videoClickProbe, testCase, {target: video})
        mouseClick(player, 5, 180)
        compare(spy.count, 0)
        mouseClick(player, 20, 180)
        compare(spy.count, 1)
        mouseClick(findChild(player, "playButton"))
        compare(spy.count, 1)
        verify(player.playing)
    }
    function test_lightPaletteAndExpandedLibrary() {
        player.lightTheme = true
        compare(player.theme.background.toString(), "#acacac")
        compare(player.theme.panel.toString(), "#c3c3c3")
        player.libraryOpen = true
        player.libraryExpanded = true
        wait(20)
        const viewport = findChild(player, "redesignViewport")
        compare(viewport.width, 371)
        compare(viewport.height, 371 * 9 / 16)
        player.libraryOpen = false
        verify(!player.libraryExpanded)
    }
    function openMiniPlayer() {
        player.libraryOpen = true
        player.libraryExpanded = true
        wait(20)
        const mini = findChild(player, "redesignMiniPlayer")
        verify(mini.visible)
        return mini
    }
    function test_miniPlayerMatchesCardAndKeepsOneVideoSurface() {
        consumeVideoHover = true
        const video = player.videoItem
        const mini = openMiniPlayer()
        compare(mini.width, 391)
        compare(mini.height, 339)
        verify(!findChild(player, "redesignToolbar").visible)
        compare(findChild(player, "redesignLibraryPanel").height, player.height - 40)
        compare(findChild(player, "redesignViewport").parent, mini.videoArea)
        compare(player.videoItem, video)
        player.artist = "Artist"
        verify(waitForRendering(mini))
        mouseClick(findChild(mini, "miniPlayerFolderButton"))
        compare(folderSpy.count, 1)
        mouseClick(findChild(mini, "miniPlayerArtistButton"))
        compare(artistSpy.count, 1)
        mouseClick(findChild(mini, "miniPlayerQueueButton"))
        compare(player.queueOpen, true)
        player.queueOpen = false
        mouseMove(mini, 160, 80)
        const play = findChild(mini, "miniPlayerPlayButton")
        tryVerify(() => play.visible)
        mouseClick(play)
        compare(player.playing, true)
        mouseClick(findChild(mini, "miniPlayerCloseButton"))
        verify(!mini.visible)
        verify(player.started && player.playing)
        player.libraryExpanded = false
        player.libraryExpanded = true
        wait(20)
        mouseMove(mini, 160, 80)
        mouseClick(findChild(mini, "miniPlayerExpandButton"))
        compare(player.libraryOpen, false)
        compare(player.libraryExpanded, false)
        compare(player.videoItem, video)
        compare(findChild(player, "redesignViewport").parent, player.backdropSource)
        // Reparenting the surface must leave the compact library above video.
        player.libraryOpen = true
        wait(220)
        mouseClick(findChild(player.libraryItem, "libraryCategory2"))
        compare(compactCategorySpy.count, 1)
    }
    function test_audioWavebarFollowsTheMiniPlayer() {
        player.hasVideo = false
        player.audioWaveformEnabled = true
        player.audioWaveformPeaks = Array(2048).fill(.8)
        const mini = openMiniPlayer()
        const seek = findChild(mini, "miniPlayerSeekBar")
        verify(seek.waveformActive)
        compare(seek.handle.width, 4)
        verify(findChild(seek, "audioWaveform").children[0].height <= seek.availableHeight)
        player.audioWaveformPeaks = []
        verify(!seek.waveformActive)
        compare(seek.handle.width, 8)
    }
    function test_miniPlayerDragMovesVideoWithoutSeekingAndStaysInBounds() {
        const mini = openMiniPlayer()
        const before = player.position
        const title = findChild(mini, "miniPlayerTitle")
        mousePress(title, 90, title.height / 2)
        mouseMove(testCase, 400, 350, 50)
        mouseMove(testCase, 60, 100, 50)
        mouseRelease(testCase, 60, 100)
        verify(mini.userMoved)
        verify(mini.x < player.width / 2)
        verify(mini.x >= 6 && mini.y >= 46)
        compare(player.position, before)
        compare(seekSpy.count, 0)
        const viewport = findChild(player, "redesignViewport")
        compare(viewport.mapToItem(player, 0, 0).x, mini.x + 10)
        compare(viewport.mapToItem(player, 0, 0).y, mini.y + 10)
        testCase.width = 640; testCase.height = 360
        wait(20)
        verify(mini.x + mini.width <= player.width - 6)
        verify(mini.y + mini.height <= player.height - 6)
    }
    function test_miniQueueFollowsCardAndKeepsVideoBounds() {
        testCase.width = 1920; testCase.height = 1080
        const mini = openMiniPlayer()
        mini.userMoved = false
        const viewport = findChild(player, "redesignViewport")
        const before = viewport.mapToItem(player, 0, 0)
        mouseClick(findChild(mini, "miniPlayerQueueButton"))
        const queue = findChild(player, "redesignFloatingQueue")
        verify(queue.visible)
        compare(queue.width, 391)
        compare(queue.height, 356)
        compare(queue.x, mini.x)
        compare(queue.y + queue.height, mini.y)
        compare(viewport.mapToItem(player, 0, 0), before)
        verify(!findChild(player, "redesignRightPanel").visible)
        const title = findChild(mini, "miniPlayerTitle")
        mouseDrag(title, 80, title.height / 2, -700, -200)
        compare(queue.x, mini.x)
        compare(queue.y + queue.height, mini.y)
        verify(queue.y >= 46)
        compare(viewport.mapToItem(player, 0, 0).x, mini.x + 10)
        compare(viewport.mapToItem(player, 0, 0).y, mini.y + 10)
        player.libraryExpanded = false
        verify(!queue.visible)
        verify(findChild(player, "redesignRightPanel").visible)
        player.libraryExpanded = true
        mouseMove(mini, 160, 80)
        mouseClick(findChild(mini, "miniPlayerCloseButton"))
        verify(!player.queueOpen)
        verify(!queue.visible)
    }
    function test_miniSeekHoldsUntilReleaseAndCancelsWhenClosed() {
        const mini = openMiniPlayer()
        mini.draggedX = 100; mini.draggedY = 100; mini.userMoved = true
        const seek = findChild(mini, "miniPlayerSeekBar")
        mousePress(seek, seek.width / 2, seek.height / 2)
        compare(seekEventOrder[0], "start")
        mouseMove(seek, seek.width + 100, seek.height / 2, 40)
        compare(player.position, 1)
        compare(seekDragFinishSpy.count, 0)
        mouseRelease(seek, seek.width + 100, seek.height / 2)
        tryCompare(seekDragFinishSpy, "count", 1)
        compare(seekDragFinishSpy.signalArguments[0][1], true)
        seekDragFinishSpy.clear()
        mousePress(seek, seek.width / 2, seek.height / 2)
        player.libraryExpanded = false
        tryCompare(seekDragFinishSpy, "count", 1)
        compare(seekDragFinishSpy.signalArguments[0][1], false)
        mouseRelease(testCase, 600, 400)
    }
    function test_audioAfterVideoRestoresHiddenControls() {
        player.controlsHideTimeout = 50
        player.fullscreen = true
        player.autoHideMode = 2
        player.forceActiveFocus()
        mouseMove(player, player.width / 2, player.height / 2)
        tryCompare(player, "bottomVisible", false)
        player.controlsHideTimeout = 1800
        player.hasVideo = false
        tryCompare(player, "bottomVisible", true)
        player.hasVideo = true
        player.revealControls()
        verify(player.bottomVisible)
        player.started = false
        verify(player.bottomVisible)
    }
    function test_resumeOffersSavedTimeAndSeparateActions_data() {
        return [
            {tag: "video", audioOnly: false, caption: "Continue Watching"},
            {tag: "audio", audioOnly: true, caption: "Continue listening"}
        ]
    }
    function test_resumeOffersSavedTimeAndSeparateActions(data) {
        player.audioOnly = data.audioOnly
        player.resumeTimeText = "00:12:34"
        player.resumeAvailable = true
        const prompt = findChild(player, "resumePlaybackPrompt")
        const label = findChild(player, "resumePlaybackLabel")
        tryCompare(prompt, "visible", true)
        compare(findChild(player, "resumeContinueButton").caption, data.caption)
        compare(prompt.Accessible.name, data.caption)
        verify(label.text.includes("00:12:34"))
        verify(!label.truncated)
        mouseClick(findChild(player, "resumeContinueButton"))
        compare(resumeSpy.count, 1)
        compare(resumeSpy.signalArguments[0][0], true)
        mouseClick(findChild(player, "resumeDismissButton"))
        compare(resumeSpy.count, 2)
        compare(resumeSpy.signalArguments[1][0], false)
        player.resumeTimeText = ""
        verify(label.text.length > 0)
        verify(!label.text.includes("00:00:00"))
        player.resumeAvailable = false
    }
    function test_minimalViewHidesChromeAndClosesPanels() {
        player.resumeAvailable = true
        player.showStatistics = true
        player.libraryOpen = true
        player.queueOpen = true
        player.settingsPage = "video"
        player.minimalView = true
        verify(!player.topVisible)
        verify(!player.bottomVisible)
        verify(!player.libraryOpen)
        verify(!player.queueOpen)
        verify(!findChild(player, "resumePlaybackPrompt").visible)
        verify(!findChild(player, "playbackStatisticsOverlay").visible)
        compare(player.settingsPage, "")
        const viewport = findChild(player, "redesignViewport")
        compare(Qt.rect(viewport.x, viewport.y, viewport.width, viewport.height), Qt.rect(10, 10, player.width - 20, player.height - 20))
        player.fullscreen = true
        compare(Qt.rect(viewport.x, viewport.y, viewport.width, viewport.height), Qt.rect(0, 0, player.width, player.height))
        player.fullscreen = false
        player.revealControls()
        verify(!player.topVisible)
        verify(!player.bottomVisible)
        player.minimalView = false
        verify(player.topVisible)
        verify(player.bottomVisible)
        player.resumeAvailable = false
    }
    function test_fullscreenUsesIndependentEdges() {
        player.controlsHideTimeout = 50
        player.fullscreen = true
        player.forceActiveFocus()
        mouseMove(player, player.width / 2, player.height / 2)
        tryCompare(player, "bottomVisible", false)
        verify(!player.topVisible)
        player.controlsHideTimeout = 1800
        mouseMove(player, player.width / 2, 2)
        tryCompare(player, "topVisible", true)
        verify(!player.bottomVisible)
        mouseMove(player, player.width / 2, player.height - 2)
        tryCompare(player, "bottomVisible", true)
    }
    function test_fullscreenSensitivityFiltersSmallMotion_data() {
        return [{tag: "top", top: true, videoHover: false, scale: 1},
                {tag: "bottom", top: false, videoHover: false, scale: 1},
                {tag: "top over video", top: true, videoHover: true, scale: 1.5},
                {tag: "bottom over video", top: false, videoHover: true, scale: 1.5}]
    }
    function test_fullscreenSensitivityFiltersSmallMotion(data) {
        consumeVideoHover = data.videoHover
        player.scaleFactor = data.scale
        player.controlsHideTimeout = 50
        player.fullscreenControlsSensitivity = 32
        player.fullscreen = true
        player.forceActiveFocus()
        mouseMove(player, 600, player.height / 2)
        wait(80)
        const boundary = data.top ? player.theme.dp(48) : player.height - player.controlsHeight - player.theme.dp(10)
        const outside = boundary + (data.top ? 1 : -1)
        const inside = boundary + (data.top ? -1 : 1)
        const target = data.top ? "topVisible" : "bottomVisible"
        const other = data.top ? "bottomVisible" : "topVisible"
        mouseMove(player, 400, outside); wait(20)
        verify(!player[target]); verify(!player[other])
        mouseMove(player, 401, inside); wait(20)
        verify(!player[target], "Low sensitivity must ignore a small movement into an edge")
        mouseMove(player, 450, inside)
        tryCompare(player, target, true)
        verify(!player[other], "Movement at one edge must not reveal the other controls")
        mouseMove(player, 500, outside)
        tryCompare(player, target, false)
        player.fullscreenControlsSensitivity = 0
        mouseMove(player, 501, inside)
        tryCompare(player, target, true)
        verify(!player[other])
    }
    function test_fullscreenTimelineBoundaryDoesNotFlicker_data() {
        return [{tag: "plain", videoHover: false}, {tag: "video-surface-hover", videoHover: true}]
    }
    function test_fullscreenTimelineBoundaryDoesNotFlicker(data) {
        consumeVideoHover = data.videoHover
        player.controlsHideTimeout = 50
        player.fullscreen = true
        player.forceActiveFocus()
        mouseMove(player, player.width / 2, player.height / 2)
        tryCompare(player, "bottomVisible", false)
        player.controlsHideTimeout = 1800
        const seek = findChild(player, "seekBar")
        verify(seek)
        mouseMove(player, player.width / 2, player.height - 2)
        tryCompare(player, "bottomVisible", true)
        toolbarVisibilitySpy.clear()
        for (let i = 0; i < 20; ++i) {
            mouseMove(seek, seek.width / 2, -1)
            wait(10)
            mouseMove(seek, seek.width / 2, 1)
            wait(10)
            mouseMove(seek, seek.width / 2, seek.height - 1)
            wait(10)
            mouseMove(seek, seek.width / 2, seek.height + 1)
            wait(10)
        }
        compare(toolbarVisibilitySpy.count, 0)
        verify(player.bottomVisible)
        player.controlsHideTimeout = 50
        mouseClick(findChild(player, "playButton"))
        verify(player.playing)
        player.forceActiveFocus()
        mouseMove(player, player.width / 2, player.height / 2)
        tryCompare(player, "bottomVisible", false)
    }
}
