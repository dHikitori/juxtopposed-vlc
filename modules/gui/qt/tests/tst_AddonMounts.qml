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
        property bool available: true
        property bool loading: false
        property bool pendingDocument: false
        property bool unavailableDocument: false
        // A panel view with its own header row (Episodes, say).
        property bool headerDocument: false
        property var routes: [{id: "org.example.ui/browser", name: "<b>Browser</b>", route: "addon/org.example.ui/browser"}]
        property var commands: []
        signal changed()
        signal availabilityChanged()
        signal documentsChanged()
        signal sessionChanged(string id)
        signal sessionFailed(string id)
        signal routeRequested(string route, var parameters)
        signal modalSessionRequested(string ticket, string contribution, var parameters)
        signal modalFinished(string ticket, bool accepted, var result)
        function descriptor(id) {
            if (pendingDocument) return {id: id, pending: true, state: {label: "First"}, registeredCommands: ["org.example.ui/update"], messages: {}, assetUrls: {}}
            if (unavailableDocument) return {id: id, unavailable: true, state: {}, registeredCommands: [], messages: {}, assetUrls: {}}
            if (headerDocument && !badDocument) return {id: id, document: {contract: "ui@1", id: "test", root: {type: "Page", children: [
                    {type: "Row", id: "controls", children: [{type: "Button", id: "heading", grow: true, text: "Show", subtitle: "Season 1", action: {command: "org.example.ui/update"}}]},
                    {type: "Column", id: "rows", children: Array.from({length: 30}, (_, n) => ({type: "MediaRow", id: "row" + n, title: "Row " + n,
                        number: String(n), selected: n === 20, primaryAction: {command: "org.example.ui/update"}}))}]}},
                state: {label: "First"}, registeredCommands: ["org.example.ui/update"], messages: {}, assetUrls: {}}
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
        property var attachedPanels: []
        function attachPanelSession(id) { attachedPanels = attachedPanels.concat([id]); return true }
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
        function materialize(id) {
            const copy = JSON.parse(JSON.stringify(sessions)), previous = copy[id]
            const fresh = descriptor(previous.id)
            fresh.state = previous.state
            copy[id] = fresh; sessions = copy; sessionChanged(id)
        }
        function setAvailability(current, pending) { available = current; loading = pending; availabilityChanged() }
        function patch(id, values) {
            const copy = JSON.parse(JSON.stringify(sessions)); copy[id].state = Object.assign({}, copy[id].state, values)
            sessions = copy; sessionChanged(id)
        }
    }
    Component {
        id: panelComponent
        RedesignAddonPanel { width: 380; height: 500; registry: fakeRegistry; panelId: "org.example.ui/panel"; entry: ({name: "<b>Episodes</b>"}) }
    }
    SignalSpy { id: panelCloseSpy; signalName: "closeRequested" }
    function test_panelFrameAttachesItsSessionAndRecovers() {
        fakeRegistry.badDocument = true
        const panel = createTemporaryObject(panelComponent, test)
        verify(panel)
        const title = findChild(panel, "addonPanelTitle")
        compare(title.textFormat, Text.PlainText); compare(title.text, "<b>Episodes</b>")
        tryVerify(() => fakeRegistry.attachedPanels.length > 0)
        // A view that fails shows the app's recovery, not an empty frame.
        tryVerify(() => panel.failed)
        verify(findChild(panel, "addonPanelRecovery").visible)
        fakeRegistry.badDocument = false
        fakeRegistry.failures = []
        const attached = fakeRegistry.attachedPanels.length
        mouseClick(findChild(panel, "addonPanelReload"))
        tryVerify(() => !panel.failed && fakeRegistry.attachedPanels.length > attached)
        tryVerify(() => findChild(panel, "addonPanelMount").usable)
        panelCloseSpy.target = panel
        panel.forceActiveFocus(); keyClick(Qt.Key_Escape)
        compare(panelCloseSpy.count, 1)
        mouseClick(findChild(panel, "addonPanelClose"))
        compare(panelCloseSpy.count, 2)
    }
    Component {
        id: headerPanelComponent
        RedesignAddonPanel { width: 380; height: 500; registry: fakeRegistry; panelId: "org.example.ui/panel"; entry: ({name: "Episodes", header: "controls"}) }
    }
    function test_panelHeaderRowHoldsTheCloseButtonAndStaysInView() {
        fakeRegistry.headerDocument = true
        const panel = createTemporaryObject(headerPanelComponent, test)
        tryVerify(() => findChild(panel, "addonPanelMount").usable)
        // The view's header row is the title: the frame adds only its close
        // button there, above the scrolled area.
        verify(!findChild(panel, "addonPanelTitle").visible)
        const host = findChild(panel, "addonPanelHeaderHost")
        tryVerify(() => !!findChild(host, "addonPanelHeader") && !!findChild(host, "addonPanelClose"))
        compare(findChild(host, "addonActionSubtitle").text, "Season 1")
        panelCloseSpy.target = panel; panelCloseSpy.clear()
        const close = findChild(host, "addonPanelClose")
        tryVerify(() => host.height > close.height)
        mouseClick(close)
        compare(panelCloseSpy.count, 1)
        // The highlighted row is shown when the panel opens.
        const scroller = findChild(panel, "addonPanelMount").parent.parent
        tryVerify(() => scroller.contentY > 0)
        const row = findChild(panel, "addonMediaRow_row20")
        const top = row.mapToItem(scroller, 0, 0).y
        verify(top >= -1 && top < scroller.height, "row 20 at " + top)
        // A failed view has no header row: the frame's own title row returns.
        panel.failed = true
        verify(findChild(panel, "addonPanelTitle").visible)
        verify(!host.visible)
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
        fakeRegistry.commands = []; fakeRegistry.available = true; fakeRegistry.loading = false
        fakeRegistry.pendingDocument = false; fakeRegistry.unavailableDocument = false; fakeRegistry.headerDocument = false
        visualizers.selectedVisualizer = ""; visualizers.selections = 0
        fakeRegistry.revision++; fakeRegistry.changed()
    }
    function test_pendingDocumentMaterializesTheSameSession() {
        fakeRegistry.pendingDocument = true
        const mount = createTemporaryObject(mountComponent, test)
        tryVerify(() => mount.sessionId.length > 0 && mount.pending)
        const id = mount.sessionId
        verify(!mount.usable); verify(findChild(mount, "addonMountLoading").visible)
        compare(fakeRegistry.failures.length, 0)
        fakeRegistry.pendingDocument = false; fakeRegistry.materialize(id)
        tryVerify(() => mount.usable)
        compare(mount.sessionId, id); verify(!fakeRegistry.closedSessions.includes(id)); compare(fakeRegistry.failures.length, 0)
    }
    function test_unrelatedChangesKeepResolvedSessionState() {
        const mount = createTemporaryObject(mountComponent, test)
        tryVerify(() => mount.usable)
        const id = mount.sessionId
        fakeRegistry.patch(id, {label: "Kept"})
        fakeRegistry.documentsChanged(); fakeRegistry.revision++; fakeRegistry.changed()
        wait(20)
        compare(mount.sessionId, id); compare(mount.descriptor.state.label, "Kept")
        verify(!fakeRegistry.closedSessions.includes(id))
    }
    function test_temporaryFreshnessPreservesStaticStateAndBlocksActions() {
        const mount = createTemporaryObject(mountComponent, test)
        tryVerify(() => mount.usable)
        const id = mount.sessionId
        fakeRegistry.patch(id, {label: "Private state"}); fakeRegistry.setAvailability(false, true)
        tryCompare(mount, "catalogueAvailable", false)
        verify(mount.usable); compare(mount.descriptor.state.label, "Private state")
        findChild(mount, "addonUiRenderer").commandRequested("org.example.ui/update", {})
        compare(fakeRegistry.dispatches.length, 0); compare(fakeRegistry.failures.length, 0)
        fakeRegistry.setAvailability(true, false); tryCompare(mount, "actionable", true)
        compare(mount.sessionId, id); compare(mount.descriptor.state.label, "Private state")
        verify(!fakeRegistry.closedSessions.includes(id))
    }
    function test_unavailableDocumentIsNotReportedAsAddonFailure() {
        fakeRegistry.pendingDocument = true
        const mount = createTemporaryObject(mountComponent, test)
        tryVerify(() => mount.sessionId.length > 0)
        const id = mount.sessionId
        fakeRegistry.pendingDocument = false; fakeRegistry.unavailableDocument = true; fakeRegistry.materialize(id)
        tryCompare(mount, "sessionId", "")
        verify(fakeRegistry.closedSessions.includes(id)); compare(fakeRegistry.failures.length, 0)
    }
    function test_parameterChangeDuringLoadingKeepsTheNewIntent() {
        const mount = createTemporaryObject(mountComponent, test)
        tryVerify(() => mount.usable)
        const old = mount.sessionId
        fakeRegistry.setAvailability(false, true); mount.parameters = ({query: "new"})
        tryVerify(() => fakeRegistry.closedSessions.includes(old))
        fakeRegistry.setAvailability(true, false)
        tryVerify(() => mount.usable && mount.sessionId !== old)
        compare(mount.descriptor.state.parameters.query, "new")
    }
    function test_stateUpdatesStayInTheirSession() {
        const first = createTemporaryObject(mountComponent, test)
        const second = createTemporaryObject(mountComponent, test)
        tryVerify(() => first.usable && second.usable)
        verify(first.sessionId !== second.sessionId)
        fakeRegistry.patch(first.sessionId, {label: "Changed"})
        tryCompare(findChild(first, "addonActionLabel"), "text", "Changed")
        compare(findChild(second, "addonActionLabel").text, "First")
        let button = findChild(first, "addonActionLabel")
        while (button && typeof button.clicked !== "function") button = button.parent
        button.clicked()
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
    function test_modalConfirmationWaitsForCurrentDocument() {
        const popup = createTemporaryObject(windowComponent, test)
        fakeRegistry.modalSessionRequested("ticket", "org.example.ui/modal", {})
        tryVerify(() => popup.visible && Object.keys(fakeRegistry.sessions).length > 0)
        const id = Object.keys(fakeRegistry.sessions)[0]
        fakeRegistry.patch(id, {result: {code: "ok"}})
        const button = findChild(popup, "addonModalConfirm")
        tryVerify(() => button.enabled)
        fakeRegistry.setAvailability(false, true)
        tryCompare(button, "enabled", false)
        button.clicked(); compare(fakeRegistry.modalResults.length, 0); compare(popup.errorMessage, "")
        verify(popup.visible); verify(!fakeRegistry.closedSessions.includes(id))
        fakeRegistry.setAvailability(true, false)
        tryCompare(button, "enabled", true)
        button.clicked(); compare(fakeRegistry.modalResults.length, 1)
        verify(fakeRegistry.modalResults[0].accepted); compare(fakeRegistry.modalResults[0].result.code, "ok")
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
