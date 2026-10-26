// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: test
    name: "AddonSelectionSurface"
    width: 700; height: 600; visible: true; when: windowShown
    QtObject {
        id: fakeRegistry
        property int revision: 0
        property int serial: 0
        property var sessions: ({})
        property var closed: []
        property bool scopeAllowed: true
        property bool schemaAllowed: true
        property bool largeView: false
        property bool mediaTitles: false
        property string selected: "org.example.ui/picker"
        signal changed()
        signal sessionChanged(string id)
        signal sessionFailed(string id)
        function replacement(hook) { return selected.length ? {id: selected, selectionTitlesAllowed: mediaTitles} : ({}) }
        function canUseSelection(id, hook) { return scopeAllowed && selected === id && ["dialog.add-to-playlist.content@1", "subtitles.search.content@1"].includes(hook) }
        function createSession(id, parameters, context) {
            const key = "session-" + (++serial)
            sessions = Object.assign({}, sessions, {[key]: {id: id,
                document: {contract: "ui@1", id: "picker", root: {type: "Text", text: largeView ? "Line\n".repeat(1000) : "Choices"}},
                state: {parameters: JSON.parse(JSON.stringify(parameters))}, registeredCommands: [], messages: {}, assetUrls: {}}})
            return key
        }
        function session(id) { return sessions[id] || ({}) }
        function validateSessionResult(id, result) { return schemaAllowed && !!sessions[id] }
        function closeSession(id) { closed = closed.concat([id]); const copy = Object.assign({}, sessions); delete copy[id]; sessions = copy }
        function suspendSession(id, suspended) {}
        function acknowledgeSession(id) {}
        function dispatchSession(id, command, arguments) { return false }
        function reportViewFailure(id) { selected = ""; ++revision; changed() }
        function selectReplacement(hook, id) { selected = id; ++revision; changed(); return true }
        function result(id, value) {
            const copy = JSON.parse(JSON.stringify(sessions)); copy[id].state.result = value
            sessions = copy; sessionChanged(id)
        }
    }
    QtObject {
        id: noScopeRegistry
        property int revision: 0
        signal changed()
        signal sessionChanged(string id)
        signal sessionFailed(string id)
        function replacement(hook) { return {id: "org.example.ui/picker"} }
        function createSession(id, parameters, context) { test.fail("The missing data scope must block session creation"); return "" }
        function closeSession(id) {}
    }
    QtObject {
        id: noValidatorRegistry
        property int revision: 0
        signal changed()
        signal sessionChanged(string id)
        signal sessionFailed(string id)
        function replacement(hook) { return {id: "org.example.ui/picker"} }
        function canUseSelection(id, hook) { return true }
        function createSession(id, parameters, context) {
            const key = fakeRegistry.createSession(id, parameters, context)
            fakeRegistry.result(key, {choiceSet: parameters.choiceSet, selected: ["target:0"]})
            return key
        }
        function session(id) { return fakeRegistry.session(id) }
        function closeSession(id) { fakeRegistry.closeSession(id) }
        function suspendSession(id, value) {}
        function acknowledgeSession(id) {}
    }
    Component { id: selectionComponent; RedesignAddonSelection {
        width: 600; registry: fakeRegistry; hook: "dialog.add-to-playlist.content@1"; kind: "playlist"; choiceSet: "dialog-1"
        targets: [{key: "playlist-17", title: "Films"}, {key: "playlist-23", title: "Films"}]
    } }
    Component { id: subtitleComponent; RedesignAddonSelection {
        width: 600; registry: fakeRegistry; hook: "subtitles.search.content@1"; kind: "subtitles"; choiceSet: "dialog-2"
        targets: [{key: "file:///selected.mkv", title: "Selected media"}]
        assets: [{key: "addon-subtitle:host-handle", title: "Subtitle"}]
    } }
    SignalSpy { id: confirmed; signalName: "confirmed" }
    SignalSpy { id: canceled; signalName: "canceled" }
    SignalSpy { id: fallback; signalName: "fallbackRequested" }
    function init() {
        fakeRegistry.sessions = ({}); fakeRegistry.closed = []; fakeRegistry.scopeAllowed = true; fakeRegistry.schemaAllowed = true; fakeRegistry.largeView = false
        fakeRegistry.selected = "org.example.ui/picker"; fakeRegistry.mediaTitles = false; ++fakeRegistry.revision
        confirmed.clear(); canceled.clear(); fallback.clear()
    }
    function surface() {
        const result = createTemporaryObject(selectionComponent, test)
        verify(result !== null)
        confirmed.target = result; canceled.target = result; fallback.target = result
        tryVerify(() => result.usingReplacement)
        return result
    }
    function mount(surface) { return findChild(surface, "addonSelectionMount") }
    function draft(surface, target) { return {choiceSet: surface.snapshotData.choiceSet, selected: [target || "target:1"]} }
    function test_confirmUsesPrivateIdentityOnlyOnce() {
        const view = surface()
        fakeRegistry.result(mount(view).sessionId, draft(view))
        compare(findChild(view, "addonSelectionSummary").text, "Selected playlist: Films")
        compare(findChild(view, "addonSelectionSummary").textFormat, Text.PlainText)
        verify(view.confirm()); compare(confirmed.count, 1); compare(confirmed.signalArguments[0][0].key, "playlist-23")
        verify(!view.confirm()); compare(confirmed.count, 1)
    }
    function test_registryRestartCannotRepeatAConfirmedChoiceSet() {
        const view = surface(); const session = mount(view).sessionId
        const value = draft(view); fakeRegistry.result(session, value)
        verify(view.confirm()); ++fakeRegistry.revision; fakeRegistry.changed()
        tryVerify(() => mount(view).sessionId !== session)
        fakeRegistry.result(mount(view).sessionId, value)
        verify(!view.confirm()); compare(confirmed.count, 1)
    }
    function test_changedTargetSnapshotRejectsTheOldDraft() {
        const view = surface(), oldSession = mount(view).sessionId, old = draft(view)
        fakeRegistry.result(oldSession, old)
        view.targets = [{key: "playlist-31", title: "Other"}, {key: "playlist-41", title: "Other"}]
        verify(!view.confirm()); compare(confirmed.count, 0)
        tryVerify(() => mount(view).sessionId !== oldSession)
        fakeRegistry.result(mount(view).sessionId, old)
        verify(!view.confirm()); compare(confirmed.count, 0)
        verify(fakeRegistry.closed.includes(oldSession))
    }
    function test_contextRevisionRejectsOldResults() {
        const view = surface(), previous = draft(view), oldSession = mount(view).sessionId
        fakeRegistry.result(oldSession, previous); view.choiceSet = "account-or-media-revision-2"
        verify(!view.confirm()); tryVerify(() => mount(view).sessionId !== oldSession)
        fakeRegistry.result(mount(view).sessionId, previous); verify(!view.confirm()); compare(confirmed.count, 0)
    }
    function test_declaredResultSchemaAlsoMustPass() {
        const view = surface(); fakeRegistry.result(mount(view).sessionId, draft(view)); fakeRegistry.schemaAllowed = false
        verify(!view.confirm()); compare(confirmed.count, 0)
        verify(view.errorMessage.length > 0)
    }
    function test_cancelReturnsNoSelectionAndClosesSession() {
        const view = surface(), session = mount(view).sessionId
        fakeRegistry.result(session, draft(view)); findChild(view, "addonSelectionCancel").clicked()
        compare(canceled.count, 1); compare(confirmed.count, 0); verify(fakeRegistry.closed.includes(session)); verify(!view.confirm())
    }
    function test_recoveryRemainsOutsideThePackageView() {
        const view = surface()
        findChild(view, "addonSelectionRecovery").clicked()
        compare(fakeRegistry.selected, ""); compare(fallback.count, 1); compare(confirmed.count, 0)
    }
    function test_missingDataScopeFallsBackBeforeCreatingAView() {
        fakeRegistry.scopeAllowed = false
        const view = createTemporaryObject(selectionComponent, test)
        fallback.target = view
        tryVerify(() => fallback.count > 0)
        verify(!view.usingReplacement); compare(Object.keys(fakeRegistry.sessions).length, 0)
        verify(!view.confirm())
    }
    function test_absentScopeMethodFallsBackBeforeCreatingAView() {
        const view = createTemporaryObject(selectionComponent, test, {registry: noScopeRegistry})
        fallback.target = view; tryVerify(() => fallback.count > 0)
        verify(!view.usingReplacement); verify(!view.confirm())
    }
    function test_absentSchemaValidatorCannotConfirm() {
        const view = createTemporaryObject(selectionComponent, test, {registry: noValidatorRegistry})
        confirmed.target = view; fallback.target = view; tryVerify(() => fallback.count > 0)
        verify(!view.usingReplacement); verify(!view.confirm()); compare(confirmed.count, 0)
    }
    function test_hiddenSelectionCannotConfirm() {
        const view = surface(); fakeRegistry.result(mount(view).sessionId, draft(view))
        view.visible = false
        verify(!view.confirm()); compare(confirmed.count, 0)
    }
    function test_largePackageViewKeepsCoreActionsVisible() {
        fakeRegistry.largeView = true
        const view = surface(); view.height = 300
        const body = findChild(view, "addonSelectionBody")
        const recovery = findChild(view, "addonSelectionRecovery")
        const confirm = findChild(view, "addonSelectionConfirm")
        tryVerify(() => body.contentHeight > body.height)
        verify(body.height <= view.maximumContentHeight)
        verify(recovery.mapToItem(view, 0, 0).y >= 0)
        const point = confirm.mapToItem(view, 0, 0)
        verify(point.y >= 0 && point.y + confirm.height <= view.height)
    }
    function test_subtitleSurfaceReturnsOnlyKnownTrackHandles() {
        const view = createTemporaryObject(subtitleComponent, test)
        confirmed.target = view; tryVerify(() => view.usingReplacement)
        const session = mount(view).sessionId
        fakeRegistry.result(session, {choiceSet: view.snapshotData.choiceSet, assignments: [{targetRef: "target:0", assetRefs: ["https://example.org/track.srt"]}]})
        verify(!view.confirm()); compare(confirmed.count, 0)
        fakeRegistry.result(session, {choiceSet: view.snapshotData.choiceSet, assignments: [{targetRef: "target:0", assetRefs: ["asset:0"]}]})
        compare(findChild(view, "addonSelectionSummary").text, "Selected media: 1. Subtitle files: 1.\nSelected media")
        verify(view.confirm()); compare(confirmed.count, 1)
        compare(confirmed.signalArguments[0][0][0].targetKey, "file:///selected.mkv")
        compare(confirmed.signalArguments[0][0][0].assetKeys[0], "addon-subtitle:host-handle")
    }
    function test_mediaTitlesNeedTheirOwnScope() {
        const view = createTemporaryObject(subtitleComponent, test); tryVerify(() => view.usingReplacement)
        compare(view.snapshotData.publicData.targets[0].title, "Media 1")
        verify(!JSON.stringify(mount(view).parameters).includes("Selected media"))
        fakeRegistry.mediaTitles = true; ++fakeRegistry.revision; fakeRegistry.changed()
        tryCompare(view, "targetTitlesAllowed", true)
        tryVerify(() => view.snapshotData.publicData.targets[0].title === "Selected media")
    }
}
