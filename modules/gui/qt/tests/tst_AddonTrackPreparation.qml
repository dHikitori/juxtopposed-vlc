// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: testCase
    name: "AddonTrackPreparation"
    width: 1200; height: 900; visible: true; when: windowShown
    readonly property string media: "file:///videos/Movie.mkv"
    readonly property string remote: "addon-subtitle:fixture-123"
    readonly property string cached: "file:///cache/fixture-123/Movie.en.srt"
    property int nativeSaves: 0
    property var savedRows: []
    QtObject {
        id: service
        property int calls: 0
        property int canceled: 0
        property int nextJob: 100
        property var lastRows: []
        signal preparationFinished(double request, var result)
        function prepareRows(rows) { calls++; lastRows = rows; return ++nextJob }
        function cancelPreparation() { canceled++ }
    }
    RedesignAddonTrackPreparation {
        id: preparation
        sourceService: service
        onPrepared: (rows, subtitles, request, resolved) => {
            dialog.resolveSources(resolved)
            testCase.nativeSaves++; testCase.savedRows = rows
            dialog.finish(request, {success: true, saved: rows})
        }
        onFailed: (request, error, resolved) => {
            dialog.resolveSources(resolved)
            dialog.finish(request, {success: false, error: error, saved: []})
        }
    }
    RedesignTrackAssignmentDialog {
        id: dialog
        parent: testCase
        onMatchRequested: (media, tracks, includeSaved, request) => loadMatches(request, {}, includeSaved)
        onSaveRequested: (rows, subtitles, request) => {
            cancelableSave = preparation.requiresPreparation(rows)
            preparation.prepare(rows, subtitles, request)
        }
        onClosed: preparation.cancel()
    }
    function init() {
        nativeSaves = 0; savedRows = []; service.calls = 0; service.canceled = 0
        verify(dialog.begin({scope: "file", url: media, urls: [media]}, true))
        tryCompare(dialog, "opened", true)
        dialog.loadRows(dialog.requestId, {success: true, rows: [{url: media, name: "Movie.mkv", baseline: []}]})
        dialog.loadSources(dialog.sourceRequestId, {success: true, sources: []})
    }
    function cleanup() { dialog.saving = false; dialog.close(); tryCompare(dialog, "visible", false); preparation.cancel() }
    function test_savePreparesAddonHandleBeforeNativeAssignment() {
        dialog.setTracks(media, [remote])
        dialog.submit()
        compare(service.calls, 1); compare(service.lastRows[0].tracks[0], remote)
        compare(nativeSaves, 0); verify(preparation.active)
        service.preparationFinished(preparation.pendingPreparation, {success: true, rows: [{url: media, tracks: [cached]}], resolved: {[remote]: cached}})
        compare(nativeSaves, 1); compare(savedRows[0].tracks[0], cached)
        verify(!preparation.active)
    }
    function test_localTrackDoesNotDownload() {
        dialog.setTracks(media, [cached]); dialog.submit()
        compare(service.calls, 0); compare(nativeSaves, 1)
        compare(savedRows[0].tracks[0], cached)
    }
    function test_cancelIgnoresLatePreparationResult() {
        dialog.setTracks(media, [remote]); dialog.submit()
        const job = preparation.pendingPreparation
        dialog.close(); tryCompare(dialog, "visible", false)
        compare(service.canceled, 1)
        service.preparationFinished(job, {success: true, rows: [{url: media, tracks: [cached]}], resolved: {[remote]: cached}})
        compare(nativeSaves, 0)
    }
    function test_failedPreparationLeavesDraftForRetry() {
        dialog.setTracks(media, [remote]); dialog.submit()
        service.preparationFinished(preparation.pendingPreparation, {success: false, rows: [], resolved: {}, error: "Try again"})
        compare(nativeSaves, 0); verify(!dialog.saving)
        compare(dialog.tracks(media)[0], remote); verify(dialog.visible)
        dialog.submit(); compare(service.calls, 2)
    }
    function test_addonHandleIsOnlineAndRequiresFolderPreparation() {
        compare(dialog.sourceKind(remote), 1)
        dialog.selectScope(1)
        dialog.loadRows(dialog.requestId, {success: true, rows: [{url: media, name: "Movie.mkv", baseline: []}]})
        dialog.loadSources(dialog.sourceRequestId, {success: true, sources: []})
        dialog.setTracks(media, [remote])
        verify(dialog.folderRuleNeedsPreparation)
        verify(dialog.folderRuleChanged)
    }
}
