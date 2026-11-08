// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "JuxtopposedVolume"
    width: 1280; height: 720
    visible: true; when: windowShown
    property var wheelEvents: []
    RedesignPlayerView {
        id: player
        anchors.fill: parent
        started: true; hasVideo: true
        volume: 0.75
        maxVolume: 1.25
        onVolumeRequested: value => volume = value
        onVolumeWheelRequested: event => testCase.wheelEvents.push({x: event.angleDelta.x, y: event.angleDelta.y})
    }
    SignalSpy { id: moved; target: player; signalName: "volumeRequested" }
    SignalSpy { id: mute; target: player; signalName: "muteRequested" }
    function slider() { return findChild(player, "volumeSlider") }
    function init() {
        slider().focus = false
        testCase.forceActiveFocus()
        player.presentationOnly = false
        player.volume = 0.75
        player.muted = false
        player.maxVolume = 1.25
        wheelEvents = []; moved.clear(); mute.clear()
        mouseMove(testCase, 600, 300)
    }
    function test_coneForwardsVerticalAndHorizontalWheelInput() {
        mouseWheel(slider(), 40, 14, 0, 120)
        compare(wheelEvents.length, 1); compare(wheelEvents[0].y, 120)
        mouseWheel(slider(), 40, 14, 0, -240)
        compare(wheelEvents.length, 2); compare(wheelEvents[1].y, -240)
        mouseWheel(slider(), 40, 14, 120, 0)
        compare(wheelEvents.length, 3); compare(wheelEvents[2].x, 120)
        compare(moved.count, 0) // Scrolling does not use the clamped drag value.
    }
    function test_presentationDoesNotChangeVolume() {
        player.presentationOnly = true
        mouseWheel(slider(), 40, 14, 0, 120)
        compare(wheelEvents.length, 0)
    }
    function test_smallWheelDeltasAreNotRoundedToFullSteps() {
        for (let i = 0; i < 8; ++i)
            mouseWheel(slider(), 40, 14, 0, 15)
        compare(wheelEvents.length, 8)
        compare(wheelEvents.reduce((sum, event) => sum + event.y, 0), 120)
        compare(moved.count, 0)
    }
    function test_muteButtonForwardsWheelAndStillAcceptsClicks() {
        const button = findChild(player, "muteButton")
        mouseWheel(button, button.width / 2, button.height / 2, 0, 120)
        compare(wheelEvents.length, 1); compare(wheelEvents[0].y, 120)
        compare(mute.count, 0)
        mouseClick(button)
        compare(mute.count, 1)
    }
    function test_draggingStillWorks() {
        mousePress(slider(), 20, 14)
        mouseMove(slider(), 70, 14)
        mouseRelease(slider(), 70, 14)
        verify(moved.count > 0)
        verify(player.volume > 0.75 && player.volume <= player.maxVolume)
    }
    function test_unavailableVolumeDoesNotShowOrSendANegativeLevel() {
        player.volume = -1
        const control = slider()
        verify(!control.enabled)
        compare(findChild(control, "redesignToolTip").text, "Volume")
        compare(findChild(player, "muteButton").iconName, "volume")
        mouseWheel(control, 40, 14, 0, 120)
        mouseClick(control, 50, 14)
        compare(wheelEvents.length, 0)
        compare(moved.count, 0)
        player.volume = 0.5
        verify(control.enabled)
        compare(findChild(control, "redesignToolTip").text, "50%")
        mouseWheel(control, 40, 14, 0, 120)
        compare(wheelEvents.length, 1)
    }
    function test_fillIsVisibleInsideTheCone() {
        const control = slider()
        tryCompare(findChild(control, "volumeConeFill"), "status", Image.Ready)
        verify(waitForRendering(control))
        // Under load the first frame can predate the loaded fill: grab again
        // until it shows, then check the unfilled part of that same frame.
        let image = null, filled = null, empty = null
        tryVerify(() => {
            image = testImages.grabWindow(testCase)
            filled = image.point(control, 10, 14); empty = image.point(control, 80, 14)
            return image.green(filled.x, filled.y) > 100
        }, 5000, "The filled cone must be visible")
        verify(image.red(empty.x, empty.y) < 20, "The unfilled cone must stay dark")
    }
    function test_updatesKeepTheLoadedArtworkAndShowActualVolume() {
        const control = slider()
        const fill = findChild(control, "volumeConeFill")
        const empty = findChild(control, "volumeConeBackground")
        const clip = findChild(control, "volumeConeClip")
        tryCompare(fill, "status", Image.Ready)
        tryCompare(empty, "status", Image.Ready)
        const fillSource = fill.source, emptySource = empty.source
        for (const level of [0, 0.25, 0.75, 1.25, 2, 1, 0.5]) {
            player.volume = level
            compare(fill.source, fillSource)
            compare(empty.source, emptySource)
            compare(fill.status, Image.Ready)
            compare(empty.status, Image.Ready)
            compare(control.currentVolume, level)
            fuzzyCompare(clip.width, Math.min(level / player.maxVolume, 1) * (control.width - 4), 0.01)
            compare(findChild(control, "redesignToolTip").text, Math.round(level * 100) + "%")
        }
        player.muted = true
        compare(fill.source, fillSource); compare(empty.source, emptySource)
        // A decrease from above the cone's range uses the actual backend level.
        player.volume = 2
        mouseWheel(control, 40, 14, 0, -120)
        compare(wheelEvents.length, 1)
        compare(moved.count, 0)
        compare(control.currentVolume, 2)
    }
}
