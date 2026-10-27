// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    name: "AddonSessions"
    width: 760; height: 1100; visible: true; when: windowShown
    QtObject {
        id: service
        property var controllers: [{id: "org.example.session/controller", name: "<b>Controller</b>", origins: ["wss://relay.example"], operations: ["pause", "seek"]}]
        property var sessions: []
        property var review: ({})
        property bool preparing: false
        property bool starting: false
        property string errorMessage: ""
        property string status: ""
        property int revision: 0
        property int preparations: 0
        property int accepts: 0
        property int resumes: 0
        property int ends: 0
        property int closed: 0
        property var selected: []
        function prepare(id, relay, room) {
            preparations++
            review = {name: "<b>Controller</b>", relay: "wss://relay.example", room: room, observationFields: ["positionUs", "paused"],
                operations: ["pause", "seek"], continuesAfterViewClose: true}
            revision++; return true
        }
        function accept(operations) { accepts++; selected = operations.slice(); review = ({}); revision++; return true }
        function cancelReview() { review = ({}); revision++ }
        function viewClosed() { closed++; cancelReview() }
        function resume(id) { resumes++; return true }
        function end(id) { ends++; sessions = []; revision++; return true }
    }
    RedesignAddonSessions { id: page; width: 650; backend: service }
    function item(name) { return findChild(page, name) }
    function readyReview() {
        item("addonSessionRelay").text = "wss://relay.example/room"
        item("addonSessionRoom").text = "<i>Room</i>"
        mouseClick(item("addonSessionPrepare")); wait(20)
    }
    function init() {
        page.visible = true; page.active = true; page.lightTheme = false
        service.review = ({}); service.sessions = []; service.preparing = false; service.starting = false
        service.errorMessage = ""; service.status = ""; service.preparations = 0; service.accepts = 0
        service.resumes = 0; service.ends = 0; service.closed = 0; service.selected = []; service.revision++
        item("addonSessionRelay").text = ""; item("addonSessionRoom").text = ""; wait(20)
    }
    function test_noAutomaticPrepareOrApproval() {
        compare(service.preparations, 0); compare(service.accepts, 0)
        readyReview(); compare(service.preparations, 1); compare(service.accepts, 0)
        compare(page.selectedOperations.length, 0); verify(!item("addonSessionAllow_pause").checked)
        mouseClick(item("addonSessionAccept")); compare(service.accepts, 1); compare(service.selected.length, 0)
    }
    function test_onlyExplicitPlaybackChoicesAreSent() {
        readyReview(); mouseClick(item("addonSessionAllow_pause")); compare(page.selectedOperations.join(","), "pause")
        mouseClick(item("addonSessionAccept")); compare(service.selected.join(","), "pause")
        readyReview(); compare(page.selectedOperations.length, 0)
    }
    function test_capturedNamesAndRoomStayPlain() {
        readyReview()
        compare(item("addonSessionReviewName").textFormat, Text.PlainText)
        compare(item("addonSessionReviewName").text, "<b>Controller</b>")
        compare(item("addonSessionReviewRoom").textFormat, Text.PlainText)
        verify(item("addonSessionReviewRoom").text.includes("<i>Room</i>"))
        compare(item("addonSessionController").contentItem.textFormat, Text.PlainText)
        compare(item("addonSessionReviewFields").textFormat, Text.PlainText)
        verify(item("addonSessionBackground").text.includes("continues"))
    }
    function test_closingCancelsReviewWithoutEndingApprovedSession() {
        readyReview(); page.active = false; compare(service.closed, 1); verify(Object.keys(service.review).length === 0); compare(service.ends, 0)
        page.active = true; readyReview(); page.visible = false; compare(service.closed, 2); compare(service.ends, 0)
    }
    function test_pendingAndSuspendedSessionsKeepHostControls() {
        service.sessions = [{id: "approved", name: "<b>Controller</b>", room: "<i>Room</i>", starting: false, suspended: true, connected: true}]
        wait(20); compare(service.resumes, 0); compare(service.ends, 0)
        compare(item("addonSessionActiveLabel").textFormat, Text.PlainText)
        mouseClick(item("addonSessionResume_approved")); compare(service.resumes, 1)
        mouseClick(item("addonSessionEnd_approved")); compare(service.ends, 1)
    }
    function test_sharedThemeAndStatusLabels() {
        const before = item("addonSessionRelay").background.color.toString()
        page.lightTheme = true; wait(20)
        compare(item("addonSessionReviewName").color.toString(), page.theme.settingsHeading.toString())
        compare(item("addonSessionRelay").background.color.toString(), page.theme.field.toString())
        verify(item("addonSessionRelay").background.color.toString() !== before)
        service.errorMessage = "<b>Unavailable</b>"; compare(item("addonSessionError").textFormat, Text.PlainText)
        compare(item("addonSessionError").text, "<b>Unavailable</b>")
        compare(item("addonSessionRelay").maximumLength, 2048); compare(item("addonSessionRoom").maximumLength, 256)
    }
}
