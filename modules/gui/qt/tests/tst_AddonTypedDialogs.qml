// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: test
    name: "AddonTypedDialogs"
    width: 1200; height: 850; visible: true; when: windowShown
    readonly property string media: "file:///private/Movie.mkv"
    readonly property string handle: "addon-subtitle:host-reference"
    property bool prepareSelection: false
    QtObject {
        id: registry
        property int revision: 0
        property int serial: 0
        property var selected: ({})
        property var sessions: ({})
        property var closed: []
        property bool scopeAllowed: true
        property bool schemaAllowed: true
        property bool available: true
        property bool loading: false
        signal changed()
        signal availabilityChanged()
        signal sessionChanged(string id)
        signal sessionFailed(string id)
        function setAvailability(current, pending) { available = current; loading = pending; availabilityChanged() }
        function replacement(hook) { return selected[hook] ? {id: selected[hook], selectionTitlesAllowed: false} : ({}) }
        function canPrepareSelection(id, hook) { return scopeAllowed && selected[hook] === id }
        function canUseSelection(id, hook) { return available && scopeAllowed && selected[hook] === id }
        function validateSessionResult(id, result) { return available && schemaAllowed && !!sessions[id] }
        function selectReplacement(hook, id) { selected = Object.assign({}, selected, {[hook]: id}); ++revision; changed(); return true }
        function sections(hook) { return [] }
        function createSession(id, parameters, context) {
            const key = "session-" + (++serial)
            sessions = Object.assign({}, sessions, {[key]: {id: id, document: {contract: "ui@1", id: "picker", root: {type: "Text", text: "Choices"}},
                state: {parameters: JSON.parse(JSON.stringify(parameters))}, registeredCommands: [], messages: {}, assetUrls: {}}})
            return key
        }
        function session(id) { return sessions[id] || ({}) }
        function closeSession(id) { closed = closed.concat([id]); const copy = Object.assign({}, sessions); delete copy[id]; sessions = copy }
        function suspendSession(id, value) {}
        function acknowledgeSession(id) {}
        function reportViewFailure(id) { for (const hook of Object.keys(selected)) if (selected[hook] === id) selectReplacement(hook, "") }
        function result(picker, value) {
            const mount = test.findChild(picker, "addonSelectionMount"), id = mount.sessionId
            const copy = JSON.parse(JSON.stringify(sessions)); copy[id].state.result = value
            sessions = copy; sessionChanged(id)
        }
    }
    ListModel {
        id: playlists
        property bool deferred: false
        property var callbacks: []
        property var returnedRows: null
        function getDataFlat(indices, callback) {
            if (deferred) callbacks = callbacks.concat([callback])
            else callback(returnedRows || indices.map(index => get(index)))
        }
    }
    RedesignAddToPlaylistDialog { id: playlistDialog; parent: test; playlistModel: playlists; addonRegistry: registry }
    RedesignTrackAssignmentDialog {
        id: subtitleDialog; parent: test; addonRegistry: registry
        onMatchRequested: (media, tracks, includeSaved, request) => subtitleDialog.loadMatches(request, {}, includeSaved)
        onSaveRequested: (rows, subtitles, request) => { if (test.prepareSelection) preparation.prepare(rows, subtitles, request) }
        onClosed: preparation.cancel()
    }
    QtObject {
        id: sourceService
        property int serial: 0
        property var rows: []
        signal preparationFinished(int request, var result)
        function prepareRows(value) { rows = JSON.parse(JSON.stringify(value)); return ++serial }
        function cancelPreparation() {}
    }
    RedesignAddonTrackPreparation { id: preparation; sourceService: sourceService }
    SignalSpy { id: playlistSave; target: playlistDialog; signalName: "saveRequested" }
    SignalSpy { id: subtitleSave; target: subtitleDialog; signalName: "saveRequested" }
    SignalSpy { id: prepared; target: preparation; signalName: "prepared" }
    function init() {
        playlistDialog.busy = false; playlistDialog.close(); subtitleDialog.saving = false; subtitleDialog.close()
        tryVerify(() => !playlistDialog.visible && !subtitleDialog.visible)
        preparation.cancel(); prepareSelection = false
        registry.sessions = ({}); registry.closed = []; registry.selected = ({}); registry.scopeAllowed = true; registry.schemaAllowed = true
        registry.available = true; registry.loading = false
        ++registry.revision; registry.changed()
        playlists.deferred = false; playlists.callbacks = []; playlists.returnedRows = null; playlists.clear()
        playlists.append([{id: "playlist-17", name: "Films", thumbnail: "", readOnly: false},
            {id: "playlist-23", name: "Films", thumbnail: "", readOnly: false}, {id: "playlist-31", name: "Imported", thumbnail: "", readOnly: true}])
        playlistSave.clear(); subtitleSave.clear(); prepared.clear(); sourceService.rows = []
    }
    function cleanup() {
        playlistDialog.busy = false; playlistDialog.close(); subtitleDialog.saving = false; subtitleDialog.close()
        preparation.cancel()
    }
    function playlist() {
        registry.selectReplacement("dialog.add-to-playlist.content@1", "org.example.ui/playlist")
        verify(playlistDialog.begin(2)); tryVerify(() => playlistDialog.opened)
        const picker = findChild(playlistDialog, "addonPlaylistSelection")
        tryVerify(() => picker.usingReplacement)
        return picker
    }
    function builtinPlaylist() {
        const picker = playlist()
        findChild(playlistDialog, "addonPlaylistRecovery").clicked()
        tryVerify(() => !picker.usingReplacement && !playlistDialog.addonWanted)
        const list = findChild(playlistDialog, "playlistDestinations")
        list.forceLayout()
        tryVerify(() => list.itemAtIndex(1) !== null && list.itemAtIndex(1).key === playlists.get(1).id)
        return list
    }
    function subtitles(initialTracks, extraRows) {
        registry.selectReplacement("subtitles.search.content@1", "org.example.ui/subtitles")
        subtitleDialog.providerConfigured = true
        verify(subtitleDialog.begin(extraRows ? {scope: "playlist", key: "native-playlist"} : {scope: "file", url: media}, true)); tryVerify(() => subtitleDialog.opened)
        subtitleDialog.loadRows(subtitleDialog.requestId, {success: true, rows: [{url: media, name: "Private movie.mkv", baseline: initialTracks || []}].concat(extraRows || [])})
        subtitleDialog.loadSources(subtitleDialog.sourceRequestId, {success: true, sources: []})
        subtitleDialog.selectSource(1)
        subtitleDialog.loadSearch(subtitleDialog.searchRequestId, {success: true, sources: [{url: handle, name: "<b>Subtitle</b>.srt"}]})
        const picker = findChild(subtitleDialog, "addonSubtitleSelection")
        tryVerify(() => picker.usingReplacement)
        return picker
    }
    function subtitleResult(picker, assets) { return {choiceSet: picker.snapshotData.choiceSet, assignments: [{targetRef: "target:0", assetRefs: assets}]} }
    function test_refreshKeepsTypedDialogChoicesWithoutAllowingSave_data() {
        return [{tag: "playlist", subtitles: false}, {tag: "subtitles", subtitles: true}]
    }
    function test_refreshKeepsTypedDialogChoicesWithoutAllowingSave(data) {
        const picker = data.subtitles ? subtitles() : playlist()
        const dialog = data.subtitles ? subtitleDialog : playlistDialog
        const save = data.subtitles ? subtitleSave : playlistSave
        const mount = findChild(picker, "addonSelectionMount"), session = mount.sessionId, snapshot = picker.snapshotData.choiceSet
        registry.result(picker, data.subtitles ? subtitleResult(picker, ["asset:0"]) : {choiceSet: snapshot, selected: ["target:1"]})
        const button = findChild(dialog, data.subtitles ? "trackAssignmentSave" : "addToPlaylistSave")
        verify(button !== null); verify(button.enabled)
        registry.setAvailability(false, true)
        tryCompare(picker, "prepared", false)
        verify(picker.usingReplacement); verify(picker.visible); verify(!button.enabled)
        dialog.submit(); compare(save.count, 0); verify(!dialog.addonFallback)
        compare(mount.sessionId, session); compare(picker.snapshotData.choiceSet, snapshot)
        registry.setAvailability(true, false)
        tryCompare(picker, "prepared", true); verify(button.enabled)
        compare(mount.sessionId, session); compare(picker.snapshotData.choiceSet, snapshot)
        verify(!registry.closed.includes(session)); dialog.submit(); compare(save.count, 1)
    }
    function test_playlistUsesThePrivateIdAfterCoreSave() {
        const picker = playlist()
        const data = JSON.stringify(findChild(picker, "addonSelectionMount").parameters)
        verify(!data.includes("playlist-17")); verify(!data.includes("playlist-23"))
        registry.result(picker, {choiceSet: picker.snapshotData.choiceSet, selected: ["target:1"]})
        verify(findChild(playlistDialog, "addToPlaylistSave").enabled)
        playlistDialog.submit(); compare(playlistSave.count, 1)
        compare(playlistSave.signalArguments[0][0], "playlist-23"); compare(playlistSave.signalArguments[0][1], "")
        playlistDialog.submit(); compare(playlistSave.count, 1)
    }
    function test_playlistRejectsUnknownReadOnlyAndWrongSchemaResults() {
        const picker = playlist()
        for (const ref of ["playlist-23", "target:2", "target:99"]) {
            registry.result(picker, {choiceSet: picker.snapshotData.choiceSet, selected: [ref]})
            playlistDialog.submit(); compare(playlistSave.count, 0)
        }
        registry.result(picker, {choiceSet: picker.snapshotData.choiceSet, selected: ["target:0"]}); registry.schemaAllowed = false
        playlistDialog.submit(); compare(playlistSave.count, 0)
    }
    function test_playlistRevisionRejectsTheOldChoiceSet() {
        const picker = playlist(), old = picker.snapshotData.choiceSet
        registry.result(picker, {choiceSet: old, selected: ["target:1"]})
        playlists.setProperty(1, "id", "playlist-24")
        playlistDialog.submit(); compare(playlistSave.count, 0)
        tryVerify(() => picker.usingReplacement && picker.snapshotData.choiceSet !== old)
        registry.result(picker, {choiceSet: old, selected: ["target:1"]}); playlistDialog.submit(); compare(playlistSave.count, 0)
        registry.result(picker, {choiceSet: picker.snapshotData.choiceSet, selected: ["target:1"]})
        playlistDialog.submit(); compare(playlistSave.signalArguments[0][0], "playlist-24")
    }
    function test_newPlaylistNeedsTheCoreAddAction() {
        const picker = playlist()
        registry.result(picker, {choiceSet: picker.snapshotData.choiceSet, selected: [], newName: " New playlist "})
        compare(playlistSave.count, 0); playlistDialog.submit()
        compare(playlistSave.count, 1); compare(playlistSave.signalArguments[0][0], ""); compare(playlistSave.signalArguments[0][1], "New playlist")
    }
    function test_asyncPlaylistSnapshotIgnoresAStaleLoad() {
        playlists.deferred = true
        registry.selectReplacement("dialog.add-to-playlist.content@1", "org.example.ui/playlist")
        playlistDialog.begin(1); tryVerify(() => playlists.callbacks.length > 0)
        const old = playlists.callbacks.slice()
        playlists.callbacks = []; playlists.setProperty(1, "id", "playlist-24")
        tryVerify(() => playlists.callbacks.length > 0)
        for (const callback of old) callback([{id: "stale-1", name: "Old"}, {id: "stale-2", name: "Old"}, {id: "stale-3", name: "Old"}])
        verify(!playlistDialog.addonChoicesReady)
        const current = playlists.callbacks[playlists.callbacks.length - 1]
        current(Array.from({length: playlists.count}, (_, i) => playlists.get(i)))
        const picker = findChild(playlistDialog, "addonPlaylistSelection"); tryVerify(() => picker.usingReplacement)
        registry.result(picker, {choiceSet: picker.snapshotData.choiceSet, selected: ["target:1"]})
        playlistDialog.submit(); compare(playlistSave.count, 0); verify(playlistDialog.checkingTarget); verify(picker.busy)
        playlists.callbacks[playlists.callbacks.length - 1]([playlists.get(1)])
        compare(playlistSave.signalArguments[0][0], "playlist-24")
    }
    function test_coreRecoveryKeepsTheBuiltInPlaylistPicker() {
        const list = builtinPlaylist()
        compare(playlists.get(1).id, "playlist-23"); compare(list.itemAtIndex(1).key, "playlist-23")
        mouseClick(list.itemAtIndex(1)); compare(playlistDialog.selectedKey, "playlist-23")
        playlistDialog.submit()
        compare(playlistSave.count, 1); compare(playlistSave.signalArguments[0][0], "playlist-23")
    }
    function test_builtinSelectionRequiresTheCurrentModel_data() {
        return [{tag: "replace"}, {tag: "remove"}, {tag: "readOnly"}, {tag: "move"}, {tag: "reset"}]
    }
    function test_builtinSelectionRequiresTheCurrentModel(data) {
        const list = builtinPlaylist()
        mouseClick(list.itemAtIndex(1)); compare(playlistDialog.selectedKey, "playlist-23")
        if (data.tag === "replace") playlists.setProperty(1, "id", "playlist-24")
        else if (data.tag === "remove") playlists.remove(1)
        else if (data.tag === "readOnly") playlists.setProperty(1, "readOnly", true)
        else if (data.tag === "move") playlists.move(1, 0, 1)
        else { playlists.clear(); playlists.append({id: "playlist-24", name: "Films", thumbnail: "", readOnly: false}) }
        playlistDialog.submit(); compare(playlistSave.count, 0); compare(playlistDialog.selectedKey, "")
        list.forceLayout()
        tryVerify(() => list.itemAtIndex(0) !== null && list.itemAtIndex(0).key === playlists.get(0).id)
        mouseClick(list.itemAtIndex(0)); playlistDialog.submit()
        compare(playlistSave.count, 1); compare(playlistSave.signalArguments[0][0], playlists.get(0).id)
    }
    function test_builtinRejectsAStalePresentedKey() {
        const list = builtinPlaylist(), oldKey = list.itemAtIndex(1).key
        playlists.setProperty(1, "id", "playlist-24")
        playlistDialog.chooseBuiltinTarget(1, oldKey)
        playlistDialog.submit(); compare(playlistSave.count, 0); compare(playlistDialog.selectedKey, "")
    }
    function test_builtinSaveReadsTheTargetAgain_data() {
        return [{tag: "replace", rows: [{id: "playlist-24", name: "Films", readOnly: false}]},
            {tag: "readOnly", rows: [{id: "playlist-23", name: "Films", readOnly: true}]}, {tag: "remove", rows: []}]
    }
    function test_builtinSaveReadsTheTargetAgain(data) {
        const list = builtinPlaylist()
        mouseClick(list.itemAtIndex(1)); compare(playlistDialog.selectedKey, "playlist-23")
        playlists.returnedRows = data.rows
        playlistDialog.submit(); compare(playlistSave.count, 0); compare(playlistDialog.selectedKey, "")
        verify(playlistDialog.errorMessage.length > 0)
    }
    function test_builtinPendingSaveRejectsALateOldTarget() {
        const list = builtinPlaylist()
        mouseClick(list.itemAtIndex(1)); compare(playlistDialog.selectedKey, "playlist-23")
        playlists.deferred = true; playlistDialog.submit()
        verify(playlistDialog.checkingTarget); compare(playlistSave.count, 0)
        const callback = playlists.callbacks[playlists.callbacks.length - 1]
        playlists.setProperty(1, "id", "playlist-24")
        callback([{id: "playlist-23", name: "Films", readOnly: false}])
        compare(playlistSave.count, 0); compare(playlistDialog.selectedKey, ""); verify(!playlistDialog.checkingTarget)
        playlists.deferred = false; list.forceLayout()
        tryVerify(() => list.itemAtIndex(1) !== null && list.itemAtIndex(1).key === "playlist-24")
        mouseClick(list.itemAtIndex(1)); playlistDialog.submit()
        compare(playlistSave.count, 1); compare(playlistSave.signalArguments[0][0], "playlist-24")
    }
    function test_closedBuiltinSaveIgnoresALateTargetRead() {
        const list = builtinPlaylist()
        mouseClick(list.itemAtIndex(1)); compare(playlistDialog.selectedKey, "playlist-23")
        playlists.deferred = true; playlistDialog.submit(); verify(playlistDialog.checkingTarget)
        const callback = playlists.callbacks[playlists.callbacks.length - 1]
        playlistDialog.close(); tryVerify(() => !playlistDialog.visible)
        callback([{id: "playlist-23", name: "Films", readOnly: false}])
        compare(playlistSave.count, 0); verify(!playlistDialog.checkingTarget)
    }
    function test_missingPlaylistScopeReturnsToTheCorePicker() {
        registry.scopeAllowed = false
        registry.selectReplacement("dialog.add-to-playlist.content@1", "org.example.ui/playlist")
        playlistDialog.begin(1); tryVerify(() => playlistDialog.addonFallback)
        compare(Object.keys(registry.sessions).length, 0)
        const list = findChild(playlistDialog, "playlistDestinations"); tryVerify(() => list.itemAtIndex(1) !== null)
        list.itemAtIndex(1).clicked(); playlistDialog.submit()
        compare(playlistSave.count, 1); compare(playlistSave.signalArguments[0][0], "playlist-23")
    }
    function test_closedPlaylistIgnoresAnAsyncSnapshot() {
        playlists.deferred = true
        registry.selectReplacement("dialog.add-to-playlist.content@1", "org.example.ui/playlist")
        playlistDialog.begin(1); tryVerify(() => playlists.callbacks.length > 0)
        const callbacks = playlists.callbacks.slice(); playlistDialog.close(); tryVerify(() => !playlistDialog.visible)
        for (const callback of callbacks) callback(Array.from({length: playlists.count}, (_, i) => playlists.get(i)))
        verify(!playlistDialog.addonChoicesReady); compare(playlistSave.count, 0)
    }
    function test_subtitleCoreSaveUsesOnlyTheOfferedOpaqueHandle() {
        const picker = subtitles()
        const data = JSON.stringify(findChild(picker, "addonSelectionMount").parameters)
        verify(!data.includes("file:///")); verify(!data.includes(handle)); verify(!data.includes("Private movie"))
        registry.result(picker, subtitleResult(picker, ["asset:0"]))
        compare(subtitleSave.count, 0); subtitleDialog.submit(); compare(subtitleSave.count, 1)
        const rows = subtitleSave.signalArguments[0][0]
        compare(rows.length, 1); compare(rows[0].url, media); compare(rows[0].tracks[0], handle)
    }
    function test_subtitleSaveStillRequiresProtectedPreparation() {
        const picker = subtitles(); prepareSelection = true
        registry.result(picker, subtitleResult(picker, ["asset:0"]))
        subtitleDialog.submit(); compare(prepared.count, 0); compare(sourceService.rows[0].tracks[0], handle)
        sourceService.preparationFinished(preparation.pendingPreparation, {success: true, rows: [{url: media, tracks: ["file:///cache/subtitle.srt"]}], resolved: {[handle]: "file:///cache/subtitle.srt"}})
        compare(prepared.count, 1); compare(prepared.signalArguments[0][0][0].tracks[0], "file:///cache/subtitle.srt")
    }
    function test_closedSubtitlePreparationIgnoresALateDownload() {
        const picker = subtitles(); prepareSelection = true
        registry.result(picker, subtitleResult(picker, ["asset:0"])); subtitleDialog.submit()
        const pending = preparation.pendingPreparation
        subtitleDialog.cancelableSave = true; findChild(subtitleDialog, "trackAssignmentCancel").clicked()
        tryVerify(() => !subtitleDialog.visible)
        sourceService.preparationFinished(pending, {success: true, rows: [{url: media, tracks: ["file:///cache/late.srt"]}]})
        compare(prepared.count, 0)
    }
    function test_subtitleUnknownAndOldSearchRefsCannotSave() {
        const picker = subtitles(), old = subtitleResult(picker, ["asset:0"])
        registry.result(picker, subtitleResult(picker, ["https://example.org/file.srt"]))
        subtitleDialog.submit(); compare(subtitleSave.count, 0)
        subtitleDialog.requestSearch(); subtitleDialog.loadSearch(subtitleDialog.searchRequestId, {success: true, sources: [{url: "addon-subtitle:next", name: "Next.srt"}]})
        tryVerify(() => picker.usingReplacement && picker.snapshotData.choiceSet !== old.choiceSet)
        registry.result(picker, old); subtitleDialog.submit(); compare(subtitleSave.count, 0)
    }
    function test_explicitSubtitleClearKeepsUnrelatedLocalTracks() {
        const local = "file:///private/local.srt", picker = subtitles([local, handle])
        registry.result(picker, subtitleResult(picker, [])); subtitleDialog.submit()
        compare(subtitleSave.count, 1); compare(subtitleSave.signalArguments[0][0][0].tracks.length, 1)
        compare(subtitleSave.signalArguments[0][0][0].tracks[0], local)
    }
    function test_subtitleChoiceDoesNotSaveAnUnselectedAutomaticGuess() {
        const other = "file:///private/Other.mkv"
        const picker = subtitles([], [{url: other, name: "Other.mkv", baseline: []}])
        subtitleDialog.drafts = Object.assign({}, subtitleDialog.drafts, {[other]: Object.assign({}, subtitleDialog.drafts[other], {tracks: [handle], manual: false})})
        subtitleDialog.submit(); compare(subtitleSave.count, 0)
        tryVerify(() => picker.usingReplacement)
        registry.result(picker, subtitleResult(picker, ["asset:0"]))
        subtitleDialog.submit(); compare(subtitleSave.count, 1)
        const rows = subtitleSave.signalArguments[0][0]
        compare(rows.length, 1); compare(rows[0].url, media)
    }
    function test_subtitleCancelAndRecoveryKeepCoreControls() {
        const picker = subtitles()
        registry.result(picker, subtitleResult(picker, ["asset:0"]))
        findChild(subtitleDialog, "addonSubtitleRecovery").clicked(); tryVerify(() => !picker.usingReplacement)
        verify(findChild(subtitleDialog, "trackAssignmentList").visible)
        subtitleDialog.reject(); compare(subtitleSave.count, 0)
    }
}
