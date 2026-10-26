// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: test
    name: "AddonMounts"
    width: 800; height: 650; visible: true; when: windowShown
    QtObject {
        id: fakeRegistry
        property int revision: 0
        property int serial: 0
        property var sessions: ({})
        property var closedSessions: []
        property var dispatches: []
        property var suspended: []
        property var failures: []
        property var modalResults: []
        property var replacements: ({})
        property bool badDocument: false
        property var routes: [{id: "org.example.ui/browser", name: "<b>Browser</b>", route: "addon/org.example.ui/browser"}]
        property var commands: []
        signal changed()
        signal sessionChanged(string id)
        signal sessionFailed(string id)
        signal routeRequested(string route, var parameters)
        signal modalSessionRequested(string ticket, string contribution, var parameters)
        signal modalFinished(string ticket, bool accepted, var result)
        function descriptor(id) {
            return {id: id, document: {contract: "ui@1", id: "test", root: badDocument ? {type: "Loader", source: "package.qml"} :
                {type: "Button", id: "action", text: {bind: "state.label"}, action: {command: "org.example.ui/update"}}},
                state: {label: "First"}, registeredCommands: ["org.example.ui/update"], messages: {}, assetUrls: {}}
        }
        function createSession(id, parameters, context) {
            if (failures.includes(id)) return ""
            const key = "session-" + (++serial)
            const entry = descriptor(id); entry.state.parameters = parameters; entry.state.context = context
            sessions = Object.assign({}, sessions, {[key]: entry}); return key
        }
        function session(id) { return sessions[id] || ({}) }
        function closeSession(id) { closedSessions = closedSessions.concat([id]); const copy = Object.assign({}, sessions); delete copy[id]; sessions = copy }
        function suspendSession(id, value) { suspended = suspended.concat([{id: id, value: value}]) }
        function acknowledgeSession(id) {}
        function dispatchSession(id, command, arguments) { dispatches = dispatches.concat([{id: id, command: command, arguments: arguments}]); return true }
        function triggerCommand(command, arguments) { dispatches = dispatches.concat([{command: command, arguments: arguments}]); return true }
        function reportViewFailure(id) { failures = failures.concat([id]); revision++; changed() }
        function sections(hook) { return hook === "home.sections@1" ? [descriptor("org.example.ui/section")] : [] }
        function view(id) { return failures.includes(id) ? ({}) : descriptor(id) }
        function replacement(hook) { return replacements[hook] && !failures.includes(replacements[hook]) ? descriptor(replacements[hook]) : ({}) }
        function replacementChoices(hook) { return [{id: "org.example.ui/home", name: "<b>Custom</b>"}] }
        function selectReplacement(hook, id) { replacements = Object.assign({}, replacements, {[hook]: id}); revision++; changed(); return true }
        function completeModal(ticket, result) { if (typeof result.code !== "string") return false; modalResults = modalResults.concat([{ticket: ticket, accepted: true, result: result}]); modalFinished(ticket, true, result); return true }
        function cancelModal(ticket) { modalResults = modalResults.concat([{ticket: ticket, accepted: false}]); modalFinished(ticket, false, {}); return true }
        function patch(id, values) {
            const copy = JSON.parse(JSON.stringify(sessions)); copy[id].state = Object.assign({}, copy[id].state, values)
            sessions = copy; sessionChanged(id)
        }
    }
    QtObject {
        id: visualizers
        property var availableVisualizers: [{id: "org.example.visual/bars", name: "<b>Bars</b>"}]
        property string selectedVisualizer: ""
        property string errorMessage: ""
        property int selections: 0
        function selectVisualizer(id) { selections++; selectedVisualizer = id; return true }
    }
    Component { id: mountComponent; RedesignAddonMount { width: 500; registry: fakeRegistry; contributionId: "org.example.ui/browser" } }
    Component { id: sectionsComponent; RedesignAddonSections { width: 500; registry: fakeRegistry; hook: "home.sections@1" } }
    Component { id: surfaceComponent; RedesignAddonSurface {
        width: 500; height: 400; registry: fakeRegistry; hook: "home.content@1"
        builtIn: Component { Text { objectName: "builtinBody"; text: "Core Home" } }
    } }
    Component { id: windowComponent; RedesignAddonWindow { registry: fakeRegistry } }
    Component { id: pickerComponent; RedesignAddonVisualizerSelector { width: 400; controller: visualizers } }
    Component { id: menuComponent; RedesignMenu { addonRegistry: fakeRegistry } }
    function init() {
        fakeRegistry.sessions = ({}); fakeRegistry.closedSessions = []; fakeRegistry.dispatches = []; fakeRegistry.suspended = []
        fakeRegistry.failures = []; fakeRegistry.modalResults = []; fakeRegistry.replacements = ({}); fakeRegistry.badDocument = false
        fakeRegistry.commands = []
        visualizers.selectedVisualizer = ""; visualizers.selections = 0
        fakeRegistry.revision++; fakeRegistry.changed()
    }
    function test_stateUpdatesStayInTheirSession() {
        const first = createTemporaryObject(mountComponent, test)
        const second = createTemporaryObject(mountComponent, test)
        tryVerify(() => first.usable && second.usable)
        verify(first.sessionId !== second.sessionId)
        fakeRegistry.patch(first.sessionId, {label: "Changed"})
        tryCompare(findChild(first, "addonActionLabel"), "text", "Changed")
        compare(findChild(second, "addonActionLabel").text, "First")
        findChild(first, "addonActionLabel").parent.parent.clicked()
        compare(fakeRegistry.dispatches.length, 1)
        compare(fakeRegistry.dispatches[0].id, first.sessionId)
    }
    function test_hiddenAndClosedViewsCancelTheirWork() {
        const view = createTemporaryObject(mountComponent, test)
        tryVerify(() => view.usable)
        const id = view.sessionId
        view.visible = false
        verify(fakeRegistry.suspended.some(entry => entry.id === id && entry.value))
        view.visible = true
        verify(fakeRegistry.suspended.some(entry => entry.id === id && !entry.value))
        view.release(); verify(fakeRegistry.closedSessions.includes(id))
    }
    function test_homeReplacementNeedsSelectionAndHasRecovery() {
        const surface = createTemporaryObject(surfaceComponent, test)
        verify(findChild(surface, "builtinBody") !== null)
        verify(!surface.replaced)
        fakeRegistry.selectReplacement("home.content@1", "org.example.ui/home")
        tryVerify(() => surface.replaced)
        verify(findChild(surface, "addonSurfaceRecovery").visible)
        findChild(surface, "addonSurfaceRecovery").clicked()
        tryVerify(() => !surface.replaced)
        verify(findChild(surface, "builtinBody") !== null)
    }
    function test_badViewReturnsToCore() {
        fakeRegistry.badDocument = true; fakeRegistry.selectReplacement("home.content@1", "org.example.ui/home")
        const surface = createTemporaryObject(surfaceComponent, test)
        tryVerify(() => fakeRegistry.failures.includes("org.example.ui/home"))
        verify(!surface.replaced)
        verify(findChild(surface, "builtinBody") !== null)
    }
    function test_additiveSectionUsesTheHostRenderer() {
        const sections = createTemporaryObject(sectionsComponent, test)
        tryVerify(() => findChild(sections, "addonActionLabel") !== null)
        compare(findChild(sections, "addonActionLabel").textFormat, Text.PlainText)
    }
    function test_routeKeepsCoreBackAndClosesSession() {
        const popup = createTemporaryObject(windowComponent, test)
        fakeRegistry.routeRequested("addon/org.example.ui/browser", {query: "radio"})
        tryVerify(() => popup.visible)
        compare(popup.title, "<b>Browser</b>")
        compare(popup.parameters.query, "radio")
        tryVerify(() => Object.keys(fakeRegistry.sessions).length > 0)
        keyClick(Qt.Key_Escape)
        tryVerify(() => !popup.visible)
        verify(fakeRegistry.closedSessions.length > 0)
    }
    function test_modalCancelDoesNotReturnAResult() {
        const popup = createTemporaryObject(windowComponent, test)
        fakeRegistry.modalSessionRequested("ticket", "org.example.ui/modal", {})
        tryVerify(() => popup.visible)
        keyClick(Qt.Key_Escape)
        tryVerify(() => !popup.visible)
        compare(fakeRegistry.modalResults.length, 1)
        compare(fakeRegistry.modalResults[0].accepted, false)
    }
    function test_visualizerSelectionIsExplicitAndLabelsArePlain() {
        const picker = createTemporaryObject(pickerComponent, test)
        const combo = findChild(picker, "addonVisualizerSelection")
        compare(visualizers.selections, 0); compare(combo.currentIndex, 0)
        combo.activated(1)
        compare(visualizers.selectedVisualizer, "org.example.visual/bars")
        compare(combo.contentItem.textFormat, Text.PlainText)
        compare(combo.contentItem.text, "<b>Bars</b>")
        combo.activated(0); compare(visualizers.selectedVisualizer, "")
    }
    function test_playerActionsMenuNeedsRegisteredCommands() {
        const menu = createTemporaryObject(menuComponent, test)
        const count = menu.count
        verify(findChild(menu, "addonPlayerAction_org.example.ui/open") === null)
        fakeRegistry.commands = [{id: "org.example.ui/open", name: "<b>Open</b>", document: {hook: "player.actions@1"}}]
        ++fakeRegistry.revision
        tryCompare(menu, "count", count + 1)
        const action = findChild(menu, "addonPlayerAction_org.example.ui/open")
        verify(action !== null)
        compare(action.text, "Add-on: <b>Open</b>")
        action.triggered()
        compare(fakeRegistry.dispatches[0].command, "org.example.ui/open")
        fakeRegistry.commands = []; ++fakeRegistry.revision
        tryCompare(menu, "count", count)
    }
}
