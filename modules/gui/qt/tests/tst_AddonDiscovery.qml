// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../player/qml/redesign"

TestCase {
    id: test
    name: "AddonDiscovery"
    width: 850; height: 650; visible: true; when: windowShown
    QtObject {
        id: service
        property int revision: 0
        property var sources: [{id: "org.example.radio/source", name: "<b>Radio</b>", enabled: true, configured: true, operations: ["roots", "browse", "search"]}]
        property var calls: []
        property var current: ({})
        property int serial: 0
        signal changed()
        signal viewChanged(string id)
        signal viewClosed(string id)
        signal actionFinished(string id, bool success, string error)
        function openSource(source) {
            calls = calls.concat([{operation: "open", source: source}])
            const id = "view-" + (++serial)
            current = {id: id, source: source, items: [{ref: "node-1", title: "Folder", kind: "node"}, {ref: "item-1", title: "<b>Station</b>", kind: "media"}],
                busy: false, canGoBack: false, canPlay: false, canEnqueue: false, hasMore: false, error: ""}
            return id
        }
        function view(id) { return current.id === id ? current : ({}) }
        function closeView(id) { calls = calls.concat([{operation: "close", id: id}]); current = ({}); viewClosed(id) }
        function browse(id, ref) { calls = calls.concat([{operation: "browse", id: id, ref: ref}]); return true }
        function back(id) { calls = calls.concat([{operation: "back", id: id}]); return true }
        function loadMore(id) { calls = calls.concat([{operation: "more", id: id}]); return true }
        function search(id, query) { calls = calls.concat([{operation: "search", id: id, query: query}]); return true }
        function enqueue(id, refs) { calls = calls.concat([{operation: "enqueue", id: id, refs: refs}]); return "transaction" }
        function play(id, ref) { calls = calls.concat([{operation: "play", id: id, ref: ref}]); return "play-request" }
    }
    Component { id: pageComponent; RedesignAddonDiscovery { width: 750; height: 550; backend: service } }
    Component { id: windowComponent; RedesignAddonDiscoveryWindow { backend: service } }
    Component { id: railComponent; RedesignLibraryNavigation { height: 600; expanded: true; discoveryBackend: service } }
    Component { id: menuComponent; RedesignMenu { discoveryBackend: service } }
    SignalSpy { id: navigationSignal; signalName: "discoveryRequested" }
    SignalSpy { id: menuSignal; signalName: "discoveryRequested" }
    function init() {
        service.calls = []; service.current = ({})
        service.sources = [{id: "org.example.radio/source", name: "<b>Radio</b>", enabled: true, configured: true, operations: ["roots", "browse", "search"]}]
        service.revision++; navigationSignal.clear(); menuSignal.clear()
    }
    function page() { return createTemporaryObject(pageComponent, test) }
    function test_openSourceDoesNotPlayOrEnqueue() {
        const view = page()
        const choices = findChild(view, "addonDiscoverySources")
        choices.activated(1)
        compare(view.sourceId, "org.example.radio/source")
        compare(choices.contentItem.textFormat, Text.PlainText)
        compare(choices.contentItem.text, "<b>Radio</b>")
        compare(service.calls.length, 1); compare(service.calls[0].operation, "open")
        tryVerify(() => findChild(view, "addonDiscoveryItem_item-1") !== null)
        const row = findChild(view, "addonDiscoveryItem_item-1")
        compare(row.caption, "<b>Station</b>"); compare(row.captionTextFormat, Text.PlainText)
    }
    function test_playbackStaysDisabledWithoutAdapter() {
        const view = page(); view.openSource("org.example.radio/source")
        view.select("item-1")
        verify(!findChild(view, "addonDiscoveryPlay").enabled)
        verify(!findChild(view, "addonDiscoveryEnqueue").enabled)
        compare(service.calls.length, 1)
    }
    function test_nodeAndSearchUseOnlyOpaqueReferences() {
        const view = page(); view.openSource("org.example.radio/source")
        tryVerify(() => findChild(view, "addonDiscoveryItem_node-1") !== null)
        findChild(view, "addonDiscoveryItem_node-1").clicked()
        compare(service.calls[1].operation, "browse"); compare(service.calls[1].ref, "node-1")
        const query = findChild(view, "addonDiscoveryQuery")
        query.text = "Radio title"; findChild(view, "addonDiscoverySearch").clicked()
        compare(service.calls[2].operation, "search"); compare(service.calls[2].query, "Radio title")
        query.text = "a".repeat(600); compare(query.text.length, 512)
    }
    function test_queueAndPlayNeedExplicitActions() {
        const view = page(); view.openSource("org.example.radio/source")
        service.current = Object.assign({}, service.current, {canPlay: true, canEnqueue: true}); service.viewChanged(view.viewId)
        view.select("item-1")
        findChild(view, "addonDiscoveryEnqueue").clicked()
        compare(service.calls[1].operation, "enqueue"); compare(service.calls[1].refs[0], "item-1")
        findChild(view, "addonDiscoveryPlay").clicked()
        compare(service.calls[2].operation, "play"); compare(service.calls[2].ref, "item-1")
    }
    function test_hiddenViewClosesItsSession() {
        const view = page(); view.openSource("org.example.radio/source")
        const old = view.viewId; view.visible = false
        compare(view.viewId, ""); compare(service.calls[1].operation, "close")
        service.viewChanged(old); service.actionFinished(old, false, "Late result")
        compare(view.viewId, "")
    }
    function test_coreWindowClosesItsSourceOnEscape() {
        const shell = createTemporaryObject(windowComponent, test)
        verify(shell !== null)
        shell.openSource("org.example.radio/source")
        tryVerify(() => shell.opened)
        const browser = findChild(shell, "addonDiscoveryWindowBrowser")
        verify(browser !== null); verify(browser.viewId.length > 0)
        keyClick(Qt.Key_Escape)
        tryVerify(() => !shell.visible)
        compare(browser.viewId, "")
        compare(service.calls[service.calls.length - 1].operation, "close")
    }
    function test_libraryAndMenuOpenSourcesWithoutQueueChanges() {
        const rail = createTemporaryObject(railComponent, test)
        const menu = createTemporaryObject(menuComponent, test)
        navigationSignal.target = rail; menuSignal.target = menu
        const source = findChild(rail, "addonDiscoveryNavigation_org.example.radio/source")
        verify(source !== null); compare(source.caption, "<b>Radio</b>")
        compare(source.captionTextFormat, Text.PlainText); compare(source.tooltipTextFormat, Text.PlainText)
        source.clicked(); compare(navigationSignal.count, 1)
        compare(navigationSignal.signalArguments[0][0], "org.example.radio/source")
        const action = findChild(menu, "addonDiscoveryMenuAction")
        verify(action !== null); tryCompare(menu, "discoveryAvailable", true)
        wait(0)
        const withSource = menu.count
        action.triggered(); compare(menuSignal.count, 1)
        compare(service.calls.length, 0)
        service.sources = []; ++service.revision
        tryCompare(menu, "discoveryAvailable", false)
        tryCompare(menu, "count", withSource - 1)
        tryVerify(() => findChild(rail, "addonDiscoveryNavigation_org.example.radio/source") === null)
    }
}
