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
        property bool available: true
        property bool loading: false
        property string preparationError: ""
        property int displaySerial: 0
        property var releasedRequests: []
        property var acquireHook: null
        property var sources: [{id: "org.example.radio/source", name: "<b>Radio</b>", enabled: true, configured: true, operations: ["roots", "browse", "search"]}]
        property var calls: []
        property var current: ({})
        property int serial: 0
        signal changed()
        signal availabilityChanged()
        signal viewChanged(string id)
        signal viewClosed(string id)
        signal actionFinished(string id, bool success, string error)
        function acquireDisplay() { const id = "display-" + (++displaySerial); if (acquireHook) acquireHook(); return id }
        function releaseDisplay(id) { releasedRequests = releasedRequests.concat([id]) }
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
        function enqueue(id, refs, target) { calls = calls.concat([{operation: "enqueue", id: id, refs: refs, target: target || "queue"}]); return "transaction" }
        function loadPlaylists(id, query) { calls = calls.concat([{operation: "playlists", id: id, query: query || ""}]); return true }
        function choosePlaylist(id, choice) { calls = calls.concat([{operation: "choose", id: id, choice: choice}]); return true }
        function clearPlaylist(id) { calls = calls.concat([{operation: "release", id: id}]); current = Object.assign({}, current, {playlistTarget: "", canAddToPlaylist: false}); viewChanged(id) }
        function play(id, ref) { calls = calls.concat([{operation: "play", id: id, ref: ref}]); return "play-request" }
        property var queueFill: ({})
        function startQueueFill(id, maximum) {
            calls = calls.concat([{operation: "fill", id: id, maximum: maximum}])
            queueFill = {active: true, paused: false, sourceName: "<b>Radio</b>", added: 0}; return true
        }
        function pauseQueueFill(paused) { queueFill = {active: true, paused: paused, sourceName: "<b>Radio</b>", added: 0} }
        function stopQueueFill() { queueFill = {active: false, error: ""} }
    }
    QtObject {
        id: otherService
        property int revision: 0
        property bool available: true
        property bool loading: false
        property var sources: []
        property int requests: 0
        property var released: []
        signal changed()
        signal availabilityChanged()
        signal viewChanged(string id)
        signal viewClosed(string id)
        signal actionFinished(string id, bool success, string error)
        function acquireDisplay() { return "other-" + (++requests) }
        function releaseDisplay(id) { released = released.concat([id]) }
    }
    Component { id: pageComponent; RedesignAddonDiscovery { width: 750; height: 550; backend: service } }
    Component { id: windowComponent; RedesignAddonDiscoveryWindow { backend: service } }
    Component { id: railComponent; RedesignLibraryNavigation { height: 600; expanded: true; discoveryBackend: service } }
    Component { id: menuComponent; RedesignMenu { discoveryBackend: service } }
    SignalSpy { id: navigationSignal; signalName: "discoveryRequested" }
    SignalSpy { id: menuSignal; signalName: "discoveryRequested" }
    function init() {
        service.calls = []; service.current = ({}); service.queueFill = ({})
        service.available = true; service.loading = false; service.preparationError = ""; service.displaySerial = 0; service.releasedRequests = []; service.acquireHook = null
        otherService.requests = 0; otherService.released = []
        service.sources = [{id: "org.example.radio/source", name: "<b>Radio</b>", enabled: true, configured: true, operations: ["roots", "browse", "search"]}]
        service.revision++; navigationSignal.clear(); menuSignal.clear()
    }
    function page() {
        const before = service.displaySerial
        const view = createTemporaryObject(pageComponent, test)
        tryCompare(service, "displaySerial", before + 1)
        return view
    }
    function test_visiblePageOwnsOnlyOneDisplayRequest() {
        const view = page(); compare(service.displaySerial, 1); compare(service.calls.length, 0)
        compare(view.displayRequest, "display-1")
        view.visible = false; compare(service.releasedRequests.length, 1); compare(service.releasedRequests[0], "display-1")
        view.visible = true; tryCompare(service, "displaySerial", 2)
        view.destroy(); wait(0); compare(service.releasedRequests.length, 2); compare(service.releasedRequests[1], "display-2")
    }
    function test_closedWindowDoesNotPrepareSourcePresence() {
        const shell = createTemporaryObject(windowComponent, test)
        compare(service.displaySerial, 0); compare(service.calls.length, 0)
        shell.open(); tryCompare(shell, "visible", true); tryCompare(service, "displaySerial", 1)
        shell.close(); tryCompare(shell, "visible", false); compare(service.releasedRequests.length, 1)
    }
    function test_loadingPreservesRowsSelectionAndSourceWithoutSetupError() {
        const view = page(); view.openSource("org.example.radio/source"); view.select("item-1")
        tryVerify(() => findChild(view, "addonDiscoveryItem_item-1") !== null)
        const id = view.viewId
        service.available = false; service.loading = true; service.availabilityChanged()
        compare(view.viewId, id); compare(view.selected[0], "item-1"); compare(view.actionError, "")
        verify(findChild(view, "addonDiscoveryItem_item-1") !== null)
        verify(!findChild(view, "addonDiscoveryItem_item-1").enabled); verify(!findChild(view, "addonDiscoveryOpen").enabled)
        verify(findChild(view, "addonDiscoveryLoading").visible); compare(service.calls.length, 1)
        service.available = true; service.loading = false; service.availabilityChanged()
        compare(view.viewId, id); compare(view.selected[0], "item-1"); compare(service.calls.length, 1)
        verify(findChild(view, "addonDiscoveryItem_item-1").enabled)
    }
    function test_unknownConfigurationDefersExplicitOpenWithoutSetupError() {
        service.sources = [Object.assign({}, service.sources[0], {configurationKnown: false, configured: false})]
        const view = page(); view.openSource("org.example.radio/source")
        verify(view.pendingOpen); compare(view.viewId, ""); compare(service.calls.length, 0); compare(view.actionError, "")
        verify(findChild(view, "addonDiscoveryLoading").visible)
        service.sources = [Object.assign({}, service.sources[0], {configurationKnown: true, configured: true})]; ++service.revision; service.changed()
        tryVerify(() => view.viewId.length > 0); compare(service.calls.length, 1); compare(service.calls[0].operation, "open")
        verify(!view.pendingOpen); compare(view.actionError, "")
    }
    function test_backendReplacementReleasesThroughExactOriginalOwner() {
        const view = page(); view.openSource("org.example.radio/source")
        view.backend = otherService
        compare(service.releasedRequests.length, 1); compare(service.releasedRequests[0], "display-1")
        compare(service.calls[service.calls.length - 1].operation, "close")
        tryCompare(otherService, "requests", 1); compare(view.viewId, "")
        view.visible = false; compare(otherService.released.length, 1); compare(otherService.released[0], "other-1")
    }
    function test_acquireReentryReleasesReturnedObsoleteRequest() {
        const view = page(); view.visible = false
        service.acquireHook = () => { view.visible = false }
        view.visible = true
        tryCompare(service, "displaySerial", 2); compare(view.displayRequest, "")
        compare(service.releasedRequests.length, 2); compare(service.releasedRequests[1], "display-2")
        service.acquireHook = null
    }
    function test_preparationRefusalIsVisibleWithoutAnAddonFailure() {
        service.available = false; service.preparationError = "Close another add-on view, then open this page."
        const view = page(); view.openSource("org.example.radio/source")
        compare(view.actionError, ""); compare(service.calls.length, 0)
        const status = findChild(view, "addonDiscoveryLoading")
        verify(status.visible); compare(status.text, service.preparationError)
    }
    function test_queueFillIsAnExplicitChoiceWithPauseAndStop() {
        const view = page()
        findChild(view, "addonDiscoverySources").activated(1)
        tryVerify(() => findChild(view, "addonDiscoveryItem_item-1") !== null)
        const start = findChild(view, "addonDiscoveryFill")
        verify(!start.visible)
        service.current = Object.assign({}, service.current, {canFillQueue: true}); service.viewChanged(service.current.id)
        tryVerify(() => start.visible)
        compare(service.calls.filter(call => call.operation === "fill").length, 0)
        mouseClick(start)
        compare(service.calls[service.calls.length - 1].operation, "fill"); compare(service.calls[service.calls.length - 1].maximum, 5)
        tryVerify(() => !start.visible)
        const status = findChild(view, "addonDiscoveryFillStatus")
        compare(status.text, "<b>Radio</b> keeps the queue filled: 0 added."); compare(status.textFormat, Text.PlainText)
        const pause = findChild(view, "addonDiscoveryFillPause")
        // The row has just appeared: wait for its layout before a click.
        tryVerify(() => pause.visible && findChild(view, "addonDiscoveryFillStop").x > pause.x)
        mouseClick(pause)
        compare(service.queueFill.paused, true)
        compare(status.text, "Paused: <b>Radio</b> adds no items to the queue.")
        compare(findChild(view, "addonDiscoveryFillPause").caption, "Resume")
        mouseClick(findChild(view, "addonDiscoveryFillStop"))
        tryVerify(() => start.visible)
    }
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
        verify(!findChild(view, "addonDiscoveryPlaylist").visible)
        compare(service.calls.length, 1)
    }
    function test_playableSourceHasNoSeparateAccessStep() {
        service.sources = [Object.assign({}, service.sources[0], {operations: ["roots", "browse", "resolve"]})]
        ++service.revision
        const window = createTemporaryObject(windowComponent, test)
        window.openSource("org.example.radio/source"); tryCompare(window, "opened", true)
        verify(findChild(window, "addonDiscoveryPlaybackReview") === null)
        const browser = findChild(window, "addonDiscoveryWindowBrowser")
        service.current = Object.assign({}, service.current, {canPlay: true, canEnqueue: true}); service.viewChanged(browser.viewId)
        browser.select("item-1")
        const play = findChild(window, "addonDiscoveryPlay"); verify(play.enabled); play.clicked()
        compare(service.calls[1].operation, "play"); compare(service.calls[1].ref, "item-1")
        window.close(); tryCompare(window, "opened", false)
    }
    function test_playlistNeedsAnOpaquePreparedTargetAndExplicitConfirmation() {
        const view = page(); view.openSource("org.example.radio/source"); view.select("item-1")
        service.current = Object.assign({}, service.current, {playlistAvailable: true, playlists: [{ref: "choice:one", name: "<b>Saved</b>"}], canAddToPlaylist: false})
        service.viewChanged(view.viewId)
        findChild(view, "addonDiscoveryPlaylist").clicked()
        const choices = findChild(view, "addonDiscoveryPlaylistChoices")
        verify(choices.visible); choices.activated(1)
        compare(service.calls[1].operation, "playlists"); compare(service.calls[2].operation, "choose"); compare(service.calls[2].choice, "choice:one")
        compare(choices.contentItem.textFormat, Text.PlainText); compare(choices.contentItem.text, "<b>Saved</b>")
        verify(!findChild(view, "addonDiscoveryPlaylistConfirm").enabled)
        service.current = Object.assign({}, service.current, {playlistTarget: "library:opaque", canAddToPlaylist: true})
        service.viewChanged(view.viewId)
        findChild(view, "addonDiscoveryPlaylistConfirm").clicked()
        compare(service.calls[3].operation, "enqueue"); compare(service.calls[3].target, "library:opaque")
        compare(service.calls[3].refs[0], "item-1"); verify(!view.playlistPickerOpen)
    }
    function test_playlistCancelAndItemReselectionReleaseTheTarget() {
        const view = page(); view.openSource("org.example.radio/source"); view.select("item-1")
        service.current = Object.assign({}, service.current, {playlistAvailable: true, playlists: [], playlistTarget: "library:old", canAddToPlaylist: true})
        service.viewChanged(view.viewId); findChild(view, "addonDiscoveryPlaylist").clicked()
        findChild(view, "addonDiscoveryPlaylistCancel").clicked()
        compare(service.calls[2].operation, "release"); verify(!view.playlistPickerOpen)
        findChild(view, "addonDiscoveryPlaylist").clicked(); view.select("item-1")
        compare(service.calls[4].operation, "release"); verify(!view.playlistPickerOpen)
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
